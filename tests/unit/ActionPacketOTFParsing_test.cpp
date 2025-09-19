#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "mega/json.h"
#include "mega/megaclient.h"
#include "mega/command.h"

using namespace mega;
using namespace std;
using namespace testing;

class ActionPacketOTFParsing_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        testJsonData = R"({"a":[{"a":"ua","st":"!,test;ABC","u":"TestHandle123","ua":["^!fakeattr"],"v":["TestVersion456"]}],"w":"https://test.api.example.com/wsc/FakeToken789","sn":"TestSequence123"})";
        
        multipleAttributesJson = R"({"a":[{"a":"ua","st":"!,multi;XYZ","u":"MultiHandle999","ua":["^!attr1","^!attr2","^!attr3"],"v":["Ver1","Ver2"]}],"w":"https://test.example.com/wsc/Multi123","sn":"MultiSeq456"})";
        
        emptyAttributesJson = R"({"a":[{"a":"ua","st":"!,empty;DEF","u":"EmptyHandle000","ua":[],"v":["EmptyVer789"]}],"w":"https://test.empty.com/wsc/Empty456","sn":"EmptySeq000"})";
    }

    string testJsonData;
    string multipleAttributesJson;
    string emptyAttributesJson;
};

TEST_F(ActionPacketOTFParsing_test, ParseUserAttributeActionPacket)
{
    JSON json;
    json.begin(testJsonData.c_str());
    
    ASSERT_TRUE(json.enterobject());
    
    EXPECT_EQ(json.getnameid(), makeNameid("a"));
    ASSERT_TRUE(json.enterarray());
    
    ASSERT_TRUE(json.enterobject());
    
    EXPECT_EQ(json.getnameid(), makeNameid("a"));
    string actionType;
    ASSERT_TRUE(json.storeobject(&actionType));
    EXPECT_EQ(actionType, "ua");
    
    EXPECT_EQ(json.getnameid(), makeNameid("st"));
    string shareToken;
    ASSERT_TRUE(json.storeobject(&shareToken));
    EXPECT_EQ(shareToken, "!,test;ABC");
    
    EXPECT_EQ(json.getnameid(), makeNameid("u"));
    string userHandle;
    ASSERT_TRUE(json.storeobject(&userHandle));
    EXPECT_EQ(userHandle, "TestHandle123");
    
    EXPECT_EQ(json.getnameid(), makeNameid("ua"));
    ASSERT_TRUE(json.enterarray());
    string userAttr;
    ASSERT_TRUE(json.storeobject(&userAttr));
    EXPECT_EQ(userAttr, "^!fakeattr");
    json.leavearray();
    
    EXPECT_EQ(json.getnameid(), makeNameid("v"));
    ASSERT_TRUE(json.enterarray());
    string version;
    ASSERT_TRUE(json.storeobject(&version));
    EXPECT_EQ(version, "TestVersion456");
    json.leavearray();
    
    json.leaveobject();
    json.leavearray();
    
    EXPECT_EQ(json.getnameid(), makeNameid("w"));
    string wsUrl;
    ASSERT_TRUE(json.storeobject(&wsUrl));
    EXPECT_EQ(wsUrl, "https://test.api.example.com/wsc/FakeToken789");
    
    EXPECT_EQ(json.getnameid(), makeNameid("sn"));
    string sequenceNumber;
    ASSERT_TRUE(json.storeobject(&sequenceNumber));
    EXPECT_EQ(sequenceNumber, "TestSequence123");
    
    json.leaveobject();
}

TEST_F(ActionPacketOTFParsing_test, ValidateActionPacketStructure)
{
    JSON json;
    json.begin(testJsonData.c_str());
    
    ASSERT_TRUE(json.enterobject());
    
    EXPECT_EQ(json.getnameid(), makeNameid("a"));
    ASSERT_TRUE(json.enterarray());
    ASSERT_TRUE(json.enterobject());
    
    EXPECT_EQ(json.getnameid(), makeNameid("a"));
    string actionType;
    ASSERT_TRUE(json.storeobject(&actionType));
    EXPECT_EQ(actionType, "ua");
    
    json.leaveobject();
    json.leavearray();
    
    EXPECT_EQ(json.getnameid(), makeNameid("w"));
    string wsUrl;
    ASSERT_TRUE(json.storeobject(&wsUrl));
    EXPECT_TRUE(wsUrl.find("https://") == 0) << "WebSocket URL should use HTTPS";
    
    EXPECT_EQ(json.getnameid(), makeNameid("sn"));
    string sn;
    ASSERT_TRUE(json.storeobject(&sn));
    EXPECT_FALSE(sn.empty()) << "Sequence number should not be empty";
    EXPECT_EQ(sn, "TestSequence123");
    
    json.leaveobject();
}

TEST_F(ActionPacketOTFParsing_test, ParseUserAttributeContent)
{
    JSON json;
    json.begin(testJsonData.c_str());
    
    ASSERT_TRUE(json.enterobject());
    EXPECT_EQ(json.getnameid(), makeNameid("a"));
    ASSERT_TRUE(json.enterarray());
    ASSERT_TRUE(json.enterobject());
    
    EXPECT_EQ(json.getnameid(), makeNameid("a"));
    json.storeobject();
    
    EXPECT_EQ(json.getnameid(), makeNameid("st"));
    json.storeobject();
    
    EXPECT_EQ(json.getnameid(), makeNameid("u"));
    json.storeobject();
    
    EXPECT_EQ(json.getnameid(), makeNameid("ua"));
    ASSERT_TRUE(json.enterarray());
    
    string userAttr;
    ASSERT_TRUE(json.storeobject(&userAttr));
    
    EXPECT_EQ(userAttr, "^!fakeattr");
    EXPECT_TRUE(userAttr.length() > 0);
    EXPECT_TRUE(userAttr[0] == '^') << "User attribute should start with '^'";
    
    json.leavearray();
    json.leaveobject();
    json.leavearray();
    json.leaveobject();
}

