#include <future>

#include "mega.h"

namespace mega {

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
        auto path = CFStringCreateWithCString(
                      nullptr,
                      rootPath.localpath.c_str(),
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

    // How long is the root path?
    mRootPathLength = rootPath.localpath.size();

    // Exclude any trailing separator.
    if (rootPath.endsInSeparator())
        --mRootPathLength;

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
        {
            scanFlags = Notification::FOLDER_NEEDS_SCAN_RECURSIVE;
            assert(flag & kFSEventStreamEventFlagItemIsDir);
        }

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

