#include "easy_curl.h"
#include "integration_test_utils.h"
#include "mega/scoped_helpers.h"
#include "sdk_server_test_utils.h"
#include "SdkTest_test.h"

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <future>
#include <memory>
#include <string>
#include <vector>

/**
 * Test for FTP server using port 0, which also consist of:
 * - start two FTP servers from a thread and no ports conflicting
 * - stop FTP servers from a different thread, to allow TSAN to report any data races
 */
TEST_F(SdkTest, FtpServerCanUsePort0)
{
    CASE_info << "started";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2, false));

    ASSERT_TRUE(megaApi[0]->ftpServerStart(true, 0));
    ASSERT_TRUE(megaApi[1]->ftpServerStart(true, 0));
    ASSERT_TRUE(megaApi[0]->ftpServerIsRunning());
    ASSERT_TRUE(megaApi[1]->ftpServerIsRunning());

    std::async(std::launch::async,
               [&api = megaApi]()
               {
                   api[0]->ftpServerStop();
                   api[1]->ftpServerStop();
               })
        .get();

    CASE_info << "finished";
}

namespace
{

/**
 * @brief Result of an FTP exchange driven through libcurl.
 *
 * body collects the data channel payload (the LIST or NLST output). controlLog
 * collects the raw bytes the server sent on the control channel, so tests can
 * assert on the exact reply lines the client would parse.
 */
struct FtpExchange
{
    CURLcode curlCode{CURLE_OK};
    long ftpCode{0};
    std::string body;
    std::string controlLog;
};

size_t ftpBodyCallback(void* contents, size_t size, size_t nmemb, void* userdata)
{
    const size_t total = size * nmemb;
    static_cast<std::string*>(userdata)->append(static_cast<const char*>(contents), total);
    return total;
}

int ftpControlCallback(CURL*, curl_infotype type, char* data, size_t size, void* userdata)
{
    // CURLINFO_HEADER_IN carries the server's control channel replies verbatim.
    if (type == CURLINFO_HEADER_IN)
    {
        static_cast<std::string*>(userdata)->append(data, size);
    }
    return 0;
}

// Feeds an in memory buffer to libcurl as the body of an FTP upload (STOR).
struct FtpUploadSource
{
    const std::string* data{nullptr};
    size_t offset{0};
};

size_t ftpReadCallback(char* buffer, size_t size, size_t nitems, void* userdata)
{
    auto* source = static_cast<FtpUploadSource*>(userdata);
    const size_t remaining = source->data->size() - source->offset;
    const size_t chunk = std::min(size * nitems, remaining);
    if (chunk)
    {
        std::memcpy(buffer, source->data->data() + source->offset, chunk);
        source->offset += chunk;
    }
    return chunk;
}

/**
 * @brief Minimal FTP client, built on the shared EasyCurl RAII wrapper.
 *
 * It only exposes a directory listing (LIST or NLST) and a way to run explicit
 * control commands while capturing the raw control replies.
 */
class FtpTestClient
{
public:
    static FtpExchange list(const std::string& url)
    {
        return perform(url, false, {});
    }

    static FtpExchange nlst(const std::string& url)
    {
        return perform(url, true, {});
    }

    static FtpExchange runControlCommands(const std::string& baseUrl,
                                          const std::vector<std::string>& commands)
    {
        return perform(baseUrl, false, commands);
    }

    // Uploads content to url via STOR. The path in url decides the target: a bare
    // filename on the base url stores into the login directory (the account root),
    // while a filename under the served folder path stores inside it.
    static FtpExchange upload(const std::string& url, const std::string& content)
    {
        FtpExchange exchange;
        sdk_test::EasyCurl easyCurl;
        CURL* curl = easyCurl.curl();
        FtpUploadSource source{&content, 0};

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, ftpReadCallback);
        curl_easy_setopt(curl, CURLOPT_READDATA, &source);
        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(content.size()));
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, ftpControlCallback);
        curl_easy_setopt(curl, CURLOPT_DEBUGDATA, &exchange.controlLog);

        exchange.curlCode = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &exchange.ftpCode);
        return exchange;
    }

