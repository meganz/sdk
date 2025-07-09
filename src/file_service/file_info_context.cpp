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

FileInfoContext::FileInfoContext(Activity activity,
                                 bool dirty,
                                 NodeHandle handle,
                                 FileID id,
                                 std::int64_t modified,
                                 FileServiceContext& service,
                                 std::uint64_t size):
    mActivity(std::move(activity)),
    mDirty(dirty),
    mHandle(handle),
    mID(id),
    mLock(),
    mModified(modified),
    mObservers(),
    mObserversLock(),
    mService(service),
    mSize(size)
{}

FileInfoContext::~FileInfoContext()
{
    mService.removeFromIndex(FileInfoContextBadge(), mID);
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

void FileInfoContext::modified(std::int64_t modified)
{
    // Update the file's modification time and return a new event.
    notify(
        [modified, this]()
        {
            // Make sure no one else is changing this file's information.
            UniqueLock guard(mLock);

            // Mark file as having been locally modified.
            mDirty = true;

            // Update the file's modification time.
            mModified = modified;

            // Return an event to our caller.
            return FileEvent{std::nullopt, mModified, mSize};
        }());
}

std::int64_t FileInfoContext::modified() const
{
    return get(&FileInfoContext::mModified);
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

std::uint64_t FileInfoContext::size() const
{
    return get(&FileInfoContext::mSize);
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

            // Update the file's modification time.
            mModified = modified;

            // Update the file's size.
            swap(mSize, size);

            // Assume the file's size hasn't decreased.
            FileEvent event{std::nullopt, mModified, mSize};

            // File's size has decreased.
            if (mSize < size)
                event.mRange.emplace(mSize, size);

            // Return event to our caller.
            return event;
        }());
}

void FileInfoContext::written(const FileRange& range, std::int64_t modified)
{
    // Update the file's information and return an event for notification.
    notify(
        [&range, modified, this]()
        {
            // Make sure we have exclusive access to our information.
            UniqueLock guard(mLock);

            // Mark file as having been locally modified.
            mDirty = true;

            // Update the file's modification time.
            mModified = modified;

            // Extend the file's size if necessary.
            mSize = std::max(mSize, range.mEnd);

            // Return a suitable event.
            return FileEvent{range, modified, mSize};
        }());
}

} // file_service
} // mega
