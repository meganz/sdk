/**
 * @file MEGADelegate.h
 * @brief Delegate to get all events related to a MEGA account
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
#import "MEGATransfer.h"
#import "MEGARequest.h"
#import "MEGAError.h"
#import "MEGANodeList.h"
#import "MEGAUserList.h"
#import "MEGAUserAlertList.h"
#import "MEGAContactRequestList.h"
#import "MEGAEvent.h"

NS_ASSUME_NONNULL_BEGIN

@class MEGASdk;

/**
 * @brief Protocol to get all events related to a MEGA account.
 *
 * Implementations of this protocol can receive all events (request, transfer, global).
 * The SDK will provide a new interface to get synchronization events separately in future updates.
 *
 */
@protocol MEGADelegate <NSObject>

@optional

/**
 * @brief This function is called when a request is about to start being processed.
 *
 * @param api MEGASdk object that started the request.
 * @param request Information about the request.
 */
- (void)onRequestStart:(MEGASdk *)api request:(MEGARequest *)request;

/**
 * @brief This function is called when a request has finished.
 *
 * There won't be more callbacks about this request.
 * The last parameter provides the result of the request. If the request finished without problems,
 * the error code will be MEGAErrorTypeApiOk.
 *
 * @param api MEGASdk object that started the request.
 * @param request Information about the request.
 * @param error Error information.
 */
- (void)onRequestFinish:(MEGASdk *)api request:(MEGARequest *)request error:(MEGAError *)error;

/**
 * @brief This function is called to inform about the progress of a request.
 *
 * Currently, this callback is only used for fetchNodes (MEGARequestTypeFetchNodes) requests.
 *
 * @param api MEGASdk object that started the request.
 * @param request Information about the request.
 * @see [MEGARequest totalBytes] [MEGARequest transferredBytes].
 */
- (void)onRequestUpdate:(MEGASdk *)api request:(MEGARequest *)request;

/**
 * @brief This function is called when there is a temporary error processing a request.
 *
 * The request continues after this callback, so expect more [MEGARequestDelegate onRequestTemporaryError:request:error:] or
 * a [MEGARequestDelegate onRequestFinish:request:error:] callback.
 *
 * @param api MEGASdk object that started the request.
 * @param request Information about the request.
 * @param error Error information.
 */
- (void)onRequestTemporaryError:(MEGASdk *)api request:(MEGARequest *)request error:(MEGAError *)error;

/**
 * @brief This function is called when a transfer is about to start being processed.
 *
 * @param api MEGASdk object that started the transfer.
 * @param transfer Information about the transfer.
 */
- (void)onTransferStart:(MEGASdk *)api transfer:(MEGATransfer *)transfer;

/**
 * @brief This function is called when a transfer has finished.
 *
 * There won't be more callbacks about this transfer.
 * The last parameter provides the result of the transfer. If the transfer finished without problems,
 * the error code will be MEGAErrorTypeApiOk.
 *
 * @param api MEGASdk object that started the transfer.
 * @param transfer Information about the transfer.
 * @param error Error information.
 */
- (void)onTransferFinish:(MEGASdk *)api transfer:(MEGATransfer *)transfer error:(MEGAError *)error;

/**
 * @brief This function is called to inform about the progress of a transfer.
 *
 * In case this transfer represents a recursive operation (folder upload/download) SDK will
 * notify apps about the stages transition.
 *
 * Current recursive operation stage can be retrieved with method MegaTransfer::getStage.
 * This method returns the following values:
 *  - MEGATransferStageScan                      = 1
 *  - MEGATransferStageCreateTreee               = 2
 *  - MEGATransferStageTransferringFiles         = 3
 * For more information about stages refer to [MEGATransfer stage]
 *
 * @param api MEGASdk object that started the transfer.
 * @param transfer Information about the transfer.
 *
 * @see [MEGATransfer transferredBytes], [MEGATransfer speed], [MEGATransfer stage].
 */
- (void)onTransferUpdate:(MEGASdk *)api transfer:(MEGATransfer *)transfer;

/**
 * @brief This function is called when there is a temporary error processing a transfer.
 *
 * The transfer continues after this callback, so expect more
 * [MEGATransferDelegateonTransferTemporaryError:transfer:error:] or
 * a [MEGATransferDelegate onTransferFinish:transfer:error:] callback.
 *
 * @param api MEGASdk object that started the transfer.
 * @param transfer Information about the transfer.
 * @param error Error information.
 */
- (void)onTransferTemporaryError:(MEGASdk *)api transfer:(MEGATransfer *)transfer error:(MEGAError *)error;

/**
 * @brief This function is called when there are new or updated contacts in the account.
 *
 * When the full account is reloaded or a large number of server notifications arrives at
 * once, the second parameter will be nil
 *
 * @param api MEGASdk object connected to the account.
 * @param userList List that contains the new or updated contacts.
 */
- (void)onUsersUpdate:(MEGASdk *)api userList:(nullable MEGAUserList *)userList;

/**
 * @brief This function is called when there are new or updated user alerts in the account
 *
 * The SDK retains the ownership of the MEGAUserAlertList in the second parameter. The list and all the
 * MEGAUserAlert objects that it contains will be valid until this function returns. If you want to save the
 * list, use [MEGAUserAlertList clone]. If you want to save only some of the MEGAUserAlert objects, use [MEGAUserAlert clone]
 * for those objects.
 *
 * When there is a problem parsing the incoming information from the server or the full
 * account is reloaded or a large number of server notifications arrives at once, the second
 * parameter will be nil.
 *
 * @param api MEGASdk object connected to the account
 * @param userAlertList List that contains the new or updated contacts
 */