private:
    static FtpExchange perform(const std::string& url,
                               bool dirListOnly,
                               const std::vector<std::string>& quoteCommands)
    {
        FtpExchange exchange;
        sdk_test::EasyCurl easyCurl;
        sdk_test::EasyCurlSlist quote;
        CURL* curl = easyCurl.curl();

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, ftpBodyCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &exchange.body);
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, ftpControlCallback);
        curl_easy_setopt(curl, CURLOPT_DEBUGDATA, &exchange.controlLog);

        if (dirListOnly)
        {
            curl_easy_setopt(curl, CURLOPT_DIRLISTONLY, 1L);
        }

        if (!quoteCommands.empty())
        {
            for (const auto& command: quoteCommands)
            {
                quote.append(command);
            }
            // Run commands on the control channel only, no data transfer.
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
            curl_easy_setopt(curl, CURLOPT_QUOTE, quote.slist());
        }

        exchange.curlCode = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &exchange.ftpCode);
        return exchange;
    }
};

std::string base64Handle(MegaNode* node)
{
    std::unique_ptr<char[]> handle{node->getBase64Handle()};
    return handle ? std::string{handle.get()} : std::string{};
}

// Extracts "ftp://host:port/" from a full local FTP link.
std::string ftpBaseUrl(const std::string& link)
{
    const size_t schemeEnd = link.find("://");
    if (schemeEnd == std::string::npos)
    {
        return {};
    }
    const size_t firstSlash = link.find('/', schemeEnd + 3);
    if (firstSlash == std::string::npos)
    {
        return link + "/";
    }
    return link.substr(0, firstSlash + 1);
}

} // namespace

/**
 * @brief Checks that the FTP server survives connections it cannot set up after accepting them
 *
 * The FTP control server shares the accept path with the HTTP and WebDAV servers, so a
 * connection that fails right after being accepted used to free its context while the libuv
 * close callbacks were still pending. Running this test under ASAN reports that use after free.
 */
TEST_F(SdkTest, FtpServerDropsConnectionsThatCannotBeSetUp)
{
#ifndef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED";
#else
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    ASSERT_TRUE(megaApi[0]->ftpServerStart(true, 0));
    const auto ftpServer = makeScopedDestructor(
        [&api = megaApi]()
        {
            api[0]->ftpServerStop();
        });

    const int port = megaApi[0]->ftpServerIsRunning();
    ASSERT_GT(port, 0);

    const std::string baseUrl = "ftp://127.0.0.1:" + std::to_string(port) + "/";

    constexpr int failures = 3;
    const ScopedTcpServerAcceptFailure acceptFailure{failures};

    // A dropped connection never reaches the greeting, because the server closes it before
    // it answers anything
    for (int i = 0; i < failures; i++)
    {
        const FtpExchange exchange = FtpTestClient::runControlCommands(baseUrl, {"NOOP"});
        EXPECT_NE(CURLE_OK, exchange.curlCode) << "the server answered a dropped connection";
        EXPECT_EQ(std::string::npos, exchange.controlLog.find("220 "))
            << "the server greeted a dropped connection. Control log:\n"
            << exchange.controlLog;
    }

    EXPECT_EQ(failures, acceptFailure.simulatedFailures());

    // Each dropped connection must close on its own. A context that was freed too early, or
    // one that was never closed, does not get here
    EXPECT_TRUE(acceptFailure.waitForClosedConnections(failures, std::chrono::seconds(30)))
        << "only " << acceptFailure.closedConnections() << " of " << failures
        << " dropped connections finished closing";

    // Without the lifetime fix the context is freed as soon as the accept path returns, so the
    // server no longer owns it when its close callbacks run
    EXPECT_EQ(0, acceptFailure.connectionsClosedAfterBeingFreed())
        << "a dropped connection was freed before its close callbacks ran";

    // Dropping a connection must not stop the server from listening
    const FtpExchange served = FtpTestClient::runControlCommands(baseUrl, {"NOOP"});
    EXPECT_NE(std::string::npos, served.controlLog.find("220 "))
        << "the server stopped accepting connections. Control log:\n"
        << served.controlLog;
#endif
}

