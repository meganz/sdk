#include "mega/wsupload.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef> // offsetof
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// --- SDK headers (kept local to .cpp to keep the public header light)
#include "mega/command.h"
#include "mega/file.h"
#include "mega/filesystem.h"
#include "mega/http.h"
#include "mega/logging.h"
#include "mega/megaapp.h"
#include "mega/megaclient.h"
#include "mega/testhooks.h"
#include "mega/transfer.h"
#include "mega/types.h"
#include "mega/utils.h"

// Phase 1: keep prototype's WS + threads. Phase 3: move into CurlHttpIO reactor.
#include <curl/curl.h>

#include <zlib.h>

namespace mega
{
namespace ws
{

// ---------- small time helper (deciseconds) ----------
struct SteadyTime
{
    static dstime ds()
    {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
        const dstime v = static_cast<dstime>(ms / 100);
        return v ? v : 1; // never return 0 (0 used as sentinel)
    }

    static void sleep_ds(const dstime ds)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(ds * 100));
    }

    static std::int32_t difference(const dstime a, const dstime b)
    {
        return static_cast<std::int32_t>(a - b);
    }
};

// ---------- RAII helper: temporarily release a unique_lock and re-acquire it on scope exit ----------
class ScopedUnlock
{
public:
    explicit ScopedUnlock(std::unique_lock<std::mutex>& lock):
        mLock(lock),
        mWasLocked(lock.owns_lock())
    {
        if (mWasLocked)
        {
            mLock.unlock();
        }
    }

    ~ScopedUnlock()
    {
        if (mWasLocked)
        {
            mLock.lock();
        }
    }

    ScopedUnlock(const ScopedUnlock&) = delete;
    ScopedUnlock& operator=(const ScopedUnlock&) = delete;

private:
    std::unique_lock<std::mutex>& mLock;
    bool mWasLocked{false};
};

// ---------- CRC32 wrapper ----------
struct CRC32
{
    static std::uint32_t crc32b(const char* data, const int len, const std::uint32_t seed = 0)
    {
        return static_cast<std::uint32_t>(
            ::crc32(seed, reinterpret_cast<const Bytef*>(data), static_cast<uInt>(len)));
    }
};

namespace detail
{

InboundFrameValidationResult validateInboundFrame(const char* msg, const int len)
{
    if (len < kMinInboundFrameBytes)
    {
        return InboundFrameValidationResult::TooShort;
    }

    std::uint32_t trailerCrc = 0;
    std::memcpy(&trailerCrc,
                msg + len - kInboundFrameTrailerCrcBytes,
                sizeof(trailerCrc));
    if (trailerCrc != CRC32::crc32b(msg, len - kInboundFrameTrailerCrcBytes))
    {
        return InboundFrameValidationResult::BadCrc;
    }

    return InboundFrameValidationResult::Ok;
}

} // namespace detail

// ---------- Chunk map ----------
struct ChunkMap
{
    static constexpr int SEGSIZE = 131072;
    std::map<m_off_t, int> chunkmap;

    int chunksize(const m_off_t pos) const
    {
        const auto it = chunkmap.find(pos);
        return (it == chunkmap.end()) ? 8 * SEGSIZE : it->second;
    }

    ChunkMap()
    {
        m_off_t p{0};
        int dp{0};
        while (dp < 8 * SEGSIZE)
        {
            dp += SEGSIZE;
            chunkmap[p] = dp;
            p += dp;
        }
    }
};

static ChunkMap g_chunkMap;

#ifndef NDEBUG
// Free function wrapper so unit tests can link against it via extern declaration.
int chunkSizeAtPosition(m_off_t pos)
{
    return g_chunkMap.chunksize(pos);
}
#endif

// ---------- forward decls ----------
struct WsPool;
struct WsConn;
struct WsPoolThread;
class CurlResponseProc;

class UploadEngine; // from header

enum class FailReason : std::uint8_t
{
    ServerError,
    OpenFailed,
    ReadFailed,
    StateLost,
    Protocol,
    CrcFailed,
    Unknown
};

enum class WsApiServerEvent: signed char
{
    ChunkIngested = 1, // chunk ingested (non-final)
    AlreadyOnServer = 2, // already on server (after reconnect)
    CrcFailed = 3, // chunk CRC failed
    UploadCompleted = 4, // upload completed
    Distress = 5, // server requested pool refresh
    Throttle = 6, // server requested temporary pause
    FinalDataIngested = 7 // final data ingested (server knows file is complete)
};

// ---------- WsUploadFile (uses SDK File/FileAccess) ----------
class WsUploadFile
{
    class ActiveIOGuard;

public:
    static constexpr std::int32_t RETRYINTERVAL{60 * 10}; // ds

    WsUploadFile(MegaClient& client, Transfer& t, const std::uint32_t fileno):
        mClient(client),
        mTransfer(&t),
        mLocalPath(frontFile(t)->getLocalname()),
        mFileNo(fileno),
        mSessionUrlHint(t.ws_session_url)
    {
        File* const f = frontFile(t);
        assert(f); // Transfer::files.front() must exist for PUT uploads
        mSize = f->size;
        mMtime = f->mtime;
        // mTransferKey / mCtrIv: populated later via snapshotCryptoMaterial() on the
        // worker thread, after preflightStart has guaranteed they are non-zero.
        if (!mSessionUrlHint.empty())
        {
            // Resume after restart:
            // continue from the last known contiguous position and keep already confirmed bytes.
            mHeadPos = std::min<m_off_t>(t.pos, mSize);
            mBytesConfirmed = std::min<m_off_t>(t.progresscompleted, mHeadPos);
            mLastReportedBytesConfirmed = mBytesConfirmed;
            mLastProgressReportBytes = mBytesConfirmed;
            mLastProgressReportDs = SteadyTime::ds();
        }

        LOG_debug << "WsUploadFile: " << mLocalPath << " fileno=" << mFileNo << " size=" << mSize
                  << " mtime=" << mMtime << " [this = " << this << "]";

        // Preserve transfer-level paused state on enqueue (eg, restored from cache).
        if (t.state == TRANSFERSTATE_PAUSED)
        {
            mPaused = true;
        }
    }

    ~WsUploadFile() = default;

    // Snapshot write-once crypto material. Called exactly once from WsPool::getWsUploadFile()
    // under uploadMutex, after preflightStart has guaranteed Transfer::transferkey and ctriv
    // are populated (either restored from cache or generated by prepareUploadForWs()). After
    // this returns, worker threads only read the snapshotted copies -- never Transfer&.
    void snapshotCryptoMaterial(const Transfer& t)
    {
        mTransferKey = t.transferkey;
        mCtrIv = t.ctriv;
    }

    const std::array<byte, SymmCipher::KEYLENGTH>& transferKey() const noexcept
    {
        return mTransferKey;
    }

    int64_t ctrIv() const noexcept
    {
        return mCtrIv;
    }

    Transfer& transfer() const noexcept
    {
        return *mTransfer;
    }

    bool inPool() const noexcept
    {
        return mPool != nullptr;
    }

    // queue/pool bookkeeping
    bool continuingUpload(const dstime now) const
    {
        if (mAborted || mUploadCompletionTime != 0)
            return false;

        if (mUploadFailedTime != 0)
        {
            const dstime retryUntil =
                mRetryUntil ? mRetryUntil : (mUploadFailedTime + RETRYINTERVAL);
            return now >= retryUntil;
        }

        return true;
    }

    bool sourceMatchesExpected(const m_time_t observedMtime,
                               const m_off_t observedSize) const noexcept
    {
        return observedMtime == mMtime && observedSize == mSize;
    }

    UploadEngine::FailureDisposition classifyOpenFailure(const FileAccess* fa) const noexcept
    {
        if (!fa)
            return UploadEngine::FailureDisposition::Retryable;

        // Mirror legacy slot behavior for local-source failures: once WS has selected
        // a file, a non-transient open/stat failure means the local source is no longer
        // a valid upload candidate and should not burn the generic retry budget.
        return fa->retry ? UploadEngine::FailureDisposition::Retryable :
                           UploadEngine::FailureDisposition::Permanent;
    }

    void setPool(WsPool& p); // defined after WsPool
    void unsetPool(); // defined after WsPool
    bool hasPool() const;

    bool ensureOpen(std::mutex& engineMutex)
    {
        if (mFAOpened)
            return true;
        if (!mFA)
        {
            markFailed();
            return false;
        }

        FileAccess* const faRaw = mFA.get();
        std::unique_lock<std::mutex> io(mReadMutex);

        engineMutex.unlock();
        const bool ok = faRaw->openf(FSLogging::logOnError); // does sysopen() if needed
        io.unlock(); // do not re-lock engine while holding the I/O mutex (lock ordering)
        engineMutex.lock();

        if (mFA.get() != faRaw)
            return false;
        mFAOpened = ok;
        if (!ok)
            markFailed(classifyOpenFailure(faRaw));
        return ok;
    }

    void closeFA()
    {
        std::lock_guard<std::mutex> io(mReadMutex);
        if (mFA && mFAOpened)
            mFA->closef();
        mFAOpened = false;
        mFA = nullptr;
    }

    // I/O (open on first read) — engineMutex is the single engine mutex
    bool readData(char* buf, const m_off_t pos, const int len, std::mutex& engineMutex)
    {
        LOG_debug << "[WsUploadFile::readData] BEGIN [buf=" << (void*)buf << "] [pos=" << pos
                  << "] [len=" << len << "] [this = " << this
                  << "] [thread_id=" << std::this_thread::get_id() << "]";
        ActiveIOGuard io(*this);
        // open on first use
        if (!mFA)
        {
            LOG_debug << "[WsUploadFile::readData] !mFA -> newfileaccess for localname="
                      << mLocalPath << " [this = " << this
                      << "] [thread_id=" << std::this_thread::get_id() << "]";
            engineMutex.unlock();
            auto fa = mClient.fsaccess->newfileaccess();
            fa->mShareDelete =
                true; // Allow file to be moved/deleted while WS upload holds the handle
            const bool okOpen = fa->fopen(mLocalPath, OPEN_RDONLY, FSLogging::logOnError);
            LOG_debug << "[WsUploadFile::readData] okOpen=" << okOpen
                      << " [localname=" << mLocalPath << "] [this = " << this
                      << "] [thread_id=" << std::this_thread::get_id() << "]";
            engineMutex.lock();

            if (!okOpen)
            {
                LOG_debug << "[WsUploadFile::readData] !okOpen -> markFailed() and return false "
                             "[localname="
                          << mLocalPath << "] [this = " << this
                          << "] [thread_id=" << std::this_thread::get_id() << "]";
                markFailed(classifyOpenFailure(fa.get()));
                return false;
            }

            if (!sourceMatchesExpected(fa->mtime, fa->size))
            {
                LOG_warn << "[WsUploadFile::readData] file changed before first read. "
                         << "Expected mtime=" << mMtime << " size=" << mSize
                         << ", got mtime=" << fa->mtime << " size=" << fa->size
                         << " [localname=" << mLocalPath << "]";
                markFailed(UploadEngine::FailureDisposition::Permanent);
                return false;
            }

            mFA = std::move(fa);
            mBytesSinceLastStat = 0;
            mStatIntervalBytes =
                (mSize >= STAT_INTERVAL_LARGE) ? STAT_INTERVAL_LARGE : STAT_INTERVAL_SMALL;
            LOG_debug << "[WsUploadFile::readData] mFA=" << (void*)mFA.get() << " mSize=" << mSize
                      << " mMtime=" << mMtime << " [localname=" << mLocalPath
                      << "] [this = " << this << "] [thread_id=" << std::this_thread::get_id()
                      << "]";
        }

        if (!ensureOpen(engineMutex))
        {
            LOG_debug << "[WsUploadFile::readData] !ensureOpen -> return false [localname="
                      << mLocalPath << "] [this = " << this
                      << "] [thread_id=" << std::this_thread::get_id() << "]";
            return false;
        }

        // Mirror legacy openf() behavior: periodically verify the source file
        // still exists and is unmodified. The legacy fread() does open-read-close
        // per *connection* chunk (8-32 MB), catching changes via sysstat() in
        // openf(). The WS engine reads at chunkmac granularity (128 KB - 1 MB),
        // which is 8-30x more frequent. Throttle the check to every
        // mStatIntervalBytes to match legacy frequency.
        //
        // Two checks at each threshold crossing:
        // 1. Existence:  fopen() on a temp FA fails -> file deleted/moved
        // 2. Modification: mtime or size changed -> file modified in place
        mBytesSinceLastStat += len;
        if (mBytesSinceLastStat >= mStatIntervalBytes)
        {
            mBytesSinceLastStat = 0;

            FileAccess* const faRawCheck = mFA.get();
            const LocalPath& path = mLocalPath;

            engineMutex.unlock();
            auto checkFA = mClient.fsaccess->newfileaccess();
            checkFA->mShareDelete = true;
            bool exists = checkFA->fopen(path, FSLogging::logOnError);
            m_time_t currMtime = checkFA->mtime;
            m_off_t currSize = checkFA->size;
            const auto missingDisposition = classifyOpenFailure(checkFA.get());
            checkFA.reset();
            engineMutex.lock();

            if (mFA.get() != faRawCheck)
                return false; // FA was swapped while we released the lock

            if (!exists)
            {
                LOG_warn << "[WsUploadFile::readData] file stat failed (deleted?) "
                         << "[localname=" << mLocalPath << "]";
                closeFA();
                markFailed(missingDisposition);
                return false;
            }

            if (currMtime != mMtime || currSize != mSize)
            {
                LOG_warn << "[WsUploadFile::readData] file changed since open. "
                         << "Expected mtime=" << mMtime << " size=" << mSize
                         << ", got mtime=" << currMtime << " size=" << currSize
                         << " [localname=" << mLocalPath << "]";
                closeFA();
                markFailed(UploadEngine::FailureDisposition::Permanent);
                return false;
            }
        }

        bool okRead = false;
        bool didReopen = false;
        bool reok = false;

        FileAccess* faRaw = nullptr;
        std::unique_lock<std::mutex> ioLock(mReadMutex);
        faRaw = mFA.get();
        engineMutex.unlock();
        {
            // Some platforms declare frawread(pos) as unsigned — cast explicitly to avoid
            // -Wconversion
            if (faRaw)
            {
                okRead = faRaw->frawread(reinterpret_cast<byte*>(buf),
                                         static_cast<unsigned>(len),
                                         pos,
                                         /*caller_opened=*/true,
                                         FSLogging::logOnError);
            }

            if (!okRead && faRaw)
            {
                // optional one-shot recover: reopen + retry once
                LOG_debug << "[WsUploadFile::readData] !okRead -> reopen + retry once [localname="
                          << mLocalPath << "] [this = " << this
                          << "] [thread_id=" << std::this_thread::get_id() << "]";
                faRaw->closef();
                reok = faRaw->openf(FSLogging::logOnError);
                didReopen = true;
                if (reok)
                {
                    okRead = faRaw->frawread(reinterpret_cast<byte*>(buf),
                                             static_cast<unsigned>(len),
                                             pos,
                                             /*caller_opened=*/true,
                                             FSLogging::logOnError);
                }
            }
        }
        ioLock.unlock(); // do not re-lock engine while holding the I/O mutex (lock ordering)
        LOG_debug << "[WsUploadFile::readData] okRead=" << okRead << " [localname=" << mLocalPath
                  << "] [this = " << this << "] [thread_id=" << std::this_thread::get_id() << "]";
        engineMutex.lock();

        if (didReopen && mFA.get() == faRaw)
        {
            LOG_debug << "[WsUploadFile::readData] reok=" << reok << " [localname=" << mLocalPath
                      << "] [this = " << this << "] [thread_id=" << std::this_thread::get_id()
                      << "]";
            mFAOpened = reok;
        }

        if (!okRead)
        {
            auto disposition = UploadEngine::FailureDisposition::Retryable;
            if (didReopen && !reok)
            {
                disposition = classifyOpenFailure(faRaw);
            }
            LOG_debug << "[WsUploadFile::readData] !okRead -> markFailed() [localname="
                      << mLocalPath << "] [this = " << this
                      << "] [thread_id=" << std::this_thread::get_id() << "]";
            closeFA();
            markFailed(disposition);
        }
        LOG_debug << "[WsUploadFile::readData] return okRead=" << okRead
                  << " [localname=" << mLocalPath << "] [this = " << this
                  << "] [thread_id=" << std::this_thread::get_id() << "]";
        return okRead;
    }

