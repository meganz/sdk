#pragma once

#include <mega/file_service/file_callbacks.h>
#include <mega/file_service/file_remove_request_forward.h>
#include <mega/file_service/file_request_tags.h>

namespace mega
{
namespace file_service
{

struct FileRemoveRequest
{
    // What kind of request is this?
    using Type = FileWriteRequestTag;

    // This request's human readable name.
    static const char* name()
    {
        return "remove";
    }

    // Who should we call when the file's been removed?
    FileRemoveCallback mCallback;

    // Is this file being removed because it was replaced?
    bool mReplaced;

    // Should we only remove the file from the service?
    bool mServiceOnly;
}; // FileRemoveRequest

} // file_service
} // mega
