/**
 * @brief Mega SDK test file for server implementations (TCP, HTTP)
 *
 * This test suite includes HTTP server functionality tests, stability tests,
 * and error handling tests. Tests include positive cases, negative cases,
 * edge cases, and stress tests.
 *
 * (c) 2025 by Mega Limited, New Zealand
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

#include "easy_curl.h"
#include "mega/common/testing/utility.h"
#include "mega/utils.h"
#include "sdk_server_test_utils.h"

#include <curl/curl.h>

#include <chrono>
#include <cstring>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

using namespace ::mega;
using namespace ::std;
using ::mega::common::testing::randomBytes;

namespace
{

std::string baseURL(int port)
{
    return "http://localhost:" + std::to_string(port) + "/";
}

std::string extractEndpointFromUrl(const std::string& url)
{
    // extract <protocol>://<ip:port> from url: http://127.0.0.1:4443/... or
    // https://[::1]:4443/...
    size_t schemeEndPos = url.find("://");
    if (schemeEndPos == string::npos)
    {
        return "";
    }
    size_t hostStartPos = schemeEndPos + 3;
    size_t hostEndPos = url.find('/', hostStartPos);
    if (hostEndPos == string::npos)
    {
        return url;
    }
    return url.substr(0, hostEndPos);
}

class SdkHttpServerTest: public SdkServerTest
{};

class SdkHttpServerLinkTest: public SdkServerTest
{
protected:
    void SetUp() override;

    std::unique_ptr<MegaApiTest> makeNonLoginApi()
    {
        // Use a large 10 to avoid conflicting with getAccountsForTest
        constexpr int cacheIndex = 10;
        auto api = std::make_unique<MegaApiTest>(APP_KEY.c_str(),
                                                 megaApiCacheFolder(cacheIndex).c_str(),
                                                 USER_AGENT.c_str(),
                                                 unsigned(THREADS_PER_MEGACLIENT));
        api->addListener(this);
        return api;
    }

    void runPublicFileLinkTest(MegaApi* api, const std::string& publicLink)
    {
        RequestTracker rt(api);
        api->getPublicNode(publicLink.c_str(), &rt);
        EXPECT_EQ(API_OK, rt.waitForResult());

        auto server = scopedHttpServer(api);
        ASSERT_TRUE(server);

        std::unique_ptr<char[]> link(api->httpServerGetLocalLink(rt.getPublicMegaNode().get()));
        ASSERT_NE(link, nullptr);

        auto response = HttpClient::get(link.get(), "1-5");
        EXPECT_EQ(206, response.statusCode);
        EXPECT_EQ(5, response.body.size());
    }

    void runPublicFolderLinkTest(MegaApi* api, const std::string& publicLink)
    {
        auto rt = asyncRequestLoginToFolder(api, publicLink.c_str());
        ASSERT_EQ(API_OK, rt->waitForResult());

        rt = asyncRequestFetchnodes(api);
        ASSERT_EQ(API_OK, rt->waitForResult());

        auto server = scopedHttpServer(api);
        ASSERT_TRUE(server);

        std::unique_ptr<MegaNode> root{api->getRootNode()};
        ASSERT_TRUE(root);

        std::unique_ptr<MegaNodeList> children{api->getChildren(root.get())};
        ASSERT_GE(children->size(), 1);

        std::unique_ptr<char[]> link(api->httpServerGetLocalLink(children->get(0)));
        ASSERT_NE(link, nullptr);

        auto response = HttpClient::get(link.get(), "1-5");
        EXPECT_EQ(206, response.statusCode);
        EXPECT_EQ(5, response.body.size());
    }

    void runPublicSetTest(MegaApi* api, const std::string& setLink)
    {
        RequestTracker fetchRt(api);
        api->fetchPublicSet(setLink.c_str(), &fetchRt);
        ASSERT_EQ(API_OK, fetchRt.waitForResult());

        std::unique_ptr<MegaSetElementList> sel{fetchRt.request->getMegaSetElementList()->copy()};
        ASSERT_TRUE(sel);
        ASSERT_EQ(sel->size(), 1);

        RequestTracker previewRt(api);
        api->getPreviewElementNode(sel->get(0)->id(), &previewRt);
        ASSERT_EQ(API_OK, previewRt.waitForResult()) << "getPreviewElementNode fails";
        std::unique_ptr<MegaNode> elementNode{previewRt.request->getPublicMegaNode()};
        ASSERT_TRUE(elementNode);

        auto server = scopedHttpServer(api);
        ASSERT_TRUE(server);

        std::unique_ptr<char[]> link(api->httpServerGetLocalLink(elementNode.get()));
        ASSERT_NE(link, nullptr);

        auto response = HttpClient::get(link.get(), "1-5");
        EXPECT_EQ(206, response.statusCode);
        EXPECT_EQ(5, response.body.size());
    }

    std::string createSetLink()
    {
        MegaSet* newSet = nullptr;
        if (doCreateSet(0, &newSet, "test", MegaSet::SET_TYPE_ALBUM) != API_OK)
        {
            return "";
        }
        const unique_ptr<MegaSet> newSetP(newSet);
        const MegaHandle sh = newSetP->id();

        MegaSetElementList* newElls = nullptr;
        if (doCreateSetElement(0, &newElls, sh, mFileNode->getHandle(), /*name=*/nullptr) != API_OK)
        {
            return "";
        }
        unique_ptr<MegaSetElementList> newEllsP(newElls);

        MegaSet* exportedSet = nullptr;
        string exportedSetURL;
        if (doExportSet(0, &exportedSet, exportedSetURL, sh) != API_OK)
        {
            return "";
        }
        unique_ptr<MegaSet> exportedSetP(exportedSet);
        return exportedSetURL;
    }

    unique_ptr<MegaNode> mFolderNode;

    unique_ptr<MegaNode> mFileNode;
};

void SdkHttpServerLinkTest::SetUp()
{
    SdkServerTest::SetUp();
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(2));

    mFolderNode = createFolder(0, "folder");
    ASSERT_TRUE(mFolderNode);

    mFileNode = uploadFile(0, "file.txt", "0123456789", mFolderNode.get());
    ASSERT_TRUE(mFileNode);
}

TEST_F(SdkHttpServerTest, HttpServerStartStop)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Test starting HTTP server with default port
    ASSERT_TRUE(megaApi[0]->httpServerStart(true, 0));

    // Verify server is running
    EXPECT_GT(megaApi[0]->httpServerIsRunning(), 0);

    // Test stopping HTTP server (returns void)
    megaApi[0]->httpServerStop();

    // Verify server is no longer running
    EXPECT_FALSE(megaApi[0]->httpServerIsRunning());
    CASE_info << "finished";
}

/**
 * Test for HTTP server using port 0, which also consist of:
 * - start two HTTP servers from a thread and no ports conflicting
 * - stop HTTP servers from a different thread, to allow TSAN to report any data races
 */
TEST_F(SdkHttpServerTest, HttpServerCanUsePort0)
{
    CASE_info << "started";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2, false));

    ASSERT_TRUE(megaApi[0]->httpServerStart(true, 0));
    ASSERT_TRUE(megaApi[1]->httpServerStart(true, 0));
    ASSERT_TRUE(megaApi[0]->httpServerIsRunning());
    ASSERT_TRUE(megaApi[1]->httpServerIsRunning());

    std::async(std::launch::async,
               [&api = megaApi]()
               {
                   api[0]->httpServerStop();
                   api[1]->httpServerStop();
               })
        .get();

    CASE_info << "finished";
}