    void waitForNoIO() const
    {
        while (mActiveIO.load(std::memory_order_acquire) != 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // server-confirmed progression
    void onServerConfirmedBytes(const m_off_t bytes)
    {
        mBytesConfirmed += bytes;
        if (!mFirstAckTime)
            mFirstAckTime = SteadyTime::ds();
        mClientActiveFilesTick = true;
    }

    // progress coalescing (server-confirmed): report if delta or time threshold hit
    bool progressReportDue(const dstime now)
    {
        constexpr m_off_t kStepBytes = 64 * 1024; // 64 KiB
        constexpr dstime kStepTime = 5; // 0.5s (ds)
        const bool bytesDue = (mBytesConfirmed - mLastProgressReportBytes) >= kStepBytes;
        const bool timeDue =
            SteadyTime::difference(now, mLastProgressReportDs) >= static_cast<int32_t>(kStepTime);
        if (bytesDue || timeDue)
        {
            mLastProgressReportBytes = mBytesConfirmed;
            mLastProgressReportDs = now ? now : SteadyTime::ds();
            return true;
        }
        return false;
    }

    // accessors
    m_off_t size() const noexcept
    {
        return mSize;
    }

    m_off_t headPos() const noexcept
    {
        return mHeadPos;
    }

    void advanceHead(const int by) noexcept
    {
        mHeadPos += by;
    }

    bool eofSet() const noexcept
    {
        return mEofSet;
    }

    bool hasPendingBytesOrEofToSend() const noexcept
    {
        return headPos() < size() || !eofSet();
    }

    void markEOF() noexcept
    {
        mEofSet = true;
    }

    std::uint32_t fileno() const noexcept
    {
        LOG_debug << "[WsUploadFile::fileno] return mFileNo=" << mFileNo << " [this = " << this
                  << "]";
        return mFileNo;
    }

    const std::string& sessionUrlHint() const noexcept
    {
        return mSessionUrlHint;
    }

    void clearSessionUrlHintAndRestart()
    {
        mSessionUrlHint.clear();
        resetAttemptState();
    }

    bool getCurrentSessionUrl(std::string& outUrl) const;

    bool paused() const noexcept
    {
        LOG_debug << "[WsUploadFile::paused] return mPaused=" << mPaused << " [this = " << this
                  << "]";
        return mPaused;
    }

    void setPaused(const bool p) noexcept
    {
        LOG_debug << "[WsUploadFile::setPaused] p=" << p << " [this = " << this << "]";
        mPaused = p;
        closeFA();
    }

    bool aborted() const noexcept
    {
        LOG_debug << "[WsUploadFile::aborted] return mAborted=" << mAborted << " [this = " << this
                  << "]";
        return mAborted;
    }

    void cancel() noexcept
    {
        LOG_debug << "[WsUploadFile::cancel] call [this = " << this << "]";
        mAborted = true;
        closeFA();
    }

    void setUploadStart(const dstime t) noexcept
    {
        LOG_debug << "[WsUploadFile::setUploadStart] t=" << t << " [this = " << this << "]";
        mUploadStartTime = t;
        mUploadFailedTime = 0;
        mRetryUntil = 0;
        mReadFailureDisposition = UploadEngine::FailureDisposition::Retryable;
        // Keep speed reporting scoped to the current upload attempt in case we are in a resume.
        mAttemptBaseConfirmed = mBytesConfirmed;
        mAckSpeedController = SpeedController();
    }

    void setRetryUntil(const dstime when)
    {
        mRetryUntil = when;
    }

    void markFailedForRetry(const dstime retryUntil)
    {
        resetAttemptState();
        mReadFailureDisposition = UploadEngine::FailureDisposition::Retryable;
        if (!mUploadFailedTime)
            mUploadFailedTime = SteadyTime::ds();
        mRetryUntil = retryUntil ? retryUntil : (mUploadFailedTime + RETRYINTERVAL);
        closeFA();
    }

    m_off_t bytesConfirmed() const noexcept
    {
        return mBytesConfirmed;
    }

    // hooks (Phase 2/5 will notify Transfer/app + crypto)
    void uploadFailed(const FailReason reason)
    {
        resetAttemptState();
        if (reason != FailReason::ReadFailed)
        {
            mReadFailureDisposition = UploadEngine::FailureDisposition::Retryable;
        }
        mUploadFailedTime = SteadyTime::ds();
        mRetryUntil = mUploadFailedTime + RETRYINTERVAL;
        unsetPool();
        closeFA();
        LOG_warn << "[WsUploadFile::uploadFailed] file failed, reason=" << static_cast<int>(reason)
                 << " [this = " << this << "]";
    }

    UploadEngine::FailureDisposition readFailureDisposition() const noexcept
    {
        return mReadFailureDisposition;
    }

    void uploadCompleted(const char* response, const int len)
    {
        mUploadCompletionTime = SteadyTime::ds();
        unsetPool();
        closeFA();
        const auto dsElapsed = SteadyTime::difference(mUploadCompletionTime, mUploadStartTime);
        const auto attemptConfirmed =
            std::max<m_off_t>(0, mBytesConfirmed - mAttemptBaseConfirmed);
        const auto kbps = dsElapsed ? (attemptConfirmed / dsElapsed * 10 / 1024) : 0;
        LOG_info << "[WsUploadFile::uploadCompleted] upload completed (server payload len=" << len
                 << ") Progress: " << mBytesConfirmed << " of " << mSize
                 << " bytes (attempt bytes: " << attemptConfirmed << ") @ ~" << kbps
                 << " KB/s [this = " << this << "]";
        (void)response; // Phase 5: use payload (e.g. MAC/fingerprint)
    }

    // debug throughput (server-ACK basis)
    void maybeReportThroughput(const dstime now)
    {
        if (mUploadCompletionTime || mUploadFailedTime)
            return;
        if (mBytesConfirmed == mLastReportedBytesConfirmed)
            return;

        mLastReportedBytesConfirmed = mBytesConfirmed;

        const auto dsElapsed = SteadyTime::difference(now, mUploadStartTime);
        const auto attemptConfirmed =
            std::max<m_off_t>(0, mBytesConfirmed - mAttemptBaseConfirmed);
        const auto kbps = dsElapsed ? (attemptConfirmed / dsElapsed * 10 / 1024) : 0;
        LOG_debug << "[WsUploadFile::maybeReportThroughput] " << mBytesConfirmed << " of " << mSize
                  << " bytes (attempt bytes: " << attemptConfirmed << ") @ ~" << kbps
                  << " KB/s [this = " << this << "]";
    }

    bool isUploading() const noexcept
    {
        LOG_debug << "[WsUploadFile::isUploading] mUploadStartTime= " << mUploadStartTime
                  << " mPaused=" << mPaused << " mAborted=" << mAborted
                  << " mUploadFailedTime=" << mUploadFailedTime << " returning "
                  << (mUploadStartTime != 0 && !mPaused && !mAborted && mUploadFailedTime == 0)
                  << " [this = " << this << "]";
        return mUploadStartTime != 0 && !mPaused && !mAborted && mUploadFailedTime == 0;
    }

    void onRequestSent() noexcept
    {
        ++mNumRequests;
    }

    void onRequestFailed() noexcept
    {
        ++mNumFailedRequests;
    }

    bool getTransferStats(UploadEngine::WsTransferStats& stats) const
    {
        if (!mUploadStartTime || mBytesConfirmed <= 0)
            return false;

        // Advance controller with latest ACKed position, then read the current circular mean.
        // requestProgressed() returns 0 when there's no new delta, but we still want the last
        // non-zero window speed for finish-time callbacks.
        const auto attemptConfirmed =
            std::max<m_off_t>(0, mBytesConfirmed - mAttemptBaseConfirmed);
        mAckSpeedController.requestProgressed(attemptConfirmed);
        stats.windowSpeedBytesPerSecond = mAckSpeedController.getCircularMeanSpeed();
        stats.meanSpeedBytesPerSecond = mAckSpeedController.getMeanSpeed();

        dstime latencyDs = 0;
        if (mFirstAckTime)
            latencyDs = SteadyTime::difference(mFirstAckTime, mUploadStartTime);
        else if (mUploadCompletionTime)
            latencyDs = SteadyTime::difference(mUploadCompletionTime, mUploadStartTime);
        if (latencyDs < 0)
            latencyDs = 0;
        if (latencyDs == 0 && (mBytesConfirmed > 0 || mUploadCompletionTime))
            latencyDs = 1;
        stats.avgStartTransferTime = std::chrono::milliseconds(latencyDs * 100);

        if (mNumRequests > 0)
        {
            stats.failedRequestRatio =
                static_cast<double>(mNumFailedRequests) / static_cast<double>(mNumRequests);
        }
        else
        {
            stats.failedRequestRatio = 0.0;
        }
        return true;
    }

public: // accessed by engine
    // Accumulate server-confirmed chunk MAC updates on the worker threads.
    //
    // These are later drained and applied to Transfer::chunkmacs on the MegaClient thread,
    // ensuring Transfer mutation and transfercache serialization remain single-threaded.
    void queueConfirmedChunkMacs(chunkmac_map&& macs)
    {
        if (macs.size())
        {
            mConfirmedChunkMacs.emplace_back(std::move(macs));
        }
    }

    void drainConfirmedChunkMacs(std::vector<chunkmac_map>& out)
    {
        if (mConfirmedChunkMacs.empty())
        {
            return;
        }

        if (out.empty())
        {
            out.swap(mConfirmedChunkMacs);
            return;
        }

        for (auto& m: mConfirmedChunkMacs)
        {
            out.emplace_back(std::move(m));
        }
        mConfirmedChunkMacs.clear();
    }

    void clearConfirmedChunkMacs()
    {
        mConfirmedChunkMacs.clear();
    }

    bool mClientActiveFilesTick{false};
    WsPool* mPool{nullptr};

private:
    static File* frontFile(Transfer& t)
    {
        return t.files.empty() ? nullptr : *t.files.begin();
    }

    class ActiveIOGuard
    {
    public:
        explicit ActiveIOGuard(WsUploadFile& f):
            mFile(f)
        {
            mFile.mActiveIO.fetch_add(1, std::memory_order_acq_rel);
        }

        ~ActiveIOGuard()
        {
            mFile.mActiveIO.fetch_sub(1, std::memory_order_acq_rel);
        }

    private:
        WsUploadFile& mFile;
    };

    void resetAttemptState()
    {
        // Per-attempt counters/state. When we fail and retry we restart the upload from scratch
        mHeadPos = 0;
        mBytesConfirmed = 0;
        mAttemptBaseConfirmed = 0;
        mLastReportedBytesConfirmed = 0;
        mNumRequests = 0;
        mNumFailedRequests = 0;
        mFirstAckTime = 0;
        mLastProgressReportBytes = 0;
        mLastProgressReportDs = 0;
        mAckSpeedController = SpeedController();
        mEofSet = false;
        mUploadStartTime = 0;
        mUploadCompletionTime = 0;
        mClientActiveFilesTick = false;
        mConfirmedChunkMacs.clear();
    }

    void markFailed(const UploadEngine::FailureDisposition disposition =
                        UploadEngine::FailureDisposition::Retryable)
    {
        resetAttemptState();
        mReadFailureDisposition = disposition;
        mUploadFailedTime = SteadyTime::ds();
        mRetryUntil = mUploadFailedTime + RETRYINTERVAL;
    }

private:
    MegaClient& mClient;
    Transfer* mTransfer{
        nullptr}; // identity only, used by callbacks; never dereferenced on worker threads
    // Worker threads only ever need these snapshots, copied once on the client thread (Phase 1)
    // or once on the worker thread under uploadMutex after preflightStart (Phase 2).
    LocalPath mLocalPath; // Phase 1: set in constructor
    std::array<byte, SymmCipher::KEYLENGTH>
        mTransferKey{}; // Phase 2: set in snapshotCryptoMaterial()
    int64_t mCtrIv{0}; // Phase 2
    std::unique_ptr<FileAccess> mFA{}; // open on first read
    bool mFAOpened{false};
    std::mutex mReadMutex; // needed for Android because of lseek64+read
    std::atomic<unsigned> mActiveIO{0};

    std::uint32_t mFileNo{0};

    // Saved session URL to allow best-effort resume after restart.
    std::string mSessionUrlHint;

    // progress/state
    m_off_t mSize{0};
    m_off_t mHeadPos{0};
    m_off_t mBytesConfirmed{0};
    m_off_t mAttemptBaseConfirmed{0};
    m_off_t mLastReportedBytesConfirmed{0};
    m_off_t mNumRequests{0};
    m_off_t mNumFailedRequests{0};
    dstime mFirstAckTime{0};
    m_off_t mLastProgressReportBytes{0};
    dstime mLastProgressReportDs{0};
    mutable SpeedController mAckSpeedController{};
    m_time_t mMtime{0};
    UploadEngine::FailureDisposition mReadFailureDisposition{
        UploadEngine::FailureDisposition::Retryable};

    m_off_t mBytesSinceLastStat = 0;
    m_off_t mStatIntervalBytes = STAT_INTERVAL_LARGE;

    static constexpr m_off_t STAT_INTERVAL_LARGE = 8 * 1024 * 1024; // 8 MB
    static constexpr m_off_t STAT_INTERVAL_SMALL = 2 * 1024 * 1024; // 2 MB

    bool mEofSet{false};
    bool mAborted{false};
    bool mPaused{false};

    dstime mUploadStartTime{0};
    dstime mUploadCompletionTime{0};
    dstime mUploadFailedTime{0};
    dstime mRetryUntil{0};

    // Server-confirmed chunk MAC updates awaiting application on the client thread.
    std::vector<chunkmac_map> mConfirmedChunkMacs;
};

// ---------- WS payloads ----------
#pragma pack(push, 1)

struct ChunkHeader
{
    std::uint32_t fileno;
    m_off_t pos;
    int len;
    std::uint32_t crc;
};

#pragma pack(pop)

static constexpr int MB = 1048576;

struct WsChunk
{
    m_off_t pos{0};
    int len{0};
    std::uint32_t fileno{0};
};

struct WsBuf
{
    char buf[20 + MB];
    int mSendPos{0};
    int mDataLen{0};

    void add(const char* data, const int len)
    {
        assert((len >= 0) && len <= (static_cast<int>(sizeof(buf)) - mDataLen));

        std::memcpy(buf + mDataLen, data, static_cast<size_t>(len));
        mDataLen += len;
    }

    void reset()
    {
        mSendPos = 0;
        mDataLen = 0;
    }

    bool sendWS(struct WsConn* ws, int& bufferedAmount);
};

struct ChunkFingerprintMacUpdate
{
    m_off_t pos{0};
    chunkmac_map macs;

    ChunkFingerprintMacUpdate() = default;

    explicit ChunkFingerprintMacUpdate(const m_off_t p):
        pos(p)
    {}

    ChunkFingerprintMacUpdate(const m_off_t p, chunkmac_map&& m):
        pos(p),
        macs(std::move(m))
    {}

    void apply(const m_off_t confirmedPos, WsUploadFile& file)
    {
        if (confirmedPos != pos)
            return;
        if (macs.size())
        {
            file.queueConfirmedChunkMacs(std::move(macs));
        }
    }
};

// ---------- WebSocket connection (per thread) ----------
struct WsConn
{
    CURL* curl{nullptr};
    int bufferedAmount{0};

    enum class ReadyState : std::uint8_t
    {
        CONNECTING,
        OPEN,
        CLOSING,
        CLOSED
    };
    ReadyState readyState{ReadyState::CLOSED};

    WsBuf mBufs[2];
    char mCurBuf{0};
    bool mClosing{false};

    // server response (small frames)
    char mInBuf[64];
    int mInPos{0};

    WsPool* mPool{nullptr};
    std::unordered_set<WsConn*>::iterator mConns_it;

    // in-flight chunk log (for resends on drop)
    std::vector<std::pair<WsChunk, ChunkFingerprintMacUpdate>> mChunksInFlight;

    explicit WsConn(WsPool* pool);
    ~WsConn();

    bool connectWS();
    void closeWS();

    void curlSend();
    void curlRecv();

    void onopen();
    void onclose();
    void onmessage(const char* msg, int len);

    bool haveSpace() const
    {
        return !bufferedAmount || !mBufs[0].mDataLen || !mBufs[1].mDataLen;
    }

    bool readyForData() const
    {
        return !mClosing;
    }

    void senddata(const int buf, const char* data, const int len)
    {
        LOG_debug << "[WsConn::senddata] BEGIN [buf=" << buf << "] [data=" << (void*)data
                  << "] [len=" << len << "] [bufferedAmount=" << bufferedAmount
                  << "] [this = " << this << "]";
        mBufs[buf].add(data, len);
        bufferedAmount += len;
        LOG_debug << "[WsConn::senddata] END [bufferedAmount=" << bufferedAmount
                  << "] [this = " << this << "]";
    }

    void sendChunkData(std::uint32_t fileno, m_off_t pos, const char* data, int len);
};

struct WsPoolThread
{
    std::thread t;
    bool terminate{false};
    bool terminated{false};

    explicit WsPoolThread(WsPool* pool); // defined after WsPool

    void join()
    {
        if (!t.joinable())
            return;
        if (t.get_id() == std::this_thread::get_id())
        {
            LOG_warn << "[WsPoolThread::join] refusing to join self; detaching [this = " << this
                     << "]";
            t.detach();
            return;
        }
        t.join();
    }

    ~WsPoolThread()
    {
        LOG_debug << "[WsPoolThread::~WsPoolThread] BEGIN -> t.join() [this = " << this << "]";
        join();
        LOG_debug << "[WsPoolThread::~WsPoolThread] END [this = " << this << "]";
    }
};

// ---------- Pool per size class ----------
struct WsPool
{
    static constexpr std::int32_t CONNRETRYINTERVAL = 5 * 10;
    static constexpr std::int32_t UPLOADTIMEOUT = 180 * 10;

    UploadEngine::Impl* mImpl{nullptr};

    std::unordered_set<WsConn*> mConns;
    std::vector<WsChunk> mToResend;

    std::vector<std::unique_ptr<WsPoolThread>> mActiveThreads, mExitingThreads;

    int mNumPoolFiles{0};
    WsUploadFile* mUploadingFile{nullptr};
    std::uint32_t mUFTQversion{0};

    dstime mPoolCreationTime{SteadyTime::ds()};
    std::string mUrl;

    m_off_t mMinFileSize{0}, mMaxFileSize{0};
    int mNumChunksInFlight{0};

    dstime mLastActive{0};
    dstime mLastServerResponse{0};
    dstime mPausedByServerUntil{0};

    unsigned char mNumberOfConnections{3};
    bool mRetiring{false};
    bool mPinned{false};

    explicit WsPool(std::pair<std::string, m_off_t> urlmaxsize,
                    m_off_t minsize,
                    UploadEngine::Impl* impl,
                    const unsigned char numConnections = 3):
        mImpl(impl),
        mPoolCreationTime(SteadyTime::ds()),
        mUrl(std::move(urlmaxsize.first)),
        mMinFileSize(minsize),
        mMaxFileSize(urlmaxsize.second),
        mNumberOfConnections(std::max<unsigned char>(1, numConnections))
    {
        LOG_debug << "[WsPool] constructed [mMinFileSize=" << mMinFileSize
                  << " mMaxFileSize=" << mMaxFileSize << "] [this = " << this << "]";
        checkThreads();
    }

    void increaseNumPoolFiles()
    {
        mNumPoolFiles++;
    }

    void decreaseNumPoolFiles()
    {
        mNumPoolFiles--;
    }

    void poolWorkerThread(WsPoolThread* th);
    void checkThreads();

    void setPoolNumConn(const unsigned char n)
    {
        mNumberOfConnections = n;
        checkThreads();
    }

    bool stillActive()
    {
        if (mNumPoolFiles)
            return true; // grace via active files
        if (!mNumberOfConnections)
        {
            checkThreads();
            return !mActiveThreads.empty() || !mExitingThreads.empty();
        }
        setPoolNumConn(0);
        return true;
    }

    bool freshAndSameHostMaxSize(const std::pair<std::string, m_off_t>& urlMaxSize,
                                 const dstime oldestvalid)
    {
        constexpr std::size_t wsHostPrefixLength = sizeof("wss://") - 1;

        if (SteadyTime::difference(mPoolCreationTime, oldestvalid) < 0)
            return false;
        if (mMaxFileSize != urlMaxSize.second)
            return false;

        for (std::size_t i = wsHostPrefixLength; i < urlMaxSize.first.size() && i < mUrl.size(); ++i)
        {
            if (mUrl[i] != urlMaxSize.first[i])
                return false;
            if (mUrl[i] == '/')
                return true;
        }
        return false;
    }

    void pauseSending(const dstime ds)
    {
        mPausedByServerUntil = SteadyTime::ds() + ds;
    }

    bool throttledByServer()
    {
        if (!mPausedByServerUntil)
            return false;
        if (SteadyTime::difference(SteadyTime::ds(), mPausedByServerUntil) > 0)
        {
            mPausedByServerUntil = 0;
            return false;
        }
        return true;
    }

    bool getWsUploadFile(dstime now, class UploadEngine::Impl& impl);
    WsUploadFile* findFile(std::uint32_t fileno, class UploadEngine::Impl& impl);
    bool nextChunk(WsChunk& chunk, class UploadEngine::Impl& impl, dstime* retryAfterDs = nullptr);
    void retryChunkLocked(const WsChunk& chunk);
    void retryChunk(const WsChunk& chunk);
    WsUploadFile* handshakeFailureCandidateLocked(dstime now) const;

    void retryChunksOnTheWire(WsConn* ws);

    // Remove any queued/in-flight chunks for the specified file.
    // Must be called with UploadEngine::Impl::uploadMutex held.
    void purgeFileLocked(const std::uint32_t fileno);

    void applyInFlightLocked(const std::uint32_t fileno);
    void applyInFlight(const std::uint32_t fileno);
    bool sendChunk(WsConn* ws,
                   class UploadEngine::Impl& impl,
                   dstime* retryAfterDs = nullptr);
};

// WsUploadFile pool bindings — must be defined after WsPool is complete
inline void WsUploadFile::unsetPool()
{
    LOG_debug << "[WsUploadFile::unsetPool] mPool=" << (void*)mPool << " [this = " << this << "]";
    if (mPool)
    {
        mPool->decreaseNumPoolFiles();
        mPool = nullptr;
    }
}

inline void WsUploadFile::setPool(WsPool& p)
{
    LOG_debug << "[WsUploadFile::setPool] p=" << (void*)&p << " [this = " << this << "]";
    if (mPool == &p)
    {
        // Idempotent rebind to the same pool (eg, resume path re-selects current pool).
        return;
    }
    // Cross-pool rebind is not allowed. Callers must detach first via unsetPool().
    assert(!mPool);
    mPool = &p;
    assert(mPool);
    mPool->increaseNumPoolFiles();
    LOG_debug << "[WsUploadFile::setPool] END [this = " << this << "]";
}

inline bool WsUploadFile::hasPool() const
{
    return mPool != nullptr;
}

inline bool WsUploadFile::getCurrentSessionUrl(std::string& outUrl) const
{
    if (!mPool)
    {
        return false;
    }
    outUrl = mPool->mUrl;
    return true;
}

// ---------- Curl response proc (USC) ----------
class CurlResponseProc
{
public:
    virtual ~CurlResponseProc() = default;
    std::string response;

    virtual void curlIO() {}

    virtual bool done(bool /*success*/) = 0;
};

// ---------- Pool manager (USC refresh + cURL multi) ----------
struct WsPoolMgr
{
    static constexpr std::int32_t POOLCONNKEEPALIVE = 60 * 10;
    static constexpr std::int32_t POOLFRESHNESS = 24 * 3600 * 10;
    const std::int32_t SERVERTIMEOUT = 20 * 10;

    CURLM* curlm = nullptr; // Phase 1: private multi (USC only)
    UploadEngine::Impl* mImpl{nullptr}; // backpointer

    std::vector<std::unique_ptr<WsPool>> mPools;
    std::unordered_map<CURL*, CurlResponseProc*> mCurlProcs;

    std::unordered_set<WsUploadFile*> mActiveFiles; // progress reporting
    dstime mLastNetRead{0};
    std::atomic_bool mRefreshing{false};
    dstime mNextRefreshAttempt{0};
    unsigned mRefreshFailCount{0};

    WsPoolMgr()
    {
        curlm = curl_multi_init();
    }

    ~WsPoolMgr()
    {
        for (auto& kv: mCurlProcs)
        {
            if (curlm && kv.first)
            {
                curl_multi_remove_handle(curlm, kv.first);
            }
            delete kv.second;
        }
        mCurlProcs.clear();

        if (curlm)
        {
            curl_multi_cleanup(curlm);
            curlm = nullptr;
        }
    }

    void curlIO(std::unique_lock<std::mutex>& lk); // defined later
    void checkPools(class UploadEngine::Impl& impl); // defined later
    void refreshPools(); // defined later
    bool refreshPoolsResponse(std::string& response); // defined later

    // Shared tail for USC refresh responses (parsing can be done via string parsing or JSON).
    void applyRefreshedUrls(std::vector<std::pair<std::string, m_off_t>> urls);

    // Ensure a dedicated pool exists for a pinned (resumed) session URL.
    void ensurePinnedPool(const std::string& url);

    void markPoolRetiring(WsPool& pool);
    bool poolHasNoWork(const WsPool& pool) const;
    bool pinnedPoolHasReference(const WsPool& pool, const UploadEngine::Impl& impl) const;
    void retireUnusedPinnedPools(UploadEngine::Impl& impl);
    void cleanupRetiringPools();

    void setCurlResponseProc(CURL* curl, CurlResponseProc* proc)
    {
        LOG_debug << "[WsPoolMgr::setCurlResponseProc] call [curl=" << (void*)curl
                  << "] [proc=" << (void*)proc << "] [this = " << this << "]";
        mCurlProcs[curl] = proc;
        curl_easy_setopt(
            curl,
            CURLOPT_WRITEFUNCTION,
            +[](void* ptr, size_t /*size*/, size_t nmemb, void* opaque) -> size_t
            {
                auto* p = static_cast<CurlResponseProc*>(opaque);
                p->response.append(static_cast<const char*>(ptr), nmemb);
                return nmemb;
            });
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, proc);
        curl_multi_add_handle(curlm, curl);
    }

    void bumpLastNetRead(const dstime now)
    {
        if (SteadyTime::difference(now, mLastNetRead) > 0)
            mLastNetRead = now;
    }

    void bumpAllPools(const dstime now)
    {
        for (auto& p: mPools)
        {
            p->mLastActive = now;
            p->mLastServerResponse = now;
        }
    }
};

// API command wrapper for "usc" (Upload Session Context) used by websocket uploads.
//
// NOTE: WS uploads are pool-based (not per-file), so the response can contain multiple endpoints
// across size classes. We keep parsing minimal and convert to "wss://<host>/<path>" URLs.
class CommandUSCForWsUpload final : public Command
{
public:
    using SizeClass = std::pair<std::string, m_off_t>; // (wss url, max size)
    using Completion = std::function<void(Error, std::vector<SizeClass>&&)>;

