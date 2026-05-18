/**
 * @file BenchmarkRunners.cpp
 * @brief Bench-runner cluster implementation (extracted from SdkTest_test.cpp
 *        anonymous namespace; previously a tightly coupled file-local cluster
 *        of ~932 LOC).
 *
 * The 4 SdkBenchmarkTest cells (ManySmallUploads / 1kSmallUploads /
 * SingleLargeUpload / LargePlusManySmall) call into the 3 public runner
 * functions exposed in BenchmarkRunners.h. The helpers, structs, constants
 * and the optional `recordBenchCell` JSON-channel bridge live in this TU as
 * implementation detail (file-local-static or namespace-internal).
 *
 * Public namespace: `mega::test::benchmark`. Internal helpers stay anonymous-
 * namespace static.
 */

#include "benchmark/headers/BenchmarkRunners.h"

#include "SdkTest_test.h"
#include "../stdfs.h"
#include "integration_test_utils.h"
#include "mega/scoped_helpers.h"
#include "mega/testhooks.h"
#include "mega/types.h"
#include "megaapi.h"
#include "sdk_test_utils.h"
#include "test.h"
#include "wsupload/headers/WsUploadDebugHelpers.h"
#ifdef MEGA_BENCH_FRAMEWORK_ENABLED
#include "bench_framework/headers/BenchReportWriter.h"
#endif

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <ostream>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <sys/resource.h>
#endif

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
using ::mega::test::wsupload::fetchWsUploadStatsForTesting;
#endif

namespace mega::test::benchmark
{

static void includeTransferWindow(const TransferTracker& tracker,
                                  std::int64_t& firstStartMs,
                                  std::int64_t& lastFinishMs)
{
    const auto startMs = tracker.mStartSteadyMs.load();
    const auto finishMs = tracker.mFinishSteadyMs.load();
    if (startMs > 0 && (!firstStartMs || startMs < firstStartMs))
    {
        firstStartMs = startMs;
    }
    if (finishMs > lastFinishMs)
    {
        lastFinishMs = finishMs;
    }
}

static std::int64_t benchmarkSteadyMs()
{
    return static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

struct BenchDistribution
{
    std::int64_t mean = 0;
    std::int64_t median = 0;
    std::int64_t p95 = 0;
    std::int64_t min = 0;
    std::int64_t max = 0;
};

static BenchDistribution summarizeMsDistribution(std::vector<std::int64_t> values)
{
    EXPECT_FALSE(values.empty());
    if (values.empty())
    {
        return {};
    }

    std::sort(values.begin(), values.end());
    std::int64_t sum = 0;
    for (const auto value: values)
    {
        sum += value;
    }

    BenchDistribution out;
    out.mean = sum / static_cast<std::int64_t>(values.size());
    out.median = values[values.size() / 2];
    out.p95 = values[std::min(values.size() - 1, (values.size() * 95) / 100)];
    out.min = values.front();
    out.max = values.back();
    return out;
}

struct BenchTimingSummary
{
    std::int64_t firstStartMs = 0;
    std::int64_t lastFinishMs = 0;
    std::int64_t lastPutnodesStartMs = 0;
    std::int64_t callbackTransferMs = 0;
    std::int64_t pureTransferMs = 0;
    std::int64_t putnodesOverheadMs = 0;
    std::int64_t completeTransferMs = 0;
    BenchDistribution perTransferPureTransferMs;
    BenchDistribution perTransferPutnodesOverheadMs;
};

class BenchPutnodesTimingRecorder
{
public:
    BenchPutnodesTimingRecorder()
    {
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
        globalMegaTestHooks.onUploadPutnodesStarted =
            [this](const int tag)
        {
            std::lock_guard<std::mutex> g(mMutex);
            mPutnodesStartMsByTag[tag] = benchmarkSteadyMs();
        };
#endif
    }

    ~BenchPutnodesTimingRecorder()
    {
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
        globalMegaTestHooks.onUploadPutnodesStarted = nullptr;
#endif
    }

    std::int64_t putnodesStartMsForTag(const int tag) const
    {
        std::lock_guard<std::mutex> g(mMutex);
        const auto it = mPutnodesStartMsByTag.find(tag);
        return it == mPutnodesStartMsByTag.end() ? 0 : it->second;
    }

private:
    mutable std::mutex mMutex;
    std::unordered_map<int, std::int64_t> mPutnodesStartMsByTag;
};

static BenchTimingSummary summarizeBenchTimings(
    const std::vector<const TransferTracker*>& trackers,
    const BenchPutnodesTimingRecorder& putnodesRecorder)
{
    BenchTimingSummary summary;
    std::vector<std::int64_t> perPureMs;
    std::vector<std::int64_t> perPutnodesMs;
    perPureMs.reserve(trackers.size());
    perPutnodesMs.reserve(trackers.size());

    for (const auto* tracker: trackers)
    {
        if (!tracker)
        {
            ADD_FAILURE() << "Null transfer tracker";
            continue;
        }
        const auto startMs = tracker->mStartSteadyMs.load();
        const auto finishMs = tracker->mFinishSteadyMs.load();
        const auto tag = tracker->mTag.load();
        const auto putnodesMs = putnodesRecorder.putnodesStartMsForTag(tag);

        EXPECT_GT(startMs, 0) << "Missing onTransferStart timestamp for tag " << tag;
        EXPECT_GT(finishMs, startMs) << "Missing onTransferFinish timestamp for tag " << tag;
        EXPECT_GT(tag, 0) << "Missing transfer tag for timing boundary";
        EXPECT_GE(putnodesMs, startMs) << "Missing pre-putnodes timestamp for tag " << tag;
        EXPECT_GE(finishMs, putnodesMs) << "Putnodes timestamp is after transfer finish for tag "
                                        << tag;
        if (startMs <= 0 || finishMs <= startMs || tag <= 0 || putnodesMs < startMs ||
            finishMs < putnodesMs)
        {
            continue;
        }

        if (!summary.firstStartMs || startMs < summary.firstStartMs)
        {
            summary.firstStartMs = startMs;
        }
        summary.lastFinishMs = std::max(summary.lastFinishMs, finishMs);
        summary.lastPutnodesStartMs = std::max(summary.lastPutnodesStartMs, putnodesMs);
        perPureMs.push_back(putnodesMs - startMs);
        perPutnodesMs.push_back(finishMs - putnodesMs);
    }

    EXPECT_GT(summary.firstStartMs, 0);
    EXPECT_GT(summary.lastFinishMs, summary.firstStartMs);
    EXPECT_GE(summary.lastPutnodesStartMs, summary.firstStartMs);
    EXPECT_GE(summary.lastFinishMs, summary.lastPutnodesStartMs);
    if (summary.firstStartMs <= 0 || summary.lastFinishMs <= summary.firstStartMs ||
        summary.lastPutnodesStartMs < summary.firstStartMs ||
        summary.lastFinishMs < summary.lastPutnodesStartMs)
    {
        return summary;
    }

    summary.callbackTransferMs = summary.lastFinishMs - summary.firstStartMs;
    summary.pureTransferMs = summary.lastPutnodesStartMs - summary.firstStartMs;
    summary.putnodesOverheadMs = summary.lastFinishMs - summary.lastPutnodesStartMs;
    summary.completeTransferMs = summary.callbackTransferMs;
    summary.perTransferPureTransferMs = summarizeMsDistribution(std::move(perPureMs));
    summary.perTransferPutnodesOverheadMs = summarizeMsDistribution(std::move(perPutnodesMs));
    return summary;
}

static void appendBenchTimingFields(std::ostream& out, const BenchTimingSummary& summary)
{
    out << " callbackTransferMs=" << summary.callbackTransferMs
        << " pureTransferMs=" << summary.pureTransferMs
        << " putnodesOverheadMs=" << summary.putnodesOverheadMs
        << " completeTransferMs=" << summary.completeTransferMs
        << " perTransferPureTransferMeanMs=" << summary.perTransferPureTransferMs.mean
        << " perTransferPureTransferMedianMs=" << summary.perTransferPureTransferMs.median
        << " perTransferPureTransferP95Ms=" << summary.perTransferPureTransferMs.p95
        << " perTransferPureTransferMinMs=" << summary.perTransferPureTransferMs.min
        << " perTransferPureTransferMaxMs=" << summary.perTransferPureTransferMs.max
        << " perTransferPutnodesOverheadMeanMs="
        << summary.perTransferPutnodesOverheadMs.mean
        << " perTransferPutnodesOverheadMedianMs="
        << summary.perTransferPutnodesOverheadMs.median
        << " perTransferPutnodesOverheadP95Ms=" << summary.perTransferPutnodesOverheadMs.p95
        << " perTransferPutnodesOverheadMinMs=" << summary.perTransferPutnodesOverheadMs.min
        << " perTransferPutnodesOverheadMaxMs=" << summary.perTransferPutnodesOverheadMs.max
        << " lastPutnodesStartMs=" << summary.lastPutnodesStartMs;
}

// SDK-5360 Goal 1: per-bench-cell RSS + CPU sampling via getrusage(RUSAGE_SELF).
// Bench-harness-only; no engine instrumentation needed. Windows is a no-op.
struct BenchProcessStatsSample
{
    std::uint64_t maxRssKB = 0;
    std::uint64_t userCpuMs = 0;
    std::uint64_t sysCpuMs = 0;
};

static BenchProcessStatsSample captureBenchProcessStats()
{
    BenchProcessStatsSample s;
#ifndef _WIN32
    struct rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) == 0)
    {
        s.maxRssKB = static_cast<std::uint64_t>(ru.ru_maxrss);
        s.userCpuMs = static_cast<std::uint64_t>(ru.ru_utime.tv_sec) * 1000ull
                      + static_cast<std::uint64_t>(ru.ru_utime.tv_usec) / 1000ull;
        s.sysCpuMs = static_cast<std::uint64_t>(ru.ru_stime.tv_sec) * 1000ull
                     + static_cast<std::uint64_t>(ru.ru_stime.tv_usec) / 1000ull;
    }
#endif
    return s;
}

