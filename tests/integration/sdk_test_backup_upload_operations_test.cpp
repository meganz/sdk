/**
 * @file sdk_test_backup_upload_operations_test.cpp
 * @brief This file defines a test fixture that involves backup syncs in terms of files upload.
 */
#ifdef ENABLE_SYNC

#include "backup_test_utils.h"
using namespace sdk_test;
using namespace testing;

/**
 * @class SdkTestBackupUploadsOperations
 * @brief Test fixture for validating backup syncs in terms of files upload.
 */
class SdkTestBackupUploadsOperations: public SdkTestBackup
{
public:
    static constexpr auto COMMON_TIMEOUT = 3min;
    std::unique_ptr<FSACCESS_CLASS> mFsAccess;

    /**
     * @brief Sets the cleanup function to be executed during TearDown.
     *
     * If a custom cleanup function is provided, it will be used.
     * Otherwise, a default one will be set.
     *
     * @example:
     *  - example1 (default cleanupFunction):
     *          auto cleanup = setCleanupFunction();
     *  - example2 (custom cleanupFunction):
     *          auto cleanup = setCleanupFunction([this](){
     *              // custom cleanup function code
     *          });
     *
     * @note: is mandatory calling this method at the beginning of each test of this file, otherwise
     * test will fail at teardown. The reason behind is to enforce setting an appropriate cleanup
     * function for each test.
     *
     * @param customCleanupFunction Optional custom cleanup function.
     * @return The cleanup function that was set.
     */
    std::unique_ptr<MrProper>
        setCleanupFunction(std::function<void()> customCleanupFunction = nullptr)
    {
        mCleanupFunctionSet = true;
        if (customCleanupFunction)
        {
            return std::make_unique<MrProper>(customCleanupFunction);
        }
        else
        {
            return std::make_unique<MrProper>(
                [this]()
                {
                    cleanDefaultListeners();
                });
        }
    }

    void cleanDefaultListeners()
    {
        removeBackupSync();

        if (mMtl)
        {
            megaApi[0]->removeListener(mMtl.get());
            mMtl.reset();
        }

        if (mMslStats)
        {
            megaApi[0]->removeListener(mMslStats.get());
            mMslStats.reset();
        }

        if (mMslFiles)
        {
            megaApi[0]->removeListener(mMslFiles.get());
            mMslFiles.reset();
        }
    }

    /**
     * @brief Creates a local file and waits until it is backed up.
     * @param localFilePathAbs Absolute path to the local file to be created.
     * @param contents The file contents to be written.
     * @param customMtime Optional custom modification time.
     * @return Pair (success, shared pointer to the created LocalTempFile).
     */
    std::pair<bool, shared_ptr<sdk_test::LocalTempFile>>
        createLocalFileAndWaitForSync(const fs::path& localFilePathAbs,
                                      const std::string_view contents,
                                      std::optional<fs::file_time_type> customMtime);

    /**
     * @brief Moves deconfigured backup nodes into a cloud folder.
     */
    void moveDeconfiguredBackupNodesToCloud();

    /**
     * @brief Resets local variables that tracks when backup sync is up-to-date.
     */
    void resetOnSyncStatsUpdated();

    /**
     * @brief Waits until the backup sync is up-to-date state.
     * @return True if sync was up to date within the timeout, false otherwise.
     */
    bool waitForBackupSyncUpToDate() const;

    /**
     * @brief Confirms that local and cloud models are identical.
     */
    void confirmModels() const;

protected:
    void SetUp() override;
    void TearDown() override;

    /**
     * @brief Creates the `archive` destination directory in the cloud used to store deconfigured
     * backup nodes.
     */
    void createArchiveDestinationFolder();

    /**
     * @brief Returns the names of the first-level children in the local backup folder.
     * @note Hidden files and DEBRISFOLDER are ignored
     * @return Vector with the names of local files and directories.
     */
    std::vector<std::string> getLocalFirstLevelChildrenNames() const;

