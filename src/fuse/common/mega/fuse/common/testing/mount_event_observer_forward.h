#pragma once

#include <memory>
#include <set>

namespace mega
{
namespace fuse
{
namespace testing
{

class MountEventObserver;

using MountEventObserverPtr = std::shared_ptr<MountEventObserver>;
using MountEventObserverWeakPtr = std::weak_ptr<MountEventObserver>;

using MountEventObserverWeakPtrSet =
  std::set<MountEventObserverWeakPtr,
           std::owner_less<MountEventObserverWeakPtr>>;

} // testing
} // fuse
} // mega

