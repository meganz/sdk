#include <future>

#include "mega.h"

namespace mega {

MacFileSystemAccess::MacFileSystemAccess()
  : PosixFileSystemAccess()
  , mNumNotifiers(0u)
  , mRunLoop(nullptr)
  , mWorker()
{
}

MacFileSystemAccess::~MacFileSystemAccess()
{
    // Make sure there are no notifiers active.
    assert(mNumNotifiers == 0);

    // Bail if we don't have a run loop.
    if (!mRunLoop)
        return;

    // Tell the worker's run loop to terminate.
    CFRunLoopStop(mRunLoop);

    // Wait for the worker to terminate.
    mWorker.join();
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
    // So we can wait until the worker is initialized.
    std::promise<CFRunLoopRef> ready;

    // Initialize the worker thread.
    mWorker = std::thread([&ready]{
        // Create a dummy event source.
        auto dummy = ([]{
            // Context describing our custom event source.
            CFRunLoopSourceContext context{};

            // Only required callback is a no-op.
            context.perform = [](void*){};

            // Create the event source.
            return CFRunLoopSourceCreate(nullptr, 0, &context);
        })();

        // Get our hands on this thread's run loop.
        auto loop = CFRunLoopGetCurrent();

        // Add our dummy event source to the run loop.
        CFRunLoopAddSource(loop, dummy, kCFRunLoopDefaultMode);

        // Let our owner know we're initialized.
        ready.set_value(loop);

        // Pass control to the run loop.
        //
        // It will relinquish control if:
        // - There are no event sources for it to monitor.
        // - It has been explicitly stopped.
        CFRunLoopRun();

        // Remove the dummy event source from the loop.
        CFRunLoopSourceInvalidate(dummy);

        // Release the dummy event source.
        CFRelease(dummy);
    });

    // Wait until the worker is initialized.
    mRunLoop = ready.get_future().get();

    return true;
}

DirNotify* MacFileSystemAccess::newdirnotify(LocalNode& root,
                                             const LocalPath& rootPath,
                                             Waiter* waiter)
{
    // Make sure we've been initialized.
    assert(mRunLoop);

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
        constexpr auto flags =
          kFSEventStreamCreateFlagFileEvents
          | kFSEventStreamCreateFlagWatchRoot;

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

    // Add the stream to the run loop's list of event sources.
    FSEventStreamScheduleWithRunLoop(
      mEventStream, mOwner.mRunLoop, kCFRunLoopDefaultMode);

    // Let the owner know we've added a new input source.
    CFRunLoopWakeUp(mOwner.mRunLoop);

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

    // Make sure the loop knows the stream's invalid.
    CFRunLoopWakeUp(mOwner.mRunLoop);

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

        //// Are we dealing with a symlink?
        //if ((flag & kFSEventStreamEventFlagItemIsSymlink))
        //{
        //    LOG_debug << "Link skipped: "
        //              << *paths;

        //    continue;
        //}

        auto path = *paths++ + mRootPathLength;

        // Skip leading seperator.
        if (*path == '/')
            ++path;

        //// Translate path into something useful.
        //auto localPath = LocalPath::fromPlatformEncodedRelative(path);

        //// Is this notification coming from the debris directory?
        //if (!localPath.empty() && ignore.isContainingPathOf(localPath))
        //    continue;

        // Has the root path been invalidated?
        if ((flag & kFSEventStreamEventFlagRootChanged))
            setFailed(EINVAL, "The root path has been invalidated.");

        // Has a device been unmounted below the root?
        if ((flag & kFSEventStreamEventFlagUnmount))
            setFailed(EINVAL, "A device has been unmounted below the root path.");

        auto scanFlags = Notification::NEEDS_SCAN_UNKNOWN;

        // Have some events been coalesced?
        if ((flag & kFSEventStreamEventFlagMustScanSubDirs))
            scanFlags = Notification::NEEDS_SCAN_RECURSIVE;

        // Pass the notification to the engine.
        notify(fsEventq,
               &mRoot,
               scanFlags,
               LocalPath::fromPlatformEncodedRelative(path));

        // No need for the below if we're performing a recursive scan.
        if (scanFlags == Notification::NEEDS_SCAN_RECURSIVE)
            continue;

        // Are we dealing with a directory?
        if (!(flag & kFSEventStreamEventFlagItemIsDir))
            continue;

        // Has its permissions changed?
        if (!(flag & kFSEventStreamEventFlagItemChangeOwner))
            continue;

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

