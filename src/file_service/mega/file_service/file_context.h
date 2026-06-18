#pragma once

#include <mega/auto_file_handle.h>
#include <mega/common/activity_monitor.h>
#include <mega/common/database_forward.h>
#include <mega/common/instance_logger.h>
#include <mega/common/lock_forward.h>
#include <mega/common/node_key_data.h>
#include <mega/common/statistics.h>
#include <mega/common/task_queue.h>
#include <mega/common/transaction_forward.h>
#include <mega/file_service/buffer_pointer.h>
#include <mega/file_service/file_append_request_forward.h>
#include <mega/file_service/file_buffer_pointer.h>
#include <mega/file_service/file_callbacks.h>
#include <mega/file_service/file_context_forward.h>
#include <mega/file_service/file_context_pointer.h>
#include <mega/file_service/file_event_observer.h>
#include <mega/file_service/file_event_observer_id.h>
#include <mega/file_service/file_fetch_request_forward.h>
#include <mega/file_service/file_flush_request_forward.h>
#include <mega/file_service/file_forward.h>
#include <mega/file_service/file_info_context_pointer.h>
#include <mega/file_service/file_info_forward.h>
#include <mega/file_service/file_range_forward.h>
#include <mega/file_service/file_range_map.h>
#include <mega/file_service/file_range_set.h>
#include <mega/file_service/file_range_vector.h>
#include <mega/file_service/file_read_request_forward.h>
#include <mega/file_service/file_read_request_set.h>
#include <mega/file_service/file_read_write_state.h>
#include <mega/file_service/file_reclaim_request_forward.h>
#include <mega/file_service/file_remove_request_forward.h>
#include <mega/file_service/file_request_list.h>
#include <mega/file_service/file_request_traits.h>
#include <mega/file_service/file_service_context_forward.h>
#include <mega/file_service/file_service_options_forward.h>
#include <mega/file_service/file_touch_request_forward.h>
#include <mega/file_service/file_truncate_request.h>
#include <mega/file_service/file_write_request_forward.h>
#include <mega/types.h>

#include <chrono>
#include <cstdint>
#include <future>
#include <list>
#include <memory>
#include <mutex>
#include <type_traits>

namespace mega
{
namespace file_service
{

class FileContext final: public std::enable_shared_from_this<FileContext>
{
    // Tracks state necessary for download.
    class DownloadContext;

    // Tracks state necessary for fetch.
    class FetchContext;

    // Tracks state necessary for flush.
    class FlushContext;

    // Tracks state necessary for reclaim.
    class ReclaimContext;

    // Convenience.
    using DownloadContextPtr = std::shared_ptr<DownloadContext>;
    using FetchContextPtr = std::shared_ptr<FetchContext>;
    using FlushContextPtr = std::shared_ptr<FlushContext>;
    using FlushContextWeakPtr = std::weak_ptr<FlushContext>;
    using ReclaimContextPtr = std::shared_ptr<ReclaimContext>;

    // Add a range to the database.
    void addRange(const FileRange& range, common::Transaction& transaction);

    // Cancel all downloads contained within the specified range.
    auto cancel(const FileRangeMap<DownloadContextPtr>& downloading,
                const FileRange& range) -> std::list<DownloadContextPtr>;

    auto cancel(const FileRange& range) -> std::future<void>;

    // Cancel a pending request.
    void cancel(FileRequest& request);

    // Cancel any downloads and pending requests.
    void cancel();

    // Called when a file read request has been completed.
    void completed(BufferPtr buffer, FileReadRequest&& request);

    // Called when a file request has been completed.
    template<typename Request, typename Result, typename... Captures>
    auto completed(Request&& request, Result result, Captures&&... captures)
        -> std::enable_if_t<IsFileRequestV<Request>>;

    // Called to complete all file read requests within range.
    void completed(const FileRange& range);

    // Called when a file write request has been completed.
    void completed(FileWriteRequest&& request);

    // Dispatch all read requests within range.
    template<typename Dispatcher>
    void dispatch(Dispatcher&& dispatcher, const FileRange& range);

    // Large download bitrate
    std::uint64_t downloadBitrate() const;

    // Check if a request can be executed.
    bool executable(std::unique_lock<std::mutex>& lock, bool queuing, const FileRequest& request);

    // Check if a particular class of request can be executed.
    bool executable(std::unique_lock<std::mutex>& lock, bool queuing, FileReadRequestTag tag);
    bool executable(std::unique_lock<std::mutex>& lock, bool queuing, FileWriteRequestTag tag);

    // Try and execute an append request.
    void execute(FileAppendRequest& request);

    // Try and execute a fetch request.
    void execute(FileFetchRequest& request);

    // Try and execute a flush request.
    void execute(FileFlushRequest request);

    // Try and execute a read request.
    void execute(FileReadRequest& request);

    // Try and execute a reclaim request.
    void execute(FileReclaimRequest& request);

    // Try and execute a remove request.
    void execute(FileRemoveRequest& request);

    // Try and execute a touch request.
    void execute(FileTouchRequest& request);

    // Try and execute a truncate request.
    void execute(FileTruncateRequest& request);

    // Try and execute a write request.
    void execute(FileWriteRequest& request);

    // Try and execute a request.
    void execute(FileRequest& request);

    // Execute zero or more queued requests.
    void execute();

    // Execute a request, translating any uncaught exceptions into an error code.
    template<typename Request>
    auto executeCommon(Request&& request) -> std::enable_if_t<IsFileRequestV<Request>>;

