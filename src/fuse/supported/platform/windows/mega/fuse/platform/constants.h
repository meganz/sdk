#pragma once

#include <mega/fuse/common/constants.h>
#include <mega/fuse/platform/library.h>

#include <string>

namespace mega
{
namespace fuse
{
namespace platform
{

constexpr auto MaxMountNameLength = 32u;

constexpr auto MaxVolumePrefixLength = FSP_FSCTL_VOLUME_PREFIX_SIZE / sizeof(wchar_t);

extern const std::wstring UNCPrefix;

} // platform
} // fuse
} // mega
