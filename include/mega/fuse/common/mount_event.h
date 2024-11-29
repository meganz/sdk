#pragma once

#include <mega/fuse/common/mount_event_forward.h>
#include <mega/fuse/common/mount_event_type_forward.h>
#include <mega/fuse/common/mount_result_forward.h>

#include <mega/filesystem.h>

namespace mega
{
namespace fuse
{

struct MountEvent
{
    bool operator==(const MountEvent& rhs) const;

    bool operator!=(const MountEvent& rhs) const;

    LocalPath mPath;
    MountResult mResult;
    MountEventType mType;
}; // MountEvent

} // fuse
} // mega

