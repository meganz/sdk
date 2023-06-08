/**
 * @file MEGAScheduledCopy.h
 * @brief Provides information about a backup
 *
 */

#import <Foundation/Foundation.h>
#import "MEGATransferList.h"

NS_ASSUME_NONNULL_BEGIN

@interface MEGAScheduledCopy : NSObject

/**
 * @brief Creates a copy of this MegaScheduledCopy object
 *
 * The resulting object is fully independent of the source MegaScheduledCopy,
 * it contains a copy of all internal attributes, so it will be valid after
 * the original object is deleted.
 *
 * You are the owner of the returned object
 *
 * @return Copy of the MegaScheduledCopy object
 */
- (nullable instancetype)clone;

/**
 * @brief Get the handle of the folder that is being backed up
 * @return Handle of the folder that is being backed up in MEGA
 */
@property (readonly, nonatomic) uint64_t handle;

/**
 * @brief Get the path of the local folder that is being backed up
 *
 * The SDK retains the ownership of the returned value. It will be valid until
 * the MegaScheduledCopy object is deleted.
 *
 * @return Local folder that is being backed up
 */
@property (readonly, nonatomic, nullable) NSString *localFolder;

/**
 * @brief Returns the identifier of this backup
 *
 * @return Identifier of the backup
 */
@property (readonly, nonatomic) NSUInteger tag;

/**
 * @brief Returns if backups that should have happen in the past should be taken care of
 *
 * @return Whether past backups should be taken care of
 */
@property (readonly, nonatomic) BOOL attendPastBackups;

/**
 * @brief Returns the period of the backup
 *
 * @return The period of the backup in deciseconds
 */
@property (readonly, nonatomic) int64_t period;

/**
 * @brief Returns the period string of the backup
 * Any of these 6 fields may be an asterisk (*). This would mean the entire range of possible values, i.e. each minute, each hour, etc.
 *
 * Period is formatted as follows
 *  - - - - - -
 *  | | | | | |
 *  | | | | | |
 *  | | | | | +---- Day of the Week   (range: 1-7, 1 standing for Monday)
 *  | | | | +------ Month of the Year (range: 1-12)
 *  | | | +-------- Day of the Month  (range: 1-31)
 *  | | +---------- Hour              (range: 0-23)
 *  | +------------ Minute            (range: 0-59)
 *  +-------------- Second            (range: 0-59)
 *
 * E.g:
 * - daily at 04:00:00 (UTC): "0 0 4 * * *"
 * - every 15th day at 00:00:00 (UTC) "0 0 0 15 * *"
 * - mondays at 04.30.00 (UTC): "0 30 4 * * 1"
 *
 * @return The period string of the backup
 */
@property (readonly, nonatomic, nullable) NSString *periodString;

/**
 * @brief Returns the next absolute timestamp of the next backup. backup. If none provided it'll use current one.
 *
 * Successive nested calls to this functions will give you a full schedule of the next backups.
 *
 * Timestamp measures are given in number of seconds that elapsed since January 1, 1970 (midnight UTC/GMT),
 * not counting leap seconds (in ISO 8601: 1970-01-01T00:00:00Z).
 *
 * @return timestamp of the next backup.
 */
@property (readonly, nonatomic) long long nextStartTime;


/**
 * @brief Returns the number of backups to keep
 *
 * @return Maximun number of Backups to store
 */
@property (readonly, nonatomic) NSUInteger maxBackups;

/**
 * @brief Get the state of the backup
 *
 * Possible values are:
 * - SCHEDULED_COPY_FAILED = -2
 * The backup has failed and has been disabled
 *
 * - SCHEDULED_COPY_CANCELED = -1,
 * The backup has failed and has been disabled
 *
 * - SCHEDULED_COPY_INITIALSCAN = 0,
 * The backup is doing the initial scan
 *
 * - SCHEDULED_COPY_ACTIVE
 * The backup is active
 *
 * - SCHEDULED_COPY_ONGOING
 * A backup is being performed
 *
 * - SCHEDULED_COPY_SKIPPING
 * A backup is being skipped
 *
 * - SCHEDULED_COPY_REMOVING_EXCEEDING
 * The backup is active and an exceeding backup is being removed
 * @return State of the backup
 */
@property (readonly, nonatomic) NSUInteger state;


// Current backup data:
/**
 * @brief Returns the number of folders created in the backup
 * @return number of folders created in the backup
 */
@property (readonly, nonatomic) long long numberFolders;

/**
 * @brief Returns the number of files created in the backup
 * @return number of files created in the backup
 */
@property (readonly, nonatomic) long long numberFiles;

/**
 * @brief Returns the number of files to be created in the backup
 * @return number of files to be created in the backup
 */
@property (readonly, nonatomic) long long totalFiles;

/**
 * @brief Returns the starting time of the current backup being processed (in deciseconds)
 *
 * The returned value is a monotonic time since some unspecified starting point expressed in
 * deciseconds.
 *
 * @return Starting time of the backup (in deciseconds)
 */
@property (readonly, nonatomic) int64_t currentBKStartTime;

/**
 * @brief Returns the number of transferred bytes during last backup
 * @return Transferred bytes during this backup
 */
@property (readonly, nonatomic) long long transferredBytes;

/**
 * @brief Returns the total bytes to be transferred to complete last backup
 * @return Total bytes to be transferred to complete the backup
 */
@property (readonly, nonatomic) long long totalBytes;

/**
 * @brief Returns the current speed of last backup
 * @return Current speed of this backup
 */
@property (readonly, nonatomic) long long speed;

/**
 * @brief Returns the average speed of last backup
 * @return Average speed of this backup
 */
@property (readonly, nonatomic) long long meanSpeed;

/**
 * @brief Returns the timestamp when the last data was received (in deciseconds)
 *
 * This timestamp doesn't have a defined starting point. Use the difference between
 * the return value of this function and MegaScheduledCopy::getCurrentBKStartTime to know how
 * much time the backup has been running.
 *
 * @return Timestamp when the last data was received (in deciseconds)
 */
@property (readonly, nonatomic) int64_t updateTime;

/**
 * @brief Returns the list with the transfers that have failed for during last backup
 *
 * You take the ownership of the returned value
 *
 * @return Names of the custom attributes of the node
 * @see MegaApi::setCustomNodeAttribute
 */
@property (readonly, nonatomic) MEGATransferList *failedTransferss;

@end

NS_ASSUME_NONNULL_END

