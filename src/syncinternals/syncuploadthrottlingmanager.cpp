/**
 * @file syncuploadthrottlingmanager.cpp
 * @brief Class for UploadThrottlingManager.
 */

#ifdef ENABLE_SYNC

#include "mega/syncinternals/syncuploadthrottlingmanager.h"

#include "mega/syncinternals/syncinternals_logging.h"

namespace mega
{

bool UploadThrottlingManager::setThrottleUpdateRate(const std::chrono::seconds interval)
{
    if (interval < std::chrono::seconds(THROTTLE_UPDATE_RATE_LOWER_LIMIT) ||
        interval > std::chrono::seconds(THROTTLE_UPDATE_RATE_UPPER_LIMIT))
    {
        LOG_warn << "[UploadThrottle] Invalid throttle update rate (" << interval.count()
                 << " secs). Must be >= " << THROTTLE_UPDATE_RATE_LOWER_LIMIT
                 << " and <= " << THROTTLE_UPDATE_RATE_UPPER_LIMIT << " seconds";
        return false;
    }

    LOG_debug << "[UploadThrottle] Throttle update rate set to " << interval.count() << " secs";
    mThrottleUpdateRate = interval;
    return true;
}

bool UploadThrottlingManager::setMaxUploadsBeforeThrottle(const unsigned maxUploadsBeforeThrottle)
{
    if (maxUploadsBeforeThrottle < MAX_UPLOADS_BEFORE_THROTTLE_LOWER_LIMIT ||
        maxUploadsBeforeThrottle > MAX_UPLOADS_BEFORE_THROTTLE_UPPER_LIMIT)
    {
        LOG_warn << "[UploadThrottle] Invalid max uploads value (" << maxUploadsBeforeThrottle
                 << "). Must be >= " << MAX_UPLOADS_BEFORE_THROTTLE_LOWER_LIMIT
                 << " and <= " << MAX_UPLOADS_BEFORE_THROTTLE_UPPER_LIMIT;
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
    const auto adjustedThrottleUpdateRate =
        std::max(std::chrono::seconds(THROTTLE_UPDATE_RATE_LOWER_LIMIT),
                 std::chrono::duration_cast<std::chrono::seconds>(
                     mThrottleUpdateRate / std::sqrt(static_cast<double>(mDelayedQueue.size()))));

    if (auto timeSinceLastProcessedUpload = std::chrono::steady_clock::now() - mLastProcessedTime;
        timeSinceLastProcessedUpload < adjustedThrottleUpdateRate)
    {
        SYNCS_verbose_timed
            << "[UploadThrottle] Waiting to process delayed uploads [processing every "
            << adjustedThrottleUpdateRate.count() << " secs, time lapsed since last process: "
            << std::chrono::duration_cast<std::chrono::seconds>(timeSinceLastProcessedUpload)
                   .count()
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
