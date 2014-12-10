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

typedef NS_ENUM (NSInteger, MEGAUserVisibility) {
    MEGAUserVisibilityUnknown = -1,
    MEGAUserVisibilityHidden = 0,
    MEGAUserVisibilityVisible,
    MEGAUserVisibilityMe
};

/**
 * @brief Represents an user in MEGA.
 *
 * It allows to get all data related to an user in MEGA. It can be also used
 * to start SDK requests ([MEGASdk shareNodeWithUser:level:] [MEGASdk removeContactUser:], etc.).
 *
 * Objects of this class aren't live, they are snapshots of the state of an user
 * in MEGA when the object is created, they are immutable.
 *
 * Do not inherit from this class. You can get the contacts of an account using
 * [MEGASdk contacts] and [MEGASdk contactForEmail:].
 *
 */
@interface MEGAUser : NSObject

/**
 * @brief The email associated with the contact.
 *
 * The email can be used to recover the MEGAUser object later using [MEGASdk contactForEmail:]
 *
 */
@property (readonly, nonatomic) NSString *email;

/**
 * @brief The current visibility of the contact.
 *
 * The returned value will be one of these:
 *
 * - MEGAUserVisibilityUnknown = -1
 * The visibility of the contact isn't know
 *
 * - MEGAUserVisibilityHidden = 0
 * The contact is currently hidden
 *
 * - MEGAUserVisibilityVisible = 1
 * The contact is currently visible
 *
 * - MEGAUserVisibilityMe = 2
 * The contact is the owner of the account being used by the SDK
 *
 */
@property (readonly, nonatomic) MEGAUserVisibility access;

/**
 * @brief The timestamp when the contact was added to the contact list (in seconds since the epoch).
 */
@property (readonly, nonatomic) NSDate *timestamp;

/**
 * @brief Creates a copy of this MEGAUser object.
 *
 * The resulting object is fully independent of the source MEGAUser,
 * it contains a copy of all internal attributes, so it will be valid after
 * the original object is deleted.
 *
 * You are the owner of the returned object.
 *
 * @return Copy of the MEGAUser object.
 */
- (instancetype)clone;

@end
