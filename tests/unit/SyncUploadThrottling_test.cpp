/**
 * @file SyncUploadThrottling_test.cpp
 * @brief This file is expected to contain unit tests involving SyncUploadThrottlingFile logic.
 */

#ifdef ENABLE_SYNC

#include "mega/megaclient.h"
#include "mega/syncinternals/syncuploadthrottlingfile.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>

using namespace mega;
using namespace testing;

static constexpr std::chrono::seconds DEFAULT_UPLOAD_COUNTER_INACTIVITY_EXPIRATION_TIME{10};
static constexpr unsigned DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE{2};

namespace
{

/**
 * @brief Generates a file fingerprint with the specified size and modification time.
 */
FileFingerprint generateFingerprint(const m_off_t size, const m_time_t mtime)
{
    FileFingerprint fingerprint;
    fingerprint.size = size;
    fingerprint.mtime = mtime;
    return fingerprint;
}

/**
 * @brief Fixture to test SyncUpload_inClient with the mocked MockSyncThreadsafeState.
 *
 * This fixture is useful to test changes in the SyncUpload_inClient for in-flight uploads or
 * delayed uploads, testing that the abortion or adjustement logic works as expected.
 *
 * The MockSyncThreadsafeState is an attribute of SyncUpload_inClient which is mocked for this
 * fixture, so we don't need to mock the whole SyncUpload_inClient. Also, it uses a StrictMock to
 * ensure the order of the expectations, such as the one for the transferBegin method which is
 * called during the SyncUpload_inClient instantiation. That initial expectation is necessary in
 * order to correctly check other later expectations which are specific of the new changes, like the
 * fingerprint update.
 *
 * Apart from using a StrictMock, the expectations are forced to be checked in order with
 * InSequence.
 */
class UploadThrottlingFileChangesTest: public Test
{
protected:
    /**
     * @brief Mock class for SyncThreadsafeState
     *
     * The purpose is to be able to have a real SyncUpload_inClient, which is feasible for our unit
     * tests, and only mock the SyncThreadsafeState attribute.
     */
    class MockSyncThreadsafeState: public SyncThreadsafeState
    {
    public:
        MockSyncThreadsafeState(const handle backupId,
                                MegaClient* const client,
                                const bool canChangeVault):
            SyncThreadsafeState(backupId, client, canChangeVault)
        {}

        // Mock methods from SyncThreadsafeState
        MOCK_METHOD(void, transferBegin, (direction_t direction, m_off_t numBytes), (override));
        MOCK_METHOD(void, transferComplete, (direction_t direction, m_off_t numBytes), (override));
        MOCK_METHOD(void, transferFailed, (direction_t direction, m_off_t numBytes), (override));
        MOCK_METHOD(void,
                    removeExpectedUpload,
                    (NodeHandle h, const std::string& name),
                    (override));
    };

    void SetUp() override
    {
        const handle dummyBackupId{1};
        MegaClient* const dummyClient{nullptr};
        const bool canChangeVault{false};
        mMockSyncThreadsafeState =
            std::make_shared<StrictMock<MockSyncThreadsafeState>>(dummyBackupId,
                                                                  dummyClient,
                                                                  canChangeVault);

        // Add the initial expectation that should be triggered during SyncUpload_inClient
        // instantiation.
        EXPECT_CALL(*mMockSyncThreadsafeState, transferBegin(PUT, mInitialFingerprint.size))
            .Times(1);
    }

    /**
     * @brief Create and initialize the SyncUpload_inClient with the MockSyncThreadSafeState.
     * This method should be called right after adding all the necessary expectations.
     *
     * @warning No additional expectations should be added after calling this method.
     */
    void initializeSyncUpload_inClient()
    {
        const LocalPath dummyFullPath;
        const handle fsid{123};
        const LocalPath dummyLocalName;
        const bool fromInshare{false};

        // Create the SyncUpload_inClient
        mSyncUpload = std::make_shared<SyncUpload_inClient>(mDummyHandle,
                                                            dummyFullPath,
                                                            mNodeName,
                                                            mInitialFingerprint,
                                                            mMockSyncThreadsafeState,
                                                            fsid,
                                                            dummyLocalName,
                                                            fromInshare);
        mSyncUpload->wasRequesterAbandoned = true; // We do not finish uploads.
    }

    static constexpr bool DEFAULT_TRANSFER_DIRECTION_NEEDS_TO_CHANGE{false};
    static constexpr size_t DEFAULT_SIZE{50};
    static constexpr m_time_t DEFAULT_MTIME{50};

    const NodeHandle mDummyHandle{};
    const std::string mNodeName{"testNode"};
    const LocalPath mDummyLocalName{};
    const FileFingerprint mInitialFingerprint{generateFingerprint(DEFAULT_SIZE, DEFAULT_MTIME)};
    const FileFingerprint mDummyFingerprint;
    const LocalPath mDummyFullPath;
    UploadThrottlingFile mThrottlingFile;

