#pragma once

#include <memory>
#include <vector>

namespace mega
{
namespace fuse
{

struct MountInfo;

using MountInfoPtr = std::unique_ptr<MountInfo>;
using MountInfoVector = std::vector<MountInfo>;

} // fuse
} // mega

