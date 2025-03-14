/**
 * @file
 * @brief Header defining utilities around TOTP token generation
 */

#ifndef INCLUDE_MEGA_TOTP_H_
#define INCLUDE_MEGA_TOTP_H_

#include <bitset>
#include <cassert>
#include <chrono>
#include <optional>
#include <string>

namespace mega::totp
{
using namespace std::chrono_literals;

/**
 * @brief Available algorithms for the hashing performed during otp generation.
 */
enum class HashAlgorithm
{
    SHA1,
    SHA256,
    SHA512,
};

constexpr unsigned NDIGITS_IN_MAX_INT32{10}; // int32_t::max -> 2147483647
constexpr unsigned MIN_ALLOWED_DIGITS_TOTP{6};
constexpr unsigned MAX_ALLOWED_DIGITS_TOTP{NDIGITS_IN_MAX_INT32};
constexpr unsigned DEF_NDIGITS{6};
constexpr auto DEF_EXP_TIME{30s};
constexpr HashAlgorithm DEF_ALG{HashAlgorithm::SHA1};

constexpr std::optional<HashAlgorithm> charTohashAlgorithm(const std::string_view alg)
{
    if (alg == "sha1")
        return HashAlgorithm::SHA1;
    if (alg == "sha256")
        return HashAlgorithm::SHA256;
    if (alg == "sha512")
        return HashAlgorithm::SHA512;
    return std::nullopt;
}

constexpr std::string_view hashAlgorithmToStrView(const HashAlgorithm alg)
{
    switch (alg)
    {
        case HashAlgorithm::SHA1:
            return "sha1";
        case HashAlgorithm::SHA256:
            return "sha256";
        case HashAlgorithm::SHA512:
            return "sha512";
    }
    assert(false);
    return "";
}

enum : uint32_t
{
    INVALID_TOTP_SHARED_SECRET = 0,
    INVALID_TOTP_NDIGITS = 1,
    INVALID_TOTP_EXPT = 2,
    INVALID_TOTP_ALG = 3,
    NUM_TOTP_ERRORS = 4,
};

using TotpValidationErrors = std::bitset<NUM_TOTP_ERRORS>;

constexpr bool isValidNDigits(const unsigned nDigits)
{
    return nDigits >= MIN_ALLOWED_DIGITS_TOTP && nDigits <= MAX_ALLOWED_DIGITS_TOTP;
}

/**
 * @brief Check that all the characters in the given string are contained in
 * "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567" (base 32 specified at RFC 4648). Lowercase and uppercase are
 * both allowed. Padding characters ("=") are allowed only if they are placed at the end.
 */
bool isValidBase32Key(const std::string_view base32Key);

/**
 * @brief Returns the number of valid Base32 characters in the input key
 */
size_t numberOfValidChars(const std::string_view base32Key);

/**
 * @brief Validates the fields of a TOTP.
 *
 * This function checks the validity of various TOTP, including:
 * - Shared secret (`base32Key`): Must be a non-empty, valid Base32-encoded string.
 * - Number of digits (`nDigits`): Must be within the allowed range.
 * - Expiry time (`exptime`): Must be greater than zero.
 * - Hash algorithm (`alg`): Must be an expetected hashing algorithm.
 *
 * If any of these fields are invalid, the corresponding error flag is set in the returned
 * `TotpValidationErrors` bitset.
 *
 * @param base32Key The optional Base32-encoded shared secret key.
 * @param nDigits The optional number of digits for the TOTP code.
 * @param exptime The optional expiration time in seconds.
 * @param alg The optional hashing algorithm identifier.
 * @return TotpValidationErrors A bitmask containing validation errors, if any.
 */
TotpValidationErrors validateFields(const std::optional<std::string_view> base32Key,
                                    const std::optional<unsigned> nDigits,
                                    const std::optional<std::chrono::seconds> exptime,
                                    const std::optional<std::string_view> alg);

/**
 * @brief Generates a TOTP following RFC-6238 (https://www.rfc-editor.org/rfc/rfc6238)
 *
 * @param base32Key The shared secret key.
 * Allowed characters (specified at RFC-4648 https://www.rfc-editor.org/rfc/rfc4648#section-6):
 *     "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"
 * @param nDigits Number of digits expected in the output token. Required to be in [6, 10]
 * @param timeStep To count steps since t0. Required to be greater than 0
 * @param t0 Time origin to count steps
 * @param tEval The time in which the totp wants to be calculated (usually now). Required to be
 * greater or equal than t0
 * @param hashAlgo The algorithm to use for the hashing step
 * @returns A pair with:
 *     - A string with nDigits characters representing the generated totp.
 *     - The time remaining until the token becomes invalid
 * @note If the input parameters are not valid, the returned token will be an empty string. In that
 * case, the value of the returned expiration time isn't valid.
 */
std::pair<std::string, std::chrono::seconds> generateTOTP(
    const std::string& base32Key,
    const unsigned nDigits = DEF_NDIGITS,
    const std::chrono::seconds timeStep = DEF_EXP_TIME,
    const HashAlgorithm hashAlgo = DEF_ALG,
    const std::chrono::system_clock::time_point t0 = {},
    const std::chrono::system_clock::time_point tEval = std::chrono::system_clock::now());

/**
 * @brief This overload accepts directly the number of seconds since the origin (t0 in the previous
 * version), i.e. `timeDelta`. This is very handy for testing purposes where `tEval` cannot be
 * represented as a time_point due to overflow issues.
 */
std::pair<std::string, std::chrono::seconds>
    generateTOTP(const std::string& base32Key,
                 const std::chrono::seconds timeDelta,
                 const unsigned nDigits = DEF_NDIGITS,
                 const std::chrono::seconds timeStep = DEF_EXP_TIME,
                 const HashAlgorithm hashAlgo = DEF_ALG);
}

#endif // INCLUDE_MEGA_TOTP_H_
