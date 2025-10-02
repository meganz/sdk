#include <mega/fuse/common/mount_event.h>
#include <mega/fuse/common/testing/real_client.h>

namespace mega
{
namespace fuse
{
namespace testing
{

using common::testing::Path;

void RealClient::onFuseEvent(const MountEvent& event)
{
    mountEvent(event);
}

RealClient::RealClient(const std::string& clientName,
                       const Path& databasePath,
                       const Path& storagePath):
    common::testing::Client(clientName, databasePath, storagePath),
    Client(clientName, databasePath, storagePath),
    common::testing::RealClient(clientName, databasePath, storagePath)
{
    // Make sure FUSE logs *everything*.
    mClient->mFuseService.logLevel(logDebug);
}

RealClient::~RealClient() {}

Service& RealClient::fuseService() const
{
    return mClient->mFuseService;
}

} // testing
} // fuse
} // mega