TEST_F(SdkHttpServerTest, HttpServerStartNotOnLocal)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Test starting server not limited to localhost
    ASSERT_TRUE(megaApi[0]->httpServerStart(false, 0));
    EXPECT_GT(megaApi[0]->httpServerIsRunning(), 0);
    EXPECT_FALSE(megaApi[0]->httpServerIsLocalOnly());
    megaApi[0]->httpServerStop();
    CASE_info << "finished";
}

TEST_F(SdkHttpServerTest, HttpServerIPv6)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Test starting server with IPv6 support
    ASSERT_TRUE(megaApi[0]->httpServerStart(true, 0, false, nullptr, nullptr, true));
    EXPECT_GT(megaApi[0]->httpServerIsRunning(), 0);
    megaApi[0]->httpServerStop();
    CASE_info << "finished";
}

TEST_F(SdkHttpServerTest, EnableOfflineAttribute)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Test enabling offline attribute
    megaApi[0]->httpServerEnableOfflineAttribute(true);
    EXPECT_TRUE(megaApi[0]->httpServerIsOfflineAttributeEnabled());

    megaApi[0]->httpServerEnableOfflineAttribute(false);
    EXPECT_FALSE(megaApi[0]->httpServerIsOfflineAttributeEnabled());
    CASE_info << "finished";
}

TEST_F(SdkHttpServerTest, httpServerEnableSubtitlesSupport)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Test enabling subtitles support
    megaApi[0]->httpServerEnableSubtitlesSupport(true);
    EXPECT_TRUE(megaApi[0]->httpServerIsSubtitlesSupportEnabled());

    megaApi[0]->httpServerEnableSubtitlesSupport(false);
    EXPECT_FALSE(megaApi[0]->httpServerIsSubtitlesSupportEnabled());
    CASE_info << "finished";
}

TEST_F(SdkHttpServerTest, httpServerSetMaxBufferSize)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Test setting and getting max buffer size
    int testSize = 2 * 1024 * 1024; // 2 MB
    megaApi[0]->httpServerSetMaxBufferSize(testSize);
    EXPECT_EQ(testSize, megaApi[0]->httpServerGetMaxBufferSize());
    CASE_info << "finished";
}

TEST_F(SdkHttpServerTest, httpServerSetMaxOutputSize)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Test setting and getting max output size
    int testSize = 1 * 1024 * 1024; // 1 MB
    megaApi[0]->httpServerSetMaxOutputSize(testSize);
    EXPECT_EQ(testSize, megaApi[0]->httpServerGetMaxOutputSize());
    CASE_info << "finished";
}

// test httpServerEnableFolderServer and httpServerGetLocalLink for directories
TEST_F(SdkHttpServerTest, HttpServerDirectoryListing)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    megaApi[0]->httpServerEnableFolderServer(false);
    EXPECT_FALSE(megaApi[0]->httpServerIsFolderServerEnabled());

    auto server = scopedHttpServer(megaApi[0].get());
    ASSERT_TRUE(server);

    unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_NE(nullptr, rootNode.get());

    unique_ptr<char[]> localLink(megaApi[0]->httpServerGetLocalLink(rootNode.get()));
    ASSERT_TRUE(localLink);

    CASE_info << "Performing HTTP request to folder link" << localLink.get();

    auto response = HttpClient::get(localLink.get());
    EXPECT_EQ(403, response.statusCode);

    megaApi[0]->httpServerEnableFolderServer(true);
    EXPECT_TRUE(megaApi[0]->httpServerIsFolderServerEnabled());

    response = HttpClient::get(localLink.get());
    EXPECT_EQ(200, response.statusCode);
    EXPECT_FALSE(response.body.empty());

    const std::string folderName = "subfolder";
    auto node = createFolder(0, folderName, rootNode.get());
    ASSERT_TRUE(node);
    localLink.reset(megaApi[0]->httpServerGetLocalLink(node.get()));
    ASSERT_TRUE(localLink);

    CASE_info << "Performing HTTP request to subfolder link" << localLink.get();

    response = HttpClient::get(localLink.get());
    EXPECT_EQ(200, response.statusCode);

    EXPECT_TRUE(response.body.find(folderName) != string::npos)
        << "Response body: " << response.body << " does not contain folder name: " << folderName;
    CASE_info << "finished";
}

// test httpServerEnableFileServer and httpServerGetLocalLink for files
TEST_F(SdkHttpServerTest, HttpServerFileAccess)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Prepare a test file
    const std::string testFileName = "http_test_file.txt";
    const std::string testFileContents = "This is a test file for HTTP server access.";
    auto node = uploadFile(0, testFileName, testFileContents);
    ASSERT_TRUE(node);

    megaApi[0]->httpServerEnableFileServer(false);
    EXPECT_FALSE(megaApi[0]->httpServerIsFileServerEnabled());

    auto server = scopedHttpServer(megaApi[0].get());
    ASSERT_TRUE(server);

    unique_ptr<char[]> localLink(megaApi[0]->httpServerGetLocalLink(node.get()));
    ASSERT_TRUE(localLink);

    CASE_info << "Performing HTTP request to file link " << localLink.get();

    auto response = HttpClient::get(localLink.get());
    EXPECT_EQ(403, response.statusCode);

    megaApi[0]->httpServerEnableFileServer(true);
    EXPECT_TRUE(megaApi[0]->httpServerIsFileServerEnabled());
    response = HttpClient::get(localLink.get());

    EXPECT_EQ(200, response.statusCode);
    EXPECT_EQ(testFileContents, response.body);
    CASE_info << "finished";
}

