/**
 * @file syncuploadthrottlingfile.h
 * @brief Class for UploadThrottlingFile.
 */

#ifndef MEGA_SYNCINTERNALS_UPLOADTHROTTLINGFILE_H
#define MEGA_SYNCINTERNALS_UPLOADTHROTTLINGFILE_H 1

#ifdef ENABLE_SYNC

#include "mega/file.h"

namespace mega
{

/**
 * @struct UploadThrottlingFile
 * @brief Handles upload throttling and abort handling for individual files.
 *
 * This struct encapsulates the logic for handling upload throttling and aborted uploads.
 * It tracks the number of uploads, manages timeouts, and
 * provides mechanisms for resetting counters and determining when throttling or upload
 * continuation should occur.
 *
 * @see UploadThrottlingManager
 */
struct UploadThrottlingFile
{
private:
    /**
     * @brief Counter for completed uploads.
     */
    unsigned mUploadCounter{};

    /**
     * @brief Timestamp of the last time the upload counter was processed.
     */
    std::chrono::steady_clock::time_point mUploadCounterLastTime{std::chrono::steady_clock::now()};

    /**
     * @brief Flag to bypass throttling logic.
     * This is meant for uncomplete uploads that were cancelled due to a change or failure.
     */
    bool mBypassThrottlingNextTime{};

public:
    /**
     * @brief Gets the mBypassThrottlingNextTime flag.
     */
    unsigned willBypassThrottlingNextTime()
    {
        return mBypassThrottlingNextTime;
    }

    /**
     * @brief Increases the upload counter by 1 and returns the updated counter.
     */
    unsigned increaseUploadCounter()
    {
        ++mUploadCounter;
        mUploadCounterLastTime = std::chrono::steady_clock::now();
        return mUploadCounter;
    }

    /**
     * @brief Checks throttling control logic for uploads.
     *
     * Checks if:
     *   - Flag to bypass throttling (mBypassThrottlingNextTime) is false. Otherwise it returns
     * false.
     *   - Time lapsed since last upload counter processing does not exceed
     * uploadCounterInactivityExpirationTime. Otherwise the the upload counter is reset along
     * with mUploadCounterLastTime and returns false.
     *   - The number of uploads exceeds the configured maximum before throttling. Otherwise it
     * returns false.
     *
     * @param maxUploadsBeforeThrottle Maximum uploads allowed before throttling.
     * @param uploadCounterInactivityExpirationTime Timeout for resetting the upload counter.
     * @return True if throttling is applied, otherwise false.
     */
    bool checkUploadThrottling(const unsigned maxUploadsBeforeThrottle,
                               const std::chrono::seconds uploadCounterInactivityExpirationTime);

    /**
     * @brief Handles the logic for aborting uploads due to fingerprint mismatch or termination.
     *
     * The upload can only be aborted if:
     *   - The upload has already started (not in the throttling queue). Otherwise the
     * fingerprint of the upload is updated with the new one. No need to cancel the upload.
     *   - The upload has not started the putnodes request.
     *
     * If the above conditions are met the upload must be aborted.
     * Additionally, bypassThrottlingNextTime() is called in case that the upload must be
     * aborted.
     *
     * @param SyncUpload_inClient Reference to the sync upload.
     * @param maxUploadsBeforeThrottle Maximum number of allowed uploads before the next upload
     * must be throttled.
     * @param transferPath Path of the upload being evaluated.
     * @return True if the upload should be aborted, otherwise false.
     */
    bool handleAbortUpload(SyncUpload_inClient& upload,
                           const FileFingerprint& fingerprint,
                           const unsigned maxUploadsBeforeThrottle,
                           const LocalPath& transferPath);

    /**
     * @brief Sets the mBypassThrottlingNextTime flag.
     *
     * The upload counter is not increased if the upload is not completed. However, the counter
     * could be greater than maxUploadsBeforeThrottle already, and the current upload has been
     * cancelled due to a fingerprint change or failure. In that case, this method should be
     * called to set the flag to true and bypass the throttling logic upon the upload restart.
     *
     * @param maxUploadsBeforeThrottle Maximum number of allowed uploads before the next upload
     * must be throttled.
     */
    void bypassThrottlingNextTime(const unsigned maxUploadsBeforeThrottle);
};

} // namespace mega

#endif // ENABLE_SYNC
#endif // MEGA_SYNCINTERNALS_UPLOADTHROTTLINGFILE_H
