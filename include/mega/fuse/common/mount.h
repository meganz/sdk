#pragma once

#include <future>
#include <mutex>

#include <mega/fuse/common/inode_forward.h>
#include <mega/fuse/common/inode_id_forward.h>
#include <mega/fuse/common/inode_info_forward.h>
#include <mega/fuse/common/mount_db_forward.h>
#include <mega/fuse/common/mount_flags.h>
#include <mega/fuse/common/mount_forward.h>
#include <mega/fuse/common/mount_info.h>
#include <mega/fuse/common/mount_inode_id_forward.h>
#include <mega/fuse/common/normalized_path.h>
#include <mega/fuse/common/task_executor_flags_forward.h>
#include <mega/fuse/platform/context_forward.h>
#include <mega/fuse/platform/mount_db_forward.h>

namespace mega
{
namespace fuse
{

// Represents an individual mapping between the cloud and local disk.
class Mount
  : public std::enable_shared_from_this<Mount>
{
    // Records information about a pinned inode.
    struct PinnedInodeInfo;

    // Invalidate a pinned inode.
    void invalidatePin(PinnedInodeInfo& info,
                       std::unique_lock<std::mutex>& lock);

    // What directories (or files) are currently open?
    platform::ContextRawPtrSet mContexts;

    // Serializes access to mContexts.
    std::mutex mContextsLock;

    // Signalled when the mount is destroyed.
    std::promise<void> mDisabled;

    // Specifies how the mount should behave.
    MountFlags mFlags;

    // Protects access to this mount's flags.
    mutable std::mutex mLock;

    // What cloud node are we mapping to?
    const NodeHandle mHandle;

    // What local path are we mapping from?
    const NormalizedPath mPath;

    // Used to keep (pin) inodes in memory.
    FromInodeIDMap<PinnedInodeInfo> mPins;

    // Protects access to mPins.
    std::mutex mPinsLock;

protected:
    Mount(const MountInfo& info, platform::MountDB& mountDB);

    ~Mount();

    // Try and retrieve a reference to the specified inode.
    InodeRef get(MountInodeID id, bool memoryOnly = false) const;

    // Pin an inode in memory.
    void pin(InodeRef inode, const InodeInfo& info);

    // Unpin a pinned inode.
    void unpin(InodeRef inode, std::size_t num);

public:
    // Add a context to our context set.
    void contextAdded(platform::ContextBadge badge,
                      platform::Context& context);

    // Remove a context from our context set.
    void contextRemoved(platform::ContextBadge badge,
                        platform::Context& context);

    // Retrieve a reference to this mount's disabled event.
    std::future<void> disabled();

    // Called when the mount has been enabled.
    void enabled();

    // Update this mount's executor flags.
    virtual void executorFlags(const TaskExecutorFlags& flags);

    // Update this mount's flags.
    void flags(const MountFlags& flags);

    // Retrieve this mount's flags.
    MountFlags flags() const;

    // Which cloud node is this mount mapping to?
    NodeHandle handle() const;

    // Retrieve this mount's description.
    MountInfo info() const;

    // Invalidate an inode's attributes.
    virtual void invalidateAttributes(InodeID id) = 0;

    // Invalidate an inode's data.
    virtual void invalidateData(InodeID id,
                                m_off_t offset,
                                m_off_t size) = 0;

    virtual void invalidateData(InodeID id) = 0;

    // Invalidate a directory entry.
    virtual void invalidateEntry(const std::string& name,
                                 InodeID child,
                                 InodeID parent) = 0;

    virtual void invalidateEntry(const std::string& name,
                                 InodeID parent) = 0;

    // Invalidate a pinned inode.
    void invalidatePin(InodeID id);

    // Invalidate any pinned inodes.
    void invalidatePins(InodeRefSet& invalidated);

    // Translate a mount-speicifc inode ID to a system-wide inode ID.
    virtual InodeID map(MountInodeID id) const = 0;

    // Translate a system-wide inode ID to a mount-specific inode ID.
    virtual MountInodeID map(InodeID id) const = 0;

    // What is this mount's name?
    std::string name() const;

    // What local path is this mount mapping from?
    const NormalizedPath& path() const;

    // Is this mount writable?
    bool writable() const;

    // Which database contains this mount?
    platform::MountDB& mMountDB;
}; // Mount

} // fuse
} // mega

