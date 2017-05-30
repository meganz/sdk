/**
 * @file MEGAGlobalDelegate.h
 * @brief Delegate to get global events
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

/**
 * @brief Protocol to get information about global events.
 *
 * You can implement this interface and start receiving events calling [MEGASdk addMEGAGlobalDelegate:].
 *
 * MEGADelegate objects can also receive global events.
 */
@protocol MEGAGlobalDelegate <NSObject>

@optional

/**
 * @brief This function is called when there are new or updated contacts in the account.
 *
 * @param api MEGASdk object connected to the account.
 * @param users List that contains the new or updated contacts.
 */
- (void)onUsersUpdate:(MEGASdk *)api userList:(MEGAUserList *)userList;

/**
 * @brief This function is called when there are new or updated nodes in the account.
 *
 * When the full account is reloaded or a large number of server notifications arrives at once, the
 * second parameter will be nil.
 *
 * @param api MEGASdk object connected to the account.
 * @param nodes List that contains the new or updated nodes.
 */
- (void)onNodesUpdate:(MEGASdk *)api nodeList:(MEGANodeList *)nodeList;

/**
 * @brief This function is called when the account has been updated (upgraded/downgraded)
 * @param api MEGASdk object connected to the account
 */
- (void)onAccountUpdate:(MEGASdk *)api;

/**
 * @brief This function is called when there are new or updated contact requests in the account
 *
 * When the full account is reloaded or a large number of server notifications arrives at once, the
 * second parameter will be nil.
 *
 * @param api MEGASdk object connected to the account
 * @param contactRequestList List that contains the new or updated contact requests
 */
- (void)onContactRequestsUpdate:(MEGASdk *)api contactRequestList:(MEGAContactRequestList *)contactRequestList;

/**
 * @brief This function is called when an inconsistency is detected in the local cache.
 *
 * You should call [MEGASdk fetchNodes] when this callback is received.
 *
 * @param api MEGASdk object connected to the account.
 */
- (void)onReloadNeeded:(MEGASdk *)api;

@end
