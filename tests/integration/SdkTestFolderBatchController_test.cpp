/**
 * @file SdkTestFolderBatchController_test.cpp
 * @brief Tests for MegaApi::startDownloadBatch, the batch download controller.
 *
 * A batch is one MegaTransfer over several roots, each carrying its own destination and per-item
 * options, so what is tested here is what a single-root download cannot express: several roots in
 * one transfer, per-item destinations and app data, one cancel token over the lot, and the
 * transfer-quota prediction reported before any file starts.
 */

#include "api_request_hook.h"
#include "integration_test_utils.h"
#include "mega/testhooks.h"
#include "mock_listeners.h"
#include "SdkTest_test.h"

#include <algorithm>
#include <map>
#include <set>

using namespace testing;

class SdkTestFolderBatchController: public SdkTest
{
public:
    void SetUp() override
    {
        SdkTest::SetUp();
        ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
        rootNode.reset(megaApi[0]->getRootNode());
        ASSERT_TRUE(rootNode);
    }

    const unique_ptr<MegaNode>& getRootNode() const
    {
        return rootNode;
    }

    const std::string& getLocalFolderName() const
    {
        return localFolderName;
    }

    static void removeLocalPath(const fs::path& path)
    {
        EXPECT_TRUE(sdk_test::removeLocalTree(path)) << "Could not remove " << path_u8string(path);
    }

    void removeLocalTree()
    {
        removeLocalPath(localFolderPath);
    }

    fs::path localTreeRoot() const
    {
        return fs::current_path() / localFolderName;
    }

    MegaHandle uploadLocalTree(const fs::path& basePath)
    {
        MegaHandle remoteFolderHandle = INVALID_HANDLE;
        EXPECT_EQ(MegaError::API_OK,
                  doStartUpload(0,
                                &remoteFolderHandle,
                                path_u8string(basePath).c_str(),
                                getRootNode().get(),
                                nullptr /*fileName*/,
                                ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                nullptr /*appData*/,
                                false /*isSourceTemporary*/,
                                false /*startFirst*/,
                                nullptr /*cancelToken*/))
            << "Failed to upload the folder tree";
        return remoteFolderHandle;
    }

    // A standalone file uploaded to the account root, for a batch's file items.
    std::unique_ptr<MegaNode> uploadLooseFile(const std::string& name,
                                              const std::string& content = "test ")
    {
        const fs::path filePath = fs::current_path() / name;
        std::error_code ignoredEc;
        fs::remove(filePath, ignoredEc);
        EXPECT_TRUE(createFile(path_u8string(filePath), false, content));

        MegaHandle handle = INVALID_HANDLE;
        EXPECT_EQ(MegaError::API_OK,
                  doStartUpload(0,
                                &handle,
                                path_u8string(filePath).c_str(),
                                getRootNode().get(),
                                nullptr /*fileName*/,
                                ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                nullptr /*appData*/,
                                false /*isSourceTemporary*/,
                                false /*startFirst*/,
                                nullptr /*cancelToken*/))
            << "Failed to upload " << name;
        fs::remove(filePath, ignoredEc);
        return std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(handle)};
    }

    // Everything the batch's sub-transfers report. Sampled on the SDK thread while the assertions
    // run on the test thread, so the members are guarded.
    struct BatchObserver
    {
        std::mutex mutex;
        std::map<std::string, std::string> subTransferAppData; // file name -> appData, "" when none
        // Read by the batch listener when the quota state is reported, so not under the mutex.
        std::atomic<int> subTransferStarts{0};
        int subTransfersFailed{0};
        int subTransfersSkipped{0};
        int subTransfersDownloaded{0};
        // Two roots can hold the same leaf name, which one map entry would swallow: a test deriving
        // a count from subTransferAppData must know that happened.
        int subTransferNameCollisions{0};

        // Between the runs of a test that downloads the same batch twice.
        void reset()
        {
            std::lock_guard<std::mutex> g{mutex};
            subTransferAppData.clear();
            subTransferStarts = 0;
            subTransfersFailed = 0;
            subTransfersSkipped = 0;
            subTransfersDownloaded = 0;
            subTransferNameCollisions = 0;
        }
    };

    // Per-file callbacks only reach listeners registered with addListener, never the batch's own.
    void observeSubTransfers(NiceMock<MockTransferListener>& listener, BatchObserver& obs)
    {
        EXPECT_CALL(listener, onTransferStart)
            .WillRepeatedly(
                [&obs](MegaApi*, MegaTransfer* t)
                {
                    if (!sdk_test::isDownloadSubTransfer(*t))
                    {
                        return;
                    }
                    const char* appData = t->getAppData();
                    const char* fileName = t->getFileName();
                    ++obs.subTransferStarts;
                    std::lock_guard<std::mutex> g{obs.mutex};
                    const std::string key = fileName ? fileName : "";
                    if (obs.subTransferAppData.count(key))
                    {
                        ++obs.subTransferNameCollisions;
                    }
                    obs.subTransferAppData[key] = appData ? appData : "";
                });
        EXPECT_CALL(listener, onTransferFinish)
            .WillRepeatedly(
                [&obs](MegaApi*, MegaTransfer* t, MegaError* e)
                {
                    if (!sdk_test::isDownloadSubTransfer(*t))
                    {
                        return;
                    }
                    std::lock_guard<std::mutex> g{obs.mutex};
                    if (e->getErrorCode() != MegaError::API_OK)
                    {
                        ++obs.subTransfersFailed;
                    }
                    else if (sdk_test::isSkippedDownload(*t))
                    {
                        ++obs.subTransfersSkipped;
                    }
                    else
                    {
                        ++obs.subTransfersDownloaded;
                    }
                });
        megaApi[0]->addListener(&listener);
    }

    // The batch's own listener. MockMegaTransferListener does not mock onTransferQuotaState, and
    // that callback reaches only the listener passed to startDownloadBatch.
    class BatchListener: public ::mega::MegaTransferListener
    {
    public:
        explicit BatchListener(MegaApi* api):
            mApi(api)
        {}

        // A failed waitForFinish() returns from the test body with the batch still running, and the
        // SDK thread would then call back into this freed stack object.
        ~BatchListener() override
        {
            if (!mFinishSignalled)
            {
                mApi->removeTransferListener(this);
            }
        }

        void onTransferStart(MegaApi*, MegaTransfer* t) override
        {
            std::lock_guard<std::mutex> g{mMutex};
            ++mStarts;
            mStartCarriedAppData = mStartCarriedAppData || t->getAppData() != nullptr;
        }

        void onTransferQuotaState(MegaApi*, MegaTransfer*, int state) override
        {
            std::lock_guard<std::mutex> g{mMutex};
            ++mQuotaStateReports;
            mQuotaState = state;
            if (mSubTransferStartsAtQuotaState)
            {
                mSubTransferStartsWhenReported = mSubTransferStartsAtQuotaState->load();
            }
        }

        void onTransferTemporaryError(MegaApi*, MegaTransfer* t, MegaError* e) override
        {
            std::lock_guard<std::mutex> g{mMutex};
            ++mTemporaryErrors;
            mTemporaryError = e->getErrorCode();
            // The parent must carry the failure itself, not just relay the callback.
            const MegaError* onTransfer = t->getLastErrorExtended();
            mTemporaryErrorOnTransfer = onTransfer ? onTransfer->getErrorCode() : MegaError::API_OK;
            mTemporaryErrorState = t->getState();
        }

        void onTransferFinish(MegaApi*, MegaTransfer* t, MegaError* e) override
        {
            {
                std::lock_guard<std::mutex> g{mMutex};
                ++mFinishes;
                mError = e->getErrorCode();
                mFinishState = t->getState();
                mFinishUserStatus = e->getUserStatus();
                mFinishLinkStatus = e->getLinkStatus();
                // The callback's transfer dies with it, and retryTransfer needs one afterwards
                mFinishedTransfer.reset(t->copy());
            }
            // Settled once: a second set_value() would throw out of an SDK callback and take the
            // whole binary down instead of failing this test.
            if (!mFinishSignalled.exchange(true))
            {
                mFinishedPromise.set_value();
            }
        }

        bool waitForFinish(std::chrono::milliseconds timeout)
        {
            return mFinished.wait_for(timeout) == std::future_status::ready;
        }

        // Sampled when the quota state is reported, to prove it arrived before any file started.
        // The counter must outlive this listener; declare the observer before it.
        void watchSubTransferStarts(const std::atomic<int>& counter)
        {
            std::lock_guard<std::mutex> g{mMutex};
            mSubTransferStartsAtQuotaState = &counter;
        }

        std::mutex mMutex;
        int mStarts{0};
        int mFinishes{0};
        int mError{-1};
        bool mStartCarriedAppData{false};
        int mQuotaStateReports{0};
        int mQuotaState{-1};
        int mSubTransferStartsWhenReported{-1};
        int mFinishState{-1};
        long long mFinishUserStatus{-1};
        long long mFinishLinkStatus{-1};
        int mTemporaryErrors{0};
        int mTemporaryError{-1};
        int mTemporaryErrorOnTransfer{-1};
        int mTemporaryErrorState{-1};
        std::unique_ptr<MegaTransfer> mFinishedTransfer;

    private:
        MegaApi* mApi;
        std::promise<void> mFinishedPromise;
        std::future<void> mFinished{mFinishedPromise.get_future()};
        std::atomic<bool> mFinishSignalled{false};

        // Points at a counter owned by the test's BatchObserver, which must outlive this listener.
        const std::atomic<int>* mSubTransferStartsAtQuotaState{nullptr};
    };

    static std::string base64Handle(MegaNode& node)
    {
        std::unique_ptr<char[]> handle{node.getBase64Handle()};
        return handle ? std::string{handle.get()} : std::string{};
    }

    // Matches the download-URL request for any of `handles`: its "g" command names the node
    // in "n". onMatch, when given, records which handle was answered.
    static sdk_test::ScopedApiErrorInjector::CommandPredicate
        isDownloadUrlRequestFor(std::vector<std::string> handles,
                                std::function<void(const std::string&)> onMatch = nullptr)
    {
        return
            [handles = std::move(handles), onMatch = std::move(onMatch)](const std::string& command)
        {
            if (command.find("\"a\":\"g\"") == std::string::npos)
            {
                return false;
            }

            const std::string handle = sdk_test::commandStringArg(command, "n");
            if (std::find(handles.begin(), handles.end(), handle) == handles.end())
            {
                return false;
            }

            if (onMatch)
            {
                onMatch(handle);
            }
            return true;
        };
    }

