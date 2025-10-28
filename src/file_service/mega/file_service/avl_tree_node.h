#pragma once

#include <cstdint>

namespace mega
{
namespace file_service
{

using AVLTreeHeight = std::uint8_t;

template<typename NodeType>
struct AVLTreeNode
{
    // Reference to this node's left and right children.
    //
    // The reason the child links are defined as an array rather
    // than as separate mLeft and mRight members is that it enables
    // us to reduce duplication when implementing the tree later.
    //
    // For instance, we can replace something like this:
    //
    // NodeType* next;
    //
    // if (compare(key, node.mKey) < 0)
    //   next = node.mLeft;
    // else if (compare(node.mKey, key) >= 0)
    //   next = node.mRight;
    //
    // ...
    //
    // With this:
    //
    // auto relationship = compare(key, node.mKey);
    // auto* next = node.mChildren[relationship > 0];
    NodeType* mChildren[2];

    // Reference to this node's parent.
    NodeType* mParent;

    // Tracks how "tall" or "deep" this subtree is.
    //
    // We use this member to compute a node's balance.
    AVLTreeHeight mHeight;
}; // AVLTreeNode<NodeType>

} // file_service
} // mega
