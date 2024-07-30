#pragma once

#include <memory>
#include <string>

#include <mega/fuse/common/mount_inode_id.h>
#include <mega/fuse/platform/library.h>
#include <mega/fuse/platform/mount_forward.h>
#include <mega/fuse/platform/session_forward.h>

namespace mega
{
namespace fuse
{
namespace platform
{

// How we communicate with FUSE.
class Session
{
    static void access(fuse_req_t request,
                       fuse_ino_t inode,
                       int mask);

    static void init(void* context, fuse_conn_info* connection);

    static void lookup(fuse_req_t request,
                       fuse_ino_t parent,
                       const char* name);

    static void flush(fuse_req_t request,
                      fuse_ino_t inode,
                      fuse_file_info* info);

    static void forget(fuse_req_t request,
                       fuse_ino_t inode,
                       std::size_t num);

    static void forget_multi(fuse_req_t request,
                             std::size_t count,
                             fuse_forget_data* forgets);

    static void fsync(fuse_req_t request,
                      fuse_ino_t inode,
                      int onlyData,
                      fuse_file_info* info);

    static void getattr(fuse_req_t request,
                        fuse_ino_t inode,
                        fuse_file_info* info);

    static void mkdir(fuse_req_t request,
                      fuse_ino_t parent,
                      const char* name,
                      mode_t mode);

    static void mknod(fuse_req_t request,
                      fuse_ino_t parent,
                      const char* name,
                      mode_t mode,
                      dev_t device);

    static void open(fuse_req_t request,
                     fuse_ino_t inode,
                     fuse_file_info* info);

    static void opendir(fuse_req_t request,
                        fuse_ino_t inode,
                        fuse_file_info* info);

    static void read(fuse_req_t request,
                     fuse_ino_t inode,
                     size_t size,
                     off_t offset,
                     fuse_file_info* info);

    static void readdir(fuse_req_t request,
                        fuse_ino_t inode,
                        std::size_t size,
                        off_t offset,
                        fuse_file_info* info);

    static void release(fuse_req_t request,
                        fuse_ino_t inode,
                        fuse_file_info* info);

    static void releasedir(fuse_req_t request,
                           fuse_ino_t inode,
                           fuse_file_info* info);

    static void rename(fuse_req_t request,
                       fuse_ino_t sourceParent,
                       const char* sourceName,
                       fuse_ino_t targetParent,
                       const char* targetName);

    static void rmdir(fuse_req_t request,
                      fuse_ino_t parent,
                      const char* name);

    static void setattr(fuse_req_t request,
                        fuse_ino_t inode,
                        struct stat* attributes,
                        int changes,
                        fuse_file_info* info);

    static void statfs(fuse_req_t request,
                       fuse_ino_t inode);

    static void unlink(fuse_req_t request,
                       fuse_ino_t parent,
                       const char* name);

    static void write(fuse_req_t request,
                      fuse_ino_t inode,
                      const char* data,
                      size_t size,
                      off_t offset,
                      fuse_file_info* info);

    fuse_chan* mChannel;
    Mount& mMount;
    static const fuse_lowlevel_ops mOperations;
    fuse_session* mSession;

public:
    Session(Mount& mount);

    ~Session();

    // What descriptor is the session using to communicate with FUSE?
    int descriptor() const;

    // Dispatch a request received from FUSE.
    void dispatch(std::string request);

    // Destroy the mount associated with this session.
    void destroy();

    // Has this session exited?
    bool exited() const;

    // Invalidate an inode's attributes.
    void invalidateAttributes(MountInodeID id);

    // Invalidate an inode's data.
    void invalidateData(MountInodeID id,
                        off_t offset,
                        off_t size);

    void invalidateData(MountInodeID id);

    // Invalidate a specific directory entry.
    void invalidateEntry(const std::string& name,
                         MountInodeID child,
                         MountInodeID parent);

    void invalidateEntry(const std::string& name,
                         MountInodeID parent);

    // Retrieve the next request from FUSE.
    std::string nextRequest();
}; // Session

} // platform
} // fuse
} // mega

