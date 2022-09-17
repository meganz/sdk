/**
 * @file MEGAPricing.h
 * @brief Details about pricing plans
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
#import <Foundation/Foundation.h>
#import "MEGAAccountDetails.h"

/**
 * @brief Details about pricing plans
 *
 * Use [MEGASdk pricing] to get the pricing plans to upgrade MEGA accounts
 */
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
 * @see [MEGASdk getPaymentIdForProductHandle:].
 */
- (uint64_t)handleAtProductIndex:(NSInteger)index;

/**
 * @brief Get the PRO level associated with the product.
 * @param index Product index (from 0 to [MEGAPricing products]).
 * @return PRO level associated with the product:
 * Valid values are:
 * - MEGAAccountTypeFree = 0
 * - MEGAAccountTypeProI = 1
 * - MEGAAccountTypeProII = 2
 * - MEGAAccountTypeProIII = 3
 * - MEGAAccountTypeLite = 4
 * - MEGAAccountTypeBusiness = 100
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
 * @brief Get the price of the product (in cents).
 * @param index Product index (from 0 to [MEGAPricing products]).
 * @return Price of the product (in cents).
 */
- (NSInteger)amountAtProductIndex:(NSInteger)index;

/**
 * @brief Get the price in the local currency (in cents)
 * @param index Product index (from 0 to MegaPricing::getNumProducts)
 * @return Price of the product (in cents)
 */
- (NSInteger)localPriceAtProductIndex:(NSInteger)index;

/**
 * @brief Get a description of the product
 *
 * @param index Product index (from 0 to [MEGAPricing products])
 * @return Description of the product
 */
- (NSString *)descriptionAtProductIndex:(NSInteger)index;

/**
 * @brief Get the iOS ID of the product
 *
 * @param index Product index (from 0 to [MEGAPricing products])
 * @return iOS ID of the product
 */
- (NSString *)iOSIDAtProductIndex:(NSInteger)index;

@end
