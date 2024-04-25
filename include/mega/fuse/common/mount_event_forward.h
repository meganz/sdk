#pragma once

#include <deque>

namespace mega
{
namespace fuse
{

struct MountEvent;

using MountEventQueue = std::deque<MountEvent>;

} // fuse
} // mega

