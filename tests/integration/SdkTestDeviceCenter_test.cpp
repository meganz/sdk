/**
 * @file SdkTestDeviceCenter_test.cpp
 * @brief Test Device Center operations on full-account syncs
 */

#ifdef ENABLE_SYNC

#include "integration_test_utils.h"
#include "mock_listeners.h"
#include "sdk_test_utils.h"
#include "SdkTest_test.h"

using namespace sdk_test;
using namespace testing;

/**
 * @brief Test fixture which initializates two sessions of the same account
 *
 * It offers functionality to perform operations from the Device Center.
 *
 * It initializes 2 MegaApi instance, the first (index 0) plays the role of the main device while
 * the second (index 1) is used as the remote Device Center.
 *
 */
class SdkTestDeviceCenter: public SdkTest
{
public:
    static constexpr auto MAX_TIMEOUT = 3min;
    handle mBackupID{UNDEF};

    void SetUp() override
    {
        SdkTest::SetUp();

        ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
        ASSERT_NO_FATAL_FAILURE(ensureAccountDeviceName(megaApi[0].get()));

        // Initialize a second session with the same credentials
        ASSERT_NO_FATAL_FAILURE(initializeSecondSession());
    }

    bool resumeFromDeviceCenter()
    {
        return doChangeFromDeviceCenter(DeviceCenterOperations::RESUME);
    }

    bool pauseFromDeviceCenter()
    {
        return doChangeFromDeviceCenter(DeviceCenterOperations::PAUSE);
    }

    bool deleteFromDeviceCenter()
    {
        return doChangeFromDeviceCenter(DeviceCenterOperations::REMOVE);
    }

    const fs::path& getLocalFolder()
    {
        return localFolder.getPath();
    }

    bool waitForSyncStateFromMain(const MegaSync::SyncRunningState runState)
    {
        return waitForSyncState(megaApi[0].get(), mBackupID, runState, MegaSync::NO_SYNC_ERROR) !=
               nullptr;
    }

private:
    std::string localFolderName = getFilePrefix() + "dir";
    LocalTempDir localFolder{fs::current_path() / localFolderName};

    void initializeSecondSession()
    {
        megaApi.resize(megaApi.size() + 1);
        mApi.resize(mApi.size() + 1);
        configureTestInstance(1, mApi[0].email, mApi[0].pwd);

        NiceMock<MockRequestListener> loginTracker(megaApi[1].get());
        megaApi[1]->login(mApi[1].email.c_str(), mApi[1].pwd.c_str(), &loginTracker);
        ASSERT_TRUE(loginTracker.waitForFinishOrTimeout(MAX_TIMEOUT))
            << "Second session login failed";

        NiceMock<MockRequestListener> fetchNodesTracker(megaApi[1].get());
        megaApi[1]->fetchNodes(&fetchNodesTracker);
        ASSERT_TRUE(fetchNodesTracker.waitForFinishOrTimeout(MAX_TIMEOUT))
            << "Second session fetch nodes failed";
    }

    // Internal values to define the operations in the Device Center
    enum class DeviceCenterOperations
    {
        PAUSE,
        RESUME,
        REMOVE
    };

    bool doChangeFromDeviceCenter(const DeviceCenterOperations operation)
    {
        NiceMock<MockRequestListener> reqTracker{megaApi[1].get()};

        switch (operation)
        {
            case DeviceCenterOperations::PAUSE:
                megaApi[1]->pauseFromBC(mBackupID, &reqTracker);
                break;
            case DeviceCenterOperations::RESUME:
                megaApi[1]->resumeFromBC(mBackupID, &reqTracker);
                break;
            case DeviceCenterOperations::REMOVE:
                megaApi[1]->removeFromBC(mBackupID, INVALID_HANDLE, &reqTracker);
                break;
        }
        return reqTracker.waitForFinishOrTimeout(MAX_TIMEOUT);
    }
};

class SdkTestDeviceCenterFullSync: public SdkTestDeviceCenter
{
public:
    void SetUp() override
    {
        SdkTestDeviceCenter::SetUp();

        ASSERT_NO_FATAL_FAILURE(setupFullSync());
    }

    void TearDown() override
    {
        if (const std::unique_ptr<MegaSync> sync{megaApi[0]->getSyncByBackupId(mBackupID)}; sync)
        {
            removeSync(megaApi[0].get(), mBackupID);
        }
        SdkTestDeviceCenter::TearDown();
    }

private:
    void setupFullSync()
    {
        LOG_debug << "Creating a full account sync";
        const unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
        mBackupID =
            syncFolder(megaApi[0].get(), getLocalFolder().u8string(), rootnode->getHandle());
        ASSERT_NE(mBackupID, INVALID_HANDLE) << "Invalid full-sync ID";
    }
};

/**
 * @brief Exercises the pause, resume and remove Device Center operations from a second session
 */
TEST_F(SdkTestDeviceCenterFullSync, FullSyncOperations)
{
    static const auto logPre{getLogPrefix()};

    // Pause the sync from the second session
    LOG_debug << logPre << "Pause full-sync from the Device Center";
    ASSERT_TRUE(pauseFromDeviceCenter()) << "Failed to pause full-sync from the second session";

    ASSERT_TRUE(waitForSyncStateFromMain(MegaSync::RUNSTATE_SUSPENDED))
        << "Full-sync not paused after 30 seconds";

    // Wait a while (for the *!sds user attr to be updated and propagated in response).
    std::this_thread::sleep_for(std::chrono::seconds{5});

    // Resume the sync from the second session
    LOG_debug << logPre << "Resume full-sync from the Device Center";
    ASSERT_TRUE(resumeFromDeviceCenter()) << "Failed to resume full-sync from the second session";

    ASSERT_TRUE(waitForSyncStateFromMain(MegaSync::RUNSTATE_RUNNING))
        << "Full-sync not resumed after 30 seconds";

    // Wait a while (for the *!sds user attr to be updated and propagated in response).
    std::this_thread::sleep_for(std::chrono::seconds{5});

    // Delete the sync from the second session
    LOG_debug << logPre << "Remove full-sync from the Device Center";

    NiceMock<MockSyncListener> listener;
    const auto hasExpectedId = Pointee(Property(&MegaSync::getBackupId, mBackupID));
    std::promise<void> removed;
    EXPECT_CALL(listener, onSyncDeleted(_, hasExpectedId))
        .WillOnce(
            [&removed]
            {
                removed.set_value();
            });
    megaApi[0]->addListener(&listener);

    ASSERT_TRUE(deleteFromDeviceCenter()) << "Failed to delete full-sync from the second session";
    ASSERT_EQ(removed.get_future().wait_for(MAX_TIMEOUT), std::future_status::ready)
        << "Full-sync still exists after 3 minutes";

    megaApi[0]->removeListener(&listener);
}

#endif // ENABLE_SYNC
