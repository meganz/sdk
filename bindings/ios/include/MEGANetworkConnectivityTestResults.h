/**
 * @file MEGANetworkConnectivityTestResults.h
 * @brief Container to store the network connectivity test results.
 *
 * (c) 2025 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the rules set forth in the Terms of Service.
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
 * @brief Enumeration of possible network connectivity test results.
 */
typedef NS_ENUM(NSInteger, MEGANetworkConnectivityTestResult) {
    MEGANetworkConnectivityTestPass = 0,
    MEGANetworkConnectivityTestFail = 1,
    MEGANetworkConnectivityTestNetUnreachable = 2,
};

/**
 * @brief Container class to store the results of network connectivity tests.
 */
@interface MEGANetworkConnectivityTestResults : NSObject

/**
 * @brief Get the result of testing UDP communication over IPv4.
 *
 * @return The test result as one of the MEGANetworkConnectivityTestResult values.
 */
- (MEGANetworkConnectivityTestResult)ipv4UDP;

/**
 * @brief Get the result of testing DNS resolution over IPv4.
 *
 * @return The test result as one of the MEGANetworkConnectivityTestResult values.
 */
- (MEGANetworkConnectivityTestResult)ipv4DNS;

/**
 * @brief Get the result of testing UDP communication over IPv6.
 *
 * @return The test result as one of the MEGANetworkConnectivityTestResult values.
 */
- (MEGANetworkConnectivityTestResult)ipv6UDP;

/**
 * @brief Get the result of testing DNS resolution over IPv6.
 *
 * @return The test result as one of the MEGANetworkConnectivityTestResult values.
 */
- (MEGANetworkConnectivityTestResult)ipv6DNS;

/**
 * @brief Create a copy of this network connectivity test results object.
 *
 * The caller takes ownership of the returned object.
 *
 * @return A new instance of MEGANetworkConnectivityTestResults.
 */
- (MEGANetworkConnectivityTestResults *)copy;

@end

NS_ASSUME_NONNULL_END
