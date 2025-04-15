#pragma once

#include <mega/file_service/avl_tree_node.h>
#include <mega/file_service/avl_tree_traits.h>

#include <utility>

namespace mega
{
namespace file_service
{

struct Node
{
    Node(int key):
        mLink{},
        mKey(key),
        mSize{}
    {}

    AVLTreeNode<Node> mLink;
    int mKey;
    int mSize;
}; // Node

struct Traits
{
    static constexpr auto mKeyPointer = &Node::mKey;
    static constexpr auto mLinkPointer = &Node::mLink;
}; // Traits

class TraitsWithMetadata: public Traits
{
    static auto validate(const Node* node)
    {
        using LT = detail::LinkTraits<TraitsWithMetadata>;
        using MT = detail::MetadataTraits<TraitsWithMetadata>;

        if (!node)
            return std::make_pair(0, true);

        auto [left, leftOk] = validate(LT::left(*node));

        if (!leftOk)
            return std::make_pair(0, false);

        auto [right, rightOk] = validate(LT::right(*node));

        if (!rightOk)
            return std::make_pair(0, false);

        auto actualSize = MT::metadata(*node);
        auto computedSize = left + right + 1;

        return std::make_pair(computedSize, actualSize == computedSize);
    }

public:
    static constexpr auto mMetadataPointer = &Node::mSize;

    static int update(const int* lhs, const int* rhs)
    {
        return (lhs ? *lhs : 0) + (rhs ? *rhs : 0) + 1;
    }

    static bool validate(const Node& node)
    {
        return validate(&node).second;
    }
}; // TraitsWithMetadata

} // file_service
} // mega
