#pragma once

#include <mega/file_service/source_forward.h>

#include <cstdint>

namespace mega
{
namespace file_service
{

struct FileWriteResult
{
    // Where we wrote data to the file.
    std::uint64_t mOffset;

    // How data we wrote to the file.
    std::uint64_t mLength;
}; // FileWriteResult

} // file_service
} // mega
