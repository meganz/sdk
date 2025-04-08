#pragma once

#include <mega/file_service/file_context_pointer.h>
#include <mega/file_service/file_forward.h>
#include <mega/file_service/file_info_forward.h>
#include <mega/file_service/file_service_context_badge_forward.h>

namespace mega
{
namespace file_service
{

class File
{
    FileContextPtr mContext;

public:
    File(FileServiceContextBadge badge, FileContextPtr context);

    ~File();
}; // File

} // file_service
} // mega
