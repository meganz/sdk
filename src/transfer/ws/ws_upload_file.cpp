/**
 * @file src/transfer/ws/ws_upload_file.cpp
 * @brief Non-trivial method bodies for `mega::ws::WsUploadFile`, split out of
 *        `src/transfer/ws/wsupload.cpp`. Hosts:
 *
 *          - `readData` — the I/O hot path (open-on-first-use, periodic stat,
 *            reopen + retry, generation-check interruptions).
 *          - `getTransferStats` — debug speed reporting.
 *          - `setPool` / `unsetPool` / `getCurrentSessionUrl` — pool bindings
 *            that need the full `WsPool` definition (visible via
 *            `mega/transfer/ws/wsupload_internal.h`).
 *
 *        Small accessors and inline state transitions live inline in
 *        `include/mega/transfer/ws/ws_upload_file.h`.
 *
 * (c) 2026 by MEGA Privacy Kft, Csomad, Hungary
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the rules set forth in the Terms of Service.
 *
 * @copyright Simplified (2-clause) BSD License.
 */

#ifdef MEGA_USE_WSUPLOAD

#include "mega/transfer/ws/ws_upload_file.h"

// `WsPool` full definition (and the `SteadyTime` / `WSUPLOAD_TRACE` cluster)
// is needed by setPool/unsetPool/getCurrentSessionUrl and the readData
// implementation. ws_upload_file.h already pulls this in, but re-state for
// clarity.
#include "mega/transfer/ws/wsupload_internal.h"

#include "mega/logging.h"
#include "mega/megaclient.h" // mClient.fsaccess->newfileaccess()

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

namespace mega
{
namespace ws
{

// Forward decl of the TU-local chunk-size lookup defined in wsupload.cpp (the same always-on
// declaration ws_pool.cpp uses). collectUnackedGapChunks needs it to split an un-acked gap
// back into the exact chunk boundaries the send path originally produced.
int chunkSizeAtPosition(m_off_t pos);

bool WsUploadFile::readData(char* buf,
                            const m_off_t pos,
                            const int len,
                            std::mutex& engineMutex,
                            const std::uint64_t expectedGeneration,
                            bool* interruptedByStateChange)
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

bool WsUploadFile::getTransferStats(UploadEngine::WsTransferStats& stats) const
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

// WsUploadFile pool bindings — body needs WsPool complete (via
// wsupload_internal.h pulled in above).
void WsUploadFile::unsetPool()
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

void WsUploadFile::setPool(WsPool& p)
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

bool WsUploadFile::getCurrentSessionUrl(std::string& outUrl) const
{
    if (!mPool)
    {
        return false;
    }
    outUrl = mPool->mUrl;
    return true;
}

std::size_t WsUploadFile::collectUnackedGapChunks(std::vector<WsChunk>& out) const
{
    // Enumerate [0, mHeadPos) minus mAckedIntervals (sorted, coalesced, non-overlapping),
    // splitting every gap at chunk boundaries so each appended WsChunk reproduces exactly the
    // (pos,len) the send path originally produced (keeping the len-blind server ack-match safe).
    std::size_t appended = 0;
    const auto appendRange = [&](m_off_t g0, const m_off_t g1)
    {
        while (g0 < g1)
        {
            const m_off_t len = std::min<m_off_t>(chunkSizeAtPosition(g0), g1 - g0);
            if (len <= 0)
            {
                break; // defensive: never advance by 0 (would spin)
            }
            out.push_back(WsChunk{g0, static_cast<int>(len), mFileNo, 0u});
            g0 += len;
            ++appended;
        }
    };

    m_off_t cursor = 0;
    for (const auto& iv: mAckedIntervals)
    {
        if (cursor >= mHeadPos)
        {
            break;
        }
        if (iv.first > cursor)
        {
            appendRange(cursor, std::min<m_off_t>(iv.first, mHeadPos));
        }
        if (iv.second > cursor)
        {
            cursor = iv.second;
        }
    }
    if (cursor < mHeadPos)
    {
        appendRange(cursor, mHeadPos);
    }

    return appended;
}

} // namespace ws
} // namespace mega

#endif // MEGA_USE_WSUPLOAD