// test httpServerSetRestrictedMode and httpServerGetRestrictedMode
TEST_F(SdkHttpServerTest, GetSetRestrictedMode)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // default mode is ALLOW_CREATED_LOCAL_LINKS
    EXPECT_EQ(MegaApi::HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS,
              megaApi[0]->httpServerGetRestrictedMode());
    megaApi[0]->httpServerEnableFileServer(true);

    auto server = scopedHttpServer(megaApi[0].get());
    ASSERT_TRUE(server);

    const std::string testFileNameStart = "http_test_file_start.txt";
    const std::string testFileContentsStart =
        "This is a test file for HTTP server access before changing modes.";
    auto fileNodeStart = uploadFile(0, testFileNameStart, testFileContentsStart);
    ASSERT_TRUE(fileNodeStart);

    const std::string testFileNameAfter = "http_test_file.txt";
    const std::string testFileContentsAfter = "This is a test file for HTTP server access.";
    auto fileNodeAfter = uploadFile(0, testFileNameAfter, testFileContentsAfter);
    ASSERT_TRUE(fileNodeAfter);
    unique_ptr<char[]> fileLinkAfter(megaApi[0]->httpServerGetLocalLink(fileNodeAfter.get()));
    ASSERT_TRUE(fileLinkAfter);
    std::string fileLinkAfterStr(fileLinkAfter.get());

    CASE_info << "Generated file link: " << fileLinkAfterStr;

    // generate file link for the first file
    std::string link = extractEndpointFromUrl(fileLinkAfterStr);
    ASSERT_FALSE(link.empty());
    unique_ptr<char[]> base64handlePtr(fileNodeStart->getBase64Handle());
    std::string name = fileNodeStart->getName();
    std::string escapedName;
    URLCodec::escape(&name, &escapedName);
    std::string fileLinkStartStr = link + "/" + base64handlePtr.get() + "/" + escapedName;

    CASE_info << "Generated file link for first file: " << fileLinkStartStr;

    // test restricted modes MegaApi::HTTP_SERVER_DENY_ALL
    {
        megaApi[0]->httpServerSetRestrictedMode(MegaApi::HTTP_SERVER_DENY_ALL);
        EXPECT_EQ(MegaApi::HTTP_SERVER_DENY_ALL, megaApi[0]->httpServerGetRestrictedMode());
        auto fileStartResponse = HttpClient::get(fileLinkStartStr);
        EXPECT_EQ(403, fileStartResponse.statusCode);
        auto fileAfterResponse = HttpClient::get(fileLinkAfterStr);
        EXPECT_EQ(403, fileAfterResponse.statusCode);
    }

    // test restricted modes MegaApi::HTTP_SERVER_ALLOW_ALL
    {
        megaApi[0]->httpServerSetRestrictedMode(MegaApi::HTTP_SERVER_ALLOW_ALL);
        EXPECT_EQ(MegaApi::HTTP_SERVER_ALLOW_ALL, megaApi[0]->httpServerGetRestrictedMode());
        auto fileStartResponse = HttpClient::get(fileLinkStartStr);
        EXPECT_EQ(200, fileStartResponse.statusCode);
        auto fileAfterResponse = HttpClient::get(fileLinkAfterStr);
        EXPECT_EQ(200, fileAfterResponse.statusCode);
    }

    // test restricted modes MegaApi::HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS
    {
        megaApi[0]->httpServerSetRestrictedMode(MegaApi::HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS);
        EXPECT_EQ(MegaApi::HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS,
                  megaApi[0]->httpServerGetRestrictedMode());
        auto fileStartResponse = HttpClient::get(fileLinkStartStr);
        EXPECT_EQ(403, fileStartResponse.statusCode);
        auto fileAfterResponse = HttpClient::get(fileLinkAfterStr);
        EXPECT_EQ(200, fileAfterResponse.statusCode);
    }

    // test restricted modes MegaApi::HTTP_SERVER_ALLOW_LAST_LOCAL_LINK
    {
        const std::string testFileNameLast = "http_test_file_last.txt";
        const std::string testFileContentsLast = "This is a last test file for HTTP server access.";
        auto fileNodeLast = uploadFile(0, testFileNameLast, testFileContentsLast);
        ASSERT_TRUE(fileNodeLast);
        unique_ptr<char[]> fileLinkLast(megaApi[0]->httpServerGetLocalLink(fileNodeLast.get()));
        ASSERT_TRUE(fileLinkLast);

        CASE_info << "Generated file link for last file: " << fileLinkLast.get();

        megaApi[0]->httpServerSetRestrictedMode(MegaApi::HTTP_SERVER_ALLOW_LAST_LOCAL_LINK);
        EXPECT_EQ(MegaApi::HTTP_SERVER_ALLOW_LAST_LOCAL_LINK,
                  megaApi[0]->httpServerGetRestrictedMode());
        auto fileStartResponse = HttpClient::get(fileLinkStartStr);
        EXPECT_EQ(403, fileStartResponse.statusCode);
        auto fileAfterResponse = HttpClient::get(fileLinkAfterStr);
        EXPECT_EQ(403, fileAfterResponse.statusCode);
        auto fileLastResponse = HttpClient::get(fileLinkLast.get());
        EXPECT_EQ(200, fileLastResponse.statusCode);
    }

    // test invalid restricted mode value (should not change)
    megaApi[0]->httpServerSetRestrictedMode(99);
    EXPECT_EQ(MegaApi::HTTP_SERVER_ALLOW_LAST_LOCAL_LINK,
              megaApi[0]->httpServerGetRestrictedMode());
    CASE_info << "finished";
}

/**
 * Test basic HTTP server functionality with GET request.
 */
TEST_F(SdkHttpServerTest, BasicGet)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    std::string testFileContent = "HTTP server basic test content";
    std::unique_ptr<MegaNode> uploadedNode = uploadFile(0, "test_http_basic.txt", testFileContent);
    ASSERT_NE(uploadedNode, nullptr);

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    std::unique_ptr<char[]> link(api->httpServerGetLocalLink(uploadedNode.get()));
    ASSERT_NE(link, nullptr);
    std::string url = link.get();

    auto response = HttpClient::get(url);
    EXPECT_EQ(200, response.statusCode);
    EXPECT_EQ(testFileContent, response.body);
}
/**
 * Test basic HTTP server rate limit with GET request.
 */
TEST_F(SdkHttpServerTest, BasicGetWithRateLimit)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    // 1MBytes(8Mbits) data
    std::string testFileContent = randomBytes(1024 * 1024);
    std::unique_ptr<MegaNode> uploadedNode =
        uploadFile(0, "test_http_basic_with_ratelimit.bin", testFileContent);
    ASSERT_NE(uploadedNode, nullptr);

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    // 256Kbytes per second (2Mbits per second)
    api->httpServerSetThrottleBitrate(2 * 1024 * 1024);

    std::unique_ptr<char[]> link(api->httpServerGetLocalLink(uploadedNode.get()));
    ASSERT_NE(link, nullptr);
    std::string url = link.get();

    auto response = HttpClient::get(url);
    ASSERT_EQ(200, response.statusCode);
    ASSERT_EQ(testFileContent.size(), response.body.size());
    ASSERT_EQ(testFileContent.compare(response.body), 0);
}

/**
 * Test HTTP server with HEAD request.
 */
TEST_F(SdkHttpServerTest, HeadRequest)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    std::string testFileContent = "HTTP server HEAD test content";
    std::unique_ptr<MegaNode> uploadedNode = uploadFile(0, "test_http_head.txt", testFileContent);
    ASSERT_NE(uploadedNode, nullptr);

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    std::unique_ptr<char[]> link(api->httpServerGetLocalLink(uploadedNode.get()));
    ASSERT_NE(link, nullptr);
    std::string url = link.get();

    auto response = HttpClient::head(url);
    EXPECT_EQ(200, response.statusCode);
    EXPECT_TRUE(response.body.empty());
    ASSERT_TRUE(response.headers.find("content-length") != response.headers.end());
    EXPECT_EQ(response.headers["content-length"], std::to_string(testFileContent.size()));
}

/**
 * Test HTTP server with valid range requests.
 */
