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
 * @param userList List that contains the new or updated contacts.
 */
- (void)onUsersUpdate:(MEGASdk *)api userList:(MEGAUserList *)userList;

/**
 * @brief This function is called when there are new or updated nodes in the account.
 *
 * When the full account is reloaded or a large number of server notifications arrives at once, the
 * second parameter will be nil.
 *
 * @param api MEGASdk object connected to the account.
 * @param nodeList List that contains the new or updated nodes.
 */
- (void)onNodesUpdate:(MEGASdk *)api nodeList:(MEGANodeList *)nodeList;

/**
 * @brief This function is called when the account has been updated (confirmed/upgraded/downgraded)
 *
 * The usage of this delegate to handle the external account confirmation is deprecated.
 * Instead, you should use [MEGAGlobalDelegate onEvent:event:].
 *
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

/**
 * The details about the event, like the type of event and optionally any
 * additional parameter, is received in the \c params parameter.
 *
 * Currently, the following type of events are notified:
 *
 *  - EventCommitDB: when the SDK commits the ongoing DB transaction.
 *  This event can be used to keep synchronization between the SDK cache and the
 *  cache managed by the app thanks to the sequence number.
 *
 *  Valid data in the MegaEvent object received in the callback:
 *      - [MEGAEvent text]: sequence number recorded by the SDK when this event happened
 *
 *  - EventAccountConfirmation: when a new account is finally confirmed
 *  by the user by confirming the signup link.
 *
 *  Valid data in the MegaEvent object received in the callback:
 *      - [MEGAEvent text]: email address used to confirm the account
 *
 *  - EventChangeToHttps: when the SDK automatically starts using HTTPS for all
 *  its communications. This happens when the SDK is able to detect that MEGA servers can't be
 *  reached using HTTP or that HTTP communications are being tampered. Transfers of files and
 *  file attributes (thumbnails and previews) use HTTP by default to save CPU usage. Since all data
 *  is already end-to-end encrypted, it's only needed to use HTTPS if HTTP doesn't work. Anyway,
 *  applications can force the SDK to always use HTTPS using MegaApi::useHttpsOnly. It's recommended
 *  that applications that receive one of these events save that information on its settings and
 *  automatically enable HTTPS on next executions of the app to not force the SDK to detect the problem
 *  and automatically switch to HTTPS every time that the application starts.
 *
 * You can check the type of event by calling [MEGAEvent type]
 *
 * @param api MEGASdk object connected to the account
 * @param event Details about the event
 */
- (void)onEvent:(MEGASdk *)api event:(MEGAEvent *)event;

@end
