#include <mega/common/error_or.h>
#include <mega/common/node_info.h>
#include <mega/file_service/file_structure_mutex.h>
#include <mega/fuse/common/any_lock.h>
#include <mega/fuse/common/any_lock_set.h>
#include <mega/fuse/common/client.h>
#include <mega/fuse/common/directory_inode.h>
#include <mega/fuse/common/inode.h>
#include <mega/fuse/common/inode_badge.h>
#include <mega/fuse/common/inode_cache.h>
#include <mega/fuse/common/inode_db.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/ref.h>
#include <mega/fuse/platform/mount.h>
#include <mega/fuse/platform/path_adapter.h>

#include <cassert>

namespace mega
{
namespace fuse
{

using namespace common;

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

Inode::Inode(InodeID id, const NodeInfo& info, InodeDB& inodeDB):
    mReferences(0),
    mRemoved(false),
    mID(id),
    mInodeDB(inodeDB),
    mModified(info.mModified),
    mName(info.mName),
    mParentHandle(info.mParentHandle),
    mPermissions(info.mPermissions)
{
    // We're claiming mName under mParentHandle.
    mInodeDB.childAdded(*this, mName, mParentHandle);
}

void Inode::acquiring(const Inode& inode)
{
    FUSEVerboseF("Acquiring lock on inode %s", toString(inode.id()).c_str());
}

void Inode::acquired(const Inode& inode)
{
    FUSEVerboseF("Acquired lock on inode %s", toString(inode.id()).c_str());
}

void Inode::couldntAcquire(const Inode& inode)
{
    FUSEVerboseF("Couldn't acquire lock on inode %s", toString(inode.id()).c_str());
}

void Inode::info(const NodeInfo& info, InodeDBLock& lock)
{
    // Inode's name or parent has changed.
    if (mParentHandle != info.mParentHandle || mName != info.mName)
        moved(lock, info.mName, info.mParentHandle);

    // Latch modification time and permissions.
    mModified = info.mModified;
    mPermissions = info.mPermissions;
}

void Inode::lock() const
{
    acquiring(*this);

    doLock();

    acquired(*this);
}

void Inode::released(const Inode& inode)
{
    FUSEVerboseF("Releasing lock on inode %s", toString(inode.id()).c_str());
}

void Inode::tryAcquire(const Inode& inode)
{
    FUSEVerboseF("Trying to acquire lock on inode %s", toString(inode.id()).c_str());
}

Inode::~Inode() {}

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

template<typename Path>
Error Inode::move(const Path& path, NodeHandle rootHandle, bool replace)
{
    // The name of the target inode at path, if any.
    std::string name;

    // Loop until we hit a steady state.
    while (true)
    {
        // Try and locate the target inode.
        auto [target, found] = mInodeDB.lookup(path, rootHandle, &name);

        // Couldn't locate target directory.
        if (found != API_FUSE_ENOTFOUND && found != API_OK)
            return found;

        // Target exists and the caller doesn't want to replace it.
        if (found == API_OK && !replace)
            return API_EEXIST;

        // Convenience.
        auto sourceParent = parent();
        auto targetParent = target->parent();

        AnyLockSet locks;

        locks.emplace(*this);
        locks.emplace(*sourceParent);
        locks.emplace(*target);

        // Target exists and will be replaced.
        if (found == API_OK)
            locks.emplace(*targetParent);

        // Acquire necessary locks.
        locks.lock();

        // Make sure our move target hasn't changed.
        auto [target2, found1] = mInodeDB.lookup(path, rootHandle, &name);

        // Hierarchy has changed.
        if (found != found1)
            continue;

        // Target has changed.
        if (target != target2)
            continue;

        // Target parent has changed.
        if (targetParent != target->parent())
            continue;

        // Target exists and needs to be replaced.
        if (found == API_OK)
        {
            // Target's a file.
            if (target->file())
                return this->replace({}, target, name, targetParent);

            // Source is a directory.
            if (directory())
                return API_EEXIST;

            // Source is a file.
            return API_FUSE_EISDIR;
        }

        // Sanity: Target isn't a file, it's where the source should be moved.
        assert(target->directory());

        // Move the source inode to its new location.
        return move({}, name, target->directory());
    }
}

template Error Inode::move(const platform::PathAdapter&, NodeHandle, bool);

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
    auto ref = mInodeDB.get(InodeID::from(parentHandle));

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
    return info().mParentID.toHandle();
}

accesslevel_t Inode::permissions() const
{
    return info().mPermissions;
}

ErrorOr<LocalPath> Inode::path(NodeHandle parentHandle) const
{
    // For convenience.
    return path(InodeID::from(parentHandle));
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
            return unexpected(API_ENOENT);

        // Retrieve a reference to this inode's parent.
        auto ref = mInodeDB.get(info.mParentID);

        // Couldn't get a reference to this inode's parent.
        if (!ref)
            return unexpected(API_ENOENT);

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
                   toString(InodeID::from(mParentHandle)).c_str());

        // Remove the inode from the cache.
        mInodeDB.cache().remove(*this);

        // We no longer claim mName under mParentHandle.
        return mInodeDB.childRemoved(*this, mName, mParentHandle);
    }

    // We've been added.
    FUSEDebugF("Clearing removed flag on %s [%s] (%s)",
               mName.c_str(),
               toString(mID).c_str(),
               toString(InodeID::from(mParentHandle)).c_str());

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

bool Inode::try_lock() const
{
    tryAcquire(*this);

    if (doTryLock())
        return acquired(*this), true;

    couldntAcquire(*this);

    return false;
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

void Inode::unlock() const
{
    doUnlock();

    released(*this);
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