    /**
     * @brief Gets the handle of the `archive` destination folder in the cloud.
     * @return Handle of the `archive` destination folder.
     */
    MegaHandle getArchiveDestinationFolderHandle() const;

    /**
     * @brief Gets the handle of the backup root folder in the cloud.
     * @return Handle of the backup root folder.
     */
    MegaHandle getBackupRootHandle() const;

    /**
     * @brief Recursively checks that the local and cloud models match.
     * @param parentHandle handle of the cloud parent node.
     * @param localPath Local relative path corresponding to the parent node.
     * @return True if both models match, false otherwise.
     */
    bool checkSyncRecursively(const MegaHandle parentHandle,
                              std::optional<std::string> localPath) const;
    shared_ptr<sdk_test::LocalTempFile>
        createLocalFile(const fs::path& filePath,
                        const std::string_view contents,
                        std::optional<fs::file_time_type> customMtime);

private:
    std::unique_ptr<NiceMock<MockTransferListener>> mMtl;
    std::unique_ptr<NiceMock<MockSyncListener>> mMslStats;
    std::unique_ptr<NiceMock<MockSyncListener>> mMslFiles;
    SyncItemTrackerManager<SyncUploadOperationsTracker> mSyncListenerTrackers;
    SyncItemTrackerManager<SyncUploadOperationsTransferTracker> mTransferListenerTrackers;
    MegaHandle mBackupRootHandle{INVALID_HANDLE};
    MegaHandle mCloudArchiveBackupFolderHandle{INVALID_HANDLE};
    const fs::path mCloudArchiveBackupFolderName{"BackupArchive"};
    std::atomic<bool> mIsUpToDate{false};
    std::shared_ptr<std::promise<void>> mSyncUpToDatePms;
    std::unique_ptr<std::future<void>> mSyncFut;
    bool mCleanupFunctionSet{false};
}; // class SdkTestBackupUploadsOperations

std::pair<bool, shared_ptr<sdk_test::LocalTempFile>>
    SdkTestBackupUploadsOperations::createLocalFileAndWaitForSync(
        const fs::path& localFilePathAbs,
        const std::string_view contents,
        std::optional<fs::file_time_type> customMtime)
{
    if (!mMtl)
    {
        LOG_err << "createLocalFileAndWaitForSync: invalid transfer listener";
        return {false, nullptr};
    }

    if (!mMslFiles)
    {
        LOG_err << "createLocalFileAndWaitForSync: invalid transfer listener";
        return {false, nullptr};
    }

    std::shared_ptr<SyncUploadOperationsTransferTracker> tt;
    if (tt = mTransferListenerTrackers.add(localFilePathAbs.string()); !tt)
    {
        LOG_err << "Cannot add TransferListenerTracker for: " << localFilePathAbs.string();
        return {false, nullptr};
    }

    auto st = mSyncListenerTrackers.add(localFilePathAbs.string());
    if (!st)
    {
        return {false, nullptr};
    }

    auto localFile = createLocalFile(localFilePathAbs, contents, customMtime);
    auto [stFutStatus, stErrCode] = st->waitForCompletion(COMMON_TIMEOUT);
    if (stFutStatus != std::future_status::ready)
    {
        LOG_err << "onTransferFinish not received for: " << localFilePathAbs;
        return {false, nullptr};
    }

    if (stErrCode != API_OK)
    {
        LOG_err << "Transfer failed (" << localFilePathAbs << ")";
        return {false, nullptr};
    }

    auto [ttFutStatus, ttErrCode] = tt->waitForCompletion(COMMON_TIMEOUT);
    if (ttFutStatus != std::future_status::ready)
    {
        LOG_err << "onTransferFinish not received for: " << localFilePathAbs;
        return {false, nullptr};
    }

    if (ttErrCode != API_OK)
    {
        LOG_err << "Transfer failed (" << localFilePathAbs << ")";
        return {false, nullptr};
    }

    return {true, localFile};
}

