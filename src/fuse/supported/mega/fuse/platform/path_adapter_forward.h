#pragma once

#include <mega/fuse/common/path_adapter_forward.h>

namespace mega
{
namespace fuse
{
namespace platform
{
namespace detail
{

struct PathAdapterTraits;

} // detail

using PathAdapter =
  fuse::detail::PathAdapter<detail::PathAdapterTraits>;

} // platform
} // fuse
} // mega

