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

-(NSString *)getId {
    return [NSString stringWithUTF8String: _megaAccountSubscription->getId()];
}

-(int)getStatus {
    _megaAccountSubscription->getStatus();
}

-(NSString *)getCycle {
    return [NSString stringWithUTF8String: _megaAccountSubscription->getCycle()];
}

-(NSString *)getPaymentMethod {
    return [NSString stringWithUTF8String: _megaAccountSubscription->getPaymentMethod()];
}

-(int32_t)getPaymentMethodId {
    _megaAccountSubscription->getPaymentMethodId();
}

-(int64_t)getRenewTime {
    _megaAccountSubscription->getRenewTime();
}

-(int)getAccountLevel {
    int accountLevel = _megaAccountSubscription->getAccountLevel();
    return accountLevel;
}

-(MEGAStringList *)getFeatures {
    return [MEGAStringList.alloc initWithMegaStringList:_megaAccountSubscription->getFeatures() cMemoryOwn:YES];
}

@end