void SdkTestBackupUploadsOperations::moveDeconfiguredBackupNodesToCloud()
{
    NiceMock<MockRequestListener> reqListener{megaApi[0].get()};
    reqListener.setErrorExpectations(API_OK);
    megaApi[0]->moveOrRemoveDeconfiguredBackupNodes(getBackupRootHandle(),
                                                    getArchiveDestinationFolderHandle(),
                                                    &reqListener);
    ASSERT_TRUE(reqListener.waitForFinishOrTimeout(MAX_TIMEOUT)) << "";
}

void SdkTestBackupUploadsOperations::resetOnSyncStatsUpdated()
{
    mSyncUpToDatePms.reset(new std::promise<void>());
    mSyncFut.reset(new std::future<void>(mSyncUpToDatePms->get_future()));
    mIsUpToDate = false;
}

bool SdkTestBackupUploadsOperations::waitForBackupSyncUpToDate() const
{
    if (!mMslStats)
        return false;
    return mSyncFut->wait_for(COMMON_TIMEOUT) == std::future_status::ready;
}

void SdkTestBackupUploadsOperations::confirmModels() const
{
    const auto areLocalAndCloudSyncedExhaustive = [this]() -> bool
    {
        return checkSyncRecursively(getBackupRootHandle(), nullopt);
    };

    ASSERT_TRUE(waitFor(areLocalAndCloudSyncedExhaustive, COMMON_TIMEOUT, 10s));
}

void SdkTestBackupUploadsOperations::SetUp()
{
    SdkTestBackup::SetUp();
    mFsAccess = std::make_unique<FSACCESS_CLASS>();
    ASSERT_NO_FATAL_FAILURE(createBackupSync());
    ASSERT_NO_FATAL_FAILURE(createArchiveDestinationFolder());
    const std::unique_ptr<MegaSync> sync{megaApi[0]->getSyncByBackupId(getBackupId())};
    ASSERT_TRUE(sync);
    mBackupRootHandle = sync->getMegaHandle();

    // add transfer listener
    mMtl.reset(new NiceMock<MockTransferListener>(megaApi[0].get()));
    EXPECT_CALL(*mMtl, onTransferStart)
        .WillRepeatedly(
            [this](::mega::MegaApi*, ::mega::MegaTransfer* t)
            {
                if (!t || !t->getPath())
                {
                    return;
                }

                auto element = mTransferListenerTrackers.getByPath(t->getPath());
                if (!element)
                    return;

                ASSERT_EQ(++element->transferStartCount, 1)
                    << "Unexpected times onTransferStart has been called: " << t->getPath();
            });

    EXPECT_CALL(*mMtl, onTransferFinish)
        .WillRepeatedly(
            [this](::mega::MegaApi*, ::mega::MegaTransfer* t, ::mega::MegaError* e)
            {
                if (!t || !t->getPath())
                {
                    return;
                }

                auto element = mTransferListenerTrackers.getByPath(t->getPath());
                if (!element || !e)
                    return;

                ASSERT_TRUE(!element->getActionCompleted())
                    << "onTransferFinish has been previously received: " << t->getPath();
                element->setActionCompleted();
                element->setActionCompletedPms(e->getErrorCode());
            });
    megaApi[0]->addListener(mMtl.get());

    // add sync listener and add EXPECT(S)
    mMslStats.reset(new NiceMock<MockSyncListener>(megaApi[0].get()));
    EXPECT_CALL(*mMslStats.get(), onSyncStatsUpdated(_, _))
        .WillRepeatedly(
            [this](MegaApi*, MegaSyncStats* stats)
            {
                if (stats->getBackupId() == getBackupId() && stats->getUploadCount() == 0 &&
                    !stats->isScanning() && !stats->isSyncing() && mSyncUpToDatePms && !mIsUpToDate)
                {
                    mIsUpToDate = true;
                    mSyncUpToDatePms->set_value();
                }
            });
    megaApi[0]->addListener(mMslStats.get());

    mMslFiles.reset(new NiceMock<MockSyncListener>(megaApi[0].get()));
    EXPECT_CALL(*mMslFiles.get(), onSyncFileStateChanged(_, _, _, _))
        .WillRepeatedly(
            [this](MegaApi*, MegaSync* sync, std::string* localPath, int newState)
            {
                if (sync && sync->getBackupId() == getBackupId() &&
                    newState == MegaApi::STATE_SYNCED && localPath)
                {
                    auto element = mSyncListenerTrackers.getByPath(*localPath);
                    if (!element || element->getActionCompleted())
                        return;

                    element->setActionCompleted();
                    element->setActionCompletedPms(API_OK);
                }
            });
    megaApi[0]->addListener(mMslFiles.get());
}

