#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/mount_event.h>
#include <mega/fuse/common/mount_event_type.h>
#include <mega/fuse/common/mount_info.h>
#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/normalized_path.h>
#include <mega/fuse/common/testing/client.h>
#include <mega/fuse/common/testing/cloud_path.h>
#include <mega/fuse/common/testing/directory.h>
#include <mega/fuse/common/testing/mount_event_observer.h>
#include <mega/fuse/common/testing/path.h>
#include <mega/fuse/common/testing/test.h>
#include <mega/fuse/platform/platform.h>

namespace mega
{
namespace fuse
{
namespace testing
{

struct FUSESyncTests
  : Test
{
}; // FUSESyncTests

class ScopedMount
{
    Client& mClient;
    std::string mName;
    MountResult mResult;

public:
    ScopedMount(ClientPtr& client,
                Path sourcePath,
                CloudPath targetPath);

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
               Path sourcePath,
               CloudPath targetPath);

    ScopedSync(const ScopedSync& other) = delete;

    ~ScopedSync();

    ScopedSync& operator=(const ScopedSync& rhs) = delete;

    Error error() const;

    SyncError syncError() const;
}; // ScopedSync

TEST_F(FUSESyncTests, cant_mount_above_sync)
{
    Directory s("s", mScratchPath);
    Directory sd0("sd0", s.path());

    // Sync s/sd0.
    ScopedSync ssd0(ClientW(), sd0.path(), "x/s/sd0");

    ASSERT_EQ(ssd0.error(), API_OK);
    ASSERT_EQ(ssd0.syncError(), NO_SYNC_ERROR);

    // Try and mount s.
    ScopedMount ms(ClientW(), s.path(), "x/s");

    // Attempted mount should fail.
    ASSERT_EQ(ms.result(), MOUNT_LOCAL_SYNCING);
}

TEST_F(FUSESyncTests, cant_mount_below_sync)
{
    Directory s("s", mScratchPath);

    // Sync s.
    ScopedSync ssd0(ClientW(), s.path(), "x/s");

    ASSERT_EQ(ssd0.error(), API_OK);
    ASSERT_EQ(ssd0.syncError(), NO_SYNC_ERROR);

    UNIX_ONLY(Directory sd0("sd0", s.path()));

    // Try and mount s/sd0.
    ScopedMount msd0(ClientW(), s.path() / "sd0", "x/s/sd0");

    // Attempted mount should fail.
    ASSERT_EQ(msd0.result(), MOUNT_LOCAL_SYNCING);
}

TEST_F(FUSESyncTests, cant_sync_above_mount)
{
    Directory s("s", mScratchPath);

    UNIX_ONLY(Directory sd0("sd0", s.path()));

    // Try and mount s/sd0.
    ScopedMount msd0(ClientW(), s.path() / "sd0", "x/s/sd0");

    ASSERT_EQ(msd0.result(), MOUNT_SUCCESS);

    // Try and sync s.
    ScopedSync ss(ClientW(), s.path(), "x/s");

    EXPECT_EQ(ss.error(), API_EFAILED);
    EXPECT_EQ(ss.syncError(), LOCAL_PATH_MOUNTED);
}

TEST_F(FUSESyncTests, cant_sync_below_mount)
{
    UNIX_ONLY(Directory s("s", mScratchPath));

    // Try and mount s.
    ScopedMount ms(ClientW(), mScratchPath / "s", "x/s");

    ASSERT_EQ(ms.result(), MOUNT_SUCCESS);

    // Try and sync s/sd0.
    ScopedSync ssd0(ClientW(), mScratchPath / "s" / "sd0", "x/s/sd0");

    EXPECT_EQ(ssd0.error(), API_EFAILED);
    EXPECT_EQ(ssd0.syncError(), LOCAL_PATH_MOUNTED);
}

ScopedMount::ScopedMount(ClientPtr& client,
                         Path sourcePath,
                         CloudPath targetPath)
  : mClient(*client)
  , mName(sourcePath.localPath().leafName().toPath(false))
  , mResult()
{
    MountInfo info;

    // Describe our new mount.
    info.name(mName);
    info.mHandle = targetPath.resolve(mClient);
    info.mPath = sourcePath;

    // Try and add our mount.
    mResult = mClient.addMount(info);

    // Couldn't add the mount.
    if (mResult != MOUNT_SUCCESS)
        return;

    // So we can wait until the mount is actually active.
    auto observer = mClient.mountEventObserver();

    observer->expect({
        mName,
        MOUNT_SUCCESS,
        MOUNT_ENABLED
    });

    // Try and enable mount.
    mResult = mClient.enableMount(mName, false);

    // Couldn't enable the mount.
    if (mResult != MOUNT_SUCCESS)
        return;

    // Never received enabled event.
    if (!observer->wait(Test::mDefaultTimeout))
        mResult = MOUNT_UNEXPECTED;
}

ScopedMount::~ScopedMount()
{
    // Mount was never added or enabled.
    if (mResult != MOUNT_SUCCESS)
        return;

    // Try and disable mount.
    mResult = mClient.disableMount(mName, false);

    // Couldn't disable mount.
    if (mResult != MOUNT_SUCCESS)
        return;

    // Try and remove mount.
    mResult = mClient.removeMount(mName);
}

MountResult ScopedMount::result() const
{
    return mResult;
}

ScopedSync::ScopedSync(ClientPtr& client,
                       Path sourcePath,
                       CloudPath targetPath)
  : mClient(*client)
  , mContext(mClient.synchronize(sourcePath, std::move(targetPath)))
{
}

ScopedSync::~ScopedSync()
{
    // Get our hands on the sync's backup ID.
    auto id = std::get<0>(mContext);

    // Remove the sync if it was enabled.
    if (id != UNDEF)
        mClient.desynchronize(id);
}

Error ScopedSync::error() const
{
    return std::get<1>(mContext);
}

SyncError ScopedSync::syncError() const
{
    return std::get<2>(mContext);
}

} // testing
} // fuse
} // mega

