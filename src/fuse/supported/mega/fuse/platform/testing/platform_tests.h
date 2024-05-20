#pragma once

#include <mega/fuse/common/testing/parameters.h>
#include <mega/fuse/common/testing/test_base.h>

namespace mega
{
namespace fuse
{
namespace testing
{

struct FUSEPlatformTests
  : TestBase
  , ::testing::WithParamInterface<Parameters>
{
    #define DEFINE_CLIENT_ACCESSOR(accessor, name) \
        static ClientPtr& Client##name() \
        { \
            return (*GetParam().mClients.m##accessor)(); \
        }

    DEFINE_CLIENT_ACCESSOR(ReadOnly,  RS);
    DEFINE_CLIENT_ACCESSOR(ReadWrite, WS);

    #undef DEFINE_CLIENT_ACCESSOR

    #define DEFINE_MOUNT_PATH_ACCESSOR(accessor, name) \
        static const Path& MountPath##name() \
        { \
            return (*GetParam().mPaths.m##accessor)(); \
        }

    DEFINE_MOUNT_PATH_ACCESSOR(Observer,  O);
    DEFINE_MOUNT_PATH_ACCESSOR(ReadOnly,  R);
    DEFINE_MOUNT_PATH_ACCESSOR(ReadWrite, W);

    #undef DEFINE_MOUNT_PATH_ACCESSOR

    // Perform instance-specific setup.
    void SetUp() override
    {
        ASSERT_TRUE(DoSetUp(GetParam()));
    }
}; // FUSEPlatformTests

} // testing
} // fuse
} // mega

