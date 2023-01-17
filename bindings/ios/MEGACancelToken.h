/**
 * @file MEGACancelToken.h
 * @brief Cancel MEGASdk methods.
 *
 * (c) 2019- by Mega Limited, Auckland, New Zealand
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

@interface MEGACancelToken : NSObject

/**
 * @brief The state of the flag
 */
@property (nonatomic, readonly, getter=isCancelled) BOOL cancelled;

/**
 * @brief Allows to set the value of the flag
 */
- (void)cancel;

@end

NS_ASSUME_NONNULL_END
