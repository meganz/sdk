/**
 * @file
 * @brief This file is expected to contain tests involving sync upload operations
 * e.g., what happens when a file is duplicated inside a sync.
 */

#ifdef ENABLE_SYNC

#include "integration_test_utils.h"
#include "mega/utils.h"
#include "megautils.h"
#include "mock_listeners.h"
#include "sdk_test_utils.h"
#include "SdkTestSyncNodesOperations.h"

#include <gmock/gmock.h>

using namespace sdk_test;
using namespace testing;

/**
 * @class SdkTestSyncUploadOperations
 * @brief Test fixture designed to test operations involving sync uploads.
 */
class SdkTestSyncUploadsOperations: public SdkTestSyncNodesOperations
{
protected:
    // [TODO] SDK-5629. Check Lifetime of listeners in this test suite once this ticket has been
    // resolved
    std::unique_ptr<NiceMock<MockTransferListener>> mMtl;
    std::unique_ptr<NiceMock<MockSyncListener>> mMsl;
    bool mCleanupFunctionSet{false};
    const std::string SYNC_REMOTE_PATH{"localSyncedDir"};
    std::vector<shared_ptr<sdk_test::LocalTempFile>> mLocalFiles;
    std::unique_ptr<FSACCESS_CLASS> mFsAccess;
    SyncItemTrackerManager<SyncUploadOperationsTracker> mSyncListenerTrackers;
    SyncItemTrackerManager<SyncUploadOperationsTransferTracker> mTransferListenerTrackers;
    mutable std::recursive_timed_mutex trackerMutex;
    using TestMutexGuard = std::unique_lock<std::recursive_timed_mutex>;

    std::shared_ptr<SyncUploadOperationsTracker> addSyncListenerTracker(const std::string& s)
    {
        TestMutexGuard g(trackerMutex);
        return mSyncListenerTrackers.add(s);
    }

    std::shared_ptr<SyncUploadOperationsTracker> getSyncListenerTrackerByPath(const std::string& s)
    {
        TestMutexGuard g(trackerMutex);
        return mSyncListenerTrackers.getByPath(s);
    }

    std::shared_ptr<SyncUploadOperationsTransferTracker>
        addTransferListenerTracker(const std::string& s)
    {
        TestMutexGuard g(trackerMutex);
        return mTransferListenerTrackers.add(s);
    }

    std::shared_ptr<SyncUploadOperationsTransferTracker>
        getTransferListenerTrackerByPath(const std::string& s)
    {
        TestMutexGuard g(trackerMutex);
        return mTransferListenerTrackers.getByPath(s);
    }

    /**
     * @brief Waits for sync completion and verifies transfer behavior for a file operation.
     *
     * This is the common logic for file creation and move operations.
     * Call this AFTER setting up trackers and performing the file operation.
     *
     * @param localFilePathAbs Absolute path to the file being synced (for error messages).
     * @param st The sync listener tracker for this file.
     * @param tt The transfer listener tracker for this file.
     * @param isFullUploadExpected If true, validates transfer completes with API_OK.
     * If false, validates that NO transfer occurred (clone/setattr should be used instead).
     */
    void waitForSyncAndVerifyTransfer(const fs::path& localFilePathAbs,
                                      std::shared_ptr<SyncUploadOperationsTracker> st,
                                      std::shared_ptr<SyncUploadOperationsTransferTracker> tt,
                                      const bool isFullUploadExpected)
    {
        auto [syncStatus, syncErrCode] = st->waitForCompletion(COMMON_TIMEOUT);
        ASSERT_TRUE(syncStatus == std::future_status::ready)
            << "Sync state change not received for: " << localFilePathAbs;
        ASSERT_EQ(syncErrCode, API_OK) << "Sync completed with error for: " << localFilePathAbs;

        ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

        const auto transferTimeout =
            isFullUploadExpected ? COMMON_TIMEOUT : std::chrono::seconds(30);
        auto [transferStatus, transferErrCode] = tt->waitForCompletion(transferTimeout);

        const auto expectedTransferStatus =
            isFullUploadExpected ? std::future_status::ready : std::future_status::timeout;
        ASSERT_EQ(transferStatus, expectedTransferStatus)
            << "Unexpected transfer status for: " << localFilePathAbs
            << " [isFullUploadExpected: " << isFullUploadExpected << "]";
        const auto expectedTransferStartCount = isFullUploadExpected ? 1 : 0;

        ASSERT_EQ(tt->transferStartCount.load(), expectedTransferStartCount)
            << "Transfer started count mismatch for: " << localFilePathAbs
            << " [isFullUploadExpected: " << isFullUploadExpected << "]";

        if (isFullUploadExpected)
        {
            ASSERT_EQ(transferErrCode, API_OK) << "Transfer failed (" << localFilePathAbs << ")";
        }
    }