static void logBenchProcessStatsDelta(const char* tag,
                                       const BenchProcessStatsSample& start,
                                       const BenchProcessStatsSample& end)
{
    const long long rssDeltaKB =
        static_cast<long long>(end.maxRssKB) - static_cast<long long>(start.maxRssKB);
    LOG_info << "[BenchProcessStats] " << tag
             << " startMaxRssKB=" << start.maxRssKB
             << " endMaxRssKB=" << end.maxRssKB
             << " maxRssDeltaKB=" << rssDeltaKB
             << " userCpuMs=" << (end.userCpuMs - start.userCpuMs)
             << " sysCpuMs=" << (end.sysCpuMs - start.sysCpuMs);
}

#ifdef MEGA_BENCH_FRAMEWORK_ENABLED
// Wires bench-runner results to the bench_framework JSON channel. Inert on builds
// without MEGA_BENCH_FRAMEWORK_ENABLED.
static void recordBenchCell(const char* name,
                            std::int64_t fileSizeMib,
                            unsigned connections,
                            std::int64_t totalMs,
                            double aggregateKBps,
                            const BenchTimingSummary& timing,
                            const BenchProcessStatsSample& procStart,
                            const BenchProcessStatsSample& procEnd,
                            std::size_t chunkSamples)
{
    ::mega::bench::BenchReportCell cell;
    cell.name = name;
    cell.fileSizeMib = fileSizeMib;
    cell.connections = connections;
    cell.durationMs = totalMs;
    cell.aggregateKbps = aggregateKBps;
    cell.firstByteMs = timing.firstStartMs;
    cell.lastByteMs = timing.lastFinishMs;
    cell.rssCpuDelta.rssMaxKb =
        static_cast<std::int64_t>(procEnd.maxRssKB) - static_cast<std::int64_t>(procStart.maxRssKB);
    cell.rssCpuDelta.userCpuMs =
        static_cast<std::int64_t>(procEnd.userCpuMs) - static_cast<std::int64_t>(procStart.userCpuMs);
    cell.rssCpuDelta.sysCpuMs =
        static_cast<std::int64_t>(procEnd.sysCpuMs) - static_cast<std::int64_t>(procStart.sysCpuMs);
    cell.rssCpuDelta.sampled = true;
    cell.chunkMsDist.min = static_cast<double>(timing.perTransferPureTransferMs.min);
    cell.chunkMsDist.max = static_cast<double>(timing.perTransferPureTransferMs.max);
    cell.chunkMsDist.mean = static_cast<double>(timing.perTransferPureTransferMs.mean);
    cell.chunkMsDist.median = static_cast<double>(timing.perTransferPureTransferMs.median);
    cell.chunkMsDist.p95 = static_cast<double>(timing.perTransferPureTransferMs.p95);
    cell.chunkMsDist.n = chunkSamples;
    ::mega::bench::BenchReportWriter::instance().recordCell(cell);
}
#endif

