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

#include <gtest/gtest.h>
#include <gtest/internal/gtest-internal.h>
#include <mega/crypto/sodium.h>
#include <megaapi.h>
#include <sys/wait.h>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <ratio>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "../include/mega/secureBuffer.h"
#include "../include/mega/sharedbuffer.h"
#include "../include/mega/userAttributes.h"
#include "../include/megaapi.h"
#include "../include/sodium.h"
#include "../include/mega/base64.h"

//#ifdef USE_SODIUM
#include <sodium.h>
//#endif

using namespace mega;
using ::testing::InitGoogleTest;
using ::testing::Test;
using ::testing::TestCase;
using ::testing::TestInfo;
using ::testing::TestPartResult;
using ::testing::UnitTest;

bool debug;

//TEST(JSON, storeobject) {
//    std::string in_str("Test");
//    JSON j;
//    j.storeobject(&in_str);
//}
//
//// Test 64-bit int serialization/unserialization
//TEST(Serialize64, serialize) {
//    uint64_t in = 0xDEADBEEF;
//    uint64_t out;
//    byte buf[sizeof in];
//
//    Serialize64::serialize(buf, in);
//    ASSERT_GT(Serialize64::unserialize(buf, sizeof buf, &out), 0);
//    ASSERT_EQ(in, out);
//}
//#ifdef USE_SODIUM
//class SodiumTest : public testing::Test {
//  virtual void setUp() {}
//
//  virtual void tearDown() {}
//};
//
//TEST_F(SodiumTest, testSodium) {
//    EdDSA dsa;
//    SecureBuffer kBuff = dsa.genKeySeed();
//    ASSERT_TRUE(kBuff.get() != nullptr);
//    ASSERT_EQ(crypto_sign_ed25519_SEEDBYTES, kBuff.size());
//    std::pair<SecureBuffer, SecureBuffer> kPair =
//            dsa.getKeyPair();
//    ASSERT_TRUE(kPair.first.get() != nullptr);
//    ASSERT_TRUE(kPair.second.get() != nullptr);
//    ASSERT_EQ(crypto_sign_ed25519_PUBLICKEYBYTES, kPair.first.size());
//    ASSERT_EQ(crypto_sign_ed25519_SECRETKEYBYTES, kPair.second.size());
//    std::string testData("The bla, the waffle, and the gibber.");
//    SecureBuffer sig = dsa.sign((unsigned char*)testData.c_str(), testData.length());
//    ASSERT_TRUE(sig.get() != nullptr);
//    ASSERT_EQ(crypto_sign_ed25519_BYTES + testData.length(), sig.size());
//    SecureBuffer unsigMsg = dsa.verify(sig.get(), sig.size(), kPair.first);
//    ASSERT_TRUE(unsigMsg.get() != nullptr);
//    ASSERT_EQ(testData.length(), unsigMsg.size());
//
//}

//#endif

class UserAttributesTest : public testing::Test {

public:

    virtual void setUp() {

    }

    virtual void tearDown() {

    }
};


class ApiTest : public testing::Test {
public:
    std::string apiKeyOne = "sdfsdfsdf";
    std::string loginNameOne = "michaelholmwood@mega.co.nz";
    std::string passWordOne = "Fractal*hidden*stuff!";
    std::string apiKeyTwo = "sdfsdfsdf";
    std::string loginNameTwo = "mh@mega.co.nz";
    std::string passWordTwo = "Fractal*hidden*stuff!";
    std::string loginNameThree = "mholmwood@gmail.com";
    std::string passWordThree = "Fractal*hidden*stuff!";

    virtual void setUp() {

    }

    virtual void tearDown() {

    }
};

class TestClient : public MegaRequestListener {
public:
    std::string loginName;
    std::string passWord;

    bool wait = true;
    std::string email;
    const char *rsaBase64;
    bool success = false;
    std::map<std::string, std::pair<unsigned char*, unsigned int>> *valMap;
    MegaApi *api;

    TestClient(std::string loginName, std::string passWord) :
        loginName(loginName), passWord(passWord),
                valMap(nullptr) {
        api = new MegaApi("sdfsdfsdf", (const char*)NULL, "sdk_test");
    }

    virtual ~TestClient() {
        delete api;
    }

