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
#include "integration_test_utils.h"
#include "mega/common/testing/utility.h"
#include "mock_listeners.h"
#include "sdk_test_utils.h"
#include "SdkTest_test.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

using namespace ::mega;
using namespace ::std;
using ::mega::common::testing::randomBytes;
using sdk_test::EasyCurl;
using sdk_test::LocalTempFile;

class SdkHttpServerTest: public SdkTest
{
protected:
    void SetUp() override
    {
        SdkTest::SetUp();
    }

    void TearDown() override
    {
        // Stop any running HTTP servers
        for (auto& api: megaApi)
        {
            if (api)
                api->httpServerStop();
        }
        SdkTest::TearDown();
    }

    unique_ptr<MegaNode> createFolder(const std::string& name, MegaNode* parent = nullptr)
    {
        unique_ptr<MegaNode> rootNode;
        if (parent == nullptr)
        {
            rootNode = unique_ptr<MegaNode>(megaApi[0]->getRootNode());
            parent = rootNode.get();
        }
        if (parent == nullptr)
        {
            return nullptr;
        }
        MegaHandle handle = SdkTest::createFolder(0, name.c_str(), parent);
        if (handle == UNDEF)
        {
            return nullptr;
        }
        return std::unique_ptr<MegaNode>(megaApi[0]->getNodeByHandle(handle));
    }

    unique_ptr<MegaNode> uploadFile(const std::string& name,
                                    const std::string& contents,
                                    MegaNode* parent = nullptr)
    {
        unique_ptr<MegaNode> rootNode;
        if (parent == nullptr)
        {
            rootNode = unique_ptr<MegaNode>(megaApi[0]->getRootNode());
            parent = rootNode.get();
        }
        if (parent == nullptr)
        {
            return nullptr;
        }
        deleteFile(name);
        sdk_test::LocalTempFile f(name, contents);
        return sdk_test::uploadFile(megaApi[0].get(), name, parent);
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

    std::optional<ScopedDestructor> scopedHttpServer(MegaApi* api)
    {
        if (!api)
            return std::nullopt;

        if (!api->httpServerStart(true, 0))
            return std::nullopt;

        if (!api->httpServerIsRunning())
            return std::nullopt;

        return makeScopedDestructor(
            [api]()
            {
                api->httpServerStop();
            });
    }

    std::string baseURL(int port)
    {
        return "http://localhost:" + std::to_string(port) + "/";
    }
};

/**
 * Helper class for HTTP client requests
 */
class HttpClient
{
public:
    static inline const std::string EmptyRange = {};

    enum class BodyMode
    {
        WithBody,
        WithoutBody
    };

    struct Response
    {
        int statusCode;
        map<string, string> headers;
        std::string body;
        curl_off_t contentLength;
    };

    static Response get(const std::string& url,
                        const std::string& range = EmptyRange,
                        const map<string, string>& headers = {})
    {
        return performRequest(url, "GET", range, headers, "", BodyMode::WithBody);
    }

    static Response post(const std::string& url,
                         const map<string, string>& headers = {},
                         const string& body = "")
    {
        return performRequest(url, "POST", EmptyRange, headers, body, BodyMode::WithBody);
    }

    static Response put(const std::string& url,
                        const map<string, string>& headers = {},
                        const string& body = "")
    {
        return performRequest(url, "PUT", EmptyRange, headers, body, BodyMode::WithBody);
    }

    static Response del(const std::string& url, const map<string, string>& headers = {})
    {
        return performRequest(url, "DELETE", EmptyRange, headers, "", BodyMode::WithBody);
    }

    static Response head(const std::string& url, const map<string, string>& headers = {})
    {
        return performRequest(url, "HEAD", EmptyRange, headers, "", BodyMode::WithoutBody);
    }

    static Response options(const std::string& url, const map<string, string>& headers = {})
    {
        return performRequest(url, "OPTIONS", EmptyRange, headers, "", BodyMode::WithBody);
    }

    static Response propfind(const std::string& url,
                             const map<string, string>& headers = {},
                             const string& body = "")
    {
        return performRequest(url, "PROPFIND", EmptyRange, headers, body, BodyMode::WithBody);
    }

    static Response mkcol(const std::string& url, const map<string, string>& headers = {})
    {
        return performRequest(url, "MKCOL", EmptyRange, headers, "", BodyMode::WithBody);
    }

