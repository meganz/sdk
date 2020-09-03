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

TEST(Filesystem, CanonicalizeRemoteName)
{
    using namespace mega;

    string name;

    // Raw control characters should be escaped.
    name.push_back('\0');
    name.push_back('\7');

    // Everything else should remain unchanged.
    name.append("%00%07%%31");

    // Canonicalize the name.
    FSACCESS_CLASS fsAccess;
    fsAccess.canonicalize(&name);

    // Was the name canonicalized correctly?
    ASSERT_EQ(name, "%00%07%00%07%%31");
}

TEST(Filesystem, ControlCharactersRemainEscapedOnlyWhenNecessary)
{
    using namespace mega;

    FSACCESS_CLASS fsAccess;

    // Control characters should remain escaped only if necessary.
    const string input = "%00%0d%0a";

    // Most restrictive escaping policy.
    {
        string name = input;

        fsAccess.escapefsincompatible(&name, FS_UNKNOWN);

        ASSERT_EQ(name, input);
    }

    // Least restrictive escaping policy.
    {
        string name = input;

        fsAccess.escapefsincompatible(&name, FS_EXT);

        ASSERT_EQ(name, "%00\r\n");
    }
}

TEST(Filesystem, EscapesControlCharactersIfNecessary)
{
    using namespace mega;

    FSACCESS_CLASS fsAccess;

    // Cloud should never receive unescaped control characters.
    // If it does, make sure we escape accordingly.
    const string input("\0\r\n", 3);

    // Most restrictive escaping policy.
    {
        string name = input;

        fsAccess.escapefsincompatible(&name, FS_UNKNOWN);

        ASSERT_EQ(name, "%00%0d%0a");
    }

    // Least restrictive escaping policy.
    {
        string name = input;

        fsAccess.escapefsincompatible(&name, FS_EXT);

        ASSERT_EQ(name, "%00\r\n");
    }
}

TEST(Filesystem, EscapesPercentOnlyIfNotEncodingControlCharacter)
{
    using namespace mega;

    FSACCESS_CLASS fsAccess;

    // The % in %00 should not be escaped.
    // The % in %30 should be escaped.
    string name = "%00%30";

    fsAccess.escapefsincompatible(&name, FS_UNKNOWN);

    // Was the string escaped correctly?
    ASSERT_EQ(name, "%00%2530");
}

TEST(Filesystem, EscapesReservedCharacters)
{
    using namespace mega;

    // All of these characters will be escaped.
    string name = "%\\/:?\"<>|*";

    // Generate expected result.
    ostringstream osstream;

    for (auto character : name)
    {
        osstream << "%"
                 << std::hex
                 << std::setfill('0')
                 << std::setw(2)
                 << +character;
    }

    // Use most restrictive escaping policy.
    FSACCESS_CLASS fsAccess;
    fsAccess.escapefsincompatible(&name, FS_UNKNOWN);

    // Was the string correctly escaped?
    ASSERT_EQ(name, osstream.str());
}

TEST(Filesystem, UnescapesEscapedCharacters)
{
    using namespace mega;

    FSACCESS_CLASS fsAccess;

    // All of these characters will be escaped.
    string name = "%\\/:?\"<>|*";
    fsAccess.escapefsincompatible(&name, FS_UNKNOWN);

    // Everything will be unescaped except for control characters.
    fsAccess.unescapefsincompatible(&name);

    // Was the string correctly unescaped?
    ASSERT_STREQ(name.c_str(), "%\\/:?\"<>|*");
}

TEST(Filesystem, UnescapeEncodesControlCharacters)
{
    using namespace mega;

    FSACCESS_CLASS fsAccess;

    // The cloud should never receive unescaped control characters.
    string name("\0\r\n", 3);

    fsAccess.unescapefsincompatible(&name);

    // Were the control characters correctly encoded?
    ASSERT_EQ(name, "%00%0d%0a");
}

TEST(Filesystem, UnescapesEscapeWhenNotEncodingControlCharacter)
{
    using namespace mega;

    FSACCESS_CLASS fsAccess;

    // %30 should be decoded to 0.
    // %00 should remain as %00.
    string name = "%30%00";

    fsAccess.unescapefsincompatible(&name);

    // Was the string correctly unescaped?
    ASSERT_EQ(name, "0%00");
}

