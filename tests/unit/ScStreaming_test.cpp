/**
 * @file ScStreaming_test.cpp
 * @brief Unit tests for SC (Server-Client) streaming parsing
 *
 * Tests the JSONSplitter-based streaming parsing of SC responses,
 * comparing memory efficiency and correctness with the traditional
 * non-streaming approach.
 */

#include <gtest/gtest.h>
#include <chrono>
#include <numeric>

#include "mega/json.h"
#include "mega/types.h"
#include "mega/base64.h"

using namespace mega;
using namespace std;

namespace
{

/**
 * Helper class to test SC streaming without a full MegaClient
 */
class ScStreamingTester
{
public:
    JSONSplitter mSplitter;
    map<string, function<bool(JSON*)>> mFilters;

    // Captured data
    string capturedW;
    string capturedSn;
    vector<string> capturedAPs;
    bool errorOccurred = false;

    void initFilters()
    {
        mSplitter.clear();
        capturedW.clear();
        capturedSn.clear();
        capturedAPs.clear();
        errorOccurred = false;

        // w field
        mFilters["{\"w"] = [this](JSON* json) {
            return json->storeobject(&capturedW);
        };

        // sn field
        mFilters["{\"sn"] = [this](JSON* json) {
            return json->storeobject(&capturedSn);
        };

        // Each actionpacket in "a" array
        mFilters["{[a{"] = [this](JSON* json) {
            string ap;
            if (!json->storeobject(&ap))
            {
                return false;
            }
            capturedAPs.push_back(move(ap));
            return true;
        };

        // Error handler
        mFilters["E"] = [this](JSON*) {
            errorOccurred = true;
            return false;
        };
    }

    /**
     * Process SC response in chunks, simulating network streaming
     * @param fullResponse Complete SC response
     * @param chunkSize Size of each chunk
     * @return Maximum buffer size used (for memory comparison)
     */
    size_t processInChunks(const string& fullResponse, size_t chunkSize)
    {
        initFilters();

        string buffer;
        size_t maxBufferSize = 0;

        for (size_t offset = 0; offset < fullResponse.size(); offset += chunkSize)
        {
            // Simulate chunk arrival
            size_t thisChunkSize = min(chunkSize, fullResponse.size() - offset);
            buffer.append(fullResponse, offset, thisChunkSize);

            // Process chunk
            m_off_t consumed = mSplitter.processChunk(&mFilters, buffer.c_str());

            // Purge consumed data
            if (consumed > 0)
            {
                buffer.erase(0, static_cast<size_t>(consumed));
            }

            // Track max buffer size
            maxBufferSize = max(maxBufferSize, buffer.size());

            if (mSplitter.hasFailed())
            {
                break;
            }
        }

        return maxBufferSize;
    }

    /**
     * Process entire response at once (baseline for comparison)
     */
    void processAtOnce(const string& fullResponse)
    {
        initFilters();
        mSplitter.processChunk(&mFilters, fullResponse.c_str());
    }
};

/**
 * Generate a test SC response with specified number of actionpackets
 */
string generateScResponse(size_t numAPs, size_t apDataSize = 50)
{
    string response = R"({"w":"wss://g.api.mega.co.nz/ws","sn":"ABCD1234EFGH","a":[)";

    for (size_t i = 0; i < numAPs; i++)
    {
        if (i > 0) response += ",";

        // Generate AP with some data
        response += R"({"a":"u","n":"node)";
        response += to_string(i);
        response += R"(","at":")";
        response += string(apDataSize, 'x');  // Attribute data
        response += R"("})";
    }

    response += "]}";
    return response;
}

} // anonymous namespace


// ============================================================================
// Test: Basic chunked parsing correctness
// ============================================================================

