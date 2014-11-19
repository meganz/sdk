//
//  MEGATransfer.h
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MEGANode.h"

typedef NS_ENUM (NSInteger, MEGATransferType) {
    MEGATransferTypeDownload,
    MEGATransferTypeUpload
};

/**
 * @brief Provides information about a transfer
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
 * @brief Type of the transfer (MEGATransferTypeDownload, MEGATransferTypeUpload)
 */
@property (readonly, nonatomic) MEGATransferType type;

/**
 * @brief A readable string showing the type of transfer (UPLOAD, DOWNLOAD)
 */
@property (readonly, nonatomic) NSString *transferString;

/**
 * @brief The starting time of the request
 */
@property (readonly, nonatomic) NSDate *startTime;

/**
 * @brief Transferred bytes during this request
 */
@property (readonly, nonatomic) NSNumber *transferredBytes;

/**
 * @brief Total bytes to be transferred to complete the transfer
 */
@property (readonly, nonatomic) NSNumber *totalBytes;

/**
 * @brief Local path related to this request
 *
 * For uploads, this function returns the path to the source file. For downloads, it
 * returns the path of the destination file.
 *
 */
@property (readonly, nonatomic) NSString *path;

/**
 * @brief The parent path related to this request
 *
 * For uploads, this function returns the path to the folder containing the source file.
 * For downloads, it returns that path to the folder containing the destination file.
 *
 */
@property (readonly, nonatomic) NSString *parentPath;

/**
 * @brief Handle related to this transfer
 *
 * For downloads, this function returns the handle of the source node. For uploads,
 * it always returns mega::INVALID_HANDLE
 *
 * It is possible to get the MegaNode corresponding to a just uploaded file in [MEGAGlobalListener onNodesUpdate]
 * or [MEGAListener onNodesUpdate].
 *
 */
@property (readonly, nonatomic) uint64_t nodeHandle;

/**
 * @brief Handle of the parent node related to this transfer
 *
 * For downloads, this function returns always mega::INVALID_HANDLE. For uploads,
 * it returns the handle of the destination node (folder) for the uploaded file.
 *
 */
@property (readonly, nonatomic) uint64_t parentHandle;

/**
 * @brief Starting position of the transfer for streaming downloads, otherwise 0
 *
 * The return value of this fuction will be 0 if the transfer isn't a streaming
 * download ([MEGASdk startStreaming])
 *
 */
@property (readonly, nonatomic) uint64_t startPosition;

/**
* @brief End position of the transfer for streaming downloads, otherwise 0
*
* The return value of this fuction will be 0 if the transfer isn't a streaming
* download ([MEGASdk startStreaming])
*
*/
@property (readonly, nonatomic) uint64_t endPosition;

/**
 * @brief Name of the file that is being transferred
 *
 * It's possible to upload a file with a different name ([MEGASdk startUploadWithLocalPath:parent:]). In that case,
 * this function returns the destination name.
 *
 */
@property (readonly, nonatomic) NSString *fileName;

/**
 * @brief Number of times that a transfer has temporarily failed
 */
@property (readonly, nonatomic) NSInteger numberRetry;

/**
 * @brief Maximum number of times that the transfer will be retried
 */
@property (readonly, nonatomic) NSInteger maximunRetries;

/**
 * @brief An integer that identifies this transfer
 */
@property (readonly, nonatomic) NSInteger tag;

/**
 * @brief The average speed of this transfer
 */
@property (readonly, nonatomic) NSNumber *speed;

/**
 * @brief Number of bytes transferred since the previous callback
 * @see [MEGADelega onTransferUpdate], [MEGATransferDelegate onTransferUpdate]
 */
@property (readonly, nonatomic) NSNumber *deltaSize;

/**
 * @brief Timestamp when the last data was received
 *
 * This timestamp doesn't have a defined starting point. Use the difference between
 * the return value of this function and [MEGATransfer startTime] to know how
 * much time the transfer has been running.
 *
 */
@property (readonly, nonatomic) NSDate *updateTime;

/**
 * @brief A public node related to the transfer
 *
 * The return value is only valid for downloads of public nodes
 * You take the ownership of the returned value.
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
 * You are the owner of the returned object
 *
 * @return Copy of the MEGATransfer object
 */
- (instancetype)clone;

@end
