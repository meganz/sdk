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
#import "MEGAError+init.h"

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

- (MegaTransfer *)getCPtr {
    return self.megaTransfer;
}

- (MEGATransferType)type {
    return (MEGATransferType) (self.megaTransfer ? self.megaTransfer->getType() : 0);
}

- (nullable NSString *)transferString {
    if (!self.megaTransfer) return nil;

    return self.megaTransfer->getTransferString() ? [[NSString alloc] initWithUTF8String:self.megaTransfer->getTransferString()] : nil;
}

- (nullable NSDate *)startTime {
    return self.megaTransfer ? [[NSDate alloc] initWithTimeIntervalSince1970:self.megaTransfer->getStartTime()] : nil;
}

- (long long)transferredBytes {
    return self.megaTransfer ? self.megaTransfer->getTransferredBytes() : 0;
}

- (long long)totalBytes {
    return self.megaTransfer ? self.megaTransfer->getTotalBytes() : 0;
}

- (nullable NSString *)path {
    if (!self.megaTransfer) return nil;
    
    return self.megaTransfer->getPath() ? [[NSString alloc] initWithUTF8String:self.megaTransfer->getPath()] : nil;
}

- (nullable NSString *)parentPath {
    if (!self.megaTransfer) return nil;

    return self.megaTransfer->getParentPath() ? [[NSString alloc] initWithUTF8String:self.megaTransfer->getParentPath()] : nil;
}

- (uint64_t)nodeHandle {
    return self.megaTransfer ? self.megaTransfer->getNodeHandle() : ::mega::INVALID_HANDLE;
}

- (uint64_t)parentHandle {
    return self.megaTransfer ? self.megaTransfer->getParentHandle() : ::mega::INVALID_HANDLE;
}

- (long long)startPos {
    return self.megaTransfer ? self.megaTransfer->getStartPos() : 0;
}

- (long long)endPos {
    return self.megaTransfer ? self.megaTransfer->getEndPos() : 0;
}

- (nullable NSString *)fileName {
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

- (long long)speed {
    return self.megaTransfer ? self.megaTransfer->getSpeed() : 0;
}

- (long long)deltaSize {
    return self.megaTransfer ? self.megaTransfer->getDeltaSize() : 0;
}

- (nullable NSDate *)updateTime {
    return self.megaTransfer ? [[NSDate alloc] initWithTimeIntervalSince1970:self.megaTransfer->getUpdateTime()] : nil;
}

- (nullable MEGANode *)publicNode {
    if (self.megaTransfer) {
        MegaNode *n = self.megaTransfer->getPublicMegaNode();
        if (n) {
            MEGANode *node = [[MEGANode alloc] initWithMegaNode:n cMemoryOwn:YES];
            return node;
        }
    }
    return nil;
}

- (BOOL)isStreamingTransfer {
    return self.megaTransfer ? (BOOL) self.megaTransfer->isStreamingTransfer() : NO;
}

- (BOOL)isFinished {
    return self.megaTransfer ? self.megaTransfer->isFinished() : NO;
}

- (BOOL)isForeignOverquota {
    return self.megaTransfer ? self.megaTransfer->isForeignOverquota() : NO;
}

- (nullable MEGAError *)lastErrorExtended {
    mega::MegaError *e = (mega::MegaError *)self.megaTransfer->getLastErrorExtended();
    return e ? [[MEGAError alloc] initWithMegaError:e cMemoryOwn:NO] : nil;
}

- (BOOL)isFolderTransfer {
    return self.megaTransfer ? (BOOL) self.megaTransfer->isFolderTransfer() : NO;
}

- (NSInteger)folderTransferTag {
    return self.megaTransfer ? self.megaTransfer->getFolderTransferTag() : 0;
}

- (nullable NSString *)appData {
    if (!self.megaTransfer) return nil;
    
    return self.megaTransfer->getAppData() ? [[NSString alloc] initWithUTF8String:self.megaTransfer->getAppData()] : nil;
}

- (MEGATransferState)state {
    return (MEGATransferState) (self.megaTransfer ? self.megaTransfer->getState() : 0);
}

- (MEGATransferStage)stage {
    return (MEGATransferStage) (self.megaTransfer ? self.megaTransfer->getStage() : 0);
}

- (unsigned long long)priority {
    return self.megaTransfer ? self.megaTransfer->getPriority() : 0;
}

+ (nullable NSString *)stringForTransferStage:(MEGATransferStage)stage {
    const char *stageString = MegaTransfer::stageToString((unsigned) stage);
    return stageString ? [NSString stringWithUTF8String:stageString] : nil;
}

- (long long)notificationNumber {
    return self.megaTransfer ? self.megaTransfer->getNotificationNumber() : 0;
}

- (BOOL)targetOverride {
    return self.megaTransfer ? self.megaTransfer->getTargetOverride() : NO;
}

@end
