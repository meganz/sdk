#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include <mega/fuse/common/activity_monitor_forward.h>
#include <mega/fuse/common/mount_inode_id.h>
#include <mega/fuse/platform/session_forward.h>

#include <mega/types.h>

namespace mega
{
namespace fuse
{
namespace platform
{

class InodeInvalidator
{
    // Describes an invalidation to be performed.
    struct Invalidation;

    // Associates an inode with an invalidation to be performed.
    using InvalidationMap = std::map<MountInodeID, Invalidation>;

    // Maintains the order in which invalidations should be performed.
    using InvalidationQueue = std::deque<InvalidationMap::iterator>;

    // Get the invalidation associated with an inode.
    auto invalidation(ActivityMonitor& activities,
                      MountInodeID id) -> Invalidation&;

    // Processes invalidation requests.
    void loop();

    // Signalled when an inode needs to be invalidated.
    std::condition_variable mCV;

    // What kind of invalidation does an inode require?
    InvalidationMap mInvalidationByID;

    // When should an inode be invalidated?
    InvalidationQueue mInvalidationByOrder;

    // Serializes access to instance members.
    std::mutex mLock;

    // What session are we invalidating inodes on?
    Session& mSession;

    // Signals the worker that it's time to terminate.
    std::atomic<bool> mTerminate;

    // The thread on which inodes will be invalidated.
    std::thread mWorker;

public:
    explicit InodeInvalidator(Session& session);

    ~InodeInvalidator();

    // Invalidate the attributes of a specific inode.
    void invalidateAttributes(ActivityMonitor& activities,
                              MountInodeID id);

    // Invalidate the data of a specific inode.
    void invalidateData(ActivityMonitor& activities,
                        MountInodeID id,
                        m_off_t offset,
                        m_off_t size);

    // Invalidate a directory entry in a specific inode.
    void invalidateEntry(ActivityMonitor& activities,
                         MountInodeID child,
                         const std::string& name,
                         MountInodeID parent);

    void invalidateEntry(ActivityMonitor& activities,
                         MountInodeID id,
                         const std::string& name);
}; // InodeInvalidator

} // platform
} // fuse
} // mega