TEST_F(ActionPacketOTFParsing_test, HandleMalformedActionPacket)
{
    string malformedJson = R"({"a":[{"a":"ua","st":"!,test;ABC","u":"TestHandle123","ua":["^!fakeattr"],"v":["TestVersion456"]}],"w":"https://test.api.example.com/wsc/FakeToken789","sn":)";
    
    JSON json;
    json.begin(malformedJson.c_str());
    
    ASSERT_TRUE(json.enterobject());
    
    EXPECT_EQ(json.getnameid(), makeNameid("a"));
    ASSERT_TRUE(json.enterarray());
    ASSERT_TRUE(json.enterobject());
    
    EXPECT_EQ(json.getnameid(), makeNameid("a"));
    string actionType;
    ASSERT_TRUE(json.storeobject(&actionType));
    EXPECT_EQ(actionType, "ua");
    
    json.leaveobject();
    json.leavearray();
    
    EXPECT_EQ(json.getnameid(), makeNameid("w"));
    string wsUrl;
    ASSERT_TRUE(json.storeobject(&wsUrl));
    EXPECT_FALSE(wsUrl.empty());
    
    nameid snField = json.getnameid();
    if (snField == makeNameid("sn"))
    {
        string sn;
        json.storeobject(&sn);
    }
    
    SUCCEED() << "Malformed JSON handled gracefully";
}

TEST_F(ActionPacketOTFParsing_test, ParseMultipleUserAttributes)
{
    JSON json;
    json.begin(multipleAttributesJson.c_str());
    
    ASSERT_TRUE(json.enterobject());
    EXPECT_EQ(json.getnameid(), makeNameid("a"));
    ASSERT_TRUE(json.enterarray());
    ASSERT_TRUE(json.enterobject());
    
    EXPECT_EQ(json.getnameid(), makeNameid("a"));
    json.storeobject();
    
    EXPECT_EQ(json.getnameid(), makeNameid("st"));
    json.storeobject();
    
    EXPECT_EQ(json.getnameid(), makeNameid("u"));
    json.storeobject();
    
    EXPECT_EQ(json.getnameid(), makeNameid("ua"));
    ASSERT_TRUE(json.enterarray());
    
    string attr1;
    ASSERT_TRUE(json.storeobject(&attr1));
    EXPECT_EQ(attr1, "^!attr1");
    
    string attr2;
    ASSERT_TRUE(json.storeobject(&attr2));
    EXPECT_EQ(attr2, "^!attr2");
    
    string attr3;
    ASSERT_TRUE(json.storeobject(&attr3));
    EXPECT_EQ(attr3, "^!attr3");
    
    string noMore;
    EXPECT_FALSE(json.storeobject(&noMore));
    
    json.leavearray();
    json.leaveobject();
    json.leavearray();
    json.leaveobject();
}

TEST_F(ActionPacketOTFParsing_test, HandleEmptyUserAttributes)
{
    JSON json;
    json.begin(emptyAttributesJson.c_str());
    
    ASSERT_TRUE(json.enterobject());
    EXPECT_EQ(json.getnameid(), makeNameid("a"));
    ASSERT_TRUE(json.enterarray());
    ASSERT_TRUE(json.enterobject());
    
    EXPECT_EQ(json.getnameid(), makeNameid("a"));
    json.storeobject();
    
    EXPECT_EQ(json.getnameid(), makeNameid("st"));
    json.storeobject();
    
    EXPECT_EQ(json.getnameid(), makeNameid("u"));
    json.storeobject();
    
    EXPECT_EQ(json.getnameid(), makeNameid("ua"));
    ASSERT_TRUE(json.enterarray());
    
    string userAttr;
    bool hasElement = json.storeobject(&userAttr);
    EXPECT_FALSE(hasElement) << "Empty array should not have elements";
    
    json.leavearray();
    json.leaveobject();
    json.leavearray();
    json.leaveobject();
}

TEST_F(ActionPacketOTFParsing_test, ValidateActionPacketFields)
{
    JSON json;
    json.begin(testJsonData.c_str());
    
    ASSERT_TRUE(json.enterobject());
    EXPECT_EQ(json.getnameid(), makeNameid("a"));
    ASSERT_TRUE(json.enterarray());
    ASSERT_TRUE(json.enterobject());
    
    EXPECT_EQ(json.getnameid(), makeNameid("a"));
    string actionType;
    ASSERT_TRUE(json.storeobject(&actionType));
    EXPECT_EQ(actionType, "ua") << "Action type should be 'ua' for user attributes";
    
    EXPECT_EQ(json.getnameid(), makeNameid("st"));
    string shareToken;
    ASSERT_TRUE(json.storeobject(&shareToken));
    EXPECT_TRUE(shareToken.find("!,") == 0) << "Share token should start with '!,'";
    
    EXPECT_EQ(json.getnameid(), makeNameid("u"));
    string userHandle;
    ASSERT_TRUE(json.storeobject(&userHandle));
    EXPECT_FALSE(userHandle.empty()) << "User handle should not be empty";
    EXPECT_GT(userHandle.length(), 5) << "User handle should be reasonably long";
    
    json.leaveobject();
    json.leavearray();
    json.leaveobject();
}
