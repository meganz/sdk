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

NodeHandle FileInfo::handle() const
{
    return mContext->handle();
}

FileID FileInfo::id() const
{
    return mContext->id();
}

std::int64_t FileInfo::modified() const
{
    return mContext->modified();
}

std::uint64_t FileInfo::size() const
{
    return mContext->size();
}

} // file_service
} // mega
