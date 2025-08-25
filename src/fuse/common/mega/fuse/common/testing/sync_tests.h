#pragma once

#include <mega/common/testing/cloud_path_forward.h>
#include <mega/common/testing/path_forward.h>
#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/testing/test.h>

namespace mega
{
namespace fuse
{
namespace testing
{

struct FUSESyncTests: Test
{}; // FUSESyncTests

class ScopedMount
{
    Client& mClient;
    std::string mName;
    MountResult mResult;

public:
    ScopedMount(ClientPtr& client,
                const std::string& name,
                common::testing::Path sourcePath,
                common::testing::CloudPath targetPath);

    ScopedMount(ClientPtr& client,
                common::testing::Path sourcePath,
                common::testing::CloudPath targetPath);

    ScopedMount(const ScopedMount& other) = delete;

    ~ScopedMount();

    ScopedMount& operator=(const ScopedMount& rhs) = delete;

    MountResult result() const;
}; // ScopedMount

class ScopedSync
{
    Client& mClient;
    std::tuple<handle, Error, SyncError> mContext;

public:
    ScopedSync(ClientPtr& client,
               common::testing::Path sourcePath,
               common::testing::CloudPath targetPath);

    ScopedSync(const ScopedSync& other) = delete;

    ~ScopedSync();

    ScopedSync& operator=(const ScopedSync& rhs) = delete;

    Error error() const;

    SyncError syncError() const;
}; // ScopedSync

} // testing
} // fuse
} // mega
