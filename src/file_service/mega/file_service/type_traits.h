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

} // file_service
} // mega
