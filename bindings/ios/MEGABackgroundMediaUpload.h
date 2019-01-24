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

@interface MEGABackgroundMediaUpload : NSObject

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
 * @param start The index of the first byte of the file to encrypt
 * @param length The number of bytes of the file to encrypt.  The function will round this value up by up to 1MB to fit the
 *        MEGA internal chunking algorithm.  The number of bytes acutally encrypted and stored in the new file is the updated number.
 * @param outputFilePath The name of the new file to create, and store the encrypted data in.
 * @param urlSuffix The function will update the string passed in.  The content of the string must be appended to the URL
 *        when this portion is uploaded.
 * @param adjustsSizeOnly If this is set YES, then encryption is not performed, and only the length parameter is adjusted.
 *        This feature is to enable precalculating the exact sizes of the file portions for upload.
 */
- (BOOL)encryptFileAtPath:(NSString *)inputFilePath startPosition:(int64_t)start length:(unsigned *)length outputFilePath:(NSString * _Nullable)outputFilePath urlSuffix:(NSString * _Nullable * _Nullable)urlSuffix adjustsSizeOnly:(BOOL)adjustsSizeOnly;

/**
 * @brief Retrieves the value of the uploadURL once it has been successfully requested via requestBackgroundUploadURLWithFileSize:mediaUpload:delegate: in MEGASdk.
 */
- (NSString *)uploadURLString;

/**
 * @brief Turns the data stored in this object into a binary data.
 *
 * The object can then be recreated via resumeBackgroundMediaUploadBySerializedData in MEGASdk and supplying the same binary.
 */
- (NSData *)serialize;

@end

NS_ASSUME_NONNULL_END