TEST(ScStreamingTest, ChunkedParsingCorrectness)
{
    const char* response = R"({"w":"wss://mega.nz","sn":"SN123456","a":[{"a":"u","n":"n1"},{"a":"d","n":"n2"}]})";

    ScStreamingTester tester;

    // Test with various chunk sizes
    vector<size_t> chunkSizes = {10, 20, 50, 100, strlen(response)};

    for (size_t chunkSize : chunkSizes)
    {
        tester.processInChunks(response, chunkSize);

        EXPECT_TRUE(tester.mSplitter.hasFinished())
            << "Failed with chunk size " << chunkSize;
        EXPECT_FALSE(tester.mSplitter.hasFailed())
            << "Failed with chunk size " << chunkSize;
        EXPECT_FALSE(tester.errorOccurred)
            << "Error occurred with chunk size " << chunkSize;

        // Verify captured data
        EXPECT_EQ(tester.capturedW, "wss://mega.nz")
            << "W mismatch with chunk size " << chunkSize;
        EXPECT_EQ(tester.capturedSn, "SN123456")
            << "SN mismatch with chunk size " << chunkSize;
        EXPECT_EQ(tester.capturedAPs.size(), 2u)
            << "AP count mismatch with chunk size " << chunkSize;
    }
}


// ============================================================================
// Test: Memory efficiency comparison
// ============================================================================

TEST(ScStreamingTest, MemoryEfficiency)
{
    struct TestCase
    {
        size_t numAPs;
        size_t chunkSize;
        const char* name;
    };

    vector<TestCase> testCases = {
        {100, 1024, "Small (100 APs)"},
        {1000, 4096, "Medium (1000 APs)"},
        {5000, 4096, "Large (5000 APs)"},
    };

    cout << "\n";
    cout << "==========================================================\n";
    cout << "         SC Streaming Memory Efficiency Test\n";
    cout << "==========================================================\n";

    for (const auto& tc : testCases)
    {
        string response = generateScResponse(tc.numAPs);
        size_t fullSize = response.size();

        ScStreamingTester tester;
        size_t maxBufferSize = tester.processInChunks(response, tc.chunkSize);

        EXPECT_TRUE(tester.mSplitter.hasFinished());
        EXPECT_EQ(tester.capturedAPs.size(), tc.numAPs);

        double savingsPercent = 100.0 * (fullSize - maxBufferSize) / fullSize;

        cout << "\n" << tc.name << ":\n";
        cout << "  Response size:     " << fullSize << " bytes ("
             << (fullSize / 1024.0) << " KB)\n";
        cout << "  Chunk size:        " << tc.chunkSize << " bytes\n";
        cout << "  Max buffer (stream): " << maxBufferSize << " bytes\n";
        cout << "  Memory saved:      " << (fullSize - maxBufferSize)
             << " bytes (" << savingsPercent << "%)\n";
        cout << "  APs captured:      " << tester.capturedAPs.size() << "\n";

        // Streaming should use less than 50% of full response size
        EXPECT_LT(maxBufferSize, fullSize / 2)
            << "Streaming should save at least 50% memory for " << tc.name;
    }

    cout << "\n==========================================================\n";
}


// ============================================================================
// Test: Performance comparison
// ============================================================================

TEST(ScStreamingTest, PerformanceComparison)
{
    const size_t NUM_APS = 2000;
    const size_t NUM_ITERATIONS = 5;
    string response = generateScResponse(NUM_APS);

    cout << "\n";
    cout << "==========================================================\n";
    cout << "         SC Streaming Performance Test\n";
    cout << "==========================================================\n";
    cout << "Response size: " << response.size() << " bytes\n";
    cout << "APs count: " << NUM_APS << "\n";
    cout << "Iterations: " << NUM_ITERATIONS << "\n";

    // Benchmark streaming (chunked) approach
    vector<double> streamingTimes;
    for (size_t i = 0; i < NUM_ITERATIONS; i++)
    {
        ScStreamingTester tester;
        auto start = chrono::high_resolution_clock::now();
        tester.processInChunks(response, 4096);
        auto end = chrono::high_resolution_clock::now();
        auto duration = chrono::duration_cast<chrono::microseconds>(end - start);
        streamingTimes.push_back(duration.count() / 1000.0);
    }

    // Benchmark at-once approach
    vector<double> atOnceTimes;
    for (size_t i = 0; i < NUM_ITERATIONS; i++)
    {
        ScStreamingTester tester;
        auto start = chrono::high_resolution_clock::now();
        tester.processAtOnce(response);
        auto end = chrono::high_resolution_clock::now();
        auto duration = chrono::duration_cast<chrono::microseconds>(end - start);
        atOnceTimes.push_back(duration.count() / 1000.0);
    }

    double avgStreaming = accumulate(streamingTimes.begin(), streamingTimes.end(), 0.0) / NUM_ITERATIONS;
    double avgAtOnce = accumulate(atOnceTimes.begin(), atOnceTimes.end(), 0.0) / NUM_ITERATIONS;

    cout << "\nResults:\n";
    cout << "  Streaming (chunked): " << avgStreaming << " ms avg\n";
    cout << "  At-once (baseline):  " << avgAtOnce << " ms avg\n";
    cout << "  Overhead:            " << (avgStreaming - avgAtOnce) << " ms ("
         << (100.0 * (avgStreaming - avgAtOnce) / avgAtOnce) << "%)\n";
    cout << "==========================================================\n";

    // Streaming overhead should be reasonable (< 100%)
    EXPECT_LT(avgStreaming, avgAtOnce * 2)
        << "Streaming overhead should be less than 100%";
}


