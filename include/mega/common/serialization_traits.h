#pragma once

#include <mega/common/serialization_traits_forward.h>
#include <mega/common/type_traits.h>

namespace mega
{
namespace common
{

template<typename T>
struct HasSerializationTraits
  : IsComplete<SerializationTraits<T>>
{
}; // HasSerializationTraits<T>

} // common
} // mega

