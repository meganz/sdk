#pragma once

#include <mega/file_service/file_range_context.h>

#include <memory>

namespace mega
{
namespace file_service
{

using FileRangeContextPtr = std::unique_ptr<FileRange>;

} // file_service
} // mega
