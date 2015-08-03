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
#include <functional>

#include "../include/mega/secureBuffer.h"
#include "../include/mega/sharedbuffer.h"
#include "../include/mega/userAttributes.h"
#include "../include/megaapi.h"
#include "../include/sodium.h"
#include "../include/mega/base64.h"
#include "../include/mega/logging.h"

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
    //std::string loginNameOne = "megachatclitest1@gmail.com";
    //std::string passWordOne = "Meg@test4fun";
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

    bool wait;
    std::string email;
    const char *rsaBase64;
    bool success;
    std::map<std::string, std::pair<unsigned char*, unsigned int>> *valMap;
    MegaApi *api;
    MegaRequest *lastRequest;
    TestClient(std::string loginName, std::string passWord) :
        loginName(loginName), passWord(passWord), wait(true),
        rsaBase64(nullptr), success(false), valMap(nullptr),
        lastRequest(nullptr) {
        api = new MegaApi("sdfsdfsdf", (const char*)NULL, "sdk_test");
        api->setLogLevel(mega::logTest);
    }

    virtual ~TestClient() {
        delete api;
    }

    void tlvArrayToMap(TLV *tlvArray, unsigned int tlvLen) {
        valMap = new std::map<std::string, std::pair<unsigned char*, unsigned int>>();
        for(int x = 0; x < tlvLen; x++)
        {
            std::pair<unsigned char*, unsigned int> p;
            p.second = tlvArray[x].getLength();
            p.first = (unsigned char*)malloc(p.second);
            memcpy(p.first, tlvArray[x].getValue(), p.second);
            valMap->insert({std::string(tlvArray[x].getType()), p});
        }
    }

    virtual void onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e) {
        LOG_test << "onRequestFinish called";
        lastRequest = request->copy();
        const char *eM;
        TLV *tlvArray = nullptr;
        unsigned int tlvLen = 0;
        switch(request->getType()) {
        case MegaRequest::TYPE_LOGIN :
            LOG_test << "Type login";
            success = e->getErrorCode() == MegaError::API_OK;
            wait = false;
            break;

        case MegaRequest::TYPE_GET_USER_DATA :
            LOG_test << "Type get user data";
            eM = request->getText();

            if(e->getErrorCode() == MegaError::API_OK && eM)
            {
                success = true;
                email = std::string(eM);
                rsaBase64 = request->getPassword();
            }
            else
            {
                LOG_err << e->toString();
                success = false;
            }
            LOG_test << "request finished";
            wait = false;
            break;
        case MegaRequest::TYPE_GET_USER_ATTRIBUTE :
            LOG_test << "Type = get user attribute";
            if(e->getErrorCode() == MegaError::API_OK) {
                success = true;
                request->getUserAttributeMap(&tlvArray, &tlvLen);
                tlvArrayToMap(tlvArray, tlvLen);
            }
            else {
                LOG_err << e->toString();
                success = false;
            }
            wait = false;
            break;
        case MegaRequest::TYPE_GET_STATIC_PUB_KEY :
            LOG_test << "Type = get signing keys";
            if(e->getErrorCode() == MegaError::API_OK) {
                success = true;
                request->getUserAttributeMap(&tlvArray, &tlvLen);
                tlvArrayToMap(tlvArray, tlvLen);
            }
            else {
                LOG_err << e->toString();
                success = false;
            }
            wait = false;
            break;
        case MegaRequest::TYPE_GET_SIGNING_KEYS :
            LOG_test << "Type = get signing keys";

            if(e->getErrorCode() == MegaError::API_OK) {
                success = true;
                request->getUserAttributeMap(&tlvArray, &tlvLen);
                tlvArrayToMap(tlvArray, tlvLen);
            }
            else {
                LOG_err << e->toString();
                success = false;
            }
            wait = false;
            break;
        case MegaRequest::TYPE_SET_USER_ATTRIBUTE :
            LOG_test << "Type = set user attribute";

            if(e->getErrorCode() == MegaError::API_OK) {
                success = true;
            }
            else {
                LOG_err << e->toString();
                success = false;
            }
            wait = false;
            break;
        case MegaRequest::TYPE_FETCH_NODES :
            LOG_test << "Type = fetch nodes";

            if(e->getErrorCode() == MegaError::API_OK) {
                success = true;
            }
            else {
                LOG_err << e->toString();
                success = false;
            }
            wait = false;
            break;
        case MegaRequest::TYPE_VERIFY_RSA_SIG:
        {
            LOG_test << "Type = verify rsa sig";

            if(e->getErrorCode() == MegaError::API_OK) {
                success = true;
            }
            else {
                LOG_err << e->toString();
                success = false;
            }
            wait = false;
            break;
        }
        case MegaRequest::TYPE_VERIFY_KEY_FINGERPRINT:
        {
            LOG_test << "Type = verify key fp";

            if(e->getErrorCode() == MegaError::API_OK) {
                success = true;
            }
            else {
                LOG_err << e->toString();
                success = false;
            }
            wait = false;
            break;
        }
        case MegaRequest::TYPE_SET_ATTR_USER:
        {
            LOG_test << "Type = set attr user";
            if(e->getErrorCode() == MegaError::API_OK) {
                success = true;
            }
            wait = false;
            break;
        }
        case MegaRequest::TYPE_GET_ATTR_USER:
        {
            LOG_test << "Type = get attr user";
            if(e->getErrorCode() == MegaError::API_OK) {
                success = true;
                LOG_test << request->getText();
            }
            wait = false;
            break;
        }
        default:
            wait = false;
            LOG_test << "other type: " << request->getType();
        }

        LOG_test << "exit";
    }

    bool login() {
        wait = true;
        LOG_info << "logging in";

        api->login(loginName.c_str(), passWord.c_str(), this);
        LOG_test << "Waiting";

        while(wait) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        LOG_test << "exit wait";
        if(!success) {
            LOG_test << "login failed";
            return false;
        }
        LOG_test << "login success";
        LOG_test << "fetch user data";
        wait = true;
        success = false;
        LOG_test << "get user data";
        api->getUserData(this);
        while(wait) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if(!success) {
            std::cerr << "login failed" << std::endl;
            return false;
        }
        LOG_test << "user data obtained";

        byte data[4096];
        int l = Base64::atob(rsaBase64, data, sizeof(data));
        LOG_test << "size of bytes = " << l;

        wait = true;
        success = false;
        api->fetchNodes(this);
        while(wait) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if(!success) {
            LOG_test << "fetch nodes failed";
            return false;
        }

        return success;
    }
};

