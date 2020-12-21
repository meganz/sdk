/**
 * @file driveinfocollector.cpp
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

#ifdef USE_DRIVE_NOTIFICATIONS


#include "mega/driveinfocollector.h"

using namespace std;



namespace mega {

    bool DriveInfoCollector::start(function<void()> notify)
    {
        auto addInfo = bind(&DriveInfoCollector::add, this, placeholders::_1);

        lock_guard<mutex> lock(mSyncAccessMutex);

        // start the notifier
        bool started = mNotifier.start(addInfo, addInfo);
        if (started)
        {
            mNotifyOnInfo = notify;
        }

        return started;
    }



    void DriveInfoCollector::stop()
    {
        mNotifier.stop();
        mInfoQueue.swap(decltype(mInfoQueue)()); // clear the container
    }



    pair<wstring, bool> DriveInfoCollector::get()
    {
        // sync access
        lock_guard<mutex> lock(mSyncAccessMutex);

        // no entry, return invalid data
        if (mInfoQueue.empty())  return pair<wstring, bool>();

        // get the oldest entry
        const DriveInfo& drive = mInfoQueue.front();
        pair<wstring, bool> info(move(drive.mountPoint), drive.connected);
        mInfoQueue.pop();

        return info;
    }



    void DriveInfoCollector::add(DriveInfo&& info)
    {
        // sync access
        lock_guard<mutex> lock(mSyncAccessMutex);

        // save the new info
        mInfoQueue.emplace(move(info));

        // notify that new info was received
        mNotifyOnInfo();
    }

} // namespace mega

#endif // USE_DRIVE_NOTIFICATIONS
