#pragma once

#include <mega/file_service/file_callbacks.h>
#include <mega/file_service/file_touch_request_forward.h>

#include <cstdint>

namespace mega
{
namespace file_service
{

struct FileTouchRequest
{
    // Who should we call when the file's modification time has been updated?
    FileTouchCallback mCallback;

    // What should we set the file's modification time to?
    std::int64_t mModified;
}; // FileTouchRequest

} // file_service
} // mega
