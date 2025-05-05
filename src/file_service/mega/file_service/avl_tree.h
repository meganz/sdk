#pragma once

#include <mega/file_service/avl_tree_iterator.h>
#include <mega/file_service/avl_tree_node.h>
#include <mega/file_service/avl_tree_traits.h>
#include <mega/file_service/type_traits.h>

#include <cassert>
#include <cmath>
#include <cstddef>
#include <utility>

namespace mega
{
namespace file_service
{
namespace detail
{

template<typename Traits, auto IsEqualityComparable = ValueIsEqualityComparableV<Traits>>
class AVLTree
{
    // Convenience.
    using KT = detail::KeyTraits<Traits>;
    using LT = detail::LinkTraits<Traits>;
    using MT = detail::MetadataTraits<Traits>;

    // Check if T contains a NodeType type.
    template<typename T>
    using DetectNodeType = typename T::NodeType;

public:
    // Might be useful for generic code.
    using KeyTraits = KT;
    using KeyType = typename KT::KeyType;
    using LinkTraits = LT;

    // Try and determine the user's concrete node type.
    //
    // This is necessary as it's possible that the user's node type is
    // actually a subclass of some other node type.
    //
    // In this case, it'd be possible for two different classes to be
    // reported by our traits. For instance, mValuePointer might be
    // referencing something in NodeClassA and mMetadataPointer might be
    // referencing something in NodeClass B.
    //
    // To resolve this issue, we consider the user's node type to be the
    // most specific of the node types that we have detected.
    using NodeType = MostSpecificClassT<typename KT::NodeType,
                                        typename LT::NodeType,
                                        DetectedOrT<typename KT::NodeType, DetectNodeType, MT>>;

    // Make sure the node types we've detected are actually related.
    static_assert(IsNotNoneSuchV<NodeType>);

private:
    // Rebalance a node (subtree) if necessary.
    NodeType* maybeRebalance(NodeType& node)
    {
        // How imbalanced is this node?
        auto balance = LT::balance(node);

        // Node's critically imbalanced.
        if (std::abs(balance) > 1)
            return rebalance(node, balance > 0);

        // Update this node's height and metadata.
        update(node);

        // This subtree's structure hasn't been changed so return it as is.
        return &node;
    }

    // Perform a left or right rebalance on the specified node.
    //
    // If direction is true, perform a right rebalance.
    // Otherwise, perform a left rebalance.
    NodeType* rebalance(NodeType& node, bool direction)
    {
        static const int balances[] = {+1, -1};

        auto& child = LT::child(node, direction);

        assert(child);

        auto balance = LT::balance(*child);

        // Double rotation case (left-right or right-left.)
        if (balance == balances[direction])
            child = rotate(*child, direction);

        return rotate(node, !direction);
    }

    // Rebalance the tree, traversing upwards from node.
    void rebalance(NodeType* node)
    {
        NodeType* parent;

        assert(node);

        while ((parent = LT::parent(*node)))
        {
            // Is node parent's left or right child?
            auto* left = LT::left(*parent);
            auto& link = LT::child(*parent, left != node);

            // Rebalance (restructure) this subtree if necessary.
            link = maybeRebalance(*node);

            // Move one level up the tree.
            node = parent;
        }

        // Rebalance (restructure) the root if necessary.
        mRoot = maybeRebalance(*node);
    }

