//
//  MEGATransfer.m
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MEGATransfer.h"
#import "MEGANode+init.h"

using namespace mega;

@interface MEGATransfer ()

@property MegaTransfer *megaTransfer;
@property BOOL cMemoryOwn;

@end

@implementation MEGATransfer

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
    return self.megaTransfer ? [[MEGATransfer alloc] initWithMegaTransfer:self.megaTransfer->copy() cMemoryOwn:YES] : nil;
}

- (MegaTransfer *)getCPtr {
    return self.megaTransfer;
}

- (MEGATransferType)type {
    return (MEGATransferType) (self.megaTransfer ? self.megaTransfer->getType() : 0);
}

- (NSString *)transferString {
    if (!self.megaTransfer) return nil;
    
    return self.megaTransfer ? [[NSString alloc] initWithUTF8String:self.megaTransfer->getTransferString()] : nil;
}

- (NSDate *)startTime {
    return self.megaTransfer ? [[NSDate alloc] initWithTimeIntervalSince1970:self.megaTransfer->getStartTime()] : nil;
}

- (NSNumber *)transferredBytes {
    return self.megaTransfer ? [[NSNumber alloc] initWithLongLong:self.megaTransfer->getTransferredBytes()] : nil;
}

- (NSNumber *)totalBytes {
    return self.megaTransfer ? [[NSNumber alloc] initWithLongLong:self.megaTransfer->getTotalBytes()] : nil;
}

- (NSString *)path {
    if (!self.megaTransfer) return nil;
    
    return self.megaTransfer ? [[NSString alloc] initWithUTF8String:self.megaTransfer->getPath()] : nil;
}

- (NSString *)parentPath {
    if (!self.megaTransfer) return nil;
    
    return self.megaTransfer ? [[NSString alloc] initWithUTF8String:self.megaTransfer->getParentPath()] : nil;
}

- (uint64_t)nodeHandle {
    return self.megaTransfer ? self.megaTransfer->getNodeHandle() : ::mega::INVALID_HANDLE;
}

- (uint64_t)parentHandle {
    return self.megaTransfer ? self.megaTransfer->getParentHandle() : ::mega::INVALID_HANDLE;
}

- (NSString *)fileName {
    if (!self.megaTransfer) return nil;
    
    return self.megaTransfer ? [[NSString alloc] initWithUTF8String:self.megaTransfer->getFileName()] : nil;
}

- (NSInteger)numberRetry {
    return self.megaTransfer ? self.megaTransfer->getNumRetry() : 0;
}

- (NSInteger)maximunRetries {
    return self.megaTransfer ? self.megaTransfer->getMaxRetries() : 0;
}

- (NSInteger)tag {
    return self.megaTransfer ? self.megaTransfer->getTag() : 0;
}

- (NSNumber *)speed {
    return self.megaTransfer ? [[NSNumber alloc] initWithLongLong:self.megaTransfer->getSpeed()] : nil;
}

- (NSNumber *)deltaSize {
    return self.megaTransfer ? [[NSNumber alloc] initWithLongLong:self.megaTransfer->getDeltaSize()] : nil;
}

- (NSDate *)updateTime {
    return self.megaTransfer ? [[NSDate alloc] initWithTimeIntervalSince1970:self.megaTransfer->getUpdateTime()] : nil;
}

- (MEGANode *)publicNode {
    return self.megaTransfer && self.megaTransfer->getPublicMegaNode() ? [[MEGANode alloc] initWithMegaNode:self.megaTransfer->getPublicMegaNode() cMemoryOwn:YES] : nil;
}

@end
