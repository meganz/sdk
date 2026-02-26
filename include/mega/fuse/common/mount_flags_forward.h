#pragma once

#include <memory>

namespace mega
{
namespace fuse
{

struct MountFlags;

using MountFlagsPtr = std::unique_ptr<MountFlags>;

} // fuse
} // mega

