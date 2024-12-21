#pragma once

#include <mega/fuse/common/testing/client_forward.h>
#include <mega/fuse/common/testing/cloud_path_forward.h>

#include <mega/types.h>

namespace mega
{
namespace fuse
{
namespace testing
{

class CloudPath
{
    NodeHandle mHandle;
    std::string mPath;

public:
    CloudPath() = default;

    CloudPath(const CloudPath& other) = default;

    CloudPath(CloudPath&& other) = default;

    CloudPath(const std::string& path);

    CloudPath(const char* path);

    CloudPath(NodeHandle handle);

    ~CloudPath() = default;

    CloudPath& operator=(const CloudPath& rhs) = default;

    CloudPath& operator=(CloudPath&& rhs) = default;

    NodeHandle resolve(const Client& client) const;
}; // CloudPath

} // testing
} // fuse
} // mega

