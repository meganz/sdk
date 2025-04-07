#include <mega/common/lock.h>
#include <mega/file_service/file_service.h>
#include <mega/file_service/file_service_context.h>
#include <mega/file_service/file_service_result.h>

#include <stdexcept>

namespace mega
{
namespace file_service
{

using namespace common;

FileService::FileService():
    mContext(),
    mContextLock()
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
        return FILE_SERVICE_ALREADY_INITIALIZED;

    mContext = std::make_unique<FileServiceContext>(client);

    return FILE_SERVICE_SUCCESS;
}

catch (std::runtime_error&)
{
    return FILE_SERVICE_UNEXPECTED;
}

} // file_service
} // mega
