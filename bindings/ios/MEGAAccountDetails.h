/**
 * @file MEGAAccountDetails.h
 * @brief Details about a MEGA account
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

typedef NS_ENUM (NSInteger, MEGAAccountType) {
    MEGAAccountTypeFree = 0,
    MEGAAccountTypeProI = 1,
    MEGAAccountTypeProII = 2,
    MEGAAccountTypeProIII = 3,
    MEGAAccountTypeLite = 4
};

typedef NS_ENUM(NSInteger, MEGASubscriptionStatus) {
    MEGASubscriptionStatusNone = 0,
    MEGASubscriptionStatusValid = 1,
    MEGASubscriptionStatusInvalid = 2
};

/**
 * @brief Details about a MEGA account.
 */
@interface MEGAAccountDetails : NSObject

/**
 * @brief Used storage for the account (in bytes).
 */
@property (readonly, nonatomic) NSNumber *storageUsed;

/**
 * @brief Maximum storage for the account (in bytes).
 */
@property (readonly, nonatomic) NSNumber *storageMax;

/**
 * @brief Used bandwidth for the account (in bytes).
 */
@property (readonly, nonatomic) NSNumber *transferOwnUsed;

/**
 * @brief Maximum available bandwidth for the account (in bytes).
 */
@property (readonly, nonatomic) NSNumber *transferMax;

/**
 * @brief PRO level of the MEGA account.
 *
 * Valid values are:
 * - MEGAAccountTypeFree = 0
 * - MEGAAccountTypeProI = 1
 * - MEGAAccountTypeProII = 2
 * - MEGAAccountTypeProIII = 3
 * - MEGAAccountTypeLite = 4
 */
@property (readonly, nonatomic) MEGAAccountType type;

/**
 * @brief The expiration time for the current PRO status (in seconds since the Epoch)
 */
@property (readonly, nonatomic) NSInteger proExpiration;

/**
 * @brief Check if there is a valid subscription
 *
 * If this value is MEGASubscriptionStatusValid, the PRO account will be 
 * automatically renewed.
 * See [MEGAAccountDetails subscriptionRenewTime]
 *
 * Information about about the subscription status
 *
 * Valid values are:
 * - MEGASubscriptionStatusNone = 0
 * There isn't any active subscription
 *
 * - MEGASubscriptionStatusValid = 1
 * There is an active subscription
 *
 * - MEGASubscriptionStatusInvalid = 2
 * A subscription exists, but it uses a payment gateway that is no longer valid
 *
 */
@property (readonly, nonatomic) MEGASubscriptionStatus subscriptionStatus;

/**
 * @brief The time when the the PRO account will be renewed (in seconds since the Epoch)
 */
@property (readonly, nonatomic) NSInteger subscriptionRenewTime;

/**
 * @brief The subscryption method. For example "Credit Card".
 *
 */
@property (readonly, nonatomic) NSString *subscriptionMethod;

/**
 * @brief The subscription cycle
 *
 * This value will show if the subscription will be montly or yearly renewed.
 * Example return values: "1 M", "1 Y".
 *
 */
@property (readonly, nonatomic) NSString *subscriptionCycle;

/**
 * @brief The number of nodes with account usage info
 *
 * You can get information about each node using [MEGAAccountDetails storageUsedForHandle:],
 * [MEGAAccountDetailsn numberFilesForHandle:], [MEGAAccountDetails numberFoldersForHandle:]
 *
 * This function can return:
 * - 0 (no info about any node)
 * - 3 (info about the root node, the inbox node and the rubbish node)
 * Use [MEGASdk rootNode], [MEGASdk inboxNode] and [MEGASdk rubbishNode] to get those nodes.
 *
 * - >3 (info about root, inbox, rubbish and incoming shares)
 * Use [MEGASdk inShares] to get the incoming shares
 *
 */
@property (readonly, nonatomic) NSInteger numberUsageItems;

/**
 * @brief Creates a copy of this MEGAAccountDetails object.
 *
 * The resulting object is fully independent of the source MEGAAccountDetails,
 * it contains a copy of all internal attributes, so it will be valid after
 * the original object is deleted.
 *
 * You are the owner of the returned object.
 *
 * @return Copy of the MEGAAccountDetails object.
 */
- (instancetype)clone;

/**
 * @brief Get the used storage in for a node.
 *
 * Only root nodes are supported.
 *
 * @param handle Handle of the node to check.
 * @return Used storage (in bytes).
 * @see [MEGASdk rootNode], [MEGASdk rubbishNode], [MEGASdk inboxNode].
 */
- (NSNumber *)storageUsedForHandle:(uint64_t)handle;

/**
 * @brief Get the number of files in a node.
 *
 * Only root nodes are supported.
 *
 * @param handle Handle of the node to check.
 * @return Number of files in the node.
 * @see [MEGASdk rootNode], [MEGASdk rubbishNode], [MEGASdk inboxNode].
 */
- (NSNumber *)numberFilesForHandle:(uint64_t)handle;

/**
 * @brief Get the number of folders in a node.
 *
 * Only root nodes are supported.
 *
 * @param handle Handle of the node to check.
 * @return Number of folders in the node.
 * @see [MEGASdk rootNode], [MEGASdk rubbishNode], [MEGASdk inboxNode].
 */
- (NSNumber *)numberFoldersForHandle:(uint64_t)handle;



@end
