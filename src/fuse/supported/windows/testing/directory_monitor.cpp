#include <mega/fuse/platform/windows.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <functional>

#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/testing/path.h>
#include <mega/fuse/platform/testing/directory_monitor.h>
#include <mega/fuse/platform/testing/wrappers.h>
#include <mega/fuse/platform/utility.h>

namespace mega
{
namespace fuse
{
namespace testing
{

using namespace platform;

constexpr auto BUFFER_LENGTH = 32768;

struct DirectoryMonitor::Buffer
{
    OVERLAPPED mOverlapped;
    char mStorage[BUFFER_LENGTH];
}; // DirectoryMonitor::Buffer

void DirectoryMonitor::emit(const DirectoryEvent& event)
{
    std::lock_guard<std::mutex> guard(mLock);

    auto i = std::find(mExpectations.begin(),
                       mExpectations.end(),
                       event);

    if (i == mExpectations.end())
        return;

    mExpectations.erase(i);

    mCV.notify_all();
}

void DirectoryMonitor::loop()
{
    // Make sure overlapped buffer is initialized.
    std::memset(&mBuffer->mOverlapped, 0, sizeof(mBuffer->mOverlapped));

    // Convenience.
    auto failed = [this]() {
        FUSEErrorF("Couldn't retrieve directory notifications: %u",
                   GetLastError());
    }; // failed

    for (std::wstring from; ; )
    {
        // Convenience.
        constexpr auto filter = FILE_NOTIFY_CHANGE_ATTRIBUTES
                                | FILE_NOTIFY_CHANGE_DIR_NAME
                                | FILE_NOTIFY_CHANGE_FILE_NAME
                                | FILE_NOTIFY_CHANGE_LAST_WRITE
                                | FILE_NOTIFY_CHANGE_SECURITY
                                | FILE_NOTIFY_CHANGE_SIZE;

        // Ask the system for a list of directory notifications.
        auto result = ReadDirectoryChangesW(mDirectory.get(),
                                            mBuffer->mStorage,
                                            sizeof(mBuffer->mStorage),
                                            true,
                                            filter,
                                            nullptr,
                                            &mBuffer->mOverlapped,
                                            nullptr);

        // Couldn't retrieve directory notifications.
        if (!result)
            return failed();

        ULONG_PTR key;
        DWORD num;
        OVERLAPPED* overlapped;

        // Wait for the system to post our result.
        result = GetQueuedCompletionStatus(mPort.get(),
                                           &num,
                                           &key,
                                           &overlapped,
                                           INFINITE);

        // Couldn't wait for the result.
        if (!result)
            return failed();

        // We've been asked to terminate.
        if (key == 'T')
            return;

        // Sanity.
        assert(key == 'F');

        // There were too many changes for the system to report.
        if (!num)
            continue;

        auto* position = mBuffer->mStorage;
        auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(position);

        // Process directory notifications.
        while (true)
        {
            auto length = info->FileNameLength / sizeof(wchar_t);

            // Sanity.
            assert(length);

            auto to = std::wstring(info->FileName, length);

            if (info->Action != FILE_ACTION_RENAMED_OLD_NAME)
            {
                DirectoryEvent event;

                event.mFrom = fromWideString(from);
                event.mTo = fromWideString(to);
                event.mType = info->Action;

                emit(event);

                from.clear();
            }
            else
            {
                from = std::move(to);
            }

            if (!info->NextEntryOffset)
                break;

            position += info->NextEntryOffset;
            info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(position);
        }
    }
}

DirectoryMonitor::DirectoryMonitor(const Path& path)
  : mBuffer(new Buffer())
  , mCV()
  , mDirectory()
  , mExpectations()
  , mLock()
  , mPort()
  , mWorker()
{
    // Try and open specified directory.
    auto directory = CreateFileP(path,
                                 GENERIC_READ,
                                 FILE_SHARE_DELETE
                                 | FILE_SHARE_READ
                                 | FILE_SHARE_WRITE,
                                 nullptr,
                                 OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS
                                 | FILE_FLAG_OVERLAPPED,
                                 Handle<>());

    // Couldn't open specified directory.
    if (!directory)
        throw FUSEErrorF("Couldn't open directory: %s: %u",
                         path.path().u8string().c_str(),
                         GetLastError());

    // Try and create an IO completion port.
    Handle<> port(CreateIoCompletionPort(directory.get(), nullptr, 'F', 0));

    // Couldn't create IO completion port.
    if (!port)
        throw FUSEErrorF("Couldn't create IO completion port: %u",
                         GetLastError());

    // Spawn worker thread.
    mWorker = std::thread(std::bind(&DirectoryMonitor::loop, this));

    // Latch directory and completion port.
    mDirectory = std::move(directory);
    mPort = std::move(port);
}

DirectoryMonitor::~DirectoryMonitor()
{
    // This should never fail.
    if (!PostQueuedCompletionStatus(mPort.get(), 0, 'T', nullptr))
        FUSEErrorF("Couldn't notify completion port: %u", GetLastError());

    // Wait for the worker to terminate.
    mWorker.join();
}

void DirectoryMonitor::expect(DirectoryEvent event)
{
    std::lock_guard<std::mutex> guard(mLock);

    mExpectations.emplace_back(std::move(event));
}

bool DirectoryMonitor::wait(std::chrono::steady_clock::time_point until)
{
    std::unique_lock lock(mLock);

    return mCV.wait_until(lock, until) == std::cv_status::no_timeout;
}

} // testing
} // fuse
} // mega