private:
    const std::string localFolderName = getFilePrefix() + "batchDir";
    const fs::path localFolderPath = fs::current_path() / localFolderName;
    unique_ptr<MegaNode> rootNode;
};

/**
 * A batch of one folder item and one file item is a single MegaTransfer: one onTransferStart, one
 * onTransferFinish, with every file of both roots reported as a sub-transfer.
 */
TEST_F(SdkTestFolderBatchController, MixedBatchIsOneTransfer)
{
    static const std::string logPre{getLogPrefix()};
    static constexpr int kFolders{2};
    static constexpr int kFilesPerFolder{2};
    static constexpr int kFolderFiles{kFolders * kFilesPerFolder};

    LOG_info << logPre << "starting";

    const fs::path basePath = localTreeRoot();
    sdk_test::createWideLocalTree(basePath, kFolders, kFilesPerFolder);
    const MegaHandle remoteFolderHandle = uploadLocalTree(basePath);
    std::unique_ptr<MegaNode> folderNode{megaApi[0]->getNodeByHandle(remoteFolderHandle)};
    ASSERT_TRUE(folderNode);

    const std::string looseFileName = getFilePrefix() + "looseFile";
    std::unique_ptr<MegaNode> fileNode = uploadLooseFile(looseFileName);
    ASSERT_TRUE(fileNode);

    // Nothing may be left behind locally, or the collision pre-pass skips every file and no
    // sub-transfer ever starts.
    removeLocalTree();
    std::error_code ignoredEc;
    const fs::path looseFilePath = fs::current_path() / looseFileName;
    fs::remove(looseFilePath, ignoredEc);

    BatchObserver obs;
    NiceMock<MockTransferListener> subTransferListener{megaApi[0].get()};
    observeSubTransfers(subTransferListener, obs);

    BatchListener listener{megaApi[0].get()};
    const std::string downloadParent = path_u8string(fs::current_path() / "");
    std::unique_ptr<MegaDownloadItemList> items{MegaDownloadItemList::createInstance()};
    items->add(folderNode.get(), downloadParent.c_str());
    items->add(fileNode.get(), downloadParent.c_str());

    LOG_info << logPre << "downloading the batch";
    megaApi[0]->startDownloadBatch(items.get(),
                                   nullptr /*cancelToken*/,
                                   MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                                   MegaTransfer::COLLISION_RESOLUTION_OVERWRITE,
                                   &listener);

    ASSERT_TRUE(listener.waitForFinish(std::chrono::minutes{3})) << "Batch download never finished";

    {
        std::lock_guard<std::mutex> g{listener.mMutex};
        EXPECT_EQ(MegaError::API_OK, listener.mError) << "The batch should have succeeded";
        EXPECT_EQ(1, listener.mStarts) << "The whole batch is one transfer, so one onTransferStart";
        EXPECT_EQ(1, listener.mFinishes) << "The whole batch is one transfer, so one finish";
    }
    {
        std::lock_guard<std::mutex> g{obs.mutex};
        EXPECT_EQ(0, obs.subTransfersFailed) << "No file of the batch should have failed";
        EXPECT_EQ(kFolderFiles + 1, obs.subTransferStarts.load())
            << "Every file of both roots should be reported as a sub-transfer";
    }

    for (int f = 0; f < kFolders; ++f)
    {
        for (int i = 0; i < kFilesPerFolder; ++i)
        {
            const fs::path expected =
                basePath / ("sub" + std::to_string(f)) / ("file" + std::to_string(i));
            EXPECT_TRUE(fs::is_regular_file(expected))
                << "Missing file from the folder item: " << path_u8string(expected);
        }
    }
    EXPECT_TRUE(fs::is_regular_file(looseFilePath)) << "Missing the file item";

    EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, folderNode.get()));
    EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, fileNode.get()));
    removeLocalTree();
    fs::remove(looseFilePath, ignoredEc);
}

/**
 * Each item names its own destination, so one batch may store its roots in different folders. This
 * is what a loop of startDownload calls can do and a single folder download cannot.
 */
