/**
 * @file hashcash.cpp
 * @brief Mega SDK PoW for login
 */

#include "mega/hashcash.h"

#include "mega/base64.h"
#include "mega/canceller.h"
#include "mega/logging.h"
#include "mega/utils.h"

#if defined(__APPLE__)
#include <mach/host_info.h>
#include <mach/mach.h>
#elif !defined(_WIN32)
#include <unistd.h>
#endif

namespace mega
{

namespace
{

using Clock = std::chrono::steady_clock;

constexpr std::size_t kTokenBytes = 48;
constexpr std::size_t kPrefixBytes = 4;
constexpr std::size_t kRepeat = 262144; // 12MB / 48B
constexpr std::size_t kBufSize = kPrefixBytes + kRepeat * kTokenBytes;
constexpr std::size_t kSha256Block = CryptoPP::SHA256::BLOCKSIZE;

#ifndef NDEBUG
constexpr std::chrono::milliseconds kTtl{60000};
constexpr std::chrono::milliseconds kMinBudget{10000};
// constexpr unsigned kMaxRetries{5};
#else
constexpr std::chrono::milliseconds kTtl{300000};
constexpr std::chrono::milliseconds kMinBudget{30000};
#endif
constexpr unsigned kMaxRetries{2};

#if defined(__ANDROID__) || defined(USE_IOS)
constexpr int kTtlDivisor{2};
#else
constexpr int kTtlDivisor{3};
#endif

RetryGencash retryGencash{};

uint32_t thresholdFromEasiness(const uint8_t e)
{
    return static_cast<uint32_t>((((e & 63) << 1) + 1) << ((e >> 6) * 7 + 3));
}

void initTokenArea(const std::string& tokenBin, std::vector<uint8_t>& buf)
{
    assert(buf.size() == kBufSize);
    assert(tokenBin.size() == kTokenBytes);

    std::memcpy(buf.data() + kPrefixBytes, tokenBin.data(), kTokenBytes);

    std::size_t filled = kTokenBytes;
    while (filled < kRepeat * kTokenBytes)
    {
        const auto copy = std::min(filled, kRepeat * kTokenBytes - filled);
        std::memcpy(buf.data() + kPrefixBytes + filled, buf.data() + kPrefixBytes, copy);
        filled += copy;
    }
}

uint32_t sha256FirstWord(const uint8_t* data, const std::size_t len)
{
    assert(data != nullptr);
    assert((len >= kSha256Block) && "SHA256 processes >= 1 block (64-byte)");

    CryptoPP::SHA256 h;
    h.Update(data, static_cast<unsigned>(len));

    uint32_t word{};
    h.TruncatedFinal(reinterpret_cast<CryptoPP::byte*>(&word), 4);
    return word;
}

std::tuple<bool, std::string> getTokenBin(const std::string& token)
{
    const auto tokenBin = ::mega::Base64::atob(token);
    if (tokenBin.size() != kTokenBytes)
    {
        LOG_err << "[getTokenBin] tokenBin.size (" << tokenBin.size() << ") != kTokenBytes ("
                << kTokenBytes << ") -> corrupted token from server? check with API";
        return {false, {}};
    }

    return {true, tokenBin};
}

unsigned decideWorkers(const unsigned maxWorkers)
{
    assert(maxWorkers > 0);

    const auto maxWorkersForDevice = std::max(std::thread::hardware_concurrency(), 1u);
    const auto workers = std::clamp(maxWorkers, 1u, maxWorkersForDevice);

    LOG_verbose << "[decideWorkers] workers = " << workers << " [maxWorkers = " << maxWorkers
                << ", maxWorkersForDevice = " << maxWorkersForDevice << "]";

    return workers;
}

std::chrono::milliseconds computeBudget(const std::chrono::milliseconds ttl = kTtl)
{
    if (ttl != kTtl)
        return ttl;

    const auto third = ttl / kTtlDivisor;
    const auto max90 = ttl - ttl / 10;
    return std::clamp(third, kMinBudget, max90);
}

std::chrono::milliseconds adjustBudget(const std::chrono::milliseconds budget)
{
    if (retryGencash.mForceRetryCount >= RetryGencash::kMaxRetries)
    {
        LOG_verbose << "[computeBudget] forced retries (" << retryGencash.mForceRetryCount
                    << " exceeded maxRetries = " << kMaxRetries
                    << " <-- no more timeouts will be forced";
        return std::chrono::hours(24 * 7);
    }
    return budget;
}

/**
 * @brief Search one stride of the HashCash nonce space using a shared precomputed message area.
 *
 * This worker enumerates 32 bit nonces in network (big endian) order:
 *
 *     n = start, start + stride, start + 2*stride, ...
 *
 * For each candidate nonce it computes SHA-256 over the logical message
 *
 *     [4-byte NONCE || (tokenArea bytes [4..63]) || (tokenArea bytes [64..tokenAreaSize-1])]
 *
 * Implementation notes:
 *  - The first 64 bytes are split into a small hot block prepared on stack:
 *      * block[0..3] is filled with NONCE in network order for each iteration.
 *      * block[4..63] is copied once from tokenArea + 4 (60 bytes).
 *  - The remaining bytes [tokenArea + 64 .. tokenArea + tokenAreaSize) are streamed as the
 *    "cold" part, tokenArea is treated as read only and is shared across workers.
 *  - hasher is thread_local, so there is no cross thread contention.
 *
 * A candidate is accepted when the top 32 bits of the SHA-256 digest (interpreted in big endian)
 * are <= thresholdNetOrder, which itself must be supplied in network (big endian) order.
 *
 * Cancellation / early exit:
 *  - The loop exits as soon as either:
 *      * stop becomes true (another worker won or the coordinator asked us to stop), or
 *      * scopedCanceller.triggered() observes a global cancel (e.g., app requested logout).
 *  - On success this worker does stop.store(true) to notify peers promptly.
 *
 * Threading & safety:
 *  - tokenArea must outlive this call for the duration of the worker.
 *  - tokenArea is never written to by this function (read only).
 *  - stop is shared by all workers of the same gencash() attempt.
 *
 * @param tokenArea            Pointer to the precomputed message area. Layout requirement:
 *                             bytes [0..3] correspond to the NONCE slot; bytes [4..] contain
 *                             token derived material prepared by the coordinator.
 * @param tokenAreaSize        Size in bytes of @p tokenArea. Must be >= 64.
 * @param thresholdNetOrder    Difficulty threshold in network (big endian) order.
 * @param start                First nonce for this worker (0 <= start < stride).
 * @param stride               Distance between successive nonces for this worker (> 0).
 * @param stop                 Shared flag; set to true when any worker finds a hit or the
 *                             coordinator decides to terminate early.
 * @param scopedCanceller      Snapshot of a global cancel epoch. If it trips, the worker exits.
 *
 * @return Base64 encoded 4 byte NONCE (in network order) on success, or an empty string if
 *         this worker did not win (either because another thread won or cancellation occurred).
 */
std::string gencashWorker(const uint8_t* tokenArea,
                          const std::size_t tokenAreaSize,
                          const uint32_t thresholdNetOrder,
                          const uint32_t start,
                          const uint32_t stride,
                          std::atomic<bool>& stop,
                          const ScopedCanceller& scopedCanceller)
{
    assert(tokenArea);
    assert(tokenAreaSize >= 64); // we are going to read area+64
    assert(stride > 0 && start < stride);

    // First 64 bytes of the logical message are:
    //   [4B nonce][60B from area starting at offset 4]
    const auto* tokenStart = tokenArea + 4;
    uint8_t block[64];
    std::memcpy(block + 4, tokenStart, 60);

    thread_local CryptoPP::SHA256 hasher;

    const auto stopCondition = [&stop, &scopedCanceller]() -> bool
    {
        return stop.load(std::memory_order_relaxed) || scopedCanceller.triggered();
    };

    for (uint32_t n = start; !stopCondition(); n += stride)
    {
        *reinterpret_cast<uint32_t*>(block) = htonl(n);

        hasher.Restart();
        hasher.Update(block, 64);
        hasher.Update(tokenArea + 64, static_cast<unsigned>(tokenAreaSize - 64));

        if (stopCondition())
        {
            break;
        }

        uint32_t firstWord{};
        hasher.TruncatedFinal(reinterpret_cast<CryptoPP::byte*>(&firstWord), sizeof(uint32_t));

        if (htonl(firstWord) <= thresholdNetOrder)
        {
            stop.store(true, std::memory_order_relaxed);
            const uint32_t nonceNetOrder = htonl(n);
            return ::mega::Base64::btoa(
                std::string(reinterpret_cast<const char*>(&nonceNetOrder), sizeof(uint32_t)));
        }
    }
    return {};
}

} // namespace

std::string gencash(const std::string& token,
                    const uint8_t easiness,
                    const std::chrono::milliseconds ttl,
                    const cancel_epoch_t reqSnapshot,
                    const unsigned maxWorkers)
{
    const ScopedCanceller scopedCanceller{reqSnapshot};

    const auto& start = Clock::now();
    const auto initialBudget = computeBudget(ttl);
    const auto budget = adjustBudget(initialBudget);
    const auto deadline = start + budget;
    const auto workers = decideWorkers(maxWorkers);

    const auto checkCancel =
        [&scopedCanceller, &easiness, &workers, &start](const bool cancelTriggered = false) -> bool
    {
        if (cancelTriggered || scopedCanceller.triggered())
        {
            LOG_verbose
                << "[gencash] Calculating hashcash with easiness = " << +easiness
                << " has been CANCELLED by external request -> exit and reset num retries [workers "
                   "= "
                << workers << "] [timelapsed = "
                << std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start)
                       .count()
                << " ms] [numRetriesDueToTimeout = " << retryGencash.mForceRetryCount << "]";
            retryGencash = {};
            return true;
        }
        return false;
    };

