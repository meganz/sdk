#pragma once

#include <mega/fuse/common/mount_result_forward.h>

namespace mega
{
namespace fuse
{

#define DEFINE_MOUNT_RESULTS(expander) \
    expander(MOUNT_ABORTED,             "The operation was aborted due to client shutdown") \
    expander(MOUNT_BACKEND_UNAVAILABLE, "FUSE is supported but the backend isn't installed") \
    expander(MOUNT_BUSY,                "The mount's busy and cannot be disabled") \
    expander(MOUNT_FAILED,              "A mount has encountered an expected failure and has been disabled") \
    expander(MOUNT_LOCAL_EXISTS,        "Mount target already exists") \
    expander(MOUNT_LOCAL_FILE,          "Mount target doesn't denote a directory") \
    expander(MOUNT_LOCAL_SYNCING,       "Mount target is being synchronized") \
    expander(MOUNT_LOCAL_TAKEN,         "A mount's already associated with the target path") \
    expander(MOUNT_LOCAL_UNKNOWN,       "Mount target doesn't exist") \
    expander(MOUNT_NAME_TAKEN,          "A mount already exists with a specified name") \
    expander(MOUNT_NAME_TOO_LONG,       "The mount's name is too long") \
    expander(MOUNT_NO_NAME,             "No name has been specified for a mount") \
    expander(MOUNT_REMOTE_FILE,         "Mount source doesn't describe a directory") \
    expander(MOUNT_REMOTE_UNKNOWN,      "Mount source doesn't exist") \
    expander(MOUNT_SUCCESS,             "Mount was successful") \
    expander(MOUNT_UNEXPECTED,          "Encountered an unexpected error while mounting") \
    expander(MOUNT_UNKNOWN,             "No mount is associated with the specified handle or path") \
    expander(MOUNT_UNSUPPORTED,         "FUSE isn't supported on this platform") \


enum MountResult : unsigned int
{
#define DEFINE_MOUNT_RESULT_ENUMERANT(name, description) name,
    DEFINE_MOUNT_RESULTS(DEFINE_MOUNT_RESULT_ENUMERANT)
#undef DEFINE_MOUNT_RESULT_ENUMERANT
}; // MountResult

const char* toDescription(MountResult result);
const char* toString(MountResult result);

} // fuse
} // mega

