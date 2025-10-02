#include <mega/common/error_or.h>
#include <mega/common/testing/cloud_path.h>
#include <mega/common/testing/path.h>
#include <mega/file_service/file.h>
#include <mega/file_service/file_id.h>
#include <mega/file_service/file_info.h>
#include <mega/file_service/file_range.h>
#include <mega/file_service/file_service.h>
#include <mega/file_service/file_service_result.h>
#include <mega/file_service/file_service_result_or.h>
#include <mega/file_service/testing/integration/client.h>

namespace mega
{
namespace file_service
{
namespace testing
{

using common::ErrorOr;
using common::testing::CloudPath;
using common::testing::Path;

Client::Client(const std::string& clientName, const Path& databasePath, const Path& storagePath):
    common::testing::Client(clientName, databasePath, storagePath)
{}

Client::~Client() {}

auto Client::fileCreate(NodeHandle parent, const std::string& name) -> FileServiceResultOr<File>
{
    return fileService().create(parent, name);
}

auto Client::fileInfo(CloudPath path) const -> FileServiceResultOr<FileInfo>
{
    return fileInfo(FileID::from(path.resolve(*this).valueOr(NodeHandle())));
}

auto Client::fileInfo(FileID id) const -> FileServiceResultOr<FileInfo>
{
    return fileService().info(id);
}

auto Client::fileOpen(FileID id) const -> FileServiceResultOr<File>
{
    return fileService().open(id);
}

auto Client::fileOpen(CloudPath parentPath, const std::string& name) const
    -> FileServiceResultOr<File>
{
    return fileService().open(parentPath.resolve(*this).valueOr(NodeHandle()), name);
}

auto Client::fileOpen(CloudPath path) const -> FileServiceResultOr<File>
{
    return fileOpen(FileID::from(path.resolve(*this).valueOr(NodeHandle())));
}

} // testing
} // common
} // mega