    /**
     * @brief Creates a local test file and verifies sync completion and transfer behavior.
     *
     * @param localFilePathAbs Absolute filesystem path where the test file will be created. Must be
     * an absolute path to match correctly with sync state change events.
     * @param fileContent The content to write to the test file.
     * @param customMtime Custom modification time to apply to the created file
     * @param isFullUploadExpected If true, validates that transfer completes with API_OK.
     * If false, validates that NO transfer occurred (clone/setattr should be used instead).
     */
    void createTestFileInternal(const fs::path& localFilePathAbs,
                                const std::string& fileContent,
                                std::chrono::time_point<fs::file_time_type::clock> customMtime,
                                const bool isFullUploadExpected)
    {
        static const std::string logPre{"createTestFileInternal: "};
        ASSERT_TRUE(mMtl) << logPre << "Invalid transfer listener";

        auto tt = addTransferListenerTracker(localFilePathAbs.string());
        ASSERT_TRUE(tt) << logPre
                        << "Cannot add TransferListenerTracker for: " << localFilePathAbs.string();
        auto st = addSyncListenerTracker(localFilePathAbs.string());
        ASSERT_TRUE(st) << logPre
                        << "Cannot add SyncListenerTracker for: " << localFilePathAbs.string();

        LOG_debug << logPre << "Creating local file: " << localFilePathAbs.string();
        auto localFile =
            std::make_shared<sdk_test::LocalTempFile>(localFilePathAbs, fileContent, customMtime);
        mLocalFiles.emplace_back(localFile);

        ASSERT_NO_FATAL_FAILURE(
            waitForSyncAndVerifyTransfer(localFilePathAbs, st, tt, isFullUploadExpected));
    }

    /**
     * @brief Moves a file into the sync and verifies sync completion and transfer behavior.
     *
     * @param sourcePath Absolute path to the source file (outside sync).
     * @param targetPathInSync Absolute path where file will be moved (inside sync).
     * @param isFullUploadExpected If true, validates transfer completes with API_OK.
     * If false, validates that NO transfer occurred (clone/setattr should be used instead).
     * @param expectedMtimeAfterMove Optional: if provided, verifies the moved file has this mtime.
     */
    void moveFileIntoSyncAndVerify(const fs::path& sourcePath,
                                   const fs::path& targetPathInSync,
                                   const bool isFullUploadExpected,
                                   std::optional<m_time_t> expectedMtimeAfterMove = std::nullopt)
    {
        static const std::string logPre{"moveFileIntoSyncAndVerify: "};
        ASSERT_TRUE(mMtl) << logPre << "Invalid transfer listener";

        auto tt = addTransferListenerTracker(targetPathInSync.string());
        ASSERT_TRUE(tt) << logPre
                        << "Cannot add TransferListenerTracker for: " << targetPathInSync.string();
        auto st = addSyncListenerTracker(targetPathInSync.string());
        ASSERT_TRUE(st) << logPre
                        << "Cannot add SyncListenerTracker for: " << targetPathInSync.string();

        std::error_code renameError;
        fs::rename(sourcePath, targetPathInSync, renameError);
        ASSERT_FALSE(renameError) << logPre << "Failed to move file from " << sourcePath << " to "
                                  << targetPathInSync << ": " << renameError.message();

        if (expectedMtimeAfterMove.has_value())
        {
            auto [getMtimeOk, movedFileMtime] =
                mFsAccess->getmtimelocal(LocalPath::fromAbsolutePath(targetPathInSync.u8string()));
            ASSERT_TRUE(getMtimeOk)
                << logPre << "Failed to get mtime of moved file: " << targetPathInSync;
            ASSERT_EQ(movedFileMtime, expectedMtimeAfterMove.value())
                << logPre << "fs::rename should preserve mtime for: " << targetPathInSync;
        }

        ASSERT_NO_FATAL_FAILURE(
            waitForSyncAndVerifyTransfer(targetPathInSync, st, tt, isFullUploadExpected));
    }

public:
    void SetUp() override
    {
        SdkTestNodesSetUp::SetUp();
        mFsAccess = std::make_unique<FSACCESS_CLASS>();
        if (createSyncOnSetup())
        {
            ASSERT_NO_FATAL_FAILURE(
                initiateSync(getLocalTmpDirU8string(), SYNC_REMOTE_PATH, mBackupId));
            ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());
        }

