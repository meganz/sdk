#ifdef MEGA_USE_WSUPLOAD

#include "mega/wsupload.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
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
#include "mega/commands_ws.h"
#include "mega/file.h"
#include "mega/filesystem.h"
#include "mega/http.h"
#include "mega/logging.h"
#include "mega/megaapp.h"
#include "mega/megaclient.h"
#include "mega/testhooks.h"
#include "mega/transfer.h"
#include "mega/transfer/ws/ws_encryption.h"
#include "mega/transfer/ws/ws_pool_mgr.h"
#include "mega/types.h"
#include "mega/utils.h"

// File-internal types/helpers shared with future ws_curl.cpp (kMiB,
// WSUPLOAD_CURL_MULTI_POLL_MS, WSUPLOAD_TRACE, SteadyTime, ScopedUnlock,
// CRC32, WsBuf/WsChunk/ChunkHeader/ChunkFingerprintMacUpdate, WsConn,
// WsPoolThread, WsPool). Private header under src/, not include/. Relative
// include since src/ is not on the SDKlib include path.
#include "wsupload_internal.h"

// Phase 1: keep prototype's WS + threads. Phase 3: move into CurlHttpIO reactor.
#include <curl/curl.h>

#include <zlib.h>

namespace mega
{
namespace ws
{

// WSUPLOAD_TRACE, SteadyTime, ScopedUnlock, CRC32, kMiB, and
// WSUPLOAD_CURL_MULTI_POLL_MS now live in transfer/ws/wsupload_internal.h
// (included above). They are shared with the future ws_curl.cpp split.

#ifndef NDEBUG
std::uint64_t steadyMs()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
#endif

// WS upload session-URL handshake timeout passed to CurlHttpIO::wsHandshakeForUpload.
// Mirrors the prior CurlHttpIO 15s POST timeout used for the legacy upload-start request.
constexpr long WSUPLOAD_HANDSHAKE_TIMEOUT_MS = 15000;

// Poll interval for the WsConn::connectWS handshake-completion condition variable.
// Short enough to react to stopping() in <1s; large enough to avoid spinning while
// the curl handshake makes progress on the client thread.
constexpr int WSUPLOAD_HANDSHAKE_CV_POLL_MS = 200;

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

// WsPool, WsConn, WsPoolThread forward-declared in
// transfer/ws/wsupload_internal.h (included above). UploadEngine itself is
// declared by mega/wsupload.h.

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

        WSUPLOAD_TRACE << "WsUploadFile: " << mLocalPath << " fileno=" << mFileNo << " size=" << mSize
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

    void closeFA()
    {
        std::lock_guard<std::mutex> io(mReadMutex);
        mFA = nullptr;
    }

