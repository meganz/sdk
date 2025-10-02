#pragma once

#include <mega/file_service/file_id.h>
#include <mega/file_service/file_range.h>
#include <mega/file_service/file_write_event_forward.h>

namespace mega
{
namespace file_service
{

struct FileWriteEvent
{
    bool operator==(const FileWriteEvent& rhs) const
    {
        return mID == rhs.mID && mRange == rhs.mRange;
    }

    // The portion of the file that was written.
    FileRange mRange;

    // The ID of the file that was written.
    FileID mID;
}; // FileWriteEvent

} // file_service
} // mega