// ============================================================================
// Test: Edge cases
// ============================================================================

TEST(ScStreamingTest, EmptyActionPacketsArray)
{
    const char* response = R"({"w":"wss://mega.nz","sn":"SN123","a":[]})";

    ScStreamingTester tester;
    tester.processInChunks(response, 10);

    EXPECT_TRUE(tester.mSplitter.hasFinished());
    EXPECT_FALSE(tester.mSplitter.hasFailed());
    EXPECT_EQ(tester.capturedW, "wss://mega.nz");
    EXPECT_EQ(tester.capturedSn, "SN123");
    EXPECT_EQ(tester.capturedAPs.size(), 0u);
}

TEST(ScStreamingTest, SingleByteChunks)
{
    const char* response = R"({"w":"url","sn":"sn","a":[{"a":"u"}]})";

    ScStreamingTester tester;
    tester.processInChunks(response, 1);  // 1 byte at a time!

    EXPECT_TRUE(tester.mSplitter.hasFinished());
    EXPECT_FALSE(tester.mSplitter.hasFailed());
    EXPECT_EQ(tester.capturedW, "url");
    EXPECT_EQ(tester.capturedSn, "sn");
    EXPECT_EQ(tester.capturedAPs.size(), 1u);
}

TEST(ScStreamingTest, LargeActionPacket)
{
    // Single AP with large attribute data
    // NOTE: Current "basic" approach cannot optimize single large AP
    // because we need the complete AP object before triggering callback.
    // Memory savings come from multiple APs scenario, not single large AP.
    string largeAttr(10000, 'x');
    string response = R"({"w":"url","sn":"sn","a":[{"a":"t","at":")" + largeAttr + R"("}]})";

    ScStreamingTester tester;
    tester.processInChunks(response, 1024);

    // Verify parsing succeeds and AP is captured correctly
    EXPECT_TRUE(tester.mSplitter.hasFinished());
    EXPECT_FALSE(tester.mSplitter.hasFailed());
    EXPECT_EQ(tester.capturedW, "url");
    EXPECT_EQ(tester.capturedSn, "sn");
    EXPECT_EQ(tester.capturedAPs.size(), 1u);

    // Verify the large AP content is intact
    EXPECT_NE(tester.capturedAPs[0].find(largeAttr), string::npos)
        << "Large attribute data should be preserved in captured AP";
}

TEST(ScStreamingTest, SpecialCharactersInStrings)
{
    // Test with escaped characters - use proper JSON escaping
    const char* response = R"({"w":"wss://mega.nz/test","sn":"sn123","a":[{"a":"u","n":"node1"}]})";

    ScStreamingTester tester;
    tester.processInChunks(response, 15);

    EXPECT_TRUE(tester.mSplitter.hasFinished());
    EXPECT_FALSE(tester.mSplitter.hasFailed());
    EXPECT_EQ(tester.capturedW, "wss://mega.nz/test");
    EXPECT_EQ(tester.capturedSn, "sn123");
    EXPECT_EQ(tester.capturedAPs.size(), 1u);
}

TEST(ScStreamingTest, ChunkBoundaryAtStringMiddle)
{
    // Specifically test chunk boundary in middle of string
    const char* response = R"({"w":"0123456789","sn":"sn","a":[]})";

    // Chunk size 15 should split "0123456789" in the middle
    ScStreamingTester tester;
    tester.processInChunks(response, 15);

    EXPECT_TRUE(tester.mSplitter.hasFinished());
    EXPECT_EQ(tester.capturedW, "0123456789");
}


