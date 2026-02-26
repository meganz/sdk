/**
 * @file mega/osx/megafs.h
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
#ifndef MEGA_MAC_FS_H
#define MEGA_MAC_FS_H

#include <CoreServices/CoreServices.h>

#define FSACCESS_CLASS MacFileSystemAccess
#include "mega/posix/megafs.h"

namespace mega {

class MEGA_API MacFileSystemAccess
  : public PosixFileSystemAccess
{
public:
    friend class MacDirNotify;

    MacFileSystemAccess();

    ~MacFileSystemAccess();

    void addevents(Waiter* waiter, int flags) override;
    
    int checkevents(Waiter* waiter) override;

#ifdef ENABLE_SYNC
    bool initFilesystemNotificationSystem() override;

    DirNotify* newdirnotify(LocalNode& root,
                            const LocalPath& rootPath,
                            Waiter* waiter) override;
#endif // ENABLE_SYNC

private:
    // This function ensures that all tasks in the dispatch
    // queue are completed.
    void flushDispatchQueue();

    // What queue executes our notification callbacks?
    dispatch_queue_t mDispatchQueue;
    
    // Tracks how many notifiers are active.
    std::atomic_size_t mNumNotifiers;
}; // MacFileSystemAccess

#ifdef ENABLE_SYNC

class MEGA_API MacDirNotify
  : public DirNotify
{
public:
    MacDirNotify(MacFileSystemAccess& owner,
                 LocalNode& root,
                 const LocalPath& rootPath,
                 Waiter& waiter);

    ~MacDirNotify();

private:
    // Invoked by the trampoline.
    void callback(const FSEventStreamEventFlags* flags,
                  std::size_t numEvents,
                  const char** paths);

    // Invoked by the run loop when it receives a filesystem event.
    static void trampoline(ConstFSEventStreamRef stream,
                           void *context,
                           std::size_t numPaths,
                           void* paths,
                           const FSEventStreamEventFlags* flags,
                           const FSEventStreamEventId* ids);

    // Monitors for and dispatches filesystem events.
    FSEventStreamRef mEventStream;

    // The MFSA that we are associated with.
    MacFileSystemAccess& mOwner;

    // The root that events are relative to.
    LocalNode& mRoot;

    // How much of an event's path should we skip?
    std::size_t mRootPathLength;

    // How we tell the engine it has work to do.
    Waiter& mWaiter;
}; // MacDirNotify

#endif // ENABLE_SYNC

} // mega

#endif // ! MEGA_MAC_FS_H

