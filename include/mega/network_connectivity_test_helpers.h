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

#ifndef MEGA_NETWORK_CONNECTIVITY_TEST_HELPERS_H
#define MEGA_NETWORK_CONNECTIVITY_TEST_HELPERS_H

#include <string>
#include <vector>

namespace mega
{

struct NetworkConnectivityTestServerInfo
{
    std::string ipv4;
    std::string ipv6;
    std::vector<int> ports;
};

enum class NetworkConnectivityTestMessageStatus : int
{
    NOT_RUN = -9999,
    PASS = 0,
    FAIL,
    NET_UNREACHABLE,
};

struct NetworkConnectivityTestIpResults
{
    NetworkConnectivityTestMessageStatus udpMessages{NetworkConnectivityTestMessageStatus::NOT_RUN};
    NetworkConnectivityTestMessageStatus dnsLookupMessages{
        NetworkConnectivityTestMessageStatus::NOT_RUN};
    std::string summary;
    std::vector<std::string> socketErrors;
};

struct NetworkConnectivityTestResults
{
    NetworkConnectivityTestIpResults ipv4;
    NetworkConnectivityTestIpResults ipv6;
};

} // namespace mega

#endif // MEGA_NETWORK_CONNECTIVITY_TEST_HELPERS_H
