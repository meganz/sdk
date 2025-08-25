#include <mega/common/error_or.h>
#include <mega/common/testing/cloud_path.h>
#include <mega/fuse/common/testing/client.h>

namespace mega
{
namespace common
{
namespace testing
{

using fuse::testing::Client;

CloudPath::CloudPath(const std::string& path):
    mHandle(),
    mPath(path)
{}

CloudPath::CloudPath(const char* path):
    mHandle(),
    mPath(path)
{}

CloudPath::CloudPath(NodeHandle handle):
    mHandle(handle),
    mPath()
{}

ErrorOr<NodeHandle> CloudPath::resolve(const Client& client) const
{
    if (mHandle.isUndef())
        return client.handle(mPath);

    return mHandle;
}

} // testing
} // fuse
} // mega