        mMtl.reset(new NiceMock<MockTransferListener>(megaApi[0].get()));
        EXPECT_CALL(*mMtl, onTransferStart)
            .WillRepeatedly(
                [this](::mega::MegaApi*, ::mega::MegaTransfer* t)
                {
                    if (!t || !t->getPath())
                    {
                        return;
                    }

                    auto element = getTransferListenerTrackerByPath(t->getPath());
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

                    auto element = getTransferListenerTrackerByPath(t->getPath());
                    if (!element || !e)
                        return;

                    ASSERT_TRUE(!element->getActionCompleted())
                        << "onTransferFinish has been previously received: " << t->getPath();
                    element->setActionCompleted();
                    element->setActionCompletedPms(e->getErrorCode());
                });

        megaApi[0]->addListener(mMtl.get());

        mMsl.reset(new NiceMock<MockSyncListener>(megaApi[0].get()));
        EXPECT_CALL(*mMsl.get(), onSyncFileStateChanged(_, _, _, _))
            .WillRepeatedly(
                [this](MegaApi*, MegaSync* sync, std::string* localPath, int newState)
                {
                    if (sync && sync->getBackupId() == getBackupId() &&
                        newState == MegaApi::STATE_SYNCED && localPath)
                    {
                        auto element = getSyncListenerTrackerByPath(*localPath);
                        if (!element || element->getActionCompleted())
                            return;

                        element->setActionCompleted();
                        element->setActionCompletedPms(API_OK);
                    }
                });
        megaApi[0]->addListener(mMsl.get());
    }

    void TearDown() override
    {
        ASSERT_TRUE(mCleanupFunctionSet)
            << getLogPrefix()
            << "(TearDown). cleanupfunction has not been properly set by "
               "calling `setCleanupFunction()`.";

        ASSERT_TRUE(!mMtl) << getLogPrefix()
                           << "(TearDown). Transfer listener has not been unregistered yet";
        ASSERT_TRUE(!mMsl) << getLogPrefix()
                           << "(TearDown). Sync listener has not been unregistered yet";
        SdkTestSyncNodesOperations::TearDown();
    }

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

    void removeTestSync()
    {
        if (mBackupId != UNDEF)
        {
            auto succeeded = removeSync(megaApi[0].get(), mBackupId);
            if (succeeded)
            {
                mBackupId = UNDEF;
            }
            ASSERT_TRUE(succeeded);
        }
    }

    void cleanDefaultListeners()
    {
        removeTestSync();

        if (mMtl)
        {
            megaApi[0]->removeListener(mMtl.get());
            mMtl.reset();
        }

        if (mMsl)
        {
            megaApi[0]->removeListener(mMsl.get());
            mMsl.reset();
        }
    }

    /**
     * @brief Build a file tree with two empty sync folders
     */
    const std::vector<NodeInfo>& getElements() const override
    {
        static const std::vector<NodeInfo> ELEMENTS{DirNodeInfo(SYNC_REMOTE_PATH)
                                                        .addChild(DirNodeInfo("dir1"))
                                                        .addChild(DirNodeInfo("dir2"))};

        return ELEMENTS;
    }

    /**
     * @brief Updates local node mtime. See MIN_ALLOW_MTIME_DIFFERENCE
     */
    void updateLocalNodeMtime(MegaHandle nodeHandle,
                              const LocalPath& path,
                              int64_t oldMtime,
                              int64_t newMtime,
                              const string& msg)
    {
        LOG_debug << "#### updateNodeMtime (" << msg << ")####";
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
    }

    /**
     * @brief Creates a new local file for test. See createTestFileInternal
     */
    void createTestFile(const string folderName,
                        const std::string commonFileName,
                        const std::string content,
                        std::chrono::time_point<fs::file_time_type::clock> customMtime,
                        const std::string& msg,
                        bool isFullUploadExpected)
    {
        LOG_debug << "#### createTestFile ( " << msg << ") `" << commonFileName << "` into `"
                  << folderName << "` with content(" << content
                  << ") and customMtime (full upload expected) ####";

        ASSERT_NO_FATAL_FAILURE(
            createTestFileInternal(fs::absolute(getLocalTmpDir() / folderName / commonFileName),
                                   content,
                                   customMtime,
                                   isFullUploadExpected));
    };

    /**
     * @brief Search nodes by fingerprint and validates the result.
     *
     * @param n The source node whose fingerprint will be used for the search. Must be a
     *          valid node with a non-null fingerprint.
     * @param excludeMtime If true, uses getNodesByFingerprintIgnoringMtime() which compares
     *                     only CRC + size + isValid, ignoring the modification time. If false,
     *                     uses getNodesByFingerprint() which compares the entire fingerprint
     * including modification time
     *
     * @param expNodeCount The expected number of nodes that should be found with the given
     *                     fingerprint.
     * @param msg A descriptive message used for logging purposes
     * @see FS_MTIME_TOLERANCE_SECS tolerance
     */
    void getNodesByFingerprint(MegaNode* n,
                               bool excludeMtime,
                               size_t expNodeCount,
                               const string& msg)
    {
        LOG_debug << "#### getNodesByFingerprint (" << msg << ") ####";
        ASSERT_TRUE(n) << "getNodesByFingerprint: Invalid node";
        ASSERT_TRUE(n->getFingerprint())
            << "Invalid fingerprint for node(" << toNodeHandle(n->getHandle()) << ")";
        auto auxfp = n->getFingerprint();

        std::unique_ptr<MegaNodeList> nl;
        if (excludeMtime)
            nl.reset(megaApi[0]->getNodesByFingerprintIgnoringMtime(auxfp));
        else
            nl.reset(megaApi[0]->getNodesByFingerprint(auxfp));

        ASSERT_EQ(nl->size(), expNodeCount)
            << "getNodesByFingerprint. " << msg << " Unexpected node count";
    }

    /**
     * @brief Returns the backup MegaNode
     */
    std::shared_ptr<MegaNode> getBackupNode()
    {
        std::unique_ptr<MegaSync> backupSync(megaApi[0]->getSyncByBackupId(getBackupId()));
        if (!backupSync)
        {
            LOG_err << "Cannot get backup sync";
            return nullptr;
        }

        std::unique_ptr<MegaNode> backupNode(
            megaApi[0]->getNodeByHandle(backupSync->getMegaHandle()));
        if (!backupNode)
        {
            LOG_err << "Cannot get backup sync node";
            return nullptr;
        }
        return backupNode;
    }

    /**
     * @brief Retrieves test folder nodes and their first-level child file nodes.
     *
     * This function locates a set of folders under a given backup node (`backupNode`),
     * as specified by the list of folder names in `folderNames`. For each folder found,
     * it retrieves a file node with the common name `commonFileName` contained directly
     * within that folder. The resulting folder and file nodes are stored in the
     * `folderNodes` and `fileNodes` vectors, respectively.
     *
     * This function assumes a single-level hierarchy: folders exist directly under
     * the backup node, and files exist directly inside those folders.
     *
     * @param backupNode The backup node under which the folders are located.
     * @param folderNames A list of the folder names.
     * @param folderNodes Vector to store the retrieved folder nodes.
     * @param fileNodes Vector to store the retrieved file nodes.
     * @param commonFileName The name of the common file expected inside each folder.
     * @param msg A descriptive message used for logging purposes.
     */
    void
        getTestFolderNodesAndFirstLevelChildren(std::shared_ptr<MegaNode> backupNode,
                                                const std::vector<string>& folderNames,
                                                std::vector<std::unique_ptr<MegaNode>>& folderNodes,
                                                std::vector<std::unique_ptr<MegaNode>>& fileNodes,
                                                const std::string& commonFileName,
                                                const std::string& msg)
    {
        LOG_debug << "#### getTestFolderNodesAndFirstLevelChildren (" << msg << ") ####";
        folderNodes.clear();
        fileNodes.clear();

        for (size_t i = 0; i < folderNames.size(); ++i)
        {
            std::unique_ptr<MegaNode> folderNode(
                megaApi[0]->getChildNodeOfType(backupNode.get(),
                                               folderNames.at(i).c_str(),
                                               FOLDERNODE));

            ASSERT_TRUE(folderNode) << msg << "Cannot get folderNode(" << folderNames.at(i) << ")";

            std::unique_ptr<MegaNode> fileNode(
                megaApi[0]->getChildNodeOfType(folderNode.get(), commonFileName.c_str(), FILENODE));

            ASSERT_TRUE(fileNode) << msg << "Can not get fileNode(" << commonFileName
                                  << ") which is inside " << folderNames.at(i);

            folderNodes.emplace_back(std::move(folderNode));
            fileNodes.emplace_back(std::move(fileNode));
        }
    }

    /**
     * @brief Gets the absolute LocalPath of a localtest file.
     *
     * @param folderName The name of the parent folder of file.
     * @param fileName The name of the file for which to construct the absolute path.
     * @return A LocalPath object representing the absolute path to the specified test file.
     */
    LocalPath getTestFileAbsolutePath(const std::string& folderName, const std::string& fileName)
    {
        return LocalPath::fromAbsolutePath(
            fs::absolute(getLocalTmpDir() / folderName / fileName).u8string());
    }
};

