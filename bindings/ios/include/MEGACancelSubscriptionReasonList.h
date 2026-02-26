/**
 * @file MEGACancelSubscriptionReasonList.h
 * @brief Represents a reason chosen from a multiple-choice by a user when canceling a subscription
 *
 * (c) 2024- by Mega Limited, Auckland, New Zealand
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
#import "MEGACancelSubscriptionReason.h"

NS_ASSUME_NONNULL_BEGIN

/**
 * @brief List of MEGACancelSubscriptionReason objects
 *
 */
@interface MEGACancelSubscriptionReasonList : NSObject

/**
 * @brief Create new instance of reason list
 *
 * @return New instance of a reason list
 */
+ (MEGACancelSubscriptionReasonList *)create;

/**
 * @brief The number of MEGACancelSubscriptionReason objects in the list
 */
@property (readonly, nonatomic) NSInteger size;

/**
 * @brief Add new reason to the list
 * @param reason to be added
 */
- (void)addReason:(MEGACancelSubscriptionReason *)reason;

/**
 * @brief Returns the MEGACancelSubscriptionReason at the position index in the MEGACancelSubscriptionReasonList.
 * @param index The position index of the MEGACancelSubscriptionReason to retrieve.
 * @return The MEGACancelSubscriptionReason at the given index or NULL if index was out of range.
 */
- (nullable MEGACancelSubscriptionReason *)reasonAtIndex:(NSInteger)index;

@end

NS_ASSUME_NONNULL_END