TEST_F(SdkTestFolderBatchController, ItemsUseTheirOwnDestinations)
{
    static const std::string logPre{getLogPrefix()};

    LOG_info << logPre << "starting";

    const std::string firstName = getFilePrefix() + "firstFile";
    const std::string secondName = getFilePrefix() + "secondFile";
    std::unique_ptr<MegaNode> firstNode = uploadLooseFile(firstName);
    std::unique_ptr<MegaNode> secondNode = uploadLooseFile(secondName);
    ASSERT_TRUE(firstNode);
    ASSERT_TRUE(secondNode);

    std::error_code ignoredEc;
    const fs::path firstDest = fs::current_path() / (getFilePrefix() + "destA");
    const fs::path secondDest = fs::current_path() / (getFilePrefix() + "destB");
    fs::remove_all(firstDest, ignoredEc);
    fs::remove_all(secondDest, ignoredEc);
    ASSERT_TRUE(fs::create_directories(firstDest));
    ASSERT_TRUE(fs::create_directories(secondDest));

    // The second item is also renamed, so the custom name is resolved per item too.
    const std::string customName = getFilePrefix() + "renamed";

    BatchListener listener{megaApi[0].get()};
    std::unique_ptr<MegaDownloadItemList> items{MegaDownloadItemList::createInstance()};
    items->add(firstNode.get(), path_u8string(firstDest / "").c_str());
    items->add(secondNode.get(), path_u8string(secondDest / "").c_str(), customName.c_str());

    LOG_info << logPre << "downloading the batch";
    megaApi[0]->startDownloadBatch(items.get(),
                                   nullptr /*cancelToken*/,
                                   MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                                   MegaTransfer::COLLISION_RESOLUTION_OVERWRITE,
                                   &listener);

    ASSERT_TRUE(listener.waitForFinish(std::chrono::minutes{3})) << "Batch download never finished";
    {
        std::lock_guard<std::mutex> g{listener.mMutex};
        EXPECT_EQ(MegaError::API_OK, listener.mError) << "The batch should have succeeded";
    }

    EXPECT_TRUE(fs::is_regular_file(firstDest / firstName))
        << "The first item did not land in its own destination";
    EXPECT_TRUE(fs::is_regular_file(secondDest / customName))
        << "The second item did not land in its own destination under its custom name";
    EXPECT_FALSE(fs::exists(secondDest / secondName))
        << "The custom name should have replaced the node's own name";
    EXPECT_FALSE(fs::exists(firstDest / secondName)) << "The items should not share a destination";

    EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, firstNode.get()));
    EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, secondNode.get()));
    fs::remove_all(firstDest, ignoredEc);
    fs::remove_all(secondDest, ignoredEc);
}

/**
 * A batch download carries appData per item: a folder item's value reaches every file found under
 * it, a file item's own value reaches its own transfer, and the batch transfer itself carries none.
 */
TEST_F(SdkTestFolderBatchController, AppDataIsPerItem)
{
    static const std::string logPre{getLogPrefix()};
    static constexpr int kFolders{1};
    static constexpr int kFilesPerFolder{2};
    static const std::string kFolderItemAppData{"folderItemAppData"};
    static const std::string kFileItemAppData{"fileItemAppData"};

    LOG_info << logPre << "starting";

    const fs::path basePath = localTreeRoot();
    sdk_test::createWideLocalTree(basePath, kFolders, kFilesPerFolder);
    const MegaHandle remoteFolderHandle = uploadLocalTree(basePath);
    std::unique_ptr<MegaNode> folderNode{megaApi[0]->getNodeByHandle(remoteFolderHandle)};
    ASSERT_TRUE(folderNode);

    const std::string looseFileName = getFilePrefix() + "looseFile";
    std::unique_ptr<MegaNode> fileNode = uploadLooseFile(looseFileName);
    ASSERT_TRUE(fileNode);

    removeLocalTree();
    std::error_code ignoredEc;
    const fs::path looseFilePath = fs::current_path() / looseFileName;
    fs::remove(looseFilePath, ignoredEc);

    BatchObserver obs;
    NiceMock<MockTransferListener> subTransferListener{megaApi[0].get()};
    observeSubTransfers(subTransferListener, obs);

    BatchListener listener{megaApi[0].get()};
    const std::string downloadParent = path_u8string(fs::current_path() / "");
    std::unique_ptr<MegaDownloadItemList> items{MegaDownloadItemList::createInstance()};
    items->add(folderNode.get(),
               downloadParent.c_str(),
               nullptr /*customName*/,
               kFolderItemAppData.c_str());
    items->add(fileNode.get(),
               downloadParent.c_str(),
               nullptr /*customName*/,
               kFileItemAppData.c_str(),
               true /*startFirst*/);

    LOG_info << logPre << "downloading the batch";
    megaApi[0]->startDownloadBatch(items.get(),
                                   nullptr /*cancelToken*/,
                                   MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                                   MegaTransfer::COLLISION_RESOLUTION_OVERWRITE,
                                   &listener);

    ASSERT_TRUE(listener.waitForFinish(std::chrono::minutes{3})) << "Batch download never finished";

    {
        std::lock_guard<std::mutex> g{listener.mMutex};
        EXPECT_EQ(MegaError::API_OK, listener.mError) << "The batch should have succeeded";
        EXPECT_FALSE(listener.mStartCarriedAppData)
            << "App data is per item, so the batch transfer itself must carry none";
    }

    {
        std::lock_guard<std::mutex> g{obs.mutex};
        EXPECT_EQ(0, obs.subTransferNameCollisions)
            << "Two sub-transfers shared a file name, so the app data of one was lost";

        for (int i = 0; i < kFilesPerFolder; ++i)
        {
            const std::string fileName = "file" + std::to_string(i);
            const auto it = obs.subTransferAppData.find(fileName);
            ASSERT_NE(obs.subTransferAppData.end(), it) << "No sub-transfer for " << fileName;
            EXPECT_EQ(kFolderItemAppData, it->second)
                << "A folder item's app data did not reach the file " << fileName
                << " found under it";
        }

        const auto loose = obs.subTransferAppData.find(looseFileName);
        ASSERT_NE(obs.subTransferAppData.end(), loose) << "No sub-transfer for the file item";
        EXPECT_EQ(kFileItemAppData, loose->second)
            << "A file item's own app data did not reach its transfer";
    }

    EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, folderNode.get()));
    EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, fileNode.get()));
    removeLocalTree();
    fs::remove(looseFilePath, ignoredEc);
}

/**
 * The transfer-quota prediction must reach the app before any of the batch's files starts, or it
 * cannot inform the decision the app blocks its UI on. A state that could not be determined is
 * still reported, as TRANSFER_QUOTA_STATE_UNKNOWN, so the app is always released.
 */
TEST_F(SdkTestFolderBatchController, ReportsQuotaStateBeforeAnyFileStarts)
{
    static const std::string logPre{getLogPrefix()};
    static constexpr int kFolders{2};
    static constexpr int kFilesPerFolder{2};

    LOG_info << logPre << "starting";

    const fs::path basePath = localTreeRoot();
    sdk_test::createWideLocalTree(basePath, kFolders, kFilesPerFolder);
    const MegaHandle remoteFolderHandle = uploadLocalTree(basePath);
    std::unique_ptr<MegaNode> folderNode{megaApi[0]->getNodeByHandle(remoteFolderHandle)};
    ASSERT_TRUE(folderNode);

    removeLocalTree();

    BatchObserver obs;
    NiceMock<MockTransferListener> subTransferListener{megaApi[0].get()};
    observeSubTransfers(subTransferListener, obs);

    BatchListener listener{megaApi[0].get()};
    listener.watchSubTransferStarts(obs.subTransferStarts);

    const std::string downloadParent = path_u8string(fs::current_path() / "");
    std::unique_ptr<MegaDownloadItemList> items{MegaDownloadItemList::createInstance()};
    items->add(folderNode.get(), downloadParent.c_str());

    LOG_info << logPre << "downloading the batch";
    megaApi[0]->startDownloadBatch(items.get(),
                                   nullptr /*cancelToken*/,
                                   MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                                   MegaTransfer::COLLISION_RESOLUTION_OVERWRITE,
                                   &listener);

    ASSERT_TRUE(listener.waitForFinish(std::chrono::minutes{3})) << "Batch download never finished";

    {
        std::lock_guard<std::mutex> g{listener.mMutex};
        EXPECT_EQ(MegaError::API_OK, listener.mError) << "The batch should have succeeded";
        EXPECT_EQ(1, listener.mQuotaStateReports)
            << "The quota state is reported exactly once per batch";
        EXPECT_EQ(0, listener.mSubTransferStartsWhenReported)
            << "The quota state must be reported before any of the batch's files starts";
        EXPECT_THAT(listener.mQuotaState,
                    AnyOf(MegaApi::TRANSFER_QUOTA_STATE_UNKNOWN,
                          MegaApi::TRANSFER_QUOTA_STATE_GREEN,
                          MegaApi::TRANSFER_QUOTA_STATE_ORANGE,
                          MegaApi::TRANSFER_QUOTA_STATE_RED))
            << "The reported state is not one of the documented values";
    }

    EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, folderNode.get()));
    removeLocalTree();
}