    // Remove a node from the tree.
    NodeType* remove(NodeType** link, NodeType* parent)
    {
        // Make sure we've been passed a valid node link.
        assert(link);

        // Make sure the node link actually references something.
        assert(*link);

        // Get a reference to the node we are removing.
        auto& node = **link;

        // Get a reference to the node's left child, if any.
        auto* replacement = LT::left(node);

        // Reduce tree's size as we're removing a node.
        --mSize;

        // Node has a left child.
        if (replacement)
        {
            // Check if node also has a right child.
            if (LT::right(node))
            {
                // Node's replacement will be its inorder predecessor.
                auto* replacementLink = &LT::left(node);

                while (LT::right(*replacement))
                {
                    replacementLink = &LT::right(*replacement);
                    replacement = *replacementLink;
                }

                // Replacement is node's left child.
                if ((parent = LT::parent(*replacement)) == &node)
                {
                    // Make sure rebalancing starts from the replacement.
                    parent = replacement;
                }

                // Don't lose replacement's left child, if any.
                *replacementLink = LT::left(*replacement);

                // Replacement takes node's place in the tree.
                *link = replacement;

                // Replacement inherits node's children and parent.
                LT::link(*replacement) = LT::link(node);

                // If replacement had a left child, update its parent link.
                if (*replacementLink)
                {
                    // Needed when replacemnt is not node's left child.
                    LT::parent(**replacementLink) = parent;
                }

                // Make sure replacement's new children know who their parent is.
                if (auto* left = LT::left(*replacement))
                    LT::parent(*left) = replacement;

                if (auto* right = LT::right(*replacement))
                    LT::parent(*right) = replacement;

                rebalance(parent);

                // Return a reference to the node we've removed.
                return &node;
            }
        }
        else
        {
            // Check if node has a right child.
            replacement = LT::right(node);
        }

        // If replacement is not null, node is a left or right branch.
        //
        // replacement will take node's place in the tree.
        if ((*link = replacement))
            LT::parent(*replacement) = parent;

        // Rebalance the tree starting from node's parent, if any.
        if (parent)
            rebalance(parent);

        // Return a reference to the node we removed.
        return &node;
    }

    // Rotate a node left or right.
    //
    // If direction is true, perform a right rotation.
    // Otherwise, perform a left rotation.
    NodeType* rotate(NodeType& node, bool direction)
    {
        auto* child = LT::child(node, !direction);
        assert(child);

        LT::parent(*child) = LT::parent(node);
        LT::parent(node) = child;

        auto* grandchild = LT::child(*child, direction);

        if (grandchild)
            LT::parent(*grandchild) = &node;

        LT::child(*child, direction) = &node;
        LT::child(node, !direction) = grandchild;

        // Update node invariants.
        update(node);

        // Make sure child's invariants are updated last.
        update(*child);

        return child;
    }

    // Update a node's height and metadata.
    void update(NodeType& node)
    {
        // Assume the node has no children.
        AVLTreeHeight height = 0;

        // Node has a left child so latch its height.
        if (auto* left = LT::left(node))
            height = LT::height(*left);

        // Node has a right child. Latch its height if it is higher.
        if (auto* right = LT::right(node))
            height = std::max(height, LT::height(*right));

        // Update the node's height.
        //
        // The +1 is because the height includes the node itself.
        LT::height(node) = height + 1;

        // Update any metadata associated with this node.
        MT::template update<ConstIterator>(node);
    }

    // Points to the tree's root node, if any.
    NodeType* mRoot{};

    // How many nodes does this tree contain?
    std::size_t mSize{};

public:
    using Iterator = AVLTreeIterator<NodeType, LinkTraits, false, false>;
    using ReverseIterator = ToReverseIteratorT<Iterator>;

    using ConstIterator = ToConstIteratorT<Iterator>;
    using ConstReverseIterator = ToReverseIteratorT<ConstIterator>;

    AVLTree() = default;

    // Allow move construction since we are just moving the root pointer.
    AVLTree(AVLTree&& other):
        mRoot(other.mRoot),
        mSize(other.mSize)
    {
        other.mRoot = nullptr;
        other.mSize = 0;
    }

    // Disallow move assignment as we don't know how to deallocate nodes.
    AVLTree& operator=(AVLTree&& rhs) = delete;

    // Add a node to the tree.
    auto add(NodeType** link, NodeType& node, NodeType* parent) -> std::pair<Iterator, bool>
    {
        // Sanity.
        assert(link);

        // A node in the tree's already associated with this key.
        if (auto* child = *link)
            return std::make_pair(child, false);

        // Link in the user's node.
        *link = &node;

        // Make sure the user's node knows who its parent is.
        LT::parent(node) = parent;

        // Increment node counter.
        ++mSize;

        // Rebalance the tree, updating node metadata as needed.
        rebalance(&node);

        // Let the user know the node was added.
        return std::make_pair(&node, true);
    }

