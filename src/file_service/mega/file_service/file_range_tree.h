#pragma once

#include <mega/file_service/avl_tree.h>
#include <mega/file_service/file_range.h>
#include <mega/file_service/file_range_tree_traits.h>
#include <mega/file_service/type_traits.h>

#include <cassert>
#include <memory>
#include <type_traits>
#include <utility>

namespace mega
{
namespace file_service
{

template<typename KeyFunctionType,
         typename ValueType,
         auto Comparable = IsEqualityComparableV<ValueType>,
         auto Copyable = std::is_copy_constructible_v<ValueType>>
class FileRangeTree
{
    // Convenience.
    using ByRangeBeginTree = AVLTree<detail::IndexByRangeBegin<KeyFunctionType, ValueType>>;

    using ByRangeEndTree = AVLTree<detail::IndexByRangeEnd<KeyFunctionType, ValueType>>;

    template<typename IteratorType>
    class IteratorAdapter: public IteratorType
    {
    public:
        // Minimal STL support;
        using pointer = ValueType*;
        using reference = ValueType&;
        using value_type = ValueType;

        // Inherit constructors from IteratorType.
        using IteratorType::IteratorType;

        // Allow construction from an IteratorType instance.
        IteratorAdapter(const IteratorType& iterator):
            IteratorType(iterator)
        {}

        // Return a reference to our value, not our node.
        auto& operator*() const
        {
            return IteratorType::operator*().mValue;
        }

        // Return a pointer to our value, not our node.
        auto* operator->() const
        {
            return &operator*();
        }

        IteratorAdapter& operator++()
        {
            IteratorType::operator++();

            return *this;
        }

        IteratorAdapter operator++(int)
        {
            return static_cast<IteratorType&>(*this)++;
        }

        IteratorAdapter& operator--()
        {
            IteratorType::operator--();

            return *this;
        }

        IteratorAdapter operator--(int)
        {
            return static_cast<IteratorType&>(*this)--;
        }
    }; // IteratorBase

    using NodeType = typename ByRangeBeginTree::NodeType;

    // Construct a new node.
    template<typename Parameter, typename... Parameters>
    auto construct(const FileRange& range, Parameter&& argument, Parameters&&... arguments)
    {
        return std::make_unique<NodeType>(
            std::piecewise_construct,
            std::forward_as_tuple(range),
            std::forward_as_tuple(std::forward<Parameter>(argument),
                                  std::forward<Parameters>(arguments)...));
    }

    template<typename Parameter, typename... Parameters>
    auto construct(std::piecewise_construct_t, Parameter&& argument, Parameters&&... arguments)
    {
        return std::make_unique<NodeType>(std::piecewise_construct,
                                          std::forward<Parameter>(argument),
                                          std::forward<Parameters>(arguments)...);
    }

    template<typename Parameter, typename... Parameters>
    auto construct(Parameter&& argument, Parameters&&... arguments)
    {
        return std::make_unique<NodeType>(std::forward<Parameter>(argument),
                                          std::forward<Parameters>(arguments)...);
    }

    // Indexes nodes by the end of their range.
    ByRangeEndTree mByRangeEnd;

protected:
    // Indexes nodes by the beginning of their range.
    ByRangeBeginTree mByRangeBegin;

public:
    using ConstIterator = IteratorAdapter<typename ByRangeBeginTree::ConstIterator>;
    using ConstReverseIterator = IteratorAdapter<typename ByRangeBeginTree::ConstReverseIterator>;
    using Iterator = IteratorAdapter<typename ByRangeBeginTree::Iterator>;
    using ReverseIterator = IteratorAdapter<typename ByRangeBeginTree::ReverseIterator>;

    FileRangeTree() = default;

    FileRangeTree(FileRangeTree&& other) = default;

    ~FileRangeTree()
    {
        // Remove all the ranges from the tree.
        clear();
    }

    FileRangeTree& operator=(FileRangeTree&& rhs)
    {
        FileRangeTree temp(std::move(rhs));

        swap(temp);

        return *this;
    }