/**
 * One cancel token covers the whole batch, whatever mix of roots it holds. Cancelling from the
 * STAGE_CREATE_TREE stage change hits the window where the download worker thread is running and
 * the SDK thread will later join it: a bounded wait turns a deadlock there into a failure rather
 * than a hung run.
 */
TEST_F(SdkTestFolderBatchController, CancelTokenCancelsWholeBatch)
{
    static const std::string logPre{getLogPrefix()};
    static constexpr int kFolders{4};
    static constexpr int kFilesPerFolder{8};

    LOG_info << logPre << "starting";

    const fs::path basePath = localTreeRoot();
    sdk_test::createWideLocalTree(basePath, kFolders, kFilesPerFolder);
    const MegaHandle remoteFolderHandle = uploadLocalTree(basePath);
    std::unique_ptr<MegaNode> folderNode{megaApi[0]->getNodeByHandle(remoteFolderHandle)};
    ASSERT_TRUE(folderNode);

    const std::string looseFileName = getFilePrefix() + "looseFile";
    std::unique_ptr<MegaNode> fileNode = uploadLooseFile(looseFileName);
    ASSERT_TRUE(fileNode);

    removeLocalTree();
    std::error_code ignoredEc;
    const fs::path looseFilePath = fs::current_path() / looseFileName;
    fs::remove(looseFilePath, ignoredEc);

    std::unique_ptr<MegaCancelToken> cancelToken{MegaCancelToken::createInstance()};
    ASSERT_TRUE(cancelToken);

    // Cancels from the stage change, which is delivered while the worker thread is past the
    // collision pre-pass and still creating the batch's transfers.
    class CancellingBatchListener: public BatchListener
    {
    public:
        CancellingBatchListener(MegaApi* api, MegaCancelToken* token):
            BatchListener(api),
            mToken(token)
        {}

        void onTransferUpdate(MegaApi*, MegaTransfer* t) override
        {
            if (t->getStage() == MegaTransfer::STAGE_CREATE_TREE)
            {
                mToken->cancel();
            }
        }

    private:
        MegaCancelToken* mToken;
    };

    CancellingBatchListener listener{megaApi[0].get(), cancelToken.get()};

    const std::string downloadParent = path_u8string(fs::current_path() / "");
    std::unique_ptr<MegaDownloadItemList> items{MegaDownloadItemList::createInstance()};
    items->add(folderNode.get(), downloadParent.c_str());
    items->add(fileNode.get(), downloadParent.c_str());

    LOG_info << logPre << "downloading the batch and cancelling it";
    megaApi[0]->startDownloadBatch(items.get(),
                                   cancelToken.get(),
                                   MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                                   MegaTransfer::COLLISION_RESOLUTION_OVERWRITE,
                                   &listener);

    ASSERT_TRUE(listener.waitForFinish(std::chrono::minutes{3}))
        << "The cancelled batch never finished";
    {
        std::lock_guard<std::mutex> g{listener.mMutex};
        EXPECT_EQ(MegaError::API_EINCOMPLETE, listener.mError)
            << "A cancelled batch should finish as incomplete";
        EXPECT_EQ(1, listener.mFinishes) << "The whole batch is one transfer, so one finish";
    }

    EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, folderNode.get()));
    EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, fileNode.get()));
    removeLocalTree();
    fs::remove(looseFilePath, ignoredEc);
}

/**
 * A list that holds no usable node fails the batch rather than starting an empty download. Items
 * with a null node are kept in the list, so the indices the caller added still line up.
 */
TEST_F(SdkTestFolderBatchController, RejectsAListWithNoUsableNode)
{
    static const std::string logPre{getLogPrefix()};

    LOG_info << logPre << "starting";

    const std::string downloadParent = path_u8string(fs::current_path() / "");

    for (const bool withNullItems: {false, true})
    {
        std::unique_ptr<MegaDownloadItemList> items{MegaDownloadItemList::createInstance()};
        if (withNullItems)
        {
            items->add(nullptr, downloadParent.c_str());
            items->add(nullptr, downloadParent.c_str());
        }
        ASSERT_EQ(withNullItems ? 2 : 0, items->size());

        BatchListener listener{megaApi[0].get()};
        megaApi[0]->startDownloadBatch(items.get(),
                                       nullptr /*cancelToken*/,
                                       MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                                       MegaTransfer::COLLISION_RESOLUTION_OVERWRITE,
                                       &listener);

        ASSERT_TRUE(listener.waitForFinish(std::chrono::minutes{1}))
            << "The batch never finished, with null items: " << withNullItems;

        std::lock_guard<std::mutex> g{listener.mMutex};
        EXPECT_EQ(MegaError::API_EARGS, listener.mError)
            << "A batch with no usable node should fail, with null items: " << withNullItems;
        // A rejected batch is still one transfer, so it owes the app both callbacks: the empty
        // list is rejected before any item is looked at, which is where that is easiest to lose.
        EXPECT_EQ(1, listener.mStarts)
            << "A rejected batch still reports onTransferStart, with null items: " << withNullItems;
        EXPECT_EQ(1, listener.mFinishes)
            << "A rejected batch reports one onTransferFinish, with null items: " << withNullItems;
    }
}

/**
 * A batch whose roots have nothing to transfer: a 0-byte file and an empty folder. Neither
 * produces any bytes, so what the batch has to get right is creating them at all.
 */
TEST_F(SdkTestFolderBatchController, BatchOfEmptyRoots)
{
    static const std::string logPre{getLogPrefix()};

    LOG_info << logPre << "starting";

    const std::string emptyFileName = getFilePrefix() + "emptyFile";
    const std::string emptyFolderName = getFilePrefix() + "emptyFolder";
    const fs::path emptyFilePath = fs::current_path() / emptyFileName;
    const fs::path emptyFolderPath = fs::current_path() / emptyFolderName;

    removeLocalPath(emptyFilePath);
    removeLocalPath(emptyFolderPath);
    ASSERT_TRUE(createFileWithSize(path_u8string(emptyFilePath), 0));
    ASSERT_EQ(0, fs::file_size(emptyFilePath));
    ASSERT_TRUE(fs::create_directories(emptyFolderPath));

    MegaHandle emptyFileHandle = INVALID_HANDLE;
    ASSERT_EQ(MegaError::API_OK,
              doStartUpload(0,
                            &emptyFileHandle,
                            path_u8string(emptyFilePath).c_str(),
                            getRootNode().get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/))
        << "Failed to upload the empty file";
    std::unique_ptr<MegaNode> emptyFileNode{megaApi[0]->getNodeByHandle(emptyFileHandle)};
    ASSERT_TRUE(emptyFileNode);

    const MegaHandle emptyFolderHandle = uploadLocalTree(emptyFolderPath);
    std::unique_ptr<MegaNode> emptyFolderNode{megaApi[0]->getNodeByHandle(emptyFolderHandle)};
    ASSERT_TRUE(emptyFolderNode);

    removeLocalPath(emptyFilePath);
    removeLocalPath(emptyFolderPath);

    BatchObserver obs;
    NiceMock<MockTransferListener> subTransferListener{megaApi[0].get()};
    observeSubTransfers(subTransferListener, obs);

    BatchListener listener{megaApi[0].get()};
    const std::string downloadParent = path_u8string(fs::current_path() / "");
    std::unique_ptr<MegaDownloadItemList> items{MegaDownloadItemList::createInstance()};
    items->add(emptyFileNode.get(), downloadParent.c_str());
    items->add(emptyFolderNode.get(), downloadParent.c_str());

    LOG_info << logPre << "downloading the batch";
    megaApi[0]->startDownloadBatch(items.get(),
                                   nullptr /*cancelToken*/,
                                   MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                                   MegaTransfer::COLLISION_RESOLUTION_OVERWRITE,
                                   &listener);

    ASSERT_TRUE(listener.waitForFinish(std::chrono::minutes{3})) << "Batch download never finished";
    {
        std::lock_guard<std::mutex> g{listener.mMutex};
        EXPECT_EQ(MegaError::API_OK, listener.mError) << "The batch should have succeeded";
    }

    // No sub-transfer counts here: a 0-byte file transfers nothing whether it was fetched or
    // skipped, so the skipped/downloaded split cannot tell them apart.
    EXPECT_TRUE(fs::is_regular_file(emptyFilePath)) << "The 0-byte file item was not stored";
    EXPECT_EQ(0, fs::file_size(emptyFilePath)) << "The downloaded file should still be empty";
    EXPECT_TRUE(fs::is_directory(emptyFolderPath)) << "The empty folder item was not created";
    EXPECT_TRUE(fs::is_empty(emptyFolderPath)) << "The empty folder item should stay empty";

    EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, emptyFileNode.get()));
    EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, emptyFolderNode.get()));
    removeLocalPath(emptyFilePath);
    removeLocalPath(emptyFolderPath);
}

