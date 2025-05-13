#pragma once

#include <mega/file_service/file_callbacks.h>
#include <mega/file_service/file_range.h>

namespace mega
{
namespace file_service
{

struct FileReadRequest
{
    // The callback the user wants us to call.
    FileReadCallback mCallback;

    // The content the user wants to read.
    FileRange mRange;
}; // FileReadRequest

} // file_service
} // mega
