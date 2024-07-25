/**
 * @file MEGAAccountSubscription.h
 * @brief Details about a MEGA account subscription
 *
 * (c) 2024 by Mega Limited, Auckland, New Zealand
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
#import "MEGAStringList.h"

/**
 * @brief Details about a MEGA account subscription.
 */
@interface MEGAAccountSubscription : NSObject

/**
 * @brief Get the ID of this subscription
 *
 * You take the ownership of the returned value
 *
 * @return ID of this subscription
 */
- (nullable NSString *)getId;

/**
 * @brief Check if the subscription is active
 *
 * If this function returns MegaAccountDetails::SUBSCRIPTION_STATUS_VALID,
 * the subscription will be automatically renewed.
 * See MegaAccountSubscription::getRenewTime()
 *
 * @return Information about the subscription status
 *
 * Valid return values are:
 * - MegaAccountSubscription::SUBSCRIPTION_STATUS_NONE = 0
 * There isn't any active subscription
 *
 * - MegaAccountSubscription::SUBSCRIPTION_STATUS_VALID = 1
 * There is an active subscription
 *
 * - MegaAccountSubscription::SUBSCRIPTION_STATUS_INVALID = 2
 * A subscription exists, but it uses a payment gateway that is no longer valid
 */
- (int)getStatus;

/**
 * @brief Get the subscription cycle
 *
 * The return value will show if the subscription will be montly or yearly renewed.
 * Example return values: "1 M", "1 Y".
 *
 * You take the ownership of the returned value
 *
 * @return Subscription cycle
 */
- (nullable NSString *)getCycle;

/**
 * @brief Get the subscription payment provider name
 *
 * You take the ownership of the returned value
 *
 * @return Payment provider name
 */
- (nullable NSString *)getPaymentMethod;

/**
 * @brief Get the subscription payment provider ID
 *
 * @return Payment provider ID
 */
- (int32_t)getPaymentMethodId;

/**
 * @brief Get the subscription renew timestamp
 *
 * @return Renewal timestamp (in seconds since epoch)
 */
- (int64_t)getRenewTime;

/**
 * @brief Get the subscription account level
 *
 * @return Subscription account level
 * Valid values for PRO plan subscriptions:
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
 *
 * Valid value for feature plan subscriptions:
 * - MegaAccountDetails::ACCOUNT_TYPE_FEATURE = 99999
 */
- (int)getAccountLevel;

/**
 * @brief Get the features granted by this subscription
 *
 * You take the ownership of the returned value
 *
 * @return Features granted by this subscription.
 */
- (nullable MEGAStringList *)getFeatures;

@end
