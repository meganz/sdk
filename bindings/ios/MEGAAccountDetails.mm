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
#import "megaapi.h"

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

- (instancetype)clone {
    return self.accountDetails ? [[MEGAAccountDetails alloc] initWithMegaAccountDetails:self.accountDetails->copy() cMemoryOwn:YES] : nil;
}

- (MegaAccountDetails *)getCPtr {
    return self.accountDetails;
}

- (NSNumber *)storageUsed {
    return self.accountDetails ? [[NSNumber alloc] initWithLongLong:self.accountDetails->getStorageUsed()] : nil;
}

- (NSNumber *)storageMax {
    return self.accountDetails ? [[NSNumber alloc] initWithLongLong:self.accountDetails->getStorageMax()] : nil;
}

- (NSNumber *)transferOwnUsed {
    return self.accountDetails ? [[NSNumber alloc] initWithLongLong:self.accountDetails->getTransferOwnUsed()] : nil;
}

- (NSNumber *)transferMax {
    return self.accountDetails ? [[NSNumber alloc] initWithLongLong:self.accountDetails->getTransferMax()] : nil;
}

- (MEGAAccountType)type {
    return (MEGAAccountType) (self.accountDetails ? self.accountDetails->getProLevel() : 0);
}

- (NSInteger)proExpiration {
    return self.accountDetails ? self.accountDetails->getProExpiration() : 0;
}

- (MEGASubscriptionStatus)subscriptionStatus {
    return (MEGASubscriptionStatus) (self.accountDetails ? self.accountDetails->getSubscriptionStatus() : 0);
}

- (NSInteger)subscriptionRenewTime {
    return self.accountDetails ? self.accountDetails->getSubscriptionRenewTime() : 0;
}

- (NSString *)subscriptionMethod {
    const char *val = self.accountDetails->getSubscriptionMethod();
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;    
}

- (NSString *)subscriptionCycle {
    return self.accountDetails ? [[NSString alloc] initWithUTF8String:self.accountDetails->getSubscriptionCycle()] : nil;
}

- (NSInteger)numberUsageItems {
    return self.accountDetails ? self.accountDetails->getNumUsageItems() : 0;
}

- (NSNumber *)storageUsedForHandle:(uint64_t)handle {
    return self.accountDetails ? [[NSNumber alloc] initWithLongLong:self.accountDetails->getStorageUsed(handle)] : nil;
}

- (NSNumber *)numberFilesForHandle:(uint64_t)handle {
    return self.accountDetails ? [[NSNumber alloc] initWithLongLong:self.accountDetails->getNumFiles(handle)] : nil;
}

- (NSNumber *)numberFoldersForHandle:(uint64_t)handle {
    return self.accountDetails ? [[NSNumber alloc] initWithLongLong:self.accountDetails->getNumFolders(handle)] : nil;
}

@end
