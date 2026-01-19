/**
 * @brief Mega SDK test file for server implementations (TCP, FTP)
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
#include "mock_listeners.h"
#include "sdk_server_test_utils.h"

#include <curl/curl.h>
#include <gmock/gmock.h>

#include <algorithm>
#include <cstring>
#include <future>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

int genRandomPort(int start = 30000, int end = 60000)
{
    static thread_local std::mt19937 gen{std::random_device{}()};
    std::uniform_int_distribution<int> dist(start, end);
    return dist(gen);
}

std::optional<ScopedDestructor> scopedFtpServer(MegaApi* api)
{
    if (!api)
        return std::nullopt;

    int port = genRandomPort();
    if (!api->ftpServerStart(true, 0, port, port + 100))
        return std::nullopt;

    if (!api->ftpServerIsRunning())
        return std::nullopt;

    return makeScopedDestructor(
        [api]()
        {
            api->ftpServerStop();
        });
}

class SdkFtpServerTest: public SdkServerTest
{};

/**
 * Helper class for FTP client requests
 */
class FtpClient
{
public:
    struct FtpResponse
    {
        std::string data;
        long responseCode = 0;
    };

    static FtpResponse get(const std::string& url)
    {
        return performFtpRequest(url, FtpMethod::GET);
    }

    static FtpResponse put(const std::string& url, const std::string& data)
    {
        return performFtpRequest(url, FtpMethod::PUT, data);
    }

    static FtpResponse list(const std::string& url)
    {
        return performFtpRequest(url, FtpMethod::LIST);
    }

    static FtpResponse mkd(const std::string& url, const std::string& folderName)
    {
        return performFtpRequest(url, FtpMethod::MKD, "", {"MKD " + folderName});
    }

    static FtpResponse rmd(const std::string& url, const std::string& folderName)
    {
        return performFtpRequest(url, FtpMethod::RMD, "", {"RMD " + folderName});
    }

    static FtpResponse dele(const std::string& url, const std::string& fileName)
    {
        return performFtpRequest(url, FtpMethod::DELE, "", {"DELE " + fileName});
    }

    static FtpResponse rename(const std::string& url,
                              const std::string& oldName,
                              const std::string& newName)
    {
        return performFtpRequest(url,
                                 FtpMethod::RNFR_RNTO,
                                 "",
                                 {"RNFR " + oldName, "RNTO " + newName});
    }

private:
    enum class FtpMethod
    {
        GET,
        PUT,
        LIST,
        MKD,
        RMD,
        DELE,
        RNFR_RNTO
    };

    static size_t writeCallback(void* contents, size_t size, size_t nmemb, FtpResponse* response)
    {
        size_t totalSize = size * nmemb;
        response->data.append(static_cast<char*>(contents), totalSize);
        return totalSize;
    }

    static size_t readCallback(void* ptr, size_t size, size_t nmemb, std::string* data)
    {
        size_t totalSize = size * nmemb;
        size_t dataSize = data->length();
        size_t copySize = std::min(totalSize, dataSize);

        if (copySize > 0)
        {
            memcpy(ptr, data->c_str(), copySize);
            data->erase(0, copySize);
        }

        return copySize;
    }

