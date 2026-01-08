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

#include "integration_test_utils.h"
#include "mock_listeners.h"
#include "SdkTest_test.h"

#include <curl/curl.h>
#include <gmock/gmock.h>

#include <algorithm>
#include <string>

class FtpServerTest: public SdkTest
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
        // Stop any FTP server if running
        for (auto& api: megaApi)
        {
            if (api)
                api->ftpServerStop();
        }

        SdkTest::TearDown();
    }

    struct FtpResponse
    {
        std::string data;
        long responseCode = 0;
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

    std::unique_ptr<MegaNode> createFolder(const std::string& name, MegaNode* parent = nullptr)
    {
        std::unique_ptr<MegaNode> rootNode;
        if (parent == nullptr)
        {
            rootNode = std::unique_ptr<MegaNode>(megaApi[0]->getRootNode());
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

    std::unique_ptr<MegaNode> uploadFile(const std::string& name,
                                         const std::string& contents,
                                         MegaNode* parent = nullptr)
    {
        std::unique_ptr<MegaNode> rootNode;
        if (parent == nullptr)
        {
            rootNode = std::unique_ptr<MegaNode>(megaApi[0]->getRootNode());
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

    FtpResponse performFtpRequest(const std::string& url,
                                  const std::string& method = "GET",
                                  const std::string& uploadData = "",
                                  const std::vector<std::string>& quoteCmds = {})
    {
        FtpResponse response;

        CURL* curl = curl_easy_init();
        if (!curl)
            return response;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");

        std::string uploadBuffer; // must live until perform() returns
        curl_slist* slist = nullptr; // for QUOTE commands (must be freed)

        auto setQuote = [&](const std::vector<std::string>& cmds)
        {
            for (const auto& c: cmds)
                slist = curl_slist_append(slist, c.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTQUOTE, slist);

            // Don’t do a RETR by default when we only want to run commands
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        };

        if (method == "PUT")
        {
            uploadBuffer = uploadData;

            curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
            curl_easy_setopt(curl, CURLOPT_READFUNCTION, readCallback);
            curl_easy_setopt(curl, CURLOPT_READDATA, &uploadBuffer);
            curl_easy_setopt(curl,
                             CURLOPT_INFILESIZE_LARGE,
                             static_cast<curl_off_t>(uploadBuffer.size()));
        }
        else if (method == "GET")
        {
            // Default for ftp:// is RETR (download). Nothing special needed.
        }
        else if (method == "LIST")
        {
            // NLST-like listing
            curl_easy_setopt(curl, CURLOPT_DIRLISTONLY, 1L);
        }
        else if (method == "MKD")
        {
            // IMPORTANT: url should point to the *parent* directory
            // and the folder name must be provided as a QUOTE cmd, e.g. "MKD newfolder"
            setQuote(quoteCmds);
        }
        else if (method == "RMD" || method == "DELE")
        {
            // Same mechanism as MKD: quote commands
            setQuote(quoteCmds);
        }
        else if (method == "RNFR_RNTO")
        {
            // Rename is a pair of commands, e.g. "RNFR oldname", "RNTO newname"
            setQuote(quoteCmds);
        }
        else if (method == "QUOTE")
        {
            // Generic escape hatch: send whatever FTP commands you want
            setQuote(quoteCmds);
        }
        else
        {
            // If you really want to treat "method" as a raw FTP command:
            // use QUOTE and pass it in quoteCmds.
            setQuote(quoteCmds);
        }

        const CURLcode res = curl_easy_perform(curl);

        if (res == CURLE_OK)
        {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.responseCode);
        }
        else
        {
            response.data = curl_easy_strerror(res);
            response.responseCode = -1;
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(slist);
        return response;
    }
};

TEST_F(FtpServerTest, ServerStartStop)
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

TEST_F(FtpServerTest, ServerLocalOnly)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));

    CASE_info << "started";
    // Start server with local only = true
    ASSERT_TRUE(megaApi[0]->ftpServerStart(true, 0));

    EXPECT_TRUE(megaApi[0]->ftpServerIsLocalOnly());

    megaApi[0]->ftpServerStop();

    // Start server with local only = false
    megaApi[0]->ftpServerStart(false, 0);

    EXPECT_FALSE(megaApi[0]->ftpServerIsLocalOnly());
    CASE_info << "finished";
}

/**
 * Test for FTP server using port 0, which also consist of:
 * - start two FTP servers from a thread and no ports conflicting
 * - stop FTP servers from a different thread, to allow TSAN to report any data races
 */
TEST_F(FtpServerTest, FtpServerCanUsePort0)
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

TEST_F(FtpServerTest, RestrictedMode)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Start FTP server
    ASSERT_TRUE(megaApi[0]->ftpServerStart(true, 0));

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

    // TODO: should test whether the link is valid to download, but currently download is not vaid
    // because of thread issue
    CASE_info << "finished";
}