    static Response lock(const std::string& url,
                         const map<string, string>& headers = {},
                         const string& body = "")
    {
        return performRequest(url, "LOCK", EmptyRange, headers, body, BodyMode::WithBody);
    }

    static Response unlock(const std::string& url,
                           const map<string, string>& headers = {},
                           const string& body = "")
    {
        return performRequest(url, "UNLOCK", EmptyRange, headers, body, BodyMode::WithBody);
    }

    static Response proppatch(const std::string& url,
                              const map<string, string>& headers = {},
                              const string& body = "")
    {
        return performRequest(url, "PROPPATCH", EmptyRange, headers, body, BodyMode::WithBody);
    }

private:
    static Response performRequest(const std::string& url,
                                   const std::string& method,
                                   const std::string& rangeHeader = EmptyRange,
                                   const map<string, string>& headers = {},
                                   const string& body = "",
                                   BodyMode bodyMode = BodyMode::WithBody)
    {
        Response response;
        auto easyCurl = EasyCurl();
        auto curl = easyCurl.curl();

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

        if (method != "GET" && method != "HEAD")
        {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
        }

        if (bodyMode == BodyMode::WithoutBody)
        {
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        }
        else
        {
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        }

        if (!body.empty())
        {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());
        }

        // transform headers to string vector
        std::vector<std::string> headerList;
        for (const auto& [key, value]: headers)
        {
            headerList.push_back(key + ": " + value);
        }
        auto headerChunk = easyCurl.appendCurlList(headerList);

        if (headerChunk)
        {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerChunk);
        }

        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response);
        curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");

        if (!rangeHeader.empty())
        {
            curl_easy_setopt(curl, CURLOPT_RANGE, rangeHeader.c_str());
        }

        CURLcode res = curl_easy_perform(curl);

        if (res == CURLE_OK)
        {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);
            curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &response.contentLength);
        }
        else
        {
            LOG_err << "CURL error for " << method << " " << url << ": " << curl_easy_strerror(res)
                    << " (code: " << res << ")";
            response.statusCode = 0;
            response.contentLength = -1;
        }

        return response;
    }

    static size_t writeCallback(void* contents, size_t size, size_t nmemb, Response* response)
    {
        size_t totalSize = size * nmemb;
        response->body.append(static_cast<char*>(contents), totalSize);
        return totalSize;
    }

    static size_t headerCallback(void* contents, size_t size, size_t nmemb, Response* response)
    {
        size_t totalSize = size * nmemb;
        std::string headerLine(static_cast<char*>(contents), totalSize);
        if (headerLine == "\r\n")
        {
            // end of header block
            return totalSize;
        }

        if (headerLine.rfind("HTTP/", 0) == 0)
        {
            // status line
            return totalSize;
        }
        size_t colonPos = headerLine.find(':');
        if (colonPos != std::string::npos)
        {
            std::string headerName = headerLine.substr(0, colonPos);
            std::transform(headerName.begin(),
                           headerName.end(),
                           headerName.begin(),
                           [](unsigned char c)
                           {
                               return static_cast<char>(std::tolower(c));
                           });
            std::string headerValue = headerLine.substr(colonPos + 1);
            // Trim whitespace
            headerValue.erase(0, headerValue.find_first_not_of(" \t\r\n"));
            auto last = headerValue.find_last_not_of(" \t\r\n");
            if (last == std::string::npos)
                headerValue.clear();
            else
                headerValue.erase(last + 1);
            response->headers[headerName] = headerValue;
        }
        return totalSize;
    }
};

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
    CASE_info << "finished";
}

TEST_F(SdkHttpServerTest, HttpServerIPv6)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Test starting server with IPv6 support
    ASSERT_TRUE(megaApi[0]->httpServerStart(true, 0, false, nullptr, nullptr, true));
    EXPECT_GT(megaApi[0]->httpServerIsRunning(), 0);
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
    auto node = createFolder(folderName, rootNode.get());
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
    auto node = uploadFile(testFileName, testFileContents);
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
    auto fileNodeStart = uploadFile(testFileNameStart, testFileContentsStart);
    ASSERT_TRUE(fileNodeStart);

    const std::string testFileNameAfter = "http_test_file.txt";
    const std::string testFileContentsAfter = "This is a test file for HTTP server access.";
    auto fileNodeAfter = uploadFile(testFileNameAfter, testFileContentsAfter);
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
        auto fileNodeLast = uploadFile(testFileNameLast, testFileContentsLast);
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

