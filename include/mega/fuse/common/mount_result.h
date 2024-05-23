#pragma once

#include <mega/fuse/common/mount_result_forward.h>

namespace mega
{
namespace fuse
{

#define DEFINE_MOUNT_RESULTS(expander) \
    /* The operation was aborted due to client shutdown. */ \
    expander(MOUNT_ABORTED) \
    /* FUSE is supported but the backend isn't installed. */ \
    expander(MOUNT_BACKEND_UNAVAILABLE) \
    /* The mount's busy and cannot be disabled. */ \
    expander(MOUNT_BUSY) \
    /* A mount's already associated with the target path. */ \
    expander(MOUNT_EXISTS) \
    /* A mount has encountered an expected failure and has been disabled. */ \
    expander(MOUNT_FAILED) \
    /* Mount target already exists. */ \
    expander(MOUNT_LOCAL_EXISTS) \
    /* Mount target doesn't denote a directory. */ \
    expander(MOUNT_LOCAL_FILE) \
    /* Mount target is being synchronized. */ \
    expander(MOUNT_LOCAL_SYNCING) \
    /* Mount target doesn't exist. */ \
    expander(MOUNT_LOCAL_UNKNOWN) \
    /* A mount already exists with a specified name. */ \
    expander(MOUNT_NAME_TAKEN) \
    /* The mount's name is too long. */ \
    expander(MOUNT_NAME_TOO_LONG) \
    /* No name has been specified for a mount. */ \
    expander(MOUNT_NO_NAME) \
    /* Mount source doesn't describe a directory. */ \
    expander(MOUNT_REMOTE_FILE) \
    /* Mount source doesn't exist. */ \
    expander(MOUNT_REMOTE_UNKNOWN) \
    /* Mount was successful. */ \
    expander(MOUNT_SUCCESS) \
    /* Encountered an unexpected error while mounting. */ \
    expander(MOUNT_UNEXPECTED) \
    /* No mount is associated with the specified handle or path. */ \
    expander(MOUNT_UNKNOWN) \
    /* FUSE isn't supported on this platform. */ \
    expander(MOUNT_UNSUPPORTED)

enum MountResult : unsigned int
{
#define DEFINE_MOUNT_RESULT_ENUMERANT(name) name,
    DEFINE_MOUNT_RESULTS(DEFINE_MOUNT_RESULT_ENUMERANT)
#undef DEFINE_MOUNT_RESULT_ENUMERANT
}; // MountResult

const char* toString(MountResult result);

} // fuse
} // mega

