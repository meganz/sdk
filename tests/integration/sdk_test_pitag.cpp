#include "env_var_accounts.h"
#include "integration_test_utils.h"
#include "mega/testhooks.h"
#include "passwordManager/SdkTestPasswordManager.h"
#include "sdk_test_utils.h"
#include "SdkTest_test.h"

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <type_traits>

namespace
{
void createLocalTree(const fs::path& parentPath, const std::vector<sdk_test::NodeInfo>& nodes);

void createLocalEntry(const fs::path& parentPath, const sdk_test::NodeInfo& node)
{
    std::visit(
        [&](const auto& nodeInfo)
        {
            using NodeInfoType = std::decay_t<decltype(nodeInfo)>;
            if constexpr (std::is_same_v<NodeInfoType, sdk_test::FileNodeInfo>)
            {
                const fs::path filePath = parentPath / nodeInfo.name;
                const auto fileSize = nodeInfo.size > 0 ? nodeInfo.size : 1u;
                ASSERT_NO_THROW(sdk_test::createFile(filePath, fileSize));
            }
            else
            {
                const fs::path dirPath = parentPath / nodeInfo.name;
                ASSERT_TRUE(fs::create_directory(dirPath))
                    << "Unable to create directory " << dirPath.string();
                createLocalTree(dirPath, nodeInfo.childs);
            }
        },
        node);
}

void createLocalTree(const fs::path& parentPath, const std::vector<sdk_test::NodeInfo>& nodes)
{
    for (const auto& node: nodes)
    {
        createLocalEntry(parentPath, node);
    }
}

class PitagCommandObserver
{
public:
    PitagCommandObserver():
        mPreviousHook(globalMegaTestHooks.onHttpReqPost)
    {
        globalMegaTestHooks.onHttpReqPost = [this](HttpReq* req)
        {
            handleRequest(req);
            return false;
        };
    }

    ~PitagCommandObserver()
    {
        globalMegaTestHooks.onHttpReqPost = mPreviousHook;
    }

    bool waitForValue(const std::string& expected, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mMutex);
        if (!mCv.wait_for(lock,
                          timeout,
                          [&]
                          {
                              return mCaptured;
                          }))
        {
            return false;
        }
        return mLastValue == expected;
    }

    std::string capturedValue() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mLastValue;
    }

private:
    void handleRequest(HttpReq* req)
    {
        if (!req || !req->out)
        {
            return;
        }

        const std::string& payload = *req->out;
        const std::string commandToken = "\"a\":\"p\"";
        const std::string pitagToken = "\"p\":\"";

        const auto commandPos = payload.find(commandToken);
        if (commandPos == std::string::npos)
        {
            return;
        }

        auto pitagPos = payload.find(pitagToken, commandPos);
        if (pitagPos == std::string::npos)
        {
            return;
        }
        pitagPos += pitagToken.size();

        const auto endPos = payload.find('"', pitagPos);
        if (endPos == std::string::npos)
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (mCaptured)
            {
                return;
            }
            mCaptured = true;
            mLastValue = payload.substr(pitagPos, endPos - pitagPos);
        }
        mCv.notify_all();
    }

    std::function<bool(HttpReq*)> mPreviousHook;
    mutable std::mutex mMutex;
    std::condition_variable mCv;
    bool mCaptured{false};
    std::string mLastValue;
};

class SdkTestPitag: public SdkTest
{};

class SdkTestPitagPasswordManager: public SdkTestPasswordManager
{};

} // anonymous namespace

