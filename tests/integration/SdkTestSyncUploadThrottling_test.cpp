/**
 * @file SdkTestSyncUploadThrottling_test.cpp
 * @brief This file is expected to contain tests involving syncs upload throttling.
 */

#ifdef ENABLE_SYNC

#include "integration_test_utils.h"
#include "mega/syncinternals/syncuploadthrottlingmanager.h"
#include "mega/utils.h"
#include "megautils.h"
#include "mock_listeners.h"
#include "sdk_test_utils.h"
#include "SdkTestSyncNodesOperations_test.h"

#include <gmock/gmock.h>

using namespace sdk_test;
using namespace testing;

/**
 * @brief Mock class for IUploadThrottlingManager.
 *
 * The purpose is to trigger expectations on different calls of IUploadThrottlingManager and then
 * forward calls to real implementations of the interface, such as the UploadThrottlingManager used
 * in the Syncs class.
 */
class MockUploadThrottlingManager: public IUploadThrottlingManager
{
public:
    MOCK_METHOD(void, addToDelayedUploads, (DelayedSyncUpload && delayedUpload), (override));

    MOCK_METHOD(void,
                processDelayedUploads,
                (std::function<void(std::weak_ptr<SyncUpload_inClient>&&,
                                    const VersioningOption,
                                    const bool,
                                    const NodeHandle)> &&
                 completion),
                (override));

    MOCK_METHOD(bool, setThrottleUpdateRate, (const unsigned intervalSeconds), (override));

    MOCK_METHOD(bool, setThrottleUpdateRate, (const std::chrono::seconds interval), (override));

    MOCK_METHOD(bool,
                setMaxUploadsBeforeThrottle,
                (const unsigned maxUploadsBeforeThrottle),
                (override));

    MOCK_METHOD(std::chrono::seconds, uploadCounterInactivityExpirationTime, (), (const, override));

    MOCK_METHOD(unsigned, throttleUpdateRate, (), (const, override));

    MOCK_METHOD(unsigned, maxUploadsBeforeThrottle, (), (const, override));

    MOCK_METHOD(ThrottleValueLimits, throttleValueLimits, (), (const, override));

    MOCK_METHOD(std::chrono::seconds, timeSinceLastProcessedUpload, (), (const, override));
};

namespace
{

/**
 * @brief Forwards the necessary MockUploadThrottlingManager methods to use the
 * UploadThrottlingManager methods.
 */
void forwardAllThrottlingMethods(
    const std::shared_ptr<MockUploadThrottlingManager>& mockUploadThrottlingManager,
    const std::shared_ptr<UploadThrottlingManager>& uploadThrottlingManager)
{
    ON_CALL(*mockUploadThrottlingManager, addToDelayedUploads(_))
        .WillByDefault(
            [uploadThrottlingManager](DelayedSyncUpload&& delayedUpload)
            {
                uploadThrottlingManager->addToDelayedUploads(std::move(delayedUpload));
            });

    ON_CALL(*mockUploadThrottlingManager, processDelayedUploads(_))
        .WillByDefault(
            [uploadThrottlingManager](std::function<void(std::weak_ptr<SyncUpload_inClient>&&,
                                                         const VersioningOption,
                                                         const bool,
                                                         const NodeHandle)>&& completion)
            {
                uploadThrottlingManager->processDelayedUploads(std::move(completion));
            });

    ON_CALL(*mockUploadThrottlingManager, uploadCounterInactivityExpirationTime())
        .WillByDefault(
            [uploadThrottlingManager]()
            {
                return uploadThrottlingManager->uploadCounterInactivityExpirationTime();
            });

    ON_CALL(*mockUploadThrottlingManager, throttleUpdateRate())
        .WillByDefault(
            [uploadThrottlingManager]()
            {
                return uploadThrottlingManager->throttleUpdateRate();
            });

    ON_CALL(*mockUploadThrottlingManager, maxUploadsBeforeThrottle())
        .WillByDefault(
            [uploadThrottlingManager]()
            {
                return uploadThrottlingManager->maxUploadsBeforeThrottle();
            });

    ON_CALL(*mockUploadThrottlingManager, throttleValueLimits())
        .WillByDefault(
            [uploadThrottlingManager]()
            {
                return uploadThrottlingManager->throttleValueLimits();
            });

    ON_CALL(*mockUploadThrottlingManager, timeSinceLastProcessedUpload())
        .WillByDefault(
            [uploadThrottlingManager]()
            {
                return uploadThrottlingManager->timeSinceLastProcessedUpload();
            });
}

/**
 * @brief Sets up expectations on transfer requests through the MockTransferListener.
 *
 * @param uploadStarted The promised to be resolved upon onTransferStart()
 * @param uploadFinished The promised to be resolved upon onTransferFinish()
 * @param otherConcurrentCalls Whether there can be other concurrent calls to this method,
 * overlapping expectations. In that case, we add an additional expectation call without expected
 * properties (corresponding to the other concurrent call with different expectations), which is
 * expected to be called at most otherConcurrentCalls times.
 */
void setupMockListenerExpectations(MockTransferListener& mockListener,
                                   const std::string_view fileName,
                                   const MegaHandle parentNodeHandle,
                                   std::promise<void>& uploadStarted,
                                   std::promise<void>& uploadFinished,
                                   const unsigned otherConcurrentCalls)
{
    const auto isMyFile = Pointee(Property(&MegaTransfer::getPath, EndsWith(fileName)));
    const auto isUpload = Pointee(Property(&MegaTransfer::getType, MegaTransfer::TYPE_UPLOAD));
    const auto isBelowDir = Pointee(Property(&MegaTransfer::getParentHandle, parentNodeHandle));
    const auto isOkError = Pointee(Property(&MegaError::getErrorCode, API_OK));

    EXPECT_CALL(mockListener, onTransferStart)
        .Times(AtMost(static_cast<int>(otherConcurrentCalls)));
    EXPECT_CALL(mockListener, onTransferStart(_, AllOf(isMyFile, isUpload, isBelowDir)))
        .WillOnce(
            [&uploadStarted]
            {
                uploadStarted.set_value();
            });

    EXPECT_CALL(mockListener, onTransferFinish)
        .Times(AtMost(static_cast<int>(otherConcurrentCalls)));
    EXPECT_CALL(mockListener, onTransferFinish(_, AllOf(isMyFile, isUpload, isBelowDir), isOkError))
        .WillOnce(
            [&uploadFinished]
            {
                uploadFinished.set_value();
            });
}

/**
 * @brief Helper waiter for onTransferStart()
 *
 * @param timeBeforeFileAction The time point that should have been set right before the action of
 * editing/updating the file which triggers the sync upload.
 */
void waitForUploadStart(std::promise<void>& uploadStarted,
                        const std::chrono::seconds minWaitForTransferStart,
                        const std::chrono::seconds waitForTransferStart,
                        const std::chrono::steady_clock::time_point timeBeforeFileAction)
{
    auto futStart = uploadStarted.get_future();
    if (futStart.wait_for(waitForTransferStart) != std::future_status::ready)
    {
        throw std::runtime_error("The upload didn't start within the timeout");
    }
    if (minWaitForTransferStart.count() &&
        (std::chrono::steady_clock::now() - timeBeforeFileAction < minWaitForTransferStart))
    {
        LOG_debug << "The upload started before the minimum time expected after editing the file. "
                     "Expected min: "
                  << minWaitForTransferStart.count() << " secs. Started at: "
                  << std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::steady_clock::now() - timeBeforeFileAction)
                         .count()
                  << " secs.";
        throw std::runtime_error("The upload started before the minimum time expected");
    }
}

