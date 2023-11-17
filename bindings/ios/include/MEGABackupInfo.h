/**
 * @file MEGABackupInfo.h
 * @brief Represents information of a Backup in MEGA
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
#import "BackUpState.h"
#import "BackUpSubState.h"

typedef NS_ENUM (NSInteger, MEGABackupType) {
    MEGABackupTypeInvalid = -1,
    MEGABackupTypeTwoWay = 0,
    MEGABackupTypeUpSync = 1,
    MEGABackupTypeDownSync = 2,
    MEGABackupTypeCameraUpload = 3,
    MEGABackupTypeMediaUpload = 4,
    MEGABackupTypeBackupUpload = 5,
};

typedef NS_ENUM(NSUInteger, MEGABackupHeartbeatStatus) {
    MEGABackupHeartbeatStatusUpToDate = 1,
    MEGABackupHeartbeatStatusSyncing = 2,
    MEGABackupHeartbeatStatusPending = 3,
    MEGABackupHeartbeatStatusInactive = 4,
    MEGABackupHeartbeatStatusUnknown = 5
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
 * - MEGABackupTypeInvalid                   = -1,
 * Invalid backup type.
 *
 * - MEGABackupTypeTwoWay                = 0,
 * Two way backup type.
 *
 * - MEGABackupTypeUpSync                 = 1,
 * Up sync backup type.
 *
 * - MEGABackupTypeDownSync            = 2,
 * Down sync backup type.
 *
 * - MEGABackupTypeCameraUpload     = 3,
 * Camera Upload backup type.
 *
 * - MEGABackupTypeMediaUpload        = 4,
 * Media Upload backup type.
 *
 * - MEGABackupTypeBackupUpload      = 5,
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
 * - BackUpStateInvalid                           = -1,
 * Not initialized sync state.
 *
 * - BackUpStateNotInitialized                 = 0,
 * Not initialized sync state.
 *
 * - BackUpStateActive                            = 1,
 * Working fine (enabled)
 *
 * - BackUpStateFailed                            = 2,
 * Failed (permanently disabled)
 *
 * - BackUpStateTemporaryDisabled      = 3,
 * emporarily disabled due to a transient situation (e.g: account blocked). Will be resumed when the condition passes
 *
 * - BackUpStateDisabled                       = 4,
 * Disabled by the user
 *
 * - BackUpStatePauseUp                       = 5,
 * Active but upload transfers paused in the SDK
 *
 * - BackUpStatePauseDown                  = 6,
 * Active but download transfers paused in the SDK
 *
 * - BackUpStatePauseFull                     = 7,
 * Active but transfers paused in the SDK
 *
 * - BackUpStateDeleted                        = 8,
 * Sync needs to be deleted, as required by sync-desired-state received from BackupCenter (WebClient)
 *
 * - BackUpStateUnknown                     = 9,
 * Unknown status.
 *
 */
@property (readonly, nonatomic) BackUpState state;

