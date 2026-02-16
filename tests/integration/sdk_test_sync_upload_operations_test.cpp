/**
 * @file
 * @brief This file is expected to contain tests involving sync upload operations
 * e.g., what happens when a file is duplicated inside a sync.
 */

#ifdef ENABLE_SYNC

#include "integration_test_utils.h"
#include "mega/testhooks.h"
#include "mega/utils.h"
#include "megautils.h"
#include "mock_listeners.h"
#include "sdk_test_utils.h"
#include "SdkTestSyncNodesOperations.h"

#include <gmock/gmock.h>

using namespace sdk_test;
using namespace testing;

namespace
{
struct ScopedLegacyBuggySparseCrcHook
{
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    std::function<void(bool&)> mPrev;
    bool mEnabled = false;

    explicit ScopedLegacyBuggySparseCrcHook(const bool enabled):
        mPrev{::mega::globalMegaTestHooks.onHookFileFingerprintUseLegacyBuggySparseCrc},
        mEnabled{enabled}
    {
        setEnabled(mEnabled);
    }

    ~ScopedLegacyBuggySparseCrcHook()
    {
        ::mega::globalMegaTestHooks.onHookFileFingerprintUseLegacyBuggySparseCrc = std::move(mPrev);
    }

    void setEnabled(const bool enabled)
    {
        mEnabled = enabled;
        ::mega::globalMegaTestHooks.onHookFileFingerprintUseLegacyBuggySparseCrc =
            [enabled](bool& flag)
        {
            flag = enabled;
        };
    }
#else
    explicit ScopedLegacyBuggySparseCrcHook(const bool) {}

    void setEnabled(const bool) {}
#endif
};
} // namespace

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
     * @param noTransferTimeout Timeout used when no transfer is expected.
     */
    void waitForSyncAndVerifyTransfer(
        const fs::path& localFilePathAbs,
        std::shared_ptr<SyncUploadOperationsTracker> st,
        std::shared_ptr<SyncUploadOperationsTransferTracker> tt,
        const bool isFullUploadExpected,
        const std::chrono::milliseconds noTransferTimeout = std::chrono::seconds(30))
    {
        auto [syncStatus, syncErrCode] = st->waitForCompletion(COMMON_TIMEOUT);
        ASSERT_TRUE(syncStatus == std::future_status::ready)
            << "Sync state change not received for: " << localFilePathAbs;
        ASSERT_EQ(syncErrCode, API_OK) << "Sync completed with error for: " << localFilePathAbs;

        ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

        const auto transferTimeout = isFullUploadExpected ? COMMON_TIMEOUT : noTransferTimeout;
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
        moveIntoSyncAndVerifyImpl(targetPathInSync,
                                  isFullUploadExpected,
                                  expectedMtimeAfterMove,
                                  [&]()
                                  {
                                      std::error_code ec;
                                      fs::rename(sourcePath, targetPathInSync, ec);
                                      if (ec)
                                      {
                                          LOG_err << "Failed to move file from "
                                                  << path_u8string(sourcePath) << " to "
                                                  << path_u8string(targetPathInSync)
                                                  << ". Error: " << ec.message();
                                      }
                                      return ec;
                                  });
    }

    void moveLocalTempFileIntoSyncAndVerify(
        const std::shared_ptr<sdk_test::LocalTempFile>& file,
        const fs::path& targetPathInSync,
        const bool isFullUploadExpected,
        std::optional<m_time_t> expectedMtimeAfterMove = std::nullopt)
    {
        ASSERT_TRUE(file);
        moveIntoSyncAndVerifyImpl(targetPathInSync,
                                  isFullUploadExpected,
                                  expectedMtimeAfterMove,
                                  [&]()
                                  {
                                      return file->move(targetPathInSync);
                                  });
    }

private:
    template<typename MoveFn>
    void moveIntoSyncAndVerifyImpl(const fs::path& targetPathInSync,
                                   const bool isFullUploadExpected,
                                   const std::optional<m_time_t>& expectedMtimeAfterMove,
                                   MoveFn&& moveFn)
    {
        static const std::string logPre{"moveIntoSyncAndVerifyImpl: "};
        ASSERT_TRUE(mMtl) << logPre << "Invalid transfer listener";

        auto tt = addTransferListenerTracker(targetPathInSync.string());
        ASSERT_TRUE(tt) << logPre
                        << "Cannot add TransferListenerTracker for: " << targetPathInSync.string();
        auto st = addSyncListenerTracker(targetPathInSync.string());
        ASSERT_TRUE(st) << logPre
                        << "Cannot add SyncListenerTracker for: " << targetPathInSync.string();

        const auto ec = moveFn();
        ASSERT_FALSE(ec) << logPre << "Move into sync failed for: " << targetPathInSync;

        if (expectedMtimeAfterMove.has_value())
        {
            auto [getMtimeOk, movedFileMtime] = mFsAccess->getmtimelocal(
                LocalPath::fromAbsolutePath(path_u8string(targetPathInSync)));
            ASSERT_TRUE(getMtimeOk)
                << logPre << "Failed to get mtime of moved file: " << targetPathInSync;
            ASSERT_EQ(movedFileMtime, expectedMtimeAfterMove.value())
                << logPre << "Move should preserve mtime for: " << targetPathInSync;
        }

        ASSERT_NO_FATAL_FAILURE(
            waitForSyncAndVerifyTransfer(targetPathInSync, st, tt, isFullUploadExpected));
    }