    static FtpResponse performFtpRequest(const std::string& url,
                                         FtpMethod method,
                                         const std::string& uploadData = "",
                                         const std::vector<std::string>& quoteCmds = {})
    {
        FtpResponse response;
        auto easyCurl = sdk_test::EasyCurl();
        auto easyCurlSlist = sdk_test::EasyCurlSlist();

        CURL* curl = easyCurl.curl();

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");

        std::string uploadBuffer; // must live until perform() returns

        auto setQuote = [&](const std::vector<std::string>& cmds) -> bool
        {
            if (!easyCurlSlist.appendFtpCommands(cmds))
            {
                return false;
            }
            curl_easy_setopt(curl, CURLOPT_POSTQUOTE, easyCurlSlist.slist());

            // Don’t do a RETR by default when we only want to run commands
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
            return true;
        };

        switch (method)
        {
            case FtpMethod::PUT:
                uploadBuffer = uploadData;

                curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
                curl_easy_setopt(curl, CURLOPT_READFUNCTION, readCallback);
                curl_easy_setopt(curl, CURLOPT_READDATA, &uploadBuffer);
                curl_easy_setopt(curl,
                                 CURLOPT_INFILESIZE_LARGE,
                                 static_cast<curl_off_t>(uploadBuffer.size()));
                break;

            case FtpMethod::GET:
                // Default for ftp:// is RETR (download). Nothing special needed.
                break;

            case FtpMethod::LIST:
                // NLST-like listing
                curl_easy_setopt(curl, CURLOPT_DIRLISTONLY, 1L);
                break;

            case FtpMethod::MKD:
            case FtpMethod::RMD:
            case FtpMethod::DELE:
            case FtpMethod::RNFR_RNTO:
                // the folder name must be provided as a QUOTE cmd, e.g. "MKD newfolder", "RMD
                // oldfolder", "DELE filename" Rename is a pair of commands, e.g. "RNFR oldname",
                // "RNTO newname"
                if (!setQuote(quoteCmds))
                {
                    LOG_err << "Failed to set FTP quote commands";
                    response.responseCode = -1;
                    return response;
                }
                break;
            default:
                throw std::runtime_error("Unsupported FTP method");
        }

        const CURLcode res = curl_easy_perform(curl);

        if (res == CURLE_OK)
        {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.responseCode);
        }
        else
        {
            LOG_err << "CURL error for FTP " << static_cast<int>(method) << " " << url << ": "
                    << curl_easy_strerror(res) << " (code: " << res << ")";
            response.data = curl_easy_strerror(res);
            response.responseCode = -1;
        }

        return response;
    }
};

TEST_F(SdkFtpServerTest, ServerStartStop)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));

    CASE_info << "started";
    // Test server is initially stopped
    ASSERT_FALSE(megaApi[0]->ftpServerIsRunning());

    // Start FTP server
    ASSERT_TRUE(megaApi[0]->ftpServerStart(true, 0));

    // Test server is running
    ASSERT_TRUE(megaApi[0]->ftpServerIsRunning());

    megaApi[0]->ftpServerStop();
    // Test server is stopped
    EXPECT_FALSE(megaApi[0]->ftpServerIsRunning());
    CASE_info << "finished";
}

TEST_F(SdkFtpServerTest, ServerLocalOnly)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));

    CASE_info << "started";
    // Start server with local only = true
    ASSERT_TRUE(megaApi[0]->ftpServerStart(true, 0));

    EXPECT_TRUE(megaApi[0]->ftpServerIsLocalOnly());

    megaApi[0]->ftpServerStop();

    // Start server with local only = false
    ASSERT_TRUE(megaApi[0]->ftpServerStart(false, 0));

    EXPECT_FALSE(megaApi[0]->ftpServerIsLocalOnly());
    megaApi[0]->ftpServerStop();
    CASE_info << "finished";
}

/**
 * Test for FTP server using port 0, which also consist of:
 * - start two FTP servers from a thread and no ports conflicting
 * - stop FTP servers from a different thread, to allow TSAN to report any data races
 */
TEST_F(SdkFtpServerTest, FtpServerCanUsePort0)
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

TEST_F(SdkFtpServerTest, RestrictedMode)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Start FTP server
    auto server = scopedFtpServer(megaApi[0].get());
    ASSERT_TRUE(server);

    // Test default restricted mode
    EXPECT_EQ(MegaApi::TCP_SERVER_ALLOW_CREATED_LOCAL_LINKS,
              megaApi[0]->ftpServerGetRestrictedMode());

    megaApi[0]->ftpServerSetRestrictedMode(MegaApi::TCP_SERVER_DENY_ALL);
    EXPECT_EQ(MegaApi::TCP_SERVER_DENY_ALL, megaApi[0]->ftpServerGetRestrictedMode());

    megaApi[0]->ftpServerSetRestrictedMode(MegaApi::TCP_SERVER_ALLOW_ALL);
    EXPECT_EQ(MegaApi::TCP_SERVER_ALLOW_ALL, megaApi[0]->ftpServerGetRestrictedMode());

    megaApi[0]->ftpServerSetRestrictedMode(MegaApi::TCP_SERVER_ALLOW_CREATED_LOCAL_LINKS);
    EXPECT_EQ(MegaApi::TCP_SERVER_ALLOW_CREATED_LOCAL_LINKS,
              megaApi[0]->ftpServerGetRestrictedMode());

    megaApi[0]->ftpServerSetRestrictedMode(MegaApi::TCP_SERVER_ALLOW_LAST_LOCAL_LINK);
    EXPECT_EQ(MegaApi::TCP_SERVER_ALLOW_LAST_LOCAL_LINK, megaApi[0]->ftpServerGetRestrictedMode());

    // TODO: should test whether the link is valid to download, but currently download is not valid
    // because of thread issue
    CASE_info << "finished";
}

