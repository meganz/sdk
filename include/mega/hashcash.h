/**
 * @file mega/hashcash.h
 * @brief Mega SDK PoW for login
 */

#ifndef MEGA_HASHCASH_H
#define MEGA_HASHCASH_H 1

#include "mega/canceller.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace mega
{
static constexpr unsigned MAX_WORKERS_FOR_GENCASH =
#if defined(__ANDROID__) || defined(USE_IOS)
    2u;
#else
    8u;
#endif

struct RetryGencash
{
    static constexpr auto kMaxRetries{2u};

    unsigned mForceRetryCount{0};
    uint8_t mEasiness{0};
    std::chrono::milliseconds mBudget{0};
    std::chrono::milliseconds mGencashTime{0};
};

/**
 * @brief Retrieve current RetryGencash data
 *
 * Attempt 1 (gencash execution)
 *    Timeout -> Retry 1 (RetryGencash::mForceRetryCount == 1)
 * Attempt 2 (Retry 1)
 *    Timeout -> Retry 2 (RetryGencash::mForceRetryCount == 2)
 * Attempt 3 (Retry 2)
 *    Timeout -> No more retries (RetryGencash::mForceRetryCount == 0).
 *
 * return A valid RetryGencash object if:
 *           -> There is a forced retry (RetryGencash::mForceRetryCount > 0)
 *           -> This is the last retry and, despite not forcing a new retry anymore,
 *              the time lapsed has exceeded the timeout that would have forced a new retry
 *              if we didn't reach the maxRetries limit. RetryGencash::mForceRetryCount would be 0
 * for this case. Nullopt otherwise.
 */
std::optional<RetryGencash> retryGencashData();

/**
 * @brief Multi threaded HashCash solver.
 *
 * Spawns workers threads (capped by maxWorkers and hardware_concurrency),
 * each running gencashWorker with a different stride.
 *
 * The first successful prefix is returned, all other workers are signalled to exit early.
 *
 * @param token           Base64 token issued by the server.
 * @param easiness        Target difficulty.
 * @param ttl             Timeout to give up computing this hash - return empty string.
 * @param reqSnapshot     Starting cancel_epoch_t to compare the global epoch to cancel the request
 * and stop computing immediately. If the canceller is triggered, this method will return an empty
 * string.
 * @param maxWorkers      User cap: 8 for desktop, 2 for mobile, etc.
 *
 * @return Base64 encoded 4-byte prefix satisfying the difficulty target or empty string if the ttl
 * has been reached or the global cancel_epoch_t is greater than the reqSnapshot.
 */
std::string gencash(const std::string& token,
                    const uint8_t easiness,
                    const std::chrono::milliseconds ttl,
                    const cancel_epoch_t reqSnapshot,
                    const unsigned maxWorkers);

/**
 * @brief Overload of gencash() with internal ttl (based on server's ttl).
 */
std::string gencash(const std::string& token,
                    const uint8_t easiness,
                    const cancel_epoch_t reqSnapshot,
                    const unsigned maxWorkers);

/**
 * @brief Overload of gencash() with internal ttl (based on server's ttl) and internal maxWorkers
 * based on the platform.
 */
std::string gencash(const std::string& token,
                    const uint8_t easiness,
                    const cancel_epoch_t reqSnapshot);

/**
 * @brief Offline verifier for the hashcash calculated prefix.
 *
 * Rebuilds the 12MB message from token and prefixB64 (the calculated prefix),
 * hashes it once, and checks the leading 32bits against the threshold
 * for easiness.
 *
 * @return true if (prefix, token, easiness) constitute a valid proof, false otherwise.
 */
bool validateHashcash(const std::string& token,
                      const uint8_t easiness,
                      const std::string& prefixB64);

} // namespace mega

#endif // MEGA_HASHCASH_H