void SdkTestBackupUploadsOperations::TearDown()
{
    ASSERT_TRUE(mCleanupFunctionSet) << getLogPrefix()
                                     << "(TearDown). cleanupfunction has not been properly set by "
                                        "calling `setCleanupFunction()`.";

    ASSERT_TRUE(!mMtl) << getLogPrefix()
                       << "(TearDown). Transfer listener has not been unregistered yet";
    ASSERT_TRUE(!mMslStats) << getLogPrefix()
                            << "(TearDown). Sync listener has not been unregistered yet";
    removeBackupSync();
    SdkTestBackup::TearDown();
}

void SdkTestBackupUploadsOperations::createArchiveDestinationFolder()
{
    unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode) << "setupDestinationDirectory: Account root node not available.";
    mCloudArchiveBackupFolderHandle =
        createFolder(0, mCloudArchiveBackupFolderName.u8string().c_str(), rootnode.get());
    ASSERT_NE(mCloudArchiveBackupFolderHandle, INVALID_HANDLE)
        << "setupDestinationDirectory: Invalid destination folder handle";
}

std::vector<std::string> SdkTestBackupUploadsOperations::getLocalFirstLevelChildrenNames() const
{
    fs::path localFolderPath = getLocalFolderPath();
    return sdk_test::getLocalFirstChildrenNames_if(localFolderPath,
                                                   [](const std::string& name)
                                                   {
                                                       return name.front() != '.' &&
                                                              name != DEBRISFOLDER;
                                                   });
}

MegaHandle SdkTestBackupUploadsOperations::getArchiveDestinationFolderHandle() const
{
    return mCloudArchiveBackupFolderHandle;
}

MegaHandle SdkTestBackupUploadsOperations::getBackupRootHandle() const
{
    return mBackupRootHandle;
}

bool SdkTestBackupUploadsOperations::checkSyncRecursively(
    const MegaHandle parentHandle,
    std::optional<std::string> localPath) const
{
    auto [childrenCloudNames, childrenNodeList] =
        getCloudFirstChildren(megaApi[0].get(), parentHandle);
    if (!childrenCloudNames.has_value() || !childrenNodeList)
    {
        return false;
    }

    const auto localChildrenNames = getLocalFirstLevelChildrenNames();
    if (!Value(localChildrenNames, UnorderedElementsAreArray(childrenCloudNames.value())))
    {
        return false;
    }

    for (int i = 0; i < childrenNodeList->size(); ++i)
    {
        auto childNode = childrenNodeList->get(i);
        if (!childNode)
        {
            return false;
        }

        const std::string childLocalPath = !localPath.has_value() ?
                                               childNode->getName() :
                                               localPath.value() + "/" + childNode->getName();
        if (childNode->isFolder() && !checkSyncRecursively(childNode->getHandle(), childLocalPath))
        {
            return false;
        }
    }

    return true;
}

shared_ptr<sdk_test::LocalTempFile>
    SdkTestBackupUploadsOperations::createLocalFile(const fs::path& filePath,
                                                    const std::string_view contents,
                                                    std::optional<fs::file_time_type> customMtime)
{
    return std::make_shared<sdk_test::LocalTempFile>(filePath, contents, customMtime);
}

