/**
 * (c) 2019 by Mega Limited, Wellsford, New Zealand
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
#include "../src/crypto/sodium.cpp"
#include <math.h>
#include "gtest/gtest.h"

using namespace mega;

// Test encryption/decryption using AES in mode GCM
// (test vectors from 'tlvstore_test.js', in Webclient)
TEST(Crypto, AES_GCM)
{
//    string keyStr = "dGQhii+B7+eLLHRiOA690w==";   //gitleaks:allow
    string keyStr = "dGQhii-B7-eLLHRiOA690w";     //gitleaks:allow Base64 URL encoding
    unsigned keyLen = SymmCipher::KEYLENGTH;
    byte* keyBytes = new byte[keyLen];
    keyLen = Base64::atob(keyStr.data(), keyBytes, keyLen);

    string ivStr = "R8q1njARXS7urWv3";
    unsigned ivLen = 12;
    byte* ivBytes = new byte[ivLen];
    ivLen = Base64::atob(ivStr.data(), ivBytes, ivLen);

    unsigned tagLen = 16;

    string plainStr = "dGQhwoovwoHDr8OnwossdGI4DsK9w5M";
    auto plainLen = plainStr.length();
    byte* plainBytes = new byte[plainLen];
    plainLen = Base64::atob(plainStr.data(), plainBytes, static_cast<int>(plainLen));
    string plainText((const char*)plainBytes, plainLen);

    string cipherStr = "L3zqVYAOsRk7zMg2KsNTVShcad8TjIQ7umfsvia21QO0XTj8vaeR";
    auto cipherLen = cipherStr.length();
    byte* cipherBytes = new byte[cipherLen];
    cipherLen = Base64::atob(cipherStr.data(), cipherBytes, static_cast<int>(cipherLen));
    string cipherText((const char*)cipherBytes, cipherLen);

    SymmCipher key;
    key.setkey(keyBytes, SymmCipher::KEYLENGTH);

    string result;

    // Test AES_GCM_12_16 encryption
    result.clear();
    ASSERT_TRUE(key.gcm_encrypt(&plainText, ivBytes, ivLen, tagLen, &result)) << "GCM encryption failed";

    ASSERT_STREQ(result.data(), cipherText.data()) << "GCM encryption: cipher text doesn't match the expected value";


    // Test AES_GCM_12_16 decryption
    result.clear();
    ASSERT_TRUE(key.gcm_decrypt(&cipherText, ivBytes, ivLen, tagLen, &result)) << "GCM decryption failed";

    ASSERT_STREQ(result.data(), plainText.data()) << "GCM decryption: plain text doesn't match the expected value";

    delete[] keyBytes;
    delete[] ivBytes;
    delete[] plainBytes;
    delete[] cipherBytes;
}



// Test encryption/decryption of the xxTEA algorithm that we use for media file attributes
TEST(Crypto, xxTea)
{
    // two cases with data generated in the javascript version
    {
        uint32_t key1[4] = { 0x00000000, 0x01000000, 0x02000000, 0x03000000 };
        uint32_t data1[16];
        for (unsigned i = sizeof(data1) / sizeof(data1[0]); i--; ) data1[i] = i;
        uint32_t encCmpData[16] = { 140302874, 3625593116, 1921165214, 2581869937, 2444819365, 2195760850, 718076837, 454900461, 2002331402, 793381415, 760353645, 2589596551, 709756921, 4142288381, 633884585, 418697353 };
        xxteaEncrypt(data1, 16, key1);
        ASSERT_TRUE(0 == memcmp(data1, encCmpData, sizeof(data1)));
        xxteaDecrypt(data1, 16, key1);
        for (unsigned i = sizeof(data1) / sizeof(data1[0]); i--; )
        {
            ASSERT_TRUE(data1[i] == i);
        }
    }

    {
        uint32_t key2[4] = { 0, 0xFFFFFFFF,  0xFEFFFFFF, 0xFDFFFFFF };
        uint32_t data2[16];
        for (unsigned i = sizeof(data2) / sizeof(data2[0]); i--; ) data2[i] = -(int32_t)i;
        uint32_t encCmpData2[16] = { 1331968695, 2520133218, 2881973170, 783802011, 1812010991, 1359505125, 15067484, 3344073997, 4210258643, 824383226, 3584459687, 2866083302, 881254637, 502181030, 680349945, 1722488731 };
        xxteaEncrypt(data2, 16, key2);
        ASSERT_TRUE(0 == memcmp(data2, encCmpData2, sizeof(data2)));
        xxteaDecrypt(data2, 16, key2);
        for (unsigned i = sizeof(data2) / sizeof(data2[0]); i--; )
        {
            ASSERT_TRUE(data2[i] == uint32_t(-(int)i));
        }
    }
}


// Test encryption/decryption using AES in mode CCM
// (test vectors from 'tlvstore_test.js', in Webclient)
TEST(Crypto, AES_CCM)
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
    ASSERT_TRUE(key.ccm_encrypt(&plainText, ivBytes, sizeof ivBytes, tagLen, &result)) << "CCM encryption failed";

    ASSERT_STREQ(result.data(), cipherText.data()) << "CCM encryption: cipher text doesn't match the expected value";


    // Test AES_CCM_12_16 decryption
    result.clear();
    ASSERT_TRUE(key.ccm_decrypt(&cipherText, ivBytes, sizeof ivBytes, tagLen, &result)) << "CCM decryption failed";

    ASSERT_STREQ(result.data(), plainText.data()) << "CCM decryption: plain text doesn't match the expected value";
}

#ifdef ENABLE_CHAT
// Test functions of Ed25519:
// - Binary & Hex fingerprints of public key
// - Creation of signature for RSA public key
// - Verification of signature for RSA public key
// - Creation and verification of signatures for random messages
//
// (test vectors from 'authrigh_test.js', in Webclient)
TEST(Crypto, Ed25519_Signing)
{
//    string prEd255str   = "nWGxne/9WmC6hEr0kuwsxERJxWl7MmkZcDusAxyuf2A=";           // Base64
    string prEd255str   = "nWGxne_9WmC6hEr0kuwsxERJxWl7MmkZcDusAxyuf2A=";           // Base64 URL encoded

//    string puEd255str   = "11qYAYKxCrfVS/7TyWQHOg7hcvPapiMlrwIaaPcHURo";            // Base64
    string puEd255str   = "11qYAYKxCrfVS_7TyWQHOg7hcvPapiMlrwIaaPcHURo";            // Base64 URL encoded

//    string fpEd255str   = "If4x36FUomFia/hUBG/SJxt77Us";                            // Base64
    string fpEd255str   = "If4x36FUomFia_hUBG_SJxt77Us";                            // Base64 url encoded
    string fpEd255hex   = "21FE31DFA154A261626BF854046FD2271B7BED4B";

    string pqstr        = "1XJHwX9WYEVk7KOack5nhOgzgnYWrVdt0UY2yn5Lw38mPzkVn"
                          "kHCmguqWIfL5bzVpbHHhlG9yHumvyyu9r1gKUMz4Y/1cf69"
                          "1WIQmRGfg8dB2TeRUSvwb2A7EFGeFqQZHclgvpM2aq4PXrP"
                          "PmQAciTxjguxcL1lem/fXGd1X6KKxPJ+UfQ5TZbV4O2aOwY"
                          "uxys1YHh3mNHEp/xE1/fx292hdejPTJIX8IC5zjsss76e9P"
                          "SVOgSrz+jQQYKbKpT5Yamml98bEZuLY9ncMGUmw5q4WHi/O"
                          "dcvskHUydAL0qNOqbCwvt1Y7xIQfclR0SQE/AbwuJui0mt3"
                          "PuGjM42T/DQ==";
    string estr         = "AQE=";

    string fpRSAstr     = "GN2sWsukWnEarqVPS7mE5sPro38";                            // Base64 url encoded
    string fpRSAhex     = "18ddac5acba45a711aaea54f4bb984e6c3eba37f";

    string sigRSAstr    = "AAAAAFPqtrj3Qr4d83Oz/Ya6svzJfeoSBtWPC7KBU4"           // Base64
                          "KqWMI8OX3eXT45+IyWCTTA5yeip/GThvkS8O2HBF"
                          "aNLvSAFq5/5lQG";

    uint64_t sigRSAts   = 1407891128; // authring_test.js specify 1407891127650 ms, which is later rounded to seconds


    // Initialize variables

    const int keySeedLen = EdDSA::SEED_KEY_LENGTH;
    unsigned char keySeed[keySeedLen];
    ASSERT_EQ(keySeedLen, Base64::atob(prEd255str.data(), keySeed, keySeedLen))
            << "Failed to convert Ed25519 private key to binary";

    PrnGen rng;
    EdDSA signkey(rng, keySeed);

    string puEd255bin;
    puEd255bin.resize(puEd255str.size() * 3 / 4 + 3);
    puEd255bin.resize(Base64::atob(puEd255str.data(), (byte*) puEd255bin.data(), static_cast<int>(puEd255bin.size())));
    ASSERT_TRUE(!memcmp(puEd255bin.data(), signkey.pubKey, EdDSA::PUBLIC_KEY_LENGTH))
            << "Public Ed25519 key doesn't match the derived public key";

    // convert from Base64 to Base64 URL encoding
    std::replace(pqstr.begin(), pqstr.end(), '+', '-');
    std::replace(pqstr.begin(), pqstr.end(), '/', '_');

    string pqbin;
    pqbin.resize(pqstr.size() * 3 / 4 + 3);
    pqbin.resize(Base64::atob(pqstr.data(), (byte*) pqbin.data(), static_cast<int>(pqbin.size())));

    string ebin;
    ebin.resize(estr.size() * 3 / 4 + 3);
    ebin.resize(Base64::atob(estr.data(), (byte*) ebin.data(), static_cast<int>(ebin.size())));

    string pubRSAbin;
    pubRSAbin.append(pqbin.data(), pqbin.size());
    pubRSAbin.append(ebin.data(), ebin.size());

    // convert from Base64 to Base64 URL encoding
    std::replace(sigRSAstr.begin(), sigRSAstr.end(), '+', '-');
    std::replace(sigRSAstr.begin(), sigRSAstr.end(), '/', '_');

    string sigRSAbin;
    sigRSAbin.resize(sigRSAstr.size() * 4 / 3 + 4);
    sigRSAbin.resize(Base64::atob(sigRSAstr.data(), (byte *) sigRSAbin.data(), static_cast<int>(sigRSAbin.size())));


    // ____ Check signature of RSA public key ____

    string sigPubk;
    signkey.signKey((unsigned char *) pubRSAbin.data(), pubRSAbin.size(), &sigPubk, sigRSAts);

    ASSERT_EQ(sigRSAbin.size(), sigPubk.size()) << "Wrong size of signature";
    ASSERT_TRUE(!memcmp(sigRSAbin.data(), sigPubk.data(), sigRSAbin.size())) << "RSA signatures don't match";


    // ____ Verify signature of RSA public key ____

    // good signature
    bool sigOK = EdDSA::verifyKey((unsigned char*) pubRSAbin.data(), pubRSAbin.size(),
                                   &sigRSAbin, (unsigned char*) puEd255bin.data());
    ASSERT_TRUE(sigOK) << "Verification of RSA signature failed.";

    // bad signature
    string sigBuf = sigRSAbin;
    sigBuf.at(70) = 42;
    sigOK = EdDSA::verifyKey((unsigned char*) pubRSAbin.data(), pubRSAbin.size(),
                                       &sigBuf, (unsigned char*) puEd255bin.data());
    ASSERT_FALSE(sigOK) << "Verification of bad RSA signature succeed when it should fail.";

    // empty signature
    sigBuf.clear();
    sigOK = EdDSA::verifyKey((unsigned char*) pubRSAbin.data(), pubRSAbin.size(),
                                       &sigBuf, (unsigned char*) puEd255bin.data());
    ASSERT_FALSE(sigOK) << "Verification of empty RSA signature succeed when it should fail.";

    // bad timestamp
    sigBuf = sigRSAbin;
    sigBuf.at(0) = 42;
    sigOK = EdDSA::verifyKey((unsigned char*) pubRSAbin.data(), pubRSAbin.size(),
                                       &sigBuf, (unsigned char*) puEd255bin.data());
    ASSERT_FALSE(sigOK) << "Verification of RSA signature with wrong timestamp succeed when it should fail.";

    // signature with bad point
    sigBuf = sigRSAbin;
    sigBuf.at(8) = 42;
    sigOK = EdDSA::verifyKey((unsigned char*) pubRSAbin.data(), pubRSAbin.size(),
                                       &sigBuf, (unsigned char*) puEd255bin.data());
    ASSERT_FALSE(sigOK) << "Verification of RSA signature with bad point succeed when it should fail.";


    // ____ Create and verify signatures of random messages ____

    const unsigned keylen = SymmCipher::KEYLENGTH;
    byte key[keylen];
    string sig;
    for (int i = 0; i < 100; i++)
    {
        rng.genblock(key, keylen);
        signkey.signKey((unsigned char *) key, keylen, &sig);

        ASSERT_TRUE(EdDSA::verifyKey((unsigned char*) pubRSAbin.data(), pubRSAbin.size(),
                                      &sigRSAbin, (unsigned char*) puEd255bin.data()))
                << "Verification of signature failed for a random key.";
    }
}

#endif

TEST(Crypto, SymmCipher_xorblock_bytes)
{
    byte src[10] = { (byte)0, (byte)1, (byte)2, (byte)3, (byte)4, (byte)5, (byte)6, (byte)7, (byte)8, (byte)9 };
    byte dest[10] = { (byte)20, (byte)30, (byte)40, (byte)50, (byte)60, (byte)70, (byte)80, (byte)90, (byte)100, (byte)110 };
    SymmCipher::xorblock(src, dest, sizeof(dest));
    byte result[10] = { (byte)(0 ^ (byte)20), (byte)(1 ^ (byte)30), (byte)(2 ^ (byte)40), (byte)(3 ^ (byte)50), (byte)(4 ^ (byte)60), (byte)(5 ^ (byte)70), (byte)(6 ^ (byte)80), (byte)(7 ^ (byte)90), (byte)(8 ^ (byte)100), (byte)(9 ^ (byte)110) };
    ASSERT_EQ(memcmp(dest, result, sizeof(dest)), 0);
}

TEST(Crypto, SymmCipher_xorblock_block_aligned)
{
    byte src[SymmCipher::BLOCKSIZE];
    byte n = 0;
    std::generate(src, src + sizeof(src), [&n]() {return n++; });
    ASSERT_EQ(ptrdiff_t((ptrdiff_t)src % sizeof(ptrdiff_t)), (ptrdiff_t)0);

    byte dest[SymmCipher::BLOCKSIZE];
    n = 100;
    std::generate(dest, dest + sizeof(src), [&n]() { return n = static_cast<byte>(n + 3); });
    ASSERT_EQ(ptrdiff_t((ptrdiff_t)dest % sizeof(ptrdiff_t)), (ptrdiff_t)0);

    byte result[SymmCipher::BLOCKSIZE];
    byte* output = result;
    std::transform(src, src + sizeof(src), dest, output, [](byte a, byte b) { return (byte)(a ^ b); });
    SymmCipher::xorblock(src, dest); // aligned case

    ASSERT_EQ(memcmp(dest, result, sizeof(dest)), 0);
}

TEST(Crypto, SymmCipher_xorblock_block_unaligned)
{
    byte src[SymmCipher::BLOCKSIZE + 1];
    byte n = 0;
    std::generate(src, src + sizeof(src), [&n]() {return n++; });

    byte dest[SymmCipher::BLOCKSIZE];
    n = 100;
    std::generate(dest, dest + sizeof(dest), [&n]() { return n = static_cast<byte>(n + 3); });

    byte result[SymmCipher::BLOCKSIZE];
    byte* output = result;
    std::transform(src + 1, src + sizeof(src), dest, output, [](byte a, byte b) { return (byte)(a ^ b); });
    SymmCipher::xorblock(src + 1, dest); // un-aligned case

    ASSERT_EQ(memcmp(dest, result, sizeof(dest)), 0);
}
