/**
 * @file posix/fs.cpp
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
 * MacOS X fsevents code based on osxbook.com/software/fslogger
 * (requires euid == root or passing an existing /dev/fsevents fd)
 * (c) Amit Singh
 *
 * You should have received a copy of the license along with this
 * program.
 */

#include "mega.h"
#include <sys/utsname.h>
#include <sys/ioctl.h>
#ifdef TARGET_OS_MAC
#include "mega/osx/osxutils.h"
#endif

#ifdef __ANDROID__
#include <jni.h>
extern JavaVM *MEGAjvm;
#endif

#if defined(__MACH__) && !(TARGET_OS_IPHONE)
#include <uuid/uuid.h>
#endif

namespace mega {
using namespace std;

#ifdef USE_IOS
    char* PosixFileSystemAccess::appbasepath = NULL;
#endif

#ifdef HAVE_AIO_RT
PosixAsyncIOContext::PosixAsyncIOContext() : AsyncIOContext()
{
    aiocb = NULL;
}

PosixAsyncIOContext::~PosixAsyncIOContext()
{
    LOG_verbose << "Deleting PosixAsyncIOContext";
    finish();
}

void PosixAsyncIOContext::finish()
{
    if (aiocb)
    {
        if (!finished)
        {
            LOG_debug << "Synchronously waiting for async operation";
            AsyncIOContext::finish();
        }
        delete aiocb;
        aiocb = NULL;
    }
    assert(finished);
}
#endif

PosixFileAccess::PosixFileAccess(Waiter *w, int defaultfilepermissions) : FileAccess(w)
{
    fd = -1;
    this->defaultfilepermissions = defaultfilepermissions;

#ifndef HAVE_FDOPENDIR
    dp = NULL;
#endif

    fsidvalid = false;
}

PosixFileAccess::~PosixFileAccess()
{
#ifndef HAVE_FDOPENDIR
    if (dp)
    {
        closedir(dp);
    }
#endif

    if (fd >= 0)
    {
        close(fd);
    }
}

bool PosixFileAccess::sysstat(m_time_t* mtime, m_off_t* size)
{
    struct stat statbuf;
    retry = false;

#ifdef USE_IOS
    string localname = this->localname;
    if (PosixFileSystemAccess::appbasepath)
    {
        if (localname.size() && localname.at(0) != '/')
        {
            localname.insert(0, PosixFileSystemAccess::appbasepath);
        }
    }
#endif

    type = TYPE_UNKNOWN;
    if (!stat(localname.c_str(), &statbuf))
    {
        errorcode = 0;
        if (S_ISDIR(statbuf.st_mode))
        {
            type = FOLDERNODE;
            return false;
        }

        type = FILENODE;
        *size = statbuf.st_size;
        *mtime = statbuf.st_mtime;

        FileSystemAccess::captimestamp(mtime);

        return true;
    }

    errorcode = errno;
    return false;
}

bool PosixFileAccess::sysopen(bool)
{
#ifdef USE_IOS
    string localname = this->localname;
    if (PosixFileSystemAccess::appbasepath)
    {
        if (localname.size() && localname.at(0) != '/')
        {
            localname.insert(0, PosixFileSystemAccess::appbasepath);
        }
    }
#endif

    return (fd = open(localname.c_str(), O_RDONLY)) >= 0;
}

void PosixFileAccess::sysclose()
{
    if (localname.size())
    {
        assert (fd >= 0);
        if (fd >= 0)
        {
            close(fd);
            fd = -1;
        }
    }
}

bool PosixFileAccess::asyncavailable()
{
#ifdef HAVE_AIO_RT
    #ifdef __APPLE__
        return false;
    #endif

    return true;
#else
    return false;
#endif
}

#ifdef HAVE_AIO_RT
AsyncIOContext *PosixFileAccess::newasynccontext()
{
    return new PosixAsyncIOContext();
}

void PosixFileAccess::asyncopfinished(sigval sigev_value)
{
    PosixAsyncIOContext *context = (PosixAsyncIOContext *)(sigev_value.sival_ptr);
    struct aiocb *aiocbp = context->aiocb;
    int e = aio_error(aiocbp);
    assert (e != EINPROGRESS);
    context->retry = (e == EAGAIN);
    context->failed = (aio_return(aiocbp) < 0);
    if (!context->failed)
    {
        if (context->op == AsyncIOContext::READ && context->pad)
        {
            memset((void *)(((char *)(aiocbp->aio_buf)) + aiocbp->aio_nbytes), 0, context->pad);
            LOG_verbose << "Async read finished OK";
        }
        else
        {
            LOG_verbose << "Async write finished OK";
        }
    }
    else
    {
        LOG_warn << "Async operation finished with error: " << e;
    }

    asyncfscallback userCallback = context->userCallback;
    void *userData = context->userData;
    context->finished = true;
    if (userCallback)
    {
        userCallback(userData);
    }
}
#endif

void PosixFileAccess::asyncsysopen(AsyncIOContext *context)
{
#ifdef HAVE_AIO_RT
    string path;
    path.assign((char *)context->buffer, context->len);
    context->failed = !fopen(&path, context->access & AsyncIOContext::ACCESS_READ,
                             context->access & AsyncIOContext::ACCESS_WRITE);
    context->retry = retry;
    context->finished = true;
    if (context->userCallback)
    {
        context->userCallback(context->userData);
    }
#endif
}

void PosixFileAccess::asyncsysread(AsyncIOContext *context)
{
#ifdef HAVE_AIO_RT
    if (!context)
    {
        return;
    }

    PosixAsyncIOContext *posixContext = dynamic_cast<PosixAsyncIOContext*>(context);
    if (!posixContext)
    {
        context->failed = true;
        context->retry = false;
        context->finished = true;
        if (context->userCallback)
        {
            context->userCallback(context->userData);
        }
        return;
    }

    struct aiocb *aiocbp = new struct aiocb;
    memset(aiocbp, 0, sizeof (struct aiocb));

    aiocbp->aio_fildes = fd;
    aiocbp->aio_buf = (void *)posixContext->buffer;
    aiocbp->aio_nbytes = posixContext->len;
    aiocbp->aio_offset = posixContext->pos;
    aiocbp->aio_sigevent.sigev_notify = SIGEV_THREAD;
    aiocbp->aio_sigevent.sigev_notify_function = asyncopfinished;
    aiocbp->aio_sigevent.sigev_value.sival_ptr = (void *)posixContext;
    posixContext->aiocb = aiocbp;
    if (aio_read(aiocbp))
    {
        posixContext->retry = (errno == EAGAIN);
        posixContext->failed = true;
        posixContext->finished = true;
        posixContext->aiocb = NULL;
        delete aiocbp;

        LOG_warn << "Async read failed at startup:" << errno;
        if (posixContext->userCallback)
        {
            posixContext->userCallback(posixContext->userData);
        }
    }
#endif
}

void PosixFileAccess::asyncsyswrite(AsyncIOContext *context)
{
#ifdef HAVE_AIO_RT
    if (!context)
    {
        return;
    }

    PosixAsyncIOContext *posixContext = dynamic_cast<PosixAsyncIOContext*>(context);
    if (!posixContext)
    {
        context->failed = true;
        context->retry = false;
        context->finished = true;
        if (context->userCallback)
        {
            context->userCallback(context->userData);
        }
        return;
    }

    struct aiocb *aiocbp = new struct aiocb;
    memset(aiocbp, 0, sizeof (struct aiocb));

    aiocbp->aio_fildes = fd;
    aiocbp->aio_buf = (void *)posixContext->buffer;
    aiocbp->aio_nbytes = posixContext->len;
    aiocbp->aio_offset = posixContext->pos;
    aiocbp->aio_sigevent.sigev_notify = SIGEV_THREAD;
    aiocbp->aio_sigevent.sigev_notify_function = asyncopfinished;
    aiocbp->aio_sigevent.sigev_value.sival_ptr = (void *)posixContext;
    posixContext->aiocb = aiocbp;

    if (aio_write(aiocbp))
    {
        posixContext->retry = (errno == EAGAIN);
        posixContext->failed = true;
        posixContext->finished = true;
        posixContext->aiocb = NULL;
        delete aiocbp;

        LOG_warn << "Async write failed at startup: " << errno;
        if (posixContext->userCallback)
        {
            posixContext->userCallback(posixContext->userData);
        }
    }
#endif
}

// update local name
void PosixFileAccess::updatelocalname(string* name)
{
    if (localname.size())
    {
        localname = *name;
    }
}

bool PosixFileAccess::sysread(byte* dst, unsigned len, m_off_t pos)
{
    retry = false;
#ifndef __ANDROID__
    return pread(fd, (char*)dst, len, pos) == len;
#else
    lseek64(fd, pos, SEEK_SET);
    return read(fd, (char*)dst, len) == len;
#endif
}

bool PosixFileAccess::fwrite(const byte* data, unsigned len, m_off_t pos)
{
    retry = false;
#ifndef __ANDROID__
    return pwrite(fd, data, len, pos) == len;
#else
    lseek64(fd, pos, SEEK_SET);
    return write(fd, data, len) == len;
#endif
}

bool PosixFileAccess::fopen(string* f, bool read, bool write)
{
#ifdef USE_IOS
    string absolutef;
    if (PosixFileSystemAccess::appbasepath)
    {
        if (f->size() && f->at(0) != '/')
        {
            absolutef = PosixFileSystemAccess::appbasepath;
            absolutef.append(*f);
            f = &absolutef;
        }
    }
#endif

    struct stat statbuf;

    retry = false;

#ifdef __MACH__
    if (!write)
    {
        char resolved_path[PATH_MAX];
        struct stat statbuf;
        if (memcmp(f->c_str(), ".", 2) && memcmp(f->c_str(), "..", 3)
                && !lstat(f->c_str(), &statbuf)
                && !S_ISLNK(statbuf.st_mode)
                && realpath(f->c_str(), resolved_path) == resolved_path)
        {
            const char *fname;
            size_t fnamesize;
            if ((fname = strrchr(f->c_str(), '/')))
            {
                fname++;
                fnamesize = f->size() - (fname - f->c_str());
            }
            else
            {
                fname =  f->c_str();
                fnamesize = f->size();
            }
            fnamesize++;

            const char *rname;
            size_t rnamesize;
            if ((rname = strrchr(resolved_path, '/')))
            {
                rname++;
            }
            else
            {
                rname = resolved_path;
            }
            rnamesize = strlen(rname) + 1;

            if (rnamesize == fnamesize && memcmp(fname, rname, fnamesize))
            {
                LOG_warn << "fopen failed due to invalid case: " << f->c_str();
                return false;
            }
        }
    }
#endif

#ifndef HAVE_FDOPENDIR
    if (!write)
    {
        // workaround for the very unfortunate platforms that do not implement fdopendir() (MacOS...)
        // (FIXME: can this be done without intruducing a race condition?)
        if ((dp = opendir(f->c_str())))
        {
            // stat & check if the directory is still a directory...
            if (stat(f->c_str(), &statbuf) || !S_ISDIR(statbuf.st_mode)) return false;

            size = 0;
            mtime = statbuf.st_mtime;
            type = FOLDERNODE;
            fsid = (handle)statbuf.st_ino;
            fsidvalid = true;

            FileSystemAccess::captimestamp(&mtime);

            return true;
        }

        if (errno != ENOTDIR) return false;
    }
#endif

    mode_t mode = 0;
    if (write)
    {
        mode = umask(0);
    }

    if ((fd = open(f->c_str(), write ? (read ? O_RDWR : O_WRONLY | O_CREAT) : O_RDONLY, defaultfilepermissions)) >= 0)
    {
        if (write)
        {
            umask(mode);
        }

        if (!fstat(fd, &statbuf))
        {
            #ifdef __MACH__
                //If creation time equal to kMagicBusyCreationDate
                if(statbuf.st_birthtimespec.tv_sec == -2082844800)
                {
                    LOG_debug << "File is busy: " << f->c_str();
                    retry = true;
                    return false;
                }
            #endif

            size = statbuf.st_size;
            mtime = statbuf.st_mtime;
            type = S_ISDIR(statbuf.st_mode) ? FOLDERNODE : FILENODE;
            fsid = (handle)statbuf.st_ino;
            fsidvalid = true;

            FileSystemAccess::captimestamp(&mtime);

            return true;
        }

        close(fd);
    }
    else if (write)
    {
        umask(mode);
    }

    return false;
}

PosixFileSystemAccess::PosixFileSystemAccess(int fseventsfd)
{
    assert(sizeof(off_t) == 8);

    notifyerr = false;
    notifyfailed = true;
    notifyfd = -1;

    defaultfilepermissions = 0600;
    defaultfolderpermissions = 0700;

    localseparator = "/";

#ifdef USE_IOS
    if (!appbasepath)
    {
        string basepath;
        ios_appbasepath(&basepath);
        if (basepath.size())
        {
            basepath.append("/");
            appbasepath = strdup(basepath.c_str());
        }
    }
#endif

#ifdef USE_INOTIFY
    lastcookie = 0;
    lastlocalnode = NULL;
    if ((notifyfd = inotify_init1(IN_NONBLOCK)) >= 0)
    {
        notifyfailed = false;
    }
#endif

#ifdef __MACH__
#if __LP64__
    typedef struct fsevent_clone_args {
       int8_t *event_list;
       int32_t num_events;
       int32_t event_queue_depth;
       int32_t *fd;
    } fsevent_clone_args;
#else
    typedef struct fsevent_clone_args {
       int8_t *event_list;
       int32_t pad1;
       int32_t num_events;
       int32_t event_queue_depth;
       int32_t *fd;
       int32_t pad2;
    } fsevent_clone_args;
#endif

#define FSE_IGNORE 0
#define FSE_REPORT 1
#define FSEVENTS_CLONE _IOW('s', 1, fsevent_clone_args)
#define FSEVENTS_WANT_EXTENDED_INFO _IO('s', 102)

    int fd;
    fsevent_clone_args fca;
    int8_t event_list[] = { // action to take for each event
                              FSE_REPORT,  // FSE_CREATE_FILE,
                              FSE_REPORT,  // FSE_DELETE,
                              FSE_REPORT,  // FSE_STAT_CHANGED,
                              FSE_REPORT,  // FSE_RENAME,
                              FSE_REPORT,  // FSE_CONTENT_MODIFIED,
                              FSE_REPORT,  // FSE_EXCHANGE,
                              FSE_IGNORE,  // FSE_FINDER_INFO_CHANGED,
                              FSE_REPORT,  // FSE_CREATE_DIR,
                              FSE_REPORT,  // FSE_CHOWN,
                              FSE_IGNORE,  // FSE_XATTR_MODIFIED,
                              FSE_IGNORE,  // FSE_XATTR_REMOVED,
                          };

    // for this to succeed, geteuid() must be 0, or an existing /dev/fsevents fd must have
    // been passed to the constructor
    if ((fd = fseventsfd) >= 0 || (fd = open("/dev/fsevents", O_RDONLY)) >= 0)
    {
        fca.event_list = (int8_t*)event_list;
        fca.num_events = sizeof event_list/sizeof(int8_t);
        fca.event_queue_depth = 4096;
        fca.fd = &notifyfd;

        if (ioctl(fd, FSEVENTS_CLONE, (char*)&fca) >= 0)
        {
            close(fd);

            if (ioctl(notifyfd, FSEVENTS_WANT_EXTENDED_INFO, NULL) >= 0)
            {
                notifyfailed = false;
            }
            else
            {
                close(notifyfd);
            }
        }
        else
        {
            close(fd);
        }
    }
#else
    (void)fseventsfd;  // suppress warning
#endif
}

PosixFileSystemAccess::~PosixFileSystemAccess()
{
    if (notifyfd >= 0)
    {
        close(notifyfd);
    }
}

// wake up from filesystem updates
void PosixFileSystemAccess::addevents(Waiter* w, int /*flags*/)
{
    if (notifyfd >= 0)
    {
        PosixWaiter* pw = (PosixWaiter*)w;

        FD_SET(notifyfd, &pw->rfds);
        FD_SET(notifyfd, &pw->ignorefds);

        pw->bumpmaxfd(notifyfd);
    }
}

// read all pending inotify events and queue them for processing
int PosixFileSystemAccess::checkevents(Waiter* w)
{
    int r = 0;
    if (notifyfd < 0)
    {
        return r;
    }
#ifdef ENABLE_SYNC
#ifdef USE_INOTIFY
    PosixWaiter* pw = (PosixWaiter*)w;
    string *ignore;

    if (FD_ISSET(notifyfd, &pw->rfds))
    {
        char buf[sizeof(struct inotify_event) + NAME_MAX + 1];
        int p, l;
        inotify_event* in;
        wdlocalnode_map::iterator it;
        string localpath;

        while ((l = read(notifyfd, buf, sizeof buf)) > 0)
        {
            for (p = 0; p < l; p += offsetof(inotify_event, name) + in->len)
            {
                in = (inotify_event*)(buf + p);

                if (in->mask & (IN_Q_OVERFLOW | IN_UNMOUNT))
                {
                    notifyerr = true;
                }

// this flag was introduced in glibc 2.13 and Linux 2.6.36 (released October 20, 2010)
#ifndef IN_EXCL_UNLINK
#define IN_EXCL_UNLINK 0x04000000
#endif
                if (in->mask & (IN_CREATE | IN_DELETE | IN_MOVED_FROM
                              | IN_MOVED_TO | IN_CLOSE_WRITE | IN_EXCL_UNLINK))
                {
                    //if ((in->mask & (IN_CREATE | IN_ISDIR)) != IN_CREATE) //certain operations (e.g: QFile::copy, Qt 5.11) might produce IN_CREATE with no further IN_CLOSE_WRITE
                    {
                        it = wdnodes.find(in->wd);

                        if (it != wdnodes.end())
                        {
                            if (lastcookie && lastcookie != in->cookie)
                            {
                                ignore = &lastlocalnode->sync->dirnotify->ignore;
                                if (lastname.size() < ignore->size()
                                 || memcmp(lastname.c_str(), ignore->data(), ignore->size())
                                 || (lastname.size() > ignore->size()
                                  && memcmp(lastname.c_str() + ignore->size(), localseparator.c_str(), localseparator.size())))
                                {                                    
                                    // previous IN_MOVED_FROM is not followed by the
                                    // corresponding IN_MOVED_TO, so was actually a deletion
                                    LOG_debug << "Filesystem notification (deletion). Root: " << lastlocalnode->name << "   Path: " << lastname;
                                    lastlocalnode->sync->dirnotify->notify(DirNotify::DIREVENTS,
                                                                           lastlocalnode,
                                                                           lastname.c_str(),
                                                                           lastname.size());

                                    r |= Waiter::NEEDEXEC;
                                }
                            }

                            if (in->mask & IN_MOVED_FROM)
                            {
                                // could be followed by the corresponding IN_MOVE_TO or not..
                                // retain in case it's not (in which case it's a deletion)
                                lastcookie = in->cookie;
                                lastlocalnode = it->second;
                                lastname = in->name;
                            }
                            else
                            {
                                lastcookie = 0;

                                ignore = &it->second->sync->dirnotify->ignore;
                                unsigned int insize = strlen(in->name);

                                if (insize < ignore->size()
                                 || memcmp(in->name, ignore->data(), ignore->size())
                                 || (insize > ignore->size()
                                  && memcmp(in->name + ignore->size(), localseparator.c_str(), localseparator.size())))
                                {
                                    LOG_debug << "Filesystem notification. Root: " << it->second->name << "   Path: " << in->name;
                                    it->second->sync->dirnotify->notify(DirNotify::DIREVENTS,
                                                                        it->second, in->name,
                                                                        insize);

                                    r |= Waiter::NEEDEXEC;
                                }
                            }
                        }
                    }
                }
            }
        }

        // this assumes that corresponding IN_MOVED_FROM / IN_MOVED_FROM pairs are never notified separately
        if (lastcookie)
        {
            ignore = &lastlocalnode->sync->dirnotify->ignore;

            if (lastname.size() < ignore->size()
             || memcmp(lastname.c_str(), ignore->data(), ignore->size())
             || (lastname.size() > ignore->size()
              && memcmp(lastname.c_str() + ignore->size(), localseparator.c_str(), localseparator.size())))
            {
                LOG_debug << "Filesystem notification. Root: " << lastlocalnode->name << "   Path: " << lastname;
                lastlocalnode->sync->dirnotify->notify(DirNotify::DIREVENTS,
                                                       lastlocalnode,
                                                       lastname.c_str(),
                                                       lastname.size());

                r |= Waiter::NEEDEXEC;
            }

            lastcookie = 0;
        }
    }
#endif

#ifdef __MACH__
#define FSE_MAX_ARGS 12
#define FSE_MAX_EVENTS 11
#define FSE_ARG_DONE 0xb33f
#define FSE_EVENTS_DROPPED 999
#define FSE_TYPE_MASK 0xfff
#define FSE_ARG_STRING 2
#define FSE_RENAME 3
#define FSE_GET_FLAGS(type) (((type) >> 12) & 15)

    struct kfs_event_arg {
        u_int16_t type;         // argument type
        u_int16_t len;          // size of argument data that follows this field
        union {
            struct vnode *vp;
            char *str;
            void *ptr;
            int32_t int32;
            dev_t dev;
            ino_t ino;
            int32_t mode;
            uid_t uid;
            gid_t gid;
            uint64_t timestamp;
        } data;
    };

    struct kfs_event {
        int32_t type; // event type
        pid_t pid;  // pid of the process that performed the operation
        kfs_event_arg args[FSE_MAX_ARGS]; // event arguments
    };

    // MacOS /dev/fsevents delivers all filesystem events as a unified stream,
    // which we filter
    int pos, avail;
    int off;
    int i, n, s;
    kfs_event* kfse;
    kfs_event_arg* kea;
    char buffer[131072];
    char* paths[2];
    char* path;
    Sync* pathsync[2];
    sync_list::iterator it;
    fd_set rfds;
    timeval tv = { 0 };
    struct stat statbuf;
    static char rsrc[] = "/..namedfork/rsrc";
    static unsigned int rsrcsize = sizeof(rsrc) - 1;

    for (;;)
    {
        FD_ZERO(&rfds);
        FD_SET(notifyfd, &rfds);

        // ensure nonblocking behaviour
        if (select(notifyfd + 1, &rfds, NULL, NULL, &tv) <= 0) break;

        if ((avail = read(notifyfd, buffer, sizeof buffer)) < 0)
        {
            notifyerr = true;
            break;
        }

        for (pos = 0; pos < avail; )
        {
            kfse = (kfs_event*)(buffer + pos);

            pos += sizeof(int32_t) + sizeof(pid_t);

            if (kfse->type == FSE_EVENTS_DROPPED)
            {
                // force a full rescan
                notifyerr = true;
                pos += sizeof(u_int16_t);
                continue;
            }

            n = 0;

            for (kea = kfse->args; pos < avail; kea = (kfs_event_arg*)((char*)kea + off))
            {
                // no more arguments
                if (kea->type == FSE_ARG_DONE)
                {
                    pos += sizeof(u_int16_t);
                    break;
                }

                off = sizeof(kea->type) + sizeof(kea->len) + kea->len;
                pos += off;

                if (kea->type == FSE_ARG_STRING && n < 2)
                {
                    paths[n++] = ((char*)&(kea->data.str))-4;
                }
            }

            // always skip paths that are outside synced fs trees or in a sync-local rubbish folder
            for (i = n; i--; )
            {
                path = paths[i];
                unsigned int psize = strlen(path);

                for (it = client->syncs.begin(); it != client->syncs.end(); it++)
                {
                    int rsize = (*it)->localroot.localname.size();
                    int isize = (*it)->dirnotify->ignore.size();

                    if (psize >= rsize
                      && !memcmp((*it)->localroot.localname.c_str(), path, rsize)    // prefix match
                      && (!path[rsize] || path[rsize] == '/')               // at end: end of path or path separator
                      && (psize <= (rsize + isize)                          // not ignored
                          || (path[rsize + isize + 1] && path[rsize + isize + 1] != '/')
                          || memcmp(path + rsize + 1, (*it)->dirnotify->ignore.c_str(), isize))
                      && (psize < rsrcsize                                  // it isn't a resource fork
                          || memcmp(path + psize - rsrcsize, rsrc, rsrcsize)))
                        {
                            if (!lstat(path, &statbuf) && S_ISLNK(statbuf.st_mode))
                            {
                                LOG_debug << "Link skipped:  " << path;
                                paths[i] = NULL;
                                break;
                            }

                            paths[i] += (*it)->localroot.localname.size() + 1;
                            pathsync[i] = *it;
                            break;
                        }
                }

                if (it == client->syncs.end())
                {
                    paths[i] = NULL;
                }
            }

            // for rename/move operations, skip source if both paths are synced
            // (to handle rapid a -> b, b -> c without overwriting b).
            if (n == 2 && paths[0] && paths[1] && (kfse->type & FSE_TYPE_MASK) == FSE_RENAME)
            {
                paths[0] = NULL;
            }

            for (i = n; i--; )
            {
                if (paths[i])
                {
                    LOG_debug << "Filesystem notification. Root: " << pathsync[i]->localroot.name << "   Path: " << paths[i];
                    pathsync[i]->dirnotify->notify(DirNotify::DIREVENTS,
                                                   &pathsync[i]->localroot,
                                                   paths[i],
                                                   strlen(paths[i]));

                    r |= Waiter::NEEDEXEC;
                }
            }
        }
    }
#endif
#endif
    return r;
}

// generate unique local filename in the same fs as relatedpath
void PosixFileSystemAccess::tmpnamelocal(string* localname) const
{
    static unsigned tmpindex;
    char buf[128];

    sprintf(buf, ".getxfer.%lu.%u.mega", (unsigned long)getpid(), tmpindex++);
    *localname = buf;
}

void PosixFileSystemAccess::path2local(string* path, string* local) const
{
#ifdef __MACH__
    path2localMac(path, local);
#else
    *local = *path;
#endif
}

void PosixFileSystemAccess::local2path(string* local, string* path) const
{
    *path = *local;
    normalize(path);
}

// no legacy DOS garbage here...
bool PosixFileSystemAccess::getsname(string*, string*) const
{
    return false;
}

bool PosixFileSystemAccess::renamelocal(string* oldname, string* newname, bool override)
{
#ifdef USE_IOS
    string absoluteoldname;
    string absolutenewname;
    if (appbasepath)
    {
        if (oldname->size() && oldname->at(0) != '/')
        {
            absoluteoldname = appbasepath;
            absoluteoldname.append(*oldname);
            oldname = &absoluteoldname;
        }

        if (newname->size() && newname->at(0) != '/')
        {
            absolutenewname = appbasepath;
            absolutenewname.append(*newname);
            newname = &absolutenewname;
        }
    }
#endif

    bool existingandcare = !override && (0 == access(newname->c_str(), F_OK));
    if (!existingandcare && !rename(oldname->c_str(), newname->c_str()))
    {
        LOG_verbose << "Successfully moved file: " << oldname->c_str() << " to " << newname->c_str();
        return true;
    }

    target_exists = existingandcare  || errno == EEXIST || errno == EISDIR || errno == ENOTEMPTY || errno == ENOTDIR;
    transient_error = !existingandcare && (errno == ETXTBSY || errno == EBUSY);

    int e = errno;
    if (!skip_errorreport)
    {
        LOG_warn << "Unable to move file: " << oldname->c_str() << " to " << newname->c_str() << ". Error code: " << e;
    }
    return false;
}

bool PosixFileSystemAccess::copylocal(string* oldname, string* newname, m_time_t mtime)
{
#ifdef USE_IOS
    string absoluteoldname;
    string absolutenewname;
    if (appbasepath)
    {
        if (oldname->size() && oldname->at(0) != '/')
        {
            absoluteoldname = appbasepath;
            absoluteoldname.append(*oldname);
            oldname = &absoluteoldname;
        }

        if (newname->size() && newname->at(0) != '/')
        {
            absolutenewname = appbasepath;
            absolutenewname.append(*newname);
            newname = &absolutenewname;
        }
    }
#endif

    int sfd, tfd;
    ssize_t t = -1;

#ifdef HAVE_SENDFILE
    // Linux-specific - kernel 2.6.33+ required
    if ((sfd = open(oldname->c_str(), O_RDONLY | O_DIRECT)) >= 0)
    {
        LOG_verbose << "Copying via sendfile";
        mode_t mode = umask(0);
        if ((tfd = open(newname->c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_DIRECT, defaultfilepermissions)) >= 0)
        {
            umask(mode);
            while ((t = sendfile(tfd, sfd, NULL, 1024 * 1024 * 1024)) > 0);
#else
    char buf[16384];

    if ((sfd = open(oldname->c_str(), O_RDONLY)) >= 0)
    {
        LOG_verbose << "Copying via read/write";
        mode_t mode = umask(0);
        if ((tfd = open(newname->c_str(), O_WRONLY | O_CREAT | O_TRUNC, defaultfilepermissions)) >= 0)
        {
            umask(mode);
            while (((t = read(sfd, buf, sizeof buf)) > 0) && write(tfd, buf, t) == t);
#endif
            close(tfd);
        }
        else
        {
            umask(mode);
            target_exists = errno == EEXIST;
            transient_error = errno == ETXTBSY || errno == EBUSY;

            int e = errno;
            LOG_warn << "Unable to copy file. Error code: " << e;
        }

        close(sfd);
    }

    if (!t)
    {
#ifdef ENABLE_SYNC
        t = !setmtimelocal(newname, mtime);
#else
        // fails in setmtimelocal are allowed in non sync clients.
        setmtimelocal(newname, mtime);
#endif
    }
    else
    {
        int e = errno;
        LOG_debug << "Unable to copy file: " << oldname->c_str() << " to " << newname->c_str() << ". Error code: " << e;
    }

    return !t;
}

// FIXME: add platform support for recycle bins
bool PosixFileSystemAccess::rubbishlocal(string* /*name*/)
{
    return false;
}

bool PosixFileSystemAccess::unlinklocal(string* name)
{
#ifdef USE_IOS
    string absolutename;
    if (appbasepath)
    {
        if (name->size() && name->at(0) != '/')
        {
            absolutename = appbasepath;
            absolutename.append(*name);
            name = &absolutename;
        }
    }
#endif

    if (!unlink(name->c_str())) return true;

    transient_error = errno == ETXTBSY || errno == EBUSY;

    return false;
}

// delete all files, folders and symlinks contained in the specified folder
// (does not recurse into mounted devices)
void PosixFileSystemAccess::emptydirlocal(string* name, dev_t basedev)
{
#ifdef USE_IOS
    string absolutename;
    if (appbasepath)
    {
        if (name->size() && name->at(0) != '/')
        {
            absolutename = appbasepath;
            absolutename.append(*name);
            name = &absolutename;
        }
    }
#endif

    DIR* dp;
    dirent* d;
    int removed;
    struct stat statbuf;
    size_t t;

    if (!basedev)
    {
        if (lstat(name->c_str(), &statbuf) || !S_ISDIR(statbuf.st_mode) || S_ISLNK(statbuf.st_mode)) return;
        basedev = statbuf.st_dev;
    }

    if ((dp = opendir(name->c_str())))
    {
        for (;;)
        {
            removed = 0;

            while ((d = readdir(dp)))
            {
                if (d->d_type != DT_DIR
                 || *d->d_name != '.'
                 || (d->d_name[1] && (d->d_name[1] != '.' || d->d_name[2])))
                {
                    t = name->size();
                    name->append("/");
                    name->append(d->d_name);

                    if (!lstat(name->c_str(), &statbuf))
                    {
                        if (!S_ISLNK(statbuf.st_mode) && S_ISDIR(statbuf.st_mode) && statbuf.st_dev == basedev)
                        {
                            emptydirlocal(name, basedev);
                            removed |= !rmdir(name->c_str());
                        }
                        else
                        {
                            removed |= !unlink(name->c_str());
                        }
                    }

                    name->resize(t);
                }
            }

            if (!removed)
            {
                break;
            }

            rewinddir(dp);
        }

        closedir(dp);
    }
}

int PosixFileSystemAccess::getdefaultfilepermissions()
{
    return defaultfilepermissions;
}

void PosixFileSystemAccess::setdefaultfilepermissions(int permissions)
{
    defaultfilepermissions = permissions | 0600;
}

int PosixFileSystemAccess::getdefaultfolderpermissions()
{
    return defaultfolderpermissions;
}

void PosixFileSystemAccess::setdefaultfolderpermissions(int permissions)
{
    defaultfolderpermissions = permissions | 0700;
}

bool PosixFileSystemAccess::rmdirlocal(string* name)
{
#ifdef USE_IOS
    string absolutename;
    if (appbasepath)
    {
        if (name->size() && name->at(0) != '/')
        {
            absolutename = appbasepath;
            absolutename.append(*name);
            name = &absolutename;
        }
    }
#endif

    emptydirlocal(name);

    if (!rmdir(name->c_str())) return true;

    transient_error = errno == ETXTBSY || errno == EBUSY;

    return false;
}

bool PosixFileSystemAccess::mkdirlocal(string* name, bool)
{
#ifdef USE_IOS
    string absolutename;
    if (appbasepath)
    {
        if (name->size() && name->at(0) != '/')
        {
            absolutename = appbasepath;
            absolutename.append(*name);
            name = &absolutename;
        }
    }
#endif

    mode_t mode = umask(0);
    bool r = !mkdir(name->c_str(), defaultfolderpermissions);
    umask(mode);

    if (!r)
    {
        target_exists = errno == EEXIST;
        if (target_exists)
        {
            LOG_debug << "Error creating local directory: " << name->c_str() << " errno: " << errno;
        }
        else
        {
            LOG_err << "Error creating local directory: " << name->c_str() << " errno: " << errno;
        }
        transient_error = errno == ETXTBSY || errno == EBUSY;
    }

    return r;
}

bool PosixFileSystemAccess::setmtimelocal(string* name, m_time_t mtime)
{
#ifdef USE_IOS
    string absolutename;
    if (appbasepath)
    {
        if (name->size() && name->at(0) != '/')
        {
            absolutename = appbasepath;
            absolutename.append(*name);
            name = &absolutename;
        }
    }
#endif

    struct utimbuf times = { (time_t)mtime, (time_t)mtime };

    bool success = !utime(name->c_str(), &times);
    if (!success)
    {
        LOG_err << "Error setting mtime: " << name <<" mtime: "<< mtime << " errno: " << errno;
        transient_error = errno == ETXTBSY || errno == EBUSY;
    }

    return success;
}

bool PosixFileSystemAccess::chdirlocal(string* name) const
{
#ifdef USE_IOS
    string absolutename;
    if (appbasepath)
    {
        if (name->size() && name->at(0) != '/')
        {
            absolutename = appbasepath;
            absolutename.append(*name);
            name = &absolutename;
        }
    }
#endif

    return !chdir(name->c_str());
}

size_t PosixFileSystemAccess::lastpartlocal(string* localname) const
{
    const char* ptr = localname->data();

    if ((ptr = strrchr(ptr, '/')))
    {
        return ptr - localname->data() + 1;
    }

    return 0;
}

// return lowercased ASCII file extension, including the . separator
bool PosixFileSystemAccess::getextension(string* filename, char* extension, size_t size) const
{
    const char* ptr = filename->data() + filename->size();
    char c;
    int i, j;

    size--;

    if (size > (int) filename->size())
    {
        size = filename->size();
    }

    for (i = 0; i < size; i++)
    {
        if (*--ptr == '.')
        {
            for (j = 0; j <= i; j++)
            {
                if (*ptr < '.' || *ptr > 'z') return false;

                c = *(ptr++);

                // tolower()
                if (c >= 'A' && c <= 'Z') c |= ' ';

                extension[j] = c;
            }

            extension[j] = 0;

            return true;
        }
    }

    return false;
}

bool PosixFileSystemAccess::expanselocalpath(string *path, string *absolutepath)
{
    ostringstream os;
    if (path->at(0) == '/')
    {
        *absolutepath = *path;
        char canonical[PATH_MAX];
        if (realpath(absolutepath->c_str(),canonical) != NULL)
        {
            absolutepath->assign(canonical);
        }
        return true;
    }
    else
    {
        char cCurrentPath[PATH_MAX];
        if (!getcwd(cCurrentPath, sizeof(cCurrentPath)))
        {
            *absolutepath = *path;
            return false;
        }

        *absolutepath = cCurrentPath;
        absolutepath->append("/");
        absolutepath->append(*path);

        char canonical[PATH_MAX];
        if (realpath(absolutepath->c_str(),canonical) != NULL)
        {
            absolutepath->assign(canonical);
        }

        return true;
    }
}

#ifdef __linux__
string &ltrimEtcProperty(string &s, const char &c)
{
    size_t pos = s.find_first_not_of(c);
    s = s.substr(pos == string::npos ? s.length() : pos, s.length());
    return s;
}

string &rtrimEtcProperty(string &s, const char &c)
{
    size_t pos = s.find_last_not_of(c);
    if (pos != string::npos)
    {
        pos++;
    }
    s = s.substr(0, pos);
    return s;
}

string &trimEtcproperty(string &what)
{
    rtrimEtcProperty(what,' ');
    ltrimEtcProperty(what,' ');
    if (what.size() > 1)
    {
        if (what[0] == '\'' || what[0] == '"')
        {
            rtrimEtcProperty(what, what[0]);
            ltrimEtcProperty(what, what[0]);
        }
    }
    return what;
}

string getPropertyFromEtcFile(const char *configFile, const char *propertyName)
{
    ifstream infile(configFile);
    string line;

    while (getline(infile, line))
    {
        if (line.length() > 0 && line[0] != '#')
        {
            if (!strlen(propertyName)) //if empty return first line
            {
                return trimEtcproperty(line);
            }
            string key, value;
            size_t pos = line.find("=");
            if (pos != string::npos && ((pos + 1) < line.size()))
            {
                key = line.substr(0, pos);
                rtrimEtcProperty(key, ' ');

                if (!strcmp(key.c_str(), propertyName))
                {
                    value = line.substr(pos + 1);
                    return trimEtcproperty(value);
                }
            }
        }
    }

    return string();
}

string getDistro()
{
    string distro;
    distro = getPropertyFromEtcFile("/etc/lsb-release", "DISTRIB_ID");
    if (!distro.size())
    {
        distro = getPropertyFromEtcFile("/etc/os-release", "ID");
    }
    if (!distro.size())
    {
        distro = getPropertyFromEtcFile("/etc/redhat-release", "");
    }
    if (!distro.size())
    {
        distro = getPropertyFromEtcFile("/etc/debian-release", "");
    }
    if (distro.size() > 20)
    {
        distro = distro.substr(0, 20);
    }
    transform(distro.begin(), distro.end(), distro.begin(), ::tolower);
    return distro;
}

string getDistroVersion()
{
    string version;
    version = getPropertyFromEtcFile("/etc/lsb-release", "DISTRIB_RELEASE");
    if (!version.size())
    {
        version = getPropertyFromEtcFile("/etc/os-release", "VERSION_ID");
    }
    if (version.size() > 10)
    {
        version = version.substr(0, 10);
    }
    transform(version.begin(), version.end(), version.begin(), ::tolower);
    return version;
}
#endif

void PosixFileSystemAccess::osversion(string* u) const
{
#ifdef __linux__
    string distro = getDistro();
    if (distro.size())
    {
        u->append(distro);
        string distroversion = getDistroVersion();
        if (distroversion.size())
        {
            u->append(" ");
            u->append(distroversion);
            u->append("/");
        }
        else
        {
            u->append("/");
        }
    }
#endif

    utsname uts;

    if (!uname(&uts))
    {
        u->append(uts.sysname);
        u->append(" ");
        u->append(uts.release);
        u->append(" ");
        u->append(uts.machine);
    }
}

void PosixFileSystemAccess::statsid(string *id) const
{
#ifdef __ANDROID__
    if (!MEGAjvm)
    {
        LOG_err << "No JVM found";
        return;
    }

    try
    {
        JNIEnv *env;
        MEGAjvm->AttachCurrentThread(&env, NULL);
        jclass appGlobalsClass = env->FindClass("android/app/AppGlobals");
        if (!appGlobalsClass)
        {
            env->ExceptionClear();
            LOG_err << "Failed to get android/app/AppGlobals";
            MEGAjvm->DetachCurrentThread();
            return;
        }

        jmethodID getInitialApplicationMID = env->GetStaticMethodID(appGlobalsClass,"getInitialApplication","()Landroid/app/Application;");
        if (!getInitialApplicationMID)
        {
            env->ExceptionClear();
            LOG_err << "Failed to get getInitialApplication()";
            MEGAjvm->DetachCurrentThread();
            return;
        }

        jobject context = env->CallStaticObjectMethod(appGlobalsClass, getInitialApplicationMID);
        if (!context)
        {
            LOG_err << "Failed to get context";
            MEGAjvm->DetachCurrentThread();
            return;
        }

        jclass contextClass = env->GetObjectClass(context);
        if (!contextClass)
        {
            LOG_err << "Failed to get context class";
            MEGAjvm->DetachCurrentThread();
            return;
        }

        jmethodID getContentResolverMID = env->GetMethodID(contextClass, "getContentResolver", "()Landroid/content/ContentResolver;");
        if (!getContentResolverMID)
        {
            env->ExceptionClear();
            LOG_err << "Failed to get getContentResolver()";
            MEGAjvm->DetachCurrentThread();
            return;
        }

        jobject contentResolver = env->CallObjectMethod(context, getContentResolverMID);
        if (!contentResolver)
        {
            LOG_err << "Failed to get ContentResolver";
            MEGAjvm->DetachCurrentThread();
            return;
        }

        jclass settingsSecureClass = env->FindClass("android/provider/Settings$Secure");
        if (!settingsSecureClass)
        {
            env->ExceptionClear();
            LOG_err << "Failed to get Settings.Secure class";
            MEGAjvm->DetachCurrentThread();
            return;
        }

        jmethodID getStringMID = env->GetStaticMethodID(settingsSecureClass, "getString", "(Landroid/content/ContentResolver;Ljava/lang/String;)Ljava/lang/String;");
        if (!getStringMID)
        {
            env->ExceptionClear();
            LOG_err << "Failed to get getString()";
            MEGAjvm->DetachCurrentThread();
            return;
        }

        jstring idStr = (jstring) env->NewStringUTF("android_id");
        if (!idStr)
        {
            LOG_err << "Failed to get idStr";
            MEGAjvm->DetachCurrentThread();
            return;
        }

        jstring androidId = (jstring) env->CallStaticObjectMethod(settingsSecureClass, getStringMID, contentResolver, idStr);
        if (!androidId)
        {
            LOG_err << "Failed to get android_id";
            env->DeleteLocalRef(idStr);
            MEGAjvm->DetachCurrentThread();
            return;
        }

        const char *androidIdString = env->GetStringUTFChars(androidId, NULL);
        if (!androidIdString)
        {
            LOG_err << "Failed to get android_id bytes";
            env->DeleteLocalRef(idStr);
            MEGAjvm->DetachCurrentThread();
            return;
        }

        id->append(androidIdString);
        env->DeleteLocalRef(idStr);
        env->ReleaseStringUTFChars(androidId, androidIdString);
        MEGAjvm->DetachCurrentThread();
    }
    catch (...)
    {
        try
        {
            MEGAjvm->DetachCurrentThread();
        }
        catch (...) { }
    }
#elif TARGET_OS_IPHONE
#ifdef USE_IOS
    ios_statsid(id);
#endif
#elif defined(__MACH__)
    uuid_t uuid;
    struct timespec wait = {1, 0};
    if (gethostuuid(uuid, &wait))
    {
        return;
    }

    char uuid_str[37];
    uuid_unparse(uuid, uuid_str);
    id->append(uuid_str);
#else
    int fd = open("/etc/machine-id", O_RDONLY);
    if (fd < 0)
    {
        fd = open("/var/lib/dbus/machine-id", O_RDONLY);
        if (fd < 0)
        {
            return;
        }
    }

    char buff[512];
    int len = read(fd, buff, 512);
    close(fd);

    if (len <= 0)
    {
        return;
    }

    if (buff[len - 1] == '\n')
    {
        len--;
    }

    if (len > 0)
    {
        id->append(buff, len);
    }
#endif
}

PosixDirNotify::PosixDirNotify(string* localbasepath, string* ignore) : DirNotify(localbasepath, ignore)
{
#ifdef USE_INOTIFY
    failed = 0;
#endif

#ifdef __MACH__
    failed = 0;
#endif

    fsaccess = NULL;
}

void PosixDirNotify::addnotify(LocalNode* l, string* path)
{
#ifdef ENABLE_SYNC
#ifdef USE_INOTIFY
    int wd;

    wd = inotify_add_watch(fsaccess->notifyfd, path->c_str(),
                           IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO
                           | IN_CLOSE_WRITE | IN_EXCL_UNLINK | IN_ONLYDIR);

    if (wd >= 0)
    {
        l->dirnotifytag = (handle)wd;
        fsaccess->wdnodes[wd] = l;
    }
    else
    {
        LOG_warn << "Unable to addnotify path: " <<  path->c_str() << ". Error code: " << errno;
    }
#endif
#endif
}

void PosixDirNotify::delnotify(LocalNode* l)
{
#ifdef ENABLE_SYNC
#ifdef USE_INOTIFY
    if (fsaccess->wdnodes.erase((int)(long)l->dirnotifytag))
    {
        inotify_rm_watch(fsaccess->notifyfd, (int)l->dirnotifytag);
    }
#endif
#endif
}

fsfp_t PosixDirNotify::fsfingerprint() const
{
    struct statfs statfsbuf;

    // FIXME: statfs() does not really do what we want.
    if (statfs(localbasepath.c_str(), &statfsbuf)) return 0;

    return *(fsfp_t*)&statfsbuf.f_fsid + 1;
}

bool PosixDirNotify::fsstableids() const
{
    struct statfs statfsbuf;

    if (statfs(localbasepath.c_str(), &statfsbuf))
    {
        LOG_err << "Failed to get filesystem type. Error code: " << errno;
        return true;
    }

    LOG_info << "Filesystem type: 0x" << std::hex << statfsbuf.f_type;

#ifdef __APPLE__
    return statfsbuf.f_type != 0x1c // FAT32
        && statfsbuf.f_type != 0x1d; // exFAT
#else
    return statfsbuf.f_type != 0x4d44 // FAT
        && statfsbuf.f_type != 0x65735546; // FUSE
#endif
}

FileAccess* PosixFileSystemAccess::newfileaccess()
{
    return new PosixFileAccess(waiter, defaultfilepermissions);
}

DirAccess* PosixFileSystemAccess::newdiraccess()
{
    return new PosixDirAccess();
}

DirNotify* PosixFileSystemAccess::newdirnotify(string* localpath, string* ignore)
{
    PosixDirNotify* dirnotify = new PosixDirNotify(localpath, ignore);

    dirnotify->fsaccess = this;

    return dirnotify;
}

bool PosixDirAccess::dopen(string* path, FileAccess* f, bool doglob)
{
#ifdef USE_IOS
    string absolutepath;
    if (PosixFileSystemAccess::appbasepath)
    {
        if (path->size() && path->at(0) != '/')
        {
            absolutepath = PosixFileSystemAccess::appbasepath;
            absolutepath.append(*path);
            path = &absolutepath;
        }
    }
#endif

    if (doglob)
    {
        if (glob(path->c_str(), GLOB_NOSORT, NULL, &globbuf))
        {
            return false;
        }

        globbing = true;
        globindex = 0;

        return true;
    }

    if (f)
    {
#ifdef HAVE_FDOPENDIR
        dp = fdopendir(((PosixFileAccess*)f)->fd);
        ((PosixFileAccess*)f)->fd = -1;
#else
        dp = ((PosixFileAccess*)f)->dp;
        ((PosixFileAccess*)f)->dp = NULL;
#endif
    }
    else
    {
        dp = opendir(path->c_str());
    }

    return dp != NULL;
}

bool PosixDirAccess::dnext(string* path, string* name, bool followsymlinks, nodetype_t* type)
{
#ifdef USE_IOS
    string absolutepath;
    if (PosixFileSystemAccess::appbasepath)
    {
        if (path->size() && path->at(0) != '/')
        {
            absolutepath = PosixFileSystemAccess::appbasepath;
            absolutepath.append(*path);
            path = &absolutepath;
        }
    }
#endif

    if (globbing)
    {
        struct stat statbuf;

        while (globindex < globbuf.gl_pathc)
        {
            if (!stat(globbuf.gl_pathv[globindex++], &statbuf))
            {
                if (statbuf.st_mode & (S_IFREG | S_IFDIR))
                {
                    *name = globbuf.gl_pathv[globindex - 1];
                    *type = (statbuf.st_mode & S_IFREG) ? FILENODE : FOLDERNODE;

                    return true;
                }
            }
        }

        return false;
    }

    dirent* d;
    size_t pathsize = path->size();
    struct stat statbuf;

    path->append("/");

    while ((d = readdir(dp)))
    {
        if (*d->d_name != '.' || (d->d_name[1] && (d->d_name[1] != '.' || d->d_name[2])))
        {
            path->append(d->d_name);

            if (followsymlinks ? !stat(path->c_str(), &statbuf) : !lstat(path->c_str(), &statbuf))
            {
                if (S_ISREG(statbuf.st_mode) || S_ISDIR(statbuf.st_mode))
                {
                    path->resize(pathsize);
                    *name = d->d_name;

                    if (type)
                    {
                        *type = S_ISREG(statbuf.st_mode) ? FILENODE : FOLDERNODE;
                    }

                    return true;
                }
            }

            path->resize(pathsize+1);
        }
    }

    path->resize(pathsize);

    return false;
}

PosixDirAccess::PosixDirAccess()
{
    dp = NULL;
    globbing = false;
    memset(&globbuf, 0, sizeof(glob_t));
    globindex = 0;
}

PosixDirAccess::~PosixDirAccess()
{
    if (dp)
    {
        closedir(dp);
    }

    if (globbing)
    {
        globfree(&globbuf);
    }
}
} // namespace