/**
 * One batch over folder items of every shape at once: a folder holding a single file, a folder
 * holding a single empty folder, and a tree mixing files at its own level with an empty subfolder
 * and a nested non-empty one.
 */
TEST_F(SdkTestFolderBatchController, BatchOfVariedFolderShapes)
{
    static const std::string logPre{getLogPrefix()};

    LOG_info << logPre << "starting";

    const fs::path oneFileRoot = fs::current_path() / (getFilePrefix() + "oneFile");
    const fs::path oneEmptyFolderRoot = fs::current_path() / (getFilePrefix() + "oneEmptyFolder");
    const fs::path mixedRoot = fs::current_path() / (getFilePrefix() + "mixed");

    removeLocalPath(oneFileRoot);
    removeLocalPath(oneEmptyFolderRoot);
    ASSERT_TRUE(fs::create_directories(oneFileRoot));
    ASSERT_TRUE(createFile(path_u8string(oneFileRoot / "onlyFile"), false));
    ASSERT_TRUE(fs::create_directories(oneEmptyFolderRoot / "onlyFolder"));
    const auto mixedFiles = sdk_test::createMixedLocalTree(mixedRoot);

    LOG_info << logPre << "uploading the roots";
    std::unique_ptr<MegaNode> oneFileNode{
        megaApi[0]->getNodeByHandle(uploadLocalTree(oneFileRoot))};
    std::unique_ptr<MegaNode> oneEmptyFolderNode{
        megaApi[0]->getNodeByHandle(uploadLocalTree(oneEmptyFolderRoot))};
    std::unique_ptr<MegaNode> mixedNode{megaApi[0]->getNodeByHandle(uploadLocalTree(mixedRoot))};
    ASSERT_TRUE(oneFileNode);
    ASSERT_TRUE(oneEmptyFolderNode);
    ASSERT_TRUE(mixedNode);

    // Nothing may be left behind locally, or the collision pre-pass skips every file.
    removeLocalPath(oneFileRoot);
    removeLocalPath(oneEmptyFolderRoot);
    removeLocalPath(mixedRoot);

    BatchObserver obs;
    NiceMock<MockTransferListener> subTransferListener{megaApi[0].get()};
    observeSubTransfers(subTransferListener, obs);

    BatchListener listener{megaApi[0].get()};
    const std::string downloadParent = path_u8string(fs::current_path() / "");
    std::unique_ptr<MegaDownloadItemList> items{MegaDownloadItemList::createInstance()};
    items->add(oneFileNode.get(), downloadParent.c_str());
    items->add(oneEmptyFolderNode.get(), downloadParent.c_str());
    items->add(mixedNode.get(), downloadParent.c_str());

    LOG_info << logPre << "downloading the batch";
    megaApi[0]->startDownloadBatch(items.get(),
                                   nullptr /*cancelToken*/,
                                   MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                                   MegaTransfer::COLLISION_RESOLUTION_OVERWRITE,
                                   &listener);

    ASSERT_TRUE(listener.waitForFinish(std::chrono::minutes{5})) << "Batch download never finished";

    {
        std::lock_guard<std::mutex> g{listener.mMutex};
        EXPECT_EQ(MegaError::API_OK, listener.mError) << "The batch should have succeeded";
    }
    {
        std::lock_guard<std::mutex> g{obs.mutex};
        EXPECT_EQ(0, obs.subTransfersFailed) << "No file of the batch should have failed";
        EXPECT_EQ(static_cast<int>(mixedFiles.size()) + 1, obs.subTransfersDownloaded)
            << "Every file of every root should have been downloaded";
        EXPECT_EQ(0, obs.subTransfersSkipped)
            << "Nothing was left locally, so nothing may be skipped";
    }

    EXPECT_TRUE(fs::is_regular_file(oneFileRoot / "onlyFile"))
        << "The folder item holding a single file was not restored";
    EXPECT_TRUE(fs::is_directory(oneEmptyFolderRoot / "onlyFolder"))
        << "The folder item holding a single empty folder was not restored";
    for (const auto& relative: mixedFiles)
    {
        EXPECT_TRUE(fs::is_regular_file(mixedRoot / relative))
            << "Missing file from the mixed root: " << path_u8string(mixedRoot / relative);
    }
    EXPECT_TRUE(fs::is_directory(mixedRoot / "emptySub"))
        << "An empty folder must still be created, although it produces no transfer";

    EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, oneFileNode.get()));
    EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, oneEmptyFolderNode.get()));
    EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, mixedNode.get()));
    removeLocalPath(oneFileRoot);
    removeLocalPath(oneEmptyFolderRoot);
    removeLocalPath(mixedRoot);
}

/**
 * A batch downloaded onto a destination that already holds part of it: the collision check must
 * skip the files that are already there byte for byte and fetch only the rest. The batch's
 * destination folders are the caller's own, so they must be merged into rather than renamed.
 */
