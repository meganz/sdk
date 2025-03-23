#pragma once

#include <mega/fuse/common/mount_inode_id_forward.h>
#include <mega/fuse/platform/library.h>
#include <mega/fuse/platform/mount_forward.h>

#include <mutex>
#include <string>
#include <vector>

namespace mega
{
namespace fuse
{
namespace platform
{

class SessionBase
{
    static fuse_lowlevel_ops mOperations;
    static std::once_flag mOperationsInitialized;

protected:
    class Arguments
    {
        fuse_args mArguments;
        std::vector<char*> mPointers;
        std::vector<std::string> mStrings;

    public:
        Arguments(const std::string& name);

        fuse_args* get();
    }; // Arguments

    SessionBase(Mount& mount);

    ~SessionBase();

    static void access(fuse_req_t request,
                       fuse_ino_t inode,
                       int mask);

    static void lookup(fuse_req_t request,
                       fuse_ino_t parent,
                       const char* name);

    static void flush(fuse_req_t request,
                      fuse_ino_t inode,
                      fuse_file_info* info);

    static void forget(fuse_req_t request,
                       fuse_ino_t inode,
                       std::uint64_t num);

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

    static Mount& mount(fuse_req_t request);

    static Mount& mount(void* context);

    static void open(fuse_req_t request,
                     fuse_ino_t inode,
                     fuse_file_info* info);

    static void opendir(fuse_req_t request,
                        fuse_ino_t inode,
                        fuse_file_info* info);

    const fuse_lowlevel_ops& operations();

    virtual void populateOperations(fuse_lowlevel_ops& operations);

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

    Mount& mMount;
    fuse_session* mSession;

public:
    // What descriptor is the session using to communicate with FUSE?
    virtual int descriptor() const = 0;

    // Dispatch a request received from FUSE.
    virtual void dispatch() = 0;

    // Destroy the mount associated with this session.
    void destroy();

    // Has this session exited?
    bool exited() const;

    // Invalidate an inode's attributes.
    void invalidateAttributes(MountInodeID id);

    // Invalidate an inode's data.
    virtual void invalidateData(MountInodeID id,
                                off_t offset,
                                off_t size) = 0;

    void invalidateData(MountInodeID id);

    // Invalidate a specific directory entry.
    virtual void invalidateEntry(const std::string& name,
                                 MountInodeID child,
                                 MountInodeID parent) = 0;

    virtual void invalidateEntry(const std::string& name,
                                 MountInodeID parent) = 0;
}; // SessionBase

} // platform
} // fuse
} // mega

