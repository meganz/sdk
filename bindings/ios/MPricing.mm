//
//  MPricing.m
//  mega
//
//  Created by Javier Navarro on 30/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MPricing.h"
#import "megaapi.h"

using namespace mega;

@interface MPricing ()

@property MegaPricing *pricing;
@property BOOL cMemoryOwn;

- (MegaAccountDetails *)getCPtr;

@end

@implementation MPricing

- (instancetype)initWithMegaPricing:(MegaPricing *)pricing cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _pricing = pricing;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _pricing;
    }
}

- (NSInteger)getNumProducts {
    return self.pricing ? self.pricing->getNumProducts() : 0;
}

- (uint64_t)getHandle:(NSInteger)productIndex {
    return self.pricing ? self.pricing->getHandle(productIndex) : INVALID_HANDLE;
}

- (NSInteger)getProLevel:(NSInteger)productIndex {
    return self.pricing ? self.pricing->getProLevel(productIndex) : 0;
}

- (NSInteger)getGBStorage:(NSInteger)productIndex {
    return self.pricing ? self.pricing->getGBStorage(productIndex) : 0;
}

- (NSInteger)getGBTransfer:(NSInteger)productIndex {
    return self.pricing ? self.pricing->getGBTransfer(productIndex) : 0;
}

- (NSInteger)getMonths:(NSInteger)productIndex {
    return self.pricing ? self.pricing->getMonths(productIndex) : 0;
}

- (NSInteger)getAmount:(NSInteger)productIndex {
    return self.pricing ? self.pricing->getAmount(productIndex) : 0;
}

- (NSString *)getCurrency:(NSInteger)productIndex {
    if (!self.pricing) return nil;
    
    return self.pricing->getCurrency(productIndex) ? [[NSString alloc] initWithUTF8String:self.pricing->getCurrency(productIndex)] : nil;
}

@end
