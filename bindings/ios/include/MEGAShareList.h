/**
 * @file MEGAShareList.h
 * @brief List of MEGAShare objects
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
#import <Foundation/Foundation.h>
#import "MEGAShare.h"

NS_ASSUME_NONNULL_BEGIN

/**
 * @brief List of MEGAShare objects.
 *
 * Objects of this class are immutable.
 *
 * @see [MEGASdk outSharesForNode:]
 */
@interface MEGAShareList : NSObject

/**
 * @brief Number of MEGAShare objects in the list.
 */
@property (readonly, nonatomic) NSInteger size;

/**
 * @brief Returns the MEGAShare at the position index in the MEGAShareList.
 *
 * If the index is >= the size of the list, this function returns nil.
 *
 * @param index Position of the MEGAShare that we want to get for the list.
 * @return MEGAShare at the position index in the list.
 */
- (nullable MEGAShare *)shareAtIndex:(NSInteger)index;

NS_ASSUME_NONNULL_END

@end
