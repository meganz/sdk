#include "mega/impl/share.h"

#include "mega/share.h"

namespace mega
{
namespace impl
{

ShareData::ShareData(MegaHandle nodeHandle, const Share* share, bool verified):
    mNodeHandle(nodeHandle),
    mShare(share),
    mVerified(verified)
{}

MegaHandle ShareData::getNodeHandle() const
{
    return mNodeHandle;
}

const Share* ShareData::getShare() const
{
    return mShare;
}

bool ShareData::isVerified() const
{
    return mVerified;
}

} // namespace impl
} // namespace mega
