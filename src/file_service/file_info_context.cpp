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

template<typename T, typename U>
void FileInfoContext::set(T FileInfoContext::*property, U&& value)
{
    std::lock_guard guard(mLock);

    this->*property = std::forward<U>(value);
}

void FileInfoContext::notify(const FileEvent& event)
{
    // Notify observers interested in this particular file.
    FileEventEmitter::notify(event);

    // Notify observers interested in all files.
    mService.notify(event);
}

FileInfoContext::FileInfoContext(std::int64_t accessed,
                                 Activity activity,
                                 std::uint64_t allocatedSize,
                                 bool dirty,
                                 NodeHandle handle,
                                 FileID id,
                                 const FileLocation& location,
                                 std::int64_t modified,
                                 std::uint64_t reportedSize,
                                 FileServiceContext& service,
                                 std::uint64_t size):
    FileEventEmitter(),
    mAccessed(accessed),
    mActivity(std::move(activity)),
    mAllocatedSize(allocatedSize),
    mDirty(dirty),
    mHandle(handle),
    mID(id),
    mLocation(location),
    mLock(),
    mModified(modified),
    mRemoved(false),
    mReportedSize(reportedSize),
    mService(service),
    mSize(size)
{}

FileInfoContext::~FileInfoContext()
{
    mService.removeFromIndex(FileInfoContextBadge(), *this);
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

void FileInfoContext::allocatedSize(std::uint64_t allocatedSize)
{
    set(&FileInfoContext::mAllocatedSize, allocatedSize);
}

std::uint64_t FileInfoContext::allocatedSize() const
{
    return get(&FileInfoContext::mAllocatedSize);
}

bool FileInfoContext::dirty() const
{
    return get(&FileInfoContext::mDirty);
}

void FileInfoContext::flushed(NodeHandle handle)
{
    // Sanity.
    assert(!handle.isUndef());

    // Update the file's node handle.
    set(&FileInfoContext::mHandle, handle);

    // Let observers know the file's been flushed.
    notify(FileFlushEvent{handle, mID});
}

NodeHandle FileInfoContext::handle() const
{
    return get(&FileInfoContext::mHandle);
}

FileID FileInfoContext::id() const
{
    return mID;
}

void FileInfoContext::location(const FileLocation& location)
{
    set(&FileInfoContext::mLocation, location);
}

FileLocation FileInfoContext::location() const
{
    return get(&FileInfoContext::mLocation);
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
            return FileTouchEvent{mID, mModified};
        }());
}

std::int64_t FileInfoContext::modified() const
{
    return get(&FileInfoContext::mModified);
}

void FileInfoContext::removed(bool replaced)
{
    set(&FileInfoContext::mRemoved, true);

    notify(FileRemoveEvent{mID, replaced});
}

bool FileInfoContext::removed() const
{
    return get(&FileInfoContext::mRemoved);
}

void FileInfoContext::reportedSize(std::uint64_t reportedSize)
{
    set(&FileInfoContext::mReportedSize, reportedSize);
}

std::uint64_t FileInfoContext::reportedSize() const
{
    return get(&FileInfoContext::mReportedSize);
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

            // Update the file's access time.
            mAccessed = std::max(mAccessed, modified);

            // Update the file's modification time.
            mModified = modified;

            // Update the file's size.
            swap(mSize, size);

            // Assume the file's size hasn't decreased.
            FileTruncateEvent event{std::nullopt, mID, mSize};

            // File's size has decreased.
            if (mSize < size)
                event.mRange.emplace(mSize, size);

            // Return event to our caller.
            return event;
        }());
}

void FileInfoContext::written(std::int64_t modified, const FileRange& range)
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

            // Update the file's size.
            mSize = std::max(mSize, range.mEnd);

            // Return a suitable event.
            return FileWriteEvent{range, mID};
        }());
}

} // file_service
} // mega
