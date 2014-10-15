//
//  MTransfer.h
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MNode.h"

typedef NS_ENUM (NSInteger, MTransferType) {
    MTransferTypeDownload,
    MTransferTypeUpload
};

@interface MTransfer : NSObject

- (instancetype)clone;
- (MTransferType)getType;
- (NSString *)getTransferString;
- (NSDate *)getStartTime;
- (NSNumber *)getTransferredBytes;
- (NSNumber *)getTotalBytes;
- (NSString *)getPath;
- (NSString *)getParentPath;
- (uint64_t)getNodeHandle;
- (uint64_t)getParentHandle;
- (NSInteger)getNumConnections;
- (uint64_t)getStartPos;
- (uint64_t)getEndPos;
- (NSInteger)getMaxSpeed;
- (NSString *)getFileName;
- (NSInteger)getNumRetry;
- (NSInteger)getMaxRetries;
- (NSDate *) getTime;
- (NSString *)getBase64Key;
- (NSInteger)getTag;
- (NSNumber *)getSpeed;
- (NSNumber *)getDeltaSize;
- (NSDate *)getUpdateTime;
- (MNode *)getPublicNode;
- (BOOL)isSyncTransfer;
- (BOOL)isStreamingTransfer;

@end
