/**
 * @file syncuploadthrottlingmanager.h
 * @brief Class for UploadThrottlingManager.
 */

#ifndef MEGA_SYNCINTERNALS_UPLOADTHROTTLINGMANAGER_H
#define MEGA_SYNCINTERNALS_UPLOADTHROTTLINGMANAGER_H 1

#ifdef ENABLE_SYNC

#include "mega/syncinternals/synciuploadthrottlingmanager.h"

namespace mega
{

/**
 * @class UploadThrottlingManager
 * @brief Manages throttling, delayed processing of uploads and configurable values.
 *
 * @see IUploadThrottlingManager
 */
class UploadThrottlingManager: public IUploadThrottlingManager
{
public:
    void addToDelayedUploads(DelayedSyncUpload&& delayedUpload) override
    {
        mDelayedQueue.emplace(std::move(delayedUpload));
    }

    /**
     * @brief Processes the delayed upload queue.
     *
     * Processes the next delayed upload in the queue, ensuring that throttling conditions
     * are met before initiating uploads.
     *
     * If the next delayed upload is not valid (DelayedSyncUpload::weakUpload is not valid), it will
     * be skipped and the next delayed upload in the queue, if any, will be the one to be processed.
     *
     * If a valid delayed upload is processed, it will be passed to the completion function for
     * further processing (ex: enqueue the upload to the client).
     *
     * @see checkProcessDelayedUploads()
     */
    void processDelayedUploads(std::function<void(DelayedSyncUpload&&)>&& completion) override;

    /**
     * @brief Resets last processed time of a delayed upload from the queue.
     * This time will be the start point to process the next delayed upload after the
     * throttleUpdateRate.
     */
    void resetLastProcessedTime()
    {
        mLastProcessedTime = std::chrono::steady_clock::now();
    }

    // Setters

    /**
     * Sets the throttleUpdateRate configurable value.
     * @param interval The new throttle update rate or delay to process the next
     * delayed upload. It cannot be below THROTTLE_UPDATE_RATE_LOWER_LIMIT nor above
     * THROTTLE_UPDATE_RATE_UPPER_LIMIT.
     */
    bool setThrottleUpdateRate(const std::chrono::seconds interval) override;

    /**
     * Sets the maxUploadsBeforeThrottle configurable value.
     * @param maxUploadsBeforeThrottle The maximum number of uploads that will be uploaded
     * unthrottled. It cannot be below MAX_UPLOADS_BEFORE_THROTTLE_LOWER_LIMIT nor above
     * MAX_UPLOADS_BEFORE_THROTTLE_UPPER_LIMIT.
     */
    bool setMaxUploadsBeforeThrottle(const unsigned maxUploadsBeforeThrottle) override;

    // Getters

    bool anyDelayedUploads() const override
    {
        return !mDelayedQueue.empty();
    }

    std::chrono::seconds uploadCounterInactivityExpirationTime() const override
    {
        return mUploadCounterInactivityExpirationTime;
    }

    std::chrono::seconds throttleUpdateRate() const override
    {
        return mThrottleUpdateRate;
    }

    unsigned maxUploadsBeforeThrottle() const override
    {
        return mMaxUploadsBeforeThrottle;
    }

    ThrottleValueLimits throttleValueLimits() const override
    {
        return {THROTTLE_UPDATE_RATE_LOWER_LIMIT,
                THROTTLE_UPDATE_RATE_UPPER_LIMIT,
                MAX_UPLOADS_BEFORE_THROTTLE_LOWER_LIMIT,
                MAX_UPLOADS_BEFORE_THROTTLE_UPPER_LIMIT};
    }

    std::chrono::seconds timeSinceLastProcessedUpload() const override
    {
        return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() -
                                                                mLastProcessedTime);
    }

private:
    // Limits
    static constexpr std::chrono::seconds TIMEOUT_TO_RESET_UPLOAD_COUNTERS{
        86400}; // Timeout to reset upload counters due to inactivity.
    static constexpr std::chrono::seconds THROTTLE_UPDATE_RATE_LOWER_LIMIT{60};
    static constexpr std::chrono::seconds THROTTLE_UPDATE_RATE_UPPER_LIMIT{
        TIMEOUT_TO_RESET_UPLOAD_COUNTERS - std::chrono::seconds(1)};
    static constexpr unsigned MAX_UPLOADS_BEFORE_THROTTLE_LOWER_LIMIT{2};
    static constexpr unsigned MAX_UPLOADS_BEFORE_THROTTLE_UPPER_LIMIT{5};

    // Default values
    static constexpr std::chrono::seconds DEFAULT_THROTTLE_UPDATE_RATE{180};
    static constexpr unsigned DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE{
        MAX_UPLOADS_BEFORE_THROTTLE_LOWER_LIMIT};

    // Members
    std::queue<DelayedSyncUpload> mDelayedQueue;
    std::chrono::steady_clock::time_point mLastProcessedTime{std::chrono::steady_clock::now()};
    std::chrono::seconds mUploadCounterInactivityExpirationTime{TIMEOUT_TO_RESET_UPLOAD_COUNTERS};

    // Configurable members
    std::chrono::seconds mThrottleUpdateRate{DEFAULT_THROTTLE_UPDATE_RATE};
    unsigned mMaxUploadsBeforeThrottle{DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE};

    /**
     * @brief Checks if the next delayed upload in the queue should be processed.
     *
     * Calculates a dynamic update rate taking into account:
     *    1. mDelayedQueue size.
     *    2. mThrottleUpdateRate (reference value).
     *    3. THROTTLE_UPDATE_RATE_LOWER_LIMIT.
     * The dynamic rate is the max between the THROTTLE_UPDATE_RATE_LOWER_LIMIT and the result of
     * mThrottleUpdateRate / sqrt(mDelayedQueue.size())
     *
     * @return True if the next upload should be processed, otherwise false.
     */
    bool checkProcessDelayedUploads() const;
};

} // namespace mega

#endif // ENABLE_SYNC
#endif // MEGA_SYNCINTERNALS_UPLOADTHROTTLINGMANAGER_H
