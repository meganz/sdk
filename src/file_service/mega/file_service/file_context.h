#pragma once

#include <mega/common/activity_monitor.h>
#include <mega/file_service/construction_logger.h>
#include <mega/file_service/destruction_logger.h>
#include <mega/file_service/file_context_forward.h>
#include <mega/file_service/file_info_context_pointer.h>
#include <mega/file_service/file_service_context_forward.h>

namespace mega
{
namespace file_service
{

class FileContext: DestructionLogger
{
    common::Activity mActivity;
    FileInfoContextPtr mInfo;
    FileServiceContext& mService;
    ConstructionLogger mConstructionLogger;

public:
    FileContext(common::Activity activity, FileInfoContextPtr info, FileServiceContext& service);

    ~FileContext();
}; // FileContext

} // file_service
} // mega