    CommandUSCForWsUpload(MegaClient& client, Completion completion)
        : mCompletion(std::move(completion))
    {
        cmd("usc");
        tag = client.reqtag;
        // USC is read-only and safe to run on the lockless request channel.
        mLockless = true;
    }

    bool procresult(Result r, JSON& json) override
    {
        if (r.wasErrorOrOK())
        {
            if (r.wasError(API_OK))
                mCompletion(API_EINTERNAL, {});
            else
                mCompletion(r.errorOrOK(), {});
            return true;
        }

        if (!r.hasJsonArray())
        {
            mCompletion(API_EINTERNAL, {});
            return true;
        }

        std::vector<SizeClass> sizeClasses;

        auto peek = [](const JSON& j) -> char
        {
            const char* p = j.pos;
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',' || *p == ':')
                ++p;
            return *p;
        };

        auto parseEntryArray = [&](JSON& j)
        {
            std::string host;
            std::string path;
            m_off_t maxSize = 0;

            const bool okHost = j.storeobject(&host);
            const bool okPath = j.storeobject(&path);
            if (okHost && okPath)
            {
                if (j.isnumeric())
                {
                    maxSize = j.getint();
                }

                // Ignore any extra fields we don't currently understand.
                while (j.storeobject())
                    ;

                std::string url = "wss://";
                url.append(host);
                url.append("/");
                url.append(path);
                sizeClasses.emplace_back(std::move(url), maxSize);
            }
            else
            {
                while (j.storeobject())
                    ;
            }
        };

        std::function<void(JSON&)> parseArrayContents;
        parseArrayContents = [&](JSON& j)
        {
            const char next = peek(j);
            if (next == ']')
            {
                return;
            }

            if (next == '[')
            {
                while (j.enterarray())
                {
                    parseArrayContents(j);
                    j.leavearray();
                }
                return;
            }

            if (next != '"')
            {
                while (j.storeobject())
                    ;
                return;
            }

            parseEntryArray(j);
        };

        // Parse using a copy to avoid cursor desync on the main JSON instance.
        JSON jsonCopy = json;
        while (jsonCopy.enterarray())
        {
            parseArrayContents(jsonCopy);
            jsonCopy.leavearray();
        }

        // Consume the full response element in the original JSON.
        while (json.storeobject())
            ;

        if (sizeClasses.empty())
        {
            mCompletion(API_EINTERNAL, {});
        }
        else
        {
            mCompletion(API_OK, std::move(sizeClasses));
        }
        return true;
    }

private:
    Completion mCompletion;
};

// ========== UploadEngine::Impl (queue + mgr + thread) ==========
class UploadEngine::Impl
{
public:
    explicit Impl(MegaClient& c):
        client(c)
    {
        //curl_global_init(CURL_GLOBAL_ALL);
        LOG_debug << "[UploadEngine::Impl] constructed";
    }

    ~Impl()
    {
        LOG_debug << "[UploadEngine::Impl::~Impl] BEGIN";
        stop();

        // Manager thread may be waiting up to 500ms in curl_multi_poll; then it exits.
        if (uploadThread.joinable())
            uploadThread.join();

        // Join pool worker threads before member destruction (uploadMutex must remain valid).
        for (auto& p: poolMgr.mPools)
        {
            if (!p)
                continue;
            for (auto& th: p->mActiveThreads)
            {
                if (th)
                    th->join();
            }
            for (auto& th: p->mExitingThreads)
            {
                if (th)
                    th->join();
            }
        }

        LOG_debug << "[UploadEngine::Impl::~Impl] END";
    }

    void stop()
    {
        mStopping.store(true, std::memory_order_release);

        // Stop everything under the same mutex the workers use.
        std::lock_guard<std::mutex> g(uploadMutex);
        paused = true;
        uploadThreadRunning = false; // lets run() break out

        // Ask pool worker threads to exit promptly.
        for (auto& p: poolMgr.mPools)
        {
            p->mNumberOfConnections = 0; // avoids new workers
            for (auto& th: p->mActiveThreads)
                th->terminate = true;
            for (auto& th: p->mExitingThreads)
                th->terminate = true;
        }
    }

    bool stopping() const
    {
        return mStopping.load(std::memory_order_acquire);
    }

    // Queue mirrors TransferList ordering and priority.
    void enqueue(Transfer& t)
    {
        LOG_debug << "[UploadEngine::Impl::enqueue] t=" << t.localfilename
                  << " files.size=" << files.size() << " [this = " << this << "]";
        std::lock_guard<std::mutex> g(uploadMutex);
        if (!nextFileNo)
            nextFileNo = 1;

        // ws_fileno may come from transfer cache restore (Transfer::unserialize).
        // Reuse it when possible so resumed WS state keeps the same file identity.
        std::uint32_t fileno = t.ws_fileno;
        if (!fileno || fileByNo.find(fileno) != fileByNo.end())
        {
            fileno = nextFileNo++;
            t.ws_fileno = fileno;
        }
        else if (fileno >= nextFileNo)
        {
            // Keep nextFileNo above any restored file numbers to avoid collisions.
            nextFileNo = fileno + 1;
        }

        // ws_session_url may also come from transfer cache restore.
        // If present, create/keep a dedicated pinned pool for that exact endpoint.
        if (!t.ws_session_url.empty())
        {
            poolMgr.ensurePinnedPool(t.ws_session_url);
        }

        auto uf = std::make_unique<WsUploadFile>(client, t, fileno);
        auto raw = uf.get();

        fileList.push_back(raw);
        files.emplace(&t, std::move(uf));
        inQueue.emplace(raw);
        fileByNo.emplace(raw->fileno(), raw);

        if (fileList.size() == 1)
            nextIt = fileList.begin();
        bumpQueueVersion();
    }

    void reposition(Transfer& t, Transfer* before)
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        auto it = files.find(&t);
        if (it == files.end())
            return;

        auto* f = it->second.get();
        if (!f || !inQueue.count(f))
            return;

        // remove from current spot
        for (auto lit = fileList.begin(); lit != fileList.end(); ++lit)
        {
            if (*lit == f)
            {
                if (nextIt == lit)
                    ++nextIt;
                fileList.erase(lit);
                break;
            }
        }

        // insert before (if valid), else push back
        if (before)
        {
            auto itb = files.find(before);
            if (itb != files.end() && inQueue.count(itb->second.get()))
            {
                for (auto lit = fileList.begin(); lit != fileList.end(); ++lit)
                {
                    if (*lit == itb->second.get())
                    {
                        fileList.insert(lit, f);
                        bumpQueueVersion();
                        return;
                    }
                }
            }
        }