    // Add a range into the tree.
    //
    // NOTE: This function will always allocate a new node regardless of
    // of whether the range described by that node is already present in
    // some form in the tree.
    //
    // If some overlapping range is already present in the tree, the node we
    // eagerly allocated will be deallocated and an iterator to the first
    // overlapping range in the tree will be returned.
    //
    // If you want to add a range to the tree and you really don't want to
    // allocate anything unless the addition actually happens, you should
    // call tryAdd(...) below instead.
    template<typename Parameter, typename... Parameters>
    auto add(Parameter&& argument, Parameters&&... arguments) -> std::pair<Iterator, bool>
    {
        // Construt a node to represent our range in the tree.
        auto node =
            construct(std::forward<Parameter>(argument), std::forward<Parameters>(arguments)...);

        // Convenience.
        KeyFunctionType key;

        // Get a reference to our node's range.
        auto& range = const_cast<FileRange&>(key(node->mValue));

        // Some range ends after our range begins.
        if (auto iterator = mByRangeEnd.upper_bound(range.mBegin))
        {
            // Convenience.
            auto& other = key(iterator->mValue);

            // Other range contains the leading part of our range.
            if (other.mBegin <= range.mBegin)
                return std::make_pair<Iterator>(&*iterator, false);

            // Our range may contain the leading part of the other range.
            range.mEnd = std::min(range.mEnd, other.mBegin);
        }

        // Make sure the range is sane.
        assert(range.mEnd > range.mBegin);

        // Add our range to the "by begin" index.
        auto [iterator, added] = mByRangeBegin.add(*node);

        // Sanity.
        assert(added);

        // Add our range to the "by end" index.
        added = mByRangeEnd.add(*node).second;

        // Sanity.
        assert(added);

        // The tree now owns our node.
        node.release();

        // Let the caller know their range is now in the tree.
        return std::make_pair<Iterator>(iterator, true);
    }

    // Return an iterator to the first node in the tree.
    Iterator begin()
    {
        return mByRangeBegin.begin();
    }

    ConstIterator begin() const
    {
        return mByRangeBegin.begin();
    }

    // Find the first range that begins at or after position.
    Iterator beginsAfter(std::uint64_t position)
    {
        return mByRangeBegin.lower_bound(position);
    }

    ConstIterator beginsAfter(std::uint64_t position) const
    {
        return const_cast<FileRangeTree&>(*this).beginsAfter(position);
    }

    // Return an iterator to the first node in the tree.
    ConstIterator cbegin() const
    {
        return begin();
    }

