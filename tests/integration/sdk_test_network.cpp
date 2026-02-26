/**
 * @brief Mega SDK test file for network commands
 *
 * (c) 2025 by Mega Limited, New Zealand
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

#include "SdkTest_test.h"

#include <gmock/gmock.h>

namespace
{

/**
 * @brief SdkTest.NetworkConnectivityTest
 *
 * Test for MegaApi::runNetworkConnectivityTest(), which should consist of:
 * - get ServerInfo from remote API
 * - send and receive simple UDP messages
 * - send and receive UDP messages for DNS lookup
 * - send event 99495
 */
TEST_F(SdkTest, NetworkConnectivityTest)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    RequestTracker tracker(megaApi[0].get());
    megaApi[0]->runNetworkConnectivityTest(&tracker);
    ASSERT_EQ(API_OK, tracker.waitForResult(10))
        << "Network connectivity test took way more than the expected 1 second";
    auto* testResults = tracker.request->getMegaNetworkConnectivityTestResults();
    ASSERT_THAT(testResults, ::testing::NotNull());

    ASSERT_THAT(testResults->getIPv4UDP(),
                ::testing::AnyOf(
                    MegaNetworkConnectivityTestResults::NETWORK_CONNECTIVITY_TEST_PASS,
                    MegaNetworkConnectivityTestResults::NETWORK_CONNECTIVITY_TEST_NET_UNREACHABLE));

    ASSERT_THAT(testResults->getIPv4DNS(),
                ::testing::AnyOf(
                    MegaNetworkConnectivityTestResults::NETWORK_CONNECTIVITY_TEST_PASS,
                    MegaNetworkConnectivityTestResults::NETWORK_CONNECTIVITY_TEST_NET_UNREACHABLE));

    ASSERT_THAT(testResults->getIPv6UDP(),
                ::testing::AnyOf(
                    MegaNetworkConnectivityTestResults::NETWORK_CONNECTIVITY_TEST_PASS,
                    MegaNetworkConnectivityTestResults::NETWORK_CONNECTIVITY_TEST_NET_UNREACHABLE));

    ASSERT_THAT(testResults->getIPv6DNS(),
                ::testing::AnyOf(
                    MegaNetworkConnectivityTestResults::NETWORK_CONNECTIVITY_TEST_PASS,
                    MegaNetworkConnectivityTestResults::NETWORK_CONNECTIVITY_TEST_NET_UNREACHABLE));
}

}
