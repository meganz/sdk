#include <gtest/gtest.h>
#include <mega/file_service/file_range_map.h>
#include <mega/file_service/file_range_set.h>
#include <mega/file_service/testing/unit/avl_utilities.h>

#include <cstdlib>
#include <functional>
#include <type_traits>

namespace mega
{
namespace file_service
{

struct NoCopyMove
{
    NoCopyMove(int value):
        mValue(value)
    {}

    NoCopyMove(const NoCopyMove& other) = delete;

    NoCopyMove& operator=(const NoCopyMove& rhs) = delete;

    const int mValue;
}; // NoCopyMove

// Commmon cases for add(...) and tryAdd(...).
template<typename AddFunction>
static void testAdd(AddFunction&& add);

// Maps should only be comparable if their value type is.
static_assert(IsEqualityComparableV<FileRangeMap<int>>);
static_assert(!IsEqualityComparableV<std::pair<const FileRange, NoCopyMove>>);

// Maps should only be copyable if their value type is.
static_assert(std::is_copy_assignable_v<FileRangeMap<int>>);
static_assert(std::is_copy_constructible_v<FileRangeMap<int>>);

static_assert(!std::is_copy_assignable_v<FileRangeMap<NoCopyMove>>);
static_assert(!std::is_copy_constructible_v<FileRangeMap<NoCopyMove>>);

TEST(FileRangeMap, add)
{
    FileRangeMap<NoCopyMove> map;

    auto [iterator, added] =
        map.add(std::piecewise_construct, std::forward_as_tuple(0, 1), std::forward_as_tuple(0));

    EXPECT_TRUE(added);
    ASSERT_NE(iterator, map.end());
    EXPECT_EQ(iterator->first, FileRange(0, 1));
    EXPECT_EQ(iterator->second.mValue, 0);

    std::tie(iterator, added) = map.tryAdd(FileRange(0, 1), 0);
    EXPECT_FALSE(added);
    EXPECT_EQ(iterator, map.begin());

    std::tie(iterator, added) = map.tryAdd(FileRange(1, 2), 1);
    EXPECT_TRUE(added);
    EXPECT_EQ(iterator->first, FileRange(1, 2));
    EXPECT_EQ(iterator->second.mValue, 1);
}

TEST(FileRangeSet, add)
{
    auto add = [](FileRangeSet& set, const FileRange& range)
    {
        return set.add(range);
    }; // add

    EXPECT_NO_FATAL_FAILURE(testAdd(std::move(add)));
}

TEST(FileRangeSet, constructor)
{
    FileRangeSet set;

    // A newly constructed set should be empty.
    EXPECT_EQ(set.begin(), set.end());
    EXPECT_TRUE(set.empty());
    EXPECT_EQ(set.size(), 0u);
}

TEST(FileRangeSet, copy_assignment)
{
    FileRangeSet set0;

    // Add some ranges to our set.
    set0.add(0u, 1u);
    set0.add(1u, 2u);
    set0.add(2u, 3u);

    // Sanity.
    EXPECT_EQ(set0.size(), 3u);

    FileRangeSet set1;

    set1 = set0;

    // Make sure set1 has been populated.
    EXPECT_EQ(set0.size(), set1.size());

    // set0 should be equivalent to set1.
    EXPECT_EQ(set0, set1);

    FileRangeSet set2;

    // Assigning set2 to set1 should clear set1.
    set1 = set2;

    // set1 should be empty.
    EXPECT_TRUE(set1.empty());
}

TEST(FileRangeSet, copy_constructor)
{
    FileRangeSet set0;

    // Add some ranges to our set.
    set0.add(0u, 1u);
    set0.add(1u, 2u);
    set0.add(2u, 3u);

    // Sanity.
    EXPECT_EQ(set0.size(), 3u);

    FileRangeSet set1(set0);

    // Make sure set1 has been populated.
    EXPECT_EQ(set0.size(), set1.size());

    // set0 should be equivalent to set1.
    EXPECT_EQ(set0, set1);
}

TEST(FileRangeSet, find)
{
    FileRangeSet set;

    // Add some ranges to the tree.
    set.add(2u, 4u);
    set.add(6u, 8u);

    // Sanity.
    EXPECT_EQ(set.size(), 2u);

    // Check nonoverlapping cases.
    auto [m, n] = set.find(FileRange(0, 2));
    EXPECT_EQ(m, set.end());
    EXPECT_EQ(m, n);

    std::tie(m, n) = set.find(FileRange(8, 10));
    EXPECT_EQ(m, set.end());
    EXPECT_EQ(m, n);

    // Check single overlap cases.
    std::tie(m, n) = set.find(FileRange(1, 3));
    EXPECT_NE(m, set.end());
    EXPECT_EQ(*m, FileRange(2, 4));
    EXPECT_EQ(++m, n);

    std::tie(m, n) = set.find(FileRange(2, 4));
    EXPECT_NE(m, set.end());
    EXPECT_EQ(*m, FileRange(2, 4));
    EXPECT_EQ(++m, n);

    std::tie(m, n) = set.find(FileRange(3, 5));
    EXPECT_NE(m, set.end());
    EXPECT_EQ(*m, FileRange(2, 4));
    EXPECT_EQ(++m, n);

    // Check multiple overlap cases.
    std::tie(m, n) = set.find(FileRange(1, 7));
    EXPECT_NE(m, set.end());
    EXPECT_EQ(*m, FileRange(2, 4));
    EXPECT_NE(++m, n);
    EXPECT_EQ(*m, FileRange(6, 8));
    EXPECT_EQ(++m, n);

    std::tie(m, n) = set.find(FileRange(2, 8));
    EXPECT_NE(m, set.end());
    EXPECT_EQ(*m, FileRange(2, 4));
    EXPECT_NE(++m, n);
    EXPECT_EQ(*m, FileRange(6, 8));
    EXPECT_EQ(++m, n);

    std::tie(m, n) = set.find(FileRange(3, 9));
    EXPECT_NE(m, set.end());
    EXPECT_EQ(*m, FileRange(2, 4));
    EXPECT_NE(++m, n);
    EXPECT_EQ(*m, FileRange(6, 8));
    EXPECT_EQ(++m, n);
}

TEST(FileRangeSet, iteration)
{
    // The ranges that we'll be adding to our set.
    std::vector<FileRange> ranges = {{0, 1}, {1, 2}, {2, 3}};

    FileRangeSet set;

    // Add the ranges to our set.
    for (auto& range: ranges)
        set.add(range);

    // Sanity.
    EXPECT_EQ(set.size(), 3u);

    auto i = ranges.begin();
    auto j = ranges.end();
    auto m = set.begin();
    auto n = set.end();

    // Iterate over the tree, checking each range in turn.
    for (; i != j && m != n && *i == *m; ++i, ++m)
        ;

    // Make sure we compared every range.
    EXPECT_EQ(i, j);
    EXPECT_EQ(m, n);
}

TEST(FileRangeSet, move_assignment)
{
    FileRangeSet set0;
    FileRangeSet set1;

    // Generate two identical sets.
    for (std::size_t i = 0; i < 3; ++i)
    {
        FileRange range(i, i + 1);

        set0.add(range);
        set1.add(range);
    }

    // Sanity.
    EXPECT_EQ(set0.size(), 3u);
    EXPECT_EQ(set0.size(), set1.size());

    FileRangeSet set2;

    // Move set0's contents into set2.
    set2 = std::move(set0);

    // set0 should now be empty.
    EXPECT_EQ(set0.begin(), set0.end());
    EXPECT_TRUE(set0.empty());
    EXPECT_EQ(set0.size(), 0u);

    // set1 should be equivalent to set2.
    EXPECT_EQ(set1, set2);

    // Moving set0 into set2 should clear set2.
    set2 = std::move(set0);

    // set0 should now be equivalent to set2.
    EXPECT_EQ(set0, set2);
}

TEST(FileRangeSet, move_constructor)
{
    FileRangeSet set0;
    FileRangeSet set1;

    // Generate two identical sets.
    for (std::size_t i = 0; i < 3; ++i)
    {
        FileRange range(i, i + 1);

        set0.add(range);
        set1.add(range);
    }

    // Sanity.
    EXPECT_EQ(set0.size(), 3u);
    EXPECT_EQ(set0.size(), set1.size());

    // Move set0's contents to set2.
    FileRangeSet set2(std::move(set0));

    // set0 should now be empty.
    EXPECT_EQ(set0.begin(), set0.end());
    EXPECT_TRUE(set0.empty());
    EXPECT_EQ(set0.size(), 0u);

    // set1 should now be equivalent to set2.
    EXPECT_EQ(set1, set2);
}

TEST(FileRangeSet, remove_contained)
{
    FileRangeSet set;

    set.add(1u, 3u);

    auto i = set.add(4u, 6u).first;

    set.add(7u, 9u);

    // Sanity.
    EXPECT_EQ(set.size(), 3u);

    auto m = set.remove(FileRange(0, 2));
    EXPECT_EQ(m, set.end());
    EXPECT_EQ(set.size(), 3u);

    m = set.remove(FileRange(0, 4));
    EXPECT_EQ(i, m);
    EXPECT_EQ(set.size(), 2u);

    m = set.remove(FileRange(4, 9));
    EXPECT_EQ(m, set.end());
    EXPECT_EQ(set.size(), 0u);
}

TEST(FileRangeSet, remove_multiple)
{
    FileRangeSet set;

    // Add some ranges to the set.
    auto i = set.add(0u, 1u).first;
    auto k = set.add(2u, 3u).first;

    set.add(1u, 2u);

    // Sanity.
    EXPECT_EQ(set.size(), 3u);

    // Remove ranges up to but not including k.
    auto m = set.remove(i, k);

    EXPECT_EQ(m, k);
    EXPECT_EQ(set.size(), 1u);
}

TEST(FileRangeSet, remove_single)
{
    FileRangeSet set;

    // Add some ranges to the set.
    auto i = set.add(0u, 1u).first;
    auto j = set.add(1u, 2u).first;
    auto k = set.add(2u, 3u).first;

    // Sanity.
    EXPECT_EQ(set.size(), 3u);

    // Remove each range in sequence.
    auto m = set.remove(i);
    EXPECT_EQ(m, j);
    EXPECT_EQ(set.size(), 2u);

    m = set.remove(m);
    EXPECT_EQ(m, k);
    EXPECT_EQ(set.size(), 1u);

    m = set.remove(m);
    EXPECT_EQ(m, set.end());
    EXPECT_EQ(set.size(), 0u);
}

TEST(FileRangeSet, tryAdd)
{
    auto tryAdd = [](FileRangeSet& set, const FileRange& range)
    {
        return set.tryAdd(range);
    }; // tryAdd

    EXPECT_NO_FATAL_FAILURE(testAdd(std::move(tryAdd)));
}

template<typename AddFunction>
void testAdd(AddFunction&& add)
{
    FileRangeSet set;

    // You should be able to add a range to an empty set.
    //
    // Before: ________
    //  After: __AA____
    auto [iterator, added] = std::invoke(add, set, FileRange(2, 4));

    // Make sure the range was added.
    EXPECT_TRUE(added);

    // Make sure the returned iterator references our range.
    ASSERT_NE(iterator, set.end());
    EXPECT_EQ(iterator->mBegin, 2);
    EXPECT_EQ(iterator->mEnd, 4);

    // Make sure the set recognizes it is no longer empty.
    EXPECT_FALSE(set.empty());
    EXPECT_EQ(set.size(), 1u);

    // Before: __AA____
    // Adding: _BB_____
    //  After: _BAA____
    std::tie(iterator, added) = std::invoke(add, set, FileRange(1, 3));

    EXPECT_TRUE(added);
    ASSERT_NE(iterator, set.end());
    EXPECT_EQ(iterator->mBegin, 1);
    EXPECT_EQ(iterator->mEnd, 2);
    EXPECT_EQ(set.size(), 2u);

    // Before: _BAA____
    // Adding: ___CC___
    //  After: _BAA____
    std::tie(iterator, added) = std::invoke(add, set, FileRange(3, 5));

    EXPECT_FALSE(added);
    ASSERT_NE(iterator, set.end());
    EXPECT_EQ(iterator->mBegin, 2);
    EXPECT_EQ(iterator->mEnd, 4);
    EXPECT_EQ(set.size(), 2u);

    // Before: _BAA____
    // Adding: DDDDD___
    //  After: DBAA____
    std::tie(iterator, added) = std::invoke(add, set, FileRange(0, 5));

    EXPECT_TRUE(added);
    ASSERT_NE(iterator, set.end());
    EXPECT_EQ(iterator->mBegin, 0);
    EXPECT_EQ(iterator->mEnd, 1);
    EXPECT_EQ(set.size(), 3u);

    // Before: DBAA____
    // Adding: ____CC__
    //  After: DBAACC__
    std::tie(iterator, added) = std::invoke(add, set, FileRange(4, 6));

    EXPECT_TRUE(added);
    ASSERT_NE(iterator, set.end());
    EXPECT_EQ(iterator->mBegin, 4);
    EXPECT_EQ(iterator->mEnd, 6);
    EXPECT_EQ(set.size(), 4u);
}

} // file_service
} // mega
