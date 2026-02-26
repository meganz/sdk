#pragma once

#include <mega/file_service/file_flush_event_forward.h>
#include <mega/file_service/file_id.h>
#include <mega/types.h>

namespace mega
{
namespace file_service
{

struct FileFlushEvent
{
    bool operator==(const FileFlushEvent& rhs) const
    {
        return mHandle == rhs.mHandle && mID == rhs.mID;
    }

    bool operator!=(const FileFlushEvent& rhs) const
    {
        return !(*this == rhs);
    }

    // The handle of this file's new cloud node.
    NodeHandle mHandle;

    // The ID of the file that has been flushed.
    FileID mID;
}; // FileFlushEvent

} // file_service
} // mega
