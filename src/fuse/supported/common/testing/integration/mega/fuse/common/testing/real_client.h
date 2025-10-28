#pragma once

#include <mega/common/testing/path_forward.h>
#include <mega/common/testing/real_client.h>
#include <mega/fuse/common/mount_event_forward.h>
#include <mega/fuse/common/service_forward.h>
#include <mega/fuse/common/testing/client.h>

#include <string>

namespace mega
{
namespace fuse
{
namespace testing
{

class RealClient: public Client, public common::testing::RealClient
{
    // Called when the client emits a mount event.
    void onFuseEvent(const MountEvent& event) override;

public:
    RealClient(const std::string& clientName,
               const common::testing::Path& databasePath,
               const common::testing::Path& storagePath);

    ~RealClient();

    // Get our hands on the client's FUSE interface.
    Service& fuseService() const override;
}; // RealClient

} // testing
} // fuse
} // mega
