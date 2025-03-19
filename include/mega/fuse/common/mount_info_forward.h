#pragma once

#include <memory>
#include <set>
#include <vector>

namespace mega
{
namespace fuse
{

struct MountInfo;

using MountInfoPtr = std::unique_ptr<MountInfo>;

template<typename Comparator>
using MountInfoSet = std::set<MountInfo, Comparator>;

using MountInfoVector = std::vector<MountInfo>;

} // fuse
} // mega

