#pragma once

namespace mega
{
namespace common
{
namespace platform
{

struct DefaultHandleDeleter;

template<typename Deleter = DefaultHandleDeleter>
class Handle;

} // platform
} // common
} // mega
