#pragma once

#include <mega/file_service/file_callbacks.h>
#include <mega/file_service/file_range.h>
#include <mega/file_service/file_read_request_forward.h>
#include <mega/file_service/file_request_tags.h>

namespace mega
{
namespace file_service
{

struct FileReadRequest
{
    // What kind of request is this?
    using Type = FileReadRequestTag;

    // This request's human readable name.
    static const char* name()
    {
        return "read";
    }

    // The callback the user wants us to call.
    FileReadCallback mCallback;

    // The content the user wants to read.
    FileRange mRange;
}; // FileReadRequest

} // file_service
} // mega
