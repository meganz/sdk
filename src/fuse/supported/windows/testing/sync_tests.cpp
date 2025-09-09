#include <mega/fuse/common/testing/cloud_path.h>
#include <mega/fuse/common/testing/directory.h>
#include <mega/fuse/common/testing/sync_tests.h>

namespace mega
{
namespace fuse
{
namespace testing
{

TEST_F(FUSESyncTests, can_sync_when_an_empty_path_mount_is_on)
{
    Directory empty("");

    Directory sd0("sd0", mScratchPath);

    // Try and mount s.
    ScopedMount ms(ClientW(), "empty", empty.path(), "x/s");

    // Mount should succeed.
    ASSERT_EQ(ms.result(), MOUNT_SUCCESS);

    // Try sync s/sd0.
    ScopedSync ssd0(ClientW(), sd0.path(), "x/s/sd0");

    // Attempted sync should succeed
    ASSERT_EQ(ssd0.error(), API_OK);
    ASSERT_EQ(ssd0.syncError(), NO_SYNC_ERROR);
}

TEST_F(FUSESyncTests, can_mount_empty_path_when_sync_is_on)
{
    Directory empty("");

    Directory sd0("sd0", mScratchPath);

    // Sync s/sd0.
    ScopedSync ssd0(ClientW(), sd0.path(), "x/s/sd0");

    ASSERT_EQ(ssd0.error(), API_OK);
    ASSERT_EQ(ssd0.syncError(), NO_SYNC_ERROR);

    // Try and mount s.
    ScopedMount ms(ClientW(), "empty", empty.path(), "x/s");

    // Attempted mount should succeed.
    ASSERT_EQ(ms.result(), MOUNT_SUCCESS);
}

} // testing
} // fuse
} // mega
