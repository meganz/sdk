/**
 * @file MEGASearchPage.h
 * @brief Store pagination options used in searches @see MegaApi::search, MegaApi::getChildren.
 *
 * 
 * (c) 2023- by Mega Limited, Auckland, New Zealand
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

@interface MEGASearchPage : NSObject

/**
 @brief Return the first position in the list of results to be included in the returned page (starts from 0)
**/
@property (readonly, nonatomic) size_t startingOffset;
/**
* @brief Return the maximum number of results included in the page, or 0 to return all (remaining) results
**/
@property (readonly, nonatomic) size_t pageSize;

-(instancetype)initWithStartingOffset:(size_t)startingOffset pageSize:(size_t)pageSize;

@end
NS_ASSUME_NONNULL_END