    // I/O (open on first read) — engineMutex is the single engine mutex
    bool readData(char* buf,
                  const m_off_t pos,
                  const int len,
                  std::mutex& engineMutex,
                  const std::uint64_t expectedGeneration,
                  bool* interruptedByStateChange = nullptr)
    {
        WSUPLOAD_TRACE << "[WsUploadFile::readData] BEGIN [buf=" << (void*)buf << "] [pos=" << pos
                  << "] [len=" << len << "] [this = " << this
                  << "] [thread_id=" << std::this_thread::get_id() << "]";
        if (interruptedByStateChange)
        {
            *interruptedByStateChange = false;
        }
        ActiveIOGuard io(*this);
        // open on first use
        std::unique_lock<std::mutex> ioLock(mReadMutex);
        if (!mFA)
        {
            WSUPLOAD_TRACE << "[WsUploadFile::readData] !mFA -> newfileaccess for localname="
                      << mLocalPath << " [this = " << this
                      << "] [thread_id=" << std::this_thread::get_id() << "]";
            engineMutex.unlock();
            auto fa = mClient.fsaccess->newfileaccess();
            fa->mShareDelete =
                true; // Allow file to be moved/deleted while WS upload holds the handle
            const bool okOpen = fa->fopen(mLocalPath, OPEN_RDONLY, FSLogging::logOnError);
            WSUPLOAD_TRACE << "[WsUploadFile::readData] okOpen=" << okOpen
                      << " [localname=" << mLocalPath << "] [this = " << this
                      << "] [thread_id=" << std::this_thread::get_id() << "]";
            ioLock.unlock(); // never re-lock engineMutex while holding mReadMutex
            engineMutex.lock();

            if (mWorkGeneration != expectedGeneration)
            {
                if (interruptedByStateChange)
                {
                    *interruptedByStateChange = true;
                }
                WSUPLOAD_TRACE << "[WsUploadFile::readData] upload state changed while opening; "
                             "treat as interrupted read [localname="
                          << mLocalPath << "] [this = " << this
                          << "] [thread_id=" << std::this_thread::get_id() << "]";
                return false;
            }

            // Re-check under mReadMutex: another worker may have completed the open
            // while this thread had both locks released.
            ioLock.lock();
            if (mFA)
            {
                ioLock.unlock();
            }
            else if (!okOpen)
            {
                ioLock.unlock();
                WSUPLOAD_TRACE << "[WsUploadFile::readData] !okOpen -> markFailed() and return false "
                             "[localname="
                          << mLocalPath << "] [this = " << this
                          << "] [thread_id=" << std::this_thread::get_id() << "]";
                markFailed(classifyOpenFailure(fa.get()));
                return false;
            }
            else if (!sourceMatchesExpected(fa->mtime, fa->size))
            {
                ioLock.unlock();
                LOG_warn << "[WsUploadFile::readData] file changed before first read. "
                         << "Expected mtime=" << mMtime << " size=" << mSize
                         << ", got mtime=" << fa->mtime << " size=" << fa->size
                         << " [localname=" << mLocalPath << "]";
                markFailed(UploadEngine::FailureDisposition::Permanent);
                return false;
            }
            else
            {
                mFA = std::move(fa);
                mBytesSinceLastStat = 0;
                mStatIntervalBytes =
                    (mSize >= STAT_INTERVAL_LARGE) ? STAT_INTERVAL_LARGE : STAT_INTERVAL_SMALL;
                WSUPLOAD_TRACE << "[WsUploadFile::readData] mFA=" << (void*)mFA.get() << " mSize=" << mSize
                          << " mMtime=" << mMtime << " [localname=" << mLocalPath
                          << "] [this = " << this << "] [thread_id=" << std::this_thread::get_id()
                          << "]";
                ioLock.unlock();
            }
        }
        if (ioLock.owns_lock())
            ioLock.unlock();

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

            FileAccess* faRawCheck = nullptr;
            {
                std::lock_guard<std::mutex> ioRead(mReadMutex);
                faRawCheck = mFA.get();
            }
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

            bool faSwapped = false;
            {
                std::lock_guard<std::mutex> ioRead(mReadMutex);
                faSwapped = (mFA.get() != faRawCheck);
            }

            if (mWorkGeneration != expectedGeneration || faSwapped)
            {
                if (interruptedByStateChange)
                {
                    *interruptedByStateChange = true;
                }
                WSUPLOAD_TRACE << "[WsUploadFile::readData] upload state changed during periodic stat; "
                             "treat as interrupted read [localname="
                          << mLocalPath << "] [this = " << this
                          << "] [thread_id=" << std::this_thread::get_id() << "]";
                return false;
            }

            if (!exists)
            {
                LOG_warn << "[WsUploadFile::readData] file stat failed (deleted?) "
                         << "[localname=" << mLocalPath << "]";
                closeFA();
                markFailed(missingDisposition);
                return false;
            }

            if (!sourceMatchesExpected(currMtime, currSize))
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
        bool reopenSourceChanged = false;

        FileAccess* faRaw = nullptr;
        ioLock.lock();
        faRaw = mFA.get();
        engineMutex.unlock();
        {
            // mFA may be cleared by pause/cancel while releasing engineMutex.
            // Treat this as an interrupted read so the chunk can be re-queued, not failed.
            if (!faRaw)
            {
                ioLock.unlock(); // do not re-lock engine while holding the I/O mutex
                engineMutex.lock();
                if (interruptedByStateChange)
                {
                    *interruptedByStateChange = true;
                }
                WSUPLOAD_TRACE << "[WsUploadFile::readData] mFA cleared while switching locks; "
                             "treat as interrupted read [localname="
                          << mLocalPath << "] [this = " << this
                          << "] [thread_id=" << std::this_thread::get_id() << "]";
                return false;
            }
            // Some platforms declare frawread(pos) as unsigned — cast explicitly to avoid
            // -Wconversion
            okRead = faRaw->frawread(reinterpret_cast<byte*>(buf),
                                     static_cast<unsigned>(len),
                                     pos,
                                     /*caller_opened=*/true,
                                     FSLogging::logOnError);

            if (!okRead)
            {
                // Optional one-shot recover: reopen the blocking file handle and retry once.
                WSUPLOAD_TRACE << "[WsUploadFile::readData] !okRead -> reopen + retry once [localname="
                          << mLocalPath << "] [this = " << this
                          << "] [thread_id=" << std::this_thread::get_id() << "]";
                faRaw->fclose();
                reok = faRaw->fopen(mLocalPath, OPEN_RDONLY, FSLogging::logOnError);
                didReopen = true;
                if (reok && !sourceMatchesExpected(faRaw->mtime, faRaw->size))
                {
                    LOG_warn << "[WsUploadFile::readData] file changed before reopen retry. "
                             << "Expected mtime=" << mMtime << " size=" << mSize
                             << ", got mtime=" << faRaw->mtime << " size=" << faRaw->size
                             << " [localname=" << mLocalPath << "]";
                    faRaw->fclose();
                    reok = false;
                    reopenSourceChanged = true;
                }
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
        WSUPLOAD_TRACE << "[WsUploadFile::readData] okRead=" << okRead << " [localname=" << mLocalPath
                  << "] [this = " << this << "] [thread_id=" << std::this_thread::get_id() << "]";
        engineMutex.lock();

        if (mWorkGeneration != expectedGeneration)
        {
            if (interruptedByStateChange)
            {
                *interruptedByStateChange = true;
            }
            WSUPLOAD_TRACE << "[WsUploadFile::readData] upload state changed while reading; "
                         "treat as interrupted read [localname="
                      << mLocalPath << "] [this = " << this
                      << "] [thread_id=" << std::this_thread::get_id() << "]";
            return false;
        }

        if (!okRead)
        {
            auto disposition = UploadEngine::FailureDisposition::Retryable;
            if (didReopen && !reok)
            {
                disposition = reopenSourceChanged ? UploadEngine::FailureDisposition::Permanent :
                                                    classifyOpenFailure(faRaw);
            }
            WSUPLOAD_TRACE << "[WsUploadFile::readData] !okRead -> markFailed() [localname="
                      << mLocalPath << "] [this = " << this
                      << "] [thread_id=" << std::this_thread::get_id() << "]";
            closeFA();
            markFailed(disposition);
        }
        WSUPLOAD_TRACE << "[WsUploadFile::readData] return okRead=" << okRead
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
        WSUPLOAD_TRACE << "[WsUploadFile::fileno] return mFileNo=" << mFileNo << " [this = " << this
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
        WSUPLOAD_TRACE << "[WsUploadFile::paused] return mPaused=" << mPaused << " [this = " << this
                  << "]";
        return mPaused;
    }

    void setPaused(const bool p) noexcept
    {
        WSUPLOAD_TRACE << "[WsUploadFile::setPaused] p=" << p << " [this = " << this << "]";
        mPaused = p;
        invalidateOutstandingWork();
        closeFA();
    }

    bool aborted() const noexcept
    {
        WSUPLOAD_TRACE << "[WsUploadFile::aborted] return mAborted=" << mAborted << " [this = " << this
                  << "]";
        return mAborted;
    }

    void setUploadStart(const dstime t) noexcept
    {
        WSUPLOAD_TRACE << "[WsUploadFile::setUploadStart] t=" << t << " [this = " << this << "]";
        invalidateOutstandingWork();
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

    std::uint64_t workGeneration() const noexcept
    {
        return mWorkGeneration;
    }

    bool hasFailed() const noexcept
    {
        return mUploadFailedTime != 0;
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
        invalidateOutstandingWork();
        mUploadCompletionTime = SteadyTime::ds();
        // Fix up the progress counter on completion so the final onProgress reflects the
        // truth even if opcode-2 AlreadyOnServer events left mBytesConfirmed below mSize.
        if (mBytesConfirmed < mSize)
        {
            mBytesConfirmed = mSize;
        }
        unsetPool();
        closeFA();
        const auto dsElapsed = SteadyTime::difference(mUploadCompletionTime, mUploadStartTime);
        const auto attemptConfirmed =
            std::max<m_off_t>(0, mBytesConfirmed - mAttemptBaseConfirmed);
        const auto kbps = dsElapsed ? (attemptConfirmed / dsElapsed * kDsPerSecond / 1024) : 0;
        WSUPLOAD_TRACE << "[WsUploadFile::uploadCompleted] upload completed (server payload len=" << len
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
        const auto kbps = dsElapsed ? (attemptConfirmed / dsElapsed * kDsPerSecond / 1024) : 0;
        WSUPLOAD_TRACE << "[WsUploadFile::maybeReportThroughput] " << mBytesConfirmed << " of " << mSize
                  << " bytes (attempt bytes: " << attemptConfirmed << ") @ ~" << kbps
                  << " KB/s [this = " << this << "]";
    }

    bool isUploading() const noexcept
    {
        WSUPLOAD_TRACE << "[WsUploadFile::isUploading] mUploadStartTime= " << mUploadStartTime
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

#ifndef NDEBUG
    bool markFirstByteSentForTesting() noexcept
    {
        if (mFirstByteSentForStats)
        {
            return false;
        }

        mFirstByteSentForStats = true;
        return true;
    }
#endif

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
        stats.avgStartTransferTime = std::chrono::milliseconds(dsToMs(latencyDs));

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
    void invalidateOutstandingWork() noexcept
    {
        if (++mWorkGeneration == 0)
        {
            mWorkGeneration = 1;
        }
    }

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
        invalidateOutstandingWork();
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
#ifndef NDEBUG
        mFirstByteSentForStats = false;
#endif
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
    std::unique_ptr<FileAccess> mFA{}; // blocking-opened on first read
    // Atomic so readers that do not hold engineMutex (e.g. the workGeneration() accessor
    // at wsupload.cpp ~700) observe a well-defined value. Writers still serialise via
    // engineMutex; the atomic adds cheap defence for future callers that forget.
    std::atomic<std::uint64_t> mWorkGeneration{1};
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

    static constexpr m_off_t STAT_INTERVAL_LARGE = 8 * kMiB; // 8 MiB
    static constexpr m_off_t STAT_INTERVAL_SMALL = 2 * kMiB; // 2 MiB

    bool mEofSet{false};
    bool mAborted{false};
    bool mPaused{false};

    dstime mUploadStartTime{0};
    dstime mUploadCompletionTime{0};
    dstime mUploadFailedTime{0};
    dstime mRetryUntil{0};

    // Server-confirmed chunk MAC updates awaiting application on the client thread.
    std::vector<chunkmac_map> mConfirmedChunkMacs;
#ifndef NDEBUG
    bool mFirstByteSentForStats{false};
#endif
};

// WS payloads (ChunkHeader/WsChunk/WsBuf/ChunkFingerprintMacUpdate),
// WsConn, WsPoolThread and WsPool now live in
// transfer/ws/wsupload_internal.h. Their out-of-line member bodies (e.g.
// WsPool::assignUploadingFileLocked, WsBuf::sendWS, ChunkFingerprintMacUpdate::apply)
// remain in this translation unit until commit beta of the Goal 2.a split.

#ifndef NDEBUG
void WsPool::assignUploadingFileLocked(WsUploadFile* file) noexcept
{
    if (mUploadingFile == file)
    {
        return;
    }

    const auto now = steadyMs();
    if (mUploadingFile && mUploadingFileSinceMs)
    {
        mUploadingFileOccupiedMs += now - mUploadingFileSinceMs;
    }

    mUploadingFile = file;
    mUploadingFileSinceMs = file ? now : 0;
}

void WsPool::recordFirstByteSentLocked(WsUploadFile& file) noexcept
{
    if (!file.markFirstByteSentForTesting())
    {
        return;
    }

    const auto now = steadyMs();
    const auto fileno = file.fileno();
    mLastFirstByteSentMs = now;
    mLastFirstByteSentFileno = fileno;

    if (mLastCompletedFileMs && mLastCompletedFileno != fileno)
    {
        const auto deltaMs = now - mLastCompletedFileMs;
        ++mLastAckToNextFirstByteSamples;
        mLastAckToNextFirstByteTotalMs += deltaMs;
        mLastAckToNextFirstByteMaxMs = std::max(mLastAckToNextFirstByteMaxMs, deltaMs);
        mLastCompletedFileMs = 0;
        mLastCompletedFileno = 0;
    }
}

void WsPool::recordUploadCompletedLocked(const std::uint32_t fileno) noexcept
{
    const auto now = steadyMs();
    if (mLastFirstByteSentMs && mLastFirstByteSentFileno != fileno)
    {
        ++mLastAckToNextFirstByteSamples;
        mLastCompletedFileMs = 0;
        mLastCompletedFileno = 0;
        return;
    }

    mLastCompletedFileMs = now;
    mLastCompletedFileno = fileno;
}

void WsPool::addWsUploadStatsForTesting(UploadEngine::WsUploadStatsForTesting& out) const
{
    ++out.poolCount;
    out.uploadingFileOccupiedMs +=
        mUploadingFileOccupiedMs +
        ((mUploadingFile && mUploadingFileSinceMs) ? (steadyMs() - mUploadingFileSinceMs) : 0);
    out.lastAckToNextFirstByteSamples += mLastAckToNextFirstByteSamples;
    out.lastAckToNextFirstByteTotalMs += mLastAckToNextFirstByteTotalMs;
    out.lastAckToNextFirstByteMaxMs =
        std::max(out.lastAckToNextFirstByteMaxMs, mLastAckToNextFirstByteMaxMs);
    out.allChunksInFlightBlockedMs += mAllChunksInFlightBlockedMs;
    out.eligibleFileSampleCount += mEligibleFileSampleCount;
    out.blockedByInFlightSampleCount += mBlockedByInFlightSampleCount;
    out.idleEligibleConnectionMs += mIdleEligibleConnectionMs;
    out.idleEligibleConnectionSampleCount += mIdleEligibleConnectionSampleCount;

    // Send-side counters: per-pool aggregates.
    out.haveSpaceFalseIters += mHaveSpaceFalseIters;
    out.haveSpaceFalseWaitMs += mHaveSpaceFalseWaitMs;
    out.readyForDataFalseIters += mReadyForDataFalseIters;
    out.readyForDataFalseWaitMs += mReadyForDataFalseWaitMs;
    out.throttleSleepIters += mThrottleSleepIters;
    out.throttleSleepMs += mThrottleSleepMs;
    out.backlogEmptyIters += mBacklogEmptyIters;
    out.backlogEmptyMs += mBacklogEmptyMs;
    out.chunkPrepTotalMs += mChunkPrepTotalMs;
    if (mChunkPrepMaxMs > out.chunkPrepMaxMs)
        out.chunkPrepMaxMs = mChunkPrepMaxMs;
    out.chunkPrepN += mChunkPrepN;

    // Server throttle telemetry aggregates.
    out.throttleEventCount += mThrottleEventCount;
    out.throttleEventTotalDs += mThrottleEventTotalDs;
    out.throttleEventSumSqDs += mThrottleEventSumSqDs;
    if (out.throttleEventMinDs == 0 || (mThrottleEventMinDs && mThrottleEventMinDs < out.throttleEventMinDs))
        out.throttleEventMinDs = mThrottleEventMinDs;
    if (mThrottleEventMaxDs > out.throttleEventMaxDs) out.throttleEventMaxDs = mThrottleEventMaxDs;
    out.throttleBucket0to1s += mThrottleBucket0to1s;
    out.throttleBucket1to5s += mThrottleBucket1to5s;
    out.throttleBucket5to30s += mThrottleBucket5to30s;
    out.throttleBucket30sPlus += mThrottleBucket30sPlus;
    for (int i = 0; i < 16; ++i) out.throttleEventCodeCounts[i] += mThrottleEventCodeCounts[i];
    if (mSimultaneousThrottledConnsMax > out.simultaneousThrottledConnsMax)
        out.simultaneousThrottledConnsMax = mSimultaneousThrottledConnsMax;
    out.simultaneousThrottledConnsSamples += mSimultaneousThrottledConnsSamples;
    out.simultaneousThrottledConnsSum += mSimultaneousThrottledConnsSum;

    // Per-conn counters: aggregate across all live conns in this pool.
    for (const WsConn* c: mConns)
    {
        if (!c)
            continue;
        out.curlAgainSendCount += c->mCurlAgainSendCount;
        out.curlAgainRecvCount += c->mCurlAgainRecvCount;
        if (static_cast<std::uint64_t>(c->mBufferedAmountHighWater) > out.bufferedAmountHighWater)
            out.bufferedAmountHighWater =
                static_cast<std::uint64_t>(c->mBufferedAmountHighWater);
        if (static_cast<std::uint64_t>(c->mChunksInFlightHighWater) > out.chunksInFlightHighWater)
            out.chunksInFlightHighWater =
                static_cast<std::uint64_t>(c->mChunksInFlightHighWater);
        // Per-conn throttle-recovery aggregates.
        out.throttleRecoveryAckSamples += c->mThrottleRecoveryAckSamples;
        out.throttleRecoveryAckTotalMs += c->mThrottleRecoveryAckTotalMs;
        if (c->mThrottleRecoveryAckMaxMs > out.throttleRecoveryAckMaxMs)
            out.throttleRecoveryAckMaxMs = c->mThrottleRecoveryAckMaxMs;
    }
}
#endif

// WsUploadFile pool bindings — must be defined after WsPool is complete
inline void WsUploadFile::unsetPool()
{
    WSUPLOAD_TRACE << "[WsUploadFile::unsetPool] mPool=" << (void*)mPool << " [this = " << this << "]";
    if (mPool)
    {
        if (mPool->mUploadingFile == this)
        {
            mPool->clearUploadingFileLocked();
        }
        mPool->decreaseNumPoolFiles();
        mPool = nullptr;
    }
}

inline void WsUploadFile::setPool(WsPool& p)
{
    WSUPLOAD_TRACE << "[WsUploadFile::setPool] p=" << (void*)&p << " [this = " << this << "]";
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
    WSUPLOAD_TRACE << "[WsUploadFile::setPool] END [this = " << this << "]";
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

// ---------- Pool manager (USC refresh + cURL multi) ----------
// struct WsPoolMgr is declared in include/mega/transfer/ws/ws_pool_mgr.h.
// Its method bodies (including the simple ctor/dtor + bumpLastNetRead /
// bumpAllPools) are defined here in this TU so that the header stays
// free of dependencies on the internal WsPool / SteadyTime types.

WsPoolMgr::WsPoolMgr()
{
    curlm = curl_multi_init();
}

WsPoolMgr::~WsPoolMgr()
{
    if (curlm)
    {
        curl_multi_cleanup(curlm);
        curlm = nullptr;
    }
}

void WsPoolMgr::bumpLastNetRead(const dstime now)
{
    if (SteadyTime::difference(now, mLastNetRead) > 0)
        mLastNetRead = now;
}

void WsPoolMgr::bumpAllPools(const dstime now)
{
    for (auto& p: mPools)
    {
        p->mLastActive = now;
        p->mLastServerResponse = now;
    }
}

// ========== UploadEngine::Impl (queue + mgr + thread) ==========
class UploadEngine::Impl
{
public:
    explicit Impl(MegaClient& c):
        client(c)
    {
        mInstanceId = ++sInstanceCounter;
        //curl_global_init(CURL_GLOBAL_ALL);
        WSUPLOAD_TRACE << "[UploadEngine::Impl] constructed";
    }

    ~Impl()
    {
        WSUPLOAD_TRACE << "[UploadEngine::Impl::~Impl] BEGIN";
        stop();

        // Manager thread may be waiting up to WSUPLOAD_CURL_MULTI_POLL_MS in curl_multi_poll; then it exits.
        if (uploadThread.joinable())
            uploadThread.join();

        // Join pool worker threads before member destruction (uploadMutex must remain valid).
        // Note: automatic member destruction runs in reverse declaration order, so poolMgr
        // is destroyed AFTER uploadMutex. By the time we reach this point, every WsConn
        // owned by a pool worker has been destroyed by the worker's local unique_ptr as the
        // worker thread exited, which is why we only join here and do not touch mConns.
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

        WSUPLOAD_TRACE << "[UploadEngine::Impl::~Impl] END";
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

        notifyWorkersLocked();
    }

    bool stopping() const
    {
        return mStopping.load(std::memory_order_acquire);
    }

    std::uint64_t instanceId() const noexcept
    {
        return mInstanceId;
    }

    // Refresh-latch orchestration helpers used by WsPoolMgr::refreshPools() callbacks.
    // Implementations live near other UploadEngine::Impl out-of-class definitions.
    bool clearRefreshLatchForInstance(std::uint64_t id);
    bool applyRefreshResultForInstance(std::uint64_t id,
                                       Error e,
                                       std::vector<std::pair<std::string, m_off_t>>&& urls);

    // Queue mirrors TransferList ordering and priority.
    void enqueue(Transfer& t)
    {
        WSUPLOAD_TRACE << "[UploadEngine::Impl::enqueue] t=" << t.localfilename
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
        eraseFromFileListLocked(f);

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

            WSUPLOAD_TRACE << "Removing transfer from queue: " << f->fileno();

            poolMgr.mActiveFiles.erase(f);
            for (auto& poolPtr: poolMgr.mPools)
            {
                if (!poolPtr)
                    continue;
                WsPool& pool = *poolPtr;
                if (pool.mUploadingFile == f)
                {
                    pool.clearUploadingFileLocked();
                    pool.mUFTQversion = queueVersion.load(std::memory_order_relaxed);
                }
                pool.purgeFileLocked(f->fileno());
            }
            f->unsetPool();

            eraseFromFileListLocked(f);

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
        WSUPLOAD_TRACE << "[UploadEngine::Impl::isUploading] t=" << t.localfilename
                  << " [this = " << this << "]";
        std::lock_guard<std::mutex> g(uploadMutex);
        if (const auto it = files.find(&t); it != files.end())
            return it->second->isUploading();
        WSUPLOAD_TRACE << "[UploadEngine::Impl::isUploading] t=" << t.localfilename
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
            out.openConnections = pool.countOpenConnectionsLocked();
            out.connectionsWithInFlight = pool.countConnectionsWithInFlightLocked();
            out.maxConnectionsWithInFlightSeen =
                std::max(pool.mMaxConnectionsWithInFlightSeen, out.connectionsWithInFlight);
            out.pausedByServerUntilDs = pool.mPausedByServerUntil;
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

    bool getWsUploadStatsForTesting(UploadEngine::WsUploadStatsForTesting& out) const
    {
        out = {};

        std::lock_guard<std::mutex> g(uploadMutex);
        out.found = true;
        for (const auto& poolPtr: poolMgr.mPools)
        {
            if (poolPtr)
            {
                poolPtr->addWsUploadStatsForTesting(out);
            }
        }

        return true;
    }

    bool isTrackedForTesting(const Transfer& t) const
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        return files.find(const_cast<Transfer*>(&t)) != files.end();
    }

    std::uintptr_t getFilePoolIdForTesting(Transfer& t) const
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        if (const auto it = files.find(&t);
            it != files.end() && it->second && it->second->hasPool())
        {
            return reinterpret_cast<std::uintptr_t>(it->second->mPool);
        }
        return 0;
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
                            pool->clearUploadingFileLocked();
                            pool->mUFTQversion = queueVersion.load(std::memory_order_relaxed);
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

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
                    const std::string prevUrlForHook = tp->ws_session_url;
#endif
                    tp->ws_session_url.clear();
                    tp->chunkmacs.clear();
                    tp->pos = 0;
                    tp->setProgresscompleted(0);
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
                    DEBUG_TEST_HOOK_WS_SESSION_URL_TRANSITION(tp->tag,
                                                              prevUrlForHook,
                                                              std::string{},
                                                              "invalidatePinned");
#endif

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
        // Defensive: ensure the refresh gate is not stuck from a prior engine lifecycle
        // where the posted clearRefreshing lambda was dropped before execution.
        poolMgr.mRefreshing.store(false, std::memory_order_release);
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
        WSUPLOAD_TRACE << "[UploadEngine::Impl::kick] BEGIN [this = " << this << "]";
        std::lock_guard<std::mutex> g(uploadMutex);
        // if (poolMgr.mPools.empty())
        //     poolMgr.refreshPools();
        for (auto& p: poolMgr.mPools)
        {
            WSUPLOAD_TRACE << "[UploadEngine::Impl::kick] pool(" << (void*)p.get()
                      << ") checkThreads() [this = " << this << "]";
            p->checkThreads();
        }
        WSUPLOAD_TRACE << "[UploadEngine::Impl::kick] END [numPools=" << poolMgr.mPools.size()
                  << "] [this = " << this << "]";
    }

    // Requires uploadMutex to be held by caller.
    void notifyWorkersLocked()
    {
        ++workerWakeEpoch;
        workerWakeCv.notify_all();
    }

    void notifyWorkers()
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        notifyWorkersLocked();
    }

    void notifyNetworkDisconnect()
    {
        disconnectEpoch.fetch_add(1, std::memory_order_release);
        notifyWorkers();
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
        WSUPLOAD_TRACE << "[UploadEngine::Impl::nextEligible] BEGIN [fileList.size=" << fileList.size()
                  << "] [this = " << this << "]";

        bool consecutive = true;
        if (!fileList.empty())
        {
            auto it = nextIt;
            for (std::size_t scanned = 0; scanned < fileList.size(); ++scanned)
            {
                if (it == fileList.end())
                {
                    it = fileList.begin();
                }

                WsUploadFile* f = *it;
                if (!f)
                {
                    WSUPLOAD_TRACE << "[UploadEngine::Impl::nextEligible] !f -> continue [this = "
                              << this << "]";
                    ++it;
                    continue;
                }

                const bool poolEligible = !f->hasPool() || f->mPool == requestingPool;
                if (poolEligible && !f->paused() && f->continuingUpload(currentTime) &&
                    f->hasPendingBytesOrEofToSend())
                {
                    // Retiring pools only drain already-owned files, shall not pick any new unbound work.
                    if (requestingPool && requestingPool->mRetiring && !f->hasPool())
                    {
                        ++it;
                        continue;
                    }

                    const auto& hint = f->sessionUrlHint();
                    if (requiredSessionUrl)
                    {
                        if (hint.empty() || hint != *requiredSessionUrl)
                        {
                            ++it;
                            continue;
                        }
                    }
                    else if (!hint.empty())
                    {
                        // Reserved for the pool bound to this specific session URL.
                        ++it;
                        continue;
                    }

                    if (f->size() >= min && (!max || f->size() < max))
                    {
                        WSUPLOAD_TRACE << "[UploadEngine::Impl::nextEligible] f->size(=" << f->size()
                                  << ") >= min(=" << min << ") && (!max(=" << max
                                  << ") || f->size(=" << f->size() << ") < max(=" << max
                                  << ")) -> candidate selected [consecutive=" << consecutive
                                  << "] [this = " << this << "]";
                        if (consecutive)
                        {
                            advanceNextItFrom(it);
                        }
                        return f;
                    }

                    consecutive = false;
                }

                ++it;
            }
        }
        WSUPLOAD_TRACE << "[UploadEngine::Impl::nextEligible] END - return nullptr [this = " << this
                  << "]";
        return nullptr;
    }

#ifndef NDEBUG
    bool hasEligibleFileForPoolForTesting(const m_off_t min,
                                          const m_off_t max,
                                          const std::string* requiredSessionUrl,
                                          const WsPool* requestingPool) const
    {
        for (WsUploadFile* f: fileList)
        {
            if (!f)
            {
                continue;
            }

            const bool poolEligible = !f->hasPool() || f->mPool == requestingPool;
            if (!poolEligible || f->paused() || !f->continuingUpload(currentTime) ||
                !f->hasPendingBytesOrEofToSend())
            {
                continue;
            }

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
                continue;
            }

            if (f->size() >= min && (!max || f->size() < max))
            {
                return true;
            }
        }

        return false;
    }
#endif

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
    // Atomic because poolWorkerThread reads queueVersion before acquiring
    // uploadMutex at wsupload.cpp:4315 (race with bumpQueueVersion writer).
    // Relaxed memory order is sufficient: the value is used as a change-counter
    // for "should I refreshPools?"; transitive ordering of work-state is
    // separately protected by uploadMutex.
    std::atomic<std::uint32_t> queueVersion{0};
    UploadEngine::Callbacks mCb{};

    WsPoolMgr poolMgr;

    mutable std::mutex uploadMutex;
    std::condition_variable workerWakeCv;
    std::thread uploadThread;
    std::atomic<bool> uploadThreadRunning{false};
    std::atomic<std::uint64_t> disconnectEpoch{0};
    std::uint64_t workerWakeEpoch{0};
    std::atomic<bool> mStopping{false};

    dstime currentTime{0};
    std::atomic<std::uint32_t> nextFileNo{1};
    unsigned char mPoolConnectionLimit{3};
    m_off_t mMaxUploadSpeed{0};
    m_off_t mUploadBudget{0};
    dstime mUploadBudgetLastDs{0};
    bool paused{false};
    inline static std::atomic<std::uint64_t> sInstanceCounter{0};
    std::uint64_t mInstanceId{0};

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
        const std::uint32_t newVersion =
            queueVersion.fetch_add(1, std::memory_order_relaxed) + 1;
        for (auto& pool: poolMgr.mPools)
        {
            if (pool && pool->mUploadingFile && inQueue.count(pool->mUploadingFile))
                pool->mUFTQversion = newVersion;
        }
    }

    template<class F>
    void withFile(Transfer& t, F&& fn)
    {
        WSUPLOAD_TRACE << "[UploadEngine::Impl::withFile] BEGIN [t=" << t.localfilename
                  << "] [this = " << this << "]";
        std::lock_guard<std::mutex> g(uploadMutex);
        auto it = files.find(&t);
        if (it == files.end() || !it->second)
            return;
        fn(*it->second);
        WSUPLOAD_TRACE << "[UploadEngine::Impl::withFile] END [this = " << this << "]";
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

    // Linear-scan fileList for f and erase it, rebasing nextIt to the successor on hit.
    // Caller must hold uploadMutex. Does NOT cycle nextIt back to begin() on end() —
    // callers that need that semantics must follow up with cycleNextIt().
    void eraseFromFileListLocked(WsUploadFile* f)
    {
        for (auto lit = fileList.begin(); lit != fileList.end(); ++lit)
        {
            if (*lit == f)
            {
                if (nextIt == lit)
                    ++nextIt;
                fileList.erase(lit);
                return;
            }
        }
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
        // Any in-flight chunk must have been requeued or closed out by closeWS() before
        // the owning pool-worker thread exits and destroys the WsConn; otherwise the
        // chunk is silently lost.
        assert(mChunksInFlight.empty() &&
               "WsConn destroyed with in-flight chunks; closeWS() must run first");
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
    WSUPLOAD_TRACE << "[WsConn::connectWS] BEGIN [this = " << this << "]";
    if (curl)
    {
        WSUPLOAD_TRACE << "[WsConn::connectWS] curl already exists, cleanup [this = " << this << "]";
        curl_easy_cleanup(curl);
    }
    curl = curl_easy_init();
    if (!curl)
    {
        WSUPLOAD_TRACE << "[WsConn::connectWS] curl_easy_init failed, return false [this = " << this
                  << "]";
        return false;
    }

    // Share CurlHttpIO settings but don't attach to its multi
    if (auto* cio = dynamic_cast<CurlHttpIO*>(mPool->mImpl->client.httpio))
    {
        WSUPLOAD_TRACE << "[WsConn::connectWS] configureWsEasy [curl=" << (void*)curl << "] [this = " <<
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
        WSUPLOAD_TRACE << "[WsConn::connectWS] curl_easy_perform success, set readyState=OPEN and call "
                     "onopen() -> return true [this = "
                  << this << "]";
        readyState = ReadyState::OPEN;
        onopen();
        return true;
    }
    WSUPLOAD_TRACE << "[WsConn::connectWS] curl_easy_perform failed, set readyState=CLOSED and return "
                 "false [res="
              << res << "] [strError=" << curl_easy_strerror(res) << "] [err=" << err
              << "] [this = " << this << "]";
    readyState = ReadyState::CLOSED;
    return false;
}
*/

bool WsConn::connectWS()
{
    WSUPLOAD_TRACE << "[WsConn::connectWS] BEGIN [this = " << this << "]";
    if (mPool->mImpl->stopping())
    {
        readyState.store(ReadyState::CLOSED, std::memory_order_relaxed);
        return false;
    }
    if (curl)
    {
        WSUPLOAD_TRACE << "[WsConn::connectWS] curl already exists, cleanup [this = " << this << "]";
        curl_easy_cleanup(curl);
        curl = nullptr;
    }
    readyState.store(ReadyState::CONNECTING, std::memory_order_relaxed);

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
            WSUPLOAD_TRACE << "[WsConn::connectWS] [client.wsPostToClientThread] BEGIN [this = " << self
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
                client.wsHandshakeForUpload(url, WSUPLOAD_HANDSHAKE_TIMEOUT_MS, &err));

            {
                std::lock_guard<std::mutex> g(baton->m);
                baton->easy.reset(e);
                baton->done = true;
                if (e)
                {
                    if (!err.empty())
                    {
                        WSUPLOAD_TRACE << "[WsConn::connectWS] [client.wsPostToClientThread] "
                                     "wsHandshakeForUpload failed, err="
                                  << err << ", set baton.easy=" << (void*)baton->easy.get()
                                  << " [this = " << self
                                  << "]";
                    }
                    else
                    {
                        WSUPLOAD_TRACE << "[WsConn::connectWS] [client.wsPostToClientThread] "
                                     "wsHandshakeForUpload success, set baton.easy="
                                  << (void*)baton->easy.get() << " [this = " << self << "]";
                    }
                }
                else
                {
                    WSUPLOAD_TRACE << "[WsConn::connectWS] [client.wsPostToClientThread] "
                                 "wsHandshakeForUpload returned nullptr, set baton.easy=nullptr [this = "
                              << self << "]";
                }
            }
            baton->cv.notify_one();
            WSUPLOAD_TRACE << "[WsConn::connectWS] [client.wsPostToClientThread] END [this = " << self
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
            readyState.store(ReadyState::CLOSED, std::memory_order_relaxed);
            return false;
        }

        if (std::chrono::steady_clock::now() >= waitDeadline)
        {
            WSUPLOAD_TRACE << "[WsConn::connectWS] handshake baton timed out -> readyState=CLOSED and "
                         "return false [this = "
                      << this << "]";
            readyState.store(ReadyState::CLOSED, std::memory_order_relaxed);
            return false;
        }

        baton->cv.wait_for(lk, std::chrono::milliseconds(WSUPLOAD_HANDSHAKE_CV_POLL_MS), [&]
                           {
                               return baton->done;
                           });
    }

