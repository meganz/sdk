#pragma once

#include <type_traits>

namespace mega
{
namespace common
{

template<template<typename T> class P, typename U, typename... Us>
struct AllOf
  : std::conjunction<P<U>, P<Us>...>
{
}; // AllOf<P<T>, U, Us...>

template<template<typename T> class P, typename U, typename... Us>
struct AnyOf
  : std::disjunction<P<U>, P<Us>...>
{
}; // AnyOf<P<T>, U, Us...>

template<typename T, typename = void>
struct IsComplete
  : std::false_type
{
}; // IsComplete<T, void>

template<typename T>
struct IsComplete<T, std::void_t<decltype(sizeof(T))>>
  : std::true_type
{
}; // IsComplete<T, Void<decltype(sizeof(T))>::type>

template<typename T, typename U, typename... Us>
struct IsOneOf
  : std::disjunction<std::is_same<T, U>,
                     std::is_same<T, Us>...>
{
}; // IsOneOf<T, U, Us...>

template<typename T, typename U, typename... Us>
constexpr auto IsOneOfV = IsOneOf<T, U, Us...>::value;

} // common
} // mega