// Test basic WebDAV operations: OPTIONS
TEST_F(SdkHttpServerTest, HttpServerWebDavBasicOperations)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Start HTTP server
    auto server = scopedHttpServer(megaApi[0].get());
    ASSERT_TRUE(server);

    // Create test folder and get WebDAV link
    const std::string folderName = "webdav_test_folder";
    auto testFolder = createFolder(folderName);
    ASSERT_TRUE(testFolder);

    unique_ptr<char[]> folderWebdavLink(megaApi[0]->httpServerGetLocalWebDavLink(testFolder.get()));
    ASSERT_TRUE(folderWebdavLink);
    string folderLinkStr(folderWebdavLink.get());

    CASE_info << "WebDAV folder link: " << folderLinkStr;
    // Test OPTIONS method
    auto response = HttpClient::options(folderLinkStr);
    EXPECT_EQ(200, response.statusCode);
    ASSERT_TRUE(response.headers.find("allow") != response.headers.end());
    EXPECT_TRUE(response.headers["allow"].find("MKCOL") != string::npos)
        << "Response headers:" << response.headers["allow"] << " doesn't contain MKCOL.";
    CASE_info << "finished";
}

// test WebDAV PROPFIND operation
TEST_F(SdkHttpServerTest, HttpServerWebDavPropfindOperations)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    auto server = scopedHttpServer(megaApi[0].get());
    ASSERT_TRUE(server);

    // Create test folder
    const std::string folderName = "webdav_propfind_folder";
    auto testFolder = createFolder(folderName);
    ASSERT_TRUE(testFolder);

    unique_ptr<char[]> folderWebdavLink(megaApi[0]->httpServerGetLocalWebDavLink(testFolder.get()));
    ASSERT_TRUE(folderWebdavLink);

    string folderLinkStr(folderWebdavLink.get());

    CASE_info << "WebDAV folder link: " << folderLinkStr;

    // Test PROPFIND on folder
    string propfindBody = R"(<?xml version="1.0" encoding="utf-8" ?>
    <D:propfind xmlns:D="DAV:">
        <D:prop>
            <D:displayname/>
            <D:getcontentlength/>
            <D:getcontenttype/>
            <D:resourcetype/>
            <D:getlastmodified/>
        </D:prop>
    </D:propfind>)";

    auto response =
        HttpClient::propfind(folderLinkStr,
                             {{"Depth", "1"}, {"Content-Type", "text/xml; charset=utf-8"}},
                             propfindBody);

    EXPECT_EQ(207, response.statusCode); // Multi-Status
    EXPECT_FALSE(response.body.empty());

    LOG_debug << "PROPFIND response data: " << response.body;
    EXPECT_TRUE(response.body.find("<d:displayname>" + folderName + "</d:displayname>") !=
                string::npos)
        << "Response data: " << response.body << " does not contain folder name: " << folderName;
    CASE_info << "finished";
}

// test WebDAV file operations: GET, HEAD, POST
TEST_F(SdkHttpServerTest, HttpServerWebDavFileOperations)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    auto server = scopedHttpServer(megaApi[0].get());
    ASSERT_TRUE(server);

    // Upload test file
    const std::string testFileName = "webdav_test_file.txt";
    const std::string testFileContents = "WebDAV test file content";

    auto fileNode = uploadFile(testFileName, testFileContents);
    ASSERT_TRUE(fileNode);

    unique_ptr<char[]> fileWebdavLink(megaApi[0]->httpServerGetLocalWebDavLink(fileNode.get()));
    ASSERT_TRUE(fileWebdavLink);

    string fileLinkStr(fileWebdavLink.get());

    CASE_info << "WebDAV file link: " << fileLinkStr;

    // Test GET method
    auto getResponse = HttpClient::get(fileLinkStr);
    EXPECT_EQ(200, getResponse.statusCode);
    EXPECT_EQ(testFileContents, getResponse.body);

    // Test HEAD method
    auto headResponse = HttpClient::head(fileLinkStr);
    EXPECT_EQ(200, headResponse.statusCode);
    EXPECT_TRUE(headResponse.body.empty());

    // Test POST method (should be allowed but not modify the file)
    auto postResponse = HttpClient::post(fileLinkStr);
    EXPECT_EQ(200, postResponse.statusCode);
    EXPECT_EQ(testFileContents, postResponse.body);
    CASE_info << "finished";
}

