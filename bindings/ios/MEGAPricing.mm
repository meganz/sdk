//
//  MEGAPricing.m
//  mega
//
//  Created by Javier Navarro on 30/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MEGAPricing.h"
#import "megaapi.h"

using namespace mega;

@interface MEGAPricing ()

@property MegaPricing *pricing;
@property BOOL cMemoryOwn;

@end

@implementation MEGAPricing

- (instancetype)initWithMegaPricing:(MegaPricing *)pricing cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _pricing = pricing;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (MegaPricing *)getCPtr {
    return self.pricing;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _pricing;
    }
}

- (instancetype)clone {
    return self.pricing ? [[MEGAPricing alloc] initWithMegaPricing:self.pricing->copy() cMemoryOwn:YES] : nil;
}

- (NSInteger)products {
    return self.pricing ? self.pricing->getNumProducts() : 0;
}

- (uint64_t)handleAtProductIndex:(NSInteger)index {
    return self.pricing ? self.pricing->getHandle((int)index) : INVALID_HANDLE;
}

- (MEGAAccountType)proLevelAtProductIndex:(NSInteger)index {
    return (MEGAAccountType) (self.pricing ? self.pricing->getProLevel((int)index) : 0);
}

- (NSInteger)storageGBAtProductIndex:(NSInteger)index {
    return self.pricing ? self.pricing->getGBStorage((int)index) : 0;
}

- (NSInteger)transferGBAtProductIndex:(NSInteger)index {
    return self.pricing ? self.pricing->getGBTransfer((int)index) : 0;
}

- (NSInteger)monthsAtProductIndex:(NSInteger)index {
    return self.pricing ? self.pricing->getMonths((int)index) : 0;
}

- (NSInteger)amountAtProductIndex:(NSInteger)index {
    return self.pricing ? self.pricing->getAmount((int)index) : 0;
}

- (NSString *)currencyAtProductIndex:(NSInteger)index {
    if (!self.pricing) return nil;
    
    return self.pricing->getCurrency((int)index) ? [[NSString alloc] initWithUTF8String:self.pricing->getCurrency((int)index)] : nil;
}

@end
