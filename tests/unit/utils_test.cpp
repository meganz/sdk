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
#include <mega/filesystem.h>
#include "megafs.h"

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

TEST(Filesystem, EscapesIncompatibleCharacters)
{
    using namespace mega;

    // Most restrictive set of characters.
    string name = "\\/:?\"<>|*+,;=[]";

    // Build expected result string.
    ostringstream osstream;

    for (auto character : name)
    {
        osstream << '%'
                 << std::hex
                 << std::setfill('0')
                 << std::setw(2)
                 << +character;
    }

    // Perform worst case escaping.
    FSACCESS_CLASS fsAccess;
    fsAccess.escapefsincompatible(&name, FS_UNKNOWN);

    ASSERT_EQ(name, osstream.str());
}

TEST(Filesystem, UnescapesAllEscapedCharacters)
{
    using namespace mega;

    const string expected = "\\/:?\"<>|*+,;=[]";
    string name = expected;

    FSACCESS_CLASS fsAccess;
    fsAccess.escapefsincompatible(&name, FS_UNKNOWN);
    fsAccess.unescapefsincompatible(&name);

    ASSERT_EQ(expected, name);
}

