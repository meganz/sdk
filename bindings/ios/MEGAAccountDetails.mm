/**
 * @file MEGAAccountDetails.mm
 * @brief Details about a MEGA account
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
#import "MEGAAccountDetails.h"
#import "MEGAAccountPlan+init.h"
#import "MEGAAccountFeature+init.h"
#import "MEGAAccountSubscription+init.h"
#import "megaapi.h"
#import "MEGAStringIntegerMap+init.h"

using namespace mega;

@interface MEGAAccountDetails ()

@property MegaAccountDetails *accountDetails;
@property BOOL cMemoryOwn;

@end

@implementation MEGAAccountDetails

- (instancetype)initWithMegaAccountDetails:(MegaAccountDetails *)accountDetails cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil){
        _accountDetails = accountDetails;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _accountDetails;
    }
}

- (nullable MegaAccountDetails *)getCPtr {
    return self.accountDetails;
}

- (long long)storageUsed {
    return self.accountDetails ? self.accountDetails->getStorageUsed(): -1;
}

- (long long)versionStorageUsed {
    return self.accountDetails ? self.accountDetails->getVersionStorageUsed(): -1;
}

- (long long)storageMax {
    return self.accountDetails ? self.accountDetails->getStorageMax(): -1;
}

- (long long)transferUsed {
    return self.accountDetails ? self.accountDetails->getTransferUsed(): -1;
}

- (long long)transferMax {
    return self.accountDetails ? self.accountDetails->getTransferMax(): -1;
}

- (MEGAAccountType)type {
    return (MEGAAccountType) (self.accountDetails ? self.accountDetails->getProLevel(): 0);
}

- (NSInteger)proExpiration {
    return self.accountDetails ? self.accountDetails->getProExpiration(): -1;
}

- (MEGASubscriptionStatus)subscriptionStatus __attribute__((deprecated("Use new API version 2 interfaces"))) {
    return (MEGASubscriptionStatus) (self.accountDetails ? self.accountDetails->getSubscriptionStatus(): -1);
}

- (NSInteger)subscriptionRenewTime __attribute__((deprecated("Use new API version 2 interfaces"))) {
    return self.accountDetails ? self.accountDetails->getSubscriptionRenewTime(): -1;
}

- (nullable NSString *)subscriptionMethod __attribute__((deprecated("Use new API version 2 interfaces"))) {
    const char *val = self.accountDetails ? self.accountDetails->getSubscriptionMethod() : nil;
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;    
}

- (MEGAPaymentMethod)subscriptionMethodId __attribute__((deprecated("Use new API version 2 interfaces"))) {
    return (MEGAPaymentMethod) (self.accountDetails ? self.accountDetails->getSubscriptionMethodId(): -1);
}

- (nullable NSString *)subscriptionCycle __attribute__((deprecated("Use new API version 2 interfaces"))) {
    const char *val = self.accountDetails ? self.accountDetails->getSubscriptionCycle() : nil;
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (NSInteger)numberUsageItems {
    return self.accountDetails ? self.accountDetails->getNumUsageItems(): -1;
}

- (NSInteger)numActiveFeatures {
    return self.accountDetails ? self.accountDetails->getNumActiveFeatures() : -1;
}

- (int64_t)subscriptionLevel __attribute__((deprecated("Use new API version 2 interfaces"))) {
    return self.accountDetails ? self.accountDetails->getSubscriptionLevel() : -1;
}

- (nullable MEGAAccountFeature *)activeFeatureAtIndex:(NSInteger)index {
    if (!self.accountDetails) {
        return nil;
    }
    MegaAccountFeature *feature = self.accountDetails->getActiveFeature((int)index);
    if (!feature) {
        return nil;
    }
    return [[MEGAAccountFeature alloc] initWithMegaAccountFeature:feature cMemoryOwn:YES];
}

- (NSDictionary<NSString *, NSNumber *> *)subscriptionFeatures __attribute__((deprecated("Use new API version 2 interfaces"))) {
    if (!self.accountDetails) {
        return nil;
    }
    MegaStringIntegerMap* featuresMap = self.accountDetails->getSubscriptionFeatures();
    if (!featuresMap) {
        return nil;
    }
    MEGAStringIntegerMap *megaStringIntegerMap = [[MEGAStringIntegerMap alloc] initWithMegaStringIntegerMap:featuresMap cMemoryOwn:YES];
    return [megaStringIntegerMap toDictionary];
}

- (long long)storageUsedForHandle:(uint64_t)handle {
    return self.accountDetails ? self.accountDetails->getStorageUsed(handle): -1;
}

- (long long)numberFilesForHandle:(uint64_t)handle {
    return self.accountDetails ? self.accountDetails->getNumFiles(handle): -1;
}

- (long long)numberFoldersForHandle:(uint64_t)handle {
    return self.accountDetails ? self.accountDetails->getNumFolders(handle): -1;
}

- (long long)versionStorageUsedForHandle:(uint64_t)handle {
    return self.accountDetails ? self.accountDetails->getVersionStorageUsed(handle): -1;
}

- (long long)numberOfVersionFilesForHandle:(uint64_t)handle {
    return self.accountDetails ? self.accountDetails->getNumVersionFiles(handle): -1;
}

- (NSInteger)numberOfPlans {
    return self.accountDetails ? self.accountDetails->getNumPlans(): 0;
}

- (nullable MEGAAccountPlan *)planAtIndex:(NSInteger)index {
    return self.accountDetails ? [[MEGAAccountPlan alloc] initWithMegaAccountPlan: self.accountDetails->getPlan((int)index) cMemoryOwn:YES] : nil;
}

- (NSInteger)numberOfSubscriptions {
    return self.accountDetails ? self.accountDetails->getNumSubscriptions() : 0;
}

- (nullable MEGAAccountSubscription *)subscriptionAtIndex:(NSInteger)index {
    return self.accountDetails ? [[MEGAAccountSubscription alloc] initWithMegaAccountSubscription:self.accountDetails->getSubscription((int)index) cMemoryOwn:YES] : nil;
}

+ (nullable NSString *)stringForAccountType:(MEGAAccountType)accountType {
    NSString *result;
    switch (accountType) {
        case MEGAAccountTypeFree:
            result = @"Free";
            break;
            
        case MEGAAccountTypeProI:
            result = @"Pro I";
            break;
            
        case MEGAAccountTypeProII:
            result = @"Pro II";
            break;
            
        case MEGAAccountTypeProIII:
            result = @"Pro III";
            break;
            
        case MEGAAccountTypeLite:
            result = @"Pro Lite";
            break;
            
        case MEGAAccountTypeBusiness:
            result = @"Business";
            break;
            
        case MEGAAccountTypeProFlexi:
            result = @"Pro Flexi";
            break;
            
        case MEGAAccountTypeStarter:
            result = @"Starter";
            break;
            
        case MEGAAccountTypeBasic:
            result = @"Basic";
            break;
            
        case MEGAAccountTypeEssential:
            result = @"Essential";
            break;
            
        default:
            result = @"Unknown";
            break;
    }
    
    return result;
}

@end
