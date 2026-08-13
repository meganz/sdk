/**
 * @file MEGADateSection.h
 * @brief One date bucket of nodes returned by [MEGASdk groupAllNodesByDateWithFilter:...].
 *
 * (c) 2026- by Mega Limited, Auckland, New Zealand
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
 * @brief One date bucket produced by [MEGASdk groupAllNodesByDateWithFilter:...].
 *
 * Objects of this class are immutable.
 *
 * To fetch the nodes anchored at this bucket, pass startDate and endDate as the
 * timestamp anchor of a paginated listing query.
 *
 * Group ids are stable across mutations: adding or removing an item in a bucket
 * changes its count but never its groupId. Sections with zero items are not
 * returned.
 */
NS_SWIFT_SENDABLE
@interface MEGADateSection : NSObject

/**
 * @brief Display-only date string identifying this section (e.g. "2024-07").
 *
 * Format is determined by the granularity passed to
 * [MEGASdk groupAllNodesByDateWithFilter:...]. Stable across mutations within
 * the same bucket; safe to use as a UI section header / key. Display/key only;
 * not round-tripped.
 */
@property (readonly, nonatomic, nullable) NSString *groupId;

/**
 * @brief Inclusive lower bound of this bucket, as UTC epoch seconds.
 *
 * Always the canonical bucket start (midnight UTC of the day / first day of the
 * month / first day of the year) — direction-independent, not the minimum mtime
 * of nodes actually in the bucket.
 */
@property (readonly, nonatomic) int64_t startDate;

/**
 * @brief Exclusive upper bound of this bucket, as UTC epoch seconds.
 *
 * Always the start of the next bucket at the same granularity.
 * Direction-independent.
 */
@property (readonly, nonatomic) int64_t endDate;

/**
 * @brief Number of items in this section.
 *
 * Sum count across all sections for the timeline's total length (the value the
 * fast scroller uses for its track).
 * Under a capture-time order with no timestamp anchor set, listAllNodesByPage also
 * returns nodes with no capture time, which belong to no section — so the sum
 * matches the list length only when an anchor is set. int64_t (not NSInteger
 * narrowing): a large account's total can exceed INT_MAX.
 */
@property (readonly, nonatomic) int64_t count;

@end

NS_ASSUME_NONNULL_END
