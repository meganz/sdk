/**
 * @file MEGABackupInfoList.h
 * @brief List of MEGABackupInfo objects
 *
 * (c) 2023 by Mega Limited, Auckland, New Zealand
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
#import "MEGABackupInfo.h"

NS_ASSUME_NONNULL_BEGIN

/**
 * @brief List of MEGABackupInfo objects.
 *
 */
@interface MEGABackupInfoList : NSObject

/**
 * @brief The number of MEGABackupInfo objects in the list.
 */
@property (readonly, nonatomic) NSUInteger size;

/**
 * @brief The MEGABackupInfo at the position index in the MEGABackupInfoList.
 *
 * If the index is >= the size of the list, this function returns nil.
 *
 * @param index Position of the MEGABackupInfo that we want to get for the list.
 * @return MEGABackupInfo at the position index in the list.
 */
- (nullable MEGABackupInfo *)backupInfoAtIndex:(NSInteger)index;

@end

NS_ASSUME_NONNULL_END
