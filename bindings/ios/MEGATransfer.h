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

@interface MEGATransfer : NSObject

@property (readonly) MEGATransferType type;
@property (readonly) NSString *transfer;
@property (readonly) NSDate *startTime;
@property (readonly) NSNumber *transferredBytes;
@property (readonly) NSNumber *totalBytes;
@property (readonly) NSString *path;
@property (readonly) NSString *parentPath;
@property (readonly) uint64_t nodeHandle;
@property (readonly) uint64_t parentHandle;
@property (readonly) NSInteger numberConnections;
@property (readonly) uint64_t startPosition;
@property (readonly) uint64_t endPosition;
@property (readonly) NSInteger maximunSpeed;
@property (readonly) NSString *fileName;
@property (readonly) NSInteger numberRetry;
@property (readonly) NSInteger maximunRetries;
@property (readonly) NSDate *time;
@property (readonly) NSString *base64Key;
@property (readonly) NSInteger tag;
@property (readonly) NSNumber *speed;
@property (readonly) NSNumber *deltaSize;
@property (readonly) NSDate *updateTime;
@property (readonly) MEGANode *publicNode;

- (instancetype)clone;
- (BOOL)isSyncTransfer;
- (BOOL)isStreamingTransfer;

@end