/**
 * @brief Returns the sync substate of the backup.
 *
 * @return Substate of the backup.
 *
 * It can be one of the following values:
 * - BackUpSubStateInvalid                                                          =  -1,
 *   No synchronization error.
 *
 * - BackUpSubStateNoSyncError                                                 =  0,
 *   No synchronization error.
 *
 * - BackUpSubStateUnknownError                                              =  1,
 *   Unknown error occurred during the backup process.
 *
 * - BackUpSubStateUnsupportedFileSystem                               =  2,
 *   The file system used is not supported.
 *
 * - BackUpSubStateInvalidRemoteType                                      =  3,
 *   Invalid remote type, it is not a folder that can be synced.
 *
 * - BackUpSubStateInvalidLocalType                                          =  4,
 *   Invalid local type, its path does not refer to a folder.
 *
 * - BackUpSubStateInitialScanFailed                                          =  5,
 *   Initial scan failed.
 *
 * - BackUpSubStateLocalPathTemporaryUnavailable                 =  6,
 *   Temporary unavailability of the local path. This is fatal when adding a sync.
 *
 * - BackUpSubStateLocalPathUnavailable                                  =  7,
 *   The local path is unavailable (can't be opened).
 *
 * - BackUpSubStateRemoteNodeNotFound                                =  8,
 *   The remote node no longer exists.
 *
 * - BackUpSubStateStorageOverquota                                       =  9,
 *   Account reached storage overquota.
 *
 * - BackUpSubStateAccountExpired                                           = 10,
 *   Account expired (business or pro flexi).
 *
 * - BackUpSubStateForeignTargetOverstorage                          = 11,
 *   Sync transfer fails (upload into an inshare whose account is overquota).
 *
 * - BackUpSubStateRemotePathHasChanged                          = 12,
 *   The remote path has changed (currently unused: not an error).
 *
 * - BackUpSubStateShareNonFullAccess                                 = 14,
 *   Existing inbound share sync or part thereof lost full access.
 *
 * - BackUpSubStateLocalFilesystemMismatch                         = 15,
 *   Filesystem fingerprint does not match the one stored for the synchronization.
 *
 * - BackUpSubStatePutNodesError                                          = 16,
 *   Error processing put nodes result.
 *
 * - BackUpSubStateActiveSyncBelowPath                              = 17,
 *   There's a synced node below the path to be synced.
 *
 * - BackUpSubStateActiveSyncAbovePath                              = 18,
 *   There's a synced node above the path to be synced.
 *
 * - BackUpSubStateRemoteNodeMovedToRubbish                = 19,
 *   The remote node for backup was moved to the rubbish bin.
 *
 * - BackUpSubStateRemoteNodeInsideRubbish                     = 20,
 *   The remote node for backup is attempted to be added in rubbish.
 *
 * - BackUpSubStateVBoxSharedFolderUnsupported              = 21,
 *   Found unsupported VBoxSharedFolderFS.
 *
 * - BackUpSubStateLocalPathSyncCollision                           = 22,
 *   Local path includes a synced path or is included within one.
 *
 * - BackUpSubStateAccountBlocked                                       = 23,
 *   The backup account has been blocked.
 *
 * - BackUpSubStateUnknownTemporaryError                        = 24,
 *   Unknown temporary error occurred during backup.
 *
 * - BackUpSubStateTooManyActionPackets                          = 25,
 *   Too many changes in account, local state discarded.
 *
 * - BackUpSubStateLoggedOut                                             = 26,
 *   The user has been logged out.
 *
 * - BackUpSubStateWholeAccountRefetched                       = 27,
 *   The whole account was reloaded, missed actionpacket changes could not have been applied.
 *
 * - BackUpSubStateMissingParentNode                               = 28,
 *   Setting a new parent to a parent whose LocalNode is missing its corresponding Node crossref.
 *
 * - BackUpSubStateBackupModified                                     = 29,
 *   The backup has been externally modified.
 *
 * - BackUpSubStateBackupSourceNotBelowDrive               = 30,
 *   The backup source path not below drive path.
 *
 * - BackUpSubStateSyncConfigWriteFailure                        = 31,
 *   Unable to write sync config to disk.
 *
 * - BackUpSubStateActiveSyncSamePath                           = 32,
 *   There's a synced node at the path to be synced.
 *
 * - BackUpSubStateCouldNotMoveCloudNodes                  = 33,
 *   rename() failed.
 *
 * - BackUpSubStateCouldNotCreateIgnoreFile                    = 34,
 *   Couldn't create a sync's initial ignore file.
 *
 * - BackUpSubStateSyncConfigReadFailure                       = 35,
 *   Couldn't read sync configs from disk.
 *
 * - BackUpSubStateUnknownDrivePath                              = 36,
 *   Sync's drive path isn't known.
 *
 * - BackUpSubStateInvalidScanInterval                              = 37,
 *   The user's specified an invalid scan interval.
 *
 * - BackUpSubStateNotificationSystemUnavailable           = 38,
 *   Filesystem notification subsystem has encountered an unrecoverable error.
 *
 * - BackUpSubStateUnableToAddWatch                           = 39,
 *   Unable to add a filesystem watch.
 *
 * - BackUpSubStateUnableToRetrieveRootFSID              = 40,
 *   Unable to retrieve a sync root's FSID.
 *
 * - BackUpSubStateUnableToOpenDatabase                   = 41,
 *   Unable to open state cache database.
 *
 * - BackUpSubStateInsufficientDiskSpace                        = 42,
 *   Insufficient space for download.
 *
 * - BackUpSubStateFailureAccessingPersistentStorage  = 43,
 *   Failure accessing persistent storage.
 *
 * - BackUpSubStateMismatchOfRootRSID                       = 44,
 *   The sync root's FSID changed. So this is a different folder.
 *
 * - BackUpSubStateFilesystemFileIdsAreUnstable           = 45,
 *   On MAC, the FSID of a file in an exFAT drive can change frequently.
 *
 * - BackUpSubStateFilesystemIDUnavailable          = 46,
 *   Could not get the filesystem's id
 *
 */
@property (readonly, nonatomic) BackUpSubState substate;

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
@property (readonly, nonatomic, nullable) NSDate *timestamp;

/**
 * @brief Returns the status of the backup, as reported by heartbeats.
 *
 * @return Heartbeat substatus
 *
 * It can be one of the following values:
 * - MEGABackupHeartbeatStatusUpToDate          = 1,
 * The backup status is up to date.
 *
 * - MEGABackupHeartbeatStatusSyncing             = 2,
 * The backup status is currently syncing.
 *
 * - MEGABackupHeartbeatStatusPending             = 3,
 * The backup status is pending.
 *
 * - MEGABackupHeartbeatStatusInactive             = 4,
 * The backup status is inactive.
 *
 * - MEGABackupHeartbeatStatusUnknown          = 5,
 * The backup status is unknown.
 *
 */
@property (readonly, nonatomic) MEGABackupHeartbeatStatus status;

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
@property (readonly, nonatomic, nullable) NSDate *activityTimestamp;

/**
 * @brief Returns handle of the last synced node.
 */
@property (readonly, nonatomic) uint64_t lastSync;

/**
 * @brief Returns the user-agent associated with the device where the backup originated.
 *
 */
@property (readonly, nonatomic, nullable) NSString *userAgent;

@end

NS_ASSUME_NONNULL_END