TEST_F(SdkTestPitag, PitagCapturedForRegularUpload)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const auto localFilePath = fs::current_path() / (getFilePrefix() + "pitag_regular.bin");
    const std::string remoteName = path_u8string(localFilePath.filename());
    const std::string localPathUtf8 = path_u8string(localFilePath);
    const sdk_test::LocalTempFile localFile(localFilePath, "pitag-regular-upload");

    std::unique_ptr<MegaNode> rootNode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootNode) << "Unable to get root node";

    PitagCommandObserver observer;
    TransferTracker tracker(megaApi[0].get());
    MegaUploadOptions options;
    options.fileName = remoteName;
    options.mtime = MegaApi::INVALID_CUSTOM_MOD_TIME;
    options.pitagTrigger = MegaApi::PITAG_TRIGGER_CAMERA;

    megaApi[0]->startUpload(localPathUtf8, rootNode.get(), nullptr, &options, &tracker);

    ASSERT_EQ(API_OK, tracker.waitForResult());

    const auto waitTimeout =
        std::chrono::duration_cast<std::chrono::milliseconds>(sdk_test::MAX_TIMEOUT);
    const std::string expected = std::string{"U"} + options.pitagTrigger + "fD.";
    ASSERT_TRUE(observer.waitForValue(expected, waitTimeout))
        << "Unexpected pitag payload captured: " << observer.capturedValue();
}

TEST_F(SdkTestPitag, PitagCapturedForCreateFolder)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::unique_ptr<MegaNode> rootNode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootNode) << "Unable to get root node";

    PitagCommandObserver observer;

    createFolder(0, "Folder", rootNode.get());

    const auto waitTimeout =
        std::chrono::duration_cast<std::chrono::milliseconds>(sdk_test::MAX_TIMEOUT);
    ASSERT_TRUE(observer.waitForValue("F.FD.", waitTimeout))
        << "Unexpected pitag payload captured: " << observer.capturedValue();
}

TEST_F(SdkTestPitag, PitagCapturedForUploadWithFolderController)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::unique_ptr<MegaNode> rootNode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootNode) << "Unable to get root node";

    PitagCommandObserver observer;

    createFolder(0, "Folder", rootNode.get());

    const auto waitTimeout =
        std::chrono::duration_cast<std::chrono::milliseconds>(sdk_test::MAX_TIMEOUT);
    ASSERT_TRUE(observer.waitForValue("F.FD.", waitTimeout))
        << "Unexpected pitag payload captured: " << observer.capturedValue();
}

TEST_F(SdkTestPitag, PitagCapturedForBatchFolderUpload)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::unique_ptr<MegaNode> rootNode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootNode) << "Unable to get root node";

    const std::string localFolderName = getFilePrefix() + "pitag_batch_folder";
    const fs::path localFolderPath = fs::current_path() / localFolderName;
    sdk_test::LocalTempDir localFolder(localFolderPath);

    const std::vector<sdk_test::NodeInfo> localStructure{
        sdk_test::DirNodeInfo("nested")
            .addChild(sdk_test::DirNodeInfo("inner").addChild(
                sdk_test::FileNodeInfo("inner_file.bin").setSize(8)))
            .addChild(sdk_test::FileNodeInfo("nested_file.bin").setSize(12)),
        sdk_test::FileNodeInfo("root_file_a.bin").setSize(10),
        sdk_test::FileNodeInfo("root_file_b.bin").setSize(14)};
    ASSERT_NO_FATAL_FAILURE(createLocalTree(localFolderPath, localStructure));

    PitagCommandObserver observer;
    TransferTracker tracker(megaApi[0].get());
    MegaUploadOptions folderOptions;
    folderOptions.fileName = localFolderName;
    folderOptions.mtime = MegaApi::INVALID_CUSTOM_MOD_TIME;

    megaApi[0]->startUpload(localFolderPath.string(),
                            rootNode.get(),
                            nullptr,
                            &folderOptions,
                            &tracker);
    ASSERT_EQ(API_OK, tracker.waitForResult());

    const auto waitTimeout =
        std::chrono::duration_cast<std::chrono::milliseconds>(sdk_test::MAX_TIMEOUT);
    ASSERT_TRUE(observer.waitForValue("U.FD.", waitTimeout))
        << "Unexpected pitag payload captured: " << observer.capturedValue();
}

