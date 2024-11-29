/**
 * @file MEGAContactRequest.h
 * @brief Represents a contact request with an user in MEGA
 *
 * (c) 2013-2015 by Mega Limited, Auckland, New Zealand
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

typedef NS_ENUM(NSUInteger, MEGAContactRequestStatus) {
    MEGAContactRequestStatusUnresolved = 0,
    MEGAContactRequestStatusAccepted,
    MEGAContactRequestStatusDenied,
    MEGAContactRequestStatusIgnored,
    MEGAContactRequestStatusDeleted,
    MEGAContactRequestStatusReminded
};

typedef NS_ENUM(NSUInteger, MEGAReplyAction) {
    MEGAReplyActionAccept = 0,
    MEGAReplyActionDeny,
    MEGAReplyActionIgnore
};

typedef NS_ENUM(NSUInteger, MEGAInviteAction) {
    MEGAInviteActionAdd = 0,
    MEGAInviteActionDelete,
    MEGAInviteActionRemind
};

/**
 * @brief Provides information about a contact request
 *
 * Developers can use delegates (MEGADelegate, MEGAGlobalDelegate)
 * to track the progress of each contact. MEGAContactRequest objects are provided in callbacks sent
 * to these delegates and allow developers to know the state of the contact requests, their parameters
 * and their results.
 *
 * Objects of this class aren't live, they are snapshots of the state of the contact request
 * when the object is created, they are immutable.
 *
 */

NS_ASSUME_NONNULL_BEGIN

@interface MEGAContactRequest : NSObject

/**
 * @brief The handle of this MEGAContactRequest object
 */
@property (readonly, nonatomic) uint64_t handle;

/**
 * @brief The email of the request creator
 */
@property (nullable, readonly, nonatomic) NSString *sourceEmail;

/**
 * @brief The message that the creator of the contact request has added
 */
@property (nullable, readonly, nonatomic) NSString *sourceMessage;

/**
 * @brief The email of the recipient or nil if the current account is the recipient
 */
@property (nullable, readonly, nonatomic) NSString *targetEmail;

/**
 * @brief The creation time of the contact request (in seconds since the Epoch)
 */
@property (nullable, readonly, nonatomic) NSDate *creationTime;

/**
 * @brief The last update time of the contact request (in seconds since the Epoch)
 */
@property (nullable, readonly, nonatomic) NSDate *modificationTime;

/**
 * @brief The status of the contact request
 *
 * It can be one of the following values:
 * - MEGAContactRequestStatusUnresolved = 0
 * The request is pending
 *
 * - MEGAContactRequestStatusAccepted   = 1
 * The request has been accepted
 *
 * - MEGAContactRequestStatusDenied     = 2
 * The request has been denied
 *
 * - MEGAContactRequestStatusIgnored    = 3
 * The request has been ignored
 *
 * - MEGAContactRequestDeleted          = 4
 * The request has been deleted
 *
 * - MEGAContactRequestReminded         = 5
 * The request has been reminded
 *
 * @return Status of the contact request
 */
@property (readonly, nonatomic) MEGAContactRequestStatus status;

/**
 * @brief Direction of the request
 * @return YES if the request is outgoing and NO if it's incoming
 */
- (BOOL)isOutgoing;

@end

NS_ASSUME_NONNULL_END
