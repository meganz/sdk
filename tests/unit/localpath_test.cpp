/**
 * @file localpath_test.cpp
 * @brief Unit tests for the LocalPath class.
 *
 * This test suite validates various conversions and operations involving the LocalPath class,
 * including transformations between local file system paths and MEGA path representations.
 */

#include "../stdfs.h"
#include "mega/logging.h"
#include "megaapi.h"

#include <gtest/gtest.h>
#include <mega/file.h>

#include <string>

using namespace std;
using namespace mega;

#ifdef WIN32
static const string rootName = "D";
static const string rootDrive = rootName + ':';
static const string winPathPrefix = "\\\\?\\";
#else
static const string rootName;
static const string rootDrive;
#endif
static const std::string pathSep{LocalPath::localPathSeparator_utf8};

/**
 * @test LocalPathTest.LocalPathCreation
 * @brief Tests different ways to instanciate LocalPath objects
 * This test includes the following test cases:
 * - Test1: Convert local path string into MEGA path (UTF-8) string
 * - Test2: Convert local path string into MEGA path (UTF-8 normalized) string
 * - Test3: Convert MEGA path (UTF-8) string into local path string
 * - Test4: Create absolute LocalPath from fileName string (no character scaping)
 * - Test5: Create absolute LocalPath from a string that was already converted to be appropriate for
 * a local file path
 * - Test6: Create relative LocalPath from a string that was already converted to be appropriate for
 * a local file path
 *
 */
TEST(LocalPathTest, LocalPathCreation)
{
    LOG_debug << "#### Test1: Convert local path string into MEGA path (UTF-8) string ####";
    {
#ifdef WIN32
        string_type input = string_type{L"D:\\home\\user\\Jos\x65\xCC\x81.txt"};
        std::string expected = std::string{"D:\\home\\user\\Jose\xC3\x8C\xC2\x81.txt"};
#else
        std::string input = string_type{"/home/user/Jos\x65\xCC\x81.txt"};
        std::string expected = std::string{"/home/user/Jos\x65\xCC\x81.txt"};
#endif

        std::string outputPathStr;
        LocalPath::local2path(&input, &outputPathStr, false);
        EXPECT_EQ(outputPathStr, expected);
    }

    LOG_debug
        << "#### Test2: Convert local path string into MEGA path (UTF-8 normalized) string ####";
    {
#ifdef WIN32
        string_type input{L"D:\\home\\user\\Jos\x65\xCC\x81.txt"};
        std::string expected{"D:\\home\\user\\Jose\xC3\x8C\xC2\x81.txt"};
#else
        std::string input{"/home/user/Jos\x65\xCC\x81.txt"};
        std::string expected{"/home/user/Jos\xC3\xA9.txt"};
#endif
        std::string outputPathStr;
        LocalPath::local2path(&input, &outputPathStr, true);
        EXPECT_EQ(outputPathStr, expected);
    }

    LOG_debug << "#### Test3: Convert MEGA path (UTF-8) string into local path string ####";
    {
#ifdef WIN32
        std::string input{"D:\\home\\user\\Jos\x65\xCC\x81.txt"};
        string_type expected{L"D:\\home\\user\\Jose\x301.txt"};
#else
        std::string input = string_type{"/home/user/Jos\x65\xCC\x81.txt"};
        std::string expected = std::string{"/home/user/Jos\x65\xCC\x81.txt"};
#endif
        string_type outputPathStr;
        LocalPath::path2local(&input, &outputPathStr);
        EXPECT_EQ(outputPathStr, expected);
    }

    LOG_debug
        << "#### Test4: Create absolute LocalPath from fileName string (no character scaping) ####";
    {
#ifdef WIN32
        std::string input{"Jose\xC3\x8C\xC2\x81.txt"};
        std::string expected{(fs::current_path() / "Jos\x65\xCC\x81.txt").u8string()};
#else
        std::string input{"Jos\x65\xCC\x81.txt"};
        std::string expected{(fs::current_path() / "Jos\x65\xCC\x81.txt").u8string()};
#endif

        auto auxLocalPath = LocalPath::fromAbsolutePath(expected);
        auto outputLocalPath = LocalPath::fromAbsolutePath(input);
        EXPECT_TRUE(outputLocalPath.isAbsolute());
        EXPECT_EQ(outputLocalPath, auxLocalPath);
    }

    LOG_debug << "#### Test5: Create absolute LocalPath from a string that was already converted "
                 "to be appropriate for a local file path ####";
    {
#if defined(WIN32)
        std::string input{"D:\\home\\user\\Jose\x65\xCC\x81.txt"};
        std::string expected{"D:\\home\\user\\Jose\x65\xCC\x81.txt"};
#elif defined(__MACH__)
        std::string input{"/home/user/Jos\xC3\xA9.txt"};
        std::string expected{"/home/user/Jose\xCC\x81.txt"};
#else
        std::string input{"/home/user/Jos\xC3\xA9.txt"};
        std::string expected{"/home/user/Jos\xC3\xA9.txt"};
#endif

        auto outputLocalPath = LocalPath::fromAbsolutePath(input);
        EXPECT_TRUE(outputLocalPath.isAbsolute());
        EXPECT_EQ(outputLocalPath.toPath(false), expected);
    }

    LOG_debug << "#### Test6: Create relative LocalPath from a string that was already converted "
                 "to be appropriate for a local file path ####";
    {
#if defined(WIN32)
        std::string input{"Jose\x65\xCC\x81.txt"};
        std::string expected{"Jose\x65\xCC\x81.txt"};
#elif defined(__MACH__)
        std::string input{"Jos\xC3\xA9.txt"};
        std::string expected{"Jose\xCC\x81.txt"};
#else
        std::string input{"Jos\xC3\xA9.txt"};
        std::string expected{"Jos\xC3\xA9.txt"};
#endif
        auto outputLocalPath = LocalPath::fromRelativePath(input);
        EXPECT_FALSE(outputLocalPath.isAbsolute());
        EXPECT_EQ(outputLocalPath.toPath(false), expected);
    }
}

