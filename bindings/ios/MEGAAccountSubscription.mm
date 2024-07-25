/**
 * @file MEGAAccountFeature.mm
 * @brief Details about a MEGA account subscription
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
#import "MEGAAccountSubscription.h"
#import "MEGAStringList+init.h"
#import "megaapi.h"

using namespace mega;

@interface MEGAAccountSubscription()

@property BOOL cMemoryOwn;
@property MegaAccountSubscription *megaAccountSubscription;

@end

@implementation MEGAAccountSubscription

- (instancetype)initWithMegaAccountSubscription:(MegaAccountSubscription *)accountSubscription cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];

    if (self != nil){
        _megaAccountSubscription = accountSubscription;
        _cMemoryOwn = cMemoryOwn;
    }

    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaAccountSubscription;
    }
}

- (nullable MegaAccountSubscription *)getCPtr {
    return self.megaAccountSubscription;
}

- (nullable NSString *)getId {
    return self.megaAccountSubscription ? [NSString stringWithUTF8String: self.megaAccountSubscription->getId()] : nil;
}

- (int)getStatus {
    return self.megaAccountSubscription ? self.megaAccountSubscription->getStatus() : 0;
}

- (nullable NSString *)getCycle {
    self.megaAccountSubscription ? [NSString stringWithUTF8String: self.megaAccountSubscription->getCycle()] : nil;
}

- (nullable NSString *)getPaymentMethod {
    self.megaAccountSubscription ? [NSString stringWithUTF8String: self.megaAccountSubscription->getPaymentMethod()] : nil;
}

- (int32_t)getPaymentMethodId {
    self.megaAccountSubscription ? self.megaAccountSubscription->getPaymentMethodId() : 0;
}

- (int64_t)getRenewTime {
    self.megaAccountSubscription ? self.megaAccountSubscription->getRenewTime() : 0;
}

- (int)getAccountLevel {
    int accountLevel = self.megaAccountSubscription ? self.megaAccountSubscription->getAccountLevel() : 0;
    return accountLevel;
}

- (MEGAStringList *)getFeatures {
    return self.megaAccountSubscription ? [MEGAStringList.alloc initWithMegaStringList:self.megaAccountSubscription->getFeatures() cMemoryOwn:YES] : nil;
}

@end
