#pragma once

#include <memory>

namespace mega
{
namespace fuse
{
namespace platform
{

struct LocalDeleter
{
    void operator()(void* instance);
}; // LocalDeleter

template<typename T>
using LocalPtr = std::unique_ptr<T, LocalDeleter>;

} // platform
} // fuse
} // mega

