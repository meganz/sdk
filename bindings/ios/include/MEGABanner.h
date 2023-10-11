/**
 * @file MEGABanner.h
 * @brief Represents an user banner in MEGA
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

NS_ASSUME_NONNULL_BEGIN

@interface MEGABanner : NSObject

/**
* @brief Returns the id of the MEGABanner
*
* @return Id of this banner
*/
@property (nonatomic, readonly) NSUInteger identifier;

/**
* @brief Returns the title associated with the MEGABanner object
*
* @return Title associated with the MEGABanner object
*/
@property (nonatomic, readonly, nullable) NSString *title;

/**
* @brief Returns the description associated with the MEGABanner object
*
* @return Description associated with the MEGABanner object
*/
@property (nonatomic, readonly, nullable) NSString *description;

/**
* @brief Returns the filename of the image used by the MegaBanner object
*
* @return Filename of the image used by the MegaBanner object
*/
@property (nonatomic, readonly, nullable) NSString *imageFilename;

/**
* @brief Returns the filename of the background image used by the MEGABanner object
*
* @return Filename of the background image used by the MEGABanner object
*/
@property (nonatomic, readonly, nullable) NSString *backgroundImageFilename;

/**
* @brief Returns the URL where images are located
*
* @return URL where images are located
*/
@property (nonatomic, readonly, nullable) NSString *imageLocationURLString;

/**
* @brief Returns the URL associated with the MEGABanner object
*
* @return URL associated with the MEGABanner object
*/
@property (nonatomic, readonly, nullable) NSString *URLString;


@end

NS_ASSUME_NONNULL_END
