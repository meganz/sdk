#include <cassert>

#include <mega/fuse/common/mount_result.h>

namespace mega
{
namespace fuse
{

const char* toDescription(MountResult result)
{
#define DEFINE_MOUNT_RESULT_DESCRIPTIONS(name, description) description,
    static const char * const descriptions[] = {
        DEFINE_MOUNT_RESULTS(DEFINE_MOUNT_RESULT_DESCRIPTIONS)
    }; // descriptions
#undef DEFINE_MOUNT_RESULT_DESCRIPTIONS

    if (result < sizeof(descriptions))
        return descriptions[result];

    assert(false && "Unknown mount result type");

    return "N/A";
}

const char* toString(MountResult result)
{
#define DEFINE_MOUNT_RESULT_NAMES(name, _) #name,
    static const char * const names[] = {
        DEFINE_MOUNT_RESULTS(DEFINE_MOUNT_RESULT_NAMES)
    }; // names
#undef DEFINE_MOUNT_RESULT_NAMES

    if (result < sizeof(names))
        return names[result];

    assert(false && "Unknown mount result type");

    return "N/A";
}

} // fuse
} // mega

