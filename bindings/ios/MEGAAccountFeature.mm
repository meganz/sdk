/**
 * @file MEGAAccountFeature.mm
 * @brief Details about a MEGA feature
 *
 * (c) 2024 by Mega Limited, Auckland, New Zealand
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
#import "MEGAAccountFeature.h"
#import "MEGAAccountFeature+init.h"

using namespace mega;

@interface MEGAAccountFeature ()

@property MegaAccountFeature *megaAccountFeature;
@property BOOL cMemoryOwn;

@end

@implementation MEGAAccountFeature

- (instancetype)initWithMegaAccountFeature:(mega::MegaAccountFeature *)megaAccountFeature cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];

    if (self) {
        _megaAccountFeature = megaAccountFeature;
        _cMemoryOwn = cMemoryOwn;
    }

    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaAccountFeature;
    }
}

- (mega::MegaAccountFeature *)getCPtr {
    return self.megaAccountFeature;
}

- (int64_t)expiry {
    return self.megaAccountFeature ? self.megaAccountFeature->getExpiry() : -1;
}

- (nullable NSString *)featureId {
    if (!self.megaAccountFeature) {
        return nil;
    }
    const char *cId = self.megaAccountFeature->getId();
    if (!cId) {
        return nil;
    }
    NSString *idString = [[NSString alloc] initWithUTF8String:cId];
    delete[] cId;
    return idString;
}

@end