TEST_F(SdkHttpServerTest, ValidRangeRequests)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    std::string testFileContent = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::unique_ptr<MegaNode> uploadedNode = uploadFile(0, "test_http_range.txt", testFileContent);
    ASSERT_NE(uploadedNode, nullptr);

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    std::unique_ptr<char[]> link(api->httpServerGetLocalLink(uploadedNode.get()));
    ASSERT_NE(link, nullptr);
    std::string url = link.get();

    // Standard range: first 10 bytes
    auto range1 = HttpClient::get(url, "0-9");
    EXPECT_EQ(206, range1.statusCode);
    EXPECT_EQ("0123456789", range1.body);

    // Standard range: middle 10 bytes
    auto range2 = HttpClient::get(url, "10-19");
    EXPECT_EQ(206, range2.statusCode);
    EXPECT_EQ("ABCDEFGHIJ", range2.body);

    // Overlapping range
    auto range3 = HttpClient::get(url, "5-14");
    EXPECT_EQ(206, range3.statusCode);
    EXPECT_EQ("56789ABCDE", range3.body);

    // Suffix range: last 10 bytes
    auto suffixRange = HttpClient::get(url, "-10");
    EXPECT_EQ(200, suffixRange.statusCode); // BUG: HTTP protocol expects 206 Partial Content
    EXPECT_EQ(testFileContent,
              suffixRange.body); // BUG: Server returns full file instead of last 10 bytes

    // Suffix range: last 5 bytes
    auto suffixRange2 = HttpClient::get(url, "-5");
    EXPECT_EQ(200, suffixRange2.statusCode); // BUG: HTTP protocol expects 206 Partial Content
    EXPECT_EQ(testFileContent,
              suffixRange2.body); // BUG: Server returns full file instead of last 5 bytes

    // Single byte range: first byte
    auto singleByte1 = HttpClient::get(url, "0-0");
    EXPECT_EQ(206, singleByte1.statusCode);
    EXPECT_EQ("0", singleByte1.body);

    // Single byte range: middle byte
    auto singleByte2 = HttpClient::get(url, "15-15");
    EXPECT_EQ(206, singleByte2.statusCode);
    EXPECT_EQ("F", singleByte2.body);

    // Single byte range: last byte
    size_t fileSize = testFileContent.size();
    auto singleByte3 =
        HttpClient::get(url, std::to_string(fileSize - 1) + "-" + std::to_string(fileSize - 1));
    EXPECT_EQ(206, singleByte3.statusCode);
    EXPECT_EQ("Z", singleByte3.body);

    // Prefix range: first 15 bytes (0-14)
    auto prefixRange = HttpClient::get(url, "0-14");
    EXPECT_EQ(206, prefixRange.statusCode);
    EXPECT_EQ("0123456789ABCDE", prefixRange.body);

    // Range from position to end (should return from N to end)
    auto rangeToEnd = HttpClient::get(url, "26-");
    EXPECT_EQ(206, rangeToEnd.statusCode);
    EXPECT_EQ("QRSTUVWXYZ", rangeToEnd.body);

    // Full file range (0 to last byte)
    auto fullRange = HttpClient::get(url, "0-" + std::to_string(fileSize - 1));
    EXPECT_EQ(206, fullRange.statusCode);
    EXPECT_EQ(testFileContent, fullRange.body);

    // Range starting at 1
    auto edgeCase1 = HttpClient::get(url, "1-5");
    EXPECT_EQ(206, edgeCase1.statusCode);
    EXPECT_EQ("12345", edgeCase1.body);

    // Range ending at second-to-last byte
    auto edgeCase2 =
        HttpClient::get(url, std::to_string(fileSize - 3) + "-" + std::to_string(fileSize - 2));
    EXPECT_EQ(206, edgeCase2.statusCode);
    EXPECT_EQ("XY", edgeCase2.body);
}

/**
 * Test HTTP server with very large range requests.
 * Tests various range formats on very large files including suffix ranges.
 */
TEST_F(SdkHttpServerTest, VeryLargeRangeRequests)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    std::string testFileContent = randomBytes(50 * 1024 * 1024);
    std::unique_ptr<MegaNode> uploadedNode =
        uploadFile(0, "test_http_large_range.bin", testFileContent);
    ASSERT_NE(uploadedNode, nullptr);

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    std::unique_ptr<char[]> link(api->httpServerGetLocalLink(uploadedNode.get()));
    ASSERT_NE(link, nullptr);
    std::string url = link.get();

    // Full file range
    auto fileSize = testFileContent.size();
    auto largeRange = HttpClient::get(url, "0-" + std::to_string(fileSize - 1));
    EXPECT_EQ(206, largeRange.statusCode);
    EXPECT_TRUE(testFileContent == largeRange.body);

    // Middle range: from 25% to 50%, end is inclusive
    auto begin = fileSize / 4;
    auto end = fileSize / 2;
    auto midRange = HttpClient::get(url, std::to_string(begin) + "-" + std::to_string(end));
    EXPECT_EQ(206, midRange.statusCode);
    EXPECT_TRUE(std::string_view(testFileContent.data() + begin, end - begin + 1) == midRange.body);

    // Suffix range: last 10MB (bytes=-10485760)
    auto suffixRange = HttpClient::get(url, "-10485760");
    EXPECT_EQ(200, suffixRange.statusCode); // BUG: HTTP protocol expects 206 Partial Content
    EXPECT_TRUE(testFileContent ==
                suffixRange.body); // BUG: Server returns full file instead of last 10MB

    // Suffix range: last 25% of file
    auto suffixRange2 = HttpClient::get(url, "-" + std::to_string(fileSize / 4));
    EXPECT_EQ(200, suffixRange2.statusCode); // BUG: HTTP protocol expects 206 Partial Content
    EXPECT_TRUE(testFileContent ==
                suffixRange2.body); // BUG: Server returns full file instead of last 25%

    // Range from 75% to end
    begin = fileSize * 3 / 4;
    end = testFileContent.size() - 1;
    auto rangeToEnd = HttpClient::get(url, std::to_string(begin) + "-");
    EXPECT_EQ(206, rangeToEnd.statusCode);
    EXPECT_TRUE(std::string_view(testFileContent.data() + begin, end - begin + 1) ==
                rangeToEnd.body);
}

/**
 * Test HTTP server with invalid range requests (416 Requested Range Not Satisfiable).
 */
TEST_F(SdkHttpServerTest, InvalidRangeRequests)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    std::string testFileContent = "Test content";
    std::unique_ptr<MegaNode> uploadedNode =
        uploadFile(0, "test_http_invalid_range.txt", testFileContent);
    ASSERT_NE(uploadedNode, nullptr);

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    std::unique_ptr<char[]> link(api->httpServerGetLocalLink(uploadedNode.get()));
    ASSERT_NE(link, nullptr);
    std::string url = link.get();

    // Range starting beyond file end
    auto fileSize = testFileContent.size();
    auto invalidRange1 =
        HttpClient::get(url, std::to_string(fileSize) + "-" + std::to_string(fileSize + 100));
    EXPECT_EQ(416, invalidRange1.statusCode);

    // Range completely beyond file end
    auto invalidRange2 = HttpClient::get(url, "1000-2000");
    EXPECT_EQ(416, invalidRange2.statusCode);

    // Range with start > end
    auto invalidRange3 = HttpClient::get(url, "10-5");
    EXPECT_EQ(416, invalidRange3.statusCode);
}

/**
 * Test HTTP server with non-existent file (404 Not Found).
 */
TEST_F(SdkHttpServerTest, NonExistentFile)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    int port = api->httpServerIsRunning();
    std::string invalidHandle = "12345678";
    std::string invalidUrl = baseURL(port) + invalidHandle + "/nonexistent_file.txt";

    auto response = HttpClient::get(invalidUrl);
    EXPECT_EQ(403, response.statusCode); // BUG: HTTP protocol expects 404 Not Found
}

/**
 * Test HTTP server with empty file.
 * Tests GET, HEAD, and range requests for empty files.
 */
TEST_F(SdkHttpServerTest, EmptyFile)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    // Upload empty file
    std::unique_ptr<MegaNode> uploadedNode = uploadFile(0, "test_http_empty.txt", "");
    ASSERT_NE(uploadedNode, nullptr);

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    std::unique_ptr<char[]> link(api->httpServerGetLocalLink(uploadedNode.get()));
    ASSERT_NE(link, nullptr);
    std::string url = link.get();

    // GET request for empty file
    auto response = HttpClient::get(url);
    EXPECT_EQ(200, response.statusCode);
    EXPECT_TRUE(response.body.empty());
    EXPECT_TRUE(response.headers.find("content-length") != response.headers.end());
    EXPECT_EQ("0", response.headers["content-length"]);

    // HEAD request for empty file
    auto headResponse = HttpClient::head(url);
    EXPECT_EQ(200, headResponse.statusCode);
    EXPECT_TRUE(headResponse.body.empty());
    EXPECT_TRUE(headResponse.headers.find("content-length") != headResponse.headers.end());

    // Range requests for empty file
    auto rangeResponse1 = HttpClient::get(url, "0-0");
    EXPECT_EQ(206,
              rangeResponse1.statusCode); // BUG: HTTP protocol expects 416 Range Not Satisfiable

    auto rangeResponse2 = HttpClient::get(url, "0-10");
    EXPECT_EQ(206,
              rangeResponse2.statusCode); // BUG: HTTP protocol expects 416 Range Not Satisfiable

    auto suffixRange = HttpClient::get(url, "-10");
    EXPECT_EQ(200, suffixRange.statusCode); // BUG: HTTP protocol expects 416 Range Not Satisfiable
}

