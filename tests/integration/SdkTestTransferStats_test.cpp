/**
 * @file SdkTestTransferStats_test.cpp
 * @brief This file defines some tests for testing transfer stats (uploads & downloads).
 */

#include "sdk_test_utils.h"
#include "SdkTest_test.h"

namespace
{

/**
 * @brief Calculate specific metrics values that can be expected.
 *
 * @param transferType The type of transfer: PUT for uploads, GET for downloads.
 * @param sizes A vector with the file sizes.
 * @param raidedTransferRatio The ratio of raided files per transfer.
 *
 * @return A stats::TransferStats::Metrics object with the transfer type, median size,
 * contraharmonic mean and raided transfer ratio.
 */
stats::TransferStats::Metrics calculateExpectedMetrics(const direction_t transferType,
                                                       const std::vector<m_off_t>& sizes,
                                                       const double raidedTransferRatio = 0.0)
{
    stats::TransferStats::Metrics metrics;

    // Assign the transfer type (PUT or GET).
    EXPECT_TRUE(transferType == PUT || transferType == GET);
    metrics.mTransferType = transferType;

    // Assign number of transfers.
    metrics.mNumTransfers = sizes.size();

    // Calculate the median size.
    auto sortedSizes = sizes;
    std::sort(sortedSizes.begin(), sortedSizes.end());
    metrics.mMedianSize = stats::calculateMedian(sortedSizes);

    // Calculate the contraharmonic mean (sizes weighted by their own sizes).
    metrics.mContraharmonicMeanSize = stats::calculateWeightedAverage(sizes, sizes);

    // Set RAID transfer ratio.
    metrics.mRaidedTransferRatio = raidedTransferRatio;

    return metrics;
}

/**
 * @brief Compare the expected Metrics with the metrics obtained from the TransferStatsManager.
 *
 * For medianSpeed, weightedAverageSpeed, maxSpeed, avgLatency, and failedRequestRatio,
 * we perform some light checks, as those are not fully predictable.
 *
 * @param expected The expected values for TransferStats::Metrics.
 * @param actual The TransferStats::Metrics object retrieved from the
 * MegaClient::TransferStatsManager.
 *
 */
void compareMetrics(const stats::TransferStats::Metrics& expected,
                    const stats::TransferStats::Metrics& actual)
{
    if (expected.mNumTransfers != actual.mNumTransfers)
    {
        LOG_warn << "Expected number of transfers (" << expected.mNumTransfers
                 << ") does not match with actual value (" << actual.mNumTransfers
                 << "). Skipping comparison";
        return;
    }
    EXPECT_EQ(expected.mTransferType, actual.mTransferType);
    EXPECT_EQ(expected.mMedianSize, actual.mMedianSize);
    EXPECT_EQ(expected.mContraharmonicMeanSize, actual.mContraharmonicMeanSize);
    EXPECT_TRUE(actual.mMedianSpeed > 0);
    EXPECT_TRUE(actual.mWeightedAverageSpeed >= actual.mMedianSpeed);
    EXPECT_TRUE(actual.mMaxSpeed >= actual.mMedianSpeed);
    EXPECT_TRUE(actual.mAvgLatency > 0 && actual.mAvgLatency < 1000);
    EXPECT_TRUE(actual.mFailedRequestRatio >= 0.0 && actual.mFailedRequestRatio <= 1.1);
    EXPECT_EQ(expected.mRaidedTransferRatio, actual.mRaidedTransferRatio);
};

} // namespace

/**
 * @class SdkTestTransferStats
 * @brief Fixture for test suite to test Transfer Stats.
 *
 */
class SdkTestTransferStats: public SdkTest
{
public:
    /**
     * @brief Wrapper to upload files with the necessary parameters.
     *
     * @param rootNode The ROOTNODE of the Cloud.
     * @param uploadFileName The name of the file.
     * @param content The contents of the file.
     *
     * @return A pointer to the MegaNode object created from the Cloud.
     */
    MegaNode* uploadFileForStats(MegaNode* const rootNode,
                                 const std::string_view uploadFileName,
                                 const std::string_view content)
    {
        MegaHandle fileHandle = 0;
        sdk_test::LocalTempFile testTempFile(uploadFileName.data(), content);
        EXPECT_EQ(MegaError::API_OK,
                  doStartUpload(0,
                                &fileHandle,
                                uploadFileName.data(),
                                rootNode,
                                nullptr,
                                ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                nullptr,
                                true,
                                false,
                                nullptr))
            << "Cannot upload " << uploadFileName;
        return megaApi[0]->getNodeByHandle(fileHandle);
    }

    /**
     * @brief Wrapper to download files with the necessary parameters.
     *
     * @param node The cloud node with the file info to download.
     * @param downloadFileName The name of the file.
     */
    void downloadFileForStats(MegaNode* const node, const std::string_view downloadFileName)
    {
        ASSERT_EQ(
            MegaError::API_OK,
            doStartDownload(0,
                            node,
                            downloadFileName.data(),
                            nullptr /*customName*/,
                            nullptr /*appData*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/,
                            MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
                            MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
                            false /* undelete */))
            << "Cannot download " << downloadFileName;
    };
};