TEST_F(SdkFtpServerTest, Listeners)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    testing::NiceMock<MockMegaTransferListener> mockListener{megaApi[0].get()};
    // Start FTP server
    auto server = scopedFtpServer(megaApi[0].get());
    ASSERT_TRUE(server);

    // Add listener
    megaApi[0]->ftpServerAddListener(&mockListener);

    // TODO: test listener. Listener only support download, but currently download is not valid
    // because of thread issue

    // Remove listener
    megaApi[0]->ftpServerRemoveListener(&mockListener);
    CASE_info << "finished";
}

// test FTP links and operations: LIST, GET
TEST_F(SdkFtpServerTest, LinksAndOperations)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Start FTP server
    auto server = scopedFtpServer(megaApi[0].get());
    ASSERT_TRUE(server);

    auto folderNode = createFolder(0, "ftp_test_link_folder");
    ASSERT_TRUE(folderNode);

    std::string testFileName = "ftptest_link.txt";
    std::string testFileContent = "This is a test file for FTP server testing.\nLine 2\nLine 3\n";
    auto testNode = uploadFile(0, testFileName, testFileContent);
    ASSERT_TRUE(testNode);

    // Get root link
    std::unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    std::unique_ptr<char[]> rootLink(megaApi[0]->ftpServerGetLocalLink(rootNode.get()));
    ASSERT_TRUE(rootLink);
    CASE_info << "FTP root link: " << rootLink.get();

    // Test LIST root directory
    auto listRootResponse = FtpClient::list(rootLink.get());
    EXPECT_EQ(226, listRootResponse.responseCode); // FTP success code

    // Get file link
    std::unique_ptr<char[]> fileLink(megaApi[0]->ftpServerGetLocalLink(testNode.get()));
    ASSERT_TRUE(fileLink);
    CASE_info << "FTP file link: " << fileLink.get();

    // Get folder link
    std::unique_ptr<char[]> folderLink(megaApi[0]->ftpServerGetLocalLink(folderNode.get()));
    ASSERT_TRUE(folderLink);
    CASE_info << "FTP folder link: " << folderLink.get();

    // Test LIST folder directory
    auto listFolderResponse = FtpClient::list(folderLink.get());
    EXPECT_EQ(226, listFolderResponse.responseCode); // FTP success code
    EXPECT_TRUE(listFolderResponse.data.find(testFileName) == std::string::npos);

    // Get all links
    std::unique_ptr<MegaStringList> links(megaApi[0]->ftpServerGetLinks());
    EXPECT_TRUE(links);
    EXPECT_EQ(links->size(), 3);

    /*
    * TODO: fix thread issue
    * Test GET file: sdk/src/megaapi_impl.cpp:11745: void
    * mega::MegaApiImpl::fireOnFtpStreamingStart(mega::MegaTransferPrivate*): Assertion `threadId
    * == std::this_thread::get_id()' failed.
    auto getResponse = FtpClient::get(fileLink.get());
    EXPECT_EQ(226, getResponse.responseCode); // FTP success code
    EXPECT_EQ(testFileContent, getResponse.data);
    */

    CASE_info << "finished";
}

