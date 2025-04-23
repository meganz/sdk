#pragma once

#include <mega/file_service/avl_tree_node.h>
#include <mega/file_service/type_traits.h>

#include <cassert>
#include <cmath>
#include <functional>

namespace mega
{
namespace file_service
{
namespace detail
{

// Check whether Traits contains a Compare type.
template<typename Traits>
using DetectCompareType = typename Traits::Compare;

// Check whether Traits contains a key functor.
template<typename Traits>
using DetectKeyFunction = typename Traits::KeyFunction;

// Check whether Traits contains an mKeyPointer member.
template<typename Traits>
using DetectKeyPointer = decltype(Traits::mKeyPointer);

// Check whether Traits contains an mLinkPointer member.
template<typename Traits>
using DetectLinkPointer = decltype(Traits::mLinkPointer);

// Check whether Traits contains an update member.
template<typename Traits>
using DetectUpdate = typename Traits::Update;

template<typename Traits>
class KeyTraits
{
    // Check whether Traits contains a mKeyPointer member and if it does,
    // whether that member is a class member pointer.
    //
    // DetectedT<DetectKeyPointer, Traits> will be yield NoneSuch if
    // Traits doesn't contain a mKeyPointer member.
    using KeyPointerTraits = MemberPointerTraits<DetectedT<DetectKeyPointer, Traits>>;

    // Ensure Traits::mKeyPointer is present and is a class member pointer.
    static_assert(KeyPointerTraits::value);

public:
    // Default to Identity if Traits::KeyFunction isn't defined.
    using KeyFunction = DetectedOrT<Identity, DetectKeyFunction, Traits>;

    // Convenience.
    using CandidateKeyType = typename KeyPointerTraits::member_type;

    // Make sure the user's provided a sane key functor.
    static_assert(std::is_invocable_v<KeyFunction, CandidateKeyType>);

    // Determine the tree's key type.
    using KeyType = RemoveCVRefT<std::invoke_result_t<KeyFunction, CandidateKeyType>>;

    // Determine the tree's node type.
    using NodeType = typename KeyPointerTraits::class_type;

    // Default to std::less<KeyType> if Traits::Compare isn't defined.
    using Compare = DetectedOrT<std::less<KeyType>, DetectCompareType, Traits>;

    // Make sure our comparator is sane.
    static_assert(std::is_invocable_r_v<bool, Compare const&, const KeyType&, const KeyType&>);

    // Compare lhs and rhs.
    //
    // Returns
    // <0 if lhs < rhs
    // =0 if lhs == rhs
    // >0 if lhs > rhs
    static auto compare(const KeyType& lhs, const KeyType& rhs)
    {
        Compare compare{};

        // lhs is less than rhs.
        if (compare(lhs, rhs))
            return -1;

        // lhs is greater than rhs.
        if (compare(rhs, lhs))
            return +1;

        // lhs is equal to rhs.
        return 0;
    }

    // Return a reference to the key contained by node.
    template<typename NodeType>
    static auto& key(NodeType& node)
    {
        return KeyFunction()(node.*Traits::mKeyPointer);
    }
}; // KeyTraits<Traits>

template<typename Traits>
class LinkTraits
{
    // Same technique as for KeyPointerTraits above.
    using LinkPointerTraits = MemberPointerTraits<DetectedT<DetectLinkPointer, Traits>>;

    static_assert(LinkPointerTraits::value);

public:
    using LinkType = typename LinkPointerTraits::member_type;
    using NodeType = typename LinkPointerTraits::class_type;

    // Make sure mLinkPointer actually refers to an AVL node instance.
    static_assert(std::is_base_of_v<AVLTreeNode<NodeType>, LinkType>);

    // Compute this node's balance.
    //
    // <0 if the node's left subtree is taller than its right subtree.
    // =0 if the node's subtrees are the same height.
    // >0 if the node's right subtree is taller than its left subtree.
    //
    // We use this value to determine whether we have to rebalance the
    // subtree rooted at this node.
    template<typename NodeType>
    static auto balance(NodeType& node)
    {
        auto balance = 0;

        if (auto* right = LinkTraits::right(node))
            balance = height(*right);

        if (auto* left = LinkTraits::left(node))
            balance -= height(*left);

        // Ensure the node's balance is within [-2, +2].
        assert(std::abs(balance) < 3);

        return balance;
    }

