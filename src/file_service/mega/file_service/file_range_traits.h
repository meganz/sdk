#pragma once

#include <mega/file_service/file_range_forward.h>

#include <type_traits>

namespace mega
{
namespace file_service
{

template<typename Type>
using IsFileRange = std::is_same<std::remove_cv_t<Type>, FileRange>;

template<typename Type>
constexpr auto IsFileRangeV = IsFileRange<Type>::value;

} // file_service
} // mega
