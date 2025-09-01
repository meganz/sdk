/**
 * @file test_actionpacket_streaming.cpp
 * @brief Comprehensive test suite for ActionPacket Streaming Parser
 */

#include "mega/actionpacketparser.h"
#include "mega/megaclient.h"
#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <string>
#include <chrono>

using namespace mega;
using namespace std;

namespace {

/**
 * Mock MegaClient for testing
 */
class MockMegaClient : public MegaClient {
public:
    vector<string> processedPackets;
    vector<string> errors;
    bool errorRecovered = false;
    
    MockMegaClient() : MegaClient(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "TestClient") {}
    
    void logActionPacket(const string& packet) {
        processedPackets.push_back(packet);
    }
    
    void logError(const string& error, bool recovered) {
        errors.push_back(error);
        errorRecovered = recovered;
    }
};

/**
 * Test data generator
 */
class TestDataGenerator {
public:
    static string createSimpleActionPacket(const string& action, int id = 1) {
        return R"({"a":")" + action + R"(","id":)" + to_string(id) + R"(,"data":"test"})";
    }
    
    static string createLargeActionPacket(const string& action, size_t size) {
        string data(size - 50, 'x'); // Adjust for JSON overhead
        return R"({"a":")" + action + R"(","data":")" + data + R"("})";
    }
    
    static string createTreeActionPacket(int nodeCount) {
        string result = R"({"a":"t","t":[)";
        for (int i = 0; i < nodeCount; ++i) {
            if (i > 0) result += ",";
            result += R"({"h":"node)" + to_string(i) + R"(","p":"parent","s":1024,"ts":)" + to_string(1234567890 + i) + "}";
        }
        result += "]}";
        return result;
    }
    
    static string createActionPacketSequence(const vector<string>& packets) {
        string result = "[";
        for (size_t i = 0; i < packets.size(); ++i) {
            if (i > 0) result += ",";
            result += packets[i];
        }
        result += "]";
        return result;
    }
};

} // anonymous namespace

/**
 * Basic functionality tests
 */
class ActionPacketParserBasicTest : public ::testing::Test {
protected:
    void SetUp() override {
        client = make_unique<MockMegaClient>();
        parser = make_unique<ActionPacketParser>(*client);
    }
    
    unique_ptr<MockMegaClient> client;
    unique_ptr<ActionPacketParser> parser;
};

TEST_F(ActionPacketParserBasicTest, ConstructorSetsDefaults) {
    EXPECT_GT(parser->getMemoryLimit(), 0);
    EXPECT_GT(parser->getMaxPacketSize(), 0);
    EXPECT_FALSE(parser->isProcessing());
}

TEST_F(ActionPacketParserBasicTest, MemoryLimitConfiguration) {
    size_t limit = 10 * 1024 * 1024; // 10MB
    parser->setMemoryLimit(limit);
    EXPECT_EQ(parser->getMemoryLimit(), limit);
}

TEST_F(ActionPacketParserBasicTest, MaxPacketSizeConfiguration) {
    size_t size = 5 * 1024 * 1024; // 5MB
    parser->setMaxPacketSize(size);
    EXPECT_EQ(parser->getMaxPacketSize(), size);
}

TEST_F(ActionPacketParserBasicTest, PacketHandlerConfiguration) {
    bool handlerCalled = false;
    string receivedPacket;
    
    parser->setPacketHandler([&handlerCalled, &receivedPacket](const string& packet) {
        handlerCalled = true;
        receivedPacket = packet;
    });
    
    string testPacket = TestDataGenerator::createSimpleActionPacket("test");
    parser->processChunk(testPacket.c_str(), testPacket.size());
    
    EXPECT_TRUE(handlerCalled);
    EXPECT_EQ(receivedPacket, testPacket);
}

/**
 * Streaming processing tests
 */
class ActionPacketParserStreamingTest : public ::testing::Test {
protected:
    void SetUp() override {
        client = make_unique<MockMegaClient>();
        parser = make_unique<ActionPacketParser>(*client);
        
        // Set up packet handler
        parser->setPacketHandler([this](const string& packet) {
            processedPackets.push_back(packet);
        });
    }
    
    unique_ptr<MockMegaClient> client;
    unique_ptr<ActionPacketParser> parser;
    vector<string> processedPackets;
};

TEST_F(ActionPacketParserStreamingTest, ProcessSinglePacket) {
    string packet = TestDataGenerator::createSimpleActionPacket("test");
    string data = "[" + packet + "]";
    
    size_t processed = parser->processChunk(data.c_str(), data.size());
    
    EXPECT_EQ(processed, data.size());
    EXPECT_EQ(processedPackets.size(), 1);
    EXPECT_EQ(processedPackets[0], packet);
}

