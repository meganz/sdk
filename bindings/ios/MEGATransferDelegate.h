//
//  MEGATransferDelegate.h
//
//  Created by Javier Navarro on 06/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MEGATransfer.h"
#import "MEGAError.h"

@class MEGASdk;

/**
 * @brief Protocol to receive information about transfers
 *
 * All transfers allows to pass a pointer to an implementation of this interface in the last parameter.
 * You can also get information about all transfers using [MEGASdk addMEGATransferDelegate:]
 *
 * MEGADelegate objects can also receive information about transfers
 *
 * This protocol uses MEGATransfer objects to provide information of transfers. Take into account that not all
 * fields of MEGATransfer objects are valid for all transfers. See the documentation about each transfer to know
 * which fields contain useful information for each one.
 *
 */
@protocol MEGATransferDelegate <NSObject>

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

@end