    virtual void onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e) {
        std::cout << "onRequestFinish called" << std::endl;
        const char *eM;
        switch(request->getType()) {
        case MegaRequest::TYPE_LOGIN :
            std::cout << "Type login" << std::endl;
            success = e->getErrorCode() == MegaError::API_OK;
            wait = false;
            break;

        case MegaRequest::TYPE_GET_USER_DATA :
            std::cout << "Type get user data" << std::endl;
            eM = request->getText();
            std::cout << "em true = " << ((eM) ? "true" : "false") << std::endl;
            if(e->getErrorCode() == MegaError::API_OK && eM) {
                std::cout << "Process message" << std::endl;
                success = true;
                email = std::string(eM);
                rsaBase64 = request->getPassword();
            }
            else {
                std::cerr << "Error logging in: " << std::endl;
                std::cerr << e->getErrorString() << std::endl;
            }
            std::cout << "request finished" << std::endl;
            wait = false;
            break;
        case MegaRequest::TYPE_GET_USER_ATTRIBUTE :
            std::cout << "Type = get user attribute" << std::endl;
            if(e->getErrorCode() == MegaError::API_OK) {
                success = true;
                valMap = request->getUserAttributeMap();
            }
            else {
                success = false;
            }
            wait = false;
            break;
        case MegaRequest::TYPE_GET_STATIC_PUB_KEY :
            std::cout << "Type = get signing keys" << std::endl;
            if(e->getErrorCode() == MegaError::API_OK) {
                success = true;
                valMap = request->getUserAttributeMap();
            }
            else {
                success = false;
            }
            wait = false;
            break;
        case MegaRequest::TYPE_GET_SIGNING_KEYS :
            std::cout << "Type = get signing keys" << std::endl;

            if(e->getErrorCode() == MegaError::API_OK) {
                success = true;
                valMap = request->getUserAttributeMap();
            }
            else {
                success = false;
            }
            wait = false;
            break;
        case MegaRequest::TYPE_SET_USER_ATTRIBUTE :
            std::cout << "Type = set user attribute" << std::endl;

            if(e->getErrorCode() == MegaError::API_OK) {
                success = true;
            }
            else {
                success = false;
            }
            wait = false;
            break;
        case MegaRequest::TYPE_FETCH_NODES :
            std::cout << "Type = fetch nodes" << std::endl;

            if(e->getErrorCode() == MegaError::API_OK) {
                success = true;
            }
            else {
                success = false;
            }
            wait = false;
            break;
        case MegaRequest::TYPE_VERIFY_RSA_SIG:
        {
            std::cout << "Type = verify rsa sig" << std::endl;

            if(e->getErrorCode() == MegaError::API_OK) {
                success = true;
            }
            else {
                success = false;
            }
            wait = false;
            break;
        }
        case MegaRequest::TYPE_VERIFY_KEY_FINGERPRINT:
        {
            std::cout << "Type = verify key fp" << std::endl;

            if(e->getErrorCode() == MegaError::API_OK) {
                success = true;
            }
            else {
                success = false;
            }
            wait = false;
            break;
        }
        default:
            wait = false;
            std::cout << "other type: " << request->getType() << std::endl;
        }

        std::cout << "exit" << std::endl;
    }

    bool login() {
        wait = true;
        std::cout << "logging in" << std::endl;

        api->login(loginName.c_str(), passWord.c_str(), this);
        std::cout << "Waiting" << std::endl;

        while(wait) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "exit wait" << std::endl;
        if(!success) {
            std::cout << "login failed" << std::endl;
            return false;
        }
        std::cout << "login success" << std::endl;

        wait = true;
        success = false;
        api->getUserData(this);
        while(wait) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if(!success) {
            std::cerr << "login failed" << std::endl;
            return false;
        }

        std::cout << "rsa key = " << rsaBase64 << std::endl;
        byte data[4096];
        int l = Base64::atob(rsaBase64, data, sizeof(data));
        std::cout << "size of bytes = " << l << std::endl;

        wait = true;
        success = false;
        api->fetchNodes(this);
        while(wait) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if(!success) {
            std::cout << "fetch nodes failed" << std::endl;
            return false;
        }

        return success;
    }
};

