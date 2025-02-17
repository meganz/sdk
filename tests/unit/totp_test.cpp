/**
 * @file
 * @brief Tests for the contents of totp.h file
 */

#include "mega/logging.h"
#include "mega/totp.h"
#include "megaapi.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

using namespace mega;
using namespace mega::totp;
using namespace std::chrono;

struct TotpTestRow
{
    seconds timeSinceEpoch;
    std::string expectedResult;
    HashAlgorithm algorithm;

    /**
     * @brief Return the secret used in Appendix B in https://www.rfc-editor.org/rfc/rfc6238
     * There the secrets are in hex. Here they are translated to base32
     */
    std::string getSecretInRFC() const
    {
        switch (algorithm)
        {
            case HashAlgorithm::SHA1:
                return "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ";
            case HashAlgorithm::SHA256:
                return "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQGEZA====";
            case HashAlgorithm::SHA512:
                return "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQGEZDGNBVGY3"
                       "TQOJQGEZDGNBVGY3TQOJQGEZDGNA=";
        }
        return "";
    }
};

/**
 * @brief Test the cases presented in Appendix B in https://www.rfc-editor.org/rfc/rfc6238
 */
TEST(GenerateTOTP, RFC6238TestVector)
{
    static const std::vector<TotpTestRow> testsVectors{
        {59s, "94287082", HashAlgorithm::SHA1},
        {59s, "46119246", HashAlgorithm::SHA256},
        {59s, "90693936", HashAlgorithm::SHA512},
        {1111111109s, "07081804", HashAlgorithm::SHA1},
        {1111111109s, "68084774", HashAlgorithm::SHA256},
        {1111111109s, "25091201", HashAlgorithm::SHA512},
        {1111111111s, "14050471", HashAlgorithm::SHA1},
        {1111111111s, "67062674", HashAlgorithm::SHA256},
        {1111111111s, "99943326", HashAlgorithm::SHA512},
        {1234567890s, "89005924", HashAlgorithm::SHA1},
        {1234567890s, "91819424", HashAlgorithm::SHA256},
        {1234567890s, "93441116", HashAlgorithm::SHA512},
        {2000000000s, "69279037", HashAlgorithm::SHA1},
        {2000000000s, "90698825", HashAlgorithm::SHA256},
        {2000000000s, "38618901", HashAlgorithm::SHA512},
        {20000000000s, "65353130", HashAlgorithm::SHA1},
        {20000000000s, "77737706", HashAlgorithm::SHA256},
        {20000000000s, "47863826", HashAlgorithm::SHA512}};

    for (const auto& tv: testsVectors)
    {
        const auto [totp, expirationTime] =
            generateTOTP(tv.getSecretInRFC(), tv.timeSinceEpoch, 8, 30s, tv.algorithm);
        EXPECT_EQ(totp, tv.expectedResult);
        EXPECT_EQ(expirationTime.count(), 30 - tv.timeSinceEpoch.count() % 30);
    }
}

TEST(GenerateTOTP, PreconditionsFailure)
{
    EXPECT_EQ(generateTOTP("").first, "") << "Empty shared secret";
    EXPECT_EQ(generateTOTP("GEZDGN==BVGY3TQOJQGEZDGNBVGY3TQOJQ").first, "")
        << "Padding in between the secret";
    EXPECT_EQ(generateTOTP("AAAAA0").first, "") << "Invalid character (0)";
    EXPECT_EQ(generateTOTP("GEZDGN", 5).first, "") << "Less digits than allowed";
    EXPECT_EQ(generateTOTP("GEZDGN", 11).first, "") << "More digits than allowed";
    EXPECT_EQ(generateTOTP("GEZDGN", 6, -5s).first, "") << "Negative time step";
    EXPECT_EQ(generateTOTP("GEZDGN", 6, 0s).first, "") << "Zero time step";
    EXPECT_EQ(generateTOTP("GEZDGN",
                           6,
                           30s,
                           HashAlgorithm::SHA1,
                           system_clock::now(),
                           system_clock::now() - 5s)
                  .first,
              "")
        << "tEval lower than t0";
    EXPECT_EQ(generateTOTP("GEZDGN", -5s).first, "") << "Negative time delta";
}

using TotpData = MegaNode::PasswordNodeData::TotpData;
using Validation = MegaNode::PasswordNodeData::TotpData::Validation;

static std::pair<std::unique_ptr<TotpData>, std::unique_ptr<Validation>>
    generateData(const char* shse, const int expT, const int alg, const int nd)
{
    std::unique_ptr<TotpData> data{TotpData::createInstance(shse, expT, alg, nd)};
    std::unique_ptr<Validation> valid{data->getValidation()};
    return {std::move(data), std::move(valid)};
};

TEST(GenerateTOTPFromTotpData, PreconditionsFailure)
{
    static constexpr std::string_view logPre{"GenerateTOTPFromTotpData.PreconditionsFailure: "};
    {
        LOG_debug << logPre << "Empty shared secret";
        const auto [data, valid] = generateData("", -1, -1, -1);
        EXPECT_TRUE(valid->sharedSecretExist());
        EXPECT_FALSE(valid->sharedSecretValid());
        // Validate defaults in first case
        EXPECT_TRUE(valid->nDigitsValid());
        EXPECT_TRUE(valid->expirationTimeValid());
        EXPECT_TRUE(valid->algorithmValid());
    }

    {
        LOG_debug << logPre << "Invalid shared secret";
        const auto [data, valid] = generateData("GEZDGN==BVGY3TQOJQGEZDGNBVGY3TQOJQ", -1, -1, -1);
        EXPECT_TRUE(valid->sharedSecretExist());
        EXPECT_FALSE(valid->sharedSecretValid());
    }

    {
        LOG_debug << logPre << "Invalid expiration time";
        const auto [data, valid] = generateData("GEZDGN", 0, -1, -1);
        EXPECT_FALSE(valid->expirationTimeValid());
    }

    {
        LOG_debug << logPre << "Invalid hash algorithm";
        const auto [data, valid] = generateData("GEZDGN", -1, 50, -1);
        EXPECT_FALSE(valid->algorithmValid());
    }

    {
        LOG_debug << logPre << "Invalid digits";
        const auto [data, valid] = generateData("GEZDGN", -1, -1, 5);
        EXPECT_FALSE(valid->nDigitsValid());
    }
}
