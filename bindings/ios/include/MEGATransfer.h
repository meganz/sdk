/**
 * @file MEGATransfer.h
 * @brief Provides information about a transfer
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
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
#import "MEGANode.h"
#import "MEGAError.h"

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM (NSInteger, MEGATransferType) {
    MEGATransferTypeDownload,
    MEGATransferTypeUpload,
    MEGATransferTypeLocalTCPDownload,
    MEGATransferTypeLocalHTTPDownload = 2 //Kept for backwards compatibility
};

typedef NS_ENUM (NSInteger, MEGATransferState) {
    MEGATransferStateNone,
    MEGATransferStateQueued,
    MEGATransferStateActive,
    MEGATransferStatePaused,
    MEGATransferStateRetrying,
    MEGATransferStateCompleting,
    MEGATransferStateComplete,
    MEGATransferStateCancelled,
    MEGATransferStateFailed
};

typedef NS_ENUM (NSUInteger, MEGATransferStage) {
    MEGATransferStageNone = 0,
    MEGATransferStageScan,
    MEGATransferStageCreateTree,
    MEGATransferStageTransferringFiles,
    MEGATransferStageMax = MEGATransferStageTransferringFiles,
};

/**
 * @brief Provides information about a transfer.
 *
 * Developers can use delegates (MEGADelegate, MEGATransferDelegate)
 * to track the progress of each transfer. MEGATransfer objects are provided in callbacks sent
 * to these delegates and allow developers to know the state of the transfers, their parameters
 * and their results.
 *
 * Objects of this class aren't live, they are snapshots of the state of the transfer
 * when the object is created, they are immutable.
 *
 */
@interface MEGATransfer : NSObject

/**
 * @brief Type of the transfer (MEGATransferTypeDownload, MEGATransferTypeUpload).
 */
@property (readonly, nonatomic) MEGATransferType type;

/**
 * @brief A readable string showing the type of transfer (UPLOAD, DOWNLOAD).
 */
@property (readonly, nonatomic, nullable) NSString *transferString;

/**
 * @brief The starting time of the transfer.
 */
@property (readonly, nonatomic, nullable) NSDate *startTime;

/**
 * @brief Transferred bytes during this transfer.
 */
@property (readonly, nonatomic) long long transferredBytes;

/**
 * @brief Total bytes to be transferred to complete the transfer.
 */
@property (readonly, nonatomic) long long totalBytes;

/**
 * @brief Local path related to this transfer.
 *
 * For uploads, this property is the path to the source file. For downloads, it
 * is the path of the destination file.
 *
 */
@property (readonly, nonatomic, nullable) NSString *path;

/**
 * @brief The parent path related to this transfer.
 *
 * For uploads, this property is the path to the folder containing the source file.
 * For downloads, it is that path to the folder containing the destination file.
 *
 */
@property (readonly, nonatomic, nullable) NSString *parentPath;

/**
 * @brief Handle related to this transfer.
 *
 * For downloads, this property is the handle of the source node.
 *
 * For uploads, this property is the handle of the new node in [MEGATransferDelegate onTransferFinish:transfer:error:] and [MEGADelegate onTransferFinish:transfer:error:]
 * when the error code is MEGAErrorTypeApiOk, otherwise the value is mega::INVALID_HANDLE.
 *
 */
@property (readonly, nonatomic) uint64_t nodeHandle;

/**
 * @brief Handle of the parent node related to this transfer.
 *
 * For downloads, this property is mega::INVALID_HANDLE. For uploads,
 * it is the handle of the destination node (folder) for the uploaded file.
 *
 */
@property (readonly, nonatomic) uint64_t parentHandle;

/**
 * @brief The starting position of the transfer for streaming downloads
 *
 * The value of this fuction will be 0 if the transfer isn't a streaming
 * download ([MEGASdk startStreamingNode:startPos:size:])
 */
@property (readonly, nonatomic) long long startPos;

/**
 * @brief The end position of the transfer for streaming downloads
 *
 * The value of this fuction will be 0 if the transfer isn't a streaming
 * download ([MEGASdk startStreamingNode:startPos:size:])
 */
@property (readonly, nonatomic) long long endPos;

/**
 * @brief Name of the file that is being transferred.
 *
 * It's possible to upload a file with a different name ([MEGASdk startUploadWithLocalPath:parent:]). In that case,
 * this property is the destination name.
 *
 */
@property (readonly, nonatomic, nullable) NSString *fileName;

/**
 * @brief Number of times that a transfer has temporarily failed.
 */
@property (readonly, nonatomic) NSInteger numRetry;

/**
 * @brief Maximum number of times that the transfer will be retried.
 */
@property (readonly, nonatomic) NSInteger maxRetries;

/**
 * @brief An integer that identifies this transfer.
 */
@property (readonly, nonatomic) NSInteger tag;

/**
 * @brief The average speed of this transfer.
 */
@property (readonly, nonatomic) long long speed;

/**
 * @brief Number of bytes transferred since the previous callback.
 * @see [MEGADelegate onTransferUpdate:transfer:], [MEGATransferDelegate onTransferUpdate:transfer:]
 */
@property (readonly, nonatomic) long long deltaSize;