/**
 * Test HTTP server with large file.
 * Tests various range requests on large files including suffix ranges.
 */
TEST_F(SdkHttpServerTest, LargeFile)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    std::string testFileContent = randomBytes(10 * 1024 * 1024);
    std::unique_ptr<MegaNode> uploadedNode = uploadFile(0, "test_http_large.bin", testFileContent);
    ASSERT_NE(uploadedNode, nullptr);

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    std::unique_ptr<char[]> link(api->httpServerGetLocalLink(uploadedNode.get()));
    ASSERT_NE(link, nullptr);
    std::string url = link.get();

    // Full file GET request
    auto response = HttpClient::get(url);
    EXPECT_EQ(200, response.statusCode);
    EXPECT_TRUE(testFileContent == response.body);

    // Standard range: first 1MB
    auto rangeResponse = HttpClient::get(url, "0-1048575");
    EXPECT_EQ(206, rangeResponse.statusCode);
    EXPECT_TRUE(std::string_view(testFileContent.data(), 1048575u + 1) == rangeResponse.body);

    // Standard range: second 1MB
    auto rangeResponse2 = HttpClient::get(url, "1048576-2097151");
    EXPECT_EQ(206, rangeResponse2.statusCode);
    EXPECT_TRUE(std::string_view(testFileContent.data() + 1048576, 2097151u - 1048576u + 1) ==
                rangeResponse2.body);

    // Suffix range: last 1MB (bytes=-1048576)
    auto suffixRange = HttpClient::get(url, "-1048576");
    EXPECT_EQ(200,
              suffixRange.statusCode); // BUG: HTTP protocol expects 206 Partial Content
    EXPECT_TRUE(testFileContent ==
                suffixRange.body); // BUG: Server returns full file instead of last 1MB

    // Suffix range: last 512KB
    auto suffixRange2 = HttpClient::get(url, "-524288");
    EXPECT_EQ(200,
              suffixRange2.statusCode); // BUG: HTTP protocol expects 206 Partial Content
    EXPECT_TRUE(testFileContent ==
                suffixRange2.body); // BUG: Server returns full file instead of last 512KB

    // Range from middle to near end
    auto midRange = HttpClient::get(url, "5242880-6291455");
    EXPECT_EQ(206, midRange.statusCode);
    EXPECT_TRUE(std::string_view(testFileContent.data() + 5242880, 6291455u - 5242880u + 1) ==
                midRange.body);

    // Small range from beginning
    auto smallRange = HttpClient::get(url, "0-1023");
    EXPECT_EQ(206, smallRange.statusCode);
    EXPECT_TRUE(std::string_view(testFileContent.data(), 1023u + 1) == smallRange.body);
}

/**
 * Test HTTP server with concurrent requests.
 */
TEST_F(SdkHttpServerTest, ConcurrentRequests)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    std::string testFileContent = randomBytes(100 * 1024);
    std::unique_ptr<MegaNode> uploadedNode =
        uploadFile(0, "test_http_concurrent.txt", testFileContent);
    ASSERT_NE(uploadedNode, nullptr);

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    std::unique_ptr<char[]> link(api->httpServerGetLocalLink(uploadedNode.get()));
    ASSERT_NE(link, nullptr);
    std::string url = link.get();

    const int numRequests = 10;
    std::vector<std::future<HttpClient::Response>> futures;

    for (int i = 0; i < numRequests; i++)
    {
        futures.push_back(std::async(std::launch::async,
                                     [url]()
                                     {
                                         return HttpClient::get(url);
                                     }));
    }

    for (size_t i = 0; i < futures.size(); i++)
    {
        auto response = futures[i].get();
        EXPECT_EQ(200, response.statusCode);
        EXPECT_TRUE(testFileContent == response.body);
    }
}

/**
 * Test HTTP server with concurrent range requests.
 * Tests concurrent standard and suffix range requests.
 */
TEST_F(SdkHttpServerTest, ConcurrentRangeRequests)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    std::string testFileContent = randomBytes(2 * 1024 * 1024);
    std::unique_ptr<MegaNode> uploadedNode =
        uploadFile(0, "test_http_concurrent_range.bin", testFileContent);
    ASSERT_NE(uploadedNode, nullptr);

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    std::unique_ptr<char[]> link(api->httpServerGetLocalLink(uploadedNode.get()));
    ASSERT_NE(link, nullptr);
    std::string url = link.get();

    const int numRequests = 5;
    std::vector<std::future<HttpClient::Response>> futures;

    // Concurrent standard range requests
    constexpr auto INTERVAL = 200000;
    constexpr auto LENGTH = 200000;
    for (size_t i = 0; i < numRequests; i++)
    {
        size_t start = i * INTERVAL;
        size_t end = start + LENGTH - 1;
        std::string range = std::to_string(start) + "-" + std::to_string(end);
        futures.push_back(std::async(std::launch::async,
                                     [url, range]()
                                     {
                                         return HttpClient::get(url, range);
                                     }));
    }

    for (size_t i = 0; i < futures.size(); i++)
    {
        auto response = futures[i].get();
        EXPECT_EQ(206, response.statusCode);
        size_t start = i * INTERVAL;
        EXPECT_TRUE(std::string_view(testFileContent.data() + start, LENGTH) == response.body);
    }

    futures.clear();

    // Concurrent suffix range requests
    std::vector<size_t> suffixSizes = {100000, 200000, 300000, 400000, 500000};
    for (size_t suffixSize: suffixSizes)
    {
        std::string range = "-" + std::to_string(suffixSize);
        futures.push_back(std::async(
            std::launch::async,
            [url, range, &testFileContent]()
            {
                auto resp = HttpClient::get(url, range);
                EXPECT_EQ(200, resp.statusCode); // BUG: HTTP protocol expects 206 Partial Content
                EXPECT_TRUE(testFileContent ==
                            resp.body); // BUG: Server returns full file instead of last N bytes
                return resp;
            }));
    }

    for (auto& future: futures)
    {
        future.get();
    }
}

/**
 * Test HTTP server restart and multiple start/stop cycles.
 */
TEST_F(SdkHttpServerTest, Restart)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    std::string testFileContent = "HTTP server restart test";
    std::unique_ptr<MegaNode> uploadedNode =
        uploadFile(0, "test_http_restart.txt", testFileContent);
    ASSERT_NE(uploadedNode, nullptr);

    for (int cycle = 0; cycle < 10; cycle++)
    {
        auto server = scopedHttpServer(api);
        ASSERT_TRUE(server);

        std::unique_ptr<char[]> link(api->httpServerGetLocalLink(uploadedNode.get()));
        ASSERT_NE(link, nullptr);

        auto response = HttpClient::get(link.get());
        EXPECT_EQ(200, response.statusCode);
        EXPECT_EQ(testFileContent, response.body);
    }
}

/**
 * Test HTTP server with malformed URLs.
 */
