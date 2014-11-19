//
//  MEGADelegate.h
//
//  Created by Javier Navarro on 07/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MEGATransfer.h"
#import "MEGARequest.h"
#import "MEGAError.h"
#import "MEGANodeList.h"
#import "MEGAUserList.h"

@class MEGASdk;

/**
 * @brief Protocol to get all information related to a MEGA account
 *
 * Implementations of this protocol can receive all events (request, transfer, global) and two
 * additional events related to the synchronization engine. The SDK will provide a new interface
 * to get synchronization events separately in future updates-
 *
 * Multiple inheritance isn't used for compatibility with other programming languages
 *
 */
@protocol MEGADelegate <NSObject>

@optional

/**
 * @brief This function is called when a request is about to start being processed
 *
 * The SDK retains the ownership of the request parameter.
 * Don't use it after this functions returns.
 *
 * The api object is the one created by the application, it will be valid until
 * the application deletes it.
 *
 * @param api MEGASdk object that started the request
 * @param request Information about the request
 */
- (void)onRequestStart:(MEGASdk *)api request:(MEGARequest *)request;

/**
 * @brief This function is called when a request has finished
 *
 * There won't be more callbacks about this request.
 * The last parameter provides the result of the request. If the request finished without problems,
 * the error code will be MEGAErrorTypeApiOk
 *
 * The SDK retains the ownership of the request and error parameters.
 * Don't use them after this functions returns.
 *
 * The api object is the one created by the application, it will be valid until
 * the application deletes it.
 *
 * @param api MEGASdk object that started the request
 * @param request Information about the request
 * @param error Error information
 */
- (void)onRequestFinish:(MEGASdk *)api request:(MEGARequest *)request error:(MEGAError *)error;

/**
 * @brief This function is called to inform about the progres of a request
 *
 * Currently, this callback is only used for fetchNodes (MEGARequestTypeFetchNodes) requests
 *
 * The SDK retains the ownership of the request parameter.
 * Don't use it after this functions returns.
 *
 * The api object is the one created by the application, it will be valid until
 * the application deletes it.
 *
 *
 * @param api MEGASdk object that started the request
 * @param request Information about the request
 * @see [MEGARequest totalBytes] [MEGARequest transferredBytes]
 */
- (void)onRequestUpdate:(MEGASdk *)api request:(MEGARequest *)request;

/**
 * @brief This function is called when there is a temporary error processing a request
 *
 * The request continues after this callback, so expect more [MEGARequestListener onRequestTemporaryError] or
 * a [MEGARequestListener onRequestFinish] callback
 *
 * The SDK retains the ownership of the request and error parameters.
 * Don't use them after this functions returns.
 *
 * The api object is the one created by the application, it will be valid until
 * the application deletes it.
 *
 * @param api MEGASdk object that started the request
 * @param request Information about the request
 * @param error Error information
 */
- (void)onRequestTemporaryError:(MEGASdk *)api request:(MEGARequest *)request error:(MEGAError *)error;

/**
 * @brief This function is called when a transfer is about to start being processed
 *
 * The SDK retains the ownership of the transfer parameter.
 * Don't use it after this functions returns.
 *
 * The api object is the one created by the application, it will be valid until
 * the application deletes it.
 *
 * @param api MEGASdk object that started the transfer
 * @param transfer Information about the transfer
 */
- (void)onTransferStart:(MEGASdk *)api transfer:(MEGATransfer *)transfer;

/**
 * @brief This function is called when a transfer has finished
 *
 * The SDK retains the ownership of the transfer and error parameters.
 * Don't use them after this functions returns.
 *
 * The api object is the one created by the application, it will be valid until
 * the application deletes it.
 *
 * There won't be more callbacks about this transfer.
 * The last parameter provides the result of the transfer. If the transfer finished without problems,
 * the error code will be MEGAErrorTypeApiOk
 *
 * Take into account that when a file is uploaded, an additional request is required to attach the uploaded
 * file to the account. That is automatically made by the SDK, but this means that the file won't be still
 * attached to the account when this callback is received. You can know when the file is finally attached
 * thanks to the [MEGAGlobalDelegate onNodesUpdate] [MEGADelegate onNodesUpdate] callbacks.
 *
 * @param api MEGASdk object that started the transfer
 * @param transfer Information about the transfer
 * @param error Error information
 */
- (void)onTransferFinish:(MEGASdk *)api transfer:(MEGATransfer *)transfer error:(MEGAError *)error;

/**
 * @brief This function is called to inform about the progress of a transfer
 *
 * The SDK retains the ownership of the transfer parameter.
 * Don't use it after this functions returns.
 *
 * The api object is the one created by the application, it will be valid until
 * the application deletes it.
 *
 * @param api MEGASdk object that started the transfer
 * @param transfer Information about the transfer
 *
 * @see [MEGATransfer transferredBytes], [MEGATransfer speed]
 */
- (void)onTransferUpdate:(MEGASdk *)api transfer:(MEGATransfer *)transfer;

/**
 * @brief This function is called when there is a temporary error processing a transfer
 *
 * The transfer continues after this callback, so expect more [MEGATransferDelegate onTransferTemporaryError] or
 * a [MEGATransferListener onTransferFinish] callback
 *
 * The SDK retains the ownership of the transfer and error parameters.
 * Don't use them after this functions returns.
 *
 * @param api MEGASdk object that started the transfer
 * @param request Information about the transfer
 * @param error Error information
 */
- (void)onTransferTemporaryError:(MEGASdk *)api transfer:(MEGATransfer *)transfer error:(MEGAError *)error;

/**
 * @brief This function is called when there are new or updated contacts in the account
 * @param api MEGASdk object connected to the account
 * @param users List that contains the new or updated contacts
 */
- (void)onUsersUpdate:(MEGASdk *)api userList:(MEGAUserList *)userList;

/**
 * @brief This function is called when there are new or updated nodes in the account
 * @param api MEGASdk object connected to the account
 * @param nodes List that contains the new or updated nodes
 */
- (void)onNodesUpdate:(MEGASdk *)api nodeList:(MEGANodeList *)nodeList;

/**
 * @brief This function is called when an inconsistency is detected in the local cache
 *
 * You should call [MEGASdk fetchNodes] when this callback is received
 *
 * @param api MEGASdk object connected to the account
 */
- (void)onReloadNeeded:(MEGASdk *)api;

@end