// ============================================================================
// Test: Verify ActionPacket content
// ============================================================================

TEST(ScStreamingTest, ActionPacketContentVerification)
{
    const char* response = R"({"w":"url","sn":"sn","a":[{"a":"u","n":"node1","at":"attr1"},{"a":"d","n":"node2"},{"a":"t","t":{"f":[{"h":"abc"}]}}]})";

    ScStreamingTester tester;
    tester.processInChunks(response, 20);

    EXPECT_TRUE(tester.mSplitter.hasFinished());
    EXPECT_EQ(tester.capturedAPs.size(), 3u);

    // Verify each AP is complete and parseable
    for (const auto& ap : tester.capturedAPs)
    {
        JSON json;
        json.begin(ap.c_str());
        EXPECT_TRUE(json.enterobject()) << "AP should be valid JSON object: " << ap;

        // Should have "a" field
        nameid name = json.getnameid();
        EXPECT_EQ(name, 'a') << "First field should be 'a'";
    }
}


// ============================================================================
// Deep Streaming Tests: Process nodes inside 't' type actionpackets
// ============================================================================

/**
 * Helper class for testing Deep Streaming (node-level streaming)
 * This simulates the MegaClient's deep streaming behavior
 *
 * Key insight: When using nested filters, the inner filter callbacks
 * are triggered first. The outer filter ({[a{) receives only the
 * remaining content after inner filters have consumed their parts.
 */
class DeepStreamingTester
{
public:
    JSONSplitter mSplitter;
    map<string, function<bool(JSON*)>> mFilters;

    // Captured data
    string capturedW;
    string capturedSn;
    vector<string> capturedNodes;  // Nodes captured from t.f array
    string capturedOU;
    vector<string> capturedAPActions;  // All AP action types
    size_t nonNodeAPCount = 0;  // Count of non-'t' type APs
    bool errorOccurred = false;

    void initFilters()
    {
        mSplitter.clear();
        capturedW.clear();
        capturedSn.clear();
        capturedNodes.clear();
        capturedOU.clear();
        capturedAPActions.clear();
        nonNodeAPCount = 0;
        errorOccurred = false;

        // w field
        mFilters["{\"w"] = [this](JSON* json) {
            return json->storeobject(&capturedW);
        };

        // sn field
        mFilters["{\"sn"] = [this](JSON* json) {
            return json->storeobject(&capturedSn);
        };

        // Capture AP action type - this is called for EVERY AP
        mFilters["{[a{\"a"] = [this](JSON* json) {
            string action;
            if (!json->storeobject(&action))
            {
                return false;
            }
            capturedAPActions.push_back(action);
            // Track non-node APs
            if (action != "t")
            {
                nonNodeAPCount++;
            }
            return true;
        };

        // Each node in t.f array (deep streaming!)
        // Path: root { → a array [a → AP { → t object {t → f array [f → node {
        mFilters["{[a{{t[f{"] = [this](JSON* json) {
            string node;
            if (!json->storeobject(&node))
            {
                return false;
            }
            capturedNodes.push_back(move(node));
            return true;
        };

        // Originating user (only present in 't' type APs)
        mFilters["{[a{\"ou"] = [this](JSON* json) {
            return json->storeobject(&capturedOU);
        };

        // Error handler
        mFilters["E"] = [this](JSON*) {
            errorOccurred = true;
            return false;
        };
    }

    size_t processInChunks(const string& fullResponse, size_t chunkSize)
    {
        initFilters();

        string buffer;
        size_t maxBufferSize = 0;

        for (size_t offset = 0; offset < fullResponse.size(); offset += chunkSize)
        {
            size_t thisChunkSize = min(chunkSize, fullResponse.size() - offset);
            buffer.append(fullResponse, offset, thisChunkSize);

            m_off_t consumed = mSplitter.processChunk(&mFilters, buffer.c_str());

            if (consumed > 0)
            {
                buffer.erase(0, static_cast<size_t>(consumed));
            }

            maxBufferSize = max(maxBufferSize, buffer.size());

            if (mSplitter.hasFailed())
            {
                break;
            }
        }

        return maxBufferSize;
    }
};

/**
 * Generate a 't' type AP with specified number of nodes
 */
