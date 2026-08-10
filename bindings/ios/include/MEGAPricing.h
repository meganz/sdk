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

NS_ASSUME_NONNULL_BEGIN

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
 * - MEGAAccountTypeStarter = 11
 * - MEGAAccountTypeBasic = 12
 * - MEGAAccountTypeEssential = 13
 * - MEGAAccountTypeBusiness = 100
 * - MEGAAccountTypeProFlexi = 101
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
- (nullable NSString *)descriptionAtProductIndex:(NSInteger)index;

/**
 * @brief Get the iOS ID of the product
 *
 * @param index Product index (from 0 to [MEGAPricing products])
 * @return iOS ID of the product
 */
- (nullable NSString *)iOSIDAtProductIndex:(NSInteger)index;

/**
 * @brief Get trial duration in days
 *
 * The returned value will be 0 if the plan is not eligible for trial.
 *
 * @param index Product index (from 0 to [MEGAPricing products])
 * @return Trial duration in days
 */
- (unsigned int)trialDurationInDaysAtProductIndex:(NSInteger)index;

/**
 * @brief Check whether the product has a mobile offer
 *
 * Determines if the specified product includes an associated mobile offer.
 *
 * @param index Product index (from 0 to [MEGAPricing products])
 * @return True if the product has a mobile offer, false otherwise
 */
- (BOOL)hasMobileOffersAtProductIndex:(NSInteger)index;

/**
 * @brief Get the mobile offer identifier
 *
 * Returns the identifier of the mobile offer associated with the given
 * product.
 *
 * If the product does not have a mobile offer, this method returns a empty
 * string.
 *
 * @param index Product index (from 0 to [MEGAPricing products])
 * @return A null-terminated string containing the mobile offer ID
 */
- (nullable NSString *)mobileOfferIdAtProductIndex:(NSInteger)index;

/**
 * @brief Check whether the mobile offer title should be used
 *
 * Possible values are:
 *   - false: The mobile offer title should not be displayed.
 *   - true: The mobile offer title should be displayed.
 *
 * If the product does not have a mobile offer, this method returns false.
 *
 * @param index Product index (from 0 to [MEGAPricing products])
 * @return True if the mobile offer title should be displayed, false
 * otherwise
 */
- (BOOL)hasMobileOfferUatAtProductIndex:(NSInteger)index;

/**
 * @brief Get the localized label of the mobile offer
 *
 * Returns the campaign label associated with the given product's mobile
 * offer, already localized server-side per request language (e.g. "Easter
 * Sale", "World Backup Day Sale").
 *
 * If the product does not have a mobile offer, this method returns nil.
 *
 * @param index Product index (from 0 to [MEGAPricing products])
 * @return Localized mobile offer label
 */
- (nullable NSString *)mobileOfferLabelAtProductIndex:(NSInteger)index;

/**
 * @brief Get the discount percentage of the mobile offer
 *
 * Returns the percentage (e.g. 30, 45) to display alongside the mobile
 * offer label.
 *
 * If the product does not have a mobile offer, this method returns 0.
 *
 * @param index Product index (from 0 to [MEGAPricing products])
 * @return Mobile offer discount percentage, or 0 if there is no mobile
 * offer
 */
- (int)mobileOfferDiscountPercentageAtProductIndex:(NSInteger)index;

/**
 * @brief Get the mobile offer expiry timestamp (mo.e)
 *
 * Seconds since the Epoch. If the product has no mobile offer or no expiry,
 * this method returns 0.
 *
 * @param index Product index (from 0 to [MEGAPricing products])
 * @return Offer expiry timestamp in seconds, or 0
 */
- (int64_t)mobileOfferExpiryTimestampAtProductIndex:(NSInteger)index;

/**
 * @brief Get the mobile offer feature flags (mo.f)
 *
 * Bitmask of feature flags for the client; the meaning of each bit is defined
 * by the API. If the product has no mobile offer, this method returns 0.
 *
 * @param index Product index (from 0 to [MEGAPricing products])
 * @return Flags bitmask, or 0
 */
