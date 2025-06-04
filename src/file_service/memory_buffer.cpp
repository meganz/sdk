#include <mega/file_service/memory_buffer.h>

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

bool MemoryBuffer::read(void* buffer, std::uint64_t offset, std::uint64_t length) const
{
    // Caller doesn't actually want to read anything.
    if (!length)
        return true;

    assert(buffer);

    // Caller gave us a bad buffer.
    if (!buffer)
        return false;

    assert(offset + length <= mLength);

    // Caller's read is out of bounds.
    if (offset + length > mLength)
        return false;

    // Copy data into the caller's buffer.
    std::memcpy(buffer, mBuffer.get() + offset, static_cast<std::size_t>(length));

    // Reading from our buffer always succeeds.
    return true;
}

bool MemoryBuffer::write(const void* buffer, std::uint64_t offset, std::uint64_t length)
{
    // Caller doesn't actually want to write anything.
    if (!length)
        return true;

    assert(buffer);

    // Caller gave us a bad buffer.
    if (!buffer)
        return false;

    assert(offset + length <= mLength);

    // Caller's write is out of bounds.
    if (offset + length > mLength)
        return false;

    // Copy data into our buffer.
    std::memcpy(mBuffer.get() + offset, buffer, static_cast<std::size_t>(length));

    // Writes into our buffer always succeed.
    return true;
}

bool MemoryBuffer::copy(Buffer& target,
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

    assert(offset0 + length <= mLength);

    // Caller's transfer is out of bounds.
    if (offset0 + length > mLength)
        return false;

    // Try and transfer our data to the target.
    return target.write(mBuffer.get() + offset0, offset1, length);
}

} // file_service
} // mega
