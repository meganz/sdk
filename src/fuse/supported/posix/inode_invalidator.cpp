#include <cassert>
#include <stdexcept>
#include <utility>

#include <mega/fuse/common/activity_monitor.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/utility.h>
#include <mega/fuse/platform/inode_invalidator.h>
#include <mega/fuse/platform/mount.h>

namespace mega
{
namespace fuse
{
namespace platform
{

enum InvalidationFlag : unsigned int
{
    // Invalidating an inode's attributes.
    IF_ATTRIBUTES = 0x1,
    // Invalidating an inode's data.
    IF_DATA = 0x2
}; // InvalidationFlag

using InvalidationFlags = unsigned int;

struct InodeInvalidator::Invalidation
{
    Invalidation(ActivityMonitor& activities, MountInodeID id)
      : mActivity(activities.begin())
      , mEntries()
      , mID(id)
      , mFlags(0)
    {
    }

    void invalidate(Session& session)
    try
    {
        // Invalidate the inode's attributes.
        if ((mFlags & IF_ATTRIBUTES))
            session.invalidateAttributes(mID);

        // Invalidate the inode's data.
        if ((mFlags & IF_DATA))
            session.invalidateData(mID,
                                   mData.mBegin,
                                   mData.mEnd - mData.mBegin);

        // Invalidate the inode's directory entries.
        for (auto& entry : mEntries)
        {
            for (auto& child : entry.second)
                session.invalidateEntry(entry.first, child, mID);

            session.invalidateEntry(entry.first, mID);
        }
    }
    catch (std::runtime_error& exception)
    {
        FUSEWarningF("Unable to invalidate inode %s: %s",
                     toString(mID).c_str(),
                     exception.what());
    }

    // What data do we need to invalidate?
    struct
    {
        m_off_t mBegin;
        m_off_t mEnd;
    } mData;

    // Lets our mount know its still being used.
    Activity mActivity;

    // What entries need to be invalidated?
    std::map<std::string, MountInodeIDSet> mEntries;

    // What inode are we invalidating?
    MountInodeID mID;

    // What invalidations do we need to perform?
    InvalidationFlags mFlags;
}; // Invalidation

auto InodeInvalidator::invalidation(ActivityMonitor& activities,
                                    MountInodeID id) -> Invalidation&
{
    // Has an invalidation already been queued for this inode?
    auto i = mInvalidationByID.find(id);

    // An invalidation's already been queued.
    if (i != mInvalidationByID.end())
        return i->second;

    // Add an invalidation request for this inode.
    auto result =
      mInvalidationByID.emplace(std::piecewise_construct,
                                std::forward_as_tuple(id),
                                std::forward_as_tuple(activities, id));

    // Sanity.
    assert(result.second);

    // Remember when the inode should be invalidated.
    mInvalidationByOrder.emplace_back(result.first);

    // Let the worker know there's an invalidation to perform.
    mCV.notify_one();

    // Return a reference to the new invalidation.
    return result.first->second;
}

void InodeInvalidator::loop()
{
    FUSEDebug1("Inode Invalidator Worker thread started");

    std::unique_lock<std::mutex> lock(mLock);

    auto hasWork = [&]() {
        return mTerminate || !mInvalidationByID.empty();
    }; // hasWork

    while (true)
    {
        // Wait until we have some work to do.
        mCV.wait(lock, hasWork);

        // We're being terminated.
        if (mTerminate)
            return;

        // Pop an invalidation from the queue.
        auto invalidation = ([&]() {
            assert(!mInvalidationByOrder.empty());
            
            // What invalidation are we going to perform?
            auto i = mInvalidationByOrder.front();

            // Pop the invalidation.
            mInvalidationByOrder.pop_front();

            // Latch the invalidation request.
            auto invalidation = std::move(i->second);

            // Remove the request from our map.
            mInvalidationByID.erase(i);

            // Return invalidation request to caller.
            return invalidation;
        })();

        // Release the lock so pending invalidations can be modified.
        lock.unlock();

        // Invalidate the inode.
        invalidation.invalidate(mSession);

        // Reacquire lock.
        lock.lock();
    }

    FUSEDebug1("Inode Invalidator Worker thread stopped");
}

InodeInvalidator::InodeInvalidator(Session& session)
  : mCV()
  , mInvalidationByID()
  , mInvalidationByOrder()
  , mLock()
  , mSession(session)
  , mTerminate{false}
  , mWorker(&InodeInvalidator::loop, this)
{
    FUSEDebug1("Inode Invalidator constructed");
}

InodeInvalidator::~InodeInvalidator()
{
    // Let the worker know it has to terminate.
    mTerminate = true;
    
    // Wake up the worker if's sleeping.
    mCV.notify_one();

    // Wait for the worker to terminate.
    mWorker.join();

    FUSEDebug1("Inode Invalidator destroyed");
}

void InodeInvalidator::invalidateAttributes(ActivityMonitor& activities,
                                            MountInodeID id)
{
    std::lock_guard<std::mutex> guard(mLock);

    // Get our hands on this inode's invalidation.
    auto& invalidation = this->invalidation(activities, id);

    // Signal that we need to invalidate this inode's attributes.
    invalidation.mFlags |= IF_ATTRIBUTES;
}

void InodeInvalidator::invalidateEntry(ActivityMonitor& activities,
                                       MountInodeID child,
                                       const std::string& name,
                                       MountInodeID parent)
{
    // Sanity.
    assert(!name.empty());

    std::lock_guard<std::mutex> guard(mLock);

    // Get our hands on this inode's invalidation.
    auto& invalidation = this->invalidation(activities, parent);

    // Record which entry we want to invalidate.
    invalidation.mEntries[name].emplace(child);
}

void InodeInvalidator::invalidateEntry(ActivityMonitor& activities,
                                       MountInodeID id,
                                       const std::string& name)
{
    // Sanity.
    assert(!name.empty());

    std::lock_guard<std::mutex> guard(mLock);

    // Get our hands on this inode's invalidation.
    auto& invalidation = this->invalidation(activities, id);

    // Record which entry we want to invalidate.
    invalidation.mEntries[name];
}

void InodeInvalidator::invalidateData(ActivityMonitor& activities,
                                      MountInodeID id,
                                      m_off_t offset,
                                      m_off_t size)
{
    // Sanity.
    assert(offset >= 0);
    assert(size >= 0);

    std::lock_guard<std::mutex> guard(mLock);

    // Get our hands on this inode's invalidation.
    auto& invalidation = this->invalidation(activities, id);

    // Signal that we want to invalidate this inode's data.
    invalidation.mFlags |= IF_DATA;

    // Convenience.
    auto& begin = invalidation.mData.mBegin;
    auto& end = invalidation.mData.mEnd;

    // Specify what data we want to invalidate.
    begin = std::min(begin, offset);
    end = std::max(end, offset + size);
}

} // platform
} // fuse
} // mega