/**
 * @brief Use this to call api functions asynchronusly and test the outcome.
 */
template <typename T, typename ...Params>
void callApiFunction(TestClient &testClient, T f, Params... p) {
    testClient.wait = true;
    testClient.success = false;
    (testClient.api->*f)(p...);
    while(testClient.wait) {
       std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ASSERT_TRUE(testClient.success);

}

/**
 * @brief Use this to reset key finger prints.
 */
void resetKeys(TestClient &testClient) {
    TLV resetMap[] = { TLV("", 0, nullptr)};
    TLV resetMapE[] = { TLV("", 0, nullptr)};
        testClient.wait = true;
    testClient.success = false;

    testClient.api->putGenericUserAttribute(testClient.loginName.c_str(),
            "authRSA", resetMap, 1, 1, 1, &testClient);
    while(testClient.wait) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ASSERT_TRUE(testClient.success);

    testClient.wait = true;
    testClient.success = false;

    testClient.api->putGenericUserAttribute(testClient.loginName.c_str(),
            "authring", resetMapE, 1, 1, 1, &testClient);
    while(testClient.wait) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ASSERT_TRUE(testClient.success);
}

TEST_F(ApiTest, testSetup) {
    TestClient tcOne(loginNameOne, passWordOne);
    TestClient tcTwo(loginNameThree, passWordThree);
    TestClient tcThree(loginNameTwo, passWordTwo);

    TLV resetMap[] = { TLV("", 0, nullptr)};
    TLV resetMapE[] = { TLV("", 0, nullptr)};
    if(tcOne.login()) {
        LOG_test << "Login success";
        tcOne.wait = true;
        tcOne.success = false;
        std::map<std::string, std::pair<unsigned char*, unsigned int>> map;
        std::string testValueStr("A bunch of stuff!!!!!!111!!11!11!");

        map.insert({"uName", {(unsigned char*)testValueStr.c_str(), testValueStr.size()}});

        LOG_info << "Test mapToValueMap";
        ValueMap vMap = UserAttributes::mapToValueMap(&map);
        auto j = vMap->find("uName");
        ASSERT_TRUE(j != vMap->end());
        std::string val((char*)j->second.get(), j->second.size);
        ASSERT_STREQ(testValueStr.c_str(), val.c_str());

        LOG_info << "Test valueMapToTLVarray";
        TLV *retMap = UserAttributes::valueMapToTLVarray(vMap);

        LOG_test << "Test putting attribute Names";
        tcOne.api->putGenericUserAttribute(loginNameOne.c_str(), "Names", retMap,
                vMap->size(), 0, 1, &tcOne);
        while(tcOne.wait) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ASSERT_TRUE(tcOne.success);

        callApiFunction(tcOne, &MegaApi::getGenericUserAttribute,
                loginNameOne.c_str(), "Names", &tcOne);

        //auto l = tcOne.valMap->find("");
        TLV namesTLV = tcOne.lastRequest->getTLV("");
        LOG_test << "Test names 2";
        //ASSERT_TRUE(l != tcOne.valMap->end());
        ASSERT_TRUE(namesTLV.getValue() != NULL);
        LOG_test << "Test names 3";
        //std::string retVal((char*)l->second.first, l->second.second);
        std::string retVal((char*)namesTLV.getValue(), namesTLV.getLength());
        ASSERT_STREQ(testValueStr.c_str(), retVal.c_str());

        tcOne.wait = true;
        tcOne.success = false;
        tcOne.api->putGenericUserAttribute(loginNameOne.c_str(), "Names", retMap,
                vMap->size(), 1, 0, &tcOne);
        while(tcOne.wait) {
           std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ASSERT_TRUE(tcOne.success);

        callApiFunction(tcOne, &MegaApi::getOwnStaticKeys, &tcOne);

        ASSERT_TRUE(tcOne.lastRequest->getTLV("prEd255").getValue() != NULL);
        ASSERT_TRUE(tcOne.lastRequest->getTLV("").getValue() != NULL);

        std::string fNameStr("michael");
        TLV fname("firstname", fNameStr.length(), (unsigned char*)fNameStr.c_str());
        callApiFunction(tcOne, &MegaApi::putGenericUserAttribute,
                tcOne.loginName.c_str(), "firstname", &fname, 1, 0, 1, &tcOne);

        callApiFunction(tcOne, &MegaApi::getGenericUserAttribute,
                loginNameOne.c_str(), "firstname", &tcOne);

        callApiFunction(tcOne, &MegaApi::setUserAttribute, MegaApi::USER_ATTR_FIRSTNAME,
                "mike", &tcOne);

        callApiFunction(tcOne,
                static_cast<void(MegaApi::*)(const char*, int, const char*, MegaRequestListener*)>(&MegaApi::getUserAttribute),
                loginNameOne.c_str(), MegaApi::USER_ATTR_FIRSTNAME, (const char*)nullptr, &tcOne);

    }
    else {
        LOG_test << "login tcOne failed";
        exit(-1);
    }
    if(tcThree.login()) {
        LOG_test << "Login success";
        callApiFunction(tcThree, &MegaApi::getOwnStaticKeys, &tcThree);
    }
    else {
        LOG_test << "login tcThree failed";
        exit(-1);
    }

    if(tcTwo.login()) {
        LOG_test << "Login success";
        callApiFunction(tcTwo, &MegaApi::getGenericUserAttribute,
                "michaelholmwood@mega.co.nz", "puEd255", &tcTwo);
        callApiFunction(tcTwo, &MegaApi::getGenericUserAttribute,
                "michaelholmwood@mega.co.nz", "sgPubk", &tcTwo);
        callApiFunction(tcTwo,
                static_cast<void (MegaApi::*)(const char*,
                        MegaRequestListener*)>(&MegaApi::getUserData),
                "michaelholmwood@mega.co.nz", &tcTwo);
        callApiFunction(tcTwo,
                static_cast<void (MegaApi::*)(const char*,
                        MegaRequestListener*)>(&MegaApi::getUserData),
                "michaelholmwood@mega.co.nz", &tcTwo);
        callApiFunction(tcTwo, &MegaApi::getPublicStaticKey,
                "michaelholmwood@mega.co.nz", &tcTwo);
    }
}

TEST_F(UserAttributesTest, test_null_key) {
    std::string testData("hello this is a test.");
    SharedBuffer testValue((unsigned char*)testData.c_str(), testData.size());
    ValueMap map(new std::map<std::string, SharedBuffer>);
    map->insert({"", testValue});
    SharedBuffer tlv = mega::UserAttributes::valueMapToTlv(map);
    std::cout << tlv.size << std::endl;
    for(int x = 0; x < tlv.size; x++)
    {
        std::cout << "tlv[" << x << "] = " << (char)tlv.get()[x] << ", ";
    }
    std::cout << endl;
    for(int x = 0; x < tlv.size; x++)
    {
       std::cout << "tlv[" << x << "] = " << (int)tlv.get()[x] << ", ";
    }
    std::cout << endl;

    ValueMap retMap = mega::UserAttributes::tlvToValueMap(tlv);
    auto i = retMap->find("");
    ASSERT_TRUE(i != retMap->end());
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
    SharedBuffer encData = store.valueMapToTlv(testMap);

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

    SharedBuffer encData = store.valueMapToTlv(testMap);

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

    SharedBuffer testBuffer = store.valueMapToTlv(testMap);

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

TEST_F(UserAttributesTest, testStaticFunctions) {
    std::string testDataOne("testDataOne");
    std::string testDataTwo("testDataTwo");
    std::string testDataThree("testDataThree");
    TLV tlv[] = {
            { "testDataOne", testDataOne.size(), (unsigned char*)testDataOne.c_str() },
            { "testDataTwo", testDataTwo.size(), (unsigned char*)testDataTwo.c_str() },
            { "testDataThree", testDataThree.size(), (unsigned char*)testDataThree.c_str() }
    };

    ValueMap map = UserAttributes::tlvArrayToValueMap(tlv, 3);
    ASSERT_TRUE(map->find("testDataOne") != map->end());
    ASSERT_TRUE(map->find("testDataTwo") != map->end());
    ASSERT_TRUE(map->find("testDataThree") != map->end());

    for(int x = 0; x < tlv[0].getLength(); x++) {
        ASSERT_EQ((*map)["testDataOne"].get()[x], tlv[0].getValue()[x]);
    }
    for(int x = 0; x < tlv[1].getLength(); x++) {
        ASSERT_EQ((*map)["testDataTwo"].get()[x], tlv[1].getValue()[x]);
    }
    for(int x = 0; x < tlv[2].getLength(); x++) {
        ASSERT_EQ((*map)["testDataThree"].get()[x], tlv[2].getValue()[x]);
    }

    ValueMap vMap = ValueMap(new std::map<std::string, SharedBuffer>);
    SharedBuffer vOne((unsigned char*)testDataOne.c_str(), testDataOne.size());
    SharedBuffer vTwo((unsigned char*)testDataTwo.c_str(), testDataTwo.size());
    SharedBuffer vThree((unsigned char*)testDataThree.c_str(), testDataThree.size());
    vMap->insert({"testDataOne", testDataOne});
    vMap->insert({"testDataTwo", testDataTwo});
    vMap->insert({"testDataThree", testDataThree});

    TLV *rArr = UserAttributes::valueMapToTLVarray(vMap);

    for(int x = 0; x < 3; x++)
    {
        SharedBuffer test;
        if(strcmp(rArr[x].getType(), "testDataOne") == 0)
        {
            test = vOne;
        }
        else if(strcmp(rArr[x].getType(), "testDataTwo") == 0)
        {
            test = vTwo;
        }
        else if(strcmp(rArr[x].getType(), "testDataThree") == 0)
        {
            test = vThree;
        }

        for(int y = 0; y < test.size; y++)
        {
            ASSERT_EQ(test.get()[y], rArr[x].getValue()[y]);
        }
    }
}