void runSmallUploadsBenchmark(SdkTest& test,
                                     const size_t fileCount,
                                     const char* testName,
                                     const char* summaryTag)
{
    constexpr size_t kFileSize = 1 * 1024 * 1024; // 1 MiB
    constexpr int kTimeoutS = 600; // per-file wait cap

    LOG_info << "___TEST___ " << testName;
    ASSERT_NO_FATAL_FAILURE(test.getAccountsForTest(1));

    auto accountRestorer = scopedToPro(*test.megaApi[0]);
    ASSERT_EQ(result(accountRestorer), API_OK);

    std::unique_ptr<MegaNode> rootnode{test.megaApi[0]->getRootNode()};
    ASSERT_NE(rootnode, nullptr);

    const std::string folderName =
        std::string("bench_smallmany_") + testName + "_" +
        std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    const MegaHandle folderHandle = test.createFolder(0, folderName.c_str(), rootnode.get());
    ASSERT_NE(folderHandle, UNDEF);
    std::unique_ptr<MegaNode> folder{test.megaApi[0]->getNodeByHandle(folderHandle)};
    ASSERT_NE(folder, nullptr);

    const fs::path tmpDir = fs::temp_directory_path() / ("bench_" + folderName);
    fs::create_directories(tmpDir);
    const auto cleanup = makeScopedDestructor(
        [tmpDir]()
        {
            std::error_code ec;
            fs::remove_all(tmpDir, ec);
        });
    std::vector<sdk_test::LocalTempFile> localFiles;
    localFiles.reserve(fileCount);
    for (size_t i = 0; i < fileCount; ++i)
    {
        localFiles.emplace_back(tmpDir / ("f" + std::to_string(i) + ".bin"), kFileSize);
    }

    std::vector<std::unique_ptr<TransferTracker>> trackers;
    trackers.reserve(fileCount);
    MegaUploadOptions uploadOptions;
    uploadOptions.mtime = ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME;
    BenchPutnodesTimingRecorder putnodesRecorder;

    const auto procStatsStart = captureBenchProcessStats();
    const auto apiStart = std::chrono::steady_clock::now();
    for (size_t i = 0; i < fileCount; ++i)
    {
        trackers.emplace_back(std::make_unique<TransferTracker>(test.megaApi[0].get()));
        test.megaApi[0]->startUpload(localFiles[i].getPath().string(),
                                     folder.get(),
                                     nullptr /*cancelToken*/,
                                     &uploadOptions,
                                     trackers.back().get());
    }

    std::vector<double> perFileKBps;
    perFileKBps.reserve(fileCount);
    for (size_t i = 0; i < fileCount; ++i)
    {
        const ErrorCodes res = trackers[i]->waitForResult(kTimeoutS);
        ASSERT_EQ(res, API_OK) << "Upload " << i << " failed with code " << res;
        perFileKBps.push_back(static_cast<double>(trackers[i]->mTransferMeanSpeed) / 1024.0);
    }
    const auto apiEnd = std::chrono::steady_clock::now();
    const auto procStatsEnd = captureBenchProcessStats();

    std::int64_t firstTransferStartMs = 0;
    std::int64_t lastTransferFinishMs = 0;
    std::vector<const TransferTracker*> timingTrackers;
    timingTrackers.reserve(trackers.size());
    for (const auto& tracker: trackers)
    {
        includeTransferWindow(*tracker, firstTransferStartMs, lastTransferFinishMs);
        timingTrackers.push_back(tracker.get());
    }
    ASSERT_GT(firstTransferStartMs, 0);
    ASSERT_GT(lastTransferFinishMs, firstTransferStartMs);
    const auto timingSummary = summarizeBenchTimings(timingTrackers, putnodesRecorder);

    const auto totalMs = lastTransferFinishMs - firstTransferStartMs;
    const auto apiTotalMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(apiEnd - apiStart).count();
    const double totalKiB = static_cast<double>(fileCount) * kFileSize / 1024.0;
    const double aggregateKBps =
        (totalMs > 0) ? (totalKiB * 1000.0) / static_cast<double>(totalMs) : 0.0;

    std::vector<double> sorted = perFileKBps;
    std::sort(sorted.begin(), sorted.end());
    const double minKBps = sorted.front();
    const double maxKBps = sorted.back();
    const double medianKBps = sorted[sorted.size() / 2];
    double sumKBps = 0.0;
    for (const double v: sorted)
        sumKBps += v;
    const double avgKBps = sumKBps / static_cast<double>(sorted.size());

    std::ostringstream summary;
    summary << summaryTag << " files=" << fileCount << " fileSize=" << kFileSize
            << " totalMs=" << totalMs << " aggregateKBps=" << aggregateKBps
            << " apiTotalMs=" << apiTotalMs
            << " avgKBps=" << avgKBps << " medianKBps=" << medianKBps
            << " minKBps=" << minKBps << " maxKBps=" << maxKBps;
    appendBenchTimingFields(summary, timingSummary);
    LOG_info << summary.str();

#if defined(MEGA_USE_WSUPLOAD) && defined(MEGASDK_DEBUG_TEST_HOOKS_ENABLED)
    ws::UploadEngine::WsUploadStatsForTesting stats;
    if (fetchWsUploadStatsForTesting(*test.megaApi[0], stats, 5) && stats.found)
    {
        LOG_info << "[WsUploadStats] files=" << fileCount
                 << " pools=" << stats.poolCount
                 << " uploadingFileOccupiedMs=" << stats.uploadingFileOccupiedMs
                 << " lastAckToNextFirstByteSamples="
                 << stats.lastAckToNextFirstByteSamples
                 << " lastAckToNextFirstByteTotalMs="
                 << stats.lastAckToNextFirstByteTotalMs
                 << " lastAckToNextFirstByteMaxMs=" << stats.lastAckToNextFirstByteMaxMs
                 << " allChunksInFlightBlockedMs=" << stats.allChunksInFlightBlockedMs
                 << " eligibleFileSampleCount=" << stats.eligibleFileSampleCount
                 << " blockedByInFlightSampleCount="
                 << stats.blockedByInFlightSampleCount
                 << " idleEligibleConnectionMs=" << stats.idleEligibleConnectionMs
                 << " idleEligibleConnectionSampleCount="
                 << stats.idleEligibleConnectionSampleCount
                 << " curlAgainSendCount=" << stats.curlAgainSendCount
                 << " curlAgainRecvCount=" << stats.curlAgainRecvCount
                 << " haveSpaceFalseIters=" << stats.haveSpaceFalseIters
                 << " haveSpaceFalseWaitMs=" << stats.haveSpaceFalseWaitMs
                 << " readyForDataFalseIters=" << stats.readyForDataFalseIters
                 << " readyForDataFalseWaitMs=" << stats.readyForDataFalseWaitMs
                 << " throttleSleepIters=" << stats.throttleSleepIters
                 << " throttleSleepMs=" << stats.throttleSleepMs
                 << " backlogEmptyIters=" << stats.backlogEmptyIters
                 << " backlogEmptyMs=" << stats.backlogEmptyMs
                 << " bufferedAmountHighWater=" << stats.bufferedAmountHighWater
                 << " chunksInFlightHighWater=" << stats.chunksInFlightHighWater
                 << " chunkPrepTotalMs=" << stats.chunkPrepTotalMs
                 << " chunkPrepMaxMs=" << stats.chunkPrepMaxMs
                 << " chunkPrepN=" << stats.chunkPrepN
                 << " throttleEventCount=" << stats.throttleEventCount
                 << " throttleEventMinDs=" << stats.throttleEventMinDs
                 << " throttleEventMaxDs=" << stats.throttleEventMaxDs
                 << " throttleEventMeanDs=" << (stats.throttleEventCount ? static_cast<double>(stats.throttleEventTotalDs) / static_cast<double>(stats.throttleEventCount) : 0.0)
                 << " throttleEventStdevDs=" << (stats.throttleEventCount > 1 ? std::sqrt(static_cast<double>(stats.throttleEventSumSqDs) / static_cast<double>(stats.throttleEventCount) - std::pow(static_cast<double>(stats.throttleEventTotalDs) / static_cast<double>(stats.throttleEventCount), 2.0)) : 0.0)
                 << " throttleBucket0to1s=" << stats.throttleBucket0to1s
                 << " throttleBucket1to5s=" << stats.throttleBucket1to5s
                 << " throttleBucket5to30s=" << stats.throttleBucket5to30s
                 << " throttleBucket30sPlus=" << stats.throttleBucket30sPlus
                 << " throttleEventCode6=" << stats.throttleEventCodeCounts[6]
                 << " simulThrottledConnsMax=" << stats.simultaneousThrottledConnsMax
                 << " simulThrottledConnsMean=" << (stats.simultaneousThrottledConnsSamples ? static_cast<double>(stats.simultaneousThrottledConnsSum) / static_cast<double>(stats.simultaneousThrottledConnsSamples) : 0.0)
                 << " throttleRecoveryAckSamples=" << stats.throttleRecoveryAckSamples
                 << " throttleRecoveryAckMeanMs=" << (stats.throttleRecoveryAckSamples ? static_cast<double>(stats.throttleRecoveryAckTotalMs) / static_cast<double>(stats.throttleRecoveryAckSamples) : 0.0)
                 << " throttleRecoveryAckMaxMs=" << stats.throttleRecoveryAckMaxMs;
    }
#endif

    logBenchProcessStatsDelta(testName, procStatsStart, procStatsEnd);
#ifdef MEGA_BENCH_FRAMEWORK_ENABLED
    recordBenchCell(testName,
                    /*fileSizeMib=*/static_cast<std::int64_t>(kFileSize / (1024 * 1024)),
                    /*connections=*/0,
                    /*totalMs=*/static_cast<std::int64_t>(totalMs),
                    aggregateKBps,
                    timingSummary,
                    procStatsStart,
                    procStatsEnd,
                    /*chunkSamples=*/fileCount);
#endif
    test.deleteFolder(folderName);
}

