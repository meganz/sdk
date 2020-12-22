/**
 * @file mega/posix/drivenotifyposix.cpp
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



namespace mega {

    //
    // DriveNotifyPosix
    /////////////////////////////////////////////

    bool DriveNotifyPosix::startNotifier()
    {
        return false;
    }



    void DriveNotifyPosix::stopNotifier()
    {
    }



    ~DriveNotifyPosix::DriveNotifyPosix()
    {
    }

} // namespace

#endif // USE_DRIVE_NOTIFICATIONS
