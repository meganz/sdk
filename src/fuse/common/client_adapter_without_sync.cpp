#include <mega/fuse/common/client_adapter.h>

namespace mega
{
namespace fuse
{

void ClientAdapter::desynchronize(mega::handle)
{
}

bool ClientAdapter::mountable(const NormalizedPath&) const
{
    return true;
}

auto ClientAdapter::synchronize(const NormalizedPath&, NodeHandle)
  -> std::tuple<mega::handle, Error, SyncError>
{
    return std::make_tuple(UNDEF, API_EFAILED, NO_SYNC_ERROR);
}

} // fuse
} // mega