protected:
    enum class CxfMtimeDirection
    {
        Increase,
        Decrease
    };

    void runAsyncMacComputationForCxfCase(const std::string& logPre,
                                          const CxfMtimeDirection direction)
    {
        auto cleanup = setCleanupFunction();
        LOG_debug << logPre << "Test started";

        static constexpr size_t FILE_SIZE =
            (5 * 1024 * 1024) + (2 * 1024) + 3; // 5MB + 2KB + 3 bytes
        const fs::path testFilePath = fs::absolute(getLocalTmpDir() / "test_file_cxf.dat");

        LOG_debug << logPre << "1. Creating test file";
        {
            auto st = addSyncListenerTracker(testFilePath.string());
            ASSERT_TRUE(st);
            auto localFile = std::make_shared<sdk_test::LocalTempFile>(testFilePath, FILE_SIZE);
            mLocalFiles.emplace_back(localFile);
            auto [status, errCode] = st->waitForCompletion(COMMON_TIMEOUT);
            ASSERT_EQ(status, std::future_status::ready) << "File sync timed out";
            ASSERT_EQ(errCode, API_OK) << "File sync failed";
        }

        ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

        auto [getMtimeOk, originalMtime] =
            mFsAccess->getmtimelocal(LocalPath::fromAbsolutePath(path_u8string(testFilePath)));
        ASSERT_TRUE(getMtimeOk) << "Failed to get original mtime";

        LOG_debug << logPre << "2. Removing sync (simulating logout without file deletion)";
        removeTestSync();

        LOG_debug << logPre << "3. Updating file mtime";
        const m_time_t newMtime = direction == CxfMtimeDirection::Increase ?
                                      originalMtime + MIN_ALLOW_MTIME_DIFFERENCE :
                                      originalMtime - MIN_ALLOW_MTIME_DIFFERENCE;
        ASSERT_TRUE(
            mFsAccess->setmtimelocal(LocalPath::fromAbsolutePath(path_u8string(testFilePath)),
                                     newMtime));

        LOG_debug << logPre << "4. Re-adding sync (SRT_CXF case - no LocalNodes exist)";
        ASSERT_NO_FATAL_FAILURE(
            initiateSync(getLocalTmpDirU8string(), SYNC_REMOTE_PATH, mBackupId));

        LOG_debug << logPre << "5. Waiting for sync to complete with async MAC recomputation";
        {
            auto st = addSyncListenerTracker(testFilePath.string());
            ASSERT_TRUE(st);
            auto [status, errCode] = st->waitForCompletion(COMMON_TIMEOUT);
            ASSERT_EQ(status, std::future_status::ready) << "File sync timed out";
            ASSERT_EQ(errCode, API_OK) << "File sync failed";
        }

        auto backupNode = getBackupNode();
        ASSERT_TRUE(backupNode) << "Backup node not found";

        std::unique_ptr<MegaNode> cloudNode(
            megaApi[0]->getChildNodeOfType(backupNode.get(), "test_file_cxf.dat", FILENODE));
        ASSERT_TRUE(cloudNode) << "Cloud node not found after re-sync";

        auto cloudNodeMtime = cloudNode->getModificationTime();
        if (direction == CxfMtimeDirection::Increase)
        {
            ASSERT_EQ(cloudNodeMtime, newMtime)
                << "Cloud node mtime should match the updated local mtime after CXF sync";
        }
        else
        {
            auto [getMtimeLocalOk, localMtime] =
                mFsAccess->getmtimelocal(LocalPath::fromAbsolutePath(path_u8string(testFilePath)));
            ASSERT_TRUE(getMtimeLocalOk) << "Failed to get local mtime after CXF resync";
            ASSERT_GT(cloudNodeMtime, newMtime) << "Cloud node mtime should be newer than the last "
                                                   "local changed mtime after CXF sync";
            ASSERT_EQ(localMtime, cloudNodeMtime)
                << "Local file mtime should match cloud after CXF resync when cloud is newer";
        }

        LOG_debug << logPre << "Test completed successfully";
    }

    void runAsyncMacNonBlockingScenario(const std::string& logPre,
                                        const std::string& namePrefix,
                                        const bool startFromCxf)
    {
        // File sizes shared by CSF and CXF variants.
        static constexpr size_t SMALL_FILE_SIZE = (5 * 1024 * 1024) + (126 * 1024) + 17; // ~5MB
        static constexpr size_t LARGE_FILE_SIZE = (100 * 1024 * 1024) + (212 * 1024) + 2; // ~100MB

        auto absPath = [&](const std::string& filename)
        {
            return fs::absolute(getLocalTmpDir() / filename);
        };

        const fs::path smallFile1Path = absPath(namePrefix + "small_file1.dat");
        const fs::path smallFile2Path = absPath(namePrefix + "small_file2.dat");
        const fs::path largeFilePath = absPath(namePrefix + "large_file.dat");

        LOG_debug << logPre << "1. Creating test files (2 small, 1 large)";

        auto createFileAndSync =
            [&](const fs::path& path, const size_t size, const std::string& label)
        {
            auto st = mSyncListenerTrackers.add(path.string());
            ASSERT_TRUE(st) << logPre << "Cannot add SyncListenerTracker for " << label;
            auto localFile = std::make_shared<sdk_test::LocalTempFile>(path, size);
            mLocalFiles.emplace_back(localFile);
            auto [status, errCode] = st->waitForCompletion(COMMON_TIMEOUT);
            ASSERT_EQ(status, std::future_status::ready) << logPre << label << " sync timed out";
            ASSERT_EQ(errCode, API_OK) << logPre << label << " sync failed";
        };

        ASSERT_NO_FATAL_FAILURE(createFileAndSync(smallFile1Path, SMALL_FILE_SIZE, "Small file 1"));
        ASSERT_NO_FATAL_FAILURE(createFileAndSync(smallFile2Path, SMALL_FILE_SIZE, "Small file 2"));
        ASSERT_NO_FATAL_FAILURE(createFileAndSync(largeFilePath, LARGE_FILE_SIZE, "Large file"));

        ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());
        LOG_debug << logPre << "2. All files synced, now updating mtimes";

        const m_time_t newMtime = m_time(nullptr) + MIN_ALLOW_MTIME_DIFFERENCE;

        std::shared_ptr<SyncUploadOperationsTracker> stSmall1;
        std::shared_ptr<SyncUploadOperationsTracker> stSmall2;
        std::shared_ptr<SyncUploadOperationsTracker> stLarge;

        auto addSyncListeners = [&]()
        {
            stSmall1 = mSyncListenerTrackers.add(smallFile1Path.string());
            stSmall2 = mSyncListenerTrackers.add(smallFile2Path.string());
            stLarge = mSyncListenerTrackers.add(largeFilePath.string());
            ASSERT_TRUE(stSmall1 && stSmall2 && stLarge);
        };

        if (!startFromCxf)
        {
            ASSERT_NO_FATAL_FAILURE(addSyncListeners());
        }
        else
        {
            LOG_debug << logPre << "2b. Removing sync (CXF) before updating mtimes";
            removeTestSync();
        }

        LOG_debug << logPre << "3. Updating mtimes for all 3 files";
        ASSERT_TRUE(
            mFsAccess->setmtimelocal(LocalPath::fromAbsolutePath(path_u8string(smallFile1Path)),
                                     newMtime));
        ASSERT_TRUE(
            mFsAccess->setmtimelocal(LocalPath::fromAbsolutePath(path_u8string(smallFile2Path)),
                                     newMtime));
        ASSERT_TRUE(
            mFsAccess->setmtimelocal(LocalPath::fromAbsolutePath(path_u8string(largeFilePath)),
                                     newMtime));

        if (startFromCxf)
        {
            LOG_debug << logPre << "3b. Re-adding sync (CXF path upon sync re-addition)";
            ASSERT_NO_FATAL_FAILURE(
                initiateSync(getLocalTmpDirU8string(), SYNC_REMOTE_PATH, mBackupId));

            ASSERT_NO_FATAL_FAILURE(addSyncListeners());
        }

        LOG_debug << logPre << "4. Waiting for all files to sync and tracking completion order";

        using clock = std::chrono::steady_clock;
        std::atomic<clock::time_point> small1CompleteTime{clock::time_point::max()};
        std::atomic<clock::time_point> small2CompleteTime{clock::time_point::max()};
        std::atomic<clock::time_point> largeCompleteTime{clock::time_point::max()};
        std::atomic<bool> small1Done{false};
        std::atomic<bool> small2Done{false};
        std::atomic<bool> largeDone{false};

        auto waitAndStamp = [&](std::shared_ptr<SyncUploadOperationsTracker> st,
                                std::atomic<clock::time_point>& ts,
                                std::atomic<bool>& done,
                                std::chrono::seconds timeout)
        {
            return std::thread(
                [&, st = std::move(st), timeout]()
                {
                    auto [status, err] = st->waitForCompletion(timeout);
                    if (status == std::future_status::ready && err == API_OK)
                    {
                        ts = clock::now();
                        done = true;
                    }
                });
        };

        auto waitLarge =
            waitAndStamp(stLarge, largeCompleteTime, largeDone, std::chrono::seconds(180));
        auto waitSmall1 =
            waitAndStamp(stSmall1, small1CompleteTime, small1Done, std::chrono::seconds(60));
        auto waitSmall2 =
            waitAndStamp(stSmall2, small2CompleteTime, small2Done, std::chrono::seconds(60));

        waitSmall1.join();
        waitSmall2.join();

        // By the time small files are done, the large one should not have finished yet in the
        // non-blocking scenario. If it has, we'll catch it in the ordering assertions below.
        EXPECT_FALSE(largeDone.load()) << logPre << "Large file completed before small files";

        waitLarge.join();

        ASSERT_TRUE(small1Done) << logPre << "Small file 1 mtime sync failed or timed out";
        ASSERT_TRUE(small2Done) << logPre << "Small file 2 mtime sync failed or timed out";
        ASSERT_TRUE(largeDone) << logPre << "Large file mtime sync failed or timed out";

        LOG_debug << logPre << "6. Verifying completion order";

        auto small1Time = small1CompleteTime.load();
        auto small2Time = small2CompleteTime.load();
        auto largeTime = largeCompleteTime.load();

        EXPECT_LT(small1Time, largeTime)
            << logPre << "Small file 1 should complete before large file";
        EXPECT_LT(small2Time, largeTime)
            << logPre << "Small file 2 should complete before large file";

        ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());
        LOG_debug << logPre << "Test completed successfully";
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
            path_u8string(fs::absolute(getLocalTmpDir() / folderName / fileName)));
    }
};

