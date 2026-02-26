#include <mega/common/task_queue.h>
#include <mega/fuse/common/client.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/mount_event.h>
#include <mega/fuse/common/mount_event_type.h>

#include <mega/megaapp.h>

namespace mega
{
namespace fuse
{

using namespace common;

void emitEvent(Client& client, const MountEvent& event)
{
    // Emit the event on the client thread.
    client.execute([&client, event](const Task& task) {
        // Client's being torn down.
        if (task.cancelled())
            return;

        FUSEDebugF("Emitting %s event", toString(event.mType));

        // Emit the event.
        client.application().onFuseEvent(event);
    });
}

} // fuse
} // mega

