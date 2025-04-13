#pragma once

#include <mega/file_service/type_traits.h>

#include <functional>

namespace mega
{
namespace file_service
{
namespace detail
{

template<typename Traits>
using DetectCompareType = typename Traits::compare;

template<typename Traits>
using DetectKeyPointer = decltype(Traits::mKeyPointer);

template<typename Traits>
class KeyTraits
{
    using KeyPointerTraits = MemberPointerTraits<DetectedT<DetectKeyPointer, Traits>>;

    static_assert(KeyPointerTraits::value);

public:
    using KeyType = typename KeyPointerTraits::member_type;
    using NodeType = typename KeyPointerTraits::class_type;

    static auto compare(const KeyType& lhs, const KeyType& rhs)
    {
        using Compare = DetectedOrT<std::less<KeyType>, DetectCompareType, Traits>;

        Compare compare{};

        if (compare(lhs, rhs))
            return -1;

        if (compare(rhs, lhs))
            return +1;

        return 0;
    }

    template<typename NodeType>
    static auto& key(const NodeType& node)
    {
        return node.*Traits::mKeyPointer;
    }
}; // KeyTraits<Traits>

} // detail
} // file_service
} // mega
