/**
 * @file MEGAAccountFeature.mm
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

    if (self != nil){
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

- (bool)isProPlan {
    self.megaAccountPlan ? self.megaAccountPlan->isProPlan() : NO;
}

- (MEGAAccountType)accountType {
    NSInteger accountLevelValue = self.megaAccountPlan ? self.megaAccountPlan->getAccountLevel() : -1;
    return [MEGAAccountTypeMapper accountTypeFromInteger:accountLevelValue];
}

- (nullable MEGAStringList *)features {
    return self.megaAccountPlan->getFeatures() ? [MEGAStringList.alloc initWithMegaStringList:self.megaAccountPlan->getFeatures() cMemoryOwn:YES] : nil;
}

- (int64_t)expirationTime {
    self.megaAccountPlan ? self.megaAccountPlan->getExpirationTime() : 0;
}

- (int32_t)type {
    self.megaAccountPlan ? self.megaAccountPlan->getType() : 0;
}

- (nullable NSString *)planId {
    self.megaAccountPlan ? [NSString stringWithUTF8String: self.megaAccountPlan->getId()] : nil;
}

@end