/**
 * The FTP server must not resolve a path outside the served folder.
 *
 * For an absolute FTP path the server only validates the handle of the first
 * segment (the served folder) and then resolves the remainder with getNodeByPath,
 * which honours "..". An absolute path never triggers the ".." guard (that only
 * fires when the path literally starts with ".."), so a request like
 * "/<servedHandle>/<servedName>/../<outsideName>" used to reach a sibling node
 * outside the served subtree.
 *
 * SIZE and RNFR are single control-channel commands routed through the same
 * getNodeByFtpPath() chokepoint as LIST/NLST/RETR/STOR/DELE/RMD/MKD, so they let
 * us assert the boundary without a data channel and without mutating anything:
 * a resolved node inside the served folder answers 213/350, while one that
 * escapes it must be rejected with 550.
 */
TEST_F(SdkTest, FtpServerRejectsPathTraversalOutsideServedFolder)
{
    CASE_info << "started";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    auto rootNode = makeUniqueFrom(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);

    // Secret sibling at the Cloud Drive root. No local link is created for it, so
    // its handle is never in the served allow-list. It must stay unreachable.
    const std::string outsideName = "outside_secret.txt";
    auto outsideNode = sdk_test::uploadFile(megaApi[0].get(),
                                            sdk_test::LocalTempFile{outsideName, "outside secret"},
                                            rootNode.get(),
                                            outsideName.c_str());
    ASSERT_TRUE(outsideNode);

    const std::string servedName = "served_folder";
    const MegaHandle servedHandle = createFolder(0, servedName.c_str(), rootNode.get());
    ASSERT_NE(servedHandle, UNDEF);
    auto servedNode = makeUniqueFrom(megaApi[0]->getNodeByHandle(servedHandle));
    ASSERT_TRUE(servedNode);

    const std::string insideName = "inside.txt";
    auto insideNode = sdk_test::uploadFile(megaApi[0].get(),
                                           sdk_test::LocalTempFile{insideName, "inside allowed"},
                                           servedNode.get(),
                                           insideName.c_str());
    ASSERT_TRUE(insideNode);

    ASSERT_TRUE(megaApi[0]->ftpServerStart(true, 0));
    const auto ftpServer = makeScopedDestructor(
        [&api = megaApi]()
        {
            api[0]->ftpServerStop();
        });

    // Generating a local link is what allows the served folder handle over FTP.
    std::unique_ptr<char[]> servedLink{megaApi[0]->ftpServerGetLocalLink(servedNode.get())};
    ASSERT_TRUE(servedLink);
    const std::string baseUrl = ftpBaseUrl(servedLink.get());
    ASSERT_FALSE(baseUrl.empty());

    const std::string servedRoot = "/" + base64Handle(servedNode.get()) + "/" + servedName;
    const std::string insidePath = servedRoot + "/" + insideName;
    const std::string traversalPath = servedRoot + "/../" + outsideName;

    // Positive control: a legitimate path inside the served folder resolves, so the
    // rejections below are due to the boundary check, not a broken server or link.
    {
        const FtpExchange size =
            FtpTestClient::runControlCommands(baseUrl, {"*SIZE " + insidePath});
        ASSERT_EQ(CURLE_OK, size.curlCode)
            << "SIZE inside failed: " << curl_easy_strerror(size.curlCode) << "\n"
            << size.controlLog;
        EXPECT_NE(size.controlLog.find("213 "), std::string::npos)
            << "SIZE did not resolve a file inside the served folder. Control log:\n"
            << size.controlLog;
    }

    // SIZE on a path that escapes the served folder must be rejected, never leak the size.
    {
        const FtpExchange size =
            FtpTestClient::runControlCommands(baseUrl, {"*SIZE " + traversalPath});
        ASSERT_EQ(CURLE_OK, size.curlCode)
            << "SIZE traversal failed: " << curl_easy_strerror(size.curlCode) << "\n"
            << size.controlLog;
        EXPECT_NE(size.controlLog.find("550"), std::string::npos)
            << "SIZE outside the served folder was not rejected. Control log:\n"
            << size.controlLog;
        EXPECT_EQ(size.controlLog.find("213 "), std::string::npos)
            << "SIZE leaked a node outside the served folder. Control log:\n"
            << size.controlLog;
    }

    // RNFR (rename source lookup) must not select a node outside the served folder.
    {
        const FtpExchange rnfr =
            FtpTestClient::runControlCommands(baseUrl, {"*RNFR " + traversalPath});
        ASSERT_EQ(CURLE_OK, rnfr.curlCode)
            << "RNFR traversal failed: " << curl_easy_strerror(rnfr.curlCode) << "\n"
            << rnfr.controlLog;
        EXPECT_NE(rnfr.controlLog.find("550"), std::string::npos)
            << "RNFR outside the served folder was not rejected. Control log:\n"
            << rnfr.controlLog;
        EXPECT_EQ(rnfr.controlLog.find("350"), std::string::npos)
            << "RNFR selected a node outside the served folder. Control log:\n"
            << rnfr.controlLog;
    }

    EXPECT_EQ(API_OK, doDeleteNode(0, servedNode.get()));
    EXPECT_EQ(API_OK, doDeleteNode(0, outsideNode.get()));

    CASE_info << "finished";
}

