#pragma once

#include <type_traits>
#include <utility>

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
constexpr auto IsNoneSuchV = IsNoneSuch<Type>::value;

// Check if Type is not NoneSuch.
template<typename Type>
using IsNotNoneSuch = std::negation<IsNoneSuch<Type>>;

template<typename Type>
constexpr auto IsNotNoneSuchV = IsNotNoneSuch<Type>::value;

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
constexpr auto DetectedV = Detected<Predicate, Parameters...>::value;

template<typename DefaultType, template<typename> typename Predicate, typename... Parameters>
using DetectedOr = detail::Detected<DefaultType, void, Predicate, Parameters...>;

template<typename DefaultType, template<typename> typename Predicate, typename... Parameters>
using DetectedOrT = typename DetectedOr<DefaultType, Predicate, Parameters...>::type;

template<typename DefaultType, template<typename> typename Predicate, typename... Parameters>
constexpr auto DetectedOrV = DetectedOr<DefaultType, Predicate, Parameters...>::value;

// Return value as-is.
struct Identity
{
    template<typename T>
    auto&& operator()(T&& value) const
    {
        return std::forward<T>(value);
    }
}; // Identity

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

template<typename Type>
struct MemberPointerTraits: std::false_type
{}; // MemberPointerTraits<Type>

template<typename Class, typename Member>
struct MemberPointerTraits<Member Class::*>: std::true_type
{
    using class_type = Class;
    using member_type = Member;
}; // MemberPointerTraits<Class Member::*>

template<typename Class, typename Member>
struct MemberPointerTraits<Member Class::* const>: std::true_type
{
    using class_type = Class;
    using member_type = Member;
}; // MemberPointerTraits<Class Member::* const>

template<typename Type>
using RemoveCVRef = std::remove_cv<std::remove_reference_t<Type>>;

template<typename Type>
using RemoveCVRefT = typename RemoveCVRef<Type>::type;

} // file_service
} // mega
