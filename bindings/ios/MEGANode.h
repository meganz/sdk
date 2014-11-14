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

@property (readonly) MEGANodeType type;
@property (readonly) NSString *name;
@property (readonly) NSString *base64Handle;
@property (readonly) NSNumber *size;
@property (readonly) NSDate *creationTime;
@property (readonly) NSDate *modificationTime;
@property (readonly) uint64_t handle;
@property (readonly) NSInteger tag;

- (instancetype)clone;
- (BOOL)isFile;
- (BOOL)isFolder;
- (BOOL)isRemoved;
- (BOOL)hasThumbnail;
- (BOOL)hasPreview;

@end
