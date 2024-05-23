#pragma once

#include <cstddef>
#include <string>

#include <mega/fuse/platform/library.h>
#include <mega/fuse/platform/request_forward.h>

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

    bool addDirEntry(const struct stat& attributes,
                     std::string& buffer,
                     const std::string& name,
                     const std::size_t offset,
                     const std::size_t size);

    gid_t group() const;

    uid_t owner() const;

    void replyAttributes(const struct statvfs& attributes);

    void replyAttributes(const struct stat& attributes,
                         double timeout);

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

