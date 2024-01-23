#pragma once

#include <memory>

namespace mega
{
namespace fuse
{
namespace platform
{

class FileContext;

using FileContextPtr = std::unique_ptr<FileContext>;

} // platform
} // fuse
} // mega

