#pragma once

#include <tuple>

#include <mega/fuse/common/inode_forward.h>
#include <mega/fuse/common/inode_info_forward.h>

namespace mega
{
namespace fuse
{

using MakeInodeResult = std::tuple<InodeRef, InodeInfo>;

} // fuse
} // mega

