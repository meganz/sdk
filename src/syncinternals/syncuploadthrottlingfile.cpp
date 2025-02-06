/**
 * @file syncuploadthrottlingfile.cpp
 * @brief Class for UploadThrottlingFile.
 */

#ifdef ENABLE_SYNC

#include "mega/syncinternals/syncuploadthrottlingfile.h"

#include "mega/logging.h"

namespace mega
{

void UploadThrottlingFile::increaseUploadCounter()
{
    mUploadCounterLastTime = std::chrono::steady_clock::now();
    if (mUploadCounter + 1 == std::numeric_limits<unsigned>::max())
    {
        // Reset to 0 when max is about to be reached
        LOG_err << "[UploadThrottlingFile::increaseUploadCounter] The upload counter ("
                << mUploadCounter << ") is about to reach the max allowed. Value will be reset";
        assert(false && "[UploadThrottlingFile::increaseUploadCounter] Upload counter is about "
                        "to reach the max allowed!");
        mUploadCounter = 0;
    }
    ++mUploadCounter;
}

bool UploadThrottlingFile::checkUploadThrottling(
    const unsigned maxUploadsBeforeThrottle,
    const std::chrono::seconds uploadCounterInactivityExpirationTime)
{
    if (mBypassThrottlingNextTime)
    {
        mBypassThrottlingNextTime = false;
        return false;
    }

    if (const auto timeSinceLastUploadCounterProcess =
            std::chrono::steady_clock::now() - mUploadCounterLastTime;
        timeSinceLastUploadCounterProcess >= uploadCounterInactivityExpirationTime)
    {
        // Reset the upload counter if enough time has lapsed since last time.
        mUploadCounter = 0;
        mUploadCounterLastTime = std::chrono::steady_clock::now();
        return false;
    }

    return mUploadCounter >= maxUploadsBeforeThrottle;
}

bool UploadThrottlingFile::handleAbortUpload(SyncUpload_inClient& upload,
                                             const bool transferDirectionNeedsToChange,
                                             const FileFingerprint& fingerprint,
                                             const unsigned maxUploadsBeforeThrottle,
                                             const LocalPath& transferPath)
{
    if (upload.putnodesStarted)
        return false;

    if (transferDirectionNeedsToChange)
        return true;

    if (!upload.wasStarted)
    {
        assert(!upload.wasTerminated);
        LOG_verbose << "Updating fingerprint of queued upload " << transferPath;
        upload.updateFingerprint(fingerprint);
        return false;
    }

    // If the upload is going to be aborted either due to a change while it was inflight or after a
    // failure, and the file was being throttled, let it start immediately next time.
    bypassThrottlingNextTime(maxUploadsBeforeThrottle);
    return true;
}

void UploadThrottlingFile::bypassThrottlingNextTime(const unsigned maxUploadsBeforeThrottle)
{
    if (mUploadCounter >= maxUploadsBeforeThrottle)
    {
        mBypassThrottlingNextTime = true;
    }
}

} // namespace mega

#endif // ENABLE_SYNC
