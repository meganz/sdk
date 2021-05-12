/**
 * @file osx/drivenotifyosx.cpp
 * @brief Mega SDK various utilities and helper classes
 *
 * (c) 2013-2021 by Mega Limited, Auckland, New Zealand
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

#include <sys/param.h>

namespace mega {

// CFArrayRef objects need to be constructed from const C-style array.
// We watch for changes in the Volume Path Key.
static const CFStringRef watchArray[1] = {kDADiskDescriptionVolumePathKey};

// See note here regarding callback registration/unregistration:
// https://developer.apple.com/library/archive/documentation/DriversKernelHardware/Conceptual/DiskArbitrationProgGuide/ArbitrationBasics/ArbitrationBasics.html#//apple_ref/doc/uid/TP40009310-CH2-SW9
// MediaTypeCallbacks is an abstract base class which uses the Non-Virtual Interface Pattern
// to implement callbacks, which are static member functions, where the `void* context` is a pointer
// to a concrete derived class. Per the note above, we are registering the same callback (i.e., pointer to static method)
// with a different context (i.e., pointer to derived class cast to void*)

void MediaTypeCallbacks::registerCallbacks(DASessionRef session)
{
    DARegisterDiskAppearedCallback(
        session,
        matchingDict(),
        &MediaTypeCallbacks::onDiskAppeared,
        this);

    DARegisterDiskDisappearedCallback(
        session,
        matchingDict(),
        &MediaTypeCallbacks::onDiskDisappeared,
        this);

    registerAdditionalCallbacks(session);
}

void MediaTypeCallbacks::unregisterCallbacks(DASessionRef session)
{
    DAUnregisterCallback(session, reinterpret_cast<void*>(&MediaTypeCallbacks::onDiskAppeared), this);
    DAUnregisterCallback(session, reinterpret_cast<void*>(&MediaTypeCallbacks::onDiskDisappeared), this);

    DAUnregisterCallback(session, reinterpret_cast<void*>(&MediaTypeCallbacks::onDiskDescriptionChanged), this);

    DAUnregisterApprovalCallback(session, reinterpret_cast<void*>(&MediaTypeCallbacks::onUnmountApproval), this);
}

void MediaTypeCallbacks::addDrive(CFURLRef path, bool connected)
{
    char buf[MAXPATHLEN];
    if (CFURLGetFileSystemRepresentation(path, false, reinterpret_cast<UInt8*>(buf), sizeof(buf))) 
    {
        DriveInfo di;
        di.mountPoint = buf;
        di.connected = connected;
        mParent.add(std::move(di));
    }
}

void MediaTypeCallbacks::onDiskAppeared(DADiskRef disk, void* context)
{
    MediaTypeCallbacks& self = castSelf(context);

    UniqueCFRef<CFDictionaryRef> diskDescription = description(disk);

    if (!self.shouldNotify(diskDescription)) return;

    CFURLRef path = volumePath(diskDescription);

    if (path) 
    {
        self.addDrive(path, true);
    }
    else
    {
        self.handleNoPathAppeared(diskDescription);
    }
}

void MediaTypeCallbacks::onDiskDisappeared(DADiskRef disk, void* context)
{
    MediaTypeCallbacks& self = castSelf(context);

    UniqueCFRef<CFDictionaryRef> diskDescription = description(disk);

    if (!self.shouldNotify(diskDescription)) return;

    CFURLRef path = volumePath(diskDescription);

    if (path) self.addDrive(path, false);

    self.processDisappeared(diskDescription);
}

DADissenterRef MediaTypeCallbacks::onUnmountApproval(DADiskRef disk, void* context)
{
    MediaTypeCallbacks::onDiskDisappeared(disk, context);
    return nullptr;
}

PhysicalMediaCallbacks::PhysicalMediaCallbacks(DriveNotifyOsx& parent)
    : MediaTypeCallbacks(parent),
     mMatchingDict([]{
        // Constructing with immediately invoked lambda expression to allow storage of immutable ref
        CFMutableDictionaryRef matchingDict = CFDictionaryCreateMutable(
            kCFAllocatorDefault,
            3,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);


        // Do not match Whole Media, else we get multiple notifications for the same drive
        CFDictionaryAddValue(matchingDict, kDADiskDescriptionMediaWholeKey, kCFBooleanFalse);

        // Match removable/ejectable media
        CFDictionaryAddValue(matchingDict, kDADiskDescriptionMediaRemovableKey, kCFBooleanTrue);
        CFDictionaryAddValue(matchingDict, kDADiskDescriptionMediaEjectableKey, kCFBooleanTrue);

        return matchingDict;
    }()),
    mKeysToMonitor(CFArrayCreate(
        kCFAllocatorDefault,
        reinterpret_cast<const void**>(&watchArray),
        1,
        &kCFTypeArrayCallBacks)),
    mDisksPendingPath([](const CFUUIDBytes& u1, const CFUUIDBytes& u2) noexcept {
        return std::memcmp(&u1, &u2, sizeof(CFUUIDBytes)) < 0;
    })
{}

bool PhysicalMediaCallbacks::shouldNotify(CFDictionaryRef diskDescription) const noexcept
{
    auto deviceProtocol = reinterpret_cast<CFStringRef>(
        CFDictionaryGetValue(diskDescription, kDADiskDescriptionDeviceProtocolKey));

    return CFStringCompare(deviceProtocol, CFSTR(kIOPropertyPhysicalInterconnectTypeVirtual), 0) != kCFCompareEqualTo;
}

void PhysicalMediaCallbacks::registerAdditionalCallbacks(DASessionRef session)
{
    DARegisterDiskDescriptionChangedCallback(
        session,
        matchingDict(),
        mKeysToMonitor,
        &MediaTypeCallbacks::onDiskDescriptionChanged,
        this);

    DARegisterDiskUnmountApprovalCallback(
        session,
        matchingDict(),
        &MediaTypeCallbacks::onUnmountApproval,
        this);
}

void PhysicalMediaCallbacks::handleNoPathAppeared(CFDictionaryRef diskDescription)
{
    CFUUIDRef uuid = volumeUUID(diskDescription);
    if (uuid) mDisksPendingPath.insert(CFUUIDGetUUIDBytes(uuid));
}

void PhysicalMediaCallbacks::processDisappeared(CFDictionaryRef diskDescription)
{
    CFUUIDRef uuid = volumeUUID(diskDescription);
    if (uuid) mDisksPendingPath.erase(CFUUIDGetUUIDBytes(uuid));
}

// The actual onDiskDescriptionChanged callback allows for significantly greater generality than our use, which is
// basically just to try again performing the onDiskAppeared logic with slight modification
void PhysicalMediaCallbacks::onDiskDescriptionChangedImpl(DADiskRef disk, CFArrayRef matchingKeys, void* context)
{
    auto diskDescription = UniqueCFRef<CFDictionaryRef>(MediaTypeCallbacks::description(disk));

    CFURLRef path = volumePath(diskDescription);
    if (!path) return;

    CFUUIDRef uuid = volumeUUID(diskDescription);
    if (!uuid) return;

    auto findItr = mDisksPendingPath.find(CFUUIDGetUUIDBytes(uuid));
    if (findItr == mDisksPendingPath.end()) return;

    addDrive(path, true);

    mDisksPendingPath.erase(findItr);
}

NetworkDriveCallbacks::NetworkDriveCallbacks(DriveNotifyOsx& parent)
    : MediaTypeCallbacks(parent),
    mMatchingDict([]{
        // Constructing with immediately invoked lambda expression to allow storage of immutable ref
        CFMutableDictionaryRef matchingDict = CFDictionaryCreateMutable(
            kCFAllocatorDefault,
            1,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);

        CFDictionaryAddValue(matchingDict, kDADiskDescriptionVolumeNetworkKey, kCFBooleanTrue);

        return matchingDict;
    }())
{}

bool NetworkDriveCallbacks::shouldNotify(CFDictionaryRef diskDescription) const noexcept
{
    auto volumeKind = reinterpret_cast<CFStringRef>(
        CFDictionaryGetValue(diskDescription, kDADiskDescriptionVolumeKindKey));

    return CFStringCompare(volumeKind, CFSTR("autofs"), 0) != kCFCompareEqualTo;
}

DriveNotifyOsx::DriveNotifyOsx()
    : mSession(DASessionCreate(kCFAllocatorDefault)),
    mPhysicalCbs(*this),
    mNetworkCbs(*this)
{}

bool DriveNotifyOsx::notifierSetup()
{
    mPhysicalCbs.registerCallbacks(mSession);
    mNetworkCbs.registerCallbacks(mSession);

    return true;
}

void DriveNotifyOsx::notifierTeardown()
{
    mPhysicalCbs.unregisterCallbacks(mSession);
    mNetworkCbs.unregisterCallbacks(mSession);
}

void DriveNotifyOsx::doInThread()
{
    CFRunLoopRef currentThread = CFRunLoopGetCurrent();

    DASessionScheduleWithRunLoop(mSession, currentThread, kCFRunLoopDefaultMode);

    // CFRunLoopRunInMode will run for the special setting of 0 seconds, which means process
    // precisely one event before returning.
    while (!shouldStop() && CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, true))
        ;

    CFRunLoopStop(currentThread);
}

} // namespace mega

#endif // USE_DRIVE_NOTIFICATIONS
