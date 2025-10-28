#pragma once

#include <chrono>
#include <cstdint>
#include <ratio>

namespace mega
{
namespace common
{

using deciseconds = std::chrono::duration<std::int64_t, std::deci>;

} // common
} // mega