    if (!baton->easy)
    {
        WSUPLOAD_TRACE
            << "[WsConn::connectWS] !baton.easy -> readyState=CLOSED and return false [this = "
            << this << "]";
        readyState.store(ReadyState::CLOSED, std::memory_order_relaxed);
        return false;
    }

    curl = baton->easy.release(); // worker thread exclusively owns the handle now
    readyState.store(ReadyState::OPEN, std::memory_order_relaxed);
    onopen(); // your existing callback
    WSUPLOAD_TRACE << "[WsConn::connectWS] END -> success, return true [this = " << this << "]";
    return true;
}

void WsConn::closeWS()
{
    WSUPLOAD_TRACE << "[WsConn::closeWS] BEGIN [this = " << this << "]";
    if (readyState.load(std::memory_order_relaxed) == ReadyState::CLOSED)
    {
        WSUPLOAD_TRACE << "[WsConn::closeWS] readyState=CLOSED, return [this = " << this << "]";
        return;
    }
    // Clear the deferred-close flag: once closeWS actually runs, any pending
    // request has been honoured. Worker thread is single-owner, so this is
    // a belt-and-suspenders write documenting intent.
    mPendingClose = false;
    readyState.store(ReadyState::CLOSED, std::memory_order_relaxed);
    resetBufferedSendState();
    onclose();
    WSUPLOAD_TRACE << "[WsConn::closeWS] END [this = " << this << "]";
}

void WsConn::onopen()
{
    WSUPLOAD_TRACE << "[WsConn::onopen] Connected to " << mPool->mUrl;
}

void WsConn::onclose()
{
    WSUPLOAD_TRACE << "[WsConn::onclose] BEGIN [Disconnected from " << mPool->mUrl
              << "] [this = " << this << "]";
    if (mPool)
    {
        WSUPLOAD_TRACE << "[WsConn::onclose] mPool->retryChunksOnTheWire(this) [this = " << this << "]";
        mPool->retryChunksOnTheWire(this);
    }
    WSUPLOAD_TRACE << "[WsConn::onclose] END [this = " << this << "]";
}

void WsConn::resetBufferedSendState() noexcept
{
    mBufs[0].reset();
    mBufs[1].reset();
    mCurBuf = 0;
    mInPos = 0;
    bufferedAmount = 0;
}

void WsConn::curlSend()
{
    WSUPLOAD_TRACE << "[WsConn::curlSend] BEGIN [readyState="
              << static_cast<int>(readyState.load(std::memory_order_relaxed))
              << "] [this = " << this << "]";
    if (readyState.load(std::memory_order_relaxed) != ReadyState::OPEN)
    {
        WSUPLOAD_TRACE << "[WsConn::curlSend] readyState != ReadyState::OPEN, return [this = " << this
                  << "]";
        return;
    }
    while (mBufs[static_cast<unsigned char>(mCurBuf)].sendWS(this, bufferedAmount))
        mCurBuf = !mCurBuf;
    WSUPLOAD_TRACE << "[WsConn::curlSend] END [this = " << this << "]";
}

void WsConn::curlRecv()
{
    WSUPLOAD_TRACE << "[WsConn::curlRecv] BEGIN [readyState="
              << static_cast<int>(readyState.load(std::memory_order_relaxed))
              << "] [this = " << this << "]";
    if (readyState.load(std::memory_order_relaxed) != ReadyState::OPEN)
    {
        WSUPLOAD_TRACE << "[WsConn::curlRecv] readyState != ReadyState::OPEN, return [this = " << this
                  << "]";
        return;
    }

    const struct curl_ws_frame* meta = nullptr;
    size_t recv = 0;
    unsigned drainedAfterPendingClose = 0;

    for (;;)
    {
        WSUPLOAD_TRACE << "[WsConn::curlRecv] curl_ws_recv(curl, mInBuf(=" << (void*)mInBuf
                  << "), sizeof(mInBuf)(=" << sizeof(mInBuf) << "), &recv, &meta) [this = " << this
                  << "]";
        const CURLcode res = curl_ws_recv(curl, mInBuf, sizeof(mInBuf), &recv, &meta);
        if (res == CURLE_OK && meta && !meta->bytesleft && recv > 0)
        {
            WSUPLOAD_TRACE << "[WsConn::curlRecv] res == CURLE_OK && meta && !meta->bytesleft && recv(="
                      << recv
                      << ") > 0 -> onmessage(mInBuf, static_cast<int>(recv)) [this = " << this
                      << "]";
            onmessage(mInBuf, static_cast<int>(recv));
            // If the prior onmessage requested a deferred close, keep draining
            // libcurl's recv pipeline. The curl handle remains valid because
            // closeWS() does not invoke curl_easy_cleanup (that only happens in
            // ~WsConn / the reconnect path). Break only when the state has
            // transitioned to non-OPEN for any reason OTHER than our own
            // pending-close request.
            if (readyState.load(std::memory_order_relaxed) != ReadyState::OPEN && !mPendingClose)
            {
                WSUPLOAD_TRACE << "[WsConn::curlRecv] onmessage closed connection -> break [this = "
                          << this << "]";
                break;
            }
            if (mPendingClose)
                ++drainedAfterPendingClose;
        }
        else
        {
            if (res != CURLE_AGAIN || meta)
            {
                WSUPLOAD_TRACE << "[WsConn::curlRecv] res(=" << res
                          << ") != CURLE_AGAIN || meta -> closeWS() [this = " << this << "]";
                closeWS();
            }
#ifndef NDEBUG
            else
            {
                // Count CURLE_AGAIN on recv (per-conn).
                ++mCurlAgainRecvCount;
            }
#endif
            WSUPLOAD_TRACE << "[WsConn::curlRecv] res(=" << res
                      << ") != CURLE_OK || !meta || meta->bytesleft(="
                      << (meta ? meta->bytesleft : 0) << ") > 0 || recv(=" << recv
                      << ") <= 0 -> break [this = " << this << "]";
            break;
        }
    }

    // Honour any deferred-close request now that the recv pipeline has been
    // drained. closeWS() is idempotent (guards on readyState==CLOSED), so a
    // second call from the CURLE_AGAIN-else branch above harmlessly no-ops.
    if (mPendingClose)
    {
        WSUPLOAD_TRACE << "[WsConn::curlRecv] drained " << drainedAfterPendingClose
                  << " frames before closeWS [this = " << this << "]";
        closeWS();
    }
    WSUPLOAD_TRACE << "[WsConn::curlRecv] END [this = " << this << "]";
}

void WsConn::failFileLocked(WsUploadFile* uf,
                            const std::uint32_t fileno,
                            const FailReason reason,
                            const int apierr,
                            const m_off_t aux,
                            const UploadEngine::FailureDisposition disp)
{
    mPool->purgeFileLocked(fileno);
    uf->uploadFailed(reason);
    if (mPool->mImpl->mCb.onFail)
    {
        mPool->mImpl->mCb.onFail(uf->transfer(), apierr, aux, disp);
    }
}

bool WsConn::handleBytesConfirmedOverflow(WsUploadFile* uf)
{
    if (uf->bytesConfirmed() <= uf->size())
        return false;

    LOG_warn << "[WsConn::onmessage] uf->bytesConfirmed(=" << uf->bytesConfirmed()
             << ") > uf->size(=" << uf->size()
             << ") -> server confirmed beyond expected size, failing upload [this = "
             << this << "]";
    failFileLocked(uf,
                   uf->fileno(),
                   FailReason::StateLost,
                   API_EINTERNAL,
                   uf->bytesConfirmed(),
                   UploadEngine::FailureDisposition::Retryable);
    return true;
}

void WsConn::onmessage(const char* msg, const int len)
{
    WSUPLOAD_TRACE << "[WsConn::onmessage] BEGIN [len=" << len << "] [this = " << this << "]";
    switch (detail::validateInboundFrame(msg, len))
    {
        case detail::InboundFrameValidationResult::TooShort:
            LOG_warn << "WsUpload: invalid server msg len=" << len;
            mPendingClose = true;
            return;

        case detail::InboundFrameValidationResult::BadCrc:
            LOG_warn << "WsUpload: inbound CRC failed, byteLength=" << len;
            mPendingClose = true;
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
    m_off_t chunkPos = response->chunkpos;
    auto event = static_cast<WsApiServerEvent>(response->event);
    WSUPLOAD_TRACE << "[WsConn::onmessage] response->fileno=" << response->fileno
              << " response->chunkpos=" << response->chunkpos
              << " response->event=" << static_cast<int>(event) << " [this = " << this << "]";
    WsChunk chunk;

    WsUploadFile* uf = mPool->findFile(response->fileno, *mPool->mImpl);
    if (!uf)
    {
        WSUPLOAD_TRACE << "[WsConn::onmessage] !uf -> return [this = " << this << "]";
        return; // file cancelled or moved
    }

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    int hookEvent = static_cast<int>(event);
    WsUploadServerEventAction hookAction = WsUploadServerEventAction::None;
    DEBUG_TEST_HOOK_WSUPLOAD_SERVER_EVENT(response->fileno, hookEvent, chunkPos, hookAction);
    if (hookAction == WsUploadServerEventAction::Drop)
    {
        LOG_warn << "WsUpload: debug hook dropped server event=" << static_cast<int>(event)
                 << " fileno=" << response->fileno;
        return;
    }
    if (hookAction == WsUploadServerEventAction::Modify)
    {
        LOG_warn << "WsUpload: debug hook modified server event=" << static_cast<int>(event)
                 << " -> " << hookEvent << " chunkpos=" << response->chunkpos << " -> " << chunkPos
                 << " fileno=" << response->fileno;
        event = static_cast<WsApiServerEvent>(hookEvent);
    }
#endif

    if (event < WsApiServerEvent::UploadCompleted || event == WsApiServerEvent::FinalDataIngested)
    {
        if (static_cast<int>(event) < 0)
        {
            WSUPLOAD_TRACE << "[WsConn::onmessage] response->event < 0 -> "
                         "uf->uploadFailed(FailReason::ServerError) [this = "
                      << this << "]";
            // A server-side error aborts the current upload attempt.
            // Purge in-flight/resend state for this file before unbinding it from the pool.
            failFileLocked(uf,
                           response->fileno,
                           FailReason::ServerError,
                           static_cast<int>(event),
                           chunkPos,
                           UploadEngine::FailureDisposition::Retryable);
            return;
        }

        WSUPLOAD_TRACE << "[WsConn::onmessage] response->event >= 0 -> chunk.pos = -1 [this = " << this
                  << "]";
        chunk.pos = -1;
        const bool shouldApply = event == WsApiServerEvent::ChunkIngested ||
                                 event == WsApiServerEvent::AlreadyOnServer ||
                                 event == WsApiServerEvent::FinalDataIngested;
        for (auto it = mChunksInFlight.begin(); it != mChunksInFlight.end(); ++it)
        {
            if (it->first.pos == chunkPos && it->first.fileno == response->fileno)
            {
                chunk = it->first;
                if (shouldApply)
                    it->second.apply(chunk.pos, *uf);
                mChunksInFlight.erase(it);
                if (mPool->mNumChunksInFlight > 0)
                    --mPool->mNumChunksInFlight;
                break;
            }
        }
        if (chunk.pos < 0)
        {
            LOG_warn << "WsUpload: PROTOCOL - acked chunk not in-flight [pos=" << chunkPos
                     << " fileno=" << response->fileno << " type=" << static_cast<int>(event)
                     << "]";
            return;
        }
    }

    if (len == kWsChunkResponseHeaderSize)
    {
        WSUPLOAD_TRACE << "[WsConn::onmessage] len == kWsChunkResponseHeaderSize -> "
                     "uf->uploadFailed(FailReason::Unknown) [this = "
                  << this << "]";
        // Unknown/invalid server response for this upload attempt.
        failFileLocked(uf,
                       response->fileno,
                       FailReason::Unknown,
                       API_EAGAIN,
                       0,
                       UploadEngine::FailureDisposition::Retryable);
        return;
    }

    switch (event)
    {
        case WsApiServerEvent::ChunkIngested: // non-final
#ifndef NDEBUG
            // Throttle-recovery latency = pause-start to first chunk-ack on this conn.
            if (mPauseStartedAtMs)
            {
                const auto deltaDs = static_cast<dstime>(SteadyTime::ds()) - mPauseStartedAtMs;
                const auto deltaMs = static_cast<std::uint64_t>(dsToMs(deltaDs));
                ++mThrottleRecoveryAckSamples;
                mThrottleRecoveryAckTotalMs += deltaMs;
                if (deltaMs > mThrottleRecoveryAckMaxMs) mThrottleRecoveryAckMaxMs = deltaMs;
                mPauseStartedAtMs = 0;
            }
#endif
            WSUPLOAD_TRACE
                << "[WsConn::onmessage] response->event == 1 chunk ingested (non-final) [chunk.len="
                << chunk.len << "] [this = " << this << "]";
            if (chunk.len)
            {
                uf->onServerConfirmedBytes(chunk.len);
                if (mPool->mImpl->mCb.onProgress && uf->progressReportDue(SteadyTime::ds()))
                    mPool->mImpl->mCb.onProgress(uf->transfer(), uf->bytesConfirmed());
                if (handleBytesConfirmedOverflow(uf))
                    break;
            }
            mPool->mImpl->poolMgr.mActiveFiles.insert(uf);
            break;

        case WsApiServerEvent::FinalDataIngested:
            WSUPLOAD_TRACE << "[WsConn::onmessage] response->event == 7 final data ingested (server "
                         "knows file is complete) [chunk.len="
                      << chunk.len << "] [this = " << this << "]";
            if (chunk.len)
            {
                uf->onServerConfirmedBytes(chunk.len);
                if (handleBytesConfirmedOverflow(uf))
                    break;
            }
            if (mPool->mImpl->mCb.onProgress)
                mPool->mImpl->mCb.onProgress(uf->transfer(), uf->bytesConfirmed());
            mPool->mImpl->poolMgr.mActiveFiles.insert(uf);
            break;

        case WsApiServerEvent::AlreadyOnServer:
            WSUPLOAD_TRACE << "[WsConn::onmessage] response->event == 2 already on server (after "
                         "reconnect) [pos="
                      << chunk.pos << "] [len=" << chunk.len << "] [fileno=" << chunk.fileno
                      << "] [this = " << this << "]";
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
            WSUPLOAD_TRACE << "[WsConn::onmessage] response->event == 4 upload completed -> "
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
                failFileLocked(uf,
                               response->fileno,
                               FailReason::Protocol,
                               API_EINTERNAL,
                               0,
                               UploadEngine::FailureDisposition::Retryable);
                break;
            }

            int payLen = static_cast<unsigned char>(msg[kWsChunkResponseHeaderSize]);
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
            if (globalMegaTestHooks.onWsUploadCompletionPayloadLen)
            {
                globalMegaTestHooks.onWsUploadCompletionPayloadLen(payLen);
            }
#endif
            const int maxPayloadLen = len - kCompletionPrefixLen - kTrailerCrcLen;
            if (payLen > maxPayloadLen)
            {
                LOG_warn << "WsUpload: invalid completion payload len=" << payLen
                         << " frame len=" << len;
                failFileLocked(uf,
                               response->fileno,
                               FailReason::Protocol,
                               API_EINTERNAL,
                               0,
                               UploadEngine::FailureDisposition::Retryable);
                break;
            }

#ifndef NDEBUG
            mPool->recordUploadCompletedLocked(response->fileno);
#endif
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
        {
            const dstime throttleDs = static_cast<dstime>(chunkPos / 100 + 1);
#ifndef NDEBUG
            // Tag opcode=6 in code-counts and anchor pause start for recovery latency.
            if (mPool) ++mPool->mThrottleEventCodeCounts[6];
            mPauseStartedAtMs = static_cast<dstime>(SteadyTime::ds());
#endif
            LOG_info << "[WsUpload] Server requested sending to pause for " << chunkPos
                     << " ms";
            WSUPLOAD_TRACE << "[WsConn::onmessage] response->event == 6 throttle (ms) -> ds -> "
                         "mPool->pauseSending(throttleDs) [this = "
                      << this << "]";
            mPool->pauseSending(throttleDs);
            break;
        }

        default:
            WSUPLOAD_TRACE << "[WsConn::onmessage] response->event == "
                      << static_cast<int>(event)
                      << " -> unknown server opcode=" << static_cast<int>(event)
                      << " -> break [this = " << this << "]";
            break;
    }
    WSUPLOAD_TRACE << "[WsConn::onmessage] END [this = " << this << "]";
}

void WsConn::sendChunkData(const std::uint32_t fileno,
                           const m_off_t pos,
                           const char* data,
                           const int len)
{
    WSUPLOAD_TRACE << "[WsConn::sendChunkData] BEGIN [fileno=" << fileno << "] [pos=" << pos
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

    WSUPLOAD_TRACE << "[WsConn::sendChunkData] senddata(bufIdx, reinterpret_cast<const char*>(&header), "
                 "static_cast<int>(sizeof header)) [bufIdx="
              << static_cast<int>(bufIdx) << "] [this = " << this << "]";
    senddata(bufIdx, reinterpret_cast<const char*>(&header), static_cast<int>(sizeof header));
    WSUPLOAD_TRACE << "[WsConn::sendChunkData] senddata(bufIdx, data, len) [bufIdx="
              << static_cast<int>(bufIdx) << "] [this = " << this << "]";
    senddata(bufIdx, data, len);
    WSUPLOAD_TRACE << "[WsConn::sendChunkData] END [this = " << this << "]";
}

// ========== WsPoolThread (ctor after WsPool complete) ==========
WsPoolThread::WsPoolThread(WsPool* pool):
    t(&WsPool::poolWorkerThread, pool, this)
{
    WSUPLOAD_TRACE << "[WsPoolThread::WsPoolThread] pool=" << (void*)pool << " [this = " << this << "]";
}

// ========== ChunkFingerprintMacUpdate ==========
// Out-of-line: the body calls WsUploadFile::queueConfirmedChunkMacs and so
// requires the full WsUploadFile definition (kept in this translation unit).
void ChunkFingerprintMacUpdate::apply(const m_off_t confirmedPos, WsUploadFile& file)
{
    if (confirmedPos != pos)
        return;
    if (macs.size())
    {
        file.queueConfirmedChunkMacs(std::move(macs));
    }
}

// ========== WsBuf ==========
bool WsBuf::sendWS(WsConn* ws, int& bufferedAmount)
{
    WSUPLOAD_TRACE << "[WsBuf::sendWS] BEGIN [this = " << this << "]";
    if (mSendPos >= mDataLen)
    {
        WSUPLOAD_TRACE << "[WsBuf::sendWS] mSendPos(=" << mSendPos << ") >= mDataLen(=" << mDataLen
                  << ") -> return false [this = " << this << "]";
        return false;
    }

    size_t sent = 0;
    WSUPLOAD_TRACE << "[WsBuf::sendWS] curl_ws_send(ws->curl, buf(=" << (void*)buf
              << ") + mSendPos(=" << mSendPos << ") = " << (void*)(buf + mSendPos)
              << ", mDataLen(=" << mDataLen << ") - mSendPos(=" << mSendPos
              << ") = " << (mDataLen - mSendPos) << ", &sent, 0, CURLWS_BINARY) [this = " << this
              << "]";
    const std::size_t remaining = static_cast<std::size_t>(mDataLen - mSendPos);
    const CURLcode res = curl_ws_send(ws->curl, buf + mSendPos, remaining, &sent, 0, CURLWS_BINARY);
    if (res == CURLE_OK)
    {
        WSUPLOAD_TRACE << "[WsBuf::sendWS] res == CURLE_OK -> mSendPos(=" << mSendPos
                  << ") += static_cast<int>(sent(=" << sent
                  << ")), bufferedAmount(=" << bufferedAmount
                  << ") -= static_cast<int>(sent(=" << sent << ")) [this = " << this << "]";
        mSendPos += static_cast<int>(sent);
        bufferedAmount -= static_cast<int>(sent);
        if (mSendPos == mDataLen)
        {
            WSUPLOAD_TRACE << "[WsBuf::sendWS] mSendPos(=" << mSendPos << ") == mDataLen(=" << mDataLen
                      << ") -> reset() && return true [this = " << this << "]";
            reset();
            return true;
        }
        WSUPLOAD_TRACE << "[WsBuf::sendWS] mSendPos(=" << mSendPos << ") != mDataLen(=" << mDataLen
                  << ") -> return false [this = " << this << "]";
        return false;
    }
    if (res != CURLE_AGAIN)
    {
        WSUPLOAD_TRACE << "[WsBuf::sendWS] res(=" << res << ") != CURLE_AGAIN(=" << CURLE_AGAIN
                  << ") -> ws->closeWS() [this = " << this << "]";
        ws->closeWS();
    }
#ifndef NDEBUG
    else
    {
        // Count CURLE_AGAIN on send (per-conn).
        ++ws->mCurlAgainSendCount;
    }
#endif
    WSUPLOAD_TRACE << "[WsBuf::sendWS] res(=" << res << ") != CURLE_OK -> return false [this = " << this
              << "]";
    return false;
}

#ifndef NDEBUG
void WsPool::recordWsUploadStatsSampleLocked(const UploadEngine::Impl& impl)
{
    const auto now = steadyMs();
    if (!mLastCounterSampleMs)
    {
        mLastCounterSampleMs = now;
        return;
    }

    const auto elapsedMs = now - mLastCounterSampleMs;
    mLastCounterSampleMs = now;
    if (!elapsedMs)
    {
        return;
    }

    const bool hasEligibleFile = impl.hasEligibleFileForPoolForTesting(mMinFileSize,
                                                                       mMaxFileSize,
                                                                       mPinned ? &mUrl : nullptr,
                                                                       this);
    if (!hasEligibleFile)
    {
        return;
    }

    ++mEligibleFileSampleCount;

    const unsigned openConnections = countOpenConnectionsLocked();
    const unsigned connectionsWithInFlight = countConnectionsWithInFlightLocked();
    const bool currentFileDrained =
        mUploadingFile && !mUploadingFile->hasPendingBytesOrEofToSend();

    if (currentFileDrained && mNumChunksInFlight > 0 && openConnections > 0 &&
        connectionsWithInFlight >= openConnections)
    {
        mAllChunksInFlightBlockedMs += elapsedMs;
        ++mBlockedByInFlightSampleCount;
    }

    if (openConnections > connectionsWithInFlight)
    {
        const auto idleConnections =
            static_cast<std::uint64_t>(openConnections - connectionsWithInFlight);
        mIdleEligibleConnectionMs += elapsedMs * idleConnections;
        mIdleEligibleConnectionSampleCount += idleConnections;
    }
}
#endif

// ========== WsPool ==========

WsUploadFile* WsPool::findPreflightReadyCandidate(UploadEngine::Impl& impl)
{
    const std::size_t scanBudget = impl.fileList.size();
    for (std::size_t scanned = 0; scanned < scanBudget; ++scanned)
    {
        auto* candidate = impl.nextEligible(mMinFileSize, mMaxFileSize, mPinned ? &mUrl : nullptr, this);
        if (!candidate)
        {
            break;
        }

        if (impl.mCb.preflightStart)
        {
            const auto preflightResult = impl.mCb.preflightStart(candidate->transfer());
            if (preflightResult != UploadEngine::PreflightStartResult::Ready)
            {
                mPreflightPending = true;
                continue;
            }
        }

        // A ready candidate was selected in this round, clear pending hints.
        mPreflightPending = false;
        return candidate;
    }

    return nullptr;
}

bool WsPool::getWsUploadFile(const dstime now, UploadEngine::Impl& impl)
{
    // Mark true when we observe preflight pending (queued/running) so worker threads
    // use short CV waits instead of coarse decisecond sleeps.
    mPreflightPending = false;

    const std::uint32_t implQueueVersion = impl.queueVersion.load(std::memory_order_relaxed);
    if (mUploadingFile && !mUploadingFile->paused() && mUploadingFile->continuingUpload(now) &&
        mUploadingFile->hasPendingBytesOrEofToSend() && mUFTQversion == implQueueVersion)
    {
        WSUPLOAD_TRACE << "[WsPool::getWsUploadFile] mUploadingFile->paused()=false && "
                     "mUploadingFile->continuingUpload(now) && mUFTQversion(="
                  << mUFTQversion << ") == impl.queueVersion(=" << implQueueVersion
                  << ") -> return true [this = " << this << "]";
        return true;
    }

    // Back‑pressure gate: defer starting a new file while FA pipeline is saturated
    if (impl.mCb.canStartAnotherFile && !impl.mCb.canStartAnotherFile())
    {
        WSUPLOAD_TRACE << "[WsPool::getWsUploadFile] impl.mCb.canStartAnotherFile=true && "
                     "!impl.mCb.canStartAnotherFile() -> return false [this = "
                  << this << "]";
        return false;
    }

    WsUploadFile* f = findPreflightReadyCandidate(impl);
    if (!f)
    {
        WSUPLOAD_TRACE << "[WsPool::getWsUploadFile] !f -> return false [this = " << this << "]";
        return false;
    }

    WSUPLOAD_TRACE << "[WsPool::getWsUploadFile] candidate selected [f=" << (void*)f
              << "] [this = " << this << "]";

    // snapshot write-once crypto material now that preflightStart has
    // guaranteed f->transfer().transferkey and ctriv are populated.
    f->snapshotCryptoMaterial(f->transfer());
    // Defer start-marking until preflight succeeds so failed preflight does not leave
    // the file stuck in an active attempt state.
    f->setUploadStart(impl.currentTime);

    if (mUploadingFile != f)
    {
        WSUPLOAD_TRACE << "[WsPool::getWsUploadFile] mUploadingFile(=" << (void*)mUploadingFile
                  << ") != f(=" << (void*)f << ") -> picking file fileno=" << f->fileno()
                  << " for [" << mMinFileSize << "," << mMaxFileSize
                  << ") pool and set mUFTQversion(=" << implQueueVersion
                  << ") = impl.queueVersion(=" << implQueueVersion << ") [this = " << this
                  << "]";
#ifndef NDEBUG
        assignUploadingFileLocked(f);
#else
        mUploadingFile = f;
#endif
        mUFTQversion = implQueueVersion;
        mUploadingFile->setPool(*this);
        if (impl.mCb.onStart)
            impl.mCb.onStart(mUploadingFile->transfer());
    }
    else
    {
        WSUPLOAD_TRACE << "[WsPool::getWsUploadFile] mUploadingFile(=" << (void*)mUploadingFile
                  << ") == f(=" << (void*)f << ") -> return true [this = " << this << "]";
    }
    return true;
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
    WSUPLOAD_TRACE << "[WsPool::nextChunk] BEGIN [this = " << this << "]";
    // queued retry first
    while (!mToResend.empty())
    {
        chunk = mToResend.front();
        // Discard stale resend entries whose target file is gone or whose byte range no
        // longer fits the current attempt (e.g. resetAttemptState rewound mHeadPos = 0
        // but the entry was queued under a previous attempt). Re-sending out-of-bounds
        // bytes would break the server-side sliding window; bytes the server already has
        // would be re-acked via opcode 2, but never-fitting ranges must be dropped here.
        {
            WsUploadFile* const ufStale = findFile(chunk.fileno, impl);
            if (!ufStale || (chunk.len && chunk.pos + chunk.len > ufStale->size()))
            {
                WSUPLOAD_TRACE << "[WsPool::nextChunk] discard stale resend entry [pos=" << chunk.pos
                          << "] [len=" << chunk.len << "] [fileno=" << chunk.fileno << "]";
                mToResend.erase(mToResend.begin());
                continue;
            }
        }
        if (!impl.consumeUploadBudget(static_cast<m_off_t>(chunk.len), retryAfterDs))
        {
            // Pool is alive but throttled. Keep it fresh so SERVERTIMEOUT does not
            // force an unnecessary refresh.
            mLastActive = impl.currentTime;
            return false;
        }
        mToResend.erase(mToResend.begin());
        WSUPLOAD_TRACE << "WsUpload: resending chunk pos=" << chunk.pos << " len=" << chunk.len
                  << " fileno=" << chunk.fileno;
        if (findFile(chunk.fileno, impl))
            return true; // file still valid?
    }

    if (impl.paused)
    {
        WSUPLOAD_TRACE << "[WsPool::nextChunk] impl.paused=true -> return false [this = " << this << "]";
        return false;
    }

    while (getWsUploadFile(impl.currentTime, impl))
    {
        if (mUploadingFile->headPos() < mUploadingFile->size() || !mUploadingFile->eofSet())
        {
            WSUPLOAD_TRACE << "[WsPool::nextChunk] mUploadingFile->headPos(="
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
                    // Pool is alive but throttled. Keep it fresh so SERVERTIMEOUT does not
                    // force an unnecessary refresh.
                    mLastActive = impl.currentTime;
                    return false;
                }
                mUploadingFile->advanceHead(chunk.len);
            }

            mLastActive = impl.currentTime;
            return true;
        }
        WSUPLOAD_TRACE << "[WsPool::nextChunk] mUploadingFile->headPos() < mUploadingFile->size() || "
                     "!mUploadingFile->eofSet() -> mUploadingFile = nullptr [this = "
                  << this << "]";
        clearUploadingFileLocked(); // done with this file
    }
    WSUPLOAD_TRACE << "[WsPool::nextChunk] END - !getWsUploadFile -> return false [this = " << this
              << "]";
    return false;
}

