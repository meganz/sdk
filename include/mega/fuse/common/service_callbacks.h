#pragma once

#include <functional>
#include <string>

#include <mega/fuse/common/mount_result_forward.h>

namespace mega
{
namespace fuse
{

using AbortPredicate =
    std::function<bool(const std::string&)>;

using MountDisabledCallback =
  std::function<void(MountResult)>;

} // fuse
} // mega

