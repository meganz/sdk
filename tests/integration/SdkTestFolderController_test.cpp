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

    std::unique_ptr<MegaNode>
        getRequiredChild(MegaApi* api, MegaNode* parent, const char* name, int type)
    {
        std::unique_ptr<MegaNode> child(api->getChildNode(parent, name));
        EXPECT_TRUE(child) << "Missing node: " << name;
        if (child)
        {
            EXPECT_EQ(type, child->getType()) << "Unexpected type for node: " << name;
        }
        return child;
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

/**
 * Verify a merge upload succeeds when the destination already exists and only
 * part of the first remote level is pre-created.
 *
 * Steps:
 * 1. Create the local tree:
 *      baseDir/
 *        00_new/newFile
 *        10_existing/child_new/childFile
 * 2. Create the remote base folder and pre-create only:
 *      baseDir/
 *        10_existing/
 * 3. Upload local baseDir to the cloud drive root so the SDK must merge into
 *    the existing destination, create the missing sibling 00_new, and then
 *    continue below the already existing 10_existing branch.
 * 4. Assert the folder upload completes successfully.
 */
TEST_F(SdkTestFolderController, MergeUploadToExistingDestination)
{
    static const std::string logPre{getLogPrefix()};
    static constexpr const char* kNewFolder{"00_new"};
    static constexpr const char* kExistingFolder{"10_existing"};
    static constexpr const char* kChildNewFolder{"child_new"};
    static constexpr const char* kNewFile{"newFile"};
    static constexpr const char* kChildFile{"childFile"};

    LOG_info << logPre << "starting";

    removeLocalTree();

    const fs::path basePath = fs::current_path() / getLocalFolderName();
    const fs::path newFolder = basePath / kNewFolder;
    const fs::path existingFolder = basePath / kExistingFolder;
    const fs::path existingChild = existingFolder / kChildNewFolder;

    fs::create_directories(newFolder);
    fs::create_directories(existingChild);

    ASSERT_TRUE(createFile(path_u8string(newFolder / kNewFile), false));
    ASSERT_TRUE(createFile(path_u8string(existingChild / kChildFile), false));

    MegaHandle remoteBaseHandle =
        createFolder(0, getLocalFolderName().c_str(), getRootNode().get());
    ASSERT_NE(remoteBaseHandle, UNDEF) << "Failed to create remote base folder";

    std::unique_ptr<MegaNode> remoteBaseNode(megaApi[0]->getNodeByHandle(remoteBaseHandle));
    ASSERT_TRUE(remoteBaseNode) << "Failed to get remote base folder node";

    MegaHandle remoteExistingHandle = createFolder(0, kExistingFolder, remoteBaseNode.get());
    ASSERT_NE(remoteExistingHandle, UNDEF) << "Failed to create remote existing parent folder";

    MegaHandle uploadedHandle = INVALID_HANDLE;
    const int uploadErr = doStartUpload(0,
                                        &uploadedHandle,
                                        path_u8string(basePath).c_str(),
                                        getRootNode().get(),
                                        nullptr /*fileName*/,
                                        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                        nullptr /*appData*/,
                                        false /*isSourceTemporary*/,
                                        false /*startFirst*/,
                                        nullptr /*cancelToken*/);

    std::unique_ptr<MegaNode> refreshedRemoteBase(megaApi[0]->getNodeByHandle(remoteBaseHandle));
    if (refreshedRemoteBase)
    {
        auto newNode = getRequiredChild(megaApi[0].get(),
                                        refreshedRemoteBase.get(),
                                        kNewFolder,
                                        MegaNode::TYPE_FOLDER);
        auto existingNode = getRequiredChild(megaApi[0].get(),
                                             refreshedRemoteBase.get(),
                                             kExistingFolder,
                                             MegaNode::TYPE_FOLDER);
        ASSERT_TRUE(newNode && existingNode);

        auto newFile =
            getRequiredChild(megaApi[0].get(), newNode.get(), kNewFile, MegaNode::TYPE_FILE);
        auto childNewNode = getRequiredChild(megaApi[0].get(),
                                             existingNode.get(),
                                             kChildNewFolder,
                                             MegaNode::TYPE_FOLDER);
        ASSERT_TRUE(newFile && childNewNode);

        auto childFile =
            getRequiredChild(megaApi[0].get(), childNewNode.get(), kChildFile, MegaNode::TYPE_FILE);
        EXPECT_TRUE(childFile);

        EXPECT_EQ(API_OK, doDeleteNode(0, refreshedRemoteBase.get()))
            << "Failed to cleanup remote base folder";
    }
    removeLocalTree();

    ASSERT_EQ(MegaError::API_OK, uploadErr)
        << "Folder merge upload should succeed when destination already exists";
}

/**
 * Verify a merge upload succeeds for a deeper partially existing tree and that
 * the final remote structure contains both the pre-existing nodes and the
 * missing descendants added by the upload.
 *
 * Steps:
 * 1. Create the local tree:
 *      baseDir/
 *        B/B1/B1.txt
 *        B/B1/B11.txt
 *        B/B2/
 *        C/
 *        D/D1/D11/D11.txt
 *        D/D2/D21/
 *        D/D3/
 * 2. Create the remote base folder and pre-create only:
 *      baseDir/
 *        B/B1/
 *        C/
 *        D/D1/
 *        D/D2/
 * 3. Upload B/B1/B1.txt first so the merge runs with both existing folders and
 *    an existing file already present in the destination tree.
 * 4. Upload local baseDir to the cloud drive root.
 * 5. Assert that the upload succeeds.
 * 6. Assert that the merged remote tree contains the expected folders and
 *    files, and that the total file/folder counts match the expected final
 *    structure.
 */
TEST_F(SdkTestFolderController, MergeUploadToExistingDestination_ComplexTree)
{
    static const std::string logPre{getLogPrefix()};
    static constexpr const char* kFolderB{"B"};
    static constexpr const char* kFolderB1{"B1"};
    static constexpr const char* kFileB1{"B1.txt"};
    static constexpr const char* kFileB11{"B11.txt"};
    static constexpr const char* kFolderB2{"B2"};
    static constexpr const char* kFolderC{"C"};
    static constexpr const char* kFolderD{"D"};
    static constexpr const char* kFolderD1{"D1"};
    static constexpr const char* kFolderD11{"D11"};
    static constexpr const char* kFileD11{"D11.txt"};
    static constexpr const char* kFolderD2{"D2"};
    static constexpr const char* kFolderD21{"D21"};
    static constexpr const char* kFolderD3{"D3"};

    LOG_info << logPre << "starting";

    removeLocalTree();

    const fs::path basePath = fs::current_path() / getLocalFolderName();
    const fs::path b1Path = basePath / kFolderB / kFolderB1;
    const fs::path b2Path = basePath / kFolderB / kFolderB2;
    const fs::path cPath = basePath / kFolderC;
    const fs::path d1Path = basePath / kFolderD / kFolderD1;
    const fs::path d11Path = d1Path / kFolderD11;
    const fs::path d2Path = basePath / kFolderD / kFolderD2;
    const fs::path d21Path = d2Path / kFolderD21;
    const fs::path d3Path = basePath / kFolderD / kFolderD3;

    fs::create_directories(b1Path);
    fs::create_directories(b2Path);
    fs::create_directories(cPath);
    fs::create_directories(d11Path);
    fs::create_directories(d21Path);
    fs::create_directories(d3Path);

    const fs::path b1TxtPath = b1Path / kFileB1;
    const fs::path b11TxtPath = b1Path / kFileB11;
    const fs::path d11TxtPath = d11Path / kFileD11;

    ASSERT_TRUE(createFile(path_u8string(b1TxtPath), false));
    ASSERT_TRUE(createFile(path_u8string(b11TxtPath), false));
    ASSERT_TRUE(createFile(path_u8string(d11TxtPath), false));

    MegaHandle remoteBaseHandle =
        createFolder(0, getLocalFolderName().c_str(), getRootNode().get());
    ASSERT_NE(remoteBaseHandle, UNDEF) << "Failed to create remote base folder";

    std::unique_ptr<MegaNode> remoteBaseNode(megaApi[0]->getNodeByHandle(remoteBaseHandle));
    ASSERT_TRUE(remoteBaseNode) << "Failed to get remote base folder node";

    MegaHandle remoteBHandle = createFolder(0, kFolderB, remoteBaseNode.get());
    ASSERT_NE(remoteBHandle, UNDEF) << "Failed to create remote B folder";
    std::unique_ptr<MegaNode> remoteBNode(megaApi[0]->getNodeByHandle(remoteBHandle));
    ASSERT_TRUE(remoteBNode) << "Failed to get remote B node";

    MegaHandle remoteB1Handle = createFolder(0, kFolderB1, remoteBNode.get());
    ASSERT_NE(remoteB1Handle, UNDEF) << "Failed to create remote B1 folder";
    std::unique_ptr<MegaNode> remoteB1Node(megaApi[0]->getNodeByHandle(remoteB1Handle));
    ASSERT_TRUE(remoteB1Node) << "Failed to get remote B1 node";

    MegaHandle remoteCHandle = createFolder(0, kFolderC, remoteBaseNode.get());
    ASSERT_NE(remoteCHandle, UNDEF) << "Failed to create remote C folder";

    MegaHandle remoteDHandle = createFolder(0, kFolderD, remoteBaseNode.get());
    ASSERT_NE(remoteDHandle, UNDEF) << "Failed to create remote D folder";
    std::unique_ptr<MegaNode> remoteDNode(megaApi[0]->getNodeByHandle(remoteDHandle));
    ASSERT_TRUE(remoteDNode) << "Failed to get remote D node";

    MegaHandle remoteD1Handle = createFolder(0, kFolderD1, remoteDNode.get());
    ASSERT_NE(remoteD1Handle, UNDEF) << "Failed to create remote D1 folder";
    MegaHandle remoteD2Handle = createFolder(0, kFolderD2, remoteDNode.get());
    ASSERT_NE(remoteD2Handle, UNDEF) << "Failed to create remote D2 folder";

    MegaHandle preExistingFileHandle = INVALID_HANDLE;
    ASSERT_EQ(MegaError::API_OK,
              doStartUpload(0,
                            &preExistingFileHandle,
                            path_u8string(b1TxtPath).c_str(),
                            remoteB1Node.get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/))
        << "Failed to upload pre-existing B1.txt";
    ASSERT_NE(preExistingFileHandle, INVALID_HANDLE) << "Pre-existing file handle is invalid";

    MegaHandle uploadedHandle = INVALID_HANDLE;
    const int uploadErr = doStartUpload(0,
                                        &uploadedHandle,
                                        path_u8string(basePath).c_str(),
                                        getRootNode().get(),
                                        nullptr /*fileName*/,
                                        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                        nullptr /*appData*/,
                                        false /*isSourceTemporary*/,
                                        false /*startFirst*/,
                                        nullptr /*cancelToken*/);

    std::unique_ptr<MegaNode> refreshedRemoteBase(megaApi[0]->getNodeByHandle(remoteBaseHandle));
    if (refreshedRemoteBase)
    {
        auto bNode = getRequiredChild(megaApi[0].get(),
                                      refreshedRemoteBase.get(),
                                      kFolderB,
                                      MegaNode::TYPE_FOLDER);
        auto cNode = getRequiredChild(megaApi[0].get(),
                                      refreshedRemoteBase.get(),
                                      kFolderC,
                                      MegaNode::TYPE_FOLDER);
        auto dNode = getRequiredChild(megaApi[0].get(),
                                      refreshedRemoteBase.get(),
                                      kFolderD,
                                      MegaNode::TYPE_FOLDER);
        ASSERT_TRUE(bNode && cNode && dNode);

        auto b1Node =
            getRequiredChild(megaApi[0].get(), bNode.get(), kFolderB1, MegaNode::TYPE_FOLDER);
        auto b2Node =
            getRequiredChild(megaApi[0].get(), bNode.get(), kFolderB2, MegaNode::TYPE_FOLDER);
        ASSERT_TRUE(b1Node && b2Node);

        auto b1Txt = getRequiredChild(megaApi[0].get(), b1Node.get(), kFileB1, MegaNode::TYPE_FILE);
        auto b11Txt =
            getRequiredChild(megaApi[0].get(), b1Node.get(), kFileB11, MegaNode::TYPE_FILE);
        EXPECT_TRUE(b1Txt && b11Txt);

        auto d1Node =
            getRequiredChild(megaApi[0].get(), dNode.get(), kFolderD1, MegaNode::TYPE_FOLDER);
        auto d2Node =
            getRequiredChild(megaApi[0].get(), dNode.get(), kFolderD2, MegaNode::TYPE_FOLDER);
        auto d3Node =
            getRequiredChild(megaApi[0].get(), dNode.get(), kFolderD3, MegaNode::TYPE_FOLDER);
        ASSERT_TRUE(d1Node && d2Node && d3Node);

        auto d11Node =
            getRequiredChild(megaApi[0].get(), d1Node.get(), kFolderD11, MegaNode::TYPE_FOLDER);
        ASSERT_TRUE(d11Node);
        auto d11Txt =
            getRequiredChild(megaApi[0].get(), d11Node.get(), kFileD11, MegaNode::TYPE_FILE);
        EXPECT_TRUE(d11Txt);

        auto d21Node =
            getRequiredChild(megaApi[0].get(), d2Node.get(), kFolderD21, MegaNode::TYPE_FOLDER);
        EXPECT_TRUE(d21Node);

        std::unique_ptr<MegaNode> preservedFile(megaApi[0]->getNodeByHandle(preExistingFileHandle));
        ASSERT_TRUE(preservedFile) << "Pre-existing file should still be present after merge";
        EXPECT_STREQ(kFileB1, preservedFile->getName());

        std::unique_ptr<MegaNode> preservedFileParent(
            megaApi[0]->getParentNode(preservedFile.get()));
        ASSERT_TRUE(preservedFileParent) << "Pre-existing file should keep its parent";
        EXPECT_EQ(b1Node->getHandle(), preservedFileParent->getHandle())
            << "Pre-existing file should remain under B/B1";

        int files = 0;
        int folders = 0;
        auto countTree = [&](auto&& self, MegaNode* node, int& outFiles, int& outFolders) -> void
        {
            std::unique_ptr<MegaNodeList> children(megaApi[0]->getChildren(node));
            if (!children)
            {
                return;
            }
            for (int i = 0; i < children->size(); ++i)
            {
                MegaNode* child = children->get(i);
                if (child->getType() == MegaNode::TYPE_FOLDER)
                {
                    ++outFolders;
                    self(self, child, outFiles, outFolders);
                }
                else if (child->getType() == MegaNode::TYPE_FILE)
                {
                    ++outFiles;
                }
            }
        };
        countTree(countTree, refreshedRemoteBase.get(), files, folders);

        EXPECT_EQ(3, files) << "Unexpected number of files under remote base";
        EXPECT_EQ(10, folders) << "Unexpected number of folders under remote base";

        EXPECT_EQ(API_OK, doDeleteNode(0, refreshedRemoteBase.get()))
            << "Failed to cleanup remote base folder";
    }

    removeLocalTree();

    ASSERT_EQ(MegaError::API_OK, uploadErr)
        << "Folder merge upload should succeed for a complex tree";
}

/**
 * Verify a merge upload succeeds when more than one folder-creation batch is
 * required and an already existing branch must only be recursed after all
 * missing siblings at the first remote level have been created.
 *
 * Steps:
 * 1. Create the local tree:
 *      baseDir/
 *        1001 sibling folders named batch_0000 .. batch_1000
 *        existing_branch/late_child/late_file.txt
 * 2. Create the remote base folder and pre-create only:
 *      baseDir/
 *        existing_branch/
 * 3. Upload local baseDir to the cloud drive root. This forces the SDK to:
 *    - create more than MAXNODESUPLOAD missing siblings over multiple batches
 *    - postpone recursion into existing_branch until the root level has no
 *      missing siblings left
 * 4. Assert the upload succeeds.
 * 5. Assert the remote tree contains all expected root children and the delayed
 *    descendant existing_branch/late_child/late_file.txt.
 */
TEST_F(SdkTestFolderController, MergeUploadToExistingDestination_MultipleBatches)
{
    static const std::string logPre{getLogPrefix()};
    static constexpr const char* kExistingBranch{"existing_branch"};
    static constexpr const char* kLateChild{"late_child"};
    static constexpr const char* kLateFile{"late_file.txt"};
    static constexpr const char* kBatchPrefix{"batch_"};
    static constexpr unsigned kMissingSiblings{MAXNODESUPLOAD + 1};

    LOG_info << logPre << "starting";

    removeLocalTree();

    const fs::path basePath = fs::current_path() / getLocalFolderName();
    const fs::path existingBranchPath = basePath / kExistingBranch;
    const fs::path lateChildPath = existingBranchPath / kLateChild;

    fs::create_directories(lateChildPath);
    ASSERT_TRUE(createFile(path_u8string(lateChildPath / kLateFile), false));

    for (unsigned i = 0; i < kMissingSiblings; ++i)
    {
        const std::string siblingName = kBatchPrefix + std::to_string(i / 1000) +
                                        std::to_string((i / 100) % 10) +
                                        std::to_string((i / 10) % 10) + std::to_string(i % 10);
        fs::create_directories(basePath / siblingName);
    }

    MegaHandle remoteBaseHandle =
        createFolder(0, getLocalFolderName().c_str(), getRootNode().get());
    ASSERT_NE(remoteBaseHandle, UNDEF) << "Failed to create remote base folder";

    std::unique_ptr<MegaNode> remoteBaseNode(megaApi[0]->getNodeByHandle(remoteBaseHandle));
    ASSERT_TRUE(remoteBaseNode) << "Failed to get remote base folder node";

    MegaHandle remoteExistingHandle = createFolder(0, kExistingBranch, remoteBaseNode.get());
    ASSERT_NE(remoteExistingHandle, UNDEF) << "Failed to create remote existing branch";

    MegaHandle uploadedHandle = INVALID_HANDLE;
    const int uploadErr = doStartUpload(0,
                                        &uploadedHandle,
                                        path_u8string(basePath).c_str(),
                                        getRootNode().get(),
                                        nullptr /*fileName*/,
                                        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                        nullptr /*appData*/,
                                        false /*isSourceTemporary*/,
                                        false /*startFirst*/,
                                        nullptr /*cancelToken*/);

    std::unique_ptr<MegaNode> refreshedRemoteBase(megaApi[0]->getNodeByHandle(remoteBaseHandle));
    if (refreshedRemoteBase)
    {
        std::unique_ptr<MegaNodeList> rootChildren(
            megaApi[0]->getChildren(refreshedRemoteBase.get()));
        ASSERT_TRUE(rootChildren) << "Failed to enumerate remote base children";
        EXPECT_EQ(static_cast<int>(kMissingSiblings + 1), rootChildren->size())
            << "Unexpected number of direct children under remote base";

        auto existingBranchNode = getRequiredChild(megaApi[0].get(),
                                                   refreshedRemoteBase.get(),
                                                   kExistingBranch,
                                                   MegaNode::TYPE_FOLDER);
        ASSERT_TRUE(existingBranchNode);

        auto lateChildNode = getRequiredChild(megaApi[0].get(),
                                              existingBranchNode.get(),
                                              kLateChild,
                                              MegaNode::TYPE_FOLDER);
        ASSERT_TRUE(lateChildNode);

        auto lateFileNode =
            getRequiredChild(megaApi[0].get(), lateChildNode.get(), kLateFile, MegaNode::TYPE_FILE);
        EXPECT_TRUE(lateFileNode);

        const std::string firstSiblingName = std::string{kBatchPrefix} + "0000";
        const std::string lastSiblingName = std::string{kBatchPrefix} + "1000";
        auto firstSiblingNode = getRequiredChild(megaApi[0].get(),
                                                 refreshedRemoteBase.get(),
                                                 firstSiblingName.c_str(),
                                                 MegaNode::TYPE_FOLDER);
        auto lastSiblingNode = getRequiredChild(megaApi[0].get(),
                                                refreshedRemoteBase.get(),
                                                lastSiblingName.c_str(),
                                                MegaNode::TYPE_FOLDER);
        EXPECT_TRUE(firstSiblingNode && lastSiblingNode);

        EXPECT_EQ(API_OK, doDeleteNode(0, refreshedRemoteBase.get()))
            << "Failed to cleanup remote base folder";
    }

    removeLocalTree();

    ASSERT_EQ(MegaError::API_OK, uploadErr)
        << "Folder merge upload should succeed across multiple folder batches";
}

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
namespace
{
// Saves and restores the folder-upload test hooks, so a test cannot leak them into
// other tests that share the process-global globalMegaTestHooks.
struct FolderUploadHookGuard
{
    decltype(globalMegaTestHooks.onFolderUploadPutnodesResult) prevPutnodesResult{
        globalMegaTestHooks.onFolderUploadPutnodesResult};
    decltype(globalMegaTestHooks.onFolderUploadSimulateMissing) prevSimulateMissing{
        globalMegaTestHooks.onFolderUploadSimulateMissing};

    ~FolderUploadHookGuard()
    {
        globalMegaTestHooks.onFolderUploadPutnodesResult = prevPutnodesResult;
        globalMegaTestHooks.onFolderUploadSimulateMissing = prevSimulateMissing;
    }
};
} // namespace
#endif

/**
 * When a putnodes for a folder batch reports a per-node error (overall command still
 * API_OK), the folder upload must abort with that specific error and must not crash.
 */
TEST_F(SdkTestFolderController, PutnodesNodeErrorAbortsUpload)
{
#ifndef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED (debug test hooks)";
#else
    static const std::string logPre{getLogPrefix()};
    LOG_info << logPre << "starting";

    FolderUploadHookGuard hookGuard;
    // Inject a per-node failure on the folder-creation putnodes result.
    globalMegaTestHooks.onFolderUploadPutnodesResult = [](std::vector<NewNode>& nn)
    {
        if (!nn.empty())
        {
            nn.front().mError = API_EACCESS;
        }
    };

    createLocalTree();

    MegaHandle uploadedHandle = INVALID_HANDLE;
    const int uploadErr = doStartUpload(0,
                                        &uploadedHandle,
                                        getLocalFolderName().c_str(),
                                        getRootNode().get(),
                                        nullptr /*fileName*/,
                                        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                        nullptr /*appData*/,
                                        false /*isSourceTemporary*/,
                                        false /*startFirst*/,
                                        nullptr /*cancelToken*/);

    EXPECT_EQ(MegaError::API_EACCESS, uploadErr)
        << "Folder upload should abort with the injected per-node error";

    // The folder was really created remotely before the error was injected, clean it up.
    std::unique_ptr<MegaNode> remoteFolder(
        megaApi[0]->getChildNode(getRootNode().get(), getLocalFolderName().c_str()));
    if (remoteFolder)
    {
        EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, remoteFolder.get()));
    }
    removeLocalTree();
