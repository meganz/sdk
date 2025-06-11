#pragma once

#include <mega/file_service/file_callbacks.h>
#include <mega/file_service/file_fetch_request_forward.h>

namespace mega
{
namespace file_service
{

struct FileFetchRequest
{
    // Who should we call when the fetch completes?
    FileFetchCallback mCallback;
}; // FileFetchRequest

} // file_service
} // mega
