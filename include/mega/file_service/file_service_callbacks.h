#pragma once

#include <mega/file_service/file_service_result_or_forward.h>

#include <functional>

namespace mega
{
namespace file_service
{

using ReclaimCallback = std::function<void(FileServiceResultOr<std::uint64_t>)>;

} // file_service
} // mega
