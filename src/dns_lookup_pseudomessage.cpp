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

#include "mega/dns_lookup_pseudomessage.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#include <array>
#include <sstream>

namespace mega
{

namespace dns_lookup_pseudomessage
{

enum DnsType : uint16_t
{
    IPV4 = 0x01, // a.k.a. DNS type A (1)
    IPV6 = 0x1C, // a.k.a. DNS type AAAA (28)
};

/**
 * @brief Build a string like a DNS lookup message, considering network byte order
 *
 * @param userId user id to be used for building the pseudo-domain
 * @param messageId id to be set to the DNS lookup message
 * @param dnsType specify what type of IP to resolve the name to
 *
 * @return a string like a DNS lookup message
 */
static std::string get(uint64_t userId, uint16_t messageId, DnsType dnsType)
{
    // DNS message header
    static const std::array<uint16_t, 6> header{
        htons(messageId), // random id
        htons(256), // flags: standard query, recursion desired
        htons(1), // question count: 1 since it's a query
        0, // answer count: 0 since it's a query
        0, // authority record count
        0}; // additional record count

    const std::array<uint16_t, 2> dnsOptions{htons(dnsType), // DNS type
                                             htons(1)}; // additional record count

    // Build message
    std::string hexId{userIdToHex(userId)};
    std::stringstream m;
    m.write(reinterpret_cast<const char*>(header.data()),
            header.size() * sizeof(decltype(header)::value_type));
    m << char{static_cast<char>(hexId.size())} << hexId << char{4} << "test" << char{4} << "mega"
      << char{2} << "nz" << '\0';
    m.write(reinterpret_cast<const char*>(dnsOptions.data()),
            dnsOptions.size() * sizeof(decltype(dnsOptions)::value_type));

    return m.str();
}

std::string getForIPv4(uint64_t userId, uint16_t messageId)
{
    return get(userId, messageId, DnsType::IPV4);
}

std::string getForIPv6(uint64_t userId, uint16_t messageId)
{
    return get(userId, messageId, DnsType::IPV6);
}

} // namespace dns_lookup_pseudomessage

std::string userIdToHex(uint64_t userId)
{
    std::stringstream s;
    s << std::hex << userId;
    return s.str();
}

} // namespace mega
