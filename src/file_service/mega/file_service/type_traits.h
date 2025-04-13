#pragma once

#include <type_traits>

namespace mega
{
namespace file_service
{

enum class NoneSuch
{};

// Check if Type is NoneSuch.
template<typename Type>
using IsNoneSuch = std::is_same<NoneSuch, Type>;

template<typename Type>
static constexpr auto IsNoneSuchV = IsNoneSuch<Type>::value;

// Check if Type is not NoneSuch.
template<typename Type>
using IsNotNoneSuch = std::negation<IsNoneSuch<Type>>;

template<typename Type>
static constexpr auto IsNotNoneSuchV = IsNotNoneSuch<Type>::value;

namespace detail
{

template<typename DefaultType,
         typename Enabler,
         template<typename>
         typename Predicate,
         typename... Parameters>
struct Detected: std::false_type
{
    using type = DefaultType;
}; // Detected<DefaultType, Enabler, Predicate, Parameters...>

template<typename DefaultType, template<typename> typename Predicate, typename... Parameters>
struct Detected<DefaultType, std::void_t<Predicate<Parameters...>>, Predicate, Parameters...>:
    std::true_type
{
    using type = Predicate<Parameters...>;
}; // DefaultType<DefaultType, Predicate, Parameters...>

} // detail

template<template<typename> typename Predicate, typename... Parameters>
using Detected = detail::Detected<NoneSuch, void, Predicate, Parameters...>;

template<template<typename> typename Predicate, typename... Parameters>
using DetectedT = typename Detected<Predicate, Parameters...>::type;

template<template<typename> typename Predicate, typename... Parameters>
static constexpr auto DetectedV = Detected<Predicate, Parameters...>::value;

template<typename DefaultType, template<typename> typename Predicate, typename... Parameters>
using DetectedOr = detail::Detected<DefaultType, void, Predicate, Parameters...>;

template<typename DefaultType, template<typename> typename Predicate, typename... Parameters>
using DetectedOrT = typename DetectedOr<DefaultType, Predicate, Parameters...>::type;

template<typename DefaultType, template<typename> typename Predicate, typename... Parameters>
static constexpr auto DetectedOrV = DetectedOr<DefaultType, Predicate, Parameters...>::value;

} // file_service
} // mega