        fileList.push_back(f);
        bumpQueueVersion();
    }

    void pause(Transfer& t)
    {
        withFile(t,
                 [](WsUploadFile& f)
                 {
                     f.setPaused(true);
                 });
    }

    void unpause(Transfer& t)
    {
        withFile(t,
                 [](WsUploadFile& f)
                 {
                     f.setPaused(false);
                 });
    }

    void remove(Transfer& t)
    {
        std::unique_ptr<WsUploadFile> removed;
        {
            std::lock_guard<std::mutex> g(uploadMutex);
            auto it = files.find(&t);
            if (it == files.end())
                return;

            removed = std::move(it->second);
            auto* f = removed.get();
            if (!f)
                return;

            LOG_debug << "Removing transfer from queue: " << f->fileno();

            poolMgr.mActiveFiles.erase(f);
            for (auto& poolPtr: poolMgr.mPools)
            {
                if (!poolPtr)
                    continue;
                WsPool& pool = *poolPtr;
                if (pool.mUploadingFile == f)
                {
                    pool.mUploadingFile = nullptr;
                    pool.mUFTQversion = queueVersion;
                }
                pool.purgeFileLocked(f->fileno());
            }
            f->unsetPool();

            for (auto lit = fileList.begin(); lit != fileList.end(); ++lit)
            {
                if (*lit == f)
                {
                    if (nextIt == lit)
                        ++nextIt;
                    fileList.erase(lit);
                    break;
                }
            }

            if (nextIt == fileList.end())
                nextIt = fileList.begin();

            inQueue.erase(f);
            fileByNo.erase(f->fileno());
            files.erase(it);
            bumpQueueVersion();
        }

        if (removed)
            removed->waitForNoIO();
    }

    void setRetryUntil(Transfer& t, const dstime when)
    {
        withFile(t,
                 [when](WsUploadFile& f)
                 {
                     f.setRetryUntil(when);
                 });
    }

    void markFailed(Transfer& t, const dstime retryUntil)
    {
        withFile(t,
                 [retryUntil](WsUploadFile& f)
                 {
                     f.markFailedForRetry(retryUntil);
                 });
    }

    bool isUploading(Transfer& t) const
    {
        LOG_debug << "[UploadEngine::Impl::isUploading] t=" << t.localfilename
                  << " [this = " << this << "]";
        std::lock_guard<std::mutex> g(uploadMutex);
        if (const auto it = files.find(&t); it != files.end())
            return it->second->isUploading();
        LOG_debug << "[UploadEngine::Impl::isUploading] t=" << t.localfilename
                  << " not found [this = " << this << "]";
        return false;
    }

    bool isUploading(const Transfer& t) const
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        auto it = files.find(const_cast<Transfer*>(&t));
        if (it == files.end() || !it->second)
            return false;
        const WsUploadFile* f = it->second.get();
        return f->inPool();
    }

    bool getTransferStats(const Transfer& t, UploadEngine::WsTransferStats& stats) const
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        auto it = files.find(const_cast<Transfer*>(&t));
        if (it == files.end() || !it->second)
            return false;
        return it->second->getTransferStats(stats);
    }

    bool drainConfirmedChunkMacs(Transfer& t, std::vector<chunkmac_map>& out)
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        const auto it = files.find(&t);
        if (it == files.end() || !it->second)
            return false;
        it->second->drainConfirmedChunkMacs(out);
        return true;
    }

    bool getSessionUrl(Transfer& t, std::string& outUrl) const
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        const auto it = files.find(&t);
        if (it == files.end() || !it->second)
        {
            return false;
        }
        return it->second->getCurrentSessionUrl(outUrl);
    }

#ifndef NDEBUG
    bool getPoolStateForTesting(const std::string& url,
                                UploadEngine::PoolStateForTesting& out) const
    {
        out = {};

        std::lock_guard<std::mutex> g(uploadMutex);
        auto populateState = [&out](const WsPool& pool)
        {
            out.found = true;
            out.pinned = pool.mPinned;
            out.retiring = pool.mRetiring;
            out.numPoolFiles = pool.mNumPoolFiles;
            out.hasUploadingFile = pool.mUploadingFile != nullptr;
            out.numChunksInFlight = pool.mNumChunksInFlight;
            out.queuedResends = static_cast<unsigned>(pool.mToResend.size());
            out.activeThreads = static_cast<unsigned>(pool.mActiveThreads.size());
            out.exitingThreads = static_cast<unsigned>(pool.mExitingThreads.size());
        };

        for (const auto& poolPtr: poolMgr.mPools)
        {
            if (!poolPtr || !poolPtr->mPinned || poolPtr->mUrl != url)
            {
                continue;
            }

            populateState(*poolPtr);
            out.hasReference = poolMgr.pinnedPoolHasReference(*poolPtr, *this);
            return true;
        }

        for (const auto& poolPtr: poolMgr.mPools)
        {
            if (!poolPtr || poolPtr->mUrl != url)
            {
                continue;
            }

            populateState(*poolPtr);
            out.hasReference = poolMgr.pinnedPoolHasReference(*poolPtr, *this);
            return true;
        }

        return false;
    }

    bool isTrackedForTesting(const Transfer& t) const
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        return files.find(const_cast<Transfer*>(&t)) != files.end();
    }
#endif

    void invalidatePinnedSessionUrl(const std::string& url)
    {
        if (url.empty() || stopping())
        {
            return;
        }

        struct Victim
        {
            direction_t type;
            UploadHandle uploadhandle;
            Transfer* transfer;
        };

        std::vector<Victim> victims;
        {
            std::unique_lock<std::mutex> lk(uploadMutex);
            for (auto it = files.begin(); it != files.end(); ++it)
            {
                auto* tp = it->first;
                const auto& uf = it->second;
                if (uf && uf->sessionUrlHint() == url)
                {
                    victims.push_back(Victim{tp->type, tp->uploadhandle, tp});
                }
            }

            if (victims.empty())
            {
                return;
            }

            // Update in-memory WS engine state immediately without waiting for client-thread work.
            for (const auto& v: victims)
            {
                const auto it = files.find(v.transfer);
                if (it != files.end() && it->second)
                {
                    WsUploadFile* uf = it->second.get();
                    uf->clearSessionUrlHintAndRestart();

                    // If the transfer was already bound to the soon-to-be-retired pinned pool,
                    // detach it so it can be picked by fresh (non-pinned) pools.
                    WsPool* const pool = uf->mPool;
                    if (pool && pool->mPinned && pool->mUrl == url)
                    {
                        if (pool->mUploadingFile == uf)
                        {
                            pool->mUploadingFile = nullptr;
                            pool->mUFTQversion = queueVersion;
                        }

                        pool->purgeFileLocked(uf->fileno());
                        uf->unsetPool();
                    }
                }
            }

            // Retire pinned pools for this URL so we don't keep retrying an expired endpoint.
            for (auto& pool: poolMgr.mPools)
            {
                if (pool && pool->mPinned && pool->mUrl == url)
                {
                    poolMgr.markPoolRetiring(*pool);
                }
            }

            bumpQueueVersion();
        }

        client.wsPostToClientThread(
            [victims, url](MegaClient& client, TransferDbCommitter& committer)
            {
                for (const auto& v: victims)
                {
                    if (!client.wsIsTransferAlive(v.type, v.transfer))
                    {
                        continue;
                    }

                    auto* tp = v.transfer;
                    if (!tp->uploadhandle.eq(v.uploadhandle))
                    {
                        continue;
                    }

                    // This invalidate task runs asynchronously; skip if transfer already switched
                    // to a new session URL, so stale work cannot clear newer session state.
                    if (!tp->ws_session_url.empty() && tp->ws_session_url != url)
                    {
                        continue;
                    }

                    tp->ws_session_url.clear();
                    tp->chunkmacs.clear();
                    tp->pos = 0;
                    tp->setProgresscompleted(0);

                    client.transfercacheadd(tp, &committer);
                    if (client.app)
                    {
                        client.app->transfer_update(tp);
                    }
                }
            });
    }

    void start()
    {
        std::thread stoppedUploadThread;
        std::vector<std::unique_ptr<WsPoolThread>> stoppedPoolThreads;

        {
            std::lock_guard<std::mutex> g(uploadMutex);
            if (uploadThread.joinable())
            {
                if (uploadThreadRunning.load(std::memory_order_acquire))
                    return;

                stoppedUploadThread = std::move(uploadThread);

                for (auto& pool: poolMgr.mPools)
                {
                    if (!pool)
                        continue;

                    for (auto& th: pool->mActiveThreads)
                    {
                        if (th)
                            th->terminate = true;
                        stoppedPoolThreads.push_back(std::move(th));
                    }
                    pool->mActiveThreads.clear();

                    for (auto& th: pool->mExitingThreads)
                    {
                        if (th)
                            th->terminate = true;
                        stoppedPoolThreads.push_back(std::move(th));
                    }
                    pool->mExitingThreads.clear();
                }
            }
        }

        if (stoppedUploadThread.joinable())
            stoppedUploadThread.join();

        // Join worker threads outside uploadMutex to avoid lock-order inversion with WsConn
        // teardown (~WsConn() acquires uploadMutex).
        stoppedPoolThreads.clear();

        std::lock_guard<std::mutex> g(uploadMutex);
        if (uploadThread.joinable())
            return;

        mStopping.store(false, std::memory_order_release);
        paused = false;
        poolMgr.mImpl = this;
        for (auto& pool: poolMgr.mPools)
        {
            if (!pool || pool->mRetiring)
                continue;

            pool->setPoolNumConn(mPoolConnectionLimit);
        }
        poolMgr.refreshPools();

        uploadThread = std::thread(
            [this]
            {
                this->run();
            });
    }

    void kick()
    {
        LOG_debug << "[UploadEngine::Impl::kick] BEGIN [this = " << this << "]";
        std::lock_guard<std::mutex> g(uploadMutex);
        // if (poolMgr.mPools.empty())
        //     poolMgr.refreshPools();
        for (auto& p: poolMgr.mPools)
        {
            LOG_debug << "[UploadEngine::Impl::kick] pool(" << (void*)p.get()
                      << ") checkThreads() [this = " << this << "]";
            p->checkThreads();
        }
        LOG_debug << "[UploadEngine::Impl::kick] END [numPools=" << poolMgr.mPools.size()
                  << "] [this = " << this << "]";
    }

    void notifyNetworkDisconnect()
    {
        disconnectEpoch.fetch_add(1, std::memory_order_release);
    }

    unsigned char poolConnectionLimit() const
    {
        return mPoolConnectionLimit;
    }

    void setMaxConnections(const unsigned char maxConnections)
    {
        const auto newLimit = std::max<unsigned char>(1, maxConnections);

        std::lock_guard<std::mutex> g(uploadMutex);
        mPoolConnectionLimit = newLimit;

        for (auto& pool: poolMgr.mPools)
        {
            if (!pool || pool->mRetiring)
            {
                continue;
            }

            pool->setPoolNumConn(newLimit);
        }
    }

    // Must be called with uploadMutex held.
    bool consumeUploadBudget(const m_off_t bytes, dstime* retryAfterDs = nullptr)
    {
        if (bytes <= 0 || mMaxUploadSpeed <= 0)
        {
            return true;
        }

        const dstime now = SteadyTime::ds();
        if (!mUploadBudgetLastDs)
        {
            mUploadBudgetLastDs = now;
        }

        const dstime elapsedDs = now > mUploadBudgetLastDs ? now - mUploadBudgetLastDs : 0;
        if (elapsedDs > 0)
        {
            const m_off_t maxValue = std::numeric_limits<m_off_t>::max();
            const m_off_t elapsed = static_cast<m_off_t>(elapsedDs);
            const m_off_t scale = static_cast<m_off_t>(SpeedController::DS_PER_SECOND);

            // Avoid overflow if uploading was pending for an unusually long time.
            const m_off_t budgetIncrement =
                (mMaxUploadSpeed > (maxValue / elapsed)) ?
                    maxValue :
                    (mMaxUploadSpeed * elapsed) / scale;
            if (budgetIncrement > (maxValue - mUploadBudget))
            {
                mUploadBudget = maxValue;
            }
            else
            {
                mUploadBudget += budgetIncrement;
            }

            // Keep burst behavior bounded after long idle/pending periods.
            const m_off_t burstWindowSeconds =
                static_cast<m_off_t>(SpeedController::SPEED_MEAN_CIRCULAR_BUFFER_SIZE_SECONDS);
            const m_off_t maxBurstBudget =
                (mMaxUploadSpeed > (maxValue / burstWindowSeconds)) ?
                    maxValue :
                    (mMaxUploadSpeed * burstWindowSeconds);
            const m_off_t budgetCap = std::max(maxBurstBudget, bytes);
            if (mUploadBudget > budgetCap)
            {
                mUploadBudget = budgetCap;
            }
            mUploadBudgetLastDs = now;
        }

        if (mUploadBudget < bytes)
        {
            if (retryAfterDs)
            {
                const m_off_t deficit = bytes - mUploadBudget;
                const m_off_t numerator =
                    deficit * static_cast<m_off_t>(SpeedController::DS_PER_SECOND) + mMaxUploadSpeed - 1;
                const dstime suggestedDs = static_cast<dstime>(numerator / mMaxUploadSpeed);
                *retryAfterDs = std::clamp<dstime>(suggestedDs, 1, 10);
            }
            return false;
        }

        mUploadBudget -= bytes;
        return true;
    }

    void setMaxUploadSpeed(const m_off_t bytesPerSecond)
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        mMaxUploadSpeed = std::max<m_off_t>(bytesPerSecond, 0);
        mUploadBudget = 0;
        mUploadBudgetLastDs = SteadyTime::ds();
    }

    // Called by pools to pick next file that matches [min,max)
    WsUploadFile* nextEligible(const m_off_t min,
                               const m_off_t max,
                               const std::string* requiredSessionUrl,
                               const WsPool* requestingPool)
    {
        LOG_debug << "[UploadEngine::Impl::nextEligible] BEGIN [fileList.size=" << fileList.size()
                  << "] [this = " << this << "]";

        cycleNextIt();

        bool consecutive = true;
        for (auto it = nextIt; it != fileList.end(); ++it)
        {
            WsUploadFile* f = *it;
            if (!f)
            {
                LOG_debug << "[UploadEngine::Impl::nextEligible] !f -> continue [this = " << this
                          << "]";
                continue;
            }

            const bool poolEligible = !f->hasPool() || f->mPool == requestingPool;
            if (poolEligible && !f->paused() && f->continuingUpload(currentTime) &&
                f->hasPendingBytesOrEofToSend())
            {
                LOG_debug << "[UploadEngine::Impl::nextEligible] poolEligible && "
                             "!f->paused() && f->continuingUpload -> process file"
                          << " [this = " << this << "]";
                const auto& hint = f->sessionUrlHint();
                if (requiredSessionUrl)
                {
                    if (hint.empty() || hint != *requiredSessionUrl)
                    {
                        continue;
                    }
                }
                else if (!hint.empty())
                {
                    // Reserved for the pool bound to this specific session URL.
                    continue;
                }

                if (f->size() >= min && (!max || f->size() < max))
                {
                    LOG_debug << "[UploadEngine::Impl::nextEligible] f->size(=" << f->size()
                              << ") >= min(=" << min << ") && (!max(=" << max
                              << ") || f->size(=" << f->size() << ") < max(=" << max
                              << ")) -> process file: setUploadStart(currentTime) and return file "
                                 "[consecutive="
                              << consecutive << "]"
                              << " [this = " << this << "]";
                    if (consecutive)
                    {
                        LOG_debug << "[UploadEngine::Impl::nextEligible] consecutive=true -> "
                                     "advanceNextItFrom(it) before setting upload start [this = "
                                  << this << "]";
                        advanceNextItFrom(it);
                    }
                    f->setUploadStart(currentTime);
                    return f;
                }
                LOG_debug << "[UploadEngine::Impl::nextEligible] !f->size(=" << f->size()
                          << ") >= min(=" << min << ") && (!max(=" << max
                          << ") || f->size(=" << f->size() << ") < max(=" << max
                          << ")) -> no process file, set consecutive=false and continue [this = "
                          << this << "]";

                consecutive = false;
            }
            else
            {
                LOG_debug << "[UploadEngine::Impl::nextEligible] !poolEligible || "
                             "f->paused() || "
                             "!f->continuingUpload -> continue || "
                             "! f->hasPendingBytesOrEofToSend() [this = "
                          << this << "]";
            }
        }
        LOG_debug << "[UploadEngine::Impl::nextEligible] END - return nullptr [this = " << this
                  << "]";
        return nullptr;
    }

    // Manager thread
    void run()
    {
        uploadThreadRunning = true;
        std::unique_lock<std::mutex> lk(uploadMutex);
        while (uploadThreadRunning)
        {
            currentTime = SteadyTime::ds();

            // Pump USC/aux cURL
            poolMgr.curlIO(lk);

            // per-file throughput (server-ack basis)
            for (WsUploadFile* f: poolMgr.mActiveFiles)
                f->maybeReportThroughput(currentTime);
            poolMgr.mActiveFiles.clear();

            if (!paused)
                poolMgr.checkPools(*this);

            cleanupExitedPoolThreads(lk);
        }
        uploadThreadRunning = false;
    }

    // --- state ---
    MegaClient& client;

    using ListWsUploadFile = std::list<WsUploadFile*>;
    ListWsUploadFile fileList;
    std::unordered_map<Transfer*, std::unique_ptr<WsUploadFile>> files;
    std::unordered_set<WsUploadFile*> inQueue;
    std::unordered_map<std::uint32_t, WsUploadFile*> fileByNo;

    ListWsUploadFile::iterator nextIt = fileList.begin();
    std::uint32_t queueVersion{0};
    UploadEngine::Callbacks mCb{};

    WsPoolMgr poolMgr;

    mutable std::mutex uploadMutex;
    std::thread uploadThread;
    std::atomic<bool> uploadThreadRunning{false};
    std::atomic<std::uint64_t> disconnectEpoch{0};
    std::atomic<bool> mStopping{false};

    dstime currentTime{0};
    std::atomic<std::uint32_t> nextFileNo{1};
    unsigned char mPoolConnectionLimit{3};
    m_off_t mMaxUploadSpeed{0};
    m_off_t mUploadBudget{0};
    dstime mUploadBudgetLastDs{0};
    bool paused{false};

