#include <cassert>
#include <tuple>
#include <utility>

#include <mega/fuse/common/client.h>
#include <mega/fuse/common/file_cache.h>
#include <mega/fuse/common/inode.h>
#include <mega/fuse/common/inode_db.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/mount.h>
#include <mega/fuse/common/mount_event.h>
#include <mega/fuse/common/mount_event_type.h>
#include <mega/fuse/common/mount_inode_id.h>
#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/ref.h>
#include <mega/fuse/platform/context.h>
#include <mega/fuse/platform/mount_db.h>
#include <mega/fuse/platform/service_context.h>

namespace mega
{
namespace fuse
{

struct Mount::PinnedInodeInfo
{
    PinnedInodeInfo(InodeRef inode, const InodeInfo& info)
      : mInode(std::move(inode))
      , mName(info.mName)
      , mParentID(info.mParentID)
      , mPinCount(0)
    {
    }

    // The inode that we're pinning in memory.
    InodeRef mInode;

    // The name of the inode when it was pinned.
    const std::string mName;

    // The parent of the inode when it was pinned.
    const InodeID mParentID;

    // How many times the inode has been pinned.
    std::uint64_t mPinCount;
}; /* PinnedInodeInfo */

void Mount::invalidatePin(PinnedInodeInfo& info,
                          std::unique_lock<std::mutex>&)
{
    // Convenience.
    auto& inode = info.mInode;

    // Invalidate this inode's data if it's a file.
    if (inode->file())
        invalidateData(inode->id());

    // Invalidate the inode's attributes.
    invalidateAttributes(inode->id());

    // Inode has no parent.
    if (!info.mParentID)
        return;

    // Invalidate the inode's directory entry.
    invalidateEntry(info.mName, info.mParentID);
}

Mount::Mount(const MountInfo& info, platform::MountDB& mountDB)
  : enable_shared_from_this()
  , mContexts()
  , mContextsLock()
  , mDisabled()
  , mFlags(info.mFlags)
  , mHandle(info.mHandle)
  , mPins()
  , mPinsLock()
  , mMountDB(mountDB)
{
}

Mount::~Mount()
{
    // Release dangling contexts.
    auto contexts = ([this]() {
        std::lock_guard<std::mutex> guard(mContextsLock);
        return std::move(mContexts);
    })();

    for (auto* context : contexts)
        delete context;

    // Broadcast a mount disabled event.
    mMountDB.client().emitEvent({
        name(),
        MOUNT_SUCCESS,
        MOUNT_DISABLED
    });

    // Let any waiters know we've been disabled.
    mDisabled.set_value();
}

InodeRef Mount::get(MountInodeID id, bool memoryOnly) const
{
    return mMountDB.mContext.mInodeDB.get(map(id), memoryOnly);
}

void Mount::pin(InodeRef inode, const InodeInfo& info)
{
    // Sanity.
    assert(inode);
    assert(inode->id() == info.mID);

    // Convenience.
    auto id = info.mID;

    std::lock_guard<std::mutex> guard(mPinsLock);

    FUSEDebugF("Pinning inode %s", toString(id).c_str());

    // Where should we insert this inode's pin record?
    auto position  = mPins.lower_bound(id);

    // Inode hasn't already been pinned.
    if (position == mPins.end() || position->first != id)
    {
        position = mPins.emplace_hint(
                     position,
                     std::piecewise_construct,
                     std::forward_as_tuple(id),
                     std::forward_as_tuple(std::move(inode), info));
    }

    // Increase pin count.
    position->second.mPinCount++;

    FUSEDebugF("Inode %s now has %zu reference(s)",
               toString(id).c_str(),
               position->second.mPinCount);
}

void Mount::unpin(InodeRef inode, std::uint64_t num)
{
    assert(inode);

    // The inode to be unpinned, if any.
    InodeRef ref;

    // Decrease pin count and latch inode if needed.
    {
        std::lock_guard<std::mutex> guard(mPinsLock);

        FUSEDebugF("Unpinning inode %s",
                   toString(inode->id()).c_str());

        // Has this indoe been pinned?
        auto i = mPins.find(inode->id());

        // Sanity.
        assert(i != mPins.end());
        assert(i->second.mPinCount >= num);

        // Decrement pin count.
        i->second.mPinCount -= num;

        FUSEDebugF("Inode %s now has %zu reference(s)",
                   toString(inode->id()).c_str(),
                   i->second.mPinCount);

        // Pin still has references.
        if (i->second.mPinCount)
            return;

        // Latch inode so we can release it outside of mPinsLock.
        ref = std::move(i->second.mInode);

        // Inode's no longer pinned.
        mPins.erase(i);
    }
}

void Mount::contextAdded(platform::ContextBadge, platform::Context& context)
{
    std::lock_guard<std::mutex> guard(mContextsLock);

    auto result = mContexts.emplace(&context);

    assert(result.second);

    static_cast<void>(result);
}

void Mount::contextRemoved(platform::ContextBadge, platform::Context& context)
{
    std::lock_guard<std::mutex> guard(mContextsLock);

    mContexts.erase(&context);
}

std::future<void> Mount::disabled()
{
    return mDisabled.get_future();
}

void Mount::enabled()
{
    // Convenience.
    auto& fileCache = mMountDB.mContext.mFileCache;
    auto& inodeDB   = mMountDB.mContext.mInodeDB;

    // Flush any modified files contained by this mount.
    fileCache.flush(*this, inodeDB.modified(mHandle));
}

void Mount::executorFlags(const TaskExecutorFlags&)
{
}

void Mount::flags(const MountFlags& flags)
{
    std::lock_guard<std::mutex> guard(mLock);

    mFlags = flags;
}

MountFlags Mount::flags() const
{
    std::lock_guard<std::mutex> guard(mLock);

    return mFlags;
}

NodeHandle Mount::handle() const
{
    return mHandle;
}

MountInfo Mount::info() const
{
    std::lock_guard<std::mutex> guard(mLock);

    MountInfo info;

    info.mFlags = mFlags;
    info.mHandle = mHandle;
    info.mPath = path();

    return info;
}

void Mount::invalidatePin(InodeID id)
{
    std::unique_lock<std::mutex> guard(mPinsLock);

    // Has this inode been pinned?
    auto i = mPins.find(id);

    // Inode's been pinned: Invalidate it.
    if (i != mPins.end())
        invalidatePin(i->second, guard);
}

void Mount::invalidatePins(InodeRefSet& invalidated)
{
    std::unique_lock<std::mutex> guard(mPinsLock);

    // Iterate over pinned inodes, invalidating each in turn.
    for (auto i = mPins.begin(); i != mPins.end(); )
    {
        // Get our hands on the pin's info.
        auto& pin = i++->second;

        // Let the caller know we invalidated this inode.
        invalidated.emplace(pin.mInode);

        // Invalidate the inode.
        invalidatePin(pin, guard);
    }
}

std::string Mount::name() const
{
    std::lock_guard<std::mutex> guard(mLock);

    return mFlags.mName;
}

bool Mount::writable() const
{
    std::lock_guard<std::mutex> guard(mLock);

    return !mFlags.mReadOnly;
}

} // fuse
} // mega

