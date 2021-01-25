/**
 * @file mega/posix/drivenotifyposix.h
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


#include <thread>
#include <atomic>
#include <map>

struct udev;
struct udev_monitor;
struct udev_device;

namespace mega {

    // Posix: Platform specific definition
    //
    class DriveNotifyPosix : public DriveNotify
    {
    public:
        ~DriveNotifyPosix() override;

    protected:
        bool startNotifier() override;
        void stopNotifier() override;

    private:
        void cacheMountedPartitions();
        bool isRemovable(udev_device* part);
        void doInThread();
        void evaluateDevice(udev_device* dev);  // dev must Not be null
        std::string getMountPoint(const std::string& device);

        std::atomic_bool mStop;
        std::thread mEventSinkThread;

        udev* mUdev = nullptr;
        udev_monitor* mUdevMon = nullptr;
        std::map<std::string, std::string> mMounted;
    };



    // Posix: Platform specific definition
    //
    class UniqueDriveIdPosix : public UniqueDriveId
    {
    protected:
        std::map<int, std::string> getIds(const std::string& mountPoint) override;

    private:
        std::string getDevNode(const std::string& mountPoint);  // i.e. "/mnt/foo/bar" -> "/dev/sda1"
        std::string normalizeVolumeSN(const char* volumeSN);
    };

} // namespace

#endif // USE_DRIVE_NOTIFICATIONS
