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

#ifdef _WIN32

TEST(Filesystem, GetLastComponentIndex)
{
    using mega::LocalPath;

    // Absolute
    {
        LocalPath path;

        // With drive letter.
        {
            // No trailing separator.
            path = LocalPath::fromPlatformEncoded(L"C:\\a");
            EXPECT_EQ(path.getLastComponentIndex(), 3);

            path = LocalPath::fromPlatformEncoded(L"C:\\a\\b");
            EXPECT_EQ(path.getLastComponentIndex(), 5);

            // With trailing separator.
            path = LocalPath::fromPlatformEncoded(L"C:\\");
            EXPECT_EQ(path.getLastComponentIndex(), 0);

            path = LocalPath::fromPlatformEncoded(L"C:\\a\\");
            EXPECT_EQ(path.getLastComponentIndex(), 3);

            path = LocalPath::fromPlatformEncoded(L"C:\\a\\b\\");
            EXPECT_EQ(path.getLastComponentIndex(), 5);
        }

        // With drive letter and namespace prefix.
        {
            // No trailing separator.
            path = LocalPath::fromPlatformEncoded(L"\\\\?\\C:\\a");
            EXPECT_EQ(path.getLastComponentIndex(), 7);

            path = LocalPath::fromPlatformEncoded(L"\\\\?\\C:\\a\\b");
            EXPECT_EQ(path.getLastComponentIndex(), 9);

            // With trailing separator.
            path = LocalPath::fromPlatformEncoded(L"\\\\?\\C:\\");
            EXPECT_EQ(path.getLastComponentIndex(), 0);

            path = LocalPath::fromPlatformEncoded(L"\\\\?\\C:\\a\\");
            EXPECT_EQ(path.getLastComponentIndex(), 7);

            path = LocalPath::fromPlatformEncoded(L"\\\\?\\C:\\a\\b\\");
            EXPECT_EQ(path.getLastComponentIndex(), 9);
        }
    }

    // Empty
    EXPECT_EQ(LocalPath().getLastComponentIndex(), LocalPath::npos);

    // Relative
    {
        LocalPath path;

        // With drive letter.
        {
            // No trailing separator.
            path = LocalPath::fromPlatformEncoded(L"C:");
            EXPECT_EQ(path.getLastComponentIndex(), 0);

            path = LocalPath::fromPlatformEncoded(L"C:a");
            EXPECT_EQ(path.getLastComponentIndex(), 2);

            path = LocalPath::fromPlatformEncoded(L"C:a\\b");
            EXPECT_EQ(path.getLastComponentIndex(), 4);

            // With trailing separator.
            path = LocalPath::fromPlatformEncoded(L"C:a\\");
            EXPECT_EQ(path.getLastComponentIndex(), 2);

            path = LocalPath::fromPlatformEncoded(L"C:a\\b\\");
            EXPECT_EQ(path.getLastComponentIndex(), 4);
        }

        // With drive letter and namespace prefix.
        {
            // No trailing separator.
            path = LocalPath::fromPlatformEncoded(L"\\\\?\\C:");
            EXPECT_EQ(path.getLastComponentIndex(), 0);

            path = LocalPath::fromPlatformEncoded(L"\\\\?\\C:a");
            EXPECT_EQ(path.getLastComponentIndex(), 6);

            path = LocalPath::fromPlatformEncoded(L"\\\\?\\C:a\\b");
            EXPECT_EQ(path.getLastComponentIndex(), 8);

            // With trailing separator.
            path = LocalPath::fromPlatformEncoded(L"\\\\?\\C:a\\");
            EXPECT_EQ(path.getLastComponentIndex(), 6);

            path = LocalPath::fromPlatformEncoded(L"\\\\?\\C:a\\b\\");
            EXPECT_EQ(path.getLastComponentIndex(), 8);
        }

        // With namespace prefix.
        {
            // No trailing separator.
            path = LocalPath::fromPlatformEncoded(L"\\\\?\\a");
            EXPECT_EQ(path.getLastComponentIndex(), 4);

            path = LocalPath::fromPlatformEncoded(L"\\\\?\\a\\b");
            EXPECT_EQ(path.getLastComponentIndex(), 6);

            // With trailing separator.
            path = LocalPath::fromPlatformEncoded(L"\\\\?\\a\\");
            EXPECT_EQ(path.getLastComponentIndex(), 4);

            path = LocalPath::fromPlatformEncoded(L"\\\\?\\a\\b\\");
            EXPECT_EQ(path.getLastComponentIndex(), 6);
        }

        // Without drive letter or namespace prefix.
        {
            // No trailing separator.
            path = LocalPath::fromPlatformEncoded(L"a");
            EXPECT_EQ(path.getLastComponentIndex(), 0);

            path = LocalPath::fromPlatformEncoded(L"a\\b");
            EXPECT_EQ(path.getLastComponentIndex(), 2);

            // With trailing separator.
            path = LocalPath::fromPlatformEncoded(L"a\\");
            EXPECT_EQ(path.getLastComponentIndex(), 0);

            path = LocalPath::fromPlatformEncoded(L"a\\b\\");
            EXPECT_EQ(path.getLastComponentIndex(), 2);
        }
    }
}

