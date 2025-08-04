/**
 * @file mega/hashcash.h
 * @brief Mega SDK PoW for login
 */

#ifndef MEGA_HASHCASH_H
#define MEGA_HASHCASH_H 1

#include <cstdint>
#include <string>

namespace mega
{
static constexpr unsigned MAX_WORKERS_FOR_GENCASH =
#if defined(__ANDROID__) || defined(USE_IOS)
    2u;
#else
    8u;
#endif

/**
 * @brief Multi threaded HashCash solver.
 *
 * Spawns workers threads (capped by maxWorkers and hardware_concurrency),
 * each running gencashWorker with a different stride.
 *
 * The first successful prefix is returned, all other workers are signalled to exit early.
 *
 * @param token       Base64 token issued by the server.
 * @param easiness    Target difficulty.
 * @param maxWorkers  User cap: 8 for desktop, 2 for mobile, etc.
 *
 * @return Base64 encoded 4-byte prefix satisfying the difficulty target.
 */
std::string gencash(const std::string& token,
                    const uint8_t easiness,
                    const unsigned maxWorkers = MAX_WORKERS_FOR_GENCASH);

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