/**
 * @test LocalPathTest.LocalPathUpdate
 * @brief Tests different modification operations on LocalPath instances.
 *
 * - Test1: clear Localpath
 * - Test2: append to Localpath
 * - Test3: prepend to Localpath
 * - Test4: trim NonDrive trailing separator in a LocalPath
 * - Test5: change leaf in a LocalPath
 * - Test6: change suffix in a LocalPath
 */
TEST(LocalPathTest, LocalPathUpdate)
{
    LOG_debug << "#### Test1: clear Localpath ####";
    {
        auto localPath = LocalPath::fromAbsolutePath("/home/user/Jos\x65\xCC\x81.txt");
        auto checkLocalPath = LocalPath::fromRelativePath("Jos\x65\xCC\x81.txt");
        EXPECT_FALSE(localPath.empty());
        EXPECT_EQ(localPath.leafName(), checkLocalPath);
        localPath.clear();
        EXPECT_FALSE(localPath.isAbsolute());
        EXPECT_TRUE(localPath.empty());
    }

    LOG_debug << "#### Test2: append to Localpath ####";
    {
        LocalPath localPath =
            LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1" + pathSep);
        auto leafLocalPath = LocalPath::fromRelativePath("folder2");
        localPath.append(leafLocalPath);
        auto checkLocalPath =
            LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1" + pathSep + "folder2");
        EXPECT_EQ(localPath, checkLocalPath);

        leafLocalPath = LocalPath::fromRelativePath("bar.txt");
        localPath.appendWithSeparator(leafLocalPath, true);
        checkLocalPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1" + pathSep +
                                                     "folder2" + pathSep + "bar.txt");
        EXPECT_EQ(localPath, checkLocalPath);
    }

    LOG_debug << "#### Test3: prepend to Localpath ####";
    {
        auto checkLocalPath =
            LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1" + pathSep + "bar.txt");
        auto localPath = LocalPath::fromRelativePath("bar.txt");
        auto prependPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1");
        localPath.prependWithSeparator(prependPath);
        EXPECT_EQ(localPath, checkLocalPath);
    }

    LOG_debug << "#### Test4: trim NonDrive trailing separator in a LocalPath ####";
    {
        auto checkLocalPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1");
        auto localPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1" + pathSep);
        localPath.trimNonDriveTrailingSeparator();
        EXPECT_EQ(localPath, checkLocalPath);
    }

    LOG_debug << "#### Test5: change leaf in a LocalPath ####";
    {
        auto newLeaf = LocalPath::fromRelativePath("newLeaf.txt");
        auto localPath =
            LocalPath::fromAbsolutePath(rootDrive + pathSep + "home" + pathSep + "leaf.txt");
        auto checkLocalPath =
            LocalPath::fromAbsolutePath(rootDrive + pathSep + "home" + pathSep + "newLeaf.txt");
        localPath.changeLeaf(newLeaf);
        EXPECT_EQ(localPath, checkLocalPath);
    }

    LOG_debug << "#### Test6: change suffix in a LocalPath ####";
    {
        auto localPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + "bar.txt");
        auto checkLocalPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + "bar(1).txt");
        auto auxlocalPath = localPath.insertFilenameSuffix("(1)");
        EXPECT_EQ(auxlocalPath, checkLocalPath);
    }
}

