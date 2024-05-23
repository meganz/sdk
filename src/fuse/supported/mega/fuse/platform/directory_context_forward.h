#pragma once

#include <memory>

namespace mega
{
namespace fuse
{
namespace platform
{

class DirectoryContext;

using DirectoryContextPtr = std::unique_ptr<DirectoryContext>;

} // platform
} // fuse
} // mega

