#include <mega/file_service/displaced_buffer.h>

#include <cassert>

namespace mega
{
namespace file_service
{

DisplacedBuffer::DisplacedBuffer(BufferPtr buffer, std::uint64_t displacement):
    Buffer(),
    mBuffer(std::move(buffer)),
    mDisplacement(displacement)
{
    // Sanity.
    assert(mBuffer);
}

BufferPtr DisplacedBuffer::buffer() const
{
    return mBuffer;
}

auto DisplacedBuffer::copy(Buffer& target,
                           std::uint64_t offset0,
                           std::uint64_t offset1,
                           std::uint64_t length) const -> std::pair<std::uint64_t, bool>
{
    assert(mBuffer);

    // No buffer to delegate to.
    if (!mBuffer)
        return std::make_pair(0u, false);

    // Delegate transfer.
    return mBuffer->copy(target, mDisplacement + offset0, offset1, length);
}

void DisplacedBuffer::displacement(std::uint64_t displacement)
{
    mDisplacement = displacement;
}

std::uint64_t DisplacedBuffer::displacement() const
{
    return mDisplacement;
}

auto DisplacedBuffer::read(void* buffer, std::uint64_t offset, std::uint64_t length) const
    -> std::pair<std::uint64_t, bool>
{
    assert(mBuffer);

    // No buffer to delegate to.
    if (!mBuffer)
        return std::make_pair(0u, false);

    // Delegate read.
    return mBuffer->read(buffer, mDisplacement + offset, length);
}

auto DisplacedBuffer::write(const void* buffer, std::uint64_t offset, std::uint64_t length)
    -> std::pair<std::uint64_t, bool>
{
    assert(mBuffer);

    // No buffer to delegate to.
    if (!mBuffer)
        return std::make_pair(0u, false);

    // Delegate write.
    return mBuffer->write(buffer, mDisplacement + offset, length);
}

DisplacedBufferPtr displace(BufferPtr buffer, std::uint64_t displacement)
{
    return std::make_shared<DisplacedBuffer>(std::move(buffer), displacement);
}

} // file_service
} // mega