/**
 * @test SdkTestBackupUploadsOperations.BasicTest
 *
 * 1. Create multiple local file in the backup directory and ensure it's synced.
 * 2. Suspend the backup sync and move backup nodes to the cloud.
 * 3. Confirm that local and remote models match.
 */
TEST_F(SdkTestBackupUploadsOperations, BasicTest)
{
    static const auto logPre{getLogPrefix()};
    LOG_verbose << logPre << "#### Test body started ####";
    // Add cleanup function to unregister listeners as soon as test fail/finish
    const auto cleanup = setCleanupFunction();

    // Reset MockSyncListener related promise/future
    resetOnSyncStatsUpdated();

    auto localBasePath{fs::absolute(getLocalFolderPath())};
    LOG_debug << logPre << "#### TC1 Creating local file `file1` in Backup dir ####";
    auto [res1, localFile1] = createLocalFileAndWaitForSync(localBasePath / "file1",
                                                            "abcde",
                                                            fs::file_time_type::clock::now());
    ASSERT_TRUE(res1) << "Cannot create local file `file1`";

    LOG_debug << "#### TC2 wait until all files (in Backup folder) have been synced  ####";
    ASSERT_TRUE(waitForBackupSyncUpToDate());

    LOG_debug << logPre << "#### TC3 Ensure local and cloud drive structures matches ####";
    ASSERT_NO_FATAL_FAILURE(confirmModels());

    LOG_verbose << logPre << "#### Test finished ####";
}

/**
 * @test SdkTestBackupUploadsOperations.NodesRemoteCopyUponResumingBackup
 *
 * 1. Create multiple local files in the backup directory and ensure they are synced.
 * 2. Suspend the backup sync and move backup nodes to the cloud.
 * 3. Remove the suspended sync, then set up the backup sync again.
 * 4. Resume backup sync and ensure files are synced (remote copy must be done)
 * 5. Confirm that local and remote models match.
 */
TEST_F(SdkTestBackupUploadsOperations, NodesRemoteCopyUponResumingBackup)
{
    std::shared_ptr<NiceMock<MockTransferListener>> auxMtl;
    static const auto logPre{getLogPrefix()};
    LOG_verbose << logPre << "#### Test body started ####";

    // Add cleanup function to unregister listeners as soon as test fail/finish
    const auto cleanup = setCleanupFunction(
        [this, auxMtl]()
        {
            cleanDefaultListeners();
            if (auxMtl)
            {
                megaApi[0]->removeListener(auxMtl.get());
            }
        });

    constexpr unsigned numFiles{3};
    auto localBasePath{fs::absolute(getLocalFolderPath())};
    std::vector<std::shared_ptr<sdk_test::LocalTempFile>> localFiles;

    auto mtime = fs::file_time_type::clock::now();
    for (unsigned i = 1; i <= numFiles; ++i)
    {
        auto auxMtime = mtime + std::chrono::seconds(MIN_ALLOW_MTIME_DIFFERENCE * i);
        std::string filename = "file" + std::to_string(i);
        LOG_debug << logPre << "#### TC " << std::to_string(i) << "Creating local file `"
                  << filename << "` in Backup dir ####";
        auto [res, localFile] =
            createLocalFileAndWaitForSync(localBasePath / filename, "abcde", auxMtime);

        ASSERT_TRUE(res) << "Cannot create local file `" << filename << "`";
        localFiles.push_back(localFile);
    }

    LOG_debug << logPre << "#### TC4 suspending sync ####";
    ASSERT_NO_FATAL_FAILURE(suspendBackupSync());

    LOG_debug << logPre << "#### TC5 moving backup nodes to Cloud ####";
    ASSERT_NO_FATAL_FAILURE(moveDeconfiguredBackupNodesToCloud());

    LOG_debug << logPre << "#### TC6 removing suspending ####";
    removeBackupSync();

    LOG_debug << logPre << "#### TC7 setup sync (again) ####";
    createBackupSync();
    resetOnSyncStatsUpdated();

    // Clone Put nodes (no transfer is created)
    auxMtl.reset(new NiceMock<MockTransferListener>(megaApi[0].get()));
    EXPECT_CALL(*auxMtl.get(), onTransferStart).Times(0);
    EXPECT_CALL(*auxMtl.get(), onTransferFinish).Times(0);
    megaApi[0]->addListener(auxMtl.get());

    LOG_debug << logPre << "#### TC8 resuming sync ####";
    ASSERT_NO_FATAL_FAILURE(resumeBackupSync());

    LOG_debug << logPre
              << "#### TC9 wait until all files (in Backup folder) have been synced  ####";
    ASSERT_TRUE(waitForBackupSyncUpToDate());

    LOG_debug << logPre << "#### TC10 ensure local and cloud drive models match ####";
    ASSERT_NO_FATAL_FAILURE(confirmModels());

    LOG_verbose << logPre << "#### Test finished ####";
}

