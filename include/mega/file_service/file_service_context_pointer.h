#pragma once

#include <mega/file_service/file_service_context_forward.h>

#include <memory>

namespace mega
{
namespace file_service
{

using FileServiceContextPtr = std::unique_ptr<FileServiceContext>;

} // file_service
} // mega
