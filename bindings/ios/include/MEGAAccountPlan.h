/**
 * @file MEGAAccountPlan.h
 * @brief Details about a MEGA account plan
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
#import "MEGAStringList.h"

/**
 * @brief Details about a MEGA account plan.
 */
@interface MEGAAccountPlan : NSObject

/**
 * @brief Check if the plan is a PRO plan or a feature plan.
 *
 * @return True if the plan is a PRO plan
 */
- (bool)isProPlan;

/**
 * @brief Get account level of the plan
 *
 * @return Plan level of the MEGA account.
 * Valid values for PRO plans are:
 * - MegaAccountDetails::ACCOUNT_TYPE_FREE = 0
 * - MegaAccountDetails::ACCOUNT_TYPE_PROI = 1
 * - MegaAccountDetails::ACCOUNT_TYPE_PROII = 2
 * - MegaAccountDetails::ACCOUNT_TYPE_PROIII = 3
 * - MegaAccountDetails::ACCOUNT_TYPE_LITE = 4
 * - MegaAccountDetails::ACCOUNT_TYPE_STARTER = 11
 * - MegaAccountDetails::ACCOUNT_TYPE_BASIC = 12
 * - MegaAccountDetails::ACCOUNT_TYPE_ESSENTIAL = 13
 * - MegaAccountDetails::ACCOUNT_TYPE_BUSINESS = 100
 * - MegaAccountDetails::ACCOUNT_TYPE_PRO_FLEXI = 101
 *
 * Valid value for feature plans is:
 * - MegaAccountDetails::ACCOUNT_TYPE_FEATURE = 99999
 */
- (int) getAccountLevel;

/**
 * @brief Get the features granted by this plan
 *
 * You take the ownership of the returned value
 *
 * @return Features granted by this plan.
 */
//virtual MegaStringList* getFeatures() const = 0;

/**
 * @brief Get the expiration time for the plan
 *
 * @return The time the plan expires
 */
- (int64_t) getExpirationTime;

/**
 * @brief Get the features granted by this plan
 *
 * You take the ownership of the returned value
 *
 * @return Features granted by this plan.
 */
- (MEGAStringList *) getFeatures;

/**
 * @brief The type of plan. Why it was granted.
 *
 * Not available for Bussiness/Pro Flexi.
 *
 * @return Plan type
 */
- (int32_t) getType;

/**
 * @brief Get the relating subscription ID
 *
 * Only available if the plan relates to a subscription.
 *
 * You take the ownership of the returned value
 *
 * @return ID of this subscription
 */
- (char*) getId;

@end
