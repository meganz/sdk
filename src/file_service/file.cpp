#include <mega/file_service/file.h>
#include <mega/file_service/file_context.h>
#include <mega/file_service/file_info.h>
#include <mega/file_service/file_range.h>
#include <mega/file_service/file_read_request.h>
#include <mega/file_service/file_result.h>
#include <mega/file_service/file_result_or.h>
#include <mega/file_service/file_service_context_badge.h>
#include <mega/file_service/file_write_request.h>

#include <utility>

namespace mega
{
namespace file_service
{

File::File(FileServiceContextBadge, FileContextPtr context):
    mContext(std::move(context))
{}

File::~File() = default;

File::File(File&& other):
    mContext(std::move(other.mContext))
{}

File& File::operator=(File&& rhs)
{
    using std::swap;

    if (this != &rhs)
        swap(mContext, rhs.mContext);

    return *this;
}

FileInfo File::info() const
{
    return mContext->info();
}

FileRangeVector File::ranges() const
{
    return mContext->ranges();
}

void File::read(FileReadCallback callback, std::uint64_t offset, std::uint64_t length)
{
    // Delegate.
    read(std::move(callback), FileRange(offset, offset + length));
}

void File::read(FileReadCallback callback, const FileRange& range)
{
    // Ask the context to perform the read.
    mContext->read(FileReadRequest{std::move(callback), range});
}

void File::ref()
{
    mContext->ref();
}

void File::unref()
{
    mContext->unref();
}

void File::write(const void* buffer,
                 FileWriteCallback callback,
                 std::uint64_t offset,
                 std::uint64_t length)
{
    // Delegate.
    write(buffer, std::move(callback), FileRange(offset, offset + length));
}

void File::write(const void* buffer, FileWriteCallback callback, const FileRange& range)
{
    // Ask the context to perform the write.
    mContext->write(FileWriteRequest{buffer, std::move(callback), range});
}

} // file_service
} // mega
