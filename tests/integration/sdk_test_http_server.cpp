/**
 * @brief Mega SDK test file for server implementations (TCP, HTTP)
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

#include "integration_test_utils.h"
#include "mock_listeners.h"
#include "SdkTest_test.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <future>
#include <map>
#include <string>

using namespace ::mega;
using namespace ::std;

class HttpServerTest: public SdkTest
{
protected:
    static void SetUpTestSuite()
    {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    static void TearDownTestSuite()
    {
        curl_global_cleanup();
    }

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

    struct HttpResponse
    {
        string data;
        map<string, string> headers;
        long responseCode = 0;
    };

    static size_t writeCallback(void* contents, size_t size, size_t nmemb, HttpResponse* response)
    {
        size_t totalSize = size * nmemb;
        response->data.append(static_cast<char*>(contents), totalSize);
        return totalSize;
    }

    static size_t headerCallback(void* contents, size_t size, size_t nmemb, HttpResponse* response)
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

    HttpResponse performHttpRequest(const string& url,
                                    const string& method = "GET",
                                    const string& body = "",
                                    const map<string, string>& headers = {})
    {
        HttpResponse response;
        CURL* curl = curl_easy_init();

        if (!curl)
        {
            LOG_debug << "Failed to initialize CURL";
            response.responseCode = -1;
            return response;
        }
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
        if (!body.empty())
        {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());
        }
        struct curl_slist* chunk = nullptr;
        for (const auto& header: headers)
        {
            std::string headerLine = header.first + ": " + header.second;
            chunk = curl_slist_append(chunk, headerLine.c_str());
        }
        if (chunk)
        {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
        }

        curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

        if (method == "HEAD")
        {
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        }
        char errbuf[CURL_ERROR_SIZE]{};
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

        CURLcode res = curl_easy_perform(curl);
        LOG_debug << "HTTP request to " << url << " completed with code " << res;
        if (res == CURLE_OK)
        {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.responseCode);
        }
        else
        {
            response.responseCode = -1;
            LOG_err << "curl error: " << res << " " << errbuf;
        }
        if (chunk)
        {
            curl_slist_free_all(chunk);
        }
        curl_easy_cleanup(curl);

        return response;
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
};

TEST_F(HttpServerTest, HttpServerStartStop)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Test starting HTTP server with default port
    ASSERT_TRUE(megaApi[0]->httpServerStart(true, 0));

    // Verify server is running
    EXPECT_TRUE(megaApi[0]->httpServerIsRunning());

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
TEST_F(HttpServerTest, HttpServerCanUsePort0)
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

TEST_F(HttpServerTest, HttpServerStartNotOnLocal)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Test starting server not limited to localhost
    ASSERT_TRUE(megaApi[0]->httpServerStart(false, 0));
    EXPECT_TRUE(megaApi[0]->httpServerIsRunning());
    EXPECT_FALSE(megaApi[0]->httpServerIsLocalOnly());
    CASE_info << "finished";
}

TEST_F(HttpServerTest, HttpServerIPv6)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Test starting server with IPv6 support
    ASSERT_TRUE(megaApi[0]->httpServerStart(true, 0, false, nullptr, nullptr, true));
    EXPECT_TRUE(megaApi[0]->httpServerIsRunning());
    CASE_info << "finished";
}

TEST_F(HttpServerTest, EnableOfflineAttribute)
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

TEST_F(HttpServerTest, httpServerEnableSubtitlesSupport)
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

TEST_F(HttpServerTest, httpServerSetMaxBufferSize)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Test setting and getting max buffer size
    int testSize = 2 * 1024 * 1024; // 2 MB
    megaApi[0]->httpServerSetMaxBufferSize(testSize);
    EXPECT_EQ(testSize, megaApi[0]->httpServerGetMaxBufferSize());
    CASE_info << "finished";
}

TEST_F(HttpServerTest, httpServerSetMaxOutputSize)
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
TEST_F(HttpServerTest, HttpServerDirectoryListing)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    megaApi[0]->httpServerEnableFolderServer(false);
    EXPECT_FALSE(megaApi[0]->httpServerIsFolderServerEnabled());
    ASSERT_TRUE(megaApi[0]->httpServerStart(true, 0));

    unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_NE(nullptr, rootNode.get());

    unique_ptr<char[]> localLink(megaApi[0]->httpServerGetLocalLink(rootNode.get()));
    ASSERT_TRUE(localLink);

    CASE_info << "Performing HTTP request to folder link" << localLink.get();

    auto response = performHttpRequest(localLink.get());
    EXPECT_EQ(403, response.responseCode);

    megaApi[0]->httpServerEnableFolderServer(true);
    EXPECT_TRUE(megaApi[0]->httpServerIsFolderServerEnabled());

    response = performHttpRequest(localLink.get());
    EXPECT_EQ(200, response.responseCode);
    EXPECT_FALSE(response.data.empty());

    const std::string folderName = "subfolder";
    auto node = createFolder(folderName, rootNode.get());
    ASSERT_TRUE(node);
    localLink.reset(megaApi[0]->httpServerGetLocalLink(node.get()));
    ASSERT_TRUE(localLink);

    CASE_info << "Performing HTTP request to subfolder link" << localLink.get();

    response = performHttpRequest(localLink.get());
    EXPECT_EQ(200, response.responseCode);

    EXPECT_TRUE(response.data.find(folderName) != string::npos)
        << "Response data: " << response.data;
    CASE_info << "finished";
}

// test httpServerEnableFileServer and httpServerGetLocalLink for files
TEST_F(HttpServerTest, HttpServerFileAccess)
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

    ASSERT_TRUE(megaApi[0]->httpServerStart(true, 0));

    unique_ptr<char[]> localLink(megaApi[0]->httpServerGetLocalLink(node.get()));
    ASSERT_TRUE(localLink);

    CASE_info << "Performing HTTP request to file link " << localLink.get();

    HttpResponse response = performHttpRequest(localLink.get());
    EXPECT_EQ(403, response.responseCode);

    megaApi[0]->httpServerEnableFileServer(true);
    EXPECT_TRUE(megaApi[0]->httpServerIsFileServerEnabled());
    response = performHttpRequest(localLink.get());

    EXPECT_EQ(200, response.responseCode);
    EXPECT_EQ(testFileContents, response.data);
    CASE_info << "finished";
}

// test httpServerSetRestrictedMode and httpServerGetRestrictedMode
TEST_F(HttpServerTest, GetSetRestrictedMode)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // default mode is ALLOW_CREATED_LOCAL_LINKS
    EXPECT_EQ(MegaApi::HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS,
              megaApi[0]->httpServerGetRestrictedMode());
    megaApi[0]->httpServerEnableFileServer(true);

    ASSERT_TRUE(megaApi[0]->httpServerStart(true, 0));

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
        auto fileStartResponse = performHttpRequest(fileLinkStartStr);
        EXPECT_EQ(403, fileStartResponse.responseCode);
        auto fileAfterResponse = performHttpRequest(fileLinkAfterStr);
        EXPECT_EQ(403, fileAfterResponse.responseCode);
    }

    // test restricted modes MegaApi::HTTP_SERVER_ALLOW_ALL
    {
        megaApi[0]->httpServerSetRestrictedMode(MegaApi::HTTP_SERVER_ALLOW_ALL);
        EXPECT_EQ(MegaApi::HTTP_SERVER_ALLOW_ALL, megaApi[0]->httpServerGetRestrictedMode());
        auto fileStartResponse = performHttpRequest(fileLinkStartStr);
        EXPECT_EQ(200, fileStartResponse.responseCode);
        auto fileAfterResponse = performHttpRequest(fileLinkAfterStr);
        EXPECT_EQ(200, fileAfterResponse.responseCode);
    }

    // test restricted modes MegaApi::HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS
    {
        megaApi[0]->httpServerSetRestrictedMode(MegaApi::HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS);
        EXPECT_EQ(MegaApi::HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS,
                  megaApi[0]->httpServerGetRestrictedMode());
        auto fileStartResponse = performHttpRequest(fileLinkStartStr);
        EXPECT_EQ(403, fileStartResponse.responseCode);
        auto fileAfterResponse = performHttpRequest(fileLinkAfterStr);
        EXPECT_EQ(200, fileAfterResponse.responseCode);
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
        auto fileStartResponse = performHttpRequest(fileLinkStartStr);
        EXPECT_EQ(403, fileStartResponse.responseCode);
        auto fileAfterResponse = performHttpRequest(fileLinkAfterStr);
        EXPECT_EQ(403, fileAfterResponse.responseCode);
        auto fileLastResponse = performHttpRequest(fileLinkLast.get());
        EXPECT_EQ(200, fileLastResponse.responseCode);
    }

    // test invalid restricted mode value (should not change)
    megaApi[0]->httpServerSetRestrictedMode(99);
    EXPECT_EQ(MegaApi::HTTP_SERVER_ALLOW_LAST_LOCAL_LINK,
              megaApi[0]->httpServerGetRestrictedMode());
    CASE_info << "finished";
}

TEST_F(HttpServerTest, HttpServerMultipleStartStop)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Test multiple start/stop cycles
    for (int i = 0; i < 3; ++i)
    {
        ASSERT_TRUE(megaApi[0]->httpServerStart(true, 0));

        EXPECT_TRUE(megaApi[0]->httpServerIsRunning());

        megaApi[0]->httpServerStop();

        EXPECT_FALSE(megaApi[0]->httpServerIsRunning());
    }
    CASE_info << "finished";
}

TEST_F(HttpServerTest, HttpServerWebDavBasicOperations)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Start HTTP server
    ASSERT_TRUE(megaApi[0]->httpServerStart(true, 0));

    // Create test folder and get WebDAV link
    const std::string folderName = "webdav_test_folder";
    auto testFolder = createFolder(folderName);
    ASSERT_TRUE(testFolder);

    unique_ptr<char[]> folderWebdavLink(megaApi[0]->httpServerGetLocalWebDavLink(testFolder.get()));
    ASSERT_TRUE(folderWebdavLink);
    string folderLinkStr(folderWebdavLink.get());

    CASE_info << "WebDAV folder link: " << folderLinkStr;
    // Test OPTIONS method
    auto response = performHttpRequest(folderLinkStr, "OPTIONS");
    EXPECT_EQ(200, response.responseCode);
    ASSERT_TRUE(response.headers.find("allow") != response.headers.end());
    EXPECT_TRUE(response.headers["allow"].find("MKCOL") != string::npos);
    CASE_info << "finished";
}

TEST_F(HttpServerTest, HttpServerWebDavPropfindOperations)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    ASSERT_TRUE(megaApi[0]->httpServerStart(true, 0));

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
        performHttpRequest(folderLinkStr,
                           "PROPFIND",
                           propfindBody,
                           {{"Depth", "1"}, {"Content-Type", "text/xml; charset=utf-8"}});

    EXPECT_EQ(207, response.responseCode); // Multi-Status
    EXPECT_FALSE(response.data.empty());

    LOG_debug << "PROPFIND response data: " << response.data;
    EXPECT_TRUE(response.data.find("<d:displayname>" + folderName + "</d:displayname>") !=
                string::npos)
        << "Response data: " << response.data;
    CASE_info << "finished";
}

TEST_F(HttpServerTest, HttpServerWebDavFileOperations)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    ASSERT_TRUE(megaApi[0]->httpServerStart(true, 0));

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
    auto getResponse = performHttpRequest(fileLinkStr);
    EXPECT_EQ(200, getResponse.responseCode);
    EXPECT_EQ(testFileContents, getResponse.data);

    // Test HEAD method
    auto headResponse = performHttpRequest(fileLinkStr, "HEAD");
    EXPECT_EQ(200, headResponse.responseCode);
    EXPECT_TRUE(headResponse.data.empty());

    // Test POST method (should be allowed but not modify the file)
    auto postResponse = performHttpRequest(fileLinkStr, "POST");
    EXPECT_EQ(200, postResponse.responseCode);
    EXPECT_EQ(testFileContents, postResponse.data);
    CASE_info << "finished";
}

TEST_F(HttpServerTest, HttpServerWebDavCollectionOperations)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    ASSERT_TRUE(megaApi[0]->httpServerStart(true, 0));

    unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_NE(nullptr, rootNode.get());

    unique_ptr<char[]> rootWebdavLink(megaApi[0]->httpServerGetLocalWebDavLink(rootNode.get()));
    ASSERT_TRUE(rootWebdavLink);

    string rootLinkStr(rootWebdavLink.get());

    CASE_info << "WebDAV root link: " << rootLinkStr;

    // Test MKCOL method (create collection/folder)
    string newFolderUrl = rootLinkStr + "/mkcol_webdav_folder";
    auto mkcolResponse = performHttpRequest(newFolderUrl, "MKCOL");
    EXPECT_TRUE(mkcolResponse.responseCode == 201); // 201 Created
    CASE_info << "finished";
}

TEST_F(HttpServerTest, HttpServerWebDavModificationOperations)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    ASSERT_TRUE(megaApi[0]->httpServerStart(true, 0));

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
    auto putResponse =
        performHttpRequest(putFileUrl, "PUT", putContent, {{"Content-Type", "text/plain"}});
    EXPECT_EQ(201, putResponse.responseCode); // Created

    std::unique_ptr<MegaNode> putNode(
        megaApi[0]->getNodeByPath(("/" + folderName + "/put_test_file.txt").c_str()));
    ASSERT_TRUE(putNode);

    std::unique_ptr<char[]> putFileWebdavLink(
        megaApi[0]->httpServerGetLocalWebDavLink(putNode.get()));
    ASSERT_TRUE(putFileWebdavLink);
    string putFileLinkStr(putFileWebdavLink.get());

    CASE_info << "WebDAV PUT file link: " << putFileLinkStr;

    auto getResponse = performHttpRequest(putFileLinkStr);
    EXPECT_EQ(200, getResponse.responseCode);
    EXPECT_EQ(putContent, getResponse.data);
    CASE_info << "finished";
}

// Need clarification about parameters of MOVE and COPY operations over WebDAV
TEST_F(HttpServerTest, DISABLED_HttpServerWebDavMoveAndCopyOperations) {}

TEST_F(HttpServerTest, HttpServerWebDavLockingOperations)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    ASSERT_TRUE(megaApi[0]->httpServerStart(true, 0));

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

    auto lockResponse = performHttpRequest(
        rootLinkStr,
        "LOCK",
        lockBody,
        {{"Content-Type", "text/xml; charset=utf-8"}, {"Timeout", "Infinite, Second-4100000000"}});
    EXPECT_EQ(200, lockResponse.responseCode);
    EXPECT_FALSE(lockResponse.data.empty());

    CASE_info << "LOCK response data: " << lockResponse.data;

    // Extract lock token from response
    size_t tokenStart = lockResponse.data.find("<D:locktoken>");
    ASSERT_TRUE(tokenStart != string::npos);
    tokenStart += strlen("<D:locktoken>");
    size_t tokenEnd = lockResponse.data.find("</D:locktoken>");
    ASSERT_TRUE(tokenEnd != string::npos && tokenEnd > tokenStart);
    std::string tokenRef = lockResponse.data.substr(tokenStart, tokenEnd - tokenStart);
    size_t lockTokenPos = tokenRef.find("<D:href>");
    ASSERT_TRUE(lockTokenPos != string::npos);
    lockTokenPos += strlen("<D:href>");
    size_t lockTokenEnd = tokenRef.find("</D:href>");
    ASSERT_TRUE(lockTokenEnd != std::string::npos && lockTokenEnd > lockTokenPos);
    std::string lockToken = tokenRef.substr(lockTokenPos, lockTokenEnd - lockTokenPos);
    EXPECT_FALSE(lockToken.empty());

    auto unlockResponse =
        performHttpRequest(rootLinkStr, "UNLOCK", "", {{"Lock-Token", "<" + lockToken + ">"}});
    EXPECT_EQ(204, unlockResponse.responseCode); // No Content
    CASE_info << "finished";
}

TEST_F(HttpServerTest, HttpServerWebDavPropertyOperations)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    ASSERT_TRUE(megaApi[0]->httpServerStart(true, 0));

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

    auto response = performHttpRequest(rootLinkStr,
                                       "PROPPATCH",
                                       proppatchBody,
                                       {{"Content-Type", "text/xml; charset=utf-8"}});
    EXPECT_EQ(207, response.responseCode); // Multi-Status
    CASE_info << "finished";
}

TEST_F(HttpServerTest, HttpServerWebDavDeleteOperation)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    ASSERT_TRUE(megaApi[0]->httpServerStart(true, 0));

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
    auto response = performHttpRequest(fileLinkStr, "DELETE");
    EXPECT_EQ(204, response.responseCode); // No Content

    response = performHttpRequest(fileLinkStr);
    EXPECT_EQ(404, response.responseCode); // Not Found
    CASE_info << "finished";
}

TEST_F(HttpServerTest, HttpServerWebDavGetAllLinksAndManageAllowedNodes)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    ASSERT_TRUE(megaApi[0]->httpServerStart(true, 0));
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
TEST_F(HttpServerTest, HttpServerListenerCallbacks)
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

    ASSERT_TRUE(megaApi[0]->httpServerStart(true, 0));

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

    auto response = performHttpRequest(fileLinkStr);
    EXPECT_EQ(200, response.responseCode);

    // Wait for OnTransferFinish callback
    auto fileNodeHandle = fileNodeHandlePromise.get_future();
    ASSERT_EQ(fileNodeHandle.wait_for(std::chrono::seconds(5U)), std::future_status::ready)
        << "Timeout waiting for onTransferFinish callback";
    EXPECT_EQ(fileNode->getHandle(), fileNodeHandle.get());

    megaApi[0]->httpServerRemoveListener(&mockListener);
    CASE_info << "finished";
}
