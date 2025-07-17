#pragma once

#include <mega/common/activity_monitor.h>
#include <mega/common/transaction_forward.h>
#include <mega/file_service/buffer_pointer.h>
#include <mega/file_service/file_append_request_forward.h>
#include <mega/file_service/file_buffer_pointer.h>
#include <mega/file_service/file_context_forward.h>
#include <mega/file_service/file_context_pointer.h>
#include <mega/file_service/file_event_observer.h>
#include <mega/file_service/file_event_observer_id.h>
#include <mega/file_service/file_fetch_request_forward.h>
#include <mega/file_service/file_flush_request_forward.h>
#include <mega/file_service/file_forward.h>
#include <mega/file_service/file_info_context_pointer.h>
#include <mega/file_service/file_info_forward.h>
#include <mega/file_service/file_range_context_manager.h>
#include <mega/file_service/file_range_context_pointer_map.h>
#include <mega/file_service/file_range_forward.h>
#include <mega/file_service/file_range_vector.h>
#include <mega/file_service/file_read_request_forward.h>
#include <mega/file_service/file_read_write_state.h>
#include <mega/file_service/file_reclaim_request_forward.h>
#include <mega/file_service/file_request_list.h>
#include <mega/file_service/file_request_traits.h>
#include <mega/file_service/file_service_context_forward.h>
#include <mega/file_service/file_service_options_forward.h>
#include <mega/file_service/file_touch_request_forward.h>
#include <mega/file_service/file_truncate_request.h>
#include <mega/file_service/file_write_request_forward.h>
#include <mega/types.h>

#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <type_traits>
#include <variant>

namespace mega
{
namespace file_service
{

class FileContext final: FileRangeContextManager, public std::enable_shared_from_this<FileContext>
{
    // Tracks state necessary for fetch.
    class FetchContext;

    // Tracks state necessary for flush.
    class FlushContext;

    // Tracks state necessary for reclaim.
    class ReclaimContext;

    // Convenience.
    using FetchContextPtr = std::shared_ptr<FetchContext>;
    using FlushContextPtr = std::shared_ptr<FlushContext>;
    using ReclaimContextPtr = std::shared_ptr<ReclaimContext>;

    // Add a range to the database.
    void addRange(const FileRange& range, common::Transaction& transaction);

    // Adjust this file's reference count.
    void adjustRef(std::int64_t adjustment);

    // Cancel any reads intersect the specified range.
    void cancel(const FileRange& range);

    // Cancel a pending request.
    void cancel(FileRequest& request);

    // Cancel any downloads and pending requests.
    void cancel();

    // Called when a file range has been downloaded.
    void completed(Buffer& buffer,
                   FileRangeContextPtrMap::Iterator iterator,
                   FileRange range) override;

    // Called when a file read request has been completed.
    void completed(BufferPtr buffer, FileReadRequest&& request) override;

    // Called when a file request has been completed.
    template<typename Request, typename Result, typename... Captures>
    auto completed(Request&& request, Result result, Captures&&... captures)
        -> std::enable_if_t<IsFileRequestV<Request>>;

    // Called when a file write request has been completed.
    void completed(FileWriteRequest&& request);

    // Called to execute an arbitrary function on the service's thread pool.
    void execute(std::function<void()> function) override;

    // Try and execute an append request.
    bool execute(FileAppendRequest& request);

    // Try and execute a fetch request.
    bool execute(FileFetchRequest& request);

    // Try and execute a flush request.
    bool execute(FileFlushRequest request);

    // Try and execute a read request.
    bool execute(FileReadRequest& request);

    // Try and execute a reclaim request.
    bool execute(FileReclaimRequest& request);

    // Try and execute a touch request.
    bool execute(FileTouchRequest& request);

    // Try and execute a truncate request.
    bool execute(FileTruncateRequest& request);

    // Try and execute a write request.
    bool execute(FileWriteRequest& request);

    // Try and execute a request.
    bool execute(FileRequest& request);

    // Execute zero or more queued requests.
    void execute();

