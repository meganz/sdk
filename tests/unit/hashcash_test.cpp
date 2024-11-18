/**
 * (c) 2024 by Mega Limited, New Zealand
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

#include <gtest/gtest.h>

namespace mega
{
std::string gencash(const std::string& token, uint8_t easiness);
}

TEST(hashcash, gencash)
{
    std::vector<std::tuple<std::string, uint8_t, std::string>> hashcash{
        {"wFqIT_wY3tYKcrm5zqwaUoWym3ZCz32cCsrJOgYBgihtpaWUhGyWJ--EY-zfwI-i", 180, "owAAAA"},
        {"3NIjq_fgu6bTyepwHuKiaB8a1YRjISBhktWK1fjhRx86RhOqKZNAcOZht0wJvmhQ", 180, "AQAAAA"},
    };

    for (const auto& t : hashcash)
    {
        ASSERT_EQ(::mega::gencash(std::get<0>(t), std::get<1>(t)), std::get<2>(t));
    }
}
