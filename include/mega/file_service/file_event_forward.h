#pragma once

#include <mega/file_service/file_flush_event_forward.h>
#include <mega/file_service/file_remove_event_forward.h>
#include <mega/file_service/file_touch_event_forward.h>
#include <mega/file_service/file_truncate_event_forward.h>
#include <mega/file_service/file_write_event_forward.h>

#include <variant>

namespace mega
{
namespace file_service
{

using FileEvent = std::
    variant<FileFlushEvent, FileRemoveEvent, FileTouchEvent, FileTruncateEvent, FileWriteEvent>;

} // file_service
} // mega
