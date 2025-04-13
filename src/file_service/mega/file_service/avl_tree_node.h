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
    NodeType* mChildren[2];
    NodeType* mParent;
    AVLTreeHeight mHeight;
}; // AVLTreeNode<NodeType>

} // file_service
} // mega
