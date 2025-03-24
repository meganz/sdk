#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <thread>

#include <mega/fuse/common/error_or.h>
#include <mega/fuse/common/node_info.h>
#include <mega/fuse/common/testing/client.h>
#include <mega/fuse/common/testing/cloud_path.h>
#include <mega/fuse/common/testing/utility.h>
#include <mega/fuse/platform/constants.h>
#include <mega/fuse/platform/file_descriptor.h>
#include <mega/fuse/platform/platform.h>
#include <mega/fuse/platform/testing/platform_tests.h>
#include <mega/fuse/platform/testing/printers.h>
#include <mega/fuse/platform/testing/wrappers.h>

#include <mega/logging.h>

namespace mega
{
namespace fuse
{
namespace testing
{

#ifndef HAS_OPEN_PATH
#define O_PATH O_RDONLY
#endif // !HAS_OPEN_PATH

TEST_P(FUSEPlatformTests, access_at_fails_when_below_file)
{
    auto sf0 = open(MountPathW() / "sf0", O_PATH);
    ASSERT_TRUE(sf0);

    ASSERT_LT(accessat(sf0, "x", F_OK), 0);
    ASSERT_EQ(errno, ENOTDIR);
}

TEST_P(FUSEPlatformTests, access_at_fails_when_read_only)
{
    auto s = open(MountPathR(), O_PATH);
    ASSERT_TRUE(s);

    ASSERT_LT(accessat(s, "sd0", W_OK), 0);
    ASSERT_EQ(errno, EROFS);

    ASSERT_LT(accessat(s, "sf0", W_OK), 0);
    ASSERT_EQ(errno, EROFS);
}

TEST_P(FUSEPlatformTests, access_at_fails_when_not_executable)
{
    auto s = open(MountPathW(), O_PATH);
    ASSERT_TRUE(s);

    ASSERT_LT(accessat(s, "sf0", X_OK), 0);
    ASSERT_EQ(errno, EACCES);
}

TEST_P(FUSEPlatformTests, access_at_fails_when_unknown)
{
    auto s = open(MountPathW(), O_PATH);
    ASSERT_TRUE(s);

    ASSERT_LT(accessat(s, "sfx", F_OK), 0);
    ASSERT_EQ(errno, ENOENT);

    ASSERT_LT(accessat(s, "sfx", W_OK), 0);
    ASSERT_EQ(errno, ENOENT);
}

TEST_P(FUSEPlatformTests, access_at_succeeds)
{
    auto sr = open(MountPathR(), O_PATH);
    ASSERT_TRUE(sr);

    auto sw = open(MountPathW(), O_PATH);
    ASSERT_TRUE(sw);

    // Should be able to test for existence.
    ASSERT_EQ(accessat(sr, "sd0", F_OK), 0);
    ASSERT_EQ(accessat(sw, "sf0", F_OK), 0);

    // Readable directories should have 0500 permissions.
    ASSERT_EQ(accessat(sr, "sd0", R_OK | X_OK), 0);
    
    // Readable files should have 0400 permissions.
    ASSERT_EQ(accessat(sr, "sf0", R_OK), 0);

    // Writable directories should have 0700 permissions.
    ASSERT_EQ(accessat(sw, "sd0", R_OK | W_OK | X_OK), 0);

    // Writable files should have 0600 permissions.
    ASSERT_EQ(accessat(sw, "sf0", R_OK | W_OK), 0);
}

TEST_P(FUSEPlatformTests, access_fails_when_below_file)
{
    ASSERT_LT(access(MountPathW() / "sf0" / "x", F_OK), 0);
    ASSERT_EQ(errno, ENOTDIR);

    ASSERT_LT(access(MountPathW() / "sf0" / "x", W_OK), 0);
    ASSERT_EQ(errno, ENOTDIR);
}

TEST_P(FUSEPlatformTests, access_fails_when_read_only)
{
    ASSERT_LT(access(MountPathR() / "sd0", W_OK), 0);
    ASSERT_EQ(errno, EROFS);

    ASSERT_LT(access(MountPathR() / "sf0", W_OK), 0);
    ASSERT_EQ(errno, EROFS);
}

TEST_P(FUSEPlatformTests, access_fails_when_not_executable)
{
    ASSERT_LT(access(MountPathW() / "sf0", X_OK), 0);
    ASSERT_EQ(errno, EACCES);
}

TEST_P(FUSEPlatformTests, access_fails_when_unknown)
{
    ASSERT_LT(access(MountPathW() / "x", F_OK), 00);
    ASSERT_EQ(errno, ENOENT);

    ASSERT_LT(access(MountPathW() / "x", W_OK), 0);
    ASSERT_EQ(errno, ENOENT);
}

TEST_P(FUSEPlatformTests, access_succeeds)
{
    // Should be able to test for existence.
    ASSERT_EQ(access(MountPathW(), F_OK), 0);
    ASSERT_EQ(access(MountPathW() / "sf0", F_OK), 0);

    // Readable directories should have 0500 permissions.
    ASSERT_EQ(access(MountPathR(), R_OK | X_OK), 0);

    // Readable files should have 0400 permissions.
    ASSERT_EQ(access(MountPathR() / "sf0", R_OK), 0);

    // Writable directories should have 0700 permissions.
    ASSERT_EQ(access(MountPathW(), R_OK | W_OK | X_OK), 0);

    // Writable files should have 0600 permissions.
    ASSERT_EQ(access(MountPathW() / "sf0", R_OK | W_OK), 0);
}

TEST_P(FUSEPlatformTests, fchown_fails_when_read_only)
{
    auto sf0 = open(MountPathR() / "sf0", O_RDONLY);
    ASSERT_TRUE(sf0);

    ASSERT_TRUE(fchown(sf0.get(), 0, 0));
    ASSERT_EQ(errno, EROFS);
}

TEST_P(FUSEPlatformTests, fchown_fails_when_other_user)
{
    auto sf0 = open(MountPathW() / "sf0", O_RDWR);
    ASSERT_TRUE(sf0);

    ASSERT_TRUE(fchown(sf0.get(), getuid(), getgid() + 1));
    ASSERT_EQ(errno, EPERM);

    ASSERT_TRUE(fchown(sf0.get(), getuid() + 1, getgid()));
    ASSERT_EQ(errno, EPERM);
}

TEST_P(FUSEPlatformTests, fchown_succeeds)
{
    auto sf0 = open(MountPathW() / "sf0", O_RDWR);
    ASSERT_TRUE(sf0);

    ASSERT_FALSE(fchown(sf0.get(), getuid(), getgid()));
}

TEST_P(FUSEPlatformTests, fstat_succeeds_after_directory_removed)
{
    auto info = ClientW()->get("/x/s/sd0/sd0d0");
    ASSERT_TRUE(info);

    auto sd0d0 = open(MountPathW() / "sd0" / "sd0d0", O_PATH);
    ASSERT_TRUE(sd0d0);

    auto buffer0 = Stat();
    ASSERT_EQ(fstat(sd0d0, buffer0), 0);
    ASSERT_EQ(buffer0, *info);

    ASSERT_EQ(ClientW()->remove(info->mHandle), API_OK);

    ASSERT_TRUE(waitFor([&]() {
        return access(MountPathO() / "sd0" / "sd0d0", F_OK) < 0
               && errno == ENOENT
               && access(MountPathW() / "sd0" / "sd0d0", F_OK) < 0
               && errno == ENOENT;
    }, mDefaultTimeout));

    LINUX_ONLY(buffer0.st_nlink = 0);

    auto buffer1 = Stat();
    ASSERT_EQ(fstat(sd0d0, buffer1), 0);
    ASSERT_EQ(buffer0, buffer1);
}

TEST_P(FUSEPlatformTests, fstat_succeeds_after_file_removed)
{
    auto info = ClientW()->get("/x/s/sf0");
    ASSERT_TRUE(info);

    auto sf0 = open(MountPathW() / "sf0", O_PATH);
    ASSERT_TRUE(sf0);

    auto buffer0 = Stat();
    ASSERT_EQ(fstat(sf0, buffer0), 0);
    ASSERT_EQ(buffer0, *info);

    ASSERT_EQ(ClientW()->remove(info->mHandle), API_OK);

    ASSERT_TRUE(waitFor([&]() {
        return access(MountPathO() / "sf0", F_OK) < 0
               && errno == ENOENT
               && access(MountPathW() / "sf0", F_OK) < 0
               && errno == ENOENT;
    }, mDefaultTimeout));

    LINUX_ONLY(buffer0.st_nlink = 0);

    auto buffer1 = Stat();
    ASSERT_EQ(fstat(sf0, buffer1), 0);
    ASSERT_EQ(buffer0, buffer1);
}

TEST_P(FUSEPlatformTests, fstat_succeeds)
{
    auto info = ClientW()->get("/x/s/sd0");
    ASSERT_TRUE(info);

    auto sd0 = open(MountPathW() / "sd0", O_PATH);
    ASSERT_TRUE(sd0);

    auto buffer = Stat();
    ASSERT_EQ(fstat(sd0, buffer), 0);
    ASSERT_EQ(buffer, *info);

    auto sf0 = open(MountPathW() / "sf0", O_PATH);
    ASSERT_TRUE(sf0);

    info = ClientW()->get("/x/s/sf0");
    ASSERT_TRUE(info);

    ASSERT_EQ(fstat(sf0, buffer), 0);
    ASSERT_EQ(buffer, *info);
}

TEST_P(FUSEPlatformTests, ftruncate_fails_when_directory)
{
    auto s = open(MountPathW(), O_RDONLY);
    ASSERT_TRUE(s);

    ASSERT_TRUE(ftruncate(s, 0));
    ASSERT_EQ(errno, EINVAL);
}

TEST_P(FUSEPlatformTests, ftruncate_fails_when_read_only)
{
    auto sf0 = open(MountPathR() / "sf0", O_RDONLY);
    ASSERT_TRUE(sf0);

    ASSERT_TRUE(ftruncate(sf0, 0));
    ASSERT_EQ(errno, EINVAL);
}

TEST_P(FUSEPlatformTests, ftruncate_succeeds)
{
    auto sf0 = open(MountPathW() / "sf0", O_RDWR);
    ASSERT_TRUE(sf0);

    ASSERT_FALSE(ftruncate(sf0, 64));

    Stat buffer;

    ASSERT_FALSE(fstat(sf0, buffer));
    ASSERT_EQ(buffer.st_size, 64);

    ASSERT_FALSE(ftruncate(sf0, 0));
    ASSERT_FALSE(fstat(sf0, buffer));
    ASSERT_EQ(buffer.st_size, 0);

    ASSERT_TRUE(waitFor([&]() {
        return !stat(MountPathO() / "sf0", buffer)
               && !buffer.st_size;
    }, mDefaultTimeout));

    ASSERT_FALSE(fsync(sf0));

    ASSERT_TRUE(waitFor([&]() {
        auto info = ClientW()->get("/x/s/sf0");
        return info && !info->mIsDirectory && !info->mSize;
    }, mDefaultTimeout));
}

TEST_P(FUSEPlatformTests, futimes_fails_when_read_only)
{
    auto sf0 = open(MountPathR() / "sf0", O_RDONLY);
    ASSERT_TRUE(sf0);

    struct timeval times[] = {{0, 0}, {0, 0}};

    ASSERT_TRUE(futimes(sf0, times));
    ASSERT_EQ(errno, EROFS);
}

TEST_P(FUSEPlatformTests, futimes_success)
{
    // Open file for writing.
    auto sf0 = open(MountPathW() / "sf0", O_RDWR);
    ASSERT_TRUE(sf0);

    for (auto i = 0; i < 2; ++i)
    {
        struct timeval times[] = {
            {i, i}, {i, i}
        }; // times

        // Try and set the modification time.
        ASSERT_FALSE(futimes(sf0, times));

        // Make sure the modification time was set.
        Stat buffer;

        ASSERT_FALSE(fstat(sf0, buffer));
        ASSERT_EQ(buffer.st_mtime, i);

        // Make sure the new time is visible via observer.
        ASSERT_TRUE(waitFor([&]() {
            return !stat(MountPathO() / "sf0", buffer)
                   && buffer.st_mtime == i;
        }, mDefaultTimeout));

        // Truncate the file.
        ASSERT_FALSE(ftruncate(sf0, 0));
    }
}

TEST_P(FUSEPlatformTests, mkdir_at_fails_when_already_exists)
{
    auto s = open(MountPathW(), O_PATH);
    ASSERT_TRUE(s);

    ASSERT_LT(mkdirat(s, "sd0", 0700), 0);
    ASSERT_EQ(errno, EEXIST);
}

TEST_P(FUSEPlatformTests, mkdir_at_fails_when_below_file)
{
    auto sf0 = open(MountPathW() / "sf0", O_PATH);
    ASSERT_TRUE(sf0);

    ASSERT_LT(mkdirat(sf0, "x", 0700), 0);
    ASSERT_EQ(errno, ENOTDIR);
}

TEST_P(FUSEPlatformTests, mkdir_at_fails_when_read_only)
{
    auto s = open(MountPathR(), O_PATH);
    ASSERT_TRUE(s);

    ASSERT_LT(mkdirat(s, "x", 0700), 0);
    ASSERT_EQ(errno, EROFS);

    ASSERT_LT(accessat(s, "x", F_OK), 0);
    ASSERT_EQ(errno, ENOENT);
}

TEST_P(FUSEPlatformTests, mkdir_at_fails_when_unknown)
{
    auto s = open(MountPathW(), O_PATH);
    ASSERT_TRUE(s);

    ASSERT_LT(mkdirat(s, Path("sdx") / "x", 0700), 0);
    ASSERT_EQ(errno, ENOENT);

    ASSERT_LT(accessat(s, "sdx", F_OK), 0);
    ASSERT_EQ(errno, ENOENT);
}

TEST_P(FUSEPlatformTests, mkdir_at_succeeds)
{
    auto s = open(MountPathW(), O_PATH);
    ASSERT_TRUE(s);

    ASSERT_EQ(mkdirat(s, "sd2", 0700), 0);

    ASSERT_TRUE(waitFor([&]() {
        // Make sure directory exists in the cloud.
        auto info = ClientW()->get("/x/s/sd2");

        // Directory isn't in the cloud.
        if (!info)
            return false;

        // Wrong name or type.
        if (info->mName != "sd2" || !info->mIsDirectory)
            return false;

        // Make sure cache has been invalidated in observer.
        return !access(MountPathO() / "sd2", F_OK);
    }, mDefaultTimeout));
}

TEST_P(FUSEPlatformTests, mkdir_fails_when_already_exists)
{
    ASSERT_LT(mkdir(MountPathW() / "sd0", 0700), 0);
    ASSERT_EQ(errno, EEXIST);
}

TEST_P(FUSEPlatformTests, mkdir_fails_when_below_file)
{
    ASSERT_LT(mkdir(MountPathW() / "sf0" / "x", 0700), 0);
    ASSERT_EQ(errno, ENOTDIR);
}

TEST_P(FUSEPlatformTests, mkdir_fails_when_read_only)
{
    ASSERT_LT(mkdir(MountPathR() / "x", 0700), 0);
    ASSERT_EQ(errno, EROFS);

    ASSERT_LT(access(MountPathR() / "x", F_OK), 0);
    ASSERT_EQ(errno, ENOENT);
}

TEST_P(FUSEPlatformTests, mkdir_fails_when_unknown)
{
    ASSERT_LT(mkdir(MountPathW() / "sdx" / "x", 0700), 0);
    ASSERT_EQ(errno, ENOENT);

    ASSERT_LT(access(MountPathW() / "sdx", F_OK), 0);
    ASSERT_EQ(errno, ENOENT);
}

TEST_P(FUSEPlatformTests, mkdir_succeeds)
{
    ASSERT_EQ(mkdir(MountPathW() / "sd2", 0700), 0);

    ASSERT_TRUE(waitFor([&]() {
        // Make sure directory exists in the cloud.
        auto info = ClientW()->get("/x/s/sd2");

        // Directory isn't in the cloud.
        if (!info)
            return false;

        // Wrong name or type.
        if (info->mName != "sd2" || !info->mIsDirectory)
            return false;

        // Make sure cache has been invalidated in observer.
        return !access(MountPathO() / "sd2", F_OK);
    }, mDefaultTimeout));
}

TEST_P(FUSEPlatformTests, move_local_file_succeeds)
{
    ASSERT_GE(open(MountPathW() / "sfx", O_CREAT | O_TRUNC | O_WRONLY), 0);

    ASSERT_TRUE(waitFor([&]() {
        return !access(MountPathO() / "sfx", F_OK);
    }, mDefaultTimeout));

    Stat sfxo;
    Stat sfxw;

    ASSERT_FALSE(stat(MountPathO() / "sfx", sfxo));
    ASSERT_FALSE(stat(MountPathW() / "sfx", sfxw));

    ASSERT_FALSE(rename(MountPathW() / "sfx", MountPathW() / "sfy"));

    Stat sfy;

    ASSERT_FALSE(stat(MountPathW() / "sfy", sfy));
    ASSERT_EQ(sfxw, sfy);

    ASSERT_TRUE(waitFor([&]() {
        return access(MountPathO() / "sfx", F_OK)
               && errno == ENOENT
               && !stat(MountPathO() / "sfy", sfy)
               && sfxo == sfy;
    }, mDefaultTimeout));
}

TEST_P(FUSEPlatformTests, open_at_create_succeeds)
{
    auto s = open(MountPathW(), O_PATH);
    ASSERT_TRUE(s);

    auto sfx = openat(s, "sfx", O_CREAT | O_WRONLY, 0644);
    ASSERT_TRUE(sfx);

    Stat buffer;

    ASSERT_FALSE(fstat(sfx, buffer));
    ASSERT_EQ(buffer.st_size, 0);

    ASSERT_TRUE(waitFor([&]() {
        return !stat(MountPathO() / "sfx", buffer) && !buffer.st_size;
    }, mDefaultTimeout));

    ASSERT_FALSE(fsync(sfx));

    ASSERT_TRUE(waitFor([&]() {
        auto info = ClientW()->get("/x/s/sfx");
        return info && !info->mIsDirectory && !info->mSize;
    }, mDefaultTimeout));

    ASSERT_FALSE(unlink(MountPathW() / "sfx"));
}

TEST_P(FUSEPlatformTests, open_at_fails_when_below_file)
{
    auto sf0 = open(MountPathW() / "sf0", O_PATH);
    ASSERT_TRUE(sf0);

    ASSERT_FALSE(openat(sf0, "x", O_RDWR));
    ASSERT_EQ(errno, ENOTDIR);
}

TEST_P(FUSEPlatformTests, open_at_fails_when_not_directory)
{
    auto s = open(MountPathW(), O_PATH);
    ASSERT_TRUE(s);

    ASSERT_FALSE(openat(s, "sf0", O_DIRECTORY));
    ASSERT_EQ(errno, ENOTDIR);
}

TEST_P(FUSEPlatformTests, open_at_fails_when_not_file)
{
    auto s = open(MountPathW(), O_PATH);
    ASSERT_TRUE(s);

    ASSERT_FALSE(openat(s, "sd0", O_RDWR));
    ASSERT_EQ(errno, EISDIR);
}

TEST_P(FUSEPlatformTests, open_at_fails_when_read_only)
{
    auto s = open(MountPathR(), O_PATH);
    ASSERT_TRUE(s);

    ASSERT_FALSE(openat(s, "sf0", O_RDWR));
    ASSERT_EQ(errno, EROFS);
}

TEST_P(FUSEPlatformTests, open_at_fails_when_unknown)
{
    auto s = open(MountPathW(), O_PATH);
    ASSERT_TRUE(s);

    ASSERT_FALSE(openat(s, "x", O_RDWR));
    ASSERT_EQ(errno, ENOENT);

    ASSERT_LT(accessat(s, "x", F_OK), 0);
    ASSERT_EQ(errno, ENOENT);
}

TEST_P(FUSEPlatformTests, open_at_succeeds)
{
    auto s = open(MountPathR(), O_PATH);
    ASSERT_TRUE(s);

    // Should be able to open a directory for reading.
    ASSERT_TRUE(openat(s, "sd0", O_RDONLY | O_DIRECTORY));

    // Should be able to open a file for reading.
    ASSERT_TRUE(openat(s, "sf0", O_RDONLY));

    s = open(MountPathW(), O_PATH);
    ASSERT_TRUE(s);

    // Should be able to open a file for read/write.
    ASSERT_TRUE(openat(s, "sf0", O_RDWR));

    // Should be able to open a file for writing.
    ASSERT_TRUE(openat(s, "sf0", O_WRONLY));

    // Should be able to open a file for appended writes.
    ASSERT_TRUE(openat(s, "sf0", O_APPEND | O_RDWR));
}

TEST_P(FUSEPlatformTests, open_at_truncate_succeeds)
{
    auto s = open(MountPathW(), O_PATH);
    ASSERT_TRUE(s);

    auto sf0 = openat(s, "sf0", O_TRUNC | O_WRONLY);
    ASSERT_TRUE(sf0);

    Stat buffer;

    ASSERT_FALSE(fstat(sf0, buffer));
    ASSERT_EQ(buffer.st_size, 0);

    ASSERT_TRUE(waitFor([&]() {
        return !stat(MountPathO() / "sf0", buffer) && !buffer.st_size;
    }, mDefaultTimeout));

    ASSERT_FALSE(fsync(sf0));

    ASSERT_TRUE(waitFor([&]() {
        auto info = ClientW()->get("/x/s/sf0");
        return info && !info->mIsDirectory && !info->mSize;
    }, mDefaultTimeout));
}

TEST_P(FUSEPlatformTests, opendir_fails_when_below_file)
{
    auto iterator = opendir(MountPathW() / "sf0" / "x");
    ASSERT_FALSE(iterator);
    ASSERT_EQ(errno, ENOTDIR);
}

TEST_P(FUSEPlatformTests, opendir_fails_when_not_directory)
{
    auto iterator = opendir(MountPathW() / "sf0");
    ASSERT_FALSE(iterator);
    ASSERT_EQ(errno, ENOTDIR);
}

TEST_P(FUSEPlatformTests, opendir_fails_when_unknown)
{
    auto iterator = opendir(MountPathW() / "x");
    ASSERT_FALSE(iterator);
    ASSERT_EQ(errno, ENOENT);
}

TEST_P(FUSEPlatformTests, opendir_succeeds)
{
    auto iterator = opendir(MountPathW() / "sd0");
    ASSERT_TRUE(iterator);
}

TEST_P(FUSEPlatformTests, open_create_succeeds)
{
    auto sfx = open(MountPathW() / "sfx", O_CREAT | O_WRONLY, 0644);
    ASSERT_TRUE(sfx);

    Stat buffer;

    ASSERT_FALSE(fstat(sfx, buffer));
    ASSERT_EQ(buffer.st_size, 0);

    ASSERT_TRUE(waitFor([&]() {
        return !stat(MountPathO() / "sfx", buffer) && !buffer.st_size;
    }, mDefaultTimeout));

    ASSERT_FALSE(fsync(sfx));

    ASSERT_TRUE(waitFor([&]() {
        auto info = ClientW()->get("/x/s/sfx");
        return info && !info->mIsDirectory && !info->mSize;
    }, mDefaultTimeout));

    ASSERT_FALSE(unlink(MountPathW() / "sfx"));
}

TEST_P(FUSEPlatformTests, open_fails_when_below_file)
{
    ASSERT_FALSE(open(MountPathW() / "sf0" / "x", O_RDWR));
    ASSERT_EQ(errno, ENOTDIR);
}

TEST_P(FUSEPlatformTests, open_fails_when_not_directory)
{
    ASSERT_FALSE(open(MountPathW() / "sf0", O_DIRECTORY));
    ASSERT_EQ(errno, ENOTDIR);
}

TEST_P(FUSEPlatformTests, open_fails_when_not_file)
{
    ASSERT_FALSE(open(MountPathW() / "sd0", O_RDWR));
    ASSERT_EQ(errno, EISDIR);
}

TEST_P(FUSEPlatformTests, open_fails_when_read_only)
{
    ASSERT_FALSE(open(MountPathR() / "sf0", O_RDWR));
    ASSERT_EQ(errno, EROFS);
}

TEST_P(FUSEPlatformTests, open_fails_when_unknown)
{
    ASSERT_FALSE(open(MountPathW() / "x", O_RDWR));
    ASSERT_EQ(errno, ENOENT);

    ASSERT_LT(access(MountPathW() / "x", F_OK), 0);
    ASSERT_EQ(errno, ENOENT);
}

TEST_P(FUSEPlatformTests, open_succeeds)
{
    // Should be able to open a directory for reading.
    ASSERT_TRUE(open(MountPathR() / "sd0", O_RDONLY | O_DIRECTORY));

    // Should be able to open a file for reading.
    ASSERT_TRUE(open(MountPathR() / "sf0", O_RDONLY));

    // Should be able to open a file for read/write.
    ASSERT_TRUE(open(MountPathW() / "sf0", O_RDWR));

    // Should be able to open a file for writing.
    ASSERT_TRUE(open(MountPathW() / "sf0", O_WRONLY));

    // Should be able to open a file for appended writes.
    ASSERT_TRUE(open(MountPathW() / "sf0", O_APPEND | O_RDWR));
}

TEST_P(FUSEPlatformTests, open_truncate_succeeds)
{
    auto sf0 = open(MountPathW() / "sf0", O_TRUNC | O_WRONLY);
    ASSERT_TRUE(sf0);

    Stat buffer;

    ASSERT_FALSE(fstat(sf0, buffer));
    ASSERT_EQ(buffer.st_size, 0);

    ASSERT_TRUE(waitFor([&]() {
        return !stat(MountPathO() / "sf0", buffer) && !buffer.st_size;
    }, mDefaultTimeout));

    ASSERT_FALSE(fsync(sf0));

    ASSERT_TRUE(waitFor([&]() {
        auto info = ClientW()->get("/x/s/sf0");
        return info && !info->mIsDirectory && !info->mSize;
    }, mDefaultTimeout));
}

TEST_P(FUSEPlatformTests, read_fails_when_directory)
{
    auto sd0 = open(MountPathW() / "sd0", O_RDONLY);
    ASSERT_TRUE(sd0);

    char buffer;

    ASSERT_LT(read(sd0.get(), &buffer, sizeof(buffer)), 0);
    ASSERT_EQ(errno, EISDIR);
}

TEST_P(FUSEPlatformTests, read_fails_when_write_only)
{
    auto sf0 = open(MountPathW() / "sf0", O_WRONLY);
    ASSERT_TRUE(sf0);

    char buffer;

    ASSERT_LT(read(sf0.get(), &buffer, sizeof(buffer)), 0);
    ASSERT_EQ(errno, EBADF);
}

TEST_P(FUSEPlatformTests, read_succeeds)
{
    auto sf0 = open(MountPathR() / "sf0", O_RDONLY);
    ASSERT_TRUE(sf0);

    std::string buffer(32, '\0');

    buffer.resize(sf0.read(&buffer[0], buffer.size()));
    
    ASSERT_EQ(buffer.size(), 3u);
    ASSERT_EQ(buffer, "sf0");
}

TEST_P(FUSEPlatformTests, read_write_succeeds)
{
    constexpr auto BYTES_PER_THREAD = 4u;
    constexpr auto NUM_ITERATIONS = 128u;
    constexpr auto NUM_THREADS = 4u;

    auto w = open(MountPathW() / "sfx", O_CREAT | O_WRONLY);
    ASSERT_TRUE(w);

    auto r = open(MountPathW() / "sfx", O_RDONLY);
    ASSERT_TRUE(r);

    // Tells our threads to terminate if there's a mismatch.
    std::atomic<bool> terminate{false};

    // Writes data to w, reads it back on r.
    auto loop = [&](std::size_t id) {
        // Where should this thread write its data?
        auto offset = static_cast<m_off_t>(id * BYTES_PER_THREAD);

        // Write data to w, read it back on r.
        for (auto i = 0u; !terminate && i < NUM_ITERATIONS; ++i)
        {
            // Generate some data to write to w.
            auto written = randomBytes(BYTES_PER_THREAD);

            // Write the data to w.
            w.write(&written[0], written.size(), offset);

            auto read = std::string(written.size(), '\0');

            // Try and read the data back.
            read.resize(r.read(&read[0], read.size(), offset));

            // Terminate if we couldn't read back what we wrote.
            if (read != written)
                terminate = true;
        }
    }; // loop

    std::vector<std::thread> threads;

    // Kick off a bunch of threads.
    for (auto i = 0u; i < NUM_THREADS; ++i)
        threads.emplace_back(std::bind(loop, i));

    // Wait for the threads to terminate.
    while (!threads.empty())
    {
        threads.back().join();
        threads.pop_back();
    }

    ASSERT_FALSE(unlink(MountPathW() / "sfx"));

    // Make sure there were no failures.
    ASSERT_FALSE(terminate);
}

TEST_P(FUSEPlatformTests, readdir_succeeds_when_changing)
{
    auto iterator = opendir(MountPathW());
    ASSERT_TRUE(iterator);

    auto entries = [&]() {
        std::map<std::string, ino_t> entries;

        rewinddir(iterator.get());

        auto* entry = readdir(iterator.get());
        EXPECT_TRUE(entry);

        while (entry)
        {
            entries.emplace(entry->d_name, entry->d_ino);

            errno = 0;
            entry = readdir(iterator.get());
        }

        EXPECT_EQ(errno, 0);

        return entries;
    }; // entries

    auto before = entries();

    ASSERT_EQ(before.size(), 7u);

    {
        auto sf0 = open(MountPathW() / "sf0", O_TRUNC | O_WRONLY);
        ASSERT_TRUE(sf0);
        ASSERT_EQ(fsync(sf0), 0);
    }

    ASSERT_EQ(ClientW()->remove("/x/s/sd0"), API_OK);
    ASSERT_EQ(ClientW()->move("sdx", "/x/s/sd1", "/x/s"), API_OK);
    ASSERT_EQ(ClientW()->move("sf1", "/x/s/sf1", "/x/s/sdx"), API_OK);

    ASSERT_TRUE(waitFor([&]() {
        return access(MountPathW() / "sd0", F_OK)
               && errno == ENOENT
               && !access(MountPathW() / "sdx" / "sf1", F_OK);
    }, mDefaultTimeout));

    auto after = entries();

    ASSERT_EQ(after.size(), 5u);

    ASSERT_EQ(after.count("sdx"), 1u);
    ASSERT_EQ(after.count("sf0"), 1u);

    ASSERT_EQ(after["sdx"], before["sd1"]);
    ASSERT_EQ(after["sf0"], before["sf0"]);
}

TEST_P(FUSEPlatformTests, readdir_succeeds_random_access)
{
    std::vector<struct dirent> entries;
    std::vector<long> indices;

    auto iterator = opendir(MountPathW() / "sd0");
    ASSERT_TRUE(iterator);

    while (true)
    {
        auto index = telldir(iterator.get());

        errno = 0;

        auto* entry = readdir(iterator.get());

        if (!entry)
            break;

        entries.emplace_back(*entry);
        indices.emplace_back(index);
    }

    while (std::next_permutation(indices.begin(), indices.end()))
    {
        for (auto index : indices)
        {
            seekdir(iterator.get(), index);

            auto* entry = readdir(iterator.get());
            ASSERT_TRUE(entry);

            auto index_ = static_cast<std::size_t>(index);

            ASSERT_EQ(entries[index_], *entry);
        }
    }
}

TEST_P(FUSEPlatformTests, readdir_succeeds)
{
    auto sd0 = open(MountPathW() / "sd0", O_RDONLY);
    ASSERT_TRUE(sd0);

    std::map<std::string, Stat> expectations;

    ASSERT_EQ(fstat(sd0, expectations["."]), 0);
    ASSERT_EQ(statat(sd0, "..", expectations[".."]), 0);

    for (const auto& child : ClientW()->childNames("/x/s/sd0"))
        ASSERT_EQ(statat(sd0, child, expectations[child]), 0);

    auto iterator = fdopendir(std::move(sd0));
    ASSERT_TRUE(iterator);

    auto* entry = readdir(iterator.get());
    ASSERT_TRUE(entry);

    while (entry)
    {
        auto e = expectations.find(entry->d_name);
        ASSERT_NE(e, expectations.end());
        ASSERT_EQ(entry->d_ino, e->second.st_ino);

        expectations.erase(e);

        errno = 0;
        entry = readdir(iterator.get());
    }

    ASSERT_EQ(errno, 0);
    ASSERT_TRUE(expectations.empty());
}

TEST_P(FUSEPlatformTests, rename_fails_when_below_file)
{
    ASSERT_TRUE(rename(MountPathW() / "sf0" / "x", MountPathW() / "x"));
    ASSERT_EQ(errno, ENOTDIR);

    ASSERT_TRUE(rename(MountPathW() / "sf0", MountPathW() / "sf1" / "x"));
    ASSERT_EQ(errno, ENOTDIR);

    ASSERT_EQ(access(MountPathW() / "sf0", F_OK), 0);
}

TEST_P(FUSEPlatformTests, rename_fails_when_read_only)
{
    ASSERT_TRUE(rename(MountPathR() / "sf0", MountPathR() / "sfx"));
    ASSERT_EQ(errno, EROFS);

    ASSERT_EQ(access(MountPathR() / "sf0", F_OK), 0);
}

TEST_P(FUSEPlatformTests, rename_fails_when_source_and_target_types_dont_match)
{
    ASSERT_TRUE(rename(MountPathW() / "sf0", MountPathW() / "sd0"));
    ASSERT_EQ(errno, EISDIR);

    ASSERT_FALSE(access(MountPathW() / "sf0", F_OK));

    ASSERT_TRUE(rename(MountPathW() / "sd0", MountPathW() / "sf0"));
    ASSERT_EQ(errno, ENOTDIR);

    ASSERT_FALSE(access(MountPathW() / "sd0", F_OK));
}

TEST_P(FUSEPlatformTests, rename_fails_when_target_directory_is_not_empty)
{
    ASSERT_TRUE(rename(MountPathW() / "sd0", MountPathW() / "sd1"));
    ASSERT_EQ(errno, ENOTEMPTY);
}

TEST_P(FUSEPlatformTests, rename_fails_when_unknown)
{
    ASSERT_TRUE(rename(MountPathW() / "sdx", MountPathW() / "sdy"));
    ASSERT_EQ(errno, ENOENT);
}

TEST_P(FUSEPlatformTests, rename_move_rename_succeeds)
{
    Stat before;

    ASSERT_FALSE(stat(MountPathW() / "sd0", before));

    ASSERT_FALSE(rename(MountPathW() / "sd0",
                        MountPathW() / "sd1" / "sdx"));

    Stat after;

    ASSERT_FALSE(stat(MountPathW() / "sd1" / "sdx", after));
    ASSERT_EQ(after, before);

    ASSERT_TRUE(waitFor([&]() {
        return ClientW()->get("/x/s/sd1/sdx/sd0d0")
               && !access(MountPathO() / "sd1" / "sdx" / "sd0d0", F_OK);
    }, mDefaultTimeout));
}

TEST_P(FUSEPlatformTests, rename_move_succeeds)
{
    Stat before;

    ASSERT_FALSE(stat(MountPathW() / "sd0", before));

    ASSERT_FALSE(rename(MountPathW() / "sd0",
                        MountPathW() / "sd1" / "sd0"));

    Stat after;

    ASSERT_FALSE(stat(MountPathW() / "sd1" / "sd0", after));

    ASSERT_EQ(after, before);

    ASSERT_TRUE(waitFor([&]() {
        return ClientW()->get("/x/s/sd1/sd0/sd0d0")
               && !access(MountPathO() / "sd1" / "sd0" / "sd0d0", F_OK);
    }, mDefaultTimeout));
}

TEST_P(FUSEPlatformTests, rename_rename_succeeds)
{
    Stat before;

    ASSERT_FALSE(stat(MountPathW() / "sf0", before));

    ASSERT_FALSE(rename(MountPathW() / "sf0", MountPathW() / "sfx"));

    Stat after;
    
    ASSERT_FALSE(stat(MountPathW() / "sfx", after));
    ASSERT_EQ(after, before);

    ASSERT_TRUE(waitFor([&]() {
        return !ClientW()->get("/x/s/sf0")
               && !access(MountPathO() / "sfx", F_OK);
    }, mDefaultTimeout));
}

TEST_P(FUSEPlatformTests, rename_replace_directory_succeeds)
{
    Stat before;

    ASSERT_FALSE(stat(MountPathW() / "sd0", before));

    ASSERT_FALSE(rename(MountPathW() / "sd0",
                        MountPathW() / "sd1" / "sd1d0"));

    Stat after;

    ASSERT_FALSE(stat(MountPathW() / "sd1" / "sd1d0", after));
    ASSERT_EQ(after, before);

    EXPECT_TRUE(waitFor([&]() {
        return ClientW()->get("/x/s/sd1/sd1d0/sd0d0")
               && !access(MountPathW() / "sd1" / "sd1d0" / "sd0d0", F_OK);
    }, mDefaultTimeout));
}

TEST_P(FUSEPlatformTests, rename_replace_file_cloud_local_succeeds)
{
    Stat sf0o;
    Stat sf0w;

    ASSERT_FALSE(stat(MountPathO() / "sf0", sf0o));
    ASSERT_FALSE(stat(MountPathW() / "sf0", sf0w));

    ASSERT_GE(open(MountPathW() / "sfx", O_CREAT | O_TRUNC | O_WRONLY), 0);

    ASSERT_FALSE(rename(MountPathW() / "sf0", MountPathW() / "sfx"));

    Stat sfx;

    ASSERT_TRUE(access(MountPathW() / "sf0", F_OK));
    ASSERT_EQ(errno, ENOENT);

    ASSERT_FALSE(stat(MountPathW() / "sfx", sfx));
    ASSERT_EQ(sf0w, sfx);

    EXPECT_TRUE(waitFor([&]() {
        if (ClientW()->get("/x/s/sf0"))
            return false;

        if (!ClientW()->get("/x/s/sfx"))
            return false;

        return access(MountPathO() / "sf0", F_OK)
               && errno == ENOENT
               && !stat(MountPathO() / "sfx", sfx)
               && sf0o == sfx;
    }, mDefaultTimeout));
}

TEST_P(FUSEPlatformTests, rename_replace_file_local_cloud_succeeds)
{
    ASSERT_GE(open(MountPathW() / "sfx", O_CREAT | O_TRUNC | O_WRONLY), 0);

    ASSERT_TRUE(waitFor([&]() {
        return !access(MountPathO() / "sfx", F_OK);
    }, mDefaultTimeout));

    Stat sfxo;
    Stat sfxw;

    ASSERT_FALSE(stat(MountPathO() / "sfx", sfxo));
    ASSERT_FALSE(stat(MountPathW() / "sfx", sfxw));

    ASSERT_FALSE(rename(MountPathW() / "sfx", MountPathW() / "sf0"));

    Stat sf0;

    ASSERT_FALSE(stat(MountPathW() / "sf0", sf0));
    ASSERT_EQ(sfxw, sf0);

    ASSERT_TRUE(waitFor([&]() {
        return !ClientW()->get("/x/s/sf0")
               && access(MountPathO() / "sfx", F_OK)
               && errno == ENOENT
               && !stat(MountPathO() / "sf0", sf0)
               && sfxo == sf0;
    }, mDefaultTimeout));

    ASSERT_FALSE(unlink(MountPathW() / "sf0"));
}

TEST_P(FUSEPlatformTests, rename_replace_file_local_local_succeeds)
{
    ASSERT_GE(open(MountPathW() / "sfx", O_CREAT | O_TRUNC | O_WRONLY), 0);
    ASSERT_GE(open(MountPathW() / "sfy", O_CREAT | O_TRUNC | O_WRONLY), 0);

    ASSERT_TRUE(waitFor([&]() {
        return !access(MountPathO() / "sfx", F_OK)
               && !access(MountPathO() / "sfy", F_OK);
    }, mDefaultTimeout));

    Stat sfxo;
    Stat sfxw;

    ASSERT_FALSE(stat(MountPathO() / "sfx", sfxo));
    ASSERT_FALSE(stat(MountPathW() / "sfx", sfxw));

    ASSERT_FALSE(rename(MountPathW() / "sfx", MountPathW() / "sfy"));

    Stat sfy;

    ASSERT_FALSE(stat(MountPathW() / "sfy", sfy));
    ASSERT_EQ(sfxw, sfy);

    ASSERT_TRUE(waitFor([&]() {
        return access(MountPathO() / "sfx", F_OK)
               && errno == ENOENT
               && !stat(MountPathO() / "sfy", sfy)
               && sfxo == sfy;
    }, mDefaultTimeout));

    ASSERT_FALSE(unlink(MountPathW() / "sfy"));
}

TEST_P(FUSEPlatformTests, rename_replace_file_succeeds)
{
    Stat beforeO;
    Stat beforeW;

    ASSERT_FALSE(stat(MountPathO() / "sf0", beforeO));
    ASSERT_FALSE(stat(MountPathW() / "sf0", beforeW));

    ASSERT_FALSE(rename(MountPathW() / "sf0",
                        MountPathW() / "sd0" / "sd0f0"));

    Stat after;

    ASSERT_FALSE(stat(MountPathW() / "sd0" / "sd0f0", after));
    ASSERT_EQ(after, beforeW);

    EXPECT_TRUE(waitFor([&]() {
        if (ClientW()->get("/x/s/sf0"))
            return false;

        if (!access(MountPathO() / "sf0", F_OK))
            return false;

        return !stat(MountPathO() / "sd0" / "sd0f0", after)
               && after == beforeO;
    }, mDefaultTimeout));
}

TEST_P(FUSEPlatformTests, rmdir_fails_when_below_file)
{
    ASSERT_LT(rmdir(MountPathW() / "sf0" / "x"), 0);
    ASSERT_EQ(errno, ENOTDIR);

    ASSERT_EQ(access(MountPathW() / "sf0", F_OK), 0);
}

TEST_P(FUSEPlatformTests, rmdir_fails_when_file)
{
    ASSERT_LT(rmdir(MountPathW() / "sf0"), 0);
    ASSERT_EQ(errno, ENOTDIR);

    ASSERT_EQ(access(MountPathW() / "sf0", F_OK), 0);
}

TEST_P(FUSEPlatformTests, rmdir_fails_when_not_empty)
{
    ASSERT_LT(rmdir(MountPathW() / "sd0"), 0);
    ASSERT_EQ(errno, ENOTEMPTY);

    ASSERT_EQ(access(MountPathW() / "sd0" / "sd0d0", F_OK), 0);
}

TEST_P(FUSEPlatformTests, rmdir_fails_when_read_only)
{
    ASSERT_LT(rmdir(MountPathR() / "sd0"), 0);
    ASSERT_EQ(errno, EROFS);

    ASSERT_EQ(access(MountPathR() / "sd0" / "sd0d0", F_OK), 0);
}

TEST_P(FUSEPlatformTests, rmdir_fails_when_unknown)
{
    ASSERT_LT(rmdir(MountPathW() / "sdx"), 0);
    ASSERT_EQ(errno, ENOENT);
}

TEST_P(FUSEPlatformTests, rmdir_succeeds)
{
    ASSERT_EQ(rmdir(MountPathW() / "sd0" / "sd0d0"), 0);

    ASSERT_TRUE(waitFor([&]() {
        auto info = ClientW()->get("/x/s/sd0/sd0d0");

        // Directory should no longer be visible in the cloud.
        if (info.errorOr(API_OK) != API_ENOENT)
            return false;

        // Directory should no longer be visible to observer.
        return access(MountPathW() / "sd0" / "sd0d0", F_OK) < 0
               && errno == ENOENT;
    }, mDefaultTimeout));
}

TEST_P(FUSEPlatformTests, stat_at_fails_when_below_file)
{
    Stat buffer;

    auto sf0 = open(MountPathW() / "sf0", O_PATH);
    ASSERT_TRUE(sf0);

    ASSERT_LT(statat(sf0, "x", buffer), 0);
    ASSERT_EQ(errno, ENOTDIR);
}

TEST_P(FUSEPlatformTests, stat_at_fails_when_unknown)
{
    Stat buffer;

    auto s = open(MountPathW(), O_PATH);
    ASSERT_TRUE(s);

    ASSERT_LT(statat(s, "x", buffer), 0);
    ASSERT_EQ(errno, ENOENT);
}

TEST_P(FUSEPlatformTests, stat_at_succeeds)
{
    auto buffer = Stat();

    auto s = open(MountPathW(), O_PATH);
    ASSERT_TRUE(s);

    auto info = ClientW()->get("/x/s/sd0");
    ASSERT_TRUE(info);
    ASSERT_EQ(statat(s, "sd0", buffer), 0);
    EXPECT_EQ(buffer, *info);

    info = ClientW()->get("/x/s/sf0");
    ASSERT_TRUE(info);
    ASSERT_EQ(statat(s, "sf0", buffer), 0);
    EXPECT_EQ(buffer, *info);
}

TEST_P(FUSEPlatformTests, stat_fails_when_below_file)
{
    Stat buffer;

    ASSERT_LT(stat(MountPathW() / "sf0" / "x", buffer), 0);
    ASSERT_EQ(errno, ENOTDIR);
}

TEST_P(FUSEPlatformTests, stat_fails_when_unknown)
{
    Stat buffer;

    ASSERT_LT(stat(MountPathW() / "x", buffer), 0);
    ASSERT_EQ(errno, ENOENT);
}

TEST_P(FUSEPlatformTests, stat_succeeds)
{
    auto buffer = Stat();

    auto info = ClientW()->get("/x/s/sd0");
    ASSERT_TRUE(info);
    ASSERT_EQ(stat(MountPathW() / "sd0", buffer), 0);
    EXPECT_EQ(buffer, *info);

    info = ClientW()->get("/x/s/sf0");
    ASSERT_TRUE(info);
    ASSERT_EQ(stat(MountPathW() / "sf0", buffer), 0);
    EXPECT_EQ(buffer, *info);
}

TEST_P(FUSEPlatformTests, statvfs_fails_when_below_file)
{
    struct statvfs buffer;

    ASSERT_NE(statvfs(MountPathW() / "sf0" / "bogus", buffer), 0);
    ASSERT_EQ(errno, ENOTDIR);
}

TEST_P(FUSEPlatformTests, statvfs_fails_when_unknown)
{
    struct statvfs buffer;

    ASSERT_NE(statvfs(MountPathW() / "bogus", buffer), 0);
    ASSERT_EQ(errno, ENOENT);
}

TEST_P(FUSEPlatformTests, statvfs_succeeds)
{
    struct statvfs buffer;

    ASSERT_EQ(statvfs(MountPathW() / "sf0", buffer), 0);

    EXPECT_EQ(buffer.f_bsize,   BlockSize);
    EXPECT_EQ(buffer.f_namemax, MaxNameLength);

    // The MEGA API doesn't allow us to query how much storage space one of
    // our contact is using so testing the fields below is not meaningfulfor
    // shares.
    if (isShareTest())
        return;

    auto info = ClientW()->storageInfo();

    ASSERT_EQ(info.errorOr(API_OK), API_OK);

    auto available = static_cast<fsblkcnt_t>(info->mAvailable) / BlockSize;

    EXPECT_EQ(buffer.f_bavail,  available);
    EXPECT_EQ(buffer.f_bfree,   buffer.f_bavail);

    LINUX_ONLY({
        auto capacity = static_cast<fsblkcnt_t>(info->mCapacity) / BlockSize;

        EXPECT_EQ(buffer.f_blocks, capacity);
        EXPECT_EQ(buffer.f_frsize, buffer.f_bsize);
    })
}

TEST_P(FUSEPlatformTests, truncate_fails_when_below_file)
{
    ASSERT_TRUE(truncate(MountPathW() / "sf0" / "x", 0));
    ASSERT_EQ(errno, ENOTDIR);
}

TEST_P(FUSEPlatformTests, truncate_fails_when_directory)
{
    ASSERT_TRUE(truncate(MountPathW() / "sd0", 0));
    ASSERT_EQ(errno, EISDIR);

    ASSERT_FALSE(access(MountPathW() / "sd0", F_OK));
}

TEST_P(FUSEPlatformTests, truncate_fails_when_read_only)
{
    ASSERT_TRUE(truncate(MountPathR() / "sf0", 0));
    ASSERT_EQ(errno, EROFS);

    Stat buffer;

    ASSERT_FALSE(stat(MountPathR() / "sf0", buffer));
    ASSERT_NE(buffer.st_size, 0);
}

TEST_P(FUSEPlatformTests, truncate_fails_when_unknown)
{
    ASSERT_TRUE(truncate(MountPathW() / "sfx", 0));
    ASSERT_EQ(errno, ENOENT);
}

TEST_P(FUSEPlatformTests, truncate_succeeds)
{
    ASSERT_FALSE(truncate(MountPathW() / "sf0", 0));
    ASSERT_FALSE(truncate(MountPathW() / "sf1", 32));

    ASSERT_TRUE(waitFor([&]() {
        Stat buffer;

        return !stat(MountPathO() / "sf0", buffer)
               && !buffer.st_size
               && !stat(MountPathO() / "sf1", buffer)
               && buffer.st_size == 32;
    }, mDefaultTimeout));

    auto descriptor = open(MountPathW() / "sf0", O_RDONLY);
    ASSERT_FALSE(fsync(descriptor));

    descriptor = open(MountPathW() / "sf1", O_RDONLY);
    ASSERT_FALSE(fsync(descriptor));

    EXPECT_TRUE(waitFor([&]() {
        auto sf0 = ClientW()->get("/x/s/sf0");
        auto sf1 = ClientW()->get("/x/s/sf1");

        return (sf0 && !sf0->mIsDirectory && !sf0->mSize)
               && (sf1 && !sf1->mIsDirectory && sf1->mSize == 32);
    }, mDefaultTimeout));

    auto sf0 = ClientW()->get("/x/s/sf0");
    auto sf1 = ClientW()->get("/x/s/sf1");

    ASSERT_TRUE(sf0);
    ASSERT_TRUE(sf1);

    EXPECT_FALSE(sf0->mIsDirectory);
    EXPECT_FALSE(sf1->mIsDirectory);
    EXPECT_EQ(sf0->mSize, 0);
    EXPECT_EQ(sf1->mSize, 32);
}

TEST_P(FUSEPlatformTests, unlink_at_fails_when_below_file)
{
    auto sf0 = open(MountPathW() / "sf0", O_PATH);
    ASSERT_TRUE(sf0);

    ASSERT_LT(unlinkat(sf0, "x", 0), 0);
    ASSERT_EQ(errno, ENOTDIR);
    
    ASSERT_EQ(access(MountPathW() / "sf0", F_OK), 0);
}

TEST_P(FUSEPlatformTests, unlink_at_fails_when_directory)
{
    auto s = open(MountPathW(), O_PATH);
    ASSERT_TRUE(s);

    ASSERT_LT(unlinkat(s, "sd0", 0), 0);
    ASSERT_EQ(errno, LINUX_OR_POSIX(EISDIR, EPERM));
    
    ASSERT_EQ(access(MountPathW() / "sd0", F_OK), 0);
}

TEST_P(FUSEPlatformTests, unlink_at_fails_when_read_only)
{
    auto s = open(MountPathR(), O_PATH);
    ASSERT_TRUE(s);

    ASSERT_LT(unlinkat(s, "sf0", 0), 0);
    ASSERT_EQ(errno, EROFS);
    
    ASSERT_EQ(access(MountPathR() / "sf0", F_OK), 0);
}

TEST_P(FUSEPlatformTests, unlink_at_fails_when_unknown)
{
    auto s = open(MountPathW(), O_PATH);
    ASSERT_TRUE(s);

    ASSERT_LT(unlinkat(s, "sdx", 0), 0);
    ASSERT_EQ(errno, ENOENT);

    ASSERT_LT(accessat(s, "sdx", F_OK), 0);
    ASSERT_EQ(errno, ENOENT);
}

TEST_P(FUSEPlatformTests, unlink_at_directory_succeeds)
{
    auto sd0 = open(MountPathW() / "sd0", O_PATH);
    ASSERT_TRUE(sd0);

    ASSERT_EQ(unlinkat(sd0, "sd0d0", AT_REMOVEDIR), 0);

    ASSERT_TRUE(waitFor([&]() {
        // File should no longer be visible in the cloud.
        auto info = ClientW()->get("/x/s/sd0/sd0d0");

        // File's still visible in the cloud.
        if (info.errorOr(API_OK) != API_ENOENT)
            return false;

        // File should no longer be visible by observer.
        return accessat(sd0, "sd0d0", F_OK) < 0
               && errno == ENOENT;
    }, mDefaultTimeout));
}

TEST_P(FUSEPlatformTests, unlink_at_file_succeeds)
{
    auto s = open(MountPathW(), O_PATH);
    ASSERT_TRUE(s);

    ASSERT_EQ(unlinkat(s, "sf0", 0), 0);

    ASSERT_TRUE(waitFor([&]() {
        // File should no longer be visible in the cloud.
        auto info = ClientW()->get("/x/s/sf0");

        // File's still visible in the cloud.
        if (info.errorOr(API_OK) != API_ENOENT)
            return false;

        // File should no longer be visible by observer.
        return accessat(s, "sf0", F_OK) < 0
               && errno == ENOENT;
    }, mDefaultTimeout));
}

TEST_P(FUSEPlatformTests, unlink_fails_when_below_file)
{
    ASSERT_LT(unlink(MountPathW() / "sf0" / "x"), 0);
    ASSERT_EQ(errno, ENOTDIR);
    
    ASSERT_EQ(access(MountPathW() / "sf0", F_OK), 0);
}

TEST_P(FUSEPlatformTests, unlink_fails_when_directory)
{
    ASSERT_LT(unlink(MountPathW() / "sd0"), 0);
    ASSERT_EQ(errno, LINUX_OR_POSIX(EISDIR, EPERM));
    
    ASSERT_EQ(access(MountPathW() / "sd0", F_OK), 0);
}

TEST_P(FUSEPlatformTests, unlink_fails_when_read_only)
{
    ASSERT_LT(unlink(MountPathR() / "sf0"), 0);
    ASSERT_EQ(errno, EROFS);
    
    ASSERT_EQ(access(MountPathR() / "sf0", F_OK), 0);
}

TEST_P(FUSEPlatformTests, unlink_fails_when_unknown)
{
    ASSERT_LT(unlink(MountPathW() / "sdx"), 0);
    ASSERT_EQ(errno, ENOENT);

    ASSERT_LT(access(MountPathW() / "sdx", F_OK), 0);
    ASSERT_EQ(errno, ENOENT);
}

TEST_P(FUSEPlatformTests, unlink_succeeds)
{
    ASSERT_EQ(unlink(MountPathW() / "sf0"), 0);

    ASSERT_TRUE(waitFor([&]() {
        // File should no longer be visible in the cloud.
        auto info = ClientW()->get("/x/s/sf0");

        // File's still visible in the cloud.
        if (info.errorOr(API_OK) != API_ENOENT)
            return false;

        // File should no longer be visible by observer.
        return access(MountPathW() / "sf0", F_OK) < 0
               && errno == ENOENT;
    }, mDefaultTimeout));
}

TEST_P(FUSEPlatformTests, write_fails_when_read_only)
{
    auto sf0 = open(MountPathW() / "sf0", O_RDONLY);
    ASSERT_TRUE(sf0);

    ASSERT_LT(write(sf0.get(), "", 1), 0);
    ASSERT_EQ(errno, EBADF);
}

TEST_P(FUSEPlatformTests, write_succeeds)
{
    auto sfx = open(MountPathW() / "sfx", O_CREAT | O_RDWR);
    ASSERT_TRUE(sfx);

    ASSERT_TRUE(waitFor([&]() {
        return !access(MountPathW() / "sfx", F_OK);
    }, mDefaultTimeout));

    auto written = randomBytes(32);

    ASSERT_EQ(sfx.write(&written[0], written.size()), written.size());

    auto read = std::string(written.size(), '\0');

    read.resize(sfx.read(&read[0], read.size(), 0));
    ASSERT_EQ(read, written);

    auto sfxO = open(MountPathO() / "sfx", O_RDONLY);
    ASSERT_TRUE(sfxO);

    written = randomBytes(64);

    ASSERT_EQ(sfx.write(&written[0], written.size(), 0), written.size());

    read.resize(written.size());

    ASSERT_TRUE(waitFor([&]() {
        return sfxO.read(&read[0], read.size(), 0) == read.size()
               && read == written;
    }, mDefaultTimeout));

    ASSERT_FALSE(unlink(MountPathW() / "sfx"));
}

} // testing
} // fuse
} // mega

