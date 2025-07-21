/**
 * @file hashcash.cpp
 * @brief Mega SDK PoW for login
 */

#include "mega/hashcash.h"

#include "mega/base64.h"
#include "mega/logging.h"
#include "mega/utils.h"

namespace
{

constexpr std::size_t kTokenBytes = 48;
constexpr std::size_t kPrefixBytes = 4;
constexpr std::size_t kRepeat = 262144; // 12MB / 48B
constexpr std::size_t kBufSize = kPrefixBytes + kRepeat * kTokenBytes;
constexpr std::size_t kSha256Block = CryptoPP::SHA256::BLOCKSIZE;

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

/**
 * @brief Search one stride of the HashCash solution space.
 *
 * Each worker thread enumerates 32-bit nonces in network (big-endian) order:
 *     nonce = start, start + stride, start + 2*stride, ...
 * For every candidate it computes SHA256 over
 *
 *     [nonce || token * 262144]      // 4B + 12MB
 *
 * and returns the first prefix whose top 32 bits are <= threshold (easiness).
 *
 * @param token        Base64-encoded 64-char token (48B after decoding).
 * @param easiness     Encoded target difficulty.
 * @param start        First nonce for this worker.
 * @param stride       Distance between successive nonces of this worker.
 * @param stop         Shared flag, set to true when any worker finds a hit.
 *
 * @return Base64 encoded 4-byte prefix, or empty when another thread won.
 */
std::string gencashWorker(const std::string& token,
                          const uint8_t easiness,
                          const uint32_t start,
                          const uint32_t stride,
                          std::atomic<bool>& stop)
{
    assert(stride > 0);
    assert(start < stride);

    const auto threshold = thresholdFromEasiness(easiness);
    const auto [tokenBinResult, tokenBin] = getTokenBin(token);
    if (tokenBinResult == false)
    {
        return tokenBin;
    }

    std::vector<uint8_t> buf(kBufSize);
    initTokenArea(tokenBin, buf);
    auto* const noncePtr = reinterpret_cast<uint32_t*>(buf.data());

    thread_local CryptoPP::SHA256 hasher;

    for (uint32_t n = start; !stop; n += stride)
    {
        *noncePtr = htonl(n);

        hasher.Restart();
        hasher.Update(buf.data(), static_cast<unsigned>(buf.size()));

        uint32_t firstWord{};
        hasher.TruncatedFinal(reinterpret_cast<CryptoPP::byte*>(&firstWord), sizeof(uint32_t));

        if (htonl(firstWord) <= threshold)
        {
            stop = true;
            const std::string prefix(reinterpret_cast<char*>(noncePtr), kPrefixBytes);
            return ::mega::Base64::btoa(prefix);
        }
    }

    return {};
}

} // namespace

namespace mega
{

std::string gencash(const std::string& token, const uint8_t easiness, const unsigned maxWorkers)
{
    assert(maxWorkers > 0);

    const auto maxWorkersForDevice = std::max(std::thread::hardware_concurrency(), 1u);
    const auto workers = std::max(std::min(maxWorkers, maxWorkersForDevice), 1u);

    std::atomic<bool> stop{false};
    std::string winner;
    std::mutex winnerMx;
    std::vector<std::thread> pool;
    pool.reserve(workers);

    for (const auto w: range(workers))
    {
        pool.emplace_back(
            [&winnerMx, &token, &easiness, &workers, &w, &stop, &winner]
            {
                auto local = gencashWorker(token, easiness, w, workers, stop);
                if (!local.empty())
                {
                    std::lock_guard<std::mutex> lk(winnerMx);
                    if (winner.empty())
                    {
                        winner = std::move(local);
                    }
                }
            });
    }

    for (auto& t: pool)
        t.join();

    return winner;
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

} // namespace mega
