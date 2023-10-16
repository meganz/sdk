/**
 * @file MEGAShare.mm
 * @brief Represents the outbound sharing of a folder with an user in MEGA
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
#import "MEGAShare.h"
#import "megaapi.h"

using namespace mega;

@interface MEGAShare ()

@property MegaShare *megaShare;
@property BOOL cMemoryOwn;

@end

@implementation MEGAShare

- (instancetype)initWithMegaShare:(MegaShare *)megaShare cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _megaShare = megaShare;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaShare;
    }
}

- (MegaShare *)getCPtr {
    return self.megaShare;
}

- (NSString *)user {
    if (!self.megaShare) return nil;
    
    return self.megaShare->getUser() ? [[NSString alloc] initWithUTF8String:self.megaShare->getUser()] : nil;
}

- (uint64_t)nodeHandle {
    return self.megaShare ? self.megaShare->getNodeHandle() : ::mega::INVALID_HANDLE;
}

- (MEGAShareType)access {
    return (MEGAShareType) (self.megaShare ? self.megaShare->getAccess() : MegaShare::ACCESS_UNKNOWN);
}

- (NSDate *)timestamp {
    return self.megaShare ? [[NSDate alloc] initWithTimeIntervalSince1970:self.megaShare->getTimestamp()] : nil;
}

- (BOOL)isPending {
    return self.megaShare ? self.megaShare->isPending() : NO;
}

- (BOOL)isVerified {
    return self.megaShare ? self.megaShare->isVerified() : NO;
}

@end