TEST_F(SdkTestFolderBatchController, BatchSkipsFilesTheCollisionCheckRejects)
{
    static const std::string logPre{getLogPrefix()};

    LOG_info << logPre << "starting";

    const fs::path mixedRoot = fs::current_path() / (getFilePrefix() + "mixed");
    const std::string looseFileName = getFilePrefix() + "looseFile";
    const fs::path looseFilePath = fs::current_path() / looseFileName;

    const auto mixedFiles = sdk_test::createMixedLocalTree(mixedRoot);
    std::unique_ptr<MegaNode> mixedNode{megaApi[0]->getNodeByHandle(uploadLocalTree(mixedRoot))};
    ASSERT_TRUE(mixedNode);
    std::unique_ptr<MegaNode> looseFileNode = uploadLooseFile(looseFileName);
    ASSERT_TRUE(looseFileNode);

    // The mixed tree is left in place, so its files are already there byte for byte. Drop one
    // subtree and one root file, which the download then has to fetch again. The file item's own
    // local copy was removed by uploadLooseFile, so it is fetched too.
    const fs::path removedSubtree{"sub0"};
    const fs::path removedFile{"rootFile0"};
    removeLocalPath(mixedRoot / removedSubtree);
    removeLocalPath(mixedRoot / removedFile);

    // Derived rather than hardcoded, so a change to the tree cannot silently invalidate the counts.
    const int removedFromMixed = static_cast<int>(
        std::count_if(mixedFiles.begin(),
                      mixedFiles.end(),
                      [&removedSubtree, &removedFile](const fs::path& relative)
                      {
                          return relative == removedFile || *relative.begin() == removedSubtree;
                      }));
    ASSERT_GT(removedFromMixed, 0) << "The tree no longer holds what this test removes";

    BatchObserver obs;
    NiceMock<MockTransferListener> subTransferListener{megaApi[0].get()};
    observeSubTransfers(subTransferListener, obs);

    BatchListener listener{megaApi[0].get()};
    const std::string downloadParent = path_u8string(fs::current_path() / "");
    std::unique_ptr<MegaDownloadItemList> items{MegaDownloadItemList::createInstance()};
    items->add(mixedNode.get(), downloadParent.c_str());
    items->add(looseFileNode.get(), downloadParent.c_str());

    LOG_info << logPre << "downloading the batch onto the partially existing tree";
    megaApi[0]->startDownloadBatch(items.get(),
                                   nullptr /*cancelToken*/,
                                   MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                                   MegaTransfer::COLLISION_RESOLUTION_OVERWRITE,
                                   &listener);

    ASSERT_TRUE(listener.waitForFinish(std::chrono::minutes{5})) << "Batch download never finished";

    const int keptFiles = static_cast<int>(mixedFiles.size()) - removedFromMixed;
    {
        std::lock_guard<std::mutex> g{listener.mMutex};
        EXPECT_EQ(MegaError::API_OK, listener.mError) << "The batch should have succeeded";
    }
    {
        std::lock_guard<std::mutex> g{obs.mutex};
        EXPECT_EQ(0, obs.subTransfersFailed) << "No file of the batch should have failed";
        EXPECT_EQ(keptFiles, obs.subTransfersSkipped)
            << "The files still present byte for byte should have been skipped";
        EXPECT_EQ(removedFromMixed + 1, obs.subTransfersDownloaded)
            << "The removed files and the file item should have been downloaded";
    }

    for (const auto& relative: mixedFiles)
    {
        EXPECT_TRUE(fs::is_regular_file(mixedRoot / relative))
            << "Missing file after the merge: " << path_u8string(mixedRoot / relative);
    }
    EXPECT_TRUE(fs::is_regular_file(looseFilePath)) << "Missing the file item";

    EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, mixedNode.get()));
    EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, looseFileNode.get()));
    removeLocalPath(mixedRoot);
    removeLocalPath(looseFilePath);
}

/**
 * MegaApi::retryTransfer on a batch restarts the whole batch. The item list travels on the
 * transfer, so the retry downloads the same roots to the same destinations with the same per-item
 * app data; a transfer that lost its items would fall through to the single-node path and retry
 * nothing.
 */
TEST_F(SdkTestFolderBatchController, RetryTransferRestartsTheWholeBatch)
{
    static const std::string logPre{getLogPrefix()};
    static constexpr int kFolders{1};
    static constexpr int kFilesPerFolder{2};
    static const std::string kFolderItemAppData{"folderItemAppData"};
    static const std::string kFileItemAppData{"fileItemAppData"};

    LOG_info << logPre << "starting";

    const fs::path basePath = localTreeRoot();
    const auto treeFiles = sdk_test::createWideLocalTree(basePath, kFolders, kFilesPerFolder);
    const MegaHandle remoteFolderHandle = uploadLocalTree(basePath);
    std::unique_ptr<MegaNode> folderNode{megaApi[0]->getNodeByHandle(remoteFolderHandle)};
    ASSERT_TRUE(folderNode);

    const std::string looseFileName = getFilePrefix() + "looseFile";
    std::unique_ptr<MegaNode> fileNode = uploadLooseFile(looseFileName);
    ASSERT_TRUE(fileNode);

    removeLocalTree();
    std::error_code ignoredEc;
    const fs::path looseFilePath = fs::current_path() / looseFileName;
    fs::remove(looseFilePath, ignoredEc);

    BatchObserver obs;
    NiceMock<MockTransferListener> subTransferListener{megaApi[0].get()};
    observeSubTransfers(subTransferListener, obs);

    const std::string downloadParent = path_u8string(fs::current_path() / "");
    std::unique_ptr<MegaDownloadItemList> items{MegaDownloadItemList::createInstance()};
    items->add(folderNode.get(),
               downloadParent.c_str(),
               nullptr /*customName*/,
               kFolderItemAppData.c_str());
    items->add(fileNode.get(),
               downloadParent.c_str(),
               nullptr /*customName*/,
               kFileItemAppData.c_str());

    BatchListener first{megaApi[0].get()};
    LOG_info << logPre << "downloading the batch";
    megaApi[0]->startDownloadBatch(items.get(),
                                   nullptr /*cancelToken*/,
                                   MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                                   MegaTransfer::COLLISION_RESOLUTION_OVERWRITE,
                                   &first);
    ASSERT_TRUE(first.waitForFinish(std::chrono::minutes{3})) << "Batch download never finished";

    std::unique_ptr<MegaTransfer> finished;
    {
        std::lock_guard<std::mutex> g{first.mMutex};
        ASSERT_EQ(MegaError::API_OK, first.mError) << "The batch should have succeeded";
        ASSERT_TRUE(first.mFinishedTransfer) << "No transfer was reported to retry";
        finished = std::move(first.mFinishedTransfer);
    }

    // What the first run produced, for the retry to be compared against
    std::map<std::string, std::string> firstRun;
    {
        std::lock_guard<std::mutex> g{obs.mutex};
        ASSERT_EQ(0, obs.subTransferNameCollisions)
            << "Two sub-transfers shared a file name, so the counts below undercount";
        firstRun = obs.subTransferAppData;
    }
    obs.reset();

    ASSERT_EQ(kFolders * kFilesPerFolder + 1, static_cast<int>(firstRun.size()))
        << "The batch did not download every file of both items";

    // Removed so the retry fetches them again instead of skipping them as already present
    removeLocalTree();
    fs::remove(looseFilePath, ignoredEc);

    BatchListener retried{megaApi[0].get()};
    LOG_info << logPre << "retrying the batch";
    megaApi[0]->retryTransfer(finished.get(), &retried);
    ASSERT_TRUE(retried.waitForFinish(std::chrono::minutes{3})) << "Retried batch never finished";

    {
        std::lock_guard<std::mutex> g{retried.mMutex};
        EXPECT_EQ(MegaError::API_OK, retried.mError) << "The retried batch should have succeeded";
        EXPECT_EQ(1, retried.mStarts) << "A retried batch is one transfer, as the original was";
        EXPECT_EQ(1, retried.mFinishes);
    }

    {
        std::lock_guard<std::mutex> g{obs.mutex};
        EXPECT_EQ(firstRun, obs.subTransferAppData)
            << "The retry did not fetch the same files, with the same per-item app data";
        EXPECT_EQ(0, obs.subTransfersFailed) << "No file of the retried batch should have failed";
        EXPECT_EQ(kFolders * kFilesPerFolder + 1, obs.subTransfersDownloaded)
            << "The retry did not download every file of both items again";
    }

    for (const auto& relative: treeFiles)
    {
        EXPECT_TRUE(fs::is_regular_file(basePath / relative))
            << "Missing after the retry: " << path_u8string(basePath / relative);
    }
    EXPECT_TRUE(fs::is_regular_file(looseFilePath)) << "Missing the file item after the retry";

    EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, folderNode.get()));
    EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, fileNode.get()));
    removeLocalTree();
    fs::remove(looseFilePath, ignoredEc);
}

/**
 * A batch holding a single file item reports its sub-transfer's temporary errors on the batch
 * transfer itself, the way a plain single-file download does. Before, those reached only the
 * listeners registered with addListener, never the listener passed to startDownloadBatch.
 *
 * The failure is injected, not induced from the filesystem: the download-URL request is answered
 * with API_ETEMPUNAVAIL, which Transfer::failed treats as retryable. So it runs the same on every
 * platform, and cannot pass without the injection having fired.
 */
