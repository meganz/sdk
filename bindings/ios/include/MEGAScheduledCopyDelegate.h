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
 * You can use [MEGAScheduledCopy state] to get the new state.
 *
 * @param api MEGASdk object that is backing up files
 * @param backup MEGAScheduledCopy object that has changed the state
 */
-(void)onBackupStateChanged:(MEGASdk *)api backup:(MEGAScheduledCopy *)backup;

/**
 * @brief This function is called when a backup is about to start being processed
 *
 * The api object is the one created by the application, it will be valid until
 * the application deletes it.
 *
 * @param api MEGASdk object that started the backup
 * @param backup Information about the backup
 */
-(void)onBackupStart:(MEGASdk *)api backup:(MEGAScheduledCopy *)backup;

/**
 * @brief This function is called when a backup has finished
 *
 * The api object is the one created by the application, it will be valid until
 * the application deletes it.
 *
 * There won't be more callbacks about this backup.
 * The last parameter provides the result of the backup:
 * If the backup finished without problems,
 * the error code will be MEGAErrorTypeApiOk.
 * If some transfer failed, the error code will be MEGAErrorTypeApiEIncomplete.
 * If the backup has been skipped the error code will be MEGAErrorTypeApiEExpired.
 * If the backup folder cannot be found, the error will be MEGAErrorTypeApiENoent.
 *
 *
 * @param api MEGASdk object that started the backup
 * @param backup Information about the backup
 * @param error Error information
 */
-(void)onBackupFinish:(MEGASdk *)api backup:(MEGAScheduledCopy *)backup error:(MEGAError *)error;

/**
 * @brief This function is called to inform about the progress of a backup
 *
 * The api object is the one created by the application, it will be valid until
 * the application deletes it.
 *
 * @param api MEGASdk object that started the backup
 * @param backup Information about the backup
 *
 * @see [MEGAScheduledCopy transferredBytes], [MEGAScheduledCopy speed]
 */
-(void)onBackupUpdate:(MEGASdk *)api backup:(MEGAScheduledCopy *)backup;

/**
 * @brief This function is called when there is a temporary error processing a backup
 *
 * The backup continues after this callback, so expect more MEGAScheduledCopyDelegate onBackupTemporaryError or
 * a MEGAScheduledCopyDelegate onBackupFinish callback
 *
 * @param api MEGASdk object that started the backup
 * @param backup Information about the backup
 * @param error Error information
 */
-(void)onBackupTemporaryError:(MEGASdk *)api backup:(MEGAScheduledCopy *)backup error:(MEGAError *)error;

@end

NS_ASSUME_NONNULL_END
