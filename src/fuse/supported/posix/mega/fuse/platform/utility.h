#pragma once

#include <mega/fuse/common/inode_info_forward.h>
#include <mega/fuse/common/mount_inode_id_forward.h>
#include <mega/fuse/common/mount_result_forward.h>
#include <mega/fuse/platform/file_descriptor_forward.h>
#include <mega/fuse/platform/library.h>

#include <mega/types.h>

namespace mega
{
namespace fuse
{
namespace platform
{

// Clarity.
using FilesystemPredicate =
    std::function<bool(const std::string& path,
                       const std::string& type)>;

using PathVector = std::vector<std::string>;

bool abort(const std::string& path);

PathVector filesystems(FilesystemPredicate predicate = nullptr);

FileDescriptorPair pipe(bool closeReaderOnFork,
                        bool closeWriterOnFork);

void translate(struct stat& attributes,
               MountInodeID id,
               const InodeInfo& info);

void translate(fuse_entry_param& entry,
               MountInodeID id,
               const InodeInfo& info);

int translate(Error result);

MountResult unmount(const std::string& path, bool abort);

} // platform
} // fuse
} // mega

