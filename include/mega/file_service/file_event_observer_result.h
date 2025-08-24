#pragma once

#include <mega/file_service/file_event_observer_result.h>

namespace mega
{
namespace file_service
{

enum FileEventObserverResult : unsigned int
{
    FILE_EVENT_OBSERVER_KEEP,
    FILE_EVENT_OBSERVER_REMOVE,
}; // FileEventObserverResult

} // file_service
} // mega