TEST_F(ActionPacketParserStreamingTest, ProcessMultiplePackets) {
    vector<string> packets = {
        TestDataGenerator::createSimpleActionPacket("test1", 1),
        TestDataGenerator::createSimpleActionPacket("test2", 2),
        TestDataGenerator::createSimpleActionPacket("test3", 3)
    };
    
    string data = TestDataGenerator::createActionPacketSequence(packets);
    size_t processed = parser->processChunk(data.c_str(), data.size());
    
    EXPECT_EQ(processed, data.size());
    EXPECT_EQ(processedPackets.size(), 3);
    for (size_t i = 0; i < packets.size(); ++i) {
        EXPECT_EQ(processedPackets[i], packets[i]);
    }
}

TEST_F(ActionPacketParserStreamingTest, ProcessChunkedData) {
    string packet = TestDataGenerator::createSimpleActionPacket("chunked");
    string data = "[" + packet + "]";
    
    // Process in two chunks
    size_t midpoint = data.size() / 2;
    
    size_t processed1 = parser->processChunk(data.c_str(), midpoint);
    EXPECT_EQ(processedPackets.size(), 0); // Should not complete yet
    
    size_t processed2 = parser->processChunk(data.c_str() + midpoint, data.size() - midpoint);
    
    EXPECT_EQ(processed1 + processed2, data.size());
    EXPECT_EQ(processedPackets.size(), 1);
    EXPECT_EQ(processedPackets[0], packet);
}

/**
 * Memory limit tests
 */
class ActionPacketParserMemoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        client = make_unique<MockMegaClient>();
        parser = make_unique<ActionPacketParser>(*client);
        
        // Set very low memory limit for testing
        parser->setMemoryLimit(1024); // 1KB
        parser->setMaxPacketSize(512); // 512 bytes
        
        parser->setErrorHandler([this](const string& error, bool recovered) {
            errors.push_back(error);
            errorRecovered = recovered;
        });
    }
    
    unique_ptr<MockMegaClient> client;
    unique_ptr<ActionPacketParser> parser;
    vector<string> errors;
    bool errorRecovered = false;
};

TEST_F(ActionPacketParserMemoryTest, HandlesMemoryLimitExceeded) {
    // Create packet larger than memory limit
    string packet = TestDataGenerator::createLargeActionPacket("large", 2048);
    string data = "[" + packet + "]";
    
    size_t processed = parser->processChunk(data.c_str(), data.size());
    
    // Should handle gracefully
    EXPECT_GT(errors.size(), 0);
    EXPECT_TRUE(errors[0].find("memory") != string::npos);
}

TEST_F(ActionPacketParserMemoryTest, HandlesPacketSizeExceeded) {
    // Create packet larger than max packet size
    string packet = TestDataGenerator::createLargeActionPacket("oversized", 1024);
    string data = "[" + packet + "]";
    
    size_t processed = parser->processChunk(data.c_str(), data.size());
    
    // Should handle gracefully
    EXPECT_GT(errors.size(), 0);
}

/**
 * Tree element streaming tests
 */
class ActionPacketParserTreeTest : public ::testing::Test {
protected:
    void SetUp() override {
        client = make_unique<MockMegaClient>();
        parser = make_unique<ActionPacketParser>(*client);
        
        parser->setPacketHandler([this](const string& packet) {
            processedPackets.push_back(packet);
        });
    }
    
    unique_ptr<MockMegaClient> client;
    unique_ptr<ActionPacketParser> parser;
    vector<string> processedPackets;
};

TEST_F(ActionPacketParserTreeTest, ProcessSmallTreeElement) {
    string packet = TestDataGenerator::createTreeActionPacket(10); // Small tree
    string data = "[" + packet + "]";
    
    size_t processed = parser->processChunk(data.c_str(), data.size());
    
    EXPECT_EQ(processed, data.size());
    EXPECT_EQ(processedPackets.size(), 1);
}

TEST_F(ActionPacketParserTreeTest, ProcessLargeTreeElement) {
    string packet = TestDataGenerator::createTreeActionPacket(1000); // Large tree
    string data = "[" + packet + "]";
    
    size_t processed = parser->processChunk(data.c_str(), data.size());
    
    EXPECT_EQ(processed, data.size());
    // Large tree should be handled (might be processed in chunks)
    EXPECT_GT(processedPackets.size(), 0);
}

/**
 * Error handling tests
 */
class ActionPacketParserErrorTest : public ::testing::Test {
protected:
    void SetUp() override {
        client = make_unique<MockMegaClient>();
        parser = make_unique<ActionPacketParser>(*client);
        
        parser->setErrorHandler([this](const string& error, bool recovered) {
            errors.push_back(error);
            errorRecovered = recovered;
        });
    }
    
