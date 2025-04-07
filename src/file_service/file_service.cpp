#include <mega/file_service/file_service.h>
#include <mega/file_service/file_service_result.h>

namespace mega
{
namespace file_service
{

using namespace common;

auto FileService::deinitialize() -> void {}

auto FileService::initialize(Client&) -> FileServiceResult
{
    return FILE_SERVICE_UNEXPECTED;
}

} // file_service
} // mega