/**
 * @brief Helper waiter for onTransferFinish()
 */
void waitForUploadFinish(std::promise<void>& transferTerminated,
                         const std::chrono::seconds waitForTransferFinish)
{
    auto futFinish = transferTerminated.get_future();
    if (futFinish.wait_for(waitForTransferFinish) != std::future_status::ready)
    {
        throw std::runtime_error("The upload didn't finish within the timeout");
    }
}

/**
 * @brief Helper struct to be used when triggering sync-upload actions and waiting on transfer
 * request events.
 */
struct UploadWaitConfig
{
    static constexpr auto TOLERANCE_SECONDS_FOR_STARTING_UPLOADS{
        30s}; // Time enough for the sync loop to be called, process queueClient() and start the
              // upload.
    static constexpr auto DEFAULT_MIN_WAIT_FOR_TRANSFER_START{0s};
    static constexpr auto DEFAULT_MAX_WAIT_FOR_TRANSFER_FINISH{150s};

    // The minimum expected time to reach onTransferStart(). Zero for no minimum
    std::chrono::seconds minWaitForTransferStart{DEFAULT_MIN_WAIT_FOR_TRANSFER_START};

    // The maximum extra time (added to minWaitForTransferStart) expected to reach onTransferStart()
    std::chrono::seconds maxWaitForTransferStartFromMinWait{TOLERANCE_SECONDS_FOR_STARTING_UPLOADS};

    // The maximum expected time to complete the transfer after it has started
    std::chrono::seconds waitForTransferFinish{DEFAULT_MAX_WAIT_FOR_TRANSFER_FINISH};

    // Total number of other concurrent calls with expectations on global transfer request
    // listeners.
    unsigned otherConcurrentCalls{0};
};

/**
 * @brief Helper method to edit a file and wait for it to be uploaded.
 *
 * @param MegaHandle The handle of the parent directory to upload the file to.
 * @param fileAction The completion function where the file should be edited and other necessary
 * calls should be triggered.
 * @param UploadWaitConfig The configurable time wait values.
 *
 * @see UploadWaitConfig.
 */
void editFileAndWaitForUpload(MegaApi* const api,
                              const std::string_view fileName,
                              const MegaHandle parentNodeHandle,
                              std::function<void()>&& fileAction,
                              const UploadWaitConfig& config = {})
{
    // 1) Set up a MockTransferListener and expectations.
    NiceMock<MockTransferListener> mockListener{};
    std::promise<void> uploadStarted;
    std::promise<void> uploadFinished;
    setupMockListenerExpectations(mockListener,
                                  fileName,
                                  parentNodeHandle,
                                  uploadStarted,
                                  uploadFinished,
                                  config.otherConcurrentCalls);

    // 2) Register the listener with the MegaApi.
    api->addListener(&mockListener);
    const MrProper clean{[&api, &mockListener]()
                         {
                             api->removeListener(&mockListener);
                         }};

    // 3) Call the completion fileAction function to perform now edits or changes in the file.
    const auto timeBeforeFileAction = std::chrono::steady_clock::now();
    fileAction();

    // 4) Wait for upload events.
    waitForUploadStart(uploadStarted,
                       config.minWaitForTransferStart,
                       config.minWaitForTransferStart + config.maxWaitForTransferStartFromMinWait,
                       timeBeforeFileAction);
    waitForUploadFinish(uploadFinished, config.waitForTransferFinish);
}
} // namespace