TEST_F(ApiTest, testSetup) {
    TestClient tcOne(loginNameOne, passWordOne);
    TestClient tcTwo(loginNameThree, passWordThree);
    TestClient tcThree(loginNameTwo, passWordTwo);
//    TestClient tcFour(loginNameThree, passWordThree);
//    TestClient tcFive(loginNameThree, passWordThree);
//    TestClient tcSix(loginNameThree, passWordThree);
//    TestClient tcSeven(loginNameThree, passWordThree);

    std::map<std::string, std::pair<unsigned char*, unsigned int>> resetMap;
    resetMap.insert({"authRSA", {(unsigned char*)"", 0}});

    std::map<std::string, std::pair<unsigned char*, unsigned int>> resetMapE;
    resetMapE.insert({"authring", {(unsigned char*)"", 0}});

    if(tcOne.login()) {
        tcOne.wait = true;
        tcOne.success = false;
        std::map<std::string, std::pair<unsigned char*, unsigned int>> map;
        std::string testValueStr("And so it was, it, and always shall be, the ultimate"
                        " test of one's will is pitted against those who would cause you harm. it "
                        "is not the harm that inflicts the most damage - it is the intent to cause "
                        "harm that ultimately destroys the soul.");

        map.insert({"uName", {(unsigned char*)testValueStr.c_str(), testValueStr.size()}});


        ValueMap vMap = UserAttributes::mapToValueMap(&map);
        auto j = vMap->find("uName");
        ASSERT_TRUE(j != vMap->end());
        std::string val((char*)j->second.get(), j->second.size);
        ASSERT_STREQ(testValueStr.c_str(), val.c_str());
        std::map<std::string, std::pair<unsigned char*, unsigned int>> *retMap
                = UserAttributes::valueMapToMap(vMap);
        auto k = retMap->find("uName");
        ASSERT_TRUE(k != retMap->end());
        std::string retVal((char*)k->second.first, k->second.second);
        ASSERT_STREQ(testValueStr.c_str(), retVal.c_str());
        std::cout << "RETVAL = " << retVal << std::endl;

        tcOne.api->putGenericUserAttribute("michaelholmwood@mega.co.nz", "Names", &map, 0, &tcOne);
        while(tcOne.wait) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ASSERT_TRUE(tcOne.success);

        tcOne.wait = true;
        tcOne.success = false;
        tcOne.api->getGenericUserAttribute("michaelholmwood@mega.co.nz", "Names", &tcOne);
        while(tcOne.wait) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ASSERT_TRUE(tcOne.success);
        auto l = tcOne.valMap->find("uName");
        ASSERT_TRUE(l != tcOne.valMap->end());
        retVal.assign((char*)l->second.first, l->second.second);
        ASSERT_STREQ(testValueStr.c_str(), retVal.c_str());

        tcOne.wait = true;
        tcOne.success = false;
        tcOne.api->putGenericUserAttribute("michaelholmwood@mega.co.nz", "Names", &map, 1, &tcOne);
        while(tcOne.wait) {
           std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ASSERT_TRUE(tcOne.success);

        ////////// Uncomment to reset keyrings ///////////////
//        tcOne.wait = true;
//        tcOne.success = false;
//
//        tcOne.api->putGenericUserAttribute("michaelholmwood@mega.co.nz",
//                "authRSA", &resetMap, 1, &tcOne);
//        while(tcOne.wait) {
//            std::this_thread::sleep_for(std::chrono::milliseconds(100));
//        }
//        ASSERT_TRUE(tcOne.success);
//
//        tcOne.wait = true;
//        tcOne.success = false;
//
//        tcOne.api->putGenericUserAttribute("michaelholmwood@mega.co.nz",
//                "authring", &resetMapE, 1, &tcOne);
//        while(tcOne.wait) {
//            std::this_thread::sleep_for(std::chrono::milliseconds(100));
//        }
//        ASSERT_TRUE(tcOne.success);
        /////////////////////////////////////////////////////

        std::cout << "BIG_TEST" << std::endl;
        tcOne.wait = true;
        tcOne.success = false;
//        tcTwo.wait = true;
        tcTwo.success = false;
//        tcThree.success = false;
//        tcFour.success = false;
//        tcFive.success = false;

        tcOne.api->getGenericUserAttribute("michaelholmwood@mega.co.nz", "puEd255", &tcOne);
//        tcOne.api->getGenericUserAttribute("michaelholmwood@mega.co.nz", "puEd255", &tcTwo);
//        tcOne.api->getGenericUserAttribute("michaelholmwood@mega.co.nz", "puEd255", &tcThree);
//        tcOne.api->getGenericUserAttribute("mholmwood@gmail.com", "puEd255", &tcFour);
//        tcOne.api->getGenericUserAttribute("mh@mega.co.nz", "puEd255", &tcFive);

        while(tcOne.wait) {
           std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        ASSERT_TRUE(tcOne.success);
        auto m = tcOne.valMap->find("puEd255");
        ASSERT_TRUE(m != tcOne.valMap->end());
        retVal.assign((char*)m->second.first, m->second.second);
        //ASSERT_STREQ(testValueStr.c_str(), retVal.c_str());

        for(int x = 0; x < retVal.size(); x++) {
            std::cout << (int)retVal.c_str()[x] << ",";
        }
        std::cout << std::endl;


//        ASSERT_TRUE(tcTwo.success);
//        auto m1 = tcTwo.valMap->find("puEd255");
//        ASSERT_TRUE(m1 != tcTwo.valMap->end());
//        retVal.assign((char*)m1->second.first, m1->second.second);
//
//        for(int x = 0; x < retVal.size(); x++) {
//           std::cout << (int)retVal.c_str()[x] << ",";
//        }
//        std::cout << std::endl;
//
//        //ASSERT_STREQ(testValueStr.c_str(), retVal.c_str());
//        ASSERT_TRUE(tcThree.success);
//        auto m3 = tcThree.valMap->find("puEd255");
//        ASSERT_TRUE(m3 != tcThree.valMap->end());
//        retVal.assign((char*)m3->second.first, m3->second.second);
//
//        for(int x = 0; x < retVal.size(); x++) {
//            std::cout << (int)retVal.c_str()[x] << ",";
//        }
//        std::cout << std::endl;
//
//        //ASSERT_STREQ(testValueStr.c_str(), retVal.c_str());
//        ASSERT_TRUE(tcFour.success);
//        auto m4 = tcFour.valMap->find("puEd255");
//        ASSERT_TRUE(m4 != tcFour.valMap->end());
//        retVal.assign((char*)m4->second.first, m4->second.second);
//
//        for(int x = 0; x < retVal.size(); x++) {
//           std::cout << (int)retVal.c_str()[x] << ",";
//        }
//        std::cout << std::endl;
//
//
//        ASSERT_TRUE(tcFive.success);
//        auto m5 = tcFive.valMap->find("puEd255");
//        ASSERT_TRUE(m5 != tcFive.valMap->end());
//        retVal.assign((char*)m5->second.first, m5->second.second);
//
//        for(int x = 0; x < retVal.size(); x++) {
//           std::cout << (int)retVal.c_str()[x] << ",";
//        }
//        std::cout << std::endl;

        tcOne.wait = true;
        tcOne.success = false;
        tcOne.api->getOwnStaticKeys(&tcOne);
        while(tcOne.wait) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ASSERT_TRUE(tcOne.success);
        //ASSERT_EQ(2, tcOne.valMap->size());
        ASSERT_TRUE(tcOne.valMap->find("prEd255") != tcOne.valMap->end());
        ASSERT_TRUE(tcOne.valMap->find("puEd255") != tcOne.valMap->end());
    }
    else {
        std::cout << "login tcOne failed" << std::endl;
        exit(-1);
    }
    if(tcThree.login()) {
        tcThree.wait = true;
        tcThree.success = false;
        tcThree.api->getOwnStaticKeys(&tcThree);
        while(tcThree.wait) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ASSERT_TRUE(tcThree.success);

        //////// Uncomment to reset keyrings ///////////////
//        tcThree.wait = true;
//        tcThree.success = false;
//
//        tcThree.api->putGenericUserAttribute("mh@mega.co.nz",
//                "authRSA", &resetMap, 1, &tcThree);
//        while(tcThree.wait) {
//            std::this_thread::sleep_for(std::chrono::milliseconds(100));
//        }
//        ASSERT_TRUE(tcThree.success);
//
//        tcThree.wait = true;
//        tcThree.success = false;
//
//        tcThree.api->putGenericUserAttribute("mh@mega.co.nz",
//                "authring", &resetMapE, 1, &tcThree);
//        while(tcThree.wait) {
//            std::this_thread::sleep_for(std::chrono::milliseconds(100));
//        }
//        ASSERT_TRUE(tcThree.success);
        ///////////////////////////////////////////////////
    }
    else {
        std::cout << "login tcThree failed" << std::endl;
        exit(-1);
    }

    if(tcTwo.login()) {
        tcTwo.wait = true;
        tcTwo.success = false;
        tcTwo.api->getGenericUserAttribute("michaelholmwood@mega.co.nz", "puEd255", &tcTwo);
        while(tcTwo.wait) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ASSERT_TRUE(tcTwo.success);

        tcTwo.wait = true;
        tcTwo.success = false;
        tcTwo.api->getGenericUserAttribute("michaelholmwood@mega.co.nz", "sgPubk", &tcTwo);
        while(tcTwo.wait) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ASSERT_TRUE(tcTwo.success);

        ////////// Uncomment to reset keyrings ///////////////
//        tcTwo.wait = true;
//        tcTwo.success = false;
//
//        tcTwo.api->putGenericUserAttribute("michaelholmwood@mega.co.nz",
//                "authRSA", &resetMap, 1, &tcTwo);
//        while(tcOne.wait) {
//            std::this_thread::sleep_for(std::chrono::milliseconds(100));
//        }
//        ASSERT_TRUE(tcOne.success);
//
//        tcTwo.wait = true;
//        tcTwo.success = false;
//
//        tcTwo.api->putGenericUserAttribute("michaelholmwood@mega.co.nz",
//                "authring", &resetMapE, 1, &tcTwo);
//        while(tcOne.wait) {
//            std::this_thread::sleep_for(std::chrono::milliseconds(100));
//        }
//        ASSERT_TRUE(tcOne.success);
        /////////////////////////////////////////////////////

        std::cout << "****Getting other user data" << std::endl;
        tcTwo.wait = true;
        tcTwo.success = false;
        tcTwo.api->getUserData("michaelholmwood@mega.co.nz", &tcTwo);

        while(tcTwo.wait) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "****Getting other user data" << std::endl;
        tcTwo.wait = true;
        tcTwo.success = false;
        tcTwo.api->getUserData("mh@mega.co.nz", &tcTwo);

        while(tcTwo.wait) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ASSERT_TRUE(tcTwo.success);

        std::cout << "****Calling getPublicStaticKey" << std::endl;
        tcTwo.wait = true;
        tcTwo.success = false;
        tcTwo.api->getPublicStaticKey("michaelholmwood@mega.co.nz", &tcTwo);

        while(tcTwo.wait) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        ASSERT_TRUE(tcTwo.success);

//        unsigned char rsa[] = {
//                238, 48, 151, 211, 94, 237, 170,
//                197, 5, 225, 170, 242, 150, 35,
//                255, 34, 248, 228, 237, 11, 0,
//                149, 52, 253, 91, 31, 5, 112, 141,
//                249, 250, 184, 73, 230, 71, 231,
//                70, 214, 91, 180, 187, 31, 177,
//                17, 118, 65, 6, 236, 14, 184, 70,
//                159, 99, 55, 146, 56, 192, 143,
//                28, 138, 18, 106, 123, 37, 183,
//                150, 1, 111, 115, 182, 59, 39,
//                25, 111, 124, 177, 35, 102, 86,
//                99, 138, 212, 126, 221, 188, 225,
//                32, 228, 188, 158, 85, 153, 220,
//                253, 43, 225, 19, 126, 82, 243,
//                18, 46, 137, 59, 5, 107, 133,
//                237, 99, 115, 30, 77, 217, 115,
//                32, 254, 144, 211, 139, 26,
//                167, 192, 62, 229, 50, 153, 3,
//                127, 252, 228, 191, 86, 59,
//                184, 76, 176, 165, 231, 48,
//                187, 35, 189, 184, 126, 191,
//                211, 132, 239, 92, 158, 242,
//                223, 2, 204, 183, 146, 62, 110,
//                205, 84, 121, 87, 178, 245, 37,
//                149, 181, 42, 42, 137, 109, 221,
//                197, 5, 105, 37, 121, 174, 240,
//                104, 206, 81, 172, 210, 4, 71,
//                58, 123, 119, 4, 245, 57, 10,
//                69, 24, 241, 14, 168, 220, 184,
//                105, 255, 253, 195, 60, 37, 163,
//                121, 197, 68, 212, 53, 114, 45,
//                206, 197, 88, 239, 0, 53, 79,
//                213, 58, 90, 5, 69, 191, 138, 61,
//                118, 191, 248, 240, 51, 161,
//                108, 53, 134, 68, 180, 149,
//                40, 49, 145, 245, 187, 87, 142,
//                212, 48, 238, 232, 74, 231, 240, 183};
//
//        unsigned char rsafp[] = {
//                48, 72, 122, 58, 31, 218, 139,
//                69, 52, 102, 155, 247, 32, 41,
//                58, 55, 215, 0, 236, 8,
//        };
//
//        unsigned char edfp[] = {
//                50, 22, 79, 220, 117, 185, 97,
//                57, 63, 191, 192, 71, 119, 164,
//                190, 130, 177, 144, 195, 25,
//        };
//
//        tcTwo.wait = true;
//        tcTwo.success = false;
//        std::cout << "sizeof rsa = " << sizeof(rsa) << std::endl;
//        std::cout << "calling verifyRSA" << std::endl;
//        tcTwo.api->verifyRSAFingerPrint("michaelholmwood@mega.co.nz",
//                rsa, sizeof(rsa), &tcTwo);
//        while(tcTwo.wait) {
//            std::this_thread::sleep_for(std::chrono::milliseconds(100));
//        }
//
//        ASSERT_TRUE(tcTwo.success);
//
//        tcTwo.wait = true;
//        tcTwo.success = false;
//        tcTwo.api->verifyKeyFingerPrint("michaelholmwood@mega.co.nz",
//                rsafp, sizeof(rsafp), 1, &tcTwo);
//        while(tcTwo.wait) {
//            std::this_thread::sleep_for(std::chrono::milliseconds(100));
//        }
//        ASSERT_TRUE(tcTwo.success);
//
//        tcTwo.wait = true;
//        tcTwo.success = false;
//        tcTwo.api->verifyKeyFingerPrint("michaelholmwood@mega.co.nz",
//                edfp, sizeof(edfp), 0, &tcTwo);
//        while(tcTwo.wait) {
//            std::this_thread::sleep_for(std::chrono::milliseconds(100));
//        }
//        ASSERT_TRUE(tcTwo.success);
//
//        tcTwo.wait = true;
//        tcTwo.success = false;
//        tcTwo.api->getGenericUserAttribute("mholmwood@gmail.co.nz", "authRSA", &tcTwo);
//        while(tcTwo.wait) {
//            std::this_thread::sleep_for(std::chrono::milliseconds(100));
//        }
//        ASSERT_TRUE(tcTwo.success);
//
//
//
//        int count = 0;
//
//        auto i = tcTwo.valMap->find((rsa) ? std::string("authRSA") :
//                std::string("authring"));
//        ASSERT_TRUE(i != tcTwo.valMap->end());
//
//        SharedBuffer buff(i->second.first, i->second.second);
//
//        ASSERT_TRUE((buff.size % (sizeof(handle) + 20 + 1)) == 0);
//
//        std::map<handle, FingerPrintRecord> rMap;
//        while(count < buff.size) {
//            SharedBuffer fp(20);
//            FingerPrintRecord record(fp);
//            handle h = 0;
//            memcpy(&h, buff.get(), sizeof(handle));
//            memcpy(record.fingerPrint.get(), buff.get() +
//                    (count += sizeof(handle)), 20);
//            memcpy(&record.methodConfidence, buff.get() + (count += 20), 1);
//            rMap.insert({ h, record });
//            count++;
//        }
//
//        handle uHandle = rMap.begin()->first;
//        FingerPrintRecord rec = rMap.begin()->second;
//        ASSERT_EQ(12273408314354856008, uHandle);
//        ASSERT_EQ(20, rec.fingerPrint.size);
//        for(int x = 0; x < rec.fingerPrint.size; x++) {
//            ASSERT_EQ(rsafp[x], rec.fingerPrint[x]);
//            std::cout << "rsafp[" << x << "] = " <<
//                    (int)rsafp[x] <<
//                    " fp[" << x << "] = " << (int)rec.fingerPrint[x]
//                     << std::endl;
//        }
//
//
//        tcTwo.wait = true;
//        tcTwo.success = false;
//        tcTwo.api->getGenericUserAttribute("mh@mega.co.nz", "authring", &tcTwo);
//        while(tcTwo.wait) {
//            std::this_thread::sleep_for(std::chrono::milliseconds(100));
//        }
//        ASSERT_TRUE(tcTwo.success);
//
//        count = 0;
//
//        auto w = tcTwo.valMap->find(std::string("authring"));
//        ASSERT_TRUE(w != tcTwo.valMap->end());
//
//        SharedBuffer buffEd(w->second.first, w->second.second);
//
//        ASSERT_TRUE((buffEd.size % (sizeof(handle) + 20 + 1)) == 0);
//
//        std::map<handle, FingerPrintRecord> rMapEd;
//        while(count < buffEd.size) {
//            SharedBuffer fp(20);
//            FingerPrintRecord record(fp);
//            handle h = 0;
//            memcpy(&h, buffEd.get(), sizeof(handle));
//            memcpy(record.fingerPrint.get(), buffEd.get() +
//                    (count += sizeof(handle)), 20);
//            memcpy(&record.methodConfidence, buffEd.get() + (count += 20), 1);
//            rMapEd.insert({ h, record });
//            count++;
//        }
//
//        handle uHandleEd = rMapEd.begin()->first;
//        FingerPrintRecord recEd = rMapEd.begin()->second;
//        ASSERT_EQ(12273408314354856008, uHandleEd);
//        ASSERT_EQ(20, recEd.fingerPrint.size);
//        for(int x = 0; x < recEd.fingerPrint.size; x++) {
//            ASSERT_EQ(edfp[x], recEd.fingerPrint[x]);
//            std::cout << "edfp[" << x << "] = " <<
//                    (int)edfp[x] <<
//                    " recEd.fp[" << x << "] = " << (int)recEd.fingerPrint[x]
//                     << std::endl;
//        }
//    }
//    else {
//        std::cout << "login tcTwo failed" << std::endl;
    }
}

TEST_F(UserAttributesTest, test_decode_correct_data) {
    mega::UserAttributes store;
    std::string testTag("testtagone");
    std::string testData("0123456789");
    unsigned char data[] = {
            't', 'e', 's', 't', 't', 'a', 'g', 'o', 'n', 'e', '\0',
            (unsigned char)(testData.length() >> 8), (unsigned char)testData.length(),
            '0', '1', '2', '3', '4', '5',
            '6', '7', '8', '9'
    };

    mega::ValueMap map;
    mega::SharedBuffer dataBuffer(data, sizeof(data));
    try {
        map = store.tlvToValueMap(dataBuffer);
    }
    catch(std::runtime_error &exception) {
        std::cerr << "exception thrown: "
                  << exception.what()
                  << std::endl;
        ASSERT_FALSE(true);
    }

    ASSERT_EQ((unsigned int)1, map->size());
    auto lv = map->find(testTag);
    ASSERT_FALSE(lv == map->end());
    SharedBuffer testLv = lv->second;

    ASSERT_EQ(testData.length(), (unsigned short)testLv.size);

    for(unsigned int x = 0; x < testData.length(); x++) {
        ASSERT_EQ(testData.c_str()[x], testLv.get()[x]);
    }

}

TEST_F(UserAttributesTest, test_decode_fail_missing_null_character) {
    UserAttributes store;
    std::string testTag("testtagone");
    std::string testData("0123456789");
    unsigned char data[] = {
       't', 'e', 's', 't', 't', 'a', 'g', 'o', 'n', 'e',
       (unsigned char)(testData.length() >> 8), (unsigned char)testData.length(),
       '0', '1', '2', '3', '4', '5',
       '6', '7', '8', '9'
    };

    ValueMap map;
    SharedBuffer dataBuffer(data, sizeof(data));
    try {
        map = store.tlvToValueMap(dataBuffer);
    }
    catch(std::runtime_error &exception) {
        ASSERT_TRUE(true);
        ASSERT_STREQ(INVALID_DATA_LENGTH, exception.what());
    }
}

TEST_F(UserAttributesTest, test_decode_fail_missing_length) {
    UserAttributes store;
    std::string testTag("testtagone");
    std::string testData("0123456789");
    unsigned char data[] = {
      't', 'e', 's', 't', 't', 'a', 'g', 'o', 'n', 'e', '\0',
      '0', '1', '2', '3', '4', '5',
      '6', '7', '8', '9'
    };

    ValueMap map;
    SharedBuffer dataBuffer(data, sizeof(data));
    try {
       map = store.tlvToValueMap(dataBuffer);
    }
    catch(std::runtime_error &exception) {
       ASSERT_TRUE(true);
       ASSERT_STREQ(INVALID_DATA_LENGTH, exception.what());
    }
}

TEST_F(UserAttributesTest, test_decode_fail_missing_data) {
    UserAttributes store;
    std::string testTag("testtagone");
    std::string testData("0123456789");
    unsigned char data[] = {
       't', 'e', 's', 't', 't', 'a', 'g', 'o', 'n', 'e', '\0',
       (unsigned char)(testData.length() >> 8), (unsigned char)testData.length(),
       '0', '1', '2', '3', '4', '5'
    };

    ValueMap map;
    SharedBuffer dataBuffer(data, sizeof(data));
    try {
        map = store.tlvToValueMap(dataBuffer);
    }
    catch(std::runtime_error &exception) {
        ASSERT_TRUE(true);
        ASSERT_STREQ(INVALID_DATA_LENGTH, exception.what());
    }
}

TEST_F(UserAttributesTest, test_decode_larger_file) {
    UserAttributes store;
    std::string testTag("testtagone");
    std::string testData(
                             "0123456789"
                             "0123456789"
                             "0123456789"
                             "0123456789"
                             "0123456789"
                             "0123456789"
                             "0123456789"
                             "0123456789"
                             "0123456789"
                             "0123456789"
                             "0123456789"
                             "0123456789"
                             "0123456789"
                             "0123456789"
                             "0123456789"
                             "0123456789"
                             "0123456789"
                             "0123456789"
                             "0123456789"
                             "0123456789"
                             "0123456789"
                             "0123456789"
                             "0123456789"
                             "0123456789"
                             "0123456789"
                             "0123456789");


    unsigned char data[] = {
            't', 'e', 's', 't', 't', 'a', 'g', 'o', 'n', 'e', '\0',
            (unsigned char)(testData.length() >> 8), (unsigned char)testData.length(),
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
    };

    ValueMap map;
    SharedBuffer dataBuffer(data, sizeof(data));
    try {
       map = store.tlvToValueMap(dataBuffer);
    }
    catch(std::runtime_error &exception) {
       std::cerr << "exception thrown: "
                 << exception.what()
                 << std::endl;
       ASSERT_FALSE(true);
    }

    ASSERT_EQ((unsigned int)1, map->size());
    auto lv = map->find(testTag);
    ASSERT_FALSE(lv == map->end());
    SharedBuffer testLv = lv->second;

    ASSERT_EQ(testData.length(), (unsigned short)testLv.size);

    for(unsigned int x = 0; x < testData.length(); x++) {
       ASSERT_EQ(testData.c_str()[x], testLv.get()[x]);
    }
}

TEST_F(UserAttributesTest, test_decode_fail_missing_null_larger_data) {
    UserAttributes store;
    std::string testTag("testtagone");
    std::string testData(
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789");

    unsigned char data[] = {
            't', 'e', 's', 't', 't', 'a', 'g', 'o', 'n', 'e',
            (unsigned char)(testData.length() >> 8), (unsigned char)testData.length(),
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
    };

    ValueMap map;
    SharedBuffer dataBuffer(data, sizeof(data));
    try {
        map = store.tlvToValueMap(dataBuffer);
    }
    catch(std::runtime_error &exception) {
        ASSERT_TRUE(true);
        ASSERT_STREQ(NULL_DELIMITER_NOT_FOUND, exception.what());
        return;
    }

    // Should not get here.
    ASSERT_FALSE(true);
}


TEST_F(UserAttributesTest, test_encode_decode_single_value) {
    UserAttributes store;
    std::string testData("testData");
    std::string testTag("testTag");
    SharedBuffer testValue((unsigned char*)testData.c_str(), testData.length());
    ValueMap testMap(new std::map<std::string, SharedBuffer>());
    testMap->insert({testTag, testValue});
    SharedBuffer encData = store.vauleMapToTlv(testMap);

    ValueMap testMapTwo = store.tlvToValueMap(encData);
    ASSERT_EQ((unsigned int)1, testMapTwo->size());
    std::string testTagTwo = testMapTwo->begin()->first;
    ASSERT_STREQ(testTag.c_str(), testTagTwo.c_str());
    SharedBuffer testLv = testMapTwo->begin()->second;
    ASSERT_EQ(testData.length(), (unsigned short)testLv.size);

    for(unsigned int x = 0; x < testData.length(); x++) {
        ASSERT_EQ(testData.c_str()[x], testLv.get()[x]);
    }
}

TEST_F(UserAttributesTest, test_encode_decode_multiple_values) {
    UserAttributes store;
    std::string testDataOne("testDataOne");
    std::string testTagOne("testTagOne");
    SharedBuffer testValueOne((unsigned char*)testDataOne.c_str(), testDataOne.length());

    std::string testDataTwo("testDataTwo");
    std::string testTagTwo("testTagTwo");
    SharedBuffer testValueTwo((unsigned char*)testDataTwo.c_str(), testDataTwo.length());

    std::string testDataThree("testDataThree");
    std::string testTagThree("testTagThree");
    SharedBuffer testValueThree((unsigned char*)testDataThree.c_str(), testDataThree.length());

    ValueMap testMap(new std::map<std::string, SharedBuffer>());
    testMap->insert({testTagOne, testValueOne});
    testMap->insert({testTagTwo, testValueTwo});
    testMap->insert({testTagThree, testValueThree});

    SharedBuffer encData = store.vauleMapToTlv(testMap);

    ValueMap testMapTwo = store.tlvToValueMap(encData);
    ASSERT_EQ((unsigned int)3, testMapTwo->size());

    auto t = testMapTwo->find(testTagOne);
    ASSERT_FALSE(t == testMapTwo->end());
    SharedBuffer testLvOne = t->second;
    ASSERT_EQ(testDataOne.length(), (unsigned short)testLvOne.size);

    for(unsigned int x = 0; x < testDataOne.length(); x++) {
       ASSERT_EQ(testDataOne.c_str()[x], testLvOne.get()[x]);
    }

    t = testMapTwo->find(testTagTwo);
    ASSERT_FALSE(t == testMapTwo->end());
    SharedBuffer testLvTwo = t->second;
    ASSERT_EQ(testDataTwo.length(), (unsigned short)testLvTwo.size);

    for(unsigned int x = 0; x < testDataTwo.length(); x++) {
       ASSERT_EQ(testDataTwo.c_str()[x], testLvTwo.get()[x]);
    }

    t = testMapTwo->find(testTagThree);
    ASSERT_FALSE(t == testMapTwo->end());
    SharedBuffer testLvThree = t->second;
    ASSERT_EQ(testDataThree.length(), (unsigned short)testLvThree.size);

    for(unsigned int x = 0; x < testDataThree.length(); x++) {
       ASSERT_EQ(testDataThree.c_str()[x], testLvThree.get()[x]);
    }
}


TEST_F(UserAttributesTest, test_addValue) {
    UserAttributes store;
    std::string testData("hello world");
    SharedBuffer value((unsigned char*)testData.c_str(), testData.length());
    std::string tag("TestValue");
    int offset = 0;
    int dataLength =
            // Add length of tag.
            tag.length() +
            // Add length of null byte.
            1 +
            // Add length of value length.
            2 +
            // Add length of value.
            value.size;
    SharedBuffer buffer(dataLength);
    store.addValue(tag, value, buffer, &offset);

    unsigned int o = 0;
    for(unsigned int x = 0; x < tag.length() + o; x++) {
        ASSERT_EQ(tag.c_str()[x], buffer[x]);
    }
    o += tag.length();
    ASSERT_EQ('\0', buffer[o]);

    ASSERT_EQ((unsigned char)(value.size >> 8), buffer[++o]);
    ASSERT_EQ((unsigned char)(value.size), buffer[++o]);
    ++o;
    for(int x = 0; x < value.size; x++) {
        ASSERT_EQ(value.get()[x], buffer[o + x]);
    }

    ASSERT_EQ(dataLength, offset);
}

TEST_F(UserAttributesTest, test_encode) {
    UserAttributes store;

    std::string testTagOne("testTagOne");
    std::string testDataOne("testDataOne");

    std::string testTagTwo("testTagTwo");
    std::string testDataTwo("testDataTwo");

    std::string testTagThree("testTagThree");
    std::string testDataThree("testDataThree");

    SharedBuffer testLvOne((unsigned char*)testDataOne.c_str(), testDataOne.length());
    SharedBuffer testLvTwo((unsigned char*)testDataTwo.c_str(), testDataTwo.length());
    SharedBuffer testLvThree((unsigned char*)testDataThree.c_str(), testDataThree.length());

    ValueMap testMap(new std::map<std::string, SharedBuffer>());
    testMap->insert({testTagOne, testLvOne});
    testMap->insert({testTagTwo, testLvTwo});
    testMap->insert({testTagThree, testLvThree});

    SharedBuffer testBuffer = store.vauleMapToTlv(testMap);

    unsigned int testLength =
            // Length of tagOne.
            testTagOne.length() +
            // Length of null byte.
            1 +
            // Length of testDataOne length.
            2 +
            // Length of testDataOne.
            testLvOne.size +
            // Length of tagTwo.
            testTagTwo.length() +
            // Length of null byte.
            1 +
            // Length of testDataTwo length.
            2 +
            // Length of testDataTwo.
            testLvTwo.size +
            // Length of tagThree.
            testTagThree.length() +
            // Length of null byte.
            1 +
            // Length of testDataThree length.
            2 +
            // Length of testDataThree.
            testLvThree.size;

    ASSERT_EQ(testLength, testBuffer.size);

    unsigned int o = 0;
    for(unsigned int x = 0; x < testTagOne.length() + o; x++) {
        ASSERT_EQ(testTagOne.c_str()[x], testBuffer[x]);
    }
    o += testTagOne.length();
    ASSERT_EQ('\0', testBuffer[o]);

    ASSERT_EQ((unsigned char)(testLvOne.size >> 8), testBuffer[++o]);
    ASSERT_EQ((unsigned char)(testLvOne.size), testBuffer[++o]);
    ++o;
    for(int x = 0; x < testLvOne.size; x++) {
        ASSERT_EQ(testLvOne.get()[x], testBuffer[x + o]);
    }
    o += testLvOne.size;

    for(unsigned int x = 0; x < testTagThree.length(); x++) {
        ASSERT_EQ(testTagThree.c_str()[x], testBuffer[x + o]);
    }
    o += testTagThree.length();
    ASSERT_EQ('\0', testBuffer[o]);

    ASSERT_EQ((unsigned char)(testLvThree.size >> 8), testBuffer[++o]);
    ASSERT_EQ((unsigned char)(testLvThree.size), testBuffer[++o]);
    ++o;
    for(int x = 0; x < testLvThree.size; x++) {
    ASSERT_EQ(testLvThree.get()[x], testBuffer[o + x]);
    }

    o += testLvThree.size;

    for(unsigned int x = 0; x < testTagTwo.length(); x++) {
      ASSERT_EQ(testTagTwo.c_str()[x], testBuffer[x + o]);
    }
    o += testTagTwo.length();
    ASSERT_EQ('\0', testBuffer[o]);

    ASSERT_EQ((unsigned char)(testLvTwo.size >> 8), testBuffer[++o]);
    ASSERT_EQ((unsigned char)(testLvTwo.size), testBuffer[++o]);
    ++o;
    for(int x = 0; x < testLvTwo.size; x++) {
      ASSERT_EQ(testLvTwo.get()[x], testBuffer[o + x]);
    }

    o += testLvTwo.size;
}
