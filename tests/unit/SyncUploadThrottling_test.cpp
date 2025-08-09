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

    static constexpr size_t DEFAULT_SIZE{50};
    static constexpr m_time_t DEFAULT_MTIME{50};

    const NodeHandle mDummyHandle{};
    const std::string mNodeName{"testNode"};
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


#endif // ENABLE_SYNC
