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

using namespace mega::stats;

/**************\
*  TEST MEDIAN  *
\**************/

/**
 * @brief Tests calculateMedian function with an empty vector.
 */
TEST(TransferStatsTest, TestCalculateMedianEmptyVector)
{
    const std::vector<m_off_t> values = {};
    ASSERT_EQ(calculateMedian(values), 0);
}

/**
 * @brief Tests calculateMedian function with one element.
 */
TEST(TransferStatsTest, TestCalculateMedianOneElement)
{
    const std::vector<m_off_t> values = {42};
    ASSERT_EQ(calculateMedian(values), 42);
}

/**
 * @brief Tests calculateMedian function with two elements (even size).
 *
 * Ex: (10 + 20) / 2 = 15
 */
TEST(TransferStatsTest, TestCalculateMedianEvenNumberOfElements)
{
    const std::vector<m_off_t> values = {10, 20};
    ASSERT_EQ(calculateMedian(values), 15);
}

/**
 * @brief Tests calculateMedian function with two elements requiring rounding.
 *
 * Ex: (10 + 21) / 2 = 15.5 -> rounds to 16
 */
TEST(TransferStatsTest, TestCalculateMedianEvenNumberWithRounding)
{
    const std::vector<m_off_t> values = {10, 21};
    ASSERT_EQ(calculateMedian(values), 16);
}

/**
 * @brief Tests calculateMedian function with an odd number of elements.
 */
TEST(TransferStatsTest, TestCalculateMedianOddNumberOfElements)
{
    const std::vector<m_off_t> values = {5, 10, 15};
    ASSERT_EQ(calculateMedian(values), 10);
}

/**
 * @brief Tests calculateMedian function with negative numbers.
 */
TEST(TransferStatsTest, TestCalculateMedianNegativeNumbers)
{
    const std::vector<m_off_t> values = {-10, -5, 0, 5, 10};
    ASSERT_EQ(calculateMedian(values), 0);
}

/**
 * @brief Tests calculateMedian function with a larger vector.
 *
 * Ex: (5 + 7) / 2 = 6
 */
TEST(TransferStatsTest, TestCalculateMedianLargerVector)
{
    const std::vector<m_off_t> values = {1, 3, 5, 7, 9, 11};
    ASSERT_EQ(calculateMedian(values), 6);
}

/**
 * @brief Tests calculateMedian function with large numbers.
 */
TEST(TransferStatsTest, TestCalculateMedianLargeNumbers)
{
    const std::vector<m_off_t> values = {1000000000, 2000000000, 3000000000};
    ASSERT_EQ(calculateMedian(values), 2000000000);
}

/**
 * @brief Tests calculateMedian function with large numbers and an even-sized vector, checking for
 * rounding.
 *
 * Ex: (1000000000 + 2000000001) / 2 = 1500000000.5 ->
 * rounds to 1500000001.
 */
TEST(TransferStatsTest, TestCalculateMedianLargeNumbersEvenSizeWithRounding)
{
    const std::vector<m_off_t> values = {1000000000, 2000000001};
    ASSERT_EQ(calculateMedian(values), 1500000001);
}

/************************\
*  TEST WEIGHTED AVERAGE *
\************************/

/**
 * @brief Tests calculateWeightedAverage function with empty vectors.
 */
TEST(TransferStatsTest, TestCalculateWeightedAverageEmptyVectors)
{
    const std::vector<m_off_t> values = {};
    const std::vector<m_off_t> weights = {};
    ASSERT_EQ(calculateWeightedAverage(values, weights), 0);
}

/**
 * @brief Tests calculateWeightedAverage function with one element.
 */
TEST(TransferStatsTest, TestCalculateWeightedAverageOneElement)
{
    const std::vector<m_off_t> values = {50};
    const std::vector<m_off_t> weights = {2};
    ASSERT_EQ(calculateWeightedAverage(values, weights), 50);
}

/**
 * @brief Tests calculateWeightedAverage function with zero weights.
 */
TEST(TransferStatsTest, TestCalculateWeightedAverageZeroWeights)
{
    const std::vector<m_off_t> values = {10, 20, 30};
    const std::vector<m_off_t> weights = {0, 0, 0};
    ASSERT_EQ(calculateWeightedAverage(values, weights), 0);
}

