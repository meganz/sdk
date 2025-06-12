#pragma once

#include <mega/file_service/file_callbacks.h>
#include <mega/file_service/file_fetch_request_forward.h>
#include <mega/file_service/file_request_tags.h>

namespace mega
{
namespace file_service
{

struct FileFetchRequest
{
    // What kind of request is this?
    using Type = FileReadRequestTag;

    // This request's human readable name.
    static const char* name()
    {
        return "fetch";
    }

    // Who should we call when the fetch completes?
    FileFetchCallback mCallback;
}; // FileFetchRequest

} // file_service
} // mega
