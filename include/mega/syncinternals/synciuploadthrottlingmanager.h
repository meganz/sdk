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
    std::chrono::seconds throttleUpdateRateLowerLimit;
    std::chrono::seconds throttleUpdateRateUpperLimit;
    unsigned maxUploadsBeforeThrottleLowerLimit;
    unsigned maxUploadsBeforeThrottleUpperLimit;
};

/**
 * @class IUploadThrottlingManager
 * @brief Interface for the manager in charge of throttling and delayed processing of uploads.
 *
 * The IUploadThrottlingManager is meant to handle the collecting and processing of delayed uploads,
 * as well as owning and defining the configurable values to be used either from this manager or
 * from other components which are part of the throttling logic.
 *
 * The configurable values are:
 * throttleUpdateRate: delay to process next delayed upload. This one is meant to be used directly
 * within the internal process of delayed uploads. maxUploadsBeforeThrottle: number of uploads that
 * doesn't go through the throttling logic. This one is meant to be used by other components
 * handling the individual uploads and calling addToDelayedUploads when needed.
 *
 * Additionally, the uploadCounterInactivityExpirationTime is used to reset the individual upload
 * counters after some time, to avoid increasing them forever.
 */
class IUploadThrottlingManager
{
public:
    virtual ~IUploadThrottlingManager() = default;

    // Delayed uploads perations.

    virtual void addToDelayedUploads(DelayedSyncUpload&&) = 0;

    virtual void processDelayedUploads(std::function<void(DelayedSyncUpload&&)>&&) = 0;

    // Setters.

    virtual bool setThrottleUpdateRate(const std::chrono::seconds) = 0;

    virtual bool setMaxUploadsBeforeThrottle(const unsigned) = 0;

    // Getters.

    virtual bool anyDelayedUploads() const = 0;

    virtual std::chrono::seconds uploadCounterInactivityExpirationTime() const = 0;

    virtual std::chrono::seconds throttleUpdateRate() const = 0;

    virtual unsigned maxUploadsBeforeThrottle() const = 0;

    virtual ThrottleValueLimits throttleValueLimits() const = 0;

    virtual std::chrono::seconds timeSinceLastProcessedUpload() const = 0;
};

} // namespace mega

#endif // ENABLE_SYNC
#endif // MEGA_SYNCINTERNALS_IUPLOADTHROTTLINGMANAGER_H
