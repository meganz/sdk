/**
 * @file MEGANotificationList.h
 * @brief List of MEGANotification objects
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

NS_ASSUME_NONNULL_BEGIN

@class MEGANotification;

/**
 * @brief List of MEGANotification objects
 *
 * A MEGANotificationList has the ownership of the MEGANotification objects that it contains, so they will be
 * only valid until the MEGANotificationList is deleted.
 *
 * Objects of this class are immutable.
 */
@interface MEGANotificationList : NSObject

/**
 * @brief Returns the number of MEGANotification objects in the list
 */
@property (readonly, nonatomic) NSInteger size;

/**
 * @brief Returns the MEGANotification at position index in the list
 *
 * The MEGANotificationList retains the ownership of the returned MEGANotification. It will be only valid until
 * the MEGANotificationList is deleted.
 *
 * If the index is >= the size of the list, this function returns NULL.
 *
 * @param index Position of the MEGANotification that we want to get from the list
 * @return MEGANotification at position index in the list
 */
- (nullable MEGANotification *)notificationAtIndex:(NSInteger)index;

NS_ASSUME_NONNULL_END

@end