- (uint32_t)mobileOfferFlagsAtProductIndex:(NSInteger)index;

/**
 * @brief Get the reshow timeout of the mobile offer (mo.r)
 *
 * Timeout in seconds before the offer may be shown again. If the product has
 * no mobile offer or no reshow timeout, this method returns 0.
 *
 * @param index Product index (from 0 to [MEGAPricing products])
 * @return Reshow timeout in seconds, or 0
 */
- (int64_t)mobileOfferReshowIntervalAtProductIndex:(NSInteger)index;

/**
 * @brief Get the campaign of the mobile offer (mo.c)
 *
 * Offers of the same campaign share this identifier, so they can be shown and
 * dismissed as one.
 *
 * @param index Product index (from 0 to [MEGAPricing products])
 * @return Campaign identifier, or 0 if the offer belongs to no campaign
 */
- (uint64_t)mobileOfferCampaignIdAtProductIndex:(NSInteger)index;

/**
 * @brief Check whether the mobile offer has an iOS section (mo.ios)
 *
 * @param index Product index (from 0 to [MEGAPricing products])
 * @return True if the product's mobile offer has an iOS section
 */
- (BOOL)hasMobileOfferIosAtProductIndex:(NSInteger)index;

/**
 * @brief Get the App Store promotional offer id of the mobile offer (mo.ios.oid)
 *
 * If the product has no iOS mobile offer, this method returns an empty string.
 *
 * @param index Product index (from 0 to [MEGAPricing products])
 * @return App Store promotional offer id
 */
- (nullable NSString *)mobileOfferIosOfferIdAtProductIndex:(NSInteger)index;

/**
 * @brief Get the key id of the mobile offer iOS signature (mo.ios.ki)
 *
 * If the product has no iOS mobile offer, this method returns an empty string.
 *
 * @param index Product index (from 0 to [MEGAPricing products])
 * @return Key id
 */
- (nullable NSString *)mobileOfferIosKeyIdAtProductIndex:(NSInteger)index;

/**
 * @brief Get the nonce from the mobile offer iOS signature data (mo.ios.n)
 *
 * If the product has no iOS mobile offer, this method returns an empty string.
 *
 * @param index Product index (from 0 to [MEGAPricing products])
 * @return Nonce
 */
- (nullable NSString *)mobileOfferIosNonceAtProductIndex:(NSInteger)index;

/**
 * @brief Get the timestamp of the mobile offer iOS signature, in milliseconds (mo.ios.tsm)
 *
 * If the product has no iOS mobile offer, this method returns 0.
 *
 * @param index Product index (from 0 to [MEGAPricing products])
 * @return Signature timestamp in milliseconds, or 0
 */
- (int64_t)mobileOfferIosTimestampMsAtProductIndex:(NSInteger)index;

/**
 * @brief Get the server signature of the mobile offer iOS data (mo.ios.s)
 *
 * If the product has no iOS mobile offer, this method returns an empty string.
 *
 * @param index Product index (from 0 to [MEGAPricing products])
 * @return Server signature
 */
- (nullable NSString *)mobileOfferIosSignatureAtProductIndex:(NSInteger)index;

/**
 * @brief Check whether the mobile offer has an Android section (mo.and)
 *
 * @param index Product index (from 0 to [MEGAPricing products])
 * @return True if the product's mobile offer has an Android section
 */
- (BOOL)hasMobileOfferAndroidAtProductIndex:(NSInteger)index;

/**
 * @brief Get the Google Play offer id of the mobile offer (mo.and.oid)
 *
 * If the product has no Android mobile offer, this method returns an empty string.
 *
 * @param index Product index (from 0 to [MEGAPricing products])
 * @return Google Play offer id
 */
- (nullable NSString *)mobileOfferAndroidOfferIdAtProductIndex:(NSInteger)index;

NS_ASSUME_NONNULL_END

@end
