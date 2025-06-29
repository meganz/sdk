#pragma once

#include <mega/file_service/file_explicit_flush_request_forward.h>
#include <mega/file_service/file_flush_request.h>
#include <mega/types.h>

#include <string>

namespace mega
{
namespace file_service
{

struct FileExplicitFlushRequest: FileFlushRequest
{
    // What is our file's intended name?
    std::string mName;

    // Who is our file's intended parent?
    NodeHandle mParentHandle;
}; // FileExplicitFlushRequest

} // file_service
} // mega
