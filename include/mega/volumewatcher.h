/**
 * @file mega/volumewatcher.h
 * @brief Mega SDK various utilities and helper classes
 *
 * (c) 2013-2020 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#pragma once

#include <functional>
#include <set>
#include <mutex>
#include <thread>

struct VolumeInfo;



class VolumeWatcher final
{
public:
    using notificationFuncType = std::function<void(std::set<VolumeInfo>&&)>;

    void notify(uint32_t seconds, // Notification interval (!0: start notifications; 0: stop notifications)
                notificationFuncType notifyRemoved = nullptr,
                notificationFuncType notifyAdded = nullptr);

    ~VolumeWatcher() { stopPolling(); }

private:
    void initPolling(uint32_t seconds, notificationFuncType notifyRemoved, notificationFuncType notifyAdded);
    void poll();
    void updateVolumes(notificationFuncType notifyRemoved, notificationFuncType notifyAdded);
    void stopPolling();

    // members accessed from both threads
    notificationFuncType mNotifyRemoved;
    notificationFuncType mNotifyAdded;
    std::mutex mPollingMutex;
    std::thread mPollingThread;
    uint32_t mPollingIntervalMs = 0; // keep this in millisecond, to simplify the use of intermediary wakeups

    // members accessed from polling thread only
    std::set<VolumeInfo> mVolumes;
    uint32_t mSinceLastPollMs = 0; // millisecond
    static constexpr uint32_t wakeupInterval = 500; // interval used to check whether the thread should stop
};


struct VolumeInfo
{
    // Win:     "D:/"
    // Linux:   "/foo"
    std::string rootPath;

    // Win:     "\\?\Volume{9b687c32-66e9-11e0-af36-806e6f6e6963}\"
    // Linux:   "/dev/sda3"
    std::string device;
};

bool operator<(const VolumeInfo& v1, const VolumeInfo& v2);
