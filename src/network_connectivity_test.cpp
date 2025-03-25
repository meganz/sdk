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
#include <unordered_set>

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

    // Build a list of unique ports and keep DNS port separate
    unordered_set<int> ports(serverInfo.ports.begin(), serverInfo.ports.end());
    static constexpr int DNS_PORT = 53;
    ports.erase(DNS_PORT);

    // Initiate test suite on all sockets
    mSocketTesterIPv4Dns = std::make_shared<UdpSocketTester>(serverInfo.ipv4, DNS_PORT);
    mSocketTesterIPv4Dns->startSuite(testSuite);
    mSocketTesterIPv6Dns = std::make_shared<UdpSocketTester>(serverInfo.ipv6, DNS_PORT);
    mSocketTesterIPv6Dns->startSuite(testSuite);
    mSocketTestersIPv4.reserve(ports.size());
    mSocketTestersIPv6.reserve(ports.size());
    for (int p: ports)
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
    mResults.ipv4 = getTestResults(mSocketTesterIPv4Dns, mSocketTestersIPv4, '4');
    mSocketTestersIPv4.clear();

    // IPv6 results
    mResults.ipv6 = getTestResults(mSocketTesterIPv6Dns, mSocketTestersIPv6, '6');
    mSocketTestersIPv6.clear();

    return mResults;
}

NetworkConnectivityTestIpResults
    NetworkConnectivityTest::getTestResults(shared_ptr<UdpSocketTester> dnsTester,
                                            vector<shared_ptr<UdpSocketTester>>& testers,
                                            char summaryPrefix)
{
    NetworkConnectivityTestIpResults testResults;
    vector<UdpSocketTester::SocketResults> allSocketResults;
    allSocketResults.reserve(testers.size() * mTestsPerSocket);
    map<string, uint16_t> uniqueLogEntries;

    // DNS test
    if (dnsTester)
    {
        const auto& dnsResults = dnsTester->getSocketResults(mTimeoutOfReceive);

        // extract status for each message type
        for (const auto& m: dnsResults.messageResults)
        {
            updateStatus(m.errorCode, testResults.dns);
        }

        // keep unique log entries (and occurrence count)
        for (const auto& l: dnsResults.log)
        {
            uniqueLogEntries[l.first] += l.second;
        }

        allSocketResults.push_back(dnsResults);
    }

    for (auto& t: testers)
    {
        const auto& socketResults = t->getSocketResults(mTimeoutOfReceive);

        // extract status for each message type
        for (const auto& m: socketResults.messageResults)
        {
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
