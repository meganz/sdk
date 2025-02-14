#include "mega/totp.h"

#include "mega/logging.h"

#include <cryptopp/base32.h>
#include <cryptopp/filters.h>
#include <cryptopp/hmac.h>
#include <cryptopp/secblock.h>
#include <cryptopp/sha.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iomanip>

namespace mega::totp
{

using namespace std::chrono; // convenience in this compilation unit

namespace
{

constexpr unsigned NDIGITS_IN_MAX_INT32{10}; // int32_t::max -> 2147483647
constexpr unsigned MIN_ALLOWED_DIGITS_TOTP{6};
constexpr unsigned MAX_ALLOWED_DIGITS_TOTP{NDIGITS_IN_MAX_INT32};

using CryptoPP::SecByteBlock;

constexpr bool isValidBase32Digit(const char c)
{
    return ('2' <= c && c <= '7');
}

/**
 * @brief Check if c is in "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"
 */
bool isValidBase32Char(const char c)
{
    return std::isalpha(c) != 0 || isValidBase32Digit(c);
}

static constexpr char PADDING_CHAR{'='};

constexpr bool isPaddingChar(const char c)
{
    return c == '=';
}

/**
 * @brief Check that all the characters in the given string are contained in
 * "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567". Lowercase and uppercase are both allowed.
 * Padding characters ("=") are allowed only if they are placed at the end.
 */
bool isValid(const std::string_view base32Key)
{
    if (const auto pos = base32Key.find(PADDING_CHAR); pos != std::string_view::npos)
    {
        const auto endSlice = base32Key.substr(pos);
        return isValid(base32Key.substr(0, pos)) &&
               std::all_of(begin(endSlice), end(endSlice), isPaddingChar);
    }
    return std::all_of(begin(base32Key), end(base32Key), isValidBase32Char);
}

size_t numberOfValidChars(const std::string_view base32Key)
{
    return static_cast<size_t>(std::count_if(begin(base32Key), end(base32Key), isValidBase32Char));
}

/**
 * @brief Given a char returns its index in the string "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"
 * @warning The function assumes the input char is a valid one, if not, 0 is returned.
 */
unsigned base32CharValue(const char c)
{
    if (isalpha(c) != 0)
        return static_cast<unsigned>((c - 1) % 32);
    if (isValidBase32Digit(c))
        return static_cast<unsigned>(c - 24);
    assert(false);
    return 0;
}

/**
 * @brief Converts a Base32-encoded string into a SecByteBlock of bytes.
 *
 * This function decodes a Base32 string by mapping each character to its
 * corresponding 5-bit value. These 5-bit groups are accumulated into a buffer.
 * Whenever at least 8 bits are available in the buffer, the function extracts
 * the top 8 bits to form a byte, which is then appended to the output SecByteBlock.
 * Any remaining space in the SecByteBlock is filled with zeros.
 *
 * @param base32Str A string containing the Base32-encoded data.
 * @return SecByteBlock containing the decoded byte sequence.
 */
SecByteBlock toByteBlock(const std::string& base32Str)
{
    SecByteBlock bytes(base32Str.size() * 5 / 8); // truncate size
    unsigned byteIndex = 0;
    const auto mapAndAppend = [&bytes, &byteIndex, bits = 0u, bitCount = 0u](const char c) mutable
    {
        if (isPaddingChar(c))
            return;
        const auto cValue = base32CharValue(c);
        bits = (bits << 5) | cValue;
        bitCount += 5;
        if (bitCount >= 8)
            bytes[byteIndex++] = static_cast<unsigned char>(bits >> (bitCount -= 8));
    };
    std::for_each(begin(base32Str), end(base32Str), mapAndAppend);
    std::fill(std::begin(bytes) + byteIndex, std::end(bytes), 0); // just in case
    return bytes;
}

/**
 * @brief Returns the expected number of bytes for the output token returned by the HMAC routine
 * depending on the used hashing algorithm.
 */
template<typename Hash>
constexpr size_t getHmacOutNBytes()
{
    if constexpr (std::is_same_v<Hash, CryptoPP::SHA1>)
        return 20;
    if constexpr (std::is_same_v<Hash, CryptoPP::SHA256>)
        return 32;
    if constexpr (std::is_same_v<Hash, CryptoPP::SHA512>)
        return 64;
    // Compilation error if not supported Hash algorithm
}

bool isLittleEndianHost()
{
    constexpr std::uint32_t endianness = 0xdeadbeef;
    return *reinterpret_cast<const std::uint8_t*>(&endianness) == 0xef;
}

/**
 * @brief Maps to what is denoted by HMAC-SHA-???(K, C) in RFC-4226
 * (https://www.rfc-editor.org/rfc/rfc4226)
 * - K: `secret`
 * - C: `counter`
 *
 * To get the final token, truncate the result
 */
template<typename Hash>
std::vector<std::uint8_t> hotpBytes(const SecByteBlock& secret, const std::int64_t counter)
{
    std::array<std::uint8_t, sizeof(std::int64_t)> counterArray;
    std::memcpy(counterArray.data(), &counter, sizeof(counter));
    if (isLittleEndianHost()) // Ensure big endian
        std::reverse(std::begin(counterArray), std::end(counterArray));

    std::vector<std::uint8_t> result(getHmacOutNBytes<Hash>());
    CryptoPP::HMAC<Hash> mac;
    mac.SetKey(secret.data(), secret.size());
    mac.CalculateDigest(&result[0], counterArray.data(), 8);
    return result;
}

/**
 * @brief Dispatcher to call the correct implementation depending on the given `hashAlgo`
 */
std::vector<std::uint8_t> hotpBytes(const SecByteBlock& secret,
                                    const std::int64_t counter,
                                    const HashAlgorithm hashAlgo)
{
    switch (hashAlgo)
    {
        case HashAlgorithm::SHA1:
            return hotpBytes<CryptoPP::SHA1>(secret, counter);
        case HashAlgorithm::SHA256:
            return hotpBytes<CryptoPP::SHA256>(secret, counter);
        case HashAlgorithm::SHA512:
            return hotpBytes<CryptoPP::SHA512>(secret, counter);
    }
    // Silence compilation warning
    return hotpBytes<CryptoPP::SHA1>(secret, counter);
}

/**
 * @brief Maps to what is denoted by HMAC-SHA-???(K, T) in the RFC-6238
 * - K: `secret`
 * - T: The number of `timeStep`s passed in the given timeDelta
 *
 * To get the final token, truncate the result
 */
std::vector<std::uint8_t> totpBytes(const SecByteBlock& secret,
                                    const seconds timeStep,
                                    const seconds timeDelta,
                                    const HashAlgorithm hashAlgo)
{
    const std::int64_t timeCounter = timeDelta.count() / timeStep.count();
    return hotpBytes(secret, timeCounter, hashAlgo);
}

/**
 * @brief See RFC-4226 Sec. 5.4. (https://www.rfc-editor.org/rfc/rfc4226)
 */
std::int32_t dynamicOffsetTruncation(const std::vector<std::uint8_t>& hmac)
{
    const auto offset = static_cast<size_t>(hmac[hmac.size() - 1] & 0xf);
    const std::int32_t bincode = ((hmac[offset] & 0x7f) << 24) | ((hmac[offset + 1] & 0xff) << 16) |
                                 ((hmac[offset + 2] & 0xff) << 8) |
                                 ((hmac[offset + 3] & 0xff) << 0);
    return bincode;
}

std::int32_t moduloReduction(const std::int32_t bincode, const unsigned nDigits)
{
    if (nDigits == NDIGITS_IN_MAX_INT32)
        return bincode;
    constexpr std::array powersOf10upTo9{
        1,
        10,
        100,
        1'000,
        10'000,
        100'000,
        1'000'000,
        10'000'000,
        100'000'000,
        1'000'000'000,
    };
    return bincode % powersOf10upTo9.at(nDigits);
}

int32_t truncate(const std::vector<std::uint8_t>& hmac, const unsigned nDigits)
{
    return moduloReduction(dynamicOffsetTruncation(hmac), nDigits);
}

/**
 * @brief Converts the given `number` in a string, padding '0' at front until `nDigits` characters
 */
std::string to0PaddedStr(const int32_t number, const unsigned nDigits)
{
    std::ostringstream oss;
    oss << std::setw(static_cast<int>(nDigits)) << std::setfill('0') << number;
    return oss.str();
}

bool areInputsValid(const std::string& base32Key,
                    const seconds timeDelta,
                    const unsigned nDigits,
                    const seconds timeStep)
{
    bool areValid{true};
    if (base32Key.empty() || numberOfValidChars(base32Key) == 0)
    {
        LOG_err << "Empty input shared secret";
        areValid = false;
    }
    if (!isValid(base32Key))
    {
        LOG_err << "Input shared secret contains invalid characters: " << base32Key;
        areValid = false;
    }
    if (nDigits < MIN_ALLOWED_DIGITS_TOTP || nDigits > MAX_ALLOWED_DIGITS_TOTP)
    {
        LOG_err << "Invalid number of digits (allowed between " << MIN_ALLOWED_DIGITS_TOTP
                << " and " << MAX_ALLOWED_DIGITS_TOTP << "). Given: " << nDigits;
        areValid = false;
    }
    if (timeStep <= 0s)
    {
        LOG_err << "Invalid time step (expected value greater than 0): " << timeStep.count();
        areValid = false;
    }
    if (timeDelta.count() < 0)
    {
        LOG_err << "Invalid time delta (negative): " << timeDelta.count();
        areValid = false;
    }
    return areValid;
}

seconds getRemainingValidTime(const seconds timeDelta, const seconds timeStep)
{
    return timeStep - timeDelta % timeStep;
}
}

std::pair<std::string, seconds> generateTOTP(const std::string& base32Key,
                                             const unsigned nDigits,
                                             const seconds timeStep,
                                             const system_clock::time_point t0,
                                             const system_clock::time_point tEval,
                                             const HashAlgorithm hashAlgo)
{
    return generateTOTP(base32Key, duration_cast<seconds>(tEval - t0), nDigits, timeStep, hashAlgo);
}

std::pair<std::string, seconds> generateTOTP(const std::string& base32Key,
                                             const seconds timeDelta,
                                             const unsigned nDigits,
                                             const seconds timeStep,
                                             const HashAlgorithm hashAlgo)
{
    if (!areInputsValid(base32Key, timeDelta, nDigits, timeStep))
        return {};

    const auto byteBlockKey = toByteBlock(base32Key);
    return {to0PaddedStr(truncate(totpBytes(byteBlockKey, timeStep, timeDelta, hashAlgo), nDigits),
                         nDigits),
            getRemainingValidTime(timeDelta, timeStep)};
}
}
