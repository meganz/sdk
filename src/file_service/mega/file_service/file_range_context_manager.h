#pragma once

#include <mega/file_service/buffer_forward.h>
#include <mega/file_service/file_range_context_manager_forward.h>
#include <mega/file_service/file_range_context_pointer_map.h>
#include <mega/file_service/file_range_forward.h>
#include <mega/file_service/file_read_request_forward.h>
#include <mega/file_service/file_result_forward.h>

#include <mutex>

namespace mega
{
namespace file_service
{

class FileRangeContextManager
{
protected:
    FileRangeContextManager() = default;

    ~FileRangeContextManager() = default;

public:
    // Called when a file range has been downloaded.
    virtual void completed(Buffer& buffer,
                           FileRangeContextPtrMap::Iterator iterator,
                           FileRange range) = 0;

    // Called when a file read request has been completed.
    virtual void completed(BufferPtr buffer, FileReadRequest&& request) = 0;

    // Called when a file read request has failed.
    virtual void failed(FileReadRequest&& request, FileResult result) = 0;

    // Acquire a lock on this manager.
    virtual std::unique_lock<std::recursive_mutex> lock() const = 0;

    // Return a reference to the mutex protecting this manager.
    virtual std::recursive_mutex& mutex() const = 0;
}; // FileRangeContextManager

} // file_service
} // mega