/**
 * @test SdkTestSyncUploadsOperations.BasicFileUpload
 *
 * 1. Create a new local file inside sync directory `dir2`.
 * 2. Wait for sync (sync engine must upload file to the cloud).
 * 3. Verify that local and remote models match
 */
TEST_F(SdkTestSyncUploadsOperations, BasicFileUpload)
{
    const auto cleanup = setCleanupFunction();
    auto mtime = fs::file_time_type::clock::now();
    LOG_err << "BasicFileUpload (TC1) create `file1`";
    ASSERT_NO_FATAL_FAILURE(createTestFile("dir1", "file1", "abcde", mtime, "CF1", true));
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());
}

/**
 * @test SdkTestSyncUploadsOperations.DuplicatedFilesUpload
 *
 * 1. Create a new local file `file1` in `dir1` with given content and mtime.
 *    - Expect a full upload (transfer started and finished).
 * 2. Create a new local file `file1` in `dir2` with the same content and mtime.
 *    - Expect a remote copy (no transfer started).
 * 3. Verify that local and remote models match
 */
TEST_F(SdkTestSyncUploadsOperations, DuplicatedFilesUpload)
{
    const auto cleanup = setCleanupFunction();
    auto mtime = fs::file_time_type::clock::now();
    ASSERT_NO_FATAL_FAILURE(createTestFile("dir1", "file1", "abcde", mtime, "CF1", true));
    ASSERT_NO_FATAL_FAILURE(createTestFile("dir2", "file1", "abcde", mtime, "CF2", false));
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());
}

