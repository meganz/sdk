/**
 * @file MEGAAccountFeature.h
 * @brief Details about a MEGA feature
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

NS_ASSUME_NONNULL_BEGIN

/**
 * @brief Details about a MEGA feature.
 */
@interface MEGAAccountFeature : NSObject

/**
 * @brief The expiry timestamp.
 */
@property (nonatomic, readonly) int64_t expiry;

/**
 * @brief The id of this feature.
 */
@property (nonatomic, readonly, nullable) NSString *featureId;

NS_ASSUME_NONNULL_END

@end
