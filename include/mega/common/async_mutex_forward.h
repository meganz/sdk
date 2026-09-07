#pragma once

#include <mega/common/scoped_async_lock_forward.h>

#include <type_traits>

namespace mega
{
namespace common
{
namespace detail
{

template<typename ExecutionPolicy>
class AsyncMutex;

template<typename T>
struct IsAsyncMutex: std::false_type
{}; // IsAsyncMutex<T>

template<typename ExecutionPolicy>
struct IsAsyncMutex<AsyncMutex<ExecutionPolicy>>: std::true_type
{}; // IsAsyncMutex<AsyncMutex<ExecutionPolicy>>

template<typename T>
constexpr auto IsAsyncMutexV = IsAsyncMutex<T>::value;

template<typename Callback,
         template<typename>
         typename Lock,
         typename Mutex,
         typename Voider = void>
struct IsLockCallback;

template<typename Callback, template<typename> typename Lock, typename Mutex>
constexpr auto IsLockCallbackV = IsLockCallback<Callback, Lock, Mutex>::value;

// True if callback is a valid shared lock callback.
template<typename Callback, typename Mutex>
constexpr auto IsSharedLockCallbackV = IsLockCallbackV<Callback, ScopedAsyncSharedLock, Mutex>;

// True if callback is a valid shared lock callback.
template<typename Callback, typename Mutex>
constexpr auto IsUniqueLockCallbackV = IsLockCallbackV<Callback, ScopedAsyncUniqueLock, Mutex>;

} // detail

using detail::AsyncMutex;

} // common
} // mega
