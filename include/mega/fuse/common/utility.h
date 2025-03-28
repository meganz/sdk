#pragma once

#include <chrono>
#include <cstdarg>
#include <future>
#include <memory>

#include <mega/fuse/common/error_or.h>

#include <mega/types.h>

namespace mega
{
namespace fuse
{

std::chrono::minutes defaultTimeout();

std::string format(const char* format, ...);

std::string formatv(std::va_list arguments, const char* format);

template<typename T>
using SharedPromise = std::shared_ptr<std::promise<T>>;

template<typename T>
SharedPromise<T> makeSharedPromise()
{
    return std::make_shared<std::promise<T>>();
}

template<typename T>
auto waitFor(std::future<T> future)
  -> typename std::enable_if<IsErrorLike<T>::value, T>::type
{
    // Wait for the future's promise to transmit a value.
    auto status = future.wait_for(defaultTimeout());

    // Promise didn't transmit a value in time.
    if (status == std::future_status::timeout)
    {
        if constexpr (IsErrorOr<T>::value)
            return unexpected(LOCAL_ETIMEOUT);
        else
            return LOCAL_ETIMEOUT;
    }

    // Return the promise's value to the caller.
    return future.get();
}

} // fuse
} // mega

