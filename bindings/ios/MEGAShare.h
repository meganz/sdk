//
//  MEGAShare.h
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>

typedef NS_ENUM (NSInteger, MEGAShareType) {
    MEGAShareTypeAccessUnkown = -1,
    MEGAShareTypeAccessRead = 0,
    MEGAShareTypeAccessReadWrite,
    MEGAShareTypeAccessFull,
    MEGAShareTypeAccessOwner
};

/**
 * @brief Represents the outbound sharing of a folder with an user in MEGA
 *
 * It allows to get all data related to the sharing. You can start sharing a folder with
 * a contact or cancel an existing sharing using [MEGASdk shareNodeWithEmail:level:]. A public link of a folder
 * is also considered a sharing and can be cancelled.
 *
 * Objects of this class aren't live, they are snapshots of the state of the sharing
 * in MEGA when the object is created, they are immutable.
 *
 * Do not inherit from this class. You can get current active sharings using [MEGASdk outSharesWithNode:]
 *
 */
@interface MEGAShare : NSObject

/**
 * @brief The email of the user with whom we are sharing the folder
 *
 * For public shared folders, this function return nil
 *
 * @return The email of the user with whom we share the folder, or nil if it's a public folder
 */
@property (readonly, nonatomic) NSString *user;

/**
 * @brief The handle of the folder that is being shared
 * @return The handle of the folder that is being shared
 */
@property (readonly, nonatomic) uint64_t nodeHandle;

/**
 * @brief The access level of the sharing
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
 * @return The access level of the sharing
 */
@property (readonly, nonatomic) MEGAShareType accessType;

/**
 * @brief The timestamp when the sharing was created (in seconds since the epoch)
 * @return The timestamp when the sharing was created (in seconds since the epoch)
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