TEST_F(SdkHttpServerTest, MalformedUrls)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    std::string baseUrl = baseURL(api->httpServerIsRunning());
    std::vector<std::string> malformedUrls = {
        baseUrl + "invalid",
        baseUrl + "12345/invalid",
        baseUrl + "!@#$%^&*()",
        baseUrl + "",
        baseUrl + "a/b/c/d/e/f",
    };

    for (const auto& url: malformedUrls)
    {
        auto response = HttpClient::get(url);
        EXPECT_TRUE(response.statusCode == 404 ||
                    response.statusCode ==
                        403); // BUG: HTTP protocol expects 400 Bad Request or 404 Not Found
    }
}

/**
 * Test HTTP server with unsupported HTTP methods.
 */
TEST_F(SdkHttpServerTest, UnsupportedMethods)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    std::string testFileContent = "HTTP methods test";
    std::unique_ptr<MegaNode> uploadedNode =
        uploadFile(0, "test_http_methods.txt", testFileContent);
    ASSERT_NE(uploadedNode, nullptr);

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    std::unique_ptr<char[]> link(api->httpServerGetLocalLink(uploadedNode.get()));
    ASSERT_NE(link, nullptr);
    std::string url = link.get();

    auto postResponse = HttpClient::post(url);
    EXPECT_EQ(200, postResponse.statusCode); // BUG: HTTP protocol expects 405 Method Not Allowed

    auto putResponse = HttpClient::put(url);
    // BUG: HTTP protocol expects 405 Method Not Allowed. Due to a race condition (?),
    // the server may have time to return 500
    EXPECT_TRUE(putResponse.statusCode == 0 || putResponse.statusCode == 500);

    auto deleteResponse = HttpClient::del(url);
    EXPECT_EQ(405, deleteResponse.statusCode);
}

/**
 * Test HTTP server stability under rapid requests.
 */
TEST_F(SdkHttpServerTest, RapidRequests)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    std::string testFileContent = randomBytes(1024);
    std::unique_ptr<MegaNode> uploadedNode = uploadFile(0, "test_http_rapid.txt", testFileContent);
    ASSERT_NE(uploadedNode, nullptr);

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    std::unique_ptr<char[]> link(api->httpServerGetLocalLink(uploadedNode.get()));
    ASSERT_NE(link, nullptr);
    std::string url = link.get();

    constexpr int numRequests = 50;
    int successCount = 0;
    int failureCount = 0;

    for (int i = 0; i < numRequests; i++)
    {
        auto response = HttpClient::get(url);
        if (response.statusCode == 200 && response.body == testFileContent)
        {
            ++successCount;
        }
        else
        {
            ++failureCount;
        }
    }

    EXPECT_GT(successCount, numRequests * 0.9);
    EXPECT_LT(failureCount, numRequests * 0.1);
}

/**
 * Test HTTP server with special characters in file names.
 * Tests files with spaces, URL-encoded characters, non-ASCII, and special symbols.
 */
TEST_F(SdkHttpServerTest, SpecialCharactersInFilename)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    // Files and content
    const std::vector<std::string> testFiles = {
        "file with spaces.txt",
        "file%with&special#.txt",
        "file+=with+plus.txt",
        "\xD1\x83\xD0\xBA\xD1\x80\xD0\xB0\xD1\x97\xD0\xBD\xD1\x81\xD1\x8C\xD0\xBA\xD0\xB8\xD0\xB9."
        "txt",
        "test-file-normal.txt",
    };

    std::vector<std::unique_ptr<MegaNode>> uploadedNodes;
    for (const auto& fileName: testFiles)
    {
        std::unique_ptr<MegaNode> uploadedNode = uploadFile(0, fileName, fileName);
        ASSERT_NE(uploadedNode, nullptr);
        uploadedNodes.push_back(std::move(uploadedNode));
    }

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    for (size_t i = 0; i < testFiles.size(); i++)
    {
        const auto& fileName = testFiles[i];
        std::unique_ptr<char[]> link(api->httpServerGetLocalLink(uploadedNodes[i].get()));
        ASSERT_NE(link, nullptr);
        std::string url = link.get();

        auto response = HttpClient::get(url);
        EXPECT_EQ(200, response.statusCode);
        EXPECT_EQ(fileName, response.body);
    }
}

/**
 * Test HTTP server with very small file sizes.
 */
TEST_F(SdkHttpServerTest, DifferentFileSizes)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    // Test 1-byte file
    std::string testFileContent1 = "A";
    std::unique_ptr<MegaNode> uploadedNode1 = uploadFile(0, "test_1byte.tx", testFileContent1);
    ASSERT_NE(uploadedNode1, nullptr);

    // Test 2-byte file
    std::string testFileContent2 = "AB";
    std::unique_ptr<MegaNode> uploadedNode2 = uploadFile(0, "test_2byte.tx", testFileContent2);
    ASSERT_NE(uploadedNode2, nullptr);

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    // Test 1-byte file
    std::unique_ptr<char[]> link1(api->httpServerGetLocalLink(uploadedNode1.get()));
    ASSERT_NE(link1, nullptr);
    std::string url1 = link1.get();

    // Full file GET
    auto response = HttpClient::get(url1);
    EXPECT_EQ(200, response.statusCode);
    EXPECT_EQ("A", response.body);

    // Range request for single byte
    auto rangeResponse = HttpClient::get(url1, "0-0");
    EXPECT_EQ(206, rangeResponse.statusCode);
    EXPECT_EQ("A", rangeResponse.body);

    // Range request beyond file
    auto invalidRange = HttpClient::get(url1, "1-5");
    EXPECT_EQ(416, invalidRange.statusCode);

    // Test 2-byte file
    std::unique_ptr<char[]> link2(api->httpServerGetLocalLink(uploadedNode2.get()));
    ASSERT_NE(link2, nullptr);
    std::string url2 = link2.get();

    // Range: first byte
    auto range1 = HttpClient::get(url2, "0-0");
    EXPECT_EQ(206, range1.statusCode);
    EXPECT_EQ("A", range1.body);

    // Range: second byte
    auto range2 = HttpClient::get(url2, "1-1");
    EXPECT_EQ(206, range2.statusCode);
    EXPECT_EQ("B", range2.body);

    // Range: both bytes
    auto range3 = HttpClient::get(url2, "0-1");
    EXPECT_EQ(206, range3.statusCode);
    EXPECT_EQ("AB", range3.body);
}

/**
 * Test HTTP server with very long URLs
 * Tests server behavior with extremely long URL paths, including non-existent files (404).
 */
TEST_F(SdkHttpServerTest, VeryLongUrl)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    std::string testFileContent = "Test content";
    std::unique_ptr<MegaNode> uploadedNode = uploadFile(0, "test_http_long.txt", testFileContent);
    ASSERT_NE(uploadedNode, nullptr);

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    // Create a 1 KB long path by appending many characters
    constexpr size_t targetSize = 1 * 1024;
    std::string longPath;
    longPath.reserve(targetSize);
    while (longPath.size() < targetSize)
    {
        longPath += "/very/long/path/segment/for/testing/";
    }
    longPath.resize(targetSize);

    // Test with very long path to non-existent file
    std::string longUrl = baseURL(api->httpServerIsRunning()) + longPath;
    auto response = HttpClient::get(longUrl);
    EXPECT_EQ(404, response.statusCode);

    // Test with valid URL but very long query parameters
    std::unique_ptr<char[]> link(api->httpServerGetLocalLink(uploadedNode.get()));
    ASSERT_NE(link, nullptr);
    std::string url = link.get();
    std::string longUrlWithQuery = url + "?" + std::string(targetSize - 1, 'x');
    auto queryResponse = HttpClient::get(longUrlWithQuery);
    EXPECT_EQ(404, queryResponse.statusCode); // BUG: Server treats query as part of filename

    // Verify normal URL still works
    auto normalResponse = HttpClient::get(url);
    EXPECT_EQ(200, normalResponse.statusCode);
    EXPECT_EQ(testFileContent, normalResponse.body);
}

