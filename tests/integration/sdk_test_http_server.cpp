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
#include "sdk_test_utils.h"
#include "SdkTest_test.h"

#include <curl/curl.h>

#include <future>
#include <iterator>
#include <memory>
#include <string_view>

using namespace mega;
using ::mega::common::testing::randomBytes;
using sdk_test::EasyCurl;
using sdk_test::LocalTempFile;
using sdk_test::uploadFile;

namespace
{

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
}
class SdkHttpServerTest: public SdkTest
{};

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
        std::string headers;
        std::string body;
        curl_off_t contentLength;
    };

    static Response get(const std::string& url, const std::string& range = EmptyRange)
    {
        return performRequest(url, "GET", range, BodyMode::WithBody);
    }

    static Response post(const std::string& url)
    {
        return performRequest(url, "POST", EmptyRange, BodyMode::WithBody);
    }

    static Response put(const std::string& url)
    {
        return performRequest(url, "PUT", EmptyRange, BodyMode::WithBody);
    }

    static Response del(const std::string& url)
    {
        return performRequest(url, "DELETE", EmptyRange, BodyMode::WithBody);
    }

    static Response head(const std::string& url)
    {
        return performRequest(url, "HEAD", EmptyRange, BodyMode::WithoutBody);
    }

private:
    static Response performRequest(const std::string& url,
                                   const std::string& method,
                                   const std::string& rangeHeader = EmptyRange,
                                   BodyMode bodyMode = BodyMode::WithBody)
    {
        const auto easyCurl = EasyCurl::create();
        if (!easyCurl)
        {
            std::cerr << "Failed to initialize CURL" << std::endl;
            std::abort();
        }

        auto curl = easyCurl->curl();

        std::string headerData;
        std::string bodyData;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
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
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &bodyData);
        }

        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headerData);

        if (!rangeHeader.empty())
        {
            curl_easy_setopt(curl, CURLOPT_RANGE, rangeHeader.c_str());
        }

        CURLcode res = curl_easy_perform(curl);
        Response response;

        if (res == CURLE_OK)
        {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);
            curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &response.contentLength);
        }
        else
        {
            std::cerr << "CURL error for " << method << " " << url << ": "
                      << curl_easy_strerror(res) << " (code: " << res << ")" << std::endl;
            response.statusCode = 0;
            response.contentLength = -1;
        }

        response.headers = headerData;
        response.body = bodyData;
        return response;
    }

    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userdata)
    {
        auto* str = reinterpret_cast<std::string*>(userdata);
        auto newContents = reinterpret_cast<char*>(contents);
        str->append(newContents, size * nmemb);
        return size * nmemb;
    }

    static size_t headerCallback(char* buffer, size_t size, size_t nitems, void* userdata)
    {
        auto* str = reinterpret_cast<std::string*>(userdata);
        str->append(buffer, size * nitems);
        return size * nitems;
    }
};

/**
 * Test for HTTP server using port 0, which also consist of:
 * - start two HTTP servers from a thread and no ports conflicting
 * - stop HTTP servers from a different thread, to allow TSAN to report any data races
 */
TEST_F(SdkHttpServerTest, CanUsePort0)
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

/**
 * Test basic HTTP server functionality with GET request.
 */
TEST_F(SdkHttpServerTest, BasicGet)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    std::string testFileContent = "HTTP server basic test content";
    std::unique_ptr<MegaNode> uploadedNode =
        uploadFile(api, LocalTempFile{"test_http_basic.txt", testFileContent});
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
    std::unique_ptr<MegaNode> uploadedNode =
        uploadFile(api, LocalTempFile{"test_http_head.txt", testFileContent});
    ASSERT_NE(uploadedNode, nullptr);

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    std::unique_ptr<char[]> link(api->httpServerGetLocalLink(uploadedNode.get()));
    ASSERT_NE(link, nullptr);
    std::string url = link.get();

    auto response = HttpClient::head(url);
    EXPECT_EQ(200, response.statusCode);
    EXPECT_TRUE(response.body.empty());
    EXPECT_NE(response.headers.find("Content-Length"), std::string::npos);
    EXPECT_NE(response.headers.find(std::to_string(testFileContent.size())), std::string::npos);
}

/**
 * Test HTTP server with valid range requests.
 */
