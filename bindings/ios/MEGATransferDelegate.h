/**
 * @file MEGATransferDelegate.h
 * @brief Delegate to get transfer events
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
#import "MEGAError.h"

@class MEGASdk;

/**
 * @brief Protocol to receive information about transfers.
 *
 * All transfers allows to pass a pointer to an implementation of this interface in the last parameter.
 * You can also get information about all transfers using [MEGASdk addMEGATransferDelegate:].
 *
 * MEGADelegate objects can also receive information about transfers.
 *
 * This protocol uses MEGATransfer objects to provide information of transfers. Take into account that not all
 * fields of MEGATransfer objects are valid for all transfers. See the documentation about each transfer to know
 * which fields contain useful information for each one.
 *
 */
@protocol MEGATransferDelegate <NSObject>

@optional

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
 * [MEGATransferDelegate onTransferTemporaryError:transfer:error:] or
 * a [MEGATransferDelegate onTransferFinish:transfer:error:] callback.
 *
 * @param api MEGASdk object that started the transfer.
 * @param transfer Information about the transfer.
 * @param error Error information.
 */
- (void)onTransferTemporaryError:(MEGASdk *)api transfer:(MEGATransfer *)transfer error:(MEGAError *)error;

/**
 * @brief This function is called to provide the last readed bytes of streaming downloads
 *
 * This function won't be called for non streaming downloads.
 *
 * @param api MEGASdk object that started the transfer
 * @param transfer Information about the transfer
 * @param buffer Buffer with the last readed bytes
 * @return YES to continue the transfer, NO to cancel it
 *
 * @see [MEGASdk startStreamingNode:startPos:size:]
 */
- (BOOL)onTransferData:(MEGASdk *)api transfer:(MEGATransfer *)transfer buffer:(NSData *)buffer;

@end