static constexpr std::uintmax_t kBenchMiB = 1024ull * 1024ull;
static constexpr std::uintmax_t kBenchGiB = 1024ull * kBenchMiB;
static constexpr std::uintmax_t kBenchLargeFileSize = 10ull * kBenchGiB;
static constexpr std::uintmax_t kBenchRequiredFreeBytes = 25ull * kBenchGiB;
static constexpr size_t kBenchSmallFileCount = 500;
static constexpr size_t kBenchSmallFileSize = 1 * 1024 * 1024;

static fs::path benchStagingRoot()
{
    // Use the test's PID-specific process folder ($HOME/mega_tests/pid_<PID>/ on
    // Linux, c:\tmp\mega_tests\pid_<PID>\ on Windows, $WORKSPACE-derived on
    // Jenkins). Never hardcode a user-specific path: that breaks on Jenkins and
    // on any machine where the runner user is not the SDK author.
    return TestFS::GetProcessFolder() / "bench_staging";
}

static std::string benchUniqueSuffix(const char* testName)
{
    return std::string{testName} + "_" +
           std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
}

static std::uint64_t benchUploadContentSeed(const std::string& uniqueSuffix,
                                          const std::uint64_t salt)
{
    std::uint64_t hash = 1469598103934665603ull ^ salt;
    for (const char rawCh: uniqueSuffix)
    {
        hash ^= static_cast<unsigned char>(rawCh);
        hash *= 1099511628211ull;
    }
    return hash;
}