    unique_ptr<MockMegaClient> client;
    unique_ptr<ActionPacketParser> parser;
    vector<string> errors;
    bool errorRecovered = false;
};

TEST_F(ActionPacketParserErrorTest, HandlesInvalidJSON) {
    string invalidJson = "[{invalid json}]";
    
    size_t processed = parser->processChunk(invalidJson.c_str(), invalidJson.size());
    
    // Should handle gracefully
    EXPECT_GT(errors.size(), 0);
}

TEST_F(ActionPacketParserErrorTest, HandlesIncompleteJSON) {
    string incompleteJson = "[{\"a\":\"test\",\"data\"";
    
    size_t processed = parser->processChunk(incompleteJson.c_str(), incompleteJson.size());
    
    // Should not error immediately - waiting for more data
    EXPECT_EQ(errors.size(), 0);
}

TEST_F(ActionPacketParserErrorTest, HandlesEmptyData) {
    string emptyData = "";
    
    size_t processed = parser->processChunk(emptyData.c_str(), emptyData.size());
    
    EXPECT_EQ(processed, 0);
    EXPECT_EQ(errors.size(), 0);
}

/**
 * Performance tests
 */
class ActionPacketParserPerformanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        client = make_unique<MockMegaClient>();
        parser = make_unique<ActionPacketParser>(*client);
        
        // Configure for performance testing
        parser->setMemoryLimit(100 * 1024 * 1024); // 100MB
        parser->setMaxPacketSize(10 * 1024 * 1024); // 10MB
        
        parser->setPacketHandler([this](const string& packet) {
            processedPackets.push_back(packet);
        });
    }
    
    unique_ptr<MockMegaClient> client;
    unique_ptr<ActionPacketParser> parser;
    vector<string> processedPackets;
};

TEST_F(ActionPacketParserPerformanceTest, ProcessLargeSequence) {
    // Create large sequence of packets
    vector<string> packets;
    for (int i = 0; i < 1000; ++i) {
        packets.push_back(TestDataGenerator::createSimpleActionPacket("perf", i));
    }
    
    string data = TestDataGenerator::createActionPacketSequence(packets);
    
    auto start = chrono::high_resolution_clock::now();
    size_t processed = parser->processChunk(data.c_str(), data.size());
    auto end = chrono::high_resolution_clock::now();
    
    auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);
    
    EXPECT_EQ(processed, data.size());
    EXPECT_EQ(processedPackets.size(), 1000);
    
    // Should process reasonably quickly (less than 1 second for 1000 packets)
    EXPECT_LT(duration.count(), 1000);
    
    cout << "Processed 1000 packets in " << duration.count() << "ms" << endl;
}

TEST_F(ActionPacketParserPerformanceTest, ProcessLargeTreePerformance) {
    // Create large tree packet
    string packet = TestDataGenerator::createTreeActionPacket(10000); // 10K nodes
    string data = "[" + packet + "]";
    
    auto start = chrono::high_resolution_clock::now();
    size_t processed = parser->processChunk(data.c_str(), data.size());
    auto end = chrono::high_resolution_clock::now();
    
    auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);
    
    EXPECT_EQ(processed, data.size());
    EXPECT_GT(processedPackets.size(), 0);
    
    cout << "Processed large tree (" << data.size() << " bytes) in " 
         << duration.count() << "ms" << endl;
}

/**
 * Statistics tests
 */
class ActionPacketParserStatsTest : public ::testing::Test {
protected:
    void SetUp() override {
        client = make_unique<MockMegaClient>();
        parser = make_unique<ActionPacketParser>(*client);
        
        parser->setPacketHandler([this](const string& packet) {
            processedPackets.push_back(packet);
        });
    }
    
    unique_ptr<MockMegaClient> client;
    unique_ptr<ActionPacketParser> parser;
    vector<string> processedPackets;
};

TEST_F(ActionPacketParserStatsTest, TracksBasicStatistics) {
    // Process some packets
    vector<string> packets;
    for (int i = 0; i < 10; ++i) {
        packets.push_back(TestDataGenerator::createSimpleActionPacket("stats", i));
    }
    
    string data = TestDataGenerator::createActionPacketSequence(packets);
    parser->processChunk(data.c_str(), data.size());
    
    auto stats = parser->getStats();
    EXPECT_EQ(stats.packetsProcessed, 10);
    EXPECT_GT(stats.bytesProcessed, 0);
    EXPECT_GT(stats.totalProcessingTime, 0);
}