    // Remove all ranges from the tree.
    void clear()
    {
        // Remove all ranges from the tree.
        remove(begin(), end());
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

    // Does this tree contain any ranges?
    bool empty() const
    {
        return mByRangeBegin.empty();
    }

    // Return an iterator to the end of the tree.
    Iterator end()
    {
        return mByRangeBegin.end();
    }

    ConstIterator end() const
    {
        return mByRangeBegin.end();
    }

    // Find the first range that ends at or after position.
    Iterator endsAfter(std::uint64_t position)
    {
        return mByRangeEnd.lower_bound(position).nodePointer();
    }

    auto endsAfter(std::uint64_t position) const
    {
        return const_cast<FileRangeTree&>(*this).endsAfter(position);
    }

    // Return an iterator to the last range in the tree.
    Iterator last()
    {
        return rbegin();
    }

    ConstIterator last() const
    {
        return crbegin();
    }

    // Find all ranges that overlap range.
    auto find(const FileRange& range) -> std::pair<Iterator, Iterator>
    {
        // Are there any ranges that end after we begin?
        auto i = mByRangeEnd.upper_bound(range.mBegin);

        // No ranges end after we begin.
        if (!i)
            return {};

        // Range begins after we end.
        if (KeyFunctionType()(i->mValue).mBegin >= range.mEnd)
            return {};

        // Are there any ranges that begin after (or when) we end?
        auto j = mByRangeBegin.lower_bound(range.mEnd);

        // Return range of overlapping ranges to our caller.
        return std::make_pair<Iterator>(&*i, j);
    }

    auto find(const FileRange& range) const -> std::pair<ConstIterator, ConstIterator>
    {
        return const_cast<FileRangeTree&>(*this).find(range);
    }

    // Return a reverse iterator to the last node in the tree.
    ReverseIterator rbegin()
    {
        return mByRangeBegin.rbegin();
    }

    ConstReverseIterator rbegin() const
    {
        return mByRangeBegin.rbegin();
    }

    // Remove all ranges contained in the specified range.
    Iterator remove(const FileRange& range)
    {
        // Find the first range, if any, contained by range.
        auto begin = mByRangeBegin.lower_bound(range.mBegin);

        // No range begins after the specified range.
        if (!begin)
            return {};

        // Begin isn't contained within the specified range.
        if (KeyFunctionType()(begin->mValue).mEnd > range.mEnd)
            return {};

        // Find the first range outside of range.
        auto end = mByRangeBegin.lower_bound(range.mEnd);

        // And remove them from the tree.
        return remove(begin, end);
    }

    // Remove all ranges between two iterators from the tree.
    Iterator remove(Iterator begin, Iterator end)
    {
        while (begin != end)
            begin = remove(begin);

        return begin;
    }

    // Remove a specific range from the tree.
    Iterator remove(Iterator iterator)
    {
        // Sanity.
        assert(iterator);

        // Remove the node from our "by begin" index.
        auto* node = mByRangeBegin.remove(iterator++);

        // Remove the node from our "by end" index.
        mByRangeEnd.remove(node);

        // Destroy the node.
        delete node;

        // Return an iterator to the next range in the tree.
        return iterator;
    }

    // Return a reverse iterator to the end of the tree.
    ReverseIterator rend()
    {
        return mByRangeBegin.rend();
    }

    ConstReverseIterator rend() const
    {
        return mByRangeBegin.rend();
    }

    // Get an iterator to the tree's root node.
    Iterator root()
    {
        return mByRangeBegin.root();
    }

    ConstIterator root() const
    {
        return mByRangeBegin.root();
    }

    // How many ranges does this tree contain?
    std::size_t size() const
    {
        return mByRangeBegin.size();
    }

    // Swap the contents of this tree with another.
    void swap(FileRangeTree& other)
    {
        using std::swap;

        swap(mByRangeBegin, other.mByRangeBegin);
        swap(mByRangeEnd, other.mByRangeEnd);
    }

    // Try and add a new range to the tree.
    //
    // tryAdd(...) is a more restrictive and performant version of add(...).
    //
    // Unlike add(...), it'll allocate a new node if and only if no other
    // ranges in the tree overlap the range provided.
    //
    // If some ranges overlap the range provided by the caller, this
    // function will return an iterator to the first such range.
    //
    // If no ranges overlap the range provided by the caller, a new node
    // will be created based on that range and the specified arguments.
    template<typename... Parameters>
    auto tryAdd(FileRange range, Parameters&&... arguments) -> std::pair<Iterator, bool>
    {
        // Some range ends after our range begins.
        if (auto iterator = mByRangeEnd.upper_bound(range.mBegin))
        {
            // Convenience.
            auto& other = KeyFunctionType()(iterator->mValue);

            // The other range contains the leading part of our range.
            if (other.mBegin <= range.mBegin)
                return std::make_pair<Iterator>(&*iterator, false);

            // Our range may contain the leading part of the other range.
            range.mEnd = std::min(range.mEnd, other.mBegin);
        }

        // Make sure the range is sane.
        assert(range.mEnd > range.mBegin);

        // Construct a node to represent our range in the tree.
        auto node = construct(range, std::forward<Parameters>(arguments)...);

        // Add our node to the tree's "by begin" index.
        auto [iterator, added] = mByRangeBegin.add(*node);

        // Sanity.
        assert(added);

        // Add our node to the tree's "by end" index.
        added = mByRangeEnd.add(*node).second;

        // Sanity.
        assert(added);

        // The tree now owns the node.
        node.release();

        // Let the caller know their range has been added to the tree.
        return std::make_pair<Iterator>(iterator, added);
    }
}; // FileRangeTree<KeyFunctionType, ValueType, Comparable, Copyable>

template<typename KeyFunctionType, typename ValueType>
class FileRangeTree<KeyFunctionType, ValueType, true, false>:
    public FileRangeTree<KeyFunctionType, ValueType, false, false>
{
    // Convenience.
    using BaseType = FileRangeTree<KeyFunctionType, ValueType, false, false>;

public:
    // Inherit constructors.
    using BaseType::BaseType;

    bool operator==(const FileRangeTree& rhs) const
    {
        return this->mByRangeBegin == rhs.mByRangeBegin;
    }

    bool operator!=(const FileRangeTree& rhs) const
    {
        return !(*this == rhs);
    }
}; // FileRangeTree<KeyFunctionType, ValueType, true, false>

template<typename KeyFunctionType, typename ValueType, auto Comparable>
class FileRangeTree<KeyFunctionType, ValueType, Comparable, true>:
    public FileRangeTree<KeyFunctionType, ValueType, Comparable, false>
{
    // Convenience.
    using BaseType = FileRangeTree<KeyFunctionType, ValueType, Comparable, false>;

public:
    // Inherit constructors from base File Range tree.
    using BaseType::BaseType;

    // Allow copy construction.
    FileRangeTree(const FileRangeTree& other):
        BaseType()
    {
        for (auto i = other.begin(); i != other.end(); ++i)
            this->add(*i);
    }

    FileRangeTree(FileRangeTree&& other) = default;

    // And copy assignment.
    FileRangeTree& operator=(const FileRangeTree& rhs)
    {
        FileRangeTree temp(rhs);

        this->swap(temp);

        return *this;
    }

    FileRangeTree& operator=(FileRangeTree&& rhs) = default;
}; // FileRangeTree<KeyFunctionType, ValueType, Comparable, true>

// Swap the contents of lhs with rhs.
template<typename KeyFunctionType, typename ValueType, auto Comparable, auto Copyable>
void swap(FileRangeTree<KeyFunctionType, ValueType, Comparable, Copyable>& lhs,
          FileRangeTree<KeyFunctionType, ValueType, Comparable, Copyable>& rhs)
{
    lhs.swap(rhs);
}

} // file_service
} // mega
