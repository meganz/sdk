//
//  MEGAAcountDetails.m
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MEGAAccountDetails.h"
#import "megaapi.h"

using namespace mega;

@interface MEGAAcountDetails ()

- (instancetype)initWithMegaAccountDetails:(MegaAccountDetails *)accountDetails cMemoryOwn:(BOOL)cMemoryOwn;
- (MegaAccountDetails *)getCPtr;

@property MegaAccountDetails *accountDetails;
@property BOOL cMemoryOwn;

@end

@implementation MEGAAcountDetails

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

- (MegaAccountDetails *)getCPtr {
    return self.accountDetails;
}

- (NSNumber *)getUsedStorage {
    return self.accountDetails ? [[NSNumber alloc] initWithLongLong:self.accountDetails->getStorageUsed()] : nil;
}

- (NSNumber *)getMaxStorage {
    return self.accountDetails ? [[NSNumber alloc] initWithLongLong:self.accountDetails->getStorageMax()] : nil;
}

- (NSNumber *)getOwnUsedTransfer {
    return self.accountDetails ? [[NSNumber alloc] initWithLongLong:self.accountDetails->getTransferOwnUsed()] : nil;
}

- (NSNumber *)getMaxTransfer {
    return self.accountDetails ? [[NSNumber alloc] initWithLongLong:self.accountDetails->getTransferMax()] : nil;
}

- (MEGAAccountType)getProLevel {
    return (MEGAAccountType) (self.accountDetails ? self.accountDetails->getProLevel() : 0);
}

@end