#endif
}

/**
 * If a folder created in an earlier batch can no longer be resolved in a later batch
 * (e.g. deleted by another session), the upload must abort with API_ENOENT and must not
 * re-send the already-moved newnode (which would crash).
 */
TEST_F(SdkTestFolderController, FolderVanishingBetweenBatchesAbortsUpload)
{
#ifndef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED (debug test hooks)";
#else
    static const std::string logPre{getLogPrefix()};
    LOG_info << logPre << "starting";

    FolderUploadHookGuard hookGuard;
    // On the first re-lookup of an already-created folder (in a later batch), pretend it
    // is missing, simulating a concurrent delete between batches.
    globalMegaTestHooks.onFolderUploadSimulateMissing =
        [triggered = false](const std::string&) mutable -> bool
    {
        if (!triggered)
        {
            triggered = true;
            return true;
        }
        return false;
    };

    createLocalTree();

    MegaHandle uploadedHandle = INVALID_HANDLE;
    const int uploadErr = doStartUpload(0,
                                        &uploadedHandle,
                                        getLocalFolderName().c_str(),
                                        getRootNode().get(),
                                        nullptr /*fileName*/,
                                        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                        nullptr /*appData*/,
                                        false /*isSourceTemporary*/,
                                        false /*startFirst*/,
                                        nullptr /*cancelToken*/);

    EXPECT_EQ(MegaError::API_ENOENT, uploadErr)
        << "Folder upload should abort with API_ENOENT when a created folder vanishes";

    // The folder was really created remotely in the first batch; clean it up.
    std::unique_ptr<MegaNode> remoteFolder(
        megaApi[0]->getChildNode(getRootNode().get(), getLocalFolderName().c_str()));
    if (remoteFolder)
    {
        EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, remoteFolder.get()));
    }
    removeLocalTree();
#endif
}
