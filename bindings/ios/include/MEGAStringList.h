/**
 * @file MEGAStringList.h
 * @brief List of strings
 *
 * (c) 2017- by Mega Limited, Auckland, New Zealand
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
 * @brief List of strings
 *
 * Objects of this class are immutable.
 */

@interface MEGAStringList : NSObject

/**
 * @brief The number of strings in the list
 */
@property (nonatomic, readonly) NSInteger size;

/**
 * @brief Returns the string at the position i in the MEGAStringList
 *
 * If the index is >= the size of the list, this function returns nil.
 *
 * @param index Position of the string that we want to get for the list
 * @return string at the position index in the list
 */
- (nullable NSString *)stringAtIndex:(NSInteger)index;
/**
 * @brief Returns values from MEGAStringList mapped to a NSString array
 *
 */
- (nullable NSArray<NSString *>*)toStringArray;

NS_ASSUME_NONNULL_END

@end
