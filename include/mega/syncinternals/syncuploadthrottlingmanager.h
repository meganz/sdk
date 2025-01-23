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
 * @brief Manages throttling and delayed processing of uploads.
 *
 * The UploadThrottlingManager handles the queuing and processing of delayed uploads,
 * including the throttling time and the max number of uploads allowed for a file before throttle.
 * It adjusts the throttle update rate dynamically based on queue size, allowing for
 * efficient upload handling without overloading system resources. Configuration
 * options allow users to tune the behavior as per their requirements.
 */
class UploadThrottlingManager: public IUploadThrottlingManager
{
public:
    /**
     * @brief Adds a delayed upload to the delayed queue.
     * @param delayedUpload The upload to be added to the delayed queue.
     */
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
     * futher processing (ex: enqueue the upload to the client)
     *
     * @see checkProcessDelayedUploads()
     */
    void processDelayedUploads(
        std::function<void(std::weak_ptr<SyncUpload_inClient>&& upload,
                           const VersioningOption vo,
                           const bool queueFirst,
                           const NodeHandle ovHandleIfShortcut)>&& completion) override;

    /**
     * @brief Resets last processed time to the current time.
     */
    void resetLastProcessedTime() override
    {
        mLastProcessedTime = std::chrono::steady_clock::now();
    }

    // Setters

    /**
     * @brief Sets the throttle update rate in seconds.
     * @param intervalSeconds The interval in seconds. It cannot be below
     * THROTTLE_UPDATE_RATE_LOWER_LIMIT nor above THROTTLE_UPDATE_RATE_UPPER_LIMIT.
     */
    bool setThrottleUpdateRate(const unsigned intervalSeconds) override
    {
        return setThrottleUpdateRate(std::chrono::seconds(intervalSeconds));
    }

    /**
     * @brief Sets the throttle update rate as a duration.
     * @param interval The interval as a std::chrono::seconds object.
     */
    bool setThrottleUpdateRate(const std::chrono::seconds interval) override;

    /**
     * @brief Sets the maximum uploads allowed before throttling.
     * @param maxUploadsBeforeThrottle The maximum number of uploads that will be uploaded
     * unthrottled. It cannot be below MAX_UPLOADS_BEFORE_THROTTLE_LOWER_LIMIT nor above
     * MAX_UPLOADS_BEFORE_THROTTLE_UPPER_LIMIT.
     */
    bool setMaxUploadsBeforeThrottle(const unsigned maxUploadsBeforeThrottle) override;

    // Getters

    /**
     * @brief Gets the upload counter inactivity expiration time.
     * @return The expiration time as a std::chrono::seconds object.
     */
    std::chrono::seconds uploadCounterInactivityExpirationTime() const override
    {
        return mUploadCounterInactivityExpirationTime;
    }

    /**
     * @brief Gets the throttle update rate for uploads in seconds.
     * @return The throttle update rate in seconds.
     */
    unsigned throttleUpdateRate() const override
    {
        return static_cast<unsigned>(mThrottleUpdateRate.count());
    }

    /**
     * @brief Gets the maximum uploads allowed before throttling.
     * @return The maximum number of uploads.
     */
    unsigned maxUploadsBeforeThrottle() const override
    {
        return mMaxUploadsBeforeThrottle;
    }

    /**
     * @brief Gets the lower and upper limits for throttling values.
     * @return The ThrottleValueLimits struct with lower and upper limits.
     */
    ThrottleValueLimits throttleValueLimits() const override
    {
        return {THROTTLE_UPDATE_RATE_LOWER_LIMIT,
                THROTTLE_UPDATE_RATE_UPPER_LIMIT,
                MAX_UPLOADS_BEFORE_THROTTLE_LOWER_LIMIT,
                MAX_UPLOADS_BEFORE_THROTTLE_UPPER_LIMIT};
    }

    /**
     * @brief Calculate the time since last delayed upload was processed.
     * @return The time lapsed since mLastProcessedTime in seconds.
     */
    std::chrono::seconds timeSinceLastProcessedUpload() const override
    {
        return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() -
                                                                mLastProcessedTime);
    }

private:
    // Limits
    static constexpr unsigned TIMEOUT_TO_RESET_UPLOAD_COUNTERS_SECONDS{
        86400}; // Timeout (in seconds) to reset upload counters due to inactivity.
    static constexpr unsigned THROTTLE_UPDATE_RATE_LOWER_LIMIT{
        60}; // Minimum allowed interval for processing delayed uploads.
    static constexpr unsigned THROTTLE_UPDATE_RATE_UPPER_LIMIT{
        TIMEOUT_TO_RESET_UPLOAD_COUNTERS_SECONDS - 1}; // Maximum allowed interval for processing
                                                       // delayed uploads.
    static constexpr unsigned MAX_UPLOADS_BEFORE_THROTTLE_LOWER_LIMIT{
        2}; // Minimum allowed of max uploads before throttle.
    static constexpr unsigned MAX_UPLOADS_BEFORE_THROTTLE_UPPER_LIMIT{
        5}; // Maximum allowed of max uploads before throttle.

    // Default values
    static constexpr unsigned DEFAULT_PROCESS_INTERVAL_SECONDS{
        180}; // Default interval (in seconds) for processing delayed uploads.
    static constexpr unsigned DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE{
        MAX_UPLOADS_BEFORE_THROTTLE_LOWER_LIMIT}; // Default maximum uploads allowed before
                                                  // throttling.

    // Members
    std::queue<DelayedSyncUpload> mDelayedQueue; // Queue of delayed uploads to be processed.
    std::chrono::steady_clock::time_point mLastProcessedTime{
        std::chrono::steady_clock::now()}; // Timestamp of the last processed upload.
    std::chrono::seconds mUploadCounterInactivityExpirationTime{
        TIMEOUT_TO_RESET_UPLOAD_COUNTERS_SECONDS}; // Timeout for resetting upload counters due to
                                                   // inactivity.

    // Configurable members
    std::chrono::seconds mThrottleUpdateRate{
        DEFAULT_PROCESS_INTERVAL_SECONDS}; // Configurable interval for processing uploads.
    unsigned mMaxUploadsBeforeThrottle{
        DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE}; // Maximum uploads allowed before throttling.

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
