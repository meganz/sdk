/**
 * @file mega/drivenotify.h
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

#include <functional>
#include <string>
#include <queue>
#include <mutex>
#include <map>

namespace mega {

// Interface for getting drive unique id.
//
// Platform specific implementations:
// - UniqueDriveIdWin;
// - UniqueDriveIdPosix.
class UniqueDriveId
{
public:
    /**
     * @brief Get the unique id of the removable drive at mountPoint.
     *
     * Get the ids enumerated in IdOrder below and concatenate them
     * to form the unique id of the removable drive at mountPoint.
     *
     * @param mountPoint must be of the following form:
     *     Windows:  "D:"           (no trailing separator)
     *     Linux:    "/mnt/foo/bar" (no trailing separator)
     * @param idSep is the separator between ids enumerated in IdOrder
     *
     * @return the unique id of the removable drive at mountPoint.
     * When errors have occurred, an empty string will be returned.
     */
    std::string getFor(const std::string& mountPoint, const char idSep = '+');

protected:
    virtual std::map<int, std::string> getIds(const std::string& mountPoint) = 0;

    enum IdOrder
    {
        DISK_ID,            // 27C1609381310127
        DISK_SIGNATURE,     // A00A72BA
        VOLUME_SN,          // 0EEE1DE2
        ID_COUNT
    };
};


// Structure containing relevant Drive info.
// Windows: information is provided by Windows Management Instrumentation (WMI), Microsoft's implementation of WBEM.
struct DriveInfo                          //    Local               Removable/USB          Network
{
    using StringType =
#ifdef _WIN32
    std::wstring;
#else
    std::string;
#endif

    StringType mountPoint;                // C:                   E:                    F:
    std::wstring location;                // ""/null              ""/null               \\host\f
    std::wstring volumeSerialNumber;      // EE82D138             0EEE1DE2              A01A541C

    // probably less useful
    std::wstring size;                    // 1005343207424        31020957696           843572047872
    std::wstring description;             // Local Fixed Disk     Removable Disk        Network Connection
    uint32_t     driveType = 0;           // 3    (Fixed)         2      (Removable)    4      (Network)
    uint32_t     mediaType = 0;           // 12   (Fixed HD)      0/null (Unknown)      0/null (Unknown)

    bool         connected = false;
};


// Interface for receiving drive [dis]connection events, and notifying futher.
//
// Platform specific implementations:
// - DriveNotifyWin;
// - DriveNotifyPosix.
class DriveNotify
{
public:
    bool start(std::function<void()> notify);
    void stop();

    std::pair<DriveInfo::StringType, bool> get();

    // This will most likely need to be overridden to call stop().
    virtual ~DriveNotify() = default;

protected:
    virtual bool startNotifier() = 0;
    virtual void stopNotifier() = 0;
    void add(DriveInfo&& info);

private:
    std::queue<DriveInfo> mInfoQueue;
    std::mutex mSyncAccessMutex;

    std::function<void()> mNotifyOnInfo;
};

} // namespace mega


#ifdef _WIN32
#include "mega/win32/drivenotifywin.h"
namespace mega {
    using ExternalDriveId = UniqueDriveIdWin;
    using DriveInfoCollector = DriveNotifyWin;
}
#else
#include "mega/posix/drivenotifyposix.h"
namespace mega {
    using ExternalDriveId = UniqueDriveIdPosix;
    using DriveInfoCollector = DriveNotifyPosix;
}
#endif

#endif // USE_DRIVE_NOTIFICATIONS