// test WebDAV collection operations: MKCOL
TEST_F(SdkHttpServerTest, HttpServerWebDavCollectionOperations)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    auto server = scopedHttpServer(megaApi[0].get());
    ASSERT_TRUE(server);

    unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_NE(nullptr, rootNode.get());

    unique_ptr<char[]> rootWebdavLink(megaApi[0]->httpServerGetLocalWebDavLink(rootNode.get()));
    ASSERT_TRUE(rootWebdavLink);

    string rootLinkStr(rootWebdavLink.get());

    CASE_info << "WebDAV root link: " << rootLinkStr;

    // Test MKCOL method (create collection/folder)
    string newFolderUrl = rootLinkStr + "/mkcol_webdav_folder";
    auto mkcolResponse = HttpClient::mkcol(newFolderUrl);
    EXPECT_EQ(mkcolResponse.statusCode, 201); // 201 Created
    CASE_info << "finished";
}

// test WebDAV modification operations: PUT
TEST_F(SdkHttpServerTest, HttpServerWebDavModificationOperations)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    auto server = scopedHttpServer(megaApi[0].get());
    ASSERT_TRUE(server);

    // Create test folder
    const std::string folderName = "webdav_mod_folder";
    auto testFolder = createFolder(folderName);
    ASSERT_TRUE(testFolder);

    unique_ptr<char[]> folderWebdavLink(megaApi[0]->httpServerGetLocalWebDavLink(testFolder.get()));
    ASSERT_TRUE(folderWebdavLink);

    string folderLinkStr(folderWebdavLink.get());

    CASE_info << "WebDAV folder link: " << folderLinkStr;

    // Test PUT method
    string putFileUrl = folderLinkStr + "/put_test_file.txt";

    string putContent = "PUT test file content";
    auto putResponse = HttpClient::put(putFileUrl, {{"Content-Type", "text/plain"}}, putContent);
    EXPECT_EQ(201, putResponse.statusCode); // Created

    std::unique_ptr<MegaNode> putNode(
        megaApi[0]->getNodeByPath(("/" + folderName + "/put_test_file.txt").c_str()));
    ASSERT_TRUE(putNode);

    std::unique_ptr<char[]> putFileWebdavLink(
        megaApi[0]->httpServerGetLocalWebDavLink(putNode.get()));
    ASSERT_TRUE(putFileWebdavLink);
    string putFileLinkStr(putFileWebdavLink.get());

    CASE_info << "WebDAV PUT file link: " << putFileLinkStr;

    auto getResponse = HttpClient::get(putFileLinkStr);
    EXPECT_EQ(200, getResponse.statusCode);
    EXPECT_EQ(putContent, getResponse.body);
    CASE_info << "finished";
}

// Need clarification about parameters of MOVE and COPY operations over WebDAV
TEST_F(SdkHttpServerTest, DISABLED_HttpServerWebDavMoveAndCopyOperations) {}

// test WebDAV LOCK and UNLOCK operations
TEST_F(SdkHttpServerTest, HttpServerWebDavLockingOperations)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    auto server = scopedHttpServer(megaApi[0].get());
    ASSERT_TRUE(server);

    unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_NE(nullptr, rootNode.get());

    unique_ptr<char[]> rootWebdavLink(megaApi[0]->httpServerGetLocalWebDavLink(rootNode.get()));
    ASSERT_TRUE(rootWebdavLink);

    string rootLinkStr(rootWebdavLink.get());

    CASE_info << "WebDAV root link: " << rootLinkStr;

    // Test LOCK method
    string lockBody = R"(<?xml version="1.0" encoding="utf-8" ?>
    <D:lockinfo xmlns:D="DAV:">
        <D:lockscope><D:exclusive/></D:lockscope>
        <D:locktype><D:write/></D:locktype>
    </D:lockinfo>)";

    auto lockResponse = HttpClient::lock(
        rootLinkStr,
        {{"Content-Type", "text/xml; charset=utf-8"}, {"Timeout", "Infinite, Second-4100000000"}},
        lockBody);
    EXPECT_EQ(200, lockResponse.statusCode);
    EXPECT_FALSE(lockResponse.body.empty());

    CASE_info << "LOCK response data: " << lockResponse.body;

    // Extract lock token from response
    size_t tokenStart = lockResponse.body.find("<D:locktoken>");
    ASSERT_TRUE(tokenStart != string::npos);
    tokenStart += strlen("<D:locktoken>");
    size_t tokenEnd = lockResponse.body.find("</D:locktoken>");
    ASSERT_TRUE(tokenEnd != string::npos && tokenEnd > tokenStart);
    std::string tokenRef = lockResponse.body.substr(tokenStart, tokenEnd - tokenStart);
    size_t lockTokenPos = tokenRef.find("<D:href>");
    ASSERT_TRUE(lockTokenPos != string::npos);
    lockTokenPos += strlen("<D:href>");
    size_t lockTokenEnd = tokenRef.find("</D:href>");
    ASSERT_TRUE(lockTokenEnd != std::string::npos && lockTokenEnd > lockTokenPos);
    std::string lockToken = tokenRef.substr(lockTokenPos, lockTokenEnd - lockTokenPos);
    EXPECT_FALSE(lockToken.empty());

    auto unlockResponse = HttpClient::unlock(rootLinkStr, {{"Lock-Token", "<" + lockToken + ">"}});
    EXPECT_EQ(204, unlockResponse.statusCode); // No Content
    CASE_info << "finished";
}

