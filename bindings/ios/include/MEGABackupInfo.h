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
    MEGASyncStateActive = 1,
    MEGASyncStateFailed = 2,
    MEGASyncStateTemporaryDisabled = 3,
    MEGASyncStateDisabled = 4,
    MEGASyncStatePauseUp = 5,
    MEGASyncStatePauseDown = 6,
    MEGASyncStatePauseFull = 7,
    MEGASyncStateDeleted = 8,
    MEGASyncStateUnknown = 9
};

typedef NS_ENUM (NSInteger, MEGABackupSubstate) {
    MEGABackupSubstateNoSyncError = 0,
    MEGABackupSubstateUnknownError = 1,
    MEGABackupSubstateUnsupportedFileSystem = 2,
    MEGABackupSubstateInvalidRemoteType = 3,
    MEGABackupSubstateInvalidLocalType = 4,
    MEGABackupSubstateInitialScanFailed = 5,
    MEGABackupSubstateLocalPathTemporaryUnavailable = 6,
    MEGABackupSubstateLocalPathUnavailable = 7,
    MEGABackupSubstateRemoteNodeNotFound = 8,
    MEGABackupSubstateStorageOverquota = 9,
    MEGABackupSubstateAccountExpired = 10,
    MEGABackupSubstateForeignTargetOverstorage = 11,
    MEGABackupSubstateRemotePathHasChanged = 12,
    MEGABackupSubstateShareNonFullAccess = 14,
    MEGABackupSubstateLocalFilesystemMismatch = 15,
    MEGABackupSubstatePutNodesError = 16,
    MEGABackupSubstateActiveSyncBelowPath = 17,
    MEGABackupSubstateActiveSyncAbovePath = 18,
    MEGABackupSubstateRemoteNodeMovedToRubbish = 19,
    MEGABackupSubstateRemoteNodeInsideRubbish = 20,
    MEGABackupSubstateVBoxSharedFolderUnsupported = 21,
    MEGABackupSubstateLocalPathSyncCollision = 22,
    MEGABackupSubstateAccountBlocked = 23,
    MEGABackupSubstateUnknownTemporaryError = 24,
    MEGABackupSubstateTooManyActionPackets = 25,
    MEGABackupSubstateLoggedOut = 26,
    MEGABackupSubstateWholeAccountRefetched = 27,
    MEGABackupSubstateMissingParentNode = 28,
    MEGABackupSubstateBackupModified = 29,
    MEGABackupSubstateBackupSourceNotBelowDrive = 30,
    MEGABackupSubstateSyncConfigWriteFailure = 31,
    MEGABackupSubstateActiveSyncSamePath = 32,
    MEGABackupSubstateCouldNotMoveCloudNodes = 33,
    MEGABackupSubstateCouldNotCreateIgnoreFile = 34,
    MEGABackupSubstateSyncConfigReadFailure = 35,
    MEGABackupSubstateUnknownDrivePath = 36,
    MEGABackupSubstateInvalidScanInterval = 37,
    MEGABackupSubstateNotificationSystemUnavailable = 38,
    MEGABackupSubstateUnableToAddWatch = 39,
    MEGABackupSubstateUnableToRetrieveRootFSID = 40,
    MEGABackupSubstateUnableToOpenDatabase = 41,
    MEGABackupSubstateInsufficientDiskSpace = 42,
    MEGABackupSubstateFailureAccessingPersistentStorage = 43,
    MEGABackupSubstateMismatchOfRootRSID = 44,
    MEGABackupSubstateFilesystemFileIdsAreUnstable = 45,
    MEGABackupSubstateFilesystemIDUnavailable = 46
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
 * - MEGASyncStateNotInitialized                 = 0,
 * Not initialized sync state.
 *
 * - MEGASyncStateActive                            = 1,
 * Working fine (enabled)
 *
 * - MEGASyncStateFailed                            = 2,
 * Failed (permanently disabled)
 *
 * - MEGASyncStateTemporaryDisabled      = 3,
 * emporarily disabled due to a transient situation (e.g: account blocked). Will be resumed when the condition passes
 *
 * - MEGASyncStateDisabled                       = 4,
 * Disabled by the user
 *
 * - MEGASyncStatePauseUp                       = 5,
 * Active but upload transfers paused in the SDK
 *
 * - MEGASyncStatePauseDown                  = 6,
 * Active but download transfers paused in the SDK
 *
 * - MEGASyncStatePauseFull                     = 7,
 * Active but transfers paused in the SDK
 *
 * - MEGASyncStateDeleted                        = 8,
 * Sync needs to be deleted, as required by sync-desired-state received from BackupCenter (WebClient)
 *
 * - MEGASyncStateUnknown                     = 9,
 * Unknown status.
 *
 */
@property (readonly, nonatomic) MEGASyncState state;

/**
 * @brief Returns the sync substate of the backup.
 *
 * @return Substate of the backup.
 *
 * It can be one of the following values:
 * - MEGABackupSubstateNoSyncError                                                 =  0,
 *   No synchronization error.
 *
 * - MEGABackupSubstateUnknownError                                              =  1,
 *   Unknown error occurred during the backup process.
 *
 * - MEGABackupSubstateUnsupportedFileSystem                               =  2,
 *   The file system used is not supported.
 *
 * - MEGABackupSubstateInvalidRemoteType                                      =  3,
 *   Invalid remote type, it is not a folder that can be synced.
 *
 * - MEGABackupSubstateInvalidLocalType                                          =  4,
 *   Invalid local type, its path does not refer to a folder.
 *
 * - MEGABackupSubstateInitialScanFailed                                          =  5,
 *   Initial scan failed.
 *
 * - MEGABackupSubstateLocalPathTemporaryUnavailable                 =  6,
 *   Temporary unavailability of the local path. This is fatal when adding a sync.
 *
 * - MEGABackupSubstateLocalPathUnavailable                                  =  7,
 *   The local path is unavailable (can't be opened).
 *
 * - MEGABackupSubstateRemoteNodeNotFound                                =  8,
 *   The remote node no longer exists.
 *
 * - MEGABackupSubstateStorageOverquota                                       =  9,
 *   Account reached storage overquota.
 *
 * - MEGABackupSubstateAccountExpired                                           = 10,
 *   Account expired (business or pro flexi).
 *
 * - MEGABackupSubstateForeignTargetOverstorage                          = 11,
 *   Sync transfer fails (upload into an inshare whose account is overquota).
 *
 * - MEGABackupSubstateRemotePathHasChanged                          = 12,
 *   The remote path has changed (currently unused: not an error).
 *
 * - MEGABackupSubstateShareNonFullAccess                                 = 14,
 *   Existing inbound share sync or part thereof lost full access.
 *
 * - MEGABackupSubstateLocalFilesystemMismatch                         = 15,
 *   Filesystem fingerprint does not match the one stored for the synchronization.
 *
 * - MEGABackupSubstatePutNodesError                                          = 16,
 *   Error processing put nodes result.
 *
 * - MEGABackupSubstateActiveSyncBelowPath                              = 17,
 *   There's a synced node below the path to be synced.
 *
 * - MEGABackupSubstateActiveSyncAbovePath                              = 18,
 *   There's a synced node above the path to be synced.
 *
 * - MEGABackupSubstateRemoteNodeMovedToRubbish                = 19,
 *   The remote node for backup was moved to the rubbish bin.
 *
 * - MEGABackupSubstateRemoteNodeInsideRubbish                     = 20,
 *   The remote node for backup is attempted to be added in rubbish.
 *
 * - MEGABackupSubstateVBoxSharedFolderUnsupported              = 21,
 *   Found unsupported VBoxSharedFolderFS.
 *
 * - MEGABackupSubstateLocalPathSyncCollision                           = 22,
 *   Local path includes a synced path or is included within one.
 *
 * - MEGABackupSubstateAccountBlocked                                       = 23,
 *   The backup account has been blocked.
 *
 * - MEGABackupSubstateUnknownTemporaryError                        = 24,
 *   Unknown temporary error occurred during backup.
 *
 * - MEGABackupSubstateTooManyActionPackets                          = 25,
 *   Too many changes in account, local state discarded.
 *
 * - MEGABackupSubstateLoggedOut                                             = 26,
 *   The user has been logged out.
 *
 * - MEGABackupSubstateWholeAccountRefetched                       = 27,
 *   The whole account was reloaded, missed actionpacket changes could not have been applied.
 *
 * - MEGABackupSubstateMissingParentNode                               = 28,
 *   Setting a new parent to a parent whose LocalNode is missing its corresponding Node crossref.
 *
 * - MEGABackupSubstateBackupModified                                     = 29,
 *   The backup has been externally modified.
 *
 * - MEGABackupSubstateBackupSourceNotBelowDrive               = 30,
 *   The backup source path not below drive path.
 *
 * - MEGABackupSubstateSyncConfigWriteFailure                        = 31,
 *   Unable to write sync config to disk.
 *
 * - MEGABackupSubstateActiveSyncSamePath                           = 32,
 *   There's a synced node at the path to be synced.
 *
 * - MEGABackupSubstateCouldNotMoveCloudNodes                  = 33,
 *   rename() failed.
 *
 * - MEGABackupSubstateCouldNotCreateIgnoreFile                    = 34,
 *   Couldn't create a sync's initial ignore file.
 *
 * - MEGABackupSubstateSyncConfigReadFailure                       = 35,
 *   Couldn't read sync configs from disk.
 *
 * - MEGABackupSubstateUnknownDrivePath                              = 36,
 *   Sync's drive path isn't known.
 *
 * - MEGABackupSubstateInvalidScanInterval                              = 37,
 *   The user's specified an invalid scan interval.
 *
 * - MEGABackupSubstateNotificationSystemUnavailable           = 38,
 *   Filesystem notification subsystem has encountered an unrecoverable error.
 *
 * - MEGABackupSubstateUnableToAddWatch                           = 39,
 *   Unable to add a filesystem watch.
 *
 * - MEGABackupSubstateUnableToRetrieveRootFSID              = 40,
 *   Unable to retrieve a sync root's FSID.
 *
 * - MEGABackupSubstateUnableToOpenDatabase                   = 41,
 *   Unable to open state cache database.
 *
 * - MEGABackupSubstateInsufficientDiskSpace                        = 42,
 *   Insufficient space for download.
 *
 * - MEGABackupSubstateFailureAccessingPersistentStorage  = 43,
 *   Failure accessing persistent storage.
 *
 * - MEGABackupSubstateMismatchOfRootRSID                       = 44,
 *   The sync root's FSID changed. So this is a different folder.
 *
 * - MEGABackupSubstateFilesystemFileIdsAreUnstable           = 45,
 *   On MAC, the FSID of a file in an exFAT drive can change frequently.
 *
 * - MEGABackupSubstateFilesystemIDUnavailable          = 46,
 *   Could not get the filesystem's id
 *
 */
@property (readonly, nonatomic) MEGABackupSubstate substate;

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
