#pragma once

#include <mega/common/badge_forward.h>

#include <memory>
#include <set>

namespace mega
{
namespace fuse
{
namespace platform
{

class Context;
using ContextBadge = common::Badge<Context>;
using ContextPtr = std::unique_ptr<Context>;
using ContextRawPtrSet = std::set<Context*>;

} // platform
} // fuse
} // mega
