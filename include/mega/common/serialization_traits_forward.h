#pragma once

namespace mega
{
namespace common
{

template<typename T>
struct HasSerializationTraits;

template<typename T>
static constexpr auto HasSerializationTraitsV =
  HasSerializationTraits<T>::value;

template<typename T>
struct SerializationTraits;

} // common
} // mega

