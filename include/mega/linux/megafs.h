/**
 * @file mega/linux/megafs.h
 * @brief POSIX filesystem/directory access/notification
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
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
#ifndef MEGA_LINUX_FS_H
#define MEGA_LINUX_FS_H

#define FSACCESS_CLASS LinuxFileSystemAccess
#include "mega/posix/megafs.h"

namespace mega {

class LinuxFileSystemAccess
  : public PosixFileSystemAccess
{
public:
    friend class LinuxDirNotify;

    LinuxFileSystemAccess();

    ~LinuxFileSystemAccess();

    void addevents(Waiter* waiter, int flags) override;

    int checkevents(Waiter* waiter) override;

#ifdef ENABLE_SYNC

    bool initFilesystemNotificationSystem() override;

    DirNotify* newdirnotify(LocalNode& root,
                            const LocalPath& rootPath,
                            Waiter* waiter) override;

private:
    // Tracks which notifiers were created by this instance.
    list<DirNotify*> mNotifiers;

    // Inotify descriptor.
    int mNotifyFd;

    // Tracks which nodes are associated with what inotify handle.
    WatchMap mWatches;

#endif // ENABLE_SYNC
}; // LinuxFileSystemAccess

#ifdef ENABLE_SYNC

// Convenience.
using AddWatchResult = pair<WatchMapIterator, WatchResult>;

class LinuxDirNotify
  : public DirNotify
{
public:
    LinuxDirNotify(LinuxFileSystemAccess& owner,
                   LocalNode& root,
                   const LocalPath& rootPath);

    ~LinuxDirNotify();

    AddWatchResult addWatch(LocalNode& node,
                            const LocalPath& path,
                            handle fsid);

    void removeWatch(WatchMapIterator entry);

private:
    // The LFSA that we are associated with.
    LinuxFileSystemAccess& mOwner;

    // Our position in our owner's mNotifiers list.
    list<DirNotify*>::iterator mNotifiersIt;
}; // LinuxDirNotify

#endif // ENABLE_SYNC

} // mega

#endif // ! MEGA_LINUX_FS_H

