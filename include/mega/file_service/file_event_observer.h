#pragma once

#include <mega/file_service/file_event_forward.h>
#include <mega/file_service/file_event_observer_result_forward.h>

#include <functional>

namespace mega
{
namespace file_service
{

using FileEventObserver = std::function<FileEventObserverResult(const FileEvent&)>;

} // file_service
} // mega
