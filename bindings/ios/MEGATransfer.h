#import <Foundation/Foundation.h>
#import "MEGANode.h"

typedef NS_ENUM (NSInteger, MEGATransferType) {
    MEGATransferTypeDownload,
    MEGATransferTypeUpload
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
@property (readonly, nonatomic) NSString *transferString;

/**
 * @brief The starting time of the transfer.
 */
@property (readonly, nonatomic) NSDate *startTime;

/**
 * @brief Transferred bytes during this transfer.
 */
@property (readonly, nonatomic) NSNumber *transferredBytes;

/**
 * @brief Total bytes to be transferred to complete the transfer.
 */
@property (readonly, nonatomic) NSNumber *totalBytes;

/**
 * @brief Local path related to this transfer.
 *
 * For uploads, this property is the path to the source file. For downloads, it
 * is the path of the destination file.
 *
 */
@property (readonly, nonatomic) NSString *path;

/**
 * @brief The parent path related to this transfer.
 *
 * For uploads, this property is the path to the folder containing the source file.
 * For downloads, it is that path to the folder containing the destination file.
 *
 */
@property (readonly, nonatomic) NSString *parentPath;

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
 * @brief Name of the file that is being transferred.
 *
 * It's possible to upload a file with a different name ([MEGASdk startUploadWithLocalPath:parent:]). In that case,
 * this property is the destination name.
 *
 */
@property (readonly, nonatomic) NSString *fileName;

/**
 * @brief Number of times that a transfer has temporarily failed.
 */
@property (readonly, nonatomic) NSInteger  numRetry ;

/**
 * @brief Maximum number of times that the transfer will be retried.
 */
@property (readonly, nonatomic) NSInteger  maxRetries ;

/**
 * @brief An integer that identifies this transfer.
 */
@property (readonly, nonatomic) NSInteger tag;

/**
 * @brief The average speed of this transfer.
 */
@property (readonly, nonatomic) NSNumber *speed;

/**
 * @brief Number of bytes transferred since the previous callback.
 * @see [MEGADelegate onTransferUpdate:transfer:], [MEGATransferDelegate onTransferUpdate:transfer:]
 */
@property (readonly, nonatomic) NSNumber *deltaSize;

/**
 * @brief Timestamp when the last data was received
 *
 * This timestamp doesn't have a defined starting point. Use the difference between
 * the value of this property and [MEGATransfer startTime] to know how
 * much time the transfer has been running.
 *
 */
@property (readonly, nonatomic) NSDate *updateTime;

/**
 * @brief A public node related to the transfer
 *
 * The value is only valid for downloads of public nodes.
 *
 */
@property (readonly, nonatomic) MEGANode *publicNode;

/**
 * @brief Creates a copy of this MEGATransfer object
 *
 * The resulting object is fully independent of the source MEGATransfer,
 * it contains a copy of all internal attributes, so it will be valid after
 * the original object is deleted.
 *
 * You are the owner of the returned object.
 *
 * @return Copy of the MEGATransfer object
 */
- (instancetype)clone;

@end
