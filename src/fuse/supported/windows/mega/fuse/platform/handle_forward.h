#pragma once

namespace mega
{
namespace fuse
{
namespace platform
{

struct DefaultHandleDeleter;

template<typename Deleter = DefaultHandleDeleter>
class Handle;

} // platform
} // fuse
} // mega

