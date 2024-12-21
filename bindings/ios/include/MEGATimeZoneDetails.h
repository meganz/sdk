/**
 * @file MEGATimeZoneDetails.h
 * @brief Time zone details
 *
 * (c) 2018 - by Mega Limited, Auckland, New Zealand
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
 * @brief Provides information about timezones
 *
 * This object is related to results of the function [MEGASdk fetchTimeZone]
 *
 * Objects of this class aren't live, they are snapshots of the state of the contents of the
 * folder when the object is created, they are immutable.
 *
 */

@interface MEGATimeZoneDetails : NSObject

/**
 * @brief The number of timezones in this object
 *
 */
@property (readonly, nonatomic) NSInteger numTimeZones;

/**
 * @brief The default time zone index
 *
 * If there isn't any good default known, this function will return -1
 */
@property (readonly, nonatomic) NSInteger defaultTimeZone;

/**
 * @brief Returns the timezone at an index
 *
 * @param index Index in the list (it must be lower than [MEGATimeZoneDetails numTimeZones])
 * @return Timezone at an index
 */
- (nullable NSString *)timeZoneAtIndex:(NSInteger)index;

/**
 * @brief Returns the current time offset of the time zone at an index, respect to UTC (in seconds, it can be negative)
 *
 * @param index Index in the list (it must be lower than [MEGATimeZoneDetails numTimeZones])
 * @return Current time offset of the time zone at an index, respect to UTC (in seconds, it can be negative)
 * @see [MEGATimeZoneDetails timeZoneAtIndex:]
 */
- (NSInteger)timeOffsetAtIndex:(NSInteger)index;

NS_ASSUME_NONNULL_END

@end
