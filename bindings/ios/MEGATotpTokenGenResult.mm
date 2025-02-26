/**
 * @file MEGATotpTokenGenResult.h
 * @brief Object Data for TOTP token generation result
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

#import "MEGATotpTokenGenResult.h"

@implementation MEGATotpTokenGenResult

- (instancetype)initWithResult:(int)result tokenLifetime:(MEGATotpTokenLifetime *)tokenLifetime {
    self = [super init];
    if (self) {
        _result = result;
        _tokenLifetime = tokenLifetime;
    }
    return self;
}

@end