    // Execute a request if possible otherwise queue it for later execution.
    template<typename Request>
    auto executeOrQueue(Request&& request) -> std::enable_if_t<IsFileRequestV<Request>>;

    // Called when a file read request has failed.
    void failed(FileReadRequest&& request, FileResult result) override;

    // Increase this file's size.
    auto grow(std::uint64_t newSize, std::uint64_t oldSize) -> common::Transaction;

    // Acquire a lock on this manager.
    std::unique_lock<std::recursive_mutex> lock() const override;

    // Return a reference to the mutex protecting this manager.
    std::recursive_mutex& mutex() const override;

    // Retrieve a copy of the service's current options.
    FileServiceOptions options() const override;

    // Queue a request for later execution.
    template<typename Request>
    auto queue(Request&& request) -> std::enable_if_t<IsFileRequestV<Request>>;

    // Remove zero or more ranges from the database.
    void removeRanges(const FileRange& range, common::Transaction& transaction);

    // Decrease this file's size.
    auto shrink(std::uint64_t newSize, std::uint64_t oldSize) -> common::Transaction;

    // Update this file's access and modification time in the database.
    void updateAccessAndModificationTimes(std::int64_t accessed,
                                          std::int64_t modified,
                                          common::Transaction& transaction);

    // Update the file's size in the database.
    void updateSize(std::uint64_t size, common::Transaction& transaction);

    // Keep our service alive until we're dead.
    common::Activity mActivity;

    // Wraps mFile and unifies logic.
    FileBufferPtr mBuffer;

    // How we get and set our file's attributes.
    FileInfoContextPtr mInfo;

    // Tracks any fetch in progress.
    FetchContextPtr mFetchContext;

    // Serializes access to mFetchContext.
    std::recursive_mutex mFetchContextLock;

    // The file storing our data.
    FileAccessPtr mFile;

    // Tracks any flush in progress.
    FlushContextPtr mFlushContext;

    // Serializes access to mFlushContext.
    std::recursive_mutex mFlushContextLock;

    // What ranges of the file do we have?
    FileRangeContextPtrMap mRanges;

    // Serializes access to mRanges.
    mutable std::recursive_mutex mRangesLock;

    // Tracks whether any reads or writes are in progress.
    FileReadWriteState mReadWriteState;

    // Tracks any reclaim in progress.
    ReclaimContextPtr mReclaimContext;

    // Serializes access to mReclaimContext.
    std::mutex mReclaimContextLock;

    // Tracks pending requests.
    FileRequestList mRequests;

    // Serializes access to mRequests.
    std::mutex mRequestsLock;

    // The service that manages this context.
    FileServiceContext& mService;

    // Keeps us alive until all of our ranges have died.
    common::ActivityMonitor mActivities;

public:
    FileContext(common::Activity activity,
                FileAccessPtr file,
                FileInfoContextPtr info,
                const FileRangeVector& ranges,
                FileServiceContext& service);

    ~FileContext();

    // Notify an observer when this file's information changes.
    FileEventObserverID addObserver(FileEventObserver observer);

    // Append data to the end of this file.
    void append(FileAppendRequest request);

    // Fetch all of this file's data from the cloud.
    void fetch(FileFetchRequest request);

    // Flush this file's local modifications to the cloud.
    void flush(FileFlushRequest request);

    // Retrieve information about this file.
    FileInfo info() const;

    // What ranges of this file are currently in storage?
    FileRangeVector ranges() const;

    // Read data from this file.
    void read(FileReadRequest request);

    // Reclaim this file's storage.
    void reclaim(FileReclaimCallback callback);

    // Let the service know you want it to keep this file in storage.
    void ref();

    // Remove a previously added observer.
    void removeObserver(FileEventObserverID id);

    // Update the file's modification time.
    void touch(FileTouchRequest request);

    // Truncate this file to a specified size.
    void truncate(FileTruncateRequest request);

    // Let the service know you're happy for it to remove this file.
    void unref();

    // Write data to this file.
    void write(FileWriteRequest request);
}; // FileContext

} // file_service
} // mega
