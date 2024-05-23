#pragma once

#include <type_traits>

namespace mega
{
namespace fuse
{
namespace detail
{

template<template<typename T> class P,
         typename U,
         typename V,
         typename... Vs>
struct AllOf
  : std::conditional<bool(U::value),
                     AllOf<P, P<V>, Vs...>,
                     U>::type
{
}; // AllOf<P<T>, U, V, Vs...>

template<template<typename T> class P,
         typename U,
         typename V>
struct AllOf<P, U, V>
  : std::conditional<bool(U::value), P<V>, U>::type
{
}; // AllOf<P<T>, U>

template<template<typename T> class P,
         typename U,
         typename V,
         typename... Vs>
struct AnyOf
  : std::conditional<bool(U::value),
                     U,
                     AnyOf<P, P<V>, Vs...>>::type
{
}; // AnyOf<P<T>, U, V, Vs...>

template<template<typename T> class P,
         typename U,
         typename V>
struct AnyOf<P, U, V>
  : std::conditional<bool(U::value), U, P<V>>::type
{
}; // AnyOf<P<T>, U, V>

} // detail

template<template<typename T> class P, typename U, typename... Us>
using AllOf =
  detail::AllOf<P, P<U>, Us...>;

template<template<typename T> class P, typename U, typename... Us>
using AnyOf =
  detail::AnyOf<P, P<U>, Us...>;

template<typename... Ts>
struct Void
{
    using type = void;
}; // Void<Ts...>

template<typename T, typename = void>
struct IsComplete
  : std::false_type
{
}; // IsComplete<T, void>

template<typename T>
struct IsComplete<T, typename Void<decltype(sizeof(T))>::type>
  : std::true_type
{
}; // IsComplete<T, Void<decltype(sizeof(T))>::type>

template<typename T, typename U, typename... Us>
struct IsOneOf
  : IsOneOf<T, Us...>
{
}; // IsOneOf<T, U, Us...>

template<typename T, typename U, typename... Us>
struct IsOneOf<T, T, U, Us...>
  : std::true_type
{
}; // IsOneOf<T, T, U, Us...>

template<typename T, typename U>
struct IsOneOf<T, U>
  : std::is_same<T, U>
{
}; // IsOneOf<T, U>

} // fuse
} // mega