/**
 * @test SdkTestSyncUploadsOperations.DuplicatedFilesUploadDifferentMtime
 *
 * 1. Create a new local file `file1` in `dir1` with given content and mtime `mt1`.
 *    - Expect a full upload.
 * 2. Create the same file `file1` in `dir2` with same content but different mtime `mt2`.
 *    - Expect a remote copy since fingerprints differs in mtime only, and MAC matches.
 * 3. Verify that local and remote models match
 */
TEST_F(SdkTestSyncUploadsOperations, DuplicatedFilesUploadDifferentMtime)
{
    const auto cleanup = setCleanupFunction();
    auto mtime1 = fs::file_time_type::clock::now();
    auto mtime2 =
        mtime1 + std::chrono::seconds(
                     MIN_ALLOW_MTIME_DIFFERENCE); // See MIN_ALLOW_MTIME_DIFFERENCE definition
    ASSERT_NO_FATAL_FAILURE(createTestFile("dir1", "file1", "abcde", mtime1, "CF1", true));
    ASSERT_NO_FATAL_FAILURE(createTestFile("dir2", "file1", "abcde", mtime2, "CF2", false));
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());
}

/**
 * @brief SdkTestSyncUploadsOperations.MultimediaFileUpload
 *
 * Test the metadata and thumbnails from a synced video.
 *
 */
#if !defined(USE_FREEIMAGE) && !defined(USE_MEDIAINFO)
TEST_F(SdkTestSyncUploadsOperations, DISABLED_MultimediaFileUpload)
#else
TEST_F(SdkTestSyncUploadsOperations, MultimediaFileUpload)
#endif
{
    const auto cleanup = setCleanupFunction();
    static const string VIDEO_FILE = "sample_video.mp4";
    static const std::string logPre = getLogPrefix();
    LOG_verbose << logPre << "Upload a multimedia file in a sync";

    // Get file in the sync path to be uploaded by the sync.
    ASSERT_TRUE(getFileFromArtifactory("test-data/" + VIDEO_FILE,
                                       fs::absolute(getLocalTmpDir() / VIDEO_FILE)));

    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

    auto uploadedNode = getNodeByPath(SYNC_REMOTE_PATH + "/" + VIDEO_FILE);
    ASSERT_TRUE(uploadedNode);
#ifdef USE_MEDIAINFO
    static constexpr int VIDEO_FILE_DURATION_SECS{5};
    static constexpr int VIDEO_FILE_HEIGHT_PX{360};
    static constexpr int VIDEO_FILE_WIDTH_PX{640};
    static constexpr int AVC1_FORMAT{887}; // ID from MediaInfo

    ASSERT_EQ(uploadedNode->getDuration(), VIDEO_FILE_DURATION_SECS)
        << "Duration is not correct or unavailable.";
    ASSERT_EQ(uploadedNode->getHeight(), VIDEO_FILE_HEIGHT_PX)
        << "Height is not correct or unavailable.";
    ASSERT_EQ(uploadedNode->getWidth(), VIDEO_FILE_WIDTH_PX)
        << "Width ID is not correct or unavailable.";
    ASSERT_EQ(uploadedNode->getVideocodecid(), AVC1_FORMAT)
        << "Codec ID is not correct or unavailable.";
#endif
#ifdef USE_FREEIMAGE
    ASSERT_TRUE(uploadedNode->hasThumbnail())
        << "Thumbnail is not available for the uploaded node.";
#endif
}