/**
 * @class SdkTestSyncUploadThrottling
 * @brief Test fixture designed to test operations involving sync upload throttling.
 */
class SdkTestSyncUploadThrottling: public SdkTestSyncNodesOperations
{
public:
    static constexpr auto MAX_TIMEOUT{COMMON_TIMEOUT}; // Timeout for operations in this tests suite

    /**
     * @brief Creates a real and mocked throttling manager and uses the mocked one for the sync
     * engine.
     *
     * 1. Creates the real throttling manager.
     * 2. Creates the mocked throttling manager.
     * 3. Calls createAndSetThrottlingManagers()
     * 4. Returns the real and mocked upload throttling manager for operations in tests.
     */
    std::pair<std::shared_ptr<UploadThrottlingManager>,
              std::shared_ptr<NiceMock<MockUploadThrottlingManager>>>
        createAndSetThrottlingManagers() const
    {
        const auto uploadThrottlingManager = std::make_shared<UploadThrottlingManager>();
        const auto mockUploadThrottlingManager =
            std::make_shared<NiceMock<MockUploadThrottlingManager>>();
        setThrottlingManagers(uploadThrottlingManager, mockUploadThrottlingManager);

        return {uploadThrottlingManager, mockUploadThrottlingManager};
    }

    /**
     * @brief Prepares the real and mocked throttling manager and use the mocked one for the sync
     * engine.
     *
     * 1. Gets the throttle value limits from the real throttling manager.
     * 2. Use the lower limits for the configurable values.
     * 3. Forwards all necessary mocked methods for tests to use the methods from the real
     * throttling manager.
     * 4. Sets the mocked manager to be used on Syncs.
     */
    void setThrottlingManagers(
        const std::shared_ptr<UploadThrottlingManager>& uploadThrottlingManager,
        const std::shared_ptr<MockUploadThrottlingManager>& mockUploadThrottlingManager) const
    {
        // 1) Retrieve throttle values.
        const auto throttleValueLimits = uploadThrottlingManager->throttleValueLimits();

        // 2) Set the minimum values possible.
        const auto updateRateSeconds = throttleValueLimits.throttleUpdateRateLowerLimit;
        const auto maxUploadsBeforeThrottle =
            throttleValueLimits.maxUploadsBeforeThrottleLowerLimit;

        ASSERT_TRUE(uploadThrottlingManager->setThrottleUpdateRate(updateRateSeconds));
        ASSERT_EQ(uploadThrottlingManager->throttleUpdateRate(),
                  throttleValueLimits.throttleUpdateRateLowerLimit);

        ASSERT_TRUE(uploadThrottlingManager->setMaxUploadsBeforeThrottle(maxUploadsBeforeThrottle));
        ASSERT_EQ(uploadThrottlingManager->maxUploadsBeforeThrottle(),
                  throttleValueLimits.maxUploadsBeforeThrottleLowerLimit);

        LOG_debug << "[SdkTestSyncUploadThrottling] Throttle rate: " << updateRateSeconds
                  << " , maxUploadsBeforeThrottle: " << maxUploadsBeforeThrottle;

        // 3) Forward all throttling methods from the mocked UTM to the real UTM.
        forwardAllThrottlingMethods(mockUploadThrottlingManager, uploadThrottlingManager);

        // 4) Now set up the mock in the client.
        std::promise<void> setMockThrottlingManagerPromise;
        auto client = megaApi[0]->getClient();
        client->setSyncUploadThrottlingManager(mockUploadThrottlingManager,
                                               [&setMockThrottlingManagerPromise](const error e)
                                               {
                                                   EXPECT_EQ(e, API_OK);
                                                   setMockThrottlingManagerPromise.set_value();
                                               });

        // 5) Wait for the operation to finish.
        ASSERT_EQ(setMockThrottlingManagerPromise.get_future().wait_for(MAX_TIMEOUT),
                  std::future_status::ready)
            << "The upload throttling manager set operation has timed out";
    }

