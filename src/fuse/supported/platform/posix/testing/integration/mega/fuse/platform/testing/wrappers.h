#pragma once

#include <mega/common/testing/path_forward.h>
#include <mega/fuse/common/testing/utility.h>
#include <mega/fuse/platform/file_descriptor_forward.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>

#include <dirent.h>
#include <memory>
#include <ostream>
#include <string>

// Convenience.
struct DirectoryDeleter
{
    void operator()(DIR* directory)
    {
        if (directory)
            closedir(directory);
    }
}; // DirectoryDeleter

using DirectoryIterator = std::unique_ptr<DIR, DirectoryDeleter>;
using Stat = struct stat;

bool operator==(const struct dirent& lhs, const struct dirent& rhs);
bool operator!=(const struct dirent& lhs, const struct dirent& rhs);

bool operator==(const Stat& lhs, const Stat& rhs);
bool operator!=(const Stat& lhs, const Stat& rhs);

template<typename T>
auto operator==(const Stat& lhs, const T& rhs) ->
    typename mega::fuse::testing::EnableIfInfoLike<T, bool>::type
{
    return rhs == lhs;
}

template<typename T>
auto operator!=(const Stat& lhs, const T& rhs) ->
    typename mega::fuse::testing::EnableIfInfoLike<T, bool>::type
{
    return rhs != lhs;
}

namespace mega
{
namespace common
{

bool operator==(const NodeInfo& lhs, const Stat& rhs);
bool operator!=(const NodeInfo& lhs, const Stat& rhs);

} // common

namespace fuse
{

template<typename T>
auto operator==(const T& lhs, const Stat& rhs) -> typename testing::EnableIfInfoLike<T, bool>::type;

template<typename T>
auto operator!=(const T& lhs, const Stat& rhs) -> typename testing::EnableIfInfoLike<T, bool>::type
{
    return !(lhs == rhs);
}

namespace testing
{

int access(const common::testing::Path& path, int mode);

int accessat(const platform::FileDescriptor& descriptor,
             const common::testing::Path& path,
             int mode);

DirectoryIterator fdopendir(platform::FileDescriptor descriptor);

bool flushFile(const common::testing::Path& path);

int fstat(const platform::FileDescriptor& descriptor, Stat& buffer);

int fsync(const platform::FileDescriptor& descriptor);

int ftruncate(const platform::FileDescriptor& descriptor, off_t length);

int futimes(const platform::FileDescriptor& descriptor, struct timeval (&times)[2]);

int mkdir(const common::testing::Path& path, mode_t mode);

int mkdirat(const platform::FileDescriptor& descriptor,
            const common::testing::Path& path,
            mode_t mode);

platform::FileDescriptor open(const common::testing::Path& path, int flags);

platform::FileDescriptor open(const common::testing::Path& path, int flags, mode_t mode);

platform::FileDescriptor openat(const platform::FileDescriptor& descriptor,
                                const common::testing::Path& path,
                                int flags);

platform::FileDescriptor openat(const platform::FileDescriptor& descriptor,
                                const common::testing::Path& path,
                                int flags,
                                mode_t mode);

DirectoryIterator opendir(const common::testing::Path& path);

int rename(const common::testing::Path& before, const common::testing::Path& after);

int renameat(const platform::FileDescriptor& sourceParent,
             const common::testing::Path& sourcePath,
             const platform::FileDescriptor& targetParent,
             const common::testing::Path& targetPath);

int rmdir(const common::testing::Path& path);

int stat(const common::testing::Path& path, Stat& buffer);

int statat(const platform::FileDescriptor& descriptor,
           const common::testing::Path& path,
           Stat& buffer);

int statvfs(const common::testing::Path& path, struct statvfs& buffer);

int truncate(const common::testing::Path& path, off_t length);

int unlink(const common::testing::Path& path);

int unlinkat(const platform::FileDescriptor& descriptor,
             const common::testing::Path& path,
             int flags);

} // testing
} // fuse
} // mega