/**
 * @test SdkTestSyncUploadsOperations.getnodesByFingerprintNoMtime
 *
 * 1. Create two files with identical content but different mtimes in separate directories.
 *    - File `file1` in `dir1` with mtime `mt1`
 *    - File `file1` in `dir2` with mtime `mt2` (differs by MIN_ALLOW_MTIME_DIFFERENCE)
 * 2. Get nodes by fingerprint with and without mtime
 * 3. Update the mtime of `file1` to match mtime of `file2`.
 * 4. Get nodes by fingerprint with and without mtime
 * 5. Verify that local and remote models match
 */
TEST_F(SdkTestSyncUploadsOperations, getnodesByFingerprintNoMtime)
{
    const auto cleanup = setCleanupFunction();
    auto backupNode = getBackupNode();
    ASSERT_TRUE(backupNode) << "Cannot get backup sync node";

    const std::vector<string> folderNames{"dir1", "dir2"};
    const std::string commonContent{"abcde"};
    const std::string commonFileName{"file1"};
    std::vector<std::unique_ptr<MegaNode>> folderNodes;
    std::vector<std::unique_ptr<MegaNode>> fileNodes;

    const auto mtime1 = fs::file_time_type::clock::now();
    const auto mtime2 =
        mtime1 + std::chrono::seconds(
                     MIN_ALLOW_MTIME_DIFFERENCE); // See MIN_ALLOW_MTIME_DIFFERENCE definition
    const std::vector<std::chrono::time_point<fs::file_time_type::clock>> mtimes{mtime1, mtime2};

    ASSERT_NO_FATAL_FAILURE(createTestFile(folderNames.at(0),
                                           commonFileName,
                                           commonContent,
                                           mtimes.at(0),
                                           "CF1",
                                           true));

    ASSERT_NO_FATAL_FAILURE(createTestFile(folderNames.at(1),
                                           commonFileName,
                                           commonContent,
                                           mtimes.at(1),
                                           "CF2",
                                           false));

    ASSERT_NO_FATAL_FAILURE(getTestFolderNodesAndFirstLevelChildren(backupNode,
                                                                    folderNames,
                                                                    folderNodes,
                                                                    fileNodes,
                                                                    commonFileName,
                                                                    "(GN1)"));

    ASSERT_NO_FATAL_FAILURE(getNodesByFingerprint(fileNodes.at(0).get(),
                                                  false /*excludeMtime*/,
                                                  1 /*expNumNodes*/,
                                                  "FP1"));

    ASSERT_NO_FATAL_FAILURE(getNodesByFingerprint(fileNodes.at(0).get(),
                                                  true /*excludeMtime*/,
                                                  fileNodes.size() /*expNumNodes*/,
                                                  "FP2"));

    ASSERT_NO_FATAL_FAILURE(getNodesByFingerprint(fileNodes.at(1).get(),
                                                  false /*excludeMtime*/,
                                                  1 /*expNumNodes*/,
                                                  "FP3"));

    ASSERT_NO_FATAL_FAILURE(getNodesByFingerprint(fileNodes.at(1).get(),
                                                  true /*excludeMtime*/,
                                                  fileNodes.size() /*expNumNodes*/,
                                                  "FP4"));

    updateLocalNodeMtime(fileNodes.at(0)->getHandle(),
                         getTestFileAbsolutePath(folderNames.at(0), commonFileName),
                         fileNodes.at(0)->getModificationTime(), /*oldMtime*/
                         fileNodes.at(1)->getModificationTime(), /*newMtime*/
                         "MT1");

    ASSERT_NO_FATAL_FAILURE(getTestFolderNodesAndFirstLevelChildren(backupNode,
                                                                    folderNames,
                                                                    folderNodes,
                                                                    fileNodes,
                                                                    commonFileName,
                                                                    "(GN2)"));

    ASSERT_NO_FATAL_FAILURE(getNodesByFingerprint(fileNodes.at(0).get(),
                                                  false /*excludeMtime*/,
                                                  fileNodes.size() /*expNumNodes*/,
                                                  "FP5"));
    ASSERT_NO_FATAL_FAILURE(getNodesByFingerprint(fileNodes.at(0).get(),
                                                  true /*excludeMtime*/,
                                                  fileNodes.size() /*expNumNodes*/,
                                                  "FP6"));
    ASSERT_NO_FATAL_FAILURE(getNodesByFingerprint(fileNodes.at(1).get(),
                                                  false /*excludeMtime*/,
                                                  fileNodes.size() /*expNumNodes*/,
                                                  "FP7"));
    ASSERT_NO_FATAL_FAILURE(getNodesByFingerprint(fileNodes.at(1).get(),
                                                  true /*excludeMtime*/,
                                                  fileNodes.size() /*expNumNodes*/,
                                                  "FP8"));
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());
}