void WsPool::retryChunksOnTheWire(WsConn* ws)
{
    std::lock_guard<std::mutex> g(mImpl->uploadMutex);
    retryChunksOnTheWireLocked(ws);
}

void WsPool::retryChunksOnTheWireLocked(WsConn* ws)
{
    WSUPLOAD_TRACE << "WsUpload: WS to " << mUrl << " lost; rescheduling " << ws->mChunksInFlight.size()
              << " in-flight chunks";
    for (auto& p: ws->mChunksInFlight)
    {
        mToResend.push_back(p.first);
    }
    mNumChunksInFlight -= static_cast<int>(ws->mChunksInFlight.size());
    ws->mChunksInFlight.clear();
}

void WsPool::retryChunkLocked(const WsChunk& chunk)
{
    // Per-chunk retry cap for CrcFailed (opcode 3). Transfer-level retry is already
    // covered by Transfer::failed; this cap is specifically for the narrow case where
    // the server keeps rejecting the same chunk via opcode 3, which would otherwise
    // loop forever consuming bandwidth without escalating to the app.
    static constexpr unsigned kMaxChunkRetries = 10;
    WsChunk retry = chunk;
    ++retry.retryCount;
    if (retry.retryCount > kMaxChunkRetries)
    {
        LOG_warn << "[WsPool::retryChunkLocked] per-chunk retry cap hit, failing upload "
                    "[fileno="
                 << chunk.fileno << "] [pos=" << chunk.pos << "] [retryCount=" << retry.retryCount
                 << "] [this = " << this << "]";
        WsUploadFile* const uf = findFile(chunk.fileno, *mImpl);
        purgeFileLocked(chunk.fileno);
        if (uf)
        {
            uf->uploadFailed(FailReason::Protocol);
            if (mImpl->mCb.onFail)
                mImpl->mCb.onFail(uf->transfer(),
                                  API_EINTERNAL,
                                  chunk.pos,
                                  UploadEngine::FailureDisposition::Retryable);
        }
        return;
    }
    mToResend.push_back(retry);
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

void WsPool::applyInFlight(const std::uint32_t fileno)
{
    std::lock_guard<std::mutex> g(mImpl->uploadMutex);
    applyInFlightLocked(fileno);
}

bool WsPool::sendChunk(WsConn* ws, UploadEngine::Impl& impl, dstime* retryAfterDs)
{
    if (ws->readyState.load(std::memory_order_relaxed) != WsConn::ReadyState::OPEN || !ws->haveSpace())
    {
        WSUPLOAD_TRACE << "[WsPool::sendChunk] ws->readyState="
                  << static_cast<int>(ws->readyState.load(std::memory_order_relaxed))
                  << " (ReadyState::OPEN=" << static_cast<int>(WsConn::ReadyState::OPEN)
                  << ") ws->haveSpace=" << ws->haveSpace() << " -> return false [this = " << this
                  << "]";
        return false;
    }

    WsChunk chunk;
    // Zero the out param so the worker loop's initializer (0) controls the
    // fallback path.
    if (retryAfterDs)
    {
        *retryAfterDs = 0;
    }

    if (!nextChunk(chunk, impl, retryAfterDs))
    {
        WSUPLOAD_TRACE << "[WsPool::sendChunk] !nextChunk -> return false [this = " << this << "]";
        return false;
    }

    static thread_local std::unique_ptr<char[]> tlsBuf;
    if (!tlsBuf)
    {
        WSUPLOAD_TRACE << "[WsPool::sendChunk] !tlsBuf -> tlsBuf.reset(new char[kMiB]) [this = " << this
                  << "]";
        tlsBuf.reset(new char[kMiB]);
    }

    WsUploadFile* uf = findFile(chunk.fileno, impl);
    const auto expectedGeneration = uf ? uf->workGeneration() : 0;
    bool interruptedByStateChange = false;
    const bool okRead = uf && !uf->aborted() &&
                        (chunk.len == 0 || uf->readData(tlsBuf.get(),
                                                        chunk.pos,
                                                        chunk.len,
                                                        impl.uploadMutex,
                                                        expectedGeneration,
                                                        &interruptedByStateChange));
    if (okRead)
    {
        uf = findFile(chunk.fileno, impl);
        if (!uf)
        {
            WSUPLOAD_TRACE << "[WsPool::sendChunk] drop chunk after read (file no longer in map) [pos="
                      << chunk.pos << "] [len=" << chunk.len << "] [fileno=" << chunk.fileno
                      << "] [this = " << this << "]";
            return false;
        }
        if (!uf->inPool() || uf->aborted())
        {
            WSUPLOAD_TRACE << "[WsPool::sendChunk] drop chunk after read (no longer in pool or aborted) "
                         "[pos="
                      << chunk.pos << "] [len=" << chunk.len << "] [fileno=" << chunk.fileno
                      << "] [this = " << this << "]";
            return false;
        }
        if (uf->paused())
        {
            WSUPLOAD_TRACE << "[WsPool::sendChunk] requeue chunk after read (paused) [pos=" << chunk.pos
                      << "] [len=" << chunk.len << "] [fileno=" << chunk.fileno
                      << "] [this = " << this << "]";
            mToResend.push_back(chunk);
            return false;
        }
        assert(uf->isUploading() && "invariant: upload must be active after successful read "
                                    "with inPool, !aborted, !paused checks passing; the "
                                    "mWorkGeneration match inside readData guarantees the "
                                    "attempt did not reset underneath us");

        ChunkFingerprintMacUpdate update(chunk.pos);
        if (chunk.len)
        {
            const unsigned pad =
                static_cast<unsigned>((-(int)chunk.len) & (SymmCipher::BLOCKSIZE - 1));
            if (pad)
                std::memset(tlsBuf.get() + chunk.len, 0, pad);

            // SDK-5360 worker-parallel encrypt/MAC: snapshot crypto material to
            // local stack and release impl.uploadMutex around encrypt+MAC so
            // multiple worker threads can encrypt concurrently using their own
            // thread_local SymmCipher (see mega::ws::encryptChunk). Re-validate
            // inPool/aborted/paused after re-locking, mirroring the existing
            // post-readData re-checks above.
            std::array<byte, SymmCipher::KEYLENGTH> localKey = uf->transferKey();
            const int64_t localCtrIv = uf->ctrIv();
            const m_off_t localChunkPos = chunk.pos;
            const int localChunkLen = chunk.len;

            impl.uploadMutex.unlock();
            chunkmac_map macs = mega::ws::encryptChunk(localKey,
                                                      localCtrIv,
                                                      localChunkPos,
                                                      localChunkLen,
                                                      reinterpret_cast<byte*>(tlsBuf.get()));
            impl.uploadMutex.lock();

            // Re-validate uf state. uf may have been pool-detached or aborted while
            // we were unlocked. Re-look up by fileno to also catch the (rare) case
            // where uf was replaced.
            uf = findFile(chunk.fileno, impl);
            if (!uf || !uf->inPool() || uf->aborted())
            {
                LOG_debug << "[WsPool::sendChunk] drop chunk after lock-released encrypt "
                             "(uf invalidated) [pos="
                          << localChunkPos << "] [fileno=" << chunk.fileno
                          << "] [this = " << this << "]";
                return false;
            }
            if (uf->paused())
            {
                LOG_debug << "[WsPool::sendChunk] requeue chunk after lock-released encrypt "
                             "(paused) [pos="
                          << localChunkPos << "] [fileno=" << chunk.fileno
                          << "] [this = " << this << "]";
                mToResend.push_back(chunk);
                return false;
            }
            update = ChunkFingerprintMacUpdate(chunk.pos, std::move(macs));
        }
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
        // Test seam: deterministically inject OVERQUOTA on this chunk-send by
        // short-circuiting through the same purgeFileLocked + mCb.onFail pattern
        // that the local-I/O-error path below uses. Drives the existing
        // Transfer::failed(API_EOVERQUOTA, ..., 0) plumbing — does NOT introduce
        // a new propagation path. Hook fires with impl.uploadMutex held, after uf
        // has been re-validated post-encrypt, so chunk.fileno / uf->transfer().tag
        // are stable for the duration of the call.
        {
            bool injectOverquota = false;
            DEBUG_TEST_HOOK_WS_CHUNK_SEND_OVERQUOTA(uf->transfer().tag, injectOverquota);
            if (injectOverquota)
            {
                LOG_warn << "[WsPool::sendChunk] test hook requested OVERQUOTA injection "
                            "[pos="
                         << chunk.pos << "] [fileno=" << chunk.fileno
                         << "] [tag=" << uf->transfer().tag << "] [this = " << this << "]";
                purgeFileLocked(chunk.fileno);
                uf->uploadFailed(FailReason::ServerError);
                if (impl.mCb.onFail)
                {
                    impl.mCb.onFail(uf->transfer(),
                                    API_EOVERQUOTA,
                                    chunk.pos,
                                    UploadEngine::FailureDisposition::Retryable);
                }
                return false;
            }
        }
#endif
        WSUPLOAD_TRACE << "[WsPool::sendChunk] uf->readData(tlsBuf.get(), chunk.pos, chunk.len, "
                     "impl.uploadMutex) -> emplace mChunksInFlight, sendChunkData && return true "
                     "[mNumChunksInFlight="
                  << mNumChunksInFlight << "] [this = " << this << "]";
        ws->mChunksInFlight.emplace_back(chunk, std::move(update));
#ifndef NDEBUG
        updateMaxConnectionsWithInFlightSeenLocked();
        if (chunk.len > 0)
        {
            recordFirstByteSentLocked(*uf);
        }
        // Per-conn chunks-in-flight high-water.
        if (ws->mChunksInFlight.size() > ws->mChunksInFlightHighWater)
            ws->mChunksInFlightHighWater = static_cast<unsigned>(ws->mChunksInFlight.size());
#endif
        uf->onRequestSent();
        ws->sendChunkData(chunk.fileno, chunk.pos, tlsBuf.get(), chunk.len);

        ++mNumChunksInFlight;
        return true;
    }
    uf = findFile(chunk.fileno, impl);
    if (uf && chunk.len && uf->inPool() && !uf->aborted() && interruptedByStateChange)
    {
        // Always requeue: if the attempt was paused, the chunk sends after unpause;
        // if the attempt was reset via resetAttemptState (setUploadStart / markFailed /
        // uploadFailed / uploadCompleted), mHeadPos is 0 and nextChunk will skip this
        // stale entry on drain. Never drop silently — there is no server opcode that
        // recovers a never-sent byte range.
        WSUPLOAD_TRACE << "[WsPool::sendChunk] requeue chunk after interrupted read/open [pos="
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

        if (uf->hasFailed())
        {
            // readData() already called markFailed() (which includes resetAttemptState);
            // only the pool detach that uploadFailed() normally does is still needed.
            uf->unsetPool();
        }
        else
        {
            uf->uploadFailed(FailReason::ReadFailed);
        }

        if (impl.mCb.onFail)
        {
            impl.mCb.onFail(uf->transfer(), API_EREAD, chunk.pos, uf->readFailureDisposition());
        }
        return false;
    }

    WSUPLOAD_TRACE << "[WsPool::sendChunk] read/open failed but file is no longer active in this pool"
              << " [pos=" << chunk.pos << "] [len=" << chunk.len << "] [fileno=" << chunk.fileno
              << "]";
    return false;
}

void WsPool::poolWorkerThread(WsPoolThread* th)
{
    int retryCount{0};
    dstime firstConnectFailureDs{0};
    std::uint32_t lastQueueVersion = mImpl->queueVersion.load(std::memory_order_relaxed);
    std::uint64_t seenDisconnectEpoch = mImpl->disconnectEpoch.load(std::memory_order_acquire);
    auto ws = std::make_unique<WsConn>(this);

    WSUPLOAD_TRACE << "[WsPool::poolWorkerThread] BEGIN [lastQueueVersion=" << lastQueueVersion
              << "] [this = " << this << "]";

    std::unique_lock<std::mutex> lk(mImpl->uploadMutex);
    while (!th->terminate)
    {
        const std::uint64_t disconnectEpoch =
            mImpl->disconnectEpoch.load(std::memory_order_acquire);
        if (disconnectEpoch != seenDisconnectEpoch)
        {
            seenDisconnectEpoch = disconnectEpoch;
            if (ws->readyState.load(std::memory_order_relaxed) != WsConn::ReadyState::CLOSED)
            {
                // Reuse normal close path so in-flight chunks are re-queued safely.
                ScopedUnlock unlock(lk);
                ws->closeWS();
            }
        }

        if (ws->readyState.load(std::memory_order_relaxed) == WsConn::ReadyState::CLOSED)
        {
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
            DEBUG_TEST_HOOK_WSPOOL_RECONNECT_ATTEMPT(this,
                                                    static_cast<unsigned>(retryCount),
                                                    firstConnectFailureDs);
#endif
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
                    WSUPLOAD_TRACE << "[WsPool::poolWorkerThread] retryCount=" << retryCount
                              << " -> continue [this = " << this << "]";
                    continue;
                }

                // Pinned session invalidation should be conservative: brief network glitches can
                // cause a few connect failures, but do not necessarily mean the pinned endpoint
                // is invalid. Require both a minimum retry count and sustained failure window
                // (60s).
                const auto failedForDs = SteadyTime::difference(nowDs, firstConnectFailureDs);
                if (mPinned && !mRetiring && retryCount >= 3 && failedForDs >= secondsToDs(60))
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
                            clearUploadingFileLocked();
                        }
                        mUFTQversion = mImpl->queueVersion.load(std::memory_order_relaxed);

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

                const std::uint32_t curQueueVersion =
                    mImpl->queueVersion.load(std::memory_order_relaxed);
                if (!mRetiring && lastQueueVersion != curQueueVersion)
                {
                    WSUPLOAD_TRACE << "[WsPool::poolWorkerThread] !mRetiring && lastQueueVersion("
                              << lastQueueVersion << ") != mImpl->queueVersion("
                              << curQueueVersion << ") -> refreshPools [this = " << this << "]";
                    mImpl->poolMgr.refreshPools();
                }

                WSUPLOAD_TRACE << "[WsPool::poolWorkerThread] !ok -> continue [retryCount=" << retryCount
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

        lastQueueVersion = mImpl->queueVersion.load(std::memory_order_relaxed);

        // recv server frames
        {
            ScopedUnlock unlock(lk);
            ws->curlRecv();
        }

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
        // B9 regression hook: lets a test deterministically force the WsConn into CLOSED
        // after a server ACK (e.g. a Throttle event) so it can observe reconnect pacing.
        {
            bool forceClose = false;
            DEBUG_TEST_HOOK_WSCONN_FORCE_CLOSE_NOW(ws.get(), this, mUrl, forceClose);
            if (forceClose)
            {
                WSUPLOAD_TRACE << "[WsPool::poolWorkerThread] force-close via test hook [this = "
                          << this << "]";
                ScopedUnlock unlock(lk);
                ws->closeWS();
                continue;
            }
        }
#endif

        // server throttle?
        if (throttledByServer())
        {
            WSUPLOAD_TRACE << "[WsPool::poolWorkerThread] throttledByServer=true -> continue [this = "
                      << this << "]";
#ifndef NDEBUG
            // Sample simultaneous throttled conns at throttle wait point.
            {
                unsigned simul = 0;
                for (const auto* c: mConns) if (c && c->mPauseStartedAtMs) ++simul;
                ++mSimultaneousThrottledConnsSamples;
                mSimultaneousThrottledConnsSum += simul;
                if (simul > mSimultaneousThrottledConnsMax) mSimultaneousThrottledConnsMax = simul;
            }
#endif
            {
                ScopedUnlock unlock(lk);
                SteadyTime::sleep_ds(1);
            }
#ifndef NDEBUG
            ++mThrottleSleepIters;
            mThrottleSleepMs += 100;
#endif
            continue;
        }

        // flush send buffers
        {
            ScopedUnlock unlock(lk);
            ws->curlSend();
        }

        if (!ws->haveSpace())
        {
            WSUPLOAD_TRACE << "[WsPool::poolWorkerThread] haveSpace=false -> continue [this = " << this
                      << "]";
            {
                ScopedUnlock unlock(lk);
                SteadyTime::sleep_ds(HAVE_SPACE_RETRY_DS);
            }
#ifndef NDEBUG
            ++mHaveSpaceFalseIters;
            mHaveSpaceFalseWaitMs += static_cast<std::uint64_t>(dsToMs(HAVE_SPACE_RETRY_DS));
#endif
            continue;
        }
        if (!ws->readyForData())
        {
            WSUPLOAD_TRACE << "[WsPool::poolWorkerThread] readyForData=false -> continue [this = "
                      << this << "]";
            {
                ScopedUnlock unlock(lk);
                SteadyTime::sleep_ds(READY_FOR_DATA_RETRY_DS);
            }
#ifndef NDEBUG
            ++mReadyForDataFalseIters;
            mReadyForDataFalseWaitMs += static_cast<std::uint64_t>(dsToMs(READY_FOR_DATA_RETRY_DS));
#endif
            continue;
        }

        // fetch & enqueue next chunk (unlocks around disk I/O internally)
        // Synchronous preflight on the pool worker means sendChunk=false fires only when the
        // queue is genuinely empty, the pool is paused, or throttling rejected the chunk - not
        // once per file the way the posted-lambda preflight did.
        dstime retryAfterDs = 0;  // BACKLOG_EMPTY_RETRY_DS fallback below
        const auto wakeEpochBeforeSend = mImpl->workerWakeEpoch;
#ifndef NDEBUG
        const std::uint64_t chunkPrepStartMs = steadyMs();
#endif
        if (!sendChunk(ws.get(), *mImpl, &retryAfterDs))
        {
            WSUPLOAD_TRACE << "[WsPool::poolWorkerThread] sendChunk=false -> continue [this = " << this
                      << "]";
            if (mPreflightPending)
            {
                // Preflight pending for at least one candidate file: wait on a
                // condition variable and wake immediately when preflight completes.
                static constexpr auto kPreflightPendingWait = std::chrono::milliseconds(100);
                mImpl->workerWakeCv.wait_for(
                    lk,
                    kPreflightPendingWait,
                    [&]
                    {
                        return mImpl->stopping() ||
                               mImpl->workerWakeEpoch != wakeEpochBeforeSend;
                    });
            }
            else
            {
                ScopedUnlock unlock(lk);
                SteadyTime::sleep_ds(retryAfterDs ? retryAfterDs : BACKLOG_EMPTY_RETRY_DS);
            }
#ifndef NDEBUG
            ++mBacklogEmptyIters;
            mBacklogEmptyMs +=
                static_cast<std::uint64_t>(retryAfterDs ? retryAfterDs : BACKLOG_EMPTY_RETRY_DS) *
                100;
#endif
            continue;
        }
#ifndef NDEBUG
        // Chunk-prep latency = entry-to-success of sendChunk (read + encrypt + queue).
        const std::uint64_t chunkPrepDtMs = steadyMs() - chunkPrepStartMs;
        mChunkPrepTotalMs += chunkPrepDtMs;
        if (chunkPrepDtMs > mChunkPrepMaxMs)
            mChunkPrepMaxMs = chunkPrepDtMs;
        ++mChunkPrepN;
#endif
    }

    if (ws->readyState.load(std::memory_order_relaxed) != WsConn::ReadyState::CLOSED)
    {
        ScopedUnlock unlock(lk);
        ws->closeWS();
    }

    th->terminated = true;
    WSUPLOAD_TRACE << "[WsPool::poolWorkerThread] END [lastQueueVersion=" << lastQueueVersion
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
// WsPoolMgr::curlIO and WsPoolMgr::ensurePinnedPool bodies live in the sibling
// translation unit src/transfer/ws/ws_curl.cpp; they only depend on the helpers
// exposed via wsupload_internal.h (WsPool, SteadyTime, ScopedUnlock,
// WSUPLOAD_CURL_MULTI_POLL_MS) plus the pinnedPoolConnectionLimit() forwarding
// helper defined at the bottom of this file so the .cpp split does not have to
// pull in the full UploadEngine::Impl definition.
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
        WsPool* const pool = mPools[i].get();
        if (!pool)
        {
            continue;
        }

        const bool hasPoolWork = pool->mNumPoolFiles || pool->mUploadingFile ||
                                 pool->mNumChunksInFlight || !pool->mToResend.empty();
        const bool pinnedHasReference = pool->mPinned && pinnedPoolHasReference(*pool, impl);
        const bool shouldScaleUp = !pool->mRetiring && (hasPoolWork || pinnedHasReference);
        const unsigned char targetConnLimit = impl.poolConnectionLimit();

#ifndef NDEBUG
        pool->recordWsUploadStatsSampleLocked(impl);
#endif

        if (shouldScaleUp && pool->mNumberOfConnections < targetConnLimit)
        {
            pool->setPoolNumConn(targetConnLimit);
        }

        if (pool->mNumberOfConnections > 1 && !shouldScaleUp &&
            SteadyTime::difference(impl.currentTime, pool->mLastActive) > POOLCONNKEEPALIVE)
        {
            pool->setPoolNumConn(1);
        }

        if (SteadyTime::difference(impl.currentTime, pool->mPoolCreationTime) > POOLFRESHNESS)
            refreshPools();

        if ((pool->mUploadingFile || pool->mNumChunksInFlight || !pool->mToResend.empty()) &&
            SteadyTime::difference(impl.currentTime, pool->mLastActive) > SERVERTIMEOUT)
            refreshPools();
    }

    // throughput display tick (server-acked)
    for (auto* uf: mActiveFiles)
        uf->mClientActiveFilesTick = false;
    mActiveFiles.clear();
}

