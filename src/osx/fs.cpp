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

DirNotify* MacFileSystemAccess::newdirnotify(const LocalPath& rootPath,
                                             const LocalPath& ignoreName,
                                             Waiter* waiter,
                                             LocalNode* root)
{
    // Make sure we've been passed a sane root.
    assert(root);

    // Make sure we've been passed a sane waiter.
    assert(waiter);

    return new MacDirNotify(ignoreName, *this, *root, rootPath, *waiter);
}

MacDirNotify::MacDirNotify(const LocalPath& ignoreName,
                           MacFileSystemAccess& owner,
                           LocalNode& root,
                           const LocalPath& rootPath,
                           Waiter& waiter)
  : DirNotify(rootPath, ignoreName, root.sync)
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

        // Are we dealing with a symlink?
        if ((flag & kFSEventStreamEventFlagItemIsSymlink))
        {
            LOG_debug << "Link skipped: "
                      << *paths;

            continue;
        }

        auto path = *paths++ + mRootPathLength;

        // Skip leading seperator.
        if (*path == '/')
            ++path;

        // Translate path into something useful.
        auto localPath = LocalPath::fromPlatformEncodedRelative(path);

        // Is this notification coming from the debris directory?
        if (!localPath.empty() && ignore.isContainingPathOf(localPath))
            continue;

        // Has the root path been invalidated?
        if ((flag & kFSEventStreamEventFlagRootChanged))
            setFailed(EINVAL, "The root path has been invalidated.");

        // Has a device been unmounted below the root?
        if ((flag & kFSEventStreamEventFlagUnmount))
            setFailed(EINVAL, "A device has been unmounted below the root path.");

        // Have some events been coalesced?
        auto coalesced = (flag & kFSEventStreamEventFlagMustScanSubDirs) > 0;

        LOG_debug << "Filesystem notification. Root:"
                  << mRoot.name
                  << "   Path: "
                  << path
                  << "   Coalesced: "
                  << coalesced;

        // Let the engine know this path needs to be checked.
        notify(DIREVENTS, &mRoot, std::move(localPath), false, coalesced);
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

