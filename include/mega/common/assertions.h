#pragma once

#include <string_view>

namespace mega
{
namespace common
{
namespace detail
{

[[noreturn]]
void assertFailure(std::string_view assertion,
                   std::string_view file,
                   std::string_view function,
                   const int line_no);

} // detail
} // common
} // mega

#ifdef __GNUC__
#define ASSERT_FUNCTION_NAME __PRETTY_FUNCTION__
#endif // __GNUC__

#ifdef _MSC_VER
#define ASSERT_FUNCTION_NAME __FUNCSIG__
#endif // _MSC_VER

#ifndef ASSERT_FUNCTION_NAME
#define ASSERT_FUNCTION_NAME __func__
#endif // ! ASSERT_FUNCTION_NAME

#define ASSERT_ALWAYS(expr) \
    do \
    { \
        if (!(expr)) \
            ::mega::common::detail::assertFailure(#expr, \
                                                  __FILE__, \
                                                  ASSERT_FUNCTION_NAME, \
                                                  __LINE__); \
    } \
    while (0)

#ifdef DEBUG
#define DEBUG_ASSERT(expr) ASSERT_ALWAYS(expr)
#define RELEASE_ASSERT(expr)
#else // DEBUG
#define DEBUG_ASSERT(expr)
#define RELEASE_ASSERT(expr) ASSERT_ALWAYS(expr)
#endif // ! DEBUG