TEST(Filesystem, GetPreviousComponentIndex)
{
    using mega::LocalPath;

    // Absolute
    {
        LocalPath path;

        // With drive letter.
        {
            path = LocalPath::fromPlatformEncoded(L"C:\\a\\b\\c");

            EXPECT_EQ(path.getPreviousComponentIndex(7), 5);
            EXPECT_EQ(path.getPreviousComponentIndex(5), 3);
            EXPECT_EQ(path.getPreviousComponentIndex(3), 0);
            EXPECT_EQ(path.getPreviousComponentIndex(0), LocalPath::npos);
        }

        // With drive letter and namespace prefix.
        {
            path = LocalPath::fromPlatformEncoded(L"\\\\?\\C:\\a\\b\\c");

            EXPECT_EQ(path.getPreviousComponentIndex(11), 9);
            EXPECT_EQ(path.getPreviousComponentIndex(9), 7);
            EXPECT_EQ(path.getPreviousComponentIndex(7), 0);
            EXPECT_EQ(path.getPreviousComponentIndex(0), LocalPath::npos);
        }
    }

    // Empty
    EXPECT_EQ(LocalPath().getPreviousComponentIndex(0), LocalPath::npos);

    // Relative
    {
        LocalPath path;

        // With drive letter.
        {
            path = LocalPath::fromPlatformEncoded(L"C:a\\b\\c");

            EXPECT_EQ(path.getPreviousComponentIndex(6), 4);
            EXPECT_EQ(path.getPreviousComponentIndex(4), 2);
            EXPECT_EQ(path.getPreviousComponentIndex(2), 0);
            EXPECT_EQ(path.getPreviousComponentIndex(0), LocalPath::npos);
        }

        // With drive letter and namespace prefix.
        {
            path = LocalPath::fromPlatformEncoded(L"\\\\?\\C:a\\b\\c");

            EXPECT_EQ(path.getPreviousComponentIndex(10), 8);
            EXPECT_EQ(path.getPreviousComponentIndex(8), 6);
            EXPECT_EQ(path.getPreviousComponentIndex(6), 0);
            EXPECT_EQ(path.getPreviousComponentIndex(0), LocalPath::npos);
        }

        // With namespace prefix.
        {
            path = LocalPath::fromPlatformEncoded(L"\\\\?\\a\\b\\c");

            EXPECT_EQ(path.getPreviousComponentIndex(8), 6);
            EXPECT_EQ(path.getPreviousComponentIndex(6), 4);
            EXPECT_EQ(path.getPreviousComponentIndex(4), 0);
            EXPECT_EQ(path.getPreviousComponentIndex(0), LocalPath::npos);
        }

        // Without drive letter and namespace prefix.
        {
            path = LocalPath::fromPlatformEncoded(L"a\\b\\c");

            EXPECT_EQ(path.getPreviousComponentIndex(4), 2);
            EXPECT_EQ(path.getPreviousComponentIndex(2), 0);
            EXPECT_EQ(path.getPreviousComponentIndex(0), LocalPath::npos);
        }
    }
}

