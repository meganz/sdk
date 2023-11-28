/**
 * @file MEGASearchFilterTimeFrame.mm
 * @brief Search time frame used by MEGASearchFilter
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
#import "MEGASearchFilterTimeFrame.h"

NS_ASSUME_NONNULL_BEGIN
@implementation MEGASearchFilterTimeFrame

- (instancetype)initWithLowerLimit:(NSDate *)lowerLimit upperLimit:(NSDate *)upperLimit {
    self = [super init];
    if (self != nil) {
        _lowerLimit = [lowerLimit timeIntervalSince1970];
        _upperLimit = [upperLimit timeIntervalSince1970];
    }

    return self;
}

@end
NS_ASSUME_NONNULL_END
