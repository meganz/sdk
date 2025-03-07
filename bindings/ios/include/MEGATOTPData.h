/**
 * @file MEGATOTPData.h
 * @brief Object Data for TOTP attributes
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
#import "MEGATOTPDataValidation.h"

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, MEGATOTPHashAlgorithm) {
    MEGATOTPHashUnknown = -1,
    MEGATOTPHashSha1 = 0,
    MEGATOTPHashSha256 = 1,
    MEGATOTPHashSha512 = 2
};

@interface MEGATOTPData : NSObject

/**
 * @brief Returns the shared secret key for TOTP
 *
 * @return shared secret key for TOTP if any, nil otherwise
 */
@property (readonly, nonatomic, nullable) NSString *sharedKey;

/**
 * @brief Expiration time in seconds
 *
 * @return The expiration time in seconds
 */
@property (readonly, nonatomic) NSInteger expirationTime;

/**
 * @brief Hashing algorithm to be used
 * Values:
 *     Unknown = -1
 *     SHA1 = 0
 *     SHA256 = 1
 *     SHA512 = 2
 *
 * @return The hashing algorithm `MEGATOTPHashAlgorithm` to be used
 */
@property (readonly, nonatomic) MEGATOTPHashAlgorithm hashAlgorithm;

/**
 * @brief Number of digits in the generated TOTP code
 *
 * @return The number of digits in the generated TOTP code
 */
@property (readonly, nonatomic) NSInteger digits;

/**
 * @brief Returns a `MEGATOTPDataValidation` instance that can be used to check any error detected
 * in the TotpData object
 *
 * @return A pointer to a newly created `MEGATOTPDataValidation` instance.
 */
- (nullable MEGATOTPDataValidation *)validation;

/**
 * @brief Check if MEGATOTPData is marked to be removed
 *
 * @return true if MEGATOTPData is marked to be removed, otherwise false
 */
- (BOOL)markedToRemove;

/**
 * @brief Use this constant to leave a field untouched
 *
 * @return -1 value
*/
+ (NSInteger)totpNoChangeValue;

/**
 * @brief Initializes a MEGATOTPData instance with the given parameters.
 *
 * @param sharedKey The shared secret key for TOTP. Can be nil if not available.
 * @param expirationTime The expiration time of the TOTP code in seconds.
 * @param hashAlgorithm The hashing algorithm to be used for generating the TOTP code.
 * @param digits The number of digits in the generated TOTP code.
 *
 * @return An initialized instance of MEGATOTPData.
 */
- (instancetype)initWithSharedKey:(NSString *)sharedKey
                  expirationTime:(NSInteger)expirationTime
                   hashAlgorithm:(MEGATOTPHashAlgorithm)hashAlgorithm
                           digits:(NSInteger)digits;

@end

NS_ASSUME_NONNULL_END
