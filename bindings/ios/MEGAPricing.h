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

/**
 * @brief Number of available products to upgrade the account.
 */
@property (readonly, nonatomic) NSInteger products;

/**
 * @brief Creates a copy of this MEGAPricing object.
 *
 * The resulting object is fully independent of the source MEGAPricing,
 * it contains a copy of all internal attributes, so it will be valid after
 * the original object is deleted.
 *
 * You are the owner of the returned object.
 *
 * @return Copy of the MEGAPricing object.
 */
- (instancetype)clone;

/**
 * @brief Get the handle of a product.
 * @param index Product index (from 0 to [MEGAPricing products]).
 * @return Handle of the product.
 * @see [MEGASdk getPaymentULRWithProductHandle:].
 */
- (uint64_t)handleAtProductIndex:(NSInteger)index;

/**
 * @brief Get the PRO level associated with the product.
 * @param productIndex Product index (from 0 to [MEGAPricing products]).
 * @return PRO level associated with the product:
 * Valid values are:
 * - MEGAAccountTypeFree = 0
 * - MEGAAccountTypeProI = 1
 * - MEGAAccountTypeProII = 2
 * - MEGAAccountTypeProIII = 3
 */
- (MEGAAccountType)proLevelAtProductIndex:(NSInteger)index;

/**
 * @brief Get the number of GB of storage associated with the product.
 * @param index Product index (from 0 to [MEGAPricing products]).
 * @return number of GB of storage.
 */
- (NSInteger)storageGBAtProductIndex:(NSInteger)index;

/**
 * @brief Get the number of GB of bandwidth associated with the product.
 * @param index Product index (from 0 to [MEGAPricing products]).
 * @return number of GB of bandwidth.
 */
- (NSInteger)transferGBAtProductIndex:(NSInteger)index;

/**
 * @brief Get the duration of the product (in months).
 * @param index Product index (from 0 to [MEGAPricing products]).
 * @return duration of the product (in months).
 */
- (NSInteger)monthsAtProductIndex:(NSInteger)index;

/**
 * @brief getAmount Get the price of the product (in cents).
 * @param index Product index (from 0 to [MEGAPricing products]).
 * @return Price of the product (in cents).
 */
- (NSInteger)amountAtProductIndex:(NSInteger)index;

/**
 * @brief Get the currency associated with [MEGAPricing amountAtProductIndex:].
 *
 * The SDK retains the ownership of the returned value.
 *
 * @param index Product index (from 0 to [MEGAPricing products]).
 * @return Currency associated with [MEGAPricing amountAtProductIndex:].
 */
- (NSString *)currencyAtProductIndex:(NSInteger)index;

@end