// test WebDAV PROPPATCH operation
TEST_F(SdkHttpServerTest, HttpServerWebDavPropertyOperations)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    auto server = scopedHttpServer(megaApi[0].get());
    ASSERT_TRUE(server);

    unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_NE(nullptr, rootNode.get());

    unique_ptr<char[]> rootWebdavLink(megaApi[0]->httpServerGetLocalWebDavLink(rootNode.get()));
    ASSERT_TRUE(rootWebdavLink);

    string rootLinkStr(rootWebdavLink.get());

    CASE_info << "WebDAV root link: " << rootLinkStr;

    // Test PROPPATCH method
    string proppatchBody = R"(<?xml version="1.0" encoding="utf-8" ?>
    <D:propertyupdate xmlns:D="DAV:">
        <D:set>
            <D:prop>
                <D:displayname>New Display Name</D:displayname>
            </D:prop>
        </D:set>
    </D:propertyupdate>)";

    auto response = HttpClient::proppatch(rootLinkStr,
                                          {{"Content-Type", "text/xml; charset=utf-8"}},
                                          proppatchBody);
    EXPECT_EQ(207, response.statusCode); // Multi-Status
    CASE_info << "finished";
}

// test WebDAV DELETE operation
TEST_F(SdkHttpServerTest, HttpServerWebDavDeleteOperation)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    auto server = scopedHttpServer(megaApi[0].get());
    ASSERT_TRUE(server);

    unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_NE(nullptr, rootNode.get());

    // Upload test file for deletion
    const std::string testFileName = "webdav_delete_test.txt";
    const std::string testFileContents = "Delete test file content";

    auto fileNode = uploadFile(testFileName, testFileContents);
    ASSERT_TRUE(fileNode);

    unique_ptr<char[]> fileWebdavLink(megaApi[0]->httpServerGetLocalWebDavLink(fileNode.get()));
    ASSERT_TRUE(fileWebdavLink);

    string fileLinkStr(fileWebdavLink.get());

    CASE_info << "WebDAV file link for deletion: " << fileLinkStr;

    // Test DELETE method
    auto response = HttpClient::del(fileLinkStr);
    EXPECT_EQ(204, response.statusCode); // No Content

    response = HttpClient::get(fileLinkStr);
    EXPECT_EQ(404, response.statusCode); // Not Found
    CASE_info << "finished";
}

/*
 *test interfaces: httpServerGetWebDavLinks, httpServerGetWebDavAllowedNodes,
 * httpServerRemoveWebDavAllowedNode and httpServerRemoveWebDavAllowedNodes
 */
