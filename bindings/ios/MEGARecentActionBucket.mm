/**
 * @file MEGARecentActionBucket.mm
 * @brief Represents a set of files uploaded or updated in MEGA.
 *
 * (c) 2019 - Present by Mega Limited, Auckland, New Zealand
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

#import "MEGARecentActionBucket.h"

#import "megaapi.h"

#import "MEGARecentActionBucket+init.h"
#import "MEGANodeList+init.h"

using namespace mega;

@interface MEGARecentActionBucket ()

@property MegaRecentActionBucket *recentActionBucket;
@property BOOL cMemoryOwn;

@end

@implementation MEGARecentActionBucket

- (instancetype)initWithMegaRecentActionBucket:(MegaRecentActionBucket *)megaRecentActionBucket cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    if (self) {
        _recentActionBucket = megaRecentActionBucket;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (MegaRecentActionBucket *)getCPtr {
    return self.recentActionBucket;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _recentActionBucket;
    }
}

- (NSDate *)timestamp {
    return self.recentActionBucket ? [NSDate.alloc initWithTimeIntervalSince1970:self.recentActionBucket->getTimestamp()] : nil;
}

- (NSString *)userEmail {
    if (self.recentActionBucket) {
        return self.recentActionBucket->getUserEmail() ? [NSString.alloc initWithUTF8String:self.recentActionBucket->getUserEmail()] : nil;
    } else {
        return nil;
    }
}

- (uint64_t)parentHandle {
    return self.recentActionBucket ? self.recentActionBucket->getParentHandle() : ::mega::INVALID_HANDLE;
}

- (BOOL)isUpdate {
    return self.recentActionBucket->isUpdate();
}

- (BOOL)isMedia {
    return self.recentActionBucket->isMedia();
}

- (MEGANodeList *)nodesList {
    return self.recentActionBucket ? [MEGANodeList.alloc initWithNodeList:self.recentActionBucket->getNodes()->copy() cMemoryOwn:YES] : nil;
}

@end

