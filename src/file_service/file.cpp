#include <mega/file_service/file.h>
#include <mega/file_service/file_context.h>
#include <mega/file_service/file_info.h>
#include <mega/file_service/file_range.h>
#include <mega/file_service/file_read_request.h>
#include <mega/file_service/file_result.h>
#include <mega/file_service/file_result_or.h>
#include <mega/file_service/file_service_context_badge.h>

namespace mega
{
namespace file_service
{

File::File(FileServiceContextBadge, FileContextPtr context):
    mContext(std::move(context))
{}

File::~File() = default;

FileInfo File::info() const
{
    return mContext->info();
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

FileRangeVector File::ranges() const
{
    return mContext->ranges();
}

} // file_service
} // mega