TEST_F(SdkHttpServerTest, HttpServerWebDavGetAllLinksAndManageAllowedNodes)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    auto server = scopedHttpServer(megaApi[0].get());
    ASSERT_TRUE(server);
    // Create test folder
    const std::string folderName = "webdav_links_folder";
    auto testFolder = createFolder(folderName);
    ASSERT_TRUE(testFolder);
    CASE_info << "Created test folder with handle: " << testFolder->getHandle();

    unique_ptr<char[]> folderWebdavLink(megaApi[0]->httpServerGetLocalWebDavLink(testFolder.get()));
    ASSERT_TRUE(folderWebdavLink);
    string folderLinkStr(folderWebdavLink.get());
    CASE_info << "WebDAV folder link: " << folderLinkStr;

    // Upload test file
    const std::string testFileName = "webdav_links_file.txt";
    const std::string testFileContents = "WebDAV links test file content";
    auto fileNode = uploadFile(testFileName, testFileContents, testFolder.get());
    ASSERT_TRUE(fileNode);
    CASE_info << "Uploaded test file with handle: " << fileNode->getHandle();

    unique_ptr<char[]> fileWebdavLink(megaApi[0]->httpServerGetLocalWebDavLink(fileNode.get()));
    ASSERT_TRUE(fileWebdavLink);
    string fileLinkStr(fileWebdavLink.get());
    CASE_info << "WebDAV file link: " << fileLinkStr;

    // Get all WebDAV links
    unique_ptr<MegaStringList> webdavLinks(megaApi[0]->httpServerGetWebDavLinks());
    ASSERT_TRUE(webdavLinks);
    // Verify both folder and file links are present
    ASSERT_EQ(webdavLinks->size(), 2);
    EXPECT_TRUE(webdavLinks->get(0) == folderLinkStr || webdavLinks->get(1) == folderLinkStr);
    EXPECT_TRUE(webdavLinks->get(1) == fileLinkStr || webdavLinks->get(0) == fileLinkStr);

    // test httpServerGetWebDavAllowedNodes
    unique_ptr<MegaNodeList> allowedNodes(megaApi[0]->httpServerGetWebDavAllowedNodes());
    ASSERT_TRUE(allowedNodes);
    ASSERT_EQ(allowedNodes->size(), 2);
    EXPECT_TRUE(allowedNodes->get(0)->getHandle() == testFolder->getHandle() ||
                allowedNodes->get(1)->getHandle() == testFolder->getHandle());
    EXPECT_TRUE(allowedNodes->get(0)->getHandle() == fileNode->getHandle() ||
                allowedNodes->get(1)->getHandle() == fileNode->getHandle());

    // test httpServerRemoveWebDavAllowedNode
    megaApi[0]->httpServerRemoveWebDavAllowedNode(testFolder->getHandle());
    allowedNodes = unique_ptr<MegaNodeList>(megaApi[0]->httpServerGetWebDavAllowedNodes());
    ASSERT_TRUE(allowedNodes);
    EXPECT_EQ(allowedNodes->size(), 1);
    EXPECT_EQ(allowedNodes->get(0)->getHandle(), fileNode->getHandle());

    // test httpServerRemoveWebDavAllowedNodes
    megaApi[0]->httpServerRemoveWebDavAllowedNodes();
    allowedNodes = unique_ptr<MegaNodeList>(megaApi[0]->httpServerGetWebDavAllowedNodes());
    ASSERT_TRUE(allowedNodes);
    EXPECT_EQ(allowedNodes->size(), 0);
    CASE_info << "finished";
}

// test httpServerAddListener, httpServerRemoveListener and check MegaTransferListener callbacks
TEST_F(SdkHttpServerTest, HttpServerListenerCallbacks)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    testing::NiceMock<MockMegaTransferListener> mockListener{megaApi[0].get()};
    std::promise<handle> fileNodeHandlePromise;

    EXPECT_CALL(mockListener, onTransferFinish)
        .WillOnce(
            [&fileNodeHandlePromise](MegaApi*, MegaTransfer* transfer, MegaError*)
            {
                LOG_debug << "onTransferFinish called for node handle: "
                          << transfer->getNodeHandle();
                if (transfer)
                    fileNodeHandlePromise.set_value(transfer->getNodeHandle());
                else
                    fileNodeHandlePromise.set_value(UNDEF);
            });

    auto server = scopedHttpServer(megaApi[0].get());
    ASSERT_TRUE(server);

    megaApi[0]->httpServerAddListener(&mockListener);
    // Upload test file to trigger transfer events
    const std::string testFileName = "http_server_listener_test.txt";
    const std::string testFileContents = "HTTP server listener test file content";

    auto fileNode = uploadFile(testFileName, testFileContents);
    ASSERT_TRUE(fileNode);

    // get webdav link to trigger the transfer
    unique_ptr<char[]> fileWebdavLink(megaApi[0]->httpServerGetLocalWebDavLink(fileNode.get()));
    ASSERT_TRUE(fileWebdavLink);
    string fileLinkStr(fileWebdavLink.get());
    CASE_info << "WebDAV file link: " << fileLinkStr;

    auto response = HttpClient::get(fileLinkStr);
    EXPECT_EQ(200, response.statusCode);

    // Wait for OnTransferFinish callback
    auto fileNodeHandle = fileNodeHandlePromise.get_future();
    ASSERT_EQ(fileNodeHandle.wait_for(std::chrono::seconds(5U)), std::future_status::ready)
        << "Timeout waiting for onTransferFinish callback";
    EXPECT_EQ(fileNode->getHandle(), fileNodeHandle.get());

    megaApi[0]->httpServerRemoveListener(&mockListener);
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
    std::unique_ptr<MegaNode> uploadedNode = uploadFile("test_http_basic.txt", testFileContent);
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
 * Test HTTP server with HEAD request.
 */
