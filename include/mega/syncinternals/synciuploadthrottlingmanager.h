/**
 * @file syncuploadthrottlingmanager.h
 * @brief Class for IUploadThrottlingManager.
 */

#ifndef MEGA_SYNCINTERNALS_IUPLOADTHROTTLINGMANAGER_H
#define MEGA_SYNCINTERNALS_IUPLOADTHROTTLINGMANAGER_H 1

#ifdef ENABLE_SYNC

#include "mega/file.h"
#include "mega/types.h"

#include <chrono>

namespace mega
{

/**
 * @struct ThrottleValueLimits
 * @brief Struct to contain the configurable values lower and upper limits.
 */
struct ThrottleValueLimits
{
    unsigned throttleUpdateRateLowerLimit;
    unsigned throttleUpdateRateUpperLimit;
    unsigned maxUploadsBeforeThrottleLowerLimit;
    unsigned maxUploadsBeforeThrottleUpperLimit;
};

/**
 * @class IUploadThrottlingManager
 * @brief Interface for the manager in charge of throttling and delayed processing of uploads.
 *
 * The IUploadThrottlingManager is meant to handle the queuing and processing of delayed uploads,
 * including the throttling time and the max number of uploads allowed for a file before throttle.
 */
class IUploadThrottlingManager
{
public:
    /**
     * @brief IUploadThrottlingManager destructor.
     */
    virtual ~IUploadThrottlingManager() = default;

    /**
     * @brief Adds a delayed upload to be processed.
     * @param The upload to be throttled and processed.
     */
    virtual void addToDelayedUploads(DelayedSyncUpload&& /* delayedUpload */) = 0;

    /**
     * @brief Processes the delayed uploads.
     *
     * Calls completion function if there a DelayedUpload was processed.
     */
    virtual void processDelayedUploads(
        std::function<void(std::weak_ptr<SyncUpload_inClient>&& /* upload */,
                           const VersioningOption /* vo */,
                           const bool /* queueFirst */,
                           const NodeHandle /* ovHandleIfShortcut */)>&& /* completion */) = 0;

    // Setters

    /**
     * @brief Sets the throttle update rate in seconds.
     * @param The throttle update rate in seconds.
     */
    virtual bool setThrottleUpdateRate(const unsigned /* intervalSeconds */) = 0;

    /**
     * @brief Sets the throttle update rate as a duration.
     * @param The interval as a std::chrono::seconds object.
     */
    virtual bool setThrottleUpdateRate(const std::chrono::seconds /* interval */) = 0;

    /**
     * @brief Sets the maximum uploads allowed before throttling.
     * @param The maximum number of uploads that will be uploaded unthrottled.
     */
    virtual bool setMaxUploadsBeforeThrottle(const unsigned /* maxUploadsBeforeThrottle */) = 0;

    // Getters

    /**
     * @brief Gets the upload counter inactivity expiration time.
     * @return The expiration time as a std::chrono::seconds object.
     */
    virtual std::chrono::seconds uploadCounterInactivityExpirationTime() const = 0;

    /**
     * @brief Gets the throttle update rate for uploads in seconds.
     * @return The throttle update rate in seconds.
     */
    virtual unsigned throttleUpdateRate() const = 0;

    /**
     * @brief Gets the maximum uploads allowed before throttling.
     * @return The maximum number of uploads.
     */
    virtual unsigned maxUploadsBeforeThrottle() const = 0;

    /**
     * @brief Gets the lower and upper limits for throttling values.
     * @return The ThrottleValueLimits struct with lower and upper limits.
     */
    virtual ThrottleValueLimits throttleValueLimits() const = 0;

    /**
     * @brief Calculate the time since last delayed upload was processed.
     * @return The time lapsed since last processed upload in seconds.
     */
    virtual std::chrono::seconds timeSinceLastProcessedUpload() const = 0;
};

} // namespace mega

#endif // ENABLE_SYNC
#endif // MEGA_SYNCINTERNALS_IUPLOADTHROTTLINGMANAGER_H
