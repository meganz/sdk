/**
 * @file MEGAUserAlert.mm
 * @brief Represents a user alert in MEGA.
 *
 * (c) 2018-Present by Mega Limited, Auckland, New Zealand
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
#import "MEGAUserAlert.h"
#import "megaapi.h"

using namespace mega;

@interface MEGAUserAlert ()

@property MegaUserAlert *megaUserAlert;
@property BOOL cMemoryOwn;

@end

@implementation MEGAUserAlert

- (instancetype)initWithMegaUserAlert:(MegaUserAlert *)megaUserAlert cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self) {
        _megaUserAlert = megaUserAlert;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaUserAlert;
    }
}

- (MegaUserAlert *)getCPtr {
    return self.megaUserAlert;
}

- (NSUInteger)identifier {
    return self.megaUserAlert ? self.megaUserAlert->getId() : 0;
}

- (BOOL)isSeen {
    return self.megaUserAlert ? self.megaUserAlert->getSeen(): NO;
}

- (BOOL)isRelevant {
    return self.megaUserAlert ? self.megaUserAlert->getRelevant() : NO;
}

- (MEGAUserAlertType)type {
    return (MEGAUserAlertType)(self.megaUserAlert ? self.megaUserAlert->getType() : 0);
}

- (NSString *)typeString {
    if (!self.megaUserAlert) return nil;
    
    return self.megaUserAlert->getTypeString() ? [[NSString alloc] initWithUTF8String:self.megaUserAlert->getTypeString()] : nil;
}

- (uint64_t)userHandle {
    return self.megaUserAlert ? self.megaUserAlert->getUserHandle() : ::mega::INVALID_HANDLE;
}

- (uint64_t)nodeHandle {
    return self.megaUserAlert ? self.megaUserAlert->getNodeHandle() : ::mega::INVALID_HANDLE;
}

- (NSString *)email {
    if (!self.megaUserAlert) return nil;
    
    return self.megaUserAlert->getEmail() ? [[NSString alloc] initWithUTF8String:self.megaUserAlert->getEmail()] : nil;
}

- (NSString *)path {
    if (!self.megaUserAlert) return nil;
    
    return self.megaUserAlert->getPath()? [[NSString alloc] initWithUTF8String:self.megaUserAlert->getPath()] : nil;
}

- (NSString *)name {
    if (!self.megaUserAlert) return nil;
    
    return self.megaUserAlert->getName() ? [[NSString alloc] initWithUTF8String:self.megaUserAlert->getName()] : nil;
}

- (NSString *)heading {
    if (!self.megaUserAlert) return nil;
    
    return self.megaUserAlert->getHeading() ? [[NSString alloc] initWithUTF8String:self.megaUserAlert->getHeading()] : nil;
}

- (NSString *)title {
    if (!self.megaUserAlert) return nil;
    
    return self.megaUserAlert->getTitle() ? [[NSString alloc] initWithUTF8String:self.megaUserAlert->getTitle()] : nil;
}

- (BOOL)isOwnChange {
    return self.megaUserAlert ? self.megaUserAlert->isOwnChange() : NO;
}

- (instancetype)clone {
    return self.megaUserAlert ? [[MEGAUserAlert alloc] initWithMegaUserAlert:self.megaUserAlert->copy() cMemoryOwn:YES] : nil;
}

- (int64_t)numberAtIndex:(NSUInteger)index {
    return self.megaUserAlert ? self.megaUserAlert->getNumber((unsigned int) index) : -1;
}

- (int64_t)timestampAtIndex:(NSUInteger)index {
    return self.megaUserAlert ? self.megaUserAlert->getTimestamp((unsigned int) index) : -1;
}

- (NSString *)stringAtIndex:(NSUInteger)index {
    if (!self.megaUserAlert) return nil;
    
    return self.megaUserAlert->getString((unsigned int)index)? [[NSString alloc] initWithUTF8String:self.megaUserAlert->getEmail()] : nil;
}

@end
