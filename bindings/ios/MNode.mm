//
//  MNode.m
//
//  Created by Javier Navarro on 01/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MNode.h"
#import "megaapi.h"

using namespace mega;

@interface MNode()

@property MegaNode *megaNode;
@property BOOL cMemoryOwn;

@end

@implementation MNode

- (instancetype)initWithMegaNode:(MegaNode *)megaNode cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _megaNode = megaNode;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaNode;
    }
}

- (instancetype)clone {
    return self.megaNode ? [[MNode alloc] initWithMegaNode:self.megaNode->copy() cMemoryOwn:YES] : nil;
}

- (MegaNode *)getCPtr {
    return self.megaNode;
}

- (MNodeType)getType {
    return (MNodeType) (self.megaNode ? self.megaNode->getType() : MegaNode::TYPE_UNKNOWN);
}

- (NSString *)getName {
    if(!self.megaNode) return nil;
    
    return self.megaNode ? [[NSString alloc] initWithUTF8String:self.megaNode->getName()] : nil;
}

- (NSString *)getBase64Handle {
    if (!self.megaNode) return  nil;
    
    return self.megaNode ? [[NSString alloc] initWithUTF8String:self.megaNode->getBase64Handle()] : nil;
}

- (NSNumber *)getSize {
    return self.megaNode ? [[NSNumber alloc] initWithUnsignedLongLong:self.megaNode->getSize()] : nil;
}

- (NSDate *)getCreationTime {
    return self.megaNode ? [[NSDate alloc] initWithTimeIntervalSince1970:self.megaNode->getCreationTime()] : nil;
}

- (NSDate *)getModificationTime {
    return self.megaNode ? [[NSDate alloc] initWithTimeIntervalSince1970:self.megaNode->getModificationTime()] : nil;
}

- (uint64_t)getHandle {
    return self.megaNode ? self.megaNode->getHandle() : ::mega::INVALID_HANDLE;
}

- (NSInteger)getTag {
    return self.megaNode ? self.megaNode->getTag() : 0;
}

- (BOOL)isFile {
    return self.megaNode ? self.megaNode->isFile() : NO;
}

- (BOOL)isFolder {
    return self.megaNode ? self.megaNode->isFolder() : NO;
}

- (BOOL)isRemoved {
    return self.megaNode ? self.megaNode->isRemoved() : NO;
}

- (BOOL)isSyncDeleted {
    return self.megaNode ? self.megaNode->isSyncDeleted() : NO;
}

- (BOOL)hasThumbnail {
    return self.megaNode ? self.megaNode->hasThumbnail() : NO;
}

- (BOOL)hasPreview {
    return self.megaNode ? self.megaNode->hasPreview() : NO;
}

@end
