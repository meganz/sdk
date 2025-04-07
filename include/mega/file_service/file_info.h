#pragma once

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
    FileInfo(FileServiceContextBadge badge, FileInfoContextPtr context);

    ~FileInfo();

    auto handle() const -> NodeHandle;

    auto id() const -> FileID;

    auto modified() const -> std::int64_t;

    auto size() const -> std::uint64_t;
}; // FileInfo

} // file_service
} // mega
