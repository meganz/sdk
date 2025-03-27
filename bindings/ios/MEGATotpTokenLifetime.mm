/**
 * @file MEGATotpTokenLifetime.mm
 * @brief Represents a TOTP token and its lifetime
 *
 * (c) 2025- by Mega Limited, Auckland, New Zealand
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

#import "MEGATotpTokenLifetime.h"

@implementation MEGATotpTokenLifetime

- (instancetype)initWithToken:(NSString *)token remainingLifeTimeSeconds:(NSUInteger)remainingLifeTimeSeconds {
    self = [super init];
    if (self) {
        _token = token;
        _remainingLifeTimeSeconds = remainingLifeTimeSeconds;
    }
    return self;
}

@end
