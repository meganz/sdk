#pragma once

#include <mega/file_service/file_callbacks.h>
#include <mega/file_service/file_flush_request_forward.h>
#include <mega/file_service/file_request_tags.h>
#include <mega/localpath.h>

namespace mega
{
namespace file_service
{

struct FileFlushRequest
{
    // What kind of request is this?
    using Type = FileReadRequestTag;

    // This request's human readable name.
    static const char* name()
    {
        return "flush";
    }

    // Who should we call when the flush completes?
    FileFlushCallback mCallback;

    // What path should we present to the application?
    LocalPath mLogicalPath;
}; // FileFlushRequest

} // file_service
} // mega