/**
 * @test SdkTestSyncUploadsOperations.updateLocalNodeMtime
 *
 * 1. Create a new local file `file1` in `dir1` with given content and mtime (now).
 *    - Expect a full upload.
 * 2. Update the mtime of `file1`.
 * 3. Verify that local and remote models match
 */
TEST_F(SdkTestSyncUploadsOperations, updateLocalNodeMtime)
{
    const auto cleanup = setCleanupFunction();
    auto backupNode = getBackupNode();
    ASSERT_TRUE(backupNode) << "Cannot get backup sync node";

    const std::vector<string> folderNames{"dir1"};
    const std::string commonFileName{"file1"};
    std::vector<std::unique_ptr<MegaNode>> folderNodes;
    std::vector<std::unique_ptr<MegaNode>> fileNodes;

    ASSERT_NO_FATAL_FAILURE(createTestFile(folderNames.at(0),
                                           commonFileName,
                                           "abcde",
                                           fs::file_time_type::clock::now(),
                                           "CF1",
                                           true));

    ASSERT_NO_FATAL_FAILURE(getTestFolderNodesAndFirstLevelChildren(backupNode,
                                                                    folderNames,
                                                                    folderNodes,
                                                                    fileNodes,
                                                                    commonFileName,
                                                                    "(GN1)"));

    updateLocalNodeMtime(fileNodes.at(0)->getHandle(),
                         getTestFileAbsolutePath(folderNames.at(0), commonFileName),
                         fileNodes.at(0)->getModificationTime(), /*oldMtime*/
                         fileNodes.at(0)->getModificationTime() + 100, /*newMtime*/
                         "MT1");

    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());
}

/**
 * @test SdkTestSyncUploadsOperations.CloneNodeWithDifferentMtime
 *
 * This test validates the clone node mechanism when a file with different mtime
 * is moved into the sync. The clone should be found via NODE_COMP_DIFFERS_MTIME.
 *
 * 1. Create a random file (50MB) outside the sync local root (thread-safe path).
 * 2. Upload it manually (not through sync) to a unique remote folder outside sync.
 * 3. Change the mtime of the local file.
 * 4. Set up expectations that no full transfer will occur.
 * 5. Move/rename the file into the sync local root with a different name (preserving mtime).
 * 6. Wait for sync - a clone node operation should occur (NODE_COMP_DIFFERS_MTIME).
 * 7. Verify both local and remote files have the same mtime (the updated one from step 3).
 */
