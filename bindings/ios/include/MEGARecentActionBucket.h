/**
 * @file MEGARecentActionBucket.h
 * @brief Represents a set of files uploaded or updated in MEGA.
 *
 * (c) 2019 - Present by Mega Limited, Auckland, New Zealand
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

@class MEGANodeList;

/**
 * @brief Represents a set of files uploaded or updated in MEGA.
 * These are used to display the recent changes to an account.
 *
 * Objects of this class aren't live, they are snapshots of the state
 * in MEGA when the object is created, they are immutable.
 *
 * MEGARecentActionBucket objects can be retrieved with -[MEGASdk recentActionsSinceDate:maxNodes:]
 *
 */
@interface MEGARecentActionBucket : NSObject

/**
 * @brief Returns a timestamp reflecting when these changes occurred
 *
 * @return Timestamp indicating when the changes occurred
 */
@property (readonly, nonatomic, nullable) NSDate *timestamp;

/**
 * @brief Returns the email of the user who made the changes
 *
 * @return the associated user's email
 */
@property (readonly, nonatomic, nullable) NSString *userEmail;

/**
 * @brief Returns the handle of the parent folder these changes occurred in
 *
 * @return the handle of the parent folder for these changes.
 */
@property (readonly, nonatomic) uint64_t parentHandle;

/**
 * @brief Returns whether the changes are updated files, or new files
 *
 * @return YES if the changes are updates rather than newly uploaded files.
 */
@property (readonly, nonatomic, getter=isUpdate) BOOL update;

/**
 * @brief Returns whether the files are photos or videos
 *
 * @return YES if the files in this change are media files.
 */
@property (readonly, nonatomic, getter=isMedia) BOOL media;

/**
 * @brief Returns nodes representing the files changed in this bucket
 *
 * @return A list of the files in the bucket. The bucket retains ownership.
 */
@property (readonly, nonatomic, nullable) MEGANodeList *nodesList;

@end

NS_ASSUME_NONNULL_END