private:
    void cleanupExitedPoolThreads(std::unique_lock<std::mutex>& lk)
    {
        std::vector<std::unique_ptr<WsPoolThread>> finished;

        for (auto& pool: poolMgr.mPools)
        {
            if (!pool)
                continue;

            for (std::size_t i = pool->mExitingThreads.size(); i-- > 0;)
            {
                auto& th = pool->mExitingThreads[i];
                if (!th || !th->terminated)
                    continue;

                finished.push_back(std::move(th));
                pool->mExitingThreads.erase(pool->mExitingThreads.begin() +
                                            static_cast<std::ptrdiff_t>(i));
            }
        }

        if (!finished.empty())
        {
            // Destroying WsPoolThread invokes join(); do it without uploadMutex to avoid
            // lock-order inversion with worker-thread teardown (~WsConn() acquires uploadMutex).
            ScopedUnlock unlock(lk);
            finished.clear();
        }
    }

    void bumpQueueVersion()
    {
        ++queueVersion;
        for (auto& pool: poolMgr.mPools)
        {
            if (pool && pool->mUploadingFile && inQueue.count(pool->mUploadingFile))
                pool->mUFTQversion = queueVersion;
        }
    }

    template<class F>
    void withFile(Transfer& t, F&& fn)
    {
        LOG_debug << "[UploadEngine::Impl::withFile] BEGIN [t=" << t.localfilename
                  << "] [this = " << this << "]";
        std::lock_guard<std::mutex> g(uploadMutex);
        auto it = files.find(&t);
        if (it == files.end() || !it->second)
            return;
        fn(*it->second);
        LOG_debug << "[UploadEngine::Impl::withFile] END [this = " << this << "]";
    }

    void cycleNextIt() noexcept
    {
        if (!fileList.empty() && nextIt == fileList.end())
            nextIt = fileList.begin();
    }

    void advanceNextItFrom(ListWsUploadFile::iterator it) noexcept
    {
        nextIt = std::next(it);
        cycleNextIt();
    }
};

// ========== WsConn ==========
WsConn::WsConn(WsPool* pool):
    mPool(pool)
{
    assert(pool);
    assert(pool->mImpl);

    // Protect pool->mConns against concurrent access (e.g. UploadEngine::Impl::remove() iterating it)
    std::lock_guard<std::mutex> lk(pool->mImpl->uploadMutex);
    mConns_it = pool->mConns.insert(this).first;
}

WsConn::~WsConn()
{
    if (mPool && mPool->mImpl)
    {
        std::lock_guard<std::mutex> lk(mPool->mImpl->uploadMutex);
        mPool->mConns.erase(mConns_it);
    }
    if (curl)
    {
        curl_easy_cleanup(curl);
    }
}

/*
bool WsConn::connectWS()
{
    LOG_debug << "[WsConn::connectWS] BEGIN [this = " << this << "]";
    if (curl)
    {
        LOG_debug << "[WsConn::connectWS] curl already exists, cleanup [this = " << this << "]";
        curl_easy_cleanup(curl);
    }
    curl = curl_easy_init();
    if (!curl)
    {
        LOG_debug << "[WsConn::connectWS] curl_easy_init failed, return false [this = " << this
                  << "]";
        return false;
    }

    // Share CurlHttpIO settings but don't attach to its multi
    if (auto* cio = dynamic_cast<CurlHttpIO*>(mPool->mImpl->client.httpio))
    {
        LOG_debug << "[WsConn::connectWS] configureWsEasy [curl=" << (void*)curl << "] [this = " <<
this << "]"; cio->configureWsEasy(curl, false);
    }
    else
    {
        LOG_warn << "[WsConn::connectWS] dynamic_cast<CurlHttpIO*>(mPool->mImpl->client.httpio)
failed, return false [curl=" << (void*)curl << "] [this = " << this << "]"; return false;
    }

    readyState = ReadyState::CONNECTING;

    char err[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, err);

    curl_easy_setopt(curl, CURLOPT_URL, mPool->mUrl.c_str());
    // WebSocket mode
    curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L);

    const CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK)
    {
        LOG_debug << "[WsConn::connectWS] curl_easy_perform success, set readyState=OPEN and call "
                     "onopen() -> return true [this = "
                  << this << "]";
        readyState = ReadyState::OPEN;
        onopen();
        return true;
    }
    LOG_debug << "[WsConn::connectWS] curl_easy_perform failed, set readyState=CLOSED and return "
                 "false [res="
              << res << "] [strError=" << curl_easy_strerror(res) << "] [err=" << err
              << "] [this = " << this << "]";
    readyState = ReadyState::CLOSED;
    return false;
}
*/

bool WsConn::connectWS()
{
    LOG_debug << "[WsConn::connectWS] BEGIN [this = " << this << "]";
    if (mPool->mImpl->stopping())
    {
        readyState = ReadyState::CLOSED;
        return false;
    }
    if (curl)
    {
        LOG_debug << "[WsConn::connectWS] curl already exists, cleanup [this = " << this << "]";
        curl_easy_cleanup(curl);
        curl = nullptr;
    }
    readyState = ReadyState::CONNECTING;

    struct Baton
    {
        std::mutex m;
        std::condition_variable cv;
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easy{nullptr, &curl_easy_cleanup};
        bool done = false;
    };

    auto baton = std::make_shared<Baton>();
    void* self = this;

    const std::string url = mPool->mUrl;

    mPool->mImpl->client.wsPostToClientThread(
        [baton, url, self](MegaClient& client, TransferDbCommitter&)
        {
            LOG_debug << "[WsConn::connectWS] [client.wsPostToClientThread] BEGIN [this = " << self
                      << "]";
            if (auto* engine = client.wsEngine(); !engine || engine->isStopping())
            {
                std::lock_guard<std::mutex> g(baton->m);
                baton->easy.reset();
                baton->done = true;
                baton->cv.notify_one();
                return;
            }
            std::string err;
            CURL* e = static_cast<CURL*>(
                client.wsHandshakeForUpload(url, /*timeoutMs*/ 15000, &err));

            {
                std::lock_guard<std::mutex> g(baton->m);
                baton->easy.reset(e);
                baton->done = true;
                if (e)
                {
                    if (!err.empty())
                    {
                        LOG_debug << "[WsConn::connectWS] [client.wsPostToClientThread] "
                                     "wsHandshakeForUpload failed, err="
                                  << err << ", set baton.easy=" << (void*)baton->easy.get()
                                  << " [this = " << self
                                  << "]";
                    }
                    else
                    {
                        LOG_debug << "[WsConn::connectWS] [client.wsPostToClientThread] "
                                     "wsHandshakeForUpload success, set baton.easy="
                                  << (void*)baton->easy.get() << " [this = " << self << "]";
                    }
                }
                else
                {
                    LOG_debug << "[WsConn::connectWS] [client.wsPostToClientThread] "
                                 "wsHandshakeForUpload returned nullptr, set baton.easy=nullptr [this = "
                              << self << "]";
                }
            }
            baton->cv.notify_one();
            LOG_debug << "[WsConn::connectWS] [client.wsPostToClientThread] END [this = " << self
                      << "]";
        });

    // Wait here on the worker thread until one of the following happens:
    //  1) handshake completes (success or failure including handshake timeout);
    //  2) WS engine stop is requested (stopping());
    //  3) local 20s watchdog deadline is reached.
    std::unique_lock<std::mutex> lk(baton->m);
    const auto waitDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!baton->done)
    {
        if (mPool->mImpl->stopping())
        {
            readyState = ReadyState::CLOSED;
            return false;
        }

        if (std::chrono::steady_clock::now() >= waitDeadline)
        {
            LOG_debug << "[WsConn::connectWS] handshake baton timed out -> readyState=CLOSED and "
                         "return false [this = "
                      << this << "]";
            readyState = ReadyState::CLOSED;
            return false;
        }

        baton->cv.wait_for(lk, std::chrono::milliseconds(200), [&]
                           {
                               return baton->done;
                           });
    }

    if (!baton->easy)
    {
        LOG_debug
            << "[WsConn::connectWS] !baton.easy -> readyState=CLOSED and return false [this = "
            << this << "]";
        readyState = ReadyState::CLOSED;
        return false;
    }

    curl = baton->easy.release(); // worker thread exclusively owns the handle now
    readyState = ReadyState::OPEN;
    onopen(); // your existing callback
    LOG_debug << "[WsConn::connectWS] END -> success, return true [this = " << this << "]";
    return true;
}

void WsConn::closeWS()
{
    LOG_debug << "[WsConn::closeWS] BEGIN [this = " << this << "]";
    if (readyState == ReadyState::CLOSED)
    {
        LOG_debug << "[WsConn::closeWS] readyState=CLOSED, return [this = " << this << "]";
        return;
    }
    readyState = ReadyState::CLOSED;
    onclose();
    LOG_debug << "[WsConn::closeWS] END [this = " << this << "]";
}

void WsConn::onopen()
{
    LOG_debug << "[WsConn::onopen] Connected to " << mPool->mUrl;
}

void WsConn::onclose()
{
    LOG_debug << "[WsConn::onclose] BEGIN [Disconnected from " << mPool->mUrl
              << "] [this = " << this << "]";
    if (mPool)
    {
        LOG_debug << "[WsConn::onclose] mPool->retryChunksOnTheWire(this) [this = " << this << "]";
        mPool->retryChunksOnTheWire(this);
    }
    LOG_debug << "[WsConn::onclose] END [this = " << this << "]";
}

void WsConn::curlSend()
{
    LOG_debug << "[WsConn::curlSend] BEGIN [readyState=" << static_cast<int>(readyState)
              << "] [this = " << this << "]";
    if (readyState != ReadyState::OPEN)
    {
        LOG_debug << "[WsConn::curlSend] readyState != ReadyState::OPEN, return [this = " << this
                  << "]";
        return;
    }
    while (mBufs[static_cast<unsigned char>(mCurBuf)].sendWS(this, bufferedAmount))
        mCurBuf = !mCurBuf;
    LOG_debug << "[WsConn::curlSend] END [this = " << this << "]";
}

void WsConn::curlRecv()
{
    LOG_debug << "[WsConn::curlRecv] BEGIN [readyState=" << static_cast<int>(readyState)
              << "] [this = " << this << "]";
    if (readyState != ReadyState::OPEN)
    {
        LOG_debug << "[WsConn::curlRecv] readyState != ReadyState::OPEN, return [this = " << this
                  << "]";
        return;
    }

    const struct curl_ws_frame* meta = nullptr;
    size_t recv = 0;

    for (;;)
    {
        LOG_debug << "[WsConn::curlRecv] curl_ws_recv(curl, mInBuf(=" << (void*)mInBuf
                  << "), sizeof(mInBuf)(=" << sizeof(mInBuf) << "), &recv, &meta) [this = " << this
                  << "]";
        const CURLcode res = curl_ws_recv(curl, mInBuf, sizeof(mInBuf), &recv, &meta);
        if (res == CURLE_OK && meta && !meta->bytesleft && recv > 0)
        {
            LOG_debug << "[WsConn::curlRecv] res == CURLE_OK && meta && !meta->bytesleft && recv(="
                      << recv
                      << ") > 0 -> onmessage(mInBuf, static_cast<int>(recv)) [this = " << this
                      << "]";
            onmessage(mInBuf, static_cast<int>(recv));
        }
        else
        {
            if (res != CURLE_AGAIN || meta)
            {
                LOG_debug << "[WsConn::curlRecv] res(=" << res
                          << ") != CURLE_AGAIN || meta -> closeWS() [this = " << this << "]";
                closeWS();
            }
            LOG_debug << "[WsConn::curlRecv] res(=" << res
                      << ") != CURLE_OK || !meta || meta->bytesleft(="
                      << (meta ? meta->bytesleft : 0) << ") > 0 || recv(=" << recv
                      << ") <= 0 -> break [this = " << this << "]";
            break;
        }
    }
    LOG_debug << "[WsConn::curlRecv] END [this = " << this << "]";
}

