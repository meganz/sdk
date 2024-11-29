#pragma once

#include <mutex>

namespace mega
{
namespace fuse
{

class InodeDB;

using InodeDBLock = std::unique_lock<const InodeDB>;

} // fuse
} // mega

