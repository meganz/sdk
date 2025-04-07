#include <mega/file_service/file_info.h>
#include <mega/file_service/file_info_context.h>
#include <mega/file_service/file_service_context_badge.h>

namespace mega
{
namespace file_service
{

FileInfo::FileInfo(FileServiceContextBadge, FileInfoContextPtr context):
    mContext(std::move(context))
{}

FileInfo::~FileInfo() = default;

auto FileInfo::handle() const -> NodeHandle
{
    return mContext->handle();
}

auto FileInfo::id() const -> FileID
{
    return mContext->id();
}

auto FileInfo::modified() const -> std::int64_t
{
    return mContext->modified();
}

auto FileInfo::size() const -> std::uint64_t
{
    return mContext->size();
}

} // file_service
} // mega
