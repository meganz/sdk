/**
 * (c) 2019 by Mega Limited, Wellsford, New Zealand
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

#include <array>
#include <tuple>

#include <gtest/gtest.h>

#include <mega/utils.h>

TEST(utils, hashCombine_integer)
{
    size_t hash = 0;
    mega::hashCombine(hash, 42);
#ifdef _WIN32
    // MSVC's std::hash gives different values than that of gcc/clang
    ASSERT_EQ(sizeof(hash) == 4 ? 286246808ul : 10203658983813110072ull, hash);
#else
    ASSERT_EQ(2654435811ull, hash);
#endif
}

TEST(utils, readLines)
{
    static const std::string input =
        "\r"
        "\n"
        "     \r"
        "  a\r\n"
        "b\n"
        "c\r"
        "  d  \r"
        "     \n"
        "efg\n";
    static const std::vector<std::string> expected = {
        "  a",
        "b",
        "c",
        "  d  ",
        "efg"
    };

    std::vector<std::string> output;

    ASSERT_TRUE(mega::readLines(input, output));
    ASSERT_EQ(output.size(), expected.size());
    ASSERT_TRUE(std::equal(expected.begin(), expected.end(), output.begin()));
}