TEST_F(SdkTestPitag, PitagCapturedForIncomingShareUpload)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    std::unique_ptr<MegaNode> ownerRoot{megaApi[0]->getRootNode()};
    ASSERT_TRUE(ownerRoot) << "Unable to get root node for owner account";

    inviteTestAccount(0, 1, "Hi!!");

    const std::string folderName = getFilePrefix() + "incomingShare";
    RequestTracker folderTracker(megaApi[0].get());
    megaApi[0]->createFolder(folderName.c_str(), ownerRoot.get(), &folderTracker);
    ASSERT_EQ(API_OK, folderTracker.waitForResult()) << "Failed to create folder for sharing";

    std::unique_ptr<MegaNode> folderNode{
        megaApi[0]->getNodeByHandle(folderTracker.request->getNodeHandle())};
    ASSERT_TRUE(folderNode) << "Unable to obtain shared folder node";

    ASSERT_NO_FATAL_FAILURE(
        shareFolder(folderNode.get(), mApi[1].email.c_str(), MegaShare::ACCESS_FULL));

    auto inShareAvailable = [this]()
    {
        std::unique_ptr<MegaShareList> shares{megaApi[1]->getInSharesList()};
        return shares && shares->size() > 0;
    };
    ASSERT_TRUE(WaitFor(inShareAvailable, defaultTimeoutMs))
        << "Incoming share not received by sharee";

    const MegaHandle sharedHandle = folderNode->getHandle();
    ASSERT_TRUE(WaitFor(
        [this, sharedHandle, &folderName]()
        {
            std::unique_ptr<MegaNode> node{megaApi[1]->getNodeByHandle(sharedHandle)};
            return node && node->isNodeKeyDecrypted() && node->getName() &&
                   folderName == node->getName();
        },
        defaultTimeoutMs))
        << "Incoming share not decrypted by sharee";

    int stableReadyChecks = 0;
    ASSERT_TRUE(WaitFor(
        [this, sharedHandle, &folderName, &stableReadyChecks]()
        {
            std::unique_ptr<MegaNode> node{megaApi[1]->getNodeByHandle(sharedHandle)};
            const bool ready = node && node->isNodeKeyDecrypted() && node->getName() &&
                               folderName == node->getName();
            stableReadyChecks = ready ? stableReadyChecks + 1 : 0;
            return stableReadyChecks >= 5;
        },
        defaultTimeoutMs * 2))
        << "Incoming share node state did not stabilize before copy";

    std::unique_ptr<MegaNode> incomingNode{megaApi[1]->getNodeByHandle(sharedHandle)};
    ASSERT_TRUE(incomingNode) << "Sharee cannot access incoming share node";
    ASSERT_STREQ(folderName.c_str(), incomingNode->getName());

    const auto localFilePath = fs::current_path() / (getFilePrefix() + "pitag_inshare.bin");
    const std::string localPathUtf8 = path_u8string(localFilePath);
    const sdk_test::LocalTempFile localFile(localFilePath, "pitag-inshare-upload");

    PitagCommandObserver observer;
    TransferTracker tracker(megaApi[1].get());
    MegaUploadOptions shareOptions;
    shareOptions.mtime = MegaApi::INVALID_CUSTOM_MOD_TIME;
    shareOptions.pitagTrigger = MegaApi::PITAG_TRIGGER_SCANNER;

    megaApi[1]->startUpload(localPathUtf8, incomingNode.get(), nullptr, &shareOptions, &tracker);
    ASSERT_EQ(API_OK, tracker.waitForResult());

    constexpr auto timeout = 3s; // short timeout, it has to be available
    const auto waitTimeout = std::chrono::duration_cast<std::chrono::milliseconds>(timeout);
    const std::string expected = std::string{"U"} + shareOptions.pitagTrigger + "fi.";
    ASSERT_TRUE(observer.waitForValue(expected, waitTimeout))
        << "Unexpected pitag payload captured: " << observer.capturedValue();
}