void WsConn::onmessage(const char* msg, const int len)
{
    LOG_debug << "[WsConn::onmessage] BEGIN [len=" << len << "] [this = " << this << "]";
    switch (detail::validateInboundFrame(msg, len))
    {
        case detail::InboundFrameValidationResult::TooShort:
            LOG_warn << "WsUpload: invalid server msg len=" << len;
            closeWS();
            return;

        case detail::InboundFrameValidationResult::BadCrc:
            LOG_warn << "WsUpload: inbound CRC failed, byteLength=" << len;
            closeWS();
            return;

        case detail::InboundFrameValidationResult::Ok:
            break;
    }

    // From here on we must protect pool/file/transfer state with the upload mutex.
    std::lock_guard<std::mutex> lk(mPool->mImpl->uploadMutex);

    mPool->mLastActive = mPool->mImpl->currentTime;
    mPool->mLastServerResponse = mPool->mImpl->currentTime;
    mPool->mImpl->poolMgr.bumpLastNetRead(mPool->mImpl->currentTime);

#pragma pack(push, 1)

struct ChunkResponse
{
    std::uint32_t fileno;
    m_off_t chunkpos;
    signed char event;
};

#pragma pack(pop)

    // WS completion frame layout: [ChunkResponse][payloadLength:1][payload:N][crc32:4]    
    constexpr int kWsChunkResponseHeaderSize = sizeof(ChunkResponse);
    static_assert(kWsChunkResponseHeaderSize ==
                    static_cast<std::size_t>(detail::kInboundChunkResponseBytes),
                "ChunkResponse doesn't match detail::kInboundChunkResponseBytes");

    const auto* response = reinterpret_cast<const ChunkResponse*>(msg);
    const auto event = static_cast<WsApiServerEvent>(response->event);
    LOG_debug << "[WsConn::onmessage] response->fileno=" << response->fileno
              << " response->chunkpos=" << response->chunkpos
              << " response->event=" << static_cast<int>(event) << " [this = " << this << "]";
    WsChunk chunk;

    WsUploadFile* uf = mPool->findFile(response->fileno, *mPool->mImpl);
    if (!uf)
    {
        LOG_debug << "[WsConn::onmessage] !uf -> return [this = " << this << "]";
        return; // file cancelled or moved
    }

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    bool dropServerEvent = false;
    DEBUG_TEST_HOOK_WSUPLOAD_DROP_SERVER_EVENT(response->fileno,
                                               static_cast<int>(event),
                                               dropServerEvent);
    if (dropServerEvent)
    {
        LOG_warn << "WsUpload: debug hook dropped server event=" << static_cast<int>(event)
                 << " fileno=" << response->fileno;
        return;
    }
#endif

    if (event < WsApiServerEvent::UploadCompleted || event == WsApiServerEvent::FinalDataIngested)
    {
        if (static_cast<int>(event) < 0)
        {
            LOG_debug << "[WsConn::onmessage] response->event < 0 -> "
                         "uf->uploadFailed(FailReason::ServerError) [this = "
                      << this << "]";
            // A server-side error aborts the current upload attempt.
            // Purge in-flight/resend state for this file before unbinding it from the pool.
            mPool->purgeFileLocked(response->fileno);
            uf->uploadFailed(FailReason::ServerError);
            if (mPool->mImpl->mCb.onFail)
            {
                const int apierr = static_cast<int>(event);
                const m_off_t aux = response->chunkpos;
                mPool->mImpl->mCb.onFail(uf->transfer(),
                                         apierr,
                                         aux,
                                         UploadEngine::FailureDisposition::Retryable);
            }
            return;
        }

        LOG_debug << "[WsConn::onmessage] response->event >= 0 -> chunk.pos = -1 [this = " << this
                  << "]";
        chunk.pos = -1;
        const bool shouldApply = event == WsApiServerEvent::ChunkIngested ||
                                 event == WsApiServerEvent::AlreadyOnServer ||
                                 event == WsApiServerEvent::FinalDataIngested;
        for (auto it = mChunksInFlight.begin(); it != mChunksInFlight.end(); ++it)
        {
            if (it->first.pos == response->chunkpos && it->first.fileno == response->fileno)
            {
                chunk = it->first;
                if (shouldApply)
                    it->second.apply(chunk.pos, *uf);
                mChunksInFlight.erase(it);
                mPool->mNumChunksInFlight--;
                break;
            }
        }
        if (chunk.pos < 0)
        {
            LOG_warn << "WsUpload: PROTOCOL - acked chunk not in-flight [pos=" << response->chunkpos
                     << " fileno=" << response->fileno
                     << " type=" << static_cast<int>(event) << "]";
            return;
        }
    }

    if (len == kWsChunkResponseHeaderSize)
    {
        LOG_debug << "[WsConn::onmessage] len == kWsChunkResponseHeaderSize -> "
                     "uf->uploadFailed(FailReason::Unknown) [this = "
                  << this << "]";
        // Unknown/invalid server response for this upload attempt.
        mPool->purgeFileLocked(response->fileno);
        uf->uploadFailed(FailReason::Unknown);
        if (mPool->mImpl->mCb.onFail)
            mPool->mImpl->mCb.onFail(uf->transfer(),
                                     API_EAGAIN,
                                     0,
                                     UploadEngine::FailureDisposition::Retryable);
        return;
    }

    switch (event)
    {
        case WsApiServerEvent::ChunkIngested: // non-final
            LOG_debug
                << "[WsConn::onmessage] response->event == 1 chunk ingested (non-final) [chunk.len="
                << chunk.len << "] [this = " << this << "]";
            if (chunk.len)
            {
                uf->onServerConfirmedBytes(chunk.len);
                if (mPool->mImpl->mCb.onProgress && uf->progressReportDue(SteadyTime::ds()))
                    mPool->mImpl->mCb.onProgress(uf->transfer(), uf->bytesConfirmed());
                if (uf->bytesConfirmed() > uf->size())
                {
                    LOG_warn
                        << "[WsConn::onmessage] uf->bytesConfirmed(=" << uf->bytesConfirmed()
                        << ") > uf->size(=" << uf->size()
                        << ") -> server confirmed beyond expected size, failing upload [this = "
                        << this << "]";
                    mPool->purgeFileLocked(response->fileno);
                    uf->uploadFailed(FailReason::StateLost);
                    if (mPool->mImpl->mCb.onFail)
                        mPool->mImpl->mCb.onFail(uf->transfer(),
                                                 API_EINTERNAL,
                                                 uf->bytesConfirmed(),
                                                 UploadEngine::FailureDisposition::Retryable);
                    break;
                }
            }
            mPool->mImpl->poolMgr.mActiveFiles.insert(uf);
            break;

        case WsApiServerEvent::FinalDataIngested:
            LOG_debug << "[WsConn::onmessage] response->event == 7 final data ingested (server "
                         "knows file is complete) [chunk.len="
                      << chunk.len << "] [this = " << this << "]";
            if (chunk.len)
            {
                uf->onServerConfirmedBytes(chunk.len);
                if (uf->bytesConfirmed() > uf->size())
                {
                    LOG_warn
                        << "[WsConn::onmessage] uf->bytesConfirmed(=" << uf->bytesConfirmed()
                        << ") > uf->size(=" << uf->size()
                        << ") -> server confirmed beyond expected size, failing upload [this = "
                        << this << "]";
                    mPool->purgeFileLocked(response->fileno);
                    uf->uploadFailed(FailReason::StateLost);
                    if (mPool->mImpl->mCb.onFail)
                        mPool->mImpl->mCb.onFail(uf->transfer(),
                                                 API_EINTERNAL,
                                                 uf->bytesConfirmed(),
                                                 UploadEngine::FailureDisposition::Retryable);
                    break;
                }
            }
            if (mPool->mImpl->mCb.onProgress)
                mPool->mImpl->mCb.onProgress(uf->transfer(), uf->bytesConfirmed());
            mPool->mImpl->poolMgr.mActiveFiles.insert(uf);
            break;

        case WsApiServerEvent::AlreadyOnServer:
            LOG_debug << "[WsConn::onmessage] response->event == 2 already on server (after "
                         "reconnect) [pos="
                      << chunk.pos << "] [len=" << chunk.len << "] [fileno=" << chunk.fileno
                      << "] [this = " << this << "]";
            if (chunk.len)
            {
                uf->onServerConfirmedBytes(chunk.len);
                if (mPool->mImpl->mCb.onProgress && uf->progressReportDue(SteadyTime::ds()))
                    mPool->mImpl->mCb.onProgress(uf->transfer(), uf->bytesConfirmed());
                if (uf->bytesConfirmed() > uf->size())
                {
                    LOG_warn << "[WsConn::onmessage] bytesConfirmed=" << uf->bytesConfirmed()
                             << " > uf->size()=" << uf->size()
                             << " -> uf->uploadFailed(StateLost) [this = " << this << "]";
                    mPool->purgeFileLocked(response->fileno);
                    uf->uploadFailed(FailReason::StateLost);
                    if (mPool->mImpl->mCb.onFail)
                        mPool->mImpl->mCb.onFail(uf->transfer(),
                                                 API_EINTERNAL,
                                                 uf->bytesConfirmed(),
                                                 UploadEngine::FailureDisposition::Retryable);
                    break;
                }
            }
            mPool->mImpl->poolMgr.mActiveFiles.insert(uf);
            break;

        case WsApiServerEvent::CrcFailed:
            LOG_warn << "[WsConn::onmessage] response->event == 3 CRC failed -> "
                        "mPool->retryChunk(chunk) [this = "
                     << this << "]";
            uf->onRequestFailed();
            mPool->retryChunkLocked(chunk);
            break;

        case WsApiServerEvent::UploadCompleted:
        {
            LOG_debug << "[WsConn::onmessage] response->event == 4 upload completed -> "
                         "mPool->applyInFlight(response->fileno) [this = "
                      << this << "]";
            mPool->applyInFlightLocked(response->fileno);
            uf->maybeReportThroughput(mPool->mImpl->currentTime);

            // Completion frame layout is:
            // [ChunkResponse (13 bytes)] [payloadLen (1 byte)] [payload (N bytes)] [crc32 (4 bytes)]
            // Validate boundaries before reading payloadLen/payload.
            constexpr int kWsCompletionPayloadOffset =
                kWsChunkResponseHeaderSize + static_cast<int>(sizeof(std::uint8_t));
            constexpr int kCompletionPrefixLen = kWsCompletionPayloadOffset;
            constexpr int kTrailerCrcLen = detail::kInboundFrameTrailerCrcBytes;
            if (len < (kCompletionPrefixLen + kTrailerCrcLen))
            {
                LOG_warn << "WsUpload: invalid completion frame len=" << len;
                mPool->purgeFileLocked(response->fileno);
                uf->uploadFailed(FailReason::Protocol);
                if (mPool->mImpl->mCb.onFail)
                {
                    mPool->mImpl->mCb.onFail(uf->transfer(),
                                             API_EINTERNAL,
                                             0,
                                             UploadEngine::FailureDisposition::Retryable);
                }
                break;
            }

            const int payLen = static_cast<unsigned char>(msg[kWsChunkResponseHeaderSize]);
            const int maxPayloadLen = len - kCompletionPrefixLen - kTrailerCrcLen;
            if (payLen > maxPayloadLen)
            {
                LOG_warn << "WsUpload: invalid completion payload len=" << payLen
                         << " frame len=" << len;
                mPool->purgeFileLocked(response->fileno);
                uf->uploadFailed(FailReason::Protocol);
                if (mPool->mImpl->mCb.onFail)
                {
                    mPool->mImpl->mCb.onFail(uf->transfer(),
                                             API_EINTERNAL,
                                             0,
                                             UploadEngine::FailureDisposition::Retryable);
                }
                break;
            }

            uf->uploadCompleted(msg + kWsCompletionPayloadOffset, payLen);
            if (mPool->mImpl->mCb.onComplete)
            {
                const char* payload = (payLen > 0) ? (msg + kWsCompletionPayloadOffset) : nullptr;
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
                // Test seam: allow integration tests to corrupt the upload-token
                // payload to exercise the invalid-token detach path in
                // MegaClient::onComplete.
                std::string payloadStr(payload ? payload : "",
                                       payload ? static_cast<size_t>(payLen) : 0u);
                int hookPayLen = payLen;
                DEBUG_TEST_HOOK_WSUPLOAD_CORRUPT_TOKEN(uf->fileno(), payloadStr, hookPayLen);
                mPool->mImpl->mCb.onComplete(uf->transfer(), payloadStr.data(), hookPayLen);
#else
                mPool->mImpl->mCb.onComplete(uf->transfer(), payload, payLen);
#endif
            }
            break;
        }

        case WsApiServerEvent::Distress:
            LOG_warn
                << "[WsConn::onmessage] response->event == 5 distress -> server requested pool "
                   "refresh -> refresh pools -> mPool->mImpl->poolMgr.refreshPools() [this = "
                << this << "]";
            mPool->mImpl->poolMgr.refreshPools();
            break;

        case WsApiServerEvent::Throttle:
            LOG_debug
                << "[WsConn::onmessage] response->event == 6 throttle (ms) -> ds -> "
                   "mPool->pauseSending(static_cast<dstime>(response->chunkpos / 100 + 1)) [this = "
                << this << "]";
            mPool->pauseSending(static_cast<dstime>(response->chunkpos / 100 + 1));
            break;

        default:
            LOG_debug << "[WsConn::onmessage] response->event == "
                      << static_cast<int>(event)
                      << " -> unknown server opcode=" << static_cast<int>(event)
                      << " -> break [this = " << this << "]";
            break;
    }
    LOG_debug << "[WsConn::onmessage] END [this = " << this << "]";
}

void WsConn::sendChunkData(const std::uint32_t fileno,
                           const m_off_t pos,
                           const char* data,
                           const int len)
{
    LOG_debug << "[WsConn::sendChunkData] BEGIN [fileno=" << fileno << "] [pos=" << pos
              << "] [len=" << len << "] [this = " << this << "]";
    ChunkHeader header;
    header.fileno = fileno;
    header.pos = pos;
    header.len = len;
    header.crc = CRC32::crc32b(data,
                               len,
                               CRC32::crc32b(reinterpret_cast<const char*>(&header),
                                             static_cast<int>(offsetof(ChunkHeader, crc))));

    char bufIdx = mCurBuf;
    if (mBufs[static_cast<unsigned char>(bufIdx)].mDataLen)
        bufIdx = !bufIdx;

    LOG_debug << "[WsConn::sendChunkData] senddata(bufIdx, reinterpret_cast<const char*>(&header), "
                 "static_cast<int>(sizeof header)) [bufIdx="
              << static_cast<int>(bufIdx) << "] [this = " << this << "]";
    senddata(bufIdx, reinterpret_cast<const char*>(&header), static_cast<int>(sizeof header));
    LOG_debug << "[WsConn::sendChunkData] senddata(bufIdx, data, len) [bufIdx="
              << static_cast<int>(bufIdx) << "] [this = " << this << "]";
    senddata(bufIdx, data, len);
    LOG_debug << "[WsConn::sendChunkData] END [this = " << this << "]";
}

// ========== WsPoolThread (ctor after WsPool complete) ==========
WsPoolThread::WsPoolThread(WsPool* pool):
    t(&WsPool::poolWorkerThread, pool, this)
{
    LOG_debug << "[WsPoolThread::WsPoolThread] pool=" << (void*)pool << " [this = " << this << "]";
}

// ========== WsBuf ==========
bool WsBuf::sendWS(WsConn* ws, int& bufferedAmount)
{
    LOG_debug << "[WsBuf::sendWS] BEGIN [this = " << this << "]";
    if (mSendPos >= mDataLen)
    {
        LOG_debug << "[WsBuf::sendWS] mSendPos(=" << mSendPos << ") >= mDataLen(=" << mDataLen
                  << ") -> return false [this = " << this << "]";
        return false;
    }

    size_t sent = 0;
    LOG_debug << "[WsBuf::sendWS] curl_ws_send(ws->curl, buf(=" << (void*)buf
              << ") + mSendPos(=" << mSendPos << ") = " << (void*)(buf + mSendPos)
              << ", mDataLen(=" << mDataLen << ") - mSendPos(=" << mSendPos
              << ") = " << (mDataLen - mSendPos) << ", &sent, 0, CURLWS_BINARY) [this = " << this
              << "]";
    const std::size_t remaining = static_cast<std::size_t>(mDataLen - mSendPos);
    const CURLcode res = curl_ws_send(ws->curl, buf + mSendPos, remaining, &sent, 0, CURLWS_BINARY);
    if (res == CURLE_OK)
    {
        LOG_debug << "[WsBuf::sendWS] res == CURLE_OK -> mSendPos(=" << mSendPos
                  << ") += static_cast<int>(sent(=" << sent
                  << ")), bufferedAmount(=" << bufferedAmount
                  << ") -= static_cast<int>(sent(=" << sent << ")) [this = " << this << "]";
        mSendPos += static_cast<int>(sent);
        bufferedAmount -= static_cast<int>(sent);
        if (mSendPos == mDataLen)
        {
            LOG_debug << "[WsBuf::sendWS] mSendPos(=" << mSendPos << ") == mDataLen(=" << mDataLen
                      << ") -> reset() && return true [this = " << this << "]";
            reset();
            return true;
        }
        LOG_debug << "[WsBuf::sendWS] mSendPos(=" << mSendPos << ") != mDataLen(=" << mDataLen
                  << ") -> return false [this = " << this << "]";
        return false;
    }
    if (res != CURLE_AGAIN)
    {
        LOG_debug << "[WsBuf::sendWS] res(=" << res << ") != CURLE_AGAIN(=" << CURLE_AGAIN
                  << ") -> ws->closeWS() [this = " << this << "]";
        ws->closeWS();
    }
    LOG_debug << "[WsBuf::sendWS] res(=" << res << ") != CURLE_OK -> return false [this = " << this
              << "]";
    return false;
}

// ========== WsPool ==========
bool WsPool::getWsUploadFile(const dstime now, UploadEngine::Impl& impl)
{
    if (mUploadingFile && !mUploadingFile->paused() && mUploadingFile->continuingUpload(now) &&
        mUploadingFile->hasPendingBytesOrEofToSend() && mUFTQversion == impl.queueVersion)
    {
        LOG_debug << "[WsPool::getWsUploadFile] mUploadingFile->paused()=false && "
                     "mUploadingFile->continuingUpload(now) && mUFTQversion(="
                  << mUFTQversion << ") == impl.queueVersion(=" << impl.queueVersion
                  << ") -> return true [this = " << this << "]";
        return true;
    }

    if (mRetiring)
    {
        LOG_debug << "[WsPool::getWsUploadFile] mRetiring=true -> return false [this = " << this
                  << "]";
        return false;
    }

    if (auto* f = impl.nextEligible(mMinFileSize, mMaxFileSize, mPinned ? &mUrl : nullptr, this))
    {
        LOG_debug << "[WsPool::getWsUploadFile] BEGIN -> auto* f = impl.nextEligible(mMinFileSize(="
                  << mMinFileSize << "), mMaxFileSize(=" << mMaxFileSize << "))  [this = " << this
                  << "]";
        // Back‑pressure gate: defer starting a new file while FA pipeline is saturated
        if (impl.mCb.canStartAnotherFile && !impl.mCb.canStartAnotherFile())
        {
            LOG_debug << "[WsPool::getWsUploadFile] impl.mCb.canStartAnotherFile=true && "
                         "!impl.mCb.canStartAnotherFile() -> return false [this = "
                      << this << "]";
            return false;
        }

        // Preflight: let MegaClient run its legacy "prep" (FA scheduling/metadata etc).
        // If it returns false, keep the file queued and try again later.
        if (impl.mCb.preflightStart && !impl.mCb.preflightStart(f->transfer()))
        {
            LOG_debug << "[WsPool::getWsUploadFile] impl.mCb.preflightStart=true && "
                         "!impl.mCb.preflightStart(f->transfer()) -> return false [this = "
                      << this << "]";
            return false;
        }

        // Option C: snapshot write-once crypto material now that preflightStart has
        // guaranteed f->transfer().transferkey and ctriv are populated.
        f->snapshotCryptoMaterial(f->transfer());

        if (mUploadingFile != f)
        {
            LOG_debug << "[WsPool::getWsUploadFile] mUploadingFile(=" << (void*)mUploadingFile
                      << ") != f(=" << (void*)f << ") -> picking file fileno=" << f->fileno()
                      << " for [" << mMinFileSize << "," << mMaxFileSize
                      << ") pool and set mUFTQversion(=" << impl.queueVersion
                      << ") = impl.queueVersion(=" << impl.queueVersion << ") [this = " << this
                      << "]";
            mUploadingFile = f;
            mUFTQversion = impl.queueVersion;
            mUploadingFile->setPool(*this);
            if (impl.mCb.onStart)
                impl.mCb.onStart(mUploadingFile->transfer());
        }
        else
        {
            LOG_debug << "[WsPool::getWsUploadFile] mUploadingFile(=" << (void*)mUploadingFile
                      << ") == f(=" << (void*)f << ") -> return true [this = " << this << "]";
        }
        return true;
    }
    LOG_debug << "[WsPool::getWsUploadFile] !f -> return false [this = " << this << "]";
    return false;
}

