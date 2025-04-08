#include <mega/file_service/file.h>
#include <mega/file_service/file_context.h>
#include <mega/file_service/file_service_context_badge.h>

namespace mega
{
namespace file_service
{

File::File(FileServiceContextBadge, FileContextPtr context):
    mContext(std::move(context))
{}

File::~File() = default;

} // file_service
} // mega
