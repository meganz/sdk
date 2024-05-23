#pragma once

#include <map>
#include <set>

namespace mega
{
namespace fuse
{
namespace platform
{

class Session;

template<typename T>
using FromSessionRawPtrMap = std::map<Session*, T>;

using SessionRawPtrSet = std::set<Session*>;

} // platform
} // fuse
} // mega

