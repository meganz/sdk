#include <mega/file_service/file_info_context.h>
#include <mega/file_service/sparse_file_buffer.h>

#include <algorithm>
#include <tuple>

namespace mega
{
namespace file_service
{

SparseFileBuffer::SparseFileBuffer(FileAccess& file, FileInfoContext& info):
    FileBuffer(file),
    mInfo(info)
{}

auto SparseFileBuffer::read(void* buffer, std::uint64_t offset, std::uint64_t length) const
    -> std::pair<std::uint64_t, bool>
{
    // Latch the file's logical and physical size.
    auto logicalSize = mInfo.logicalSize();
    auto physicalSize = mInfo.physicalSize();

    // Clamp the caller's length.
    length = std::min(length, std::max(logicalSize, offset) - offset);

    // Caller doesn't actually need to read any data.
    if (!length)
        return std::make_pair(length, true);

    // How much data can we actually read from disk?
    auto count = std::min(length, std::max(physicalSize, offset) - offset);

    // So we can tie.
    auto success = false;

    // Try and populate the caller's buffer.
    std::tie(count, success) = FileBuffer::read(buffer, offset, count);

    // Couldn't populate the caller's buffer.
    if (!success)
        return std::make_pair(count, success);

    // Convenience.
    auto* buffer_ = static_cast<std::uint8_t*>(buffer) + count;

    // How many zeros do we need to write to the caller's buffer?
    count = length - count;

    // Zero the remainder of the caller's buffer if necessary.
    std::fill(buffer_, buffer_ + count, 0);

    // Let the caller know the read was successful.
    return std::make_pair(length, true);
}

auto SparseFileBuffer::write(const void* buffer, std::uint64_t offset, std::uint64_t length)
    -> std::pair<std::uint64_t, bool>
{
    // Caller doesn't actually want to write any data.
    if (!length)
        return std::make_pair(0u, true);

    // Try and write the caller's data to disk.
    auto [count, success] = FileBuffer::write(buffer, offset, length);

    // Latch the file's current physical size.
    auto size = mInfo.physicalSize();

    // Bump the file's physical size if necessary.
    if (offset + count > size)
        mInfo.physicalSize(offset + count);

    // Let the caller know how much data was written.
    return std::make_pair(count, success);
}

bool SparseFileBuffer::truncate(std::uint64_t size)
{
    // Couldn't truncate the file.
    if (!FileBuffer::truncate(size))
        return false;

    // Update the file's physical size.
    mInfo.physicalSize(size);

    // Let the caller know the file was truncated.
    return true;
}

} // file_service
} // mega
