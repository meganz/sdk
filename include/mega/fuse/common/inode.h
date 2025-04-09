#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

#include <mega/common/error_or_forward.h>
#include <mega/common/lockable.h>
#include <mega/common/node_info_forward.h>
#include <mega/fuse/common/directory_inode_forward.h>
#include <mega/fuse/common/file_inode_forward.h>
#include <mega/fuse/common/inode_badge_forward.h>
#include <mega/fuse/common/inode_db_forward.h>
#include <mega/fuse/common/inode_forward.h>
#include <mega/fuse/common/inode_id.h>
#include <mega/fuse/common/inode_info_forward.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/tags.h>
#include <mega/fuse/platform/mount_forward.h>

#include <mega/types.h>

namespace mega
{
namespace common
{

template<>
struct LockableTraits<fuse::Inode>
{
    using LockType = std::recursive_mutex;

    static void acquiring(const fuse::Inode& inode);

    static void acquired(const fuse::Inode& inode);

    static void couldntAcquire(const fuse::Inode& inode);

    static void released(const fuse::Inode& inode);

    static void tryAcquire(const fuse::Inode& inode);
}; // LockableTraits<fuse::Inode>

} // common

namespace fuse
{

// Represents a filesystem entity.
class Inode
  : public common::Lockable<Inode>
{
    // Update this inode's name and parent.
    void moved(InodeDBLock& lock,
               const std::string& name,
               NodeHandle parentHandle);

    // Removes this instance from the inode database.
    virtual void remove(RefBadge badge, InodeDBLock lock) = 0;

    // Tracks how many actors reference this instance.
    unsigned long mReferences;

    // Has this inode been removed?
    mutable bool mRemoved;

protected:
    Inode(InodeID id,
          const common::NodeInfo& info,
          InodeDB& inodeDB);

    // Update an inode's cached description.
    void info(const common::NodeInfo& info, InodeDBLock& lock);

    // The inode's identifier.
    const InodeID mID;
    
    // The database that contains this inode.
    InodeDB& mInodeDB;

    // Last known modification time.
    mutable m_time_t mModified;

    // Last known name.
    mutable std::string mName;

    // Last known parent.
    mutable NodeHandle mParentHandle;

    // Last known permissions.
    mutable accesslevel_t mPermissions;

public:
    virtual ~Inode();
    
    // Update this inode's access time.
    Inode* accessed() const;

    // Is this inode in the file cache?
    virtual bool cached() const = 0;

    // Check if this inode represents a directory.
    //
    // If the inode does represent a directory, a more specialized
    // reference to the inode is returned.
    //
    // If the inode doesn't represent a directory, a null reference
    // is returned.
    virtual DirectoryInodeRef directory();

    // Check if this inode represents a file.
    //
    // If the inode does represent a file, a more specialized
    // reference to the inode is returned.
    //
    // If the inode doesn't represent a file, a null reference is
    // returned.
    virtual FileInodeRef file();

    // What cloud node, if any, is associated with this inode?
    virtual NodeHandle handle() const = 0;

    // What is this inode's identifier?
    InodeID id() const;

    // Update an inode's cached description.
    virtual void info(const common::NodeInfo& info) = 0;

    // Retrieve a description of the entity this inode represents.
    virtual InodeInfo info() const = 0;

    // Move (or rename) this inode (assuming locks are held.)
    virtual Error move(InodeBadge badge,
                       const std::string& name,
                       DirectoryInodeRef parent) = 0;

    // Move (or rename) this inode.
    Error move(const std::string& name,
               DirectoryInodeRef parent);

    // Signal that this inode has been moved (or renamed.)
    void moved(const std::string& name, NodeHandle parentHandle);

    // What is this inode's name?
    const std::string& name(CachedOnlyTag) const;
    std::string name() const;

    // Retrieve a reference to this inode's parent.
    DirectoryInodeRef parent() const;

    // What cloud node is the parent of this inode?
    NodeHandle parentHandle(CachedOnlyTag) const;
    NodeHandle parentHandle() const;

    // Determine what permissions are applicable to this inode.
    accesslevel_t permissions() const;

    // Compute this inode's path relative to the specified node.
    common::ErrorOr<LocalPath> path(NodeHandle parentHandle) const;
    common::ErrorOr<LocalPath> path(InodeID parentID) const;

    // Increment this instance's reference counter.
    void ref(RefBadge badge);

    // Signal whether this inode has been removed.
    //
    // Typically called when an inode has been overwritten or unlinked.
    void removed(bool removed) const;

    // Query whether this inode has been removed.
    bool removed() const;

    // Replace other with this inode (assuming locks are held.)
    virtual Error replace(InodeBadge badge,
                          InodeRef other,
                          const std::string& otherName,
                          DirectoryInodeRef otherParent) = 0;

    // Replace other with this inode (assuming locks are not held.)
    Error replace(InodeRef other, bool replaceDirectories);

    // Unlink this inode (without taking any locks.)
    virtual Error unlink(InodeBadge badge) = 0;

    // Unlink this inode (taking appriopriate locks.)
    Error unlink();

    // Decrements this inode's reference counter.
    //
    // If the instance's referene counter drops to zero,
    // the instance is removed from the inode database.
    void unref(RefBadge badge);
}; // Inode

} // fuse
} // mega

