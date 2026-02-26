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
#include <libudev.h> // Ubuntu: sudo apt-get install libudev-dev
#include <mntent.h>
#include <cstring>
#include <chrono>



namespace mega {

//
// DriveNotifyPosix
/////////////////////////////////////////////

bool DriveNotifyPosix::notifierSetup()
{
    // init udev resource
    mUdev = udev_new();
    if (!mUdev)  return false;  // is udevd daemon running?

    // init udev monitor
    mUdevMon = udev_monitor_new_from_netlink(mUdev, "udev");
    if (!mUdevMon)
    {
        udev_unref(mUdev);
        mUdev = nullptr;
        return false;
    }

    cacheMountedPartitions();

    // On unix systems you need to define your udev rules to allow notifications for
    // your device.
    //
    // i.e. on Ubuntu create file "100-megasync-udev.rules" either in
    // /etc/udev/rules.d/        OR
    // /usr/lib/udev/rules.d/
    // and add line:
    // SUBSYSTEM=="block", ATTRS{idDevtype}=="partition"
    udev_monitor_filter_add_match_subsystem_devtype(mUdevMon, "block", "partition");
    udev_monitor_enable_receiving(mUdevMon);

    return true;
}



void DriveNotifyPosix::notifierTeardown()
{
    // release udev monitor
    if (mUdevMon)
    {
        udev_monitor_filter_remove(mUdevMon);
        udev_monitor_unref(mUdevMon);
        mUdevMon = nullptr;
    }

    // release udev resource
    if (mUdev)
    {
        udev_unref(mUdev);
        mUdev = nullptr;
    }

    mMounted.clear();
}



void DriveNotifyPosix::doInThread()
{
    int fd = udev_monitor_get_fd(mUdevMon);
    while (!shouldStop())
    {
        // use a blocking call, with a timeout, to check for device events
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);

        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 250 * 1000; // 250ms

        int ret = select(fd+1, &fds, nullptr, nullptr, &tv);
        if (ret <= 0 || !FD_ISSET(fd, &fds))  continue;

        // get any [dis]connected device
        udev_device* dev = udev_monitor_receive_device(mUdevMon);
        if (dev)
        {
            evaluateDevice(dev);

            udev_device_unref(dev);
        }
    }

    // do some cleanup
    notifierTeardown();
}



void DriveNotifyPosix::evaluateDevice(udev_device* dev)  // dev must Not be null
{
    // filter "partition" events
    const char* devtype = udev_device_get_devtype(dev); // "partition"
    if (!devtype || strcmp(devtype, "partition"))  return;

    // filter "add"/"remove" actions
    const char* action = udev_device_get_action(dev); // "add" / "remove"
    bool added = action && !strcmp(action, "add");
    bool removed = action && !added && !strcmp(action, "remove");
    if (!(added || removed))  return; // ignore other possible actions

    // get device location
    const char* devNode = udev_device_get_devnode(dev); // "/dev/sda1"
    if (!devNode)  return; // did something go wrong?

    const std::string& devNodeStr(devNode);

    // gather drive info
    DriveInfo drvInfo;
    drvInfo.connected = added;

    // get the mount point
    if (removed)
    {
        // get it from the cache, because it will have already been removed from
        // the relevant locations by the time getMountPoint() will attempt to read it
        drvInfo.mountPoint = mMounted[devNodeStr];
        mMounted.erase(devNodeStr); // remove from cache
    }
    else // added
    {
        // reading it might happen before the relevant locations have been updated,
        // so allow retrying a few times, say at 100ms intervals
        for (int i = 0; i < 5; ++i)
        {
            drvInfo.mountPoint = getMountPoint(devNodeStr);

            if (!drvInfo.mountPoint.empty())
            {
                mMounted[devNodeStr] = drvInfo.mountPoint; // cache it
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // send notification
    if (!drvInfo.mountPoint.empty())
    {
        add(std::move(drvInfo));
    }
}



std::string DriveNotifyPosix::getMountPoint(const std::string& device)
{
    std::string mountPoint;

    // go to all mounts
    FILE* fsMounts = setmntent("/proc/mounts", "r");
    if (!fsMounts) // this should never happen
    {
        // make another attempt
        fsMounts = setmntent("/etc/mtab", "r");
        if (!fsMounts)  return mountPoint;
    }

    // search mount point for the current device
    for (mntent* mnt = getmntent(fsMounts); mnt; mnt = getmntent(fsMounts))
    {
        if (device == mnt->mnt_fsname)
        {
            mountPoint = mnt->mnt_dir;
            break;
        }
    }

    endmntent(fsMounts); // closes the file descriptor

    return mountPoint;
}



void DriveNotifyPosix::cacheMountedPartitions()
{
    // enumerate all mounted partitions
    udev_enumerate* enumerate = udev_enumerate_new(mUdev);

    udev_enumerate_add_match_subsystem(enumerate, "block");
    udev_enumerate_add_match_property(enumerate, "DEVTYPE", "partition");
    udev_enumerate_scan_devices(enumerate);

    udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
    udev_list_entry *entry;

    udev_list_entry_foreach(entry, devices) // for loop
    {
        // get a partition
        const char* entryName = udev_list_entry_get_name(entry);
        udev_device* blockDev = udev_device_new_from_syspath(mUdev, entryName);
        if (!blockDev)  continue;

        // filter only removable ones
        if (isRemovable(blockDev))
        {
            // get partition node
            const char* devNode = udev_device_get_devnode(blockDev); // "/dev/sda1"

            // cache mount point
            std::string mountPoint = getMountPoint(devNode);
            if (!mountPoint.empty())
            {
                mMounted[devNode] = mountPoint;
            }
        }

        udev_device_unref(blockDev);
    }

    udev_enumerate_unref(enumerate);
}



bool DriveNotifyPosix::isRemovable(udev_device* part)
{
    udev_device* parent = udev_device_get_parent(part); // do not unref this one!
    if (!parent)  return false;

    const char* removable = udev_device_get_sysattr_value(parent, "removable");
    return removable && !strcmp(removable, "1");
}

} // namespace

#endif // USE_DRIVE_NOTIFICATIONS

