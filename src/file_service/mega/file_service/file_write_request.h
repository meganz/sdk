#pragma once

#include <mega/file_service/file_callbacks.h>
#include <mega/file_service/file_range.h>
#include <mega/file_service/file_request_tags.h>
#include <mega/file_service/file_write_request_forward.h>

namespace mega
{
namespace file_service
{

struct FileWriteRequest
{
    // What kind of request is this?
    using Type = FileWriteRequestTag;

    // This request's human readable name.
    static const char* name()
    {
        return "write";
    }

    // The content the user wants to write.
    const void* mBuffer;

    // The callback the user wants us to invoke.
    FileWriteCallback mCallback;

    // Where the user wants us to write content.
    FileRange mRange;
}; // FileWriteRequest

} // file_service
} // mega
