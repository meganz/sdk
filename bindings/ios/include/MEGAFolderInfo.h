/**
 * @file MEGAFolderInfo.h
 * @brief Folder info
 *
 * (c) 2018 - by Mega Limited, Auckland, New Zealand
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

/**
 * @brief Provides information about the contents of a folder
 *
 * This object is related to provide the results of the function [MEGASdk getFolderInfoWithNode:]
 *
 * Objects of this class aren't live, they are snapshots of the state of the contents of the
 * folder when the object is created, they are immutable.
 *
 */
@interface MEGAFolderInfo : NSObject

/**
 * @brief Creates a copy of this MEGAFolderInfo object.
 *
 * The resulting object is fully independent of the source MEGAFolderInfo,
 * it contains a copy of all internal attributes, so it will be valid after
 * the original object is deleted.
 *
 *
 * @return Clone of the MEGAFolderInfo object.
 */
- (instancetype)clone;

/**
 * The number of file versions inside the folder
 *
 * The current version of files is not taken into account for the return value of this function
 */
@property (readonly, nonatomic) NSInteger versions;

/**
 * The number of files inside the folder
 *
 * File versions are not counted for the return value of this function
 */
@property (readonly, nonatomic) NSInteger files;

/**
 * The number of folders inside the folder
 */
@property (readonly, nonatomic) NSInteger folders;

/**
 * The total size of files inside the folder
 *
 * File versions are not taken into account for the return value of this function
 */
@property (readonly, nonatomic) long long currentSize;

/**
 * The total size of file versions inside the folder
 *
 * The current version of files is not taken into account for the return value of this function
 */
@property (readonly, nonatomic) long long versionsSize;


@end