/**
 * @test
 * SdkTestSyncUploadsOperations.CrcOnlyMismatchBugFixUpdatesRemoteFingerprintWithoutTransfer_CSF
 *
 * 1. Enable legacy (buggy) sparse CRC sampling via debug hook.
 * 2. Create a couple of large random files outside the sync and move them in -> full upload
 * expected.
 * 3. Disable legacy hook, trigger a sync rescan to recompute fingerprint.
 * 4. Verify cloud nodes get their fingerprint updated (CRC corrected) without transfers.
 */
TEST_F(SdkTestSyncUploadsOperations,
       CrcOnlyMismatchBugFixUpdatesRemoteFingerprintWithoutTransfer_CSF)
{
#ifndef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED";
#else
    const auto cleanup = setCleanupFunction();

    static const std::string logPre{
        "CrcOnlyMismatchBugFixUpdatesRemoteFingerprintWithoutTransfer_CSF: "};
    const auto threadSuffix = "_" + getThisThreadIdStr();

    ScopedLegacyBuggySparseCrcHook legacyHook{true};

    // Sizes chosen to exceed the historical 32-bit sparse CRC offset overflow threshold (~33MB),
    // while keeping uploads fast enough for integration environments.
    static constexpr size_t FILE1_SIZE = 40 * 1024 * 1024; // 40MB
    static constexpr size_t FILE2_SIZE = 90 * 1024 * 1024; // 90MB

    const fs::path file1Path =
        fs::absolute(getLocalTmpDir() / ("crc_bug_csf_1" + threadSuffix + ".dat"));
    const fs::path file2Path =
        fs::absolute(getLocalTmpDir() / ("crc_bug_csf_2" + threadSuffix + ".dat"));

    const fs::path outsideLocalDir =
        getLocalTmpDir().parent_path() / ("crc_bug_csf_tmp_dir" + threadSuffix);
    LocalTempDir outsideDirCleanup(outsideLocalDir);

    const fs::path outsideFile1 = outsideLocalDir / file1Path.filename();
    const fs::path outsideFile2 = outsideLocalDir / file2Path.filename();

    LOG_debug << logPre << "1. Creating two large files outside sync (avoid partial-file uploads)";
    auto localFile1 = std::make_shared<sdk_test::LocalTempFile>(outsideFile1, FILE1_SIZE);
    auto localFile2 = std::make_shared<sdk_test::LocalTempFile>(outsideFile2, FILE2_SIZE);
    mLocalFiles.emplace_back(localFile1);
    mLocalFiles.emplace_back(localFile2);

    auto moveFileIntoSyncWithLocalTempFileAndVerify =
        [&](const fs::path& targetPathInSync, const std::shared_ptr<sdk_test::LocalTempFile>& f)
    {
        ASSERT_NO_FATAL_FAILURE(moveLocalTempFileIntoSyncAndVerify(f, targetPathInSync, true));
    };

    LOG_debug << logPre << "2. Moving files into sync with legacy buggy sparse CRC enabled";
    ASSERT_NO_FATAL_FAILURE(moveFileIntoSyncWithLocalTempFileAndVerify(file1Path, localFile1));
    ASSERT_NO_FATAL_FAILURE(moveFileIntoSyncWithLocalTempFileAndVerify(file2Path, localFile2));

    LOG_debug << logPre << "3. Disabling legacy hook and rescanning sync to recompute fingerprints";
    legacyHook.setEnabled(false);
    megaApi[0]->rescanSync(getBackupId(), true);

    auto backupNode = getBackupNode();
    ASSERT_TRUE(backupNode) << logPre << "Cannot get backup node";

    auto waitForRemoteFingerprint = [&](const fs::path& localPath,
                                        const std::string& filename,
                                        std::shared_ptr<SyncUploadOperationsTransferTracker> tt)
    {
        const std::unique_ptr<char[]> expectedFp{
            megaApi[0]->getFingerprint(path_u8string(localPath).c_str())};
        ASSERT_TRUE(expectedFp) << logPre << "Cannot compute local fingerprint for: " << localPath;

        const auto deadline = std::chrono::steady_clock::now() + COMMON_TIMEOUT;
        for (;;)
        {
            std::unique_ptr<MegaNode> cloudNode(
                megaApi[0]->getChildNodeOfType(backupNode.get(), filename.c_str(), FILENODE));
            if (cloudNode && cloudNode->getFingerprint() &&
                std::string_view{cloudNode->getFingerprint()} == expectedFp.get())
            {
                break;
            }

            if (std::chrono::steady_clock::now() >= deadline)
            {
                FAIL() << logPre
                       << "Timed out waiting for remote fingerprint update for: " << localPath;
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        ASSERT_EQ(tt->transferStartCount.load(), 0)
            << logPre << "Transfer must not start for: " << localPath;
    };

    auto tt1 = addTransferListenerTracker(file1Path.string());
    auto tt2 = addTransferListenerTracker(file2Path.string());
    ASSERT_TRUE(tt1 && tt2);

    LOG_debug << logPre << "4. Verifying remote fingerprints updated without transfers";
    ASSERT_NO_FATAL_FAILURE(
        waitForRemoteFingerprint(file1Path, path_u8string(file1Path.filename()), tt1));
    ASSERT_NO_FATAL_FAILURE(
        waitForRemoteFingerprint(file2Path, path_u8string(file2Path.filename()), tt2));
#endif
}

/**
 * @test
 * SdkTestSyncUploadsOperations.CrcOnlyMismatchBugFixUpdatesRemoteFingerprintWithoutTransfer_CXF
 *
 * 1. Remove sync to start from CXF (no LocalNodes).
 * 2. Manually upload a large file with legacy (buggy) sparse CRC enabled.
 * 3. Disable legacy hook, place the same file into the sync local root, and re-add the sync.
 * 4. Verify cloud node fingerprint gets corrected without transfers.
 */
TEST_F(SdkTestSyncUploadsOperations,
       CrcOnlyMismatchBugFixUpdatesRemoteFingerprintWithoutTransfer_CXF)
{
#ifndef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED";
#else
    const auto cleanup = setCleanupFunction();

    static const std::string logPre{
        "CrcOnlyMismatchBugFixUpdatesRemoteFingerprintWithoutTransfer_CXF: "};
    const auto threadSuffix = "_" + getThisThreadIdStr();

    static constexpr size_t FILE_SIZE = 40 * 1024 * 1024; // 40MB
    const std::string fileName = "crc_bug_cxf" + threadSuffix + ".dat";

    auto backupNodeBefore = getBackupNode();
    ASSERT_TRUE(backupNodeBefore) << logPre << "Cannot get backup node";
    const MegaHandle backupHandle = backupNodeBefore->getHandle();

    LOG_debug << logPre << "1. Removing sync (CXF path upon re-addition)";
    removeTestSync();

    std::unique_ptr<MegaNode> backupNode(megaApi[0]->getNodeByHandle(backupHandle));
    ASSERT_TRUE(backupNode) << logPre << "Cannot re-acquire backup node by handle";

    const fs::path outsideLocalDir =
        getLocalTmpDir().parent_path() / ("crc_bug_cxf_tmp_dir" + threadSuffix);
    LocalTempDir outsideDirCleanup(outsideLocalDir);
    const fs::path outsideLocalPath = outsideLocalDir / fileName;

    LOG_debug << logPre << "2. Creating random file outside sync";
    createRandomFile(outsideLocalPath, FILE_SIZE);

    const m_time_t fixedMtime = m_time(nullptr) - 60;
    ASSERT_TRUE(
        mFsAccess->setmtimelocal(LocalPath::fromAbsolutePath(path_u8string(outsideLocalPath)),
                                 fixedMtime))
        << logPre << "Failed to set mtime on temp file";

    LOG_debug << logPre << "3. Manual upload with legacy buggy sparse CRC enabled";
    ScopedLegacyBuggySparseCrcHook legacyHook{true};
    auto uploadedNode = uploadFile(megaApi[0].get(), outsideLocalPath, backupNode.get());
    ASSERT_TRUE(uploadedNode) << logPre << "Manual upload failed";

    LOG_debug << logPre << "4. Disabling legacy hook and moving file into sync local root";
    legacyHook.setEnabled(false);

    const fs::path insideSyncPath = fs::absolute(getLocalTmpDir() / fileName);
    std::error_code renameError;
    fs::rename(outsideLocalPath, insideSyncPath, renameError);
    ASSERT_FALSE(renameError) << logPre
                              << "Failed to move file into sync: " << renameError.message();

    auto [getMtimeOk, movedMtime] =
        mFsAccess->getmtimelocal(LocalPath::fromAbsolutePath(path_u8string(insideSyncPath)));
    ASSERT_TRUE(getMtimeOk) << logPre << "Failed to get mtime of moved file";
    ASSERT_EQ(movedMtime, fixedMtime) << logPre << "fs::rename should preserve mtime";

    auto tt = addTransferListenerTracker(insideSyncPath.string());
    ASSERT_TRUE(tt);

    LOG_debug << logPre << "5. Re-adding sync and waiting for remote fingerprint correction";
    ASSERT_NO_FATAL_FAILURE(initiateSync(getLocalTmpDirU8string(), SYNC_REMOTE_PATH, mBackupId));

    const std::unique_ptr<char[]> expectedFp{
        megaApi[0]->getFingerprint(path_u8string(insideSyncPath).c_str())};
    ASSERT_TRUE(expectedFp) << logPre << "Cannot compute local fingerprint";

    const auto deadline = std::chrono::steady_clock::now() + COMMON_TIMEOUT;
    for (;;)
    {
        std::unique_ptr<MegaNode> cloudNode(
            megaApi[0]->getChildNodeOfType(backupNode.get(), fileName.c_str(), FILENODE));
        if (cloudNode && cloudNode->getFingerprint() &&
            std::string_view{cloudNode->getFingerprint()} == expectedFp.get())
        {
            break;
        }

        if (std::chrono::steady_clock::now() >= deadline)
        {
            FAIL() << logPre << "Timed out waiting for remote fingerprint update";
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    ASSERT_EQ(tt->transferStartCount.load(), 0) << logPre << "Transfer must not start";
#endif
}

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

    // Get the file first and move it later to ensure that it is fully uploaded at once.
    ASSERT_TRUE(getFileFromArtifactory("test-data/" + VIDEO_FILE, VIDEO_FILE));
    fs::rename(VIDEO_FILE, getLocalTmpDir() / VIDEO_FILE);

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

    LOG_debug << logPre
              << "1b. Creating random file outside sync at: " << path_u8string(outsideLocalPath);
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
    LOG_debug << logPre << "3. Changing local file mtime to: " << newMtimeTimeT << " seconds";
    ASSERT_TRUE(
        mFsAccess->setmtimelocal(LocalPath::fromAbsolutePath(path_u8string(outsideLocalPath)),
                                 newMtimeTimeT))
        << "Failed to set mtime on file outside sync";

    const fs::path insideSyncPath = fs::absolute(getLocalTmpDir() / clonedFileName);
    LOG_debug << logPre << "4. Moving file into sync and waiting for sync (no transfer expected): "
              << path_u8string(insideSyncPath);
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
            mFsAccess->getmtimelocal(LocalPath::fromAbsolutePath(path_u8string(insideSyncPath)));
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

/**
 * @brief Tests that async MAC computation doesn't block small files.
 *
 * Creates 3 files (2 small, 1 large ~10MB), syncs them, then updates mtimes for all 3.
 * Verifies that the small files complete their mtime-only sync quickly, even while
 * the large file's MAC computation may be in progress.
 *
 * This validates the non-blocking MAC computation implementation for SRT_CSF cases.
 */
TEST_F(SdkTestSyncUploadsOperations, AsyncMacComputationDoesNotBlockSmallFiles)
{
    static const std::string logPre{"AsyncMacComputationDoesNotBlockSmallFiles: "};
    auto cleanup = setCleanupFunction();
    LOG_debug << logPre << "Test started";

    ASSERT_NO_FATAL_FAILURE(runAsyncMacNonBlockingScenario(logPre, "csf_", false));
}

/**
 * @brief Tests async MAC computation does not block small files when resyncing from CXF state.
 *
 * Creates two small files and one large file, updates mtimes, removes and re-adds the sync (CXF),
 * and verifies small files complete before the large file while using the shared async MAC path.
 */
TEST_F(SdkTestSyncUploadsOperations, AsyncMacComputationDoesNotBlockSmallFilesFromCxf)
{
    static const std::string logPre{"AsyncMacComputationDoesNotBlockSmallFilesFromCxf: "};
    auto cleanup = setCleanupFunction();
    LOG_debug << logPre << "Test started";

    ASSERT_NO_FATAL_FAILURE(runAsyncMacNonBlockingScenario(logPre, "cxf_", true));
}

/**
 * @brief Tests async MAC computation for SRT_CXF case (local logout/relog).
 *
 * This exercises the scenario where LocalNodes are lost (logout/relog) but files
 * remain in sync folder. Upon re-sync, files with mtime-only differences use
 * async MAC computation as unsynced nodes are re-processed.
 *
 * The test verifies that the sync eventually completes correctly.
 */
TEST_F(SdkTestSyncUploadsOperations, AsyncMacComputationForCxfCase_LocalNewer)
{
    ASSERT_NO_FATAL_FAILURE(
        runAsyncMacComputationForCxfCase("AsyncMacComputationForCxfCase_LocalNewer: ",
                                         CxfMtimeDirection::Increase));
}

TEST_F(SdkTestSyncUploadsOperations, AsyncMacComputationForCxfCase_CloudNewer)
{
    ASSERT_NO_FATAL_FAILURE(
        runAsyncMacComputationForCxfCase("AsyncMacComputationForCxfCase_CloudNewer: ",
                                         CxfMtimeDirection::Decrease));
}

/**
 * @brief Tests MAC computation obsolescence when file is deleted during computation.
 *
 * Creates a large file, triggers mtime update to start MAC computation, then
 * deletes the file while computation may be pending. The sync should handle
 * this gracefully without errors.
 */
TEST_F(SdkTestSyncUploadsOperations, MacComputationObsolescenceOnDelete)
{
    static const std::string logPre{"MacComputationObsolescenceOnDelete: "};
    auto cleanup = setCleanupFunction();
    LOG_debug << logPre << "Test started";

    // Use a large file to increase chance of pending MAC computation
    static constexpr size_t LARGE_FILE_SIZE = 100 * 1024 * 1024; // 100MB
    const fs::path largeFilePath = fs::absolute(getLocalTmpDir() / "large_file_delete_test.dat");

    LOG_debug << logPre << "1. Creating large test file";
    {
        auto st = addSyncListenerTracker(largeFilePath.string());
        ASSERT_TRUE(st);
        auto localFile = std::make_shared<sdk_test::LocalTempFile>(largeFilePath, LARGE_FILE_SIZE);
        mLocalFiles.emplace_back(localFile);
        auto [status, errCode] = st->waitForCompletion(COMMON_TIMEOUT);
        ASSERT_EQ(status, std::future_status::ready) << "Large file sync timed out";
        ASSERT_EQ(errCode, API_OK) << "Large file sync failed";
    }

    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

    LOG_debug << logPre << "2. Updating mtime to trigger MAC computation";
    const m_time_t newMtime = m_time(nullptr) + MIN_ALLOW_MTIME_DIFFERENCE;
    ASSERT_TRUE(mFsAccess->setmtimelocal(LocalPath::fromAbsolutePath(path_u8string(largeFilePath)),
                                         newMtime));

    // Brief delay to let sync start processing
    std::this_thread::sleep_for(std::chrono::seconds(3));

    LOG_debug << logPre << "3. Deleting the file while MAC computation may be pending";
    mLocalFiles.clear();

    LOG_debug << logPre << "4. Waiting for sync to stabilize (file should be removed from cloud)";
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

    // Verify the cloud node is gone
    auto backupNode = getBackupNode();
    ASSERT_TRUE(backupNode) << "Backup node not found";

    std::unique_ptr<MegaNode> cloudNode(
        megaApi[0]->getChildNodeOfType(backupNode.get(), "large_file_delete_test.dat", FILENODE));
    ASSERT_FALSE(cloudNode) << "Cloud node should have been deleted";

    LOG_debug << logPre << "Test completed successfully";
}

/**
 * @brief Tests MAC computation obsolescence when file is moved during computation.
 *
 * Creates a large file, triggers mtime update to start MAC computation, then
 * moves the file to another directory while computation may be pending.
 * The sync should handle this gracefully, completing the move correctly.
 */
TEST_F(SdkTestSyncUploadsOperations, MacComputationObsolescenceOnMove)
{
    static const std::string logPre{"MacComputationObsolescenceOnMove: "};
    auto cleanup = setCleanupFunction();
    LOG_debug << logPre << "Test started";

    // Sync is already created during SetUp()

    // Use a large file to increase chance of pending MAC computation
    static constexpr size_t LARGE_FILE_SIZE = 100 * 1024 * 1024; // 100MB
    const fs::path largeFilePath = fs::absolute(getLocalTmpDir() / "large_file_move_test.dat");
    const fs::path destDir = fs::absolute(getLocalTmpDir() / "dir1");
    const fs::path destFilePath = destDir / "large_file_move_test.dat";

    LOG_debug << logPre << "1. Creating large test file";
    {
        auto st = addSyncListenerTracker(largeFilePath.string());
        ASSERT_TRUE(st);
        auto localFile = std::make_shared<sdk_test::LocalTempFile>(largeFilePath, LARGE_FILE_SIZE);
        mLocalFiles.emplace_back(localFile);
        auto [status, errCode] = st->waitForCompletion(COMMON_TIMEOUT);
        ASSERT_EQ(status, std::future_status::ready) << "Large file sync timed out";
        ASSERT_EQ(errCode, API_OK) << "Large file sync failed";
    }

    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

    LOG_debug << logPre << "2. Updating mtime to trigger MAC computation";
    const m_time_t newMtime = m_time(nullptr) + MIN_ALLOW_MTIME_DIFFERENCE;
    ASSERT_TRUE(mFsAccess->setmtimelocal(LocalPath::fromAbsolutePath(path_u8string(largeFilePath)),
                                         newMtime));

    // Brief delay to let sync start processing
    std::this_thread::sleep_for(std::chrono::seconds(3));

    LOG_debug << logPre << "3. Moving file to dir1 while MAC computation may be pending";
    {
        auto st = addSyncListenerTracker(destFilePath.string());
        ASSERT_TRUE(st);

        // On Windows, if MAC computation has the file open, the move will fail.
        // Retry a few times with delays to handle this platform limitation.
        std::error_code ec;
        static constexpr int MAX_MOVE_RETRIES = 10;
        static constexpr auto RETRY_DELAY = std::chrono::seconds(3);
        for (int attempt = 0; attempt < MAX_MOVE_RETRIES; ++attempt)
        {
            ec = mLocalFiles.back()->move(destFilePath);
            if (!ec)
            {
                LOG_debug << logPre << "File moved successfully on attempt " << (attempt + 1);
                break;
            }
            LOG_debug << logPre << "Move attempt " << (attempt + 1) << " failed: " << ec.message()
                      << ". Retrying...";
            std::this_thread::sleep_for(RETRY_DELAY);
        }
        ASSERT_FALSE(ec) << "Failed to move large file after " << MAX_MOVE_RETRIES
                         << " attempts: " << ec.message();

        auto [status, errCode] = st->waitForCompletion(COMMON_TIMEOUT);
        ASSERT_EQ(status, std::future_status::ready) << "Large file sync timed out";
        ASSERT_EQ(errCode, API_OK) << "Large file sync failed";
    }

    LOG_debug << logPre << "4. Waiting for sync to complete the move";
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

    // Verify the cloud node is in the new location
    auto backupNode = getBackupNode();
    ASSERT_TRUE(backupNode) << "Backup node not found";

    std::unique_ptr<MegaNode> dir1Node(
        megaApi[0]->getChildNodeOfType(backupNode.get(), "dir1", FOLDERNODE));
    ASSERT_TRUE(dir1Node) << "dir1 not found in cloud";

    std::unique_ptr<MegaNode> movedNode(
        megaApi[0]->getChildNodeOfType(dir1Node.get(), "large_file_move_test.dat", FILENODE));
    ASSERT_TRUE(movedNode) << "Moved file not found in dir1";

    // Original location should be empty
    std::unique_ptr<MegaNode> oldLocationNode(
        megaApi[0]->getChildNodeOfType(backupNode.get(), "large_file_move_test.dat", FILENODE));
    ASSERT_FALSE(oldLocationNode) << "File should not exist at original location";

    // Verify the mtime of the moved file
    ASSERT_EQ(movedNode->getModificationTime(), newMtime)
        << "Moved file should have the updated mtime";

    LOG_debug << logPre << "Test completed successfully";
}

/**
 * @brief Tests pre-computed MAC for clone candidates (SRT_XSF case).
 *
 * This test validates the non-blocking MAC pre-computation for files that
 * have potential clone candidates in the cloud (outside the sync root).
 *
 * Scenario:
 * 1. Upload several files to cloud (outside sync root) - various sizes
 * 2. Create sync with local files having same content but different mtime
 * 3. Verify that small files sync quickly (cloned) while large file MAC computes
 */
TEST_F(SdkTestSyncUploadsOperations, PreComputedMacForCloneCandidatesNonBlocking)
{
    static const std::string logPre{"PreComputedMacForCloneCandidatesNonBlocking: "};
    auto cleanup = setCleanupFunction();
    LOG_debug << logPre << "Test started";

    // File sizes - names chosen so large file is processed first alphabetically
    // ("a_large" < "b_small1" < "c_small2")
    static constexpr size_t SMALL_FILE_SIZE = (5 * 1024 * 1024) + (100 * 1024) + 7; // ~5MB
    static constexpr size_t LARGE_FILE_SIZE = (400 * 1024 * 1024) + (500 * 1024) + 3; // ~400MB

    LOG_debug << logPre << "1. Creating cloud folder for clone candidates (outside sync root)";

    std::unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);

    const std::string candidatesFolderName = "clone_candidates_" + std::to_string(m_time(nullptr));
    {
        RequestTracker createFolderTracker(megaApi[0].get());
        megaApi[0]->createFolder(candidatesFolderName.c_str(),
                                 rootNode.get(),
                                 &createFolderTracker);
        ASSERT_EQ(API_OK, createFolderTracker.waitForResult());
    }

    std::unique_ptr<MegaNode> candidatesFolder(
        megaApi[0]->getChildNode(rootNode.get(), candidatesFolderName.c_str()));
    ASSERT_TRUE(candidatesFolder);

    LOG_debug << logPre << "2. Creating temp files for upload to cloud";

    // Create temp files outside sync folder using LocalTempFile
    const fs::path tempDir = makeProcessTempDir("clone_test");

    auto largeTempFile =
        std::make_shared<sdk_test::LocalTempFile>(tempDir / "a_large_file.dat", LARGE_FILE_SIZE);
    auto small1TempFile =
        std::make_shared<sdk_test::LocalTempFile>(tempDir / "b_small_file1.dat", SMALL_FILE_SIZE);
    auto small2TempFile =
        std::make_shared<sdk_test::LocalTempFile>(tempDir / "c_small_file2.dat", SMALL_FILE_SIZE);

    LOG_debug << logPre << "3. Uploading clone candidate files to cloud (outside sync)";

    // Helper to upload a file to cloud with older mtime
    auto uploadToCloud = [&](const fs::path& filePath, const int timeoutSeconds = 60)
    {
        TransferTracker uploadTracker(megaApi[0].get());
        MegaUploadOptions uploadOptions;
        uploadOptions.mtime = m_time(nullptr) - 86400;

        const auto localPath = path_u8string(filePath);
        megaApi[0]->startUpload(localPath,
                                candidatesFolder.get(),
                                nullptr,
                                &uploadOptions,
                                &uploadTracker);
        return uploadTracker.waitForResult(timeoutSeconds) == API_OK;
    };

    ASSERT_TRUE(uploadToCloud(largeTempFile->getPath(), 600)) << "Failed to upload large file";
    ASSERT_TRUE(uploadToCloud(small1TempFile->getPath())) << "Failed to upload small file 1";
    ASSERT_TRUE(uploadToCloud(small2TempFile->getPath())) << "Failed to upload small file 2";

    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

    LOG_debug << logPre << "4. Moving files into sync folder (triggers clone with MAC computation)";

    const fs::path largeSyncPath = fs::absolute(getLocalTmpDir() / "a_large_file.dat");
    const fs::path small1SyncPath = fs::absolute(getLocalTmpDir() / "b_small_file1.dat");
    const fs::path small2SyncPath = fs::absolute(getLocalTmpDir() / "c_small_file2.dat");

    // Track completion times
    using clock = std::chrono::steady_clock;
    std::atomic<clock::time_point> largeCompleteTime{clock::time_point::max()};
    std::atomic<clock::time_point> small1CompleteTime{clock::time_point::max()};
    std::atomic<clock::time_point> small2CompleteTime{clock::time_point::max()};
    std::atomic<bool> largeDone{false}, small1Done{false}, small2Done{false};

    auto stLarge = mSyncListenerTrackers.add(largeSyncPath.string());
    auto stSmall1 = mSyncListenerTrackers.add(small1SyncPath.string());
    auto stSmall2 = mSyncListenerTrackers.add(small2SyncPath.string());
    ASSERT_TRUE(stLarge && stSmall1 && stSmall2);

    // Move files into sync folder
    ASSERT_FALSE(largeTempFile->move(largeSyncPath)) << "Failed to move large file";
    ASSERT_FALSE(small1TempFile->move(small1SyncPath)) << "Failed to move small file 1";
    ASSERT_FALSE(small2TempFile->move(small2SyncPath)) << "Failed to move small file 2";

    // Track these for cleanup
    mLocalFiles.emplace_back(largeTempFile);
    mLocalFiles.emplace_back(small1TempFile);
    mLocalFiles.emplace_back(small2TempFile);

    LOG_debug << logPre << "5. Waiting for sync completions and tracking order";

    // Wait in parallel threads to capture accurate timestamps
    std::thread waitLarge(
        [&]()
        {
            auto [status, err] = stLarge->waitForCompletion(std::chrono::seconds(600));
            if (status == std::future_status::ready && err == API_OK)
            {
                largeCompleteTime = clock::now();
                largeDone = true;
                LOG_debug << logPre << "Large file sync completed";
            }
        });

    std::thread waitSmall1(
        [&]()
        {
            auto [status, err] = stSmall1->waitForCompletion(std::chrono::seconds(300));
            if (status == std::future_status::ready && err == API_OK)
            {
                small1CompleteTime = clock::now();
                small1Done = true;
                LOG_debug << logPre << "Small file 1 sync completed";
            }
        });

    std::thread waitSmall2(
        [&]()
        {
            auto [status, err] = stSmall2->waitForCompletion(std::chrono::seconds(300));
            if (status == std::future_status::ready && err == API_OK)
            {
                small2CompleteTime = clock::now();
                small2Done = true;
                LOG_debug << logPre << "Small file 2 sync completed";
            }
        });

    waitLarge.join();
    waitSmall1.join();
    waitSmall2.join();

    // Verify all completed
    ASSERT_TRUE(largeDone) << "Large file sync failed or timed out";
    ASSERT_TRUE(small1Done) << "Small file 1 sync failed or timed out";
    ASSERT_TRUE(small2Done) << "Small file 2 sync failed or timed out";

    LOG_debug << logPre
              << "6. Verifying completion order (small files should complete before large)";

    auto largeTime = largeCompleteTime.load();
    auto small1Time = small1CompleteTime.load();
    auto small2Time = small2CompleteTime.load();

    LOG_debug
        << logPre << "Completion times relative to large file:"
        << " small1="
        << std::chrono::duration_cast<std::chrono::milliseconds>(small1Time - largeTime).count()
        << "ms"
        << " small2="
        << std::chrono::duration_cast<std::chrono::milliseconds>(small2Time - largeTime).count()
        << "ms";

    // The key assertion: small files should complete BEFORE large file
    EXPECT_LT(small1Time, largeTime)
        << "Small file 1 should complete before large file - async MAC may be blocking";
    EXPECT_LT(small2Time, largeTime)
        << "Small file 2 should complete before large file - async MAC may be blocking";

    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

    // Verify files exist in sync
    auto backupNode = getBackupNode();
    ASSERT_TRUE(backupNode);

    std::unique_ptr<MegaNode> largeNode(
        megaApi[0]->getChildNodeOfType(backupNode.get(), "a_large_file.dat", FILENODE));
    std::unique_ptr<MegaNode> small1Node(
        megaApi[0]->getChildNodeOfType(backupNode.get(), "b_small_file1.dat", FILENODE));
    std::unique_ptr<MegaNode> small2Node(
        megaApi[0]->getChildNodeOfType(backupNode.get(), "c_small_file2.dat", FILENODE));

    ASSERT_TRUE(largeNode) << "Large file not found in sync";
    ASSERT_TRUE(small1Node) << "Small file 1 not found in sync";
    ASSERT_TRUE(small2Node) << "Small file 2 not found in sync";

    // Cleanup
    fs::remove_all(tempDir);
    {
        RequestTracker removeTracker(megaApi[0].get());
        megaApi[0]->remove(candidatesFolder.get(), &removeTracker);
        removeTracker.waitForResult();
    }

    LOG_debug << logPre << "Test completed successfully";
}

/**
 * @brief Tests clone candidate MAC computation when local file is deleted mid-computation.
 */
TEST_F(SdkTestSyncUploadsOperations, CloneCandidateMacObsolescenceOnLocalDelete)
{
    static const std::string logPre{"CloneCandidateMacObsolescenceOnLocalDelete: "};
    auto cleanup = setCleanupFunction();
    LOG_debug << logPre << "Test started";

    static constexpr size_t LARGE_FILE_SIZE = 300 * 1024 * 1024; // 300MB for reliable MAC delay

    LOG_debug << logPre << "1. Creating cloud candidate file (outside sync)";

    std::unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);

    const std::string candidatesFolderName = "clone_del_test_" + std::to_string(m_time(nullptr));
    {
        RequestTracker createFolderTracker(megaApi[0].get());
        megaApi[0]->createFolder(candidatesFolderName.c_str(),
                                 rootNode.get(),
                                 &createFolderTracker);
        ASSERT_EQ(API_OK, createFolderTracker.waitForResult());
    }

    std::unique_ptr<MegaNode> candidatesFolder(
        megaApi[0]->getChildNode(rootNode.get(), candidatesFolderName.c_str()));
    ASSERT_TRUE(candidatesFolder);

    // Create temp file for upload using LocalTempFile
    const fs::path tempDir = makeProcessTempDir("clone_del");

    auto tempFile =
        std::make_shared<sdk_test::LocalTempFile>(tempDir / "large_clone_del.dat", LARGE_FILE_SIZE);

    // Upload to cloud with old mtime
    {
        TransferTracker uploadTracker(megaApi[0].get());
        MegaUploadOptions uploadOptions;
        uploadOptions.mtime = m_time(nullptr) - 86400;

        const auto tempLocalPath = path_u8string(tempFile->getPath());
        megaApi[0]->startUpload(tempLocalPath,
                                candidatesFolder.get(),
                                nullptr,
                                &uploadOptions,
                                &uploadTracker);
        ASSERT_EQ(API_OK, uploadTracker.waitForResult(600));
    }

    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

    LOG_debug << logPre << "2. Moving file into sync folder (triggers clone MAC computation)";

    const fs::path localPath = fs::absolute(getLocalTmpDir() / "large_clone_del.dat");
    ASSERT_FALSE(tempFile->move(localPath)) << "Failed to move file into sync";
    mLocalFiles.emplace_back(std::move(tempFile));

    // Brief delay to let MAC computation start
    std::this_thread::sleep_for(std::chrono::seconds(10));

    LOG_debug << logPre << "3. Deleting local file while MAC computation may be pending";
    mLocalFiles.clear();

    LOG_debug << logPre << "4. Waiting for sync to stabilize";
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

    // File should not exist in sync since we deleted it
    LOG_debug << logPre << "5. Verifying file does not exist in sync";
    auto backupNode = getBackupNode();
    ASSERT_TRUE(backupNode);

    std::unique_ptr<MegaNode> syncNode(
        megaApi[0]->getChildNodeOfType(backupNode.get(), "large_clone_del.dat", FILENODE));
    ASSERT_FALSE(syncNode) << "File should not exist in sync after deletion";

    // Cleanup
    fs::remove_all(tempDir);
    {
        RequestTracker removeTracker(megaApi[0].get());
        megaApi[0]->remove(candidatesFolder.get(), &removeTracker);
        removeTracker.waitForResult();
    }

    LOG_debug << logPre << "Test completed successfully";
}

/**
 * @brief Tests clone candidate MAC computation when cloud candidate is deleted mid-computation.
 */
TEST_F(SdkTestSyncUploadsOperations, CloneCandidateMacObsolescenceOnCloudDelete)
{
    static const std::string logPre{"CloneCandidateMacObsolescenceOnCloudDelete: "};
    auto cleanup = setCleanupFunction();
    LOG_debug << logPre << "Test started";

    static constexpr size_t LARGE_FILE_SIZE = 300 * 1024 * 1024; // 300MB for reliable MAC delay

    LOG_debug << logPre << "1. Creating cloud candidate file (outside sync)";

    std::unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);

    const std::string candidatesFolderName = "clone_cloud_del_" + std::to_string(m_time(nullptr));
    {
        RequestTracker createFolderTracker(megaApi[0].get());
        megaApi[0]->createFolder(candidatesFolderName.c_str(),
                                 rootNode.get(),
                                 &createFolderTracker);
        ASSERT_EQ(API_OK, createFolderTracker.waitForResult());
    }

    std::unique_ptr<MegaNode> candidatesFolder(
        megaApi[0]->getChildNode(rootNode.get(), candidatesFolderName.c_str()));
    ASSERT_TRUE(candidatesFolder);

    // Create temp file for upload using LocalTempFile
    const fs::path tempDir = makeProcessTempDir("clone_cloud_del");

    auto tempFile =
        std::make_shared<sdk_test::LocalTempFile>(tempDir / "large_cloud_del.dat", LARGE_FILE_SIZE);

    // Upload to cloud with old mtime
    {
        TransferTracker uploadTracker(megaApi[0].get());
        MegaUploadOptions uploadOptions;
        uploadOptions.mtime = m_time(nullptr) - 86400;

        const auto tempLocalPath = path_u8string(tempFile->getPath());
        megaApi[0]->startUpload(tempLocalPath,
                                candidatesFolder.get(),
                                nullptr,
                                &uploadOptions,
                                &uploadTracker);
        ASSERT_EQ(API_OK, uploadTracker.waitForResult(600));
    }

    std::unique_ptr<MegaNode> candidateNode(
        megaApi[0]->getChildNode(candidatesFolder.get(), "large_cloud_del.dat"));
    ASSERT_TRUE(candidateNode);

    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

    LOG_debug << logPre << "2. Moving file into sync folder (triggers clone MAC computation)";

    const fs::path localPath = fs::absolute(getLocalTmpDir() / "large_cloud_del.dat");

    // Set up tracker BEFORE moving file
    auto st = mSyncListenerTrackers.add(localPath.string());
    ASSERT_TRUE(st);

    ASSERT_FALSE(tempFile->move(localPath)) << "Failed to move file into sync";
    mLocalFiles.emplace_back(std::move(tempFile));

    // Brief delay to let MAC computation start
    std::this_thread::sleep_for(std::chrono::seconds(10));

    LOG_debug << logPre << "3. Deleting cloud candidate while MAC computation may be pending";
    {
        RequestTracker removeTracker(megaApi[0].get());
        megaApi[0]->remove(candidateNode.get(), &removeTracker);
        ASSERT_EQ(API_OK, removeTracker.waitForResult());
    }

    LOG_debug << logPre << "4. Waiting for sync to complete (should fall back to full upload)";

    auto [status, err] = st->waitForCompletion(std::chrono::seconds(600));
    ASSERT_EQ(status, std::future_status::ready) << "Sync timed out after cloud candidate deletion";

    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

    // File should exist in sync (uploaded, not cloned since candidate was deleted)
    LOG_debug << logPre << "5. Verifying file exists in sync";
    auto backupNode = getBackupNode();
    ASSERT_TRUE(backupNode);

    std::unique_ptr<MegaNode> syncNode(
        megaApi[0]->getChildNodeOfType(backupNode.get(), "large_cloud_del.dat", FILENODE));
    ASSERT_TRUE(syncNode) << "File should exist in sync after full upload";

    // Cleanup
    fs::remove_all(tempDir);
    {
        RequestTracker removeTracker(megaApi[0].get());
        megaApi[0]->remove(candidatesFolder.get(), &removeTracker);
        removeTracker.waitForResult();
    }

    LOG_debug << logPre << "Test completed successfully";
}
#endif // ENABLE_SYNC
