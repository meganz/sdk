#include "mega/megaclient.h"
#include "mega/syncinternals/syncuploadthrottlingfile.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>

using namespace mega;
using namespace testing;

static constexpr std::chrono::seconds DEFAULT_UPLOAD_COUNTER_INACTIVITY_EXPIRATION_TIME{10};
static constexpr unsigned DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE{2};

/**
 * @brief Mock class for SyncThreadsafeState
 *
 * The purpose is to be able to have a real SyncUpload_inClient, which is feasible for our unit
 * tests, and only mock the SyncThreadsafeState attribute. This mocked class will also be used to
 * set some expectations on calls that we know that should be triggered upon new code flows, like
 * updating the fingerprint when the file has changed while it was delayed/throttled and not
 * started.
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
    MOCK_METHOD(void, removeExpectedUpload, (NodeHandle h, const std::string& name), (override));
};

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
 * @brief Creates the SyncUpload_inClient object along with the MockSyncThreadsafeState.
 *
 * The MockSyncThreadsafeState is an attribute of SyncUpload_inClient which is mocked for this test,
 * so we don't need to mock the whole SyncUpload_inClient. Also, this function adds the necessary
 * expectation of the transferBegin method which is called during the SyncUpload_inClient, as the
 * expectation needs to be added before the constructor. That expectation is necessary in order to
 * correctly check other later expectations which are specific of the new changes, like the
 * fingerprint update.
 */
std::pair<shared_ptr<SyncUpload_inClient>, std::shared_ptr<MockSyncThreadsafeState>>
    createSyncUploadWithExpectations()
{
    // Arrange
    const handle dummyBackupId{1};
    MegaClient* const dummyClient{nullptr};
    const bool canChangeVault{false};
    const auto mockSyncThreadsafeState =
        std::make_shared<StrictMock<MockSyncThreadsafeState>>(dummyBackupId,
                                                              dummyClient,
                                                              canChangeVault);

    const NodeHandle dummyHandle;
    const LocalPath dummyFullPath;
    const std::string nodeName{"testNode"};
    const FileFingerprint initialFingerprint =
        generateFingerprint(50, 60); // Use some random values for size (50) and mtime (60)
    const handle fsid{123};
    const LocalPath dummyLocalName;
    const bool fromInshare{false};

    // Set expectations
    EXPECT_CALL(*mockSyncThreadsafeState, transferBegin(PUT, initialFingerprint.size)).Times(1);

    // Create the SyncUpload_inClient
    const auto syncUpload = std::make_shared<SyncUpload_inClient>(dummyHandle,
                                                                  dummyFullPath,
                                                                  nodeName,
                                                                  initialFingerprint,
                                                                  mockSyncThreadsafeState,
                                                                  fsid,
                                                                  dummyLocalName,
                                                                  fromInshare);
    syncUpload->wasRequesterAbandoned = true; // We do not complete uploads.

    return {syncUpload, mockSyncThreadsafeState};
}

/**
 * @brief Increases the upload counter nTimes.
 *
 * @param UploadThrottlingFile The throttling file object whose upload counter must be increased.
 * @param nTimes The number of times to increase the counter.
 */
void increaseUploadCounter(UploadThrottlingFile& throttlingFile,
                           const unsigned nTimes = DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE)
{
    for ([[maybe_unused]] const auto _: range(nTimes))
    {
        throttlingFile.increaseUploadCounter();
    }
}
}

// Test cases

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
    std::this_thread::sleep_for(std::chrono::seconds(3)); // Simulate delay

    ASSERT_FALSE(throttlingFile.checkUploadThrottling(DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE,
                                                      std::chrono::seconds(2)));
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
 * @test Verifies that no abort occurs when putnodes have started.
 */
TEST(UploadThrottlingFileTest, HandleAbortUploadNoAbortWhenPutnodesStarted)
{
    InSequence seq; // We want the expectations to be called in order.

    const auto [syncUpload, mockSyncThreadsafeState] = createSyncUploadWithExpectations();
    syncUpload->putnodesStarted = true;

    const FileFingerprint dummyFingerprint;
    const LocalPath dummyFullPath;
    UploadThrottlingFile throttlingFile;

    EXPECT_CALL(*mockSyncThreadsafeState, transferFailed(PUT, syncUpload->fingerprint().size))
        .Times(1);
    EXPECT_CALL(*mockSyncThreadsafeState, removeExpectedUpload(syncUpload->h, syncUpload->name))
        .Times(1);
    ASSERT_FALSE(throttlingFile.handleAbortUpload(*syncUpload,
                                                  dummyFingerprint,
                                                  DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE,
                                                  dummyFullPath));
}

/**
 * @test Verifies that no abort occurs when upload is completed but it wasn't processed yet when
 * calling handleAbortUpload.
 */
