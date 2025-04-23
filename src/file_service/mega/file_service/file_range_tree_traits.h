#pragma once

#include <mega/file_service/file_range_traits.h>

namespace mega
{
namespace file_service
{
namespace detail
{

template<typename ValueType>
struct IsValidValueType: IsFileRange<ValueType>
{}; // IsValidValueType<ValueType>

template<typename FirstType, typename SecondType>
struct IsValidValueType<std::pair<FirstType, SecondType>>: IsFileRange<FirstType>
{}; // IsValidValueType<std::pair<FirstType, SecondType>>

template<typename ValueType>
constexpr auto IsValidValueTypeV = IsValidValueType<ValueType>::value;

} // detail
} // file_service
} // mega