- (void)onUserAlertsUpdate:(MEGASdk *)api userAlertList:(nullable MEGAUserAlertList *)userAlertList;

/**
 * @brief This function is called when there are new or updated nodes in the account.
 *
 * When the full account is reloaded or a large number of server notifications arrives at once, the
 * second parameter will be nil.
 *
 * @param api MEGASdk object connected to the account.
 * @param nodeList List that contains the new or updated nodes.
 */
- (void)onNodesUpdate:(MEGASdk *)api nodeList:(nullable MEGANodeList *)nodeList;

/**
 * @brief This function is called when a Set has been updated (created / updated / removed)
 *
 * When the full account is reloaded or a large number of server notifications arrives at
 * once, the second parameter will be nil.
 *
 * @param api MEGASdk object connected to the account
 * @param sets Array that contains the new or updated Sets
 */
- (void)onSetsUpdate:(MEGASdk *)api sets:(nullable NSArray<MEGASet *> *)sets;

/**
 * @brief This function is called when a SetElement has been updated (created / updated / removed)
 *
 * When the full account is reloaded or a large number of server notifications arrives at
 * once, the second parameter will be nil.
 *
 * @param api MEGASdk object connected to the account
 * @param setElements Array that contains the new or updated Set-Elements
 */
- (void)onSetElementsUpdate:(MEGASdk *)api setElements:(nullable NSArray<MEGASetElement *> *)setElements;

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
- (void)onContactRequestsUpdate:(MEGASdk *)api contactRequestList:(nullable MEGAContactRequestList *)contactRequestList;

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
 * You can check the type of event by calling [MEGAEvent type]
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
 * - EventDisconnect: when the SDK performs a disconnect to reset all the
 * existing open-connections, since they have become unusable. It's recommended that the app
 * receiving this event reset its connections with other servers, since the disconnect
 * performed by the SDK is due to a network change or IP addresses becoming invalid.
 *
 * - EventAccountBlocked: when the account get blocked, typically because of
 * infringement of the Mega's terms of service repeatedly. This event is followed by an automatic
 * logout.
 *
 *  Valid data in the MegaEvent object received in the callback:
 *      - [MEGAEvent text]: message to show to the user.
 *      - [MEGAEvent number]: code representing the reason for being blocked.
 *          200: suspension message for any type of suspension, but copyright suspension.
 *          300: suspension only for multiple copyright violations.
 *          400: the subuser account has been disabled.
 *          401: the subuser account has been removed.
 *          500: The account needs to be verified by an SMS code.
 *          700: the account is supended for Weak Account Protection.
 *
 * - EventStorage: when the status of the storage changes.
 *
 * For this event type, [MEGAEvent number] provides the current status of the storage
 *
 * There are four possible storage states:
 *     - StorageStateGreen = 0
 *     There are no storage problems
 *
 *     - StorageStateOrange = 1
 *     The account is almost full
 *
 *     - StorageStateRed = 2
 *     The account is full. Uploads have been stopped
 *
 *     - StorageStateChange = 3
 *     There is a possible significant change in the storage state.
 *     It's needed to call [MEGASdk getAccountDetails] to check the storage status.
 *     After calling it, this callback will be called again with the corresponding
 *     state if there is really a change.
 *
 *     - StorageStatePaywall = 4
 *     The account has been full for a long time. Now most of actions are disallowed.
 *     You will need to call [MEGASdk getUserData] before retrieving the overquota deadline/warnings
 *     timestamps.
 *
 * - EventNodesCurrent: when all external changes have been received
 *
 * - EventMediaInfoReady: when codec-mappings have been received
 *
 * - EventBusinessStatus: when the status of a business account has changed.
 *
 * For this event type, [MEGAEvent number] provides the new business status.
 *
 * The posible values are:
 *   - BusinessStatusExpired = -1
 *   - BusinessStatusInactive = 0
 *   - BusinessStatusActive = 1
 *   - BusinessStatusGracePeriod = 2
 *
 * - EventKeyModified: when the key of a user has changed.
 *
 * For this event type, [MEGAEvent handle] provides the handle of the user whose key has been modified.
 * For this event type, [MEGAEvent number] provides type of key that has been modified.
 *
 * The posible values are:
 *  - Public chat key (Cu25519)     = 0
 *  - Public signing key (Ed25519)  = 1
 *  - Public RSA key                = 2
 *  - Signature of chat key         = 3
 *  - Signature of RSA key          = 4
 *
 * - EventMiscFlagsReady: when the miscellaneous flags are available/updated.
 *
 * - EventReqStatProgress: Provides the per mil progress of a long-running API operation
 *  in [MEGAEvent number], or -1 if there isn't any operation in progress.
 *
 * - EventReloading: when the API server has forced a full reload. The app should show a
 * similar UI to the one displayed during the initial load (fetchnodes).
 *
 * @param api MEGASdk object connected to the account
 * @param event Details about the event
 */
- (void)onEvent:(MEGASdk *)api event:(MEGAEvent *)event;

@end

NS_ASSUME_NONNULL_END
