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

#include <map>
#include <thread>
#include <atomic>


class IWbemLocator;
class IWbemServices;
class IWbemClassObject;



// Structure containing relevant Drive info, provided by Windows Management Instrumentation (WMI),
// that is Microsoft's implementation of WBEM
struct VolumeInfo
{                                           //     Local              Removable/USB         Network
    std::wstring deviceId;                  // C:                   E:                  F:
    std::wstring description;               // Local Fixed Disk     Removable Disk      Network Connection
    uint32_t    driveType = 0;              // 3                    2                   4
    uint32_t    mediaType = 0;              // 12                   (NULL)              4
    std::wstring providerName;              // (NULL)               (NULL)              \\host\f
    std::wstring size;                      // 1005343207424        31020957696         843572047872
    std::wstring volumeSerialNumber;        // EE82D138             0EEE1DE2            A01A541C
};



// Base class containing COM initialization code, and common property reading code for WMI.
// Not really useful on its own.
class VolumeWmiBase
{
public:
    virtual ~VolumeWmiBase() = default;

protected:
    static bool InitializeCom();
    static bool GetWbemService(IWbemLocator** pLocator, IWbemServices** pService);

    static VolumeInfo GetProperties(IWbemClassObject* pQueryObject);

    static uint32_t GetUi32Property(IWbemClassObject* pQueryObject, const std::wstring& name);
    static std::wstring GetStringProperty(IWbemClassObject* pQueryObject, const std::wstring& name);
};



// Class for receiving drive [dis]connection notifications.
// Use 'wbemtest' Windws tool to run WQL queries, for testing and comparison.
class VolumeWatcher : public VolumeWmiBase
{
public:
    using NotificationFunc = std::function<void(VolumeInfo&&)>;

    // Start the thread that will send notifications for every drive [dis]connection event.
    // driveDisconnected: function called to notify about a disconnected drive
    // driveConnected: function called to notify about a connected drive
    bool start(NotificationFunc driveDisconnected, NotificationFunc driveConnected);

    // Stop the thread that sends notifications for every drive [dis]connection event.
    void stop();

    VolumeWatcher() : mStop(false) {}
    ~VolumeWatcher() override { stop(); }

private:
    bool doInThread(NotificationFunc driveRemoved, NotificationFunc driveAdded);

    std::atomic_bool mStop;
    std::thread mEventSinkThread;

    enum EventType
    {
        UNKNOWN_EVENT,
        DRIVE_CONNECTED_EVENT,
        DRIVE_DISCONNECTED_EVENT
    };
};



// Class for listing drives available at any moment.
// This class queries 'Win32_LogicalDisk' for drives available to the user, that are assigned a drive letter.
// In the same way, for further information about the partition and physical drive, other providers can be queried:
// - Win32_LogicalDiskToPartition
// - Win32_DiskPartition
// - Win32_DiskDriveToDiskPartition
// - Win32_DiskDrive
// - Win32_MappedLogicalDisk -- only for mapped drives
// Other non-WMI Volume Management functions available under MS Windows:
// https://docs.microsoft.com/en-us/windows/win32/fileio/volume-management-functions
// Nice article about possible Dynamic Disk Structures:
// https://www.apriorit.com/dev-blog/345-dynamic-disk-structure-parser
class VolumeQuery : public VolumeWmiBase
{
public:
    std::map<std::wstring, VolumeInfo> query();
};
