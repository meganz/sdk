#pragma once

#include <mega/file_service/avl_tree_traits.h>

#include <cassert>
#include <type_traits>

namespace mega
{
namespace file_service
{

template<typename BaseNodeType, typename LinkTraits, auto IsConstIterator>
class AVLTreeIterator
{
    // Determine our actual node type.
    using NodeType = std::conditional_t<IsConstIterator, const BaseNodeType, BaseNodeType>;

    // Convenience.
    using OtherIteratorType = AVLTreeIterator<BaseNodeType, LinkTraits, !IsConstIterator>;

    // Where we are in the tree.
    NodeType* mNode{};

public:
    AVLTreeIterator() = default;

    AVLTreeIterator(NodeType* node):
        mNode(node)
    {}

    AVLTreeIterator(const OtherIteratorType& other):
        mNode(other.nodePointer())
    {}

    AVLTreeIterator left() const
    {
        assert(mNode);

        return LinkTraits::left(*mNode);
    }

    NodeType* nodePointer() const
    {
        return mNode;
    }

    AVLTreeIterator parent() const
    {
        assert(mNode);

        return LinkTraits::parent(*mNode);
    }

    AVLTreeIterator right() const
    {
        assert(mNode);

        return LinkTraits::right(*mNode);
    }

    NodeType& operator*() const
    {
        assert(mNode);

        return *mNode;
    }

    NodeType* operator->() const
    {
        assert(mNode);

        return mNode;
    }

    bool operator==(const AVLTreeIterator& rhs) const
    {
        return mNode == rhs.mNode;
    }

    bool operator!=(const AVLTreeIterator& rhs) const
    {
        return !(*this == rhs);
    }

    AVLTreeIterator& operator++()
    {
        assert(mNode);

        if (auto* node = LinkTraits::right(*mNode))
        {
            for (mNode = node; (node = LinkTraits::left(*mNode));)
                mNode = node;

            return *this;
        }

        for (auto* node = mNode; (mNode = LinkTraits::parent(*node));)
        {
            if (LinkTraits::right(*mNode) != node)
                break;

            node = mNode;
        }

        return *this;
    }

    AVLTreeIterator operator++(int)
    {
        AVLTreeIterator result = *this;

        ++(*this);

        return result;
    }
}; // AVLTreeIterator<Traits, IsConstIterator>

} // file_service
} // mega
