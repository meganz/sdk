#pragma once

#include <mega/common/expected_forward.h>

namespace mega
{

class Error;

namespace common
{

template<typename T>
using ErrorOr = Expected<Error, T>;

template<typename T>
struct IsError;

template<typename T>
struct IsErrorOr;

template<typename T>
struct IsErrorLike;

} // common
} // mega

