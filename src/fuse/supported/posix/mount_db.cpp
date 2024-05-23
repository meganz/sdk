#include <poll.h>

#include <cassert>
#include <cerrno>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include <mega/fuse/common/client.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/mount_info.h>
#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/utility.h>
#include <mega/fuse/platform/mount_db.h>
#include <mega/fuse/platform/service_context.h>
#include <mega/fuse/platform/session.h>

namespace mega
{
namespace fuse
{
namespace platform
{

// Makes dealing with fd_set a little more convenient.
class DescriptorSet
{
    // Compares a pollfd instance against a descriptor.
    static bool less(const struct pollfd& lhs, int rhs);

    // Which descriptors are we monitoring?
    std::vector<struct pollfd> mDescriptors;

public:
    DescriptorSet();

    // Add a descriptor to the set.
    template<typename T>
    void add(const T& descriptor);

    // Remove all descriptors from the set.
    void clear();

    // Remove a descriptor from the set.
    template<typename T>
    void remove(const T& descriptor);

    // Check if a descriptor has any activity.
    template<typename T>
    bool set(const T& descriptor) const;

    // Wait for activity on some descriptor.
    void wait();
}; // DescriptorSet

MountResult MountDB::check(const Client& client, const MountInfo& info) const
{
    // Convenience.
    auto& path = info.mPath;

    // User's specified a bogus path.
    if (path.empty())
    {
        FUSEError1("Invalid local path specified");

        return MOUNT_LOCAL_UNKNOWN;
    }

    // Try and retrieve the local path's type.
    auto fileAccess = client.fsAccess().newfileaccess(false);

    fileAccess->fopen(path, FSLogging::noLogging);

    // Path isn't accessible.
    if (fileAccess->type == TYPE_UNKNOWN)
    {
        FUSEErrorF("Local path doesn't exist: %s",
                   path.toPath(false).c_str());

        return MOUNT_LOCAL_UNKNOWN;
    }

    // Path denotes a file.
    if (fileAccess->type != FOLDERNODE)
    {
        FUSEErrorF("Local path is not a directory: %s",
                   path.toPath(false).c_str());

        return MOUNT_LOCAL_FILE;
    }

    // Path's okay.
    return MOUNT_SUCCESS;
}

void MountDB::dispatch()
{
    DescriptorSet descriptors;

    // Wake up when a session has been added.
    descriptors.add(mPendingAdd);

    // Wake up when a session has been removed.
    descriptors.add(mPendingRemove);

    // Wake up when we need to terminate.
    descriptors.add(mTerminate);

    // Dispatch incoming FUSE requests.
    while (true)
    {
        // Wait for some event to wake us up.
        descriptors.wait();

        // Acquire lock.
        std::unique_lock<std::mutex> lock(mLock);

        // Some sessions have been added.
        if (descriptors.set(mPendingAdd))
        {
            mPendingAdd.clear();

            // Add each new session to our map.
            for (auto& a : mPendingAdds)
            {
                // Add the session to our set.
                mSessions.emplace(a.first);

                // Monitor the session's descriptor for activity.
                descriptors.add(*a.first);

                // Let waiter know the session's been added.
                a.second.set_value();
            }

            mPendingAdds.clear();
        }

        // Some sessions have been removed.
        if (descriptors.set(mPendingRemove))
        {
            mPendingRemove.clear();

            // Remove each session from our map.
            for (auto& r : mPendingRemoves)
            {
                // We're no longer interested in this session's activity.
                descriptors.remove(*r.first);

                // Remove the session from our set.
                mSessions.erase(r.first);

                // Let waiter know the session's been removed.
                r.second.set_value();
            }

            mPendingRemoves.clear();
        }

        // We've been asked to terminate.
        if (descriptors.set(mTerminate))
        {
            // Sanity.
            assert(mPendingAdds.empty());
            assert(mPendingRemoves.empty());

            return;
        }

        // Release lock so work can proceed while we dispatch requests.
        lock.unlock();

        // Dispatch incoming requests.
        for (auto* session : mSessions)
        {
            // Session hasn't received a request.
            if (!descriptors.set(*session))
                continue;

            // Retrieve the latest request from the session.
            auto request = session->nextRequest();

            // Dispatch the request.
            session->dispatch(std::move(request));
        }
    }
}

void MountDB::doDeinitialize()
{
    // Let the dispatcher know it's time to terminate.
    mTerminate.raise();

    // Wait for the dispatcher to terminate.
    mThread.join();
}

void MountDB::loop()
{
    FUSEDebug1("Mount Request Dispatcher started");

    dispatch();

    FUSEDebug1("Mount Request Dispatcher stopped");
}

MountDB::MountDB(ServiceContext& context)
  : fuse::MountDB(context)
  , mLock()
  , mPendingAdd("PendingAdd")
  , mPendingAdds()
  , mPendingRemove("PendingRemove")
  , mPendingRemoves()
  , mSessions()
  , mTerminate("Terminate")
  , mThread(&MountDB::loop, this)
{
    FUSEDebug1("Mount DB constructed");
}

void MountDB::sessionAdded(Session& session)
{
    std::unique_lock<std::mutex> lock(mLock);

    assert(!mPendingAdds.count(&session));
    assert(!mPendingRemoves.count(&session));
    assert(!mSessions.count(&session));

    auto promise = std::promise<void>();
    auto future  = promise.get_future();

    mPendingAdds.emplace(std::piecewise_construct,
                         std::forward_as_tuple(&session),
                         std::forward_as_tuple(std::move(promise)));

    mPendingAdd.raise();

    lock.unlock();

    future.get();
}

void MountDB::sessionRemoved(Session& session)
{
    std::unique_lock<std::mutex> lock(mLock);

    assert(!mPendingAdds.count(&session));
    assert(!mPendingRemoves.count(&session));
    assert(mSessions.count(&session));

    auto promise = std::promise<void>();
    auto future  = promise.get_future();

    mPendingRemoves.emplace(std::piecewise_construct,
                            std::forward_as_tuple(&session),
                            std::forward_as_tuple(std::move(promise)));

    mPendingRemove.raise();

    lock.unlock();

    future.get();
}

bool DescriptorSet::less(const struct pollfd& lhs, int rhs)
{
    return lhs.fd < rhs;
}

DescriptorSet::DescriptorSet()
  : mDescriptors()
{
}

template<typename T>
void DescriptorSet::add(const T& descriptor)
{
    // Populate a new poll record.
    struct pollfd record = {
        descriptor.descriptor(),
        POLLIN,
        0
    }; // record

    // Where should we add our new record?
    auto i = std::lower_bound(mDescriptors.begin(),
                              mDescriptors.end(),
                              record.fd,
                              less);

    // The set should never contain any duplicates.
    assert(i == mDescriptors.end() || i->fd != record.fd);

    // Add the new record to our set.
    mDescriptors.insert(i, record);
}

template<typename T>
void DescriptorSet::remove(const T& descriptor)
{
    // Locate this descriptor's record.
    auto i = std::lower_bound(mDescriptors.begin(),
                              mDescriptors.end(),
                              descriptor.descriptor(),
                              less);

    // The descriptor must be in the set.
    assert(i != mDescriptors.end()
           && i->fd == descriptor.descriptor());

    // Remove the record from our set.
    mDescriptors.erase(i);
}

void DescriptorSet::clear()
{
    mDescriptors.clear();
}

template<typename T>
bool DescriptorSet::set(const T& descriptor) const
{
    // Locate this descriptor's record.
    auto i = std::lower_bound(mDescriptors.begin(),
                              mDescriptors.end(),
                              descriptor.descriptor(),
                              less);

    // The descriptor must be in the set.
    assert(i != mDescriptors.end()
           && i->fd == descriptor.descriptor());

    // Let the caller know if the descriptor's readable.
    return i->revents > 0;
}

void DescriptorSet::wait()
{
    while (true)
    {
        // Wait for one of our descriptors to become readable.
        auto result = poll(mDescriptors.data(), mDescriptors.size(), -1);

        // No descriptors were readable.
        if (!result)
            continue;

        // Some descriptors were readable.
        if (result > 0)
            return;

        // Call was interrupted.
        if (errno == EAGAIN || errno == EINTR)
            continue;

        // Encounterd some unexpected error waiting for activity.
        throw FUSEErrorF("Unexpected error waiting for requests: %s",
                         std::strerror(errno));
    }
}

} // platform
} // fuse
} // mega