TEST_F(SdkTestSyncUploadsOperations, CloneNodeWithDifferentMtime)
{
    static const auto logPre = getLogPrefix();
    const auto cleanup = setCleanupFunction();

    const std::string originalFileName = "original_file_outside_sync.dat";
    const std::string clonedFileName = "cloned_file_inside_sync.dat";
    const size_t fileSize = 50 * 1024 * 1024; // 50MB

    LOG_debug << logPre << "1. Prepare unique remote folder outside sync";
    const std::string threadSuffix = "_" + getThisThreadIdStr();
    const std::string uniqueRemoteFolderName = "clone_mtime_test_folder" + threadSuffix;

    const fs::path outsideLocalDir =
        getLocalTmpDir().parent_path() / ("clone_mtime_test_dir" + threadSuffix);
    LocalTempDir outsideDirCleanup(outsideLocalDir);
    const fs::path outsideLocalPath = outsideLocalDir / originalFileName;

    LOG_debug << logPre << "1b. Creating random file outside sync at: " << outsideLocalPath;
    createRandomFile(outsideLocalPath, fileSize);

    LOG_debug << logPre << "2. Creating unique remote folder to manually upload the file: "
              << uniqueRemoteFolderName;
    auto backupNode = getBackupNode();
    ASSERT_TRUE(backupNode) << "Cannot get backup sync node";

    std::unique_ptr<MegaNode> rootTestNode(
        megaApi[0]->getNodeByHandle(backupNode->getParentHandle()));
    ASSERT_TRUE(rootTestNode) << "Cannot get parent node of sync remote root";

    auto uploadTargetHandle = createFolder(0, uniqueRemoteFolderName.c_str(), rootTestNode.get());
    ASSERT_NE(uploadTargetHandle, UNDEF) << "Failed to create unique remote folder";

    std::unique_ptr<MegaNode> uploadTargetNode(megaApi[0]->getNodeByHandle(uploadTargetHandle));
    ASSERT_TRUE(uploadTargetNode) << "Cannot get created remote folder node";

    LOG_debug << logPre << "2b. Uploading file manually to cloud at the unique remote folder: "
              << uniqueRemoteFolderName;
    auto uploadedNode = uploadFile(megaApi[0].get(), outsideLocalPath, uploadTargetNode.get());
    ASSERT_TRUE(uploadedNode) << "Manual upload failed";

    const int64_t originalMtime = uploadedNode->getModificationTime();
    LOG_debug << logPre << "2c. Original uploaded file mtime: " << originalMtime;

    const m_time_t newMtimeTimeT = m_time(nullptr) + MIN_ALLOW_MTIME_DIFFERENCE;
    LOG_debug << logPre << "3. Changing local file mtime to: " << newMtimeTimeT;
    ASSERT_TRUE(mFsAccess->setmtimelocal(LocalPath::fromAbsolutePath(outsideLocalPath.u8string()),
                                         newMtimeTimeT))
        << "Failed to set mtime on file outside sync";

    const fs::path insideSyncPath = fs::absolute(getLocalTmpDir() / clonedFileName);
    LOG_debug << logPre << "4. Moving file into sync and waiting for sync (no transfer expected)";
    ASSERT_NO_FATAL_FAILURE(moveFileIntoSyncAndVerify(outsideLocalPath,
                                                      insideSyncPath,
                                                      false /*isFullUploadExpected*/,
                                                      newMtimeTimeT /*expectedMtimeAfterMove*/));

    LOG_debug << logPre << "5. Verifying mtime of cloned node";
    std::unique_ptr<MegaNode> clonedNode(
        megaApi[0]->getChildNodeOfType(backupNode.get(), clonedFileName.c_str(), FILENODE));
    ASSERT_TRUE(clonedNode) << "Cloned node not found in cloud";

    ASSERT_EQ(clonedNode->getModificationTime(), newMtimeTimeT)
        << "Cloned remote node mtime should match the updated local mtime";

    LOG_debug << logPre << "6. Verifying mtime of local file";
    {
        auto [getMtimeSucceeded, currentLocalMtimeTimeT] =
            mFsAccess->getmtimelocal(LocalPath::fromAbsolutePath(insideSyncPath.u8string()));
        ASSERT_TRUE(getMtimeSucceeded) << "Failed to get local file mtime";
        ASSERT_EQ(currentLocalMtimeTimeT, newMtimeTimeT)
            << "Local file mtime should still be the updated value";
    }

    LOG_debug << logPre << "7. Verifying mtime of original uploaded node";
    {
        std::unique_ptr<MegaNode> refreshedUploadedNode(
            megaApi[0]->getNodeByHandle(uploadedNode->getHandle()));
        ASSERT_TRUE(refreshedUploadedNode) << "Cannot get refreshed original uploaded node";

        ASSERT_EQ(refreshedUploadedNode->getModificationTime(), originalMtime)
            << "Original uploaded node mtime should remain unchanged";
    }

    LOG_debug << logPre << "8. Cleanup: removing remote test folder";
    ASSERT_EQ(API_OK, doDeleteNode(0, uploadTargetNode.get()))
        << "Failed to cleanup remote test folder";

    LOG_debug << logPre << "Test completed successfully";
}
#endif // ENABLE_SYNC
