/**
 * @file MEGAUploadOptions.h
 * @brief Optional parameters to customize an upload.
 *
 * (c) 2025 by Mega Limited, Auckland, New Zealand
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

NS_ASSUME_NONNULL_BEGIN

/**
 * @brief Options for uploading files and folders to MEGA.
 *
 * This class encapsulates various configuration options for upload transfers,
 * including custom file naming, modification times, and upload behavior.
 */
@interface MEGAUploadOptions : NSObject

/**
 * @brief Custom file or folder name in MEGA.
 *
 * If nil or empty, the name is taken from the local path.
 */
@property (nonatomic, readonly, nullable) NSString *fileName;

/**
 * @brief Custom modification time for files (seconds since epoch).
 *
 * Use MEGASdk.INVALID_CUSTOM_MOD_TIME to keep the local mtime.
 * Default value is MEGASdk.INVALID_CUSTOM_MOD_TIME (-1).
 */
@property (nonatomic) int64_t mtime;

/**
 * @brief Custom app data associated with the transfer.
 *
 * Accessible via MEGATransfer.appData.
 * Default value is nil.
 */
@property (nonatomic, readonly, nullable) NSString *appData;

/**
 * @brief If YES, the SDK deletes the local file when the upload finishes.
 *
 * Intended for temporary files only.
 * Default value is NO.
 */
@property (nonatomic) BOOL isSourceTemporary;

/**
 * @brief If YES, the upload is put on top of the upload queue.
 *
 * Default value is NO.
 */
@property (nonatomic) BOOL startFirst;

/**
 * @brief One-byte upload trigger tag.
 *
 * Valid values are:
 * - MEGAPitagTriggerNotApplicable = '.'
 * - MEGAPitagTriggerPicker = 'p'
 * - MEGAPitagTriggerDragAndDrop = 'd'
 * - MEGAPitagTriggerCamera = 'c'
 * - MEGAPitagTriggerScanner = 's'
 * - MEGAPitagTriggerSyncAlgorithm = 'a'
 *
 * Default value is MEGAPitagTriggerNotApplicable.
 */
@property (nonatomic) char pitagTrigger;

/**
 * @brief Indicate if the upload is done to a chat.
 *
 * Default value is NO.
 */
@property (nonatomic) BOOL isChatUpload;

/**
 * @brief One-byte upload target tag.
 *
 * Allows specifying destinations such as chat uploads.
 * Apps uploading to chats should set the appropriate chat target (c, C, or s);
 * for other uploads keep the default value to avoid interfering with internal logic.
 *
 * Valid values are:
 * - MEGAPitagTargetNotApplicable = '.'
 * - MEGAPitagTargetCloudDrive = 'D'
 * - MEGAPitagTargetChat1To1 = 'c'
 * - MEGAPitagTargetChatGroup = 'C'
 * - MEGAPitagTargetNoteToSelf = 's'
 * - MEGAPitagTargetIncomingShare = 'i'
 * - MEGAPitagTargetMultipleChats = 'M'
 *
 * Default value is MEGAPitagTargetNotApplicable.
 */
@property (nonatomic) char pitagTarget;

/**
 * @brief Creates a new instance with default values.
 *
 * @return A new MEGAUploadOptions instance with all default values.
 */
- (instancetype)init;

/**
 * @brief Creates a new instance with a custom file name.
 *
 * @param fileName The custom name for the file or folder in MEGA.
 * @return A new MEGAUploadOptions instance.
 */
- (instancetype)initWithFileName:(nullable NSString *)fileName;

/**
 * @brief Creates a new instance with custom file name and modification time.
 *
 * @param fileName The custom name for the file or folder in MEGA.
 * @param mtime Custom modification time (seconds since epoch).
 * @return A new MEGAUploadOptions instance.
 */
- (instancetype)initWithFileName:(nullable NSString *)fileName mtime:(int64_t)mtime;

/**
 * @brief Creates a new instance with all common options.
 *
 * @param fileName The custom name for the file or folder in MEGA.
 * @param mtime Custom modification time (seconds since epoch).
 * @param appData Custom app data associated with the transfer.
 * @return A new MEGAUploadOptions instance.
 */
- (instancetype)initWithFileName:(nullable NSString *)fileName
                           mtime:(int64_t)mtime
                         appData:(nullable NSString *)appData;

/**
 * @brief Creates a new instance with all available options.
 *
 * @param fileName The custom name for the file or folder in MEGA.
 * @param mtime Custom modification time (seconds since epoch).
 * @param appData Custom app data associated with the transfer.
 * @param isSourceTemporary If YES, deletes the local file after upload.
 * @param startFirst If YES, puts the upload at the top of the queue.
 * @param pitagTrigger One-byte upload trigger tag.
 * @param isChatUpload Indicate if the upload is done to a chat.
 * @param pitagTarget One-byte upload target tag.
 * @return A new MEGAUploadOptions instance.
 */
- (instancetype)initWithFileName:(nullable NSString *)fileName
                           mtime:(int64_t)mtime
                         appData:(nullable NSString *)appData
                isSourceTemporary:(BOOL)isSourceTemporary
                      startFirst:(BOOL)startFirst
                    pitagTrigger:(char)pitagTrigger
                    isChatUpload:(BOOL)isChatUpload
                     pitagTarget:(char)pitagTarget NS_DESIGNATED_INITIALIZER;

@end

NS_ASSUME_NONNULL_END
