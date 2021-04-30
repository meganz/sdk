/**
 * @file mega/win32/drivenotifywin.h
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

#ifdef USE_DRIVE_NOTIFICATIONS

// Include "mega/drivenotify.h" where needed.
// This header cannot be used by itself.

#include <map>
#include <thread>
#include <atomic>


namespace mega {

// Windows: Platform specific definition
//
// Uses WMI. Use 'wbemtest' tool to run WQL queries, for testing and comparison.
class DriveNotifyWin final : public DriveNotify
{
public:
    ~DriveNotifyWin() override { stopNotifier(); }

protected:
    void doInThread() override;

    enum EventType
    {
        UNKNOWN_EVENT,
        DRIVE_CONNECTED_EVENT,
        DRIVE_DISCONNECTED_EVENT
    };
};

} // namespace

#endif // USE_DRIVE_NOTIFICATIONS