TEST_F(FtpServerTest, Listeners)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    testing::NiceMock<MockMegaTransferListener> mockListener{megaApi[0].get()};
    // Start FTP server
    ASSERT_TRUE(megaApi[0]->ftpServerStart(true, 0));

    // Add listener
    megaApi[0]->ftpServerAddListener(&mockListener);

    // TODO: test listener. Listener only support download, but currently download is not valid
    // because of thread issue

    // Remove listener
    megaApi[0]->ftpServerRemoveListener(&mockListener);
    CASE_info << "finished";
}

// test FTP links and operations: LIST, GET
TEST_F(FtpServerTest, LinksAndOperations)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Start FTP server
    ASSERT_TRUE(megaApi[0]->ftpServerStart(true, 0));

    auto folderNode = createFolder("ftp_test_link_folder");
    ASSERT_TRUE(folderNode);

    std::string testFileName = "ftptest_link.txt";
    std::string testFileContent = "This is a test file for FTP server testing.\nLine 2\nLine 3\n";
    auto testNode = uploadFile(testFileName, testFileContent);
    ASSERT_TRUE(testNode);

    // Get root link
    std::unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    std::unique_ptr<char[]> rootLink(megaApi[0]->ftpServerGetLocalLink(rootNode.get()));
    ASSERT_TRUE(rootLink);
    CASE_info << "FTP root link: " << rootLink.get();

    // Test LIST root directory
    FtpResponse listRootResponse = performFtpRequest(rootLink.get(), "LIST");
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
    FtpResponse listFolderResponse = performFtpRequest(folderLink.get(), "LIST");
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
    * == std::this_thread::get_id()' failed. FtpResponse getResponse =
    performFtpRequest(fileLink.get());
    auto getResponse = performFtpRequest(fileLink.get(), "GET");
    EXPECT_EQ(226, getResponse.responseCode); // FTP success code
    EXPECT_EQ(testFileContent, getResponse.data);
    */

    CASE_info << "finished";
}

TEST_F(FtpServerTest, BufferSizeSettings)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Start FTP server
    ASSERT_TRUE(megaApi[0]->ftpServerStart(true, 0));

    // Test default buffer size
    int defaultBufferSize = megaApi[0]->ftpServerGetMaxBufferSize();
    EXPECT_GT(defaultBufferSize, 0);

    // Set new buffer size
    int newBufferSize = 1024 * 1024; // 1MB
    megaApi[0]->ftpServerSetMaxBufferSize(newBufferSize);

    EXPECT_EQ(newBufferSize, megaApi[0]->ftpServerGetMaxBufferSize());
    CASE_info << "finished";
}

TEST_F(FtpServerTest, OutputSizeSettings)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Start FTP server
    ASSERT_TRUE(megaApi[0]->ftpServerStart(true, 0));

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
TEST_F(FtpServerTest, FtpUploadOperation)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Start FTP server
    ASSERT_TRUE(megaApi[0]->ftpServerStart(true, 0));

    std::string uploadFileName = "ftp_upload_test.txt";
    std::string uploadFileContent =
        "This is a test file for FTP upload operation.\nLine 2\nLine 3\n";
    std::unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);
    std::unique_ptr<char[]> rootLink(megaApi[0]->ftpServerGetLocalLink(rootNode.get()));
    ASSERT_TRUE(rootLink);
    std::string uploadUrl = std::string(rootLink.get()) + "/" + uploadFileName;
    CASE_info << "FTP upload URL: " << uploadUrl;

    FtpResponse uploadResponse = performFtpRequest(uploadUrl, "PUT", uploadFileContent);
    EXPECT_EQ(250, uploadResponse.responseCode); // FTP success code
    // Verify file uploaded
    std::unique_ptr<MegaNode> uploadedNode(
        megaApi[0]->getNodeByPath(uploadFileName.c_str(), rootNode.get()));
    ASSERT_TRUE(uploadedNode);
    EXPECT_EQ(uploadedNode->getType(), MegaNode::TYPE_FILE);
    CASE_info << "finished";
}

