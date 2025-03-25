#include <sys/mount.h>
#include <sys/param.h>

#include <cassert>
#include <future>

#include "mega.h"

// Disk Arbitration is not available on iOS.
#ifndef USE_IOS
#    include <DiskArbitration/DADisk.h>
#endif // ! USE_IOS

namespace mega {

// Convenience.
template<typename T>
class CFPtr
{
    T mRef;

public:
    CFPtr()
      : mRef(nullptr)
    {
    }

    explicit CFPtr(T ref)
      : mRef(ref)
    {
    }

    CFPtr(const CFPtr& other)
      : mRef(other.mRef)
    {
        if (mRef)
            CFRetain(mRef);
    }

    CFPtr(CFPtr&& other)
      : mRef(other.mRef)
    {
        other.mRef = nullptr;
    }

    ~CFPtr()
    {
        if (mRef)
            CFRelease(mRef);
    }

    operator bool() const
    {
        return mRef;
    }

    CFPtr& operator=(const CFPtr& rhs)
    {
        CFPtr temp(rhs);

        std::swap(mRef, temp.mRef);

        return *this;
    }

    CFPtr& operator=(CFPtr&& rhs)
    {
        CFPtr temp(std::move(rhs));

        std::swap(mRef, temp.mRef);

        return *this;
    }

    bool operator!() const
    {
        return mRef == nullptr;
    }

