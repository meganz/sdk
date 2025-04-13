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

template<typename Class0, typename Class1, typename... Classes>
struct MostSpecificClass:
    MostSpecificClass<typename MostSpecificClass<Class0, Class1>::type, Classes...>
{}; // MostSpecificClass<Class0, Class1, Classes...>

template<typename Class, typename... Classes>
struct MostSpecificClass<NoneSuch, Class, Classes...>
{
    using type = NoneSuch;
}; // MostSpecificClass<NoneSuch, Class, Classes...>

template<typename Class0, typename Class1>
struct MostSpecificClass<Class0, Class1>
{
    using type =
        std::conditional_t<std::is_base_of_v<Class0, Class1>,
                           Class1,
                           std::conditional_t<std::is_base_of_v<Class1, Class0>, Class0, NoneSuch>>;
}; // MostSpecificClass

template<typename Class0, typename Class1, typename... Classes>
using MostSpecificClassT = typename MostSpecificClass<Class0, Class1, Classes...>::type;

} // file_service
} // mega