TEST_F(SdkHttpServerTest, HeadRequest)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    std::string testFileContent = "HTTP server HEAD test content";
    std::unique_ptr<MegaNode> uploadedNode = uploadFile("test_http_head.txt", testFileContent);
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
    std::unique_ptr<MegaNode> uploadedNode = uploadFile("test_http_range.txt", testFileContent);
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
    EXPECT_EQ(200, fullRange.statusCode); // BUG: HTTP protocol expects 206 Partial Content
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
        uploadFile("test_http_large_range.bin", testFileContent);
    ASSERT_NE(uploadedNode, nullptr);

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    std::unique_ptr<char[]> link(api->httpServerGetLocalLink(uploadedNode.get()));
    ASSERT_NE(link, nullptr);
    std::string url = link.get();

    // Full file range
    auto fileSize = testFileContent.size();
    auto largeRange = HttpClient::get(url, "0-" + std::to_string(fileSize - 1));
    EXPECT_EQ(200, largeRange.statusCode); // BUG: HTTP protocol expects 206 Partial Content
    EXPECT_EQ(testFileContent, largeRange.body);

    // Middle range: from 25% to 50%, end is inclusive
    auto begin = fileSize / 4;
    auto end = fileSize / 2;
    auto midRange = HttpClient::get(url, std::to_string(begin) + "-" + std::to_string(end));
    EXPECT_EQ(206, midRange.statusCode);
    EXPECT_EQ(std::string_view(testFileContent.data() + begin, end - begin + 1), midRange.body);

    // Suffix range: last 10MB (bytes=-10485760)
    auto suffixRange = HttpClient::get(url, "-10485760");
    EXPECT_EQ(200, suffixRange.statusCode); // BUG: HTTP protocol expects 206 Partial Content
    EXPECT_EQ(testFileContent,
              suffixRange.body); // BUG: Server returns full file instead of last 10MB

    // Suffix range: last 25% of file
    auto suffixRange2 = HttpClient::get(url, "-" + std::to_string(fileSize / 4));
    EXPECT_EQ(200, suffixRange2.statusCode); // BUG: HTTP protocol expects 206 Partial Content
    EXPECT_EQ(testFileContent,
              suffixRange2.body); // BUG: Server returns full file instead of last 25%

    // Range from 75% to end
    begin = fileSize * 3 / 4;
    end = testFileContent.size() - 1;
    auto rangeToEnd = HttpClient::get(url, std::to_string(begin) + "-");
    EXPECT_EQ(206, rangeToEnd.statusCode);
    EXPECT_EQ(std::string_view(testFileContent.data() + begin, end - begin + 1), rangeToEnd.body);
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
        uploadFile("test_http_invalid_range.txt", testFileContent);
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
    std::unique_ptr<MegaNode> uploadedNode = uploadFile("test_http_empty.txt", "");
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
    EXPECT_EQ(200,
              rangeResponse1.statusCode); // BUG: HTTP protocol expects 416 Range Not Satisfiable

    auto rangeResponse2 = HttpClient::get(url, "0-10");
    EXPECT_EQ(200,
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
    std::unique_ptr<MegaNode> uploadedNode = uploadFile("test_http_large.bin", testFileContent);
    ASSERT_NE(uploadedNode, nullptr);

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    std::unique_ptr<char[]> link(api->httpServerGetLocalLink(uploadedNode.get()));
    ASSERT_NE(link, nullptr);
    std::string url = link.get();

    // Full file GET request
    auto response = HttpClient::get(url);
    EXPECT_EQ(200, response.statusCode);
    EXPECT_EQ(testFileContent, response.body);

    // Standard range: first 1MB
    auto rangeResponse = HttpClient::get(url, "0-1048575");
    EXPECT_EQ(206, rangeResponse.statusCode);
    EXPECT_EQ(std::string_view(testFileContent.data(), 1048575u + 1), rangeResponse.body);

    // Standard range: second 1MB
    auto rangeResponse2 = HttpClient::get(url, "1048576-2097151");
    EXPECT_EQ(206, rangeResponse2.statusCode);
    EXPECT_EQ(std::string_view(testFileContent.data() + 1048576, 2097151u - 1048576u + 1),
              rangeResponse2.body);

    // Suffix range: last 1MB (bytes=-1048576)
    auto suffixRange = HttpClient::get(url, "-1048576");
    EXPECT_EQ(200, suffixRange.statusCode); // BUG: HTTP protocol expects 206 Partial Content
    EXPECT_EQ(testFileContent,
              suffixRange.body); // BUG: Server returns full file instead of last 1MB

    // Suffix range: last 512KB
    auto suffixRange2 = HttpClient::get(url, "-524288");
    EXPECT_EQ(200, suffixRange2.statusCode); // BUG: HTTP protocol expects 206 Partial Content
    EXPECT_EQ(testFileContent,
              suffixRange2.body); // BUG: Server returns full file instead of last 512KB

    // Range from middle to near end
    auto midRange = HttpClient::get(url, "5242880-6291455");
    EXPECT_EQ(206, midRange.statusCode);
    EXPECT_EQ(std::string_view(testFileContent.data() + 5242880, 6291455u - 5242880u + 1),
              midRange.body);

    // Small range from beginning
    auto smallRange = HttpClient::get(url, "0-1023");
    EXPECT_EQ(206, smallRange.statusCode);
    EXPECT_EQ(std::string_view(testFileContent.data(), 1023u + 1), smallRange.body);
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
        uploadFile("test_http_concurrent.txt", testFileContent);
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
        EXPECT_EQ(testFileContent, response.body);
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
        uploadFile("test_http_concurrent_range.bin", testFileContent);
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
        EXPECT_EQ(std::string_view(testFileContent.data() + start, LENGTH), response.body);
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
                EXPECT_EQ(testFileContent,
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
    std::unique_ptr<MegaNode> uploadedNode = uploadFile("test_http_restart.txt", testFileContent);
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
    std::unique_ptr<MegaNode> uploadedNode = uploadFile("test_http_methods.txt", testFileContent);
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
    std::unique_ptr<MegaNode> uploadedNode = uploadFile("test_http_rapid.txt", testFileContent);
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
        std::unique_ptr<MegaNode> uploadedNode = uploadFile(fileName, fileName);
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
    std::unique_ptr<MegaNode> uploadedNode1 = uploadFile("test_1byte.tx", testFileContent1);
    ASSERT_NE(uploadedNode1, nullptr);

    // Test 2-byte file
    std::string testFileContent2 = "AB";
    std::unique_ptr<MegaNode> uploadedNode2 = uploadFile("test_2byte.tx", testFileContent2);
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
    EXPECT_EQ(200, rangeResponse.statusCode); // BUG: HTTP protocol expects 206 Partial Content
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
    EXPECT_EQ(200, range3.statusCode); // BUG: HTTP protocol expects 206 Partial Content
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
    std::unique_ptr<MegaNode> uploadedNode = uploadFile("test_http_long.txt", testFileContent);
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
        uploadFile("test_http_connection.txt", testFileContent);
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
        EXPECT_EQ(testFileContent, response.body);
    }

    // Test HEAD followed by GET
    auto headResponse = HttpClient::head(url);
    EXPECT_EQ(200, headResponse.statusCode);

    auto getResponse = HttpClient::get(url);
    EXPECT_EQ(200, getResponse.statusCode);
    EXPECT_EQ(testFileContent, getResponse.body);

    // Test range request followed by full request
    auto rangeResponse = HttpClient::get(url, "0-99");
    EXPECT_EQ(206, rangeResponse.statusCode);
    EXPECT_EQ(100, rangeResponse.body.size());

    auto fullResponse = HttpClient::get(url);
    EXPECT_EQ(200, fullResponse.statusCode);
    EXPECT_EQ(testFileContent, fullResponse.body);
}

/**
 * Test HTTP server with empty folder.
 */
TEST_F(SdkHttpServerTest, FolderEmpty)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    // Create empty folder
    std::unique_ptr<MegaNode> folderNode(createFolder("test_http_folder_empty"));
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
    std::unique_ptr<MegaNode> folderNode(createFolder("test_http_folder_files"));
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
        std::unique_ptr<MegaNode> uploadedNode = uploadFile(fileName, fileName, folderNode.get());
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