    // Add a node to the tree.
    auto add(NodeType& node) -> std::pair<Iterator, bool>
    {
        // Where should we link in the user's node?
        auto [parent, link] = findLink(KT::key(node));

        // Try and add the node to the tree.
        return add(link, node, parent);
    }

    // Return an iterator to the first node in the tree.
    Iterator begin()
    {
        // No nodes? No iterator.
        if (!mRoot)
            return {};

        auto* node = mRoot;

        // Locate the tree's smallest key.
        while (LT::left(*node))
            node = LT::left(*node);

        return node;
    }

    // Return an iterator to the first node in the tree.
    ConstIterator begin() const
    {
        return const_cast<AVLTree&>(*this).begin();
    }

    ConstIterator cbegin() const
    {
        return begin();
    }

    // Return an iterator to the end of the tree.
    ConstIterator cend() const
    {
        return end();
    }

    // Return a reverse iterator to the last node in the tree.
    ConstReverseIterator crbegin() const
    {
        return rbegin();
    }

    // Return a reverse iterator to the end of the tree.
    ConstReverseIterator crend() const
    {
        return rend();
    }

    // Does the tree contain any nodes?
    bool empty() const
    {
        return !mSize;
    }

    // Return an iterator to the end of the tree.
    Iterator end()
    {
        return {};
    }

    // Return an iterator to the end of the tree.
    ConstIterator end() const
    {
        return {};
    }

    // Try and locate key in the tree.
    //
    // This function returns two values, one directly and one through the
    // link parameter.
    //
    // The value returned directly by this function will be a pointer to the
    // last node that was traversed when searching for key. Put differently,
    // it will point to the parent of the node that does (or would) contain
    // key.
    //
    // The link parameter will point to the last child link that was taken
    // before traversal terminated. If key is already in the tree, *link
    // will reference the node that contains it. If key isn't in the tree
    // then we can use *link to attach a new directly to the appropriate
    // parent.
    auto findLink(const KeyType& key) -> std::pair<NodeType*, NodeType**>
    {
        // Start the search from the root.
        //
        // Note that link is a reference to a node pointer.
        auto link = &mRoot;

        // The root node has no parent.
        NodeType* parent{};

        for (NodeType* child; (child = *link);)
        {
            // How does the user's key relate to the child's?
            auto relationship = KT::compare(key, KT::key(*child));

            // User's key is equivalent to the child's.
            if (!relationship)
                break;

            // Which child are we going to traverse into?
            //
            // If relationship is >0, traverse into the right child.
            // Otherwise, traverse into the left child.
            link = &LT::child(*child, relationship > 0);

            // This child is the parent of the next.
            parent = child;
        }

        return std::make_pair(parent, link);
    }

    // Return an iterator to the node associated with key.
    Iterator find(const KeyType& key)
    {
        // Try and locate the node associated with key.
        auto [_, link] = findLink(key);

        return *link;
    }

    // Return an iterator to the node associated with key.
    ConstIterator find(const KeyType& key) const
    {
        return const_cast<AVLTree&>(*this).find(key);
    }

    // Return a reference to the first node not less than key.
    Iterator lower_bound(const KeyType& key)
    {
        NodeType* candidate = nullptr;

        // Search the tree for key.
        for (auto* node = mRoot; node;)
        {
            // How does key relate to this node's key?
            auto relationship = KT::compare(key, KT::key(*node));

            // Key's equivalent to this node's key.
            if (!relationship)
                return node;

            // Key's less than this node's key.
            if (relationship < 0)
                candidate = node;

            // Continue the search down the tree.
            node = LT::child(*node, relationship > 0);
        }

        // If candidate's not null, it'll always reference the node with the
        // smallest key greater than key.
        return candidate;
    }

    // Return a reference to the first node not less than key.
    ConstIterator lower_bound(const KeyType& key) const
    {
        return const_cast<AVLTree&>(*this).lower_bound(key);
    }

