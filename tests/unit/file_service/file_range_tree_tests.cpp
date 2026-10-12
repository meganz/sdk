#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mega/file_service/file_range.h>
#include <mega/file_service/file_range_map.h>
#include <mega/file_service/file_range_set.h>
#include <mega/file_service/file_range_tree_utilities.h>
#include <mega/file_service/file_range_vector.h>
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

TEST(FileRangeMapTests, add)
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

TEST(FileRangeMapTests, add_with_iterators)
{
    static const std::pair<FileRange, int> elements[] = {{FileRange(0, 1), 2},
                                                         {FileRange(3, 4), 5},
                                                         {FileRange(6, 7), 8}}; // elements

    FileRangeMap<int> map(std::begin(elements), std::end(elements));

    for (auto& element: elements)
    {
        auto i = map.beginsAt(element.first.mBegin);
        auto j = map.endsAt(element.first.mEnd);

        ASSERT_NE(i, map.end());
        ASSERT_EQ(i, j);
        ASSERT_EQ(i->first, element.first);
        ASSERT_EQ(i->second, element.second);
    }
}

TEST(FileRangeSetTests, add)
{
    auto add = [](FileRangeSet& set, const FileRange& range)
    {
        return set.add(range);
    }; // add

    EXPECT_NO_FATAL_FAILURE(testAdd(std::move(add)));
}

TEST(FileRangeSetTests, add_with_iterators)
{
    static const FileRange ranges[] = {{1, 2}, {3, 4}, {5, 6}}; // ranges

    FileRangeSet set(std::begin(ranges), std::end(ranges));

    for (auto& range: ranges)
    {
        auto i = set.beginsAt(range.mBegin);
        auto j = set.endsAt(range.mEnd);

        ASSERT_NE(i, set.end());
        ASSERT_EQ(i, j);
        ASSERT_EQ(*i, range);
    }
}

TEST(FileRangeSetTests, constructor)
{
    FileRangeSet set;

    // A newly constructed set should be empty.
    EXPECT_EQ(set.begin(), set.end());
    EXPECT_TRUE(set.empty());
    EXPECT_EQ(set.size(), 0u);
}

TEST(FileRangeSetTests, copy_assignment)
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

TEST(FileRangeSetTests, copy_constructor)
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

TEST(FileRangeSetTests, find)
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

TEST(FileRangeSetTests, iteration)
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

TEST(FileRangeSetTests, move_assignment)
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

TEST(FileRangeSetTests, move_constructor)
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

TEST(FileRangeSetTests, remove_contained)
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

TEST(FileRangeSetTests, remove_multiple)
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

TEST(FileRangeSetTests, remove_single)
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

TEST(FileRangeSetTests, tryAdd)
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
    //  After: __AA____
    std::tie(iterator, added) = std::invoke(add, set, FileRange(1, 3));

    EXPECT_FALSE(added);
    ASSERT_NE(iterator, set.end());
    EXPECT_EQ(iterator->mBegin, 2);
    EXPECT_EQ(iterator->mEnd, 4);
    EXPECT_EQ(set.size(), 1u);

    // Before: __AA____
    // Adding: ___CC___
    //  After: __AA____
    std::tie(iterator, added) = std::invoke(add, set, FileRange(3, 5));

    EXPECT_FALSE(added);
    ASSERT_NE(iterator, set.end());
    EXPECT_EQ(iterator->mBegin, 2);
    EXPECT_EQ(iterator->mEnd, 4);
    EXPECT_EQ(set.size(), 1u);

    // Before: __AA____
    // Adding: DDDDD___
    //  After: __AA____
    std::tie(iterator, added) = std::invoke(add, set, FileRange(0, 5));

    EXPECT_FALSE(added);
    ASSERT_NE(iterator, set.end());
    EXPECT_EQ(iterator->mBegin, 2);
    EXPECT_EQ(iterator->mEnd, 4);
    EXPECT_EQ(set.size(), 1u);

    // Before: __AA____
    // Adding: ____CC__
    //  After: __AACC__
    std::tie(iterator, added) = std::invoke(add, set, FileRange(4, 6));

    EXPECT_TRUE(added);
    ASSERT_NE(iterator, set.end());
    EXPECT_EQ(iterator->mBegin, 4);
    EXPECT_EQ(iterator->mEnd, 6);
    EXPECT_EQ(set.size(), 2u);
}

struct FileRangeGapIteratorTests: ::testing::Test
{
    FileRangeSet mRanges;

    FileRangeGapIteratorTests():
        mRanges()
    {
        // Populate ranges.
        mRanges.add(16u, 24u);
        mRanges.add(32u, 40u);
        mRanges.add(48u, 56u);
    }

    // Return all gaps in ranges between begin and end.
    FileRangeVector gaps(std::uint64_t begin, std::uint64_t end)
    {
        auto i = file_service::gaps(mRanges, begin, end);
        return FileRangeVector(i, i.end());
    };
}; // FileRangeGapIteratorTests

// Convenience.
using ::testing::ElementsAre;

TEST_F(FileRangeGapIteratorTests, all_gaps_between_middle_of_two_gaps)
{
    EXPECT_THAT(gaps(30, 44), ElementsAre(FileRange(30, 32), FileRange(40, 44)));
}

TEST_F(FileRangeGapIteratorTests, all_gaps_from_middle_of_first_range)
{
    EXPECT_THAT(gaps(20, 64), ElementsAre(FileRange(24, 32), FileRange(40, 48), FileRange(56, 64)));
}

TEST_F(FileRangeGapIteratorTests, all_gaps_up_to_middle_of_last_range)
{
    EXPECT_THAT(gaps(0, 50), ElementsAre(FileRange(0, 16), FileRange(24, 32), FileRange(40, 48)));
}

TEST_F(FileRangeGapIteratorTests, all_gaps_within_larger_bounding_range)
{
    EXPECT_THAT(
        gaps(0, 64),
        ElementsAre(FileRange(0, 16), FileRange(24, 32), FileRange(40, 48), FileRange(56, 64)));
}

TEST_F(FileRangeGapIteratorTests, all_gaps_within_minimum_bounding_range)
{
    EXPECT_THAT(gaps(16, 56), ElementsAre(FileRange(24, 32), FileRange(40, 48)));
}

TEST_F(FileRangeGapIteratorTests, change_during_iteration)
{
    // So we can modify the set during gap iteration.
    auto ranges = mRanges;

    // Fill each gap in ranges.
    for (auto& gap: file_service::gaps(ranges, 8, 64))
    {
        // Fill the gap.
        auto iterator = ranges.merge(gap);

        // Make sure the gap was filled.
        ASSERT_LE(iterator->mBegin, gap.mBegin);
        ASSERT_GE(iterator->mEnd, gap.mEnd);
    }

    // We should now have a single large range.
    ASSERT_THAT(ranges, ElementsAre(FileRange(8, 64)));
}

TEST_F(FileRangeGapIteratorTests, no_gaps)
{
    EXPECT_THAT(gaps(16, 24), ElementsAre());
}

TEST_F(FileRangeGapIteratorTests, no_ranges)
{
    EXPECT_THAT(gaps(64, 128), ElementsAre(FileRange(64, 128)));
}

} // file_service
} // mega
