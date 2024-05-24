#pragma once

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>

#include <dirent.h>

#include <memory>
#include <ostream>
#include <string>

#include <mega/fuse/common/testing/path_forward.h>
#include <mega/fuse/common/testing/utility.h>
#include <mega/fuse/platform/file_descriptor_forward.h>

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
auto operator==(const Stat& lhs, const T& rhs)
  -> typename mega::fuse::testing::EnableIfInfoLike<T, bool>::type
{
    return rhs == lhs;
}

template<typename T>
auto operator!=(const Stat& lhs, const T& rhs)
  -> typename mega::fuse::testing::EnableIfInfoLike<T, bool>::type
{
    return rhs != lhs;
}

namespace mega
{
namespace fuse
{

template<typename T>
auto operator==(const T& lhs, const Stat& rhs)
  -> typename testing::EnableIfInfoLike<T, bool>::type;

template<typename T>
auto operator!=(const T& lhs, const Stat& rhs)
  -> typename testing::EnableIfInfoLike<T, bool>::type
{
    return !(lhs == rhs);
}

namespace testing
{

int access(const Path& path, int mode);

int accessat(const platform::FileDescriptor& descriptor,
             const Path& path,
             int mode);

DirectoryIterator fdopendir(platform::FileDescriptor descriptor);

int fstat(const platform::FileDescriptor& descriptor, Stat& buffer);

int fsync(const platform::FileDescriptor& descriptor);

int ftruncate(const platform::FileDescriptor& descriptor, off_t length);

int futimes(const platform::FileDescriptor& descriptor,
            struct timeval (&times)[2]);

int mkdir(const Path& path, mode_t mode);

int mkdirat(const platform::FileDescriptor& descriptor,
            const Path& path,
            mode_t mode);

platform::FileDescriptor open(const Path& path, int flags);

platform::FileDescriptor open(const Path& path,
                              int flags,
                              mode_t mode);

platform::FileDescriptor openat(const platform::FileDescriptor& descriptor,
                                const Path& path,
                                int flags);

platform::FileDescriptor openat(const platform::FileDescriptor& descriptor,
                                const Path& path,
                                int flags,
                                mode_t mode);

DirectoryIterator opendir(const Path& path);

int rename(const Path& before, const Path& after);

int renameat(const platform::FileDescriptor& sourceParent,
             const Path& sourcePath,
             const platform::FileDescriptor& targetParent,
             const Path& targetPath);

int rmdir(const Path& path);

int stat(const Path& path, Stat& buffer);

int statat(const platform::FileDescriptor& descriptor,
           const Path& path,
           Stat& buffer);

int statvfs(const Path& path, struct statvfs& buffer);

int truncate(const Path& path, off_t length);

int unlink(const Path& path);

int unlinkat(const platform::FileDescriptor& descriptor,
             const Path& path,
             int flags);

} // testing
} // fuse
} // mega

