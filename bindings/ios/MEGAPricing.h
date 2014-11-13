//
//  MEGAPricing.h
//  mega
//
//  Created by Javier Navarro on 30/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>

@interface MEGAPricing : NSObject

- (NSInteger)getNumProducts;
- (uint64_t)getHandle:(NSInteger)productIndex;
- (NSInteger)getProLevel:(NSInteger)productIndex;
- (NSInteger)getGBStorage:(NSInteger)productIndex;
- (NSInteger)getGBTransfer:(NSInteger)productIndex;
- (NSInteger)getMonths:(NSInteger)productIndex;
- (NSInteger)getAmount:(NSInteger)productIndex;
- (NSString *)getCurrency:(NSInteger)productIndex;

@end
