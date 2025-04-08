#include <mega/common/utility.h>
#include <mega/file_service/file_context.h>
#include <mega/file_service/file_context_badge.h>
#include <mega/file_service/file_id.h>
#include <mega/file_service/file_info_context.h>
#include <mega/file_service/file_service_context.h>
#include <mega/filesystem.h>

namespace mega
{
namespace file_service
{

using namespace common;

FileContext::FileContext(Activity activity,
                         FileAccessPtr file,
                         FileInfoContextPtr info,
                         FileServiceContext& service):
    mActivity(std::move(activity)),
    mFile(std::move(file)),
    mInfo(std::move(info)),
    mService(service)
{}

FileContext::~FileContext()
{
    mService.removeFromIndex(FileContextBadge(), mInfo->id());
}

} // file_service
} // mega
