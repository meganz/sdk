/**
 * @file mega/volumewatcher.cpp
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

#include "VolumeWatcher.h"

#include <QStorageInfo>



void VolumeWatcher::notify(uint32_t seconds, notificationFuncType notifyRemoved, notificationFuncType notifyAdded)
{
    // quick check that the received parameters make sense
    assert((!seconds && !notifyRemoved && !notifyAdded) // cancel notifications
           || (seconds && (notifyRemoved || notifyAdded))); // request notifications

    if (seconds)  initPolling(seconds, notifyRemoved, notifyAdded);

    else  stopPolling();
}


void VolumeWatcher::initPolling(uint32_t seconds, notificationFuncType notifyRemoved, notificationFuncType notifyAdded)
{
    std::lock_guard<std::mutex> lock(mPollingMutex);

    // reset polling and notification details; this works for an already created thread too.
    mPollingIntervalMs = seconds * 1000;
    mNotifyRemoved = notifyRemoved;
    mNotifyAdded = notifyAdded;

    // create and start the thread if not done already
    if (!mPollingThread.joinable())
    {
        mPollingThread = std::thread(&VolumeWatcher::poll, this);
    }
}


void VolumeWatcher::poll()
{
    for (;;)
    {
        // make copies of member variables
        uint32_t pollingIntervalMs;
        notificationFuncType removed;
        notificationFuncType added;

        {
            std::lock_guard<std::mutex> lock(mPollingMutex);
            pollingIntervalMs = mPollingIntervalMs;
            removed = mNotifyRemoved;
            added = mNotifyAdded;
        }

        // 0 polling-interval means it will stop
        if (!pollingIntervalMs)  return;

        // update volumes at every polling-interval wakeup
        if (!(mSinceLastPollMs % pollingIntervalMs))
        {
            mSinceLastPollMs = 0;
            updateVolumes(removed, added);
        }

        // sleep for another short interval
        std::this_thread::sleep_for(std::chrono::milliseconds(wakeupInterval));
        mSinceLastPollMs += wakeupInterval; // intermadiary wakeups are used to check if it needed to stop
    }
}


void VolumeWatcher::updateVolumes(notificationFuncType notifyRemoved, notificationFuncType notifyAdded)
{
    // get mounted volumes
    std::set<VolumeInfo> volumesNow;

    const auto& list = QStorageInfo::mountedVolumes();
    for (auto& v : list)
    {
        // Win:   {"D:/",  "\\?\Volume{9b687c32-66e9-11e0-af36-806e6f6e6963}\"}
        // Linux: {"/foo", "/dev/sda3"}
        VolumeInfo vi{ v.rootPath().toStdString(), v.device().toStdString() };
        volumesNow.emplace(vi);
    }

    // check for removed volumes
    if (notifyRemoved)
    {
        std::set<VolumeInfo> removed;

        std::set_difference(mVolumes.begin(), mVolumes.end(),
                            volumesNow.begin(), volumesNow.end(),
                            std::inserter(removed, removed.end()));

        if (!removed.empty())  notifyRemoved(std::move(removed));
    }

    // check for added volumes
    if (notifyAdded)
    {
        std::set<VolumeInfo> added;

        std::set_difference(volumesNow.begin(), volumesNow.end(),
                            mVolumes.begin(), mVolumes.end(),
                            std::inserter(added, added.begin()));

        if (!added.empty())  notifyAdded(std::move(added));
    }

    // cache info of current volumes
    mVolumes = volumesNow;
}


void VolumeWatcher::stopPolling()
{
    if (!mPollingThread.joinable())  return;

    // lock_guard scope
    {
        std::lock_guard<std::mutex> lock(mPollingMutex);
        mPollingIntervalMs = 0; // this will make secondary thread stop
        mNotifyRemoved = nullptr;
        mNotifyAdded = nullptr;
    }

    mPollingThread.join();
}


bool operator<(const VolumeInfo& v1, const VolumeInfo& v2)
{
    return v1.rootPath < v2.rootPath;
}
