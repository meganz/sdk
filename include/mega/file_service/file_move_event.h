#pragma once

#include <mega/file_service/file_id.h>
#include <mega/file_service/file_location.h>
#include <mega/file_service/file_move_event_forward.h>

namespace mega
{
namespace file_service
{

struct FileMoveEvent
{
    bool operator==(const FileMoveEvent& rhs) const
    {
        return mID == rhs.mID && mFrom == rhs.mFrom && mTo == rhs.mTo;
    }

    bool operator!=(const FileMoveEvent& rhs) const
    {
        return !(*this == rhs);
    }

    // Where the file was moved from.
    FileLocation mFrom;

    // Where the file was moved to.
    FileLocation mTo;

    // The ID of the file that was moved.
    FileID mID;
}; // FileMoveEvent

} // file_service
} // mega
