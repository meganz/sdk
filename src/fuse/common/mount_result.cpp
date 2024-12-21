#include <mega/fuse/common/mount_result.h>

namespace mega
{
namespace fuse
{

const char* toString(MountResult result)
{
    switch (result)
    {
#define DEFINE_MOUNT_RESULT_CLAUSE(name) case name: return #name;
        DEFINE_MOUNT_RESULTS(DEFINE_MOUNT_RESULT_CLAUSE);
#undef  DEFINE_MOUNT_RESULT_CLAUSE
    }

    // Here only to silence the compiler.
    return "N/A";
}

} // fuse
} // mega

