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
#import "MEGAContactRequestList.h"

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
 * @param api MEGASdk object that started the transfer.
 * @param transfer Information about the transfer.
 *
 * @see [MEGATransfer transferredBytes], [MEGATransfer speed].
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
