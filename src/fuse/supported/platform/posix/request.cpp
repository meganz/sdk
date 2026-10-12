#include <mega/fuse/common/logging.h>
#include <mega/fuse/platform/request.h>

#include <cstring>

namespace mega
{
namespace fuse
{
namespace platform
{

template<typename T>
void Request::reply(T&& responder)
{
    // Sanity.
    assert(mRequest);

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

        FUSEErrorF("Unable to send response for request: %p: %s", mRequest, std::strerror(-result));
    }
}

Request::Request(fuse_req_t request):
    mRequest(request)
{}

fuse_req_t Request::get() const
{
    return mRequest;
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
    reply(
        [&](fuse_req_t request)
        {
            return fuse_reply_statfs(request, &attributes);
        });
}

void Request::replyAttributes(const struct stat& attributes, const double timeout)
{
    reply(
        [&](fuse_req_t request)
        {
            return fuse_reply_attr(request, &attributes, timeout);
        });
}

void Request::replyBuffer(const std::string& buffer)
{
    reply(
        [&](fuse_req_t request)
        {
            return fuse_reply_buf(request, buffer.data(), buffer.size());
        });
}

void Request::replyEntry(const struct fuse_entry_param& entry)
{
    reply(
        [&](fuse_req_t request)
        {
            return fuse_reply_entry(request, &entry);
        });
}

void Request::replyError(const int error)
{
    reply(
        [=](fuse_req_t request)
        {
            return fuse_reply_err(request, error);
        });
}

void Request::replyNone()
{
    reply(
        [&](fuse_req_t request)
        {
            return fuse_reply_none(request), 0;
        });
}

void Request::replyOk()
{
    replyError(0);
}

void Request::replyOpen(const fuse_file_info& info)
{
    reply(
        [&](fuse_req_t request)
        {
            return fuse_reply_open(request, &info);
        });
}

void Request::replyWritten(const std::size_t numBytes)
{
    reply(
        [&](fuse_req_t request)
        {
            return fuse_reply_write(request, numBytes);
        });
}

} // platform
} // fuse
} // mega
