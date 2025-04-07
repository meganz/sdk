#include <mega/common/lock.h>
#include <mega/file_service/file_service.h>
#include <mega/file_service/file_service_context.h>
#include <mega/file_service/file_service_result.h>
#include <mega/file_service/logging.h>

#include <stdexcept>

namespace mega
{
namespace file_service
{

using namespace common;

static const char* kName = "FileService";

FileService::FileService():
    DestructionLogger(kName),
    mContext(),
    mContextLock(),
    mConstructionLogger(kName)
{}

FileService::~FileService() = default;

auto FileService::deinitialize() -> void
{
    UniqueLock<SharedMutex> guard(mContextLock);

    mContext.reset();
}

auto FileService::initialize(Client& client) -> FileServiceResult
try
{
    UniqueLock<SharedMutex> guard(mContextLock);

    if (mContext)
    {
        FSError1("File Service has already been initialized");

        return FILE_SERVICE_ALREADY_INITIALIZED;
    }

    mContext = std::make_unique<FileServiceContext>(client);

    FSInfo1("File Service initialized");

    return FILE_SERVICE_SUCCESS;
}

catch (std::runtime_error& exception)
{
    FSErrorF("Unable to initialize File Service: %s", exception.what());

    return FILE_SERVICE_UNEXPECTED;
}

} // file_service
} // mega
