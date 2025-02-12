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

#ifndef MEGA_DNS_LOOKUP_PSEUDOMESSAGE_H
#define MEGA_DNS_LOOKUP_PSEUDOMESSAGE_H

#include <string>

namespace mega
{

namespace dnsLookupPseudomessage
{
std::string getForIPv4(uint64_t userId, uint16_t messageId);
std::string getForIPv6(uint64_t userId, uint16_t messageId);
}

} // namespace mega

#endif // MEGA_DNS_LOOKUP_PSEUDOMESSAGE_H
