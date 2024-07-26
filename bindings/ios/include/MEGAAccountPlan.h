/**
 * @file MEGAAccountPlan.h
 * @brief Details about a MEGA account plan
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

/**
 * @brief Details about a MEGA account plan.
 */
@interface MEGAAccountPlan : NSObject

/**
 * @brief Check if the plan is a PRO plan or a feature plan.
 *
 * @return YES if the plan is a PRO plan
 */
@property (readonly, nonatomic) BOOL isProPlan;

/**
 * @brief Get account level of the plan
 *
 * @return Plan level of the MEGA account.
 * Valid values for PRO plans are:
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
 * Valid value for feature plans is:
 * - MEGAAccountTypeFeature = 99999
 */
@property (readonly, nonatomic) MEGAAccountType accountType;

/**
 * @brief Get the expiration time for the plan
 *
 * @return The time the plan expires
 */
@property (readonly, nonatomic) int64_t expirationTime;

/**
 * @brief Get the features granted by this plan
 *
 *
 * @return Features granted by this plan.
 */
- (nullable MEGAStringList *)features;

/**
 * @brief The type of plan. Why it was granted.
 *
 * Not available for Bussiness/Pro Flexi.
 *
 * @return Plan type
 */
@property (readonly, nonatomic) int32_t type;

/**
 * @brief Get the relating subscription ID
 *
 * Only available if the plan relates to a subscription.
 *
 *
 * @return ID of this subscription
 */
@property (readonly, nonatomic, nullable) NSString *subscriptionId;

@end