    InSequence mSeq; // We want the expectations to be called in order.
    std::shared_ptr<MockSyncThreadsafeState> mMockSyncThreadsafeState;
    shared_ptr<SyncUpload_inClient> mSyncUpload;
};

/**
 * @brief Increases the upload counter nTimes.
 *
 * @param UploadThrottlingFile The throttling file object whose upload counter must be increased.
 * @param nTimes The number of times to increase the counter.
 */
void increaseUploadCounter(UploadThrottlingFile& throttlingFile, const unsigned nTimes)
{
    for (auto i = nTimes; i--;)
    {
        throttlingFile.increaseUploadCounter();
    }
}
} // namespace

// UploadThrottlingFileTest test cases

/**
 * @test Verifies that the upload counter method increases the counter correctly.
 */
TEST(UploadThrottlingFileTest, IncreaseUploadCounterIncrementsCounter)
{
    // Initial state
    UploadThrottlingFile throttlingFile;
    ASSERT_EQ(throttlingFile.uploadCounter(), 0);

    // Increase the counter and check expectations.
    constexpr unsigned numIncreases = 2;
    increaseUploadCounter(throttlingFile, numIncreases);
    ASSERT_EQ(throttlingFile.uploadCounter(), numIncreases);
}

/**
 * @test Verifies that the upload counter resets after inactivity.
 */
TEST(UploadThrottlingFileTest, CheckUploadThrottlingResetsCounterAfterInactivity)
{
    UploadThrottlingFile throttlingFile;
    increaseUploadCounter(throttlingFile, DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE);

    const auto uploadCounterInactivityExpirationTime = std::chrono::seconds(2);
    std::this_thread::sleep_for(
        uploadCounterInactivityExpirationTime +
        std::chrono::seconds(1)); // Wait enough time to exceed the inactivity expiration time.

    ASSERT_FALSE(throttlingFile.checkUploadThrottling(DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE,
                                                      uploadCounterInactivityExpirationTime));
}

/**
 * @test Verifies that throttling is applied when max uploads are exceeded.
 */
TEST(UploadThrottlingFileTest, CheckUploadThrottlingExceedsMaxUploads)
{
    UploadThrottlingFile throttlingFile;
    increaseUploadCounter(throttlingFile, DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE);

    ASSERT_TRUE(
        throttlingFile.checkUploadThrottling(DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE,
                                             DEFAULT_UPLOAD_COUNTER_INACTIVITY_EXPIRATION_TIME));
}

/**
 * @test Verifies that the bypass flag is respected during throttling checks.
 * 1. First call should not bypass throttling.
 * 2. After setting the flag, next call should bypass throttling.
 * 3. Next call after that should not bypass throttling (flag is reset).
 */
TEST(UploadThrottlingFileTest, CheckUploadThrottlingBypassFlag)
{
    UploadThrottlingFile throttlingFile;
    increaseUploadCounter(throttlingFile, DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE);

    // First call should not bypass throttling.
    ASSERT_TRUE(
        throttlingFile.checkUploadThrottling(DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE,
                                             DEFAULT_UPLOAD_COUNTER_INACTIVITY_EXPIRATION_TIME));

    // Set bypass flag to true.
    throttlingFile.bypassThrottlingNextTime(DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE);

    // Next call should bypass throttling.
    ASSERT_FALSE(
        throttlingFile.checkUploadThrottling(DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE,
                                             DEFAULT_UPLOAD_COUNTER_INACTIVITY_EXPIRATION_TIME));

    // Subsequent calls should not bypass.
    ASSERT_TRUE(
        throttlingFile.checkUploadThrottling(DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE,
                                             DEFAULT_UPLOAD_COUNTER_INACTIVITY_EXPIRATION_TIME));
}

// UploadThrottlingFileChangesTest test cases

/**
 * @test Verifies that no abort occurs when putnodes have started.
 */
TEST_F(UploadThrottlingFileChangesTest, HandleAbortUploadNoAbortWhenPutnodesStarted)
{
    EXPECT_CALL(*mMockSyncThreadsafeState, transferFailed(PUT, mInitialFingerprint.size)).Times(1);
    EXPECT_CALL(*mMockSyncThreadsafeState, removeExpectedUpload(mDummyHandle, mNodeName)).Times(1);

    initializeSyncUpload_inClient();
    mSyncUpload->putnodesStarted = true;

    ASSERT_FALSE(mThrottlingFile.handleAbortUpload(*mSyncUpload,
                                                   DEFAULT_TRANSFER_DIRECTION_NEEDS_TO_CHANGE,
                                                   mDummyFingerprint,
                                                   DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE,
                                                   mDummyFullPath));
}

/**
 * @test Verifies that no abort occurs when upload is completed but it wasn't processed yet when
 * calling handleAbortUpload.
 */
