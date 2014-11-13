//
//  MEGANode.h
//
//  Created by Javier Navarro on 01/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>

typedef NS_ENUM (NSInteger, MEGANodeType) {
    MEGANodeTypeUnknown = -1,
    MEGANodeTypeFile = 0,
    MEGANodeTypeFolder,	
    MEGANodeTypeRoot,
    MEGANodeTypeIncoming,
    MEGANodeTypeRubbish,
    MEGANodeTypeMail
};

@interface MEGANode : NSObject

- (instancetype)clone;
- (MEGANodeType)getType;
- (NSString *)getName;
- (NSString *)getBase64Handle;
- (NSNumber *)getSize;
- (NSDate *)getCreationTime;
- (NSDate *)getModificationTime;
- (uint64_t)getHandle;
- (NSInteger)getTag;
- (BOOL)isFile;
- (BOOL)isFolder;
- (BOOL)isRemoved;
- (BOOL)isSyncDeleted;
- (BOOL)hasThumbnail;
- (BOOL)hasPreview;

@end
