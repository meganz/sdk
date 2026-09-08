#include <mega/fuse/common/service_context.h>
#include <mega/fuse/common/service_flags.h>
#include <mega/fuse/common/service.h>

namespace mega
{
namespace fuse
{

using namespace common;
using namespace file_service;

ServiceContext::ServiceContext(FileService& fileService, Service& service):
    mFileService(fileService),
    mService(service)
{
}

ServiceContext::~ServiceContext()
{
}

Client& ServiceContext::client() const
{
    return mService.mClient;
}

void ServiceContext::serviceFlags(const ServiceFlags&)
{
}

ServiceFlags ServiceContext::serviceFlags() const
{
    return mService.serviceFlags();
}

} // fuse
} // mega

