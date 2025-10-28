#pragma once

#include <mega/file_service/file_append_request_forward.h>
#include <mega/file_service/file_callbacks.h>
#include <mega/file_service/file_request_tags.h>

#include <cstdint>

namespace mega
{
namespace file_service
{

struct FileAppendRequest
{
    // What kind of request is this?
    using Type = FileWriteRequestTag;

    // This request's human readable name.
    static const char* name()
    {
        return "append";
    }

    // What data does the user want to append to the file?
    const void* mBuffer;

    // Who should we call when the append completes?
    FileAppendCallback mCallback;

    // How much data does the user want to append?
    std::uint64_t mLength;
}; // FileAppendRequest

} // file_service
} // mega
