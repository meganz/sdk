/**
 * @file MEGAUserAlertList.h
 * @brief List of MEGAUserAlert objects
 *
 * (c) 2018-Present by Mega Limited, Auckland, New Zealand
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

@class MEGAUserAlert;

/**
 * @brief List of MEGAUserAlert objects
 *
 * A MEGAUserAlertList has the ownership of the MEGAUserAlert objects that it contains, so they will be
 * only valid until the MEGAUserAlertList is deleted. If you want to retain a MEGAUserAlert returned by
 * a MEGAUserAlertList, use [MEGAUserAlert clone].
 *
 * Objects of this class are immutable.
 *
 * @see [MEGASdk userAlertList]
 *
 */
@interface MEGAUserAlertList : NSObject

/**
 * @brief Returns the number of MEGAUserAlert objects in the list
 */
@property (readonly, nonatomic) NSInteger size;

/**
 * @brief Creates a copy of this MEGAUserAlertList object.
 *
 * The resulting object is fully independent of the source MEGAUserAlertList,
 * it contains a copy of all internal attributes, so it will be valid after
 * the original object is deleted.
 *
 * You are the owner of the returned object.
 *
 * @return Copy of the MEGAUserAlertList object.
 */
- (instancetype)clone;

/**
 * @brief Returns the MEGAUserAlert at the position index in the MEGAUserAlertList
 *
 * The MEGAUserAlertList retains the ownership of the returned MEGAUserAlert. It will be only valid until
 * the MEGAUserAlertList is deleted.
 *
 * If the index is >= the size of the list, this function returns nil.
 *
 * @param index Position of the MEGAUserAlert that we want to get for the list
 * @return MEGAUserAlert at the position index in the list
 */
- (MEGAUserAlert *)usertAlertAtIndex:(NSInteger)index;

@end
