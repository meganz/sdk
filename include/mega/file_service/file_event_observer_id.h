#pragma once

#include <mega/file_service/file_event_emitter_forward.h>

#include <cstdint>
#include <utility>

namespace mega
{
namespace file_service
{

using FileEventObserverID = std::pair<FileEventEmitter*, std::uint64_t>;

} // file_service
} // mega
