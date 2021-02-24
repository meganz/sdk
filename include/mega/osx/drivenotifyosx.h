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

namespace detail {

// Custom deleter for use with unique_ptr specialization below
struct CoreFoundationDeleter {
    void operator()(CFTypeRef cf)
    {
        CFRelease(cf);
    }
};

// Incomplete class for partial template specialization deduction
// Complex CoreFoundation types such as dictionary or array are passed around as, e.g.,
// CFArrayRef or CFDictionaryRef,
// which are typedefs pointers to implementation-defined types not meant to be named or instantiated
// by users. Since unique_ptr<T> holds a T*, but we can't name T, the classes below serve
// as type deduction helpers. 
template<class T>
struct UniqueCFRefImpl;

template<class T>
struct UniqueCFRefImpl<T*> {
    using type = std::unique_ptr<T, CoreFoundationDeleter>;
};

// Key comparison operator< for set<CFUUIDBytes>. 
struct UUIDLess {
    bool operator()(const CFUUIDBytes& u1, const CFUUIDBytes& u2) const noexcept
    {
        return std::memcmp(&u1, &u2, sizeof(CFUUIDBytes)) < 0;
    }
};

} // namespace detail

// Automatic memory management class template for "Create Rule" references to CoreFoundation types
// See 
// https://developer.apple.com/library/archive/documentation/CoreFoundation/Conceptual/CFMemoryMgmt/Concepts/Ownership.html#//apple_ref/doc/uid/20001148-103029
template<class T>
class UniqueCFRef {
using Ptr = typename detail::UniqueCFRefImpl<T>::type;
public:
    using pointer = typename Ptr::pointer;

    // Construction from return value of CoreFoundation "create" function
    UniqueCFRef(pointer p) noexcept
        : mPtr(p, {})
    {}

    // Implicit conversion to underlying reference type for easy interaction with CF interfaces
    operator pointer() const noexcept { return mPtr.get(); }

    operator bool() const noexcept { return mPtr; }
private:
    Ptr mPtr;
};

class DriveNotifyOsx;

// Encapsulate filtering and callbacks for different media types.
// Depending on the disk type being notified, the logic for getting names, paths, etc
// may vary considerably. This class must implement a subset of Disk Arbitration callbacks
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

    MediaTypeCallbacks(DriveNotifyOsx& parent)
        : mParent(&parent)
    {}

    // Register the disk appeared and disappeared callbacks, and additional implementation-specific ones
    // Note: merely registers callbacks; does not start running them.
    void registerCallbacks(DASessionRef session);

    // Unregister the callbacks registered by registerCallbacks.
    void unregisterCallbacks(DASessionRef session);

    // The matching dictionary used to filter disk types in callbacks
    virtual CFDictionaryRef matchingDict() const noexcept = 0;

    // Additional filtering step which takes place in callbacks after
    // filtering by matchingDict
    bool shouldNotify(DADiskRef disk) const noexcept
    {
        return shouldNotify(UniqueCFRef<CFDictionaryRef>(DADiskCopyDescription(disk)));
    }

    virtual bool shouldNotify(CFDictionaryRef diskDescription) const noexcept = 0;

protected:
    void addDrive(CFURLRef path, bool connected);

    static void onDiskAppeared(DADiskRef disk, void* context);
    static void onDiskDisappeared(DADiskRef disk, void* context);

    static void onDiskDescriptionChanged(DADiskRef disk, CFArrayRef changedKeys, void* context)
    {
        auto& self = castSelf(context);
        if (!self.shouldNotify(disk)) return;

        self.onDiskDescriptionChangedImpl(disk, changedKeys, context);
    }

    // Approval callbacks in Disk Arbitration framework allow for operations to be disapproved which is out of scope
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
    virtual void noPathAppeared(CFDictionaryRef diskDescription) {}

    // Perform additional processing on a disappearing disk, if applicable
    virtual void processDisappeared(CFDictionaryRef diskDescription) {}

    virtual void onDiskDescriptionChangedImpl(DADiskRef disk, CFArrayRef changedKeys, void* context)
    {       
    }

    DriveNotifyOsx* mParent;
};

// Callbacks for physical media such as USB Drives
// Unlike network drives, there are various points in the lifetime of a DADiskRef object where its path
// is null. The callbacks below handle various cases related to ejection or physical removal of disks,
// and disks whose mounting occurs before or after program start
class PhysicalMediaCallbacks : public MediaTypeCallbacks {
public:
    PhysicalMediaCallbacks(DriveNotifyOsx& parent);

    // Filters removable and ejectable media corresponding to actual mounted partition
    CFDictionaryRef matchingDict() const noexcept override { return mMatchingDict; }

    // After filtration by matchingDict, ignore Virtual Interface drives
    bool shouldNotify(CFDictionaryRef diskDescription) const noexcept override;

private:
    void registerAdditionalCallbacks(DASessionRef session) override;

    // If a disk appears with no path, store it for later notification in the pending collection
    void noPathAppeared(CFDictionaryRef diskDescription) override;

    // If a disk disappears, remove it from the pending collection
    void processDisappeared(CFDictionaryRef diskDescription) override;

    // Check if description has changed to add a Volume Path for a disk that previously appeared with no path
    void onDiskDescriptionChangedImpl(DADiskRef disk, CFArrayRef changedKeys, void* context);

    UniqueCFRef<CFDictionaryRef> mMatchingDict;

    // Monitor for changes in Volume Path for onDiskDescriptionChanged
    UniqueCFRef<CFArrayRef> mKeysToMonitor;

    // Set of drives which appeared in onDiskAppeared with no Volume Path
    // Drives in this set are "in limbo" and their existence will not be
    // announced until their description is changed in onDiskDescriptionChanged
    // to have a volume path, at which point they are removed from this set.
    // Disks which disappear are also removed.
    std::set<CFUUIDBytes, detail::UUIDLess> mDisksPendingPath;
};

// Callbacks for Network Attached Storage
// Unlike PhysicalMedia, for NetworkDrive storage the Volume Path is always known at time of onDiskAppeared
// This implementation does not override any of the stub methods related to onDiskDescriptionChanged
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

class DriveNotifyOsx : public DriveNotify {
public:
    DriveNotifyOsx();

    ~DriveNotifyOsx() override { stop(); }

protected:
    friend MediaTypeCallbacks;

    bool startNotifier() override;
    void stopNotifier() override;

private:
    void doInThread();

    std::atomic_bool mStop{false};
    std::thread mEventSinkThread;

    // Disk Arbitration framework session object
    UniqueCFRef<DASessionRef> mSession;

    PhysicalMediaCallbacks mPhysicalCbs;
    NetworkDriveCallbacks mNetworkCbs;
};

}

#endif // USE_DRIVE_NOTIFICATIONS