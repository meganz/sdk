/**
 * (c) 2025 by Mega Limited, Wellsford, New Zealand
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
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "mega/command.h"
#include "mega/megaapp.h"
#include "mega/megaclient.h"
#include "mega/json.h"

using namespace mega;

class MockMegaClient : public MegaClient
{
public:
    MockMegaClient(MegaApp* a,
                   std::shared_ptr<Waiter> w,
                   HttpIO* h,
                   DbAccess* d,
                   GfxProc* g,
                   const char* k,
                   const char* u,
                   unsigned workerThreadCount,
                   MegaClient::ClientType clientType)
        : MegaClient(a, w, h, d, g, k, u, workerThreadCount, clientType)
    {
    }

    int readUserCount = 0;          // Count of user parsing operations
    int readNodeCount = 0;          // Count of node parsing operations
    int processTChunkCount = 0;     // Count of T-element chunk processing operations
    bool processTChunkResult = true;// Mock result for T-element processing (success by default)
    std::unordered_map<int64_t, size_t> largeTElemProcessed;

    // Mock implementation of readuser: Parses a single user change
    int readuser(JSON* json, bool isActionPacket, int type = 0)
    {
        readUserCount++;
        // Skip the content of the user object to avoid parsing errors
        if (json->enterobject())
        {
            while (json->getnameid() != EOO)
            {
                // Skip values using a combination of existing JSON class methods
                if (json->enterobject())
                {
                    json->leaveobject();
                }
                else if (json->enterarray())
                {
                    json->leavearray();
                }
                else if (!json->skipnullvalue())
                {
                    json->getvalue();
                }
            }
            json->leaveobject();
        }
        return 1; // Simulate successful parsing
    }

    // Mock implementation of readnode: Parses a single node change
    int readnode(JSON* json, int type, int putType, NewNode* newNode, bool checkQuota, bool isActionPacket,
                 NodeManager::MissingParentNodes& missingParents, handle& parentHandle,
                 std::vector<handle>* deletedNodes = nullptr, std::vector<Node*>* modifiedNodes = nullptr,
                 std::set<handle>* movedNodes = nullptr)
    {
        readNodeCount++;
        // Skip the content of the node object
        if (json->enterobject())
        {
            while (json->getnameid() != EOO)
            {
                if (json->enterobject())
                {
                    json->leaveobject();
                }
                else if (json->enterarray())
                {
                    json->leavearray();
                }
                else if (!json->skipnullvalue())
                {
                    json->getvalue();
                }
            }
            json->leaveobject();
        }
        return 1; // Simulate successful parsing
    }

    // Mock implementation of processLargeTElementChunk: Counts T-element processing times
    bool processLargeTElementChunk(int64_t, size_t, const char*, size_t)
    {
        processTChunkCount++;
        return processTChunkResult;
    }
};

std::shared_ptr<MockMegaClient> createMockClient(
    mega::MegaApp& app,
    bool processTChunkResult = true,
    const std::unordered_map<int64_t, size_t>& largeTElemProcessed = {})
{
    struct HttpIo : mega::HttpIO
    {
        void addevents(mega::Waiter*, int) override {}
        void post(struct mega::HttpReq*, const char* = NULL, unsigned = 0) override {}
        void cancel(mega::HttpReq*) override {}
        m_off_t postpos(void*) override { return {}; }
        bool doio(void) override { return {}; }
        void setuseragent(std::string*) override {}
    };

    auto httpio = new HttpIo;

    auto deleter = [httpio](MockMegaClient* client)
    {
        delete client;
        delete httpio;
    };

    auto waiter = std::make_shared<WAIT_CLASS>();

    std::shared_ptr<MockMegaClient> client{new MockMegaClient{&app,
                                                              waiter,
                                                              httpio,
                                                              nullptr,
                                                              nullptr,
                                                              "XXX",
                                                              "unit_test",
                                                              1,
                                                              MegaClient::ClientType::DEFAULT
        }, deleter};

    client->processTChunkResult = processTChunkResult;
    client->largeTElemProcessed = largeTElemProcessed;
    return client;
}

void processChunks(JSONSplitter& splitter, CommandProcessActionPackets& cmd, const std::vector<std::string>& chunks)
{
    for (const auto& chunk : chunks)
    {
        splitter.processChunk(&cmd.mFilters, chunk.c_str());
    }
}

// Test Case 1: Normal chunked parsing (3 users + 5 nodes, transmitted in 3 chunks)
TEST(CommandProcessActionPacketsTest, NormalChunkParsing_U_F_Array)
{
    mega::MegaApp app;
    auto mockClient = createMockClient(app);
    CommandProcessActionPackets cmd(mockClient.get(), 1, false);
    JSONSplitter splitter;

    std::vector<std::string> chunks = {
        R"({"ap":{"apId":"ap_test","timestamp":1719865200000,"u":[)",
        R"({"userId":"u1","userActionType":"USER_INFO_UPDATE"},{"userId":"u2","userActionType":"USER_PERMISSION_CHANGE"},{"userId":"u3","userActionType":"USER_STATUS_CHANGE"}],"f":[)",
        R"({"nodeId":"n1","nodeType":"FOLDER"},{"nodeId":"n2","nodeType":"FILE"},{"nodeId":"n3","nodeType":"FILE"},{"nodeId":"n4","nodeType":"FILE"},{"nodeId":"n5","nodeType":"FILE"}]}})"
    };

    processChunks(splitter, cmd, chunks);

    EXPECT_EQ(mockClient->readUserCount, 3);    // 3 users parsed successfully
    EXPECT_EQ(mockClient->readNodeCount, 5);    // 5 nodes parsed successfully
    EXPECT_EQ(mockClient->processTChunkCount, 0); // No T-elements, processing count = 0
    EXPECT_FALSE(splitter.hasFailed());         // No parsing errors
}

// Test Case 2: Chunked parsing of large T-element (5MB, split into 3 chunks)
TEST(CommandProcessActionPacketsTest, LargeTElemChunkParsing_5MB)
{
    mega::MegaApp app;
    std::unordered_map<int64_t, size_t> largeTElemMap = {{1001, 5242880}}; // Simulate 5MB processed successfully
    auto mockClient = createMockClient(app, true, largeTElemMap);
    CommandProcessActionPackets cmd(mockClient.get(), 2, false);
    JSONSplitter splitter;

    std::vector<std::string> chunks;
    // Chunk 1: T-element metadata + 1MB content (pad to 1MB total)
    std::string chunk1 = R"({"ap":{"apId":"ap_test_t","timestamp":1719865200000,"largeTElement":{"t_id":1001,"t_total":5242880,"t":"[")";
    chunk1.append(std::max(0, static_cast<int>(1024 * 1024 - chunk1.size())), 'x');
    chunks.push_back(chunk1);
    // Chunk 2: 2MB of pure content
    chunks.push_back(std::string(2 * 1024 * 1024, 'y'));
    // Chunk 3: 2MB content + closing delimiters (reserve 20 bytes for JSON closing structure)
    std::string chunk3 = std::string(2 * 1024 * 1024 - 20, 'z');
    chunk3.append("]}]}"); // Close "t" string, largeTElement object, and ap object
    chunks.push_back(chunk3);

    processChunks(splitter, cmd, chunks);

    EXPECT_EQ(mockClient->processTChunkCount, 5); // Process once per 1MB, 5MB = 5 times
    EXPECT_EQ(mockClient->readUserCount, 0);      // No user changes
    EXPECT_EQ(mockClient->readNodeCount, 0);      // No node changes
    EXPECT_FALSE(splitter.hasFailed());           // Parsing successful
}

// Test Case 3: Large T-element missing "t_id" (error handling scenario)
TEST(CommandProcessActionPacketsTest, LargeTElemMissingTId_Error)
{
    mega::MegaApp app;
    auto mockClient = createMockClient(app);
    CommandProcessActionPackets cmd(mockClient.get(), 3, false);
    JSONSplitter splitter;

    std::vector<std::string> chunks = {
        R"({"ap":{"largeTElement":{"t_total":1048576,"t":"[{\"nodeId\":\"n1\",\"sha256\":\"xxx\"}]"}}})"
    };

    processChunks(splitter, cmd, chunks);

    EXPECT_EQ(mockClient->processTChunkCount, 0); // T-element processing not triggered
    EXPECT_TRUE(splitter.hasFailed());            // Parsing failed (missing "t_id")
}

// Test Case 4: Incomplete chunked data (T-element size mismatch error)
TEST(CommandProcessActionPacketsTest, LargeTElemSizeMismatch_Error)
{
    mega::MegaApp app;
    std::unordered_map<int64_t, size_t> largeTElemMap = {{1002, 524288}}; // Only 512KB processed
    auto mockClient = createMockClient(app, true, largeTElemMap);
    CommandProcessActionPackets cmd(mockClient.get(), 4, false);
    JSONSplitter splitter;

    std::vector<std::string> chunks;
    std::string chunk = R"({"ap":{"largeTElement":{"t_id":1002,"t_total":1048576,"t":"[")";

    chunk.append(std::max(0, static_cast<int>(512 * 1024 - chunk.size())), 'a');
    chunk.append("]}})"); // Close JSON structure
    chunks.push_back(chunk);


    processChunks(splitter, cmd, chunks);

    EXPECT_EQ(mockClient->processTChunkCount, 1); // Processed once (512KB < 1MB threshold)
    EXPECT_TRUE(splitter.hasFailed());            // Parsing failed (size mismatch)
}