/**
 * @test SdkTestBackupUploadsOperations.UpdateNodeMtime
 *
 * 1. Create a local file in the backup directory and ensure it is synced.
 * 2. Wait until the backup sync is up to date.
 * 3. Update mtime to local file and wait for notification that confirms change.
 * 4. Confirm that local and remote (cloud) models match.
 */
TEST_F(SdkTestBackupUploadsOperations, UpdateNodeMtime)
{
    static const auto logPre{getLogPrefix()};
    LOG_verbose << logPre << "#### Test body started ####";
    // Add cleanup function to unregister listeners as soon as test fail/finish
    const auto cleanup = setCleanupFunction();

    // Reset MockSyncListener related promise/future
    resetOnSyncStatsUpdated();

    auto localBasePath{fs::absolute(getLocalFolderPath())};
    LOG_debug << logPre << "#### TC1 Creating local file `file1` in Backup dir ####";
    auto [res1, localFile1] = createLocalFileAndWaitForSync(localBasePath / "file1",
                                                            "abcde",
                                                            fs::file_time_type::clock::now());
    ASSERT_TRUE(res1) << "Cannot create local file `file1`";

    LOG_debug << "#### TC2 wait until all files (in Backup folder) have been synced  ####";
    ASSERT_TRUE(waitForBackupSyncUpToDate());
    resetOnSyncStatsUpdated();

    std::unique_ptr<MegaSync> backupSync(megaApi[0]->getSyncByBackupId(getBackupId()));
    ASSERT_TRUE(backupSync) << "Cannot get backup sync";
    std::unique_ptr<MegaNode> backupNode(megaApi[0]->getNodeByHandle(backupSync->getMegaHandle()));
    ASSERT_TRUE(backupNode) << "Cannot get backup sync";
    std::unique_ptr<MegaNode> fileNode(
        megaApi[0]->getChildNodeOfType(backupNode.get(), "file1", FILENODE));
    ASSERT_TRUE(fileNode) << "Cannot get file node";

    bool mTimeChangeRecv{false};
    mApi[0].mOnNodesUpdateCompletion =
        [&mTimeChangeRecv,
         oldMtime = fileNode->getModificationTime(),
         targetNodeHandle = fileNode->getHandle()](size_t, MegaNodeList* nodes)
    {
        ASSERT_TRUE(nodes) << "Invalid meganode list received";
        for (int i = 0; i < nodes->size(); ++i)
        {
            MegaNode* n = nodes->get(i);
            if (n && n->getHandle() == targetNodeHandle &&
                n->hasChanged(static_cast<uint64_t>(MegaNode::CHANGE_TYPE_ATTRIBUTES)) &&
                oldMtime != n->getModificationTime())
            {
                mTimeChangeRecv = true;
            }
        }
    };

    LOG_debug << logPre << "#### TC3 Update mtime to local file ####";
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    mFsAccess->setmtimelocal(LocalPath::fromAbsolutePath((localBasePath / "file1").u8string()),
                             m_time(nullptr));
    ASSERT_TRUE(waitForResponse(&mTimeChangeRecv))
        << "No mtime change received after " << maxTimeout << " seconds";
    resetOnNodeUpdateCompletionCBs(); // important to reset

    LOG_debug << logPre << "#### TC4 Ensure local and cloud drive structures matches ####";
    ASSERT_NO_FATAL_FAILURE(confirmModels());
    LOG_verbose << logPre << "#### Test finished ####";
}

