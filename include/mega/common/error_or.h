#pragma once

#include <mega/common/error_or_forward.h>
#include <mega/common/expected.h>
#include <mega/types.h>

#include <type_traits>
#include <utility>

namespace mega
{
namespace common
{
namespace detail
{

// Check if T is an error type.
template<typename T>
struct IsError
  : std::false_type
{
}; // IsError<T>

template<>
struct IsError<Error>
  : std::true_type
{
}; // IsError<Error>

template<>
struct IsError<ErrorCodes>
  : std::true_type
{
}; // IsError<ErrorCodes>

// Checks if T is an ErrorOr type.
template<typename T>
struct IsErrorOr
  : std::false_type
{
}; // IsErrorOr<T>

template<typename T>
struct IsErrorOr<ErrorOr<T>>
  : std::true_type
{
}; // IsErrorOr<ErrorOr<T>>

}; // detail

// Checks if T is the Error type.
template<typename T>
struct IsError
  : detail::IsError<typename std::decay<T>::type>
{
}; // IsError<T>

// Checks if T is an ErrorOr type.
template<typename T>
struct IsErrorOr
  : detail::IsErrorOr<typename std::decay<T>::type>
{
}; // IsErrorOr<T>

// Combines above checks.
template<typename T>
struct IsErrorLike
  : std::integral_constant<bool,
                           IsError<T>::value || IsErrorOr<T>::value>
{
}; // IsErrorLike<T>

} // common
} // mega

