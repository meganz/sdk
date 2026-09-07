#pragma once

#include <mega/fuse/platform/session_base.h>

namespace mega
{
namespace fuse
{
namespace platform
{

// How we communicate with FUSE.
class Session: public SessionBase
{
    void populateCapabilities(fuse_conn_info* connection) override;

    void populateOperations(fuse_lowlevel_ops& operations) override;

    static void readdirplus(fuse_req_t request,
                            fuse_ino_t inode,
                            size_t size,
                            off_t offset,
                            struct fuse_file_info* info);

    static void rename(fuse_req_t request,
                       fuse_ino_t sourceParent,
                       const char* sourceName,
                       fuse_ino_t targetParent,
                       const char* targetName,
                       unsigned int flags);

    // Receive buffer, reused across dispatch() calls so the full channel buffer
    // isn't re-allocated on every FUSE request.
    //
    // Only ever accessed by the single MountDB dispatcher thread that drives
    // dispatch(). If request reception ever becomes multi-threaded, this must
    // become per-receiving-thread rather than a shared member.
    fuse_buf mBuffer{};

public:
    Session(Mount& mount);

    ~Session();

    // What descriptor is the session using to communicate with FUSE?
    int descriptor() const override;

    // Dispatch a request received from FUSE.
    void dispatch() override;

    // Invalidate an inode's data.
    void invalidateData(MountInodeID id, off_t offset, off_t size) override;

    // Invalidate a specific directory entry.
    void invalidateEntry(const std::string& name, MountInodeID child, MountInodeID parent) override;

    void invalidateEntry(const std::string& name, MountInodeID parent) override;
}; // Session

} // platform
} // fuse
} // mega
