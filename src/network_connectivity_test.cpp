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

#include "mega/network_connectivity_test.h"

#include "mega/udp_socket_tester.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#else
#include <cerrno>
#endif

#include <map>

using namespace std;
using namespace std::chrono;

namespace mega
{

bool NetworkConnectivityTest::start(UdpSocketTester::TestSuite&& testSuite,
                                    const NetworkConnectivityTestServerInfo& serverInfo)
{
    if (!mSocketTestersIPv4.empty() || !mSocketTestersIPv6.empty())
    {
        return false;
    }

    mTestsPerSocket = testSuite.totalMessageCount();
    mResults = {};

    // Initiate test suite on all sockets
    mSocketTestersIPv4.reserve(serverInfo.ports.size());
    mSocketTestersIPv6.reserve(serverInfo.ports.size());
    for (int p: serverInfo.ports)
    {
        mSocketTestersIPv4.emplace_back(std::make_shared<UdpSocketTester>(serverInfo.ipv4, p))
            ->startSuite(testSuite);
        mSocketTestersIPv6.emplace_back(std::make_shared<UdpSocketTester>(serverInfo.ipv6, p))
            ->startSuite(testSuite);
    }

    return true;
}

const NetworkConnectivityTestResults& NetworkConnectivityTest::getResults()
{
    mTimeoutOfReceive = high_resolution_clock::now() + 1s;

    // IPv4 results
    if (!mSocketTestersIPv4.empty())
    {
        mResults.ipv4 = getTestResults(mSocketTestersIPv4, '4');
        mSocketTestersIPv4.clear();
    }

    // IPv6 results
    if (!mSocketTestersIPv6.empty())
    {
        mResults.ipv6 = getTestResults(mSocketTestersIPv6, '6');
        mSocketTestersIPv6.clear();
    }

    return mResults;
}

NetworkConnectivityTestIpResults
    NetworkConnectivityTest::getTestResults(vector<shared_ptr<UdpSocketTester>>& testers,
                                            char summaryPrefix)
{
    NetworkConnectivityTestIpResults testResults;
    vector<UdpSocketTester::SocketResults> allSocketResults;
    allSocketResults.reserve(testers.size() * mTestsPerSocket);
    map<string, uint16_t> uniqueLogEntries;
    for (auto& t: testers)
    {
        const auto& socketResults = t->getSocketResults(mTimeoutOfReceive);

        // extract status for each message type
        for (const auto& m: socketResults.messageResults)
        {
            if (m.messageType == UdpSocketTester::TestSuite::MessageType::DNS)
                updateStatus(m.errorCode, testResults.dns);
            else
                updateStatus(m.errorCode, testResults.messages);
        }

        // keep unique log entries (and occurrence count)
        for (const auto& l: socketResults.log)
        {
            uniqueLogEntries[l.first] += l.second;
        }

        allSocketResults.push_back(socketResults);
    }

    // convert log entry map to simple messages
    testResults.socketErrors.reserve(uniqueLogEntries.size());
    for (const auto& l: uniqueLogEntries)
    {
        string& entry = testResults.socketErrors.emplace_back(l.first);
        if (l.second > 1)
            entry += " (repeated " + std::to_string(l.second) + " times)";
    }

    testResults.summary = getSummary(summaryPrefix, allSocketResults);

    return testResults;
}

void NetworkConnectivityTest::updateStatus(int error, NetworkConnectivityTestMessageStatus& status)
{
    if (!error)
    {
        if (status == NetworkConnectivityTestMessageStatus::NOT_RUN)
            status = NetworkConnectivityTestMessageStatus::PASS;
        else if (status != NetworkConnectivityTestMessageStatus::PASS)
            status = NetworkConnectivityTestMessageStatus::FAIL;
    }
    else if (isNetworkUnreachable(error))
    {
        if (status == NetworkConnectivityTestMessageStatus::NOT_RUN)
            status = NetworkConnectivityTestMessageStatus::NET_UNREACHABLE;
        else if (status != NetworkConnectivityTestMessageStatus::NET_UNREACHABLE)
            status = NetworkConnectivityTestMessageStatus::FAIL;
    }
    else
    {
        status = NetworkConnectivityTestMessageStatus::FAIL;
    }
}

bool NetworkConnectivityTest::isNetworkUnreachable(int errorCode)
{
#if defined(_WIN32)
    return errorCode == WSAENETUNREACH;
#elif defined(__linux__)
    return errorCode == ENETUNREACH;
#elif defined(__APPLE__)
    return errorCode == EHOSTUNREACH;
#else
    return false;
#endif
}

string
    NetworkConnectivityTest::getSummary(char ipPrefix,
                                        const vector<UdpSocketTester::SocketResults>& socketResults)
{
    // Build a string from all results, with format
    // "ip_ver:port:message_type:attempts:successful [...]", ex:
    // "4:1234:S:30:29 [...]"

    map<string, pair<uint16_t, uint16_t>> results;
    for (const auto& r: socketResults)
    {
        string prefix = string{ipPrefix, ':'} + std::to_string(r.port) + ':';

        for (const auto& m: r.messageResults)
        {
            auto& counters = results[prefix + static_cast<char>(m.messageType)];

            ++counters.first; // attempts
            if (!m.errorCode)
                ++counters.second; // successful
        }
    }

    string summary;
    for (const auto& r: results)
    {
        if (!summary.empty())
            summary += ' ';

        summary +=
            r.first + ':' + std::to_string(r.second.first) + ':' + std::to_string(r.second.second);
    }

    return summary;
}

} // namespace mega
