#pragma once

#include <mega/common/client_forward.h>
#include <mega/common/shared_mutex.h>
#include <mega/file_service/file_forward.h>
#include <mega/file_service/file_id_forward.h>
#include <mega/file_service/file_info_forward.h>
#include <mega/file_service/file_service_context_pointer.h>
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

    auto deinitialize() -> void;

    auto info(FileID id) -> FileServiceResultOr<FileInfo>;

    auto initialize(common::Client& client) -> FileServiceResult;

    auto open(FileID id) -> FileServiceResultOr<File>;
}; // FileService

} // file_service
} // mega
