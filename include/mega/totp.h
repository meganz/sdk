/**
 * @file
 * @brief Header defining utilities around TOTP token generation
 */

#ifndef INCLUDE_MEGA_TOTP_H_
#define INCLUDE_MEGA_TOTP_H_

#include <chrono>
#include <string>

namespace mega::totp
{

/**
 * @brief Available algorithms for the hashing performed during otp generation.
 */
enum class HashAlgorithm
{
    SHA1,
    SHA256,
    SHA512
};

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
    const unsigned nDigits = 6,
    const std::chrono::seconds timeStep = std::chrono::seconds{30},
    const std::chrono::system_clock::time_point t0 = {},
    const std::chrono::system_clock::time_point tEval = std::chrono::system_clock::now(),
    const HashAlgorithm hashAlgo = HashAlgorithm::SHA1);

/**
 * @brief This overload accepts directly the number of seconds since the origin (t0 in the previous
 * version), i.e. `timeDelta`. This is very handy for testing purposes where `tEval` cannot be
 * represented as a time_point due to overflow issues.
 */
std::pair<std::string, std::chrono::seconds>
    generateTOTP(const std::string& base32Key,
                 const std::chrono::seconds timeDelta,
                 const unsigned nDigits = 6,
                 const std::chrono::seconds timeStep = std::chrono::seconds{30},
                 const HashAlgorithm hashAlgo = HashAlgorithm::SHA1);
}

#endif // INCLUDE_MEGA_TOTP_H_