/**
 * @test LocalPathTest.LocalPathRead
 * @brief Unit tests for LocalPath reading operations.
 *
 * - Test1: get LocalPath internal representation
 * - Test2: get LocalPath leaf name
 * - Test3: check if LocalPath is empty
 * - Test4: Check if LocalPath is a root path
 * - Test5: get LocalPath parent path
 * - Test6: get LocalPath leaf name given a byte index
 * - Test7: get subpath from a LocalPath
 * - Test8: check if one LocalPath contains another one
 * - Test9: check nextPathComponent for a LocalPath
 * - Test10: check LocalPath extension
 * - Test11: Check if one LocalPath is related to another one
 * - Test12: Convert LocalPath to string path
 */
TEST(LocalPathTest, LocalPathRead)
{
    LOG_debug << "#### Test1: get LocalPath  utf8 representation ####";
    {
        auto localPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + "bar.txt");
        EXPECT_EQ(localPath.toPath(false), rootDrive + pathSep + "bar.txt");
    }

    LOG_debug << "#### Test2: get LocalPath leaf name ####";
    {
        auto localPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + "bar.txt");
        auto checkLocalPath = LocalPath::fromRelativePath("bar.txt");
        EXPECT_EQ(localPath.leafName(), checkLocalPath);
    }

    LOG_debug << "#### Test3: check if LocalPath is empty ####";
    {
        LocalPath localPath;
        EXPECT_TRUE(localPath.empty());
    }

    LOG_debug << "#### Test4: Check if LocalPath is a root path ####";
    {
        auto localPath = LocalPath::fromAbsolutePath(rootDrive + pathSep);
        EXPECT_TRUE(localPath.isRootPath());
        localPath = LocalPath::fromRelativePath("bar.txt");
        EXPECT_FALSE(localPath.isRootPath());
    }

    LOG_debug << "#### Test5: get LocalPath parent path ####";
    {
        auto localPath =
            LocalPath::fromAbsolutePath(rootDrive + pathSep + "home" + pathSep + "bar.txt");
        auto checkLocalPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + "home" + pathSep);
        auto auxLocalPath = localPath.parentPath();
        EXPECT_EQ(auxLocalPath, checkLocalPath);
    }

    LOG_debug << "#### Test6: get LocalPath leaf name given a byte index ####";
    {
        const std::string leaf = "bar.txt";
        auto localPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + leaf);
#ifdef WIN32
        EXPECT_EQ(localPath.getLeafnameByteIndex(), leaf.size());
#else
        EXPECT_EQ(localPath.getLeafnameByteIndex(), rootDrive.size());
#endif
    }

    LOG_debug << "#### Test7: get subpath from a LocalPath ####";
    {
        auto localPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + "bar.txt");
        auto checkLocalPath = LocalPath::fromRelativePath("bar.txt");
#ifdef WIN32
        auto sublocalPath =
            localPath.subpathFrom(winPathPrefix.size() + rootDrive.size() + pathSep.size());
#else
        auto sublocalPath = localPath.subpathFrom(rootDrive.size() + pathSep.size());
