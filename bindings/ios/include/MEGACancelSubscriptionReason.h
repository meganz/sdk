/**
 * @file MEGACancelSubscriptionReason.h
 * @brief Represents a reason chosen by a user when canceling a subscription
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

NS_ASSUME_NONNULL_BEGIN

@interface MEGACancelSubscriptionReason : NSObject

/**
 * @brief Create new instance of reason
 *
 * @param text The actual text of the reason.
 * @param position The rendered position of the selected reason (for example "1", "1.a", "2" etc.)
 *
 * @return New instance of a reason
 */
+ (MEGACancelSubscriptionReason *)create:(NSString *)text position:(NSString *)position;

/**
 * @brief Returns the text of the reason
 *
 * @return The actual text of the reason
 */
@property (nonatomic, readonly, nullable) NSString *text;

/**
 * @brief Returns the rendered position of the selected reason
 *
 * @return The rendered position of the selected reason (for example "1", "1.a", "2" etc.)
 */
@property (nonatomic, readonly, nullable) NSString *position;

@end

NS_ASSUME_NONNULL_END