/**
 * Test HTTP server various connections handling.
 */
TEST_F(SdkHttpServerTest, ConnectionHandling)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    std::string testFileContent = randomBytes(1024);
    std::unique_ptr<MegaNode> uploadedNode =
        uploadFile(0, "test_http_connection.txt", testFileContent);
    ASSERT_NE(uploadedNode, nullptr);

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    std::unique_ptr<char[]> link(api->httpServerGetLocalLink(uploadedNode.get()));
    ASSERT_NE(link, nullptr);
    std::string url = link.get();

    // Test multiple sequential requests
    for (int i = 0; i < 5; i++)
    {
        auto response = HttpClient::get(url);
        EXPECT_EQ(200, response.statusCode);
        EXPECT_TRUE(testFileContent == response.body);
    }

    // Test HEAD followed by GET
    auto headResponse = HttpClient::head(url);
    EXPECT_EQ(200, headResponse.statusCode);

    auto getResponse = HttpClient::get(url);
    EXPECT_EQ(200, getResponse.statusCode);
    EXPECT_TRUE(testFileContent == getResponse.body);

    // Test range request followed by full request
    auto rangeResponse = HttpClient::get(url, "0-99");
    EXPECT_EQ(206, rangeResponse.statusCode);
    EXPECT_TRUE(100 == rangeResponse.body.size());

    auto fullResponse = HttpClient::get(url);
    EXPECT_EQ(200, fullResponse.statusCode);
    EXPECT_TRUE(testFileContent == fullResponse.body);
}

/**
 * Test HTTP server with empty folder.
 */
TEST_F(SdkHttpServerTest, FolderEmpty)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    // Create empty folder
    auto folderNode = createFolder(0, "test_http_folder_empty");
    ASSERT_NE(folderNode, nullptr);

    // Enable folder server support
    api->httpServerEnableFolderServer(true);
    ASSERT_TRUE(api->httpServerIsFolderServerEnabled());

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    std::unique_ptr<char[]> link(api->httpServerGetLocalLink(folderNode.get()));
    ASSERT_NE(link, nullptr);
    std::string url = link.get();

    auto response = HttpClient::get(url);
    EXPECT_EQ(200, response.statusCode);
    EXPECT_NE(response.body.find("<title>"),
              std::string::npos); // BUG: Server returns HTML page without <html></html>

    auto headResponse = HttpClient::head(url);
    EXPECT_EQ(200, headResponse.statusCode);
}

/**
 * Test HTTP server with folder with files.
 */
TEST_F(SdkHttpServerTest, FolderWithFiles)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    // Create folder
    auto folderNode = createFolder(0, "test_http_folder_files");
    ASSERT_NE(folderNode, nullptr);

    // Upload files to folder
    const std::vector<std::string> testFiles = {
        "file 1.txt",
        "file#2.txt",
#ifndef WIN32 // ? is not allowed on Windows
        "file?3.dat",
#endif
        "file-3.dat",
    };

    std::vector<std::unique_ptr<MegaNode>> uploadedNodes;
    for (const auto& fileName: testFiles)
    {
        std::unique_ptr<MegaNode> uploadedNode =
            uploadFile(0, fileName, fileName, folderNode.get());
        ASSERT_NE(uploadedNode, nullptr);
        uploadedNodes.push_back(std::move(uploadedNode));
    }

    // Enable folder server support
    api->httpServerEnableFolderServer(true);
    ASSERT_TRUE(api->httpServerIsFolderServerEnabled());

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    std::unique_ptr<char[]> link(api->httpServerGetLocalLink(folderNode.get()));
    ASSERT_NE(link, nullptr);
    std::string url = link.get();

    auto response = HttpClient::get(url);
    EXPECT_EQ(200, response.statusCode);
    EXPECT_NE(response.body.find("<title>"),
              std::string::npos); // BUG: Server returns HTML page without <html></html>

    // Check that file names appear in the HTML
    for (const auto& fileName: testFiles)
    {
        EXPECT_TRUE(response.body.find(fileName) != std::string::npos);
    }

    // HEAD request
    auto headResponse = HttpClient::head(url);
    EXPECT_EQ(200, headResponse.statusCode);
}

/**
 * Test that node names containing HTML special characters are escaped in the
 * directory listing, so the generated HTML stays well-formed.
 */
TEST_F(SdkHttpServerTest, FolderListingEscapesNodeNames)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    // Parent folder that will be served and listed
    auto folderNode = createFolder(0, "test_http_folder_escaping");
    ASSERT_NE(folderNode, nullptr);

    // Child folder whose name contains HTML special characters. Node names accept
    // arbitrary UTF-8 (including < > " &), so the name reaches the listing unchanged.
    const std::string markupFolderName = "<folder-name>";
    auto childFolder = createFolder(0, markupFolderName, folderNode.get());
    ASSERT_NE(childFolder, nullptr);
    ASSERT_STREQ(markupFolderName.c_str(), childFolder->getName());

#ifndef _WIN32
    // On native Unix filesystems < and > are usually valid in file names, so a file
    // (not only a folder) can carry these characters in its cloud name and appear in
    // the listing. They are not valid in file names on Windows, hence the guard.
    const std::string markupFileName = "<file-name>.txt";
    auto childFile = uploadFile(0, markupFileName, "content", folderNode.get());
    ASSERT_NE(childFile, nullptr);
    ASSERT_STREQ(markupFileName.c_str(), childFile->getName());
#endif

    api->httpServerEnableFolderServer(true);
    ASSERT_TRUE(api->httpServerIsFolderServerEnabled());

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    std::unique_ptr<char[]> link(api->httpServerGetLocalLink(folderNode.get()));
    ASSERT_NE(link, nullptr);
    std::string url = link.get();

    auto response = HttpClient::get(url);
    EXPECT_EQ(200, response.statusCode);

    // The raw characters must not appear verbatim: they must be escaped instead.
    EXPECT_EQ(response.body.find("<folder-name"), std::string::npos)
        << "Folder name was not escaped in directory listing. Body: " << response.body;
    EXPECT_NE(response.body.find("&lt;folder-name"), std::string::npos)
        << "Escaped folder name not found in directory listing. Body: " << response.body;

#ifndef _WIN32
    // Same expectation for the file name.
    EXPECT_EQ(response.body.find("<file-name"), std::string::npos)
        << "File name was not escaped in directory listing. Body: " << response.body;
    EXPECT_NE(response.body.find("&lt;file-name"), std::string::npos)
        << "Escaped file name not found in directory listing. Body: " << response.body;
#endif

    // In relative-link mode (the default) the served folder name becomes the first
    // segment of each child's href. That value must resolve as a relative path and
    // must never be interpreted as a URI scheme, so it is emitted with a leading "./".
    auto schemeFolder = createFolder(0, "javascript:void(0)");
    ASSERT_NE(schemeFolder, nullptr);
    ASSERT_NE(createFolder(0, "child", schemeFolder.get()), nullptr);

    std::unique_ptr<char[]> schemeLink(api->httpServerGetLocalLink(schemeFolder.get()));
    ASSERT_NE(schemeLink, nullptr);
    auto schemeResponse = HttpClient::get(schemeLink.get());
    EXPECT_EQ(200, schemeResponse.statusCode);
    EXPECT_EQ(schemeResponse.body.find("href=\"javascript:"), std::string::npos)
        << "Child href must be a relative path, not a URI scheme. Body: " << schemeResponse.body;
}

