#pragma once

#include <mega/file_service/file_location_forward.h>

#include <functional>
#include <optional>

namespace mega
{
namespace file_service
{

using FileLocationProvider = std::function<std::optional<FileLocation>(FileID)>;

} // file_service
} // mega
