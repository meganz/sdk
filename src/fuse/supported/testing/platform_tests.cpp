#include <mega/fuse/platform/testing/platform_tests.h>

namespace mega
{
namespace fuse
{
namespace testing
{

using ::testing::TestParamInfo;
using ::testing::Values;

INSTANTIATE_TEST_SUITE_P(
    ,
    FUSEPlatformTests,
    Values(SHARED_UNVERSIONED,
           SHARED_VERSIONED,
           STANDARD_UNVERSIONED,
           STANDARD_VERSIONED),
    [](const TestParamInfo<Parameters>& info) {
        return toString(info.param);
    });

} // testing
} // fuse
} // mega

