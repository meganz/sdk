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

namespace mega {

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
        voidSelf());

    DARegisterDiskDisappearedCallback(
        session,
        matchingDict(),
        &MediaTypeCallbacks::onDiskDisappeared,
        voidSelf());

    DARegisterDiskDescriptionChangedCallback(
        session,
        matchingDict(),
        keysToMonitor(),
        &MediaTypeCallbacks::onDiskDescriptionChanged,
        voidSelf());
}

void MediaTypeCallbacks::unregisterCallbacks(DASessionRef session)
{
    DAUnregisterCallback(session, &MediaTypeCallbacks::onDiskAppeared, voidSelf());
    DAUnregisterCallback(session, &MediaTypeCallbacks::onDiskDisappeared, voidSelf());
    DAUnregisterCallback(session, &MediaTypeCallbacks::onDiskDescriptionChanged, voidSelf());
}

PhysicalMediaCallbacks::PhysicalMediaCallbacks()
    : mMatchingDict([]{
        // Constructing with immediately invoked lambda expression to allow storage of immutable ref
        CFMutableDictionaryRef matchingDict = CFDictionaryCreateMutable(
            kCFAllocatorDefault,
            3,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);


        CFDictionaryAddValue(matchingDict, kDADiskDescriptionMediaWholeKey, kCFBooleanFalse);
        CFDictionaryAddValue(matchingDict, kDADiskDescriptionMediaRemovableKey, kCFBooleanTrue);
        CFDictionaryAddValue(matchingDict, kDADiskDescriptionMediaEjectableKey, kCFBooleanTrue);

        return matchingDict;
    }()),
    mKeysToMonitor(CFArrayCreate(
        kCFAllocatorDefault,
        reinterpret_cast<const void**>(&watchArray),
        1,
        &kCFTypeArrayCallBacks))
{}

bool PhysicalMediaCallbacks::shouldNotify(CFDictionaryRef diskDescription) const noexcept
{
    auto deviceProtocol = reinterpret_cast<CFStringRef>(
        CFDictionaryGetValue(diskDescription, kDADiskDescriptionDeviceProtocolKey));

    return CFStringCompare(deviceProtocol, CFSTR(kIOPropertyPhysicalInterconnectTypeVirtual), 0) != kCFCompareEqualTo;
}

void PhysicalMediaCallbacks::onDiskAppeared(DADiskRef disk, void* context)
{

}

NetworkDriveCallbacks::NetworkDriveCallbacks()
    : mMatchingDict([]{
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

bool DriveNotifyOsx::startNotifier()
{
    if (mEventSinkThread.joinable() || mStop.load()) return false;

    mPhysicalCbs.registerCallbacks(mSession);
    mNetworkCbs.registerCallbacks(mSession);

    mEventSinkThread = std::thread(&DriveNotifyOsx::doInThread, this);

    return true;
}

void DriveNotifyOsx::stopNotifier()
{
    if (!mEventSinkThread.joinable()) return;

    mStop.store(true);
    mEventSinkThread.join();

    mPhysicalCbs.unregisterCallbacks(session);
    mNetworkCbs.unregisterCallbacks(session);

    mStop.store(false);
}

bool DriveNotifyOsx::doInThread()
{
    CFRunLoopRef currentThread = CFRunLoopGetCurrent();

    DASessionScheduleWithRunLoop(mSession, currentThread, kCFRunLoopDefaultMode);

    // CFRunLoopRunInMode will run for the special setting of 0 seconds, which means process
    // precisely one event before returning.
    while (!mStop.load() && CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, true))
        ;

    CFRunLoopStop(currentThread);
}

void DriveNotifyOsx::onDiskAppeared(DADiskRef disk, void* context)
{
    if (shouldIgnore(disk)) return;

    auto& self = *reinterpret_cast<DriveNotifyOsx*>(context);

    auto description = UniqueCFRef<CFDictionaryRef>(DADiskCopyDescription(disk));

    auto path = reinterpret_cast<CFURLRef>(
        CFDictionaryGetValue(description, kDADiskDescriptionVolumePathKey));

    if (!path) {
        auto uuid = reinterpret_cast<CFUUIDRef>(CFDictionaryGetValue(description, kDADiskDescriptionVolumeUUIDKey));
        if (uuid) self.mDisksPendingPath.insert(CFUUIDGetUUIDBytes(uuid));

        return;
    }
}

void DriveNotifyOsx::onDiskDisappeared(DADiskRef disk, void* context)
{
    if (shouldIgnore(disk)) return;

    auto& self = *reinterpret_cast<DriveNotifyOsx*>(context);

    auto description = UniqueCFRef<CFDictionaryRef>(DADiskCopyDescription(disk));

    auto uuid = reinterpret_cast<CFUUIDRef>(CFDictionaryGetValue(description, kDADiskDescriptionVolumeUUIDKey));
    if (uuid) self.mDisksPendingPath.erase(CFUUIDGetUUIDBytes(uuid));
}

} // namespace mega

#endif // USE_DRIVE_NOTIFICATIONS