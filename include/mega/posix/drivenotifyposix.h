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
// Not implemented.
class DriveNotifyPosix final : public DriveNotify
{
public:
    ~DriveNotifyPosix() override { stopNotifier(); }
protected:
    bool notifierSetup() override;
    void doInThread() override;

private:
    void notifierTeardown();    // don't make it virtual, it's called from destructor

    void cacheMountedPartitions();
    bool isRemovable(udev_device* part);
    void evaluateDevice(udev_device* dev);  // dev must Not be null
    std::string getMountPoint(const std::string& device);

    udev* mUdev = nullptr;
    udev_monitor* mUdevMon = nullptr;
    std::map<std::string, std::string> mMounted;
};

} // namespace

#endif // USE_DRIVE_NOTIFICATIONS