/**
 * @brief TEST_F SdkTestTransferStats
 *
 * Upload and download regular files and a CloudRAID file,
 * collect Transfer Metrics and check expected results.
 *
 * Note: We don't compare upload metrics before to ensure
 *       that upload and downloads metrics are separated and
 *       were not mixed up by the TransferStatsManager.
 *
 * 1. UPLOAD AND DOWNLOAD TWO FILES TO COLLECT TRANSFER STATS.
 *    1.1 Upload files.
 *    1.2 Download both files.
 * 2. COLLECT AND COMPARE UPLOADS AND DOWNLOADS METRICS.
 *    2.1 Collect metrics.
 *    2.2 Define sizes of uploaded and regular downloaded files.
 *    2.3 Define expected metrics for uploads and compare results.
 *    2.4 Define expected metrics for the regular downloads and compare results.
 * 3. CHECK DOWNLOAD TRANSFER STATS INCLUDING A NEW CLOUDRAID FILE.
 *    3.1 Download a CloudRAID file.
 *    3.2 Collect metrics for downloads including the CloudRAID file.
 *    3.3 Define expected metrics after RAID download and compare results.
 */
TEST_F(SdkTestTransferStats, SdkTestTransferStats)
{
    LOG_info << "___TEST SdkTestTransferStats";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);

    // 1. UPLOAD AND DOWNLOAD TWO FILES TO COLLECT TRANSFER STATS.

    // 1.1 Upload files.
    constexpr std::string_view file1content = "Current content 1";
    std::unique_ptr<MegaNode> testFileNode1(
        uploadFileForStats(rootNode.get(), "test1.txt", file1content));
    ASSERT_TRUE(testFileNode1);

    constexpr std::string_view file2content = "Current content 2 - longer";
    std::unique_ptr<MegaNode> testFileNode2(
        uploadFileForStats(rootNode.get(), "test2.txt", file2content));
    ASSERT_TRUE(testFileNode2);

    // 1.2. Download both files.
    downloadFileForStats(testFileNode1.get(), DOTSLASH "downfile1.txt");
    downloadFileForStats(testFileNode2.get(), DOTSLASH "downfile2.txt");

    // 2. COLLECT AND COMPARE UPLOADS AND DOWNLOADS METRICS.

    // 2.1 Collect metrics.
    const auto* const client{megaApi[0]->getClient()};
    LOG_debug << "[SdkTest::SdkTestTransferStats] collectAndPrintMetrics for UPLOADS";
    const stats::TransferStats::Metrics uploadMetrics =
        client->mTransferStatsManager.collectAndPrintMetrics(PUT);

    LOG_debug << "[SdkTest::SdkTestTransferStats] collectAndPrintMetrics for DOWNLOADS";
    const stats::TransferStats::Metrics downloadMetrics1 =
        client->mTransferStatsManager.collectAndPrintMetrics(GET);

    // 2.2 Define sizes of uploaded and regular downloaded files.
    const m_off_t file1size = file1content.size(); // size = 17 bytes
    const m_off_t file2size = file2content.size(); // size = 26 bytes
    const std::vector<m_off_t> regularFileSizes = {file1size, file2size};

    // 2.3 Define expected metrics for uploads and compare results.
    const stats::TransferStats::Metrics expectedUploadMetrics =
        calculateExpectedMetrics(PUT, regularFileSizes);
    compareMetrics(expectedUploadMetrics, uploadMetrics);

    // 2.4 Define expected metrics for the regular downloads and compare results.
    const stats::TransferStats::Metrics expectedDownloadMetrics1 =
        calculateExpectedMetrics(GET, regularFileSizes);
    compareMetrics(expectedDownloadMetrics1, downloadMetrics1);

    // 3. CHECK DOWNLOAD TRANSFER STATS INCLUDING A NEW CLOUDRAID FILE.

    // 3.1 Download a CloudRAID file.
    {
        std::string url100MB =
            "/#!JzckQJ6L!X_p0u26-HOTenAG0rATFhKdxYx-rOV1U6YHYhnz2nsA"; // https://mega.nz/file/JzckQJ6L#X_p0u26-HOTenAG0rATFhKdxYx-rOV1U6YHYhnz2nsA
        const auto importHandle =
            importPublicLink(0, MegaClient::MEGAURL + url100MB, rootNode.get());
        std::unique_ptr<MegaNode> nimported{megaApi[0]->getNodeByHandle(importHandle)};

        constexpr std::string_view downloadFileName3{DOTSLASH "downfile3.cloudraided.sdktest"};
        deleteFile(downloadFileName3.data());
        downloadFileForStats(nimported.get(), DOTSLASH "downfile3.cloudraided.sdktest");
        deleteFile(downloadFileName3.data());
    }

    // 3.2 Collect metrics for downloads including the CloudRAID file.
    std::this_thread::sleep_for(std::chrono::seconds{1});
    LOG_debug << "[SdkTest::SdkTestTransferStats] collectAndPrintMetrics for DOWNLOADS after "
                 "CLOUDRAID download";
    const stats::TransferStats::Metrics downloadMetrics2 =
        client->mTransferStatsManager.collectAndPrintMetrics(GET);

    // 3.3 Define expected metrics after RAID download and compare results.
    const m_off_t raidFileSize = 100 * 1024 * 1024; // 100MB
    const std::vector<m_off_t> allFileSizes = {file1size, file2size, raidFileSize};
    const stats::TransferStats::Metrics expectedDownloadMetrics2 =
        calculateExpectedMetrics(GET,
                                 allFileSizes,
                                 0.33); // 1 out of 3 is RAID, with 2 decimal precision
    compareMetrics(expectedDownloadMetrics2, downloadMetrics2);
}