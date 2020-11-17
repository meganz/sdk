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
#import "MEGABanner.h"

/**
* @brief List of MEGABanner objects
*
* A MEGABannerList has the ownership of the MEGABanner objects that it contains, so they will be
* only valid until the MEGABannerList is deleted.
*
*/
@interface MEGABannerList : NSObject

/**
 * @brief The number of banners in the list
 */
@property (nonatomic, readonly) NSInteger size;

- (instancetype _Nonnull)clone;

/**
* @brief Returns the MEGABanner at position i in the MEGABannerList
*
* The MEGABannerList retains the ownership of the returned MEGABanner. It will be only valid until
* the MEGABannerList is deleted.
*
* If the index is >= the size of the list, this function returns NULL.
*
* @param i Position of the MEGABanner that we want to get for the list
* @return MEGABanner at position i in the list
*/
- (MEGABanner * _Nullable)bannerAtIndex:(NSInteger)index;

@end
