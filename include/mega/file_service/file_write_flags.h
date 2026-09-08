#pragma once

#include <type_traits>

namespace mega
{
namespace file_service
{

enum FileWriteFlag : unsigned int
{
    FILE_WRITE_FLAG_NO_GROW = 1
}; // FileWriteFlag

using FileWriteFlags = std::underlying_type_t<FileWriteFlag>;

} // file_service
} // mega
