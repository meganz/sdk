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
    static constexpr auto mLinkPointer = &Node::mLink;
    static constexpr auto mValuePointer = &Node::mKey;
}; // Traits

class TraitsWithMetadata: public Traits
{
    using LT = detail::LinkTraits<TraitsWithMetadata>;
    using MT = detail::MetadataTraits<TraitsWithMetadata>;

public:
    static constexpr auto mMetadataPointer = &Node::mSize;

    struct Update
    {
        template<typename IteratorType>
        int operator()(IteratorType node) const
        {
            auto size = 1;

            if (auto left = node.left())
                size += left->mSize;

            if (auto right = node.right())
                size += right->mSize;

            return size;
        }
    }; // Update

    class Validate
    {
        template<typename IteratorType>
        auto validate(IteratorType node) const
        {
            if (!node)
                return std::make_pair(0, true);

            auto [left, leftOk] = validate(node.left());

            if (!leftOk)
                return std::make_pair(0, false);

            auto [right, rightOk] = validate(node.right());

            if (!rightOk)
                return std::make_pair(0, false);

            auto actualSize = node->mSize;
            auto computedSize = left + right + 1;

            return std::make_pair(computedSize, actualSize == computedSize);
        }

    public:
        template<typename IteratorType>
        bool operator()(IteratorType node) const
        {
            return validate(node).second;
        }
    }; // Validate
}; // TraitsWithMetadata

struct Uncomparable
{}; // Uncomparable

struct UncomparableNode
{
    AVLTreeNode<UncomparableNode> mLink;
    Uncomparable mValue;
}; // UncomparableNode

struct UncomparableTraits
{
    static constexpr auto mLinkPointer = &UncomparableNode::mLink;
    static constexpr auto mValuePointer = &UncomparableNode::mValue;
}; // UncomparableTraits

} // file_service
} // mega
