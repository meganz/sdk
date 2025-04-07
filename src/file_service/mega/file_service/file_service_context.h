#pragma once

#include <mega/common/client_forward.h>
#include <mega/common/database.h>
#include <mega/file_service/construction_logger.h>
#include <mega/file_service/destruction_logger.h>
#include <mega/file_service/file_service_context_forward.h>
#include <mega/file_service/file_storage.h>

namespace mega
{
namespace file_service
{

class FileServiceContext: DestructionLogger
{
    common::Client& mClient;
    FileStorage mStorage;
    common::Database mDatabase;
    ConstructionLogger mConstructionLogger;

public:
    FileServiceContext(common::Client& client);
}; // FileServiceContext

} // file_service
} // mega