TEST_F(SdkTestPitag, PitagCapturedForBackgroundMediaUpload)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const auto sourcePath = fs::current_path() / (getFilePrefix() + "pitag_background_upload.bin");
    const std::string encryptedPath =
        path_u8string(sourcePath) + ".enc"; // encryptFile output destination
    const std::string fileOutput = getFilePrefix() + "pitag_background_remote.bin";

    // Create input file to upload through the background media upload pipeline
    size_t size = 1024;
    const sdk_test::LocalTempFile localFile(sourcePath, size);
    const int64_t fileSize = static_cast<int64_t>(fs::file_size(sourcePath));

    PitagCommandObserver observer;

    synchronousMediaUpload(/*apiIndex*/ 0,
                           fileSize,
                           path_u8string(sourcePath).c_str(),
                           encryptedPath.c_str(),
                           fileOutput.c_str());

    const auto waitTimeout =
        std::chrono::duration_cast<std::chrono::milliseconds>(sdk_test::MAX_TIMEOUT);
    const std::string expected = std::string{"U"} + MegaApi::PITAG_TRIGGER_CAMERA + "fD.";
    ASSERT_TRUE(observer.waitForValue(expected, waitTimeout))
        << "Unexpected pitag payload captured: " << observer.capturedValue();
}

TEST_F(SdkTestPitag, PitagCapturedForChatTargetUpload)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const auto localFilePath = fs::current_path() / (getFilePrefix() + "pitag_chat_target.bin");
    const std::string remoteName = localFilePath.filename().string();
    const std::string localPathUtf8 = localFilePath.string();
    const sdk_test::LocalTempFile localFile(localFilePath, "pitag-chat-target-upload");

    std::unique_ptr<MegaNode> rootNode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootNode) << "Unable to get root node";

    PitagCommandObserver observer;
    TransferTracker tracker(megaApi[0].get());
    MegaUploadOptions options;
    options.fileName = remoteName;
    options.mtime = MegaApi::INVALID_CUSTOM_MOD_TIME;
    options.pitagTrigger = MegaApi::PITAG_TRIGGER_CAMERA;
    options.pitagTarget = MegaApi::PITAG_TARGET_CHAT_1TO1;

    megaApi[0]->startUpload(localPathUtf8, rootNode.get(), nullptr, &options, &tracker);
    ASSERT_EQ(API_OK, tracker.waitForResult());

    const auto waitTimeout =
        std::chrono::duration_cast<std::chrono::milliseconds>(sdk_test::MAX_TIMEOUT);
    std::string expected;
    expected.push_back('U');
    expected.push_back(options.pitagTrigger);
    expected.push_back('f');
    expected.push_back(options.pitagTarget);
    expected.push_back('.');

    ASSERT_TRUE(observer.waitForValue(expected, waitTimeout))
        << "Unexpected pitag payload captured: " << observer.capturedValue();
}

TEST_F(SdkTestPitag, PitagCapturedForCopyNodeFromCloudDrive)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::unique_ptr<MegaNode> rootNode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootNode) << "Unable to get root node";

    const std::string srcName = getFilePrefix() + "pitag_copy_src";
    RequestTracker createTracker(megaApi[0].get());
    megaApi[0]->createFolder(srcName.c_str(), rootNode.get(), &createTracker);
    ASSERT_EQ(API_OK, createTracker.waitForResult());

    std::unique_ptr<MegaNode> srcNode{
        megaApi[0]->getNodeByHandle(createTracker.request->getNodeHandle())};
    ASSERT_TRUE(srcNode) << "Unable to get source folder";

    PitagCommandObserver observer;
    RequestTracker copyTracker(megaApi[0].get());
    const std::string dstName = getFilePrefix() + "pitag_copy_dst";
    megaApi[0]->copyNode(srcNode.get(), rootNode.get(), dstName.c_str(), &copyTracker);
    ASSERT_EQ(API_OK, copyTracker.waitForResult());

    const auto waitTimeout =
        std::chrono::duration_cast<std::chrono::milliseconds>(sdk_test::MAX_TIMEOUT);
    ASSERT_TRUE(observer.waitForValue("C.FDD", waitTimeout))
        << "Unexpected pitag payload captured: " << observer.capturedValue();
}

