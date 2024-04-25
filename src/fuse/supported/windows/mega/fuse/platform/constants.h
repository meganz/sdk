#pragma once

#include <string>

#include <mega/fuse/common/constants.h>
#include <mega/fuse/platform/library.h>

namespace mega
{
namespace fuse
{
namespace platform
{

constexpr auto MaxMountNameLength =
  FSP_FSCTL_VOLUME_NAME_SIZE / sizeof(wchar_t);

constexpr auto MaxVolumePrefixLength =
  FSP_FSCTL_VOLUME_PREFIX_SIZE / sizeof(wchar_t);

extern const std::wstring UNCPrefix;

} // platform
} // fuse
} // mega

