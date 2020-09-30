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

TEST(CharacterSet, IterateUtf8)
{
    using mega::codepointIterator;

    // Single code-unit.
    {
        auto it = codepointIterator("abc");

        EXPECT_FALSE(it.end());
        EXPECT_EQ(it.get(), 'a');
        EXPECT_EQ(it.get(), 'b');
        EXPECT_EQ(it.get(), 'c');
        EXPECT_TRUE(it.end());
        EXPECT_EQ(it.get(), '\0');
    }

    // Multiple code-unit.
    {
        auto it = codepointIterator("q\xf0\x90\x80\x80r");

        EXPECT_FALSE(it.end());
        EXPECT_EQ(it.get(), 'q');
        EXPECT_EQ(it.get(), 0x10000);
        EXPECT_EQ(it.get(), 'r');
        EXPECT_TRUE(it.end());
        EXPECT_EQ(it.get(), '\0');
    }
}

TEST(CharacterSet, IterateUtf16)
{
    using mega::codepointIterator;

    // Single code-unit.
    {
        auto it = codepointIterator(L"abc");

        EXPECT_FALSE(it.end());
        EXPECT_EQ(it.get(), L'a');
        EXPECT_EQ(it.get(), L'b');
        EXPECT_EQ(it.get(), L'c');
        EXPECT_TRUE(it.end());
        EXPECT_EQ(it.get(), L'\0');
    }

    // Multiple code-unit.
    {
        auto it = codepointIterator(L"q\xd800\xdc00r");

        EXPECT_FALSE(it.end());
        EXPECT_EQ(it.get(), L'q');
        EXPECT_EQ(it.get(), 0x10000);
        EXPECT_EQ(it.get(), L'r');
        EXPECT_TRUE(it.end());
        EXPECT_EQ(it.get(), L'\0');
    }
}

