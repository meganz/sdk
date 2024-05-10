#pragma once

#include <memory>

namespace mega
{
namespace fuse
{

struct ServiceFlags;

using ServiceFlagsPtr = std::unique_ptr<ServiceFlags>;

} // fuse
} // mega

