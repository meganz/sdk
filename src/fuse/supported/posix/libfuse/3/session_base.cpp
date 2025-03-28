#include <mega/fuse/platform/session_base.h>

namespace mega
{
namespace fuse
{
namespace platform
{

void SessionBase::SessionDeleter::operator()(fuse_session* session)
{
    if (!session)
        return;

    fuse_session_unmount(session);
    fuse_session_destroy(session);
}

} // platform
} // fuse
} // mega

