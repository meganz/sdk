#include <mega/fuse/common/mount_result.h>

namespace mega
{
namespace fuse
{

const char* toString(MountResult result)
{
    static const char * const sValues [] {
        #define SOME_GENERATOR_MACRO(name, _) #name,
            DEFINE_MOUNT_RESULTS(SOME_GENERATOR_MACRO)
        #undef SOME_GENERATOR_MACRO
    };
    if (result >= sizeof(sValues))
    {
        return "N/A";
    }

    return sValues[result];
}

const char* getDescriptionString(MountResult result)
{
    static const char * const sValues [] {
#define SOME_GENERATOR_MACRO(name, description) description,
    DEFINE_MOUNT_RESULTS(SOME_GENERATOR_MACRO)
#undef SOME_GENERATOR_MACRO
    };
    if (result >= sizeof(sValues))
    {
        return "N/A";
    }

    return sValues[result];
}

} // fuse
} // mega