    // Execute a request if possible otherwise queue it for later execution.
    template<typename Request>
    auto executeOrQueue(Request&& request) -> std::enable_if_t<IsFileRequestV<Request>>;

    // Called when a request of a particular class has executed.
    void executed(FileReadRequestTag tag);
    void executed(FileWriteRequestTag tag);

    // Called to fail all read requests within range.
    void failed(const FileRange& range, FileResult result);

    // Increase this file's size.
    auto grow(std::uint64_t newSize, std::uint64_t oldSize)
        -> std::pair<common::UniqueLock<common::Database>, common::Transaction>;

    // Queue a request for later execution.
    template<typename Request>
    auto queue(std::unique_lock<std::mutex> lock, Request&& request)
        -> std::enable_if_t<IsFileRequestV<Request>>;

    // Return an error if this request should be rejected.
    template<typename Request>
    auto reject(const Request& request) -> std::enable_if_t<IsFileRequestV<Request>, FileResult>;

    // Remove zero or more ranges from the database.
    void removeRanges(const FileRange& range, common::Transaction& transaction);

    // Mark the file as removed.
    FileResult setRemoved(bool replaced);

    // Decrease this file's size.
    auto shrink(std::uint64_t newSize, std::uint64_t oldSize)
        -> std::pair<common::UniqueLock<common::Database>, common::Transaction>;

    // Update this file's access time in the database.
    void updateAccessTime(std::int64_t accessed, common::Transaction& transaction);

    // Update this file's access and modification time in the database.
    void updateAccessAndModificationTimes(std::int64_t accessed,
                                          std::int64_t modified,
                                          common::Transaction& transaction);

    // Called after a range has been written to disk.
    FileRange updateRanges(FileRange range, common::Transaction& transaction);

    // Update the file's sizes in the database.
    void updateSize(std::uint64_t size, common::Transaction& transaction);

    // Logs instance lifetime.
    common::InstanceLogger<FileContext> mInstanceLogger;

    // Keep our service alive until we're dead.
    common::Activity mActivity;

    // What's the average bitrate of our large downloads?
    common::EmaInteger mAverageLargeDownloadBitrate;

    // Wraps mFile and unifies logic.
    FileBufferPtr mBuffer;

    // What ranges are currently being downloaded?
    struct
    {
        FileRangeMap<DownloadContextPtr> mLarge;
        FileRangeMap<DownloadContextPtr> mSmall;
    } mDownloading;

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

    // The file's decryption key, IV and authentication tokens.
    const std::optional<common::NodeKeyData> mKeyData;

    // Serializes access to mDownloading, mOnDisk and mPendingReadRequests.
    mutable std::recursive_mutex mLock;

    // What ranges are present on disk?
    FileRangeSet mOnDisk;

    // Read requests pending completion.
    FileReadRequestSet mPendingReadRequests;

    // Task pinning this context in memory, if any.
    common::Task mPinTask;

    // Serializes access to mPinTask.
    std::recursive_mutex mPinTaskLock;

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

    // Keeps us alive until all of our downloads have completed.
    common::ActivityMonitor mDownloadMonitor;

    // Keeps us alive until all non-download operations have completed.
    common::ActivityMonitor mMonitor;

public:
    FileContext(common::Activity activity,
                FileAccessPtr file,
                FileInfoContextPtr info,
                std::optional<common::NodeKeyData> keyData,
                FileRangeSet ranges,
                FileServiceContext& service);

    ~FileContext();

    // Notify an observer when this file's information changes.
    FileEventObserverID addObserver(FileEventObserver observer);

    // Append data to the end of this file.
    void append(FileContextPtr context, FileAppendRequest request);

    // Duplicate OS file descriptor of storage file, return an unset AutoFileHandle on errors
    AutoFileHandle dupFileDescriptor();

    // What ranges are currently being downloaded?
    FileRangeVector downloading() const;

    // Fetch all of this file's data from the cloud.
    void fetch(FileContextPtr context, FileFetchRequest request);

    // Wait until all fetches in progress have completed.
    void fetchBarrier(FileFetchBarrierCallback callback, FileContextPtr context);

    // Flush this file's local modifications to the cloud.
    void flush(FileContextPtr context, FileFlushRequest request);

    // Retrieve information about this file.
    FileInfo info() const;

    // Pin this context in memory for a specified period of time.
    template<typename Rep, typename Duration>
    void pinFor(FileContextPtr context, std::chrono::duration<Rep, Duration> period)
    {
        pinUntil(std::move(context), std::chrono::steady_clock::now() + period);
    }

    // Pin this context in memory until the specified time.
    void pinUntil(FileContextPtr context, std::chrono::steady_clock::time_point when);

    // Return a copy of the service's current options.
    ServiceOptions serviceOptions() const;

    // What ranges of this file are currently in storage?
    FileRangeVector ranges() const;

    // Read data from this file.
    void read(FileContextPtr context, FileReadRequest request);

    // Reclaim this file's storage.
    void reclaim(FileReclaimCallback callback, FileContextPtr context);

    // Remove this file.
    void remove(FileContextPtr context, FileRemoveRequest request);

    // Remove a previously added observer.
    void removeObserver(FileEventObserverID id);

    // Check if this file has been removed.
    bool removed() const;

    // Update the file's modification time.
    void touch(FileContextPtr context, FileTouchRequest request);

    // Truncate this file to a specified size.
    void truncate(FileContextPtr context, FileTruncateRequest request);

    // Write data to this file.
    void write(FileContextPtr context, FileWriteRequest request);
}; // FileContext

} // file_service
} // mega
