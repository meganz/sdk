#pragma once

#include <chrono>
#include <condition_variable>
#include <list>
#include <thread>

#include <mega/fuse/common/testing/mount_event_observer_forward.h>

#include <mega/fuse/common/mount_event_forward.h>
#include <mega/fuse/common/mount_event_type_forward.h>

namespace mega
{
namespace fuse
{
namespace testing
{

class MountEventObserver
{
    std::condition_variable mCV;
    std::list<MountEvent> mEvents;
    std::mutex mLock;

public:
    static MountEventObserverPtr create();

    void emitted(const MountEvent& event);

    void expect(MountEvent event);

    bool wait(std::chrono::steady_clock::time_point when);

    template<typename Rep, typename Period>
    bool wait(std::chrono::duration<Rep, Period> timeout)
    {
        return wait(std::chrono::steady_clock::now() + timeout);
    }
}; // MountEventObserver

} // testing
} // fuse
} // mega

