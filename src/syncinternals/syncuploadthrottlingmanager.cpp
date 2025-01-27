/**
 * @file syncuploadthrottlingmanager.cpp
 * @brief Class for UploadThrottlingManager.
 */

#ifdef ENABLE_SYNC

#include "mega/syncinternals/syncuploadthrottlingmanager.h"

#include "mega/syncinternals/syncinternals_logging.h"

#include <algorithm>

namespace mega
{

template<typename T>
static bool valueIsOutOfRange(const T& value, const T& lower, const T& upper)
{
    if (value == std::clamp(value, lower, upper))
        return false;

    LOG_warn << "[UploadThrottle::valueIsOutOfRange] Value out of range (" << value
             << "). Must be >= " << lower << " and <= " << upper;
    return true;
}

bool UploadThrottlingManager::setThrottleUpdateRate(const std::chrono::seconds interval)
{
    if (valueIsOutOfRange(static_cast<unsigned>(interval.count()),
                          THROTTLE_UPDATE_RATE_LOWER_LIMIT,
                          THROTTLE_UPDATE_RATE_UPPER_LIMIT))
    {
        return false;
    }

    LOG_debug << "[UploadThrottle] Throttle update rate set to " << interval.count() << " secs";
    mThrottleUpdateRate = interval;
    return true;
}

bool UploadThrottlingManager::setMaxUploadsBeforeThrottle(const unsigned maxUploadsBeforeThrottle)
{
    if (valueIsOutOfRange(maxUploadsBeforeThrottle,
                          MAX_UPLOADS_BEFORE_THROTTLE_LOWER_LIMIT,
                          MAX_UPLOADS_BEFORE_THROTTLE_UPPER_LIMIT))
    {
        return false;
    }

    LOG_debug << "[UploadThrottle] Num uploads before throttle set to " << maxUploadsBeforeThrottle;
    mMaxUploadsBeforeThrottle = maxUploadsBeforeThrottle;
    return true;
}

bool UploadThrottlingManager::checkProcessDelayedUploads() const
{
    if (mDelayedQueue.empty())
    {
        return false;
    }

    // Calculate adjusted interval
    const auto adjustedThrottleUpdateRate = std::chrono::duration_cast<std::chrono::seconds>(
        mThrottleUpdateRate / std::sqrt(static_cast<double>(mDelayedQueue.size())));
    const auto throttleUpdateRate = std::max(std::chrono::seconds(THROTTLE_UPDATE_RATE_LOWER_LIMIT),
                                             adjustedThrottleUpdateRate);

    if (const auto timeSinceLastProcessedUploadInSeconds = timeSinceLastProcessedUpload();
        timeSinceLastProcessedUploadInSeconds < throttleUpdateRate)
    {
        SYNCS_verbose_timed
            << "[UploadThrottle] Waiting to process delayed uploads [processing every "
            << throttleUpdateRate.count() << " secs, time lapsed since last process: "
            << timeSinceLastProcessedUploadInSeconds.count()
            << " secs, delayed uploads = " << mDelayedQueue.size() << "]";
        return false;
    }

    return true;
}

void UploadThrottlingManager::processDelayedUploads(
    std::function<void(std::weak_ptr<SyncUpload_inClient>&& upload,
                       const VersioningOption vo,
                       const bool queueFirst,
                       const NodeHandle ovHandleIfShortcut)>&& completion)
{
    if (!checkProcessDelayedUploads())
    {
        return;
    }

    LOG_verbose << "[UploadThrottle] Processing delayed uploads. Queue size: "
                << mDelayedQueue.size();

    bool delayedUploadProcessed{false};
    do
    {
        DelayedSyncUpload delayedUpload = std::move(mDelayedQueue.front());
        mDelayedQueue.pop();

        if (!delayedUpload.mWeakUpload.lock())
        {
            LOG_warn << "[UploadThrottle] Upload is no longer valid. Skipping this task";
            continue;
        }

        delayedUploadProcessed = true;
        resetLastProcessedTime();
        completion(std::move(delayedUpload.mWeakUpload),
                   delayedUpload.mVersioningOption,
                   delayedUpload.mQueueFirst,
                   delayedUpload.mOvHandleIfShortcut);
    }
    while (!mDelayedQueue.empty() && !delayedUploadProcessed);
}

} // namespace mega

#endif // ENABLE_SYNC
