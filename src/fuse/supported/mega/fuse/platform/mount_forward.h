#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

namespace mega
{
namespace fuse
{
namespace platform
{

class Mount;

using MountLock = std::unique_lock<Mount>;
using MountPtr = std::shared_ptr<Mount>;
using MountPtrVector = std::vector<MountPtr>;
using MountPtrSet = std::set<MountPtr>;
using MountWeakPtr = std::weak_ptr<Mount>;

template<typename T>
using ToMountPtrMap = std::map<T, MountPtr>;

template<typename T>
using ToMountPtrSetMap = std::map<T, MountPtrSet>;

} // platform
} // fuse
} // mega

