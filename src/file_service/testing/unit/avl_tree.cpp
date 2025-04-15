#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mega/file_service/avl_tree.h>
#include <mega/file_service/testing/unit/avl_node.h>

#include <deque>
#include <vector>

namespace mega
{
namespace file_service
{

template<typename Traits>
using DetectValidate = decltype(Traits::validate);

template<typename Traits>
static constexpr auto HasValidateV =
    std::is_invocable_r_v<bool, DetectedT<DetectValidate, Traits>, const Node&>;

template<typename Traits, typename KeyType = typename AVLTree<Traits>::KeyType>
static std::vector<KeyType> breadth(const AVLTree<Traits>& tree);

template<typename Traits, typename NodeType = typename AVLTree<Traits>::NodeType>
static AVLTree<Traits> treeFrom(std::vector<NodeType>& nodes);

template<typename Traits>
static bool validate(const AVLTree<Traits>& tree);

template<typename Traits>
static bool validate(typename AVLTree<Traits>::ConstIterator node,
                     typename AVLTree<Traits>::ConstIterator parent);

TEST(AVLTree, add)
{
    using ::testing::ElementsAre;

    // Basic addition tests.
    {
        Node n00{0};
        Node n01{0};

        AVLTree<Traits> tree;

        // Trees are always initially empty.
        ASSERT_TRUE(tree.empty());

        // We can add a node to the tree.
        auto iterator = tree.add(n00);

        // The iterator references the node we added.
        ASSERT_NE(iterator, tree.end());
        EXPECT_EQ(&*iterator, &n00);

        // Make sure the tree's valid.
        ASSERT_TRUE(validate(tree));

        // When we add a node with a duplicate key, we get an iterator
        // referencing the node in the tree with that key.
        EXPECT_EQ(tree.add(n01), iterator);

        // Make sure the tree remains valid.
        ASSERT_TRUE(validate(tree));
    }

    // Add the specified nodes to a tree, validating after each addition.
    auto addAndValidate = [](AVLTree<Traits>& tree, std::vector<Node>& nodes)
    {
        // Add each node to the tree.
        for (auto& node: nodes)
        {
            // Add the node to the tree.
            auto iterator = tree.add(node);

            // Make sure the iterator's valid.
            ASSERT_NE(iterator, tree.end());

            // And references the node we just added.
            ASSERT_EQ(&*iterator, &node);

            // And that adding the node didn't invalidate the tree.
            ASSERT_TRUE(validate(tree));

            // How many nodes have we added?
            auto count = &node - &nodes[0] + 1;

            // Make sure the tree contains the right number of nodes.
            ASSERT_EQ(tree.size(), count);
        }
    }; // addAndValidate

    // Add with left-left rebalance.
    {
        std::vector<Node> nodes = {2, 1, 0};
        AVLTree<Traits> tree;
        ASSERT_NO_FATAL_FAILURE(addAndValidate(tree, nodes));
        ASSERT_THAT(breadth(tree), ElementsAre(1, 0, 2));
    }

    // Add with left-right rebalance.
    {
        std::vector<Node> nodes = {2, 0, 1};
        AVLTree<Traits> tree;
        ASSERT_NO_FATAL_FAILURE(addAndValidate(tree, nodes));
        ASSERT_THAT(breadth(tree), ElementsAre(1, 0, 2));
    }

    // Add with right-left rebalance.
    {
        std::vector<Node> nodes = {0, 2, 1};
        AVLTree<Traits> tree;
        ASSERT_NO_FATAL_FAILURE(addAndValidate(tree, nodes));
        ASSERT_THAT(breadth(tree), ElementsAre(1, 0, 2));
    }

    // Add with right-right rebalance.
    {
        std::vector<Node> nodes = {0, 1, 2};
        AVLTree<Traits> tree;
        ASSERT_NO_FATAL_FAILURE(addAndValidate(tree, nodes));
        ASSERT_THAT(breadth(tree), ElementsAre(1, 0, 2));
    }
}

TEST(AVLTree, find)
{
    std::vector<Node> nodes = {0, 1, 2, 3, 4, 5, 6, 7};
    AVLTree<Traits> tree;

    // Add a bunch of nodes to the tree.
    for (auto& node: nodes)
        tree.add(node);

    // Make sure we can find them all.
    for (auto& node: nodes)
    {
        // Try and find this node in the tree.
        auto iterator = tree.find(node.mKey);

        // Make sure the iterator references the node we expect.
        ASSERT_NE(iterator, tree.end());
        EXPECT_EQ(&*iterator, &node);
    }
}

TEST(AVLTree, metadata)
{
    std::vector<Node> nodes = {0, 1, 2, 3, 4, 5, 6};
    AVLTree<TraitsWithMetadata> tree;

    // Add each node, checking that tree metadata is correct.
    for (auto& node: nodes)
    {
        tree.add(node);
        ASSERT_TRUE(validate(tree));
    }
}

TEST(AVLTree, remove)
{
    using testing::ElementsAre;

    // Remove leaf nodes.
    {
        std::vector<Node> nodes = {1, 0, 2};
        auto tree = treeFrom<Traits>(nodes);

        // Remove by key.
        auto* node = tree.remove(0);
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->mKey, 0);

        // Validate the tree.
        ASSERT_EQ(tree.size(), 2u);
        ASSERT_TRUE(validate(tree));
        ASSERT_THAT(breadth(tree), ElementsAre(1, 2));

        // Remove by iterator.
        auto iterator = tree.find(2);
        ASSERT_NE(iterator, tree.end());

        node = tree.remove(iterator);
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->mKey, 2);

