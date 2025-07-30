#include <mega/fuse/common/logging.h>
#include <mega/fuse/platform/request.h>

namespace mega
{
namespace fuse
{
namespace platform
{

template<typename T>
void Request::reply(T&& responder)
{
    while (true)
    {
        auto result = responder(mRequest);

        if (result == -EINTR)
            continue;

        if (!result)
        {
            FUSEDebugF("Response sent for request: %p", mRequest);
            return;
        }

        throw FUSEErrorF("Unable to send response for request: %p", mRequest);
    }
}

Request::Request(fuse_req_t request)
  : mRequest(request)
{
}

bool Request::addDirEntry(const struct stat& attributes,
                                 std::string& buffer,
                                 const std::string& name,
                                 const std::size_t offset,
                                 const std::size_t size)
{
    // How much have we written to the buffer?
    auto current = buffer.size();

    // How much space does this entry need?
    auto required = fuse_add_direntry(mRequest,
                                      nullptr,
                                      0,
                                      name.c_str(),
                                      nullptr,
                                      0);

    // Don't have enough space for this entry.
    if (current + required > size)
        return false;

    // Expand the buffer.
    buffer.resize(current + required);

    // Add the entry to the buffer.
    fuse_add_direntry(mRequest,
                      &buffer[current],
                      required,
                      name.c_str(),
                      &attributes,
                      static_cast<off_t>(offset));

    // Let the caller know the entry's been added.
    return true;
}

gid_t Request::group() const
{
    return fuse_req_ctx(mRequest)->gid;
}

uid_t Request::owner() const
{
    return fuse_req_ctx(mRequest)->uid;
}

pid_t Request::process() const
{
    return fuse_req_ctx(mRequest)->pid;
}

void Request::replyAttributes(const struct statvfs& attributes)
{
    reply([&](fuse_req_t request) {
        return fuse_reply_statfs(request, &attributes);
    });
}

void Request::replyAttributes(const struct stat& attributes,
                              const double timeout)
{
    reply([&](fuse_req_t request) {
        return fuse_reply_attr(request, &attributes, timeout);
    });
}

void Request::replyBuffer(const std::string& buffer)
{
    reply([&](fuse_req_t request) {
        return fuse_reply_buf(request, buffer.data(), buffer.size());
    });
}

void Request::replyEntry(const struct fuse_entry_param& entry)
{
    reply([&](fuse_req_t request) {
        return fuse_reply_entry(request, &entry);
    });
}

void Request::replyError(const int error)
{
    reply([=](fuse_req_t request) {
        return fuse_reply_err(request, error);
    });
}

void Request::replyNone()
{
    reply([&](fuse_req_t request) {
        return fuse_reply_none(request), 0;
    });
}

void Request::replyOk()
{
    replyError(0);
}

void Request::replyOpen(const fuse_file_info& info)
{
    reply([&](fuse_req_t request) {
        return fuse_reply_open(request, &info);
    });
}

void Request::replyWritten(const std::size_t numBytes)
{
    reply([&](fuse_req_t request) {
        return fuse_reply_write(request, numBytes);
    });
}

} // platform
} // fuse
} // mega

