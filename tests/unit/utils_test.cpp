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

#include <mega/base64.h>
#include <mega/filesystem.h>
#include <mega/utils.h>
#include "megafs.h"

#include <mega/db.h>
#include <mega/db/sqlite.h>
#include <mega/json.h>

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

TEST(CharacterSet, IterateUtf8)
{
    using mega::unicodeCodepointIterator;

    // Single code-unit.
    {
        auto it = unicodeCodepointIterator("abc");

        EXPECT_FALSE(it.end());
        EXPECT_EQ(it.get(), 'a');
        EXPECT_EQ(it.get(), 'b');
        EXPECT_EQ(it.get(), 'c');
        EXPECT_TRUE(it.end());
        EXPECT_EQ(it.get(), '\0');
    }

    // Multiple code-unit.
    {
        auto it = unicodeCodepointIterator("q\xf0\x90\x80\x80r");

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
    using mega::unicodeCodepointIterator;

    // Single code-unit.
    {
        auto it = unicodeCodepointIterator(L"abc");

        EXPECT_FALSE(it.end());
        EXPECT_EQ(it.get(), L'a');
        EXPECT_EQ(it.get(), L'b');
        EXPECT_EQ(it.get(), L'c');
        EXPECT_TRUE(it.end());
        EXPECT_EQ(it.get(), L'\0');
    }

    // Multiple code-unit.
    {
        auto it = unicodeCodepointIterator(L"q\xd800\xdc00r");

        EXPECT_FALSE(it.end());
        EXPECT_EQ(it.get(), L'q');
        EXPECT_EQ(it.get(), 0x10000);
        EXPECT_EQ(it.get(), L'r');
        EXPECT_TRUE(it.end());
        EXPECT_EQ(it.get(), L'\0');
    }
}

using namespace mega;
using namespace std;

// Disambiguate between Microsoft's FileSystemType.
using ::mega::FileSystemType;

class ComparatorTest
  : public ::testing::Test
{
public:
    ComparatorTest()
      : mFSAccess()
    {
    }

    template<typename T, typename U>
    int compare(const T& lhs, const U& rhs) const
    {
        return compareUtf(lhs, true, rhs, true, false);
    }

    template<typename T, typename U>
    int ciCompare(const T& lhs, const U& rhs) const
    {
        return compareUtf(lhs, true, rhs, true, true);
    }

    LocalPath fromPath(const string& s)
    {
        return LocalPath::fromPath(s, mFSAccess);
    }

    template<typename T, typename U>
    int fsCompare(const T& lhs, const U& rhs, const FileSystemType type) const
    {
        const auto caseInsensitive = isCaseInsensitive(type);

        return compareUtf(lhs, true, rhs, true, caseInsensitive);
    }

private:
    FSACCESS_CLASS mFSAccess;
}; // ComparatorTest

TEST_F(ComparatorTest, CompareLocalPaths)
{
    LocalPath lhs;
    LocalPath rhs;

    // Case insensitive
    {
        // Make sure basic characters are uppercased.
        lhs = fromPath("abc");
        rhs = fromPath("ABC");

        EXPECT_EQ(ciCompare(lhs, rhs), 0);
        EXPECT_EQ(ciCompare(rhs, lhs), 0);

        // Make sure comparison invariants are not violated.
        lhs = fromPath("abc");
        rhs = fromPath("ABCD");

        EXPECT_LT(ciCompare(lhs, rhs), 0);
        EXPECT_GT(ciCompare(rhs, lhs), 0);

        // Make sure escapes are decoded.
        lhs = fromPath("a%30b");
        rhs = fromPath("A0B");

        EXPECT_EQ(ciCompare(lhs, rhs), 0);
        EXPECT_EQ(ciCompare(rhs, lhs), 0);

        // Make sure decoded characters are uppercased.
        lhs = fromPath("%61%62%63");
        rhs = fromPath("ABC");

        EXPECT_EQ(ciCompare(lhs, rhs), 0);
        EXPECT_EQ(ciCompare(rhs, lhs), 0);

        // Invalid escapes are left as-is.
        lhs = fromPath("a%qb%");
        rhs = fromPath("A%qB%");

        EXPECT_EQ(ciCompare(lhs, rhs), 0);
        EXPECT_EQ(ciCompare(rhs, lhs), 0);
    }

    // Case sensitive
    {
        // Basic comparison.
        lhs = fromPath("abc");

        EXPECT_EQ(compare(lhs, lhs), 0);

        // Make sure characters are not uppercased.
        rhs = fromPath("ABC");

        EXPECT_NE(compare(lhs, rhs), 0);
        EXPECT_NE(compare(rhs, lhs), 0);

        // Make sure comparison invariants are not violated.
        lhs = fromPath("abc");
        rhs = fromPath("abcd");

        EXPECT_LT(compare(lhs, rhs), 0);
        EXPECT_GT(compare(rhs, lhs), 0);

        // Make sure escapes are decoded.
        lhs = fromPath("a%30b");
        rhs = fromPath("a0b");

        EXPECT_EQ(compare(lhs, rhs), 0);
        EXPECT_EQ(compare(rhs, lhs), 0);

        // Invalid escapes are left as-is.
        lhs = fromPath("a%qb%");

        EXPECT_EQ(compare(lhs, lhs), 0);

#ifdef _WIN32
        // Non-UNC prefixes should be skipped.
        lhs = fromPath("\\\\?\\C:\\");
        rhs = fromPath("C:\\");

        EXPECT_EQ(compare(lhs, rhs), 0);
        EXPECT_EQ(compare(rhs, lhs), 0);

        lhs = fromPath("\\\\.\\C:\\");
        rhs = fromPath("C:\\");

        EXPECT_EQ(compare(lhs, rhs), 0);
        EXPECT_EQ(compare(rhs, lhs), 0);

        // Prefixes should only be removed from absolute paths.
        lhs = fromPath("\\\\?\\X");
        rhs = fromPath("X");

        EXPECT_NE(compare(lhs, rhs), 0);
        EXPECT_NE(compare(rhs, lhs), 0);
#endif // _WIN32
    }

    // Filesystem-specific
    {
        lhs = fromPath("a\7%30b%31c");
        rhs = fromPath("A%070B1C");

        // exFAT, FAT32, NTFS and UNKNOWN are case-insensitive.
        EXPECT_EQ(fsCompare(lhs, rhs, FS_EXFAT), 0);
        EXPECT_EQ(fsCompare(lhs, rhs, FS_FAT32), 0);
        EXPECT_EQ(fsCompare(lhs, rhs, FS_NTFS), 0);
        EXPECT_EQ(fsCompare(lhs, rhs, FS_UNKNOWN), 0);

#ifndef _WIN32
        // Everything else is case-sensitive.
        EXPECT_NE(fsCompare(lhs, rhs, FS_EXT), 0);

        rhs = fromPath("a%070b1c");
        EXPECT_EQ(fsCompare(lhs, rhs, FS_EXT), 0);
#endif // ! _WIN32
    }
}

TEST_F(ComparatorTest, CompareLocalPathAgainstString)
{
    LocalPath lhs;
    string rhs;

    // Case insensitive
    {
        // Simple comparison.
        lhs = fromPath("abc");
        rhs = "ABC";

        EXPECT_EQ(ciCompare(lhs, rhs), 0);

        // Invariants.
        lhs = fromPath("abc");
        rhs = "abcd";

        EXPECT_LT(ciCompare(lhs, rhs), 0);

        lhs = fromPath("abcd");
        rhs = "abc";

        EXPECT_GT(ciCompare(lhs, rhs), 0);

        // All local escapes are decoded.
        lhs = fromPath("a%30b%31c");
        rhs = "A0b1C";

        EXPECT_EQ(ciCompare(lhs, rhs), 0);

        // Escapes are uppercased.
        lhs = fromPath("%61%62%63");
        rhs = "ABC";

        EXPECT_EQ(ciCompare(lhs, rhs), 0);

        // Invalid escapes are left as-is.
        lhs = fromPath("a%qb%");
        rhs = "A%QB%";

        EXPECT_EQ(ciCompare(lhs, rhs), 0);
    }

    // Case sensitive
    {
        // Simple comparison.
        lhs = fromPath("abc");
        rhs = "abc";

        EXPECT_EQ(compare(lhs, rhs), 0);

        // Invariants.
        rhs = "abcd";

        EXPECT_LT(compare(lhs, rhs), 0);

        lhs = fromPath("abcd");
        rhs = "abc";

        EXPECT_GT(compare(lhs, rhs), 0);

        // All local escapes are decoded.
        lhs = fromPath("a%30b%31c");
        rhs = "a0b1c";

        EXPECT_EQ(compare(lhs, rhs), 0);

        // Invalid escapes left as-is.
        lhs = fromPath("a%qb%r");
        rhs = "a%qb%r";

        EXPECT_EQ(compare(lhs, rhs), 0);

#ifdef _WIN32
        // Non-UNC prefixes should be skipped.
        lhs = fromPath("\\\\?\\C:\\");
        rhs = "C:\\";

        EXPECT_EQ(compare(lhs, rhs), 0);
        EXPECT_EQ(compare(rhs, lhs), 0);

        lhs = fromPath("\\\\.\\C:\\");
        rhs = "C:\\";

        EXPECT_EQ(compare(lhs, rhs), 0);
        EXPECT_EQ(compare(rhs, lhs), 0);

        // Prefixes should only be removed from absolute paths.
        lhs = fromPath("\\\\?\\X");
        rhs = "X";

        EXPECT_NE(compare(lhs, rhs), 0);
        EXPECT_NE(compare(rhs, lhs), 0);
#endif // _WIN32
    }

    // Filesystem-specific
    {
        lhs = fromPath("a\7%30b%31c");
        rhs = "A%070B1C";

        // exFAT, FAT32, NTFS and UNKNOWN are case-insensitive.
        EXPECT_EQ(fsCompare(lhs, rhs, FS_EXFAT), 0);
        EXPECT_EQ(fsCompare(lhs, rhs, FS_FAT32), 0);
        EXPECT_EQ(fsCompare(lhs, rhs, FS_NTFS), 0);
        EXPECT_EQ(fsCompare(lhs, rhs, FS_UNKNOWN), 0);

#ifndef _WIN32
        // Everything else is case-sensitive.
        EXPECT_NE(fsCompare(lhs, rhs, FS_EXT), 0);

        rhs = "a%070b1c";
        EXPECT_EQ(fsCompare(lhs, rhs, FS_EXT), 0);
#endif // ! _WIN32
    }
}

TEST(Conversion, HexVal)
{
    // Decimal [0-9]
    for (int i = 0x30; i < 0x3a; ++i)
    {
        EXPECT_EQ(hexval(i), i - 0x30);
    }
    
    // Lowercase hexadecimal [a-f]
    for (int i = 0x41; i < 0x47; ++i)
    {
        EXPECT_EQ(hexval(i), i - 0x37);
    }

    // Uppercase hexadeimcal [A-F]
    for (int i = 0x61; i < 0x67; ++i)
    {
        EXPECT_EQ(hexval(i), i - 0x57);
    }
}

TEST(URLCodec, Unescape)
{
    string input = "a%4a%4Bc";
    string output;

    URLCodec::unescape(&input, &output);
    EXPECT_EQ(output, "aJKc");
}

TEST(URLCodec, UnescapeInvalidEscape)
{
    string input;
    string output;

    // First character is invalid.
    input = "a%qbc";
    URLCodec::unescape(&input, &output);
    EXPECT_EQ(output, "a%qbc");

    // Second character is invalid.
    input = "a%bqc";
    URLCodec::unescape(&input, &output);
    EXPECT_EQ(output, "a%bqc");
}

TEST(URLCodec, UnescapeShortEscape)
{
    string input;
    string output;

    // No hex digits.
    input = "a%";
    URLCodec::unescape(&input, &output);
    EXPECT_EQ(output, "a%");

    // Single hex digit.
    input = "a%a";
    URLCodec::unescape(&input, &output);
    EXPECT_EQ(output, "a%a");
}


TEST(Filesystem, isContainingPathOf)
{
    using namespace mega;

#ifdef _WIN32
#define SEP "\\"
#else // _WIN32
#define SEP "/"
#endif // ! _WIN32

    FSACCESS_CLASS fsAccess;

    LocalPath lhs;
    LocalPath rhs;
    size_t pos;

    // lhs does not contain rhs.
    constexpr const size_t sentinel = std::numeric_limits<size_t>::max();
    pos = sentinel;
    lhs = LocalPath::fromPath("a" SEP "b", fsAccess);
    rhs = LocalPath::fromPath("a" SEP "c", fsAccess);

    EXPECT_FALSE(lhs.isContainingPathOf(rhs, &pos));
    EXPECT_EQ(pos, sentinel);

    // lhs does not contain rhs.
    // they do, however, share a common prefix.
    pos = sentinel;
    lhs = LocalPath::fromPath("a", fsAccess);
    rhs = LocalPath::fromPath("ab", fsAccess);

    EXPECT_FALSE(lhs.isContainingPathOf(rhs, &pos));
    EXPECT_EQ(pos, sentinel);

    // lhs contains rhs.
    // no trailing separator.
    pos = sentinel;
    lhs = LocalPath::fromPath("a", fsAccess);
    rhs = LocalPath::fromPath("a" SEP "b", fsAccess);

    EXPECT_TRUE(lhs.isContainingPathOf(rhs, &pos));
    EXPECT_EQ(pos, 2u);

    // trailing separator.
    pos = sentinel;
    lhs = LocalPath::fromPath("a" SEP, fsAccess);
    rhs = LocalPath::fromPath("a" SEP "b", fsAccess);

    EXPECT_TRUE(lhs.isContainingPathOf(rhs, &pos));
    EXPECT_EQ(pos, 2u);

    // lhs contains itself.
    pos = sentinel;
    lhs = LocalPath::fromPath("a" SEP "b", fsAccess);

    EXPECT_TRUE(lhs.isContainingPathOf(lhs, &pos));
    EXPECT_EQ(pos, 3u);

#ifdef _WIN32
    // case insensitive.
    pos = sentinel;
    lhs = LocalPath::fromPath("a" SEP "B", fsAccess);
    rhs = LocalPath::fromPath("A" SEP "b", fsAccess);

    EXPECT_TRUE(lhs.isContainingPathOf(rhs, &pos));
    EXPECT_EQ(pos, 3u);
#endif // _WIN32

#undef SEP
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

    input = LocalPath::fromPath("\\\\.\\C:", fsAccess);
    expected = LocalPath::fromPath("\\\\.\\C:\\", fsAccess);

    EXPECT_EQ(NormalizeAbsolute(input), expected);
    EXPECT_EQ(NormalizeAbsolute(expected), expected);

    input = LocalPath::fromPath("\\\\?\\C:", fsAccess);
    expected = LocalPath::fromPath("\\\\?\\C:\\", fsAccess);

    EXPECT_EQ(NormalizeAbsolute(input), expected);
    EXPECT_EQ(NormalizeAbsolute(expected), expected);
}

TEST(Filesystem, NormalizeAbsoluteRemoveTrailingSeparator)
{
    using namespace mega;

    FSACCESS_CLASS fsAccess;

    LocalPath input = LocalPath::fromPath("A\\", fsAccess);
    LocalPath expected = LocalPath::fromPath("A", fsAccess);

    EXPECT_EQ(NormalizeAbsolute(input), expected);
    EXPECT_EQ(NormalizeAbsolute(expected), expected);

    input = LocalPath::fromPath("\\\\.\\C:\\A\\", fsAccess);
    expected = LocalPath::fromPath("\\\\.\\C:\\A", fsAccess);

    EXPECT_EQ(NormalizeAbsolute(input), expected);
    EXPECT_EQ(NormalizeAbsolute(expected), expected);

    input = LocalPath::fromPath("\\\\?\\C:\\A\\", fsAccess);
    expected = LocalPath::fromPath("\\\\?\\C:\\A", fsAccess);

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

TEST(Filesystem, isReservedName)
{
    using namespace mega;

    FSACCESS_CLASS fsAccess;
    bool expected = false;

#ifdef _WIN32
    expected = true;
#endif // _WIN32
    
    // Representative examples.
    static const string reserved[] = {"AUX", "com1", "LPT4"};

    for (auto& r : reserved)
    {
        EXPECT_EQ(isReservedName(r, FILENODE),   expected);
        EXPECT_EQ(isReservedName(r, FOLDERNODE), expected);
    }

    EXPECT_EQ(isReservedName("a.", FILENODE),   false);
    EXPECT_EQ(isReservedName("a.", FOLDERNODE), expected);
}

class SqliteDBTest
  : public ::testing::Test
{
public:
        SqliteDBTest()
          : Test()
          , fsAccess()
          , name("test")
          , rng()
          , rootPath()
        {
            // Get the current path.
            bool result = fsAccess.cwd(rootPath);
            assert(result);

            // Create temporary DB root path.
            rootPath.appendWithSeparator(
                LocalPath::fromPath("db", fsAccess), false);

            // Make sure our root path is clear.
            fsAccess.emptydirlocal(rootPath);
            fsAccess.rmdirlocal(rootPath);

            // Create root path.
            result = fsAccess.mkdirlocal(rootPath, false, true);
            assert(result);
        }

        ~SqliteDBTest()
        {
            // Remove temporary root path.
            fsAccess.emptydirlocal(rootPath);

            bool result = fsAccess.rmdirlocal(rootPath);
            assert(result);
        }

        FSACCESS_CLASS fsAccess;
        string name;
        PrnGen rng;
        LocalPath rootPath;
}; // SqliteDBTest

TEST_F(SqliteDBTest, CreateCurrent)
{
    SqliteDbAccess dbAccess(rootPath);

    // Assume databases are in legacy format until proven otherwise.
    EXPECT_EQ(dbAccess.currentDbVersion, DbAccess::LEGACY_DB_VERSION);

    // Create a new database.
    unique_ptr<SqliteDbTable> dbTable(dbAccess.open(rng, fsAccess, name));

    // Was the database created successfully?
    ASSERT_TRUE(!!dbTable);

    // New databases should not be in the legacy format.
    EXPECT_EQ(dbAccess.currentDbVersion, DbAccess::DB_VERSION);

    // Are the DB files named correctly?
    auto dbFile =
      dbAccess.databasePath(fsAccess,
                            name,
                            DbAccess::DB_VERSION);

    EXPECT_EQ(dbTable->dbFile(), dbFile);

    // Was the file actually created with the correct name?
    auto fileAccess = fsAccess.newfileaccess(false);

    EXPECT_TRUE(fileAccess->isfile(dbFile));

    // Check for SHM suffix.
    {
        ScopedLengthRestore restorer(dbFile);

        dbFile.append(LocalPath::fromPath("-shm", fsAccess));
        EXPECT_TRUE(fileAccess->isfile(dbFile));
    }

    // Check for WAL suffix.
    {
        ScopedLengthRestore restorer(dbFile);

        dbFile.append(LocalPath::fromPath("-shm", fsAccess));
        EXPECT_TRUE(fileAccess->isfile(dbFile));
    }
}

TEST_F(SqliteDBTest, OpenLegacy)
{
    // Create a dummy database.
    {
        SqliteDbAccess dbAccess(rootPath);

        unique_ptr<SqliteDbTable> dbTable(dbAccess.open(rng, fsAccess, name));
        ASSERT_TRUE(!!dbTable);

        auto from = dbTable->dbFile();
        dbTable.reset();

        auto to =
          dbAccess.databasePath(fsAccess,
                                name,
                                DbAccess::LEGACY_DB_VERSION);

        EXPECT_TRUE(fsAccess.renamelocal(from, to, false));
    }

    // Open the database.
    SqliteDbAccess dbAccess(rootPath);

    EXPECT_EQ(dbAccess.currentDbVersion, DbAccess::LEGACY_DB_VERSION);

    unique_ptr<SqliteDbTable> dbTable(dbAccess.open(rng, fsAccess, name));
    ASSERT_TRUE(!!dbTable);

    EXPECT_EQ(dbAccess.currentDbVersion, DbAccess::LEGACY_DB_VERSION);

    // Is the file named correctly?
    auto dbFile =
      dbAccess.databasePath(fsAccess,
                            name,
                            DbAccess::LEGACY_DB_VERSION);

    EXPECT_EQ(dbFile, dbTable->dbFile());

    dbTable.reset();

    // Check the file's presence.
    auto fileAccess = fsAccess.newfileaccess(false);
    EXPECT_TRUE(fileAccess->isfile(dbFile));
}

TEST_F(SqliteDBTest, OpenCurrent)
{
    // Create a dummy database.
    {
        SqliteDbAccess dbAccess(rootPath);

        EXPECT_EQ(dbAccess.currentDbVersion, DbAccess::LEGACY_DB_VERSION);

        DbTablePtr dbTable(dbAccess.open(rng, fsAccess, name));
        ASSERT_TRUE(!!dbTable);

        EXPECT_EQ(dbAccess.currentDbVersion, DbAccess::DB_VERSION);
    }

    // Open the database.
    SqliteDbAccess dbAccess(rootPath);

    EXPECT_EQ(dbAccess.currentDbVersion, DbAccess::LEGACY_DB_VERSION);

    DbTablePtr dbTable(dbAccess.open(rng, fsAccess, name));
    EXPECT_TRUE(!!dbTable);

    EXPECT_EQ(dbAccess.currentDbVersion, DbAccess::DB_VERSION);
}

TEST_F(SqliteDBTest, ProbeCurrent)
{
    SqliteDbAccess dbAccess(rootPath);

    // Create dummy database.
    {
        auto dbFile =
          dbAccess.databasePath(fsAccess,
                                name,
                                DbAccess::DB_VERSION);

        auto fileAccess = fsAccess.newfileaccess(false);
        EXPECT_TRUE(fileAccess->fopen(dbFile, false, true));
    }

    EXPECT_TRUE(dbAccess.probe(fsAccess, name));
}

TEST_F(SqliteDBTest, ProbeLegacy)
{
    SqliteDbAccess dbAccess(rootPath);

    // Create dummy database.
    {
        auto dbFile =
          dbAccess.databasePath(fsAccess,
                                name,
                                DbAccess::LEGACY_DB_VERSION);

        auto fileAccess = fsAccess.newfileaccess(false);
        EXPECT_TRUE(fileAccess->fopen(dbFile, false, true));
    }

    EXPECT_TRUE(dbAccess.probe(fsAccess, name));
}

TEST_F(SqliteDBTest, ProbeNone)
{
    SqliteDbAccess dbAccess(rootPath);
    EXPECT_FALSE(dbAccess.probe(fsAccess, name));
}

TEST_F(SqliteDBTest, RecycleLegacy)
{
    LocalPath legacyPath;
    LocalPath legacyShmPath;
    LocalPath legacyWalPath;

    // Create a dummy database.
    {
        SqliteDbAccess dbAccess(rootPath);

        unique_ptr<SqliteDbTable> dbTable(dbAccess.open(rng, fsAccess, name));
        ASSERT_TRUE(!!dbTable);

        auto from = dbTable->dbFile();
        dbTable.reset();

        legacyPath =
          dbAccess.databasePath(fsAccess,
                                name,
                                DbAccess::LEGACY_DB_VERSION);

        EXPECT_TRUE(fsAccess.renamelocal(from, legacyPath, false));

        // Create dummy SHM file.
        {
            legacyShmPath = legacyPath;
            legacyShmPath.append(LocalPath::fromPath("-shm", fsAccess));

            auto fileAccess = fsAccess.newfileaccess(false);
            EXPECT_TRUE(fileAccess->fopen(legacyShmPath, false, true));
        }

        // Create dummy WAL file.
        {
            legacyWalPath = legacyPath;
            legacyWalPath.append(LocalPath::fromPath("-wal", fsAccess));

            auto fileAccess = fsAccess.newfileaccess(false);
            EXPECT_TRUE(fileAccess->fopen(legacyWalPath, false, true));
        }
    }

    SqliteDbAccess dbAccess(rootPath);
    
    // Assume database is in the current format.
    dbAccess.currentDbVersion = DbAccess::DB_VERSION;

    // Create a new database, taking care to recycle prior versions.
    auto dbTable =
      unique_ptr<SqliteDbTable>(
        dbAccess.open(rng, fsAccess, name, DB_OPEN_FLAG_RECYCLE));
    ASSERT_TRUE(!!dbTable);

    // Database should remain in the current version.
    EXPECT_EQ(dbAccess.currentDbVersion, DbAccess::DB_VERSION);

    // Has the database been named correctly?
    EXPECT_NE(dbTable->dbFile(), legacyPath);

    auto fileAccess = fsAccess.newfileaccess(false);

    // Was the file created with the correct name?
    EXPECT_TRUE(fileAccess->isfile(dbTable->dbFile()));

    // Was the old DB file recycled?
    EXPECT_FALSE(fileAccess->isfile(legacyPath));

    // Were the old SHM/WAL files recycled?
    EXPECT_FALSE(fileAccess->isfile(legacyShmPath));
    EXPECT_FALSE(fileAccess->isfile(legacyWalPath));
}

TEST_F(SqliteDBTest, RemoveLegacy)
{
    LocalPath legacyPath;

    // Create a dummy database.
    {
        SqliteDbAccess dbAccess(rootPath);

        unique_ptr<SqliteDbTable> dbTable(dbAccess.open(rng, fsAccess, name));
        ASSERT_TRUE(!!dbTable);

        auto from = dbTable->dbFile();
        dbTable.reset();

        legacyPath =
          dbAccess.databasePath(fsAccess,
                                name,
                                DbAccess::LEGACY_DB_VERSION);

        EXPECT_TRUE(fsAccess.renamelocal(from, legacyPath, false));
    }

    SqliteDbAccess dbAccess(rootPath);

    // Assume databases are in the current format.
    dbAccess.currentDbVersion = DbAccess::DB_VERSION;

    // Create a new database, taking care to remove any prior versions.
    unique_ptr<SqliteDbTable> dbTable(dbAccess.open(rng, fsAccess, name));
    ASSERT_TRUE(!!dbTable);

    // Database should remain in current format.
    EXPECT_EQ(dbAccess.currentDbVersion, DbAccess::DB_VERSION);

    // Is the new database named correctly?
    EXPECT_NE(dbTable->dbFile(), legacyPath);

    auto fileAccess = fsAccess.newfileaccess(false);

    // Was the correct file actually created?
    EXPECT_TRUE(fileAccess->isfile(dbTable->dbFile()));

    // Was the old database removed?
    EXPECT_FALSE(fileAccess->isfile(legacyPath));
}

TEST_F(SqliteDBTest, RootPath)
{
    SqliteDbAccess dbAccess(rootPath);
    EXPECT_EQ(dbAccess.rootPath(), rootPath);
}

#ifdef WIN32
#define SEP "\\"
#else // WIN32
#define SEP "/"
#endif // ! WIN32

TEST(LocalPath, AppendWithSeparator)
{
    FSACCESS_CLASS fsAccess;
    LocalPath source;
    LocalPath target;

    // Doesn't add a separator if the target is empty.
    source = LocalPath::fromPath("a", fsAccess);
    target.appendWithSeparator(source, false);

    EXPECT_EQ(target.toPath(fsAccess), "a");

    // Doesn't add a separator if the source begins with one.
    source = LocalPath::fromPath(SEP "b", fsAccess);
    target = LocalPath::fromPath("a", fsAccess);

    target.appendWithSeparator(source, true);
    EXPECT_EQ(target.toPath(fsAccess), "a" SEP "b");

    // Doesn't add a separator if the target ends with one.
    source = LocalPath::fromPath("b", fsAccess);
    target = LocalPath::fromPath("a" SEP, fsAccess);

    target.appendWithSeparator(source, true);
    EXPECT_EQ(target.toPath(fsAccess), "a" SEP "b");

    // Adds a separator when:
    // - source doesn't begin with one.
    // - target doesn't end with one.
    target = LocalPath::fromPath("a", fsAccess);

    target.appendWithSeparator(source, true);
    EXPECT_EQ(target.toPath(fsAccess), "a" SEP "b");
}

TEST(LocalPath, PrependWithSeparator)
{
    FSACCESS_CLASS fsAccess;

    LocalPath source;
    LocalPath target;

    // No separator if target is empty.
    source = LocalPath::fromPath("b", fsAccess);

    target.prependWithSeparator(source);
    EXPECT_EQ(target.toPath(fsAccess), "b");

    // No separator if target begins with separator.
    target = LocalPath::fromPath(SEP "a", fsAccess);

    target.prependWithSeparator(source);
    EXPECT_EQ(target.toPath(fsAccess), "b" SEP "a");

    // No separator if source ends with separator.
    source = LocalPath::fromPath("b" SEP, fsAccess);
    target = LocalPath::fromPath("a", fsAccess);

    target.prependWithSeparator(source);
    EXPECT_EQ(target.toPath(fsAccess), "b" SEP "a");
}

#undef SEP

TEST(JSONWriter, arg_stringWithEscapes)
{
    JSONWriter writer;
    writer.arg_stringWithEscapes("ke", "\"\\");
    EXPECT_EQ(writer.getstring(), "\"ke\":\"\\\"\\\\\"");
}

TEST(JSONWriter, escape)
{
    class Writer
      : public JSONWriter
    {
    public:
        using JSONWriter::escape;
    };

    Writer writer;
    string input = "\"\\";
    string expected = "\\\"\\\\";

    EXPECT_EQ(writer.escape(input.c_str(), input.size()), expected);
}

TEST(JSON, NullValue)
{
    string s = "\"foo\":,\"bar\":null,\"restof\":\"json\"}remainder"; // no leading '{'
    JSON j(s);
    EXPECT_EQ(j.getnameid(), j.getnameid("restof\"")); // no leading '"'
    EXPECT_EQ(0, strcmp(j.pos, "\"json\"}remainder"));
}

