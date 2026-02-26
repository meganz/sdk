/**
 * @file MEGABackgroundMediaUpload.h
 * @brief Background media upload
 *
 * (c) 2018 - by Mega Limited, Auckland, New Zealand
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

@class MEGASdk;

@interface MEGABackgroundMediaUpload : NSObject

/**
 * @brief Initial step to upload a photo/video via iOS low-power background upload feature.
 *
 * Creates an object which can be used to encrypt a media file, and upload it outside of the SDK,
 * eg. in order to take advantage of a particular platform's low power background upload functionality.
 *
 * @param sdk The MEGASdk instance the new object will be used with. It must live longer than the new object.
 * @return A pointer to an object that keeps some needed state through the process of
 *         uploading a media file via iOS low power background uploads (or similar).
 */
- (nullable instancetype)initWithMEGASdk:(MEGASdk *)sdk;

/**
 * @brief Extract mediainfo information about the photo or video.
 *
 * Call this function once with the file to be uploaded. It uses mediainfo to extract information that will
 * help other clients to show or to play the files. The information is stored in this object until the whole
 * operation completes.
 *
 * Call ensureMediaInfo in MEGASdk first in order prepare the library to attach file attributes
 * that enable videos to be identified and played in the web browser.
 *
 * @param inputFilepath The file to analyse with MediaInfo.
 * @return YES if analysis was performed (and any relevant attributes stored ready for upload), NO if mediainfo was not ready yet.
 */
- (BOOL)analyseMediaInfoForFileAtPath:(NSString *)inputFilepath;

/**
 * @brief Encrypt the file or a portion of it.
 *
 * Call this function once with the file to be uploaded. It uses mediainfo to extract information that will
 * help the webclient show or play the file in various browsers. The information is stored in this object
 * until the whole operation completes. The encrypted data is stored in a new file.
 *
 * In order to save space on mobile devices, this function can be called in such a way that the last portion
 * of the file is encrypted (to a new file), and then that last portion of the file is removed by file truncation.
 * That operation can be repeated until the file is completely encrypted, and only the encrypted version remains,
 * and takes up the same amount of space on the device. The size of the portions must first be calculated by using
 * the 'adjustsSizeOnly' parameter, and iterating from the start of the file, specifying the approximate sizes of the portions.
 *
 * Encryption is done by reading small pieces of the file, encrypting them, and outputting to the new file,
 * so that RAM usage is not excessive.
 *
 * @param inputFilePath The file to encrypt a portion of (and the one that is ultimately being uploaded).
 * @param start The index of the first byte of the file to encrypt.
 * @param length The number of bytes of the file to encrypt. The function will round this value up by up to 1MB to fit the
 *        MEGA internal chunking algorithm. The number of bytes actually encrypted and stored in the new file is the updated number.
 *        You can supply -1 as input to request the remainder file (from start) be encrypted.
 * @param outputFilePath The name of the new file to create, and store the encrypted data in.
 * @param adjustsSizeOnly If this is set YES, then encryption is not performed, and only the length parameter is adjusted.
 *        This feature is to enable precalculating the exact sizes of the file portions for upload.
 * @return If the function tries to encrypt and succeeds, the return value is the suffix to append to the URL when uploading this enrypted chunk.
 *         If adjustsizeonly was set, and the function succeeds, the return value will be a nonempty string.
 *         If the function fails, the return value is an empty string, and an error will have been logged.
 */
- (nullable NSString *)encryptFileAtPath:(NSString *)inputFilePath startPosition:(int64_t)start length:(int64_t *)length outputFilePath:(nullable NSString *)outputFilePath adjustsSizeOnly:(BOOL)adjustsSizeOnly;

/**
 * @brief Retrieves the value of the uploadURL once it has been successfully requested via requestBackgroundUploadURLWithFileSize:mediaUpload:delegate: in MEGASdk.
 *
 * @return The URL to upload to (after appending the suffix), if one has been received. Otherwise the string will be empty.
 */
- (nullable NSString *)uploadURLString;

/**
 * @brief Sets the GPS coordinates for the node
 *
 * The node created via completeBackgroundMediaUpload:fileName:parentNode:fingerprint:originalFingerprint:binaryUploadToken:delegate: in MEGASdk
 * will gain these coordinates as part of the
 * node creation. If the unshareable flag is set, the coodinates are encrypted in a way that even if the
 * node is later shared, the GPS coordinates cannot be decrypted by a different account.
 *
 * @param latitude The GPS latitude
 * @param longitude The GPS longitude
 * @param unshareable Set this true to prevent the coordinates being readable by other accounts.
 */
- (void)setCoordinatesWithLatitude:(double)latitude longitude:(double)longitude isUnshareable:(BOOL)unshareable;

/**
 * @brief Turns the data stored in this object into a base 64 encoded binary data.
 *
 * The object can then be recreated via unserialize method in MEGABackgroundMediaUpload and supplying the returned binary data.
 *
 * You take ownership of the returned value.
 *
 * @return serialized version of this object (including URL, mediainfo attributes, and internal data suitable to resume uploading with in future).
 */
- (nullable NSData *)serialize;

/**
 * @brief Get back the needed MEGABackgroundMediaUpload after the iOS app exited and restarted.
 *
 * In case the iOS app exits while a background upload is going on, and the app is started again
 * to complete the operation, call this function to recreate the MEGABackgroundMediaUpload object
 * needed for a call to completeBackgroundMediaUpload:fileName:parentNode:fingerprint:originalFingerprint:binaryUploadToken:delegate: in MEGASdk.
 * The object must have been serialised before the app was unloaded by using serialize method in MEGABackgroundMediaUpload.
 *
 * You take ownership of the returned value.
 *
 * @param data The binary the object was serialized to previously.
 * @param sdk The MEGASdk this object will be used with. It must live longer than this object.
 * @return A new MEGABackgroundMediaUpload instance with all fields set to the data that was
 *         stored in the serialized binary data.
 */
+ (nullable instancetype)unserializByData:(NSData *)data MEGASdk:(MEGASdk *)sdk;

@end

NS_ASSUME_NONNULL_END
