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

TEST(Filesystem, NormalizeAbsoluteAddDriveSeparator)
{
    using namespace mega;

    FSACCESS_CLASS fsAccess;

    LocalPath input = LocalPath::fromPath("C:", fsAccess);
    LocalPath expected = LocalPath::fromPath("C:\\", fsAccess);

    EXPECT_EQ(NormalizeAbsolute(input), expected);
    EXPECT_EQ(NormalizeAbsolute(expected), expected);
}

TEST(Filesystem, NormalizeAbsoluteRemoveTrailingSeparator)
{
    using namespace mega;

    FSACCESS_CLASS fsAccess;

    LocalPath input = LocalPath::fromPath("a\\", fsAccess);
    LocalPath expected = LocalPath::fromPath("a", fsAccess);

    EXPECT_EQ(NormalizeAbsolute(input), expected);
    EXPECT_EQ(NormalizeAbsolute(expected), expected);
}

TEST(Filesystem, NormalizeRelativeRemoveLeadingSeparator)
{
    using namespace mega;

    FSACCESS_CLASS fsAccess;

    LocalPath input = LocalPath::fromPath("\\a\\b\\", fsAccess);
    LocalPath expected = LocalPath::fromPath("a\\b", fsAccess);

    EXPECT_EQ(NormalizeRelative(input), expected);
    EXPECT_EQ(NormalizeRelative(expected), expected);
}

TEST(Filesystem, NormalizeRelativeRemoveTrailingSeparator)
{
    using namespace mega;

    FSACCESS_CLASS fsAccess;

    LocalPath input = LocalPath::fromPath("a\\b\\", fsAccess);
    LocalPath expected = LocalPath::fromPath("a\\b", fsAccess);

    EXPECT_EQ(NormalizeRelative(input), expected);
    EXPECT_EQ(NormalizeRelative(expected), expected);
}

#else // _WIN32

TEST(Filesystem, NormalizeAbsoluteAddRootSeparator)
{
    using namespace mega;

    FSACCESS_CLASS fsAccess;

    LocalPath input = LocalPath::fromPath("", fsAccess);
    LocalPath expected = LocalPath::fromPath("/", fsAccess);

    EXPECT_EQ(NormalizeAbsolute(input), expected);
    EXPECT_EQ(NormalizeAbsolute(expected), expected);
}

TEST(Filesystem, NormalizeAbsoluteRemoveTrailingSeparator)
{
    using namespace mega;

    FSACCESS_CLASS fsAccess;

    LocalPath input = LocalPath::fromPath("a/", fsAccess);
    LocalPath expected = LocalPath::fromPath("a", fsAccess);

    EXPECT_EQ(NormalizeAbsolute(input), expected);
    EXPECT_EQ(NormalizeAbsolute(expected), expected);
}

TEST(Filesystem, NormalizeRelativeRemoveLeadingSeparator)
{
    using namespace mega;

    FSACCESS_CLASS fsAccess;

    LocalPath input = LocalPath::fromPath("/a/b/", fsAccess);
    LocalPath expected = LocalPath::fromPath("a/b", fsAccess);

    EXPECT_EQ(NormalizeRelative(input), expected);
    EXPECT_EQ(NormalizeRelative(expected), expected);
}

TEST(Filesystem, NormalizeRelativeRemoveTrailingSeparator)
{
    using namespace mega;

    FSACCESS_CLASS fsAccess;

    LocalPath input = LocalPath::fromPath("a/b/", fsAccess);
    LocalPath expected = LocalPath::fromPath("a/b", fsAccess);

    EXPECT_EQ(NormalizeRelative(input), expected);
    EXPECT_EQ(NormalizeRelative(expected), expected);
}

#endif // _WIN32

TEST(Filesystem, NormalizeRelativeEmpty)
{
    using namespace mega;

    LocalPath path;

    EXPECT_EQ(NormalizeRelative(path), path);
}