/**
 * STOR and MKD with a bare name must not write outside the served folder.
 *
 * The server must reject the write when cwd is outside every served subtree,
 * while still accepting a legitimate upload made inside the served folder.
 */
TEST_F(SdkTest, FtpServerRejectsWriteToUnservedWorkingDirectory)
{
    CASE_info << "started";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    auto rootNode = makeUniqueFrom(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);

    const std::string servedName = "served_folder";
    const MegaHandle servedHandle = createFolder(0, servedName.c_str(), rootNode.get());
    ASSERT_NE(servedHandle, UNDEF);
    auto servedNode = makeUniqueFrom(megaApi[0]->getNodeByHandle(servedHandle));
    ASSERT_TRUE(servedNode);

    ASSERT_TRUE(megaApi[0]->ftpServerStart(true, 0));
    const auto ftpServer = makeScopedDestructor(
        [&api = megaApi]()
        {
            api[0]->ftpServerStop();
        });

    // Only the served folder handle is allowed over FTP; the account root is not.
    std::unique_ptr<char[]> servedLink{megaApi[0]->ftpServerGetLocalLink(servedNode.get())};
    ASSERT_TRUE(servedLink);
    const std::string baseUrl = ftpBaseUrl(servedLink.get());
    ASSERT_FALSE(baseUrl.empty());

    // Test: a bare filename stored while the client is at the FTP root. cwd is
    // the Cloud Drive root, which was never served, so it must be rejected and
    // nothing may be created at the root.
    const std::string outsideUpload = "escaped_upload.txt";
    {
        const FtpExchange stor = FtpTestClient::upload(baseUrl + outsideUpload, "payload");
        EXPECT_NE(stor.controlLog.find("550"), std::string::npos)
            << "STOR of a bare name at the account root was not rejected. Control log:\n"
            << stor.controlLog;
        EXPECT_EQ(stor.controlLog.find("150 Opening data connection for storing"),
                  std::string::npos)
            << "STOR opened a data connection to write outside the served folder. Control log:\n"
            << stor.controlLog;

        auto escaped =
            makeUniqueFrom(megaApi[0]->getChildNode(rootNode.get(), outsideUpload.c_str()));
        EXPECT_FALSE(escaped)
            << "STOR created a file at the Cloud Drive root from a link serving only a subfolder";
    }

    // MKD of a bare name at the FTP root must be rejected too.
    {
        const FtpExchange mkd = FtpTestClient::runControlCommands(baseUrl, {"MKD escaped_folder"});
        EXPECT_NE(mkd.controlLog.find("550"), std::string::npos)
            << "MKD of a bare name at the account root was not rejected. Control log:\n"
            << mkd.controlLog;

        auto escaped = makeUniqueFrom(megaApi[0]->getChildNode(rootNode.get(), "escaped_folder"));
        EXPECT_FALSE(escaped)
            << "MKD created a folder at the Cloud Drive root from a link serving only a subfolder";
    }

    // An upload made inside the served folder must still be
    // accepted, proving the check does not block legitimate writes.
    {
        const std::string servedPath = servedLink.get();
        const FtpExchange stor =
            FtpTestClient::upload(servedPath + "/allowed_upload.txt", "payload");
        EXPECT_NE(stor.controlLog.find("150 Opening data connection for storing"),
                  std::string::npos)
            << "A legitimate STOR inside the served folder was rejected. Control log:\n"
            << stor.controlLog;
    }

    EXPECT_EQ(API_OK, doDeleteNode(0, servedNode.get()));

    CASE_info << "finished";
}