        ASSERT_EQ(tree.size(), 1u);
        ASSERT_TRUE(validate(tree));
        ASSERT_THAT(breadth(tree), ElementsAre(1));

        // Remove root.
        node = tree.remove(1);
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->mKey, 1);

        // Validate tree.
        EXPECT_TRUE(tree.empty());
        EXPECT_EQ(tree.size(), 0u);
        ASSERT_TRUE(validate(tree));
        ASSERT_THAT(breadth(tree), ElementsAre());
    }

    // Remove branch nodes.
    {
        std::vector<Node> nodes = {3, 1, 5, 2, 4};
        auto tree = treeFrom<Traits>(nodes);

        // Remove right-leaning branch.
        auto* node = tree.remove(1);
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->mKey, 1);

        ASSERT_TRUE(validate(tree));
        EXPECT_THAT(breadth(tree), ElementsAre(3, 2, 5, 4));

        // Remove left-leaning branch.
        node = tree.remove(5);
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->mKey, 5);

        ASSERT_TRUE(validate(tree));
        EXPECT_THAT(breadth(tree), ElementsAre(3, 2, 4));
    }

    // Remove subtree nodes.
    {
        std::vector<Node> nodes = {5, 2, 8, 1, 4, 6, 9, 3, 7};
        auto tree = treeFrom<Traits>(nodes);

        // Remove root (replacement has child left.)
        auto* node = tree.remove(5);
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->mKey, 5);

        auto iterator = tree.root();
        ASSERT_NE(iterator, tree.end());
        ASSERT_EQ(iterator->mKey, 4);

        ASSERT_TRUE(validate(tree));
        ASSERT_THAT(breadth(tree), ElementsAre(4, 2, 8, 1, 3, 6, 9, 7));

        // Remove 8 (replacement has no child.)
        node = tree.remove(8);
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->mKey, 8);

        ASSERT_TRUE(validate(tree));
        ASSERT_THAT(breadth(tree), ElementsAre(4, 2, 7, 1, 3, 6, 9));

        // Remove root (replacement has no child.);
        node = tree.remove(4);
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->mKey, 4);

        ASSERT_TRUE(validate(tree));
        ASSERT_THAT(breadth(tree), ElementsAre(3, 2, 7, 1, 6, 9));
    }

    // Left-left rebalance.
    {
        std::vector<Node> nodes = {1, 2, 3, 4};
        auto tree = treeFrom<Traits>(nodes);

        auto* node = tree.remove(4);
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->mKey, 4);

        ASSERT_TRUE(validate(tree));
        ASSERT_THAT(breadth(tree), ElementsAre(2, 1, 3));
    }

    // Left-right rebalance.
    {
        std::vector<Node> nodes = {3, 1, 4, 2};
        auto tree = treeFrom<Traits>(nodes);

        auto* node = tree.remove(4);
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->mKey, 4);

        ASSERT_TRUE(validate(tree));
        ASSERT_THAT(breadth(tree), ElementsAre(2, 1, 3));
    }

    // Right-left rebalance.
    {
        std::vector<Node> nodes = {2, 1, 4, 3};
        auto tree = treeFrom<Traits>(nodes);

        auto* node = tree.remove(1);
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->mKey, 1);

        ASSERT_TRUE(validate(tree));
        ASSERT_THAT(breadth(tree), ElementsAre(3, 2, 4));
    }

    // Right-right rebalance.
    {
        std::vector<Node> nodes = {2, 1, 3, 4};
        auto tree = treeFrom<Traits>(nodes);

        auto* node = tree.remove(1);
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->mKey, 1);

        ASSERT_TRUE(validate(tree));
        ASSERT_THAT(breadth(tree), ElementsAre(3, 2, 4));
    }
}

