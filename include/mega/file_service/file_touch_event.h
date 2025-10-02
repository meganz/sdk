#pragma once

#include <mega/file_service/file_id.h>
#include <mega/file_service/file_touch_event_forward.h>

#include <cstdint>

namespace mega
{
namespace file_service
{

struct FileTouchEvent
{
    bool operator==(const FileTouchEvent& rhs) const
    {
        return mID == rhs.mID && mModified == rhs.mModified;
    }

    bool operator!=(const FileTouchEvent& rhs) const
    {
        return !(*this == rhs);
    }

    // The ID of the file that has been touched.
    FileID mID;

    // The file's new modification time.
    std::int64_t mModified;
}; // FileTouchEvent

} // file_service
} // mega
