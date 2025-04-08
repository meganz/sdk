#include <mega/common/utility.h>
#include <mega/file_service/file_context.h>
#include <mega/file_service/file_context_badge.h>
#include <mega/file_service/file_id.h>
#include <mega/file_service/file_info_context.h>
#include <mega/file_service/file_service_context.h>

namespace mega
{
namespace file_service
{

using namespace common;

static std::string name(FileID id);

FileContext::FileContext(Activity activity, FileInfoContextPtr info, FileServiceContext& service):
    DestructionLogger(name(info->id())),
    mActivity(std::move(activity)),
    mInfo(std::move(info)),
    mService(service),
    mConstructionLogger(name(info->id()))
{}

FileContext::~FileContext()
{
    mService.removeFromIndex(FileContextBadge(), mInfo->id());
}

std::string name(FileID id)
{
    return format("File Context %s", toString(id).c_str());
}

} // file_service
} // mega
