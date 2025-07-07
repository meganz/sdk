#pragma once

#include <mega/file_service/file_event_forward.h>

#include <functional>

namespace mega
{
namespace file_service
{

using FileEventObserver = std::function<void(const FileEvent&)>;

} // file_service
} // mega
