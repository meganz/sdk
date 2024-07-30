#pragma once

namespace mega
{
namespace fuse
{
namespace detail
{

template<typename T, typename... Ts>
struct AreTimeValues;

class DateTime;

template<typename T>
struct IsTimeValue;

template<typename T>
struct TimeValueTraits;

} // detail

template<typename T, typename... Ts>
using AreTimeValues = detail::AreTimeValues<T, Ts...>;

using DateTime = detail::DateTime;

template<typename T>
using IsTimeValue = detail::IsTimeValue<T>;

} // fuse
} // mega