/**
 * @test SdkTestBackupUploadsOperations.getnodesByFingerprintNoMtime
 *
 * 1. Create '3' local files in the backup directory and ensure they are synced.
 * 2. Validate `getNodesByFingerprint` and `getNodesByFingerprintIgnoringMtime` results
 * 3. Modify the mtime of local file (idx_0) setting mtime of local file (idx_2) and wait for sync.
 * 4. Modify the mtime of local file (idx_1) setting mtime of local file (idx_2) and wait for sync.
 * 5. Validate `getNodesByFingerprint` and `getNodesByFingerprintIgnoringMtime` results
 * 6. Confirm that local and remote (cloud) models match.
 */
TEST_F(SdkTestBackupUploadsOperations, getnodesByFingerprintNoMtime)
{
    static const auto logPre{getLogPrefix()};
    LOG_verbose << logPre << "#### Test body started ####";

    // Add cleanup function to unregister listeners as soon as test fail/finish
    const auto cleanup = setCleanupFunction();

    std::unique_ptr<MegaSync> backupSync(megaApi[0]->getSyncByBackupId(getBackupId()));
    ASSERT_TRUE(backupSync) << "Cannot get backup sync";
    std::unique_ptr<MegaNode> backupNode(megaApi[0]->getNodeByHandle(backupSync->getMegaHandle()));
    ASSERT_TRUE(backupNode) << "Cannot get backup sync node";

    constexpr unsigned numFiles{3};
    auto localBasePath{fs::absolute(getLocalFolderPath())};
    std::vector<std::pair<std::shared_ptr<sdk_test::LocalTempFile>, fs::file_time_type>> localFiles;
    std::vector<std::string> filenames;

    LOG_debug << logPre << "#### TC1: create (" << numFiles
              << ") local files and wait until back up has been completed ####";

    auto mtime{fs::file_time_type::clock::now()};
    for (unsigned i = 1; i <= numFiles; ++i)
    {
        const auto auxMtime{mtime + std::chrono::seconds(MIN_ALLOW_MTIME_DIFFERENCE * i)};
        std::string fn = "file" + std::to_string(i);
        filenames.emplace_back(fn);
        LOG_debug << logPre << "#### TC1." << std::to_string(i) << " Creating local file `" << fn
                  << "` in Backup dir ####";
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        auto [res, localFile] =
            createLocalFileAndWaitForSync(localBasePath / fn, "abcde", auxMtime);

        ASSERT_TRUE(res) << "Cannot sync local file `" << fn << "`";
        localFiles.push_back({localFile, auxMtime});
    }

    LOG_debug << logPre << "#### TC2: getNodesByFingerprint with and without mtime ####";
    std::unique_ptr<MegaNodeList> nl;
    std::vector<std::unique_ptr<MegaNode>> nodes;
    nodes.emplace_back(
        megaApi[0]->getChildNodeOfType(backupNode.get(), filenames[0].c_str(), FILENODE));
    nodes.emplace_back(
        megaApi[0]->getChildNodeOfType(backupNode.get(), filenames[1].c_str(), FILENODE));
    nodes.emplace_back(
        megaApi[0]->getChildNodeOfType(backupNode.get(), filenames[2].c_str(), FILENODE));

    for (size_t i = 0; i < nodes.size(); ++i)
    {
        auto& n = nodes.at(i);
        ASSERT_TRUE(n) << "Invalid node with index(" << std::to_string(i) << ")";
        ASSERT_TRUE(n->getFingerprint())
            << "Invalid fingerprint for node(" << toNodeHandle(n->getHandle()) << ")";
        auto auxfp = n->getFingerprint();

        nl.reset(megaApi[0]->getNodesByFingerprint(auxfp));
        ASSERT_EQ(nl->size(), 1) << "TC2.1(" << std::to_string(i) << "): getNodesByFingerprint("
                                 << auxfp << ") Unexpected node count";

        nl.reset(megaApi[0]->getNodesByFingerprintIgnoringMtime(n->getFingerprint()));
        ASSERT_EQ(nl->size(), nodes.size())
            << "TC2.2(" << std::to_string(i) << "): getNodesByFingerprintIgnoringMtime(" << auxfp
            << ") Unexpected node count";
    }

    auto updateNodeMtime =
        [this](MegaHandle nodeHandle, const LocalPath& path, int64_t oldMtime, int64_t newMtime)
    {
        bool mTimeChangeRecv{false};
        mApi[0].mOnNodesUpdateCompletion =
            [&mTimeChangeRecv, oldMtime, nodeHandle](size_t, MegaNodeList* nodes)
        {
            ASSERT_TRUE(nodes) << "Invalid meganode list received";
            for (int i = 0; i < nodes->size(); ++i)
            {
                MegaNode* n = nodes->get(i);
                if (n && n->getHandle() == nodeHandle &&
                    n->hasChanged(static_cast<uint64_t>(MegaNode::CHANGE_TYPE_ATTRIBUTES)) &&
                    oldMtime != n->getModificationTime())
                {
                    mTimeChangeRecv = true;
                }
            }
        };

        mFsAccess->setmtimelocal(path, newMtime);
        ASSERT_TRUE(waitForResponse(&mTimeChangeRecv))
            << "No mtime change received after " << maxTimeout << " seconds";
        resetOnNodeUpdateCompletionCBs(); // important to reset
    };

    LOG_debug
        << logPre
        << "#### TC3 update localNode (idx_0) mtime (with mtime of idx_2) and wait for sync ####";
    auto h = nodes.at(0)->getHandle();
    auto path = LocalPath::fromAbsolutePath(localFiles.at(0).first->getPath().u8string());
    auto oldMtime = nodes.at(0)->getModificationTime();
    auto newMtime = nodes.at(2)->getModificationTime();
    updateNodeMtime(h, path, oldMtime, newMtime);

    LOG_debug
        << logPre
        << "#### TC4 update localNode (idx_1) mtime (with mtime of idx_2) and wait for sync ####";
    h = nodes.at(1)->getHandle();
    path = LocalPath::fromAbsolutePath(localFiles.at(1).first->getPath().u8string());
    oldMtime = nodes.at(1)->getModificationTime();
    newMtime = nodes.at(2)->getModificationTime();
    updateNodeMtime(h, path, oldMtime, newMtime);

    LOG_debug << logPre
              << "#### TC5: getNodesByFingerprint with and without mtime (Now 3 nodes should have "
                 "same mtime) ####";
    nodes.clear();
    nodes.emplace_back(
        megaApi[0]->getChildNodeOfType(backupNode.get(), filenames[0].c_str(), FILENODE));
    nodes.emplace_back(
        megaApi[0]->getChildNodeOfType(backupNode.get(), filenames[1].c_str(), FILENODE));
    nodes.emplace_back(
        megaApi[0]->getChildNodeOfType(backupNode.get(), filenames[2].c_str(), FILENODE));
    for (auto& n: nodes)
    {
        nl.reset(megaApi[0]->getNodesByFingerprint(n->getFingerprint()));
        ASSERT_EQ(nl->size(), nodes.size())
            << "(getNodesByFingerprint) Unexpected node count by FP1";

        nl.reset(megaApi[0]->getNodesByFingerprintIgnoringMtime(n->getFingerprint()));
        ASSERT_EQ(nl->size(), nodes.size())
            << "(getNodesByFingerprintIgnoringMtime) Unexpected node count by FP1";
    }

    LOG_debug << logPre << "#### TC4 Ensure local and cloud drive structures matches ####";
    ASSERT_NO_FATAL_FAILURE(confirmModels());
    LOG_verbose << logPre << "#### Test finished ####";
}
#endif