    T get() const
    {
        return mRef;
    }
}; // CFPtr<T>

// Convenience.
using DeviceOfResult =
  std::pair<std::string, std::uint64_t>;

static DeviceOfResult deviceOf(const std::string& path)
{
    struct statfs buffer;
    DeviceOfResult result;
    
    // Couldn't determine which device contains path.
    if (statfs(path.c_str(), &buffer))
    {
        // Latch error.
        auto error = errno;
        
        LOG_err << "Couldn't determine which device contains "
                << path
                << ": "
                << strerror(error);
                
        return result;
    }

    // Compute legacy fingerprint.
    std::memcpy(&result.second,
                &buffer.f_fsid,
                sizeof(result.second));

    ++result.second;

    // Latch device mount point.
    result.first = buffer.f_mntfromname;

    // Return result to caller.
    return result;
}

#ifndef USE_IOS

static std::string uuidOf(const std::string& device)
{
    // Convenience.
    using DictionaryPtr = CFPtr<CFDictionaryRef>;
    using DiskPtr       = CFPtr<DADiskRef>;
    using SessionPtr    = CFPtr<DASessionRef>;
    using StringPtr     = CFPtr<CFStringRef>;

    auto allocator = kCFAllocatorDefault;

    // Try and establish a DA session.
    SessionPtr session(DASessionCreate(allocator));

    // Couldn't establish DA session.
    if (!session)
        return {};

    // Try and get a reference to the specified device.
    DiskPtr disk(DADiskCreateFromBSDName(allocator,
                                         session.get(),
                                         device.c_str()));

    // Couldn't get a reference to the device.
    if (!disk)
        return {};

    // Try and get the device's description.
    DictionaryPtr info(DADiskCopyDescription(disk.get()));

    // Couldn't get device's description.
    if (!info)
        return {};

    // What UUIDs do we want to retrieve?
    static const std::vector<const char*> names = {
        // UUID of a particular filesystem.
        "DAVolumeUUID",
        // UUID of a particular device.
        "DAMediaUUID"
    }; // names

    // Convenience.
    auto encoding = kCFStringEncodingASCII;

    // Try and extract one of the UUIDs named above.
    for (auto* name : names)
    {
        // Translate name into a usable key.
        StringPtr key(CFStringCreateWithCStringNoCopy(allocator,
                                                      name,
                                                      encoding,
                                                      kCFAllocatorNull));

        // Couldn't create key.
        if (!key)
            return {};

        // Try and retrieve UUID associated with key.
        auto uuid = ([&]() {
            // Try and retrieve value.
            auto value = CFDictionaryGetValue(info.get(), key.get());
            
            // Return value casted to appropriate type.
            return static_cast<CFUUIDRef>(value);
        })();

        // Key had no UUID.
        if (!uuid)
            continue;

        // Try and translate UUID to a string.
        StringPtr string(CFUUIDCreateString(allocator, uuid));

        // Couldn't translate UUID to a string.
        if (!string)
            break;

        // Try and retrieve the string's raw value.
        if (auto* value = CFStringGetCStringPtr(string.get(), encoding))
            return value;

        // Compute necessary buffer length.
        auto required =
          CFStringGetMaximumSizeForEncoding(CFStringGetLength(string.get()),
                                            encoding);

        // Sanity.
        assert(required > 0);

        // Create suitable sized buffer.
        std::string buffer(static_cast<std::size_t>(required + 1), 'X');

        // Try and transcode the UUID's string value.
        auto result = CFStringGetCString(string.get(),
                                         &buffer[0],
                                         required + 1,
                                         encoding);

        // Couldn't transcode the UUID's string value.
        if (!result)
            break;

        // Shrink buffer to fit.
        buffer.resize(static_cast<std::size_t>(required));

        // Return UUID to caller.
        return buffer;
    }

    // Couldn't retrieve UUID.
    return {};
}

#else // ! USE_IOS

static std::string uuidOf(const std::string&)
{
    return {};
}

#endif // USE_IOS

fsfp_t FileSystemAccess::fsFingerprint(const LocalPath& path) const
{
    // Convenience.
    using detail::adjustBasePath;

    // What device contains path?
    auto device = deviceOf(adjustBasePath(path));
    
    // Couldn't determine which device contains path.
    if (device.first.empty())
        return fsfp_t();

    // Try and determine the device's UUID.
    auto uuid = uuidOf(device.first);

    // Return fingerprint to caller.
    return fsfp_t(device.second, std::move(uuid));
}

MacFileSystemAccess::MacFileSystemAccess()
  : PosixFileSystemAccess()
  , mDispatchQueue(nullptr)
  , mNumNotifiers(0u)
{
}

MacFileSystemAccess::~MacFileSystemAccess()
{
    // Make sure there are no notifiers active.
    assert(mNumNotifiers == 0);

    // Bail if we don't have a dispatch queue.
    if (!mDispatchQueue)
        return;

    // Release the dispatch queue.
    dispatch_release(mDispatchQueue);
}

void MacFileSystemAccess::addevents(Waiter*, int)
{
    // Here until we factor Linux stuff out of PFSA.
}

int MacFileSystemAccess::checkevents(Waiter*)
{
    // Here until we factor Linux stuff out of PFSA.
    return 0;
}

void MacFileSystemAccess::flushDispatchQueue()
{
    dispatch_barrier_sync_f(mDispatchQueue, nullptr, [](void*) {});
}

#ifdef ENABLE_SYNC

bool MacFileSystemAccess::initFilesystemNotificationSystem()
{
    static const char* name = "mega.FilesystemMonitor";

    // Try and create a dispatch queue.
    mDispatchQueue = dispatch_queue_create(name, DISPATCH_QUEUE_SERIAL);

    // We're successful if the queue was created.
    return mDispatchQueue;
}

DirNotify* MacFileSystemAccess::newdirnotify(LocalNode& root,
                                             const LocalPath& rootPath,
                                             Waiter* waiter)
{
    // Make sure we've been passed a sane waiter.
    assert(waiter);

    return new MacDirNotify(*this, root, rootPath, *waiter);
}

MacDirNotify::MacDirNotify(MacFileSystemAccess& owner,
                           LocalNode& root,
                           const LocalPath& rootPath,
                           Waiter& waiter)
  : DirNotify(rootPath)
  , mEventStream(nullptr)
  , mOwner(owner)
  , mRoot(root)
  , mRootPathLength()
  , mWaiter(waiter)
{
    // Assume we'll be unable to create the event stream.
    setFailed(1, "Unable to create filesystem event stream.");

    // Create the event stream.
    mEventStream = ([&rootPath, this]{
        // What path are we monitoring?
        auto path = CFStringCreateWithCString(nullptr,
                                              rootPath.toPath(false).c_str(),
                                              kCFStringEncodingUTF8);

        // What paths are we monitoring?
        auto paths = CFArrayCreate(
                       nullptr,
                       reinterpret_cast<const void**>(&path),
                       1,
                       &kCFTypeArrayCallBacks);

        // Now owned by the array.
        CFRelease(path);

        // Context describing the stream we're about to create.
        FSEventStreamContext context{};

        // Passed to the trampoline when it is invoked.
        context.info = this;

        // Flags customizing the stream's behavior.
        constexpr auto flags = kFSEventStreamCreateFlagFileEvents;

        // How long the stream should wait before sending events.
        constexpr auto latency = 0.1;

        // Try and create the stream.
        auto stream = FSEventStreamCreate(nullptr,
                                          &MacDirNotify::trampoline,
                                          &context,
                                          paths,
                                          kFSEventStreamEventIdSinceNow,
                                          latency,
                                          flags);

        // No longer necessary.
        CFRelease(paths);

        // Return the stream to the caller.
        return stream;
    })();

    // Bail if we couldn't create the stream.
    if (!mEventStream)
        return;

    // How long is the normalized root path((like std::canonical and Exclude any trailing separator)?
    LocalPath expanseRootPath;
    if (!FSACCESS_CLASS().expanselocalpath(rootPath, expanseRootPath))
    {
        LOG_err << "Fail to expanseRootPath:" << rootPath.toPath(false).c_str();
        return;
    }
    mRootPathLength = expanseRootPath.endsInSeparator() ? expanseRootPath.toPath(false).size() - 1 :
                                                          expanseRootPath.toPath(false).size();

    // Specify who should process our filesystem events.
    FSEventStreamSetDispatchQueue(mEventStream, mOwner.mDispatchQueue);

    // Start monitoring filesystem events.
    FSEventStreamStart(mEventStream);

    // Let the owner know we're active.
    ++mOwner.mNumNotifiers;

    // Let the engine know everything's ok.
    setFailed(0, "");
}

MacDirNotify::~MacDirNotify()
{
    // No stream? Nothing to clean up.
    if (!mEventStream)
        return;

    // Stop monitoring the filesystem for events.
    FSEventStreamStop(mEventStream);

    // Remove the stream from the owner's run loop.
    FSEventStreamInvalidate(mEventStream);

    // Destroy the event stream.
    FSEventStreamRelease(mEventStream);

    // Works on the queue might target for this MacDirNotify object.
    // Or the callback is running might is owned by this MacDirNotify object.
    // This call ensures that all tasks on the dispatch queue have been completed.
    mOwner.flushDispatchQueue();

    // Let our owner know we are no longer active.
    --mOwner.mNumNotifiers;
}

void MacDirNotify::callback(const FSEventStreamEventFlags* flags,
                            std::size_t numEvents,
                            const char** paths)
{
    while (numEvents--)
    {
        auto flag = *flags++;

        auto path = *paths++;

        LOG_debug << "FSNotification: " << flag << " " << path;

        path += mRootPathLength;

        // Skip leading seperator.
        if (*path == '/')
            ++path;

        // Has the root path been invalidated?
        if ((flag & kFSEventStreamEventFlagRootChanged))
            setFailed(EINVAL, "The root path has been invalidated.");

        // Has a device been unmounted below the root?
        if ((flag & kFSEventStreamEventFlagUnmount))
            setFailed(EINVAL, "A device has been unmounted below the root path.");

        // even a folder renamed, comes in as that folder's path and we need to rescan the parent to see the changed name
        auto scanFlags = Notification::NEEDS_PARENT_SCAN;

        // Have some events been coalesced?
        if ((flag & kFSEventStreamEventFlagMustScanSubDirs))
            scanFlags = Notification::FOLDER_NEEDS_SCAN_RECURSIVE;

        // log the unusual possiblities
        if (flag == kFSEventStreamEventFlagNone) LOG_debug << "FSEv flag none";
        if (flag & kFSEventStreamEventFlagMustScanSubDirs) LOG_debug << "FSEv scan subdirs";
        if (flag & kFSEventStreamEventFlagUserDropped) LOG_debug << "FSEv user dropped";
        if (flag & kFSEventStreamEventFlagKernelDropped) LOG_debug << "FSEv kernel dropped";
        if (flag & kFSEventStreamEventFlagEventIdsWrapped) LOG_debug << "FSEv ids wrapped";
        if (flag & kFSEventStreamEventFlagHistoryDone) LOG_debug << "FSEv history done";
        if (flag & kFSEventStreamEventFlagRootChanged) LOG_debug << "FSEv root changed";
        if (flag & kFSEventStreamEventFlagMount) LOG_debug << "FSEv mount";
        if (flag & kFSEventStreamEventFlagUnmount) LOG_debug << "FSEv unmount";
        if (flag & kFSEventStreamEventFlagItemCreated) LOG_debug << "FSEv item created";
        if (flag & kFSEventStreamEventFlagItemRemoved) LOG_debug << "FSEv item removed";
        //if (flag & kFSEventStreamEventFlagItemInodeMetaMod) LOG_debug << "FSEv inode meta mod";
        //if (flag & kFSEventStreamEventFlagItemRenamed) LOG_debug << "FSEv item renamed";
        //if (flag & kFSEventStreamEventFlagItemModified) LOG_debug << "FSEv item modified";
        if (flag & kFSEventStreamEventFlagItemFinderInfoMod) LOG_debug << "FSEv finder info mod";
        if (flag & kFSEventStreamEventFlagItemChangeOwner) LOG_debug << "FSEv change owner";
        if (flag & kFSEventStreamEventFlagItemXattrMod) LOG_debug << "FSEv xattr mod";
        //if (flag & kFSEventStreamEventFlagItemIsFile) LOG_debug << "FSEv is file";
        //if (flag & kFSEventStreamEventFlagItemIsDir) LOG_debug << "FSEv is dir";
        if (flag & kFSEventStreamEventFlagItemIsSymlink) LOG_debug << "FSEv is symlink";
        if (flag & kFSEventStreamEventFlagOwnEvent) LOG_debug << "FSEv own event";
        if (flag & kFSEventStreamEventFlagItemIsHardlink) LOG_debug << "FSEv is hard link";
        if (flag & kFSEventStreamEventFlagItemIsLastHardlink) LOG_debug << "FSEv is last hard link";
        //if (flag & kFSEventStreamEventFlagItemCloned) LOG_debug << "FSEv item cloned";

        // Pass the notification to the engine.
        notify(fsEventq,
               &mRoot,
               scanFlags,
               LocalPath::fromPlatformEncodedRelative(path));

        // No need for the below if we're performing a recursive scan.
        if (scanFlags == Notification::FOLDER_NEEDS_SCAN_RECURSIVE)
            continue;

        // Are we dealing with a directory?
        if (!(flag & kFSEventStreamEventFlagItemIsDir))
            continue;

        // Has its permissions changed?
        if (!(flag & kFSEventStreamEventFlagItemChangeOwner))
            continue;

        LOG_debug << "FSNotification folder self-rescan: " << path;

        // If so, rescan the directory's contents.
        //
        // The reason for this is that we may not have been able to list
        // the directory's contents before. If we didn't rescan, we
        // wouldn't notice these files until some other event is
        // triggered in or below this directory.
        notify(fsEventq,
               &mRoot,
               Notification::FOLDER_NEEDS_SELF_SCAN,
               LocalPath::fromPlatformEncodedRelative(path));
    }

    // Let the engine know it has events to process.
    mWaiter.notify();
}

void MacDirNotify::trampoline(ConstFSEventStreamRef,
                              void *context,
                              std::size_t numPaths,
                              void* paths,
                              const FSEventStreamEventFlags* flags,
                              const FSEventStreamEventId*)
{
    // What instance is associated with these events?
    auto instance = reinterpret_cast<MacDirNotify*>(context);

    // Let the instance process the events.
    instance->callback(flags, numPaths, static_cast<const char**>(paths));
}

#endif // ENABLE_SYNC

} // mega

