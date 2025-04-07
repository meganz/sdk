#pragma once

#include <mega/common/client_forward.h>
#include <mega/file_service/file_service_context_forward.h>

namespace mega
{
namespace file_service
{

class FileServiceContext
{
    common::Client& mClient;

public:
    FileServiceContext(common::Client& client);
}; // FileServiceContext

} // file_service
} // mega