/**
 * @brief Timestamp when the last data was received
 *
 * This timestamp doesn't have a defined starting point. Use the difference between
 * the value of this property and [MEGATransfer startTime] to know how
 * much time the transfer has been running.
 *
 */
@property (readonly, nonatomic, nullable) NSDate *updateTime;

/**
 * @brief A public node related to the transfer
 *
 * The value is only valid for downloads of public nodes.
 *
 */
@property (readonly, nonatomic, nullable) MEGANode *publicNode;

/**
 * @brief YES if this is a streaming transfer, NO otherwise
 * @see [MEGASdk startStreamingNode:startPos:size:];
 */
@property (readonly, nonatomic) BOOL isStreamingTransfer;

/**
 * @brief YES if the transfer is at finished state (completed, cancelled or failed)
 * @return YES if this transfer is finished, NO otherwise
 */
@property (readonly, nonatomic, getter=isFinished) BOOL finished;

/**
 * @brief YES if the transfer has failed with MEGAErrorTypeApiEOverquota
 * and the target is foreign.
 *
 * @return YES if the transfer has failed with MEGAErrorTypeApiEOverquota and the target is foreign.
 */
@property (readonly, nonatomic, getter=isForeignOverquota) BOOL foreignOverquota;

/**
 * @brief The last error related to the transfer with extra info
 *
 */
@property (readonly, nonatomic, nullable) MEGAError *lastErrorExtended;

/**
 * @brief YES if it's a folder transfer, otherwise (file transfer) it returns NO
 */
@property (readonly, nonatomic) BOOL isFolderTransfer;

/**
 * @brief The identifier of the folder transfer associated to this transfer.
 * Tag of the associated folder transfer.
 *
 * This property is only useful for transfers automatically started in the context of a folder transfer.
 * For folder transfers (the ones directly started with startUpload), it returns -1
 * Otherwise, it returns 0
 *
 */
@property (readonly, nonatomic) NSInteger folderTransferTag;

/**
 * @brief The application data associated with this transfer
 *
 * You can set the data returned by this function in [MEGASdk startDownloadNode:localPath:] and
 * [MEGASdk startDownloadNode:localPath:delegate:]
 *
 */
@property (readonly, nonatomic, nullable) NSString *appData;

/**
 * @brief State of the transfer
 *
 * It can be one of these values:
 * - MEGATransferStateNone = 0
 * Unknown state. This state should be never returned.
 *
 * - MEGATransferStateQueued = 1
 * The transfer is queued. No data related to it is being transferred.
 *
 * - MEGATransferStateActive = 2
 * The transfer is active. Its data is being transferred.
 *
 * - MEGATransferStatePaused= 3
 * The transfer is paused. It won't be activated until it's resumed.
 *
 * - MEGATransferStateRetrying = 4
 * The transfer is waiting to be retried due to a temporary error.
 *
 * - MEGATransferStateCompleting = 5
 * The transfer is being completed. All data has been transferred
 * but it's still needed to attach the resulting node to the
 * account (uploads), to attach thumbnails/previews to the
 * node (uploads of images) or to create the resulting local
 * file (downloads). The transfer should be completed in a short time.
 *
 * - MEGATransferStateComplete = 6
 * The transfer has being finished.
 *
 * - MEGATransferStateCancelled = 7
 * The transfer was cancelled by the user.
 *
 * - MEGATransferStateFailed = 8
 * The transfer was cancelled by the SDK due to a fatal error or
 * after a high number of retries.
 *
 */
@property (readonly, nonatomic) MEGATransferState state;

/**
 * @brief The current stage in case this transfer represents a recursive operation.
 * This method can return the following values:
 *  - MEGATransferStageScan                      = 1
 *  - MEGATransferStageCreateTreee               = 2
 *  - MEGATransferStageTransferringFiles         = 3
 * Any other returned value, must be ignored.
 *
 * Note: a recursive operation (folder upload/download) can be cancelled using a MEGACancelToken,
 * but this cancellation mechanism will only have effect between the following stages:
 * MEGATransferStageScan and MEGATransferStageProcessTransferQueue both included.
 *
 */
@property (readonly, nonatomic) MEGATransferStage stage;

/**
 * @brief Returns the priority of the transfer
 *
 * This value is intended to keep the order of the transfer queue on apps.
 *
 * @return Priority of the transfer
 */
@property (readonly, nonatomic) unsigned long long priority;

/**
 * @brief Returns a string that identify the recursive operation stage
 *
 * @return A string that identify the recursive operation stage
 */
+ (nullable NSString *)stringForTransferStage:(MEGATransferStage)stage;

/**
 * @brief Returns the notification number of the SDK when this MEGATransfer was generated
 *
 * The notification number of the SDK is increased every time the SDK sends a callback
 * to the app.
 *
 * @return Notification number
 */
@property (readonly, nonatomic) long long notificationNumber;

/**
 * @brief Returns whether the target folder of the transfer was overriden by the API server
 *
 * It may happen that the target folder fo a transfer is deleted by the time the node
 * is going to be added. Hence, the API will create the node in the rubbish bin.
 *
 * @return YES if target folder was overriden (apps can check the final parent)
 */
@property (readonly, nonatomic) BOOL targetOverride;

NS_ASSUME_NONNULL_END

@end