    if (checkCancel())
    {
        return {};
    }

    LOG_verbose << "[gencash] Calculating hashcash with easiness = " << static_cast<int>(easiness)
                << " and workers = " << workers << " [maxWorkers = " << maxWorkers
                << "] [timeLimit = " << budget.count() << " ms]";

    // 1) Precompute everything once
    const auto [tokenBinResult, tokenBin] = getTokenBin(token);
    if (tokenBinResult == false)
    {
        retryGencash = {};
        return {};
    }

    auto tokenArea = std::make_shared<std::vector<uint8_t>>(kBufSize);
    initTokenArea(tokenBin, *tokenArea);

    const auto threshold = thresholdFromEasiness(easiness);

    if (checkCancel())
    {
        return {};
    }

    // 2) Spawn workers that all read from the same buffer
    std::atomic<bool> stop{false};
    std::string winner;
    std::mutex winnerMx;
    std::condition_variable cv;

    std::vector<std::thread> pool;
    pool.reserve(workers);

    for (uint32_t w = 0; w < workers; ++w)
    {
        pool.emplace_back(
            [&tokenArea, &threshold, w, workers, &stop, &scopedCanceller, &winnerMx, &winner, &cv]
            {
                auto local = gencashWorker(tokenArea->data(),
                                           tokenArea->size(),
                                           threshold,
                                           w,
                                           workers,
                                           stop,
                                           scopedCanceller);

                bool shouldNotify{false};

                if (!local.empty())
                {
                    std::lock_guard<std::mutex> lk(winnerMx);
                    if (winner.empty())
                    {
                        winner = std::move(local);
                        shouldNotify = true;
                    }
                }

                shouldNotify |= scopedCanceller.triggered();
                if (shouldNotify)
                {
                    stop.store(true, std::memory_order_relaxed);
                    cv.notify_all();
                }
            });
    }

