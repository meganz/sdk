#include <unistd.h>

#include <cerrno>
#include <cstring>

#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/mount_inode_id.h>
#include <mega/fuse/common/utility.h>
#include <mega/fuse/platform/constants.h>
#include <mega/fuse/platform/file_descriptor.h>
#include <mega/fuse/platform/utility.h>

namespace mega
{
namespace fuse
{
namespace platform
{

FileDescriptorPair pipe(bool closeReaderOnFork,
                        bool closeWriterOnFork)
{
    int descriptors[2];

    // Try and create our pipe.
    auto result = ::pipe(descriptors);
    
    // Couldn't create the pipe.
    if (result < 0)
        throw FUSEErrorF("Unable to create pipe: %s",
                         std::strerror(errno));

    FileDescriptor r(descriptors[0], closeReaderOnFork);
    FileDescriptor w(descriptors[1], closeWriterOnFork);

    // Return pipe to caller.
    return std::make_pair(std::move(r), std::move(w));
}

void translate(struct stat& attributes,
               MountInodeID id,
               const InodeInfo& info)
{
    std::memset(&attributes, 0, sizeof(attributes));

    // Raw permissions for this entity.
    //
    // Directories use these permissions verbatim.
    // Files mask these permission as they are never executable.
    const mode_t permissions = info.mPermissions == FULL ? 0700 : 0500;

    attributes.st_atime = static_cast<time_t>(info.mModified);
    attributes.st_blksize = BlockSize;
    attributes.st_ctime = attributes.st_atime;
    attributes.st_gid = getgid();
    attributes.st_ino = id.get();
    attributes.st_mode = S_IFDIR | permissions;
    attributes.st_mtime = attributes.st_atime;
    attributes.st_nlink = 1;
    attributes.st_size = static_cast<off_t>(info.mSize);
    attributes.st_uid = getuid();

    // All files require at least a single "block."
    auto size = std::max<off_t>(BlockSize, attributes.st_size);

    // Always align size to a 512B boundary.
    attributes.st_blocks = (size + 511) / 512;

    if (info.mIsDirectory)
        return;

    // Files are never executable.
    attributes.st_mode = S_IFREG | (permissions & 0600);
}

void translate(fuse_entry_param& entry,
               MountInodeID id,
               const InodeInfo& info)
{
    entry.attr_timeout = AttributeTimeout;
    entry.entry_timeout = EntryTimeout;
    entry.generation = 0;
    entry.ino = id.get();

    translate(entry.attr, id, info);
}

int translate(Error result)
{
    switch (result)
    {
    case API_EACCESS:
        return EROFS;
    case API_EEXIST:
        return EEXIST;
    case API_ENOENT:
        return ENOENT;
    case API_FUSE_EBADF:
        return EBADF;
    case API_FUSE_EISDIR:
        return EISDIR;
    case API_FUSE_ENAMETOOLONG:
        return ENAMETOOLONG;
    case API_FUSE_ENOTDIR:
        return ENOTDIR;
    case API_FUSE_ENOTEMPTY:
        return ENOTEMPTY;
    case API_FUSE_EPERM:
        return EPERM;
    case API_FUSE_EROFS:
        return EROFS;
    case API_OK:
        return 0;
    default:
        break;
    }

    return EIO;
}

} // platform
} // fuse
} // mega

