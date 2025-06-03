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

bool FileBuffer::read(void* buffer, std::uint64_t offset, std::uint64_t length) const
{
    // Caller doesn't want to read anything.
    if (!length)
        return true;

    assert(buffer);

    // Caller gave us a bad buffer.
    if (!buffer)
        return false;

    // Disambiguate.
    using file_service::read;

    // Try and populate the user's buffer.
    return read(mFile, buffer, offset, length) == length;
}

bool FileBuffer::write(const void* buffer, std::uint64_t offset, std::uint64_t length)
{
    // Caller doesn't actually want to write anything.
    if (!length)
        return true;

    assert(buffer);

    // Caller didn't give us a valid buffer.
    if (!buffer)
        return false;

    // Disambiguate.
    using file_service::write;

    // Try and write the caller's buffer to file.
    return write(mFile, buffer, offset, length) == length;
}

bool FileBuffer::transfer(Buffer& target,
                          std::uint64_t offset0,
                          std::uint64_t offset1,
                          std::uint64_t length) const
{
    // Transfers to the same buffer are a no-op.
    if (this == &target)
        return true;

    // Caller doesn't actually want to transfer any data.
    if (!length)
        return true;

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
        // A 16MiB block should be enough for our needs.
        size = std::min<std::uint64_t>(length, 1u << 24);

        // Instantiate buffer.
        memoryBuffer.reset(new std::uint8_t[static_cast<std::size_t>(size)]);

        // Use the in-memory buffer.
        buffer = memoryBuffer.get();
    }

    // Transfer data to the target buffer.
    while (length > size)
    {
        // Couldn't read data from storage.
        if (!read(buffer, offset0, size))
            return false;

        // Couldn't write data to the target buffer.
        if (!target.write(buffer, offset1, size))
            return false;

        // Adjust offsets and remaining length.
        length -= size;
        offset0 += size;
        offset1 += size;
    }

    // Couldn't read data from storage.
    if (!read(buffer, offset0, length))
        return false;

    // Try and write data to the target buffer.
    return target.write(buffer, offset1, length);
}

} // file_service
} // mega
