/**
 * @file MEGATotpTokenLifetime.h
 * @brief Represents a TOTP token and its lifetime
 *
 * (c) 2023- by Mega Limited, Auckland, New Zealand
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
 * @brief Represents a TOTP token  and its lifetime.
 */
@interface MEGATotpTokenLifetime : NSObject

@property (nonatomic, strong) NSString *token;
@property (nonatomic, assign) NSUInteger lifetime;

- (instancetype)initWithToken:(NSString *)token lifetime:(NSUInteger)lifetime;

@end

NS_ASSUME_NONNULL_END

