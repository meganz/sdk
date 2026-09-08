#include <mega/common/concurrent_execution_policy.h>
#include <mega/common/lock.h>
#include <mega/common/utility.h>
#include <mega/file_service/application_data.h>
#include <mega/file_service/file_event.h>
#include <mega/file_service/file_info_context.h>
#include <mega/file_service/file_info_context_badge.h>
#include <mega/file_service/file_range.h>
#include <mega/file_service/file_service_context.h>
#include <mega/file_service/logger.h>
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

FileInfoContext::FileInfoContext(std::int64_t accessed,
                                 Activity activity,
                                 std::uint64_t allocatedSize,
                                 bool dirty,
                                 std::optional<std::uint32_t> duration,
                                 NodeHandle handle,
                                 FileID id,
                                 bool managed,
                                 std::int64_t modified,
                                 std::uint64_t reportedSize,
                                 FileServiceContext& service,
                                 std::uint64_t size):
    mInstanceLogger("FileInfoContext", *this, logger()),
    mAccessed(accessed),
    mActivity(std::move(activity)),
    mAllocatedSize(allocatedSize),
    mDirty(dirty),
    mDuration(duration),
    mHandle(handle),
    mID(id),
    mLock(),
    mManaged(managed),
    mModified(modified),
    mRemoved(false),
    mReportedSize(reportedSize),
    mService(service),
    mSize(size),
    mStructureMutex(ConcurrentExecutionPolicy(mService.executor()))
{
    // Sanity: If the file's duration is specified, it must not be zero.
    assert(!mDuration || *mDuration);
}

FileInfoContext::~FileInfoContext()
{
    mService.removeFromIndex(FileInfoContextBadge(), *this);
}

std::int64_t FileInfoContext::accessed(std::int64_t accessed)
{
    std::lock_guard guard(mLock);

    mAccessed = std::max(accessed, mAccessed);

    return mAccessed;
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

std::optional<std::uint64_t> FileInfoContext::bitrate() const
{
    // Can't compute bitrate without duration.
    if (!mDuration.value_or(0))
        return std::nullopt;

    // Compute estimated bitrate.
    auto rate = size() * 8 / *mDuration;

    // Return estimated bitrate to our caller.
    return rate;
}

bool FileInfoContext::dirty() const
{
    return get(&FileInfoContext::mDirty);
}

std::optional<std::uint32_t> FileInfoContext::duration() const
{
    return mDuration;
}

void FileInfoContext::flushed(NodeHandle handle)
{
    // Sanity.
    assert(!handle.isUndef());

    // Let observers know the file's been flushed.
    mService.notify(
        [=]()
        {
            // Make sure no one else is changing our information.
            std::lock_guard guard(mLock);

            // Flushed files are never dirty.
            mDirty = false;

            // Update the file's node handle.
            mHandle = handle;

            return FileFlushEvent{handle, mID};
        }());
}

NodeHandle FileInfoContext::handle() const
{
    return get(&FileInfoContext::mHandle);
}

FileID FileInfoContext::id() const
{
    return mID;
}

void FileInfoContext::lock_shared() const
{
    mLock.lock_shared();
}

void FileInfoContext::managed(bool managed)
{
    set(&FileInfoContext::mManaged, managed);
}

bool FileInfoContext::managed() const
{
    return get(&FileInfoContext::mManaged);
}

void FileInfoContext::modified(std::int64_t accessed,
                               const ApplicationData& applicationData,
                               std::int64_t modified)
{
    // Update the file's modification time and return a new event.
    mService.notify(
        [accessed, &applicationData, modified, this]()
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
            return FileTouchEvent{applicationData, mID, mModified};
        }());
}

std::int64_t FileInfoContext::modified() const
{
    return get(&FileInfoContext::mModified);
}

void FileInfoContext::removed(std::true_type)
{
    // Update removed status and return an event for notification.
    mService.notify(
        [&]()
        {
            // Make sure no one else is changing this file's information.
            UniqueLock guard(mLock);

            // Mark this file as removed.
            mRemoved = true;

            // Return event to our caller.
            return FileRemoveEvent{mHandle, mID};
        }());
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

FileStructureMutex& FileInfoContext::structureMutex() const
{
    return mStructureMutex;
}

void FileInfoContext::truncated(const ApplicationData& applicationData,
                                std::int64_t modified,
                                std::uint64_t size)
{
    // Update the file's information and return an event for notification.
    mService.notify(
        [&applicationData, modified, size, this]() mutable
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
            FileTruncateEvent event{applicationData, std::nullopt, mID, mSize};

            // File's size has decreased.
            if (mSize < size)
                event.mRange.emplace(mSize, size);

            // Return event to our caller.
            return event;
        }());
}

void FileInfoContext::unlock_shared() const
{
    mLock.unlock_shared();
}

void FileInfoContext::written(const ApplicationData& applicationData,
                              std::int64_t modified,
                              const FileRange& range)
{
    // Update the file's information and return an event for notification.
    mService.notify(
        [&applicationData, modified, &range, this]()
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
            return FileWriteEvent{applicationData, range, mID};
        }());
}

} // file_service
} // mega
