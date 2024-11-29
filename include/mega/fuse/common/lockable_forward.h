#pragma once

namespace mega
{
namespace fuse
{

template<typename Derived>
class Lockable;

template<typename T>
struct LockableTraits;

template<typename T, typename LockType>
struct LockableTraitsCommon;

} // fuse
} // mega

