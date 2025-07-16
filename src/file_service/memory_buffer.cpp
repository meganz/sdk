#include <mega/file_service/memory_buffer.h>

#include <algorithm>
#include <cassert>
#include <cstring>

namespace mega
{
namespace file_service
{

MemoryBuffer::MemoryBuffer(std::uint64_t length):
    Buffer(),
    mBuffer(new std::uint8_t[static_cast<std::size_t>(length)]),
    mLength(length)
{}

auto MemoryBuffer::copy(Buffer& target,
                        std::uint64_t offset0,
                        std::uint64_t offset1,
                        std::uint64_t length) const -> std::pair<std::uint64_t, bool>
{
    assert(this != &target);

    // Can't copy to the same buffer.
    if (this == &target)
        return std::make_pair(0u, false);

    // Clamp length as necessary.
    length = std::min(length, std::max(mLength, offset0) - offset0);

    // Caller doesn't actually want to transfer any data.
    if (!length)
        return std::make_pair(0u, true);

    // Try and transfer our data to the target.
    return target.write(mBuffer.get() + offset0, offset1, length);
}

auto MemoryBuffer::read(void* buffer, std::uint64_t offset, std::uint64_t length) const
    -> std::pair<std::uint64_t, bool>
{
    assert(buffer);

    // Caller gave us a bad buffer.
    if (!buffer)
        return std::make_pair(0u, false);

    // Clamp length.
    length = std::min(length, std::max(mLength, offset) - offset);

    // Caller doesn't actually want to read anything.
    if (!length)
        return std::make_pair(0u, true);

    // Convenenience.
    auto* destination = static_cast<std::uint8_t*>(buffer);
    auto* source = mBuffer.get() + offset;

    // Copy data into the caller's buffer.
    std::copy(source, source + length, destination);

    // Let the user know how much data was read.
    return std::make_pair(length, true);
}

auto MemoryBuffer::write(const void* buffer, std::uint64_t offset, std::uint64_t length)
    -> std::pair<std::uint64_t, bool>
{
    assert(buffer);

    // Caller gave us a bad buffer.
    if (!buffer)
        return std::make_pair(0u, false);

    // Clamp length as necessary.
    length = std::min(length, std::max(mLength, offset) - offset);

    // Caller doesn't actually want to write anything.
    if (!length)
        return std::make_pair(0u, true);

    // Convenience.
    auto* destination = mBuffer.get() + offset;
    auto* source = static_cast<const std::uint8_t*>(buffer);

    // Copy data into our buffer.
    std::copy(source, source + length, destination);

    // Let the user know how many bytes were written.
    return std::make_pair(length, true);
}

} // file_service
} // mega
