/**
 * @file
 * @brief This file contains tests for the public interfaces available to modify the local root of a
 * sync.
 */
#ifdef ENABLE_SYNC

#include "integration_test_utils.h"
#include "megautils.h"
#include "mock_listeners.h"
#include "sdk_test_utils.h"
#include "SdkTestNodesSetUp_test.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace sdk_test;
using namespace testing;

/**
 * @class SdkTestSyncLocalRootChange
 * @brief Test fixture designed to test the feature that allows changing the local root of a sync.
 */
class SdkTestSyncLocalRootChange: public SdkTestNodesSetUp
{
public:
    static constexpr auto MAX_TIMEOUT = 3min; // Timeout for operations in this tests suite

    void SetUp() override
    {
        SdkTestNodesSetUp::SetUp();

        mBackupId = syncFolder(megaApi[0].get(),
                               getLocalTmpDir().u8string(),
                               getNodeByPath("dir1/")->getHandle());
        ASSERT_NE(mBackupId, UNDEF) << "API Error adding a new sync";
        ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());
    }

    void TearDown() override
    {
        if (mBackupId != UNDEF)
        {
            ASSERT_NO_FATAL_FAILURE(removeSync(megaApi[0].get(), mBackupId));
        }
        SdkTestNodesSetUp::TearDown();
    }

    /**
     * @brief Build a simple file tree. dir1 for sync and dir2 as auxiliary node
     */
    const std::vector<NodeInfo>& getElements() const override
    {
        static const std::vector<NodeInfo> ELEMENTS{
            DirNodeInfo("dir1")
                .addChild(FileNodeInfo("testFile").setSize(1))
                .addChild(FileNodeInfo("testCommonFile"))
                .addChild(FileNodeInfo("testFile1")),
            DirNodeInfo("dir2")};
        return ELEMENTS;
    }

    const std::string& getRootTestDir() const override
    {
        static const std::string dirName{"SDK_TEST_SYNC_LOCAL_ROOT_CHANGE_AUX_DIR"};
        return dirName;
    }

    /**
     * @brief We don't want different creation times
     */
    bool keepDifferentCreationTimes() override
    {
        return false;
    }

    /**
     * @brief Waits until all direct successors from both remote and local roots of the sync match.
     *
     * Asserts false if a timeout is exceeded.
     */
    void waitForSyncToMatchCloudAndLocal() const
    {
        const auto areLocalAndCloudSynched = [this]() -> bool
        {
            const auto childrenCloudName =
                getCloudFirstChildrenNames(megaApi[0].get(), getSync()->getMegaHandle());
            return childrenCloudName && Value(getLocalFirstChildrenNames(),
                                              UnorderedElementsAreArray(*childrenCloudName));
        };
        ASSERT_TRUE(waitFor(areLocalAndCloudSynched, MAX_TIMEOUT, 10s));
    }

    /**
     * @brief Returns a vector with the names of the first successor files/directories inside the
     * local root.
     *
     * Hidden files (starting with .) and the debris folder are excluded
     */
    std::vector<std::string> getLocalFirstChildrenNames() const
    {
        return getLocalFirstChildrenNames_if(getLocalSyncRoot().value_or(getLocalTmpDir()),
                                             [](const std::string& name)
                                             {
                                                 return name.front() != '.' && name != DEBRISFOLDER;
                                             });
    }

    /**
     * @brief Returns the identifier to get the sync from megaApi
     */
    handle getBackupId() const
    {
        return mBackupId;
    }

    /**
     * @brief Returns the initiated sync object
     */
    std::unique_ptr<MegaSync> getSync() const
    {
        return std::unique_ptr<MegaSync>(megaApi[0]->getSyncByBackupId(mBackupId));
    }

    /**
     * @brief Returns the current sync state if initiated
     */
    std::optional<int> getSyncRunState() const
    {
        const auto sync = getSync();
        if (!sync)
            return {};
        return sync->getRunState();
    }

    /**
     * @brief Returns the current path the sync is using as root. If there is no sync, nullopt is
     * returned
     */
    std::optional<std::filesystem::path> getLocalSyncRoot() const
    {
        const auto sync = getSync();
        if (!sync)
            return {};
        return sync->getLocalFolder();
    }

    /**
     * @brief Where should we put our sync locally?
     */
    static const fs::path& getLocalTmpDir()
    {
        // Prevent parallel test from the same suite writing to the same dir
        thread_local const fs::path localTmpDir{"./SDK_TEST_SYNC_LOCAL_ROOT_CHANGE_AUX_LOCAL_DIR_" +
                                                getThisThreadIdStr()};
        return localTmpDir;
    }

    /**
     * @brief Removes the node located at the give relative path
     */
    void removeRemoteNode(const std::string& path)
    {
        const auto node = getNodeByPath(path);
        ASSERT_EQ(API_OK, doDeleteNode(0, node.get()));
    }

    /**
     * @brief Changes the local root of the sync and expects the operation to success.
     */
    void changeLocalSyncRootNoErrors(const std::filesystem::path& newRootPath) const
    {
        NiceMock<MockRequestListener> mockListener;
        mockListener.setErrorExpectations(API_OK);
        const auto rootPath = newRootPath.u8string();
        megaApi[0]->changeSyncLocalRoot(getBackupId(), rootPath.c_str(), &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    /**
     * @brief Sets up three files inside the given directory. These are:
     * - testCommonFile: An exact copy of the file created originally in the cloud
     * - testFile: A file with the same name as the one in the cloud originally but different
     *   contents (2 bytes of data)
     * - testFile2: Complete new file
     */
    void prepareSimilarRoot(const std::filesystem::path& newRootPath) const
    {
        const auto currentRoot = getLocalSyncRoot();
        ASSERT_TRUE(currentRoot);
        ASSERT_THAT(getLocalFirstChildrenNames(),
                    IsSupersetOf({"testCommonFile", "testFile", "testFile1"}));
        // Exact copy (including the mtime)
        const auto source = *currentRoot / "testCommonFile";
        const auto destination = newRootPath / "testCommonFile";
        std::filesystem::copy(source,
                              destination,
                              std::filesystem::copy_options::overwrite_existing);
        const auto mod_time = fs::last_write_time(source);
        fs::last_write_time(destination, mod_time);

        // Empty different file
        std::ofstream test2(newRootPath / "testFile2", std::ios::binary);

        // Same name different content
        std::ofstream test(newRootPath / "testFile", std::ios::binary);
        std::vector<char> buffer(2, 0);
        test.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    }

    /**
     * @brief Check that the current local root of the sync has the contents specified by the
     * prepareSimilarRoot method
     */
    void checkCurrentLocalMatchesSimilar() const
    {
        const auto currentRoot = getLocalSyncRoot();
        ASSERT_TRUE(currentRoot);
        ASSERT_THAT(getLocalFirstChildrenNames(),
                    UnorderedElementsAre("testCommonFile", "testFile2", "testFile"));
        ASSERT_EQ(std::filesystem::file_size(*currentRoot / "testFile"), 2);
    }

    /**
     * @brief Ensures the current local root of the sync matches the state expected after mirroring
     * original contests + the ones specified by prepareSimilarRoot. This includes a stall issue
     * with "testFile"
     */
    void checkCurrentLocalMatchesMirror() const
    {
        ASSERT_THAT(getLocalFirstChildrenNames(),
                    UnorderedElementsAre("testFile", "testCommonFile", "testFile1", "testFile2"));
        ASSERT_NO_FATAL_FAILURE(thereIsAStall("testFile"));
    }

    /**
     * @brief Ensures there is a stall issue involving the file with the given name.
     *
     * The expected reason for the stall is: LocalAndRemotePreviouslyUnsyncedDiffer_userMustChoose
     */
    void thereIsAStall(const std::string_view fileName) const
    {
        const auto stalls = sdk_test::getStalls(megaApi[0].get());
        ASSERT_EQ(stalls.size(), 1);
        ASSERT_TRUE(stalls[0]);
        const auto& stall = *stalls[0];
        EXPECT_THAT(stall.path(false, 0), EndsWith(fileName));
        EXPECT_THAT(
            stall.reason(),
            MegaSyncStall::SyncStallReason::LocalAndRemotePreviouslyUnsyncedDiffer_userMustChoose);
        ASSERT_FALSE(HasNonfatalFailure());
    }

    void moveLocalTmpDir(const std::filesystem::path& newLocation)
    {
        ASSERT_TRUE(mTempLocalDir.move(newLocation)) << "Error moving local tmp dir";
    }

protected:
    handle mBackupId{UNDEF};

private:
    LocalTempDir mTempLocalDir{getLocalTmpDir()};
};

/**
 * @brief SdkTestSyncLocalRootChange.ArgumentErrors : Validate the input error code paths.
 */
TEST_F(SdkTestSyncLocalRootChange, ArgumentErrors)
{
    static const std::string logPre{"SdkTestSyncLocalRootChange.ArgumentErrors : "};

    {
        LOG_verbose << logPre << "Giving undef backupId and undef remote handle";
        NiceMock<MockRequestListener> mockListener;
        mockListener.setErrorExpectations(API_EARGS, NO_SYNC_ERROR);
        megaApi[0]->changeSyncLocalRoot(UNDEF, nullptr, &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    const std::filesystem::path newRootPath{"./newLocaRootPathForTests/"};
    const LocalTempDir newRootDir(newRootPath);
    const auto newRootAbsPath = std::filesystem::absolute(newRootPath).u8string();

    {
        LOG_verbose << logPre << "Giving undef backupId and good new root path";
        NiceMock<MockRequestListener> mockListener;
        mockListener.setErrorExpectations(API_EARGS, _);
        megaApi[0]->changeSyncLocalRoot(UNDEF, newRootAbsPath.c_str(), &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    {
        LOG_verbose << logPre << "Giving non existent backupId and good remote handle";
        NiceMock<MockRequestListener> mockListener;
        mockListener.setErrorExpectations(API_EARGS, UNKNOWN_ERROR);
        megaApi[0]->changeSyncLocalRoot(*getNodeHandleByPath("dir1"),
                                        newRootAbsPath.c_str(),
                                        &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    {
        LOG_verbose << logPre << "Giving good backupId and a path to a file";
        NiceMock<MockRequestListener> mockListener;
        mockListener.setErrorExpectations(API_EACCESS, INVALID_LOCAL_TYPE);
        const auto filePath = std::filesystem::absolute(getLocalTmpDir() / "testFile").u8string();
        megaApi[0]->changeSyncLocalRoot(getBackupId(), filePath.c_str(), &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    {
        LOG_verbose << logPre << "Giving good backupId and a path to non existent dir";
        NiceMock<MockRequestListener> mockListener;
        mockListener.setErrorExpectations(API_ENOENT, LOCAL_PATH_UNAVAILABLE);
        const auto nonExsitsPath = std::filesystem::absolute("./NoExistsDir/").u8string();
        megaApi[0]->changeSyncLocalRoot(getBackupId(), nonExsitsPath.c_str(), &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    {
        LOG_verbose << logPre << "Giving good backupId and path to the already synced root";
        NiceMock<MockRequestListener> mockListener;
        mockListener.setErrorExpectations(API_EARGS, LOCAL_PATH_SYNC_COLLISION);
        const auto rootPath = std::filesystem::absolute(getLocalTmpDir()).u8string();
        megaApi[0]->changeSyncLocalRoot(getBackupId(), rootPath.c_str(), &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    // Just make sure that after all the attempts the sync is still running fine
    ASSERT_EQ(getSyncRunState(), std::optional{MegaSync::RUNSTATE_RUNNING});
}

/**
 * @brief SdkTestSyncLocalRootChange.ErrorNestedSyncs : Validate error code paths triggered when
 * trying to set the new root to a directory that is part of an existing sync
 */
TEST_F(SdkTestSyncLocalRootChange, ErrorNestedSyncs)
{
    static const std::string logPre{"SdkTestSyncLocalRootChange.ErrorNestedSyncs : "};

    LOG_verbose << logPre << "Creating a new sync between auxTmpDirForNewSync/ and dir2/";
    const LocalTempDir tmpDir{"./auxTmpDirForNewSync/"};
    const LocalTempDir tmpSubDir{"./auxTmpDirForNewSync/subdir"};
    const auto dir2BackupId = syncFolder(megaApi[0].get(),
                                         tmpDir.getPath().u8string(),
                                         getNodeByPath("dir2/")->getHandle());
    ASSERT_NE(dir2BackupId, UNDEF) << "API Error adding a new sync";

    {
        LOG_verbose << logPre << "Moving local root to another sync root";
        NiceMock<MockRequestListener> mockListener;
        mockListener.setErrorExpectations(API_EARGS, LOCAL_PATH_SYNC_COLLISION);
        const auto rootPath = tmpDir.getPath().u8string();
        megaApi[0]->changeSyncLocalRoot(getBackupId(), rootPath.c_str(), &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    {
        LOG_verbose << logPre << "Moving local root to a subdir inside another sync";
        NiceMock<MockRequestListener> mockListener;
        mockListener.setErrorExpectations(API_EARGS, LOCAL_PATH_SYNC_COLLISION);
        const auto rootPath = tmpSubDir.getPath().u8string();
        megaApi[0]->changeSyncLocalRoot(getBackupId(), rootPath.c_str(), &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(MAX_TIMEOUT));
    }
}

#ifndef WIN32
/**
 * @brief SdkTestSyncLocalRootChange.ErrorNestedSyncSymLink :
 * 1. Change the root of the sync to a symlink pointing to the original root
 * 2. Change the root of the sync to a symlink pointing to a root of another sync
 * NOTE: This test does not make sense on windows due to how symlinks are handled there.
 */
TEST_F(SdkTestSyncLocalRootChange, ErrorNestedSyncSymLink)
{
    static const std::string logPre{"SdkTestSyncLocalRootChange.ErrorNestedSyncSymLink : "};
    LOG_verbose << logPre
                << "Creating a new sync between auxTmpDirErrorNestedSyncSymLink/ and dir2/";
    const LocalTempDir tmpDir{"./auxTmpDirErrorNestedSyncSymLink/"};
    const auto dir2BackupId = syncFolder(megaApi[0].get(),
                                         tmpDir.getPath().u8string(),
                                         getNodeByPath("dir2/")->getHandle());
    ASSERT_NE(dir2BackupId, UNDEF) << "API Error adding a new sync";

    {
        LOG_verbose << logPre << "Changing the root to a symlink pointing to the original root";
        std::filesystem::path linkName{"./symLinkToOriginal"};
        std::filesystem::create_directory_symlink(getLocalTmpDir(), linkName);
        NiceMock<MockRequestListener> mockListener;
        mockListener.setErrorExpectations(API_EARGS, LOCAL_PATH_SYNC_COLLISION);
        const auto rootPath = linkName.u8string();
        megaApi[0]->changeSyncLocalRoot(getBackupId(), rootPath.c_str(), &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(MAX_TIMEOUT));
        std::filesystem::remove(linkName);
    }

    {
        LOG_verbose << logPre
                    << "Changing the root to a symlink pointing to the root of another sync";
        std::filesystem::path linkName{"./symLinkToSecondSync"};
        std::filesystem::create_directory_symlink(tmpDir.getPath(), linkName);
        NiceMock<MockRequestListener> mockListener;
        mockListener.setErrorExpectations(API_EARGS, LOCAL_PATH_SYNC_COLLISION);
        const auto rootPath = linkName.u8string();
        megaApi[0]->changeSyncLocalRoot(getBackupId(), rootPath.c_str(), &mockListener);
        EXPECT_TRUE(mockListener.waitForFinishOrTimeout(MAX_TIMEOUT));
        std::filesystem::remove(linkName);
    }
}
#endif

/**
 * @brief SdkTestSyncLocalRootChange.OKSyncRunningToEmptyRoot: Change the root of a running sync to
 * an empty directory. Ensure the new .debris is properly created.
 */
TEST_F(SdkTestSyncLocalRootChange, OKSyncRunningToEmptyRoot)
{
    static const std::string logPre{"SdkTestSyncLocalRootChange.OKSyncRunningToEmptyRoot : "};

    ASSERT_EQ(getSyncRunState(), std::optional{MegaSync::RUNSTATE_RUNNING});

    LOG_verbose << logPre << "Moving local root to an empty new root";
    const LocalTempDir tmpDir{"./auxTmpDirOKSyncRunningToEmptyRoot/"};
    ASSERT_NO_FATAL_FAILURE(changeLocalSyncRootNoErrors(tmpDir.getPath()));
    ASSERT_EQ(getSyncRunState(), std::optional{MegaSync::RUNSTATE_RUNNING});

    LOG_verbose << logPre << "Waiting for local to match cloud";
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());

    LOG_verbose << logPre << "Validating expectations: Empty dir (local has preference)";
    EXPECT_THAT(getLocalFirstChildrenNames_if(tmpDir.getPath()),
                testing::UnorderedElementsAre(".megaignore"));

    // Create a file and remove it in the cloud to force debris creation
    LOG_verbose << logPre << "Creating new file and removing from cloud to force .debris";
    const std::string testFileName{"testTempFile.txt"};
    LocalTempFile f{tmpDir.getPath() / testFileName, 0};
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());
    removeRemoteNode("dir1/" + testFileName);
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());

    LOG_verbose << logPre << "Validating expectations: Empty + .debris";
    EXPECT_THAT(getLocalFirstChildrenNames_if(tmpDir.getPath()),
                testing::UnorderedElementsAre(".megaignore", DEBRISFOLDER));
}

/**
 * @brief SdkTestSyncLocalRootChange.OKSyncRunningPauseAndResume: Change the root of a running sync
 * and ensure everything works as expected after pausing and resuming.
 */
TEST_F(SdkTestSyncLocalRootChange, OKSyncRunningPauseAndResume)
{
    static const std::string logPre{"SdkTestSyncLocalRootChange.OKSyncRunningPauseAndResume : "};

    LOG_verbose << logPre << "Moving local root to an empty new root";
    const LocalTempDir tmpDir{"./auxTmpDirOKSyncRunningToEmptyRoot/"};
    ASSERT_NO_FATAL_FAILURE(changeLocalSyncRootNoErrors(tmpDir.getPath()));

    ASSERT_EQ(getSyncRunState(), std::optional{MegaSync::RUNSTATE_RUNNING});

    LOG_verbose << logPre << "Suspending the sync";
    ASSERT_TRUE(sdk_test::suspendSync(megaApi[0].get(), getBackupId()))
        << "Error suspending the sync";
    ASSERT_EQ(getSyncRunState(), std::optional{MegaSync::RUNSTATE_SUSPENDED});

    LOG_verbose << logPre << "Creating a new file locally";
    const std::string testFileName{"testTempFile.txt"};
    LocalTempFile f{tmpDir.getPath() / testFileName, 0};

    LOG_verbose << logPre << "Resuming the sync";
    ASSERT_TRUE(sdk_test::resumeSync(megaApi[0].get(), getBackupId())) << "Error resuming the sync";
    ASSERT_EQ(getSyncRunState(), std::optional{MegaSync::RUNSTATE_RUNNING});

    LOG_verbose << logPre << "Checking the new file uploads";
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());
    EXPECT_THAT(getLocalFirstChildrenNames_if(tmpDir.getPath()),
                testing::UnorderedElementsAre(".megaignore", testFileName));
}

/**
 * @brief SdkTestSyncLocalRootChange.OKSyncRunningToSimilarRoot: Change the root of a running sync
 * to a directory that contains different files:
 * - One exactly the same as in the previous root
 * - One different
 * - One with same name and different contents
 * - It misses one that was in previous root
 *
 * The final state prioritize local new root
 *
 */
TEST_F(SdkTestSyncLocalRootChange, OKSyncRunningToSimilarRoot)
{
    static const std::string logPre{"SdkTestSyncLocalRootChange.OKSyncRunningToSimilarRoot : "};

    LOG_verbose << logPre << "Preparing new root with similar contents";
    const LocalTempDir tmpDir{"./auxTmpOKSyncRunningToSimilarRoot/"};
    prepareSimilarRoot(tmpDir.getPath());

    LOG_verbose << logPre << "Changing the root";
    ASSERT_NO_FATAL_FAILURE(changeLocalSyncRootNoErrors(tmpDir.getPath()));
    ASSERT_EQ(getSyncRunState(), std::optional{MegaSync::RUNSTATE_RUNNING});
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());

    LOG_verbose << logPre << "Validating expectations";
    ASSERT_NO_FATAL_FAILURE(checkCurrentLocalMatchesSimilar());
}

/**
 * @brief SdkTestSyncLocalRootChange.OKSyncSuspendedToSimilarRoot: Same as
 * OKSyncRunningToSimilarRoot but changing the root while the sync is suspended, then it is resumed
 * and wait to validate expectations.
 */
TEST_F(SdkTestSyncLocalRootChange, OKSyncSuspendedToSimilarRoot)
{
    static const std::string logPre{"SdkTestSyncLocalRootChange.OKSyncSuspendedToSimilarRoot : "};

    LOG_verbose << logPre << "Preparing new root with similar contents";
    const LocalTempDir tmpDir{"./auxTmpOKSyncSuspendedToSimilarRoot/"};
    prepareSimilarRoot(tmpDir.getPath());

    LOG_verbose << logPre << "Suspending the sync";
    ASSERT_TRUE(sdk_test::suspendSync(megaApi[0].get(), getBackupId()))
        << "Error suspending the sync";
    ASSERT_EQ(getSyncRunState(), std::optional{MegaSync::RUNSTATE_SUSPENDED});

    LOG_verbose << logPre << "Changing the root";
    ASSERT_NO_FATAL_FAILURE(changeLocalSyncRootNoErrors(tmpDir.getPath()));
    ASSERT_EQ(getSyncRunState(), std::optional{MegaSync::RUNSTATE_SUSPENDED});

    LOG_verbose << logPre << "Resuming the sync";
    ASSERT_TRUE(sdk_test::resumeSync(megaApi[0].get(), getBackupId())) << "Error resuming the sync";
    ASSERT_EQ(getSyncRunState(), std::optional{MegaSync::RUNSTATE_RUNNING});

    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());

    LOG_verbose << logPre << "Validating expectations";
    ASSERT_NO_FATAL_FAILURE(checkCurrentLocalMatchesSimilar());
}

/**
 * @brief SdkTestSyncLocalRootChange.OKSyncDisabledToSimilarRoot: Same as
 * OKSyncRunningToSimilarRoot but changing the root while the sync is disabled, then it is enabled
 * and wait to validate expectations.
 *
 * NOTE: In this case, the final state must be a mirror between cloud and local.
 */
TEST_F(SdkTestSyncLocalRootChange, OKSyncDisabledToSimilarRoot)
{
    static const std::string logPre{"SdkTestSyncLocalRootChange.OKSyncDisabledToSimilarRoot : "};

    LOG_verbose << logPre << "Preparing new root with similar contents";
    const LocalTempDir tmpDir{"./auxTmpOKSyncDisabledToSimilarRoot/"};
    prepareSimilarRoot(tmpDir.getPath());

    LOG_verbose << logPre << "Disable the sync";
    ASSERT_TRUE(sdk_test::disableSync(megaApi[0].get(), getBackupId()))
        << "Error suspending the sync";
    ASSERT_EQ(getSyncRunState(), std::optional{MegaSync::RUNSTATE_DISABLED});

    LOG_verbose << logPre << "Changing the root";
    ASSERT_NO_FATAL_FAILURE(changeLocalSyncRootNoErrors(tmpDir.getPath()));
    ASSERT_EQ(getSyncRunState(), std::optional{MegaSync::RUNSTATE_DISABLED});

    LOG_verbose << logPre << "Resuming the sync";
    ASSERT_TRUE(sdk_test::resumeSync(megaApi[0].get(), getBackupId())) << "Error resuming the sync";
    ASSERT_EQ(getSyncRunState(), std::optional{MegaSync::RUNSTATE_RUNNING});

    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());

    LOG_verbose << logPre << "Validating expectations";
    ASSERT_NO_FATAL_FAILURE(checkCurrentLocalMatchesMirror());
}

#ifndef WIN32
/**
 * @brief SdkTestSyncLocalRootChange.OKSyncRunningMoveRootAndReassing:
 * 1. Move the root directory of a running sync to a different location
 * 2. Check that it gets suspended
 * 3. Reassign the root to the new location
 * 4. Sync can be resumed and everything stays as it was
 * NOTE: This test does not apply to windows because windows will block the rename operation on the
 * root while the sync is running (the directory is opened by the sync engine). We should pause the
 * sync before the rename but that scenario falls into the domain of other tests.
 */
TEST_F(SdkTestSyncLocalRootChange, OKSyncRunningMoveRootAndReassing)
{
    static const std::string logPre{
        "SdkTestSyncLocalRootChange.OKSyncRunningMoveRootAndReassing : "};

    ASSERT_EQ(getSyncRunState(), std::optional{MegaSync::RUNSTATE_RUNNING});

    LOG_verbose << logPre << "Renaming local root";
    const auto newRoot = getLocalTmpDir().parent_path() / "TestDirOKSyncRunningMoveRootAndReassing";
    ASSERT_NO_FATAL_FAILURE(moveLocalTmpDir(newRoot));

    LOG_verbose << logPre << "Waiting for the sync to be disabled";
    ASSERT_TRUE(waitFor(
        [this]()
        {
            return getSyncRunState() == std::optional{MegaSync::RUNSTATE_SUSPENDED};
        },
        MAX_TIMEOUT,
        10s));

    LOG_verbose << logPre << "Change sync root to new location";
    ASSERT_NO_FATAL_FAILURE(changeLocalSyncRootNoErrors(newRoot));

    LOG_verbose << logPre << "Enabling the sync";
    ASSERT_TRUE(sdk_test::resumeSync(megaApi[0].get(), getBackupId())) << "Error resuming the sync";
    ASSERT_EQ(getSyncRunState(), std::optional{MegaSync::RUNSTATE_RUNNING});

    // Move the directory back to where it was
    moveLocalTmpDir(getLocalTmpDir());
}
#endif

/**
 * @brief SdkTestSyncLocalRootChange.OKChangRootToASymLink: Change root to a symlink to an empty
 * directory. Validate final state.
 */
TEST_F(SdkTestSyncLocalRootChange, OKChangRootToASymLink)
{
    static const std::string logPre{"SdkTestSyncLocalRootChange.OKChangRootToASymLink : "};

    const LocalTempDir tmpDir{"./auxTmpDirOKChangRootToASymLink/"};
    std::filesystem::path linkName{"./symLinkToEmpty"};
    const MrProper defer{[&linkName]()
                         {
                             std::filesystem::remove(linkName);
                         }};

    ASSERT_EQ(getSyncRunState(), std::optional{MegaSync::RUNSTATE_RUNNING});

    std::filesystem::create_directory_symlink(tmpDir.getPath(), linkName);

    LOG_verbose << logPre << "Moving local root to an empty new root";
    ASSERT_NO_FATAL_FAILURE(changeLocalSyncRootNoErrors(linkName));
    ASSERT_EQ(getSyncRunState(), std::optional{MegaSync::RUNSTATE_RUNNING});

    LOG_verbose << logPre << "Waiting for local to match cloud";
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());

    LOG_verbose << logPre << "Validating expectations: Empty dir (local has preference)";
    EXPECT_THAT(getLocalFirstChildrenNames_if(tmpDir.getPath()),
                testing::UnorderedElementsAre(".megaignore"));
}

class SdkTestBackupSyncLocalRootChange: public SdkTestSyncLocalRootChange
{
public:
    void SetUp() override
    {
        SdkTestNodesSetUp::SetUp();
        createAuxFiles();
        mBackupId = backupFolder(megaApi[0].get(), getLocalTmpDir().u8string(), "myBackup");
        ASSERT_NE(mBackupId, UNDEF) << "API Error adding a new backup sync";
        ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());
    }

    /**
     * @brief We don't need nodes on the cloud for backups
     */
    const std::vector<NodeInfo>& getElements() const override
    {
        static const std::vector<NodeInfo> ELEMENTS{};
        return ELEMENTS;
    }

    void createAuxFiles()
    {
        auxFiles.emplace_back(getLocalTmpDir() / "testFile", 1);
        auxFiles.emplace_back(getLocalTmpDir() / "testCommonFile", 0);
        auxFiles.emplace_back(getLocalTmpDir() / "testFile1", 0);
    }

    enum class StopAction
    {
        disable,
        pause
    };

    /**
     * @brief Disables or pauses the current backup, then changes the local root to a new directory
     * with similar contents. The backup is resumed and the final state is validated.
     *
     * @param action Specifies how the backup has to be stopped
     */
    void changeRootToSimilarWhileStop(const StopAction action)
    {
        const auto* testInfo = ::testing::UnitTest::GetInstance()->current_test_info();
        const std::string suiteName = testInfo->test_suite_name();
        const std::string testName = testInfo->name();
        const std::string logPrefix = suiteName + "." + testName + " : ";

        LOG_verbose << logPrefix << "Preparing new root with similar contents";
        const LocalTempDir tmpDir{"./auxTmp" + testName};
        prepareSimilarRoot(tmpDir.getPath());

        MegaSync::SyncRunningState expectedRunState;
        switch (action)
        {
            case StopAction::pause:
            {
                LOG_verbose << logPrefix << "Suspending the backup sync";
                ASSERT_TRUE(sdk_test::suspendSync(megaApi[0].get(), getBackupId()))
                    << "Error suspending the sync";
                expectedRunState = MegaSync::RUNSTATE_SUSPENDED;
                break;
            }
            case StopAction::disable:
            {
                LOG_verbose << logPrefix << "Disable the backup sync";
                ASSERT_TRUE(sdk_test::disableSync(megaApi[0].get(), getBackupId()))
                    << "Error disabling the sync";
                expectedRunState = MegaSync::RUNSTATE_DISABLED;
                break;
            }
        }
        ASSERT_EQ(getSyncRunState(), std::optional{expectedRunState});

        LOG_verbose << logPrefix << "Changing the root";
        ASSERT_NO_FATAL_FAILURE(changeLocalSyncRootNoErrors(tmpDir.getPath()));
        ASSERT_EQ(getSyncRunState(), std::optional{expectedRunState});

        LOG_verbose << logPrefix << "Resuming the backup sync";
        ASSERT_TRUE(sdk_test::resumeSync(megaApi[0].get(), getBackupId()))
            << "Error resuming the sync";
        ASSERT_EQ(getSyncRunState(), std::optional{MegaSync::RUNSTATE_RUNNING});

        ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());

        LOG_verbose << logPrefix << "Validating expectations";
        ASSERT_NO_FATAL_FAILURE(checkCurrentLocalMatchesSimilar());
    }

private:
    std::vector<LocalTempFile> auxFiles;
};

/**
 * @brief Change the root of the backup to an empty local dir.
 * Expectations -> final state = empty
 * The name of the backup and the remote root node do not change
 */
TEST_F(SdkTestBackupSyncLocalRootChange, OKChangeRootToEmpty)
{
    static const std::string logPre{"SdkTestBackupSyncLocalRootChange.OKChangeRootToEmpty : "};

    ASSERT_EQ(getSyncRunState(), std::optional{MegaSync::RUNSTATE_RUNNING});

    LOG_verbose << logPre << "Moving local root to an empty new root";
    const LocalTempDir tmpDir{"./auxTmpDirOKChangeRootToEmpty/"};
    ASSERT_NO_FATAL_FAILURE(changeLocalSyncRootNoErrors(tmpDir.getPath()));
    ASSERT_EQ(getSyncRunState(), std::optional{MegaSync::RUNSTATE_RUNNING});

    LOG_verbose << logPre << "Waiting for local to match cloud";
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());

    LOG_verbose << logPre << "Validating expectations: Empty dir (local has preference)";
    EXPECT_THAT(getLocalFirstChildrenNames_if(tmpDir.getPath()),
                testing::UnorderedElementsAre(".megaignore"));

    const auto backup = getSync();
    ASSERT_TRUE(backup);
    EXPECT_STREQ(backup->getName(), "myBackup");
    EXPECT_THAT(backup->getLastKnownMegaFolder(), EndsWith("myBackup"));
}

/**
 * @brief SdkTestBackupSyncLocalRootChange.OKBackupRunningToSimilarRoot: Change the root of a
 * running backup sync to a directory that contains different files:
 * - One exactly the same as in the previous root
 * - One different
 * - One with same name and different contents
 * - It misses one that was in previous root
 *
 * The final state prioritize the new local root
 */
TEST_F(SdkTestBackupSyncLocalRootChange, OKBackupRunningToSimilarRoot)
{
    static const std::string logPre{
        "SdkTestBackupSyncLocalRootChange.OKBackupRunningToSimilarRoot : "};

    LOG_verbose << logPre << "Preparing new root with similar contents";
    const LocalTempDir tmpDir{"./auxTmpOKBackupRunningToSimilarRoot/"};
    prepareSimilarRoot(tmpDir.getPath());

    LOG_verbose << logPre << "Changing the root";
    ASSERT_NO_FATAL_FAILURE(changeLocalSyncRootNoErrors(tmpDir.getPath()));
    ASSERT_EQ(getSyncRunState(), std::optional{MegaSync::RUNSTATE_RUNNING});
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());

    LOG_verbose << logPre << "Validating expectations";
    ASSERT_NO_FATAL_FAILURE(checkCurrentLocalMatchesSimilar());
}

/**
 * @brief SdkTestBackupSyncLocalRootChange.OKBackupSuspendedToSimilarRoot: Same as
 * OKBackupRunningToSimilarRoot but changing the root while the backup is suspended, then it is
 * resumed and waits to validate expectations.
 */
TEST_F(SdkTestBackupSyncLocalRootChange, OKBackupSuspendedToSimilarRoot)
{
    ASSERT_NO_FATAL_FAILURE(changeRootToSimilarWhileStop(StopAction::pause));
}

/**
 * @brief SdkTestBackupSyncLocalRootChange.OKBackupDisabledToSimilarRoot: Same as
 * OKBackupRunningToSimilarRoot but changing the root while the backup is disabled, then it is
 * enabled and waits to validate expectations.
 */
TEST_F(SdkTestBackupSyncLocalRootChange, OKBackupDisabledToSimilarRoot)
{
    ASSERT_NO_FATAL_FAILURE(changeRootToSimilarWhileStop(StopAction::disable));
}
#endif // ENABLE_SYNC
