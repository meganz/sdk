#pragma once

#include <mega/file_service/file_range_context_forward.h>

#include <memory>

namespace mega
{
namespace file_service
{

using FileRangeContextPtr = std::unique_ptr<FileRangeContext>;

} // file_service
} // mega
