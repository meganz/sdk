#include <mega/common/lock.h>
#include <mega/common/utility.h>
#include <mega/file_service/file_event.h>
#include <mega/file_service/file_info_context.h>
#include <mega/file_service/file_info_context_badge.h>
#include <mega/file_service/file_range.h>
#include <mega/file_service/file_service_context.h>
#include <mega/filesystem.h>

#include <utility>

namespace mega
{
namespace file_service
{

using namespace common;

template<typename T>
auto FileInfoContext::get(T FileInfoContext::* const property) const
{
    SharedLock guard(mLock);

    return this->*property;
}

void FileInfoContext::notify(const FileEvent& event)
{
    // Make sure no one's modifying our map of observers.
    std::lock_guard guard(mObserversLock);

    // Transmit event to each observer.
    for (auto i = mObservers.begin(); i != mObservers.end();)
    {
        // Just in case the observer removes itself.
        auto j = i++;

        // Transmit event to our observer.
        j->second(event);
    }
}

FileInfoContext::FileInfoContext(std::int64_t accessed,
                                 Activity activity,
                                 bool dirty,
                                 NodeHandle handle,
                                 FileID id,
                                 std::uint64_t logicalSize,
                                 std::int64_t modified,
                                 std::uint64_t physicalSize,
                                 FileServiceContext& service):
    mAccessed(accessed),
    mActivity(std::move(activity)),
    mDirty(dirty),
    mHandle(handle),
    mID(id),
    mLock(),
    mLogicalSize(logicalSize),
    mModified(modified),
    mPhysicalSize(physicalSize),
    mObservers(),
    mObserversLock(),
    mService(service)
{}

FileInfoContext::~FileInfoContext()
{
    mService.removeFromIndex(FileInfoContextBadge(), *this);
}

FileEventObserverID FileInfoContext::addObserver(FileEventObserver observer)
{
    // Should be sufficient.
    static std::atomic<FileEventObserverID> next{0ul};

    // Sanity.
    assert(observer);

    // Make sure no one else is messing with our observers.
    std::lock_guard guard(mObserversLock);

    // Add the observer to our map.
    auto [iterator, added] = mObservers.emplace(next.fetch_add(1), std::move(observer));

    // Return the observer's ID to our caller.
    return iterator->first;
}

void FileInfoContext::accessed(std::int64_t accessed)
{
    std::lock_guard guard(mLock);

    mAccessed = std::max(accessed, mAccessed);
}

std::int64_t FileInfoContext::accessed() const
{
    return get(&FileInfoContext::mAccessed);
}

bool FileInfoContext::dirty() const
{
    return get(&FileInfoContext::mDirty);
}

void FileInfoContext::handle(NodeHandle handle)
{
    UniqueLock guard(mLock);

    // Update this file's node handle.
    mHandle = handle;
}

NodeHandle FileInfoContext::handle() const
{
    return get(&FileInfoContext::mHandle);
}

FileID FileInfoContext::id() const
{
    return mID;
}

std::uint64_t FileInfoContext::logicalSize() const
{
    return get(&FileInfoContext::mLogicalSize);
}

void FileInfoContext::modified(std::int64_t accessed, std::int64_t modified)
{
    // Update the file's modification time and return a new event.
    notify(
        [accessed, modified, this]()
        {
            // Make sure no one else is changing this file's information.
            UniqueLock guard(mLock);

            // Mark file as having been locally modified.
            mDirty = true;

            // Update the file's access time.
            mAccessed = std::max(accessed, mAccessed);

            // Update the file's modification time.
            mModified = modified;

            // Return an event to our caller.
            return FileEvent{std::nullopt, mModified, mLogicalSize};
        }());
}

std::int64_t FileInfoContext::modified() const
{
    return get(&FileInfoContext::mModified);
}

void FileInfoContext::physicalSize(std::uint64_t physicalSize)
{
    std::lock_guard guard(mLock);

    mPhysicalSize = physicalSize;
}

std::uint64_t FileInfoContext::physicalSize() const
{
    return get(&FileInfoContext::mPhysicalSize);
}

void FileInfoContext::removeObserver(FileEventObserverID id)
{
    // Make sure no one else is messing with our observers.
    std::lock_guard guard(mObserversLock);

    // Remove the observer from our map.
    [[maybe_unused]] auto count = mObservers.erase(id);

    // Sanity.
    assert(count);
}

void FileInfoContext::truncated(std::int64_t modified, std::uint64_t size)
{
    // Update the file's information and return an event for notification.
    notify(
        [modified, size, this]() mutable
        {
            // Convenience.
            using std::swap;

            // Make sure no one else changes this file's information.
            UniqueLock guard(mLock);

            // Mark file as having been locally modified.
            mDirty = true;

            // Update the file's access time.
            mAccessed = std::max(mAccessed, modified);

            // Update the file's modification time.
            mModified = modified;

            // Update the file's size.
            swap(mLogicalSize, size);

            // Assume the file's size hasn't decreased.
            FileEvent event{std::nullopt, mModified, mLogicalSize};

            // File's size has decreased.
            if (mLogicalSize < size)
                event.mRange.emplace(mLogicalSize, size);

            // Return event to our caller.
            return event;
        }());
}

void FileInfoContext::written(const FileRange& range, std::int64_t modified)
{
    // Update the file's information and return an event for notification.
    notify(
        [modified, &range, this]()
        {
            // Make sure we have exclusive access to our information.
            UniqueLock guard(mLock);

            // Mark file as having been locally modified.
            mDirty = true;

            // Update the file's access time.
            mAccessed = std::max(mAccessed, modified);

            // Update the file's modification time.
            mModified = modified;

            // Extend the file's size if necessary.
            mLogicalSize = std::max(mLogicalSize, range.mEnd);

            // Return a suitable event.
            return FileEvent{range, modified, mLogicalSize};
        }());
}

} // file_service
} // mega
