/**
 * @file PasswordNodeData.h
 * @brief Object Data for Password Node attributes
 *
 * (c) 2023- by Mega Limited, Auckland, New Zealand
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
#import "MEGATOTPData.h"

NS_ASSUME_NONNULL_BEGIN

@interface PasswordNodeData : NSObject

/**
 * @brief The password value of the node.
 *
 * @return A string containing the password value.
 */
@property (readonly, nonatomic) NSString *password;

/**
 * @brief The notes associated with the password node.
 *
 * @return A string containing the notes or nil if not set.
 */
@property (readonly, nonatomic, nullable) NSString *notes;

/**
 * @brief The URL associated with the password node.
 *
 * @return A string containing the URL or nil if not set.
 */
@property (readonly, nonatomic, nullable) NSString *url;

/**
 * @brief The username associated with the password node.
 *
 * @return A string containing the username or nil if not set.
 */
@property (readonly, nonatomic, nullable) NSString *userName;

/**
 * @brief Represents data related to TOTP (Time-based One-Time Password) token
 * generation
 *
 * @return A `MEGATOTPData` object containing the TOTP data or nil if not set.
 */
@property (readonly, nonatomic, nullable) MEGATOTPData *totp;

/**
 * @brief Initializes a new instance with the provided password-related attributes.
 *
 * This initializer creates a new object using the attributes from `mega::MEGANode::PasswordNodeData`.
 *
 * @param password The password string (required).
 * @param notes Additional notes related to the password (optional).
 * @param url The associated URL, if any (optional).
 * @param userName The username linked to the password (optional).
 * @param totp A `MEGATOTPData` instance representing TOTP data (optional).
 *
 * @return An initialized instance of the object.
 */
- (instancetype)initWithPassword:(NSString *)password notes:(nullable NSString *)notes url:(nullable NSString *)url userName:(nullable NSString *)userName totp:(nullable MEGATOTPData *)totp;

@end

NS_ASSUME_NONNULL_END
