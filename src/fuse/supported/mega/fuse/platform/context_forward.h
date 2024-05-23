#pragma once

#include <memory>
#include <set>

#include <mega/fuse/common/badge_forward.h>

namespace mega
{
namespace fuse
{
namespace platform
{

class Context;
using ContextBadge = Badge<Context>;
using ContextPtr = std::unique_ptr<Context>;
using ContextRawPtrSet = std::set<Context*>;

} // platform
} // fuse
} // mega

