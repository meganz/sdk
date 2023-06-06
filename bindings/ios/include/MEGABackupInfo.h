/**
 * @file MEGABackupInfo.h
 * @brief Represents information of a Backup in MEGA
 *
 * It allows getting all information about a Backup.
 *
 * Objects of this class aren't live, they are snapshots of the state of a Backup
 * when the object was created. They are immutable.
 *
 */

#import <Foundation/Foundation.h>

typedef NS_ENUM (NSInteger, MEGABackupType) {
    MEGABackupTypeInvalid = -1,
    MEGABackupTypeTwoWay = 0,
    MEGABackupTypeUpSync = 1,
    MEGABackupTypeDownSync = 2,
    MEGABackupTypeCameraUpload = 3,
    MEGABackupTypeMediaUpload = 4,
    MEGABackupTypeBackupUpload = 5,
};

typedef NS_ENUM (NSInteger, MEGASyncState) {
    MEGASyncStateNotInitialized = 0,
    MEGASyncStateUpToDate = 1,
    MEGASyncStateSyncing = 2,
    MEGASyncStatePending = 3,
    MEGASyncStateInactive = 4,
    MEGASyncStateUnknown = 5,
};

NS_ASSUME_NONNULL_BEGIN

@interface MEGABackupInfo : NSObject

/**
 * @brief Returns Backup id.
 */
@property (readonly, nonatomic) NSUInteger id;

/**
 * @brief Returns Backup type.
 *
 * @return Sync state of the backup.
 *
 * It can be one of the following values:
 * - MEGABackupTypeInvalid              = -1,
 * Invalid backup type.
 *
 * - MEGABackupTypeTwoWay           = 0,
 * Two way backup type.
 *
 * - MEGABackupTypeUpSync              = 1,
 * Up sync backup type.
 *
 * - MEGABackupTypeDownSync         = 2,
 * Down sync backup type.
 *
 * - MEGABackupTypeCameraUpload = 3,
 * Camera Upload backup type.
 *
 * - MEGABackupTypeMediaUpload      = 4,
 * Media Upload backup type.
 *
 * - MEGABackupTypeBackupUpload     = 5,
 * Backup upload backup type.
 *
 */
@property (readonly, nonatomic) MEGABackupType type;

/**
 * @brief Returns handle of Backup root.
 */
@property (readonly, nonatomic) uint64_t root;

/**
 * @brief Returns the name of the backed up local folder.
 */
@property (readonly, nonatomic, nullable) NSString *localFolder;

/**
 * @brief Returns the id of the device where the backup originated.
 */
@property (readonly, nonatomic, nullable) NSString *deviceId;

/**
 * @brief Returns the sync state of the backup.
 *
 * @return Sync state of the backup.
 *
 * It can be one of the following values:
 * - MEGASyncStateNotInitialized              = 0,
 * Not initialized sync state.
 *
 * - MEGASyncStateUpToDate                   = 1,
 * Up to date: local and remote paths are in sync.
 *
 * - MEGASyncStateSyncing                      = 2,
 * The sync engine is working, transfers are in progress.
 *
 * - MEGASyncStatePending                      = 3,
 * The sync engine is working, e.g: scanning local folders.
 *
 * - MEGASyncStateInactive                       = 4,
 * Sync is not active. A state != ACTIVE should have been sent through '''sp'''.
 *
 * - MEGASyncStateUnknown                    = 5,
 * Unknown status.
 *
 */
@property (readonly, nonatomic) MEGASyncState state;

/**
 * @brief Returns the sync substate of the backup.
 */
@property (readonly, nonatomic) NSUInteger substate;

/**
 * @brief Returns extra information, used as source for extracting other details.
 */
@property (readonly, nonatomic, nullable) NSString *extra;

/**
 * @brief Returns the name of the backup.
 */
@property (readonly, nonatomic, nullable) NSString *name;

/**
 * @brief Returns the timestamp of the backup, as reported by heartbeats.
 */
@property (readonly, nonatomic) NSDate *ts;

/**
 * @brief Returns the status of the backup, as reported by heartbeats.
 */
@property (readonly, nonatomic) NSUInteger status;

/**
 * @brief Returns the progress of the backup, as reported by heartbeats.
 */
@property (readonly, nonatomic) NSUInteger progress;

/**
 * @brief Returns upload count.
 */
@property (readonly, nonatomic) NSUInteger uploads;

/**
 * @brief Returns download count.
 */
@property (readonly, nonatomic) NSUInteger downloads;

/**
 * @brief Returns the last activity timestamp, as reported by heartbeats.
 */
@property (readonly, nonatomic) NSDate *activityTs;

/**
 * @brief Returns handle of the last synced node.
 */
@property (readonly, nonatomic) uint64_t lastSync;

@end

NS_ASSUME_NONNULL_END