static void requireBenchStagingSpace()
{
    const auto root = benchStagingRoot();
    std::error_code ec;
    fs::create_directories(root, ec);
    ASSERT_FALSE(ec) << "Cannot create bench staging root: " << root << ": "
                     << ec.message();

    const auto space = fs::space(root, ec);
    ASSERT_FALSE(ec) << "Cannot inspect bench staging space: " << root << ": "
                     << ec.message();
    ASSERT_GE(space.available, kBenchRequiredFreeBytes)
        << "Need at least " << kBenchRequiredFreeBytes
        << " bytes available for bench large-file staging under " << root
        << ", got " << space.available;
}

static void createDenseDeterministicFile(const fs::path& path,
                                         const std::uintmax_t sizeBytes,
                                         const std::uint64_t seed)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    ASSERT_FALSE(ec) << "Cannot create parent directory for " << path << ": " << ec.message();

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out) << "Cannot create dense benchmark file: " << path;

    constexpr std::size_t kBlockSize = 4 * 1024 * 1024;
    std::vector<char> block(kBlockSize);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& ch: block)
    {
        ch = static_cast<char>(dist(rng));
    }

    for (std::uintmax_t remaining = sizeBytes; remaining > 0;)
    {
        const auto toWrite =
            static_cast<std::size_t>(std::min<std::uintmax_t>(remaining, block.size()));
        out.write(block.data(), static_cast<std::streamsize>(toWrite));
        ASSERT_TRUE(out) << "Failed while writing dense benchmark file: " << path;
        remaining -= toWrite;
    }
}

static double aggregateKBpsForBytes(const std::uintmax_t bytes, const std::int64_t totalMs)
{
    return totalMs > 0 ? (static_cast<double>(bytes) / 1024.0 * 1000.0) /
                             static_cast<double>(totalMs) :
                         0.0;
}

static void appendTrackerMeanKBps(const TransferTracker& tracker, std::vector<double>& out)
{
    out.push_back(static_cast<double>(tracker.mTransferMeanSpeed) / 1024.0);
}

static void summarizeKBps(const std::vector<double>& values,
                          double& avg,
                          double& median,
                          double& min,
                          double& max)
{
    ASSERT_FALSE(values.empty());

    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    double sum = 0.0;
    for (const double v: sorted)
    {
        sum += v;
    }

    avg = sum / static_cast<double>(sorted.size());
    median = sorted[sorted.size() / 2];
    min = sorted.front();
    max = sorted.back();
}

static void verifyUploadedFile(SdkTest& test,
                               const MegaHandle nodeHandle,
                               const std::string& expectedName,
                               const std::uintmax_t expectedSize)
{
    ASSERT_NE(nodeHandle, ::mega::INVALID_HANDLE);
    std::unique_ptr<MegaNode> uploadedNode(test.megaApi[0]->getNodeByHandle(nodeHandle));
    ASSERT_NE(uploadedNode, nullptr);
    ASSERT_STREQ(expectedName.c_str(), uploadedNode->getName());
    ASSERT_EQ(uploadedNode->getSize(), static_cast<int64_t>(expectedSize));
}

