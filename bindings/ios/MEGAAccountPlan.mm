/**
 * @file MEGAAccountPlan.mm
 * @brief Details about a MEGA account plan
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
#import "MEGAAccountPlan.h"
#import "MEGAStringList+init.h"
#import "megaapi.h"

using namespace mega;

@interface MEGAAccountPlan()

@property BOOL cMemoryOwn;
@property MegaAccountPlan *megaAccountPlan;

@end

@implementation MEGAAccountPlan

- (instancetype)initWithMegaAccountPlan:(MegaAccountPlan *)accountPlan cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];

    if (self != nil) {
        _megaAccountPlan = accountPlan;
        _cMemoryOwn = cMemoryOwn;
    }

    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaAccountPlan;
    }
}

- (nullable MegaAccountPlan *)getCPtr {
    return self.megaAccountPlan;
}

- (BOOL)isProPlan {
    return self.megaAccountPlan ? self.megaAccountPlan->isProPlan() : NO;
}

- (MEGAAccountType)accountType {
    return (MEGAAccountType) (self.megaAccountPlan ? self.megaAccountPlan->getAccountLevel() : -1);
}

- (NSArray<NSString *>*)features {
    if (!self.megaAccountPlan) return nil;

    MegaStringList* features = self.megaAccountPlan ? self.megaAccountPlan->getFeatures() : nil;
    
    if (!features) return nil;

    MEGAStringList* megaStringList = [MEGAStringList.alloc initWithMegaStringList:features cMemoryOwn:YES];

    return [megaStringList toStringArray];
}

- (int64_t)expirationTime {
    return self.megaAccountPlan ? self.megaAccountPlan->getExpirationTime() : 0;
}

- (int32_t)type {
    return self.megaAccountPlan ? self.megaAccountPlan->getType() : 0;
}

- (nullable NSString *)subscriptionId {
    const char *val = self.megaAccountPlan ? self.megaAccountPlan->getId() : nil;
    if (!val) return nil;

    NSString *ret = [[NSString alloc] initWithUTF8String:val];

    delete [] val;
    return ret;
}

@end
