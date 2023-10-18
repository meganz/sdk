/**
 * @file MEGAHandleList.h
 * @brief List of MegaHandle
 *
 * (c) 2013-2017 by Mega Limited, Auckland, New Zealand
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
 * @brief List of MegaHandle objects
 *
 */
@interface MEGAHandleList : NSObject

/**
 * @brief The number of handles in the list
 */
@property (readonly, nonatomic) NSUInteger size;

/**
 * @brief Add new handle to handleList
 * @param handle to be added.
 */
- (void)addMegaHandle:(uint64_t)handle;

/**
 * @brief Returns the MegaHandle at the position index in the MEGAHandleList
 *
 *
 * If the index is >= the size of the list, this function returns INVALID_HANDLE.
 *
 * @param index Position of the MegaHandle that we want to get for the list
 * @return handle at the position iindex in the list
 */
- (uint64_t)megaHandleAtIndex:(NSUInteger)index;

NS_ASSUME_NONNULL_END

@end
