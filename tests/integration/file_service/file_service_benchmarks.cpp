#include <mega/common/statistics.h>
#include <mega/common/testing/cloud_path.h>
#include <mega/common/testing/single_client_test.h>
#include <mega/common/testing/utility.h>
#include <mega/file_service/file.h>
#include <mega/file_service/file_range.h>
#include <mega/file_service/file_range_vector.h>
#include <mega/file_service/file_read_result.h>
#include <mega/file_service/file_result.h>
#include <mega/file_service/file_result_or.h>
#include <mega/file_service/file_service_result.h>
#include <mega/file_service/file_service_result_or.h>
#include <mega/file_service/logging.h>
#include <mega/file_service/testing/integration/client.h>
#include <mega/file_service/testing/integration/real_client.h>
#include <mega/scoped_helpers.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <type_traits>
#include <vector>

namespace mega
{
namespace file_service
{
namespace testing
{

// Represents a single read request in a benchmark.
struct BenchmarkReadRequest
{
    // What file content should this request retrieve?
    FileRange mRange;

    // When should this request be executed?
    //
    // NB. This is relative to the beginning of the benchmark.
    std::chrono::milliseconds mWhen{};
}; // BenchmarkReadRequest

// Convenience.
using BenchmarkReadRequestVector = std::vector<BenchmarkReadRequest>;

// Statistics collected while performing a single read request.
struct BenchmarkReadRequestResult
{
    // Average amount of data retrieved by a read.
    double mAverageReadLength;

    // Average amount of time taken by a read.
    double mAverageReadTime;

    // Maximum amount of data returned by a read.
    std::uint64_t mMaximumReadLength;

    // Maximum amount of time taken by a read.
    std::chrono::milliseconds mMaximumReadTime;

    // Minimum amount of data returned by a read.
    std::uint64_t mMinimumReadLength;

    // Minimum amount of time taken by a read.
    std::chrono::milliseconds mMinimumReadTime;

    // How many reads were necessary to satisfy the request.
    std::uint64_t mNumReads;

    // Index of the request these results relate to.
    std::size_t mRequestIndex;

    // Time taken to retrieve the first byte of the request.
    std::chrono::milliseconds mTimeToFirstByte;

    // Time taken to satisfy the entire request.
    std::chrono::milliseconds mTimeToCompletion;
}; // BenchmarkReadRequestResult

// Convenience.
using BenchmarkReadRequestResultVector = std::vector<BenchmarkReadRequestResult>;

// Bundles state necessary to perform a benchmark.
struct BenchmarkContext
{
    // Signalled when a request has completed.
    std::condition_variable mCV;

    // How many requests are currently in progress.
    std::size_t mCount = 0;

    // Tracks whether any requests failed.
    bool mFailed = false;

    // Serializes access to instance members.
    std::mutex mLock;

    // Results collected from each request.
    BenchmarkReadRequestResultVector mResults;
}; // BenchmarkContext

// Convenience.
using BenchmarkContextPtr = std::shared_ptr<BenchmarkContext>;

class BenchmarkReadRequestContext;

// Convenience.
using BenchmarkReadRequestContextPtr = std::shared_ptr<BenchmarkReadRequestContext>;

// Responsible for executing a single read request.
class BenchmarkReadRequestContext
{
    // Called when we've retrieved data from the service.
    void onRead(std::chrono::steady_clock::time_point began,
                BenchmarkReadRequestContextPtr& context,
                File file,
                std::uint64_t length,
                std::uint64_t offset,
                FileResultOr<FileReadResult> result)
    {
        // Couldn't read data.
        if (!result)
        {
            std::lock_guard guard(mBenchmarkContext->mLock);

            // Request failed.
            mBenchmarkContext->mFailed = true;

            // Request is no longer in progress.
            --mBenchmarkContext->mCount;

            // Wake benchmark if necessary.
            return mBenchmarkContext->mCV.notify_one();
        }

        // Convenience.
        using std::chrono::duration_cast;
        using std::chrono::milliseconds;
        using std::chrono::steady_clock;

        auto now = steady_clock::now();

        // How long did this read take to complete?
        auto elapsed = duration_cast<milliseconds>(now - began);

        LOG_debug << "Request #" << mRequestIndex << ": onRead: elapsed: " << elapsed.count()
                  << "ms";

        // Factor read time into our statistics.
        mAverageReadTime(elapsed.count());
        mMaximumReadTime(elapsed.count());
        mMinimumReadTime(elapsed.count());

        if (!mTimeToFirstByte)
            mTimeToFirstByte = elapsed;

        // Bump number of reads.
        ++mNumReads;

        // No more data to read.
        if (!result->mLength)
        {
            // Populate request statistics.
            BenchmarkReadRequestResult result = {
                mAverageReadLength.get().value(),
                mAverageReadTime.get().value(),
                mMaximumReadLength.get().value(),
                milliseconds(mMaximumReadTime.get().value()),
                mMinimumReadLength.get().value(),
                milliseconds(mMinimumReadTime.get().value()),
                mNumReads,
                mRequestIndex,
                mTimeToFirstByte.value(),
                duration_cast<milliseconds>(now - mBegan)}; // result

            // Make sure no one else is messing with the benchmark.
            std::lock_guard guard(mBenchmarkContext->mLock);

            // Request is no longer in progress.
            --mBenchmarkContext->mCount;

            // Publish this request's statistics.
            mBenchmarkContext->mResults.emplace_back(result);

            // Wake benchmark if necessary.
            return mBenchmarkContext->mCV.notify_one();
        }

        // Factor read length into our statistics.
        mAverageReadLength(result->mLength);
        mMaximumReadLength(result->mLength);
        mMinimumReadLength(result->mLength);

        // Read remaining data.
        execute(std::move(context),
                std::move(file),
                length - result->mLength,
                offset + result->mLength);
    }

