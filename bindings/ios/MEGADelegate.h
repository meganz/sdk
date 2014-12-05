#import <Foundation/Foundation.h>
#import "MEGATransfer.h"
#import "MEGARequest.h"
#import "MEGAError.h"
#import "MEGANodeList.h"
#import "MEGAUserList.h"

@class MEGASdk;

/**
 * @brief Protocol to get all information related to a MEGA account.
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
 * @param request Information about the transfer.
 * @param error Error information.
 */
- (void)onTransferTemporaryError:(MEGASdk *)api transfer:(MEGATransfer *)transfer error:(MEGAError *)error;

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
 * @brief This function is called when an inconsistency is detected in the local cache.
 *
 * You should call [MEGASdk fetchNodes] when this callback is received.
 *
 * @param api MEGASdk object connected to the account.
 */
- (void)onReloadNeeded:(MEGASdk *)api;

@end
