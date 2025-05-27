#pragma once

#include <mega/common/client_forward.h>
#include <mega/common/shared_mutex.h>
#include <mega/file_service/file_forward.h>
#include <mega/file_service/file_id_forward.h>
#include <mega/file_service/file_info_forward.h>
#include <mega/file_service/file_service_context_pointer.h>
#include <mega/file_service/file_service_forward.h>
#include <mega/file_service/file_service_result_forward.h>
#include <mega/file_service/file_service_result_or_forward.h>

namespace mega
{
namespace file_service
{

class FileService
{
    FileServiceContextPtr mContext;
    common::SharedMutex mContextLock;

public:
    FileService();

    ~FileService();

    // Deinitialize the file service.
    void deinitialize();

    // Retrieve information about a file managed by the file service.
    auto info(FileID id) -> FileServiceResultOr<FileInfo>;

    // Initialize the file service.
    auto initialize(common::Client& client) -> FileServiceResult;

    // Open a file for reading or writing.
    auto open(FileID id) -> FileServiceResultOr<File>;
}; // FileService

} // file_service
} // mega
