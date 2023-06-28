/**
 * @file MEGAScheduledCopy.h
 * @brief Provides information about a backup
 *
 * (c) 2023 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#import <Foundation/Foundation.h>
#import "MEGATransferList.h"

typedef NS_ENUM(NSInteger, MEGAScheduledCopyState) {
    MEGAScheduledCopyStateFailed = -2,
    MEGAScheduledCopyStateCanceled = -1,
    MEGAScheduledCopyStateInitialScan = 0,
    MEGAScheduledCopyStateActive = 1,
    MEGAScheduledCopyStateOnGoing = 2,
    MEGAScheduledCopyStateSkipping = 3,
    MEGAScheduledCopyStateRemovingExceeding = 4,
};

NS_ASSUME_NONNULL_BEGIN

@interface MEGAScheduledCopy : NSObject

/**
 * @brief Get the handle of the folder that is being backed up
 * @return Handle of the folder that is being backed up in MEGA
 */
@property (readonly, nonatomic) uint64_t handle;

/**
 * @brief Get the path of the local folder that is being backed up
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
 *
 * Possible values are:
 * - MEGAScheduledCopyStateFailed                          = -2
 * The backup has failed and has been disabled
 *
 * - MEGAScheduledCopyStateCanceled                    = -1,
 * The backup has failed and has been disabled
 *
 * - MEGAScheduledCopyStateInitialScan                   = 0,
 * The backup is doing the initial scan
 *
 * - MEGAScheduledCopyStateActive                          = 1,
 * The backup is active
 *
 * - MEGAScheduledCopyStateOnGoing                     = 2,
 * A backup is being performed
 *
 * - MEGAScheduledCopyStateSkipping                      = 3,
 * A backup is being skipped
 *
 * - MEGAScheduledCopyStateRemovingExceeding   = 4,
 * The backup is active and an exceeding backup is being removed
 * @return State of the backup
 */
@property (readonly, nonatomic) MEGAScheduledCopyState state;


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
 * @return List with all failed transfers
 */
@property (readonly, nonatomic) MEGATransferList *failedTransfers;

@end

NS_ASSUME_NONNULL_END

