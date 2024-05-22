#include <fcntl.h>
#include <unistd.h>

#include <cstring>

#include <mega/fuse/common/constants.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/node_info.h>
#include <mega/fuse/common/testing/path.h>
#include <mega/fuse/platform/file_descriptor.h>
#include <mega/fuse/platform/platform.h>
#include <mega/fuse/platform/testing/wrappers.h>

bool operator==(const struct dirent& lhs, const struct dirent& rhs)
{
    return lhs.d_ino == rhs.d_ino
           && !std::strcmp(lhs.d_name, rhs.d_name);
}

bool operator!=(const struct dirent& lhs, const struct dirent& rhs)
{
    return !(lhs == rhs);
}

bool operator==(const Stat& lhs, const Stat& rhs)
{
    return !std::memcmp(&lhs, &rhs, sizeof(lhs));
}

bool operator!=(const Stat& lhs, const Stat& rhs)
{
    return !(lhs == rhs);
}

namespace mega
{
namespace fuse
{

// Convenience.
using testing::EnableIfInfoLike;
using testing::id;
using testing::toUint64;

template<typename T>
auto operator==(const T& lhs, const Stat& rhs)
  -> typename EnableIfInfoLike<T, bool>::type
{
    mode_t permissions = S_IRUSR;
    mode_t type = S_IFREG;

    if (lhs.mIsDirectory)
    {
        permissions |= S_IXUSR;
        type = S_IFDIR;
    }

    if (lhs.mPermissions == FULL)
        permissions |= S_IWUSR;

    auto size = std::max<off_t>(lhs.mSize, BlockSize);
    auto blocks = (size + 511) / 512;

    constexpr auto mask = S_IRWXG | S_IRWXO | S_IRWXU;

    return rhs.st_blocks == blocks
           && LINUX_OR_POSIX(rhs.st_blksize == BlockSize, true)
           && rhs.st_gid == getgid()
           && rhs.st_ino == toUint64(id(lhs))
           && (rhs.st_mode & mask) == permissions
           && (rhs.st_mode & S_IFMT) == type
           && rhs.st_mtime == lhs.mModified
           && rhs.st_uid == getuid();
}

template bool operator==(const InodeInfo&, const Stat&);
template bool operator==(const NodeInfo&, const Stat&);

namespace testing
{

using namespace platform;

int access(const Path& path, int mode)
{
    return ::access(path.string().c_str(), mode);
}

int accessat(const FileDescriptor& descriptor,
             const Path& path,
             int mode)
{
    return faccessat(descriptor.get(),
                     path.string().c_str(),
                     mode,
                     0);
}

DirectoryIterator fdopendir(FileDescriptor descriptor)
{
    auto* iterator = ::fdopendir(descriptor.get());

    if (iterator)
        descriptor.release();

    return DirectoryIterator(iterator);
}

int fstat(const FileDescriptor& descriptor, Stat& buffer)
{
    return ::fstat(descriptor.get(), &buffer);
}

int fstatvfs(const platform::FileDescriptor& descriptor,
             struct statvfs& buffer)
{
    return ::fstatvfs(descriptor.get(), &buffer);
}

int fsync(const FileDescriptor& descriptor)
{
    return ::fsync(descriptor.get());
}

int ftruncate(const FileDescriptor& descriptor, off_t length)
{
    return ::ftruncate(descriptor.get(), length);
}

int futimes(const FileDescriptor& descriptor, struct timeval (&times)[2])
{
    return ::futimes(descriptor.get(), times);
}

int mkdir(const Path& path, mode_t mode)
{
    return ::mkdir(path.string().c_str(), mode);
}

int mkdirat(const FileDescriptor& descriptor,
            const Path& path,
            mode_t mode)
{
    return ::mkdirat(descriptor.get(),
                     path.string().c_str(),
                     mode);
}

FileDescriptor open(const Path& path, int flags)
{
    auto descriptor = ::open(path.string().c_str(), flags);

    return FileDescriptor(descriptor);
}

FileDescriptor open(const Path& path, int flags, mode_t mode)
{
    auto descriptor = ::open(path.string().c_str(), flags, mode);

    return FileDescriptor(descriptor);
}

FileDescriptor openat(const FileDescriptor& descriptor,
                      const Path& path,
                      int flags)
{
    auto descriptor_ = ::openat(descriptor.get(),
                                path.string().c_str(),
                                flags);

    return FileDescriptor(descriptor_);
}

FileDescriptor openat(const FileDescriptor& descriptor,
                      const Path& path,
                      int flags,
                      mode_t mode)
{
    auto descriptor_ = ::openat(descriptor.get(),
                                path.string().c_str(),
                                flags,
                                mode);

    return FileDescriptor(descriptor_);
}

DirectoryIterator opendir(const Path& path)
{
    return DirectoryIterator(::opendir(path.string().c_str()));
}

int rename(const Path& before, const Path& after)
{
    return ::rename(before.string().c_str(), after.string().c_str());
}

int renameat(const FileDescriptor& sourceParent,
             const Path& sourcePath,
             const FileDescriptor& targetParent,
             const Path& targetPath)
{
    return ::renameat(sourceParent.get(),
                      sourcePath.string().c_str(),
                      targetParent.get(),
                      targetPath.string().c_str());
}

int rmdir(const Path& path)
{
    return ::rmdir(path.string().c_str());
}

int stat(const Path& path, Stat& buffer)
{
    return ::stat(path.string().c_str(), &buffer);
}

int statat(const platform::FileDescriptor& descriptor,
           const Path& path,
           Stat& buffer)
{
    return ::fstatat(descriptor.get(),
                     path.string().c_str(),
                     &buffer,
                     0);
}

int statvfs(const Path& path, struct statvfs& buffer)
{
    return ::statvfs(path.string().c_str(), &buffer);
}

int truncate(const Path& path, off_t length)
{
    return ::truncate(path.string().c_str(), length);
}

int unlink(const Path& path)
{
    return ::unlink(path.string().c_str());
}

int unlinkat(const FileDescriptor& descriptor,
             const Path& path,
             int flags)
{
    return ::unlinkat(descriptor.get(),
                      path.string().c_str(),
                      flags);
}

} // testing
} // fuse
} // mega

