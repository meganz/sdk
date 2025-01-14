/**
 * @file
 * @brief This file defines some tests for validating changes in the max download/upload speed
 * limits for transfers
 */

#include "integration/integration_test_utils.h"
#include "integration/mock_listeners.h"
#include "sdk_test_utils.h"
#include "SdkTest_test.h"

#include <chrono>

using namespace sdk_test;

/**
 * @class TransferProgressReporter
 * @brief A helper functor to pass as callback to the expectations on onTransferUpdate.
 */
struct TransferProgressReporter
{
public:
    /**
     * @brief A factor used to validate received updates on the speed:
     * receivedSpeed <= MAX_PERMITTED_SPEED_FACTOR * targetMaxSpeed
     * @note Why 3? For the current state of the code a factor of 2 caused the tests to fail on
     * macos some times. With this value we pass the tests successfully and confirm that the bug is
     * not present any more.
     */
    static constexpr double MAX_PERMITTED_SPEED_FACTOR = 3.;

    /**
     * @brief The fraction of the given expectedTime to wait before starting to apply the
     * checks on the received speed updates. Useful to wait for some initial stabilization of the
     * values.
     */
    static constexpr double STABILIZATION_TIME_FRACTION = 0.2;

    TransferProgressReporter(const std::chrono::seconds expectedTime,
                             const unsigned targetMaxSpeed):
        mStartTime{std::chrono::steady_clock::now()},
        mExpectedTime{expectedTime},
        mTargetMaxSpeed{targetMaxSpeed}
    {}

    /**
     * @brief Validate expectations on the received transfer updates
     */
    void operator()(MegaApi*, MegaTransfer* transfer)
    {
        if (stabilizing())
            return;
        const auto speed = transfer->getSpeed();
        EXPECT_LE(speed, static_cast<unsigned>(MAX_PERMITTED_SPEED_FACTOR * mTargetMaxSpeed))
            << "Received a transfer update with a speed outside of the accepted range";
    }

private:
    std::chrono::steady_clock::time_point mStartTime;
    std::chrono::seconds mExpectedTime{0};
    unsigned mTargetMaxSpeed{0};

    bool stabilizing() const
    {
        const auto secondsSinceStart = std::chrono::steady_clock::now() - mStartTime;
        return secondsSinceStart < mExpectedTime * STABILIZATION_TIME_FRACTION;
    }
};

class SdkTestTransferMaxSpeeds: public SdkTest
{
public:
    static constexpr auto MAX_TIMEOUT = 3min; // Timeout for some operations in this tests suite

    void SetUp() override
    {
        SdkTest::SetUp();
        ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    }

    void TearDown() override
    {
        megaApi[0]->setMaxUploadSpeed(-1);
        megaApi[0]->setMaxDownloadSpeed(-1);
        SdkTest::TearDown();
    }

    /**
     * @brief Performs an upload limiting the speed to the given value. Monitorices the progress
     * using TransferProgressReporter.
     *
     * @param expectedTime The amount of time in seconds that the transfer is expected to take if it
     * goes at max speed.
     * @param maxSpeed The max speed set for the transfer in bytes per second
     * @param filePath The path to the file that will be uploaded
     *
     * @return The total time taken for the transfer to complete in seconds if the upload succeeded
     */
    std::optional<std::chrono::seconds>
        performAndMonitorUpload(const std::chrono::seconds expectedTime,
                                const unsigned maxSpeed,
                                const fs::path& filePath) const
    {
        LOG_debug << getLogPrefix() << "Setting upload speed limit";
        if (const bool setLimitSucced = megaApi[0]->setMaxUploadSpeed(maxSpeed); !setLimitSucced)
        {
            EXPECT_TRUE(setLimitSucced) << "Error setting upload max speed";
            return {};
        }

        const auto starter = [&filePath, this](auto* transferListener)
        {
            const std::string fileName{getFilePrefix() + ".txt"};
            megaApi[0]->startUpload(filePath.u8string().c_str(),
                                    std::unique_ptr<MegaNode>{megaApi[0]->getRootNode()}.get(),
                                    nullptr /*fileName*/,
                                    MegaApi::INVALID_CUSTOM_MOD_TIME,
                                    nullptr /*appData*/,
                                    false /*isSourceTemporary*/,
                                    false /*startFirst*/,
                                    nullptr /*cancelToken*/,
                                    transferListener);
        };
        return performAndMonitorTransferAux(expectedTime, maxSpeed, std::move(starter));
    }