template<typename Traits, typename KeyType>
static std::vector<KeyType> breadth(const AVLTree<Traits>& tree)
{
    // Convenience.
    using KeyTraits = detail::KeyTraits<Traits>;

    // No nodes to order if the tree's empty.
    if (tree.empty())
        return {};

    // Convenience.
    using Iterator = decltype(tree.end());

    // The keys we've seen in breadth-first order.
    std::vector<KeyType> keys;

    // Nodes remaining to be processed.
    std::deque<Iterator> pending(1, tree.root());

    // Reserve space for our node references.
    keys.reserve(tree.size());

    // Traverse the tree.
    while (!pending.empty())
    {
        // Pop the first iterator from the queue.
        auto iterator = pending.front();

        pending.pop_front();

        // Keep track of which key this iterator references.
        keys.emplace_back(KeyTraits::key(*iterator));

        // Push this node's children onto the queue.
        if (auto left = iterator.left(); left != Iterator{})
            pending.emplace_back(left);

        if (auto right = iterator.right(); right != Iterator{})
            pending.emplace_back(right);
    }

    // Return ordered keys to caller.
    return keys;
}

template<typename Traits, typename NodeType>
AVLTree<Traits> treeFrom(std::vector<NodeType>& nodes)
{
    AVLTree<Traits> tree;

    for (auto& node: nodes)
        tree.add(node);

    return tree;
}

template<typename Traits>
bool validate(const AVLTree<Traits>& tree)
{
    return validate<Traits>(tree.root(), tree.end());
}

template<typename Traits>
bool validate(typename AVLTree<Traits>::ConstIterator node,
              typename AVLTree<Traits>::ConstIterator parent)
{
    using KeyTraits = detail::KeyTraits<Traits>;
    using LinkTraits = detail::LinkTraits<Traits>;

    static const typename AVLTree<Traits>::ConstIterator null;

    // No node? Can't be invalid.
    if (node == null)
        return true;

    // A node's parent must be who linked to us.
    if (node.parent() != parent)
        return false;

    // A node's balance must be between [-1, +1].
    if (std::abs(LinkTraits::balance(*node)) > 1)
        return false;

    // Get our hands on the node's key.
    auto& key = KeyTraits::key(*node);

    // Validate left subtree.
    if (auto left = node.left(); left != null)
    {
        // Our left subtree must have a key less than ours.
        if (KeyTraits::key(*left) >= key)
            return false;

        // Bail if our left subtree isn't valid.
        if (!validate<Traits>(left, node))
            return false;
    }

    // Validate our metadata.
    if constexpr (HasValidateV<Traits>)
    {
        if (!Traits::validate(*node))
            return false;
    }

    auto right = node.right();

    // No right subtree so we're valid.
    if (right == null)
        return true;

    // Right subtree must have a key greater than ours.
    if (KeyTraits::key(*right) <= key)
        return false;

    // Validate our right subtree.
    return validate<Traits>(right, node);
}

} // file_service
} // mega