WsUploadFile* WsPool::findFile(const std::uint32_t fileno, UploadEngine::Impl& impl)
{
    if (mUploadingFile && mUploadingFile->fileno() == fileno)
        return (mUploadingFile->mPool == this) ? mUploadingFile : nullptr;

    auto it = impl.fileByNo.find(fileno);
    if (it == impl.fileByNo.end())
        return nullptr;
    return (it->second && it->second->mPool == this) ? it->second : nullptr;
}

bool WsPool::nextChunk(WsChunk& chunk, UploadEngine::Impl& impl, dstime* retryAfterDs)
{
    LOG_debug << "[WsPool::nextChunk] BEGIN [this = " << this << "]";
    // queued retry first
    while (!mToResend.empty())
    {
        chunk = mToResend.front();
        if (!impl.consumeUploadBudget(static_cast<m_off_t>(chunk.len), retryAfterDs))
        {
            return false;
        }
        mToResend.erase(mToResend.begin());
        LOG_debug << "WsUpload: resending chunk pos=" << chunk.pos << " len=" << chunk.len
                  << " fileno=" << chunk.fileno;
        if (findFile(chunk.fileno, impl))
            return true; // file still valid?
    }

    if (impl.paused)
    {
        LOG_debug << "[WsPool::nextChunk] impl.paused=true -> return false [this = " << this << "]";
        return false;
    }

    while (getWsUploadFile(impl.currentTime, impl))
    {
        if (mUploadingFile->headPos() < mUploadingFile->size() || !mUploadingFile->eofSet())
        {
            LOG_debug << "[WsPool::nextChunk] mUploadingFile->headPos(="
                      << mUploadingFile->headPos()
                      << ") < mUploadingFile->size(=" << mUploadingFile->size()
                      << ") || !mUploadingFile->eofSet(=" << mUploadingFile->eofSet()
                      << ") -> process chunk"
                      << " [this = " << this << "]";
            chunk.fileno = mUploadingFile->fileno();

            if (mUploadingFile->headPos() == mUploadingFile->size())
            {
                // empty EOF chunk
                mUploadingFile->markEOF();
                chunk.pos = mUploadingFile->headPos();
                chunk.len = 0;
            }
            else
            {
                chunk.pos = mUploadingFile->headPos();

                const int advance = g_chunkMap.chunksize(chunk.pos);
                m_off_t newHead = chunk.pos + advance;

                if (newHead > mUploadingFile->size())
                {
                    newHead = mUploadingFile->size();
                    mUploadingFile->markEOF();
                }

                chunk.len = static_cast<int>(newHead - chunk.pos);
                if (!impl.consumeUploadBudget(static_cast<m_off_t>(chunk.len), retryAfterDs))
                {
                    return false;
                }
                mUploadingFile->advanceHead(chunk.len);
            }

            mLastActive = impl.currentTime;
            return true;
        }
        LOG_debug << "[WsPool::nextChunk] mUploadingFile->headPos() < mUploadingFile->size() || "
                     "!mUploadingFile->eofSet() -> mUploadingFile = nullptr [this = "
                  << this << "]";
        mUploadingFile = nullptr; // done with this file
    }
    LOG_debug << "[WsPool::nextChunk] END - !getWsUploadFile -> return false [this = " << this
              << "]";
    return false;
}

void WsPool::retryChunksOnTheWire(WsConn* ws)
{
    LOG_debug << "WsUpload: WS to " << mUrl << " lost; rescheduling " << ws->mChunksInFlight.size()
              << " in-flight chunks";
    std::lock_guard<std::mutex> g(mImpl->uploadMutex);
    for (auto& p: ws->mChunksInFlight)
    {
        mToResend.push_back(p.first);
    }
    mNumChunksInFlight -= static_cast<int>(ws->mChunksInFlight.size());
    ws->mChunksInFlight.clear();
}

void WsPool::retryChunkLocked(const WsChunk& chunk)
{
    mToResend.push_back(chunk);
}

void WsPool::retryChunk(const WsChunk& chunk)
{
    std::lock_guard<std::mutex> g(mImpl->uploadMutex);
    retryChunkLocked(chunk);
}

WsUploadFile* WsPool::handshakeFailureCandidateLocked(const dstime now) const
{
    auto eligibleInPool = [this, now](WsUploadFile* f)
    {
        return f && f->mPool == this && !f->paused() && f->continuingUpload(now);
    };

    if (eligibleInPool(mUploadingFile))
    {
        return mUploadingFile;
    }

    // mUploadingFile can temporarily be null while the same pool still owns a resumable upload.
    for (const auto& kv: mImpl->fileByNo)
    {
        if (eligibleInPool(kv.second))
        {
            return kv.second;
        }
    }

    // Fallback: a transfer can be briefly unbound from this pool while still pinned to this
    // session URL hint during reconnect/refresh transitions.
    auto eligibleByHint = [this, now](WsUploadFile* f)
    {
        return f && !f->hasPool() && f->sessionUrlHint() == mUrl && !f->paused() &&
               f->continuingUpload(now);
    };
    for (const auto& kv: mImpl->fileByNo)
    {
        if (eligibleByHint(kv.second))
        {
            return kv.second;
        }
    }

    return nullptr;
}

