#include <mega/common/async_mutex.h>
#include <mega/common/async_mutex_locker.h>
#include <mega/common/scoped_async_lock.h>
#include <mega/common/sequential_execution_policy.h>

namespace mega
{
namespace common
{
namespace testing
{

// Convenience.
using AsyncMutex = common::AsyncMutex<SequentialExecutionPolicy>;

using detail::IsAsyncLockerV;
using detail::IsAsyncMutexV;
using detail::IsLockAllCallbackV;

template<typename T>
constexpr auto IsSharedLockCallbackV = detail::IsSharedLockCallbackV<T, AsyncMutex>;

template<typename T>
constexpr auto IsUniqueLockCallbackV = detail::IsUniqueLockCallbackV<T, AsyncMutex>;

using ScopedSharedLock = ScopedAsyncSharedLock<AsyncMutex>;
using ScopedUniqueLock = ScopedAsyncUniqueLock<AsyncMutex>;

using SharedLocker = AsyncSharedLocker<AsyncMutex>;
using UniqueLocker = AsyncUniqueLocker<AsyncMutex>;

// A valid async locker for some mutex is either:
// - AsyncSharedLocker<Mutex>
// - AsyncUniqueLocker<Mutex>
static_assert(IsAsyncLockerV<SharedLocker>);
static_assert(IsAsyncLockerV<UniqueLocker>);
static_assert(!IsAsyncLockerV<int>);

// A valid async mutex is AsyncMutex<ExecutionPolicy>.
static_assert(IsAsyncMutexV<AsyncMutex>);
static_assert(!IsAsyncMutexV<int>);

// A lock callback is valid if it accepts a scoped lock by rvalue reference.
static_assert(IsSharedLockCallbackV<std::function<void(ScopedSharedLock&&)>>);
static_assert(IsUniqueLockCallbackV<std::function<void(ScopedUniqueLock&&)>>);

// A lock callback is valid if it accepts a scoped lock by value.
static_assert(IsSharedLockCallbackV<std::function<void(ScopedSharedLock)>>);
static_assert(IsUniqueLockCallbackV<std::function<void(ScopedUniqueLock)>>);

// A lock callback is valid only if it accepts the correct scoped lock type.
static_assert(!IsSharedLockCallbackV<std::function<void(ScopedUniqueLock)>>);
static_assert(!IsUniqueLockCallbackV<std::function<void(ScopedSharedLock)>>);

// A lock callback is valid only if it returns void.
static_assert(!IsSharedLockCallbackV<std::function<bool(ScopedSharedLock)>>);
static_assert(!IsUniqueLockCallbackV<std::function<bool(ScopedUniqueLock)>>);

// A lock callback must be an invocable object.
struct DummyLockCallback
{
    template<typename Lock>
    void operator()(Lock&&)
    {}
}; // DummyLockCallback

static_assert(IsUniqueLockCallbackV<DummyLockCallback>);
static_assert(IsSharedLockCallbackV<DummyLockCallback>);
static_assert(!IsSharedLockCallbackV<int>);
static_assert(!IsUniqueLockCallbackV<int>);

// A lock-all callback is valid if it accepts scoped locks by rvalue reference.
static_assert(IsLockAllCallbackV<std::function<void(ScopedSharedLock&&, ScopedUniqueLock&&)>,
                                 SharedLocker,
                                 UniqueLocker>);

// A lock-all callback is valid if it accepts scoped locks by value.
static_assert(IsLockAllCallbackV<std::function<void(ScopedSharedLock, ScopedUniqueLock)>,
                                 SharedLocker,
                                 UniqueLocker>);

// A lock-all callback is valid only if it accepts locks of the correct type.
static_assert(!IsLockAllCallbackV<std::function<void(ScopedUniqueLock, ScopedSharedLock)>,
                                  SharedLocker,
                                  UniqueLocker>);

// A lock-all callback is valid only if accepts the corect number of locks.
static_assert(
    !IsLockAllCallbackV<std::function<void(ScopedSharedLock)>, SharedLocker, UniqueLocker>);

// A lock-all callback is only valid if it returns void.
static_assert(!IsLockAllCallbackV<std::function<bool(ScopedSharedLock)>, SharedLocker>);

// A lock-all callback must be an invocable object.
struct DummyLockAllCallback
{
    template<typename Lock, typename... Locks>
    void operator()(Lock&&, Locks&&...)
    {}
}; // DummyLockAllCallback

static_assert(IsLockAllCallbackV<DummyLockAllCallback, SharedLocker>);
static_assert(IsLockAllCallbackV<DummyLockAllCallback, SharedLocker, UniqueLocker>);
static_assert(!IsLockAllCallbackV<int, SharedLocker>);

} // testing
} // common
} // mega
