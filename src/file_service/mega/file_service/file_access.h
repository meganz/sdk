#pragma once

#include <cstdint>
#include <utility>

namespace mega
{

struct FileAccess;

namespace file_service
{

auto read(FileAccess& file, void* buffer, std::uint64_t offset, std::uint64_t length)
    -> std::pair<std::uint64_t, bool>;

auto write(FileAccess& file, const void* buffer, std::uint64_t offset, std::uint64_t length)
    -> std::pair<std::uint64_t, bool>;

bool truncate(FileAccess& file, std::uint64_t size);

} // file_service
} // mega