    bool cancelTriggered{false};
    {
        std::unique_lock<std::mutex> lk(winnerMx);
        cv.wait_until(lk,
                      deadline,
                      [&]
                      {
                          return !winner.empty() || scopedCanceller.triggered();
                      });
        cancelTriggered = scopedCanceller.triggered();
        if (const auto earlyExit = winner.empty() || cancelTriggered; earlyExit)
        {
            stop.store(true, std::memory_order_relaxed);
        }
    }

    for (auto& t: pool)
        t.join();

    const auto timeLapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);

    if (const auto forcedTimeout = winner.empty() && !cancelTriggered; forcedTimeout)
    {
        retryGencash =
            RetryGencash{retryGencash.mForceRetryCount + 1, easiness, budget, timeLapsed};
        LOG_verbose << "[gencash] Calculating hashcash exceeded deadline -> EARLY EXIT & retry "
                       "[timeLimit = "
                    << budget.count() << " ms, timelapsed = " << timeLapsed.count()
                    << " ms [workers = " << workers
                    << "] [numRetries = " << retryGencash.mForceRetryCount << "]";
        return winner;
    }

    if (checkCancel(cancelTriggered))
    {
        return {};
    }

    assert(!winner.empty());
    LOG_verbose << "[gencash] Calculated hashcash with easiness = " << +easiness
                << " and workers = " << workers << " in " << timeLapsed.count() << " ms"
                << " [numRetries = " << retryGencash.mForceRetryCount << "]";

    retryGencash = (retryGencash.mForceRetryCount >= RetryGencash::kMaxRetries &&
                    timeLapsed >= initialBudget) ?
                       RetryGencash{0, easiness, initialBudget, timeLapsed} :
                       RetryGencash{};

    return winner;
}

std::string gencash(const std::string& token,
                    const uint8_t easiness,
                    const cancel_epoch_t reqSnapshot,
                    const unsigned maxWorkers)
{
    return gencash(token, easiness, kTtl, reqSnapshot, maxWorkers);
}

std::string gencash(const std::string& token,
                    const uint8_t easiness,
                    const cancel_epoch_t reqSnapshot)
{
    return gencash(token, easiness, kTtl, reqSnapshot, MAX_WORKERS_FOR_GENCASH);
}

bool validateHashcash(const std::string& token,
                      const uint8_t easiness,
                      const std::string& prefixB64)
{
    const auto prefix = Base64::atob(prefixB64);
    if (prefix.size() != kPrefixBytes)
    {
        LOG_debug << "[validateHashcash] prefix.size (" << prefix.size()
                  << ") != valid prefix bytes (" << kPrefixBytes << ") -> return false";
        return false;
    }

    const auto [tokenBinResult, tokenBin] = getTokenBin(token);
    if (tokenBinResult == false)
    {
        return false;
    }

    std::vector<uint8_t> buf(kBufSize);
    initTokenArea(tokenBin, buf);
    std::memcpy(buf.data(), prefix.data(), kPrefixBytes);

    const auto word = sha256FirstWord(buf.data(), buf.size());
    return htonl(word) <= thresholdFromEasiness(easiness);
}

std::optional<RetryGencash> retryGencashData()
{
    using namespace std::chrono_literals;
    assert(retryGencash.mEasiness == 0 ||
           (retryGencash.mBudget > 0ms && (retryGencash.mGencashTime >= retryGencash.mBudget)));

    if (retryGencash.mEasiness == 0)
        return {};

    return {retryGencash};
}

} // namespace mega
