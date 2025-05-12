#pragma once

#include <mega/file_service/file_read_result.h>
#include <mega/file_service/file_result_or_forward.h>

#include <cstdint>
#include <functional>

namespace mega
{
namespace file_service
{

using FileReadCallback = std::function<void(FileResultOr<FileReadResult>)>;

} // file_service
} // mega