TEST(Filesystem, GetNextComponentIndex)
{
    using mega::LocalPath;

    // Absolute
    {
        LocalPath path;

        // With drive letter.
        {
            path = LocalPath::fromPlatformEncoded(L"C:\\a\\b\\c");

            EXPECT_EQ(path.getNextComponentIndex(0), 3);
            EXPECT_EQ(path.getNextComponentIndex(3), 5);
            EXPECT_EQ(path.getNextComponentIndex(5), 7);

            // No trailing seperator.
            EXPECT_EQ(path.getNextComponentIndex(7), LocalPath::npos);

            // With trailing separator.
            path = LocalPath::fromPlatformEncoded(L"C:\\a\\b\\c\\");
            EXPECT_EQ(path.getNextComponentIndex(7), LocalPath::npos);
        }

        // With drive letter and namespace prefix.
        {
            path = LocalPath::fromPlatformEncoded(L"\\\\?\\C:\\a\\b\\c");

            EXPECT_EQ(path.getNextComponentIndex(0), 7);
            EXPECT_EQ(path.getNextComponentIndex(7), 9);
            EXPECT_EQ(path.getNextComponentIndex(9), 11);

            // No trailing seperator.
            EXPECT_EQ(path.getNextComponentIndex(11), LocalPath::npos);

            // With trailing separator.
            path = LocalPath::fromPlatformEncoded(L"\\\\?\\C:\\a\\b\\c\\");
            EXPECT_EQ(path.getNextComponentIndex(11), LocalPath::npos);
        }
    }

    // Empty
    EXPECT_EQ(LocalPath().getNextComponentIndex(0), LocalPath::npos);

    // Relative
    {
        LocalPath path;

        // With drive letter.
        {
            path = LocalPath::fromPlatformEncoded(L"C:a\\b\\c");

            EXPECT_EQ(path.getNextComponentIndex(0), 2);
            EXPECT_EQ(path.getNextComponentIndex(2), 4);
            EXPECT_EQ(path.getNextComponentIndex(4), 6);

            // No trailing separator.
            EXPECT_EQ(path.getNextComponentIndex(6), LocalPath::npos);

            // With trailing separator.
            path = LocalPath::fromPlatformEncoded(L"C:a\\b\\c\\");
            EXPECT_EQ(path.getNextComponentIndex(6), LocalPath::npos);
        }

        // With drive letter and namespace prefix.
        {
            path = LocalPath::fromPlatformEncoded(L"\\\\?\\C:a\\b\\c");

            EXPECT_EQ(path.getNextComponentIndex(0), 6);
            EXPECT_EQ(path.getNextComponentIndex(6), 8);
            EXPECT_EQ(path.getNextComponentIndex(8), 10);

            // No trailing separator.
            EXPECT_EQ(path.getNextComponentIndex(10), LocalPath::npos);

            // With trailing separator.
            path = LocalPath::fromPlatformEncoded(L"\\\\?\\C:a\\b\\c\\");
            EXPECT_EQ(path.getNextComponentIndex(10), LocalPath::npos);
        }

        // With namespace prefix.
        {
            path = LocalPath::fromPlatformEncoded(L"\\\\?\\a\\b\\c");

            EXPECT_EQ(path.getNextComponentIndex(0), 4);
            EXPECT_EQ(path.getNextComponentIndex(4), 6);
            EXPECT_EQ(path.getNextComponentIndex(6), 8);

            // No trailing separator.
            EXPECT_EQ(path.getNextComponentIndex(8), LocalPath::npos);

            // With trailing separator.
            path = LocalPath::fromPlatformEncoded(L"\\\\?\\a\\b\\c\\");
            EXPECT_EQ(path.getNextComponentIndex(8), LocalPath::npos);
        }

        // Without drive letter and namespace prefix.
        {
            path = LocalPath::fromPlatformEncoded(L"a\\b\\c");

            EXPECT_EQ(path.getNextComponentIndex(0), 2);
            EXPECT_EQ(path.getNextComponentIndex(2), 4);

            // No trailing separator.
            EXPECT_EQ(path.getNextComponentIndex(4), LocalPath::npos);

            // With trailing separator.
            path = LocalPath::fromPlatformEncoded(L"a\\b\\c\\");
            EXPECT_EQ(path.getNextComponentIndex(4), LocalPath::npos);
        }
    }
}