/**
 * @brief Tests calculateWeightedAverage function with normal (equal) weights.
 */
TEST(TransferStatsTest, TestCalculateWeightedAverageNormalWeights)
{
    const std::vector<m_off_t> values = {10, 20, 30};
    const std::vector<m_off_t> weights = {1, 1, 1};
    ASSERT_EQ(calculateWeightedAverage(values, weights), 20);
}

/**
 * @brief Tests calculateWeightedAverage function with varied weights.
 *
 * Ex: Weighted average = (10*1 + 20*2 + 30*3) / (1 + 2 + 3)
 * = (10 + 40 + 90) / 6 = 140 / 6 = 23.3333 -> rounds to 23.
 */
TEST(TransferStatsTest, TestCalculateWeightedAverageVariedWeights)
{
    const std::vector<m_off_t> values = {10, 20, 30};
    const std::vector<m_off_t> weights = {1, 2, 3};
    ASSERT_EQ(calculateWeightedAverage(values, weights), 23);
}

/**
 * @brief Tests calculateWeightedAverage function with negative values.
 *
 * Ex: Weighted average = (-10*1 + -20*2 + -30*3) / (1+2+3) = (-10 -40 -90)/6 =
 * -140/6 ≈ -23.3333 -> rounds to -23.
 */
TEST(TransferStatsTest, TestCalculateWeightedAverageNegativeValues)
{
    const std::vector<m_off_t> values = {-10, -20, -30};
    const std::vector<m_off_t> weights = {1, 2, 3};
    ASSERT_EQ(calculateWeightedAverage(values, weights), -23);
}

/**
 * @brief Tests calculateWeightedAverage function when weights sum to zero.
 *
 * Ex: Total weight = 1 -1 + 0 = 0, should return 0.
 */
TEST(TransferStatsTest, TestCalculateWeightedAverageWeightsSummingToZero)
{
    const std::vector<m_off_t> values = {10, 20, 30};
    const std::vector<m_off_t> weights = {1, -1, 0};
    ASSERT_EQ(calculateWeightedAverage(values, weights), 0);
}

/**
 * @brief Tests calculateWeightedAverage function when the weighted sum is zero.
 *
 * Ex: Weighted sum = 10*1 + (-10)*1 = 0, total weight = 1 + 1 = 2
 */
TEST(TransferStatsTest, TestCalculateWeightedAverageWeightedSumIsZero)
{
    const std::vector<m_off_t> values = {10, -10};
    const std::vector<m_off_t> weights = {1, 1};
    ASSERT_EQ(calculateWeightedAverage(values, weights), 0);
}

/**
 * @brief Tests calculateWeightedAverage function with large numbers.
 *
 * Ex: Weighted average = (1000000000*1 + 2000000000*3) / 4 =
 * (1000000000 + 6000000000)/4 = 7000000000/4 = 1750000000.
 */
TEST(TransferStatsTest, TestCalculateWeightedAverageLargeNumbers)
{
    const std::vector<m_off_t> values = {1000000000, 2000000000};
    const std::vector<m_off_t> weights = {1, 3};
    ASSERT_EQ(calculateWeightedAverage(values, weights), 1750000000);
}

/**
 * @brief Tests calculateWeightedAverage function when the result rounds up.
 *
 * Ex: Weighted average = (1 + 2) / 2 = 1.5 -> rounds to 2.
 */
TEST(TransferStatsTest, TestCalculateWeightedAverageRoundingUp)
{
    const std::vector<m_off_t> values = {1, 2};
    const std::vector<m_off_t> weights = {1, 1};
    ASSERT_EQ(calculateWeightedAverage(values, weights), 2);
}

/**
 * @brief Tests calculateWeightedAverage function when the result rounds down.
 *
 * Ex: Weighted average = (1*2 + 2*1)/3 = (2 + 2)/3 =
 * 1.3333 -> rounds to 1.
 */
TEST(TransferStatsTest, TestCalculateWeightedAverageRoundingDown)
{
    const std::vector<m_off_t> values = {1, 2};
    const std::vector<m_off_t> weights = {2, 1};
    ASSERT_EQ(calculateWeightedAverage(values, weights), 1);
}