#include <mega/file_service/file_access.h>
#include <mega/filesystem.h>

#include <cassert>

namespace mega
{
namespace file_service
{

// Maximum read or write length.
constexpr std::uint64_t maximum = std::numeric_limits<unsigned long>::max();

auto read(FileAccess& file, void* buffer, std::uint64_t offset, std::uint64_t length)
    -> std::pair<std::uint64_t, bool>
{
    // Sanity.
    assert(buffer);

    // Caller didn't pass us a valid buffer.
    if (!buffer)
        return std::make_pair(0u, false);

    // Convenience.
    auto* buffer_ = static_cast<std::uint8_t*>(buffer);

    // How many bytes we have left to read.
    auto remaining = length;

    // To avoid data races in FileAccess.
    auto retry = false;

    // Read as much data from the file as possible.
    while (remaining)
    {
        // Figure out how much data we can read in this iteration.
        auto count = std::min(maximum, remaining);

        // Couldn't read data from the file.
        if (!file.frawread(buffer_,
                           static_cast<unsigned long>(count),
                           static_cast<m_off_t>(offset),
                           true,
                           FSLogging::logOnError,
                           &retry))
            break;

        // Bump pointers.
        buffer_ += count;
        offset += count;
        remaining -= count;
    }

    // Let the caller know how much we could read.
    return std::make_pair(length - remaining, !remaining);
}

auto write(FileAccess& file, const void* buffer, std::uint64_t offset, std::uint64_t length)
    -> std::pair<std::uint64_t, bool>
{
    // Sanity.
    assert(buffer);

    // Caller didn't pass us a valid buffer.
    if (!buffer)
        return std::make_pair(0u, false);

    // Convenience.
    auto* buffer_ = static_cast<const std::uint8_t*>(buffer);

    // How many bytes we have left to write.
    auto remaining = length;

    // To avoid data races in FileAccess.
    auto retry = false;

    // Write as much data to the file as possible.
    while (remaining)
    {
        // How much data should we write in this iteration?
        auto count = std::min(remaining, maximum);

        // Track how much data we wrote in this iteration.
        auto written = 0ul;

        // Try and write the data to file.
        auto result = file.fwrite(buffer_,
                                  static_cast<unsigned long>(count),
                                  static_cast<m_off_t>(offset),
                                  &written,
                                  &retry);

        // Update count of remaining bytes.
        remaining -= written;

        // Couldn't write data to the file.
        if (!result)
            break;

        // Bump pointers.
        buffer_ += written;
        offset += written;
    }

    // Let the caller know how much data we wrote.
    return std::make_pair(length - remaining, !remaining);
}

bool truncate(FileAccess& file, std::uint64_t size)
{
    return file.ftruncate(static_cast<m_off_t>(size));
}

} // file_service
} // mega