// refreshPools posts a series of lambdas to the client thread via wsPostToClientThread().
// Those lambdas capture `MegaClient&` by reference and rely on the SDK-wide contract that
// MegaClient destroys its UploadEngine synchronously on the client thread during teardown
// (~MegaClient), so every queued work item sees a live MegaClient. NF-4's clearRefreshing
// closure additionally re-validates wsEngine() identity under uploadMutex to guard against
// engine-replacement mid-flight.
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

    const auto engineId = mImpl->instanceId();

    auto validateEngine = [](MegaClient& client, const std::uint64_t id) -> UploadEngine*
    {
        auto* engine = client.wsEngine();
        if (!engine || engine->instanceId() != id || engine->isStopping())
        {
            return nullptr;
        }
        return engine;
    };

    auto clearRefreshing = [](MegaClient& client, const std::uint64_t id)
    {
        if (auto* engine = client.wsEngine())
            engine->clearRefreshLatchForInstance(id);
    };

    mImpl->client.wsPostToClientThread(
        [engineId, validateEngine, clearRefreshing](MegaClient& client, TransferDbCommitter&)
        {
            if (!validateEngine(client, engineId))
            {
                clearRefreshing(client, engineId);
                return;
            }

            // Queue lockless USC command using the RequestDispatcher. This avoids blocking on
            // the main client-server channel when lockless channels are enabled.
            client.queueCommand(new CommandUSCForWsUpload(
                client,
                [&client, engineId, validateEngine, clearRefreshing](
                    Error e,
                    std::vector<std::pair<std::string, m_off_t>>&& sizeClasses)
                {
                    auto* currentEngine = validateEngine(client, engineId);
                    if (!currentEngine)
                    {
                        clearRefreshing(client, engineId);
                        return;
                    }

                    currentEngine->applyRefreshResultForInstance(engineId,
                                                                 e,
                                                                 std::move(sizeClasses));
                }));
        });
}

