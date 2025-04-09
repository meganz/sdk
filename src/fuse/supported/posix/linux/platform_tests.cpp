#include <mega/common/error_or.h>
#include <mega/common/node_info.h>
#include <mega/fuse/common/testing/client.h>
#include <mega/fuse/common/testing/cloud_path.h>
#include <mega/fuse/common/testing/utility.h>
#include <mega/fuse/platform/file_descriptor.h>
#include <mega/fuse/platform/testing/platform_tests.h>
#include <mega/fuse/platform/testing/wrappers.h>

namespace mega
{
namespace fuse
{
namespace testing
{

using platform::FileDescriptor;

static int futimesat(const FileDescriptor& descriptor,
                     const Path& path,
                     struct timeval (&times)[2]);

static int mknod(const Path& path, mode_t mode, dev_t dev);

static int mknodat(const FileDescriptor& descriptor,
                   const Path& path,
                   mode_t mode,
                   dev_t dev);

TEST_P(FUSEPlatformTests, mknod_at_fails_when_already_exists)
{
    auto s = open(MountPathW(), O_PATH);
    ASSERT_TRUE(s);

    ASSERT_TRUE(mknodat(s, "sf0", S_IFREG | 0644, 0));
    ASSERT_EQ(errno, EEXIST);
}

TEST_P(FUSEPlatformTests, mknod_at_fails_when_below_file)
{
    auto sf0 = open(MountPathW() / "sf0", O_PATH);
    ASSERT_TRUE(sf0);

    ASSERT_TRUE(mknodat(sf0, "x", S_IFREG | 0644, 0));
    ASSERT_EQ(errno, ENOTDIR);
}

TEST_P(FUSEPlatformTests, mknod_at_fails_when_not_regular_file)
{
    static const std::vector<mode_t> types = {
        S_IFBLK,
        S_IFCHR,
        S_IFDIR,
        S_IFIFO,
        S_IFSOCK,
    }; // types

    auto s = open(MountPathW(), O_PATH);
    ASSERT_TRUE(s);

    for (auto type : types)
    {
        ASSERT_TRUE(mknodat(s, "sfx", type | 0644, 0));
        ASSERT_EQ(errno, EPERM);

        ASSERT_TRUE(accessat(s, "sfx", F_OK));
        ASSERT_EQ(errno, ENOENT);
    }
}

TEST_P(FUSEPlatformTests, mknod_at_fails_when_read_only)
{
    auto s = open(MountPathR(), O_PATH);
    ASSERT_TRUE(s);

    ASSERT_TRUE(mknodat(s, "sfx", S_IFREG | 0644, 0));
    ASSERT_EQ(errno, EROFS);

    ASSERT_TRUE(accessat(s, "sfx", F_OK));
    ASSERT_EQ(errno, ENOENT);
}

TEST_P(FUSEPlatformTests, mknod_at_fails_when_unknown)
{
    auto s = open(MountPathW(), O_PATH);
    ASSERT_TRUE(s);

    ASSERT_TRUE(mknodat(s, Path("sdx") / "sfx", S_IFREG | 0644, 0));
    ASSERT_EQ(errno, ENOENT);
}

TEST_P(FUSEPlatformTests, mknod_at_succeeds)
{
    auto s = open(MountPathW(), O_PATH);
    ASSERT_TRUE(s);

    ASSERT_FALSE(mknodat(s, "sfx", S_IFREG | 0644, 0));

    ASSERT_TRUE(waitFor([&]() {
        return !access(MountPathO() / "sfx", F_OK);
    }, mDefaultTimeout));

    auto sfx = openat(s, "sfx", O_RDWR);
    ASSERT_TRUE(sfx);

    ASSERT_FALSE(fsync(sfx));

    ASSERT_TRUE(waitFor([&]() {
        auto info = ClientW()->get("/x/s/sfx");
        return info && !info->mIsDirectory && !info->mSize;
    }, mDefaultTimeout));
}

TEST_P(FUSEPlatformTests, mknod_fails_when_already_exists)
{
    ASSERT_TRUE(mknod(MountPathW() / "sf0", S_IFREG | 0644, 0));
    ASSERT_EQ(errno, EEXIST);
}

TEST_P(FUSEPlatformTests, mknod_fails_when_below_file)
{
    ASSERT_TRUE(mknod(MountPathW() / "sf0" / "x", S_IFREG | 0644, 0));
    ASSERT_EQ(errno, ENOTDIR);
}

TEST_P(FUSEPlatformTests, mknod_fails_when_not_regular_file)
{
    static const std::vector<mode_t> types = {
        S_IFBLK,
        S_IFCHR,
        S_IFDIR,
        S_IFIFO,
        S_IFSOCK,
    }; // types

    for (auto type : types)
    {
        ASSERT_TRUE(mknod(MountPathW() / "sfx", type | 0644, 0));
        ASSERT_EQ(errno, EPERM);

        ASSERT_TRUE(access(MountPathW() / "sfx", F_OK));
        ASSERT_EQ(errno, ENOENT);
    }
}

TEST_P(FUSEPlatformTests, mknod_fails_when_read_only)
{
    ASSERT_TRUE(mknod(MountPathR() / "sfx", S_IFREG | 0644, 0));
    ASSERT_EQ(errno, EROFS);

    ASSERT_TRUE(access(MountPathR() / "sfx", F_OK));
    ASSERT_EQ(errno, ENOENT);
}

TEST_P(FUSEPlatformTests, mknod_fails_when_unknown)
{
    ASSERT_TRUE(mknod(MountPathW() / "sdx" / "sfx", S_IFREG | 0644, 0));
    ASSERT_EQ(errno, ENOENT);
}

TEST_P(FUSEPlatformTests, mknod_succeeds)
{
    ASSERT_FALSE(mknod(MountPathW() / "sfx", S_IFREG | 0644, 0));

    ASSERT_TRUE(waitFor([&]() {
        return !access(MountPathO() / "sfx", F_OK);
    }, mDefaultTimeout));

    auto sfx = open(MountPathW() / "sfx", O_RDWR);
    ASSERT_TRUE(sfx);

    ASSERT_FALSE(fsync(sfx));

    ASSERT_TRUE(waitFor([&]() {
        auto info = ClientW()->get("/x/s/sfx");
        return info && !info->mIsDirectory && !info->mSize;
    }, mDefaultTimeout));
}

int futimesat(const FileDescriptor& descriptor,
              const Path& path,
              struct timeval (&times)[2])
{
    return ::futimesat(descriptor.get(),
                       path.string().c_str(),
                       times);
}

int mknod(const Path& path, mode_t mode, dev_t dev)
{
    return ::mknod(path.string().c_str(), mode, dev);
}

int mknodat(const FileDescriptor& descriptor,
            const Path& path,
            mode_t mode,
            dev_t dev)

{
    return ::mknodat(descriptor.get(),
                     path.string().c_str(),
                     mode,
                     dev);
}

} // testing
} // fuse
} // mega

