/**
 * @file drivenotify.cpp
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


#include "mega/drivenotify.h"

using namespace std;



namespace mega {

    bool DriveNotify::start(function<void()> notify)
    {
        lock_guard<mutex> lock(mSyncAccessMutex);

        // start the notifier
        bool started = startNotifier();
        if (started)
        {
            mNotifyOnInfo = notify;
        }

        return started;
    }



    void DriveNotify::stop()
    {
        stopNotifier();
        decltype(mInfoQueue) temp;
        mInfoQueue.swap(temp); // clear the container
    }



    pair<wstring, bool> DriveNotify::get()
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



    void DriveNotify::add(DriveInfo&& info)
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