#endif /* _WIN32 */

#ifndef _WIN32

TEST(Filesystem, GetLastComponentIndex)
{
    using mega::LocalPath;

    // Absolute
    {
        LocalPath path;

        // Only a single component.
        path = LocalPath::fromPlatformEncoded(L"/");
        EXPECT_EQ(path.getLastComponentIndex(), 0);

        // No trailing separator.
        path = LocalPath::fromPlatformEncoded(L"/a/b");
        EXPECT_EQ(path.getLastComponentIndex(), 3);

        // Trailing separator.
        path = LocalPath::fromPlatformEncoded(L"/a/b/");
        EXPECT_EQ(path.getLastComponentIndex(), 3);
    }

    // Empty
    EXPECT_EQ(LocalPath().getLastComponentIndex(), LocalPath::npos);

    // Relative
    {
        LocalPath path;

        // Only a single component.
        path = LocalPath::fromPlatformEncoded(L"a");
        EXPECT_EQ(path.getLastComponentIndex(), 0);

        // No trailing separator.
        path = LocalPath::fromPlatformEncoded(L"a/b");
        EXPECT_EQ(path.getLastComponentIndex(), 2);

        // Trailing separator.
        path = LocalPath::fromPlatformEncoded(L"a/b/");
        EXPECT_EQ(path.getLastComponentIndex(), 2);
    }
}

TEST(Filesystem, GetNextComponentIndex)
{
    using mega::LocalPath;

    // Absolute
    {
        auto path = LocalPath::fromPlatformEncoded(L"/a/b/c");

        EXPECT_EQ(path.getNextComponentIndex(0), 1);
        EXPECT_EQ(path.getNextComponentIndex(1), 3);
        EXPECT_EQ(path.getNextComponentIndex(3), 5);

        // No trailing separator.
        EXPECT_EQ(path.getNextComponentIndex(5), LocalPath::npos);

        // Trailing separator.
        path = LocalPath::fromPlatformEncoded(L"/a/b/c/");
        EXPECT_EQ(path.getNextComponentIndex(5), LocalPath::npos);
    }

    // Empty
    EXPECT_EQ(LocalPath().getNextComponentIndex(0), LocalPath::npos);

    // Relative
    {
        auto path = LocalPath::fromPlatformEncoded(L"a/b/c");

        EXPECT_EQ(path.getNextComponentIndex(0), 2);
        EXPECT_EQ(path.getNextComponentIndex(2), 4);

        // No trailing separator.
        EXPECT_EQ(path.getNextComponentIndex(4), LocalPath::npos);

        // Trailing separator.
        path = LocalPath::fromPlatformEncoded(L"a/b/c/");
        EXPECT_EQ(path.getNextComponentIndex(4), LocalPath::npos);
    }
}

TEST(Filesystem, GetPreviousComponentIndex)
{
    using mega::LocalPath;

    // Absolute
    {
        auto path = LocalPath::fromPlatformEncoded(L"/a/b/c");

        EXPECT_EQ(path.getPreviousComponentIndex(5), 3);
        EXPECT_EQ(path.getPreviousComponentIndex(3), 1);
        EXPECT_EQ(path.getPreviousComponentIndex(1), 0);
        EXPECT_EQ(path.getPreviousComponentIndex(0), LocalPath::npos);
    }

    // Empty
    EXPECT_EQ(LocalPath().getPreviousComponentIndex(0), LocalPath::npos);

    // Relative
    {
        auto path = LocalPath::fromPlatformEncoded(L"a/b/c");

        EXPECT_EQ(path.getPreviousComponentIndex(4), 2);
        EXPECT_EQ(path.getPreviousComponentIndex(2), 0);
        EXPECT_EQ(path.getPreviousComponentIndex(0), LocalPath::npos);
    }
}

#endif /* ! _WIN32 */

