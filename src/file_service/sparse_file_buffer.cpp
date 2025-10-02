#include <mega/file_service/file_size_info.h>
#include <mega/file_service/sparse_file_buffer.h>
#include <mega/filesystem.h>

#include <algorithm>
#include <cassert>
#include <tuple>

namespace mega
{
namespace file_service
{

SparseFileBuffer::SparseFileBuffer(FileAccess& file, FileSizeInfo& info):
    FileBuffer(file),
    mInfo(info)
{}

auto SparseFileBuffer::read(void* buffer, std::uint64_t offset, std::uint64_t length) const
    -> std::pair<std::uint64_t, bool>
{
    auto reportedSize = mInfo.reportedSize();
    auto size = mInfo.size();

    // Clamp the caller's length.
    length = std::min(length, std::max(offset, size) - offset);

    // Caller doesn't actually need to read any data.
    if (!length)
        return std::make_pair(length, true);

    // How much data can we actually read from disk?
    auto count = std::min(length, std::max(offset, reportedSize) - offset);

    // So we can tie.
    auto success = false;

    // Try and populate the caller's buffer.
    std::tie(count, success) = FileBuffer::read(buffer, offset, count);

    // Retrieve the file's current file sizes.
    auto sizes = mFile.getFileSize();

    // If getFileSize(...) failed so should've read.
    assert(sizes || (!count && !success));

    // Couldn't populate the caller's buffer.
    if (!sizes || !success)
        return std::make_pair(count, success);

    std::uint64_t allocatedSize;

    // Clarity.
    std::tie(allocatedSize, reportedSize) = *sizes;

    // Update the file's allocated and reported sizes.
    mInfo.allocatedSize(allocatedSize);
    mInfo.reportedSize(reportedSize);

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

    // Retrieve the file's updated sizes.
    auto sizes = mFile.getFileSize();

    // If getFileSize(...) failed so shoud've write(...).
    assert(sizes || (!count && !success));

    // Couldn't retrieve the file's updated sizes.
    if (!sizes)
        return std::make_pair(count, success);

    // Clarity.
    auto [allocatedSize, reportedSize] = *sizes;

    // Update the file's sizes.
    mInfo.allocatedSize(allocatedSize);
    mInfo.reportedSize(reportedSize);

    // Let the caller know how much data was written.
    return std::make_pair(count, success);
}

bool SparseFileBuffer::truncate(std::uint64_t newSize)
{
    // Couldn't truncate the file.
    if (!FileBuffer::truncate(newSize))
        return false;

    // Try and retrieve the file's updated sizes.
    auto sizes = mFile.getFileSize();

    // This should never fail.
    assert(sizes);

    // Destructure sizes for clarity.
    auto [allocatedSize, reportedSize] = *sizes;

    // Update the file's sizes.
    mInfo.allocatedSize(allocatedSize);
    mInfo.reportedSize(reportedSize);

    // Let the caller know the file was truncated.
    return true;
}

} // file_service
} // mega