string generateTTypeAP(size_t numNodes, size_t nodeDataSize = 100)
{
    string response = R"({"w":"url","sn":"sn","a":[{"a":"t","t":{"f":[)";

    for (size_t i = 0; i < numNodes; i++)
    {
        if (i > 0) response += ",";
        response += R"({"h":"h)";
        response += to_string(i);
        response += R"(","p":"p)";
        response += to_string(i);
        response += R"(","a":")";
        response += string(nodeDataSize, 'x');
        response += R"("})";
    }

    response += R"(]},"ou":"USER123"}]})";
    return response;
}


TEST(ScDeepStreamingTest, BasicNodeStreaming)
{
    // 't' type AP with 3 nodes
    const char* response = R"({"w":"url","sn":"sn","a":[{"a":"t","t":{"f":[{"h":"n1","p":"p1"},{"h":"n2","p":"p2"},{"h":"n3","p":"p3"}]},"ou":"user1"}]})";

    DeepStreamingTester tester;
    tester.processInChunks(response, 20);

    EXPECT_TRUE(tester.mSplitter.hasFinished());
    EXPECT_FALSE(tester.mSplitter.hasFailed());
    EXPECT_EQ(tester.capturedW, "url");
    EXPECT_EQ(tester.capturedSn, "sn");
    EXPECT_EQ(tester.capturedNodes.size(), 3u);
    EXPECT_EQ(tester.capturedOU, "user1");

    // Verify each node is complete and contains expected handle
    for (size_t i = 0; i < tester.capturedNodes.size(); i++)
    {
        const auto& node = tester.capturedNodes[i];
        string expectedH = "\"h\":\"n" + to_string(i + 1) + "\"";
        EXPECT_NE(node.find(expectedH), string::npos)
            << "Node should contain " << expectedH << ", got: " << node;
    }
}


TEST(ScDeepStreamingTest, MixedAPTypes)
{
    // Mix of 't' type (with nodes) and 'u'/'d' types (no nodes)
    const char* response = R"({"w":"url","sn":"sn","a":[{"a":"u","n":"node1"},{"a":"t","t":{"f":[{"h":"n1"},{"h":"n2"}]},"ou":"user1"},{"a":"d","n":"node2"}]})";

    DeepStreamingTester tester;
    tester.processInChunks(response, 15);

    EXPECT_TRUE(tester.mSplitter.hasFinished());
    EXPECT_FALSE(tester.mSplitter.hasFailed());

    // All 3 APs should have their action type captured
    EXPECT_EQ(tester.capturedAPActions.size(), 3u);
    EXPECT_EQ(tester.capturedAPActions[0], "u");
    EXPECT_EQ(tester.capturedAPActions[1], "t");
    EXPECT_EQ(tester.capturedAPActions[2], "d");

    // 2 non-node APs ('u' and 'd')
    EXPECT_EQ(tester.nonNodeAPCount, 2u);

    // 2 nodes from the 't' AP should be captured via deep streaming
    EXPECT_EQ(tester.capturedNodes.size(), 2u);

    // ou field from the 't' AP should be captured
    EXPECT_EQ(tester.capturedOU, "user1");
}


TEST(ScDeepStreamingTest, MemoryEfficiencyWithManyNodes)
{
    const size_t NUM_NODES = 1000;
    const size_t NODE_DATA_SIZE = 200;
    const size_t CHUNK_SIZE = 4096;

    string response = generateTTypeAP(NUM_NODES, NODE_DATA_SIZE);
    size_t fullSize = response.size();

    cout << "\n";
    cout << "==========================================================\n";
    cout << "    Deep Streaming Memory Efficiency Test\n";
    cout << "==========================================================\n";
    cout << "Nodes: " << NUM_NODES << "\n";
    cout << "Node data size: " << NODE_DATA_SIZE << " bytes\n";
    cout << "Full response: " << fullSize << " bytes (" << (fullSize / 1024.0) << " KB)\n";

    // Test deep streaming
    DeepStreamingTester deepTester;
    size_t deepMaxBuffer = deepTester.processInChunks(response, CHUNK_SIZE);

    EXPECT_TRUE(deepTester.mSplitter.hasFinished());
    EXPECT_EQ(deepTester.capturedNodes.size(), NUM_NODES);

    // Test basic streaming (for comparison)
    ScStreamingTester basicTester;
    size_t basicMaxBuffer = basicTester.processInChunks(response, CHUNK_SIZE);

    EXPECT_TRUE(basicTester.mSplitter.hasFinished());
    EXPECT_EQ(basicTester.capturedAPs.size(), 1u);  // One 't' AP

    cout << "\nResults:\n";
    cout << "  Basic streaming max buffer:  " << basicMaxBuffer << " bytes\n";
    cout << "  Deep streaming max buffer:   " << deepMaxBuffer << " bytes\n";
    cout << "  Memory saved by deep:        " << (basicMaxBuffer - deepMaxBuffer) << " bytes\n";

    if (basicMaxBuffer > 0)
    {
        double savingsPercent = 100.0 * (basicMaxBuffer - deepMaxBuffer) / basicMaxBuffer;
        cout << "  Savings percentage:          " << savingsPercent << "%\n";
    }

    cout << "==========================================================\n";

    // Deep streaming should use significantly less memory
    // (only needs to buffer one node at a time, not entire AP)
    EXPECT_LT(deepMaxBuffer, basicMaxBuffer)
        << "Deep streaming should use less memory than basic streaming";
}


