#pragma once

#include <mega/file_service/avl_tree_traits.h>

#include <cassert>
#include <type_traits>

namespace mega
{
namespace file_service
{

template<typename BaseNodeType, typename LinkTraits, auto IsConstIterator, auto IsReverseIterator>
class AVLTreeIterator
{
    // Determine our actual node type.
    using NodeType = std::conditional_t<IsConstIterator, const BaseNodeType, BaseNodeType>;

    // Convenience.
    using OtherIteratorType =
        AVLTreeIterator<BaseNodeType, LinkTraits, !IsConstIterator, IsReverseIterator>;

    // Move the iterator forward one node.
    AVLTreeIterator& next()
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

    // Move the iterator backwards one node.
    AVLTreeIterator& previous()
    {
        assert(mNode);

        if (auto* node = LinkTraits::left(*mNode))
        {
            for (mNode = node; (node = LinkTraits::right(*mNode));)
                mNode = node;

            return *this;
        }

        for (auto* node = mNode; (mNode = LinkTraits::parent(*node));)
        {
            if (LinkTraits::left(*mNode) != node)
                break;

            node = mNode;
        }

        return *this;
    }

    // Where we are in the tree.
    NodeType* mNode{};

public:
    AVLTreeIterator() = default;

    AVLTreeIterator(NodeType* node):
        mNode(node)
    {}

    AVLTreeIterator(const OtherIteratorType& other):
        mNode(const_cast<NodeType*>(other.nodePointer()))
    {}

    explicit operator bool() const
    {
        return mNode != nullptr;
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

    bool operator!() const
    {
        return !mNode;
    }

    AVLTreeIterator& operator++()
    {
        if constexpr (IsReverseIterator)
        {
            return previous();
        }
        else
        {
            return next();
        }
    }

    AVLTreeIterator operator++(int)
    {
        AVLTreeIterator result = *this;

        ++(*this);

        return result;
    }

    AVLTreeIterator& operator--()
    {
        if constexpr (IsReverseIterator)
        {
            return next();
        }
        else
        {
            return previous();
        }
    }

    AVLTreeIterator operator--(int)
    {
        AVLTreeIterator result = *this;

        --(*this);

        return result;
    }

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
}; // AVLTreeIterator<BaseNodeType, LinkTraits, IsConstIterator, IsReverseIterator>

// Convenience.
template<typename Type>
struct ToConstIterator;

template<typename BaseNodeType, typename LinkTraits, auto IsConstIterator, auto IsReverseIterator>
struct ToConstIterator<
    AVLTreeIterator<BaseNodeType, LinkTraits, IsConstIterator, IsReverseIterator>>
{
    using Type = AVLTreeIterator<BaseNodeType, LinkTraits, true, IsReverseIterator>;
}; // ToConstIterator<AVLTreeIterator<...>>

template<typename Type>
using ToConstIteratorT = typename ToConstIterator<Type>::Type;

template<typename Type>
struct ToReverseIterator;

template<typename BaseNodeType, typename LinkTraits, auto IsConstIterator, auto IsReverseIterator>
struct ToReverseIterator<
    AVLTreeIterator<BaseNodeType, LinkTraits, IsConstIterator, IsReverseIterator>>
{
    using Type = AVLTreeIterator<BaseNodeType, LinkTraits, IsConstIterator, true>;
}; // ToReverseIterator<AVLTreeIterator<...>>

template<typename Type>
using ToReverseIteratorT = typename ToReverseIterator<Type>::Type;

} // file_service
} // mega