void WsPoolMgr::applyRefreshBackoff(Error e)
{
    ++mRefreshFailCount;
    // Protects the mRefreshFailCount - 1 expression below from wrap-around if a future
    // caller ever routes here without the increment above.
    assert(mRefreshFailCount > 0 && "mRefreshFailCount must be positive at this point");
    const unsigned count = mRefreshFailCount - 1;
    const unsigned exponent = std::min<unsigned>(count, 6);
    const dstime baseDelay = secondsToDs(10); // 10 seconds
    const dstime maxDelay = secondsToDs(10 * 60); // 10 minutes
    dstime backoff = baseDelay * (static_cast<dstime>(1) << exponent);
    if (backoff > maxDelay)
    {
        backoff = maxDelay;
    }
    mNextRefreshAttempt = SteadyTime::ds() + backoff;

    LOG_warn << "[WsPoolMgr::refreshPools] USC command failed: " << e
             << " [poolMgr=" << this << "]";
}

void WsPoolMgr::applyRefreshedUrls(std::vector<std::pair<std::string, m_off_t>> apiSizeClasses)
{
    if (apiSizeClasses.empty())
    {
        LOG_warn << "WsUpload: USC returned no upload pools";
        return;
    }

    const dstime now = SteadyTime::ds();

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
        const auto& refreshed = apiSizeClasses[i];
        const auto activateMatchedPool = [this, i](const std::size_t matchedIndex)
        {
            if (matchedIndex != i)
            {
                std::swap(mPools[i], mPools[matchedIndex]);
            }
            mPools[i]->mRetiring = false;
        };

        bool matched = false;

        // Prefer exact endpoint identity to preserve in-flight state whenever possible.
        for (std::size_t j = mPools.size(); j-- > 0;)
        {
            if (mPools[j]->mPinned)
            {
                continue;
            }
            if (mPools[j]->mMaxFileSize == refreshed.second && mPools[j]->mUrl == refreshed.first)
            {
                mPools[j]->mPoolCreationTime = now;
                activateMatchedPool(j);
                matched = true;
                break;
            }
        }

        // Host+size fallback is only safe for a fully idle pool. Active pools keep retiring
        // and a new pool is created for the refreshed endpoint.
        if (!matched)
        {
            for (std::size_t j = mPools.size(); j-- > 0;)
            {
                WsPool* const pool = mPools[j].get();
                if (!pool || pool->mPinned || !pool->sameHostMaxSize(refreshed))
                {
                    continue;
                }

                const bool canRetarget =
                    poolHasNoWork(*pool) && pool->mConns.empty() &&
                    pool->mActiveThreads.empty() && pool->mExitingThreads.empty();
                if (!canRetarget)
                {
                    continue;
                }

                if (pool->mUrl != refreshed.first)
                {
                    LOG_info << "WsUpload: retargeting idle pool URL"
                             << " [old=" << pool->mUrl << "] [new=" << refreshed.first << "]";
                    pool->mUrl = refreshed.first;
                }

                pool->mPoolCreationTime = now;
                activateMatchedPool(j);
                matched = true;
                break;
            }
        }

        if (!matched)
        {
            mPools.insert(mPools.begin() + static_cast<std::ptrdiff_t>(i),
                          std::make_unique<WsPool>(refreshed,
                                                   i ? apiSizeClasses[i - 1].second : 0,
                                                   mImpl,
                                                   mImpl->poolConnectionLimit()));
        }
    }

    bumpAllPools(now);
    LOG_info << "WsUpload: refreshed pools (" << apiSizeClasses.size() << " size classes)";
}

