//
//  MTransfer.m
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MTransfer.h"
#import "MNode+init.h"

using namespace mega;

@interface MTransfer ()

@property MegaTransfer *megaTransfer;
@property BOOL cMemoryOwn;

@end

@implementation MTransfer

- (instancetype)initWithMegaTransfer:(MegaTransfer *)megaTransfer cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _megaTransfer = megaTransfer;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaTransfer;
    }
}

- (instancetype)clone {
    return self.megaTransfer ? [[MTransfer alloc] initWithMegaTransfer:self.megaTransfer->copy() cMemoryOwn:YES] : nil;
}

- (MegaTransfer *)getCPtr {
    return self.megaTransfer;
}

- (MTransferType)getType {
    return (MTransferType) (self.megaTransfer ? self.megaTransfer->getType() : 0);
}

- (NSString *)getTransferString {
    if (!self.megaTransfer) return nil;
    
    return self.megaTransfer ? [[NSString alloc] initWithUTF8String:self.megaTransfer->getTransferString()] : nil;
}

- (NSDate *)getStartTime {
    return self.megaTransfer ? [[NSDate alloc] initWithTimeIntervalSince1970:self.megaTransfer->getStartTime()] : nil;
}

- (NSNumber *)getTransferredBytes {
    return self.megaTransfer ? [[NSNumber alloc] initWithLongLong:self.megaTransfer->getTransferredBytes()] : nil;
}

- (NSNumber *)getTotalBytes {
    return self.megaTransfer ? [[NSNumber alloc] initWithLongLong:self.megaTransfer->getTotalBytes()] : nil;
}

- (NSString *)getPath {
    if (!self.megaTransfer) return nil;
    
    return self.megaTransfer ? [[NSString alloc] initWithUTF8String:self.megaTransfer->getPath()] : nil;
}

- (NSString *)getParentPath {
    if (!self.megaTransfer) return nil;
    
    return self.megaTransfer ? [[NSString alloc] initWithUTF8String:self.megaTransfer->getParentPath()] : nil;
}

- (uint64_t)getNodeHandle {
    return self.megaTransfer ? self.megaTransfer->getNodeHandle() : ::mega::INVALID_HANDLE;
}

- (uint64_t)getParentHandle {
    return self.megaTransfer ? self.megaTransfer->getParentHandle() : ::mega::INVALID_HANDLE;
}

- (NSInteger)getNumConnections {
    return self.megaTransfer ? self.megaTransfer->getNumConnections() : 0;
}

- (uint64_t)getStartPos {
    return self.megaTransfer ? self.megaTransfer->getStartPos() : 0;
}

- (uint64_t)getEndPos {
    return self.megaTransfer ? self.megaTransfer->getEndPos() : 0;
}

- (NSInteger)getMaxSpeed {
    return self.megaTransfer ? self.megaTransfer->getMaxSpeed() : 0;
}

- (NSString *)getFileName {
    if (!self.megaTransfer) return nil;
    
    return self.megaTransfer ? [[NSString alloc] initWithUTF8String:self.megaTransfer->getFileName()] : nil;
}

- (NSInteger)getNumRetry {
    return self.megaTransfer ? self.megaTransfer->getNumRetry() : 0;
}

- (NSInteger)getMaxRetries {
    return self.megaTransfer ? self.megaTransfer->getMaxRetries() : 0;
}

- (NSDate *)getTime {
    return self.megaTransfer ? [[NSDate alloc] initWithTimeIntervalSince1970:self.megaTransfer->getTime()] : nil;
}

- (NSString *)getBase64Key {
    if (!self.megaTransfer) return nil;
    
    return self.megaTransfer ? [[NSString alloc] initWithUTF8String:self.megaTransfer->getBase64Key()] : nil;
}

- (NSInteger)getTag {
    return self.megaTransfer ? self.megaTransfer->getTag() : 0;
}

- (NSNumber *)getSpeed {
    return self.megaTransfer ? [[NSNumber alloc] initWithLongLong:self.megaTransfer->getSpeed()] : nil;
}

- (NSNumber *)getDeltaSize {
    return self.megaTransfer ? [[NSNumber alloc] initWithLongLong:self.megaTransfer->getDeltaSize()] : nil;
}

- (NSDate *)getUpdateTime {
    return self.megaTransfer ? [[NSDate alloc] initWithTimeIntervalSince1970:self.megaTransfer->getUpdateTime()] : nil;
}

- (MNode *)getPublicNode {
    return self.megaTransfer && self.megaTransfer->getPublicNode() ? [[MNode alloc] initWithMegaNode:self.megaTransfer->getPublicNode()->copy() cMemoryOwn:YES] : nil;
}

- (BOOL)isSyncTransfer {
    return self.megaTransfer ? self.megaTransfer->isSyncTransfer() : NO;
}

- (BOOL)isStreamingTransfer {
    return self.megaTransfer ? self.megaTransfer->isStreamingTransfer() : NO;
}

@end