    // Convenience.
    template<typename T>
    using Averager = common::Averager<T, 5>;

    template<typename T>
    using Maximizer = common::Maximizer<T>;

    template<typename T>
    using Minimizer = common::Minimizer<T>;

    using Rep = std::chrono::milliseconds::rep;

    // Average amount of data retrieved by a single read.
    Averager<std::uint64_t> mAverageReadLength;

    // Average amount of time taken to satisfy a single read.
    Averager<Rep> mAverageReadTime;

    // When did we begin this request?
    std::chrono::steady_clock::time_point mBegan;

    // Reference to benchmark context.
    BenchmarkContextPtr mBenchmarkContext;

    // Maximum amount of data retrieved by a single read.
    Maximizer<std::uint64_t> mMaximumReadLength;

    // Maximum amount of time to satisfy a single read.
    Maximizer<Rep> mMaximumReadTime;

    // Minimum amount of data retrieved by a single read.
    Minimizer<std::uint64_t> mMinimumReadLength;

    // Minimum amount of time taken to satisfy a single read.
    Minimizer<Rep> mMinimumReadTime;

    // Tracks how many reads we have performed.
    std::uint64_t mNumReads;

    // The index of the request we are executing.
    std::size_t mRequestIndex;

    // How long did our first read take?
    std::optional<std::chrono::milliseconds> mTimeToFirstByte;

public:
    BenchmarkReadRequestContext(BenchmarkContextPtr benchmarkContext, std::size_t requestIndex):
        mAverageReadLength(),
        mAverageReadTime(),
        mBegan(std::chrono::steady_clock::now()),
        mBenchmarkContext(std::move(benchmarkContext)),
        mMaximumReadLength(),
        mMaximumReadTime(),
        mMinimumReadLength(),
        mMinimumReadTime(),
        mNumReads(0),
        mRequestIndex(requestIndex),
        mTimeToFirstByte()
    {}

    // Execute this request.
    void execute(BenchmarkReadRequestContextPtr context,
                 File file,
                 std::uint64_t length,
                 std::uint64_t offset)
    {
        // Sanity.
        assert(context.get() == this);

        // Try and read data from the service.
        file.read(std::bind(&BenchmarkReadRequestContext::onRead,
                            this,
                            std::chrono::steady_clock::now(),
                            std::move(context),
                            file,
                            length,
                            offset,
                            std::placeholders::_1),
                  offset,
                  length,
                  StreamReadTag{true});
    }
}; // BenchmarkReadRequestContext

// The result of a benchmark.
struct BenchmarkResult
{
    // Results of each read request performed by the benchmark.
    BenchmarkReadRequestResultVector mResults;

    // How long did the benchmark take to run?
    std::chrono::milliseconds mTimeToCompletion;
}; // BenchmarkResult

// So our benchmark fixture below uses the correct type of clients.
struct FileServiceBenchmarkTraits
{
    using AbstractClient = Client;
    using ConcreteClient = RealClient;