TEST_F(ActionPacketParserStatsTest, TracksMemoryUsage) {
    // Process large packet to trigger memory tracking
    string packet = TestDataGenerator::createLargeActionPacket("memory", 1024);
    string data = "[" + packet + "]";
    
    parser->processChunk(data.c_str(), data.size());
    
    auto stats = parser->getStats();
    EXPECT_GT(stats.memoryPeak, 0);
}

/**
 * Integration tests
 */
class ActionPacketParserIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        client = make_unique<MockMegaClient>();
        
        // Test integration with MegaClient
        client->enableStreamingActionPackets(true);
        EXPECT_TRUE(client->streamingActionPacketsEnabled());
        
        client->setActionPacketHandler([this](const string& packet) {
            processedPackets.push_back(packet);
        });
        
        client->setActionPacketErrorHandler([this](const string& error, bool recovered) {
            errors.push_back(error);
            errorRecovered = recovered;
        });
    }
    
    unique_ptr<MockMegaClient> client;
    vector<string> processedPackets;
    vector<string> errors;
    bool errorRecovered = false;
};

TEST_F(ActionPacketParserIntegrationTest, MegaClientIntegration) {
    // Test that MegaClient properly integrates with parser
    EXPECT_TRUE(client->streamingActionPacketsEnabled());
    
    // Test enabling/disabling
    client->enableStreamingActionPackets(false);
    EXPECT_FALSE(client->streamingActionPacketsEnabled());
    
    client->enableStreamingActionPackets(true);
    EXPECT_TRUE(client->streamingActionPacketsEnabled());
}

TEST_F(ActionPacketParserIntegrationTest, EndToEndStreaming) {
    // Simulate end-to-end streaming scenario
    string packet = TestDataGenerator::createSimpleActionPacket("integration");
    string data = "[" + packet + "]";
    
    // Simulate streaming data processing
    size_t processed = client->procsc_streaming(data.c_str(), data.size());
    
    EXPECT_EQ(processed, data.size());
    EXPECT_EQ(processedPackets.size(), 1);
    EXPECT_EQ(processedPackets[0], packet);
}

/**
 * Edge case tests
 */
class ActionPacketParserEdgeCaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        client = make_unique<MockMegaClient>();
        parser = make_unique<ActionPacketParser>(*client);
        
        parser->setPacketHandler([this](const string& packet) {
            processedPackets.push_back(packet);
        });
        
        parser->setErrorHandler([this](const string& error, bool recovered) {
            errors.push_back(error);
        });
    }
    
    unique_ptr<MockMegaClient> client;
    unique_ptr<ActionPacketParser> parser;
    vector<string> processedPackets;
    vector<string> errors;
};

TEST_F(ActionPacketParserEdgeCaseTest, EmptyPacketSequence) {
    string data = "[]";
    
    size_t processed = parser->processChunk(data.c_str(), data.size());
    
    EXPECT_EQ(processed, data.size());
    EXPECT_EQ(processedPackets.size(), 0);
    EXPECT_EQ(errors.size(), 0);
}

TEST_F(ActionPacketParserEdgeCaseTest, SingleByteChunks) {
    string packet = TestDataGenerator::createSimpleActionPacket("single");
    string data = "[" + packet + "]";
    
    // Process one byte at a time
    size_t totalProcessed = 0;
    for (size_t i = 0; i < data.size(); ++i) {
        size_t processed = parser->processChunk(data.c_str() + i, 1);
        totalProcessed += processed;
    }
    
    EXPECT_EQ(totalProcessed, data.size());
    EXPECT_EQ(processedPackets.size(), 1);
    EXPECT_EQ(processedPackets[0], packet);
}

TEST_F(ActionPacketParserEdgeCaseTest, VeryLargePacket) {
    // Create packet larger than default limits
    string packet = TestDataGenerator::createLargeActionPacket("huge", 50 * 1024 * 1024); // 50MB
    string data = "[" + packet + "]";
    
    size_t processed = parser->processChunk(data.c_str(), data.size());
    
    // Should handle gracefully (might trigger memory limit)
    // The exact behavior depends on configured limits
}

TEST_F(ActionPacketParserEdgeCaseTest, NestedJSONStructures) {
    string nestedPacket = R"({"a":"nested","data":{"level1":{"level2":{"level3":"deep"}}}})";
    string data = "[" + nestedPacket + "]";
    
    size_t processed = parser->processChunk(data.c_str(), data.size());
    
    EXPECT_EQ(processed, data.size());
    EXPECT_EQ(processedPackets.size(), 1);
    EXPECT_EQ(processedPackets[0], nestedPacket);
}

/**
 * Main test runner
 */
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
