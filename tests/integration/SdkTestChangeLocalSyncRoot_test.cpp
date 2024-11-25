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
     * @brief Waits until all direct successors from both remote and local roots of the sync matches
     *
     * Asserts false if a timeout is overpassed.
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
        return getLocalFirstChildrenNames_if(getLocalTmpDir(),
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
     * @brief Where should we put our sync locally?
     */
    static const fs::path& getLocalTmpDir()
    {
        // Prevent parallel test from the same suite writing to the same dir
        thread_local const fs::path localTmpDir{"./SDK_TEST_SYNC_LOCAL_ROOT_CHANGE_AUX_LOCAL_DIR_" +
                                                getThisThreadIdStr()};
        return localTmpDir;
    }

private:
    LocalTempDir mTempLocalDir{getLocalTmpDir()};
    handle mBackupId{UNDEF};
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
        mockListener.setErrorExpectations(API_EEXIST, UNKNOWN_ERROR);
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

#endif // ENABLE_SYNC
