#pragma once

#include <utility>

namespace mega
{
namespace fuse
{
namespace platform
{

class FileDescriptor;

using FileDescriptorPair =
  std::pair<FileDescriptor, FileDescriptor>;

} // platform
} // fuse
} // mega