// Forwarding helper for ws_curl.cpp. Lives here because UploadEngine::Impl is
// only fully defined in this translation unit; declaring the method on
// WsPoolMgr lets sibling TUs query the pool-connection limit without pulling
// the Impl definition into ws_pool_mgr.h.
unsigned char WsPoolMgr::pinnedPoolConnectionLimit() const
{
    return mImpl->poolConnectionLimit();
}

// ========== UploadEngine (public facade) ==========
UploadEngine::UploadEngine(MegaClient& client):
    pImpl(new Impl(client))
{}

UploadEngine::~UploadEngine() = default;

std::uint64_t UploadEngine::instanceId() const noexcept
{
    return pImpl->instanceId();
}

bool UploadEngine::Impl::clearRefreshLatchForInstance(std::uint64_t id)
{
    std::lock_guard<std::mutex> g(uploadMutex);
    if (mInstanceId != id)
        return false;
    poolMgr.mRefreshing.store(false, std::memory_order_release);
    return true;
}

bool UploadEngine::Impl::applyRefreshResultForInstance(
    std::uint64_t id,
    Error e,
    std::vector<std::pair<std::string, m_off_t>>&& urls)
{
    std::lock_guard<std::mutex> g(uploadMutex);
    if (mInstanceId != id)
        return false;
    if (e == API_OK)
    {
        poolMgr.mRefreshFailCount = 0;
        poolMgr.mNextRefreshAttempt = 0;
        poolMgr.applyRefreshedUrls(std::move(urls));
    }
    else
    {
        poolMgr.applyRefreshBackoff(e);
    }
    poolMgr.mRefreshing.store(false, std::memory_order_release);
    return true;
}

bool UploadEngine::clearRefreshLatchForInstance(std::uint64_t id)
{
    return pImpl->clearRefreshLatchForInstance(id);
}

bool UploadEngine::applyRefreshResultForInstance(
    std::uint64_t id,
    Error e,
    std::vector<std::pair<std::string, m_off_t>>&& urls)
{
    return pImpl->applyRefreshResultForInstance(id, e, std::move(urls));
}

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

bool UploadEngine::getWsUploadStatsForTesting(WsUploadStatsForTesting& out) const
{
    return pImpl->getWsUploadStatsForTesting(out);
}

bool UploadEngine::isTrackedForTesting(const Transfer& t) const
{
    return pImpl->isTrackedForTesting(t);
}

std::uintptr_t UploadEngine::getFilePoolIdForTesting(Transfer& t) const
{
    return pImpl->getFilePoolIdForTesting(t);
}
#endif

void UploadEngine::kick()
{
    pImpl->kick();
}

void UploadEngine::notifyWorkers()
{
    pImpl->notifyWorkers();
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

#endif // MEGA_USE_WSUPLOAD
