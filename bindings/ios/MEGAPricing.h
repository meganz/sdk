//
//  MEGAPricing.h
//  mega
//
//  Created by Javier Navarro on 30/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MEGAAccountDetails.h"

@interface MEGAPricing : NSObject

@property (readonly) NSInteger products;

- (uint64_t)handleAtProductIndex:(NSInteger)index;
- (MEGAAccountType)proLevelAtProductIndex:(NSInteger)index;
- (NSInteger)storageGBAtProductIndex:(NSInteger)index;
- (NSInteger)transferGBAtProductIndex:(NSInteger)index;
- (NSInteger)monthsAtProductIndex:(NSInteger)index;
- (NSInteger)amountAtProductIndex:(NSInteger)index;
- (NSString *)currencyAtProductIndex:(NSInteger)index;

@end