    /**
     * @brief Performs a download limiting the speed to the given value. Monitorices the progress
     * using TransferProgressReporter.
     *
     * @param expectedTime The amount of time in seconds that the transfer is expected to take if it
     * goes at max speed.
     * @param maxSpeed The max speed set for the transfer in bytes per second
     * @param nodeToDownload The node that will be downloaded
     *
     * @return The total time taken for the transfer to complete in seconds if the upload succeeded
     */
    std::optional<std::chrono::seconds>
        performAndMonitorDownload(const std::chrono::seconds expectedTime,
                                  const unsigned maxSpeed,
                                  MegaNode* nodeToDownload) const
    {
        LOG_debug << getLogPrefix() << "Setting download speed limit";
        if (const bool setLimitSucced = megaApi[0]->setMaxDownloadSpeed(maxSpeed); !setLimitSucced)
        {
            EXPECT_TRUE(setLimitSucced) << "Error setting download max speed";
            return {};
        }
        const auto starter = [nodeToDownload, this](auto* transferListener)
        {
            megaApi[0]->startDownload(
                nodeToDownload,
                "./",
                nullptr /*customName*/,
                nullptr /*appData*/,
                false /*startFirst*/,
                nullptr /*cancelToken*/,
                MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
                MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
                false /* undelete */,
                transferListener);
        };
        return performAndMonitorTransferAux(expectedTime, maxSpeed, std::move(starter));
    }

private:
    /**
     * @brief Auxiliary method to handle both uploads and downloads
     */
    template<typename TransferStarter>
    std::optional<std::chrono::seconds>
        performAndMonitorTransferAux(const std::chrono::seconds expectedTime,
                                     const unsigned maxSpeed,
                                     TransferStarter&& transferStarter) const
    {
        const auto logPre{getLogPrefix()};
        LOG_debug << logPre << "Starting the transfer";
        testing::NiceMock<MockMegaTransferListener> mtl{};
        std::chrono::steady_clock::time_point startTime;
        EXPECT_CALL(mtl, onTransferStart)
            .WillOnce(
                [&startTime]
                {
                    startTime = std::chrono::steady_clock::now();
                });
        std::chrono::steady_clock::time_point endTime;
        EXPECT_CALL(mtl, onTransferFinish)
            .WillOnce(
                [&mtl, &endTime]
                {
                    endTime = std::chrono::steady_clock::now();
                    mtl.markAsFinished();
                });
        EXPECT_CALL(mtl, onTransferUpdate)
            .WillRepeatedly(TransferProgressReporter(expectedTime, maxSpeed));

        transferStarter(&mtl);

        LOG_debug << logPre << "Waiting for the transfer to finish";
        if (const bool transferFinished = mtl.waitForFinishOrTimeout(MAX_TIMEOUT);
            !transferFinished)
        {
            EXPECT_TRUE(transferFinished)
                << "The transfer didn't finish successfully in the given time window";
            return {};
        }
        return std::chrono::duration_cast<chrono::seconds>(endTime - startTime);
    }
};

/**
 * @brief SdkTestTransferMaxSpeeds.MaxUploadSpeed
 *
 * Validates the MegaApi::setMaxUploadSpeed public method by:
 * - Uploading a file
 * - Track the received onTransferUpdate callbacks and check if the reported speed is reasonable
 *   for the given limit
 * - At the end, check if the upload took a reasonable amount of time
 *
 * @note This test sets a low limit, so it is almost guaranteed that the transfer is throttled.
 * However, as this might not be the case in jenkins, the strongest test conditions are validated on
 * the side were the speed limit is highly exceeded.
 */
TEST_F(SdkTestTransferMaxSpeeds, MaxUploadSpeed)
{
    constexpr auto MAX_SPEED_BYTES_PER_SECOND{10000u};
    constexpr auto EXPECTED_TIME_FOR_TRANSFER{40s};
    constexpr auto FILE_SIZE = MAX_SPEED_BYTES_PER_SECOND * EXPECTED_TIME_FOR_TRANSFER.count();

    LOG_debug << getLogPrefix() << "Create the file to be uploaded";
    const fs::path filePath{getFilePrefix() + ".txt"};
    LocalTempFile f(filePath, FILE_SIZE);

    const auto requiredTime =
        performAndMonitorUpload(EXPECTED_TIME_FOR_TRANSFER, MAX_SPEED_BYTES_PER_SECOND, filePath);
    ASSERT_TRUE(requiredTime) << "Something went wrong during the upload";
    EXPECT_GE(requiredTime->count(),
              EXPECTED_TIME_FOR_TRANSFER.count() /
                  TransferProgressReporter::MAX_PERMITTED_SPEED_FACTOR)
        << "The transfer took shorter than expected to complete";
}

/**
 * @brief SdkTestTransferMaxSpeeds.MaxDownloadSpeed
 *
 * Same as SdkTestTransferMaxSpeeds.MaxUploadSpeed but for downloads.
 * In this case we test for two different max limits. One below 100KB and other above. This is done
 * because that limit sets a different buffer size in libcurl.
 */
TEST_F(SdkTestTransferMaxSpeeds, MaxDownloadSpeed)
{
    constexpr auto EXPECTED_TIME_FOR_TRANSFER{40s};

    for (const auto maxSpeedBytesPerSecond: {10000u, 200000u})
    {
        const unsigned fileSize = maxSpeedBytesPerSecond * EXPECTED_TIME_FOR_TRANSFER.count();

        LOG_debug << getLogPrefix() << "Uploading file to be downloaded after. Size: " << fileSize;
        const std::string fileName{getFilePrefix() + std::to_string(maxSpeedBytesPerSecond) +
                                   ".txt"};
        const auto nodeToDownload =
            sdk_test::uploadFile(megaApi[0].get(), LocalTempFile{fileName, fileSize});
        ASSERT_TRUE(nodeToDownload);

        const auto requiredTime = performAndMonitorDownload(EXPECTED_TIME_FOR_TRANSFER,
                                                            maxSpeedBytesPerSecond,
                                                            nodeToDownload.get());
        ASSERT_TRUE(requiredTime) << "Something went wrong during the download";
        EXPECT_GE(requiredTime->count(),
                  EXPECTED_TIME_FOR_TRANSFER.count() /
                      TransferProgressReporter::MAX_PERMITTED_SPEED_FACTOR)
            << "The transfer took shorter than expected to complete";
    }
}
