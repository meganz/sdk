/**
 * @file
 * @brief This file is expected to contain tests involving sync upload operations
 * e.g., what happens when a file is duplicated inside a sync.
 */

#ifdef ENABLE_SYNC

#include "integration_test_utils.h"
#include "mega/utils.h"
#include "megautils.h"
#include "mock_listeners.h"
#include "sdk_test_utils.h"
#include "SdkTestSyncNodesOperations.h"

#include <gmock/gmock.h>

using namespace sdk_test;
using namespace testing;

/**
 * @class SdkTestSyncUploadOperations
 * @brief Test fixture designed to test operations involving sync uploads.
 */
class SdkTestSyncUploadsOperations: public SdkTestSyncNodesOperations
{
public:
    const std::string SYNC_REMOTE_PATH{"localSyncedDir"};

    void SetUp() override
    {
        SdkTestNodesSetUp::SetUp();
        if (createSyncOnSetup())
        {
            ASSERT_NO_FATAL_FAILURE(
                initiateSync(getLocalTmpDirU8string(), SYNC_REMOTE_PATH, mBackupId));
            ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());
        }
    }

    /**
     * @brief Build a file tree with two empty sync folders
     */
    const std::vector<NodeInfo>& getElements() const override
    {
        static const std::vector<NodeInfo> ELEMENTS{DirNodeInfo(SYNC_REMOTE_PATH)
                                                        .addChild(DirNodeInfo("dir1"))
                                                        .addChild(DirNodeInfo("dir2"))};

        return ELEMENTS;
    }

    shared_ptr<sdk_test::LocalTempFile>
        createLocalFile(const fs::path& filePath,
                        const std::string_view contents,
                        std::optional<fs::file_time_type> customMtime)
    {
        return std::make_shared<sdk_test::LocalTempFile>(filePath, contents, customMtime);
    }

    /**
     * @brief Creates a local file and waits until onSyncFileStateChanged with STATE_SYNCED is
     * received.
     * @note It's important that localFilePathAbs is an absolute path, othwewise it won't match with
     * received one in onSyncFileStateChanged
     *
     * @param localFilePathAbs Absolute filesystem path where the file will be created.
     * @param contents The file contents to write.
     * @param customMtime Optional custom modification time to apply to the file.
     *                    If not provided, the current system clock time will be used.
     * @return `true` if the file was successfully synchronized within the timeout, `false`
     * otherwise.
     */
    bool createLocalFileAndWaitForSync(const fs::path& localFilePathAbs,
                                       const std::string_view contents,
                                       std::optional<fs::file_time_type> customMtime)
    {
        std::shared_ptr<std::promise<int>> fileUploadPms = std::make_shared<std::promise<int>>();
        std::unique_ptr<std::future<int>> fut(new std::future<int>(fileUploadPms->get_future()));
        NiceMock<MockSyncListener> msl{megaApi[0].get()};
        const auto hasExpectedId = Pointee(Property(&MegaSync::getBackupId, mBackupId));
        EXPECT_CALL(msl, onSyncFileStateChanged(_, hasExpectedId, _, _))
            .WillRepeatedly(
                [fileUploadPms,
                 localFilePathStr = localFilePathAbs.string()](MegaApi*,
                                                               MegaSync*,
                                                               std::string* localPath,
                                                               int newState)
                {
                    if (newState == MegaApi::STATE_SYNCED && localPath &&
                        *localPath == localFilePathStr)
                    {
                        fileUploadPms->set_value(newState);
                    }
                });
        megaApi[0]->addListener(&msl);

        auto localFile2 = createLocalFile(localFilePathAbs, contents, customMtime);
        return fut->wait_for(COMMON_TIMEOUT) == std::future_status::ready;
    }
};

/**
 * @test SdkTestSyncUploadsOperations.BasicFileUpload
 *
 * 1. Create a new local file inside sync directory `dir2`.
 * 2. Wait for sync (sync engine must upload file to the cloud).
 * 3. Verify that local and remote models match after upload.
 */
