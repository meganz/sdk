#include "easy_curl.h"
#include "integration_test_utils.h"
#include "mega/scoped_helpers.h"
#include "SdkTest_test.h"

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <future>
#include <memory>
#include <string>
#include <thread>
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
    // Long enough for a command that has to wait for an API request to finish.
    static constexpr size_t maxAttempts = 100;
    static constexpr std::chrono::milliseconds attemptWait{100};

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

    // An FTP reply ends with a line that opens with the code and a space, as
    // opposed to the dash that marks a line with more to follow.
    static bool isCompleteFtpReply(const std::string& text)
    {
        constexpr size_t shortestReplyLine = 6; // "ddd \r\n"

        if (text.size() < shortestReplyLine || text.compare(text.size() - 2, 2, "\r\n") != 0)
        {
            return false;
        }

        const size_t previousEnd = text.rfind("\r\n", text.size() - 3);
        const size_t lastLine = (previousEnd == std::string::npos) ? 0 : previousEnd + 2;
        if (text.size() - lastLine < shortestReplyLine)
        {
            return false;
        }

        const auto isDigit = [](const char c)
        {
            return c >= '0' && c <= '9';
        };

        return isDigit(text[lastLine]) && isDigit(text[lastLine + 1]) &&
               isDigit(text[lastLine + 2]) && text[lastLine + 3] == ' ';
    }

    // Downloads url via RETR, collecting the file content as the body. Used by the
    // download test, which is disabled at the bottom of this file.
    static FtpExchange download(const std::string& url)
    {
        return perform(url, false, {});
    }

    // Sends raw bytes on the control channel and returns what comes back. libcurl
    // refuses to put a lone CR or LF in a command of its own, so a malformed
    // command line has to be written to the socket as it is. The connection is
    // already logged in, so the greeting and the login replies are not returned.
    static FtpExchange rawControlCommand(const std::string& baseUrl, const std::string& command)
    {
        return rawControlCommands(baseUrl, {command});
    }

    // The same, for commands that have to share one connection, such as a PASV that
    // sets up a transfer and the command that then asks for it.
    static FtpExchange rawControlCommands(const std::string& baseUrl,
                                          const std::vector<std::string>& commands)
    {
        FtpExchange exchange;
        sdk_test::EasyCurl easyCurl;
        CURL* curl = easyCurl.curl();

        curl_easy_setopt(curl, CURLOPT_URL, baseUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
        curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 1L);

        exchange.curlCode = curl_easy_perform(curl);
        if (exchange.curlCode != CURLE_OK)
        {
            return exchange;
        }

        for (const auto& command: commands)
        {
            // A socket takes what it takes, so keep offering the rest of the command.
            for (size_t offset = 0, attempt = 0; offset < command.size(); attempt++)
            {
                if (attempt >= maxAttempts)
                {
                    exchange.curlCode = CURLE_OPERATION_TIMEDOUT;
                    return exchange;
                }

                size_t sent = 0;
                const CURLcode code =
                    curl_easy_send(curl, command.data() + offset, command.size() - offset, &sent);
                if (code == CURLE_AGAIN)
                {
                    std::this_thread::sleep_for(attemptWait);
                    continue;
                }

                if (code != CURLE_OK)
                {
                    exchange.curlCode = code;
                    return exchange;
                }

                offset += sent;
            }

            // The reply of a command that creates a node arrives once the request has
            // completed, and a socket may hand it over in pieces, so collect until the
            // reply is whole or the budget runs out.
            std::string reply;
            for (size_t attempt = 0; attempt < maxAttempts && !isCompleteFtpReply(reply); attempt++)
            {
                char buffer[4096];
                size_t received = 0;
                const CURLcode code = curl_easy_recv(curl, buffer, sizeof(buffer), &received);
                if (code == CURLE_OK && received)
                {
                    reply.append(buffer, received);
                    continue;
                }

                if (code == CURLE_OK)
                {
                    // Reading nothing on a successful call means the peer is gone.
                    break;
                }

                if (code != CURLE_AGAIN)
                {
                    exchange.curlCode = code;
                    break;
                }

                std::this_thread::sleep_for(attemptWait);
            }

            if (exchange.curlCode == CURLE_OK && !isCompleteFtpReply(reply))
            {
                // Say what went wrong here rather than let the caller puzzle over half
                // of a reply.
                exchange.curlCode = CURLE_OPERATION_TIMEDOUT;
            }

            exchange.controlLog.append(reply);
            if (exchange.curlCode != CURLE_OK)
            {
                return exchange;
            }
        }

        return exchange;
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

// Takes the file name out of an ls style listing line the way a client parses it,
// that is everything past the eight fields that precede it. Splitting on runs of
// spaces is what makes a name holding one work, and a tab rides along with it.
std::string nameFromListingLine(const std::string& line)
{
    size_t pos = 0;
    for (int field = 0; field < 8; field++)
    {
        pos = line.find(' ', pos);
        if (pos == std::string::npos)
        {
            return {};
        }

        pos = line.find_first_not_of(' ', pos);
        if (pos == std::string::npos)
        {
            return {};
        }
    }

    return line.substr(pos);
}

// Returns the first line of body holding needle, with any line ending removed.
std::string lineContaining(const std::string& body, const std::string& needle)
{
    for (size_t begin = 0; begin < body.size();)
    {
        const size_t end = body.find('\n', begin);
        std::string entry = body.substr(begin, end - begin);
        if (!entry.empty() && entry.back() == '\r')
        {
            entry.pop_back();
        }

        if (entry.find(needle) != std::string::npos)
        {
            return entry;
        }

        if (end == std::string::npos)
        {
            break;
        }

        begin = end + 1;
    }

    return {};
}

// Reads the pathname out of a 257 reply, which carries it in double quotes. The
// paths used here hold no quote of their own, so no undoubling is needed.
std::string quotedPathFromReply(const std::string& controlLog)
{
    const size_t begin = controlLog.find("257 \"");
    if (begin == std::string::npos)
    {
        return {};
    }

    const size_t pathBegin = begin + 5;
    const size_t pathEnd = controlLog.find("\" directory created", pathBegin);
    if (pathEnd == std::string::npos)
    {
        return {};
    }

    return controlLog.substr(pathBegin, pathEnd - pathBegin);
}

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

/**
 * The FTP server must neutralize CR/LF (and other control bytes) in node names
 * before reflecting them into FTP responses. Otherwise a node name
 * carrying "\r\n" injects extra lines: a forged entry on the data channel (LIST /
 * NLST) or a rogue reply on the control channel (PWD).
 *
 * This drives the running server with libcurl and asserts the injected sequences
 * are escaped in place instead of splitting a response into extra lines.
 */
TEST_F(SdkTest, FtpServerSanitizesControlCharactersInResponses)
{
    CASE_info << "started";

    // The CR/LF inside these names is what an attacker would use to inject a
    // second listing entry / control reply. The fix must escape it as %0D%0A.
    const std::string forgedListingLine =
        "-rw-r--r-- 1 1000 1000 9 Jan 01 00:00 FORGED_invoice.pdf";
    const std::string injectedFileName = "list_injection_cafe_é\r\n" + forgedListingLine;
    const std::string injectedPasvReply = "227 Entering Passive Mode (1,2,3,4,5,6)";
    const std::string injectedFolderName = "pwd_injection_cafe_é\r\n" + injectedPasvReply;
    const std::string sanitizedCrlf = "%0D%0A";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    auto rootNode = makeUniqueFrom(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);

    const std::string parentName = "ftp_sanitize_" + std::to_string(m_time());
    const MegaHandle parentHandle = createFolder(0, parentName.c_str(), rootNode.get());
    ASSERT_NE(parentHandle, UNDEF);
    auto parentNode = makeUniqueFrom(megaApi[0]->getNodeByHandle(parentHandle));
    ASSERT_TRUE(parentNode);

    const MegaHandle injectedFolderHandle =
        createFolder(0, injectedFolderName.c_str(), parentNode.get());
    ASSERT_NE(injectedFolderHandle, UNDEF);
    auto injectedFolderNode = makeUniqueFrom(megaApi[0]->getNodeByHandle(injectedFolderHandle));
    ASSERT_TRUE(injectedFolderNode);

    // A child of the injected folder lets us reach the PWD sink deterministically:
    // entering the child and issuing CDUP rebuilds cwdpath from the parent node name.
    const MegaHandle childHandle = createFolder(0, "child", injectedFolderNode.get());
    ASSERT_NE(childHandle, UNDEF);
    auto childNode = makeUniqueFrom(megaApi[0]->getNodeByHandle(childHandle));
    ASSERT_TRUE(childNode);

    auto injectedFile =
        sdk_test::uploadFile(megaApi[0].get(),
                             sdk_test::LocalTempFile{"ftp_sanitize_upload.txt", "test"},
                             parentNode.get(),
                             injectedFileName.c_str());
    ASSERT_TRUE(injectedFile);

    ASSERT_TRUE(megaApi[0]->ftpServerStart(true, 0));
    const auto ftpServer = makeScopedDestructor(
        [&api = megaApi]()
        {
            api[0]->ftpServerStop();
        });

    // Generating a local link also allows the handle to be served over FTP.
    std::unique_ptr<char[]> parentFtpLink{megaApi[0]->ftpServerGetLocalLink(parentNode.get())};
    ASSERT_TRUE(parentFtpLink);
    std::unique_ptr<char[]> injectedFolderFtpLink{
        megaApi[0]->ftpServerGetLocalLink(injectedFolderNode.get())};
    ASSERT_TRUE(injectedFolderFtpLink);
    std::unique_ptr<char[]> childFtpLink{megaApi[0]->ftpServerGetLocalLink(childNode.get())};
    ASSERT_TRUE(childFtpLink);

    const std::string listUrl = std::string{parentFtpLink.get()} + "/";
    const std::string baseUrl = ftpBaseUrl(parentFtpLink.get());
    ASSERT_FALSE(baseUrl.empty());

    // 1. Data channel, LIST: the injected file name must render on a single line.
    {
        const FtpExchange list = FtpTestClient::list(listUrl);
        ASSERT_EQ(CURLE_OK, list.curlCode)
            << "LIST failed: " << curl_easy_strerror(list.curlCode) << "\n"
            << list.controlLog;

        EXPECT_NE(list.body.find("list_injection_cafe_é" + sanitizedCrlf), std::string::npos)
            << "File name CR/LF was not escaped in the LIST output. Body:\n"
            << list.body;
        EXPECT_EQ(list.body.find("\r\n" + forgedListingLine), std::string::npos)
            << "Injected LIST entry appeared as its own line. Body:\n"
            << list.body;
        EXPECT_EQ(list.body.find("\r\n" + injectedPasvReply), std::string::npos)
            << "Injected folder name split the LIST output. Body:\n"
            << list.body;
    }

    // 2. Data channel, NLST: the injected file name must render on a single line.
    {
        const FtpExchange nlst = FtpTestClient::nlst(listUrl);
        ASSERT_EQ(CURLE_OK, nlst.curlCode)
            << "NLST failed: " << curl_easy_strerror(nlst.curlCode) << "\n"
            << nlst.controlLog;

        EXPECT_NE(nlst.body.find(sanitizedCrlf), std::string::npos)
            << "Node name CR/LF was not escaped in the NLST output. Body:\n"
            << nlst.body;
        EXPECT_EQ(nlst.body.find("\r\n" + forgedListingLine), std::string::npos)
            << "Injected NLST entry appeared as its own line. Body:\n"
            << nlst.body;
    }

    // 3. Control channel, PWD: entering the child then CDUP rebuilds cwdpath from
    //    the injected folder name, which PWD reflects into its 257 reply.
    {
        const std::vector<std::string> commands{
            "*CWD /" + base64Handle(childNode.get()),
            "*CDUP",
            "*PWD",
        };
        const FtpExchange pwd = FtpTestClient::runControlCommands(baseUrl, commands);
        ASSERT_EQ(CURLE_OK, pwd.curlCode)
            << "Control commands failed: " << curl_easy_strerror(pwd.curlCode) << "\n"
            << pwd.controlLog;

        EXPECT_NE(pwd.controlLog.find(sanitizedCrlf + injectedPasvReply), std::string::npos)
            << "PWD reply did not reflect the escaped folder name. Control log:\n"
            << pwd.controlLog;
        EXPECT_EQ(pwd.controlLog.find("\r\n" + injectedPasvReply), std::string::npos)
            << "Injected 227 reply appeared as a standalone control line. Control log:\n"
            << pwd.controlLog;
    }

    EXPECT_EQ(API_OK, doDeleteNode(0, parentNode.get()));

    CASE_info << "finished";
}

/**
 * A name that a client is expected to send back must survive the round trip.
 *
 * Both forms of listing feed a name back into a later command, a scripted client
 * taking it from NLST and a graphical one out of the LIST line it parses, and the
 * PWD path is meant to be reused as a CWD argument. A tab survives all three: it
 * reaches the server intact inside a command argument, and node names are matched
 * byte for byte, so escaping it would make the listed name unusable.
 *
 * SIZE resolves its argument through the same chokepoint as RETR, which lets the
 * reuse be asserted on the control channel alone.
 */
TEST_F(SdkTest, FtpServerKeepsListedNamesUsableAsCommandArguments)
{
    CASE_info << "started";

    const std::string tabbedFileName = "roundtrip_cafe_é\treport.pdf";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    auto rootNode = makeUniqueFrom(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);

    const std::string servedName = "ftp_roundtrip_" + std::to_string(m_time());
    const MegaHandle servedHandle = createFolder(0, servedName.c_str(), rootNode.get());
    ASSERT_NE(servedHandle, UNDEF);
    auto servedNode = makeUniqueFrom(megaApi[0]->getNodeByHandle(servedHandle));
    ASSERT_TRUE(servedNode);

    auto tabbedFile =
        sdk_test::uploadFile(megaApi[0].get(),
                             sdk_test::LocalTempFile{"ftp_roundtrip_upload.txt", "test"},
                             servedNode.get(),
                             tabbedFileName.c_str());
    ASSERT_TRUE(tabbedFile);

    ASSERT_TRUE(megaApi[0]->ftpServerStart(true, 0));
    const auto ftpServer = makeScopedDestructor(
        [&api = megaApi]()
        {
            api[0]->ftpServerStop();
        });

    // Generating a local link also allows the handle to be served over FTP.
    std::unique_ptr<char[]> servedLink{megaApi[0]->ftpServerGetLocalLink(servedNode.get())};
    ASSERT_TRUE(servedLink);
    const std::string listUrl = std::string{servedLink.get()} + "/";
    const std::string baseUrl = ftpBaseUrl(servedLink.get());
    ASSERT_FALSE(baseUrl.empty());

    // NLST must hand back the name byte for byte.
    std::string listedName;
    {
        const FtpExchange nlst = FtpTestClient::nlst(listUrl);
        ASSERT_EQ(CURLE_OK, nlst.curlCode)
            << "NLST failed: " << curl_easy_strerror(nlst.curlCode) << "\n"
            << nlst.controlLog;

        listedName = lineContaining(nlst.body, "roundtrip_cafe_");

        ASSERT_FALSE(listedName.empty()) << "NLST did not list the file. Body:\n" << nlst.body;
        EXPECT_EQ(listedName, tabbedFileName)
            << "NLST altered a name that a client is expected to send back. Body:\n"
            << nlst.body;
    }

    // The listed name, used verbatim as a command argument, must still resolve.
    {
        const std::string listedPath =
            "/" + base64Handle(servedNode.get()) + "/" + servedName + "/" + listedName;
        const FtpExchange size =
            FtpTestClient::runControlCommands(baseUrl, {"*SIZE " + listedPath});
        ASSERT_EQ(CURLE_OK, size.curlCode)
            << "SIZE failed: " << curl_easy_strerror(size.curlCode) << "\n"
            << size.controlLog;
        EXPECT_NE(size.controlLog.find("213 "), std::string::npos)
            << "The name taken from NLST no longer resolves. Control log:\n"
            << size.controlLog;
    }

    // A graphical client parses the LIST line instead, and the name it takes from
    // there has to work as a command argument just as an NLST entry does.
    {
        const FtpExchange list = FtpTestClient::list(listUrl);
        ASSERT_EQ(CURLE_OK, list.curlCode)
            << "LIST failed: " << curl_easy_strerror(list.curlCode) << "\n"
            << list.controlLog;

        const std::string listedLine = lineContaining(list.body, "roundtrip_cafe_");
        ASSERT_FALSE(listedLine.empty()) << "LIST did not list the file. Body:\n" << list.body;

        const std::string nameFromList = nameFromListingLine(listedLine);
        EXPECT_EQ(nameFromList, tabbedFileName)
            << "LIST altered the name a client parses out of the line. Body:\n"
            << list.body;

        const std::string listedPath =
            "/" + base64Handle(servedNode.get()) + "/" + servedName + "/" + nameFromList;
        const FtpExchange size =
            FtpTestClient::runControlCommands(baseUrl, {"*SIZE " + listedPath});
        ASSERT_EQ(CURLE_OK, size.curlCode)
            << "SIZE failed: " << curl_easy_strerror(size.curlCode) << "\n"
            << size.controlLog;
        EXPECT_NE(size.controlLog.find("213 "), std::string::npos)
            << "The name taken from the LIST line no longer resolves. Control log:\n"
            << size.controlLog;
    }

    EXPECT_EQ(API_OK, doDeleteNode(0, servedNode.get()));

    CASE_info << "finished";
}

/**
 * A pathname reply must survive a double quote in a node name.
 *
 * A 257 reply carries the pathname wrapped in double quotes, and RFC 959
 * Appendix II requires an embedded double quote to be doubled. Without that a
 * folder named foo" bar closes the quoted string early, so a client parsing the
 * PWD reply keeps only the truncated pathname and no longer knows where it is.
 * That is the same desynchronization CR/LF used to cause, reached through the
 * quote grammar instead.
 */
TEST_F(SdkTest, FtpServerDoublesQuotesInPathnameReplies)
{
    CASE_info << "started";

    const std::string quotedFolderName = "pwd_quote_cafe_é\" injection";
    const std::string truncatingReply = quotedFolderName + "\"";
    const std::string doubledReply = "pwd_quote_cafe_é\"\" injection\"";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    auto rootNode = makeUniqueFrom(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);

    const std::string parentName = "ftp_quote_" + std::to_string(m_time());
    const MegaHandle parentHandle = createFolder(0, parentName.c_str(), rootNode.get());
    ASSERT_NE(parentHandle, UNDEF);
    auto parentNode = makeUniqueFrom(megaApi[0]->getNodeByHandle(parentHandle));
    ASSERT_TRUE(parentNode);

    const MegaHandle quotedFolderHandle =
        createFolder(0, quotedFolderName.c_str(), parentNode.get());
    ASSERT_NE(quotedFolderHandle, UNDEF);
    auto quotedFolderNode = makeUniqueFrom(megaApi[0]->getNodeByHandle(quotedFolderHandle));
    ASSERT_TRUE(quotedFolderNode);

    // Entering the child and issuing CDUP rebuilds cwdpath from the folder name,
    // which PWD then reflects into its 257 reply.
    const MegaHandle childHandle = createFolder(0, "child", quotedFolderNode.get());
    ASSERT_NE(childHandle, UNDEF);
    auto childNode = makeUniqueFrom(megaApi[0]->getNodeByHandle(childHandle));
    ASSERT_TRUE(childNode);

    ASSERT_TRUE(megaApi[0]->ftpServerStart(true, 0));
    const auto ftpServer = makeScopedDestructor(
        [&api = megaApi]()
        {
            api[0]->ftpServerStop();
        });

    // Generating a local link also allows the handle to be served over FTP.
    std::unique_ptr<char[]> parentFtpLink{megaApi[0]->ftpServerGetLocalLink(parentNode.get())};
    ASSERT_TRUE(parentFtpLink);
    std::unique_ptr<char[]> quotedFolderFtpLink{
        megaApi[0]->ftpServerGetLocalLink(quotedFolderNode.get())};
    ASSERT_TRUE(quotedFolderFtpLink);
    std::unique_ptr<char[]> childFtpLink{megaApi[0]->ftpServerGetLocalLink(childNode.get())};
    ASSERT_TRUE(childFtpLink);

    const std::string baseUrl = ftpBaseUrl(parentFtpLink.get());
    ASSERT_FALSE(baseUrl.empty());

    const std::vector<std::string> commands{
        "*CWD /" + base64Handle(childNode.get()),
        "*CDUP",
        "*PWD",
    };
    const FtpExchange pwd = FtpTestClient::runControlCommands(baseUrl, commands);
    ASSERT_EQ(CURLE_OK, pwd.curlCode)
        << "Control commands failed: " << curl_easy_strerror(pwd.curlCode) << "\n"
        << pwd.controlLog;

    EXPECT_NE(pwd.controlLog.find(doubledReply), std::string::npos)
        << "PWD reply did not double the quote in the folder name. Control log:\n"
        << pwd.controlLog;
    EXPECT_EQ(pwd.controlLog.find(truncatingReply), std::string::npos)
        << "PWD reply closed the pathname early on the quote. Control log:\n"
        << pwd.controlLog;

    EXPECT_EQ(API_OK, doDeleteNode(0, parentNode.get()));

    CASE_info << "finished";
}

/**
 * A LIST issued before any CWD lists the served handles instead of a node's
 * children, which is a separate branch of the LIST handler. It is also the only
 * place that names a listing entry after the handle rather than after the node,
 * so nothing else covers that path.
 */
TEST_F(SdkTest, FtpServerListsServedHandlesAtRoot)
{
    CASE_info << "started";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    auto rootNode = makeUniqueFrom(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);

    const std::string servedName = "ftp_root_" + std::to_string(m_time());
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

    // Generating a local link also allows the handle to be served over FTP.
    std::unique_ptr<char[]> servedLink{megaApi[0]->ftpServerGetLocalLink(servedNode.get())};
    ASSERT_TRUE(servedLink);
    const std::string baseUrl = ftpBaseUrl(servedLink.get());
    ASSERT_FALSE(baseUrl.empty());

    // No path, so the server is still at the root and lists what it serves.
    const FtpExchange list = FtpTestClient::list(baseUrl);
    ASSERT_EQ(CURLE_OK, list.curlCode)
        << "LIST at the root failed: " << curl_easy_strerror(list.curlCode) << "\n"
        << list.controlLog;

    ASSERT_FALSE(list.body.empty()) << "The root listing came back empty. Control log:\n"
                                    << list.controlLog;

    EXPECT_NE(list.body.find(base64Handle(servedNode.get())), std::string::npos)
        << "The served handle was not listed at the root. Body:\n"
        << list.body;
    EXPECT_EQ(list.body.front(), 'd') << "The served folder was not listed as a directory. Body:\n"
                                      << list.body;

    EXPECT_EQ(API_OK, doDeleteNode(0, servedNode.get()));

    CASE_info << "finished";
}

/**
 * The reply to MKD must name the path the client can refer to the new folder by,
 * quoted, as RFC 959 Appendix II requires, and a name in it must not be able to
 * break the line framing.
 *
 * The command parser only requires the first CRLF pair to sit at the end of the
 * buffer, so a lone LF travels inside an argument untouched and a folder can be
 * created with one in its name. Such a path is escaped and stops being usable,
 * which is unavoidable: no argument can carry that byte back. A tab can, so the
 * path of a folder named with one has to come back whole and still resolve.

 */
TEST_F(SdkTest, FtpServerAnswersMkdWithAQuotedUsablePath)
{
    CASE_info << "started";

    const std::string escapedFolderName = "mkd_cafe_é\nfolder";
    const std::string reusableFolderName = "mkd_cafe_é\tfolder";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    auto rootNode = makeUniqueFrom(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);

    const std::string servedName = "ftp_reply_" + std::to_string(m_time());
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

    // Generating a local link also allows the handle to be served over FTP.
    std::unique_ptr<char[]> servedLink{megaApi[0]->ftpServerGetLocalLink(servedNode.get())};
    ASSERT_TRUE(servedLink);
    const std::string baseUrl = ftpBaseUrl(servedLink.get());
    ASSERT_FALSE(baseUrl.empty());

    const std::string servedRoot = "/" + base64Handle(servedNode.get()) + "/" + servedName;

    // A name holding a line feed: the path comes back escaped and on one line.
    {
        const std::string escapedPath = servedRoot + "/" + escapedFolderName;
        const FtpExchange mkd =
            FtpTestClient::rawControlCommand(baseUrl, "MKD " + escapedPath + "\r\n");
        ASSERT_EQ(CURLE_OK, mkd.curlCode)
            << "MKD failed: " << curl_easy_strerror(mkd.curlCode) << "\n"
            << mkd.controlLog;

        const std::string expected =
            "257 \"" + servedRoot + "/mkd_cafe_é%0Afolder\" directory created\r\n";
        EXPECT_NE(mkd.controlLog.find(expected), std::string::npos)
            << "The 257 reply did not quote the escaped path. Control log:\n"
            << mkd.controlLog;
        EXPECT_EQ(mkd.controlLog.find("\nfolder"), std::string::npos)
            << "The 257 reply was split by the name it carries. Control log:\n"
            << mkd.controlLog;

        auto createdNode =
            makeUniqueFrom(megaApi[0]->getChildNode(servedNode.get(), escapedFolderName.c_str()));
        EXPECT_TRUE(createdNode) << "MKD did not create the folder, so the reply proves nothing.";
    }

    // A name holding a tab: the path comes back whole and has to still resolve.
    {
        const std::string reusablePath = servedRoot + "/" + reusableFolderName;
        const FtpExchange mkd =
            FtpTestClient::rawControlCommand(baseUrl, "MKD " + reusablePath + "\r\n");
        ASSERT_EQ(CURLE_OK, mkd.curlCode)
            << "MKD failed: " << curl_easy_strerror(mkd.curlCode) << "\n"
            << mkd.controlLog;

        const std::string answeredPath = quotedPathFromReply(mkd.controlLog);
        ASSERT_FALSE(answeredPath.empty()) << "The 257 reply carried no quoted path. Control log:\n"
                                           << mkd.controlLog;
        EXPECT_EQ(answeredPath, reusablePath)
            << "The 257 reply altered a path that has to stay usable. Control log:\n"
            << mkd.controlLog;

        // The whole point of the reply: the client can refer to the folder by it.
        const FtpExchange cwd =
            FtpTestClient::rawControlCommand(baseUrl, "CWD " + answeredPath + "\r\n");
        ASSERT_EQ(CURLE_OK, cwd.curlCode)
            << "CWD failed: " << curl_easy_strerror(cwd.curlCode) << "\n"
            << cwd.controlLog;
        EXPECT_NE(cwd.controlLog.find("250 "), std::string::npos)
            << "The path from the 257 reply did not resolve. Control log:\n"
            << cwd.controlLog;
    }

    EXPECT_EQ(API_OK, doDeleteNode(0, servedNode.get()));

    CASE_info << "finished";
}

/**
 * An ordinary name must reach the client untouched in every reply that carries
 * one. Without this a sanitizer that grows too broad, or an escape applied twice,
 * would pass the tests that only look for escaped sequences.
 */
TEST_F(SdkTest, FtpServerRendersOrdinaryNamesVerbatim)
{
    CASE_info << "started";

    const std::string plainFolderName = "plain_folder_café";
    const std::string plainFileName = "plain_report_café.pdf";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    auto rootNode = makeUniqueFrom(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);

    const std::string parentName = "ftp_plain_" + std::to_string(m_time());
    const MegaHandle parentHandle = createFolder(0, parentName.c_str(), rootNode.get());
    ASSERT_NE(parentHandle, UNDEF);
    auto parentNode = makeUniqueFrom(megaApi[0]->getNodeByHandle(parentHandle));
    ASSERT_TRUE(parentNode);

    const MegaHandle plainFolderHandle = createFolder(0, plainFolderName.c_str(), parentNode.get());
    ASSERT_NE(plainFolderHandle, UNDEF);
    auto plainFolderNode = makeUniqueFrom(megaApi[0]->getNodeByHandle(plainFolderHandle));
    ASSERT_TRUE(plainFolderNode);

    auto plainFile = sdk_test::uploadFile(megaApi[0].get(),
                                          sdk_test::LocalTempFile{"ftp_plain_upload.txt", "test"},
                                          plainFolderNode.get(),
                                          plainFileName.c_str());
    ASSERT_TRUE(plainFile);

    // Entering the child and issuing CDUP rebuilds cwdpath from the folder name,
    // which PWD then reflects into its 257 reply.
    const MegaHandle childHandle = createFolder(0, "child", plainFolderNode.get());
    ASSERT_NE(childHandle, UNDEF);
    auto childNode = makeUniqueFrom(megaApi[0]->getNodeByHandle(childHandle));
    ASSERT_TRUE(childNode);

    ASSERT_TRUE(megaApi[0]->ftpServerStart(true, 0));
    const auto ftpServer = makeScopedDestructor(
        [&api = megaApi]()
        {
            api[0]->ftpServerStop();
        });

    // Generating a local link also allows the handle to be served over FTP.
    std::unique_ptr<char[]> parentFtpLink{megaApi[0]->ftpServerGetLocalLink(parentNode.get())};
    ASSERT_TRUE(parentFtpLink);
    std::unique_ptr<char[]> plainFolderFtpLink{
        megaApi[0]->ftpServerGetLocalLink(plainFolderNode.get())};
    ASSERT_TRUE(plainFolderFtpLink);
    std::unique_ptr<char[]> childFtpLink{megaApi[0]->ftpServerGetLocalLink(childNode.get())};
    ASSERT_TRUE(childFtpLink);

    const std::string listUrl = std::string{plainFolderFtpLink.get()} + "/";
    const std::string baseUrl = ftpBaseUrl(parentFtpLink.get());
    ASSERT_FALSE(baseUrl.empty());

    {
        const FtpExchange list = FtpTestClient::list(listUrl);
        ASSERT_EQ(CURLE_OK, list.curlCode)
            << "LIST failed: " << curl_easy_strerror(list.curlCode) << "\n"
            << list.controlLog;

        EXPECT_NE(list.body.find(plainFileName), std::string::npos)
            << "LIST altered an ordinary name. Body:\n"
            << list.body;
        EXPECT_EQ(list.body.find('%'), std::string::npos)
            << "LIST escaped something in a listing of ordinary names. Body:\n"
            << list.body;
    }

    {
        const FtpExchange nlst = FtpTestClient::nlst(listUrl);
        ASSERT_EQ(CURLE_OK, nlst.curlCode)
            << "NLST failed: " << curl_easy_strerror(nlst.curlCode) << "\n"
            << nlst.controlLog;

        EXPECT_NE(nlst.body.find(plainFileName), std::string::npos)
            << "NLST altered an ordinary name. Body:\n"
            << nlst.body;
        EXPECT_EQ(nlst.body.find('%'), std::string::npos)
            << "NLST escaped something in a listing of ordinary names. Body:\n"
            << nlst.body;
    }

    {
        const std::vector<std::string> commands{
            "*CWD /" + base64Handle(childNode.get()),
            "*CDUP",
            "*PWD",
        };
        const FtpExchange pwd = FtpTestClient::runControlCommands(baseUrl, commands);
        ASSERT_EQ(CURLE_OK, pwd.curlCode)
            << "Control commands failed: " << curl_easy_strerror(pwd.curlCode) << "\n"
            << pwd.controlLog;

        const std::string expectedReply =
            "257 \"/" + base64Handle(plainFolderNode.get()) + "/" + plainFolderName + "\"";
        EXPECT_NE(pwd.controlLog.find(expectedReply), std::string::npos)
            << "PWD altered an ordinary path. Control log:\n"
            << pwd.controlLog;
    }

    EXPECT_EQ(API_OK, doDeleteNode(0, parentNode.get()));

    CASE_info << "finished";
}

/**
 * FEAT is the one reply that spans several lines by design, so it is the case
 * that the single line guard has to let through. With assertions enabled a guard
 * that stopped exempting it would abort here instead of failing quietly.
 */
TEST_F(SdkTest, FtpServerKeepsMultiLineFeatReply)
{
    CASE_info << "started";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    auto rootNode = makeUniqueFrom(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);

    const std::string servedName = "ftp_feat_" + std::to_string(m_time());
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

    // Generating a local link also allows the handle to be served over FTP.
    std::unique_ptr<char[]> servedLink{megaApi[0]->ftpServerGetLocalLink(servedNode.get())};
    ASSERT_TRUE(servedLink);
    const std::string baseUrl = ftpBaseUrl(servedLink.get());
    ASSERT_FALSE(baseUrl.empty());

    const FtpExchange feat = FtpTestClient::runControlCommands(baseUrl, {"*FEAT"});
    ASSERT_EQ(CURLE_OK, feat.curlCode)
        << "FEAT failed: " << curl_easy_strerror(feat.curlCode) << "\n"
        << feat.controlLog;

    EXPECT_NE(feat.controlLog.find("211-Features:\r\n"), std::string::npos)
        << "FEAT did not open a multi-line reply. Control log:\n"
        << feat.controlLog;
    EXPECT_NE(feat.controlLog.find("\r\n UTF8 ON\r\n"), std::string::npos)
        << "FEAT lost a feature line. Control log:\n"
        << feat.controlLog;
    EXPECT_NE(feat.controlLog.find("\r\n211 End\r\n"), std::string::npos)
        << "FEAT did not close the multi-line reply. Control log:\n"
        << feat.controlLog;

    EXPECT_EQ(API_OK, doDeleteNode(0, servedNode.get()));

    CASE_info << "finished";
}

/**
 * A command argument echoed back into a reply must not be able to split it.
 *
 * The command parser only checks that the first CRLF pair ends the buffer, so a
 * lone LF reaches an argument intact, and OPTS echoes its arguments back with no
 * node involved at all. Any client that can connect can therefore reach this
 * reply, which is why the escaping matters even though the client is splitting
 * a reply addressed to itself.
 */
TEST_F(SdkTest, FtpServerEscapesEchoedCommandArguments)
{
    CASE_info << "started";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    auto rootNode = makeUniqueFrom(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);

    const std::string servedName = "ftp_echo_" + std::to_string(m_time());
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

    // Generating a local link also allows the handle to be served over FTP.
    std::unique_ptr<char[]> servedLink{megaApi[0]->ftpServerGetLocalLink(servedNode.get())};
    ASSERT_TRUE(servedLink);
    const std::string baseUrl = ftpBaseUrl(servedLink.get());
    ASSERT_FALSE(baseUrl.empty());

    // The lone LF passes the parser, so it lands in the argument the reply echoes.
    const FtpExchange opts = FtpTestClient::rawControlCommand(baseUrl, "OPTS a\nb ON\r\n");
    ASSERT_EQ(CURLE_OK, opts.curlCode)
        << "OPTS failed: " << curl_easy_strerror(opts.curlCode) << "\n"
        << opts.controlLog;

    EXPECT_NE(opts.controlLog.find("501 Unrecognized OPTS A%0AB ON\r\n"), std::string::npos)
        << "The line feed was not escaped in the echoed argument. Control log:\n"
        << opts.controlLog;
    EXPECT_EQ(opts.controlLog.find("\nB ON"), std::string::npos)
        << "The reply was split by the argument it echoes. Control log:\n"
        << opts.controlLog;

    EXPECT_EQ(API_OK, doDeleteNode(0, servedNode.get()));

    CASE_info << "finished";
}

/**
 * A LIST issued at the handle itself, without the name segment that usually
 * follows it, lists the served node rather than its children. That is a third
 * branch of the LIST handler, and no other test reaches it.
 */
TEST_F(SdkTest, FtpServerListsTheServedFolderAtItsHandle)
{
    CASE_info << "started";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    auto rootNode = makeUniqueFrom(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);

    const std::string servedName = "ftp_handle_" + std::to_string(m_time());
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

    // Generating a local link also allows the handle to be served over FTP.
    std::unique_ptr<char[]> servedLink{megaApi[0]->ftpServerGetLocalLink(servedNode.get())};
    ASSERT_TRUE(servedLink);
    const std::string baseUrl = ftpBaseUrl(servedLink.get());
    ASSERT_FALSE(baseUrl.empty());

    // The path stops at the handle, so the working directory is the handle itself.
    const std::string handleUrl = baseUrl + base64Handle(servedNode.get()) + "/";
    const FtpExchange list = FtpTestClient::list(handleUrl);
    ASSERT_EQ(CURLE_OK, list.curlCode)
        << "LIST at the handle failed: " << curl_easy_strerror(list.curlCode) << "\n"
        << list.controlLog;

    const std::string listedLine = lineContaining(list.body, servedName);
    ASSERT_FALSE(listedLine.empty()) << "The served folder was not listed at its handle. Body:\n"
                                     << list.body;
    EXPECT_EQ(nameFromListingLine(listedLine), servedName)
        << "LIST altered the name at the handle. Body:\n"
        << list.body;
    EXPECT_EQ(listedLine.front(), 'd') << "The served folder was not listed as a directory. Body:\n"
                                       << list.body;

    EXPECT_EQ(API_OK, doDeleteNode(0, servedNode.get()));

    CASE_info << "finished";
}

/**
 * The reply that announces a download carries the node name, so a control byte in
 * it has to be escaped like anywhere else.
 *
 * A download cannot be driven to completion here: the streaming callbacks fire
 * from the FTP server thread and trip a thread assertion in the SDK. It does not
 * have to be. The server answers RETR on the control channel and only then looks
 * for a data connection to write to, so a client that asks for the transfer
 * without ever opening that connection sees the reply and nothing starts.
 */
TEST_F(SdkTest, FtpServerEscapesTheNameThatAnnouncesADownload)
{
    CASE_info << "started";

    const std::string retrFileName = "retr_cafe_é\treport.pdf";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    auto rootNode = makeUniqueFrom(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);

    const std::string servedName = "ftp_retr_" + std::to_string(m_time());
    const MegaHandle servedHandle = createFolder(0, servedName.c_str(), rootNode.get());
    ASSERT_NE(servedHandle, UNDEF);
    auto servedNode = makeUniqueFrom(megaApi[0]->getNodeByHandle(servedHandle));
    ASSERT_TRUE(servedNode);

    auto retrFile = sdk_test::uploadFile(megaApi[0].get(),
                                         sdk_test::LocalTempFile{"ftp_retr_upload.txt", "test"},
                                         servedNode.get(),
                                         retrFileName.c_str());
    ASSERT_TRUE(retrFile);

    ASSERT_TRUE(megaApi[0]->ftpServerStart(true, 0));
    const auto ftpServer = makeScopedDestructor(
        [&api = megaApi]()
        {
            api[0]->ftpServerStop();
        });

    // Generating a local link also allows the handle to be served over FTP.
    std::unique_ptr<char[]> servedLink{megaApi[0]->ftpServerGetLocalLink(servedNode.get())};
    ASSERT_TRUE(servedLink);
    const std::string baseUrl = ftpBaseUrl(servedLink.get());
    ASSERT_FALSE(baseUrl.empty());

    // PASV opens the transfer, RETR answers on the control channel. The port PASV
    // hands out is deliberately left alone, so no byte of the file is ever sent.
    const std::string retrPath =
        "/" + base64Handle(servedNode.get()) + "/" + servedName + "/" + retrFileName;
    const FtpExchange retr =
        FtpTestClient::rawControlCommands(baseUrl, {"PASV\r\n", "RETR " + retrPath + "\r\n"});
    ASSERT_EQ(CURLE_OK, retr.curlCode)
        << "RETR failed: " << curl_easy_strerror(retr.curlCode) << "\n"
        << retr.controlLog;

    EXPECT_NE(retr.controlLog.find("227 "), std::string::npos)
        << "PASV did not open a transfer. Control log:\n"
        << retr.controlLog;
    EXPECT_NE(retr.controlLog.find("150 Here comes the file: retr_cafe_é%09report.pdf\r\n"),
              std::string::npos)
        << "The tab was not escaped in the reply that announces the download. Control log:\n"
        << retr.controlLog;

    EXPECT_EQ(API_OK, doDeleteNode(0, servedNode.get()));

    CASE_info << "finished";
}

// A download over FTP cannot run in a build with assertions enabled. The streaming
// callbacks are fired from the FTP server thread, while
// MegaApiImpl::fireOnFtpStreamingStart() asserts that it runs on the SDK thread,
// so the very first transferred file aborts the process:
//
//   megaapi_impl.cpp: void mega::MegaApiImpl::fireOnFtpStreamingStart(...):
//   Assertion `threadId == std::this_thread::get_id()' failed.
//
// The reply that announces a transfer is covered by the test above, which stops
// before a byte is sent. What is missing is the transfer itself, which is what
// this test does. Enable it once those callbacks reach the SDK thread.
#if 0
TEST_F(SdkTest, FtpServerDownloadsAFile)
{
    CASE_info << "started";

    const std::string downloadedFileName = "download_cafe_é.txt";
    const std::string downloadedContent = "the bytes that have to come back";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    auto rootNode = makeUniqueFrom(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);

    const std::string servedName = "ftp_download_" + std::to_string(m_time());
    const MegaHandle servedHandle = createFolder(0, servedName.c_str(), rootNode.get());
    ASSERT_NE(servedHandle, UNDEF);
    auto servedNode = makeUniqueFrom(megaApi[0]->getNodeByHandle(servedHandle));
    ASSERT_TRUE(servedNode);

    auto downloadedFile =
        sdk_test::uploadFile(megaApi[0].get(),
                             sdk_test::LocalTempFile{"ftp_download_upload.txt", downloadedContent},
                             servedNode.get(),
                             downloadedFileName.c_str());
    ASSERT_TRUE(downloadedFile);

    ASSERT_TRUE(megaApi[0]->ftpServerStart(true, 0));
    const auto ftpServer = makeScopedDestructor(
        [&api = megaApi]()
        {
            api[0]->ftpServerStop();
        });

    // Generating a local link also allows the handle to be served over FTP.
    std::unique_ptr<char[]> fileLink{megaApi[0]->ftpServerGetLocalLink(downloadedFile.get())};
    ASSERT_TRUE(fileLink);

    const FtpExchange retr = FtpTestClient::download(fileLink.get());
    ASSERT_EQ(CURLE_OK, retr.curlCode)
        << "RETR failed: " << curl_easy_strerror(retr.curlCode) << "\n"
        << retr.controlLog;

    EXPECT_EQ(retr.body, downloadedContent) << "The file content did not transfer.";
    EXPECT_NE(retr.controlLog.find("150 Here comes the file: " + downloadedFileName),
              std::string::npos)
        << "The transfer was not announced with the name of the file. Control log:\n"
        << retr.controlLog;
    EXPECT_NE(retr.controlLog.find("226 "), std::string::npos)
        << "The transfer was not reported as complete. Control log:\n"
        << retr.controlLog;

    EXPECT_EQ(API_OK, doDeleteNode(0, servedNode.get()));

    CASE_info << "finished";
}
#endif

/**
 * A data port range handed over the wrong way round has to keep working.
 *
 * Nothing validates the two bounds, neither the SDK nor the callers that take
 * them from a command line, so a range that begins after it ends does reach the
 * server. It gets one attempt at the port it begins with, which is what it got
 * before the server started walking the range.
 */
TEST_F(SdkTest, FtpServerAcceptsADataPortRangeGivenTheWrongWayRound)
{
    CASE_info << "started";

    // Outside the range every other test uses, so a worker running one of those
    // cannot take this port first.
    constexpr int dataPortBegin = 1699;
    constexpr int dataPortEnd = 1698;

    const std::string listedFileName = "reversed_range_café.txt";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    auto rootNode = makeUniqueFrom(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);

    const std::string servedName = "ftp_reversed_" + std::to_string(m_time());
    const MegaHandle servedHandle = createFolder(0, servedName.c_str(), rootNode.get());
    ASSERT_NE(servedHandle, UNDEF);
    auto servedNode = makeUniqueFrom(megaApi[0]->getNodeByHandle(servedHandle));
    ASSERT_TRUE(servedNode);

    auto listedFile = sdk_test::uploadFile(megaApi[0].get(),
                                           sdk_test::LocalTempFile{"ftp_reversed_upload.txt", "t"},
                                           servedNode.get(),
                                           listedFileName.c_str());
    ASSERT_TRUE(listedFile);

    ASSERT_TRUE(megaApi[0]->ftpServerStart(true, 0, dataPortBegin, dataPortEnd));
    const auto ftpServer = makeScopedDestructor(
        [&api = megaApi]()
        {
            api[0]->ftpServerStop();
        });

    // Generating a local link also allows the handle to be served over FTP.
    std::unique_ptr<char[]> servedLink{megaApi[0]->ftpServerGetLocalLink(servedNode.get())};
    ASSERT_TRUE(servedLink);

    // Any listing needs a data channel, so it fails if no port was ever tried.
    const FtpExchange list = FtpTestClient::list(std::string{servedLink.get()} + "/");
    ASSERT_EQ(CURLE_OK, list.curlCode)
        << "LIST failed: " << curl_easy_strerror(list.curlCode) << "\n"
        << list.controlLog;

    EXPECT_EQ(list.controlLog.find("421 "), std::string::npos)
        << "The data channel was refused. Control log:\n"
        << list.controlLog;
    EXPECT_NE(list.body.find(listedFileName), std::string::npos)
        << "The file was not listed. Body:\n"
        << list.body;

    EXPECT_EQ(API_OK, doDeleteNode(0, servedNode.get()));

    CASE_info << "finished";
}
