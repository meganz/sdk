#pragma once

#include <mega/file_service/avl_tree_node.h>

namespace mega
{
namespace file_service
{
namespace detail
{

template<typename ValueType>
struct FileRangeTreeNode
{
    FileRangeTreeNode() = default;

    // Allows us to construct mValue any way we please.
    template<typename T, typename... Ts>
    FileRangeTreeNode(T&& first, Ts&&... rest):
        mByRangeBegin(),
        mByRangeEnd(),
        mValue(std::forward<T>(first), std::forward<Ts>(rest)...)
    {}

    // Nodes are never copied or moved.
    FileRangeTreeNode(const FileRangeTreeNode& other) = delete;

    FileRangeTreeNode& operator=(const FileRangeTreeNode& rhs) = delete;

    // Convenience.
    using LinkType = AVLTreeNode<FileRangeTreeNode<ValueType>>;

    // So we can index this node by the beginning of its range.
    LinkType mByRangeBegin;

    // So we can index this node by the end of its range.
    LinkType mByRangeEnd;

    // The value carried by this node.
    ValueType mValue;
}; // FileRangeTreeNode<ValueType>

} // detail
} // file_service
} // mega
