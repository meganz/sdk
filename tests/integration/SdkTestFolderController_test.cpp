/**
 * @file SdkTestFoldercontroller_test.cpp
 * @brief This file defines tests related to the folder controller functionality.
 */

#include "mock_listeners.h"
#include "SdkTest_test.h"

using namespace testing;

class SdkTestFolderController: public SdkTest
{
public:
    void SetUp() override
    {
        SdkTest::SetUp();
        ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
        rootNode.reset(megaApi[0]->getRootNode());
        ASSERT_TRUE(rootNode);
    }

    void createLocalTree()
    {
        removeLocalTree();
        // Expand to a more complex structure when needed
        fs::create_directories(localFolderPath);
        ASSERT_TRUE(createFile(path_u8string(localFolderPath / localFileName), false));
    }

    void removeLocalTree()
    {
        if (fs::exists(localFolderPath))
        {
            std::error_code ignoredEc;
            fs::remove_all(localFolderPath, ignoredEc);
        }
        ASSERT_FALSE(fs::exists(localFolderPath));
    }

    const std::string& getLocalFolderName() const
    {
        return localFolderName;
    }

    const std::string& getFileName() const
    {
        return localFileName;
    }

    const unique_ptr<MegaNode>& getRootNode() const
    {
        return rootNode;
    }

private:
    const std::string localFolderName = getFilePrefix() + "baseDir";
    const std::string localFileName = "fileTest"; // One (any) file in the tree structure
    const fs::path localFolderPath = fs::current_path() / localFolderName;
    unique_ptr<MegaNode> rootNode;
};

/**
 * Check propagation of appData to files of folder transfers
 */
TEST_F(SdkTestFolderController, AppData)
{
    const std::string testAppData = "myAppData";
    static const std::string logPre{getLogPrefix()};

    LOG_info << logPre << "starting";
    std::unique_ptr<NiceMock<MockTransferListener>> listener;

    // Add a listener and expectations on the transfers:
    // - A specific file should be uploaded once. Store its appData in the promise
    // - A specific file should be downloaded once. Store its appData in the promise
    listener.reset(new NiceMock<MockTransferListener>{megaApi[0].get()});
    std::promise<std::string> appDataUploadTransfer;
    std::promise<std::string> appDataDownloadTransfer;
    const auto matchFileName =
        Pointee(Property(&MegaTransfer::getFileName, EndsWith(getFileName())));
    const auto matchUpload = Pointee(Property(&MegaTransfer::getType, MegaTransfer::TYPE_UPLOAD));
    const auto matchDownload =
        Pointee(Property(&MegaTransfer::getType, MegaTransfer::TYPE_DOWNLOAD));

    EXPECT_CALL(*listener.get(), onTransferStart).Times(AnyNumber());
    EXPECT_CALL(*listener.get(), onTransferStart(_, AllOf(matchFileName, matchUpload)))
        .WillOnce(
            [&appDataUploadTransfer](MegaApi*, MegaTransfer* transfer)
            {
                if (transfer->getAppData())
                {
                    appDataUploadTransfer.set_value(transfer->getAppData());
                }
                else
                {
                    appDataUploadTransfer.set_value("");
                }
            });
    EXPECT_CALL(*listener.get(), onTransferStart(_, AllOf(matchFileName, matchDownload)))
        .WillOnce(
            [&appDataDownloadTransfer](MegaApi*, MegaTransfer* transfer)
            {
                if (transfer->getAppData())
                {
                    appDataDownloadTransfer.set_value(transfer->getAppData());
                }
                else
                {
                    appDataDownloadTransfer.set_value("");
                }
            });
    megaApi[0]->addListener(listener.get());

    LOG_info << logPre << "Testing appData during a folder upload";
    createLocalTree();

    MegaHandle remoteFolderHandle = INVALID_HANDLE;
    ASSERT_EQ(MegaError::API_OK,
              doStartUpload(0,
                            &remoteFolderHandle,
                            getLocalFolderName().c_str(),
                            getRootNode().get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            testAppData.c_str(),
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/
                            ))
        << "Failed to upload a folder";

    auto futureAppData = appDataUploadTransfer.get_future();
    ASSERT_EQ(futureAppData.wait_for(1s), std::future_status::ready)
        << "Expected file not uploaded";
    ASSERT_EQ(testAppData, futureAppData.get())
        << "appData has not been correctly propagated to the upload subtransfers";

    LOG_info << logPre << "Testing appData during a folder download";
    removeLocalTree();

    unique_ptr<MegaNode> remoteFolderNode{megaApi[0]->getNodeByHandle(remoteFolderHandle)};
    ASSERT_TRUE(remoteFolderNode);

    ASSERT_EQ(MegaError::API_OK,
              doStartDownload(0,
                              remoteFolderNode.get(),
                              getLocalFolderName().c_str(),
                              nullptr /*customName*/,
                              testAppData.c_str(),
                              false /*startFirst*/,
                              nullptr /*cancelToken*/,
                              MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                              MegaTransfer::COLLISION_RESOLUTION_OVERWRITE,
                              false /* undelete */
                              ))
        << "Failed to download a folder";

    futureAppData = appDataDownloadTransfer.get_future();
    ASSERT_EQ(futureAppData.wait_for(1s), std::future_status::ready)
        << "Expected file not downloaded";
    ASSERT_EQ(testAppData, futureAppData.get())
        << "appData has not been correctly propagated to the download subtransfers";
}