/**
 * Manual inspection helper for the HTTP directory listing.
 *
 * DISABLED by default. Run explicitly with:
 *   --gtest_also_run_disabled_tests
 *   --gtest_filter='*ManualFolderListingInspection'
 *
 * Keeps the local HTTP server running so listing HTML and file downloads can be
 * checked from a browser or curl.
 */
TEST_F(SdkHttpServerTest, DISABLED_ManualFolderListingInspection)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    MegaApi* api = megaApi[0].get();

    auto root = createFolder(0, "manual_listing_check");
    ASSERT_NE(root, nullptr);

    const std::string markupFolderName = "<folder-name>";
    auto markupFolder = createFolder(0, markupFolderName, root.get());
    ASSERT_NE(markupFolder, nullptr);

    auto schemeFolder = createFolder(0, "javascript:void(0)", root.get());
    ASSERT_NE(schemeFolder, nullptr);
    ASSERT_NE(createFolder(0, "child", schemeFolder.get()), nullptr);

    const std::string downloadContent = "manual-download-ok\n";
    auto downloadFile = uploadFile(0, "download-me.txt", downloadContent, root.get());
    ASSERT_NE(downloadFile, nullptr);

#ifndef _WIN32
    const std::string markupFileName = "<file-name>.txt";
    auto markupFile = uploadFile(0, markupFileName, "markup-file-content\n", root.get());
    ASSERT_NE(markupFile, nullptr);
#endif

    api->httpServerEnableFolderServer(true);
    api->httpServerEnableFileServer(true);
    // Absolute /handle/name links in the listing, easier to click in a browser.
    api->httpServerSetRestrictedMode(MegaApi::HTTP_SERVER_ALLOW_ALL);

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    const int port = api->httpServerIsRunning();
    ASSERT_GT(port, 0);

    auto localLink = [](MegaApi* megaApi, MegaNode* node) -> std::string
    {
        std::unique_ptr<char[]> link(megaApi->httpServerGetLocalLink(node));
        return link ? std::string(link.get()) : std::string();
    };

    const std::string rootUrl = localLink(api, root.get());
    const std::string markupFolderUrl = localLink(api, markupFolder.get());
    const std::string schemeFolderUrl = localLink(api, schemeFolder.get());
    const std::string downloadUrl = localLink(api, downloadFile.get());
#ifndef _WIN32
    const std::string markupFileUrl = localLink(api, markupFile.get());
#endif
    ASSERT_FALSE(rootUrl.empty());
    ASSERT_FALSE(downloadUrl.empty());

    constexpr int seconds = 900;

    std::cout << "\n"
              << "========== Manual HTTP listing inspection ==========\n"
              << "Server port: " << port << "\n"
              << "Restricted mode: HTTP_SERVER_ALLOW_ALL\n"
              << "Open the listing URL in a browser, or use the curl commands below.\n"
              << "Expect escaped names in HTML (&lt;...&gt;), working downloads,\n"
              << "and child hrefs that do not start with a URI scheme.\n"
              << "\n"
              << "Listing (parent folder):\n"
              << "  " << rootUrl << "\n"
              << "  curl -sS '" << rootUrl << "'\n"
              << "\n"
              << "Markup folder listing:\n"
              << "  " << markupFolderUrl << "\n"
              << "\n"
              << "Scheme-looking folder listing:\n"
              << "  " << schemeFolderUrl << "\n"
              << "\n"
              << "Plain file download (expect body: manual-download-ok):\n"
              << "  " << downloadUrl << "\n"
              << "  curl -sS '" << downloadUrl << "'\n"
#ifndef _WIN32
              << "\n"
              << "Markup-named file download (expect body: markup-file-content):\n"
              << "  " << markupFileUrl << "\n"
              << "  curl -sS '" << markupFileUrl << "'\n"
#endif
              << "\n"
              << "Quick checks on the listing HTML:\n"
              << "  curl -sS '" << rootUrl << "' | grep -F '&lt;folder-name'\n"
              << "  curl -sS '" << rootUrl
              << "' | grep -F 'href=\"javascript:'   # should find nothing\n"
              << "\n"
              << "Server stays up for " << seconds << " seconds. Ctrl+C to stop earlier.\n"
              << "======================================================\n"
              << std::flush;

    std::this_thread::sleep_for(std::chrono::seconds(seconds));
}

TEST_F(SdkHttpServerLinkTest, LoginClientPublicFileLink)
{
    CASE_info << "started";

    const std::string fileLink = createPublicLink(0, mFileNode.get(), 0, maxTimeout, true);
    ASSERT_FALSE(fileLink.empty());

    auto& api = megaApi[1];

    auto rt = std::make_unique<RequestTracker>(api.get());
    api->getPublicNode(fileLink.c_str(), rt.get());
    EXPECT_EQ(API_OK, rt->waitForResult()) << "Public link retrieval failed";
    auto server = scopedHttpServer(api.get());
    ASSERT_TRUE(server);
    std::unique_ptr<char[]> link(api->httpServerGetLocalLink(rt->getPublicMegaNode().get()));
    ASSERT_NE(link, nullptr);
    std::string url = link.get();

    auto response = HttpClient::get(url, "1-5");
    EXPECT_EQ(206, response.statusCode);
    EXPECT_EQ(5, response.body.size());

    CASE_info << "finished";
}

TEST_F(SdkHttpServerLinkTest, NonLoginClientPublicFileLink)
{
    CASE_info << "started";

    const std::string fileLink = createPublicLink(0, mFileNode.get(), 0, maxTimeout, true);
    ASSERT_FALSE(fileLink.empty());

    auto api = makeNonLoginApi();
    ASSERT_NO_FATAL_FAILURE(runPublicFileLinkTest(api.get(), fileLink));

    CASE_info << "finished";
}

TEST_F(SdkHttpServerLinkTest, LoginClientPublicFolderLink)
{
    CASE_info << "started";

    const std::string folderLink = createPublicLink(0, mFolderNode.get(), 0, maxTimeout, true);
    ASSERT_FALSE(folderLink.empty());
    ASSERT_NO_FATAL_FAILURE(runPublicFolderLinkTest(megaApi[1].get(), folderLink));

    CASE_info << "finished";
}

TEST_F(SdkHttpServerLinkTest, NonLoginClientPublicFolderLink)
{
    CASE_info << "started";

    const std::string folderLink = createPublicLink(0, mFolderNode.get(), 0, maxTimeout, true);
    ASSERT_FALSE(folderLink.empty());
    auto api = makeNonLoginApi();
    ASSERT_NO_FATAL_FAILURE(runPublicFolderLinkTest(api.get(), folderLink));

    CASE_info << "finished";
}

TEST_F(SdkHttpServerLinkTest, LoginClientPublicSet)
{
    CASE_info << "started";

    auto exportedSetURL = createSetLink();
    ASSERT_FALSE(exportedSetURL.empty());

    ASSERT_NO_FATAL_FAILURE(runPublicSetTest(megaApi[1].get(), exportedSetURL));

    CASE_info << "finished";
}

TEST_F(SdkHttpServerLinkTest, NonLoginClientPublicSet)
{
    CASE_info << "started";

    auto exportedSetURL = createSetLink();
    ASSERT_FALSE(exportedSetURL.empty());

    auto api = makeNonLoginApi();
    ASSERT_NO_FATAL_FAILURE(runPublicSetTest(api.get(), exportedSetURL));

    CASE_info << "finished";
}
}