    /**
     * @brief Calls MegaApi::setThrottleUpdateRate with parametrizable limits and expected
     * errors.
     *
     * @param maxUploadsBeforeThrottle The limit of allowed uploads before throttling the file.
     * @param expectedError The expected error for the MegaApi::setSyncMaxUploadsBeforeThrottle
     * result.
     */
    void setThrottleUpdateRate(const unsigned throttleUpdateRate, const error expectedError)
    {
        // Expectations on the request listener.
        NiceMock<MockRequestListener> mockReqListener;
        mockReqListener.setErrorExpectations(expectedError,
                                             _,
                                             MegaRequest::TYPE_SET_SYNC_UPLOAD_THROTTLE_VALUES);

        // Code execution.
        megaApi[0]->setSyncThrottleUpdateRate(throttleUpdateRate, &mockReqListener);

        // Wait for everything to finish.
        ASSERT_TRUE(mockReqListener.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    /**
     * @brief Calls MegaApi::setSyncMaxUploadsBeforeThrottle with parametrizable limits and expected
     * errors.
     *
     * @param maxUploadsBeforeThrottle The limit of allowed uploads before throttling the file.
     * @param expectedError The expected error for the MegaApi::setSyncMaxUploadsBeforeThrottle
     * result.
     */
    void setMaxUploadsBeforeThrottle(const unsigned maxUploadsBeforeThrottle,
                                     const error expectedError)
    {
        // Expectations on the request listener.
        NiceMock<MockRequestListener> mockReqListener;
        mockReqListener.setErrorExpectations(expectedError,
                                             _,
                                             MegaRequest::TYPE_SET_SYNC_UPLOAD_THROTTLE_VALUES);

        // Code execution.
        megaApi[0]->setSyncMaxUploadsBeforeThrottle(maxUploadsBeforeThrottle, &mockReqListener);

        // Wait for everything to finish.
        ASSERT_TRUE(mockReqListener.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    /**
     * @brief Creates and edits a file and let it sync-upload the max number of times before
     * throttle.
     *
     * 1. Creates the file and let it up-sync.
     * 2. Edits the file and let it up-sync maxUploadsBeforeThrottle - 1.
     *
     * @param tempFile The LocalTempFile shared_ptr that will be re-created and used to create and
     * edit the file.
     * @param MegaHandle The handle of the directory to upload the file to.
     * @param maxUploadsBeforeThrottle The limit of allowed uploads before throttling the file.
     */
    void doUnthrottledUploads(std::shared_ptr<LocalTempFile>& tempFile,
                              const std::string_view newFileName,
                              const fs::path& newFilePath,
                              MegaHandle dirHandle,
                              const unsigned maxUploadsBeforeThrottle)
    {
        // Wait for the created file to be uploaded. This upload must be unthrottled.
        editFileAndWaitForUpload(megaApi[0].get(),
                                 newFileName,
                                 dirHandle,
                                 [&tempFile, &newFilePath]()
                                 {
                                     tempFile = std::make_shared<LocalTempFile>(newFilePath, 1000);
                                 });

        // Now we'll do (maxBeforeThrottle - 1) edits to trigger sync-uploads that should also be
        // unthrottled.
        ASSERT_NE(maxUploadsBeforeThrottle, 0);
        const unsigned unthrottledEdits = maxUploadsBeforeThrottle - 1;

        // File edit action to be executed within editFileAndWaitForUpload().
        const auto fileEditAction = [&tempFile = std::as_const(tempFile)]()
        {
            tempFile->appendData(100);
        };

        for (const auto i: range(unthrottledEdits))
        {
            LOG_debug << "[doUnthrottledUploads] Doing unthrottled edit #" << i + 1;
            editFileAndWaitForUpload(megaApi[0].get(), newFileName, dirHandle, fileEditAction);
        }
    }
};

/**
 * @brief SdkTestSyncUploadThrottling.TestPublicInterfaces_GetThrottleValues
 *
 * Test MegaApi::getSyncUploadThrottleUpperLimits to get the current throttle values.
 * We just check that the method is called correctly and the operation finishes with API_OK.
 */
TEST_F(SdkTestSyncUploadThrottling, TestPublicInterfaces_GetThrottleValues)
{
    static const std::string logPre{
        "SdkTestSyncUploadThrottling.TestPublicInterfaces_GetThrottleValues : "};
    LOG_verbose << logPre << "Getting upload throttle configurable values";

    // Expectations on the request listener.
    NiceMock<MockRequestListener> mockReqListener;
    mockReqListener.setErrorExpectations(API_OK,
                                         _,
                                         MegaRequest::TYPE_GET_SYNC_UPLOAD_THROTTLE_VALUES);

    // Get the throttle values.
    megaApi[0]->getSyncUploadThrottleValues(&mockReqListener);

    // Wait for everything to finish.
    ASSERT_TRUE(mockReqListener.waitForFinishOrTimeout(MAX_TIMEOUT));
}

/**
 * @brief SdkTestSyncUploadThrottling.TestPublicInterfaces_GetThrottleValuesLowerLimits
 *
 * Test MegaApi::getSyncUploadThrottleUpperLimits to get the lower limits for the configurable
 * throttle values.
 */
TEST_F(SdkTestSyncUploadThrottling, TestPublicInterfaces_GetThrottleValuesLowerLimits)
{
    static const std::string logPre{
        "SdkTestSyncUploadThrottling.TestPublicInterfaces_GetThrottleValuesLowerLimits : "};
    LOG_verbose << logPre << "Getting lower limits of throttle configurable values";

    // Expectations on the request listener.
    NiceMock<MockRequestListener> mockReqListener;
    mockReqListener.setErrorExpectations(API_OK,
                                         _,
                                         MegaRequest::TYPE_GET_SYNC_UPLOAD_THROTTLE_LIMITS);

    // Get the lower limits.
    megaApi[0]->getSyncUploadThrottleLowerLimits(&mockReqListener);

    // Wait for everything to finish.
    ASSERT_TRUE(mockReqListener.waitForFinishOrTimeout(MAX_TIMEOUT));
}

/**
 * @brief SdkTestSyncUploadThrottling.TestPublicInterfaces_GetThrottleValuesUpperLimits
 *
 * Test MegaApi::getSyncUploadThrottleUpperLimits to get the upper limits for the configurable
 * throttle values.
 */
TEST_F(SdkTestSyncUploadThrottling, TestPublicInterfaces_GetThrottleValuesUpperLimits)
{
    static const std::string logPre{
        "SdkTestSyncUploadThrottling.TestPublicInterfaces_GetThrottleValuesUpperLimits : "};
    LOG_verbose << logPre << "Getting upper limits of throttle configurable values";

    // Expectations on the request listener
    NiceMock<MockRequestListener> mockReqListener;
    mockReqListener.setErrorExpectations(API_OK,
                                         _,
                                         MegaRequest::TYPE_GET_SYNC_UPLOAD_THROTTLE_LIMITS);

    // Get the upper limits
    megaApi[0]->getSyncUploadThrottleUpperLimits(&mockReqListener);

    // Wait for everything to finish
    ASSERT_TRUE(mockReqListener.waitForFinishOrTimeout(MAX_TIMEOUT));
}

/**
 * @brief SdkTestSyncUploadThrottling.TestPublicInterfaces_SetThrottleUpdateRate_ValidValue
 *
 * Test MegaApi::setThrottleUpdateRate with a valid value.
 */
TEST_F(SdkTestSyncUploadThrottling, TestPublicInterfaces_SetThrottleUpdateRate_ValidValue)
{
    static const std::string logPre{
        "SdkTestSyncUploadThrottling.TestPublicInterfaces_SetThrottleUpdateRate_ValidValue : "};

    const auto uploadThrottlingManager = std::make_shared<UploadThrottlingManager>();
    const auto throttleValueLimits = uploadThrottlingManager->throttleValueLimits();

    setThrottleUpdateRate(throttleValueLimits.throttleUpdateRateLowerLimit, API_OK);
}

/**
 * @brief SdkTestSyncUploadThrottling.TestPublicInterfaces_SetThrottleUpdateRate_InvalidLowerValue
 *
 * Test MegaApi::setThrottleUpdateRate with an invalid value which is below the lower limit.
 */
TEST_F(SdkTestSyncUploadThrottling, TestPublicInterfaces_SetThrottleUpdateRate_InvalidLowerValue)
{
    static const std::string logPre{"SdkTestSyncUploadThrottling.TestPublicInterfaces_"
                                    "SetThrottleUpdateRate_InvalidLowerValue : "};

    const auto uploadThrottlingManager = std::make_shared<UploadThrottlingManager>();
    const auto throttleValueLimits = uploadThrottlingManager->throttleValueLimits();
    ASSERT_NE(throttleValueLimits.maxUploadsBeforeThrottleLowerLimit, 0);

    setThrottleUpdateRate(throttleValueLimits.throttleUpdateRateLowerLimit - 1, API_EARGS);
}

/**
 * @brief SdkTestSyncUploadThrottling.TestPublicInterfaces_SetThrottleUpdateRate_InvalidUpperValue
 *
 * Test MegaApi::setThrottleUpdateRate with an invalid value which is above the upper limit.
 */
TEST_F(SdkTestSyncUploadThrottling, TestPublicInterfaces_SetThrottleUpdateRate_InvalidUpperValue)
{
    static const std::string logPre{"SdkTestSyncUploadThrottling.TestPublicInterfaces_"
                                    "SetThrottleUpdateRate_InvalidUpperValue : "};

    const auto uploadThrottlingManager = std::make_shared<UploadThrottlingManager>();
    const auto throttleValueLimits = uploadThrottlingManager->throttleValueLimits();

    setThrottleUpdateRate(throttleValueLimits.throttleUpdateRateUpperLimit + 1, API_EARGS);
}

/**
 * @brief SdkTestSyncUploadThrottling.TestPublicInterfaces_SetMaxUploadsBeforeThrottle_ValidValue
 *
 * Test MegaApi::setMaxUploadsBeforeThrottle with a valid value.
 */
TEST_F(SdkTestSyncUploadThrottling, TestPublicInterfaces_SetMaxUploadsBeforeThrottle_ValidValue)
{
    static const std::string logPre{"SdkTestSyncUploadThrottling.TestPublicInterfaces_"
                                    "SetMaxUploadsBeforeThrottle_ValidValue : "};

    const auto uploadThrottlingManager = std::make_shared<UploadThrottlingManager>();
    const auto throttleValueLimits = uploadThrottlingManager->throttleValueLimits();

    setMaxUploadsBeforeThrottle(throttleValueLimits.maxUploadsBeforeThrottleLowerLimit, API_OK);
}

/**
 * @brief
 * SdkTestSyncUploadThrottling.TestPublicInterfaces_SetMaxUploadsBeforeThrottle_InvalidLowerValue
 *
 * Test MegaApi::setMaxUploadsBeforeThrottle with an invalid value which is below the lower limit.
 */
TEST_F(SdkTestSyncUploadThrottling,
       TestPublicInterfaces_SetMaxUploadsBeforeThrottle_InvalidLowerValue)
{
    static const std::string logPre{"SdkTestSyncUploadThrottling.TestPublicInterfaces_"
                                    "SetMaxUploadsBeforeThrottle_InvalidLowerValue : "};

    const auto uploadThrottlingManager = std::make_shared<UploadThrottlingManager>();
    const auto throttleValueLimits = uploadThrottlingManager->throttleValueLimits();
    ASSERT_NE(throttleValueLimits.maxUploadsBeforeThrottleLowerLimit, 0);

    setMaxUploadsBeforeThrottle(throttleValueLimits.maxUploadsBeforeThrottleLowerLimit - 1,
                                API_EARGS);
}

/**
 * @brief
 * SdkTestSyncUploadThrottling.TestPublicInterfaces_SetMaxUploadsBeforeThrottle_InvalidUpperValue
 *
 * Test MegaApi::setMaxUploadsBeforeThrottle with an invalid value which is above the upper limit.
 */
TEST_F(SdkTestSyncUploadThrottling,
       TestPublicInterfaces_SetMaxUploadsBeforeThrottle_InvalidUpperValue)
{
    static const std::string logPre{"SdkTestSyncUploadThrottling.TestPublicInterfaces_"
                                    "SetMaxUploadsBeforeThrottle_InvalidUpperValue : "};

    const auto uploadThrottlingManager = std::make_shared<UploadThrottlingManager>();
    const auto throttleValueLimits = uploadThrottlingManager->throttleValueLimits();

    setMaxUploadsBeforeThrottle(throttleValueLimits.maxUploadsBeforeThrottleUpperLimit + 1,
                                API_EARGS);
}

/**
 * @brief SdkTestSyncUploadThrottling.UploadUnthrottledFile
 *
 * Create a file and edit it the max number of times allowed before being throttled.
 *
 * 1. Create a file and let it upsync. This counts as one time in the internal counters.
 * 2. Edit the file and let it upsync the max number of times allowed to be uploaded unthrottled
 * (counting the first upload upon creating the file).
 */
TEST_F(SdkTestSyncUploadThrottling, UploadUnthrottledFile)
{
    static const std::string logPre{"SdkTestSyncUploadThrottling.UploadUnthrottledFile : "};

    LOG_verbose << logPre << "Ensuring sync is running on dir1";
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir1"));

    LOG_verbose << logPre << "Creating real and mocked upload manager";
    const auto throttlingManagers = createAndSetThrottlingManagers();
    const auto& uploadThrottlingManager = throttlingManagers.first;
    const auto& mockUploadThrottlingManager = throttlingManagers.second;
    const unsigned maxUploadsBeforeThrottle = uploadThrottlingManager->maxUploadsBeforeThrottle();

    LOG_verbose << logPre << "Get the dir path node handle";
    const auto dir1HandleOpt = getNodeHandleByPath("dir1");
    ASSERT_TRUE(dir1HandleOpt);
    const auto dir1Handle = *dir1HandleOpt;

    LOG_verbose << logPre << "Prepare the new file locally";
    const std::string_view newFileName{"test_file_new.txt"};
    const auto newFilePath = getLocalTmpDir() / newFileName;
    std::shared_ptr<LocalTempFile> tempFile;

    LOG_verbose << logPre
                << "Prepare expectations and the file so it is created and uploaded and then edit "
                   "it for further unthrottled uploads until reaching the maxUploadsBeforeThrottle("
                << maxUploadsBeforeThrottle << ") threshold";
    EXPECT_CALL(*mockUploadThrottlingManager, addToDelayedUploads(_)).Times(0);
    doUnthrottledUploads(tempFile, newFileName, newFilePath, dir1Handle, maxUploadsBeforeThrottle);
}

/**
 * @brief SdkTestSyncUploadThrottling.UploadThrottledFile
 *
 * Upload a delayed (throttled) file twice.
 * For this, the test edits a file enough times to be throttled and adds expectations regarding
 * throttling times and methods to be called.
 *
 * 1. Edit a file and let it upsync enough times to be throttled upon next sync-upload.
 * 2. Add expectations and reset the lastProcessedTime counter right before editing the file. That
 * way we can have more accurate expectations regarding the upload start based on throttling update
 * rate.
 * 3. Edit the file again and let it be added to the throttled uploads.
 * 4. Wait for it to finish and upload it again. Both times the upload must have been throttled.
 */
TEST_F(SdkTestSyncUploadThrottling, UploadThrottledFile)
{
    static const std::string logPre{"SdkTestSyncUploadThrottling.UploadThrottledFile : "};

    LOG_verbose << logPre << "Ensuring sync is running on dir1";
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir1"));

    LOG_verbose << logPre << "Creating real and mocked upload manager";
    const auto throttlingManagers = createAndSetThrottlingManagers();
    const auto& uploadThrottlingManager = throttlingManagers.first;
    const auto& mockUploadThrottlingManager = throttlingManagers.second;
    const unsigned updateRateSeconds = uploadThrottlingManager->throttleUpdateRate();
    const unsigned maxUploadsBeforeThrottle = uploadThrottlingManager->maxUploadsBeforeThrottle();

    LOG_verbose << logPre << "Get the dir path node handle";
    const auto dir1HandleOpt = getNodeHandleByPath("dir1");
    ASSERT_TRUE(dir1HandleOpt);
    const auto dir1Handle = *dir1HandleOpt;

    LOG_verbose << logPre << "Prepare the new file locally";
    const std::string_view newFileName{"test_file_new.txt"};
    const auto newFilePath = getLocalTmpDir() / newFileName;
    std::shared_ptr<LocalTempFile> tempFile;

    LOG_verbose << logPre << "Edit and upload the file until reaching the maxUploadsBeforeThrottle("
                << maxUploadsBeforeThrottle << ") threshold";
    doUnthrottledUploads(tempFile, newFileName, newFilePath, dir1Handle, maxUploadsBeforeThrottle);

    for (const auto i: range(2))
    {
        LOG_verbose << logPre << "Prepare and edit the file for the next upload (num: "
                    << (maxUploadsBeforeThrottle + i) << ") which must be throttled ";
        EXPECT_CALL(*mockUploadThrottlingManager, processDelayedUploads(_)).Times(AtLeast(1));
        InSequence seq; // ensure order from here: addToDelayedQueue -> (process the delayed upload)
                        // -> transfer requests.
        EXPECT_CALL(*mockUploadThrottlingManager, addToDelayedUploads(_)).Times(Exactly(1));

        // Calculate min time to start the upload.
        // First time we are resetting the last processed time.
        // However, the second time some secs may have lapsed so we can just add updateRateSeconds
        // as the expected minimum.
        const auto minTimeToStartUpload = std::invoke(
            [i, updateRateSeconds, &uploadThrottlingManager]() -> unsigned
            {
                if (i == 0)
                    return updateRateSeconds;

                if (uploadThrottlingManager->timeSinceLastProcessedUpload().count() >=
                    updateRateSeconds)
                    return 0;

                return updateRateSeconds -
                       static_cast<unsigned>(
                           uploadThrottlingManager->timeSinceLastProcessedUpload().count() +
                           1); // +1 to round up non-whole seconds.
            });

        // Define the edit action to be executed within editFileAndWaitForUpload().
        const auto fileEditAction =
            [&tempFile = std::as_const(tempFile), &uploadThrottlingManager, i]()
        {
            if (i == 0) // Only the first time
            {
                uploadThrottlingManager
                    ->resetLastProcessedTime(); // This will ensure that the throttle time is
                                                // more or less updateRateSeconds when calling
                                                // resetLastProcessedTime().
            }
            tempFile->appendData(100);
        };

        // Finally edit the file and wait for upload and meeting expectations.
        editFileAndWaitForUpload(megaApi[0].get(),
                                 newFileName,
                                 dir1Handle,
                                 fileEditAction,
                                 UploadWaitConfig{std::chrono::seconds(minTimeToStartUpload)});
    }
}

/**
 * @brief SdkTestSyncUploadThrottling.UploadSeveralThrottledFiles
 *
 * Similar to SdkTestSyncUploadThrottling.UploadThrottledFile but with two files, checking that the
 * throttle logic is handled correctly for different transfers.
 *
 * 1a. Edit a file1 and let it upsync enough times to be throttled upon next sync-upload.
 * 1b. Do the same with a file2.
 * 2a. Edit the file1 again and let it be added to the throttled uploads.
 * 2b. When this happens, edit file2 so it get throttled too. Add expectations taking into account
 * that the throttling time for this file2 to start is twice the throttle update rate, as file1
 * needs to be processed first.
 */
TEST_F(SdkTestSyncUploadThrottling, UploadSeveralThrottledFiles)
{
    static const std::string logPre{"SdkTestSyncUploadThrottling.UploadSeveralThrottledFiles : "};

    LOG_verbose << logPre << "Ensuring sync is running on dir1";
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir1"));

    LOG_verbose << logPre << "Creating real and mocked upload manager";
    const auto throttlingManagers = createAndSetThrottlingManagers();
    const auto& uploadThrottlingManager = throttlingManagers.first;
    const auto& mockUploadThrottlingManager = throttlingManagers.second;
    const auto updateRateSeconds = uploadThrottlingManager->throttleUpdateRate();
    const auto maxUploadsBeforeThrottle = uploadThrottlingManager->maxUploadsBeforeThrottle();

    LOG_verbose << logPre << "Get the dir path node handle";
    const auto dir1HandleOpt = getNodeHandleByPath("dir1");
    ASSERT_TRUE(dir1HandleOpt);
    const auto dir1Handle = *dir1HandleOpt;

    LOG_verbose << logPre << "Prepare the new file1 locally";
    const std::string_view newFile1Name{"test_file1_new.txt"};
    const auto newFile1Path = getLocalTmpDir() / newFile1Name;
    std::shared_ptr<LocalTempFile> tempFile1;

    LOG_verbose << logPre
                << "Edit and upload the file1 until reaching the maxUploadsBeforeThrottle("
                << maxUploadsBeforeThrottle << ") threshold";
    doUnthrottledUploads(tempFile1,
                         newFile1Name,
                         newFile1Path,
                         dir1Handle,
                         maxUploadsBeforeThrottle);

    LOG_verbose << logPre << "Prepare the new file2 locally";
    const std::string_view newFile2Name{"test_file2_new.txt"};
    const auto newFile2Path = getLocalTmpDir() / newFile2Name;
    std::shared_ptr<LocalTempFile> tempFile2;

    LOG_verbose << logPre
                << "Edit and upload the file2 until reaching the maxUploadsBeforeThrottle("
                << maxUploadsBeforeThrottle << ") threshold";
    doUnthrottledUploads(tempFile2,
                         newFile2Name,
                         newFile2Path,
                         dir1Handle,
                         maxUploadsBeforeThrottle);

    LOG_verbose << logPre << "Prepare expectations and limits";

    EXPECT_CALL(*mockUploadThrottlingManager, processDelayedUploads(_)).Times(AtLeast(2));
    EXPECT_CALL(*mockUploadThrottlingManager, addToDelayedUploads(_)).Times(Exactly(2));

    // Prepare file edit action for file1. When the file1 is editted, it will signal file1Edited
    // promise.
    std::promise<void> file1Edited;
    const auto file1EditAction = [&tempFile1, &uploadThrottlingManager, &file1Edited]()
    {
        uploadThrottlingManager
            ->resetLastProcessedTime(); // Reset the last processed time so we ensure that the file1
                                        // will need to wait the throttleUpdateRate time and use it
                                        // as the expectation.
        tempFile1->appendData(100); // Simulate editing the file.
        file1Edited.set_value(); // Signal that file1 has been edited.
    };

    // Prepare file edit action for file2. When file1Edited is resolved, file2 will be edited
    // afterwards, so it will be the second task in the delayed uploads queue.
    const auto file2EditAction = [&tempFile2, &file1Edited]()
    {
        // Wait until file1 has been edited.
        // Give 10secs as max, but it should be almost immediate.
        auto file1EditedFuture = file1Edited.get_future();
        if (file1EditedFuture.wait_for(std::chrono::seconds(10)) != std::future_status::ready)
        {
            throw std::runtime_error("The file1 wasn't edited within the timeout");
        }

        // Simulate editing file2.
        tempFile2->appendData(100);
    };

    // Wait config values for task 1.
    const auto uploadWaitConfigTask1 = std::invoke(
        [&updateRateSeconds]() -> UploadWaitConfig
        {
            UploadWaitConfig uploadWaitConfig{};
            uploadWaitConfig.minWaitForTransferStart = std::chrono::seconds(updateRateSeconds);
            uploadWaitConfig.otherConcurrentCalls =
                1; // Task2 adds expectations to the transfer request listeners.
            return uploadWaitConfig;
        });

    // Define wait config values for task 2.
    const auto uploadWaitConfigTask2 = std::invoke(
        [&updateRateSeconds]() -> UploadWaitConfig
        {
            UploadWaitConfig uploadWaitConfig{};
            uploadWaitConfig.minWaitForTransferStart = std::chrono::seconds(updateRateSeconds * 2);
            uploadWaitConfig.maxWaitForTransferStartFromMinWait =
                UploadWaitConfig::TOLERANCE_SECONDS_FOR_STARTING_UPLOADS * 2,
            uploadWaitConfig.otherConcurrentCalls =
                1; // Task1 adds expectations to the transfer request listeners.
            return uploadWaitConfig;
        });

    // First file upload task.
    LOG_verbose << logPre
                << "Prepare and edit the file1 for the next upload which must be throttled";
    auto task1 = std::async(std::launch::async,
                            [&]()
                            {
                                editFileAndWaitForUpload(megaApi[0].get(),
                                                         newFile1Name,
                                                         dir1Handle,
                                                         file1EditAction,
                                                         uploadWaitConfigTask1);
                            });

    // Second file upload task.
    LOG_debug << logPre
              << "Prepare and edit the file2 for the next upload which must be throttled. The "
                 "file2 will be edited right afterwards file1 so it gets enqueued after it";
    auto task2 = std::async(std::launch::async,
                            [&]()
                            {
                                editFileAndWaitForUpload(megaApi[0].get(),
                                                         newFile2Name,
                                                         dir1Handle,
                                                         file2EditAction,
                                                         uploadWaitConfigTask2);
                            });

    // Wait for both tasks to complete.
    task1.get();
    task2.get();
}

/**
 * @brief SdkTestSyncUploadThrottling.UploadThrottledFilePauseSyncAndUploadItUnthrottled
 *
 * 1. Edit a file and let it upsync enough times to be throttled upon next sync-upload.
 * 2. Edit the file again and let it be added to the throttled uploads.
 * 3. Pause the sync before the delayed upload starts.
 * 4. Resume the sync.
 * 5. Checks that the former delayed upload is now triggered and uploaded without throttling.
 */
TEST_F(SdkTestSyncUploadThrottling, UploadThrottledFilePauseSyncAndUploadItUnthrottled)
{
    static const std::string logPre{
        "SdkTestSyncUploadThrottling.UploadThrottledFilePauseSyncAndUploadItUnthrottled : "};

    LOG_verbose << logPre << "Ensuring sync is running on dir1";
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir1"));

    LOG_verbose << logPre << "Creating real and mocked upload manager";
    const auto throttlingManagers = createAndSetThrottlingManagers();
    const auto& uploadThrottlingManager = throttlingManagers.first;
    const auto& mockUploadThrottlingManager = throttlingManagers.second;
    const unsigned updateRateSeconds = uploadThrottlingManager->throttleUpdateRate();
    const unsigned maxUploadsBeforeThrottle = uploadThrottlingManager->maxUploadsBeforeThrottle();

    LOG_verbose << logPre << "Get the dir path node handle";
    const auto dir1HandleOpt = getNodeHandleByPath("dir1");
    ASSERT_TRUE(dir1HandleOpt);
    const auto dir1Handle = *dir1HandleOpt;

    LOG_verbose << logPre << "Prepare the new file locally";
    const std::string_view newFileName{"test_file_new.txt"};
    const auto newFilePath = getLocalTmpDir() / newFileName;
    std::shared_ptr<LocalTempFile> tempFile;

    LOG_verbose << logPre << "Edit and upload the file until reaching the maxUploadsBeforeThrottle("
                << maxUploadsBeforeThrottle << ") threshold";
    doUnthrottledUploads(tempFile, newFileName, newFilePath, dir1Handle, maxUploadsBeforeThrottle);

    LOG_verbose << logPre
                << "Prepare and edit the file for the next upload which must be throttled";
    EXPECT_CALL(*mockUploadThrottlingManager, processDelayedUploads(_)).Times(AnyNumber());
    EXPECT_CALL(*mockUploadThrottlingManager, addToDelayedUploads(_)).Times(Exactly(1));

    // Define the file edit action to be executed within editFileAndWaitForUpload().
    const auto fileEditAction =
        [&tempFile = std::as_const(tempFile), &uploadThrottlingManager, updateRateSeconds, this]()
    {
        uploadThrottlingManager->resetLastProcessedTime(); // This will ensure that the throttle
                                                           // time is more or less updateRateSeconds
                                                           // when calling resetLastProcessedTime().
        tempFile->appendData(100);

        std::this_thread::sleep_for(
            std::chrono::seconds(updateRateSeconds / 3)); // Wait a bit before suspending the sync.

        LOG_verbose << logPre << "Pausing the sync";
        ASSERT_NO_FATAL_FAILURE(suspendSync());

        LOG_verbose << logPre << "Resuming the sync";
        ASSERT_NO_FATAL_FAILURE(resumeSync());

        LOG_verbose << logPre << "Waiting for the upload to resume and finish";
    };

    editFileAndWaitForUpload(megaApi[0].get(), newFileName, dir1Handle, fileEditAction);
}
#endif // ENABLE_SYNC
