#pragma once

#include <mega/fuse/common/mount_event_type_forward.h>

namespace mega
{
namespace fuse
{

#define DEFINE_MOUNT_EVENT_TYPES(expander) \
    /* A mount was being added. */ \
    expander(MOUNT_ADDED) \
    /* A mount's flags were being changed. */ \
    expander(MOUNT_CHANGED) \
    /* A mount was being disabled. */ \
    expander(MOUNT_DISABLED) \
    /* A mount was being enabled. */ \
    expander(MOUNT_ENABLED) \
    /* A mount was be being removed. */ \
    expander(MOUNT_REMOVED)

enum MountEventType : unsigned int
{
#define DEFINE_MOUNT_EVENT_TYPE_ENUMERANT(name) name,
    DEFINE_MOUNT_EVENT_TYPES(DEFINE_MOUNT_EVENT_TYPE_ENUMERANT)
#undef DEFINE_MOUNT_EVENT_TYPE_ENUMERANT
}; // MountEventType

const char* toString(MountEventType type);

} // fuse
} // mega

