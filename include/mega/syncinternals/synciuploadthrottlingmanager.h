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
 * The IUploadThrottlingManager is meant to handle the collecting and processing of delayed uploads,
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
     */
    virtual void addToDelayedUploads(DelayedSyncUpload&& /* delayedUpload */) = 0;

    /**
     * @brief Processes the delayed uploads.
     *
     * Calls completion function to be called if a DelayedUpload was processed.
     */
    virtual void processDelayedUploads(
        std::function<void(std::weak_ptr<SyncUpload_inClient>&& /* upload */,
                           const VersioningOption /* vo */,
                           const bool /* queueFirst */,
                           const NodeHandle /* ovHandleIfShortcut */)>&& /* completion */) = 0;

    // Setters

    /**
     * @brief Sets the throttle update rate in seconds.
     */
    virtual bool setThrottleUpdateRate(const unsigned /* intervalSeconds */) = 0;

    /**
     * @brief Sets the throttle update rate as a duration.
     */
    virtual bool setThrottleUpdateRate(const std::chrono::seconds /* interval */) = 0;

    /**
     * @brief Sets the maximum uploads allowed before throttling.
     */
    virtual bool setMaxUploadsBeforeThrottle(const unsigned /* maxUploadsBeforeThrottle */) = 0;

    // Getters

    /**
     * @brief Gets the upload counter inactivity expiration time.
     */
    virtual std::chrono::seconds uploadCounterInactivityExpirationTime() const = 0;

    /**
     * @brief Gets the throttle update rate for uploads in seconds.
     */
    virtual unsigned throttleUpdateRate() const = 0;

    /**
     * @brief Gets the maximum uploads allowed before throttling.
     */
    virtual unsigned maxUploadsBeforeThrottle() const = 0;

    /**
     * @brief Gets the lower and upper limits for throttling values.
     */
    virtual ThrottleValueLimits throttleValueLimits() const = 0;

    /**
     * @brief Calculates the time since last delayed upload was processed.
     */
    virtual std::chrono::seconds timeSinceLastProcessedUpload() const = 0;
};

} // namespace mega

#endif // ENABLE_SYNC
#endif // MEGA_SYNCINTERNALS_IUPLOADTHROTTLINGMANAGER_H
