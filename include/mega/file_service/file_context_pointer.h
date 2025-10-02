#pragma once

#include <mega/file_service/file_context_forward.h>

#include <memory>

namespace mega
{
namespace file_service
{

using FileContextPtr = std::shared_ptr<FileContext>;
using FileContextWeakPtr = std::weak_ptr<FileContext>;

} // file_service
} // mega
