#pragma once

#include <mega/file_service/file_service_result_forward.h>

#include <functional>

namespace mega
{
namespace file_service
{

using ReclaimCallback = std::function<void(FileServiceResult)>;

} // file_service
} // mega
