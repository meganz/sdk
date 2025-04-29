#pragma once

#include <mega/common/unexpected_forward.h>

#include <type_traits>
#include <utility>

namespace mega
{
namespace common
{

namespace detail
{

template<typename T>
struct IsUnexpected: std::false_type
{}; // IsUnexpected<T>

template<typename T>
struct IsUnexpected<Unexpected<T>>: std::true_type
{}; // IsUnexpected<Unexpected<T>>

} // detail

template<typename T>
class Unexpected
{
    // The error value we're wrapping.
    T mValue;

public:
    Unexpected(T&& value):
        mValue(std::move(value))
    {}

    Unexpected(const T& value):
        mValue(value)
    {}

    T& value() &
    {
        return mValue;
    }

    T&& value() &&
    {
        return std::move(mValue);
    }

    const T& value() const&
    {
        return mValue;
    }

    const T&& value() const&&
    {
        return std::move(mValue);
    }
}; // Unexpected<T>

template<typename T>
struct IsUnexpected: detail::IsUnexpected<std::decay_t<T>>
{}; // IsUnexpected<T>

// For convenience.
template<typename T>
auto unexpected(T&& value)
{
    return Unexpected(std::forward<T>(value));
}

} // common
} // mega
