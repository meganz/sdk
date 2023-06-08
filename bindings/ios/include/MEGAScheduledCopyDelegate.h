/**
 * @file MEGAScheduledCopyDelegate.h
 * @brief Delegate to get global events
 *
 * (c) 2023 by Mega Limited, Auckland, New Zealand
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
#import "MEGAScheduledCopy.h"
#import "MEGAError.h"

NS_ASSUME_NONNULL_BEGIN

@class MEGASdk;

/**
 * @brief Protocol to get information about global events.
 *
 * You can implement this interface and start receiving events calling [MEGASdk addMEGAScheduledCopyDelegate:].
 *
 * MEGADelegate objects can also receive global events.
 */
@protocol MEGAScheduledCopyDelegate <NSObject>

@optional

/**
 * @brief This function is called when the state of the backup changes
 *
 * The SDK calls this function when the state of the backup changes, for example
 * from 'active' to 'ongoing' or 'removing exceeding'.
 *
 * You can use MegaScheduledCopy::getState to get the new state.
 *
 * @param api MegaApi object that is backing up files
 * @param backup MegaScheduledCopy object that has changed the state
 */
-(void)onBackupStateChanged:(MEGASdk *)api backup:(MEGAScheduledCopy *)backup;

/**
 * @brief This function is called when a backup is about to start being processed
 *
 * The SDK retains the ownership of the backup parameter.
 * Don't use it after this functions returns.
 *
 * The api object is the one created by the application, it will be valid until
 * the application deletes it.
 *
 * @param api MegaApi object that started the backup
 * @param backup Information about the backup
 */
-(void)onBackupStart:(MEGASdk *)api backup:(MEGAScheduledCopy *)backup;

/**
 * @brief This function is called when a backup has finished
 *
 * The SDK retains the ownership of the backup and error parameters.
 * Don't use them after this functions returns.
 *
 * The api object is the one created by the application, it will be valid until
 * the application deletes it.
 *
 * There won't be more callbacks about this backup.
 * The last parameter provides the result of the backup:
 * If the backup finished without problems,
 * the error code will be API_OK.
 * If some transfer failed, the error code will be API_EINCOMPLETE.
 * If the backup has been skipped the error code will be API_EEXPIRED.
 * If the backup folder cannot be found, the error will be API_ENOENT.
 *
 *
 * @param api MegaApi object that started the backup
 * @param backup Information about the backup
 * @param error Error information
 */
-(void)onBackupFinish:(MEGASdk *)api backup:(MEGAScheduledCopy *)backup error:(MEGAError *)error;

/**
 * @brief This function is called to inform about the progress of a backup
 *
 * The SDK retains the ownership of the backup parameter.
 * Don't use it after this functions returns.
 *
 * The api object is the one created by the application, it will be valid until
 * the application deletes it.
 *
 * @param api MegaApi object that started the backup
 * @param backup Information about the backup
 *
 * @see MegaScheduledCopy::getTransferredBytes, MegaScheduledCopy::getSpeed
 */
-(void)onBackupUpdate:(MEGASdk *)api backup:(MEGAScheduledCopy *)backup;

/**
 * @brief This function is called when there is a temporary error processing a backup
 *
 * The backup continues after this callback, so expect more MegaScheduledCopyListener::onBackupTemporaryError or
 * a MegaScheduledCopyListener::onBackupFinish callback
 *
 * The SDK retains the ownership of the backup and error parameters.
 * Don't use them after this functions returns.
 *
 * @param api MegaApi object that started the backup
 * @param backup Information about the backup
 * @param error Error information
 */
-(void)onBackupTemporaryError:(MEGASdk *)api backup:(MEGAScheduledCopy *)backup error:(MEGAError *)error;

@end

NS_ASSUME_NONNULL_END
