#pragma once

#include <mega/file_service/avl_tree_node.h>
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
using DetectLinkPointer = decltype(Traits::mLinkPointer);

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

template<typename Traits>
class LinkTraits
{
    using LinkPointerTraits = MemberPointerTraits<DetectedT<DetectLinkPointer, Traits>>;

    static_assert(LinkPointerTraits::value);

public:
    using LinkType = typename LinkPointerTraits::member_type;
    using NodeType = typename LinkPointerTraits::class_type;

    static_assert(std::is_base_of_v<AVLTreeNode<NodeType>, LinkType>);

    template<typename NodeType>
    static auto balance(NodeType& node)
    {
        auto balance = 0;

        if (auto* right = LinkTraits::right(node))
            balance = height(*right);

        if (auto* left = LinkTraits::left(node))
            balance -= height(*left);

        return balance;
    }

    template<typename NodeType>
    static auto& child(NodeType& node, bool direction)
    {
        return link(node).mChildren[direction];
    }

    template<typename NodeType>
    static auto& height(NodeType& node)
    {
        return link(node).mHeight;
    }

    template<typename NodeType>
    static auto& left(NodeType& node)
    {
        return child(node, 0);
    }

    template<typename NodeType>
    static auto& link(NodeType& node)
    {
        return node.*Traits::mLinkPointer;
    }

    template<typename NodeType>
    static auto& parent(NodeType& node)
    {
        return link(node).mParent;
    }

    template<typename NodeType>
    static auto& right(NodeType& node)
    {
        return child(node, 1);
    }
}; // LinkTraits<Traits>

} // detail
} // file_service
} // mega