    // Return a reverse iterator to the last node in the tree.
    ReverseIterator rbegin()
    {
        // No nodes? No iterator.
        if (!mRoot)
            return {};

        auto* node = mRoot;

        // Locate the tree's largest key.
        while (LT::right(*node))
            node = LT::right(*node);

        return node;
    }

    ReverseIterator rbegin() const
    {
        return const_cast<AVLTree&>(*this).rbegin();
    }

    // Remove the node associated with the specified key.
    NodeType* remove(const KeyType& key)
    {
        // Try and locate the node in the tree.
        auto [parent, link] = findLink(key);

        // Key is associated with some node in the tree.
        if (*link)
            return remove(link, parent);

        // Key isn't associated with any node in the tree.
        return nullptr;
    }

    // Remove the node identified by this iterator from the tree.
    NodeType* remove(Iterator iterator)
    {
        // Make sure our iterator is valid.
        assert(iterator);

        // What node is the iterator referencing?
        auto& node = *iterator;

        // Who is our node's parent?
        auto* parent = LT::parent(node);

        // We're removing the root node.
        if (!parent)
            return remove(&mRoot, nullptr);

        // Get a reference to our parent's child links.
        auto* link = &LT::left(*parent);

        // Is node parent's left or right child?
        link = &link[*link != &node];

        // Remove node from the tree.
        return remove(link, parent);
    }

    // Return a reverse iterator to the end of the tree.
    ReverseIterator rend()
    {
        return {};
    }

    ConstReverseIterator rend() const
    {
        return {};
    }

    // Return an iterator to this tree's root node.
    Iterator root()
    {
        return mRoot;
    }

    // Return a const iterator to this tree's root node.
    ConstIterator root() const
    {
        return const_cast<AVLTree&>(*this).root();
    }

    // How many nodes does this tree contain?
    std::size_t size() const
    {
        return mSize;
    }

    // Swap the contents of this tree with another.
    void swap(AVLTree& other)
    {
        using std::swap;

        swap(mRoot, other.mRoot);
        swap(mSize, other.mSize);
    }

    // Return a reference to the first node greater than key.
    Iterator upper_bound(const KeyType& key)
    {
        NodeType* candidate = nullptr;

        // Search the tree for key.
        for (auto* node = mRoot; node;)
        {
            // How does key relate to this node's key?
            auto relationship = KT::compare(key, KT::key(*node));

            // Key's less than this node's key.
            if (relationship < 0)
                candidate = node;

            // Continue the search down the tree.
            node = LT::child(*node, relationship >= 0);
        }

        // If candidate's not null, it'll be the first node greater than key.
        return candidate;
    }

    ConstIterator upper_bound(const KeyType& key) const
    {
        return const_cast<AVLTree&>(*this).upper_bound(key);
    }
}; // AVLTree<Traits, false>

template<typename Traits>
class AVLTree<Traits, true>: public AVLTree<Traits, false>
{
public:
    bool operator==(const AVLTree& rhs) const
    {
        // A tree's always equal to itself.
        if (this == &rhs)
            return true;

        // Can't be equal if the trees differ in size.
        if (this->size() != rhs.size())
            return false;

        auto i = this->begin();
        auto j = this->end();

        // Convenience.
        using KT = typename AVLTree::KeyTraits;

        // Iterate over the tree comparing values as we go.
        for (auto m = rhs.begin(); i != j && KT::value(*i) == KT::value(*m); ++i, ++m)
            ;

        // Make sure we compared every value in the tree.
        return i == j;
    }

    bool operator!=(const AVLTree& rhs) const
    {
        return !(*this == rhs);
    }
}; // AVLTree<Traits, true>

// Swap the contents of lhs with rhs.
template<typename Traits, auto IsEqualityComparable>
void swap(AVLTree<Traits, IsEqualityComparable>& lhs, AVLTree<Traits, IsEqualityComparable>& rhs)
{
    lhs.swap(rhs);
}

} // detail

using detail::AVLTree;

} // file_service
} // mega
