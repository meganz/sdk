/**
 * @file MEGAShare.h
 * @brief Represents the outbound sharing of a folder with an user in MEGA
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

typedef NS_ENUM (NSInteger, MEGAShareType) {
    MEGAShareTypeAccessUnkown = -1,
    MEGAShareTypeAccessRead = 0,
    MEGAShareTypeAccessReadWrite,
    MEGAShareTypeAccessFull,
    MEGAShareTypeAccessOwner
};

/**
 * @brief Represents the outbound sharing of a folder with an user in MEGA.
 *
 * It allows to get all data related to the sharing. You can start sharing a folder with
 * a contact or cancel an existing sharing using [MEGASdk shareNodeWithUser:level:]. A public link of a folder
 * is also considered a sharing and can be cancelled.
 *
 * Objects of this class aren't live, they are snapshots of the state of the sharing
 * in MEGA when the object is created, they are immutable.
 *
 * Do not inherit from this class. You can get current active sharings using [MEGASdk outSharesForNode:]
 *
 */
@interface MEGAShare : NSObject

/**
 * @brief The email of the user with whom we are sharing the folder.
 *
 * For public shared folders, this property is nil.
 *
 */
@property (readonly, nonatomic) NSString *user;

/**
 * @brief The handle of the folder that is being shared.
 */
@property (readonly, nonatomic) uint64_t nodeHandle;

/**
 * @brief The access level of the sharing.
 *
 * Possible return values are:
 * - MEGAShareTypeAccessUnkown = -1
 * It means that the access level is unknown
 *
 * - MEGAShareTypeAccessRead = 0
 * The user can read the folder only
 *
 * - MEGAShareTypeAccessReadWrite = 1
 * The user can read and write the folder
 *
 * - MEGAShareTypeAccessFull = 2
 * The user has full permissions over the folder
 *
 * - MEGAShareTypeAccessOwner = 3
 * The user is the owner of the folder
 *
 */
@property (readonly, nonatomic) MEGAShareType access;

/**
 * @brief The timestamp when the sharing was created (in seconds since the epoch)
 */
@property (readonly, nonatomic) NSDate *timestamp;

/**
 * @brief Creates a copy of this MEGAShare object.
 *
 * The resulting object is fully independent of the source MEGAShare,
 * it contains a copy of all internal attributes, so it will be valid after
 * the original object is deleted.
 *
 * You are the owner of the returned object
 *
 * @return Copy of the MEGAShare object
 */
- (instancetype)clone;

@end
