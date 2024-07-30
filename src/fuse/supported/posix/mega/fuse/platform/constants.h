#pragma once

#include <string>

#include <mega/fuse/common/constants.h>

namespace mega
{
namespace fuse
{
namespace platform
{

constexpr auto AttributeTimeout = 120.0;
constexpr auto EntryTimeout = 120.0;

extern const std::string FilesystemName;

} // platform
} // fuse
} // mega

