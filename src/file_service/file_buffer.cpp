#include <mega/file_service/file_access.h>
#include <mega/file_service/file_buffer.h>

#include <cassert>
#include <memory>
#include <tuple>

namespace mega
{
namespace file_service
{

FileBuffer::FileBuffer(FileAccess& file):
    Buffer(),
    mFile(file)
{}

auto FileBuffer::copy(Buffer& target,
                      std::uint64_t offset0,
                      std::uint64_t offset1,
                      std::uint64_t length) const -> std::pair<std::uint64_t, bool>
{
    assert(this != &target);

    // Can't copy to the same buffer.
    if (this == &target)
        return std::make_pair(0u, false);

    // Caller doesn't actually want to transfer any data.
    if (!length)
        return std::make_pair(0u, true);

    // Maximum length allowed for on-stack buffer.
    constexpr std::uint64_t threshold = 1u << 12;

    // Buffer used when length < threshold.
    std::uint8_t stackBuffer[threshold];

    // Assume we'll use the on-stack buffer.
    auto buffer = &stackBuffer[0];
    auto size = threshold;

    // Buffer used when length >= threshold.
    std::unique_ptr<std::uint8_t[]> memoryBuffer;

    // Need to use an in-memory buffer.
    if (threshold < length)
    {
        // A 128KiB block should be enough for our needs.
        size = std::min<std::uint64_t>(length, 1u << 17);

        // Instantiate buffer.
        memoryBuffer.reset(new std::uint8_t[static_cast<std::size_t>(size)]);

        // Use the in-memory buffer.
        buffer = memoryBuffer.get();
    }

    // How much data have we copied?
    std::uint64_t copied = 0u;

    // Transfer data to the target buffer.
    while (length > size)
    {
        // Try and read data from storage.
        auto [count, success] = read(buffer, offset0 + copied, size);

        // Couldn't read data from storage.
        if (!success)
            return std::make_pair(copied, false);

        // Try and write data to the target buffer.
        std::tie(count, success) = target.write(buffer, offset1 + copied, size);

        // Bump counters.
        copied += count;
        length -= count;

        // Couldn't write data to the target buffer.
        if (!success)
            return std::make_pair(copied, false);
    }

    // Try and read data from storage.
    auto [count, success] = read(buffer, offset0 + copied, length);

    // Couldn't read data from storage.
    if (!success)
        return std::make_pair(copied, false);

    // Try and write data to the target buffer.
    std::tie(count, success) = target.write(buffer, offset1 + copied, length);

    // Let the caller know how much data was copied.
    return std::make_pair(count + copied, success);
}

auto FileBuffer::read(void* buffer, std::uint64_t offset, std::uint64_t length) const
    -> std::pair<std::uint64_t, bool>
{
    // Caller doesn't want to read anything.
    if (!length)
        return std::make_pair(0u, true);

    assert(buffer);

    // Caller gave us a bad buffer.
    if (!buffer)
        return std::make_pair(0, false);

    // Disambiguate.
    using file_service::read;

    // Let the caller know how much data we read from the file.
    return read(mFile, buffer, offset, length);
}

auto FileBuffer::write(const void* buffer, std::uint64_t offset, std::uint64_t length)
    -> std::pair<std::uint64_t, bool>
{
    // Caller doesn't actually want to write anything.
    if (!length)
        return std::make_pair(0u, true);

    assert(buffer);

    // Caller didn't give us a valid buffer.
    if (!buffer)
        return std::make_pair(0u, false);

    // Disambiguate.
    using file_service::write;

    // Let the caller know how much data we wrote to the file.
    return write(mFile, buffer, offset, length);
}

bool FileBuffer::truncate(std::uint64_t size)
{
    // Disambiguate.
    using file_service::truncate;

    // Truncate the file.
    return truncate(mFile, size);
}

} // file_service
} // mega
