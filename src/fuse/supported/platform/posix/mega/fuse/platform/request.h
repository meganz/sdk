#pragma once

#include <mega/fuse/platform/library.h>
#include <mega/fuse/platform/request_forward.h>

#include <cstddef>
#include <string>

namespace mega
{
namespace fuse
{
namespace platform
{

class Request
{
    template<typename T>
    void reply(T&& responder);

    const fuse_req_t mRequest;

public:
    explicit Request(fuse_req_t request);

    fuse_req_t get() const;

    gid_t group() const;

    uid_t owner() const;

    pid_t process() const;

    void replyAttributes(const struct statvfs& attributes);

    void replyAttributes(const struct stat& attributes, double timeout);

    void replyBuffer(const std::string& buffer);

    void replyEntry(const struct fuse_entry_param& entry);

    void replyError(int error);

    void replyNone();

    void replyOk();

    void replyOpen(const fuse_file_info& info);

    void replyWritten(std::size_t numBytes);
}; // Request

} // platform
} // fuse
} // mega