TEST_F(SdkTestFolderBatchController, SingleFileBatchReportsTemporaryErrorOnTheBatchTransfer)
{
    static const std::string logPre{getLogPrefix()};

    LOG_info << logPre << "starting";

    const std::string looseFileName = getFilePrefix() + "temporaryErrorFile";
    std::unique_ptr<MegaNode> fileNode = uploadLooseFile(looseFileName);
    ASSERT_TRUE(fileNode);

    std::error_code ignoredEc;
    const fs::path looseFilePath = fs::current_path() / looseFileName;
    fs::remove(looseFilePath, ignoredEc);

    // Answers every attempt: the sub-transfer backs off and asks again while it retries.
    sdk_test::ScopedApiErrorInjector injector{API_ETEMPUNAVAIL,
                                              isDownloadUrlRequestFor({base64Handle(*fileNode)})};

    std::unique_ptr<MegaCancelToken> cancelToken{MegaCancelToken::createInstance()};
    ASSERT_TRUE(cancelToken);

    BatchListener listener{megaApi[0].get()};

    const std::string downloadParent = path_u8string(fs::current_path() / "");
    std::unique_ptr<MegaDownloadItemList> items{MegaDownloadItemList::createInstance()};
    items->add(fileNode.get(), downloadParent.c_str());

    LOG_info << logPre << "downloading one file whose download URL keeps failing";
    megaApi[0]->startDownloadBatch(items.get(),
                                   cancelToken.get(),
                                   MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                                   MegaTransfer::COLLISION_RESOLUTION_OVERWRITE,
                                   &listener);

    // The sub-transfer retries with backoff, so the batch never ends on its own.
    const bool sawTemporaryError = WaitFor(
        [&listener]
        {
            std::lock_guard<std::mutex> g{listener.mMutex};
            return listener.mTemporaryErrors > 0;
        },
        180 * 1000);
    cancelToken->cancel();
    EXPECT_TRUE(listener.waitForFinish(std::chrono::minutes{3}))
        << "The cancelled batch never finished";

    EXPECT_TRUE(injector.fired()) << "The failure was never injected, so nothing here was tested";

    {
        std::lock_guard<std::mutex> g{listener.mMutex};
        EXPECT_TRUE(sawTemporaryError)
            << "The batch listener never saw its single file's temporary error";
        EXPECT_NE(MegaError::API_OK, listener.mTemporaryError)
            << "The forwarded error should carry the sub-transfer's failure";
        EXPECT_EQ(listener.mTemporaryError, listener.mTemporaryErrorOnTransfer)
            << "getLastErrorExtended() on the batch transfer should match the reported error";
        EXPECT_EQ(MegaTransfer::STATE_RETRYING, listener.mTemporaryErrorState)
            << "A retrying sub-transfer should leave the batch transfer retrying too";
    }

    EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, fileNode.get()));
    fs::remove(looseFilePath, ignoredEc);
}

/**
 * The same induced failure in a batch of two file items reports nothing on the batch transfer:
 * with more than one file there is no single sub-transfer whose error the parent could stand for,
 * so only the listeners registered with addListener hear about it. Pins that the single-file rule
 * did not widen into every batch.
 */
TEST_F(SdkTestFolderBatchController, MultiItemBatchKeepsTemporaryErrorsOffTheBatchTransfer)
{
    static const std::string logPre{getLogPrefix()};

    LOG_info << logPre << "starting";

    const std::string firstName = getFilePrefix() + "temporaryErrorFirst";
    const std::string secondName = getFilePrefix() + "temporaryErrorSecond";
    std::unique_ptr<MegaNode> firstNode = uploadLooseFile(firstName, "first ");
    ASSERT_TRUE(firstNode);
    std::unique_ptr<MegaNode> secondNode = uploadLooseFile(secondName, "second ");
    ASSERT_TRUE(secondNode);

    std::error_code ignoredEc;
    fs::remove(fs::current_path() / firstName, ignoredEc);
    fs::remove(fs::current_path() / secondName, ignoredEc);

    // Which files were actually failed, by handle: a bare count cannot tell two sub-transfers
    // apart from one retrying twice. Declared before the injector so it outlives the hook.
    std::mutex failedMutex;
    std::set<std::string> failedHandles;

    sdk_test::ScopedApiErrorInjector injector{
        API_ETEMPUNAVAIL,
        isDownloadUrlRequestFor({base64Handle(*firstNode), base64Handle(*secondNode)},
                                [&failedMutex, &failedHandles](const std::string& handle)
                                {
                                    std::lock_guard<std::mutex> g{failedMutex};
                                    failedHandles.insert(handle);
                                })};

    std::unique_ptr<MegaCancelToken> cancelToken{MegaCancelToken::createInstance()};
    ASSERT_TRUE(cancelToken);

    BatchListener listener{megaApi[0].get()};

    const std::string downloadParent = path_u8string(fs::current_path() / "");
    std::unique_ptr<MegaDownloadItemList> items{MegaDownloadItemList::createInstance()};
    items->add(firstNode.get(), downloadParent.c_str());
    items->add(secondNode.get(), downloadParent.c_str());

    LOG_info << logPre << "downloading two files whose download URLs keep failing";
    megaApi[0]->startDownloadBatch(items.get(),
                                   cancelToken.get(),
                                   MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                                   MegaTransfer::COLLISION_RESOLUTION_OVERWRITE,
                                   &listener);

    // Both files must actually fail before the absence of a batch callback proves anything, and
    // it has to be both of them: each distinct handle is one sub-transfer reporting a temporary
    // error to the global listeners.
    const bool bothFailed = WaitFor(
        [&failedMutex, &failedHandles]
        {
            std::lock_guard<std::mutex> g{failedMutex};
            return failedHandles.size() >= 2;
        },
        180 * 1000);
    ASSERT_TRUE(bothFailed) << "The batch's two downloads were not both failed";

    // Now give the batch listener every chance to report one it should not have.
    WaitFor(
        [&listener]
        {
            std::lock_guard<std::mutex> g{listener.mMutex};
            return listener.mTemporaryErrors > 0;
        },
        15 * 1000);

    cancelToken->cancel();
    EXPECT_TRUE(listener.waitForFinish(std::chrono::minutes{3}))
        << "The cancelled batch never finished";

    {
        std::lock_guard<std::mutex> g{listener.mMutex};
        EXPECT_EQ(0, listener.mTemporaryErrors)
            << "A batch of several files must not report a single file's error as its own";
    }

    EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, firstNode.get()));
    EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, secondNode.get()));
}

/**
 * Cancelling a single-file batch reports API_EINCOMPLETE and STATE_CANCELLED, not STATE_FAILED:
 * the sub-transfer resolves as incomplete, and that is a cancellation, exactly as
 * processTransferRemoved reads it for a plain single-file download.
 */
