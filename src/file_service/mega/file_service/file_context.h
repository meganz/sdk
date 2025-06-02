#pragma once

#include <mega/common/activity_monitor.h>
#include <mega/file_service/buffer_pointer.h>
#include <mega/file_service/file_context_forward.h>
#include <mega/file_service/file_context_pointer.h>
#include <mega/file_service/file_forward.h>
#include <mega/file_service/file_info_context_pointer.h>
#include <mega/file_service/file_info_forward.h>
#include <mega/file_service/file_range_context_manager.h>
#include <mega/file_service/file_range_context_pointer_map.h>
#include <mega/file_service/file_range_forward.h>
#include <mega/file_service/file_range_vector.h>
#include <mega/file_service/file_read_request_forward.h>
#include <mega/file_service/file_read_write_state.h>
#include <mega/file_service/file_service_context_forward.h>
#include <mega/types.h>

#include <forward_list>
#include <memory>
#include <mutex>
#include <variant>

namespace mega
{
namespace file_service
{

class FileContext final: FileRangeContextManager, public std::enable_shared_from_this<FileContext>
{
    // Convenience.
    using FileRequest = std::variant<FileReadRequest>;
    using FileRequestList = std::forward_list<FileRequest>;

    // Adjust this file's reference count.
    void adjustRef(std::int64_t adjustment);

    // Cancel a pending request.
    void cancel(FileRequest& request);

    // Cancel any downloads and pending requests.
    void cancel();

    // Called when a file range has been downloaded.
    void completed(Buffer& buffer,
                   FileRangeContextPtrMap::Iterator iterator,
                   const FileRange& range) override;

    // Called when a file read request has been completed.
    void completed(BufferPtr buffer, FileReadRequest& request) override;

    // Try and execute a read request.
    bool execute(FileReadRequest& request);

    // Try and execute a request.
    bool execute(FileRequest& request);

    // Execute zero or more queued requests.
    void execute();

    // Called when a file read request has failed.
    void failed(FileReadRequest& request, FileResult result) override;

    // Acquire a lock on this manager.
    std::unique_lock<std::recursive_mutex> lock() const override;

    // Return a reference to the mutex protecting this manager.
    virtual std::recursive_mutex& mutex() const override;

    // Queue a request for later execution.
    template<typename Request>
    void queue(Request&& request);

    // Keep our service alive until we're dead.
    common::Activity mActivity;

    // Wraps mFile and unifies logic.
    BufferPtr mBuffer;

    // How we get and set our file's attributes.
    FileInfoContextPtr mInfo;

    // The file storing our data.
    FileAccessPtr mFile;

    // What ranges of the file do we have?
    FileRangeContextPtrMap mRanges;

    // Serializes access to mRanges.
    mutable std::recursive_mutex mRangesLock;

    // Tracks whether any reads or writes are in progress.
    FileReadWriteState mReadWriteState;

    // Tracks pending requests.
    FileRequestList mRequests;

    // Serializes access to mRequests.
    std::recursive_mutex mRequestsLock;

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

    // Retrieve information about this file.
    FileInfo info() const;

    // What ranges of this file are currently in storage?
    FileRangeVector ranges() const;

    // Read data from this file.
    void read(FileReadRequest request);

    // Let the service know you want it to keep this file in storage.
    void ref();

    // Let the service know you're happy for it to remove this file.
    void unref();
}; // FileContext

} // file_service
} // mega