TEST_F(SdkTestSyncUploadsOperations, BasicFileUpload)
{
    static const std::string logPre{"SdkTestSyncUploadsOperations.BasicFileUpload : "};
    LOG_verbose << logPre << "Creating a new file and upload it to dir2";

    auto localFilePathAbs{fs::absolute(getLocalTmpDir() / "dir2" / "file1")};
    ASSERT_TRUE(
        createLocalFileAndWaitForSync(localFilePathAbs, "abcde", fs::file_time_type::clock::now()));
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());
}

/**
 * @test SdkTestSyncUploadsOperations.DuplicatedFilesUpload
 *
 * 1. Create a new local file `file1` in `dir1` with given content and mtime.
 *    - Expect a full upload (transfer started and finished).
 * 2. Create a new local file `file1` in `dir2` with the same content and mtime.
 *    - Expect a remote copy (no transfer started).
 * 3. Confirm that cloud and local models remain consistent.
 */
TEST_F(SdkTestSyncUploadsOperations, DuplicatedFilesUpload)
{
    const std::string prefix{"SdkTestSyncUploadsOperations.DuplicatedFilesUpload: "};
    NiceMock<MockTransferListener> mtl{megaApi[0].get()};
    megaApi[0]->addListener(&mtl);

    LOG_debug
        << prefix
        << "#### TC1 Create localfile `file1` into `dir1` with Content1 and Mtime1 (full upload "
           "expected) ####";
    std::shared_ptr<std::promise<int>> fileUploadPms = std::make_shared<std::promise<int>>();
    std::unique_ptr<std::future<int>> fut(new std::future<int>(fileUploadPms->get_future()));

    EXPECT_CALL(mtl, onTransferStart).Times(1);
    EXPECT_CALL(mtl, onTransferFinish)
        .Times(1)
        .WillOnce(
            [fileUploadPms](::mega::MegaApi*, ::mega::MegaTransfer*, ::mega::MegaError* e)
            {
                fileUploadPms->set_value(e->getErrorCode());
            });

    auto customMtime = fs::file_time_type::clock::now();
    auto localFilePathAbs1{fs::absolute(getLocalTmpDir() / "dir1" / "file1")};
    ASSERT_TRUE(createLocalFileAndWaitForSync(localFilePathAbs1, "abcde", customMtime));
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

    ASSERT_EQ(fut->wait_for(COMMON_TIMEOUT), std::future_status::ready);
    ASSERT_EQ(fut->get(), API_OK) << "file1 transfer failed";

    // Make sure phase-1 expectations are satisfied and then clear them.
    testing::Mock::VerifyAndClearExpectations(&mtl);

    // - Note: Sync engine does not create an associated MegaTransfer for remote copies.
    LOG_debug
        << prefix
        << "#### TC2 Create localfile `file1` into `dir2` with Content1 and Mtime1 (remote copy "
           "expected) ####";
    EXPECT_CALL(mtl, onTransferStart).Times(0);
    EXPECT_CALL(mtl, onTransferFinish).Times(0);

    auto localFilePathAbs2{fs::absolute(getLocalTmpDir() / "dir2" / "file1")};
    ASSERT_TRUE(createLocalFileAndWaitForSync(localFilePathAbs2, "abcde", customMtime));
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());
}

/**
 * @test SdkTestSyncUploadsOperations.DuplicatedFilesUploadDifferentMtime
 *
 * 1. Create a new local file `file1` in `dir1` with given content and mtime `mt1`.
 *    - Expect a full upload.
 * 2. Create the same file `file1` in `dir2` with same content but different mtime `mt2`.
 *    - Expect another full upload since fingerprints differs (Fp = CRC + size + mtime).
 * 3. Verify that local and remote states are fully synchronized.
 */
