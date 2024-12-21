#include <cassert>

#include <mega/fuse/common/any_lock_set.h>
#include <mega/fuse/common/any_lock.h>
#include <mega/fuse/common/client.h>
#include <mega/fuse/common/directory_inode.h>
#include <mega/fuse/common/error_or.h>
#include <mega/fuse/common/inode_badge.h>
#include <mega/fuse/common/inode_cache.h>
#include <mega/fuse/common/inode_db.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/inode.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/node_info.h>
#include <mega/fuse/common/ref.h>
#include <mega/fuse/platform/mount.h>

namespace mega
{
namespace fuse
{

void LockableTraits<Inode>::acquiring(const Inode& inode)
{
    FUSEDebugF("Acquiring lock on inode %s", toString(inode.id()).c_str());
}

void LockableTraits<Inode>::acquired(const Inode& inode)
{
    FUSEDebugF("Acquired lock on inode %s", toString(inode.id()).c_str());
}

void LockableTraits<Inode>::couldntAcquire(const Inode& inode)
{
    FUSEDebugF("Couldn't acquire lock on inode %s",
               toString(inode.id()).c_str());
}

void LockableTraits<Inode>::released(const Inode& inode)
{
    FUSEDebugF("Releasing lock on inode %s",
	           toString(inode.id()).c_str());
}

void LockableTraits<Inode>::tryAcquire(const Inode& inode)
{
    FUSEDebugF("Trying to acquire lock on inode %s",
               toString(inode.id()).c_str());
}

void Inode::moved([[maybe_unused]] InodeDBLock& lock,
                  const std::string& name,
                  NodeHandle parentHandle)
{
    // Sanity.
    assert(lock.owns_lock());

    // The node's been marked as removed.
    if (mRemoved)
    {
        // So just update the node's name and parent.
        mName = name;
        mParentHandle = parentHandle;

        return;
    }

    // Node's no longer present under mParentHandle as mName;
    mInodeDB.childRemoved(*this, mName, mParentHandle);

    // Update the node's name and parent.
    mName = name;
    mParentHandle = parentHandle;

    // Node's now visible as mName under mParentHandle.
    mInodeDB.childAdded(*this, mName, mParentHandle);
}

Inode::Inode(InodeID id,
             const NodeInfo& info,
             InodeDB& inodeDB)
  : Lockable()
  , mReferences(0)
  , mRemoved(false)
  , mID(id)
  , mInodeDB(inodeDB)
  , mModified(info.mModified)
  , mName(info.mName)
  , mParentHandle(info.mParentHandle)
  , mPermissions(info.mPermissions)
{
    // We're claiming mName under mParentHandle.
    mInodeDB.childAdded(*this, mName, mParentHandle);
}

void Inode::info(const NodeInfo& info, InodeDBLock& lock)
{
    // Inode's name or parent has changed.
    if (mParentHandle != info.mParentHandle
        || mName != info.mName)
        moved(lock, info.mName, info.mParentHandle);

    // Latch modification time and permissions.
    mModified = info.mModified;
    mPermissions = info.mPermissions;
}

Inode::~Inode()
{
}

Inode* Inode::accessed() const
{
    // The cache should only contain inodes that haven't been removed.
    if (!removed())
        mInodeDB.cache().add(*this);

    // Return a reference to this inode in the name of convenience.
    return const_cast<Inode*>(this);
}

DirectoryInodeRef Inode::directory()
{
    return DirectoryInodeRef();
}

FileInodeRef Inode::file()
{
    return FileInodeRef();
}

InodeID Inode::id() const
{
    return mID;
}

Error Inode::move(const std::string& name,
                  DirectoryInodeRef targetParent)
{
    // Sanity.
    assert(!name.empty());
    assert(targetParent);

    while (true)
    {
        DirectoryInodeRef sourceParent;

        // For easy management of multiple locks.
        AnyLockSet locks;

        // Lock ourselves.
        locks.emplace(*this);
        locks.lock();

        // Get a reference to our current parent.
        sourceParent = parent();

        // Release locks.
        locks.unlock();

        // Lock our parent.
        if (sourceParent)
            locks.emplace(*sourceParent);

        // Lock the target parent.
        locks.emplace(*targetParent);
        locks.lock();

        // Make sure our parent hasn't changed.
        if (sourceParent != parent())
            continue;

        // Target already has a child with this name.
        if (targetParent->get(name))
            return API_EEXIST;

        // Try and move ourselves.
        return move(InodeBadge(),
                    name,
                    std::move(targetParent));
    }
}

void Inode::moved(const std::string& name, NodeHandle parentHandle)
{
    InodeDBLock lock(mInodeDB);

    // Sanity.
    assert(!mRemoved);

    // Update this inode's name and parent.
    moved(lock, name, parentHandle);
}

const std::string& Inode::name(CachedOnlyTag) const
{
    InodeDBLock lock(mInodeDB);

    return mName;
}

std::string Inode::name() const
{
    return info().mName;
}

DirectoryInodeRef Inode::parent() const
{
    // Who is this inode's parent?
    auto parentHandle = this->parentHandle();

    // We have no parent.
    if (parentHandle.isUndef())
        return DirectoryInodeRef();

    // Try and get a reference to our parent.
    auto ref = mInodeDB.get(parentHandle);

    // Parent exists.
    if (ref)
        return ref->directory();

    // Either we have no parent or it has been removed.
    return DirectoryInodeRef();
}

NodeHandle Inode::parentHandle(CachedOnlyTag) const
{
    InodeDBLock lock(mInodeDB);

    return mParentHandle;
}

NodeHandle Inode::parentHandle() const
{
    return NodeHandle(info().mParentID);
}

accesslevel_t Inode::permissions() const
{
    return info().mPermissions;
}

ErrorOr<LocalPath> Inode::path(NodeHandle parentHandle) const
{
    // For convenience.
    return path(static_cast<InodeID>(parentHandle));
}

ErrorOr<LocalPath> Inode::path(InodeID parentID) const
{
    // Stores the description of the current inode.
    auto info = InodeInfo();

    // Stores the computed path of this inode.
    auto path = LocalPath();

    // Ascend from this inode.
    info.mParentID = mID;

    // Ascend until we find parentID or hit the cloud root.
    while (true)
    {
        // We've reached parentID.
        if (info.mParentID == parentID)
            return path;

        // We've hit the cloud root.
        if (!info.mParentID)
            return API_ENOENT;

        // Retrieve a reference to this inode's parent.
        auto ref = mInodeDB.get(info.mParentID);

        // Couldn't get a reference to this inode's parent.
        if (!ref)
            return API_ENOENT;

        // Retrieve the parent's description.
        info = ref->info();

        // Translate the parent's name to a local path.
        auto name = LocalPath::fromRelativePath(info.mName);
        
        // Prepend the parent's name to our path.
        path.prependWithSeparator(name);
    }
}

void Inode::ref(RefBadge)
{
    // Make sure nothing else can mess with our counter.
    InodeDBLock guard(mInodeDB);

    // Increment the counter.
    ++mReferences;

    // Make sure our counter hasn't wrapped around.
    assert(mReferences);
}

void Inode::removed(bool removed) const
{
    InodeDBLock guard(mInodeDB);

    // Nothing to do if our removal state has changed.
    if (mRemoved == removed)
        return;

    // Update our removal state.
    mRemoved = removed;

    // We've been removed.
    if (mRemoved)
    {
        FUSEDebugF("Setting removed flag on %s [%s] (%s)",
                   mName.c_str(),
                   toString(mID).c_str(),
                   toString(InodeID(mParentHandle)).c_str());

        // Remove the inode from the cache.
        mInodeDB.cache().remove(*this);

        // We no longer claim mName under mParentHandle.
        return mInodeDB.childRemoved(*this, mName, mParentHandle);
    }

    // We've been added.
    FUSEDebugF("Clearing removed flag on %s [%s] (%s)",
               mName.c_str(),
               toString(mID).c_str(),
               toString(InodeID(mParentHandle)).c_str());

    // Add the inode to the cache.
    mInodeDB.cache().add(*this);

    // Claim mName under mParentHandle.
    mInodeDB.childAdded(*this, mName, mParentHandle);
}

bool Inode::removed() const
{
    InodeDBLock guard(mInodeDB);

    return mRemoved;
}

Error Inode::replace(InodeRef target, bool replaceDirectories)
{
    assert(target);

    // Is our target a directory?
    auto targetDirectory = target->directory();

    // Is it legal for us to replace target?
    if (targetDirectory)
    {
        // We're a directory but the target's a file.
        if (file())
            return API_FUSE_EISDIR;

        // Caller doesn't want to replace directories.
        if (!replaceDirectories)
            return API_EEXIST;
    }
    else if (directory())
    {
        // We're a directory but the target's a file.
        return API_FUSE_ENOTDIR;
    }

    while (true)
    {
        auto sourceParent = parent();
        auto targetParent = target->parent();

        AnyLockSet locks;

        // Lock the parents.
        locks.emplace(*sourceParent);
        locks.emplace(*targetParent);

        // Lock ourselves and our target.
        locks.emplace(*this);
        locks.emplace(*target);

        // Parent's have changed.
        if (sourceParent != parent()
            || targetParent != target->parent())
            continue;

        locks.lock();

        // Our target's a directory.
        if (targetDirectory)
        {
            // And it must be empty.
            auto hasChildren = targetDirectory->hasChildren();

            // Target's been removed.
            if (!hasChildren)
                return API_ENOENT;

            // Target's not empty.
            if (*hasChildren)
                return API_FUSE_ENOTEMPTY;
        }

        // Latch the target's name.
        auto targetName = target->name();

        // Try and replace the target.
        return replace(InodeBadge(),
                       target,
                       std::move(targetName),
                       targetParent);
    }
}

Error Inode::unlink()
{
    while (true)
    {
        DirectoryInodeRef parent;

        // Easy management of multiple locks.
        AnyLockSet locks;

        // Lock ourselves.
        locks.emplace(*this);
        locks.lock();

        // Lock our parent if necessary.
        if ((parent = this->parent()))
        {
            // Release locks.
            locks.unlock();

            // Lock our parent.
            locks.emplace(*parent);
            locks.lock();

            // Parent's changed.
            if (this->parent() != parent)
                continue;
        }

        // Try and unlink ourselves.
        return unlink(InodeBadge());
    }
}

void Inode::unref(RefBadge badge)
{
    // Make sure nothing else can mess with our counter.
    InodeDBLock lock(mInodeDB);

    // Make sure the counter's sane.
    assert(mReferences);

    // Decrement the counter.
    --mReferences;

    // Inode still has some references.
    if (mReferences)
        return;

    // All references to this inode have been dropped.

    // Inode's no longer associated with this name or parent.
    if (!mRemoved)
        mInodeDB.childRemoved(*this, mName, mParentHandle);

    // Remove the inode from the database.
    remove(badge, std::move(lock));
}

void doRef(RefBadge badge, Inode& inode)
{
    inode.ref(badge);
}

void doUnref(RefBadge badge, Inode& inode)
{
    inode.unref(badge);
}

} // fuse
} // mega