static void logBenchWsStats(SdkTest& test, const size_t fileCount)
{
#if defined(MEGA_USE_WSUPLOAD) && defined(MEGASDK_DEBUG_TEST_HOOKS_ENABLED)
    ws::UploadEngine::WsUploadStatsForTesting stats;
    if (fetchWsUploadStatsForTesting(*test.megaApi[0], stats, 5) && stats.found)
    {
        LOG_info << "[WsUploadStats] files=" << fileCount
                 << " pools=" << stats.poolCount
                 << " uploadingFileOccupiedMs=" << stats.uploadingFileOccupiedMs
                 << " lastAckToNextFirstByteSamples="
                 << stats.lastAckToNextFirstByteSamples
                 << " lastAckToNextFirstByteTotalMs="
                 << stats.lastAckToNextFirstByteTotalMs
                 << " lastAckToNextFirstByteMaxMs=" << stats.lastAckToNextFirstByteMaxMs
                 << " allChunksInFlightBlockedMs=" << stats.allChunksInFlightBlockedMs
                 << " eligibleFileSampleCount=" << stats.eligibleFileSampleCount
                 << " blockedByInFlightSampleCount="
                 << stats.blockedByInFlightSampleCount
                 << " idleEligibleConnectionMs=" << stats.idleEligibleConnectionMs
                 << " idleEligibleConnectionSampleCount="
                 << stats.idleEligibleConnectionSampleCount
                 << " curlAgainSendCount=" << stats.curlAgainSendCount
                 << " curlAgainRecvCount=" << stats.curlAgainRecvCount
                 << " haveSpaceFalseIters=" << stats.haveSpaceFalseIters
                 << " haveSpaceFalseWaitMs=" << stats.haveSpaceFalseWaitMs
                 << " readyForDataFalseIters=" << stats.readyForDataFalseIters
                 << " readyForDataFalseWaitMs=" << stats.readyForDataFalseWaitMs
                 << " throttleSleepIters=" << stats.throttleSleepIters
                 << " throttleSleepMs=" << stats.throttleSleepMs
                 << " backlogEmptyIters=" << stats.backlogEmptyIters
                 << " backlogEmptyMs=" << stats.backlogEmptyMs
                 << " bufferedAmountHighWater=" << stats.bufferedAmountHighWater
                 << " chunksInFlightHighWater=" << stats.chunksInFlightHighWater
                 << " chunkPrepTotalMs=" << stats.chunkPrepTotalMs
                 << " chunkPrepMaxMs=" << stats.chunkPrepMaxMs
                 << " chunkPrepN=" << stats.chunkPrepN
                 << " throttleEventCount=" << stats.throttleEventCount
                 << " throttleEventMinDs=" << stats.throttleEventMinDs
                 << " throttleEventMaxDs=" << stats.throttleEventMaxDs
                 << " throttleEventMeanDs=" << (stats.throttleEventCount ? static_cast<double>(stats.throttleEventTotalDs) / static_cast<double>(stats.throttleEventCount) : 0.0)
                 << " throttleEventStdevDs=" << (stats.throttleEventCount > 1 ? std::sqrt(static_cast<double>(stats.throttleEventSumSqDs) / static_cast<double>(stats.throttleEventCount) - std::pow(static_cast<double>(stats.throttleEventTotalDs) / static_cast<double>(stats.throttleEventCount), 2.0)) : 0.0)
                 << " throttleBucket0to1s=" << stats.throttleBucket0to1s
                 << " throttleBucket1to5s=" << stats.throttleBucket1to5s
                 << " throttleBucket5to30s=" << stats.throttleBucket5to30s
                 << " throttleBucket30sPlus=" << stats.throttleBucket30sPlus
                 << " throttleEventCode6=" << stats.throttleEventCodeCounts[6]
                 << " simulThrottledConnsMax=" << stats.simultaneousThrottledConnsMax
                 << " simulThrottledConnsMean=" << (stats.simultaneousThrottledConnsSamples ? static_cast<double>(stats.simultaneousThrottledConnsSum) / static_cast<double>(stats.simultaneousThrottledConnsSamples) : 0.0)
                 << " throttleRecoveryAckSamples=" << stats.throttleRecoveryAckSamples
                 << " throttleRecoveryAckMeanMs=" << (stats.throttleRecoveryAckSamples ? static_cast<double>(stats.throttleRecoveryAckTotalMs) / static_cast<double>(stats.throttleRecoveryAckSamples) : 0.0)
                 << " throttleRecoveryAckMaxMs=" << stats.throttleRecoveryAckMaxMs;
    }
#else
    (void)test;
    (void)fileCount;
#endif
}

void runSingleLargeUploadBenchmark(SdkTest& test)
{
    constexpr int kTimeoutS = 4 * 60 * 60;
    constexpr const char* kTestName = "SdkTestBenchmarkSingleLargeUpload";

    LOG_info << "___TEST___ " << kTestName;
    ASSERT_NO_FATAL_FAILURE(test.getAccountsForTest(1));
    ASSERT_NO_FATAL_FAILURE(requireBenchStagingSpace());

    // Env-var override of upload connection count.
    if (const char* envConns = std::getenv("MEGA_BENCH_UPLOAD_CONNECTIONS"))
    {
        const int n = std::atoi(envConns);
        if (n > 0)
        {
            ASSERT_EQ(API_OK, test.doSetMaxConnections(0, n));
            LOG_info << "[BenchSingleLargeUpload] connections override = " << n;
        }
    }

    auto accountRestorer = scopedToPro(*test.megaApi[0]);
    ASSERT_EQ(result(accountRestorer), API_OK);

    std::unique_ptr<MegaNode> rootnode{test.megaApi[0]->getRootNode()};
    ASSERT_NE(rootnode, nullptr);

    const std::string suffix = benchUniqueSuffix(kTestName);
    const std::string folderName = "bench_single_large_" + suffix;
    const MegaHandle folderHandle = test.createFolder(0, folderName.c_str(), rootnode.get());
    ASSERT_NE(folderHandle, UNDEF);
    std::unique_ptr<MegaNode> folder{test.megaApi[0]->getNodeByHandle(folderHandle)};
    ASSERT_NE(folder, nullptr);

    const fs::path stagingDir = benchStagingRoot() / folderName;
    std::error_code cleanupEc;
    fs::remove_all(stagingDir, cleanupEc);
    const auto cleanup = makeScopedDestructor(
        [stagingDir]()
        {
            std::error_code ec;
            fs::remove_all(stagingDir, ec);
        });

    const std::string largeName = "bench_single_large_10g.bin";
    const fs::path largePath = stagingDir / largeName;
    ASSERT_NO_FATAL_FAILURE(
        createDenseDeterministicFile(largePath,
                                     kBenchLargeFileSize,
                                     benchUploadContentSeed(suffix, 0x5360f501ull)));

    MegaUploadOptions uploadOptions;
    uploadOptions.mtime = ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME;
    TransferTracker tracker(test.megaApi[0].get());
    BenchPutnodesTimingRecorder putnodesRecorder;

    const auto procStatsStart = captureBenchProcessStats();
    const auto apiStart = std::chrono::steady_clock::now();
    test.megaApi[0]->startUpload(largePath.string(),
                                 folder.get(),
                                 nullptr,
                                 &uploadOptions,
                                 &tracker);

    const ErrorCodes res = tracker.waitForResult(kTimeoutS);
    const auto apiEnd = std::chrono::steady_clock::now();
    const auto procStatsEnd = captureBenchProcessStats();
    ASSERT_EQ(res, API_OK) << "Large upload failed with code " << res;
    ASSERT_NO_FATAL_FAILURE(
        verifyUploadedFile(test, tracker.resultNodeHandle, largeName, kBenchLargeFileSize));

    const auto startMs = tracker.mStartSteadyMs.load();
    const auto finishMs = tracker.mFinishSteadyMs.load();
    ASSERT_GT(startMs, 0);
    ASSERT_GT(finishMs, startMs);
    const auto timingSummary =
        summarizeBenchTimings(std::vector<const TransferTracker*>{&tracker},
                                  putnodesRecorder);

    const auto totalMs = finishMs - startMs;
    const auto apiTotalMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(apiEnd - apiStart).count();
    const double aggregateKBps = aggregateKBpsForBytes(kBenchLargeFileSize, totalMs);

    std::ostringstream summary;
    summary << "[BenchSingleLargeUpload] files=1 fileSize=" << kBenchLargeFileSize
            << " totalBytes=" << kBenchLargeFileSize
            << " totalMs=" << totalMs << " aggregateKBps=" << aggregateKBps
            << " apiTotalMs=" << apiTotalMs
            << " avgKBps=" << aggregateKBps << " medianKBps=" << aggregateKBps
            << " minKBps=" << aggregateKBps << " maxKBps=" << aggregateKBps
            << " finishMs=" << tracker.mFinishSteadyMs.load()
            << " contentSeed=" << benchUploadContentSeed(suffix, 0x5360f501ull);
    appendBenchTimingFields(summary, timingSummary);
    LOG_info << summary.str();

    logBenchProcessStatsDelta("SingleLargeUpload", procStatsStart, procStatsEnd);
    logBenchWsStats(test, 1);
#ifdef MEGA_BENCH_FRAMEWORK_ENABLED
    recordBenchCell("SingleLargeUpload",
                    /*fileSizeMib=*/static_cast<std::int64_t>(kBenchLargeFileSize / kBenchMiB),
                    /*connections=*/0,
                    /*totalMs=*/static_cast<std::int64_t>(totalMs),
                    aggregateKBps,
                    timingSummary,
                    procStatsStart,
                    procStatsEnd,
                    /*chunkSamples=*/1);
#endif
    test.deleteFolder(folderName);
}

