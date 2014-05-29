/**
 * @file posix/fs.cpp
 * @brief POSIX filesystem/directory access/notification
 *
 * (c) 2013-2014 by Mega Limited, Wellsford, New Zealand
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

namespace mega {
PosixFileAccess::PosixFileAccess()
{
    fd = -1;

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

    if (!stat(localname.c_str(), &statbuf))
    {
        if (S_ISDIR(statbuf.st_mode))
        {
            return false;
        }

        *size = statbuf.st_size;
        *mtime = statbuf.st_mtime;

        FileSystemAccess::captimestamp(mtime);

        return true;
    }

    return true;
}

bool PosixFileAccess::sysopen()
{
    return (fd = open(localname.c_str(), O_RDONLY)) >= 0;
}

void PosixFileAccess::sysclose()
{
    if (localname.size())
    {
        // fd will always be valid at this point
        close(fd);
        fd = -1;
    }
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
#ifndef __ANDROID__
    return pread(fd, (char*)dst, len, pos) == len;
#else
    lseek(fd, pos, SEEK_SET);
    return read(fd, (char*)dst, len) == len;
#endif
}

bool PosixFileAccess::fwrite(const byte* data, unsigned len, m_off_t pos)
{
#ifndef __ANDROID__
    return pwrite(fd, data, len, pos) == len;
#else
    lseek(fd, pos, SEEK_SET);
    return write(fd, data, len) == len;
#endif
}

bool PosixFileAccess::fopen(string* f, bool read, bool write)
{
    struct stat statbuf;

    retry = false;

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

    if ((fd = open(f->c_str(), write ? (read ? O_RDWR : O_WRONLY | O_CREAT | O_TRUNC) : O_RDONLY, 0600)) >= 0)
    {
        if (!fstat(fd, &statbuf))
        {
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

    return false;
}

PosixFileSystemAccess::PosixFileSystemAccess(int fseventsfd)
{
    assert(sizeof(off_t) == 8);

    notifyerr = false;
    notifyfailed = true;
    notifyfd = -1;

    localseparator = "/";

#ifdef USE_INOTIFY
    if ((notifyfd = inotify_init1(IN_NONBLOCK)) >= 0)
    {
        lastcookie = 0;
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
#define	FSEVENTS_CLONE _IOW('s', 1, fsevent_clone_args)
#define	FSEVENTS_WANT_EXTENDED_INFO _IO('s', 102)

    int fd;
    struct fsevent_clone_args fca;
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
void PosixFileSystemAccess::addevents(Waiter* w, int flags)
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
// FIXME: ignore sync-specific debris folder
int PosixFileSystemAccess::checkevents(Waiter* w)
{
    int r = 0;

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

                if (in->mask & (IN_CREATE | IN_DELETE | IN_MOVED_FROM
                                 | IN_MOVED_TO | IN_CLOSE_WRITE | IN_EXCL_UNLINK))
                {
                    if ((in->mask & (IN_CREATE | IN_ISDIR)) != IN_CREATE)
                    {
                        it = wdnodes.find(in->wd);

                        if (it != wdnodes.end())
                        {
                            if (lastcookie && lastcookie != in->cookie)
                            {
                                ignore = &lastlocalnode->sync->dirnotify->ignore;
                                if((lastname.size() < ignore->size())
                                    || memcmp(lastname.c_str(), ignore->data(), ignore->size())
                                    || ((lastname.size() > ignore->size())
                                            && memcmp(lastname.c_str() + ignore->size(), localseparator.c_str(), localseparator.size())))
                                {
                                    // previous IN_MOVED_FROM is not followed by the
                                    // corresponding IN_MOVED_TO, so was actually a deletion
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
                                if((strlen(in->name) < ignore->size())
                                    || memcmp(in->name, ignore->data(), ignore->size())
                                    || ((strlen(in->name) > ignore->size())
                                            && memcmp(in->name + ignore->size(), localseparator.c_str(), localseparator.size())))
                                {
                                    it->second->sync->dirnotify->notify(DirNotify::DIREVENTS,
                                                                        it->second, in->name,
                                                                        strlen(in->name));

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

                for (it = client->syncs.begin(); it != client->syncs.end(); it++)
                {
                    int s = (*it)->localroot.localname.size();

                    if (!memcmp((*it)->localroot.localname.c_str(), path, s)	// prefix match
                      && (!path[s] || path[s] == '/')				// at end: end of path or path separator
                      && (memcmp(path + s + 1, (*it)->dirnotify->ignore.c_str(), (*it)->dirnotify->ignore.size())
                          || (path[s + (*it)->dirnotify->ignore.size() + 1]
                           && path[s + (*it)->dirnotify->ignore.size() + 1] != '/')))
                        {
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

void PosixFileSystemAccess::path2local(string* local, string* path) const
{
    *path = *local;
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

bool PosixFileSystemAccess::renamelocal(string* oldname, string* newname, bool)
{
    if (!rename(oldname->c_str(), newname->c_str()))
    {
        return true;
    }
    
    target_exists = errno == EEXIST;
    transient_error = errno == ETXTBSY || errno == EBUSY;

    return false;
}

bool PosixFileSystemAccess::copylocal(string* oldname, string* newname, m_time_t mtime)
{
    int sfd, tfd;
    ssize_t t = -1;

#ifdef HAVE_SENDFILE
    // Linux-specific - kernel 2.6.33+ required
    if ((sfd = open(oldname->c_str(), O_RDONLY | O_DIRECT)) >= 0)
    {
        if ((tfd = open(newname->c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_DIRECT, 0600)) >= 0)
        {
            while ((t = sendfile(tfd, sfd, NULL, 1024 * 1024 * 1024)) > 0);
#else
    char buf[16384];

    if ((sfd = open(oldname->c_str(), O_RDONLY)) >= 0)
    {
        if ((tfd = open(newname->c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600)) >= 0)
        {
            while (((t = read(sfd, buf, sizeof buf)) > 0) && write(tfd, buf, t) == t);
#endif
            close(tfd);
        }
        else
        {
            target_exists = errno == EEXIST;
            transient_error = errno == ETXTBSY || errno == EBUSY;
        }

        close(sfd);
    }

    if (!t)
    {
        setmtimelocal(newname,mtime);    
    }

    return !t;
}

// FIXME: add platform support for recycle bins
bool PosixFileSystemAccess::rubbishlocal(string* name)
{
    return false;
}

bool PosixFileSystemAccess::unlinklocal(string* name)
{
    if (!unlink(name->c_str())) return true;

    transient_error = errno == ETXTBSY || errno == EBUSY;

    return false;
}

bool PosixFileSystemAccess::rmdirlocal(string* name)
{
    if (!rmdir(name->c_str())) return true;

    transient_error = errno == ETXTBSY || errno == EBUSY;

    return false;
}

bool PosixFileSystemAccess::mkdirlocal(string* name, bool)
{
    bool r = !mkdir(name->c_str(), 0700);

    if (!r)
    {
        target_exists = errno == EEXIST;
        transient_error = errno == ETXTBSY || errno == EBUSY;
    }

    return r;
}

bool PosixFileSystemAccess::setmtimelocal(string* name, m_time_t mtime) const
{
    struct utimbuf times = { mtime, mtime };

    return !utime(name->c_str(), &times);
}

bool PosixFileSystemAccess::chdirlocal(string* name) const
{
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
bool PosixFileSystemAccess::getextension(string* filename, char* extension, int size) const
{
	const char* ptr = filename->data() + filename->size();
    char c;
    int i, j;

	size--;

	if (size > filename->size())
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

void PosixFileSystemAccess::osversion(string* u) const
{
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

PosixDirNotify::PosixDirNotify(string* localbasepath, string* ignore) : DirNotify(localbasepath, ignore)
{
#ifdef USE_INOTIFY
    failed = false;
#endif

#ifdef __MACH__
    failed = false;
#endif
}

void PosixDirNotify::addnotify(LocalNode* l, string* path)
{
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
#endif
}

void PosixDirNotify::delnotify(LocalNode* l)
{
#ifdef USE_INOTIFY
    if (fsaccess->wdnodes.erase((int)(long)l->dirnotifytag))
    {
        inotify_rm_watch(fsaccess->notifyfd, (int)l->dirnotifytag);
    }
#endif
}

FileAccess* PosixFileSystemAccess::newfileaccess()
{
    return new PosixFileAccess();
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

bool PosixDirAccess::dnext(string* name, nodetype_t* type)
{
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

    while ((d = readdir(dp)))
    {
        if (((d->d_type == DT_DIR)
                || (d->d_type == DT_REG))
                && ((d->d_type != DT_DIR)
                        || (*d->d_name != '.')
                        || (d->d_name[1] && ((d->d_name[1] != '.') || d->d_name[2]))))
        {
            *name = d->d_name;

            if (type)
            {
                *type = d->d_type == DT_DIR ? FOLDERNODE : FILENODE;
            }

            return true;
        }
    }

    return false;
}

PosixDirAccess::PosixDirAccess()
{
    dp = NULL;
    globbing = false;
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
