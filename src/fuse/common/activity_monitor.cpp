#include <cassert>
#include <utility>

#include <mega/fuse/common/activity_monitor.h>

namespace mega
{
namespace fuse
{

Activity::Activity(ActivityMonitor& monitor)
  : mMonitor(&monitor)
{
    std::lock_guard<std::mutex> guard(monitor.mLock);

    ++monitor.mProcessing;

    assert(monitor.mProcessing);
}

Activity::Activity()
  : mMonitor(nullptr)
{
}

Activity::Activity(const Activity& other)
  : mMonitor(other.mMonitor)
{
    if (!mMonitor)
        return;

    std::lock_guard<std::mutex> guard(mMonitor->mLock);

    ++mMonitor->mProcessing;

    assert(mMonitor->mProcessing);
}

Activity::Activity(Activity&& other)
  : mMonitor(std::move(other.mMonitor))
{
    other.mMonitor = nullptr;
}

Activity::~Activity()
{
    if (!mMonitor)
        return;

    std::lock_guard<std::mutex> guard(mMonitor->mLock);

    assert(mMonitor->mProcessing);

    --mMonitor->mProcessing;

    if (!mMonitor->mProcessing)
        mMonitor->mCompleted.notify_all();
}

Activity& Activity::operator=(const Activity& rhs)
{
    Activity temp(rhs);

    swap(temp);

    return *this;
}

Activity& Activity::operator=(Activity&& rhs)
{
    Activity temp(std::move(rhs));

    swap(temp);

    return *this;
}

void Activity::swap(Activity& other)
{
    using std::swap;

    swap(mMonitor, other.mMonitor);
}

ActivityMonitor::ActivityMonitor()
  : mCompleted()
  , mLock()
  , mProcessing(0u)
{
}

ActivityMonitor::~ActivityMonitor()
{
    waitUntilIdle();
}

bool ActivityMonitor::active() const
{
    std::lock_guard<std::mutex> guard(mLock);

    return mProcessing;
}

Activity ActivityMonitor::begin()
{
    return Activity(*this);
}

void ActivityMonitor::waitUntilIdle()
{
    std::unique_lock<std::mutex> lock(mLock);

    mCompleted.wait(lock, [&]() { return !mProcessing; });
}

} // fuse
} // mega

