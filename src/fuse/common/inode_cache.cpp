#include <chrono>
#include <functional>

#include <mega/fuse/common/inode_cache.h>
#include <mega/fuse/common/inode.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/ref.h>
#include <mega/fuse/common/utility.h>

namespace mega
{
namespace fuse
{

// Convenience.
using std::chrono::steady_clock;

struct InodeCache::Entry
{
    Entry(const Inode& inode)
      : mAccessed(steady_clock::now())
      , mInode(const_cast<Inode*>(&inode))
      , mPosition()
    {
    }

    // When was the inode last accessed?
    steady_clock::time_point mAccessed;

    // What inode has been cached?
    InodeRef mInode;

    // Where is the inode in the cache's position map?
    EntryPositionMapIterator mPosition;
}; // Entry;

void InodeCache::loop()
{
    // Convenience.
    auto& ageThreshold = mFlags.mCleanAgeThreshold;
    auto& interval = mFlags.mCleanInterval;
    auto& sizeThreshold = mFlags.mCleanSizeThreshold;

    FUSEDebug1("Inode Cache Cleaner thread started");

    while (true)
    {
        // Stores references to any evicted inodes.
        auto evicted = InodeRefVector();

        // Acquire lock.
        Lock lock(mLock);

        // Are we shutting down?
        if (mTerminate)
            break;

        // Wait until interval has passed or until we're notified.
        mCV.wait_for(lock, interval);

        // Try and reduce the cache's size.
        evicted = reduce(ageThreshold, lock, sizeThreshold);
    }

    FUSEDebug1("Inode Cache Cleaner thread stopped");
}

InodeRefVector InodeCache::reduce(std::chrono::seconds age, Lock&, std::size_t size)
{
    // For debugging.
    FUSEDebugF("Cleaning inode cache: age >= %lus, size > %lu",
               age.count(),
               size);

    // Which inodes are to be evicted?
    auto evicted = InodeRefVector();

    // Convenience.
    auto now = steady_clock::now();

    // How many inodes are in the cache?
    auto num = mEntries.size();

    // Evict inodes until we store less than size.
    while (mEntries.size() > size)
    {
        // Select oldest inode in the cache.
        auto& entry = mEntries.back();

        // We've evicted all inodes older than age.
        if (age.count() && age > now - entry.mAccessed)
            break;

        FUSEDebugF("Removing inode %s from inode cache",
                   toString(entry.mInode->id()).c_str());

        // Remove inode from the position map.
        mPositions.erase(entry.mPosition);

        // Take ownership of the entry's inode.
        evicted.emplace_back(std::move(entry.mInode));

        // Remove inode from the cache.
        mEntries.pop_back();
    }

    FUSEDebugF("Removed %lu/%lu inode(s) from the inode cache",
               evicted.size(),
               num);

    return evicted;
}

InodeCache::InodeCache(const InodeCacheFlags& flags)
  : mCV()
  , mEntries()
  , mFlags(flags)
  , mLock()
  , mPositions()
  , mTerminate{false}
  , mThread(&InodeCache::loop, this)
{
    FUSEDebug1("Inode Cache constructed");
}

InodeCache::~InodeCache()
{
    // Let the cleaner know it should terminate.
    mTerminate = true;

    // Wake the cleaner if necessary.
    mCV.notify_one();

    // Wait for the cleaner to terminate.
    mThread.join();

    // We're done.
    FUSEDebug1("Inode Cache destroyed");
}

bool InodeCache::add(const Inode& inode)
{
    Lock guard(mLock);

    // Convenience.
    auto e  = mEntries.begin();
    auto id = inode.id();

    // Is the inode already in the cache?
    auto p = mPositions.find(id);

    // Inode's already in the cache.
    if (p != mPositions.end())
    {
        // Mark inode as most recently accessed if necessary.
        if (e != p->second)
            mEntries.splice(e, mEntries, p->second);

        // Update inode's access time.
        p->second->mAccessed = steady_clock::now();

        // Inode's been updated.
        return false;
    }

    // Add inode to the cache.
    e = mEntries.emplace(e, inode);
    
    // For debugging.
    FUSEDebugF("Adding inode %s to inode cache",
               toString(id).c_str());

    // Add inode to the position map.
    e->mPosition = mPositions.emplace(id, e).first;

    // Inode's been added to the cache.
    return true;
}

void InodeCache::clear()
{
    EntryList entries;
    EntryPositionMap positions;

    // Acquire ownership of mEntries and mPositions.
    {
        // Acquire lock.
        std::lock_guard<std::mutex> guard(mLock);

        // Take ownership of mEntries and mPositions.
        entries = std::move(mEntries);
        positions = std::move(mPositions);
    }
}

void InodeCache::flags(const InodeCacheFlags& flags)
{
    Lock guard(mLock);

    // Update the cache's flags.
    mFlags = flags;

    // Make sure the cleaner doesn't poll.
    if (!mFlags.mCleanInterval.count())
        mFlags.mCleanInterval = std::chrono::seconds::max();

    // Wake the cleaner so the flags take effect.
    mCV.notify_one();
}

InodeCacheFlags InodeCache::flags() const
{
    Lock guard(mLock);

    return mFlags;
}

bool InodeCache::remove(const Inode& inode)
{
    Lock guard(mLock);

    // Convenience.
    auto id = inode.id();

    // Is this inode in the cache?
    auto p = mPositions.find(id);

    // Inode isn't in the cache.
    if (p == mPositions.end())
        return false;

    // For debugging.
    FUSEDebugF("Removing inode %s from inode cache",
               toString(id).c_str());

    // Remove the inode from the cache.
    mEntries.erase(p->second);

    // Remove inode from the position map.
    mPositions.erase(p);

    // Inode's been removed from the cache.
    return true;
}

} // fuse
} // mega

