#include "mega.h"

namespace mega {

LinuxFileSystemAccess::LinuxFileSystemAccess()
  : PosixFileSystemAccess()
#ifdef ENABLE_SYNC
  , mNotifiers()
  , mNotifyFd(-EINVAL)
  , mWatches()
#endif // ENABLE_SYNC
{
}

LinuxFileSystemAccess::~LinuxFileSystemAccess()
{
#ifdef ENABLE_SYNC

    // Make sure there are no active notifiers.
    assert(mNotifiers.empty());

    // Release inotify descriptor, if any.
    if (mNotifyFd >= 0)
        close(mNotifyFd);

#endif // ENABLE_SYNC
}

void LinuxFileSystemAccess::addevents(Waiter* waiter, int flags)
{
#ifdef ENABLE_SYNC

    if (mNotifyFd < 0)
        return;

    auto w = static_cast<PosixWaiter*>(waiter);

    MEGA_FD_SET(mNotifyFd, &w->rfds);
    MEGA_FD_SET(mNotifyFd, &w->ignorefds);

    w->bumpmaxfd(mNotifyFd);

#endif // ENABLE_SYNC
}

int LinuxFileSystemAccess::checkevents(Waiter* waiter)
{
    int result = 0;

#ifdef ENABLE_SYNC

    if (mNotifyFd < 0)
        return result;

    // Called so that related syncs perform a rescan.
    auto notifyTransientFailure = [&]() {
        for (auto* notifier : mNotifiers)
            ++notifier->mErrorCount;
    };

    auto* w = static_cast<PosixWaiter*>(waiter);

    if (!MEGA_FD_ISSET(mNotifyFd, &w->rfds))
        return result;

    char buf[sizeof(struct inotify_event) + NAME_MAX + 1];
    ssize_t p, l;
    inotify_event* in;
    WatchMapIterator it;
    string localpath;

    auto notifyAll = [&](int handle, const string& name)
    {
        // Loop over and notify all associated nodes.
        auto associated = mWatches.equal_range(handle);

        for (auto i = associated.first; i != associated.second;)
        {
            // Convenience.
            using std::move;
            auto& node = *i->second.first;
            auto& sync = *node.sync;
            auto& notifier = *sync.dirnotify;

            LOG_debug << "Filesystem notification:"
                      << " Root: "
                      << node.localname.toPath()
                      << " Path: "
                      << name;

            if ((in->mask & IN_DELETE_SELF))
            {
                // The FS directory watched is gone
                node.mWatchHandle.invalidate();
                // Remove it from the container (C++11 and up)
                i = mWatches.erase(i);
            }
            else
            {
                ++i;
            }

            auto localName = LocalPath::fromPlatformEncodedRelative(name);
            notifier.notify(notifier.fsEventq, &node, Notification::NEEDS_SCAN_UNKNOWN, move(localName));
            result |= Waiter::NEEDEXEC;
        }
    };

    while ((l = read(mNotifyFd, buf, sizeof buf)) > 0)
    {
        for (p = 0; p < l; p += offsetof(inotify_event, name) + in->len)
        {
            in = (inotify_event*)(buf + p);

            if ((in->mask & (IN_Q_OVERFLOW | IN_UNMOUNT)))
            {
                LOG_err << "inotify "
                        << (in->mask & IN_Q_OVERFLOW ? "IN_Q_OVERFLOW" : "IN_UNMOUNT");

                notifyTransientFailure();
            }

// this flag was introduced in glibc 2.13 and Linux 2.6.36 (released October 20, 2010)
#ifndef IN_EXCL_UNLINK
#define IN_EXCL_UNLINK 0x04000000
#endif
            if ((in->mask & (IN_CREATE | IN_DELETE_SELF | IN_DELETE | IN_MOVED_FROM
                          | IN_MOVED_TO | IN_CLOSE_WRITE | IN_EXCL_UNLINK)))
            {
                LOG_verbose << "Filesystem notification:"
                            << "event: " << std::hex << in->mask;
                it = mWatches.find(in->wd);

                if (it != mWatches.end())
                {
                    // What nodes are associated with this handle?
                    notifyAll(it->first, in->len? in->name : "");
                }
            }
        }
    }

#endif // ENABLE_SYNC

    return result;
}

#ifdef ENABLE_SYNC

bool LinuxFileSystemAccess::initFilesystemNotificationSystem(int)
{
    mNotifyFd = inotify_init1(IN_NONBLOCK);

    if (mNotifyFd < 0)
        return mNotifyFd = -errno, false;

    return true;
}

DirNotify* LinuxFileSystemAccess::newdirnotify(LocalNode& root,
                                               const LocalPath& rootPath,
                                               Waiter*)
{
    return new LinuxDirNotify(*this, root, rootPath);
}

LinuxDirNotify::LinuxDirNotify(LinuxFileSystemAccess& owner,
                               LocalNode& root,
                               const LocalPath& rootPath)
  : DirNotify(rootPath)
  , mOwner(owner)
  , mNotifiersIt(owner.mNotifiers.insert(owner.mNotifiers.end(), this))
{
    // Assume our owner couldn't initialize.
    setFailed(-owner.mNotifyFd, "Unable to create filesystem monitor.");

    // Did our owner initialize correctly?
    if (owner.mNotifyFd >= 0)
        setFailed(0, "");
}

LinuxDirNotify::~LinuxDirNotify()
{
    // Remove ourselves from our owner's list of notiifers.
    mOwner.mNotifiers.erase(mNotifiersIt);
}

AddWatchResult LinuxDirNotify::addWatch(LocalNode& node,
                                        const LocalPath& path,
                                        handle fsid)
{
    using std::forward_as_tuple;
    using std::piecewise_construct;

    assert(node.type == FOLDERNODE);

    // Convenience.
    auto& watches = mOwner.mWatches;

    auto handle =
      inotify_add_watch(mOwner.mNotifyFd,
                        path.localpath.c_str(),
                        IN_CLOSE_WRITE
                        | IN_CREATE
                        | IN_DELETE
                        | IN_DELETE_SELF
                        | IN_EXCL_UNLINK
                        | IN_MOVED_FROM // event->cookie set as IN_MOVED_TO
                        | IN_MOVED_TO
                        | IN_ONLYDIR);

    if (handle >= 0)
    {
        auto entry =
          watches.emplace(piecewise_construct,
                          forward_as_tuple(handle),
                          forward_as_tuple(&node, fsid));

        return make_pair(entry, WR_SUCCESS);
    }

    LOG_warn << "Unable to monitor path for filesystem notifications: "
             << path.localpath.c_str()
             << ": Descriptor: "
             << mOwner.mNotifyFd
             << ": Error: "
             << errno;

    if (errno == ENOMEM || errno == ENOSPC)
        return make_pair(watches.end(), WR_FATAL);

    return make_pair(watches.end(), WR_FAILURE);
}

void LinuxDirNotify::removeWatch(WatchMapIterator entry)
{
    LOG_verbose << "[" << std::this_thread::get_id() << "]"
                <<  " removeWatch for handle: " << entry->first;
    auto& watches = mOwner.mWatches;

    auto handle = entry->first;
    assert(handle >= 0);

    watches.erase(entry); // Removes first instance

    if (watches.find(handle) != watches.end())
    {
        LOG_warn << "[" << std::this_thread::get_id() << "]"
                 << " There are more watches under handle: " << handle;

        auto it = watches.find(handle);

        while (it!=watches.end() && it->first == handle)
        {
            LOG_warn << "[" << std::this_thread::get_id() << "]"
                     << " handle: " << handle << " fsid:" << it->second.second;

            ++it;
        }

        return;
    }

    auto const removedResult = inotify_rm_watch(mOwner.mNotifyFd, handle);

    if (removedResult)
    {
        LOG_verbose << "[" << std::this_thread::get_id() << "]"
                    <<  "inotify_rm_watch for handle: " << handle
                    <<  " error no: " << errno;
    }
}

#endif // ENABLE_SYNC

} // mega

