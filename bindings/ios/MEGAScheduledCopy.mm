/**
 * @file MEGAScheduledCopy.mm
 * @brief Provides information about a transfer
 *
 * (c) 2023 by Mega Limited, Auckland, New Zealand
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
#import "MEGAScheduledCopy.h"
#import "megaapi.h"
#import "MEGATransferList+init.h"

using namespace mega;

@interface MEGAScheduledCopy ()

@property MegaScheduledCopy *megaScheduledCopy;
@property BOOL cMemoryOwn;

@end

@implementation MEGAScheduledCopy

- (instancetype)initWithMegaScheduledCopy:(MegaScheduledCopy *)megaScheduledCopy cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _megaScheduledCopy = megaScheduledCopy;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaScheduledCopy;
    }
}

- (MegaScheduledCopy *)getCPtr {
    return self.megaScheduledCopy;
}

- (uint64_t)handle {
    return self.megaScheduledCopy ? self.megaScheduledCopy->getMegaHandle() :
        ::mega::INVALID_HANDLE;
}

- (NSString *)localFolder {
    if(!self.megaScheduledCopy) return nil;
    
    return self.megaScheduledCopy->getLocalFolder() ? [[NSString alloc] initWithUTF8String:self.megaScheduledCopy->getLocalFolder()] : nil;
}

- (NSUInteger)tag {
    return self.megaScheduledCopy ? self.megaScheduledCopy->getTag() : 0;
}

- (BOOL)attendPastBackups {
    return self.megaScheduledCopy ? self.megaScheduledCopy->getAttendPastBackups() : NO;
}

- (int64_t)period {
    return self.megaScheduledCopy ? self.megaScheduledCopy->getPeriod() : 0;
}

- (NSString *)periodString {
    if(!self.megaScheduledCopy) return nil;
    
    return self.megaScheduledCopy->getPeriodString() ? [[NSString alloc] initWithUTF8String:self.megaScheduledCopy->getPeriodString()] : nil;
}

- (long long)nextStartTime {
    return self.megaScheduledCopy ? self.megaScheduledCopy->getNextStartTime() : -1;
}

- (NSUInteger)maxBackups {
    return self.megaScheduledCopy ? self.megaScheduledCopy->getMaxBackups() : 0;
}

- (MEGAScheduledCopyState)state {
    return (MEGAScheduledCopyState) (self.megaScheduledCopy ? self.megaScheduledCopy->getState() : MEGAScheduledCopyStateFailed);
}

- (long long)numberFolders {
    return self.megaScheduledCopy ? self.megaScheduledCopy->getNumberFolders() : 0;
}

- (long long)numberFiles {
    return self.megaScheduledCopy ? self.megaScheduledCopy->getNumberFiles() : 0;
}

- (long long)totalFiles {
    return self.megaScheduledCopy ? self.megaScheduledCopy->getTotalFiles() : 0;
}

- (int64_t)currentBKStartTime {
    return self.megaScheduledCopy ? self.megaScheduledCopy->getCurrentBKStartTime() : 0;
}

- (long long)transferredBytes {
    return self.megaScheduledCopy ? self.megaScheduledCopy->getTransferredBytes() : 0;
}

- (long long)totalBytes {
    return self.megaScheduledCopy ? self.megaScheduledCopy->getTotalBytes() : 0;
}

- (long long)speed {
    return self.megaScheduledCopy ? self.megaScheduledCopy->getSpeed() : 0;
}

- (long long)meanSpeed {
    return self.megaScheduledCopy ? self.megaScheduledCopy->getMeanSpeed() : 0;
}

- (int64_t)updateTime {
    return self.megaScheduledCopy ? self.megaScheduledCopy->getUpdateTime() : 0;
}

- (MEGATransferList *)failedTransfers {
    if (self.megaScheduledCopy == nil) return nil;
    return [[MEGATransferList alloc] initWithTransferList:self.megaScheduledCopy->getFailedTransfers() cMemoryOwn:YES];
}

@end

