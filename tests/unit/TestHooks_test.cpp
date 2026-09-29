/**
 * @file TestHooks_test.cpp
 * @brief O-13 regression test for MegaTestHooks whole-struct assignment race.
 *
 * Under TSAN, stress the reset-vs-read race and ensure no diagnostic fires.
 * Without a sanitizer the test is a probabilistic smoke test using many
 * iterations (liveness check: reader thread must observe at least one hook
 * invocation during the race window).
 */

#include "mega/testhooks.h"

#include <gtest/gtest.h>

#include <atomic>
#include <string>
#include <thread>

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED

// The DEBUG_TEST_HOOK_* macros reference the mega-internal types HttpReq,
// RaidBufferManager, etc. unqualified, so the reader thread below must live
// inside `namespace mega` for the macros to expand cleanly. All existing
// call sites in the tree (src/*.cpp) also sit inside namespace mega, so this
// matches the production invocation context.
using namespace mega; // NOLINT(google-build-using-namespace)

#if defined(__SANITIZE_THREAD__) || \
    (defined(__has_feature) && __has_feature(thread_sanitizer))
#define SDK_TEST_WITH_TSAN 1
#else
#define SDK_TEST_WITH_TSAN 0
#endif

namespace
{
constexpr int kIterationsNoSan = 50000;
constexpr int kIterationsSan = 10000;
constexpr int kResetIterations = SDK_TEST_WITH_TSAN ? kIterationsSan : kIterationsNoSan;

void installAllHooks()
{
    // Serialize field-level writes against reader macros by taking the same
    // mutex the macros do. This ensures the installAllHooks writes are
    // data-race-free even during the hot reset loop below.
    std::lock_guard<std::mutex> g(mega::globalMegaTestHooks.mMutex);
    mega::globalMegaTestHooks.onDownloadFailed = [](mega::error) {};
    mega::globalMegaTestHooks.onHashcashCalculationStarted = []() {};
    mega::globalMegaTestHooks.onHookDeviceId = [](std::string&) {};
}
} // namespace

TEST(TestHooks, ConcurrentResetDoesNotRaceWithReaders)
{
    installAllHooks();
    std::atomic<bool> stopReaders{false};
    std::atomic<long> readsObserved{0};

    std::thread reader(
        [&]()
        {
            while (!stopReaders.load(std::memory_order_relaxed))
            {
                DEBUG_TEST_HOOK_DOWNLOAD_FAILED(API_EARGS);
                DEBUG_TEST_HOOK_HASHCASH_CALCULATION_STARTED;
                std::string id;
                DEBUG_TEST_HOOK_DEVICE_ID(id);
                readsObserved.fetch_add(1, std::memory_order_relaxed);
            }
        });

    for (int i = 0; i < kResetIterations; ++i)
    {
        installAllHooks();
        // Whole-struct move-assignment: this is the exact idiom used at
        // SdkTest_test.cpp:7368, :20644 (scoped destructor), and :26857
        // (MrProper destructor). Under TSAN, any remaining data race between
        // this line and the reader thread's macro invocations would fail the
        // test via TSAN's halt-on-error behavior.
        globalMegaTestHooks = MegaTestHooks();
    }

    stopReaders.store(true, std::memory_order_relaxed);
    reader.join();

    // Liveness check: if readsObserved is 0, the race surface is vacuous and
    // the test isn't really exercising anything.
    EXPECT_GT(readsObserved.load(), 0);
}

#endif // MEGASDK_DEBUG_TEST_HOOKS_ENABLED
