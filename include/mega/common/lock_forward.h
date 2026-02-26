#pragma once

namespace mega
{
namespace common
{
namespace detail
{

template<typename T, typename Traits>
struct Lock;

template<typename T>
struct SharedLock;

template<typename T>
struct SharedLockTraits;

template<typename T>
struct UniqueLock;

template<typename T>
struct UniqueLockTraits;

} // detail

using detail::SharedLock;
using detail::UniqueLock;

} // common
} // mega