TEST_F(SdkTestSyncUploadsOperations, DuplicatedFilesUploadDifferentMtime)
{
    const std::string prefix{"SdkTestSyncUploadsOperations.DuplicatedFilesUploadDifferentMtime: "};
    NiceMock<MockTransferListener> mtl{megaApi[0].get()};
    megaApi[0]->addListener(&mtl);

    auto createLocalFileAndSync =
        [&mtl, this](const fs::path& localFilePathAbs,
                     const std::string& fileContent,
                     std::chrono::time_point<fs::file_time_type::clock> customMtime)
    {
        std::shared_ptr<std::promise<int>> fileUploadPms = std::make_shared<std::promise<int>>();
        std::unique_ptr<std::future<int>> fut(new std::future<int>(fileUploadPms->get_future()));

        EXPECT_CALL(mtl, onTransferStart).Times(1);
        EXPECT_CALL(mtl, onTransferFinish)
            .Times(1)
            .WillOnce(
                [fileUploadPms](::mega::MegaApi*, ::mega::MegaTransfer*, ::mega::MegaError* e)
                {
                    fileUploadPms->set_value(e->getErrorCode());
                });

        ASSERT_TRUE(createLocalFileAndWaitForSync(localFilePathAbs, fileContent, customMtime));
        ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

        ASSERT_EQ(fut->wait_for(COMMON_TIMEOUT), std::future_status::ready);
        ASSERT_EQ(fut->get(), API_OK) << "file1 transfer failed";
        testing::Mock::VerifyAndClearExpectations(&mtl);
    };

    LOG_debug
        << prefix
        << "#### TC1 Create localfile `file1` into `dir1` with Content1 and Mtime1 (full upload "
           "expected) ####";
    ASSERT_NO_FATAL_FAILURE(
        createLocalFileAndSync(fs::absolute(getLocalTmpDir() / "dir1" / "file1"),
                               "abcde",
                               fs::file_time_type::clock::now()));

    LOG_debug
        << prefix
        << "#### TC2 Create localfile `file1` into `dir2` with Content1 and Mtime2 (full upload "
           "expected) ####";
    ASSERT_NO_FATAL_FAILURE(
        createLocalFileAndSync(fs::absolute(getLocalTmpDir() / "dir2" / "file1"),
                               "abcde",
                               fs::file_time_type::clock::now()));

    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());
}

/**
 * @brief SdkTestSyncUploadsOperations.MultimediaFileUpload
 *
 * Test the metadata and thumbnails from a synced video.
 *
 */
#if !defined(USE_FREEIMAGE) && !defined(USE_MEDIAINFO)
TEST_F(SdkTestSyncUploadsOperations, DISABLED_MultimediaFileUpload)
#else
TEST_F(SdkTestSyncUploadsOperations, MultimediaFileUpload)
#endif
{
    static const string VIDEO_FILE = "sample_video.mp4";
    static const int AVC1_FORMAT = 887; // ID from MediaInfo

    static const std::string logPre = getLogPrefix();
    LOG_verbose << logPre << "Upload a multimedia file in a sync";

    // Get file in the sync path to be uploaded by the sync.
    ASSERT_TRUE(getFileFromArtifactory("test-data/" + VIDEO_FILE,
                                       fs::absolute(getLocalTmpDir() / VIDEO_FILE)));

    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

    auto uploadedNode = getNodeByPath(SYNC_REMOTE_PATH + "/" + VIDEO_FILE);
    ASSERT_TRUE(uploadedNode);
#ifdef USE_MEDIAINFO
    ASSERT_EQ(uploadedNode->getDuration(), 5) << "Duration is not correct or unavailable.";
    ASSERT_EQ(uploadedNode->getHeight(), 360) << "Height is not correct or unavailable.";
    ASSERT_EQ(uploadedNode->getWidth(), 640) << "Width ID is not correct or unavailable.";
    ASSERT_EQ(uploadedNode->getVideocodecid(), AVC1_FORMAT)
        << "Codec ID is not correct or unavailable.";
#endif
#ifdef USE_FREEIMAGE
    ASSERT_TRUE(uploadedNode->hasThumbnail())
        << "Thumbnail is not available for the uploaded node.";
#endif
}

#endif // ENABLE_SYNC
