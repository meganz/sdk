#pragma once

#include <mega/file_service/file_callbacks.h>
#include <mega/file_service/file_truncate_request_forward.h>

#include <cstdint>

namespace mega
{
namespace file_service
{

struct FileTruncateRequest
{
    // Who should we call when the file's been truncated?
    FileTruncateCallback mCallback;

    // What size should the file be truncated to?
    std::uint64_t mSize;
}; // FileTruncateRequest

} // file_service
} // mega
