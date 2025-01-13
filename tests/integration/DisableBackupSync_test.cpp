/**
 * @file
 * @brief This file contains tests that involve operations on disabled backup syncs.
 */

#ifdef ENABLE_SYNC

#include "gtest_common.h"
#include "integration/mock_listeners.h"
#include "integration_test_utils.h"
#include "sdk_test_utils.h"
#include "SdkTest_test.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>

namespace fs = std::filesystem;
using namespace sdk_test;
using namespace testing;

/**
 * @class DisableBackupSync
 * @brief Test fixture that creates a backup sync, waits until all the files have been uploaded and
 * then disables it.
 */
class DisableBackupSync: public SdkTest
{
public:
    static constexpr auto MAX_TIMEOUT = 3min; // Timeout for operations in this tests suite
    static constexpr auto TIME_DELTA_CONSECUTIVE_TRIES = 10s; // Time to wait between consecutive
                                                              // calls in a waitFor or some sleeps

    void SetUp() override
    {
        SdkTest::SetUp();
        ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
        ASSERT_NO_FATAL_FAILURE(ensureAccountDeviceName(megaApi[0].get()));
        createInitialLocalFiles();
        mBackupId = backupFolder(megaApi[0].get(), getLocalTmpDir().u8string());
        ASSERT_NE(mBackupId, UNDEF);
        ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());
        ASSERT_TRUE(disableSync(megaApi[0].get(), mBackupId));
    }

    void TearDown() override
    {
        if (mBackupId != UNDEF)
        {
            removeSync(megaApi[0].get(), mBackupId);
        }
        SdkTest::TearDown();
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
        ASSERT_TRUE(waitFor(areLocalAndCloudSynched, MAX_TIMEOUT, TIME_DELTA_CONSECUTIVE_TRIES));
    }

    /**
     * @brief Resume the sync, wait for local and cloud to match assert it is running and there are
     * no stall issues.
     */
    void resumeAndValidateOK() const
    {
        ASSERT_TRUE(resumeSync(megaApi[0].get(), getBackupId()));
        std::this_thread::sleep_for(TIME_DELTA_CONSECUTIVE_TRIES);
        ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());
        ASSERT_EQ(getSyncRunState(), std::optional{MegaSync::RUNSTATE_RUNNING});
        ASSERT_TRUE(getStalls(megaApi[0].get()).empty());
    }

    /**
     * @brief Waits until the node at the given path has the given size.
     *
     * @param nodePath The path to the node relative to the backup root path
     * @param fileSize The expected size for that node
     */
    void waitForCloudNodeToMatchSize(const std::string_view nodePath, const int64_t nodeSize) const
    {
        const auto confirmSize = [nodePath, nodeSize, this]() -> bool
        {
            const auto node = getNodeByPath(nodePath);
            return node && node->getSize() == nodeSize;
        };
        waitFor(confirmSize, MAX_TIMEOUT, TIME_DELTA_CONSECUTIVE_TRIES);
    }

    /**
     * @brief Get the node inside the backup in the given relative path
     *
     * Returns nullptr if it is not found or there is no valid backup sync running.
     */
    std::unique_ptr<MegaNode> getNodeByPath(const std::string_view path) const
    {
        const auto backup = getSync();
        if (!backup)
            return nullptr;
        const std::string filePath = backup->getLastKnownMegaFolder() + "/"s + std::string(path);
        return std::unique_ptr<MegaNode>(megaApi[0]->getNodeByPath(filePath.c_str()));
    }

    /**
     * @brief Get the names of the first successors in the current local sync root.
     */
    std::vector<std::string> getLocalFirstChildrenNames() const
    {
        return getLocalFirstChildrenNames_if(getLocalSyncRoot().value_or(getLocalTmpDir()),
                                             [](const std::string& name)
                                             {
                                                 return name.front() != '.' && name != DEBRISFOLDER;
                                             });
    }

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

    static const fs::path& getLocalTmpDir()
    {
        // Prevent parallel test from the same suite writing to the same dir
        thread_local const fs::path localTmpDir{"./DISABLE_BACKUP_SYNC_AUX_TMP_DIR_" +
                                                getThisThreadIdStr()};
        return localTmpDir;
    }

private:
    LocalTempDir tmpDir{getLocalTmpDir()};
    handle mBackupId{UNDEF};

    void createInitialLocalFiles() const
    {
        sdk_test::createFile(getLocalTmpDir() / "testFile", 1);
    }
};

/**
 * @brief DisableBackupSync.RemoveLocalFile:
 * - Remove file while the sync is disabled
 * - Resume it
 * - Confirm the file gets deleted on the cloud and the backup keeps running
 */
TEST_F(DisableBackupSync, RemoveLocalFile)
{
    fs::remove(getLocalTmpDir() / "testFile");
    ASSERT_NO_FATAL_FAILURE(resumeAndValidateOK());
    ASSERT_TRUE(getLocalFirstChildrenNames().empty());
}

/**
 * @brief DisableBackupSync.ModifyLocalFile:
 * - Remove file while the sync is disabled
 * - Resume it
 * - Confirm the file gets updated on the cloud and the backup keeps running
 */
TEST_F(DisableBackupSync, ModifyLocalFile)
{
    sdk_test::createFile(getLocalTmpDir() / "testFile", 5);
    ASSERT_NO_FATAL_FAILURE(resumeAndValidateOK());
    ASSERT_THAT(getLocalFirstChildrenNames(), UnorderedElementsAre("testFile"));
    ASSERT_NO_FATAL_FAILURE(waitForCloudNodeToMatchSize("testFile", 5));
}

/**
 * @brief DisableBackupSync.CreateNewLocalFile:
 * - Create a new file while the sync is disabled
 * - Resume it
 * - Confirm the file gets uploaded to the cloud and the backup keeps running
 */
TEST_F(DisableBackupSync, CreateNewLocalFile)
{
    sdk_test::createFile(getLocalTmpDir() / "testFile2", 1);
    ASSERT_NO_FATAL_FAILURE(resumeAndValidateOK());
    ASSERT_THAT(getLocalFirstChildrenNames(), UnorderedElementsAre("testFile", "testFile2"));
}

/**
 * @brief DisableBackupSync.RenameLocalFile:
 * - Rename a local file while the sync is disabled.
 * - Resume the sync.
 * - Confirm that the move is detected and the backup keeps running.
 */
TEST_F(DisableBackupSync, RenameLocalFile)
{
    fs::rename(getLocalTmpDir() / "testFile", getLocalTmpDir() / "testFile2");
    ASSERT_NO_FATAL_FAILURE(resumeAndValidateOK());
    ASSERT_THAT(getLocalFirstChildrenNames(), UnorderedElementsAre("testFile2"));
}

#endif