TEST_F(SdkTestPitag, PitagCapturedForCopyNodeFromIncomingShare)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    std::unique_ptr<MegaNode> ownerRoot{megaApi[0]->getRootNode()};
    ASSERT_TRUE(ownerRoot) << "Unable to get root node for owner account";

    inviteTestAccount(0, 1, "Hi!!");

    const std::string folderName = getFilePrefix() + "pitag_copy_inshare_src";
    RequestTracker folderTracker(megaApi[0].get());
    megaApi[0]->createFolder(folderName.c_str(), ownerRoot.get(), &folderTracker);
    ASSERT_EQ(API_OK, folderTracker.waitForResult()) << "Failed to create folder for sharing";

    std::unique_ptr<MegaNode> folderNode{
        megaApi[0]->getNodeByHandle(folderTracker.request->getNodeHandle())};
    ASSERT_TRUE(folderNode) << "Unable to obtain shared folder node";

    ASSERT_NO_FATAL_FAILURE(
        shareFolder(folderNode.get(), mApi[1].email.c_str(), MegaShare::ACCESS_FULL));

    auto inShareAvailable = [this]()
    {
        std::unique_ptr<MegaShareList> shares{megaApi[1]->getInSharesList()};
        return shares && shares->size() > 0;
    };
    ASSERT_TRUE(WaitFor(inShareAvailable, defaultTimeoutMs))
        << "Incoming share not received by sharee";

    const MegaHandle sharedHandle = folderNode->getHandle();
    ASSERT_TRUE(WaitFor(
        [this, sharedHandle, &folderName]()
        {
            std::unique_ptr<MegaNode> node{megaApi[1]->getNodeByHandle(sharedHandle)};
            return node && node->isNodeKeyDecrypted() && node->getName() &&
                   folderName == node->getName();
        },
        60 * 1000))
        << "Incoming share node not ready";

    int stableReadyChecks = 0;
    ASSERT_TRUE(WaitFor(
        [this, sharedHandle, &folderName, &stableReadyChecks]()
        {
            std::unique_ptr<MegaNode> node{megaApi[1]->getNodeByHandle(sharedHandle)};
            const bool ready = node && node->isNodeKeyDecrypted() && node->getName() &&
                               folderName == node->getName();
            stableReadyChecks = ready ? stableReadyChecks + 1 : 0;
            return stableReadyChecks >= 5;
        },
        60 * 1000))
        << "Incoming share node state did not stabilize before copy";

    std::unique_ptr<MegaNode> incomingNode{megaApi[1]->getNodeByHandle(sharedHandle)};
    ASSERT_TRUE(incomingNode) << "Sharee cannot access incoming share node";
    ASSERT_STREQ(folderName.c_str(), incomingNode->getName());

    std::unique_ptr<MegaNode> shareeRoot{megaApi[1]->getRootNode()};
    ASSERT_TRUE(shareeRoot) << "Unable to get sharee root node";

    PitagCommandObserver observer;
    RequestTracker copyTracker(megaApi[1].get());
    const std::string dstName = getFilePrefix() + "pitag_copy_from_inshare_dst";
    megaApi[1]->copyNode(incomingNode.get(), shareeRoot.get(), dstName.c_str(), &copyTracker);
    ASSERT_EQ(API_OK, copyTracker.waitForResult());

    const auto waitTimeout =
        std::chrono::duration_cast<std::chrono::milliseconds>(sdk_test::MAX_TIMEOUT);
    ASSERT_TRUE(observer.waitForValue("C.FDi", waitTimeout))
        << "Unexpected pitag payload captured: " << observer.capturedValue();
}