TEST(UploadThrottlingFileTest, HandleAbortUploadNoAbortWhenUploadIsCompleted)
{
    InSequence seq; // We want the expectations to be called in order.

    const auto [syncUpload, mockSyncThreadsafeState] = createSyncUploadWithExpectations();
    syncUpload->putnodesStarted = true;
    syncUpload->wasCompleted = true;
    syncUpload->wasPutnodesCompleted = true;
    syncUpload->wasRequesterAbandoned = false;

    const FileFingerprint dummyFingerprint;
    const LocalPath dummyFullPath;
    UploadThrottlingFile throttlingFile;

    EXPECT_CALL(*mockSyncThreadsafeState, transferComplete(PUT, syncUpload->fingerprint().size))
        .Times(1);
    EXPECT_CALL(*mockSyncThreadsafeState, removeExpectedUpload(syncUpload->h, syncUpload->name))
        .Times(1);
    ASSERT_FALSE(throttlingFile.handleAbortUpload(*syncUpload,
                                                  dummyFingerprint,
                                                  DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE,
                                                  dummyFullPath));
}

/**
 * @test Verifies that no abort occurs when the upload hasn't started and the fingerprint is
 * updated. The fingerprint is checked before and after handleUbortUpload to ensure it is correctly
 * updated.
 */
TEST(UploadThrottlingFileTest, HandleAbortUploadNoAbortWhenNotStartedAndUpdateFingerprint)
{
    InSequence seq; // We want the expectations to be called in order.

    const auto [syncUpload, mockSyncThreadsafeState] = createSyncUploadWithExpectations();

    const FileFingerprint newFingerprint = generateFingerprint(100, 20);
    const LocalPath dummyFullPath;
    UploadThrottlingFile throttlingFile;

    EXPECT_CALL(*mockSyncThreadsafeState, transferFailed(PUT, syncUpload->fingerprint().size))
        .Times(1);
    EXPECT_CALL(*mockSyncThreadsafeState, transferBegin(PUT, newFingerprint.size)).Times(1);
    EXPECT_CALL(*mockSyncThreadsafeState, transferFailed(PUT, newFingerprint.size)).Times(1);

    ASSERT_NE(newFingerprint.size, syncUpload->fingerprint().size);
    ASSERT_NE(newFingerprint.mtime, syncUpload->fingerprint().mtime);

    ASSERT_FALSE(throttlingFile.handleAbortUpload(*syncUpload,
                                                  newFingerprint,
                                                  DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE,
                                                  dummyFullPath));

    ASSERT_EQ(newFingerprint.size, syncUpload->fingerprint().size);
    ASSERT_EQ(newFingerprint.mtime, syncUpload->fingerprint().mtime);
}

/**
 * @test Verifies that the upload must be aborted if it started but putnodes does not.
 * Case 1: The upload counter did not reach DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE and the upload must
 * bypass throttling logic next time.
 */
TEST(UploadThrottlingFileTest, HandleAbortUploadDoNotSetBypassFlag)
{
    InSequence seq; // We want the expectations to be called in order.

    const auto [syncUpload, mockSyncThreadsafeState] = createSyncUploadWithExpectations();
    syncUpload->wasStarted = true;

    const FileFingerprint dummyFingerprint;
    const LocalPath dummyFullPath;
    UploadThrottlingFile throttlingFile;
    increaseUploadCounter(throttlingFile, DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE - 1);

    EXPECT_CALL(*mockSyncThreadsafeState, transferFailed(PUT, syncUpload->fingerprint().size))
        .Times(1);
    ASSERT_TRUE(throttlingFile.handleAbortUpload(*syncUpload,
                                                 dummyFingerprint,
                                                 DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE,
                                                 dummyFullPath));
    ASSERT_FALSE(throttlingFile.willBypassThrottlingNextTime());
}

/**
 * @test Verifies that the upload must be aborted if it started but putnodes does not.
 * Case 2: The upload counter reached DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE and the upload must
 * bypass throttling logic next time.
 */
TEST(UploadThrottlingFileTest, HandleAbortUploadAndSetBypassFlag)
{
    InSequence seq; // We want the expectations to be called in order.

    const auto [syncUpload, mockSyncThreadsafeState] = createSyncUploadWithExpectations();
    syncUpload->wasStarted = true;

    const FileFingerprint dummyFingerprint;
    const LocalPath dummyFullPath;
    UploadThrottlingFile throttlingFile;
    increaseUploadCounter(throttlingFile, DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE);

    EXPECT_CALL(*mockSyncThreadsafeState, transferFailed(PUT, syncUpload->fingerprint().size))
        .Times(1);
    ASSERT_TRUE(throttlingFile.handleAbortUpload(*syncUpload,
                                                 dummyFingerprint,
                                                 DEFAULT_MAX_UPLOADS_BEFORE_THROTTLE,
                                                 dummyFullPath));
    ASSERT_TRUE(throttlingFile.willBypassThrottlingNextTime());
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
