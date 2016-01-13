/**
 * @file tests/tests.cpp
 * @brief Mega SDK main test file
 *
 * (c) 2013 by Mega Limited, Wellsford, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#include "mega.h"
#include "gtest/gtest.h"

using namespace mega;
using ::testing::InitGoogleTest;
using ::testing::Test;
using ::testing::TestCase;
using ::testing::TestInfo;
using ::testing::TestPartResult;
using ::testing::UnitTest;

bool debug;

TEST(JSON, storeobject)
{
    std::string in_str("Test");
    JSON j;
    j.storeobject(&in_str);
}

// Test 64-bit int serialization/unserialization
TEST(Serialize64, serialize)
{
    uint64_t in = 0xDEADBEEF;
    uint64_t out;
    byte buf[sizeof in];

    Serialize64::serialize(buf, in);
    ASSERT_GT(Serialize64::unserialize(buf, sizeof buf, &out), 0);
    ASSERT_EQ(in, out);
}

// Test encryption/decryption using AES in mode GCM
// (test vectors from 'tlvstore_test.js', in Webclient)
TEST(CryptoPP, AES_GCM)
{
//    string keyStr = "dGQhii+B7+eLLHRiOA690w==";   // Base64
    string keyStr = "dGQhii-B7-eLLHRiOA690w";     // Base64 URL encoding
    unsigned keyLen = SymmCipher::KEYLENGTH;
    byte keyBytes[keyLen];
    keyLen = Base64::atob(keyStr.data(), keyBytes, keyLen);

    string ivStr = "R8q1njARXS7urWv3";
    unsigned ivLen = 12;
    byte ivBytes[ivLen];
    ivLen = Base64::atob(ivStr.data(), ivBytes, ivLen);

    unsigned tagLen = 16;

    string plainStr = "dGQhwoovwoHDr8OnwossdGI4DsK9w5M";
    unsigned plainLen = plainStr.length();
    byte plainBytes[plainLen];
    plainLen = Base64::atob(plainStr.data(), plainBytes, plainLen);
    string plainText((const char*)plainBytes, plainLen);

    string cipherStr = "L3zqVYAOsRk7zMg2KsNTVShcad8TjIQ7umfsvia21QO0XTj8vaeR";
    unsigned cipherLen = cipherStr.length();
    byte cipherBytes[cipherLen];
    cipherLen = Base64::atob(cipherStr.data(), cipherBytes, cipherLen);
    string cipherText((const char*)cipherBytes, cipherLen);

    SymmCipher key;
    key.setkey(keyBytes, SymmCipher::KEYLENGTH);

    string result;

    // Test AES_GCM_12_16 encryption
    result.clear();
    key.gcm_encrypt(&plainText, ivBytes, ivLen, tagLen, &result);

    ASSERT_STREQ(result.data(), cipherText.data()) << "GCM encryption: cipher text doesn't match the expected value";


    // Test AES_GCM_12_16 decryption
    result.clear();
    key.gcm_decrypt(&cipherText, ivBytes, ivLen, tagLen, &result);

    ASSERT_STREQ(result.data(), plainText.data()) << "GCM decryption: plain text doesn't match the expected value";
}

// Test encryption/decryption using AES in mode CCM
// (test vectors from 'tlvstore_test.js', in Webclient)
TEST(CryptoPP, AES_CCM)
{
    byte keyBytes[] = {
        0x0f, 0x0e, 0x0d, 0x0c,
        0x0b, 0x0a, 0x09, 0x08,
        0x07, 0x06, 0x05, 0x04,
        0x03, 0x02, 0x01, 0x00 };

    byte ivBytes[] = {
        0x00, 0x01, 0x02, 0x03,
        0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b };

    unsigned tagLen = 16;

    byte plainBytes[] = { 0x34, 0x32 };     // "42" in hexadecimal
    string plainText((const char*)plainBytes, sizeof plainBytes);

    byte cipherBytes[] = {
        0x28, 0xbe, 0x1a, 0xc7,
        0xb4, 0x3d, 0x88, 0x68,
        0x86, 0x9b, 0x9a, 0x45,
        0xd3, 0xde, 0x43, 0x6c,
        0xd0, 0xcc };

    string cipherText((const char*)cipherBytes, sizeof cipherBytes);

    SymmCipher key;
    key.setkey(keyBytes, sizeof keyBytes);

    string result;

    // Test AES_CCM_12_16 encryption
    result.clear();
    key.ccm_encrypt(&plainText, ivBytes, sizeof ivBytes, tagLen, &result);

    ASSERT_STREQ(result.data(), cipherText.data()) << "CCM encryption: cipher text doesn't match the expected value";


    // Test AES_CCM_12_16 decryption
    result.clear();
    key.ccm_decrypt(&cipherText, ivBytes, sizeof ivBytes, tagLen, &result);

    ASSERT_STREQ(result.data(), plainText.data()) << "CCM decryption: plain text doesn't match the expected value";
}

int main (int argc, char *argv[])
{
    InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
