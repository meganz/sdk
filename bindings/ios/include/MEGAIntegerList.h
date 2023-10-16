/**
 * @file MEGAIntegerList.h
 * @brief List of integers
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

/**
 * @brief List of integers
 *
 * Objects of this class are immutable.
 */
@interface MEGAIntegerList : NSObject

/**
 * @brief The number of integers in the list
 */
@property (nonatomic, readonly) NSInteger size;

/**
 * @brief Returns the integer at the position index in the MEGAIntegerList
 *
 * If the index is >= the size of the list, this function returns -1.
 *
 * @param index Position of the integer that we want to get for the list
 * @return Integer at the position index in the list
 */
- (int64_t)integerAtIndex:(NSInteger)index;

@end