TEST_F(SdkTestPitag, PitagCapturedForRemoteCopyUploadDedup)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::unique_ptr<MegaNode> rootNode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootNode) << "Unable to get root node";

    const auto localFilePath = fs::current_path() / (getFilePrefix() + "pitag_remote_copy.bin");
    const std::string localPathUtf8 = path_u8string(localFilePath);
    const sdk_test::LocalTempFile localFile(localFilePath, "pitag-remote-copy");

    // Initial upload to create the deduplication source in Cloud Drive
    {
        TransferTracker tracker(megaApi[0].get());
        MegaUploadOptions options;
        options.fileName = localFilePath.filename().string();
        options.mtime = MegaApi::INVALID_CUSTOM_MOD_TIME;
        megaApi[0]->startUpload(localPathUtf8, rootNode.get(), nullptr, &options, &tracker);
        ASSERT_EQ(API_OK, tracker.waitForResult());
    }

    PitagCommandObserver observer;

    TransferTracker tracker(megaApi[0].get());
    MegaUploadOptions options;
    options.fileName = getFilePrefix() + "pitag_remote_copy_dest.bin";
    options.mtime = MegaApi::INVALID_CUSTOM_MOD_TIME;
    options.pitagTrigger = MegaApi::PITAG_TRIGGER_CAMERA;

    megaApi[0]->startUpload(localPathUtf8, rootNode.get(), nullptr, &options, &tracker);
    ASSERT_EQ(API_OK, tracker.waitForResult());

    const auto waitTimeout =
        std::chrono::duration_cast<std::chrono::milliseconds>(sdk_test::MAX_TIMEOUT);
    const std::string expected = std::string{"c"} + options.pitagTrigger + "fD.";
    ASSERT_TRUE(observer.waitForValue(expected, waitTimeout))
        << "Unexpected pitag payload captured: " << observer.capturedValue();
}

TEST_F(SdkTestPitagPasswordManager, PitagCapturedForPasswordNodeCreation)
{
    PitagCommandObserver observer;

    std::unique_ptr<MegaNode::PasswordNodeData> pwdData{
        MegaNode::PasswordNodeData::createInstance("pwd", "notes", "url", "user", nullptr)};
    const auto pwdHandle = sdk_test::createPasswordNode(mApi,
                                                        getFilePrefix() + "pitag_pwd",
                                                        pwdData.get(),
                                                        getBaseHandle());
    ASSERT_NE(pwdHandle, UNDEF) << "Password node was not created";

    const auto waitTimeout =
        std::chrono::duration_cast<std::chrono::milliseconds>(SdkTestPasswordManager::MAX_TIMEOUT);
    ASSERT_TRUE(observer.waitForValue("P..D.", waitTimeout))
        << "Unexpected pitag payload captured: " << observer.capturedValue();
}

TEST_F(SdkTestPitagPasswordManager, PitagCapturedForPasswordFolderCreation)
{
    PitagCommandObserver observer;

    const std::string folderName = getFilePrefix() + "pitag_pwm_folder";
    MegaHandle folderHandle = createFolder(0, folderName.c_str(), getBaseNode().get());
    ASSERT_NE(folderHandle, UNDEF) << "Password manager folder was not created";

    const auto waitTimeout =
        std::chrono::duration_cast<std::chrono::milliseconds>(SdkTestPasswordManager::MAX_TIMEOUT);
    ASSERT_TRUE(observer.waitForValue("P.FD.", waitTimeout))
        << "Unexpected pitag payload captured: " << observer.capturedValue();
}

TEST_F(SdkTestPitag, PitagCapturedForImportFileLink)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::unique_ptr<MegaNode> rootNode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootNode);

    RequestTracker folderTracker(megaApi[0].get());
    const std::string targetFolderName = getFilePrefix() + "pitag_import_target";
    megaApi[0]->createFolder(targetFolderName.c_str(), rootNode.get(), &folderTracker);
    ASSERT_EQ(API_OK, folderTracker.waitForResult());
    std::unique_ptr<MegaNode> importParent{
        megaApi[0]->getNodeByHandle(folderTracker.request->getNodeHandle())};
    ASSERT_TRUE(importParent);

    const auto localFilePath = fs::current_path() / (getFilePrefix() + "pitag_import.bin");
    const sdk_test::LocalTempFile localFile(localFilePath, "pitag-import");
    const std::string localPathUtf8 = path_u8string(localFilePath);

    {
        TransferTracker tracker(megaApi[0].get());
        megaApi[0]->startUpload(localPathUtf8, rootNode.get(), nullptr, nullptr, &tracker);
        ASSERT_EQ(API_OK, tracker.waitForResult());
    }

    std::unique_ptr<MegaNode> uploaded{
        megaApi[0]->getNodeByPath((std::string("/") + localFilePath.filename().string()).c_str())};
    ASSERT_TRUE(uploaded);

    RequestTracker linkTracker(megaApi[0].get());
    megaApi[0]->exportNode(uploaded.get(), 0, false, false, &linkTracker);
    ASSERT_EQ(API_OK, linkTracker.waitForResult());
    const char* link = linkTracker.request->getLink();
    ASSERT_TRUE(link);

    PitagCommandObserver observer;

    RequestTracker importTracker(megaApi[0].get());
    megaApi[0]->importFileLink(link, importParent.get(), &importTracker);
    ASSERT_EQ(API_OK, importTracker.waitForResult());

    const auto waitTimeout =
        std::chrono::duration_cast<std::chrono::milliseconds>(sdk_test::MAX_TIMEOUT);
    ASSERT_TRUE(observer.waitForValue("I.fDf", waitTimeout))
        << "Unexpected pitag payload captured: " << observer.capturedValue();
}

