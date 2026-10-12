#pragma once

#include <mega/file_service/file_id.h>
#include <mega/file_service/file_remove_event_forward.h>

namespace mega
{
namespace file_service
{

struct FileRemoveEvent
{
    bool operator==(const FileRemoveEvent& rhs) const
    {
        return mHandle == rhs.mHandle && mID == rhs.mID;
    }

    bool operator!=(const FileRemoveEvent& rhs) const
    {
        return !(*this == rhs);
    }

    // The handle of hte file that's been removed.
    NodeHandle mHandle;

    // The ID of the file that's been removed.
    FileID mID;
}; // FileRemoveEvent

} // file_service
} // mega
