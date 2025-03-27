#pragma once

#include <future>
#include <mutex>
#include <set>
#include <thread>

#include <mega/fuse/common/mount_db.h>
#include <mega/fuse/platform/mount_db_forward.h>
#include <mega/fuse/platform/service_context_forward.h>
#include <mega/fuse/platform/session_forward.h>
#include <mega/fuse/platform/signal.h>

namespace mega
{
namespace fuse
{
namespace platform
{

class MountDB final
  : public fuse::MountDB
{
    MountResult check(const common::Client& client,
                      const MountInfo& info) const override;

    void dispatch();

    void doDeinitialize() override;

    void loop();

    std::mutex mLock;
    Signal mPendingAdd;
    FromSessionRawPtrMap<std::promise<void>> mPendingAdds;
    Signal mPendingRemove;
    FromSessionRawPtrMap<std::promise<void>> mPendingRemoves;
    SessionRawPtrSet mSessions;
    Signal mTerminate;
    std::thread mThread;

public:
    MountDB(ServiceContext& context);

    void sessionAdded(Session& session);

    void sessionRemoved(Session& session);
}; // MountDB

} // platform
} // fuse
} // mega

