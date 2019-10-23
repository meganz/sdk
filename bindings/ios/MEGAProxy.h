/**
* @file MEGAProxy.h
* @brief Contains the information related to a proxy server.
*
* (c) 2019- by Mega Limited, Auckland, New Zealand
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

typedef NS_ENUM(NSUInteger, MEGAProxyType) {
    MEGAProxyTypeNone,
    MEGAProxyTypeAuto,
    MEGAProxyTypeCustom
};

NS_ASSUME_NONNULL_BEGIN

@interface MEGAProxy : NSObject

/**
 * @brief The current proxy type
 *
 * The allowed values are:
 * - MEGAProxyTypeNone means no proxy
 * - MEGAProxyTypeAuto means automatic detection (default)
 * - MEGAProxyTypeCustom means a proxy using user-provided data
 */
@property (nonatomic) MEGAProxyType type;

/**
 * @brief The URL of the proxy
 *
 * That URL must follow this format: "<scheme>://<hostname|ip>:<port>"
 * This is a valid example: http://127.0.0.1:8080
 */
@property (nonatomic, nullable) NSURL *url;

/**
 * @brief Return the username required to access the proxy
 */
@property (nonatomic, readonly, nullable) NSString *username;

/**
 * @brief Return the password required to access the proxy
 */
@property (nonatomic, readonly, nullable) NSString *password;

/**
 * @brief Set the credentials needed to use the proxy
 *
 * If you don't need to use any credentials, do not use this function
 * or pass nil in the first parameter.
 *
 * @param username Username to access the proxy, or nil if credentials aren't needed
 * @param password Password to access the proxy
 */
- (void)setCredentialsWithUsername:(NSString * _Nullable)username andPassword:(NSString * _Nullable)password;

@end

NS_ASSUME_NONNULL_END