#endif
        EXPECT_EQ(sublocalPath, checkLocalPath);
    }

    LOG_debug << "#### Test8: check if one LocalPath contains another one ####";
    {
        auto localPath =
            LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1" + pathSep + "bar.txt");
        auto sublocalPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1" + pathSep);
        EXPECT_TRUE(sublocalPath.isContainingPathOf(localPath));
    }

    LOG_debug << "#### Test9: check nextPathComponent for a LocalPath ####";
    {
        LocalPath nextComponent;
        auto localPath =
            LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1" + pathSep + "bar.txt");
#ifdef WIN32
        size_t idx = winPathPrefix.size() + rootDrive.size() + pathSep.size();
#else
        size_t idx = rootDrive.size() + pathSep.size();
#endif
        localPath.nextPathComponent(idx, nextComponent);
        EXPECT_TRUE(localPath.hasNextPathComponent(idx));
        EXPECT_EQ(nextComponent.toPath(false), "folder1");

        EXPECT_TRUE(localPath.hasNextPathComponent(idx));
        localPath.nextPathComponent(idx, nextComponent);
        EXPECT_EQ(nextComponent.toPath(false), "bar.txt");
        EXPECT_FALSE(localPath.hasNextPathComponent(idx));
    }

    LOG_debug << "#### Test10: check LocalPath extension ####";
    {
        auto localPath =
            LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1" + pathSep + "bar.txt");
        std::string ext = ".txt";
        EXPECT_EQ(localPath.extension(), ext);
        EXPECT_TRUE(localPath.extension(ext));
    }

    LOG_debug << "#### Test11: Check if one LocalPath is related to another one ####";
    {
        auto localPath =
            LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1" + pathSep + "bar.txt");
        auto checkLocalPath =
            LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1" + pathSep);
        EXPECT_TRUE(localPath.related(checkLocalPath));
        EXPECT_TRUE(checkLocalPath.related(localPath));
    }

    LOG_debug << "#### Test12: Convert LocalPath to string path ####";
    {
        auto localPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1" + pathSep +
                                                     "Jos\x65\xCC\x81.txt");
        EXPECT_EQ(localPath.toPath(false),
                  rootDrive + pathSep + "folder1" + pathSep + "Jos\x65\xCC\x81.txt");
        EXPECT_EQ(localPath.toPath(true),
                  rootDrive + pathSep + "folder1" + pathSep + "Jos\xC3\xA9.txt");
    }
}

/**
 * @file LocalPathTest_leafOrParentName
 * @brief Unit tests for LocalPath::leafOrParentName() method.
 *
 * - Test1: get leafOrParentName for different example LocalPaths
 */
TEST(LocalPathTest, leafOrParentName)
{
    LOG_debug << "#### Test1: get leafOrParentName for different example LocalPaths ####";
    // "D:\\foo\\bar.txt" or "/foo/bar.txt"
    LocalPath lp = LocalPath::fromAbsolutePath(rootDrive + pathSep + "foo" + pathSep + "bar.txt");
    ASSERT_EQ(lp.leafOrParentName(), "bar.txt");

    // "D:\\foo\\" or "/foo/"
    lp = LocalPath::fromAbsolutePath(rootDrive + pathSep + "foo" + pathSep);
    ASSERT_EQ(lp.leafOrParentName(), "foo");

    // "D:\\foo" or "/foo"
    lp = LocalPath::fromAbsolutePath(rootDrive + pathSep + "foo");
    ASSERT_EQ(lp.leafOrParentName(), "foo");

    // "D:\\" or "/"
    lp = LocalPath::fromAbsolutePath(rootDrive + pathSep);
    ASSERT_EQ(lp.leafOrParentName(), rootName);

#ifdef WIN32
    // "D:"
    lp = LocalPath::fromAbsolutePath(rootDrive);
    ASSERT_EQ(lp.leafOrParentName(), rootName);

    // "D"
    lp = LocalPath::fromAbsolutePath(rootName);
    ASSERT_EQ(lp.leafOrParentName(), rootName);

    // Current implementation prevents the following from working correctly on *nix platforms

    // "D:\\foo\\bar\\.\\" or "/foo/bar/./"
    lp = LocalPath::fromAbsolutePath(rootDrive + pathSep + "foo" + pathSep + "bar" + pathSep + '.' +
                                     pathSep);
    ASSERT_EQ(lp.leafOrParentName(), "bar");

    // "D:\\foo\\bar\\." or "/foo/bar/."
    lp = LocalPath::fromAbsolutePath(rootDrive + pathSep + "foo" + pathSep + "bar" + pathSep + '.');
    ASSERT_EQ(lp.leafOrParentName(), "bar");

    // "D:\\foo\\bar\\..\\" or "/foo/bar/../"
    lp = LocalPath::fromAbsolutePath(rootDrive + pathSep + "foo" + pathSep + "bar" + pathSep +
                                     ".." + pathSep);
    ASSERT_EQ(lp.leafOrParentName(), "foo");

    // "D:\\foo\\bar\\.." or "/foo/bar/.."
    lp =
        LocalPath::fromAbsolutePath(rootDrive + pathSep + "foo" + pathSep + "bar" + pathSep + "..");
    ASSERT_EQ(lp.leafOrParentName(), "foo");
#endif

    // ".\\foo\\" or "./foo/"
    lp = LocalPath::fromRelativePath(string(".") + pathSep + "foo" + pathSep);
    ASSERT_EQ(lp.leafOrParentName(), "foo");

    // ".\\foo" or "./foo"
    lp = LocalPath::fromRelativePath(string(".") + pathSep + "foo");
    ASSERT_EQ(lp.leafOrParentName(), "foo");
}
