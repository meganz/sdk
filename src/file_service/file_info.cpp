#include <mega/file_service/file_context_badge.h>
#include <mega/file_service/file_info.h>
#include <mega/file_service/file_info_context.h>
#include <mega/file_service/file_location.h>
#include <mega/file_service/file_service_context_badge.h>
#include <mega/file_service/logger.h>

namespace mega
{
namespace file_service
{

void FileInfo::lock_shared() const
{
    mContext->lock_shared();
}

void FileInfo::unlock_shared() const
{
    mContext->unlock_shared();
}

FileInfo::FileInfo(FileContextBadge, FileInfoContextPtr context):
    mInstanceLogger("FileInfo", *this, logger()),
    mContext(std::move(context))
{}

FileInfo::FileInfo(FileServiceContextBadge, FileInfoContextPtr context):
    mInstanceLogger("FileInfo", *this, logger()),
    mContext(std::move(context))
{}

FileInfo::FileInfo(const FileInfo& other):
    mInstanceLogger("FileInfo", *this, logger()),
    mContext(other.mContext)
{}

FileInfo::~FileInfo() = default;

FileInfo& FileInfo::operator=(const FileInfo& rhs)
{
    if (this != &rhs)
        mContext = rhs.mContext;

    return *this;
}

std::int64_t FileInfo::accessed() const
{
    return mContext->accessed();
}

std::uint64_t FileInfo::allocatedSize() const
{
    return mContext->allocatedSize();
}

std::optional<std::uint64_t> FileInfo::bitrate() const
{
    return mContext->bitrate();
}

bool FileInfo::dirty() const
{
    return mContext->dirty();
}

std::optional<std::uint32_t> FileInfo::duration() const
{
    return mContext->duration();
}

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

bool FileInfo::removed() const
{
    return mContext->removed();
}

std::uint64_t FileInfo::reportedSize() const
{
    return mContext->reportedSize();
}

std::uint64_t FileInfo::size() const
{
    return mContext->size();
}

} // file_service
} // mega
