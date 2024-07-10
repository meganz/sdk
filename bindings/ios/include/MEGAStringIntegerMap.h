/**
 * @file MEGAStringIntegerMap.h
 * @brief Map of integer values with string keys (map<string, int64_t>)
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
#import "MEGAStringList.h"

NS_ASSUME_NONNULL_BEGIN

/**
 * @brief Map of integer values with string keys.
 */
@interface MEGAStringIntegerMap : NSObject

/**
 * @brief The number of (string, int64_t) pairs in the map.
 */
@property (nonatomic, readonly) NSInteger size;

/**
 * @brief Returns the list of keys in the MEGAStringIntegerMap.
 *
 * @return A MEGAStringList containing the keys present in the MEGAStringIntegerMap.
 */
- (MEGAStringList *)keys;

/**
 * @brief Returns the integer value for the provided key.
 *
 * @param key The key of the element that you want to get from the map.
 * @return The value for the provided key.
 */
- (int64_t)integerValueForKey:(NSString *)key;

/**
 * @brief Returns a dictionary containing all (string, int64_t) pairs in the map.
 *
 * @return An NSDictionary containing the keys and their corresponding integer values.
 */
- (NSDictionary<NSString *, NSNumber *> *)toDictionary;

NS_ASSUME_NONNULL_END

@end