void WsPool::purgeFileLocked(const std::uint32_t fileno)
{
    // Drop any queued resends for this file.
    for (auto it = mToResend.begin(); it != mToResend.end();)
    {
        if (it->fileno == fileno)
        {
            it = mToResend.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Drop any in-flight records for this file.
    for (auto& conn: mConns)
    {
        if (!conn)
            continue;
        for (auto it = conn->mChunksInFlight.begin(); it != conn->mChunksInFlight.end();)
        {
            if (it->first.fileno == fileno)
            {
                it = conn->mChunksInFlight.erase(it);
                if (mNumChunksInFlight > 0)
                    --mNumChunksInFlight;
            }
            else
            {
                ++it;
            }
        }
    }
}

void WsPool::applyInFlightLocked(const std::uint32_t fileno)
{
    WsUploadFile* uf = findFile(fileno, *mImpl);
    for (auto& conn: mConns)
    {
        for (auto it = conn->mChunksInFlight.begin(); it != conn->mChunksInFlight.end();)
        {
            if (it->first.fileno == fileno)
            {
                if (uf)
                {
                    it->second.apply(it->first.pos, *uf);
                }
                it = conn->mChunksInFlight.erase(it);
                mNumChunksInFlight--;
            }
            else
            {
                ++it;
            }
        }
    }
}

void WsPool::applyInFlight(const std::uint32_t fileno)
{
    std::lock_guard<std::mutex> g(mImpl->uploadMutex);
    applyInFlightLocked(fileno);
}

bool WsPool::sendChunk(WsConn* ws, UploadEngine::Impl& impl, dstime* retryAfterDs)
{
    if (ws->readyState != WsConn::ReadyState::OPEN || !ws->haveSpace())
    {
        LOG_debug << "[WsPool::sendChunk] ws->readyState=" << static_cast<int>(ws->readyState)
                  << " (ReadyState::OPEN=" << static_cast<int>(WsConn::ReadyState::OPEN)
                  << ") ws->haveSpace=" << ws->haveSpace() << " -> return false [this = " << this
                  << "]";
        return false;
    }

    WsChunk chunk;
    if (!nextChunk(chunk, impl, retryAfterDs))
    {
        LOG_debug << "[WsPool::sendChunk] !nextChunk -> return false [this = " << this << "]";
        return false;
    }

    static thread_local std::unique_ptr<char[]> tlsBuf;
    if (!tlsBuf)
    {
        LOG_debug << "[WsPool::sendChunk] !tlsBuf -> tlsBuf.reset(new char[MB]) [this = " << this
                  << "]";
        tlsBuf.reset(new char[MB]);
    }

    WsUploadFile* uf = findFile(chunk.fileno, impl);
    const bool okRead =
        uf && !uf->aborted() &&
        (chunk.len == 0 || uf->readData(tlsBuf.get(), chunk.pos, chunk.len, impl.uploadMutex));
    if (okRead)
    {
        uf = findFile(chunk.fileno, impl);
        if (!uf)
            return false;
        if (!uf->inPool() || uf->aborted())
        {
            LOG_debug << "[WsPool::sendChunk] drop chunk after read (no longer in pool or aborted) "
                         "[pos="
                      << chunk.pos << "] [len=" << chunk.len << "] [fileno=" << chunk.fileno << "]";
            return false;
        }
        if (uf->paused())
        {
            LOG_debug << "[WsPool::sendChunk] requeue chunk after read (paused) [pos=" << chunk.pos
                      << "] [len=" << chunk.len << "] [fileno=" << chunk.fileno << "]";
            mToResend.push_back(chunk);
            return false;
        }

        ChunkFingerprintMacUpdate update(chunk.pos);
        if (chunk.len)
        {
            const unsigned pad =
                static_cast<unsigned>((-(int)chunk.len) & (SymmCipher::BLOCKSIZE - 1));
            if (pad)
                std::memset(tlsBuf.get() + chunk.len, 0, pad);

            thread_local std::unique_ptr<SymmCipher> tlsCipher;
            if (!tlsCipher)
                tlsCipher.reset(new SymmCipher(uf->transferKey().data()));
            else
                tlsCipher->setkey(uf->transferKey().data());

            chunkmac_map macs;
            macs.ctr_encrypt(chunk.pos,
                             tlsCipher.get(),
                             reinterpret_cast<byte*>(tlsBuf.get()),
                             static_cast<unsigned>(chunk.len),
                             chunk.pos,
                             uf->ctrIv(),
                             false);
            update = ChunkFingerprintMacUpdate(chunk.pos, std::move(macs));
        }
        LOG_debug << "[WsPool::sendChunk] uf->readData(tlsBuf.get(), chunk.pos, chunk.len, "
                     "impl.uploadMutex) -> emplace mChunksInFlight, sendChunkData && return true "
                     "[mNumChunksInFlight="
                  << mNumChunksInFlight << "] [this = " << this << "]";
        ws->mChunksInFlight.emplace_back(chunk, std::move(update));
        uf->onRequestSent();
        ws->sendChunkData(chunk.fileno, chunk.pos, tlsBuf.get(), chunk.len);

        ++mNumChunksInFlight;
        return true;
    }
    uf = findFile(chunk.fileno, impl);
    if (uf && chunk.len && uf->paused() && uf->inPool())
    {
        LOG_debug << "[WsPool::sendChunk] requeue chunk (paused while reading/opening) [pos="
                  << chunk.pos << "] [len=" << chunk.len << "] [fileno=" << chunk.fileno << "]";
        mToResend.push_back(chunk);
        return false;
    }

    // Local I/O error (or other non-paused failure while reading/opening).
    // Abort this upload attempt and let MegaClient run the standard Transfer::failed retry/backoff
    // logic.
    if (uf && !uf->aborted())
    {
        LOG_warn << "[WsPool::sendChunk] read/open failed, aborting upload attempt [pos="
                 << chunk.pos << "] [len=" << chunk.len << "] [fileno=" << chunk.fileno << "]";

        purgeFileLocked(chunk.fileno);
        uf->uploadFailed(FailReason::ReadFailed);

        if (impl.mCb.onFail)
        {
            impl.mCb.onFail(uf->transfer(), API_EREAD, chunk.pos, uf->readFailureDisposition());
        }
        return false;
    }

    LOG_debug << "[WsPool::sendChunk] read/open failed but file is no longer active in this pool"
              << " [pos=" << chunk.pos << "] [len=" << chunk.len << "] [fileno=" << chunk.fileno
              << "]";
    return false;
}

void WsPool::poolWorkerThread(WsPoolThread* th)
{
    int retryCount{0};
    dstime firstConnectFailureDs{0};
    std::uint32_t lastQueueVersion = mImpl->queueVersion;
    std::uint64_t seenDisconnectEpoch = mImpl->disconnectEpoch.load(std::memory_order_acquire);
    auto ws = std::make_unique<WsConn>(this);

    LOG_debug << "[WsPool::poolWorkerThread] BEGIN [lastQueueVersion=" << lastQueueVersion
              << "] [this = " << this << "]";

    std::unique_lock<std::mutex> lk(mImpl->uploadMutex);
    while (!th->terminate)
    {
        const std::uint64_t disconnectEpoch =
            mImpl->disconnectEpoch.load(std::memory_order_acquire);
        if (disconnectEpoch != seenDisconnectEpoch)
        {
            seenDisconnectEpoch = disconnectEpoch;
            if (ws->readyState != WsConn::ReadyState::CLOSED)
            {
                // Reuse normal close path so in-flight chunks are re-queued safely.
                ScopedUnlock unlock(lk);
                ws->closeWS();
            }
        }

        if (ws->readyState == WsConn::ReadyState::CLOSED)
        {
            // Do not hold the engine mutex while blocking on connect.
            bool ok = false;
            {
                ScopedUnlock unlock(lk);
                ok = ws->connectWS();
            }

            if (!ok)
            {
                const dstime nowDs = SteadyTime::ds();
                if (!firstConnectFailureDs)
                {
                    firstConnectFailureDs = nowDs;
                }

                if (!retryCount++)
                {
                    LOG_debug << "[WsPool::poolWorkerThread] retryCount=" << retryCount
                              << " -> continue [this = " << this << "]";
                    continue;
                }

                // Pinned session invalidation should be conservative: brief network glitches can
                // cause a few connect failures, but do not necessarily mean the pinned endpoint
                // is invalid. Require both a minimum retry count and sustained failure window
                // (60s).
                const auto failedForDs = SteadyTime::difference(nowDs, firstConnectFailureDs);
                if (mPinned && !mRetiring && retryCount >= 3 && failedForDs >= 60 * 10)
                {
                    LOG_warn << "[WsPool::poolWorkerThread] pinned session URL failed to connect "
                                "after retries: "
                             << mUrl << " [retryCount=" << retryCount
                             << "] [failedForDs=" << failedForDs << "] [this = " << this << "]";
                    ScopedUnlock unlock(lk);
                    mImpl->invalidatePinnedSessionUrl(mUrl);
                    continue;
                }

                // Repeated handshake failures for an active upload must eventually transition
                // through Transfer::failed/backoff instead of looping forever in reconnect.
                dstime sustainedHandshakeFailureWindowDs = UPLOADTIMEOUT;
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
                DEBUG_TEST_HOOK_WSUPLOAD_SUSTAINED_HANDSHAKE_FAILURE_WINDOW_DS(
                    sustainedHandshakeFailureWindowDs);
#endif
                if (retryCount >= 3 &&
                    failedForDs >= sustainedHandshakeFailureWindowDs)
                {
                    auto* uploadToFail = handshakeFailureCandidateLocked(nowDs);
                    if (uploadToFail)
                    {
                        LOG_warn << "[WsPool::poolWorkerThread] sustained WS handshake failures for "
                                    "active upload -> fail current attempt"
                                 << " [url=" << mUrl << "] [retryCount=" << retryCount
                                 << "] [failedForDs=" << failedForDs
                                 << "] [retiring=" << mRetiring
                                 << "] [fileno=" << uploadToFail->fileno() << "] [this = " << this
                                 << "]";

                        // Reset the active-file fast path so the eventual retry must re-enter
                        // nextEligible()/setUploadStart()/onStart() instead of silently reusing the
                        // stale active file after backoff expires.
                        purgeFileLocked(uploadToFail->fileno());
                        uploadToFail->markFailedForRetry(0);
                        uploadToFail->unsetPool();
                        if (mUploadingFile == uploadToFail)
                        {
                            mUploadingFile = nullptr;
                        }
                        mUFTQversion = mImpl->queueVersion;

                        if (mImpl->mCb.onFail)
                        {
                            mImpl->mCb.onFail(uploadToFail->transfer(),
                                              API_EAGAIN,
                                              0,
                                              UploadEngine::FailureDisposition::Retryable);
                        }

                        // Reset connect-failure window for the next attempt.
                        retryCount = 0;
                        firstConnectFailureDs = 0;
                        continue;
                    }
                }

                if (!mRetiring && lastQueueVersion != mImpl->queueVersion)
                {
                    LOG_debug << "[WsPool::poolWorkerThread] !mRetiring && lastQueueVersion("
                              << lastQueueVersion << ") != mImpl->queueVersion("
                              << mImpl->queueVersion << ") -> refreshPools [this = " << this << "]";
                    mImpl->poolMgr.refreshPools();
                }

                LOG_debug << "[WsPool::poolWorkerThread] !ok -> continue [retryCount=" << retryCount
                          << "] [this = " << this << "]";

                {
                    ScopedUnlock unlock(lk);
                    SteadyTime::sleep_ds(CONNRETRYINTERVAL);
                }
                continue;
            }
            retryCount = 0;
            firstConnectFailureDs = 0;
        }

        lastQueueVersion = mImpl->queueVersion;

        // recv server frames
        {
            ScopedUnlock unlock(lk);
            ws->curlRecv();
        }

        // server throttle?
        if (throttledByServer())
        {
            LOG_debug << "[WsPool::poolWorkerThread] throttledByServer=true -> continue [this = "
                      << this << "]";
            {
                ScopedUnlock unlock(lk);
                SteadyTime::sleep_ds(1);
            }
            continue;
        }

        // flush send buffers
        {
            ScopedUnlock unlock(lk);
            ws->curlSend();
        }

        if (!ws->haveSpace())
        {
            LOG_debug << "[WsPool::poolWorkerThread] haveSpace=false -> continue [this = " << this
                      << "]";
            {
                ScopedUnlock unlock(lk);
                SteadyTime::sleep_ds(2);
            }
            continue;
        }
        if (!ws->readyForData())
        {
            LOG_debug << "[WsPool::poolWorkerThread] readyForData=false -> continue [this = "
                      << this << "]";
            {
                ScopedUnlock unlock(lk);
                SteadyTime::sleep_ds(2);
            }
            continue;
        }

        // fetch & enqueue next chunk (unlocks around disk I/O internally)
        dstime sendRetryAfterDs = 10;
        if (!sendChunk(ws.get(), *mImpl, &sendRetryAfterDs))
        {
            LOG_debug << "[WsPool::poolWorkerThread] sendChunk=false -> continue [this = " << this
                      << "]";
            {
                ScopedUnlock unlock(lk);
                SteadyTime::sleep_ds(sendRetryAfterDs);
            }
            continue;
        }
    }

    if (ws->readyState != WsConn::ReadyState::CLOSED)
    {
        ScopedUnlock unlock(lk);
        ws->closeWS();
    }

    th->terminated = true;
    LOG_debug << "[WsPool::poolWorkerThread] END [lastQueueVersion=" << lastQueueVersion
              << "] [this = " << this << "]";
}

void WsPool::checkThreads()
{
    while (mActiveThreads.size() < mNumberOfConnections)
    {
        auto thr = std::make_unique<WsPoolThread>(this);
        mActiveThreads.push_back(std::move(thr));
    }
    while (mActiveThreads.size() > mNumberOfConnections)
    {
        mActiveThreads.back()->terminate = true;
        auto thr = std::move(mActiveThreads.back());
        mActiveThreads.pop_back();
        mExitingThreads.push_back(std::move(thr));
    }
}

// ========== WsPoolMgr ==========
void WsPoolMgr::curlIO(std::unique_lock<std::mutex>& lk)
{
    int still_running = 0, msgs_left = 0;
    curl_multi_perform(curlm, &still_running);

    for (auto& kv: mCurlProcs)
        if (kv.second)
            kv.second->curlIO();

    CURLMsg* msg;
    while ((msg = curl_multi_info_read(curlm, &msgs_left)) != nullptr)
    {
        if (msg->msg == CURLMSG_DONE)
        {
            CURL* curl = msg->easy_handle;
            auto it = mCurlProcs.find(curl);
            if (it != mCurlProcs.end())
            {
                CurlResponseProc* proc = it->second;
                const bool ok = (msg->data.result == CURLE_OK);
                if (proc && proc->done(ok))
                {
                    curl_multi_remove_handle(curlm, curl);
                    mCurlProcs.erase(it);
                    delete proc;
                }
            }
        }
    }

    // Don't hold the engine mutex while blocking in curl I/O.
    {
        ScopedUnlock unlock(lk);
        (void)curl_multi_poll(curlm, nullptr, 0, 500, nullptr);
    }
}

void WsPoolMgr::ensurePinnedPool(const std::string& url)
{
    if (!mImpl || url.empty())
    {
        return;
    }

    for (const auto& pool: mPools)
    {
        if (pool && pool->mPinned && !pool->mRetiring && pool->mUrl == url)
        {
            return;
        }
    }

    // Create a dedicated pool that will only serve transfers pinned to this session URL.
    auto pool = std::make_unique<WsPool>(std::make_pair(url, static_cast<m_off_t>(0)),
                                         0,
                                         mImpl,
                                         mImpl->poolConnectionLimit());
    pool->mPinned = true;
    mPools.emplace_back(std::move(pool));

    bumpAllPools(SteadyTime::ds());
}

void WsPoolMgr::markPoolRetiring(WsPool& pool)
{
    if (pool.mRetiring)
    {
        return;
    }

    pool.mRetiring = true;
    pool.setPoolNumConn(0);
}

bool WsPoolMgr::poolHasNoWork(const WsPool& pool) const
{
    return (pool.mNumPoolFiles == 0) && (pool.mUploadingFile == nullptr) &&
           (pool.mNumChunksInFlight == 0) && pool.mToResend.empty();
}

bool WsPoolMgr::pinnedPoolHasReference(const WsPool& pool, const UploadEngine::Impl& impl) const
{
    for (const auto& entry: impl.files)
    {
        const auto& uf = entry.second;
        if (!uf)
        {
            continue;
        }

        if (uf->mPool == &pool || uf->sessionUrlHint() == pool.mUrl)
        {
            return true;
        }
    }

    return false;
}

void WsPoolMgr::retireUnusedPinnedPools(UploadEngine::Impl& impl)
{
    for (std::size_t i = mPools.size(); i-- > 0;)
    {
        WsPool* const pool = mPools[i].get();
        if (!pool || !pool->mPinned || pool->mRetiring)
        {
            continue;
        }

        const bool hasReference = pinnedPoolHasReference(*pool, impl);
        const bool hasNoWork = poolHasNoWork(*pool);
        const bool idleLongEnough =
            SteadyTime::difference(impl.currentTime, pool->mLastActive) > POOLCONNKEEPALIVE;

        if (!hasReference && hasNoWork && idleLongEnough)
        {
            markPoolRetiring(*pool);
        }
    }
}

void WsPoolMgr::cleanupRetiringPools()
{
    for (std::size_t i = mPools.size(); i-- > 0;)
    {
        if (mPools[i] && mPools[i]->mRetiring && !mPools[i]->stillActive())
        {
            LOG_info << "WsUpload: closing idle pool " << i << " (" << mPools[i]->mUrl << ")";
            mPools.erase(mPools.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
}

void WsPoolMgr::checkPools(UploadEngine::Impl& impl)
{
    // update last net read from pools
    for (std::size_t i = mPools.size(); i-- > 0;)
    {
        if (SteadyTime::difference(mPools[i]->mLastServerResponse, mLastNetRead) > 0)
            mLastNetRead = mPools[i]->mLastServerResponse;
    }

    // close idle retiring pools
    retireUnusedPinnedPools(impl);

    cleanupRetiringPools();

    // trim connections / refresh stale or stalled pools
    for (std::size_t i = mPools.size(); i-- > 0;)
    {
        if (mPools[i]->mNumberOfConnections > 1 &&
            SteadyTime::difference(impl.currentTime, mPools[i]->mLastActive) > POOLCONNKEEPALIVE)
        {
            mPools[i]->mNumberOfConnections = 1;
            mPools[i]->checkThreads();
        }

        if (SteadyTime::difference(impl.currentTime, mPools[i]->mPoolCreationTime) > POOLFRESHNESS)
            refreshPools();

        if ((mPools[i]->mUploadingFile || mPools[i]->mNumChunksInFlight ||
             !mPools[i]->mToResend.empty()) &&
            SteadyTime::difference(impl.currentTime, mPools[i]->mLastActive) > SERVERTIMEOUT)
            refreshPools();
    }

    // throughput display tick (server-acked)
    for (auto* uf: mActiveFiles)
        uf->mClientActiveFilesTick = false;
    mActiveFiles.clear();
}

void WsPoolMgr::refreshPools()
{
    if (!mImpl)
        return;
    if (mImpl->stopping())
        return;

    const dstime now = SteadyTime::ds();
    if (now < mNextRefreshAttempt)
    {
        return;
    }

    if (mRefreshing.exchange(true))
    {
        return;
    }

    mImpl->client.wsPostToClientThread(
        [this](MegaClient& client, TransferDbCommitter&)
        {
            if (!mImpl || mImpl->stopping())
            {
                mRefreshing = false;
                return;
            }

            // Queue lockless USC command using the RequestDispatcher. This avoids blocking on
            // the main client-server channel when lockless channels are enabled.
            client.queueCommand(new CommandUSCForWsUpload(
                client,
                [this](Error e, std::vector<std::pair<std::string, m_off_t>>&& sizeClasses)
                {
                    std::lock_guard<std::mutex> g(mImpl->uploadMutex);
                    if (e == API_OK)
                    {
                        mRefreshFailCount = 0;
                        mNextRefreshAttempt = 0;
                        applyRefreshedUrls(std::move(sizeClasses));
                    }
                    else
                    {
                        ++mRefreshFailCount;
                        const unsigned count =
                            mRefreshFailCount ? (mRefreshFailCount - 1) : 0;
                        const unsigned exponent = std::min<unsigned>(count, 6);
                        const dstime baseDelay = 10 * 10; // 10 seconds (dstime is deciseconds)
                        const dstime maxDelay = 10 * 60 * 10; // 10 minutes
                        dstime backoff = baseDelay * (static_cast<dstime>(1) << exponent);
                        if (backoff > maxDelay)
                        {
                            backoff = maxDelay;
                        }
                        mNextRefreshAttempt = SteadyTime::ds() + backoff;

                        LOG_warn << "[WsPoolMgr::refreshPools] USC command failed: " << e
                                 << " [this = " << this << "]";
                    }
                    mRefreshing = false;
                }));
        });
}

bool WsPoolMgr::refreshPoolsResponse(std::string& response)
{
    std::vector<std::pair<std::string, m_off_t>> apiSizeClasses;

    const char* p = response.c_str();
    const char* q = nullptr;
    std::string url;
    bool ok{false};

    LOG_debug << "USC response: " << response;

    if (!std::memcmp(p, "[[[\"", 3))
    {
        for (;;)
        {
            p += 4;
            if (q = std::strchr(p, '"'); q)
            {
                url = "wss://";
                url.append(p, static_cast<size_t>(q - p));
                url.append("/");

                p = q + 3;
                if (q = std::strchr(p, '"'); q)
                {
                    url.append(p, static_cast<size_t>(q - p));
                }

                if (q[1] == ',')
                {
                    apiSizeClasses.emplace_back(url, static_cast<m_off_t>(atoll(q + 2)));
                    if (p = std::strchr(q, ']'); p && !std::memcmp(p, "],[\"", 4))
                    {
                        continue;
                    }
                }
                else
                {
                    apiSizeClasses.emplace_back(url, m_off_t(0));
                    ok = true;
                    break;
                }
            }
            LOG_warn << "Invalid USC response: " << response;
            break;
        }

        if (ok)
        {
            applyRefreshedUrls(std::move(apiSizeClasses));
        }
    }
    return ok;
}

void WsPoolMgr::applyRefreshedUrls(std::vector<std::pair<std::string, m_off_t>> apiSizeClasses)
{
    if (apiSizeClasses.empty())
    {
        LOG_warn << "WsUpload: USC returned no upload pools";
        return;
    }

    const dstime oldest = SteadyTime::ds() - POOLFRESHNESS;

    // Mark all currently-active pools as retiring; we'll unretire those that still match.
    for (std::size_t i = 0; i < mPools.size(); ++i)
    {
        if (!mPools[i]->mPinned)
        {
            mPools[i]->mRetiring = true;
        }
    }

    for (std::size_t i = 0; i < apiSizeClasses.size(); ++i)
    {
        bool matched = false;
        for (std::size_t j = mPools.size(); j-- > 0;)
        {
            if (mPools[j]->mPinned)
            {
                continue;
            }
            if (mPools[j]->freshAndSameHostMaxSize(apiSizeClasses[i], oldest))
            {
                if (j != i)
                {
                    std::swap(mPools[i], mPools[j]);
                }
                mPools[i]->mRetiring = false;
                matched = true;
                break;
            }
        }
        if (!matched)
        {
            mPools.insert(mPools.begin() + static_cast<std::ptrdiff_t>(i),
                          std::make_unique<WsPool>(apiSizeClasses[i],
                                                   i ? apiSizeClasses[i - 1].second : 0,
                                                   mImpl,
                                                   mImpl->poolConnectionLimit()));
        }
    }

    bumpAllPools(mImpl->currentTime);
    LOG_info << "WsUpload: refreshed pools (" << apiSizeClasses.size() << " size classes)";
}

// ========== UploadEngine (public facade) ==========
UploadEngine::UploadEngine(MegaClient& client):
    pImpl(new Impl(client))
{}

UploadEngine::~UploadEngine() = default;

void UploadEngine::start()
{
    pImpl->start();
}

void UploadEngine::stop()
{
    pImpl->stop();
}

bool UploadEngine::isStopping() const
{
    return pImpl->stopping();
}

void UploadEngine::enqueue(Transfer& t)
{
    pImpl->enqueue(t);
}

void UploadEngine::reposition(Transfer& t, Transfer* before)
{
    pImpl->reposition(t, before);
}

void UploadEngine::pause(Transfer& t)
{
    pImpl->pause(t);
}

void UploadEngine::unpause(Transfer& t)
{
    pImpl->unpause(t);
}

void UploadEngine::remove(Transfer& t)
{
    pImpl->remove(t);
}

void UploadEngine::setRetryUntil(Transfer& t, const dstime when)
{
    pImpl->setRetryUntil(t, when);
}

void UploadEngine::markFailed(Transfer& t, const dstime retryUntil)
{
    pImpl->markFailed(t, retryUntil);
}

bool UploadEngine::isUploading(Transfer& t) const
{
    return pImpl->isUploading(t);
}

bool UploadEngine::drainConfirmedChunkMacs(Transfer& t, std::vector<chunkmac_map>& out)
{
    return pImpl->drainConfirmedChunkMacs(t, out);
}

bool UploadEngine::getSessionUrl(Transfer& t, std::string& outUrl) const
{
    return pImpl->getSessionUrl(t, outUrl);
}

void UploadEngine::setCallbacks(UploadEngine::Callbacks cb)
{
    pImpl->mCb = std::move(cb);
}

bool UploadEngine::getTransferStats(const Transfer& t, UploadEngine::WsTransferStats& stats) const
{
    return pImpl->getTransferStats(t, stats);
}

#ifndef NDEBUG
bool UploadEngine::getPoolStateForTesting(const std::string& url,
                                          UploadEngine::PoolStateForTesting& out) const
{
    return pImpl->getPoolStateForTesting(url, out);
}

bool UploadEngine::isTrackedForTesting(const Transfer& t) const
{
    return pImpl->isTrackedForTesting(t);
}
#endif

void UploadEngine::kick()
{
    pImpl->kick();
}

void UploadEngine::notifyNetworkDisconnect()
{
    pImpl->notifyNetworkDisconnect();
}

void UploadEngine::setMaxConnections(const unsigned char maxConnections)
{
    pImpl->setMaxConnections(maxConnections);
}

void UploadEngine::setMaxUploadSpeed(const m_off_t bytesPerSecond)
{
    pImpl->setMaxUploadSpeed(bytesPerSecond);
}

// Simple feature gate for now (could later inspect client caps/settings)
bool wsEnabled(const MegaClient&)
{
    return true;
}

} // namespace ws
} // namespace mega
