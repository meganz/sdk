#pragma once

#include <mega/common/client.h>
#include <mega/fuse/common/mount_event_forward.h>

namespace mega
{
namespace fuse
{

// Emit a FUSE event.
void emitEvent(common::Client& client, const MountEvent& event);

} // fuse
} // mega

