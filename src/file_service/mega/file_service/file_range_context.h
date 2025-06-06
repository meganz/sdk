#pragma once

#include <mega/common/activity_monitor.h>
#include <mega/common/client_forward.h>
#include <mega/common/partial_download_callback.h>
#include <mega/common/partial_download_forward.h>
#include <mega/file_service/buffer_pointer.h>
#include <mega/file_service/file_range_context_forward.h>
#include <mega/file_service/file_range_context_manager_forward.h>
#include <mega/file_service/file_range_context_pointer_map.h>
#include <mega/file_service/file_read_request_forward.h>
#include <mega/file_service/file_read_request_set.h>

#include <cstdint>
#include <mutex>

namespace mega
{

class Error;
class NodeHandle;
struct FileAccess;

namespace file_service
{

class FileRangeContext: private common::PartialDownloadCallback
{
    // Called when the file range has been downloaded.
    template<typename Lock>
    void completed(Lock&& lock, Error result);

    void completed(Error result) override;

    // Called repeatedly as data is donwloaded from the cloud.
    auto data(const void* buffer, std::uint64_t offset, std::uint64_t length)
        -> std::variant<Abort, Continue> override;

    // Dispatch zero or more read requests.
    void dispatch();

    // Check if a request can be dispatched.
    bool dispatchable(const FileReadRequest& request) const;

    // Called when our download's encountered a failure.
    virtual auto failed(Error result, int retries) -> std::variant<Abort, Retry> override;

    // Keeps our manager alive until we're dead.
    common::Activity mActivity;

    // The buffer containing our downloaded data.
    BufferPtr mBuffer;

    // The download that's retrieving this file range's data.
    common::PartialDownloadPtr mDownload;

    // Where does our downloaded data currently end?
    std::uint64_t mEnd;

    // Where are we in our manager's map of contexts?
    FileRangeContextPtrMap::Iterator mIterator;

    // Who's responsible for this context?
    FileRangeContextManager& mManager;

    // Requests pending completion.
    FileReadRequestSet mRequests;

public:
    FileRangeContext(common::Activity activity,
                     FileRangeContextPtrMap::Iterator iterator,
                     FileRangeContextManager& manager);

    ~FileRangeContext();

    // Cancel this range's download.
    void cancel();

    // Create a download this range.
    auto download(common::Client& client, FileAccess& file, NodeHandle handle)
        -> common::PartialDownloadPtr;

    // Queue a file read request for later completion.
    void queue(FileReadRequest request);
}; // FileRangeContext

} // file_service
} // mega
