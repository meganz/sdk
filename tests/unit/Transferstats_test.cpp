/**
 * (c) 2024 by Mega Limited, Wellsford, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#include "mega/transferstats.h"

#include <gtest/gtest.h>

using namespace mega::stats::utils;

/**
 * @brief Unit test for mega::stats::utils::calculateMedian()
 */
TEST(TransferStatsTest, TestCalculateMedian)
{
    // Test with empty vector.
    {
        std::vector<m_off_t> values = {};
        ASSERT_EQ(calculateMedian(values), 0);
    }

    // Test with one element.
    {
        std::vector<m_off_t> values = {42};
        ASSERT_EQ(calculateMedian(values), 42);
    }

    // Test with two elements (even number).
    {
        std::vector<m_off_t> values = {10, 20};
        ASSERT_EQ(calculateMedian(values), 15); // (10 + 20) / 2 = 15
    }

    // Test with two elements requiring rounding.
    {
        std::vector<m_off_t> values = {10, 21};
        ASSERT_EQ(calculateMedian(values), 16); // (10 + 21) / 2 = 15.5 -> rounds to 16
    }

    // Test with odd number of elements.
    {
        std::vector<m_off_t> values = {5, 10, 15};
        ASSERT_EQ(calculateMedian(values), 10);
    }

    // Test with negative numbers.
    {
        std::vector<m_off_t> values = {-10, -5, 0, 5, 10};
        ASSERT_EQ(calculateMedian(values), 0);
    }

    // Test with larger vector.
    {
        std::vector<m_off_t> values = {1, 3, 5, 7, 9, 11};
        ASSERT_EQ(calculateMedian(values), 6); // (5 + 7) / 2 = 6
    }

    // Test with large numbers.
    {
        std::vector<m_off_t> values = {1000000000, 2000000000, 3000000000};
        ASSERT_EQ(calculateMedian(values), 2000000000);
    }

    // Test with large numbers and even size to check rounding
    {
        std::vector<m_off_t> values = {1000000000, 2000000001};
        // The median should be (1000000000 + 2000000001) / 2 = 1500000000.5 ->
        // rounds to 1500000001.
        ASSERT_EQ(calculateMedian(values), 1500000001);
    }
}

/**
 * @brief Unit test for mega::stats::utils::calculateWeightedAverage()
 */
TEST(TransferStatsTest, TestCalculateWeightedAverage)
{
    // Test with empty vectors.
    {
        std::vector<m_off_t> values = {};
        std::vector<m_off_t> weights = {};
        ASSERT_EQ(calculateWeightedAverage(values, weights), 0);
    }

    // Test with one element.
    {
        std::vector<m_off_t> values = {50};
        std::vector<m_off_t> weights = {2};
        ASSERT_EQ(calculateWeightedAverage(values, weights), 50);
    }

    // Test with zero weights.
    {
        std::vector<m_off_t> values = {10, 20, 30};
        std::vector<m_off_t> weights = {0, 0, 0};
        ASSERT_EQ(calculateWeightedAverage(values, weights), 0);
    }

    // Test with normal weights.
    {
        std::vector<m_off_t> values = {10, 20, 30};
        std::vector<m_off_t> weights = {1, 1, 1};
        ASSERT_EQ(calculateWeightedAverage(values, weights), 20);
    }

    // Test with varied weights.
    {
        std::vector<m_off_t> values = {10, 20, 30};
        std::vector<m_off_t> weights = {1, 2, 3};
        // Weighted average = (10*1 + 20*2 + 30*3) / (1 + 2 + 3)
        // = (10 + 40 + 90) / 6 = 140 / 6 = 23.3333 -> rounds to 23.
        ASSERT_EQ(calculateWeightedAverage(values, weights), 23);
    }

    // Test with negative values.
    {
        std::vector<m_off_t> values = {-10, -20, -30};
        std::vector<m_off_t> weights = {1, 2, 3};
        // Weighted average = (-10*1 + -20*2 + -30*3) / (1+2+3) = (-10 -40 -90)/6 =
        // -140/6 ≈ -23.3333 -> rounds to -23.
        assert(calculateWeightedAverage(values, weights) == -23);
    }

    // Test with weights summing to zero.
    {
        std::vector<m_off_t> values = {10, 20, 30};
        std::vector<m_off_t> weights = {1, -1, 0};
        // Total weight = 1 -1 + 0 = 0, should return 0.
        ASSERT_EQ(calculateWeightedAverage(values, weights), 0);
    }

    // Test where weighted sum is zero.
    {
        std::vector<m_off_t> values = {10, -10};
        std::vector<m_off_t> weights = {1, 1};
        // Weighted sum = 10*1 + (-10)*1 = 0, total weight = 1 + 1 = 2.
        ASSERT_EQ(calculateWeightedAverage(values, weights), 0);
    }

    // Test with large numbers
    {
        std::vector<m_off_t> values = {1000000000, 2000000000};
        std::vector<m_off_t> weights = {1, 3};
        // Weighted average = (1000000000*1 + 2000000000*3) / 4 =
        // (1000000000 + 6000000000)/4 = 7000000000/4 = 1750000000.
        ASSERT_EQ(calculateWeightedAverage(values, weights), 1750000000);
    }

    // Test with rounding up.
    {
        std::vector<m_off_t> values = {1, 2};
        std::vector<m_off_t> weights = {1, 1};
        // Weighted average = (1 + 2) / 2 = 1.5 -> rounds to 2.
        ASSERT_EQ(calculateWeightedAverage(values, weights), 2);
    }

    // Test with rounding down.
    {
        std::vector<m_off_t> values = {1, 2};
        std::vector<m_off_t> weights = {2, 1};
        // Weighted average = (1*2 + 2*1)/3 = (2 + 2)/3 =
        // 1.3333 -> rounds to 1.
        ASSERT_EQ(calculateWeightedAverage(values, weights), 1);
    }
}
