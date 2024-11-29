/**
 * @file MEGANotification.h
 * @brief Represents a notification in MEGA
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

/**
 * @brief Container class to store all information of a notification.
 *
 *  - ID.
 *  - Title.
 *  - Description.
 *  - Image name for the notification.
 *  - Default static path for the notification image.
 *  - Timestamp of when the notification became available to the user.
 *  - Timestamp of when the notification will expire.
 *  - Whether it should show a banner or only render a notification.
 *  - Metadata for the first call-to-action ("link" and "text" attributes).
 *  - Metadata for the second call-to-action ("link" and "text" attributes).
 *
 * Objects of this class are immutable.
 */
@interface MEGANotification : NSObject

/**
 * @brief Get the ID associated with this notification.
 *
 * @return the ID associated with this notification.
 */
@property (nonatomic, readonly) NSUInteger identifier;

/**
 * @brief Get the title of this notification.
 *
 * @return the title of this notification.
 */
@property (nonatomic, readonly, nullable) NSString *title;

/**
 * @brief Get the description for this notification.
 *
 * @return the description for this notification.
 */
@property (nonatomic, readonly, nullable) NSString *description;

/**
 * @brief Get the image name for this notification.
 *
 * @return the image name for this notification.
 */
@property (nonatomic, readonly, nullable) NSString *imageName;

/**
 * @brief Get the default static path of the image associated with this notification.
 *
 * @return the default static path of the image associated with this notification.
 */
@property (nonatomic, readonly, nullable) NSString *imagePath;

/**
 * @brief Get the name of the icon for this notification.
 *
 * @return the name of the icon for this notification.
 */
@property (nonatomic, readonly, nullable) NSString *iconName;

/**
 * @brief Get the date of when the notification became available to the user.
 *
 * @return the date of when the notification became available to the user.
 */
@property (nonatomic, readonly, nullable) NSDate *startDate;

/**
 * @brief Get the date of when the notification will expire.
 *
 * @return the date of when the notification will expire.
 */
@property (nonatomic, readonly, nullable) NSDate *endDate;

/**
 * @brief Report whether it should show a banner or only render a notification.
 *
 * @return whether it should show a banner or only render a notification.
 */
@property (nonatomic, readonly, getter=shouldShowBanner) BOOL showBanner;

/**
 * @brief Get metadata for the first call to action, represented by attributes "link" and "text",
 * and their corresponding values.
 *
 * @return metadata for the first call to action.
 */
@property (nonatomic, readonly, nullable) NSDictionary<NSString *, NSString *> *firstCallToAction;

/**
 * @brief Get metadata for the second call-to-action, represented by attributes "link" and "text",
 * and their corresponding values.
 *
 * @return metadata for the second call-to-action.
 */
@property (nonatomic, readonly, nullable) NSDictionary<NSString *, NSString *> *secondCallToAction;

NS_ASSUME_NONNULL_END

@end