    // Return a reference to one of the node's child pointers.
    //
    // direction specifies which child pointer we want to reference.
    //
    // When true, return a reference to the node's right child pointer.
    // Otherwise, return a reference to the node's left child pointer.
    template<typename NodeType>
    static auto& child(NodeType& node, bool direction)
    {
        return link(node).mChildren[direction];
    }

    // Return a reference to the node's height member.
    template<typename NodeType>
    static auto& height(NodeType& node)
    {
        return link(node).mHeight;
    }

    // Return a reference to the node's left child pointer.
    template<typename NodeType>
    static auto& left(NodeType& node)
    {
        return child(node, 0);
    }

    // Return a reference to this node's node link member.
    template<typename NodeType>
    static auto& link(NodeType& node)
    {
        return node.*Traits::mLinkPointer;
    }

    // Return a reference to the node's parent pointer.
    template<typename NodeType>
    static auto& parent(NodeType& node)
    {
        return link(node).mParent;
    }

    // Return a reference to the node's right child pointer.
    template<typename NodeType>
    static auto& right(NodeType& node)
    {
        return child(node, 1);
    }
}; // LinkTraits<Traits>

// Used when a client isn't using an augmented tree.
template<typename Traits, typename = void>
struct MetadataTraits
{
    // Provide a dummy method so our AVL code doesn't care whether the
    // user's tree is augmented or not.
    template<typename IteratorType, typename NodeType>
    static void update(NodeType&)
    {}
}; // MetadataTraits<Traits, void>

// Traits has a mMetadataPointer member.
template<typename Traits>
class MetadataTraits<Traits, std::void_t<decltype(Traits::mMetadataPointer)>>
{
    // Same technique as for KeyPointerTraits above.
    using MetadataPointerTraits = MemberPointerTraits<decltype(Traits::mMetadataPointer)>;

    // If mMetadataPointer is present, make sure it's a class member pointer.
    static_assert(MetadataPointerTraits::value);

    using MetadataType = typename MetadataPointerTraits::member_type;

public:
    using NodeType = typename MetadataPointerTraits::class_type;

    // If Traits::mMetadataPointer exists so much Traits::update(...).
    //
    // This function is called when a node's metadata needs to be updated.
    //
    // As an example, imagine the user's defining an augmented tree that
    // where each node knows how many children it has.
    //
    // Let's add the keys 0, 1, 2 in that order.
    //
    // Notation is this: key(size).
    //
    // Add 0:
    // 0(0)
    //
    // When 0 is added, update(...) is called on the new node to compute the
    // sizes of its children, if any.
    //
    // Add 1:
    // 0(1) -.
    //      1(0)
    //
    // When 1 is added, we update its size as the case above but note that
    // the tree's structure has changed as 0 now has a right child. So, we
    // traverse up the tree and update 0's metadata, too.
    //
    // Add 2:
    // 0 -.                        .--- 1(2) ---.
    //    1 -.  -> rebalance ->  0(0)          2(0)
    //       2
    //
    // When 2 is added, the tree becomes imbalanced so the structure of the
    // tree is altered to restore that balance. Since the tree's structure
    // has changed, we need to update the metadata of each node that has
    // been altered. The result is what you'd expect.
    using Update = DetectedT<DetectUpdate, Traits>;
    static_assert(IsNotNoneSuchV<Update>);

    // Return a reference to a node's metadata.
    template<typename NodeType>
    static auto& metadata(NodeType& node)
    {
        return node.*Traits::mMetadataPointer;
    }

    // Update a node's metadata based on that of its children.
    template<typename IteratorType, typename NodeType>
    static void update(NodeType& node)
    {
        // Make sure our update functor accepts an iterator.
        static_assert(std::is_invocable_r_v<MetadataType, Update, IteratorType>);

        IteratorType iterator(&node);

        // Recompute this node's metadata.
        metadata(node) = Update()(iterator);
    }
}; // MetadataTraits<Traits, void>

} // detail
} // file_service
} // mega
