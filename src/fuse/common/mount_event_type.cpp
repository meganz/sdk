#include <mega/fuse/common/mount_event_type.h>

namespace mega
{
namespace fuse
{

const char* toString(MountEventType type)
{
    switch (type)
    {
#define DEFINE_MOUNT_EVENT_TYPE_CLAUSE(name) case name: return #name;
        DEFINE_MOUNT_EVENT_TYPES(DEFINE_MOUNT_EVENT_TYPE_CLAUSE);
#undef DEFINE_MOUNT_EVENT_TYPE_CLAUSE
    }

    // Silence the compiler.
    return "N/A";
}

} // fuse
} // mega