// test FTP mkdir operation
TEST_F(FtpServerTest, FtpMkdirOperation)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Start FTP server
    ASSERT_TRUE(megaApi[0]->ftpServerStart(true, 0));

    std::string newFolderName = "ftp_mkdir_test_folder";
    std::unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);
    std::unique_ptr<char[]> rootLink(megaApi[0]->ftpServerGetLocalLink(rootNode.get()));
    ASSERT_TRUE(rootLink);
    CASE_info << "FTP root link: " << rootLink.get();

    std::string mkdirUrl = std::string(rootLink.get());
    std::vector<std::string> quoteCmds = {"MKD " + newFolderName};
    FtpResponse mkdirResponse = performFtpRequest(mkdirUrl, "MKD", "", quoteCmds);
    EXPECT_EQ(257, mkdirResponse.responseCode); // FTP success code
    // Verify folder created
    std::unique_ptr<MegaNode> newFolderNode(
        megaApi[0]->getNodeByPath(newFolderName.c_str(), rootNode.get()));
    ASSERT_TRUE(newFolderNode);
    EXPECT_EQ(newFolderNode->getType(), MegaNode::TYPE_FOLDER);
    CASE_info << "finished";
}

// test FTP delete file operation
TEST_F(FtpServerTest, FtpDeleteOperation)
{
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Start FTP server
    ASSERT_TRUE(megaApi[0]->ftpServerStart(true, 0));

    // create a file to delete
    std::string deleteFileName = "ftp_delete_test.txt";
    std::string deleteFileContent =
        "This is a test file for FTP delete operation.\nLine 2\nLine 3\n";
    auto deleteNode = uploadFile(deleteFileName, deleteFileContent);
    ASSERT_TRUE(deleteNode);
    std::unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);
    std::unique_ptr<char[]> rootLink(megaApi[0]->ftpServerGetLocalLink(rootNode.get()));
    ASSERT_TRUE(rootLink);
    std::string deleteUrl = std::string(rootLink.get()) + "/" + deleteFileName;
    CASE_info << "FTP delete URL: " << deleteUrl;

    std::vector<std::string> quoteCmds = {"DELE " + deleteFileName};
    FtpResponse deleteResponse = performFtpRequest(deleteUrl, "DELE", "", quoteCmds);
    EXPECT_EQ(250, deleteResponse.responseCode); // FTP success code

    // Verify file deleted
    std::unique_ptr<MegaNode> deletedNode(megaApi[0]->getNodeByHandle(deleteNode->getHandle()));
    EXPECT_FALSE(deletedNode);
    CASE_info << "finished";
}

// RMD operation can delete non-empty folders
TEST_F(FtpServerTest, FtpRMDOperation)
{
    // test FTP RMD operation
    ASSERT_NO_FATAL_FAILURE(SdkTest::getAccountsForTest(1));
    CASE_info << "started";
    // Start FTP server
    ASSERT_TRUE(megaApi[0]->ftpServerStart(true, 0));
    // create a folder to delete
    std::string deleteFolderName = "ftp_rmd_test_folder";
    auto deleteFolderNode = createFolder(deleteFolderName);
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
    auto nestedFileNode = uploadFile(nestedFileName, nestedFileContent, deleteFolderNode.get());
    ASSERT_TRUE(nestedFileNode);

    EXPECT_EQ(nestedFileNode->getParentHandle(), deleteFolderNode->getHandle());

    std::vector<std::string> quoteCmds = {"RMD " + deleteFolderName};
    FtpResponse rmdResponse = performFtpRequest(deleteUrl, "RMD", "", quoteCmds);
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