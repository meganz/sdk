#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <thread>

#include <mega/fuse/common/testing/path_forward.h>
#include <mega/fuse/platform/handle.h>

namespace mega
{
namespace fuse
{
namespace testing
{

struct DirectoryEvent
{
    bool operator==(const DirectoryEvent& rhs) const
    {
        return mType == rhs.mType
               && mFrom == rhs.mFrom
               && mTo == rhs.mTo;
    }

    Path mFrom;
    Path mTo;
    unsigned long mType;
}; // DirectoryEvent

class DirectoryMonitor
{
    struct Buffer;

    void emit(const DirectoryEvent& event);

    void loop();

    std::unique_ptr<Buffer> mBuffer;
    std::condition_variable mCV;
    platform::Handle<> mDirectory;
    std::list<DirectoryEvent> mExpectations;
    std::mutex mLock;
    platform::Handle<> mPort;
    std::thread mWorker;

public:
    DirectoryMonitor(const Path& path);

    ~DirectoryMonitor();

    void expect(DirectoryEvent event);

    bool wait(std::chrono::steady_clock::time_point until);

    template<typename Rep, typename Period>
    bool wait(std::chrono::duration<Rep, Period> delay)
    {
        return wait(std::chrono::steady_clock::now() + delay);
    }
}; // DirectoryMonitor

} // testing
} // fuse
} // mega