TEST(LocalPath, Comparator)
{
    using namespace mega;
    using namespace std;

    FSACCESS_CLASS fsAccess;
    LocalPath lhs;

    auto fromPath =
      [&](const string& s)
      {
          return LocalPath::fromPath(s, fsAccess);
      };

    // Local <-> Local
    {
        LocalPath rhs;

        // Case insensitive
        {
            // Make sure basic characters are uppercased.
            lhs = fromPath("abc");
            rhs = fromPath("ABC");

            EXPECT_EQ(lhs.ciCompare(rhs), 0);
            EXPECT_EQ(rhs.ciCompare(lhs), 0);

            // Make sure comparison invariants are not violated.
            lhs = fromPath("abc");
            rhs = fromPath("ABCD");

            EXPECT_LT(lhs.ciCompare(rhs), 0);
            EXPECT_GT(rhs.ciCompare(lhs), 0);

            // Make sure escapes are decoded.
            lhs = fromPath("a%30b");
            rhs = fromPath("A0B");

            EXPECT_EQ(lhs.ciCompare(rhs), 0);
            EXPECT_EQ(rhs.ciCompare(lhs), 0);

            // Make sure decoded characters are uppercased.
            lhs = fromPath("%61%62%63");
            rhs = fromPath("ABC");

            EXPECT_EQ(lhs.ciCompare(rhs), 0);
            EXPECT_EQ(rhs.ciCompare(lhs), 0);

            // Invalid escapes are left as-is.
            lhs = fromPath("a%qb%");
            rhs = fromPath("A%qB%");

            EXPECT_EQ(lhs.ciCompare(rhs), 0);
            EXPECT_EQ(rhs.ciCompare(lhs), 0);
        }

        // Case sensitive
        {
            // Basic comparison.
            lhs = fromPath("abc");

            EXPECT_EQ(lhs.compare(lhs), 0);

            // Make sure characters are not uppercased.
            rhs = fromPath("ABC");

            EXPECT_NE(lhs.compare(rhs), 0);
            EXPECT_NE(rhs.compare(lhs), 0);

            // Make sure comparison invariants are not violated.
            lhs = fromPath("abc");
            rhs = fromPath("abcd");

            EXPECT_LT(lhs.compare(rhs), 0);
            EXPECT_GT(rhs.compare(lhs), 0);

            // Make sure escapes are decoded.
            lhs = fromPath("a%30b");
            rhs = fromPath("a0b");

            EXPECT_EQ(lhs.compare(rhs), 0);
            EXPECT_EQ(rhs.compare(lhs), 0);

            // Invalid escapes are left as-is.
            lhs = fromPath("a%qb%");

            EXPECT_EQ(lhs.compare(lhs), 0);
        }

        // Filesystem-specific
        {
            lhs = fromPath("a\7%30b%31c");
            rhs = fromPath("A%070B1C");

            // exFAT, FAT32, NTFS and UNKNOWN are case-insensitive.
            EXPECT_EQ(lhs.fsCompare(rhs, FS_EXFAT), 0);
            EXPECT_EQ(lhs.fsCompare(rhs, FS_FAT32), 0);
            EXPECT_EQ(lhs.fsCompare(rhs, FS_NTFS), 0);
            EXPECT_EQ(lhs.fsCompare(rhs, FS_UNKNOWN), 0);

            // Everything else is case-sensitive.
            EXPECT_NE(lhs.fsCompare(rhs, FS_EXT), 0);

            rhs = fromPath("a%070b1c");
            EXPECT_EQ(lhs.fsCompare(rhs, FS_EXT), 0);
        }
    }

    // Local <-> Remote
    {
        string rhs;

        // Case insensitive
        {
            // Simple comparison.
            lhs = fromPath("abc");
            rhs = "ABC";

            EXPECT_EQ(lhs.ciCompare(rhs), 0);

            // Invariants.
            lhs = fromPath("abc");
            rhs = "abcd";

            EXPECT_LT(lhs.ciCompare(rhs), 0);

            lhs = fromPath("abcd");
            rhs = "abc";

            EXPECT_GT(lhs.ciCompare(rhs), 0);

            // All local escapes are decoded.
            lhs = fromPath("a%30b%31c");
            rhs = "A0b1C";

            EXPECT_EQ(lhs.ciCompare(rhs), 0);

            // Escapes are uppercased.
            lhs = fromPath("%61%62%63");
            rhs = "ABC";

            EXPECT_EQ(lhs.ciCompare(rhs), 0);

            // Only remote control escapes are decoded.
            lhs = fromPath("a\7%2530");
            rhs = "a%07%30";

            EXPECT_EQ(lhs.ciCompare(rhs), 0);

            // Invalid escapes are left as-is.
            lhs = fromPath("a%qb%");
            rhs = "A%QB%";

            EXPECT_EQ(lhs.ciCompare(rhs), 0);
        }

        // Case sensitive
        {
            // Simple comparison.
            lhs = fromPath("abc");
            rhs = "abc";

            EXPECT_EQ(lhs.compare(rhs), 0);

            // Invariants.
            rhs = "abcd";

            EXPECT_LT(lhs.compare(rhs), 0);

            lhs = fromPath("abcd");
            rhs = "abc";

            EXPECT_GT(lhs.compare(rhs), 0);

            // All local escapes are decoded.
            lhs = fromPath("a%30b%31c");
            rhs = "a0b1c";

            EXPECT_EQ(lhs.compare(rhs), 0);

            // Only remote control escapes are decoded.
            lhs = fromPath("a\7%2530");
            rhs = "a%07%30";

            EXPECT_EQ(lhs.compare(rhs), 0);

            // Invalid escapes left as-is.
            lhs = fromPath("a%qb%r");
            rhs = "a%qb%r";

            EXPECT_EQ(lhs.compare(rhs), 0);
        }

        // Filesystem-specific
        {
            lhs = fromPath("a\7%30b%31c");
            rhs = "A%070B1C";

            // exFAT, FAT32, NTFS and UNKNOWN are case-insensitive.
            EXPECT_EQ(lhs.fsCompare(rhs, FS_EXFAT), 0);
            EXPECT_EQ(lhs.fsCompare(rhs, FS_FAT32), 0);
            EXPECT_EQ(lhs.fsCompare(rhs, FS_NTFS), 0);
            EXPECT_EQ(lhs.fsCompare(rhs, FS_UNKNOWN), 0);

            // Everything else is case-sensitive.
            EXPECT_NE(lhs.fsCompare(rhs, FS_EXT), 0);

            rhs = "a%070b1c";
            EXPECT_EQ(lhs.fsCompare(rhs, FS_EXT), 0);
        }
    }
}

