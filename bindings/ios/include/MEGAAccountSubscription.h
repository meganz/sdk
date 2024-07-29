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
#import "MEGAAccountType.h"
#import "MEGAStringList.h"
#import "MEGASubscriptionStatus.h"

/**
 * @brief Details about a MEGA account subscription.
 */
@interface MEGAAccountSubscription : NSObject

/**
 * @brief Get the ID of this subscription
 *
 * @return ID of this subscription
 */
@property (readonly, nonatomic, nullable) NSString *subcriptionId;

/**
 * @brief Check if the subscription is active
 *
 * If this function returns MEGASubscriptionStatusValid
 * the subscription will be automatically renewed.
 * See [MEGAAccountSubscription renewTime]
 *
 * @return Information about the subscription status
 *
 * Valid return values are:
 * - MEGASubscriptionStatusNone = 0
 * There isn't any active subscription
 *
 * - MEGASubscriptionStatusValid = 1
 * There is an active subscription
 *
 * - MEGASubscriptionStatusInvalid = 2
 * A subscription exists, but it uses a payment gateway that is no longer valid
 */
@property (readonly, nonatomic) MEGASubscriptionStatus status;

/**
 * @brief Get the subscription cycle
 *
 * The return value will show if the subscription will be montly or yearly renewed.
 * Example return values: "1 M", "1 Y".
 *
 * @return Subscription cycle
 */
@property (readonly, nonatomic, nullable) NSString *cycle;

/**
 * @brief Get the subscription payment provider name
 *
 * @return Payment provider name
 */
@property (readonly, nonatomic, nullable) NSString *paymentMethod;

/**
 * @brief Get the subscription payment provider ID
 *
 * @return Payment provider ID
 */
@property (readonly, nonatomic) int32_t paymentMethodId;

/**
 * @brief Get the subscription renew timestamp
 *
 * @return Renewal timestamp (in seconds since epoch)
 */
@property (readonly, nonatomic) int64_t renewTime;

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
 * - MEGAAccountTypeFeature = 99999
 */
@property (readonly, nonatomic) MEGAAccountType accountType;

/**
 * @brief Get the features granted by this subscription
 *
 * @return Features granted by this subscription.
 */
@property (readonly, nonatomic, nullable) MEGAStringList *features;

@end
