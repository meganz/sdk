#pragma once

#include <cstdint>

#include <mega/fuse/common/inode_forward.h>
#include <mega/fuse/common/mount_db.h>
#include <mega/fuse/common/service_context_forward.h>
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
    // Checks whether a mount's local path is valid.
    MountResult check(const Client& client,
                      const MountInfo& info) const override;

public:
    MountDB(ServiceContext& context);

    // Security descriptor for read-only inodes.
    const SecurityDescriptor mReadOnlySecurityDescriptor;

    // Security descriptor for read-write inodes.
    const SecurityDescriptor mReadWriteSecurityDescriptor;
}; // MountDB

} // platform
} // fuse
} // mega

