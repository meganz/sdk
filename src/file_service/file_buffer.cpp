#include <mega/file_service/file_access.h>
#include <mega/file_service/file_buffer.h>

#include <cassert>
#include <memory>

namespace mega
{
namespace file_service
{

FileBuffer::FileBuffer(FileAccess& file):
    Buffer(),
    mFile(file)
{}

std::uint64_t FileBuffer::read(void* buffer, std::uint64_t offset, std::uint64_t length) const
{
    // Caller doesn't want to read anything.
    if (!length)
        return 0;

    assert(buffer);

    // Caller gave us a bad buffer.
    if (!buffer)
        return 0;

    // Disambiguate.
    using file_service::read;

    // Couldn't read from the file.
    if (!read(mFile, buffer, offset, length))
        return 0;

    // Let the caller know the read succeeded.
    return length;
}

std::uint64_t FileBuffer::write(const void* buffer, std::uint64_t offset, std::uint64_t length)
{
    // Caller doesn't actually want to write anything.
    if (!length)
        return 0u;

    assert(buffer);

    // Caller didn't give us a valid buffer.
    if (!buffer)
        return 0u;

    // Disambiguate.
    using file_service::write;

    // Try and write the caller's buffer to file.
    return write(mFile, buffer, offset, length);
}

std::uint64_t FileBuffer::copy(Buffer& target,
                               std::uint64_t offset0,
                               std::uint64_t offset1,
                               std::uint64_t length) const
{
    assert(this != &target);

    // Can't copy to the same buffer.
    if (this == &target)
        return 0u;

    // Caller doesn't actually want to transfer any data.
    if (!length)
        return 0u;

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
        auto count = read(buffer, offset0 + copied, size);

        // Couldn't read data from storage.
        if (count != size)
            return copied;

        // Try and write data to the target buffer.
        count = target.write(buffer, offset1 + copied, size);

        // Bump counters.
        copied += count;
        length -= count;

        // Couldn't write data to the target buffer.
        if (count != size)
            return copied;
    }

    // Try and read data from storage.
    auto count = read(buffer, offset0 + copied, length);

    // Couldn't read data from storage.
    if (count != length)
        return copied;

    // Try and write data to the target buffer.
    count = target.write(buffer, offset1 + copied, length);

    // Let the caller know how much data was copied.
    return count + copied;
}

} // file_service
} // mega
