/**
 * @file mega/osx/drivenotifyosx.h
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

#pragma once
#ifdef USE_DRIVE_NOTIFICATIONS

// Include "mega/drivenotify.h" where needed.
// This header cannot be used by itself.

#include <atomic>
#include <memory>
#include <set>
#include <thread>

#include <CoreFoundation/CFBase.h>
#include <CoreFoundation/CFArray.h>
#include <CoreFoundation/CFDictionary.h>
#include <DiskArbitration/DiskArbitration.h>
#include <IOKit/storage/IOStorageProtocolCharacteristics.h>

namespace mega {

// Automatic memory management class template for "Create Rule" references to CoreFoundation types
// See
// https://developer.apple.com/library/archive/documentation/CoreFoundation/Conceptual/CFMemoryMgmt/Concepts/Ownership.html#//apple_ref/doc/uid/20001148-103029
template<class T>
class UniqueCFRef {
private:
    // CoreFoundation types such as dict, array, etc are passed around as CFDictionaryRef, CFArrayRef.
    // These Ref types are type aliases to a pointer to implementation-defined "unutterable" types.
    // Thus we use the type alias below since unique_ptr<T> holds a pointer to T, but we cannot name T.
    using Ptr = std::unique_ptr<typename std::remove_pointer<T>::type, decltype(&CFRelease)>;
public:
    using pointer = typename Ptr::pointer;

    // Construction from return value of CoreFoundation "create" function
    UniqueCFRef(pointer p) noexcept
        : mPtr(p, &CFRelease)
    {}

    // Implicit conversion to underlying reference type for easy interaction with CF interfaces
    operator pointer() const noexcept { return mPtr.get(); }

    operator bool() const noexcept { return mPtr; }
private:
    Ptr mPtr;
};

class DriveNotifyOsx;

// Encapsulate filtering and callbacks for different media types.
// For the purpose of DriveNotify we are mainly concerned with the presence of Volume Path, and
// within Disk Arbitration Framework (DAF), media types vary based on when in their lifetime the path
// (and other info) are or are not available.
// Specializations of this class may implement specific logic for Disk Arbitration callbacks,
// which will be registered to a session.
class MediaTypeCallbacks {
public:
    static UniqueCFRef<CFDictionaryRef> description(DADiskRef disk)
    {
        return UniqueCFRef<CFDictionaryRef>(DADiskCopyDescription(disk));
    }

    static CFURLRef volumePath(CFDictionaryRef diskDescription)
    {
        return reinterpret_cast<CFURLRef>(
            CFDictionaryGetValue(diskDescription, kDADiskDescriptionVolumePathKey));
    }

    static CFUUIDRef volumeUUID(CFDictionaryRef diskDescription)
    {
        return reinterpret_cast<CFUUIDRef>(
            CFDictionaryGetValue(diskDescription, kDADiskDescriptionVolumeUUIDKey));
    }

    // MediaTypeCallbacks lifetime is bound to a parent object to which it registers constructed DriveInfo
    MediaTypeCallbacks(DriveNotifyOsx& parent)
        : mParent(parent)
    {}

    // Register the disk appeared and disappeared callbacks.
    // Note: merely registers callbacks; does not start running them.
    void registerCallbacks(DASessionRef session);

    // Unregister all possible callbacks.
    void unregisterCallbacks(DASessionRef session);

    // The matching dictionary used to filter disk types in callbacks.
    // If this dictionary is used to register a callback then the callback only fires for disks that match
    // the dictionary's criteria.
    // All properties in the dictionary are checked with a logical-AND.
    virtual CFDictionaryRef matchingDict() const noexcept = 0;

    // Additional filtering step which takes place in callbacks after filtering by matchingDict.
    // Allows for finer-grained filtering based on traits not expressible through dictionary filtering.
    bool shouldNotify(DADiskRef disk) const noexcept
    {
        auto description = UniqueCFRef<CFDictionaryRef>(DADiskCopyDescription(disk));
        return shouldNotify(description);
    }

    virtual bool shouldNotify(CFDictionaryRef diskDescription) const noexcept = 0;

protected:
    // Add a drive to the parent DriveNotifyOsx object.
    void addDrive(CFURLRef path, bool connected);

    // Callback for when a disk appears to DAF.
    // Disk appearance means when a DAF session becomes aware of a disk. This includes disks that were
    // connected before the DAF session began.
    static void onDiskAppeared(DADiskRef disk, void* context);

    // Callback for when a disk disappears to DAF.
    // This callback fires for disks that are ejected or also yanked without proper ejection/unmounting.
    static void onDiskDisappeared(DADiskRef disk, void* context);

    // Callback for when a disk's description changes, adding or removing keys from the description dict.
    // In practice we are only interested in the appearance or disappearance of Volume Path, but greater
    // filtering is possible with changedKeys.
    static void onDiskDescriptionChanged(DADiskRef disk, CFArrayRef changedKeys, void* context)
    {
        auto& self = castSelf(context);
        if (!self.shouldNotify(disk)) return;

        self.onDiskDescriptionChangedImpl(disk, changedKeys, context);
    }

    // Callback for approval to unmount a disk.
    // Note: Approval callbacks in Disk Arbitration framework allow for operations to be disapproved which is out of scope
    // for our application so this returns null unconditionally and the impl method is void
    static DADissenterRef onUnmountApproval(DADiskRef disk, void* context);

private:
    static MediaTypeCallbacks& castSelf(void* context)
    {
        return *reinterpret_cast<MediaTypeCallbacks*>(context);
    }

    // Optional method to register additional callbacks other than appeared/disappeared
    virtual void registerAdditionalCallbacks(DASessionRef session) {}

    // Handle the appearance of a disk with no Volume Path key, if applicable
    virtual void handleNoPathAppeared(CFDictionaryRef diskDescription) {}

    // Perform additional processing on a disappearing disk, if applicable
    virtual void processDisappeared(CFDictionaryRef diskDescription) {}

    // The implementation of an onDiskDescriptionChanged callback.
    virtual void onDiskDescriptionChangedImpl(DADiskRef disk, CFArrayRef changedKeys, void* context)
    {
    }

    DriveNotifyOsx& mParent;
};

// Callbacks for physical media such as USB Drives
// Unlike network drives, there are various points in the lifetime of a DADiskRef object where its path
// is null. The callbacks below handle various cases related to ejection or physical removal of disks,
// and disks whose mounting occurs before or after program start.
// Disk appearance:
//  - if the disk was already plugged in before session start, onDiskAppeared is called with a volume path present
//  - else, we have to wait for the path to appear in onDiskDescriptionChanged with a volume path
// Disk disappearance:
//  - if the disk is yanked without ejecting, it appears in onDiskDisappeared with a volume path present
//  - else, we get the volume path in onUnmountApproval and mark it as removed there.
class PhysicalMediaCallbacks : public MediaTypeCallbacks {
public:
    PhysicalMediaCallbacks(DriveNotifyOsx& parent);

    // Filters removable and ejectable media corresponding to actual mounted partition
    CFDictionaryRef matchingDict() const noexcept override { return mMatchingDict; }

    // After filtration by matchingDict, ignore Virtual Interface drives
    bool shouldNotify(CFDictionaryRef diskDescription) const noexcept override;

private:
    // Register unmount approval and description changed callbacks.
    void registerAdditionalCallbacks(DASessionRef session) override;

    // If a disk appears with no path, store it for later notification in the pending collection
    // Physical media plugged in after the start of a DAF session shows to onDiskAppeared with no
    // volume path, so we store it for later registration.
    void handleNoPathAppeared(CFDictionaryRef diskDescription) override;

    // If a disk disappears, remove it from the pending collection
    void processDisappeared(CFDictionaryRef diskDescription) override;

    // Check if description has changed to add a Volume Path for a disk that previously appeared with no path
    void onDiskDescriptionChangedImpl(DADiskRef disk, CFArrayRef changedKeys, void* context) override;

    UniqueCFRef<CFDictionaryRef> mMatchingDict;

    // Monitor for changes in Volume Path for onDiskDescriptionChanged
    UniqueCFRef<CFArrayRef> mKeysToMonitor;

    // Set of drives which appeared in onDiskAppeared with no Volume Path
    // Drives in this set are "in limbo" and their existence will not be
    // announced until their description is changed in onDiskDescriptionChanged
    // to have a volume path, at which point they are removed from this set.
    // Disks which disappear are also removed.
    std::set<CFUUIDBytes, bool(*)(const CFUUIDBytes&, const CFUUIDBytes&)> mDisksPendingPath;
};

// Callbacks for Network Attached Storage
// Unlike PhysicalMedia, for NetworkDrive storage the Volume Path is always known at time of onDiskAppeared
// and onDiskDisappeared. Thus we do not override any of the specific helper methods or other callbacks, and
// no additional callbacks are registered.
class NetworkDriveCallbacks : public MediaTypeCallbacks {
public:
    NetworkDriveCallbacks(DriveNotifyOsx& parent);

    // Matching dict for network drives
    CFDictionaryRef matchingDict() const noexcept override { return mMatchingDict; }

    // After filtration by matchingDict, ignore autofs network volumes in /System/Volumes
    // see https://apple.stackexchange.com/questions/367158/whats-system-volumes-data
    bool shouldNotify(CFDictionaryRef diskDescription) const noexcept override;

private:
    UniqueCFRef<CFDictionaryRef> mMatchingDict;
};

class DriveNotifyOsx final : public DriveNotify {
public:
    DriveNotifyOsx();
    ~DriveNotifyOsx() override { stopNotifier(); }

protected:
    // Provide access to add(DriveInfo&&) method
    friend MediaTypeCallbacks;

    bool notifierSetup() override;

private:
    void doInThread() override;
    void notifierTeardown();    // don't make it virtual, it's called from destructor

    // Disk Arbitration framework session object
    UniqueCFRef<DASessionRef> mSession;

    PhysicalMediaCallbacks mPhysicalCbs;
    NetworkDriveCallbacks mNetworkCbs;
};

} // namespace mega

#endif // USE_DRIVE_NOTIFICATIONS
