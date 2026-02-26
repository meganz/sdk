/**
 * @file MEGATOTPDataValidation.h
 * @brief Object Data for TOTP validation attributes
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

@interface MEGATOTPDataValidation : NSObject

/**
 * @brief Returns `true` if shared secret exists, `false` otherwise
*/
@property (readonly, nonatomic) BOOL sharedSecretExist;

/**
 * @brief Returns `true` if shared secret is valid, `false` otherwise
*/
@property (readonly, nonatomic) BOOL sharedSecretValid;

/**
 * @brief Returns `true` if algorithm has a value different from -1, `false` otherwise
 */
@property (readonly, nonatomic) BOOL algorithmExist;

/**
 * @brief Returns `true` if algorithm is valid, `false` otherwise
*/
@property (readonly, nonatomic) BOOL algorithmValid;

/**
 * @brief Returns `true` if expiration time has a value different from -1, `false` otherwise
 */
@property (readonly, nonatomic) BOOL expirationTimeExist;

/**
 * @brief Returns `true` if expiration time is valid, `false` otherwise
*/
@property (readonly, nonatomic) BOOL expirationTimeValid;

/**
 * @brief Returns `true` if number of digits has a value different from -1, `false` otherwise
 */
@property (readonly, nonatomic) BOOL digitsExist;

/**
 * @brief Returns `true` if number of digits is valid, `false` otherwise
*/
@property (readonly, nonatomic) BOOL digitsValid;

/**
 * @brief Returns `true` if MEGATOTPData instance is valid for initializing a new
 * totp data field in a password node, `false` otherwise
 * @note For initializing the totpData field in a password node, mandatory
 * fields such as the shared secret must be present in the TotpData instance.
*/
@property (readonly, nonatomic) BOOL isValidForCreate;

/**
 * @brief Returns `true` if MEGATOTPData instance is valid just for updating
 * existing totp data on a password node, `false` otherwise
*/
@property (readonly, nonatomic) BOOL isValidForUpdate;

@end

NS_ASSUME_NONNULL_END
