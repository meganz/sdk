#pragma once

#include <mega/file_service/file_range_forward.h>
#include <mega/file_service/file_range_tree_traits.h>

#include <cassert>
#include <cstddef>
#include <iterator>
#include <utility>

namespace mega
{
namespace file_service
{
namespace detail
{

template<typename Container>
class FileRangeGapIterator
{
    // Make sure Container is a file range tree.
    static_assert(IsFileRangeTreeV<Container>);

    // Convenience.
    using ConstIterator = typename Container::ConstIterator;
    using KeyFunctionType = GetKeyFunctionTypeT<Container>;

    // The container we're iterating over.
    const Container* mContainer{};

    // The last gap we detected.
    FileRange mGap{};

    // The range we're iterating over.
    FileRange mRange{};

    std::uint64_t mLast{};

    // Detect the next gap.
    void detect()
    {
        // Locate the next range in the container.
        auto iterator = mContainer->endsAfter(mRange.mBegin);

        // There are no more ranges in our container.
        if (!iterator)
            return mGap = std::exchange(mRange, {}), void();

        KeyFunctionType key;

        // Range begins after mRange.
        if (auto range = key(*iterator); range.mBegin > mRange.mBegin)
        {
            // Populate gap.
            mGap.mBegin = mRange.mBegin;
            mGap.mEnd = std::min(mRange.mEnd, range.mBegin);

            // Bump mRange.
            mRange.mBegin = mGap.mEnd;

            // Nothing more to do.
            return;
        }

        // Range begins before mRange.

        // Range ends after mRange.
        if (key(*iterator).mEnd >= mRange.mEnd)
            return mGap = mRange = {}, void();

        // Populate gap assuming there are no further ranges.
        mGap = {key(*iterator).mEnd, mRange.mEnd};

        // Move iterator to the next range in our container.
        ++iterator;

        // Iterator moved to another range.
        if (iterator)
            mGap.mEnd = std::min(mRange.mEnd, key(*iterator).mBegin);

        // Bump mRange.
        mRange.mBegin = mGap.mEnd;
    }

public:
    // For compatibility with the STL.
    using difference_type = ptrdiff_t;
    using iterator_category = std::input_iterator_tag;
    using pointer = const FileRange*;
    using reference = const FileRange&;
    using value_type = FileRange;

    FileRangeGapIterator() = default;

    FileRangeGapIterator(const FileRangeGapIterator& other) = default;

    FileRangeGapIterator(const Container& container, const FileRange& range):
        mContainer(&container),
        mGap(),
        mRange(range),
        mLast(range.mBegin)
    {
        // Can't find any gaps in a range of zero length.
        if (mRange.mBegin >= mRange.mEnd)
            return;

        // Detect the first gap in mRange.
        detect();
    }

    FileRangeGapIterator& operator=(const FileRangeGapIterator& rhs) = default;

    const FileRange& operator*() const
    {
        // Make sure we're not past the end of mRange.
        assert(mGap.mBegin != mGap.mEnd);

        // Return a reference to the last gap we detected.
        return mGap;
    }

    const FileRange* operator->() const
    {
        return &operator*();
    }

    bool operator==(const FileRangeGapIterator& rhs) const
    {
        // Iterators are equal if they're both beyond their range.
        if (mGap.empty() && rhs.mGap.empty())
            return true;

        // Iterators are equal if their components are equal.
        return mGap == rhs.mGap && mRange == rhs.mRange;
    }

    bool operator!=(const FileRangeGapIterator& rhs) const
    {
        return !(*this == rhs);
    }

    FileRangeGapIterator& operator++()
    {
        // Make sure we're not advancing past the end of mRange.
        assert(mGap.mBegin != mGap.mEnd);

        // Detect the next gap.
        detect();

        // Return a reference to ourselves to our caller.
        return *this;
    }

    FileRangeGapIterator operator++(int)
    {
        // Take a snapshot of this iterator.
        auto snapshot = *this;

        // Move the iterator forward one step.
        ++*this;

        // Return snapshot to our caller.
        return snapshot;
    }

    FileRangeGapIterator begin() const
    {
        return *this;
    }

    bool empty() const
    {
        return begin() == end();
    }

    FileRangeGapIterator end() const
    {
        return FileRangeGapIterator();
    }
}; // FileRangeGapIterator<Container>

} // detail

using detail::FileRangeGapIterator;

// Convenience.
template<typename Container, std::enable_if_t<IsFileRangeTreeV<Container>>* = nullptr>
auto gaps(const Container& container, const FileRange& range)
{
    return FileRangeGapIterator(container, range);
}

template<typename Container, std::enable_if_t<IsFileRangeTreeV<Container>>* = nullptr>
auto gaps(const Container& container, std::uint64_t begin, std::uint64_t end)
{
    return gaps(container, FileRange(begin, end));
}

} // file_service
} // mega
