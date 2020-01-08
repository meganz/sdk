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
#include "gtest/gtest.h"

using namespace mega;

TEST(PayCrypter, allFeatures)
{
    char BASE64_IV[]        = "7XS3jX8CrWh6gpZIavQamA";
    char BASE64_ENCKEY[]    = "IcfMNKnMLJNJAH-XPMDShw";
    char BASE64_HMACKEY[]   = "rChwtATCap-CXO-KGxbEKZLL88lVfdZPZfZcdnMtj8o";
    char KEYS[]             = "IcfMNKnMLJNJAH-XPMDSh6wocLQEwmqfglzvihsWxCmSy_PJVX3WT2X2XHZzLY_K";

    char CONTENT[] = "{\"first_name\": \"First\", \"last_name\": \"Last\","
                   " \"card_number\": \"4532646653175959\","
                   " \"expiry_date_month\": \"10\", \"expiry_date_year\": \"2020\","
                   " \"cv2\": \"123\", \"address1\": \"120 Albert St\","
                   " \"address2\": \"Auckland Central\", \"city\": \"Auckland\","
                   " \"province\": \"Auckland\", \"postal_code\": \"1010\","
                   " \"country_code\": \"NZ\"}";

    char CIPHER_BYTES[] = "Btu6B6YxQV1oeMRij4Fn0Que9FfIE1LJyYdacVbNBM1bS-GZAtwQh5"
       "ZTtsakK6v_mMZGiQ3egRFSTNHzQU0jVa0GYZJ087NhlKlGtVO6PvBKmTkxpcnZpy1im"
       "S6uzKLccQU-IxKm1XnBF7gB7McbXDxb-j_s3-sjMJo_npDBOR3hUePGSyN-jmed7mvO"
       "K_fNY8DHqodpdVk7vy2PL8_iAY2SefttWGCD8DwiyxXx42KAjUaRHiYJqgdkZheF_Rp"
       "9l-KxgW8krDdkHsQu-nqeciezk5iA5OlylUmCfc57AKztBElyd4KIfz4B7kprmTeiiH"
       "8lhTCq7xZ64GdABzwfQghkf-fM9NJUD9bHfbTYfnnDRSvDrdJtD1gRVrkxnHNNVKhd6"
       "rtKToreM2bFhfUpcw";

    char HMAC[] = "C7WRAdge50wzsAMqdM2_BVhntsP_OUYxaDMkPtRvewg";

    //The same as the Javascript example
    char SDK_PUBKEY[] = "CACmWnYy7M5dqH7shqrj4jERfhhCfzoU5uDycAof1o8JyHu_F47b0aAB9KhKsIVKv90"
        "nbuea7wGuWsc0pxlrR5kKOnqMEcIQrLysFupSleqwilIgp5MUBvkPTdsn22Qc9Qldwm"
        "p_cbBNVfTrUVFSifv0QjDnbl7t9sLF5GgFMfYhWqMxAr3D3072cQF9eTbDLCbPD7RrC"
        "vUiTdqI1bT79e_187YSzCdjeVq_tZb5YnhLPHlgNQffmFJj41itSwpqrEYN8e5kIvsE"
        "INpHiLtXIIBBnld6NZu55U37sHeYkn5PB6cMi3ZEm90uIB7MT5CyHYLaEbJ9RkzJNRc"
        "xJAC2w4CnABEBAAE";

    //From the Javascript example, disabling random padding (padding with 0)
    char CRYPT_GORT_SAYS[]  = "AQB4PLTVCTdrPFXPWWCWZA3LdkjsIQgr7Ug8WBqFQlGqDR0YX0heatGVudAEb3TBOwvuoYsbOwVLOya22pqDJP6E-RUYDxbYC0dA02K7TSs97A9ZqnxnL6jvjW95X3BuR8YjStQJyy-a3FyAhrjyT9TnLOfKuUwIMLHf1eZB8H4JlAJ8VEQq9-SlusubiQZGZpYMeu2SBFJN-HI-93PEw2U3k-K6h7YYdhM-kIJ4-d2LuPWfyvuyjhs5fncgDgqPGZhq_4XOmV5Xh76aoqx8SBrPsotFvxE_CxOydivXhBMRaN6b6iL7MhuQXXDbOjvVis9uV2HnWraCxHbFwmUxoD6K";
    char CRYPT_KEYS_BYTES[] = "AQB1FZOZJiEviXTXeBEOjyM6F9odENY6q4wzt73X0vVCbGBZyubKzHrNzHLaNkwGubd1RQ6wTuH3ypbK5wdM3QsyTcLq6DMv7O3JsH2R3MynRLuPGzHiNmZq2VkAMvELOo-XBeUknxrAstHZhWNQJImH4DBtnY57Mid1o-BTz7xKvRIUQvsj217CqE4CnVV6lxaloq6jvlenWATzCdEa1Q6Y8XN7hftn4Hl5ZrnAltIblBI0_fq2bkhqzZolpURbhypAg0oTFpnmj82QEBy4vwwdCOaQ8_lQjqQhsd3ah4O9gSkpYa6YoAtV9eBu338skJbhjprUVq04qi62Er_iichx";


    //Initialize values
    byte enckey[sizeof(BASE64_ENCKEY)];
    Base64::atob(BASE64_ENCKEY, enckey, sizeof(enckey));

    byte iv[sizeof(BASE64_IV)];
    int ivLen = Base64::atob(BASE64_IV, iv, sizeof(iv));

    byte hmacKey[sizeof(BASE64_HMACKEY)];
    int hmacKeyLen = Base64::atob(BASE64_HMACKEY, hmacKey, sizeof(hmacKey));

    char keys[sizeof(KEYS)];
    int keysLen = Base64::atob(KEYS, (byte *)keys, sizeof(keys));

    byte pubkdata[sizeof(SDK_PUBKEY)];
    int pubkdatalen = Base64::atob(SDK_PUBKEY, (byte *)pubkdata, sizeof(pubkdata));
    //////////////////////


    //Test AES-CBC encryption
    string result;
    string input = CONTENT;
    SymmCipher sym(enckey);
    sym.cbc_encrypt_pkcs_padding(&input, iv, &result);

    //Check result
    char* base64Result = new char[result.size()*4/3+4];
    Base64::btoa((const byte *)result.data(), result.size(), base64Result);
    ASSERT_STREQ(base64Result, CIPHER_BYTES);
    //////////////////////


    //Test AES-CBC decryption
    string plain;
    sym.cbc_decrypt_pkcs_padding(&result, iv, &plain);

    //Check result
    ASSERT_STREQ(input.c_str(), plain.c_str());
    //////////////////////


    //Test HMAC-SHA256
    string toAuth;
    toAuth.assign((char *)iv, ivLen);
    toAuth.append(result);

    string mac;
    mac.resize(32);
    HMACSHA256 hmacProcessor(hmacKey, hmacKeyLen);
    hmacProcessor.add((byte *)toAuth.data(), toAuth.size());
    hmacProcessor.get((byte *)mac.data());

    //Check result
    char* macResult = new char[mac.size()*4/3+4];
    Base64::btoa((const byte *)mac.data(), mac.size(), macResult);
    ASSERT_STREQ(macResult, HMAC);
    //////////////////////


    //Test PayCrypter:encryptPayload()
    PrnGen rng;
    string payCrypterResult;
    PayCrypter payCrypter(rng);
    payCrypter.setKeys(enckey, hmacKey, iv);
    ASSERT_TRUE(payCrypter.encryptPayload(&input, &payCrypterResult));

    //Prepare the expected result
    string CRYPT_PAYLOAD = mac;
    CRYPT_PAYLOAD.append((char *)iv, ivLen);
    CRYPT_PAYLOAD.append(result);
    char* expectedPayload = new char[CRYPT_PAYLOAD.size()*4/3+4];
    Base64::btoa((const byte *)CRYPT_PAYLOAD.data(), CRYPT_PAYLOAD.size(), expectedPayload);

    //Check result
    char* encryptPayloadResult = new char[payCrypterResult.size()*4/3+4];
    Base64::btoa((const byte *)payCrypterResult.data(), payCrypterResult.size(), encryptPayloadResult);
    ASSERT_STREQ(expectedPayload, encryptPayloadResult);
    //////////////////////


    //Test PayCrypter:rsaEncryptKeys(), disabling random padding to get known results
    string message = "Klaatu barada nikto.";
    string rsaRes;
    ASSERT_TRUE(payCrypter.rsaEncryptKeys(&message, pubkdata, pubkdatalen, &rsaRes, false));

    //Check result
    char* rsaResult = new char[rsaRes.size()*4/3+4];
    Base64::btoa((const byte *)rsaRes.data(), rsaRes.size(), rsaResult);
    ASSERT_STREQ(rsaResult, CRYPT_GORT_SAYS);
    //////////////////////

    //Test PayCrypter:rsaEncryptKeys() with a binary input, disabling random padding to get known results
    string cryptKeysBytesBin;
    message.assign(keys, keysLen);
    ASSERT_TRUE(payCrypter.rsaEncryptKeys(&message, pubkdata, pubkdatalen, &cryptKeysBytesBin, false));

    //Check result
    char* cryptKeysBytes = new char[cryptKeysBytesBin.size()*4/3+4];
    Base64::btoa((const byte *)cryptKeysBytesBin.data(), cryptKeysBytesBin.size(), cryptKeysBytes);
    ASSERT_STREQ(cryptKeysBytes, CRYPT_KEYS_BYTES);
    //////////////////////


    //Test PayCrypter:hybridEncrypt()
    string finalResult;
    string contentString = CONTENT;
    ASSERT_TRUE(payCrypter.hybridEncrypt(&contentString, pubkdata, pubkdatalen, &finalResult, false));

    //Prepare the expected result
    string expectedResult = cryptKeysBytesBin;
    expectedResult.append(CRYPT_PAYLOAD);
    char* expectedBase64Result = new char[expectedResult.size()*4/3+4];
    Base64::btoa((const byte *)expectedResult.data(), expectedResult.size(), expectedBase64Result);

    //Check result
    char* finalCheck = new char[finalResult.size()*4/3+4];
    Base64::btoa((const byte *)finalResult.data(), finalResult.size(), finalCheck);
    ASSERT_STREQ(finalCheck, expectedBase64Result);
    //////////////////////

    
    delete[] finalCheck;
    delete[] expectedBase64Result;
    delete[] cryptKeysBytes;
    delete[] rsaResult;
    delete[] base64Result;
    delete[] macResult;
    delete[] expectedPayload;
    delete[] encryptPayloadResult;

}
