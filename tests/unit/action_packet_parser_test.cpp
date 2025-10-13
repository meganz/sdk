#include "gtest/gtest.h"
#include "mega/action_packet_parser.h"
#include "mega/megaclient.h"
#include "mega/json.h"
#include <memory>
#include <vector>

using namespace mega;

// MockMegaClient类用于测试
class MockMegaClient : public MegaClient {
public:
    MockMegaClient() : MegaClient(nullptr, nullptr, nullptr, nullptr, nullptr, "", "", 0) {
        nodeTreeMutex.unlock();
    }
    
    MOCK_METHOD0(sc_updatenode, void());
    MOCK_METHOD0(sc_deltree, std::unique_ptr<Node>());
    MOCK_METHOD0(sc_shares, bool());
    MOCK_METHOD0(mergenewshares, void(int));
    MOCK_METHOD0(sc_contacts, void());
    MOCK_METHOD0(sc_fileattr, void());
    MOCK_METHOD0(sc_userattr, void());
    MOCK_METHOD1(sc_upgrade, bool(int));
    MOCK_METHOD0(sc_paymentreminder, void());
    MOCK_METHOD0(sc_ipc, void());
    MOCK_METHOD0(sc_opc, void());
    MOCK_METHOD1(sc_upc, void(bool));
    MOCK_METHOD0(sc_ph, void());
    MOCK_METHOD0(sc_se, void());
    MOCK_METHOD1(sc_newnodes, handle(Node*, bool&));
    
    // 用于测试的辅助方法
    void setLoggedIntoFolder(bool value) {
        loggedIntoFolderValue = value;
    }
    
    bool loggedIntoFolder() override {
        return loggedIntoFolderValue;
    }
    
private:
    bool loggedIntoFolderValue = false;
};

class ActionPacketParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        client = std::make_shared<::testing::NiceMock<MockMegaClient>>();
        parser = std::make_unique<ActionPacketParser>(client.get());
    }
    
    void TearDown() override {
        parser.reset();
        client.reset();
    }
    
    std::shared_ptr<::testing::NiceMock<MockMegaClient>> client;
    std::unique_ptr<ActionPacketParser> parser;
};

// 测试初始化状态
TEST_F(ActionPacketParserTest, InitialState) {
    EXPECT_EQ(parser->getState(), ActionPacketParser::STATE_NOT_STARTED);
    EXPECT_FALSE(parser->hasFinished());
    EXPECT_FALSE(parser->hasFailed());
}

// 测试clear方法
TEST_F(ActionPacketParserTest, ClearMethod) {
    // 先处理一些数据来改变状态
    const char* chunk = "[{\"a\":\"u\"}]";
    parser->processChunk(chunk, strlen(chunk));
    
    // 清除状态
    parser->clear();
    
    // 验证状态被重置
    EXPECT_EQ(parser->getState(), ActionPacketParser::STATE_NOT_STARTED);
    EXPECT_FALSE(parser->hasFinished());
    EXPECT_FALSE(parser->hasFailed());
}

// 测试处理完整的JSON数据
TEST_F(ActionPacketParserTest, ProcessCompleteJson) {
    // 完整的action packet数据
    const char* completeJson = "[{\"a\":\"u\"}]";
    
    // 期望调用sc_updatenode
    EXPECT_CALL(*client, sc_updatenode()).Times(1);
    
    // 处理数据
    m_off_t consumed = parser->processChunk(completeJson, strlen(completeJson));
    
    // 验证解析完成且消费了所有数据
    EXPECT_EQ(consumed, strlen(completeJson));
    EXPECT_EQ(parser->getState(), ActionPacketParser::STATE_COMPLETED);
    EXPECT_TRUE(parser->hasFinished());
    EXPECT_FALSE(parser->hasFailed());
}

// 测试处理分块的JSON数据
TEST_F(ActionPacketParserTest, ProcessChunkedJson) {
    // 分块的JSON数据
    const char* chunk1 = "[{\"a\":";
    const char* chunk2 = "\"u\"}]";
    
    // 期望调用sc_updatenode
    EXPECT_CALL(*client, sc_updatenode()).Times(1);
    
    // 处理第一块数据
    m_off_t consumed1 = parser->processChunk(chunk1, strlen(chunk1));
    EXPECT_EQ(consumed1, strlen(chunk1));
    EXPECT_EQ(parser->getState(), ActionPacketParser::STATE_PARSING);
    
    // 处理第二块数据
    m_off_t consumed2 = parser->processChunk(chunk2, strlen(chunk2));
    EXPECT_EQ(consumed2, strlen(chunk2));
    EXPECT_EQ(parser->getState(), ActionPacketParser::STATE_COMPLETED);
    EXPECT_TRUE(parser->hasFinished());
}

