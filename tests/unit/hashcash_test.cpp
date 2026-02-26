/**
 * (c) 2024 by Mega Limited, New Zealand
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

#include <gtest/gtest.h>
#include <mega/canceller.h>
#include <mega/hashcash.h>

#include <future>
#include <thread>

using namespace mega;

namespace
{

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

const std::string kTokenHard{"K4QHo4I6XmnLNNsFqutTwObWZMClxf7ov--5OHLdGXSMHRwN8bLvrUTlpnhXVdtO"};
constexpr uint8_t kHighEasiness{200};
constexpr uint8_t kLowEasiness{5};
constexpr auto kCappedWorkers{1u};

constexpr auto kLowTtl{30ms};
constexpr auto kLargeTtl{15min};

struct RunResult
{
    std::string gencashRes;
    std::chrono::milliseconds elapsed;
};

auto run_gencash(const std::string& token,
                 const uint8_t easiness,
                 const std::chrono::milliseconds ttl,
                 const cancel_epoch_t epoch,
                 const unsigned workers) -> RunResult
{
    const auto start = Clock::now();
    auto gencashRes = gencash(token, easiness, ttl, epoch, workers);
    return {std::move(gencashRes),
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start)};
}

auto run_gencash_async = [](auto&&... args)
{
    auto ready = std::make_shared<std::promise<void>>();
    std::future<void> go = ready->get_future();

    auto fut =
        std::async(std::launch::async,
                   [ready, tup = std::make_tuple(std::forward<decltype(args)>(args)...)]() mutable
                   {
                       ready->set_value();
                       return std::apply(run_gencash, std::move(tup));
                   });
    go.wait();
    return fut;
};

auto expect_cancelled = [](const RunResult& r,
                           const cancel_epoch_t epoch,
                           const std::chrono::milliseconds upper,
                           const bool cancelTriggered = false)
{
    EXPECT_TRUE(r.gencashRes.empty());
    EXPECT_LT(r.elapsed, upper);
    EXPECT_EQ(ScopedCanceller(epoch).triggered(), cancelTriggered);
};

auto expect_completed = [](const RunResult& r, const cancel_epoch_t epoch)
{
    EXPECT_FALSE(r.gencashRes.empty());
    EXPECT_FALSE(ScopedCanceller(epoch).triggered());
};

} // namespace

TEST(Hashcash, Gencash)
{
    const std::vector<std::uint8_t> easinessV{180, 192};
    const std::vector<unsigned> numWorkersV{8u, 2u};
    const std::vector<std::string> hashcash{
        "wFqIT_wY3tYKcrm5zqwaUoWym3ZCz32cCsrJOgYBgihtpaWUhGyWJ--EY-zfwI-i",
        "3NIjq_fgu6bTyepwHuKiaB8a1YRjISBhktWK1fjhRx86RhOqKZNAcOZht0wJvmhQ",
        "HGztcvhT0sngIveS6C4CY1nx64YFtXnbcqX_Dvj7NxmX0SCNRlCZ51_pMWQgpHdv",
    };

    for (const auto& easiness: easinessV)
    {
        for (const auto& numWorkers: numWorkersV)
        {
            for (const auto& hc: hashcash)
            {
                const auto reqEpochSnapshot{cancel_epoch_snapshot()};
                const auto res = run_gencash(hc, easiness, kLargeTtl, reqEpochSnapshot, numWorkers);

                ASSERT_TRUE(validateHashcash(hc, easiness, res.gencashRes))
                    << "Failed hash: " << hc << ": genCashResult [easiness = " << +easiness
                    << ", numWorkers = " << numWorkers << "]";
                ASSERT_EQ(retryGencashData(), std::nullopt);
            }
        }
    }
}

TEST(Hashcash, CancelsDuringComputeReturnsQuickly)
{
    const auto reqEpochSnapshot{cancel_epoch_snapshot()};

    auto fut =
        run_gencash_async(kTokenHard, kLowEasiness, kLargeTtl, reqEpochSnapshot, kCappedWorkers);

    std::this_thread::sleep_for(200ms); // A bit more wait once we know that we are running
    cancel_epoch_bump();
    const auto res = fut.get();

    expect_cancelled(res, reqEpochSnapshot, 1s, true);
}

TEST(Hashcash, CancelBeforeStartSkipsDoesNotAffect)
{
    cancel_epoch_bump();

    const auto reqEpochSnapshot{cancel_epoch_snapshot()};

    const auto res = gencash(kTokenHard, kHighEasiness, reqEpochSnapshot);
    EXPECT_FALSE(res.empty());
}

// With very hard difficulty and test-shortened budget, we should early-exit.
TEST(Hashcash, BudgetEarlyExitWithoutCancel)
{
    const auto reqEpochSnapshot{cancel_epoch_snapshot()};

    const auto res = run_gencash(kTokenHard,
                                 kLowEasiness,
                                 kLowTtl, // intentionally too small
                                 reqEpochSnapshot,
                                 kCappedWorkers);

    expect_cancelled(res, reqEpochSnapshot, 2s);
}

TEST(Hashcash, BudgetEarlyExitWithoutCancelWRetries)
{
    const auto reqEpochSnapshot{cancel_epoch_snapshot()};

    // Warm up: gencash must succeed and reset previous retry data (if any)
    {
        const auto res = run_gencash(kTokenHard,
                                     kHighEasiness,
                                     kLargeTtl,
                                     reqEpochSnapshot,
                                     MAX_WORKERS_FOR_GENCASH);
        expect_completed(res, reqEpochSnapshot);

        ASSERT_EQ(retryGencashData(), std::nullopt);
    }

    // Force retry up to RetryGencash::kMaxRetries times
    for (unsigned i = 0; i < RetryGencash::kMaxRetries; ++i)
    {
        const auto res =
            run_gencash(kTokenHard, kLowEasiness, kLowTtl, reqEpochSnapshot, kCappedWorkers);

        expect_cancelled(res, reqEpochSnapshot, 2s);

        const auto retryData = retryGencashData();
        ASSERT_NE(retryData, std::nullopt);
        EXPECT_EQ(retryData->mForceRetryCount, i + 1);
        EXPECT_GE(res.elapsed, retryData->mBudget);
        EXPECT_GE(retryData->mGencashTime, retryData->mBudget);
    }

    // Attempt n RetryGencash::kMaxRetries won't trigger any more retries
    {
        const auto retryDataPre = retryGencashData();
        ASSERT_NE(retryDataPre, std::nullopt);
        EXPECT_EQ(retryDataPre->mForceRetryCount, RetryGencash::kMaxRetries);

        const auto res = run_gencash(kTokenHard,
                                     kHighEasiness,
                                     kLowTtl,
                                     reqEpochSnapshot,
                                     MAX_WORKERS_FOR_GENCASH);
        expect_completed(res, reqEpochSnapshot);

        const auto retryData = retryGencashData();
        ASSERT_NE(retryData, std::nullopt);
        EXPECT_EQ(retryData->mForceRetryCount, 0u);
    }

    // Attempt n RetryGencash::kMaxRetries+1 should reset any previous retryGencashData
    {
        const auto res = run_gencash(kTokenHard,
                                     kHighEasiness,
                                     kLargeTtl,
                                     reqEpochSnapshot,
                                     MAX_WORKERS_FOR_GENCASH);
        expect_completed(res, reqEpochSnapshot);

        EXPECT_EQ(retryGencashData(), std::nullopt);
    }
}