    static constexpr const char* mName = "file_service_benchmark";
}; // FileServiceBenchmarkTraits

// Convenience.
using common::testing::randomName;
using common::testing::SingleClientTest;

// Fixture for individual benchmarks.
struct FileServiceBenchmarks: SingleClientTest<FileServiceBenchmarkTraits>
{}; // FileServiceBenchmarks

// Perform a benchmark.
template<typename Rep, typename Period>
static bool benchmark(Client& client,
                      const BenchmarkReadRequestVector& requests,
                      std::chrono::duration<Rep, Period>);

// Convenience.
using namespace std::literals::chrono_literals;

// This test is not intented to be run on Jenkins every time we push a commit for an MR.
// Instead, it's intended to be run manually when optimizing to gauge whether our optimizations are
// having a positive effect.
TEST_F(FileServiceBenchmarks, DISABLED_benchmark_firefox_linear_playback)
{
    // Based on linear playback in Firefox via MEGAsync.
    BenchmarkReadRequestVector requests = {{FileRange(0, 668951065), 0ms},
                                           {FileRange(668729344, 668951065), 185ms},
                                           {FileRange(1409024, 668951065), 206ms},
                                           {FileRange(105218048, 668951065), 360ms},
                                           {FileRange(209027072, 668951065), 579ms},
                                           {FileRange(312836096, 668951065), 718ms},
                                           {FileRange(416645120, 668951065), 839ms},
                                           {FileRange(520454144, 668951065), 965ms},
                                           {FileRange(589660160, 668951065), 30123ms}}; // requests

    // Execute the benchmark.
    ASSERT_TRUE(benchmark(*mClient, requests, 15min));
}

// This test is not intented to be run on Jenkins every time we push a commit for an MR.
// Instead, it's intended to be run manually when optimizing to gauge whether our optimizations are
// having a positive effect.
TEST_F(FileServiceBenchmarks, DISABLED_benchmark_firefox_random_playback)
{
    // Based on random playback in Firefox via MEGAsync.
    BenchmarkReadRequestVector requests = {{FileRange(0, 668951065), 0ms},
                                           {FileRange(668729344, 668951065), 50ms},
                                           {FileRange(1572864, 668951065), 71ms},
                                           {FileRange(174587904, 668951065), 229ms},
                                           {FileRange(209190912, 668951065), 261ms},
                                           {FileRange(243793920, 668951065), 298ms},
                                           {FileRange(278396928, 668951065), 328ms},
                                           {FileRange(312999936, 668951065), 357ms},
                                           {FileRange(347602944, 668951065), 385ms},
                                           {FileRange(615710720, 668951065), 3588ms},
                                           {FileRange(620953600, 668951065), 3598ms},
                                           {FileRange(616366080, 668951065), 3652ms},
                                           {FileRange(501481472, 668951065), 5585ms},
                                           {FileRange(131530752, 668951065), 9301ms},
                                           {FileRange(138051584, 668951065), 9327ms},
                                           {FileRange(218923008, 668951065), 9458ms},
                                           {FileRange(246317056, 668951065), 9567ms},
                                           {FileRange(280920064, 668951065), 9656ms},
                                           {FileRange(311066624, 668951065), 9747ms},
                                           {FileRange(345669632, 668951065), 9837ms},
                                           {FileRange(380272640, 668951065), 9915ms},
                                           {FileRange(414875648, 668951065), 9985ms},
                                           {FileRange(449478656, 668951065), 10052ms},
                                           {FileRange(484081664, 668951065), 10108ms},
                                           {FileRange(518684672, 668951065), 10157ms},
                                           {FileRange(553287680, 668951065), 10197ms},
                                           {FileRange(587890688, 668951065), 10230ms},
                                           {FileRange(622493696, 668951065), 10256ms},
                                           {FileRange(133857280, 668951065), 10258ms},
                                           {FileRange(213811200, 668951065), 10391ms},
                                           {FileRange(248152064, 668951065), 10501ms},
                                           {FileRange(282755072, 668951065), 10570ms},
                                           {FileRange(306872320, 668951065), 10665ms},
                                           {FileRange(341475328, 668951065), 10755ms},
                                           {FileRange(376078336, 668951065), 10835ms},
                                           {FileRange(410681344, 668951065), 10909ms},
                                           {FileRange(445284352, 668951065), 10974ms},
                                           {FileRange(655392768, 668951065), 11005ms},
                                           {FileRange(40894464, 668951065), 19387ms},
                                           {FileRange(48267264, 668951065), 19415ms},
                                           {FileRange(127172608, 668951065), 19560ms},
                                           {FileRange(161775616, 668951065), 19668ms},
                                           {FileRange(196378624, 668951065), 19736ms},
                                           {FileRange(221282304, 668951065), 19830ms},
                                           {FileRange(255885312, 668951065), 19918ms},
                                           {FileRange(290488320, 668951065), 19998ms},
                                           {FileRange(325091328, 668951065), 20070ms},
                                           {FileRange(359694336, 668951065), 20134ms},
                                           {FileRange(394297344, 668951065), 20191ms},
                                           {FileRange(428900352, 668951065), 20242ms},
                                           {FileRange(463503360, 668951065), 20283ms},
                                           {FileRange(498106368, 668951065), 20317ms},
                                           {FileRange(43089920, 668951065), 20330ms},
                                           {FileRange(117932032, 668951065), 20374ms},
                                           {FileRange(129073152, 668951065), 20488ms},
                                           {FileRange(163676160, 668951065), 20594ms},
                                           {FileRange(198279168, 668951065), 20646ms},
                                           {FileRange(216104960, 668951065), 20743ms},
                                           {FileRange(250707968, 668951065), 20829ms},
                                           {FileRange(285310976, 668951065), 20906ms},
                                           {FileRange(319913984, 668951065), 20976ms},
                                           {FileRange(354516992, 668951065), 21040ms},
                                           {FileRange(389120000, 668951065), 21097ms},
                                           {FileRange(423723008, 668951065), 21145ms},
                                           {FileRange(458326016, 668951065), 21185ms},
                                           {FileRange(492929024, 668951065), 21221ms},
                                           {FileRange(527532032, 668951065), 21247ms},
                                           {FileRange(564756480, 668951065), 21256ms}}; // requests

    // Execute the benchmark.
    ASSERT_TRUE(benchmark(*mClient, requests, 15min));
}

// This test is not intented to be run on Jenkins every time we push a commit for an MR.
// Instead, it's intended to be run manually when optimizing to gauge whether our optimizations are
// having a positive effect.
TEST_F(FileServiceBenchmarks, DISABLED_benchmark_sequential_nonoverlapping)
{
    BenchmarkReadRequestVector requests;

    // Populate requests.
    for (auto i = 0ul; i < 8; ++i)
    {
        BenchmarkReadRequest request;

        request.mRange.mBegin = 5120ul * 1024 * i;
        request.mRange.mEnd = request.mRange.mBegin + 4096 * 1024;

        requests.emplace_back(request);
    }

    // Execute the benchmark.
    ASSERT_TRUE(benchmark(*mClient, requests, 15min));
}

// This test is not intented to be run on Jenkins every time we push a commit for an MR.
// Instead, it's intended to be run manually when optimizing to gauge whether our optimizations are
// having a positive effect.
TEST_F(FileServiceBenchmarks, DISABLED_benchmark_sequential_overlapping)
{
    BenchmarkReadRequestVector requests;

    // Populate requests.
    for (auto i = 0ul; i < 8; ++i)
    {
        BenchmarkReadRequest request;

        request.mRange.mBegin = i * 2048 * 1024;
        request.mRange.mEnd = request.mRange.mBegin + 4096 * 1024;

        requests.emplace_back(request);
    }

    // Execute the benchmark.
    ASSERT_TRUE(benchmark(*mClient, requests, 15min));
}

template<typename Rep, typename Period>
bool benchmark(Client& client,
               const BenchmarkReadRequestVector& requests,
               std::chrono::duration<Rep, Period> timeout)
{
    // Generate and upload a file suitable for our benchmark.
    auto handle = [&]()
    {
        // Minimum size necessary to satisfy all requests.
        std::uint64_t size = 0;

        // Figure out how large our file needs to be.
        for (auto& request: requests)
            size = std::max(request.mRange.mEnd, size);

        // Generate and upload the file.
        return client.upload(size, randomName(), client.rootHandle());
    }();

    // Couldn't upload file.
    if (!handle)
        return false;

    // Make sure file is removed when the benchmark completes.
    auto remover = makeScopedDestructor(
        [&client, handle = *handle]()
        {
            client.remove(handle);
        });

    // Open file for reading.
    auto file = client.fileOpen(*handle);

    // Couldn't open file.
    if (!file)
        return false;

    // Compare indices by their respective request's execution time.
    auto compare = [&requests](auto lhs, auto rhs)
    {
        return requests[lhs].mWhen < requests[rhs].mWhen;
    }; // less

    // Convenience.
    using Compare = decltype(compare);
    using Container = std::vector<std::size_t>;
    using Queue = std::priority_queue<std::size_t, Container, Compare>;

    // Tracks pending requests ordered by execution time.
    Queue pending(std::move(compare));

    // Queue requests for execution.
    for (auto i = 0ul; i < requests.size(); ++i)
        pending.push(i);

    // Convenience.
    using std::chrono::duration_cast;
    using std::chrono::milliseconds;
    using std::chrono::steady_clock;

    // Instantiate benchmark context.
    auto context = std::make_shared<BenchmarkContext>();

    // Keep track of when the benchmark began.
    auto began = steady_clock::now();

    // When should we forcibly terminate the benchmark?
    auto max = began + timeout;

    // Execute the benchmark.
    while (true)
    {
        // Check whether the benchmark has completed.
        auto completed = [&]()
        {
            return context->mFailed || (!context->mCount && pending.empty()) ||
                   steady_clock::now() >= max;
        }; // completed

        // Retrieve the next request.
        auto nextRequest = [&]() -> const BenchmarkReadRequest&
        {
            return requests[pending.top()];
        }; // nextRequest

        // When should we execute a request?
        auto nextWakeup = [&]()
        {
            if (!pending.empty())
                return std::min(began + nextRequest().mWhen, max);

            return max;
        }; // nextWakeup

        // Acquire context lock.
        std::unique_lock lock(context->mLock);

        // Wait until either:
        // - The benchmark has completed
        // - It's time to execute another request
        auto hasCompleted = context->mCV.wait_until(lock, nextWakeup(), completed);

        // Convenience.
        auto now = steady_clock::now();

        // Benchmark's completed.
        if (hasCompleted)
            break;

        // No requests left to execute.
        if (pending.empty())
            continue;

        // Convenience.
        auto& request = nextRequest();

        // Request isn't ready for execution.
        if (now - began < request.mWhen)
            continue;

        // Convenience.
        using RequestContext = BenchmarkReadRequestContext;

        // Instantiate a new request context.
        auto requestContext = std::make_shared<RequestContext>(context, pending.top());

        // Convenience.
        auto [begin, end] = request.mRange;

        // Execute the the request.
        requestContext->execute(requestContext, *file, end - begin, begin);

        // Request is no longer pending execution.
        pending.pop();

        // Request is now in progress.
        ++context->mCount;
    }

    // Make sure no pending reads mess with our context.
    std::lock_guard guard(context->mLock);

    // Benchmark failed.
    if (context->mFailed)
    {
        LOG_verbose << "Benchmark failed";
        return false;
    }

    // Convenience.
    auto now = steady_clock::now();

    // Benchmark timed out.
    if (now > max)
    {
        LOG_verbose << "Benchmark timed out";
        return false;
    }

    // How long did the benchmark take to execute?
    LOG_verbose << "Benchmark completed in " << duration_cast<milliseconds>(now - began).count()
                << "ms";

    // How many requests did we execute?
    LOG_verbose << "Benchmark executed " << requests.size() << " request(s)";

    // Keep track of how many bytes we downloaded.
    std::uint64_t totalBytes = 0;

    // Log statistics about each request.
    for (auto& result: context->mResults)
    {
        // Convenience.
        auto index = result.mRequestIndex;
        auto range = requests[index].mRange;

        // Convenience.
        auto bytesRead = range.mEnd - range.mBegin;

        // Adjust total bytes downloaded.
        totalBytes += bytesRead;

        // So we have more control over formatting.
        std::ostringstream ostream;

        ostream.precision(2);
        ostream.setf(std::ios::fixed);

        // Log statistics about this request.
        ostream << "Request #" << index << " (" << toString(range) << "):\n"
                << "  Average read length: " << result.mAverageReadLength << " byte(s)\n"
                << "  Average read time: " << result.mAverageReadTime << "ms\n"
                << "  Bytes read: " << bytesRead << "\n"
                << "  Maximum read length: " << result.mMaximumReadLength << " byte(s)\n"
                << "  Maximum read time: " << result.mMaximumReadTime.count() << "ms\n"
                << "  Minimum read length: " << result.mMinimumReadLength << " byte(s)\n"
                << "  Minimum read time: " << result.mMinimumReadTime.count() << "ms\n"
                << "  Number of reads: " << result.mNumReads << "\n"
                << "  Time to first byte: " << result.mTimeToFirstByte.count() << "ms\n"
                << "  Time to completion: " << result.mTimeToCompletion.count() << "ms\n";

        LOG_verbose << ostream.str();
    }

    // Log total number of bytes downloaded.
    LOG_verbose << "Benchmark downloaded " << totalBytes << " byte(s)";

    // Benchmark succeeded.
    return true;
}

} // testing
} // file_service
} // mega