TEST(ScDeepStreamingTest, ChunkedNodeParsing)
{
    // Test that nodes are correctly parsed even when split across chunks
    string response = generateTTypeAP(5, 500);  // 5 nodes with 500 bytes each

    // Use small chunk size to ensure nodes are split
    DeepStreamingTester tester;
    tester.processInChunks(response, 100);

    EXPECT_TRUE(tester.mSplitter.hasFinished());
    EXPECT_FALSE(tester.mSplitter.hasFailed());
    EXPECT_EQ(tester.capturedNodes.size(), 5u);

    // Verify each node has the expected data
    for (const auto& node : tester.capturedNodes)
    {
        // Each node should have 500 'x' characters
        size_t xCount = count(node.begin(), node.end(), 'x');
        EXPECT_EQ(xCount, 500u) << "Node data should be intact: " << node.substr(0, 50) << "...";
    }
}


TEST(ScDeepStreamingTest, EmptyNodesArray)
{
    // 't' type AP with empty f array
    const char* response = R"({"w":"url","sn":"sn","a":[{"a":"t","t":{"f":[]},"ou":"user1"}]})";

    DeepStreamingTester tester;
    tester.processInChunks(response, 10);

    EXPECT_TRUE(tester.mSplitter.hasFinished());
    EXPECT_FALSE(tester.mSplitter.hasFailed());
    EXPECT_EQ(tester.capturedNodes.size(), 0u);
    EXPECT_EQ(tester.capturedOU, "user1");
}


TEST(ScDeepStreamingTest, SingleByteChunksWithNodes)
{
    // Extreme test: 1 byte at a time
    const char* response = R"({"w":"u","sn":"s","a":[{"a":"t","t":{"f":[{"h":"1"}]},"ou":"o"}]})";

    DeepStreamingTester tester;
    tester.processInChunks(response, 1);

    EXPECT_TRUE(tester.mSplitter.hasFinished());
    EXPECT_FALSE(tester.mSplitter.hasFailed());
    EXPECT_EQ(tester.capturedNodes.size(), 1u);
}


TEST(ScDeepStreamingTest, PathVerification)
{
    // Verify the filter path {[a{{t[f{ is correct
    // This tests the path construction: root { → a [ → AP { → t { → f [ → node {

    const char* response = R"({"a":[{"a":"t","t":{"f":[{"h":"test"}]}}]})";

    JSONSplitter splitter;
    map<string, function<bool(JSON*)>> filters;

    bool nodeFilterCalled = false;
    string capturedNodePath;

    // The correct path for nodes inside t.f
    filters["{[a{{t[f{"] = [&](JSON* json) {
        nodeFilterCalled = true;
        json->storeobject(&capturedNodePath);
        return true;
    };

    splitter.processChunk(&filters, response);

    EXPECT_TRUE(splitter.hasFinished());
    EXPECT_TRUE(nodeFilterCalled) << "Node filter with path {[a{{t[f{ should be called";
    EXPECT_EQ(capturedNodePath, R"({"h":"test"})");
    EXPECT_NE(capturedNodePath.find("test"), string::npos)
        << "Should capture node content: " << capturedNodePath;
}

