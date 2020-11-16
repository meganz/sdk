/**
 * @file MEGAUser.h
 * @brief Represents an user in MEGA
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

@interface MEGABanner : NSObject

/**
* @brief Returns the id of the MegaBanner
*
* @return Id of this banner
*/
@property (nonatomic, readonly) NSUInteger identifier;

/**
* @brief Returns the title associated with the MegaBanner object
*
* The SDK retains the ownership of the returned value. It will be valid until
* the MegaBanner object is deleted.
*
* @return Title associated with the MegaBanner object
*/
@property (nonatomic, readonly) NSString *title;

/**
* @brief Returns the description associated with the MegaBanner object
*
* The SDK retains the ownership of the returned value. It will be valid until
* the MegaBanner object is deleted.
*
* @return Description associated with the MegaBanner object
*/
@property (nonatomic, readonly) NSString *description;

/**
* @brief Returns the filename of the image used by the MegaBanner object
*
* The SDK retains the ownership of the returned value. It will be valid until
* the MegaBanner object is deleted.
*
* @return Filename of the image used by the MegaBanner object
*/
@property (nonatomic, readonly) NSString *imageFilename;

/**
* @brief Returns the filename of the background image used by the MegaBanner object
*
* The SDK retains the ownership of the returned value. It will be valid until
* the MegaBanner object is deleted.
*
* @return Filename of the background image used by the MegaBanner object
*/
@property (nonatomic, readonly) NSString *backgroundImageFilename;

/**
* @brief Returns the URL where images are located
*
* The SDK retains the ownership of the returned value. It will be valid until
* the MegaBanner object is deleted.
*
* @return URL where images are located
*/
@property (nonatomic, readonly) NSString *imageLocationURLString;

/**
* @brief Returns the URL associated with the MegaBanner object
*
* The SDK retains the ownership of the returned value. It will be valid until
* the MegaBanner object is deleted.
*
* @return URL associated with the MegaBanner object
*/
@property (nonatomic, readonly) NSString *URLString;


@end
