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

template<typename Traits>
class AVLTree
{
    using KT = detail::KeyTraits<Traits>;
    using LT = detail::LinkTraits<Traits>;
    using MT = detail::MetadataTraits<Traits>;

    template<typename T>
    using DetectNodeType = typename T::NodeType;

public:
    using KeyTraits = KT;
    using KeyType = typename KT::KeyType;
    using LinkTraits = LT;

    using NodeType = MostSpecificClassT<typename KT::NodeType,
                                        typename LT::NodeType,
                                        DetectedOrT<typename KT::NodeType, DetectNodeType, MT>>;

    static_assert(IsNotNoneSuchV<NodeType>);

private:
    auto* find(const KeyType& key, NodeType**& link)
    {
        link = &mRoot;
        NodeType* parent{};

        for (NodeType* child; (child = *link);)
        {
            auto relationship = KT::compare(key, KT::key(*child));

            if (!relationship)
                break;

            link = &LT::child(*child, relationship > 0);
            parent = child;
        }

        return parent;
    }

    NodeType* maybeRebalance(NodeType& node)
    {
        auto balance = LT::balance(node);

        if (std::abs(balance) > 1)
            return rebalance(node, balance > 0);

        update(node);

        return &node;
    }

    NodeType* rebalance(NodeType& node, bool direction)
    {
        static const int balances[] = {+1, -1};

        auto& child = LT::child(node, direction);

        assert(child);

        auto balance = LT::balance(*child);

        if (balance == balances[direction])
            child = rotate(*child, direction);

        return rotate(node, !direction);
    }

    void rebalance(NodeType* node)
    {
        NodeType* parent;

        assert(node);

        while ((parent = LT::parent(*node)))
        {
            auto* left = LT::left(*parent);
            auto& link = LT::child(*parent, left != node);

            link = maybeRebalance(*node);
            node = parent;
        }

        mRoot = maybeRebalance(*node);
    }

    NodeType* remove(NodeType** link, NodeType* parent)
    {
        assert(link);
        assert(*link);

        auto& node = **link;
        auto* replacement = LT::left(node);

        --mSize;

        if (replacement)
        {
            if (LT::right(node))
            {
                auto* replacementLink = &LT::left(node);

                while (LT::right(*replacement))
                {
                    replacementLink = &LT::right(*replacement);
                    replacement = *replacementLink;
                }

                if ((parent = LT::parent(*replacement)) == &node)
                    parent = replacement;

                *replacementLink = LT::left(*replacement);
                *link = replacement;

                LT::link(*replacement) = LT::link(node);

                if (*replacementLink)
                    LT::parent(**replacementLink) = parent;

                if (auto* left = LT::left(*replacement))
                    LT::parent(*left) = replacement;

                if (auto* right = LT::right(*replacement))
                    LT::parent(*right) = replacement;

                rebalance(parent);

                return &node;
            }
        }
        else
        {
            replacement = LT::right(node);
        }

        if ((*link = replacement))
            LT::parent(*replacement) = parent;

        if (parent)
            rebalance(parent);

        return &node;
    }

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

        update(node);
        update(*child);

        return child;
    }

    void update(NodeType& node)
    {
        AVLTreeHeight height = 0;

        if (auto* left = LT::left(node))
            height = LT::height(*left);

        if (auto* right = LT::right(node))
            height = std::max(height, LT::height(*right));

        LT::height(node) = height + 1;

        MT::template update<LT>(node);
    }

    NodeType* mRoot{};
    std::size_t mSize{};

public:
    using ConstIterator = AVLTreeIterator<NodeType, LinkTraits, true>;
    using Iterator = AVLTreeIterator<NodeType, LinkTraits, false>;

    auto add(NodeType& node) -> std::pair<Iterator, bool>
    {
        NodeType** link{};
        auto* parent = find(KT::key(node), link);

        if (auto* child = *link)
            return std::make_pair(child, false);

        *link = &node;
        LT::parent(node) = parent;
        ++mSize;

        rebalance(&node);

        return std::make_pair(&node, true);
    }

    Iterator begin()
    {
        if (!mRoot)
            return {};

        auto* node = mRoot;

        while (LT::left(*node))
            node = LT::left(*node);

        return node;
    }

    ConstIterator begin() const
    {
        return const_cast<AVLTree<Traits>&>(*this).begin();
    }

    bool empty() const
    {
        return !mSize;
    }

    Iterator end()
    {
        return {};
    }

    ConstIterator end() const
    {
        return {};
    }

    Iterator find(const KeyType& key)
    {
        NodeType** link{};

        find(key, link);

        return *link;
    }

    ConstIterator find(const KeyType& key) const
    {
        return const_cast<AVLTree<Traits>&>(*this).find(key);
    }

    Iterator lower_bound(const KeyType& key)
    {
        NodeType** link;
        auto* parent = find(key, link);

        if (*link)
            return *link;

        if (parent && link == &LT::left(*parent))
            return parent;

        return nullptr;
    }

    ConstIterator lower_bound(const KeyType& key) const
    {
        return const_cast<AVLTree<Traits>&>(*this).lower_bound(key);
    }

    NodeType* remove(const KeyType& key)
    {
        NodeType** link{};
        auto* parent = find(key, link);

        if (*link)
            return remove(link, parent);

        return nullptr;
    }

    NodeType* remove(Iterator iterator)
    {
        assert(iterator != Iterator{});

        auto& node = *iterator;
        auto* parent = LT::parent(node);

        if (!parent)
            return remove(&mRoot, nullptr);

        auto* link = &LT::left(*parent);

        return remove(&link[*link != &node], parent);
    }

    Iterator root()
    {
        return mRoot;
    }

    ConstIterator root() const
    {
        return const_cast<AVLTree<Traits>&>(*this).root();
    }

    std::size_t size() const
    {
        return mSize;
    }
}; // AVLTree<Traits>

} // file_service
} // mega
