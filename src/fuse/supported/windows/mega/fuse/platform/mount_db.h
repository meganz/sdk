#pragma once

#include <mega/fuse/common/inode_forward.h>
#include <mega/fuse/common/mount_db.h>
#include <mega/fuse/common/service_context_forward.h>
#include <mega/fuse/platform/file_explorer_setter.h>
#include <mega/fuse/platform/handle.h>
#include <mega/fuse/platform/security_descriptor.h>

namespace mega
{
namespace fuse
{
namespace platform
{

class MountDB
  : public fuse::MountDB
{
    FileExplorerSetter mFileExplorerSetter{};

    // Checks whether a mount's local path is valid.
    MountResult check(const common::Client& client,
                      const MountInfo& info) const override;

    // Checks whether a mount's name is valid.
    MountResult checkName(const std::string& name) const override;

public:
    MountDB(ServiceContext& context);

    void notifyFileExplorerSetter();

    // Security descriptor for read-only inodes.
    const SecurityDescriptor mReadOnlySecurityDescriptor;

    // Security descriptor for read-write inodes.
    const SecurityDescriptor mReadWriteSecurityDescriptor;
}; // MountDB

} // platform
} // fuse
} // mega