// 测试处理无效的JSON数据
TEST_F(ActionPacketParserTest, ProcessInvalidJson) {
    // 无效的JSON数据
    const char* invalidJson = "[{\"a\":invalid}]";
    
    // 处理数据
    m_off_t consumed = parser->processChunk(invalidJson, strlen(invalidJson));
    
    // 验证解析失败
    EXPECT_EQ(parser->getState(), ActionPacketParser::STATE_FAILED);
    EXPECT_FALSE(parser->hasFinished());
    EXPECT_TRUE(parser->hasFailed());
}

// 测试处理多个action packets
TEST_F(ActionPacketParserTest, ProcessMultipleActionPackets) {
    // 包含多个action packets的JSON
    const char* multiplePackets = "[{\"a\":\"u\"},{\"a\":\"c\"}]";
    
    // 期望调用相应的方法
    EXPECT_CALL(*client, sc_updatenode()).Times(1);
    EXPECT_CALL(*client, sc_contacts()).Times(1);
    
    // 处理数据
    m_off_t consumed = parser->processChunk(multiplePackets, strlen(multiplePackets));
    
    // 验证解析完成
    EXPECT_EQ(consumed, strlen(multiplePackets));
    EXPECT_EQ(parser->getState(), ActionPacketParser::STATE_COMPLETED);
    EXPECT_TRUE(parser->hasFinished());
}

// 测试处理节点元素
TEST_F(ActionPacketParserTest, ProcessNodesElement) {
    // 包含节点元素的JSON
    const char* nodesJson = "[{\"t\":[]}]";
    
    // 期望调用sc_newnodes和mergenewshares
    EXPECT_CALL(*client, sc_newnodes(nullptr, ::testing::_)).Times(1).WillOnce(::testing::Return(123));
    EXPECT_CALL(*client, mergenewshares(1)).Times(1);
    
    // 处理数据
    m_off_t consumed = parser->processChunk(nodesJson, strlen(nodesJson));
    
    // 验证解析完成
    EXPECT_EQ(consumed, strlen(nodesJson));
    EXPECT_EQ(parser->getState(), ActionPacketParser::STATE_COMPLETED);
    EXPECT_TRUE(parser->hasFinished());
}

// 测试在解析完成或失败后不再处理数据
TEST_F(ActionPacketParserTest, NoProcessingAfterCompletionOrFailure) {
    // 完整的JSON数据
    const char* completeJson = "[{\"a\":\"u\"}]";
    const char* additionalData = "extra data";
    
    // 期望只调用一次sc_updatenode
    EXPECT_CALL(*client, sc_updatenode()).Times(1);
    
    // 处理完整数据
    parser->processChunk(completeJson, strlen(completeJson));
    EXPECT_TRUE(parser->hasFinished());
    
    // 尝试处理额外数据
    m_off_t consumed = parser->processChunk(additionalData, strlen(additionalData));
    
    // 验证没有消费额外数据
    EXPECT_EQ(consumed, 0);
}

// 测试错误恢复 - 清除后重新开始解析
TEST_F(ActionPacketParserTest, ErrorRecovery) {
    // 无效的JSON数据
    const char* invalidJson = "[{\"a\":invalid}]";
    const char* validJson = "[{\"a\":\"u\"}]";
    
    // 期望调用sc_updatenode
    EXPECT_CALL(*client, sc_updatenode()).Times(1);
    
    // 处理无效数据，应该失败
    parser->processChunk(invalidJson, strlen(invalidJson));
    EXPECT_TRUE(parser->hasFailed());
    
    // 清除状态
    parser->clear();
    
    // 处理有效数据，应该成功
    m_off_t consumed = parser->processChunk(validJson, strlen(validJson));
    
    // 验证解析成功
    EXPECT_EQ(consumed, strlen(validJson));
    EXPECT_TRUE(parser->hasFinished());
    EXPECT_FALSE(parser->hasFailed());
}

// 测试处理大型数据（模拟大数据场景）
TEST_F(ActionPacketParserTest, ProcessLargeData) {
    // 构建大型JSON数据
    std::string largeJson = "[";
    for (int i = 0; i < 100; ++i) {
        largeJson += "{\"a\":\"u\"}";
        if (i < 99) largeJson += ",";
    }
    largeJson += "]";
    
    // 期望调用sc_updatenode 100次
    EXPECT_CALL(*client, sc_updatenode()).Times(100);
    
    // 处理大型数据
    m_off_t consumed = parser->processChunk(largeJson.c_str(), largeJson.size());
    
    // 验证解析完成
    EXPECT_EQ(consumed, static_cast<m_off_t>(largeJson.size()));
    EXPECT_EQ(parser->getState(), ActionPacketParser::STATE_COMPLETED);
    EXPECT_TRUE(parser->hasFinished());
}