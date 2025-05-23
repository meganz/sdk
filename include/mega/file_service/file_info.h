#pragma once

#include <mega/file_service/file_context_badge_forward.h>
#include <mega/file_service/file_id_forward.h>
#include <mega/file_service/file_info_context_pointer.h>
#include <mega/file_service/file_info_forward.h>
#include <mega/file_service/file_service_context_badge_forward.h>

#include <cstdint>

namespace mega
{

class NodeHandle;

namespace file_service
{

class FileInfo
{
    FileInfoContextPtr mContext;

public:
    FileInfo(FileContextBadge badge, FileInfoContextPtr context);

    FileInfo(FileServiceContextBadge badge, FileInfoContextPtr context);

    ~FileInfo();

    NodeHandle handle() const;

    FileID id() const;

    std::int64_t modified() const;

    std::uint64_t size() const;
}; // FileInfo

} // file_service
} // mega
