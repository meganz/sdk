/**
 * @file BackUpSubState.h
 * @brief Backup substates
 *
 * (c) 2023- by Mega Limited, Auckland, New Zealand
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

typedef NS_ENUM (NSInteger, BackUpSubState) {
    BackUpSubStateInvalid = -1,
    BackUpSubStateNoSyncError = 0,
    BackUpSubStateUnknownError = 1,
    BackUpSubStateUnsupportedFileSystem = 2,
    BackUpSubStateInvalidRemoteType = 3,
    BackUpSubStateInvalidLocalType = 4,
    BackUpSubStateInitialScanFailed = 5,
    BackUpSubStateLocalPathTemporaryUnavailable = 6,
    BackUpSubStateLocalPathUnavailable = 7,
    BackUpSubStateRemoteNodeNotFound = 8,
    BackUpSubStateStorageOverquota = 9,
    BackUpSubStateAccountExpired = 10,
    BackUpSubStateForeignTargetOverstorage = 11,
    BackUpSubStateRemotePathHasChanged = 12,
    BackUpSubStateShareNonFullAccess = 14,
    BackUpSubStateLocalFilesystemMismatch = 15,
    BackUpSubStatePutNodesError = 16,
    BackUpSubStateActiveSyncBelowPath = 17,
    BackUpSubStateActiveSyncAbovePath = 18,
    BackUpSubStateRemoteNodeMovedToRubbish = 19,
    BackUpSubStateRemoteNodeInsideRubbish = 20,
    BackUpSubStateVBoxSharedFolderUnsupported = 21,
    BackUpSubStateLocalPathSyncCollision = 22,
    BackUpSubStateAccountBlocked = 23,
    BackUpSubStateUnknownTemporaryError = 24,
    BackUpSubStateTooManyActionPackets = 25,
    BackUpSubStateLoggedOut = 26,
    BackUpSubStateWholeAccountRefetched = 27,
    BackUpSubStateMissingParentNode = 28,
    BackUpSubStateBackupModified = 29,
    BackUpSubStateBackupSourceNotBelowDrive = 30,
    BackUpSubStateSyncConfigWriteFailure = 31,
    BackUpSubStateActiveSyncSamePath = 32,
    BackUpSubStateCouldNotMoveCloudNodes = 33,
    BackUpSubStateCouldNotCreateIgnoreFile = 34,
    BackUpSubStateSyncConfigReadFailure = 35,
    BackUpSubStateUnknownDrivePath = 36,
    BackUpSubStateInvalidScanInterval = 37,
    BackUpSubStateNotificationSystemUnavailable = 38,
    BackUpSubStateUnableToAddWatch = 39,
    BackUpSubStateUnableToRetrieveRootFSID = 40,
    BackUpSubStateUnableToOpenDatabase = 41,
    BackUpSubStateInsufficientDiskSpace = 42,
    BackUpSubStateFailureAccessingPersistentStorage = 43,
    BackUpSubStateMismatchOfRootRSID = 44,
    BackUpSubStateFilesystemFileIdsAreUnstable = 45,
    BackUpSubStateFilesystemIDUnavailable = 46
};

