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
#include <mega/hashcash.h>

TEST(hashcash, gencash)
{
    std::vector<std::uint8_t> easinessV{180, 192};
    std::vector<unsigned> numWorkersV{8u, 2u};
    std::vector<std::string> hashcash{
        "wFqIT_wY3tYKcrm5zqwaUoWym3ZCz32cCsrJOgYBgihtpaWUhGyWJ--EY-zfwI-i",
        "3NIjq_fgu6bTyepwHuKiaB8a1YRjISBhktWK1fjhRx86RhOqKZNAcOZht0wJvmhQ",
        "HGztcvhT0sngIveS6C4CY1nx64YFtXnbcqX_Dvj7NxmX0SCNRlCZ51_pMWQgpHdv",
    };

    for (const auto& easiness: easinessV)
    {
        for (const auto& numWorkers: numWorkersV)
        {
            for (const auto& hc: hashcash)
            {
                const auto genCashResult = ::mega::gencash(hc, easiness, numWorkers);
                ASSERT_TRUE(::mega::validateHashcash(hc, easiness, genCashResult))
                    << "Failed hash: " << hc << ": genCashResult [easiness = " << easiness
                    << ", numWorkers = " << numWorkers << "]";
            }
        }
    }
}