/**
 * @file MEGATransfer.mm
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
    
    return self.megaTransfer->getTransferString() ? [[NSString alloc] initWithUTF8String:self.megaTransfer->getTransferString()] : nil;
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
    
    return self.megaTransfer->getPath() ? [[NSString alloc] initWithUTF8String:self.megaTransfer->getPath()] : nil;
}

- (NSString *)parentPath {
    if (!self.megaTransfer) return nil;
    
    return self.megaTransfer->getParentPath() ? [[NSString alloc] initWithUTF8String:self.megaTransfer->getParentPath()] : nil;
}

- (uint64_t)nodeHandle {
    return self.megaTransfer ? self.megaTransfer->getNodeHandle() : ::mega::INVALID_HANDLE;
}

- (uint64_t)parentHandle {
    return self.megaTransfer ? self.megaTransfer->getParentHandle() : ::mega::INVALID_HANDLE;
}

- (NSNumber *)startPos {
    return self.megaTransfer ? [[NSNumber alloc] initWithLongLong:self.megaTransfer->getStartPos()] : nil;
}

- (NSNumber *)endPos {
    return self.megaTransfer ? [[NSNumber alloc] initWithLongLong:self.megaTransfer->getEndPos()] : nil;
}

- (NSString *)fileName {
    if (!self.megaTransfer) return nil;
    
    return self.megaTransfer->getFileName() ? [[NSString alloc] initWithUTF8String:self.megaTransfer->getFileName()] : nil;
}

- (NSInteger) numRetry  {
    return self.megaTransfer ? self.megaTransfer->getNumRetry() : 0;
}

- (NSInteger) maxRetries  {
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
    return (self.megaTransfer && self.megaTransfer->getPublicMegaNode()) ? [[MEGANode alloc] initWithMegaNode:self.megaTransfer->getPublicMegaNode() cMemoryOwn:YES] : nil;
}

- (BOOL)isStreamingTransfer {
    return self.megaTransfer ? (BOOL) self.megaTransfer->isStreamingTransfer() : NO;
}

- (BOOL)isFolderTransfer {
    return self.megaTransfer ? (BOOL) self.megaTransfer->isFolderTransfer() : NO;
}

- (NSInteger)folderTransferTag {
    return self.megaTransfer ? self.megaTransfer->getFolderTransferTag() : 0;
}

- (NSString *)appData {
    if (!self.megaTransfer) return nil;
    
    return self.megaTransfer->getAppData() ? [[NSString alloc] initWithUTF8String:self.megaTransfer->getAppData()] : nil;
}

@end
