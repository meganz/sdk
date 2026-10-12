#pragma once

#include <type_traits>

namespace mega
{
namespace file_service
{

enum FileTruncateFlag : unsigned int
{
    // The file is truncated if and only if it is larger than the requested
    // size. Otherwise the truncate completes successfully without altering
    // the file.
    //
    // This exists to represent Windows' notion of a file's allocation size:
    // Windows extends a file's allocation size, rounded up to the volume's
    // allocation unit, before it writes any data to that file. As we don't
    // track a file's allocation size separately from its size, it should
    // be a no-op for preallocation.
    FILE_TRUNCATE_FLAG_NO_GROW = 1
}; // FileTruncateFlag

using FileTruncateFlags = std::underlying_type_t<FileTruncateFlag>;

} // file_service
} // mega
