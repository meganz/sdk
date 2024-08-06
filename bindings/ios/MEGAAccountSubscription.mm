/**
 * @file MEGAAccountSubscription.mm
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

    if (self != nil) {
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

- (nullable NSString *)subcriptionId {
    const char *val = self.megaAccountSubscription ? self.megaAccountSubscription->getId() : nil;
    if (!val) return nil;

    NSString *ret = [[NSString alloc] initWithUTF8String:val];

    delete [] val;
    return ret;
}

- (MEGASubscriptionStatus)status {
    return (MEGASubscriptionStatus) (self.megaAccountSubscription ? self.megaAccountSubscription->getStatus() : 0);
}

- (nullable NSString *)cycle {
    const char *val = self.megaAccountSubscription ? self.megaAccountSubscription->getCycle() : nil;
    if (!val) return nil;

    NSString *ret = [[NSString alloc] initWithUTF8String:val];

    delete [] val;
    return ret;
}

- (nullable NSString *)paymentMethod {
    const char *val = self.megaAccountSubscription ? self.megaAccountSubscription->getPaymentMethod() : nil;
    if (!val) return nil;

    NSString *ret = [[NSString alloc] initWithUTF8String:val];

    delete [] val;
    return ret;
}

- (MEGAPaymentMethod)paymentMethodId {
    return (MEGAPaymentMethod) (self.megaAccountSubscription ? self.megaAccountSubscription->getPaymentMethodId() : -1);
}

- (int64_t)renewTime {
    return self.megaAccountSubscription ? self.megaAccountSubscription->getRenewTime() : 0;
}

- (MEGAAccountType)accountType {
    return (MEGAAccountType) (self.megaAccountSubscription ? self.megaAccountSubscription->getAccountLevel() : -1);
}

- (NSArray<NSString *>*)features {
    if (!self.megaAccountSubscription) return nil;

    MegaStringList* features = self.megaAccountSubscription ? self.megaAccountSubscription->getFeatures() : nil;
    
    if (!features) return nil;

    MEGAStringList* megaStringList = [MEGAStringList.alloc initWithMegaStringList:features cMemoryOwn:YES];

    return [megaStringList toStringArray];
}

@end