TEST_F(SdkFtpServerTest, BufferSizeSettings)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Start FTP server
    auto server = scopedFtpServer(megaApi[0].get());
    ASSERT_TRUE(server);

    // Test default buffer size
    int defaultBufferSize = megaApi[0]->ftpServerGetMaxBufferSize();
    EXPECT_GT(defaultBufferSize, 0);

    // Set new buffer size
    int newBufferSize = 1024 * 1024; // 1MB
    megaApi[0]->ftpServerSetMaxBufferSize(newBufferSize);

    EXPECT_EQ(newBufferSize, megaApi[0]->ftpServerGetMaxBufferSize());
    CASE_info << "finished";
}

TEST_F(SdkFtpServerTest, OutputSizeSettings)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Start FTP server
    auto server = scopedFtpServer(megaApi[0].get());
    ASSERT_TRUE(server);

    // Test default output size
    int defaultOutputSize = megaApi[0]->ftpServerGetMaxOutputSize();
    EXPECT_GT(defaultOutputSize, 0);

    // Set new output size
    int newOutputSize = 2 * 1024 * 1024; // 2MB
    megaApi[0]->ftpServerSetMaxOutputSize(newOutputSize);

    EXPECT_EQ(newOutputSize, megaApi[0]->ftpServerGetMaxOutputSize());
    CASE_info << "finished";
}

// test FTP upload operation
TEST_F(SdkFtpServerTest, FtpUploadOperation)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Start FTP server
    auto server = scopedFtpServer(megaApi[0].get());
    ASSERT_TRUE(server);

    std::string uploadFileName = "ftp_upload_test.txt";
    std::string uploadFileContent =
        "This is a test file for FTP upload operation.\nLine 2\nLine 3\n";
    std::unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);
    std::unique_ptr<char[]> rootLink(megaApi[0]->ftpServerGetLocalLink(rootNode.get()));
    ASSERT_TRUE(rootLink);
    std::string uploadUrl = std::string(rootLink.get()) + "/" + uploadFileName;
    CASE_info << "FTP upload URL: " << uploadUrl;

    auto uploadResponse = FtpClient::put(uploadUrl, uploadFileContent);
    EXPECT_EQ(250, uploadResponse.responseCode); // FTP success code
    // Verify file uploaded
    std::unique_ptr<MegaNode> uploadedNode(
        megaApi[0]->getNodeByPath(uploadFileName.c_str(), rootNode.get()));
    ASSERT_TRUE(uploadedNode);
    EXPECT_EQ(uploadedNode->getType(), MegaNode::TYPE_FILE);
    CASE_info << "finished";
}

// test FTP mkdir operation
TEST_F(SdkFtpServerTest, FtpMkdirOperation)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Start FTP server
    auto server = scopedFtpServer(megaApi[0].get());
    ASSERT_TRUE(server);

    std::string newFolderName = "ftp_mkdir_test_folder";
    std::unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);
    std::unique_ptr<char[]> rootLink(megaApi[0]->ftpServerGetLocalLink(rootNode.get()));
    ASSERT_TRUE(rootLink);
    CASE_info << "FTP root link: " << rootLink.get();

    std::string mkdirUrl = std::string(rootLink.get());
    auto mkdirResponse = FtpClient::mkd(mkdirUrl, newFolderName);
    EXPECT_EQ(257, mkdirResponse.responseCode); // FTP success code
    // Verify folder created
    std::unique_ptr<MegaNode> newFolderNode(
        megaApi[0]->getNodeByPath(newFolderName.c_str(), rootNode.get()));
    ASSERT_TRUE(newFolderNode);
    EXPECT_EQ(newFolderNode->getType(), MegaNode::TYPE_FOLDER);
    CASE_info << "finished";
}