TEST_F(SdkTestPitag, PitagCapturedForImportFolderLink)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    // Account 0 uploads a file inside a folder, exports the folder link
    std::unique_ptr<MegaNode> ownerRoot{megaApi[0]->getRootNode()};
    ASSERT_TRUE(ownerRoot);

    const std::string folderName = getFilePrefix() + "pitag_import_folder";
    const MegaHandle folderHandle = createFolder(0, folderName.c_str(), ownerRoot.get());
    ASSERT_NE(folderHandle, UNDEF);
    std::unique_ptr<MegaNode> folder{megaApi[0]->getNodeByHandle(folderHandle)};
    ASSERT_TRUE(folder);

    const auto localFilePath =
        fs::current_path() / (getFilePrefix() + "pitag_import_folder_file.bin");
    const sdk_test::LocalTempFile localFile(localFilePath, "pitag-import-folder");
    {
        TransferTracker tracker(megaApi[0].get());
        megaApi[0]->startUpload(localFilePath.string(), folder.get(), nullptr, nullptr, &tracker);
        ASSERT_EQ(API_OK, tracker.waitForResult());
    }

    RequestTracker linkTracker(megaApi[0].get());
    megaApi[0]->exportNode(folder.get(),
                           0 /*expireTime*/,
                           false /*writable*/,
                           false /*megaHosted*/,
                           &linkTracker);
    ASSERT_EQ(API_OK, linkTracker.waitForResult());
    const char* folderLink = linkTracker.request->getLink();
    ASSERT_TRUE(folderLink);

    // Account 1 logs into the folder link (guest), authorizes the folder, then logs back to import
    RequestTracker loginLinkTracker(megaApi[1].get());
    megaApi[1]->loginToFolder(folderLink, &loginLinkTracker);
    ASSERT_EQ(API_OK, loginLinkTracker.waitForResult());
    ASSERT_NO_FATAL_FAILURE(fetchnodes(1));

    std::unique_ptr<MegaNode> linkRoot{megaApi[1]->getRootNode()};
    ASSERT_TRUE(linkRoot);
    std::unique_ptr<MegaNode> authorized{megaApi[1]->authorizeNode(linkRoot.get())};
    ASSERT_TRUE(authorized);

    RequestTracker logoutLink(megaApi[1].get());
    megaApi[1]->logout(false, &logoutLink);
    ASSERT_EQ(API_OK, logoutLink.waitForResult());

    ASSERT_NO_FATAL_FAILURE(login(1));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(1));

    std::unique_ptr<MegaNode> destRoot{megaApi[1]->getRootNode()};
    ASSERT_TRUE(destRoot);

    PitagCommandObserver observer;
    RequestTracker copyTracker(megaApi[1].get());
    megaApi[1]->copyNode(authorized.get(), destRoot.get(), nullptr, &copyTracker);
    ASSERT_EQ(API_OK, copyTracker.waitForResult());

    const auto waitTimeout =
        std::chrono::duration_cast<std::chrono::milliseconds>(sdk_test::MAX_TIMEOUT);
    ASSERT_TRUE(observer.waitForValue("I.FDF", waitTimeout))
        << "Unexpected pitag payload captured: " << observer.capturedValue();
}

#endif // MEGASDK_DEBUG_TEST_HOOKS_ENABLED
