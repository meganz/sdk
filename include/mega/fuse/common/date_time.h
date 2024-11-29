#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include <mega/types.h>

#include <mega/fuse/common/date_time_forward.h>
#include <mega/fuse/common/type_traits.h>

namespace mega
{
namespace fuse
{
namespace detail
{

template<bool value>
using BoolConstant = std::integral_constant<bool, value>;

template<typename T, typename... Ts>
struct AreTimeValues
  : BoolConstant<IsTimeValue<T>::value && IsTimeValue<Ts...>::value>
{
}; // AreTimeValues<T, Ts...>

class DateTime
{
    std::uint64_t mValue = 0u;

public:
    DateTime() = default;

    DateTime(const DateTime& other) = default;

    template<typename T,
             typename U = IsTimeValue<T>,
             typename V = typename std::enable_if<U::value>::type>
    DateTime(const T& other)
      : mValue(TimeValueTraits<T>::from(other))
    {
    }

    DateTime& operator=(const DateTime& rhs) = default;

    template<typename T,
             typename U = IsTimeValue<T>,
             typename V = typename std::enable_if<U::value>::type>
    DateTime& operator=(const T& rhs)
    {
        return operator=(DateTime(rhs));
    }

    bool operator==(const DateTime& rhs) const;

    template<typename T,
             typename U = IsTimeValue<T>,
             typename V = typename std::enable_if<U::value>::type>
    bool operator==(const T& rhs) const
    {
        return operator==(DateTime(rhs));
    }

    bool operator!=(const DateTime& rhs) const;

    template<typename T,
             typename U = IsTimeValue<T>,
             typename V = typename std::enable_if<U::value>::type>
    bool operator!=(const T& rhs) const
    {
        return operator!=(DateTime(rhs));
    }

    template<typename T>
    operator T() const
    {
        return asValue<T>();
    }

    template<typename T,
             typename U = IsTimeValue<T>,
             typename V = typename std::enable_if<U::value>::type>
    T asValue() const
    {
        return TimeValueTraits<T>::to(mValue);
    }
}; // DateTime

template<typename T>
struct IsTimeValue
  : IsComplete<TimeValueTraits<T>>
{
}; // IsTimeValue<T>

template<>
struct TimeValueTraits<m_time_t>
{
    static std::uint64_t from(m_time_t value)
    {
        return static_cast<std::uint64_t>(value);
    }

    static m_time_t to(std::uint64_t value)
    {
        return static_cast<m_time_t>(value);
    }
}; // TimeValueTraits<m_time_t>

// Convenience.
using SystemClock = std::chrono::system_clock;
using SystemTime = SystemClock::time_point;

template<>
struct TimeValueTraits<SystemTime>
{
    static std::uint64_t from(const SystemTime& value)
    {
        return static_cast<std::uint64_t>(SystemClock::to_time_t(value));
    }

    static SystemTime to(std::uint64_t value)
    {
        return SystemClock::from_time_t(static_cast<time_t>(value));
    }
}; // TimeValueTraits<SystemTime>

std::string toString(const DateTime& value);

} // detail

} // fuse
} // mega

