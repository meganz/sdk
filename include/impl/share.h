#pragma once

#include "megaapi.h"

namespace mega
{

struct Share;

namespace impl
{

class ShareData
{
public:
    ShareData(MegaHandle nodeHandle, const Share* share, bool verified);

    MegaHandle getNodeHandle() const;

    const Share* getShare() const;

    bool isVerified() const;

private:
    MegaHandle mNodeHandle;

    const Share* mShare;

    bool mVerified;
};

} // namespace impl
} // namespace mega