// test FTP delete file operation
TEST_F(SdkFtpServerTest, FtpDeleteOperation)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Start FTP server
    auto server = scopedFtpServer(megaApi[0].get());
    ASSERT_TRUE(server);

    // create a file to delete
    std::string deleteFileName = "ftp_delete_test.txt";
    std::string deleteFileContent =
        "This is a test file for FTP delete operation.\nLine 2\nLine 3\n";
    auto deleteNode = uploadFile(0, deleteFileName, deleteFileContent);
    ASSERT_TRUE(deleteNode);
    std::unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);
    std::unique_ptr<char[]> rootLink(megaApi[0]->ftpServerGetLocalLink(rootNode.get()));
    ASSERT_TRUE(rootLink);
    std::string deleteUrl = std::string(rootLink.get()) + "/" + deleteFileName;
    CASE_info << "FTP delete URL: " << deleteUrl;

    auto deleteResponse = FtpClient::dele(deleteUrl, deleteFileName);
    EXPECT_EQ(250, deleteResponse.responseCode); // FTP success code

    // Verify file deleted
    std::unique_ptr<MegaNode> deletedNode(megaApi[0]->getNodeByHandle(deleteNode->getHandle()));
    EXPECT_FALSE(deletedNode);
    CASE_info << "finished";
}

// RMD operation can delete non-empty folders
TEST_F(SdkFtpServerTest, FtpRMDOperation)
{
    // test FTP RMD operation
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Start FTP server
    auto server = scopedFtpServer(megaApi[0].get());
    ASSERT_TRUE(server);

    // create a folder to delete
    std::string deleteFolderName = "ftp_rmd_test_folder";
    auto deleteFolderNode = createFolder(0, deleteFolderName);
    ASSERT_TRUE(deleteFolderNode);
    std::unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);
    std::unique_ptr<char[]> rootLink(megaApi[0]->ftpServerGetLocalLink(rootNode.get()));
    ASSERT_TRUE(rootLink);
    std::string deleteUrl = std::string(rootLink.get()) + "/" + deleteFolderName;
    CASE_info << "FTP RMD URL: " << deleteUrl;

    // upload a file into the folder to test RMD failure on non-empty folder
    std::string nestedFileName = "nested_file.txt";
    std::string nestedFileContent = "This is a nested file.\n";
    auto nestedFileNode = uploadFile(0, nestedFileName, nestedFileContent, deleteFolderNode.get());
    ASSERT_TRUE(nestedFileNode);

    EXPECT_EQ(nestedFileNode->getParentHandle(), deleteFolderNode->getHandle());

    auto rmdResponse = FtpClient::rmd(deleteUrl, deleteFolderName);
    EXPECT_EQ(250, rmdResponse.responseCode); // FTP success code

    // Verify folder deleted
    std::unique_ptr<MegaNode> deletedFolderNode(
        megaApi[0]->getNodeByHandle(deleteFolderNode->getHandle()));
    EXPECT_FALSE(deletedFolderNode);

    // Verify file inside folder also deleted
    std::unique_ptr<MegaNode> deletedNestedFileNode(
        megaApi[0]->getNodeByHandle(nestedFileNode->getHandle()));
    EXPECT_FALSE(deletedNestedFileNode);
    CASE_info << "finished";
}

// test FTP rename operation
TEST_F(SdkFtpServerTest, FtpRenameOperation)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Start FTP server
    auto server = scopedFtpServer(megaApi[0].get());
    ASSERT_TRUE(server);
    // create a file to rename
    std::string originalFileName = "ftp_rename_test.txt";
    std::string fileContent = "This is a test file for FTP rename operation.\nLine 2\nLine 3\n";
    auto fileNode = uploadFile(0, originalFileName, fileContent);
    ASSERT_TRUE(fileNode);
    std::unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);
    std::unique_ptr<char[]> rootLink(megaApi[0]->ftpServerGetLocalLink(rootNode.get()));
    ASSERT_TRUE(rootLink);
    std::string renameUrl = std::string(rootLink.get()) + "/" + originalFileName;
    CASE_info << "FTP rename URL: " << renameUrl;

    std::string newFileName = "ftp_renamed_test.txt";
    auto renameResponse = FtpClient::rename(renameUrl, originalFileName, newFileName);
    EXPECT_EQ(250, renameResponse.responseCode); // FTP success code
    // Verify file renamed
    std::unique_ptr<MegaNode> renamedNode(
        megaApi[0]->getNodeByPath(newFileName.c_str(), rootNode.get()));
    ASSERT_TRUE(renamedNode);
    EXPECT_EQ(renamedNode->getType(), MegaNode::TYPE_FILE);
    EXPECT_EQ(renamedNode->getHandle(), fileNode->getHandle());
    CASE_info << "finished";
}