TEST_F(SdkTestFolderBatchController, CancelledSingleFileBatchIsCancelledNotFailed)
{
    static const std::string logPre{getLogPrefix()};

    LOG_info << logPre << "starting";

    const std::string looseFileName = getFilePrefix() + "cancelledFile";
    std::unique_ptr<MegaNode> fileNode = uploadLooseFile(looseFileName);
    ASSERT_TRUE(fileNode);

    std::error_code ignoredEc;
    const fs::path looseFilePath = fs::current_path() / looseFileName;
    fs::remove(looseFilePath, ignoredEc);

    std::unique_ptr<MegaCancelToken> cancelToken{MegaCancelToken::createInstance()};
    ASSERT_TRUE(cancelToken);

    class CancellingBatchListener: public BatchListener
    {
    public:
        CancellingBatchListener(MegaApi* api, MegaCancelToken* token):
            BatchListener(api),
            mToken(token)
        {}

        void onTransferUpdate(MegaApi*, MegaTransfer* t) override
        {
            if (t->getStage() == MegaTransfer::STAGE_CREATE_TREE)
            {
                mToken->cancel();
            }
        }

    private:
        MegaCancelToken* mToken;
    };

    CancellingBatchListener listener{megaApi[0].get(), cancelToken.get()};

    const std::string downloadParent = path_u8string(fs::current_path() / "");
    std::unique_ptr<MegaDownloadItemList> items{MegaDownloadItemList::createInstance()};
    items->add(fileNode.get(), downloadParent.c_str());

    LOG_info << logPre << "downloading one file and cancelling it";
    megaApi[0]->startDownloadBatch(items.get(),
                                   cancelToken.get(),
                                   MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                                   MegaTransfer::COLLISION_RESOLUTION_OVERWRITE,
                                   &listener);

    ASSERT_TRUE(listener.waitForFinish(std::chrono::minutes{3}))
        << "The cancelled batch never finished";

    {
        std::lock_guard<std::mutex> g{listener.mMutex};
        EXPECT_EQ(MegaError::API_EINCOMPLETE, listener.mError)
            << "A cancelled batch should finish as incomplete";
        EXPECT_EQ(1, listener.mFinishes) << "The whole batch is one transfer, so one finish";
        EXPECT_EQ(MegaTransfer::STATE_CANCELLED, listener.mFinishState)
            << "A cancelled single-file batch is cancelled, not completed and not failed";
    }

    EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, fileNode.get()));
    fs::remove(looseFilePath, ignoredEc);
}

/**
 * A single-file batch whose one file is taken down reports API_ETOOMANY with the ETD user and link
 * status, and STATE_FAILED. It used to report API_EINCOMPLETE with the extra info dropped, because
 * MegaRecursiveOperation flattens every sub-transfer error into that one code.
 *
 * processTransferFailed routes API_ETOOMANY with extra info to fireOnTransferFinish rather than
 * fireOnTransferTemporaryError, so this is the terminal half of the same behaviour. No test
 * account can be taken down on demand, so the response is forged on the lockless CS channel the
 * download-URL request uses, the way sdk_test_sc_channel forges SC responses.
 */
TEST_F(SdkTestFolderBatchController, SingleFileBatchReportsItsFileTakedownNotIncomplete)
{
    static const std::string logPre{getLogPrefix()};
    // MegaError::USER_ENABLED and MegaError::LINK_DOWN_ETD: the link is down by an ETD while the
    // account itself is fine, which is one of the shapes the API sends.
    static constexpr long long kUserStatus{0};
    static constexpr long long kLinkStatus{2};

    LOG_info << logPre << "starting";

    const std::string looseFileName = getFilePrefix() + "takenDownFile";
    std::unique_ptr<MegaNode> fileNode = uploadLooseFile(looseFileName);
    ASSERT_TRUE(fileNode);

    std::error_code ignoredEc;
    const fs::path looseFilePath = fs::current_path() / looseFileName;
    fs::remove(looseFilePath, ignoredEc);

    sdk_test::ScopedApiErrorInjector injector{API_ETOOMANY,
                                              {kUserStatus, kLinkStatus},
                                              isDownloadUrlRequestFor({base64Handle(*fileNode)})};

    BatchListener listener{megaApi[0].get()};

    const std::string downloadParent = path_u8string(fs::current_path() / "");
    std::unique_ptr<MegaDownloadItemList> items{MegaDownloadItemList::createInstance()};
    items->add(fileNode.get(), downloadParent.c_str());

    LOG_info << logPre << "downloading one file whose download URL is answered with a takedown";
    megaApi[0]->startDownloadBatch(items.get(),
                                   nullptr /*cancelToken*/,
                                   MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                                   MegaTransfer::COLLISION_RESOLUTION_OVERWRITE,
                                   &listener);

    ASSERT_TRUE(listener.waitForFinish(std::chrono::minutes{3})) << "The batch never finished";
    ASSERT_TRUE(injector.fired()) << "The takedown was never injected, so nothing here was tested";

    {
        std::lock_guard<std::mutex> g{listener.mMutex};
        EXPECT_EQ(MegaError::API_ETOOMANY, listener.mError)
            << "A single-file batch should report its file's own error, not API_EINCOMPLETE";
        EXPECT_EQ(kUserStatus, listener.mFinishUserStatus)
            << "The ETD user status did not survive to the batch's listener";
        EXPECT_EQ(kLinkStatus, listener.mFinishLinkStatus)
            << "The ETD link status did not survive to the batch's listener";
        EXPECT_EQ(MegaTransfer::STATE_FAILED, listener.mFinishState)
            << "A batch whose only file failed should report itself failed";
        EXPECT_EQ(1, listener.mFinishes) << "The whole batch is one transfer, so one finish";
    }

    EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, fileNode.get()));
    fs::remove(looseFilePath, ignoredEc);
}

/**
 * A single-file batch that fails before its sub-transfer ever starts still reports itself failed,
 * as the equivalent MegaApi::startDownload would. The destination folder is blocked by a regular
 * file, which ensureLocalFolder_unlocked refuses during the collision pre-pass: a real filesystem
 * refusal that needs no permissions, so it behaves the same on every platform.
 *
 * This is the half of resolvedState() the cancellation test cannot reach. The error never passes
 * through a sub-transfer, so mSingleFileError is untouched and complete()'s own error is all there
 * is to decide the state on; before it was given one, this reported STATE_COMPLETED.
 */
TEST_F(SdkTestFolderBatchController, SingleFileBatchFailingBeforeItsTransferReportsFailed)
{
    static const std::string logPre{getLogPrefix()};

    LOG_info << logPre << "starting";

    const std::string looseFileName = getFilePrefix() + "blockedDestFile";
    std::unique_ptr<MegaNode> fileNode = uploadLooseFile(looseFileName);
    ASSERT_TRUE(fileNode);

    std::error_code ignoredEc;
    fs::remove(fs::current_path() / looseFileName, ignoredEc);

    // A regular file standing exactly where the destination folder would have to be.
    const fs::path blockedDest = fs::current_path() / (getFilePrefix() + "blockedDest");
    fs::remove_all(blockedDest, ignoredEc);
    ASSERT_TRUE(createFile(path_u8string(blockedDest), false));
    ASSERT_TRUE(fs::is_regular_file(blockedDest));

    BatchListener listener{megaApi[0].get()};

    // Trailing separator: the path names the folder to download into, not the file to save as.
    const std::string downloadParent = path_u8string(blockedDest / "");
    std::unique_ptr<MegaDownloadItemList> items{MegaDownloadItemList::createInstance()};
    items->add(fileNode.get(), downloadParent.c_str());

    LOG_info << logPre << "downloading one file into a destination a file is standing in";
    megaApi[0]->startDownloadBatch(items.get(),
                                   nullptr /*cancelToken*/,
                                   MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                                   MegaTransfer::COLLISION_RESOLUTION_OVERWRITE,
                                   &listener);

    ASSERT_TRUE(listener.waitForFinish(std::chrono::minutes{3})) << "The batch never finished";

    {
        std::lock_guard<std::mutex> g{listener.mMutex};
        EXPECT_EQ(MegaError::API_EARGS, listener.mError)
            << "A file where the destination folder belongs should fail the batch with the "
               "destination's own error, not the flattened API_EINCOMPLETE";
        EXPECT_EQ(MegaTransfer::STATE_FAILED, listener.mFinishState)
            << "A single-file batch that failed before its transfer started is failed, not "
               "completed";
        EXPECT_EQ(1, listener.mStarts) << "The batch is one transfer, so one onTransferStart";
        EXPECT_EQ(1, listener.mFinishes) << "... and one onTransferFinish";
    }

    fs::remove(blockedDest, ignoredEc);
    EXPECT_EQ(MegaError::API_OK, doDeleteNode(0, fileNode.get()));
}