void runLargePlusManySmallBenchmark(SdkTest& test)
{
    constexpr int kSmallTimeoutS = 60 * 60;
    constexpr int kLargeTimeoutS = 4 * 60 * 60;
    constexpr auto kLargeGateTimeout = std::chrono::seconds{90};
    constexpr m_off_t kLargeGateBytes = 512ll * 1024ll * 1024ll;
    constexpr const char* kTestName = "SdkTestBenchmarkLargePlusManySmall";

    LOG_info << "___TEST___ " << kTestName;
    ASSERT_NO_FATAL_FAILURE(test.getAccountsForTest(1));
    ASSERT_NO_FATAL_FAILURE(requireBenchStagingSpace());

    auto accountRestorer = scopedToPro(*test.megaApi[0]);
    ASSERT_EQ(result(accountRestorer), API_OK);

    std::unique_ptr<MegaNode> rootnode{test.megaApi[0]->getRootNode()};
    ASSERT_NE(rootnode, nullptr);

    const std::string suffix = benchUniqueSuffix(kTestName);
    const std::string folderName = "bench_large_plus_small_" + suffix;
    const MegaHandle folderHandle = test.createFolder(0, folderName.c_str(), rootnode.get());
    ASSERT_NE(folderHandle, UNDEF);
    std::unique_ptr<MegaNode> folder{test.megaApi[0]->getNodeByHandle(folderHandle)};
    ASSERT_NE(folder, nullptr);

    const fs::path stagingDir = benchStagingRoot() / folderName;
    std::error_code cleanupEc;
    fs::remove_all(stagingDir, cleanupEc);
    const auto cleanup = makeScopedDestructor(
        [stagingDir]()
        {
            std::error_code ec;
            fs::remove_all(stagingDir, ec);
        });

    const std::string largeName = "bench_combined_large_10g.bin";
    const fs::path largePath = stagingDir / largeName;
    ASSERT_NO_FATAL_FAILURE(
        createDenseDeterministicFile(largePath,
                                     kBenchLargeFileSize,
                                     benchUploadContentSeed(suffix, 0x5360f502ull)));

    const fs::path smallDir = stagingDir / "small";
    fs::create_directories(smallDir);
    std::vector<sdk_test::LocalTempFile> smallFiles;
    smallFiles.reserve(kBenchSmallFileCount);
    for (size_t i = 0; i < kBenchSmallFileCount; ++i)
    {
        smallFiles.emplace_back(smallDir / ("f" + std::to_string(i) + ".bin"),
                                kBenchSmallFileSize);
    }

    MegaUploadOptions uploadOptions;
    uploadOptions.mtime = ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME;
    TransferTracker largeTracker(test.megaApi[0].get());
    BenchPutnodesTimingRecorder putnodesRecorder;

    const auto procStatsStart = captureBenchProcessStats();
    const auto apiStart = std::chrono::steady_clock::now();
    test.megaApi[0]->startUpload(largePath.string(),
                                 folder.get(),
                                 nullptr,
                                 &uploadOptions,
                                 &largeTracker);

    const auto gateStart = std::chrono::steady_clock::now();
    bool gateTimedOut = false;
    while (!largeTracker.finished &&
           largeTracker.mTransferredBytes.load() < kLargeGateBytes)
    {
        if (std::chrono::steady_clock::now() - gateStart >= kLargeGateTimeout)
        {
            gateTimedOut = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds{1});
    }
    const auto largeBytesAtSmallStart = largeTracker.mTransferredBytes.load();

    std::vector<std::unique_ptr<TransferTracker>> smallTrackers;
    smallTrackers.reserve(kBenchSmallFileCount);
    const auto smallApiStart = std::chrono::steady_clock::now();
    for (size_t i = 0; i < kBenchSmallFileCount; ++i)
    {
        smallTrackers.emplace_back(std::make_unique<TransferTracker>(test.megaApi[0].get()));
        test.megaApi[0]->startUpload(smallFiles[i].getPath().string(),
                                     folder.get(),
                                     nullptr,
                                     &uploadOptions,
                                     smallTrackers.back().get());
    }

    std::vector<double> smallKBps;
    smallKBps.reserve(kBenchSmallFileCount);
    for (size_t i = 0; i < kBenchSmallFileCount; ++i)
    {
        const ErrorCodes res = smallTrackers[i]->waitForResult(kSmallTimeoutS);
        ASSERT_EQ(res, API_OK) << "Small upload " << i << " failed with code " << res;
        appendTrackerMeanKBps(*smallTrackers[i], smallKBps);
    }
    const auto smallApiEnd = std::chrono::steady_clock::now();

    const ErrorCodes largeRes = largeTracker.waitForResult(kLargeTimeoutS);
    const auto apiEnd = std::chrono::steady_clock::now();
    const auto procStatsEnd = captureBenchProcessStats();
    ASSERT_EQ(largeRes, API_OK) << "Large upload failed with code " << largeRes;
    ASSERT_NO_FATAL_FAILURE(verifyUploadedFile(test,
                                               largeTracker.resultNodeHandle,
                                               largeName,
                                               kBenchLargeFileSize));

    double avgKBps = 0.0;
    double medianKBps = 0.0;
    double minKBps = 0.0;
    double maxKBps = 0.0;
    ASSERT_NO_FATAL_FAILURE(summarizeKBps(smallKBps, avgKBps, medianKBps, minKBps, maxKBps));

    std::int64_t firstTransferStartMs = 0;
    std::int64_t lastTransferFinishMs = 0;
    includeTransferWindow(largeTracker, firstTransferStartMs, lastTransferFinishMs);
    std::vector<const TransferTracker*> timingTrackers;
    timingTrackers.reserve(kBenchSmallFileCount + 1);
    timingTrackers.push_back(&largeTracker);

    std::int64_t firstSmallStartMs = 0;
    std::int64_t lastSmallFinishMs = 0;
    for (const auto& tracker: smallTrackers)
    {
        includeTransferWindow(*tracker, firstTransferStartMs, lastTransferFinishMs);
        includeTransferWindow(*tracker, firstSmallStartMs, lastSmallFinishMs);
        timingTrackers.push_back(tracker.get());
    }

    ASSERT_GT(firstTransferStartMs, 0);
    ASSERT_GT(lastTransferFinishMs, firstTransferStartMs);
    ASSERT_GT(firstSmallStartMs, 0);
    ASSERT_GT(lastSmallFinishMs, firstSmallStartMs);

    const auto totalMs = lastTransferFinishMs - firstTransferStartMs;
    const auto smallTotalMs = lastSmallFinishMs - firstSmallStartMs;
    const auto apiTotalMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(apiEnd - apiStart).count();
    const auto smallApiTotalMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(smallApiEnd - smallApiStart)
            .count();
    const std::uintmax_t totalBytes =
        kBenchLargeFileSize +
        static_cast<std::uintmax_t>(kBenchSmallFileCount) * kBenchSmallFileSize;
    const double aggregateKBps = aggregateKBpsForBytes(totalBytes, totalMs);
    const double smallAggregateKBps =
        aggregateKBpsForBytes(static_cast<std::uintmax_t>(kBenchSmallFileCount) *
                                  kBenchSmallFileSize,
                              smallTotalMs);
    const auto largeStartMs = largeTracker.mStartSteadyMs.load();
    const auto largeFinishMs = largeTracker.mFinishSteadyMs.load();
    ASSERT_GT(largeStartMs, 0);
    ASSERT_GT(largeFinishMs, largeStartMs);
    const auto largeObservedMs = largeFinishMs - largeStartMs;
    const auto timingSummary = summarizeBenchTimings(timingTrackers, putnodesRecorder);

    std::ostringstream summary;
    summary << "[BenchLargePlusManySmall] files=" << (kBenchSmallFileCount + 1)
            << " smallFiles=" << kBenchSmallFileCount
            << " fileSize=" << kBenchSmallFileSize
            << " largeFileSize=" << kBenchLargeFileSize
            << " totalBytes=" << totalBytes
            << " totalMs=" << totalMs << " aggregateKBps=" << aggregateKBps
            << " apiTotalMs=" << apiTotalMs
            << " avgKBps=" << avgKBps << " medianKBps=" << medianKBps
            << " minKBps=" << minKBps << " maxKBps=" << maxKBps
            << " smallTotalMs=" << smallTotalMs
            << " smallApiTotalMs=" << smallApiTotalMs
            << " smallAggregateKBps=" << smallAggregateKBps
            << " largeObservedMs=" << largeObservedMs
            << " largeBytesAtSmallStart=" << largeBytesAtSmallStart
            << " gateTimedOut=" << (gateTimedOut ? 1 : 0)
            << " contentSeed=" << benchUploadContentSeed(suffix, 0x5360f502ull);
    appendBenchTimingFields(summary, timingSummary);
    LOG_info << summary.str();

    logBenchProcessStatsDelta("LargePlusManySmall", procStatsStart, procStatsEnd);
    logBenchWsStats(test, kBenchSmallFileCount + 1);
#ifdef MEGA_BENCH_FRAMEWORK_ENABLED
    recordBenchCell("LargePlusManySmall",
                    /*fileSizeMib=*/static_cast<std::int64_t>(kBenchLargeFileSize / kBenchMiB),
                    /*connections=*/0,
                    /*totalMs=*/static_cast<std::int64_t>(totalMs),
                    aggregateKBps,
                    timingSummary,
                    procStatsStart,
                    procStatsEnd,
                    /*chunkSamples=*/kBenchSmallFileCount + 1);
#endif
    test.deleteFolder(folderName);
}

} // namespace mega::test::benchmark
