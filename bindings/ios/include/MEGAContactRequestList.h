/**
 * @file MEGAContactRequestList.h
 * @brief List of MEGAContactRequest objects
 *
 * (c) 2013-2015 by Mega Limited, Auckland, New Zealand
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
#import "MEGAContactRequest.h"

/**
 * @brief List of MEGAContactRequest objects
 *
 * Objects of this class are immutable.
 *
 * @see [MEGASdk contactRequests]
 */
@interface MEGAContactRequestList : NSObject

/**
 * @brief The number of MEGAContactRequest objects in the list.
 */
@property (readonly, nonatomic) NSNumber *size;

/**
 * @brief Creates a copy of this MEGAContactRequestList object.
 *
 * The resulting object is fully independent of the source MEGAContactRequestList,
 * it contains a copy of all internal attributes, so it will be valid after
 * the original object is deleted.
 *
 * You are the owner of the returned object.
 *
 * @return Copy of the MEGAContactRequestList object.
 */
- (instancetype)clone;


/**
 * @brief Returns the MEGAContactRequest at the position index in the MEGAContactRequestList.
 *
 * If the index is >= the size of the list, this function returns nil.
 *
 * @param index Position of the MEGAContactRequest that we want to get for the list.
 * @return MEGAContactRequest at the position index in the list.
 */
- (MEGAContactRequest *)contactRequestAtIndex:(NSInteger)index;

@end
