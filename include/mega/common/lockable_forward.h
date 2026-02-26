#pragma once

namespace mega
{
namespace common
{

template<typename Derived>
class Lockable;

template<typename T>
struct LockableTraits;

template<typename T, typename LockType>
struct LockableTraitsCommon;

} // common
} // mega