TEST_F(UploadThrottlingFileChangesTest, HandleAbortUploadNoAbortWhenUploadIsCompleted)
{
    EXPECT_CALL(*mMockSyncThreadsafeState, transferComplete(PUT, mInitialFingerprint.size))
        .Times(1);
    EXPECT_CALL(*mMockSyncThreadsafeState, removeExpectedUpload(mDummyHandle, mNodeName)).Times(1);

    initializeSyncUpload_inClient();
    mSyncUpload->putnodesStarted = true;
    mSyncUpload->wasCompleted = true;
    mSyncUpload->wasPutnodesCompleted = true;
    mSyncUpload->wasRequesterAbandoned = false;

    ASSERT_FALSE(mThrottlingFile.handleAbortUpload(*mSyncUpload,
                                                   DEFAULT_TRANSFER_DIRECTION_NEEDS_TO_CHANGE,
                                                   mDummyFingerprint,
                                                   DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE,
                                                   mDummyFullPath));
}

/**
 * @test Verifies that no abort occurs when the upload hasn't started and the fingerprint is
 * updated. The fingerprint is checked before and after handleAbortUpload to ensure it is correctly
 * updated.
 */
TEST_F(UploadThrottlingFileChangesTest, HandleAbortUploadNoAbortWhenNotStartedAndUpdateFingerprint)
{
    const FileFingerprint newFingerprint = generateFingerprint(100, 20);

    EXPECT_CALL(*mMockSyncThreadsafeState, transferFailed(PUT, mInitialFingerprint.size)).Times(1);
    EXPECT_CALL(*mMockSyncThreadsafeState, transferBegin(PUT, newFingerprint.size)).Times(1);
    EXPECT_CALL(*mMockSyncThreadsafeState, transferFailed(PUT, newFingerprint.size)).Times(1);

    initializeSyncUpload_inClient();

    ASSERT_NE(newFingerprint.size, mSyncUpload->fingerprint().size);
    ASSERT_NE(newFingerprint.mtime, mSyncUpload->fingerprint().mtime);

    ASSERT_FALSE(mThrottlingFile.handleAbortUpload(*mSyncUpload,
                                                   DEFAULT_TRANSFER_DIRECTION_NEEDS_TO_CHANGE,
                                                   newFingerprint,
                                                   DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE,
                                                   mDummyFullPath));

    ASSERT_EQ(newFingerprint.size, mSyncUpload->fingerprint().size);
    ASSERT_EQ(newFingerprint.mtime, mSyncUpload->fingerprint().mtime);
}

/**
 * @test Verifies that the upload must be aborted if it started but putnodes does not.
 * Case 1: The upload counter did not reach DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE and the upload must
 * bypass throttling logic next time.
 */
TEST_F(UploadThrottlingFileChangesTest, HandleAbortUploadDoNotSetBypassFlag)
{
    EXPECT_CALL(*mMockSyncThreadsafeState, transferFailed(PUT, mInitialFingerprint.size)).Times(1);

    initializeSyncUpload_inClient();
    mSyncUpload->wasStarted = true;

    increaseUploadCounter(mThrottlingFile, DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE - 1);

    ASSERT_TRUE(mThrottlingFile.handleAbortUpload(*mSyncUpload,
                                                  DEFAULT_TRANSFER_DIRECTION_NEEDS_TO_CHANGE,
                                                  mDummyFingerprint,
                                                  DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE,
                                                  mDummyFullPath));
    ASSERT_FALSE(mThrottlingFile.willBypassThrottlingNextTime());
}

/**
 * @test Verifies that the upload must be aborted if it started but putnodes does not.
 * Case 2: The upload counter reached DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE and the upload must
 * bypass throttling logic next time.
 */
TEST_F(UploadThrottlingFileChangesTest, HandleAbortUploadAndSetBypassFlag)
{
    EXPECT_CALL(*mMockSyncThreadsafeState, transferFailed(PUT, mInitialFingerprint.size)).Times(1);

    initializeSyncUpload_inClient();
    mSyncUpload->wasStarted = true;

    increaseUploadCounter(mThrottlingFile, DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE);

    ASSERT_TRUE(mThrottlingFile.handleAbortUpload(*mSyncUpload,
                                                  DEFAULT_TRANSFER_DIRECTION_NEEDS_TO_CHANGE,
                                                  mDummyFingerprint,
                                                  DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE,
                                                  mDummyFullPath));
    ASSERT_TRUE(mThrottlingFile.willBypassThrottlingNextTime());
}

/**
 * @test Verifies that the upload must be aborted when the transfer direction needs to change (and
 * put nodes has not started).
 */
TEST_F(UploadThrottlingFileChangesTest, HandleAbortUploadAbortDueToTransferDirectionNeedsToChange)
{
    EXPECT_CALL(*mMockSyncThreadsafeState, transferFailed(PUT, mInitialFingerprint.size)).Times(1);

    initializeSyncUpload_inClient();

    constexpr bool transferDirectionNeedsToChange{true};
    ASSERT_TRUE(mThrottlingFile.handleAbortUpload(*mSyncUpload,
                                                  transferDirectionNeedsToChange,
                                                  mDummyFingerprint,
                                                  DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE,
                                                  mDummyFullPath));
}

#endif // ENABLE_SYNC