TEST_F(SdkHttpServerTest, ValidRangeRequests)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    std::string testFileContent = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::unique_ptr<MegaNode> uploadedNode =
        uploadFile(api, LocalTempFile{"test_http_range.txt", testFileContent});
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
        uploadFile(api, LocalTempFile{"test_http_large_range.bin", testFileContent});
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
        uploadFile(api, LocalTempFile{"test_http_invalid_range.txt", testFileContent});
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
    std::unique_ptr<MegaNode> uploadedNode =
        uploadFile(api, LocalTempFile{"test_http_empty.txt", ""});
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
    EXPECT_NE(response.headers.find("Content-Length"), std::string::npos);
    EXPECT_NE(response.headers.find("Content-Length: 0"), std::string::npos);

    // HEAD request for empty file
    auto headResponse = HttpClient::head(url);
    EXPECT_EQ(200, headResponse.statusCode);
    EXPECT_TRUE(headResponse.body.empty());
    EXPECT_NE(headResponse.headers.find("Content-Length"), std::string::npos);

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
    std::unique_ptr<MegaNode> uploadedNode =
        uploadFile(api, LocalTempFile{"test_http_large.bin", testFileContent});
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
        uploadFile(api, LocalTempFile{"test_http_concurrent.txt", testFileContent});
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
        uploadFile(api, LocalTempFile{"test_http_concurrent_range.bin", testFileContent});
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
    std::unique_ptr<MegaNode> uploadedNode =
        uploadFile(api, LocalTempFile{"test_http_restart.txt", testFileContent});
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
        uploadFile(api, LocalTempFile{"test_http_methods.txt", testFileContent});
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
    std::unique_ptr<MegaNode> uploadedNode =
        uploadFile(api, LocalTempFile{"test_http_rapid.txt", testFileContent});
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
        std::unique_ptr<MegaNode> uploadedNode = uploadFile(api, LocalTempFile{fileName, fileName});
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
    std::unique_ptr<MegaNode> uploadedNode1 =
        uploadFile(api, LocalTempFile{"test_1byte.tx", testFileContent1});
    ASSERT_NE(uploadedNode1, nullptr);

    // Test 2-byte file
    std::string testFileContent2 = "AB";
    std::unique_ptr<MegaNode> uploadedNode2 =
        uploadFile(api, LocalTempFile{"test_2byte.tx", testFileContent2});
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
 * Test HTTP server with very long URLs (1 MB).
 * Tests server behavior with extremely long URL paths, including non-existent files (404).
 */
TEST_F(SdkHttpServerTest, VeryLongUrl)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    std::string testFileContent = "Test content";
    std::unique_ptr<MegaNode> uploadedNode =
        uploadFile(api, LocalTempFile{"test_http_long.txt", testFileContent});
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
        uploadFile(api, LocalTempFile{"test_http_connection.txt", testFileContent});
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

    std::unique_ptr<MegaNode> rootNode(api->getRootNode());
    ASSERT_NE(rootNode, nullptr);

    // Create empty folder
    MegaHandle folderHandle = createFolder(0, "test_http_folder_empty", rootNode.get());
    ASSERT_NE(folderHandle, INVALID_HANDLE);
    std::unique_ptr<MegaNode> folderNode(api->getNodeByHandle(folderHandle));
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

    std::unique_ptr<MegaNode> rootNode(api->getRootNode());
    ASSERT_NE(rootNode, nullptr);

    // Create folder
    MegaHandle folderHandle = createFolder(0, "test_http_folder_files", rootNode.get());
    ASSERT_NE(folderHandle, INVALID_HANDLE);
    std::unique_ptr<MegaNode> folderNode(api->getNodeByHandle(folderHandle));
    ASSERT_NE(folderNode, nullptr);

    // Upload files to folder
    const std::vector<std::string> testFiles = {
        "file 1.txt",
        "file#2.txt",
        "file?3.dat",
        "file-3.dat",
    };

    std::vector<std::unique_ptr<MegaNode>> uploadedNodes;
    for (const auto& fileName: testFiles)
    {
        std::unique_ptr<MegaNode> uploadedNode =
            uploadFile(api, LocalTempFile{fileName, fileName}, folderNode.get());
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
 * Test HTTP server with folder server disabled.
 */
TEST_F(SdkHttpServerTest, FolderDisabled)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    MegaApi* api = megaApi[0].get();

    std::unique_ptr<MegaNode> rootNode(api->getRootNode());
    ASSERT_NE(rootNode, nullptr);

    // Create folder
    MegaHandle folderHandle = createFolder(0, "test_http_folder_disabled", rootNode.get());
    ASSERT_NE(folderHandle, INVALID_HANDLE);
    std::unique_ptr<MegaNode> folderNode(api->getNodeByHandle(folderHandle));
    ASSERT_NE(folderNode, nullptr);

    // Ensure folder server is disabled (default)
    api->httpServerEnableFolderServer(false);
    ASSERT_FALSE(api->httpServerIsFolderServerEnabled());

    auto server = scopedHttpServer(api);
    ASSERT_TRUE(server);

    std::unique_ptr<char[]> link(api->httpServerGetLocalLink(folderNode.get()));
    EXPECT_NE(link.get(), nullptr);

    std::string url = link.get();
    auto response = HttpClient::get(url);
    EXPECT_EQ(403, response.statusCode);
}
