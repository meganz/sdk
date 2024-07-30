#include <algorithm>

#include <mega/fuse/common/mount_event.h>
#include <mega/fuse/common/testing/mount_event_observer.h>

namespace mega
{
namespace fuse
{
namespace testing
{

MountEventObserverPtr MountEventObserver::create()
{
    return std::make_shared<MountEventObserver>();
}

void MountEventObserver::emitted(const MountEvent& event)
{
    std::lock_guard<std::mutex> guard(mLock);

    auto i = std::find(mEvents.begin(), mEvents.end(), event);

    if (i == mEvents.end())
        return;

    mEvents.erase(i);

    if (mEvents.empty())
        mCV.notify_all();
}

void MountEventObserver::expect(MountEvent event)
{
    std::lock_guard<std::mutex> guard(mLock);

    mEvents.emplace_back(std::move(event));
}

bool MountEventObserver::wait(std::chrono::steady_clock::time_point when)
{
    std::unique_lock<std::mutex> lock(mLock);

    return mCV.wait_until(lock, when, [&]() {
        return mEvents.empty();
    });
}

} // testing
} // fuse
} // mega

