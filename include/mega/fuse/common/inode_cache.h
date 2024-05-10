#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <list>
#include <map>
#include <mutex>
#include <thread>

#include <mega/fuse/common/inode_cache_flags.h>
#include <mega/fuse/common/inode_cache_forward.h>
#include <mega/fuse/common/inode_forward.h>
#include <mega/fuse/common/inode_id_forward.h>

namespace mega
{
namespace fuse
{

class InodeCache
{
    // Describes an inode in the cache.
    struct Entry;

    using EntryList = std::list<Entry>;
    using EntryListIterator = EntryList::iterator;
    using EntryPositionMap = std::map<InodeID, EntryListIterator>;
    using EntryPositionMapIterator = EntryPositionMap::iterator;

    using Lock = std::unique_lock<std::mutex>;

    // Periodically tries to reduce the cache's size.
    void loop();

    // Reduce the cache to the specified size.
    //
    // Only entries age or older are evicted.
    InodeRefVector reduce(std::chrono::seconds age,
                          Lock& lock,
                          std::size_t size);

    // Wakes up the cleaner thread.
    std::condition_variable mCV;

    // Describes each inode in the cache.
    EntryList mEntries;

    // Dictates how we behave.
    InodeCacheFlags mFlags;

    // Serializes access to class members.
    mutable std::mutex mLock;

    // Tracks where each inode can be found in the cache.
    EntryPositionMap mPositions;

    // Signals the cleaner thread to terminate.
    std::atomic<bool> mTerminate;

    // Responsible for periodically cleaning the cache.
    std::thread mThread;

public:
    explicit InodeCache(const InodeCacheFlags& flags);

    ~InodeCache();

    // Add an inode to the cache.
    bool add(const Inode& inode);

    // Evict all inodes from the cache.
    void clear();

    // Update this cache's flags.
    void flags(const InodeCacheFlags& flags);

    // Retrieve this cache's flags.
    InodeCacheFlags flags() const;

    // Remove an inode from the cache.
    bool remove(const Inode& inode);
}; // InodeCache

} // fuse
} // mega

