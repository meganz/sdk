#pragma once

#include <mega/fuse/common/expected_forward.h>

namespace mega
{

class Error;

namespace fuse
{

template<typename T>
using ErrorOr = Expected<Error, T>;

template<typename T>
struct IsError;

template<typename T>
struct IsErrorOr;

template<typename T>
struct IsErrorLike;

} // fuse
} // mega

