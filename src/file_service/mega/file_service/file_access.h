#pragma once

#include <cstdint>

namespace mega
{

struct FileAccess;

namespace file_service
{

std::uint64_t read(FileAccess& file, void* buffer, std::uint64_t offset, std::uint64_t length);

std::uint64_t
    write(FileAccess& file, const void* buffer, std::uint64_t offset, std::uint64_t length);

} // file_service
} // mega
