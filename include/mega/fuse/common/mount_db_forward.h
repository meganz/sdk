#pragma once

#include <mutex>

namespace mega
{
namespace fuse
{

class MountDB;

using MountDBLock = std::unique_lock<const MountDB>;

} // fuse
} // mega

