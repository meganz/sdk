#pragma once

#include <mega/common/client_forward.h>
#include <mega/common/shared_mutex.h>
#include <mega/file_service/file_service_context_pointer.h>
#include <mega/file_service/file_service_result_forward.h>

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

    auto initialize(common::Client& client) -> FileServiceResult;
}; // FileService

} // file_service
} // mega
