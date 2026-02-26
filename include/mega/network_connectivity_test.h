/**
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

#ifndef MEGA_NETWORK_CONNECTIVITY_TEST_H
#define MEGA_NETWORK_CONNECTIVITY_TEST_H

#include "mega/network_connectivity_test_helpers.h"
#include "mega/udp_socket_tester.h"

#include <chrono>

namespace mega
{

/**
 * @brief Mechanism for running a network connectivity test and providing results in a predefined
 * format.
 * It includes:
 *  - run the entire test suite on each socket;
 *  - gather test results from all sockets;
 *  - encapsulate the logic for further condensing and summarizing the results.
 */
class NetworkConnectivityTest
{
public:
    bool start(UdpSocketTester::TestSuite&& testSuite,
               const NetworkConnectivityTestServerInfo& serverInfo);

    const NetworkConnectivityTestResults& getResults();

private:
    NetworkConnectivityTestIpResults
        getTestResults(std::shared_ptr<UdpSocketTester> dnsTester,
                       std::vector<std::shared_ptr<UdpSocketTester>>& testers,
                       char summaryPrefix);
    static void updateStatus(int error, NetworkConnectivityTestMessageStatus& status);
    static bool isNetworkUnreachable(int errorCode);
    std::string getSummary(char ipPrefix,
                           const std::vector<UdpSocketTester::SocketResults>& results);

    std::shared_ptr<UdpSocketTester> mSocketTesterIPv4Dns;
    std::vector<std::shared_ptr<UdpSocketTester>> mSocketTestersIPv4;
    std::shared_ptr<UdpSocketTester> mSocketTesterIPv6Dns;
    std::vector<std::shared_ptr<UdpSocketTester>> mSocketTestersIPv6;
    uint16_t mTestsPerSocket{};
    std::chrono::high_resolution_clock::time_point mTimeoutOfReceive;
    NetworkConnectivityTestResults mResults;
};

} // namespace mega

#endif // MEGA_NETWORK_CONNECTIVITY_TEST_H
