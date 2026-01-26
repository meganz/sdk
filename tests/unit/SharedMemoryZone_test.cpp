#include "mega/types.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <type_traits>
#include <vector>

using ::SharedResource;

TEST(SharedMemoryZone, OwnerThreadReadIsConst)
{
    SharedResource<std::vector<int>> zone;
    zone.initOwnerThread();

    const auto guard = zone.getReadOwnerThread();
    using DataRef = decltype(guard.getData());
    static_assert(std::is_const<std::remove_reference_t<DataRef>>::value,
                  "Read guard should expose const container");
}

TEST(SharedMemoryZone, OwnerThreadWriteAllowsMutation)
{
    SharedResource<std::vector<int>> zone;
    zone.initOwnerThread();

    {
        auto guard = zone.getWriteGuardOwnerThread();
        guard.getData().push_back(42);
    }

    const auto guard = zone.getReadOwnerThread();
    ASSERT_EQ(guard.getData().size(), 1u);
    EXPECT_EQ(guard.getData().front(), 42);
}

TEST(SharedMemoryZone, OtherThreadReadUsesMutexAndAllowsConstAccess)
{
    SharedResource<std::vector<int>> zone;
    zone.initOwnerThread();

    {
        auto guard = zone.getWriteGuardOwnerThread();
        guard.getData().push_back(7);
    }

    std::atomic<size_t> observedSize{0};
    std::thread reader(
        [&]()
        {
            const auto guard = zone.getReadOtherThread();
            observedSize = guard.getData().size();
        });

    reader.join();
    EXPECT_EQ(observedSize.load(), 1u);
}

TEST(SharedMemoryZone, OtherThreadReadIsConst)
{
    SharedResource<std::vector<int>> zone;
    zone.initOwnerThread();

    std::thread reader(
        [&]()
        {
            const auto guard = zone.getReadOtherThread();
            using DataRef = decltype(guard.getData());
            static_assert(std::is_const<std::remove_reference_t<DataRef>>::value,
                          "Read guard should expose const container");
        });

    reader.join();
}

TEST(SharedMemoryZone, OtherThreadWaitsForTimedMutex)
{
    SharedResource<std::vector<int>> zone;
    zone.initOwnerThread();
    std::thread reader;
    std::atomic<long long> elapsedMs{-1};
    {
        auto guard = zone.getWriteGuardOwnerThread();

        reader = std::thread{(
            [&]()
            {
                const auto start = std::chrono::steady_clock::now();
                const auto guard = zone.getReadOtherThread();
                const auto end = std::chrono::steady_clock::now();
                elapsedMs =
                    std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            })};

        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    }

    reader.join();
    ASSERT_GE(elapsedMs.load(), 1000);
}
