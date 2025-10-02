#pragma once

#include <mega/file_service/source_forward.h>

#include <cstdint>

namespace mega
{
namespace file_service
{

struct FileReadResult
{
    // Contains the data that we read.
    Source& mSource;

    // The offset in the file that we read from.
    std::uint64_t mOffset;

    // How much data we were able to read.
    std::uint64_t mLength;
}; // FileReadResult

} // file_service
} // mega
