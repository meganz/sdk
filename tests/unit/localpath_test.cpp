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

using namespace mega;

#ifdef WIN32
static const std::string rootName = "D";
static const std::string rootDrive = rootName + ':';
static const std::string winPathPrefix = "\\\\?\\";
#else
static const std::string rootName;
static const std::string rootDrive;
#endif
static const std::string pathSep{LocalPath::localPathSeparator_utf8};

TEST(LocalPathTest, AppendEmptyLocalPathWithSeparator)
{
    const std::string input{"/home/user/myFolder"};
    const std::string expected{"/home/user/myFolder" + pathSep};
    LocalPath auxLocalPath = LocalPath::fromAbsolutePath(input);
    ASSERT_FALSE(auxLocalPath.endsInSeparator());
    auxLocalPath.appendWithSeparator(LocalPath(), true);
    EXPECT_EQ(auxLocalPath.toPath(false), expected);
}

TEST(LocalPathTest, LocalPathStrToMegaPathStr)
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

TEST(LocalPathTest, LocalPathStrToMegaPathStrNormalized)
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

TEST(LocalPathTest, MegaPathStrToLocalPathStr)
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

TEST(LocalPathTest, AbsoluteLocalPathFromFileNameStr)
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

TEST(LocalPathTest, AbsoluteLocalPathFromPreformattedLocalPathStr)
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

TEST(LocalPathTest, RelativeLocalPathFromPreformattedLocalPathStr)
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

TEST(LocalPathTest, AbsolutePathFromPlatformEncodedStr)
{
#if defined(WIN32)
    std::wstring input{L"D:\\home\\user\\leaf.txt"};
    std::string expected{"D:\\home\\user\\leaf.txt"};
#else
    std::string input{"/home/userleaf.txt"};
    std::string expected = input;
#endif
    auto outputLocalPath = LocalPath::fromPlatformEncodedAbsolute(std::move(input));
    EXPECT_EQ(outputLocalPath.toPath(false), expected);
}

TEST(LocalPathTest, Clear)
{
    auto localPath = LocalPath::fromAbsolutePath("/home/user/Jos\x65\xCC\x81.txt");
    auto checkLocalPath = LocalPath::fromRelativePath("Jos\x65\xCC\x81.txt");
    EXPECT_FALSE(localPath.empty());
    EXPECT_EQ(localPath.leafName(), checkLocalPath);
    localPath.clear();
    EXPECT_FALSE(localPath.isAbsolute());
    EXPECT_TRUE(localPath.empty());
}

TEST(LocalPathTest, Append)
{
    LocalPath localPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1" + pathSep);
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

TEST(LocalPathTest, Prepend)
{
    auto checkLocalPath =
        LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1" + pathSep + "bar.txt");
    auto localPath = LocalPath::fromRelativePath("bar.txt");
    auto prependPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1");
    localPath.prependWithSeparator(prependPath);
    EXPECT_EQ(localPath, checkLocalPath);
}

TEST(LocalPathTest, Trim)
{
    auto checkLocalPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1");
    auto localPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1" + pathSep);
    localPath.trimNonDriveTrailingSeparator();
    EXPECT_EQ(localPath, checkLocalPath);
}

TEST(LocalPathTest, ChangeLeaf)
{
    auto newLeaf = LocalPath::fromRelativePath("newLeaf.txt");
    auto localPath =
        LocalPath::fromAbsolutePath(rootDrive + pathSep + "home" + pathSep + "leaf.txt");
    auto checkLocalPath =
        LocalPath::fromAbsolutePath(rootDrive + pathSep + "home" + pathSep + "newLeaf.txt");
    localPath.changeLeaf(newLeaf);
    EXPECT_EQ(localPath, checkLocalPath);
}

TEST(LocalPathTest, ChangeSuffix)
{
    auto localPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + "bar.txt");
    auto checkLocalPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + "bar(1).txt");
    auto auxlocalPath = localPath.insertFilenameSuffix("(1)");
    EXPECT_EQ(auxlocalPath, checkLocalPath);
}

TEST(LocalPathTest, GetUTF8Representation)
{
    auto localPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + "bar.txt");
    EXPECT_EQ(localPath.toPath(false), rootDrive + pathSep + "bar.txt");
}

TEST(LocalPathTest, GetLeafName)
{
    auto localPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + "bar.txt");
    auto checkLocalPath = LocalPath::fromRelativePath("bar.txt");
    EXPECT_EQ(localPath.leafName(), checkLocalPath);
}

TEST(LocalPathTest, CheckIfEmpty)
{
    LocalPath localPath;
    EXPECT_TRUE(localPath.empty());
}

TEST(LocalPathTest, IsRootPath)
{
    auto localPath = LocalPath::fromAbsolutePath(rootDrive + pathSep);
    EXPECT_TRUE(localPath.isRootPath());
    localPath = LocalPath::fromRelativePath("bar.txt");
    EXPECT_FALSE(localPath.isRootPath());
}

TEST(LocalPathTest, GetPatentPath)
{
    auto localPath =
        LocalPath::fromAbsolutePath(rootDrive + pathSep + "home" + pathSep + "bar.txt");
    auto checkLocalPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + "home" + pathSep);
    auto auxLocalPath = localPath.parentPath();
    EXPECT_EQ(auxLocalPath, checkLocalPath);
}

TEST(LocalPathTest, GetLeafNameByteIndex)
{
    const std::string leaf = "bar.txt";
    auto localPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + leaf);
#ifdef WIN32
    EXPECT_EQ(localPath.getLeafnameByteIndex(), leaf.size());
#else
    EXPECT_EQ(localPath.getLeafnameByteIndex(), rootDrive.size());
#endif
}

TEST(LocalPathTest, GetSubPath)
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

TEST(LocalPathTest, ContainsAnotherPath)
{
    auto localPath =
        LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1" + pathSep + "bar.txt");
    auto sublocalPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1" + pathSep);
    EXPECT_TRUE(sublocalPath.isContainingPathOf(localPath));
}

TEST(LocalPathTest, NextPathComponent)
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

TEST(LocalPathTest, GetExtention)
{
    auto localPath =
        LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1" + pathSep + "bar.txt");
    std::string ext = ".txt";
    EXPECT_EQ(localPath.extension(), ext);
    EXPECT_TRUE(localPath.extension(ext));
}

TEST(LocalPathTest, IsLocalPathRelated)
{
    auto localPath =
        LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1" + pathSep + "bar.txt");
    auto checkLocalPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1" + pathSep);
    EXPECT_TRUE(localPath.related(checkLocalPath));
    EXPECT_TRUE(checkLocalPath.related(localPath));
}

TEST(LocalPathTest, LocalPathToLocalPathStr)
{
    auto localPath = LocalPath::fromAbsolutePath(rootDrive + pathSep + "folder1" + pathSep +
                                                 "Jos\x65\xCC\x81.txt");
    EXPECT_EQ(localPath.toPath(false),
              rootDrive + pathSep + "folder1" + pathSep + "Jos\x65\xCC\x81.txt");
    EXPECT_EQ(localPath.toPath(true),
              rootDrive + pathSep + "folder1" + pathSep + "Jos\xC3\xA9.txt");
}

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
    lp = LocalPath::fromRelativePath(std::string(".") + pathSep + "foo" + pathSep);
    ASSERT_EQ(lp.leafOrParentName(), "foo");

    // ".\\foo" or "./foo"
    lp = LocalPath::fromRelativePath(std::string(".") + pathSep + "foo");
    ASSERT_EQ(lp.leafOrParentName(), "foo");
}
