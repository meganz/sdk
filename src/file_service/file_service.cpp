#include <mega/common/lock.h>
#include <mega/file_service/file.h>
#include <mega/file_service/file_id.h>
#include <mega/file_service/file_info.h>
#include <mega/file_service/file_range.h>
#include <mega/file_service/file_service.h>
#include <mega/file_service/file_service_context.h>
#include <mega/file_service/file_service_options.h>
#include <mega/file_service/file_service_result.h>
#include <mega/file_service/file_service_result_or.h>
#include <mega/file_service/logging.h>

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

auto FileService::create(NodeHandle parent, const std::string& name) -> FileServiceResultOr<File>
{
    SharedLock guard(mContextLock);

    if (!mContext)
        return unexpected(FILE_SERVICE_UNINITIALIZED);

    return mContext->create(parent, name);
}

void FileService::deinitialize()
{
    UniqueLock guard(mContextLock);

    mContext.reset();
}

auto FileService::info(FileID id) -> FileServiceResultOr<FileInfo>
{
    SharedLock guard(mContextLock);

    if (!mContext)
        return unexpected(FILE_SERVICE_UNINITIALIZED);

    if (!id)
        return unexpected(FILE_SERVICE_FILE_DOESNT_EXIST);

    return mContext->info(id);
}

auto FileService::open(FileID id) -> FileServiceResultOr<File>
{
    SharedLock guard(mContextLock);

    if (!mContext)
        return unexpected(FILE_SERVICE_UNINITIALIZED);

    if (!id)
        return unexpected(FILE_SERVICE_FILE_DOESNT_EXIST);

    return mContext->open(id);
}

auto FileService::options(const FileServiceOptions& options) -> FileServiceResult
{
    SharedLock guard(mContextLock);

    if (!mContext)
        return FILE_SERVICE_UNINITIALIZED;

    mContext->options(options);

    return FILE_SERVICE_SUCCESS;
}

auto FileService::options() -> FileServiceResultOr<FileServiceOptions>
{
    SharedLock guard(mContextLock);

    if (!mContext)
        return unexpected(FILE_SERVICE_UNINITIALIZED);

    return mContext->options();
}

auto FileService::initialize(Client& client, const FileServiceOptions& options) -> FileServiceResult
try
{
    UniqueLock guard(mContextLock);

    if (mContext)
    {
        FSError1("File Service has already been initialized");

        return FILE_SERVICE_ALREADY_INITIALIZED;
    }

    mContext = std::make_unique<FileServiceContext>(client, options);

    FSInfo1("File Service initialized");

    return FILE_SERVICE_SUCCESS;
}
catch (std::runtime_error& exception)
{
    FSErrorF("Unable to initialize File Service: %s", exception.what());

    return FILE_SERVICE_UNEXPECTED;
}

auto FileService::initialize(Client& client) -> FileServiceResult
{
    return initialize(client, FileServiceOptions());
}

auto FileService::ranges(FileID id) -> FileServiceResultOr<FileRangeVector>
{
    SharedLock guard(mContextLock);

    if (!mContext)
        return unexpected(FILE_SERVICE_UNINITIALIZED);

    if (!id)
        return unexpected(FILE_SERVICE_FILE_DOESNT_EXIST);

    return mContext->ranges(id);
}

auto FileService::storageUsed() -> FileServiceResultOr<std::uint64_t>
{
    SharedLock guard(mContextLock);

    if (!mContext)
        return unexpected(FILE_SERVICE_UNINITIALIZED);

    return mContext->storageUsed();
}

} // file_service
} // mega
