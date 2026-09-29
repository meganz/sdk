/**
 * @file SdkTestMassiveActionPackets_test.cpp
 * @brief Integration test for massive action packets processing
 *
 * This test verifies that multiple clients can correctly receive and process
 * a large number of action packets in different parsing modes.
 *
 * Test scenario:
 * - Client A (executor): Performs all operations to generate action packets
 * - Client B (receiver1): Receives action packets with streaming parsing mode
 * - Client C (receiver2): Receives action packets with legacy parsing mode
 *
 * Verification:
 * 1. Client B and Client C should have consistent state after processing all APs
 * 2. Client A and Client B (or C) should have consistent state
 *
 * Action Packets generated (approximately 1000+):
 * - Phase 1: Create folders (210 t APs)
 * - Phase 2: Upload files (100-200 t+fa APs)
 * - Phase 3: Set node attributes (150 u APs)
 * - Phase 4: Share and export (40 s/s2/ph APs)
 * - Phase 5: Move and copy (90 d+t APs)
 * - Phase 6: Rename (60 u APs)
 * - Phase 7: Sets operations (103 asp/aep APs)
 * - Phase 8: User attributes (9 ua APs)
 * - Phase 9: Contact invitations (3 opc APs)
 * - Phase 10: Delete operations (62+ d/aer/asr APs)
 * - Phase 11: Acknowledge alerts (1 la AP)
 */

#include "integration_test_utils.h"
#include "mega/testhooks.h"
#include "mock_listeners.h"
#include "SdkTest_test.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <future>
#include <random>
#include <thread>
#include <vector>

using namespace mega;
using namespace testing;

namespace
{
// Test configuration constants
constexpr unsigned int EXECUTOR_INDEX = 0; // Client A - executes operations
constexpr unsigned int RECEIVER1_INDEX = 1; // Client B - receives APs
constexpr unsigned int RECEIVER2_INDEX = 2; // Client C - receives APs

// Folder structure constants
constexpr int NUM_LEVEL1_FOLDERS = 10;
constexpr int NUM_LEVEL2_FOLDERS_PER_L1 = 5; // Total: 50
constexpr int NUM_LEVEL3_FOLDERS_PER_L2 = 3; // Total: 150

// File constants, this is time consuming
constexpr int NUM_FILES_TO_UPLOAD = 100;

// Attribute constants
constexpr int NUM_TAGS_TO_SET = 50;
constexpr int NUM_FAVORITES_TO_SET = 50;
constexpr int NUM_SENSITIVE_TO_SET = 50;
constexpr int NUM_DESCRIPTIONS_TO_SET = 30;

// Share and link constants
constexpr int NUM_SHARES_TO_CREATE = 10;
constexpr int NUM_LINKS_TO_EXPORT = 20;
constexpr int NUM_LINKS_TO_DISABLE = 10;

// Move and copy constants
constexpr int NUM_FILES_TO_MOVE = 30;
constexpr int NUM_FILES_TO_COPY = 30;

// Rename constants
constexpr int NUM_FOLDERS_TO_RENAME = 30;
constexpr int NUM_FILES_TO_RENAME = 30;

// Set constants
constexpr int NUM_SETS_TO_CREATE = 5;
constexpr int NUM_ELEMENTS_TO_ADD = 50;
constexpr int NUM_ELEMENTS_TO_UPDATE_NAME = 20;
constexpr int NUM_ELEMENTS_TO_UPDATE_ORDER = 20;
constexpr int NUM_SETS_TO_EXPORT = 3;
constexpr int NUM_SETS_TO_UPDATE_NAME = 5;

// User attribute constants
constexpr int NUM_USER_ATTRS_TO_SET = 2;
constexpr int NUM_USER_ATTRS_TO_UPDATE = 5;

const int ATTR_TYPES[] = {MegaApi::USER_ATTR_FIRSTNAME,
                          MegaApi::USER_ATTR_LASTNAME,
                          MegaApi::USER_ATTR_LANGUAGE};

// Contact constants
constexpr int NUM_CONTACT_INVITES = 3;

// Delete constants
constexpr int NUM_FILES_TO_DELETE = 30;
constexpr int NUM_SET_ELEMENTS_TO_DELETE = 20;
constexpr int NUM_SETS_TO_DELETE = 2;
constexpr int NUM_FOLDERS_TO_DELETE = 10;

constexpr int NUM_COPY_FILES = 5000;

// Timeout constants
constexpr unsigned int AP_WAIT_TIMEOUT_SECONDS = 300; // 5 minutes for massive APs

const char* TEST_FILE_NAME = "test.png";

} // anonymous namespace

/**
 * @class SdkTestMassiveActionPackets
 * @brief Test fixture for massive action packets testing
 */
class SdkTestMassiveActionPackets: public SdkTest
{
public:
    void SetUp() override
    {
        SdkTest::SetUp();

        // Setup: Get 3 clients logged into the same account
        // Client 0 (A) = executor, Clients 1 and 2 (B and C) = receivers
        ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1, true));

        ASSERT_NO_FATAL_FAILURE(initializeOtherSessions(2));
        // TODO: enableStreaming() and disableStreaming() were previously used by the
        // test cases below, which are disabled. They are no longer useful because
        // the action packet parsing mode is now determined by the "apm" field in the
        // SC response. We should use setScParserMode() instead. However,
        // setScParserMode() changes the mode globally, so we need a way to configure
        // it per MegaApi instance.
        // - DISABLED_MassiveActionPacketsProcessing
        // - DISABLED_IncrementalActionPacketsProcessing
        // megaApi[1]->getClient()->enableStreaming();
        // megaApi[2]->getClient()->disableStreaming();
    }

    void TearDown() override
    {
        // Clean up test-specific resources before calling parent TearDown
        cleanupTestResources();

        SdkTest::TearDown();
    }

    /**
     * @brief Clean up all resources created by this test
     * This method is called in TearDown to ensure cleanup even if test fails
     */
    void cleanupTestResources()
    {
        const std::string logPre = getLogPrefix();
        LOG_info << logPre << "=== Cleaning up test resources ===";

        // Only cleanup if executor client is logged in
        if (!megaApi[EXECUTOR_INDEX] || !megaApi[EXECUTOR_INDEX]->isLoggedIn())
        {
            LOG_warn << logPre << "Executor client not logged in, skipping cleanup";
            return;
        }

        // 1. Disable exported node links (before deleting nodes)
        cleanupExportedNodeLinks();

        // 2. Clean up Sets (disable exports first, then remove sets)
        // Must be done before deleting nodes, as sets reference nodes
        cleanupSets();

        // 3. Delete test root folder (this will delete all files and folders inside)
        // This should be done after Sets cleanup, as sets reference nodes
        cleanupTestRootFolder();

        // 4. Clean up user attributes (independent of nodes)
        cleanupUserAttributes();

        LOG_info << logPre << "=== Test resources cleanup completed ===";
    }

    /**
     * @brief Disable all exported node links found in the account
     */
    void cleanupExportedNodeLinks()
    {
        LOG_info << "Cleaning up exported node links...";

        // Get all public links from the account
        std::unique_ptr<MegaNodeList> nodeLinks(megaApi[EXECUTOR_INDEX]->getPublicLinks());
        if (!nodeLinks)
        {
            return;
        }

        for (int i = 0; i < nodeLinks->size(); ++i)
        {
            MegaNode* node = nodeLinks->get(i);
            if (!node)
                continue;

            // Disable export, ignore errors if already disabled or node deleted
            int err = doDisableExport(EXECUTOR_INDEX, node);
            if (err != API_OK && err != API_ENOENT && err != API_EARGS)
            {
                LOG_warn << "Failed to disable export for node " << toHandle(node->getHandle())
                         << ", error: " << err;
            }
        }
    }

    /**
     * @brief Clean up all Sets found in the account: disable exports first, then remove sets
     */
    void cleanupSets()
    {
        LOG_info << "Cleaning up Sets...";

        // Get all Sets from the account
        std::unique_ptr<MegaSetList> allSets(megaApi[EXECUTOR_INDEX]->getSets());
        if (!allSets)
        {
            return;
        }

        // Collect all set IDs first (we need to collect before deleting to avoid iterator issues)
        std::vector<MegaHandle> setIdsToDelete;

        // First, disable exports for exported sets and collect test-related sets
        for (unsigned i = 0; i < allSets->size(); ++i)
        {
            const MegaSet* s = allSets->get(i);
            if (!s)
                continue;

            // Disable export if exported
            if (s->isExported())
            {
                int err = doDisableExportSet(EXECUTOR_INDEX, s->id());
                if (err != API_OK && err != API_ENOENT && err != API_EARGS)
                {
                    LOG_warn << "Failed to disable export for Set " << toHandle(s->id())
                             << ", error: " << err;
                }
            }

            // Check if this is a test-related Set (starts with "TestSet_" or "UpdatedSet_")
            const char* setName = s->name();
            if (setName)
            {
                std::string nameStr(setName);
                if (nameStr.find("TestSet_") == 0 || nameStr.find("UpdatedSet_") == 0)
                {
                    setIdsToDelete.push_back(s->id());
                }
            }
        }

        // Then remove all test-related sets
        for (auto setId: setIdsToDelete)
        {
            int err = doRemoveSet(EXECUTOR_INDEX, setId);
            if (err != API_OK && err != API_ENOENT && err != API_EARGS)
            {
                LOG_warn << "Failed to remove Set " << toHandle(setId) << ", error: " << err;
            }
        }
    }

    /**
     * @brief Delete all test root folders found in the account (starting with "MassiveAPTest_")
     */
    void cleanupTestRootFolder()
    {
        LOG_info << "Cleaning up test root folders...";

        std::unique_ptr<MegaNode> rootNode(megaApi[EXECUTOR_INDEX]->getRootNode());
        if (!rootNode)
        {
            LOG_warn << "Cannot get root node, skipping folder cleanup";
            return;
        }

        // Get all children of root node
        std::unique_ptr<MegaNodeList> children(
            megaApi[EXECUTOR_INDEX]->getChildren(rootNode.get()));
        if (!children)
        {
            return;
        }

        // Collect folders to delete (we need to collect before deleting to avoid iterator issues)
        std::vector<MegaHandle> foldersToDelete;

        for (int i = 0; i < children->size(); ++i)
        {
            MegaNode* node = children->get(i);
            if (!node || !node->isFolder())
                continue;

            const char* nodeName = node->getName();
            if (nodeName && std::string(nodeName).find("MassiveAPTest_") == 0)
            {
                foldersToDelete.push_back(node->getHandle());
            }
        }

        // Delete all test root folders
        for (auto folderHandle: foldersToDelete)
        {
            std::unique_ptr<MegaNode> folder(
                megaApi[EXECUTOR_INDEX]->getNodeByHandle(folderHandle));
            if (!folder)
                continue;

            LOG_info << "Deleting test root folder: " << folder->getName()
                     << " handle: " << toHandle(folderHandle);

            int err = doDeleteNode(EXECUTOR_INDEX, folder.get());
            if (err != API_OK && err != API_ENOENT)
            {
                LOG_warn << "Failed to delete test root folder " << folder->getName()
                         << ", error: " << err;
            }
        }
    }

    /**
     * @brief Clean up user attributes set by this test
     */
    void cleanupUserAttributes()
    {
        LOG_info << "Cleaning up user attributes...";

        // Delete user attributes: FIRSTNAME, LASTNAME, LANGUAGE
        for (int i = 0; i < NUM_USER_ATTRS_TO_SET && i < 3; ++i)
        {
            RequestTracker rt(megaApi[EXECUTOR_INDEX].get());
            megaApi[EXECUTOR_INDEX]->deleteUserAttribute(ATTR_TYPES[i], &rt);
            int err = rt.waitForResult();
            if (err != API_OK && err != API_ENOENT)
            {
                LOG_warn << "Failed to delete user attribute " << ATTR_TYPES[i]
                         << ", error: " << err;
            }
        }

        // Delete avatar
        {
            RequestTracker rt(megaApi[EXECUTOR_INDEX].get());
            megaApi[EXECUTOR_INDEX]->deleteUserAttribute(MegaApi::USER_ATTR_AVATAR, &rt);
            int err = rt.waitForResult();
            if (err != API_OK && err != API_ENOENT)
            {
                LOG_warn << "Failed to delete avatar, error: " << err;
            }
        }

        // Reset device name (set to empty string)
        const char* deviceId = megaApi[EXECUTOR_INDEX]->getDeviceId();
        if (deviceId)
        {
            int err = doSetDeviceName(EXECUTOR_INDEX, deviceId, "");
            if (err != API_OK && err != API_ENOENT && err != API_EARGS)
            {
                LOG_warn << "Failed to reset device name, error: " << err;
            }
        }
    }

protected:
    // Storage for created resources (for cleanup and verification)
    std::vector<MegaHandle> mCreatedFolderHandles;
    std::vector<MegaHandle> mCreatedFileHandles;
    std::vector<MegaHandle> mCreatedSetHandles;
    std::vector<std::pair<MegaHandle, MegaHandle>> mCreatedSetElements; // {setId, elementId}
    std::vector<MegaHandle> mExportedNodeHandles;
    std::vector<MegaHandle> mSharedFolderHandles;
    std::string mTestRootFolderName;
    MegaHandle mTestRootHandle = INVALID_HANDLE;

    // Session storage for receiver clients
    std::unique_ptr<char[]> mSessionClients[3] = {nullptr};

    /**
     * @brief Initialize other sessions, they should login to the same account
     */
    void initializeOtherSessions(size_t count)
    {
        megaApi.resize(megaApi.size() + count);
        mApi.resize(mApi.size() + count);
        for (size_t i = 1; i < megaApi.size(); i++)
        {
            configureTestInstance(static_cast<unsigned>(i), mApi[0].email, mApi[0].pwd);

            NiceMock<MockRequestListener> loginTracker(megaApi[i].get());
            megaApi[i]->login(mApi[i].email.c_str(), mApi[i].pwd.c_str(), &loginTracker);
            ASSERT_TRUE(loginTracker.waitForFinishOrTimeout(sdk_test::MAX_TIMEOUT))
                << "[" << i << " ]session login failed";

            NiceMock<MockRequestListener> fetchNodesTracker(megaApi[i].get());
            megaApi[i]->fetchNodes(&fetchNodesTracker);
            ASSERT_TRUE(fetchNodesTracker.waitForFinishOrTimeout(sdk_test::MAX_TIMEOUT))
                << "[" << i << "] session fetch nodes failed";
        }
    }

    /**
     * @brief Create the test root folder
     */
    void verifyClientsLogin(const std::string& logPre, const std::vector<unsigned int>& apiIndexes)
    {
        for (auto apiIndex: apiIndexes)
        {
            ASSERT_TRUE(megaApi[apiIndex]->isLoggedIn())
                << "Client " << apiIndex << " not logged in";
        }

        LOG_info << logPre << "All clients logged in successfully";
    }

    /**
     * @brief Create the test root folder
     */
    void createTestRootFolder()
    {
        mTestRootFolderName =
            "MassiveAPTest_" +
            std::to_string(std::chrono::system_clock::now().time_since_epoch().count());

        std::unique_ptr<MegaNode> rootNode(megaApi[EXECUTOR_INDEX]->getRootNode());
        ASSERT_NE(rootNode, nullptr);

        mTestRootHandle = createFolder(EXECUTOR_INDEX, mTestRootFolderName.c_str(), rootNode.get());
        ASSERT_NE(mTestRootHandle, INVALID_HANDLE) << "Failed to create test root folder";

        LOG_info << "Created test root folder: " << mTestRootFolderName
                 << " handle: " << toHandle(mTestRootHandle);
    }

    /**
     * @brief Phase 1.1: Create level 1 folders (10 folders)
     */
    void phase1_1_CreateLevel1Folders()
    {
        LOG_info << "Phase 1.1: Creating " << NUM_LEVEL1_FOLDERS << " level 1 folders...";

        std::unique_ptr<MegaNode> testRoot(
            megaApi[EXECUTOR_INDEX]->getNodeByHandle(mTestRootHandle));
        ASSERT_NE(testRoot, nullptr);

        for (int i = 0; i < NUM_LEVEL1_FOLDERS; ++i)
        {
            std::string folderName = "L1_Folder_" + std::to_string(i);
            MegaHandle handle = createFolder(EXECUTOR_INDEX, folderName.c_str(), testRoot.get());
            ASSERT_NE(handle, INVALID_HANDLE) << "Failed to create L1 folder: " << folderName;
            mCreatedFolderHandles.push_back(handle);
        }

        LOG_info << "Phase 1.1 completed: Created " << NUM_LEVEL1_FOLDERS << " L1 folders";
    }

    /**
     * @brief Phase 1.2: Create level 2 folders (50 folders)
     */
    void phase1_2_CreateLevel2Folders()
    {
        LOG_info << "Phase 1.2: Creating " << (NUM_LEVEL1_FOLDERS * NUM_LEVEL2_FOLDERS_PER_L1)
                 << " level 2 folders...";

        size_t l1FolderCount =
            std::min(static_cast<size_t>(NUM_LEVEL1_FOLDERS), mCreatedFolderHandles.size());

        for (size_t i = 0; i < l1FolderCount; ++i)
        {
            std::unique_ptr<MegaNode> parentNode(
                megaApi[EXECUTOR_INDEX]->getNodeByHandle(mCreatedFolderHandles[i]));
            ASSERT_NE(parentNode, nullptr);

            for (int j = 0; j < NUM_LEVEL2_FOLDERS_PER_L1; ++j)
            {
                std::string folderName = "L2_Folder_" + std::to_string(i) + "_" + std::to_string(j);
                MegaHandle handle =
                    createFolder(EXECUTOR_INDEX, folderName.c_str(), parentNode.get());
                ASSERT_NE(handle, INVALID_HANDLE) << "Failed to create L2 folder: " << folderName;
                mCreatedFolderHandles.push_back(handle);
            }
        }

        LOG_info << "Phase 1.2 completed";
    }

    /**
     * @brief Phase 1.3: Create level 3 folders (150 folders)
     */
    void phase1_3_CreateLevel3Folders()
    {
        LOG_info << "Phase 1.3: Creating level 3 folders...";

        // L2 folders start at index NUM_LEVEL1_FOLDERS
        size_t l2StartIndex = NUM_LEVEL1_FOLDERS;
        size_t l2Count = NUM_LEVEL1_FOLDERS * NUM_LEVEL2_FOLDERS_PER_L1;

        for (size_t i = 0; i < l2Count && (l2StartIndex + i) < mCreatedFolderHandles.size(); ++i)
        {
            std::unique_ptr<MegaNode> parentNode(
                megaApi[EXECUTOR_INDEX]->getNodeByHandle(mCreatedFolderHandles[l2StartIndex + i]));
            if (!parentNode)
                continue;

            for (int j = 0; j < NUM_LEVEL3_FOLDERS_PER_L2; ++j)
            {
                std::string folderName = "L3_Folder_" + std::to_string(i) + "_" + std::to_string(j);
                MegaHandle handle =
                    createFolder(EXECUTOR_INDEX, folderName.c_str(), parentNode.get());
                ASSERT_NE(handle, INVALID_HANDLE) << "Failed to create L3 folder: " << folderName;
                mCreatedFolderHandles.push_back(handle);
            }
        }

        LOG_info << "Phase 1.3 completed";
    }

    /**
     * @brief Phase 2.1: Upload files (300 files)
     */
    void phase2_1_UploadFiles()
    {
        LOG_info << "Phase 2.1: Uploading " << NUM_FILES_TO_UPLOAD << " files...";

        // Step 1: Download png file and use RAII to ensure cleanup
        const std::string pngFileName = "logo.png";
        ASSERT_TRUE(getFileFromArtifactory("test-data/logo.png", pngFileName.c_str()));

        const MrProper defer{[pngFileName, this]()
                             {
                                 deleteFile(pngFileName.c_str());
                             }};

        int64_t fileSize = getFilesize(pngFileName.c_str());
        ASSERT_GT(fileSize, 0) << "Failed to get file size for " << pngFileName;

        // Distribute files across folders
        size_t folderCount = mCreatedFolderHandles.size();
        ASSERT_GT(folderCount, 0u) << "No folders available for file upload";

        // Step 2: Upload files with media attributes to generate "fa" action packets
        for (size_t i = 0; i < static_cast<size_t>(NUM_FILES_TO_UPLOAD); ++i)
        {
            size_t folderIndex = i % folderCount;
            std::unique_ptr<MegaNode> parentNode(
                megaApi[EXECUTOR_INDEX]->getNodeByHandle(mCreatedFolderHandles[folderIndex]));

            if (!parentNode)
                continue;

            std::string cloudFileName = "File_" + std::to_string(i) + ".png";
            std::string encryptedFileName = "File_" + std::to_string(i) + "_encrypted.png";
            std::string thumbnailFileName = "File_" + std::to_string(i) + "_thumbnail.png";

            const MrProper defer{[encryptedFileName, thumbnailFileName, this]()
                                 {
                                     // Clean up temporary files for this iteration
                                     deleteFile(encryptedFileName.c_str());
                                     deleteFile(thumbnailFileName.c_str());
                                 }};

            // Create a "media upload" instance
            std::unique_ptr<MegaBackgroundMediaUpload> req(
                MegaBackgroundMediaUpload::createInstance(megaApi[EXECUTOR_INDEX].get()));

            // Request a media upload URL
            int err =
                synchronousMediaUploadRequestURL(EXECUTOR_INDEX, fileSize, req.get(), nullptr);
            ASSERT_EQ(API_OK, err) << "Cannot request media upload URL for file " << i;

            // Get the generated media upload URL
            std::unique_ptr<char[]> url(req->getUploadURL());
            ASSERT_NE(nullptr, url) << "Got NULL media upload URL for file " << i;
            ASSERT_NE('\0', url[0]) << "Got empty media upload URL for file " << i;

            // Encrypt file contents with the file key and get URL suffix
            int64_t encryptedFileSize = fileSize;
            std::unique_ptr<char[]> suffix(req->encryptFile(pngFileName.c_str(),
                                                            0,
                                                            &encryptedFileSize,
                                                            encryptedFileName.c_str(),
                                                            false));
            ASSERT_NE(nullptr, suffix) << "Got NULL suffix after encryption for file " << i;

            std::unique_ptr<char[]> fingerprint(
                megaApi[EXECUTOR_INDEX]->getFingerprint(pngFileName.c_str()));

            // Create and PUT thumbnail to generate "fa" action packets
            ASSERT_EQ(true,
                      megaApi[EXECUTOR_INDEX]->createThumbnail(pngFileName.c_str(),
                                                               thumbnailFileName.c_str()))
                << "Failed to create thumbnail for file " << i;
            err = doPutThumbnail(EXECUTOR_INDEX, req.get(), thumbnailFileName.c_str());
            ASSERT_EQ(API_OK, err) << "ERROR putting thumbnail for file " << i;

            // Build final URL and upload file
            string finalurl(url.get());
            if (suffix)
                finalurl.append(suffix.get());

            string binaryUploadToken;
            synchronousHttpPOSTFile(finalurl, encryptedFileName, binaryUploadToken);

            ASSERT_NE(binaryUploadToken.size(), 0u);
            ASSERT_GT(binaryUploadToken.size(), 3u)
                << "POST failed for file " << i << ", fa server error: " << binaryUploadToken;

            std::unique_ptr<char[]> base64UploadToken(
                megaApi[EXECUTOR_INDEX]->binaryToBase64(binaryUploadToken.data(),
                                                        binaryUploadToken.length()));

            // Complete media upload to the target folder using RequestTracker to get node handle
            RequestTracker rt(megaApi[EXECUTOR_INDEX].get());
            megaApi[EXECUTOR_INDEX]->backgroundMediaUploadComplete(req.get(),
                                                                   cloudFileName.c_str(),
                                                                   parentNode.get(),
                                                                   fingerprint.get(),
                                                                   nullptr,
                                                                   base64UploadToken.get(),
                                                                   &rt);
            err = rt.waitForResult();
            ASSERT_EQ(API_OK, err) << "Cannot complete media upload for file " << i;

            // Get the uploaded node handle
            MegaHandle uploadedHandle = rt.getNodeHandle();
            if (uploadedHandle != INVALID_HANDLE)
            {
                mCreatedFileHandles.push_back(uploadedHandle);
            }

            // Log progress every 50 files
            if ((i + 1) % 50 == 0)
            {
                LOG_info << "Uploaded " << (i + 1) << "/" << NUM_FILES_TO_UPLOAD << " files";
            }
        }

        LOG_info << "Phase 2.1 completed: Uploaded " << mCreatedFileHandles.size() << " files";
    }

    /**
     * @brief Phase 3.1: Set tags on nodes (50 operations)
     */
    void phase3_1_SetTags()
    {
        LOG_info << "Phase 3.1: Setting tags on " << NUM_TAGS_TO_SET << " nodes...";

        size_t nodeCount =
            std::min(static_cast<size_t>(NUM_TAGS_TO_SET), mCreatedFileHandles.size());

        for (size_t i = 0; i < nodeCount; ++i)
        {
            std::unique_ptr<MegaNode> node(
                megaApi[EXECUTOR_INDEX]->getNodeByHandle(mCreatedFileHandles[i]));
            if (!node)
                continue;

            std::string tagName = "tag_" + std::to_string(i);
            RequestTracker rt(megaApi[EXECUTOR_INDEX].get());
            megaApi[EXECUTOR_INDEX]->addNodeTag(node.get(), tagName.c_str(), &rt);
            ASSERT_EQ(rt.waitForResult(), API_OK) << "Failed to add tag: " << tagName;
        }

        LOG_info << "Phase 3.1 completed";
    }

    /**
     * @brief Phase 3.2: Set favorites (50 operations)
     */
    void phase3_2_SetFavorites()
    {
        LOG_info << "Phase 3.2: Setting " << NUM_FAVORITES_TO_SET << " nodes as favorites...";

        size_t startIndex = NUM_TAGS_TO_SET;
        size_t endIndex = std::min(startIndex + NUM_FAVORITES_TO_SET, mCreatedFileHandles.size());

        for (size_t i = startIndex; i < endIndex; ++i)
        {
            std::unique_ptr<MegaNode> node(
                megaApi[EXECUTOR_INDEX]->getNodeByHandle(mCreatedFileHandles[i]));
            if (!node)
                continue;

            ASSERT_EQ(synchronousSetNodeFavourite(EXECUTOR_INDEX, node.get(), true), API_OK)
                << "Failed to set favorite";
        }

        LOG_info << "Phase 3.2 completed";
    }

    /**
     * @brief Phase 3.3: Set sensitive markers (50 operations)
     */
    void phase3_3_SetSensitive()
    {
        LOG_info << "Phase 3.3: Setting " << NUM_SENSITIVE_TO_SET << " nodes as sensitive...";

        size_t startIndex = NUM_TAGS_TO_SET + NUM_FAVORITES_TO_SET;
        size_t endIndex = std::min(startIndex + NUM_SENSITIVE_TO_SET, mCreatedFileHandles.size());

        for (size_t i = startIndex; i < endIndex; ++i)
        {
            std::unique_ptr<MegaNode> node(
                megaApi[EXECUTOR_INDEX]->getNodeByHandle(mCreatedFileHandles[i]));
            if (!node)
                continue;

            ASSERT_EQ(synchronousSetNodeSensitive(EXECUTOR_INDEX, node.get(), true), API_OK)
                << "Failed to set sensitive";
        }

        LOG_info << "Phase 3.3 completed";
    }

    /**
     * @brief Phase 3.4: Set descriptions (30 operations)
     */
    void phase3_4_SetDescriptions()
    {
        LOG_info << "Phase 3.4: Setting descriptions on " << NUM_DESCRIPTIONS_TO_SET << " nodes...";

        size_t startIndex = NUM_TAGS_TO_SET + NUM_FAVORITES_TO_SET + NUM_SENSITIVE_TO_SET;
        size_t endIndex =
            std::min(startIndex + NUM_DESCRIPTIONS_TO_SET, mCreatedFileHandles.size());

        for (size_t i = startIndex; i < endIndex; ++i)
        {
            std::unique_ptr<MegaNode> node(
                megaApi[EXECUTOR_INDEX]->getNodeByHandle(mCreatedFileHandles[i]));
            if (!node)
                continue;

            std::string description = "Description for file " + std::to_string(i);
            RequestTracker rt(megaApi[EXECUTOR_INDEX].get());
            megaApi[EXECUTOR_INDEX]->setNodeDescription(node.get(), description.c_str(), &rt);
            ASSERT_EQ(rt.waitForResult(), API_OK) << "Failed to set description";
        }

        LOG_info << "Phase 3.4 completed";
    }

    /**
     * @brief Phase 4.1: Create shares (10 operations)
     */
    void phase4_1_CreateShares()
    {
        LOG_info << "Phase 4.1: Creating " << NUM_SHARES_TO_CREATE << " shares...";

        // Note: Shares require another user. For this test, we'll use openShareDialog
        // which prepares a folder for sharing (generates s2 action packet)
        size_t folderCount =
            std::min(static_cast<size_t>(NUM_SHARES_TO_CREATE), mCreatedFolderHandles.size());

        for (size_t i = 0; i < folderCount; ++i)
        {
            std::unique_ptr<MegaNode> node(
                megaApi[EXECUTOR_INDEX]->getNodeByHandle(mCreatedFolderHandles[i]));
            if (!node)
                continue;

            int err = doOpenShareDialog(EXECUTOR_INDEX, node.get());
            ASSERT_EQ(err, API_OK) << "Failed to open share dialog for folder " << i;
            mSharedFolderHandles.push_back(mCreatedFolderHandles[i]);
        }

        LOG_info << "Phase 4.1 completed";
    }

    /**
     * @brief Phase 4.2: Export links (20 operations)
     */
    void phase4_2_ExportLinks()
    {
        LOG_info << "Phase 4.2: Exporting " << NUM_LINKS_TO_EXPORT << " links...";

        size_t fileCount =
            std::min(static_cast<size_t>(NUM_LINKS_TO_EXPORT), mCreatedFileHandles.size());

        for (size_t i = 0; i < fileCount; ++i)
        {
            std::unique_ptr<MegaNode> node(
                megaApi[EXECUTOR_INDEX]->getNodeByHandle(mCreatedFileHandles[i]));
            if (!node)
                continue;

            int err = doExportNode(EXECUTOR_INDEX, node.get(), 0, false, false);
            ASSERT_EQ(err, API_OK) << "Failed to export node " << i;
            mExportedNodeHandles.push_back(mCreatedFileHandles[i]);
        }

        LOG_info << "Phase 4.2 completed";
    }

    /**
     * @brief Phase 4.3: Disable links (10 operations)
     */
    void phase4_3_DisableLinks()
    {
        LOG_info << "Phase 4.3: Disabling " << NUM_LINKS_TO_DISABLE << " links...";

        size_t count =
            std::min(static_cast<size_t>(NUM_LINKS_TO_DISABLE), mExportedNodeHandles.size());

        for (size_t i = 0; i < count; ++i)
        {
            std::unique_ptr<MegaNode> node(
                megaApi[EXECUTOR_INDEX]->getNodeByHandle(mExportedNodeHandles[i]));
            if (!node)
                continue;

            int err = doDisableExport(EXECUTOR_INDEX, node.get());
            ASSERT_EQ(err, API_OK) << "Failed to disable export for node " << i;
        }

        // Remove disabled handles from exported list
        if (count > 0)
        {
            mExportedNodeHandles.erase(mExportedNodeHandles.begin(),
                                       mExportedNodeHandles.begin() +
                                           static_cast<std::ptrdiff_t>(count));
        }

        LOG_info << "Phase 4.3 completed";
    }

    /**
     * @brief Phase 5.1: Move files (30 operations)
     */
    void phase5_1_MoveFiles()
    {
        LOG_info << "Phase 5.1: Moving " << NUM_FILES_TO_MOVE << " files...";

        // We need source files and destination folders
        size_t fileCount = mCreatedFileHandles.size();
        size_t folderCount = mCreatedFolderHandles.size();

        if (fileCount < NUM_FILES_TO_MOVE || folderCount < 2)
        {
            LOG_warn << "Not enough files/folders for move operation, skipping...";
            return;
        }

        // Move files from the end of the list to different folders
        for (size_t i = 0; i < static_cast<size_t>(NUM_FILES_TO_MOVE); ++i)
        {
            size_t fileIndex = fileCount - 1 - i;
            size_t destFolderIndex = (i + 1) % folderCount; // Different from source

            std::unique_ptr<MegaNode> fileNode(
                megaApi[EXECUTOR_INDEX]->getNodeByHandle(mCreatedFileHandles[fileIndex]));
            std::unique_ptr<MegaNode> destNode(
                megaApi[EXECUTOR_INDEX]->getNodeByHandle(mCreatedFolderHandles[destFolderIndex]));

            if (!fileNode || !destNode)
                continue;

            MegaHandle movedHandle = INVALID_HANDLE;
            int err = doMoveNode(EXECUTOR_INDEX, &movedHandle, fileNode.get(), destNode.get());
            ASSERT_EQ(err, API_OK) << "Failed to move file " << i;
        }

        LOG_info << "Phase 5.1 completed";
    }

    /**
     * @brief Phase 5.2: Copy files (30 operations)
     */
    void phase5_2_CopyFiles()
    {
        LOG_info << "Phase 5.2: Copying " << NUM_FILES_TO_COPY << " files...";

        size_t fileCount = mCreatedFileHandles.size();
        size_t folderCount = mCreatedFolderHandles.size();

        if (fileCount < NUM_FILES_TO_COPY || folderCount < 2)
        {
            LOG_warn << "Not enough files/folders for copy operation, skipping...";
            return;
        }

        for (size_t i = 0; i < static_cast<size_t>(NUM_FILES_TO_COPY); ++i)
        {
            size_t fileIndex = i % fileCount;
            size_t destFolderIndex = (i + 2) % folderCount;

            std::unique_ptr<MegaNode> fileNode(
                megaApi[EXECUTOR_INDEX]->getNodeByHandle(mCreatedFileHandles[fileIndex]));
            std::unique_ptr<MegaNode> destNode(
                megaApi[EXECUTOR_INDEX]->getNodeByHandle(mCreatedFolderHandles[destFolderIndex]));

            if (!fileNode || !destNode)
                continue;

            MegaHandle copiedHandle = INVALID_HANDLE;
            int err = doCopyNode(EXECUTOR_INDEX, &copiedHandle, fileNode.get(), destNode.get());
            ASSERT_EQ(err, API_OK) << "Failed to copy file " << i;

            if (copiedHandle != INVALID_HANDLE)
            {
                mCreatedFileHandles.push_back(copiedHandle);
            }
        }

        LOG_info << "Phase 5.2 completed";
    }

    /**
     * @brief Phase 6.1: Rename folders (30 operations)
     */
    void phase6_1_RenameFolders()
    {
        LOG_info << "Phase 6.1: Renaming " << NUM_FOLDERS_TO_RENAME << " folders...";

        size_t count =
            std::min(static_cast<size_t>(NUM_FOLDERS_TO_RENAME), mCreatedFolderHandles.size());

        for (size_t i = 0; i < count; ++i)
        {
            std::unique_ptr<MegaNode> node(
                megaApi[EXECUTOR_INDEX]->getNodeByHandle(mCreatedFolderHandles[i]));
            if (!node)
                continue;

            std::string newName = std::string(node->getName()) + "_renamed";
            int err = doRenameNode(EXECUTOR_INDEX, node.get(), newName.c_str());
            ASSERT_EQ(err, API_OK) << "Failed to rename folder " << i;
        }

        LOG_info << "Phase 6.1 completed";
    }

    /**
     * @brief Phase 6.2: Rename files (30 operations)
     */
    void phase6_2_RenameFiles()
    {
        LOG_info << "Phase 6.2: Renaming " << NUM_FILES_TO_RENAME << " files...";

        size_t count =
            std::min(static_cast<size_t>(NUM_FILES_TO_RENAME), mCreatedFileHandles.size());

        for (size_t i = 0; i < count; ++i)
        {
            std::unique_ptr<MegaNode> node(
                megaApi[EXECUTOR_INDEX]->getNodeByHandle(mCreatedFileHandles[i]));
            if (!node)
                continue;

            std::string newName = std::string(node->getName()) + "_renamed";
            int err = doRenameNode(EXECUTOR_INDEX, node.get(), newName.c_str());
            ASSERT_EQ(err, API_OK) << "Failed to rename file " << i;
        }

        LOG_info << "Phase 6.2 completed";
    }

    /**
     * @brief Phase 7.1: Create Sets (5 operations)
     */
    void phase7_1_CreateSets()
    {
        LOG_info << "Phase 7.1: Creating " << NUM_SETS_TO_CREATE << " Sets...";

        for (int i = 0; i < NUM_SETS_TO_CREATE; ++i)
        {
            std::string setName = "TestSet_" + std::to_string(i);
            MegaSet* newSet = nullptr;

            int err =
                doCreateSet(EXECUTOR_INDEX, &newSet, setName.c_str(), MegaSet::SET_TYPE_ALBUM);
            ASSERT_EQ(err, API_OK) << "Failed to create Set: " << setName;

            if (newSet)
            {
                mCreatedSetHandles.push_back(newSet->id());
                delete newSet;
            }
        }

        LOG_info << "Phase 7.1 completed: Created " << mCreatedSetHandles.size() << " Sets";
    }

    /**
     * @brief Phase 7.2: Add elements to Sets (50 operations)
     */
    void phase7_2_AddElements()
    {
        LOG_info << "Phase 7.2: Adding " << NUM_ELEMENTS_TO_ADD << " elements to Sets...";

        if (mCreatedSetHandles.empty() || mCreatedFileHandles.empty())
        {
            LOG_warn << "No Sets or files available for adding elements, skipping...";
            return;
        }

        size_t fileCount = mCreatedFileHandles.size();
        size_t setCount = mCreatedSetHandles.size();

        for (size_t i = 0; i < static_cast<size_t>(NUM_ELEMENTS_TO_ADD) && i < fileCount; ++i)
        {
            MegaHandle setId = mCreatedSetHandles[i % setCount];
            MegaHandle nodeId = mCreatedFileHandles[i];

            std::string elementName = "Element_" + std::to_string(i);
            MegaSetElementList* newElements = nullptr;

            int err = doCreateSetElement(EXECUTOR_INDEX,
                                         &newElements,
                                         setId,
                                         nodeId,
                                         elementName.c_str());

            if (err == API_OK && newElements && newElements->size() > 0)
            {
                mCreatedSetElements.push_back({setId, newElements->get(0)->id()});
            }

            if (newElements)
                delete newElements;

            // Some elements might fail due to duplicates, that's OK
        }

        LOG_info << "Phase 7.2 completed: Added " << mCreatedSetElements.size() << " elements";
    }

    /**
     * @brief Phase 7.3: Update element names (20 operations)
     */
    void phase7_3_UpdateElementNames()
    {
        LOG_info << "Phase 7.3: Updating " << NUM_ELEMENTS_TO_UPDATE_NAME << " element names...";

        size_t count =
            std::min(static_cast<size_t>(NUM_ELEMENTS_TO_UPDATE_NAME), mCreatedSetElements.size());

        for (size_t i = 0; i < count; ++i)
        {
            MegaHandle setId = mCreatedSetElements[i].first;
            MegaHandle elementId = mCreatedSetElements[i].second;
            std::string newName = "UpdatedElement_" + std::to_string(i);

            MegaHandle resultId = INVALID_HANDLE;
            (void)doUpdateSetElementName(EXECUTOR_INDEX,
                                         &resultId,
                                         setId,
                                         elementId,
                                         newName.c_str());
            // Ignore errors for elements that may have been removed
        }

        LOG_info << "Phase 7.3 completed";
    }

    /**
     * @brief Phase 7.4: Update element orders (20 operations)
     */
    void phase7_4_UpdateElementOrders()
    {
        LOG_info << "Phase 7.4: Updating " << NUM_ELEMENTS_TO_UPDATE_ORDER << " element orders...";

        size_t count =
            std::min(static_cast<size_t>(NUM_ELEMENTS_TO_UPDATE_ORDER), mCreatedSetElements.size());

        for (size_t i = 0; i < count; ++i)
        {
            MegaHandle setId = mCreatedSetElements[i].first;
            MegaHandle elementId = mCreatedSetElements[i].second;
            int64_t newOrder = static_cast<int64_t>(i * 100);

            MegaHandle resultId = INVALID_HANDLE;
            (void)doUpdateSetElementOrder(EXECUTOR_INDEX, &resultId, setId, elementId, newOrder);
            // Ignore errors
        }

        LOG_info << "Phase 7.4 completed";
    }

    /**
     * @brief Phase 7.5: Export Sets (3 operations)
     */
    void phase7_5_ExportSets()
    {
        LOG_info << "Phase 7.5: Exporting " << NUM_SETS_TO_EXPORT << " Sets...";

        size_t count = std::min(static_cast<size_t>(NUM_SETS_TO_EXPORT), mCreatedSetHandles.size());

        for (size_t i = 0; i < count; ++i)
        {
            MegaSet* exportedSet = nullptr;
            std::string url;

            (void)doExportSet(EXECUTOR_INDEX, &exportedSet, url, mCreatedSetHandles[i]);
            // Ignore errors

            if (exportedSet)
                delete exportedSet;
        }

        LOG_info << "Phase 7.5 completed";
    }

    /**
     * @brief Phase 7.6: Update Set names (5 operations)
     */
    void phase7_6_UpdateSetNames()
    {
        LOG_info << "Phase 7.6: Updating " << NUM_SETS_TO_UPDATE_NAME << " Set names...";

        size_t count =
            std::min(static_cast<size_t>(NUM_SETS_TO_UPDATE_NAME), mCreatedSetHandles.size());

        for (size_t i = 0; i < count; ++i)
        {
            std::string newName = "UpdatedSet_" + std::to_string(i);
            MegaHandle resultId = INVALID_HANDLE;

            (void)
                doUpdateSetName(EXECUTOR_INDEX, &resultId, mCreatedSetHandles[i], newName.c_str());
            // Ignore errors
        }

        LOG_info << "Phase 7.6 completed";
    }

    /**
     * @brief Phase 8.1: Set user attributes (3 operations)
     */
    void phase8_1_SetUserAttributes()
    {
        LOG_info << "Phase 8.1: Setting " << NUM_USER_ATTRS_TO_SET << " user attributes...";

        // Get device ID first
        const char* deviceId = megaApi[EXECUTOR_INDEX]->getDeviceId();
        if (deviceId)
        {
            // Set device name
            (void)doSetDeviceName(EXECUTOR_INDEX, deviceId, "MassiveAPTestDevice");
            // May fail if already set, ignore errors
        }

        // Set other attributes using generic method
        for (int i = 0; i < NUM_USER_ATTRS_TO_SET; ++i)
        {
            std::string value = "TestValue_" + std::to_string(i);
            (void)synchronousSetUserAttribute(EXECUTOR_INDEX, ATTR_TYPES[i], value.c_str());
        }

        LOG_info << "Phase 8.1 completed";
    }

    /**
     * @brief Phase 8.2: Set avatar (1 operation)
     */
    void phase8_2_SetAvatar()
    {
        LOG_info << "Phase 8.2: Setting avatar...";

        // Create a simple test image file
        const std::string avatarFile = "test_avatar.jpg";

        // Check if we have a test image, if not skip
        if (!createFile(avatarFile, false))
        {
            LOG_warn << "Could not create avatar file, skipping...";
            return;
        }

        (void)synchronousSetAvatar(EXECUTOR_INDEX, avatarFile.c_str());
        // Avatar setting may fail for various reasons, ignore errors

        deleteFile(avatarFile);

        LOG_info << "Phase 8.2 completed";
    }

    /**
     * @brief Phase 8.3: Update user attributes (5 operations)
     */
    void phase8_3_UpdateUserAttributes()
    {
        LOG_info << "Phase 8.3: Updating " << NUM_USER_ATTRS_TO_UPDATE << " user attributes...";

        // Get device ID first
        const char* deviceId = megaApi[EXECUTOR_INDEX]->getDeviceId();
        if (deviceId)
        {
            // Update device name multiple times
            for (int i = 0; i < NUM_USER_ATTRS_TO_UPDATE; ++i)
            {
                std::string deviceName = "UpdatedDevice_" + std::to_string(i);
                (void)doSetDeviceName(EXECUTOR_INDEX, deviceId, deviceName.c_str());
                // Ignore errors
            }
        }

        LOG_info << "Phase 8.3 completed";
    }

    /**
     * @brief Phase 9.1: Send contact invitations (3 operations)
     * Note: This requires auxiliary test accounts
     */
    void phase9_1_SendContactInvitations()
    {
        LOG_info << "Phase 9.1: Sending " << NUM_CONTACT_INVITES << " contact invitations...";

        // Generate fake email addresses for invitation (they won't be accepted)
        // This still generates opc action packets
        for (int i = 0; i < NUM_CONTACT_INVITES; ++i)
        {
            std::string fakeEmail =
                "fake_contact_" + std::to_string(i) + "_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                "@example-nonexistent-domain.com";

            (void)synchronousInviteContact(EXECUTOR_INDEX,
                                           fakeEmail.c_str(),
                                           "Test invitation",
                                           MegaContactRequest::INVITE_ACTION_ADD);
            // Invitations to non-existent emails may fail, that's OK
        }

        LOG_info << "Phase 9.1 completed";
    }

    /**
     * @brief Phase 10.1: Delete files (30 operations)
     */
    void phase10_1_DeleteFiles()
    {
        LOG_info << "Phase 10.1: Deleting " << NUM_FILES_TO_DELETE << " files...";

        size_t count =
            std::min(static_cast<size_t>(NUM_FILES_TO_DELETE), mCreatedFileHandles.size());

        for (size_t i = 0; i < count; ++i)
        {
            std::unique_ptr<MegaNode> node(
                megaApi[EXECUTOR_INDEX]->getNodeByHandle(mCreatedFileHandles[i]));
            if (!node)
                continue;

            int err = doDeleteNode(EXECUTOR_INDEX, node.get());
            // Delete node is quite often and shouldn't fail, so assert for errors
            ASSERT_EQ(err, API_OK) << "Failed to delete file " << i;
        }

        // Remove handle
        if (count > 0)
        {
            mCreatedFileHandles.erase(mCreatedFileHandles.begin(),
                                      mCreatedFileHandles.begin() +
                                          static_cast<std::ptrdiff_t>(count));
        }

        LOG_info << "Phase 10.1 completed";
    }

    /**
     * @brief Phase 10.2: Delete Set elements (20 operations)
     */
    void phase10_2_DeleteSetElements()
    {
        LOG_info << "Phase 10.2: Deleting " << NUM_SET_ELEMENTS_TO_DELETE << " Set elements...";

        size_t count =
            std::min(static_cast<size_t>(NUM_SET_ELEMENTS_TO_DELETE), mCreatedSetElements.size());

        size_t actualDeletedCount = count;
        for (size_t i = 0; i < count; ++i)
        {
            MegaHandle setId = mCreatedSetElements[i].first;
            MegaHandle elementId = mCreatedSetElements[i].second;

            if (doRemoveSetElement(EXECUTOR_INDEX, setId, elementId) != API_OK)
            {
                --actualDeletedCount;
            }
            // Ignore errors for already deleted elements
        }

        // To simplify the logic, we only recored the count for statistics,
        // so it's irrelevant which elements are deleted.
        if (actualDeletedCount > 0)
        {
            mCreatedSetElements.erase(mCreatedSetElements.begin(),
                                      mCreatedSetElements.begin() +
                                          static_cast<std::ptrdiff_t>(actualDeletedCount));
        }

        LOG_info << "Phase 10.2 completed";
    }

    /**
     * @brief Phase 10.3: Delete Sets (2 operations)
     */
    void phase10_3_DeleteSets()
    {
        LOG_info << "Phase 10.3: Deleting " << NUM_SETS_TO_DELETE << " Sets...";

        size_t count = std::min(static_cast<size_t>(NUM_SETS_TO_DELETE), mCreatedSetHandles.size());

        size_t actualDeletedCount = count;
        for (size_t i = 0; i < count; ++i)
        {
            if (doRemoveSet(EXECUTOR_INDEX, mCreatedSetHandles[i]) != API_OK)
            {
                --actualDeletedCount;
            }
        }

        // Adjust renaming count for statistics
        if (actualDeletedCount > 0)
        {
            mCreatedSetHandles.erase(mCreatedSetHandles.begin(),
                                     mCreatedSetHandles.begin() +
                                         static_cast<std::ptrdiff_t>(actualDeletedCount));
        }

        LOG_info << "Phase 10.3 completed";
    }

    /**
     * @brief Phase 10.4: Delete folders (10 operations)
     */
    void phase10_4_DeleteFolders()
    {
        LOG_info << "Phase 10.4: Deleting " << NUM_FOLDERS_TO_DELETE << " folders...";

        // Delete from the end (L3 folders first to avoid parent-child conflicts)
        size_t totalFolders = mCreatedFolderHandles.size();
        size_t count = std::min(static_cast<size_t>(NUM_FOLDERS_TO_DELETE), totalFolders);
        size_t actualDeletedCount = count;
        for (size_t i = 0; i < count; ++i)
        {
            size_t index = totalFolders - 1 - i;
            std::unique_ptr<MegaNode> node(
                megaApi[EXECUTOR_INDEX]->getNodeByHandle(mCreatedFolderHandles[index]));
            if (!node)
                continue;

            if (doDeleteNode(EXECUTOR_INDEX, node.get()) != API_OK)
            {
                --actualDeletedCount;
            }
            // Ignore errors for folders that may have been deleted with parents
        }

        // Remove from tracking (from the end)
        if (count > 0 && totalFolders >= actualDeletedCount && actualDeletedCount > 0)
        {
            mCreatedFolderHandles.resize(totalFolders - actualDeletedCount);
        }

        LOG_info << "Phase 10.4 completed";
    }

    /**
     * @brief Phase 11: Acknowledge user alerts (1 operation)
     */
    void phase11_AcknowledgeAlerts()
    {
        LOG_info << "Phase 11: Acknowledging user alerts...";

        (void)doAckUserAlerts(EXECUTOR_INDEX);
        // May have no alerts, ignore errors

        LOG_info << "Phase 11 completed";
    }

    /**
     * @brief Download test file from artifactory and upload it to the test root folder
     */
    void downloadAndUploadTestFile()
    {
        // Step 1: Download test file from artifactory
        const std::string testFileName = TEST_FILE_NAME;
        ASSERT_TRUE(getFileFromArtifactory("test-data/invalid.png", testFileName.c_str()));

        const MrProper defer{[testFileName, this]()
                             {
                                 deleteFile(testFileName.c_str());
                             }};

        // Step 2: Upload to the test root folder
        std::unique_ptr<MegaNode> testRootNode(
            megaApi[EXECUTOR_INDEX]->getNodeByHandle(mTestRootHandle));
        ASSERT_NE(testRootNode, nullptr) << "Test root folder not found";

        TransferTracker uploadTracker(megaApi[EXECUTOR_INDEX].get());
        MegaUploadOptions uploadOptions;
        uploadOptions.mtime = ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME;
        megaApi[EXECUTOR_INDEX]->startUpload(testFileName,
                                             testRootNode.get(),
                                             nullptr,
                                             &uploadOptions,
                                             &uploadTracker);

        ASSERT_EQ(API_OK, uploadTracker.waitForResult())
            << "Failed to upload " << testFileName << " to test root folder";
    }

    /**
     * @brief Copy massive test files in the test root folder
     */
    void copyTestFiles(MegaHandle dstHandle)
    {
        LOG_info << "Copying test files in parallel...";

        std::unique_ptr<MegaNode> testRootNode(
            megaApi[EXECUTOR_INDEX]->getNodeByHandle(mTestRootHandle));
        ASSERT_NE(testRootNode, nullptr) << "Test root folder not found";

        std::unique_ptr<MegaNode> testFileNode(
            megaApi[EXECUTOR_INDEX]->getChildNode(testRootNode.get(), TEST_FILE_NAME));
        ASSERT_NE(testFileNode, nullptr) << TEST_FILE_NAME << " not found in test root folder";

        std::unique_ptr<MegaNode> dstNode(megaApi[EXECUTOR_INDEX]->getNodeByHandle(dstHandle));
        ASSERT_NE(dstNode, nullptr) << "Test destination folder not found";

        // Fire all copy requests in parallel
        struct CopyRequest
        {
            std::string name;
            std::unique_ptr<RequestTracker> tracker;
        };

        std::vector<CopyRequest> requests;
        requests.reserve(NUM_COPY_FILES);

        for (int i = 0; i < NUM_COPY_FILES; ++i)
        {
            char copyName[16];
            snprintf(copyName, sizeof(copyName), "test%03d.png", i);

            auto tracker = RequestTracker::async(
                *megaApi[EXECUTOR_INDEX],
                static_cast<void (
                    MegaApi::*)(MegaNode*, MegaNode*, const char*, MegaRequestListener*)>(
                    &MegaApi::copyNode),
                testFileNode.get(),
                dstNode.get(),
                copyName);

            requests.push_back({copyName, std::move(tracker)});
        }

        // Wait for all copy requests to complete
        for (auto& req: requests)
        {
            int err = req.tracker->waitForResult();
            ASSERT_EQ(err, API_OK) << "Failed to copy " << TEST_FILE_NAME << " to " << req.name;

            MegaHandle copiedHandle = req.tracker->getNodeHandle();
            if (copiedHandle != INVALID_HANDLE)
            {
                mCreatedFileHandles.push_back(copiedHandle);
            }
        }

        LOG_info << "Successfully created massive copies of " << TEST_FILE_NAME << " in parallel";
    }

    /**
     * @brief Pause receiver clients by saving session and logging out locally
     */
    void pauseReceiverClients()
    {
        LOG_info << "Pausing receiver clients B and C...";

        pauseClient(RECEIVER1_INDEX);
        pauseClient(RECEIVER2_INDEX);

        LOG_info << "Receiver clients paused";
    }

    /**
     * @brief Resume receiver clients to receive accumulated action packets
     */
    void resumeReceiverClients()
    {
        LOG_info << "Resuming receiver clients B and C...";

        resumeClient(RECEIVER1_INDEX);
        resumeClient(RECEIVER2_INDEX);

        LOG_info << "Receiver clients resumed and fetched nodes";
    }

    /**
     * @brief Wait for all clients to be synchronized
     */
    void waitForClientsSynchronized(const std::vector<unsigned int>& apiIndexes = {EXECUTOR_INDEX,
                                                                                   RECEIVER1_INDEX,
                                                                                   RECEIVER2_INDEX})
    {
        LOG_info << "Waiting for all clients to synchronize...";

        // Use catchup to ensure all clients are current
        auto catchup = [this](unsigned apiIndex)
        {
            RequestTracker rt(megaApi[apiIndex].get());
            megaApi[apiIndex]->catchup(&rt);
            return rt.waitForResult(AP_WAIT_TIMEOUT_SECONDS) == API_OK;
        };

        for (auto& apiIndex: apiIndexes)
        {
            ASSERT_TRUE(catchup(apiIndex)) << "Client " << apiIndex << " catchup failed";
        }

        LOG_info << "All clients synchronized";
    }

    /**
     * @brief Compare node trees between two clients
     * @return true if trees match
     */
    bool compareNodeTrees(unsigned apiIndex1, unsigned apiIndex2, MegaHandle rootHandle)
    {
        std::unique_ptr<MegaNode> node1(megaApi[apiIndex1]->getNodeByHandle(rootHandle));
        std::unique_ptr<MegaNode> node2(megaApi[apiIndex2]->getNodeByHandle(rootHandle));

        if (!node1 && !node2)
            return true; // Both don't exist, OK
        if (!node1 || !node2)
            return false; // One exists, one doesn't

        // Compare basic properties
        if (std::string(node1->getName()) != std::string(node2->getName()))
        {
            LOG_err << "Name mismatch: " << node1->getName() << " vs " << node2->getName();
            return false;
        }

        if (node1->getType() != node2->getType())
        {
            LOG_err << "Type mismatch for " << node1->getName();
            return false;
        }

        if (node1->isFile() && node1->getSize() != node2->getSize())
        {
            LOG_err << "Size mismatch for " << node1->getName();
            return false;
        }

        // Compare children for folders
        if (node1->isFolder())
        {
            std::unique_ptr<MegaNodeList> children1(megaApi[apiIndex1]->getChildren(node1.get()));
            std::unique_ptr<MegaNodeList> children2(megaApi[apiIndex2]->getChildren(node2.get()));

            if (children1->size() != children2->size())
            {
                LOG_err << "Children count mismatch for " << node1->getName() << ": "
                        << children1->size() << " vs " << children2->size();
                return false;
            }

            // Build maps for comparison
            std::map<std::string, MegaHandle> childMap1, childMap2;
            for (int i = 0; i < children1->size(); ++i)
            {
                childMap1[children1->get(i)->getName()] = children1->get(i)->getHandle();
            }
            for (int i = 0; i < children2->size(); ++i)
            {
                childMap2[children2->get(i)->getName()] = children2->get(i)->getHandle();
            }

            for (const auto& [name, handle1]: childMap1)
            {
                auto it = childMap2.find(name);
                if (it == childMap2.end())
                {
                    LOG_err << "Child " << name << " missing in second client";
                    return false;
                }

                if (!compareNodeTrees(apiIndex1, apiIndex2, handle1))
                {
                    return false;
                }
            }
        }

        return true;
    }

    /**
     * @brief Compare Sets between two clients
     */
    bool compareSets(unsigned apiIndex1, unsigned apiIndex2)
    {
        std::unique_ptr<MegaSetList> sets1(megaApi[apiIndex1]->getSets());
        std::unique_ptr<MegaSetList> sets2(megaApi[apiIndex2]->getSets());

        if (!sets1 && !sets2)
            return true;
        if (!sets1 || !sets2)
            return false;

        if (sets1->size() != sets2->size())
        {
            LOG_err << "Set count mismatch: " << sets1->size() << " vs " << sets2->size();
            return false;
        }

        // Build maps for comparison
        std::map<MegaHandle, std::string> setMap1, setMap2;
        for (unsigned i = 0; i < sets1->size(); ++i)
        {
            setMap1[sets1->get(i)->id()] = sets1->get(i)->name() ? sets1->get(i)->name() : "";
        }
        for (unsigned i = 0; i < sets2->size(); ++i)
        {
            setMap2[sets2->get(i)->id()] = sets2->get(i)->name() ? sets2->get(i)->name() : "";
        }

        for (const auto& [id, name1]: setMap1)
        {
            auto it = setMap2.find(id);
            if (it == setMap2.end())
            {
                LOG_err << "Set " << toHandle(id) << " missing in second client";
                return false;
            }
            if (it->second != name1)
            {
                LOG_err << "Set name mismatch for " << toHandle(id) << ": " << name1 << " vs "
                        << it->second;
                return false;
            }
        }

        return true;
    }

    /**
     * @brief Verify consistency between clients
     */
    void verifyClientConsistency()
    {
        LOG_info << "Verifying consistency between clients...";

        // Get root node handles
        std::unique_ptr<MegaNode> rootA(megaApi[EXECUTOR_INDEX]->getRootNode());
        ASSERT_NE(rootA, nullptr);

        // 1. Verify B and C are consistent
        LOG_info << "Checking consistency between Client B and Client C...";
        compareClients(RECEIVER1_INDEX, RECEIVER2_INDEX, rootA->getHandle());
        LOG_info << "Client B and Client C are consistent";

        // 2. Verify A and B are consistent
        LOG_info << "Checking consistency between Client A and Client B...";
        compareClients(EXECUTOR_INDEX, RECEIVER1_INDEX, rootA->getHandle());
        LOG_info << "Client A and Client B are consistent";

        LOG_info << "All consistency checks passed!";
    }

    void compareClients(unsigned apiIndex1, unsigned apiIndex2, MegaHandle rootHandle)
    {
        ASSERT_TRUE(compareNodeTrees(apiIndex1, apiIndex2, rootHandle))
            << "Node tree mismatch between Client " << apiIndex1 << " and Client " << apiIndex2;
        ASSERT_TRUE(compareSets(apiIndex1, apiIndex2))
            << "Sets mismatch between Client " << apiIndex1 << " and Client " << apiIndex2;
        LOG_info << "Client " << apiIndex1 << " and Client " << apiIndex2 << " are consistent";
    }

    /**
     * @brief Get statistics about created resources
     */
    void logStatistics()
    {
        LOG_info << "=== Test Statistics ===\n"
                 << "Folders created: " << mCreatedFolderHandles.size()
                 << ", Files created: " << mCreatedFileHandles.size()
                 << ", Sets created: " << mCreatedSetHandles.size()
                 << ", Set elements created: " << mCreatedSetElements.size()
                 << ", Exported nodes: " << mExportedNodeHandles.size()
                 << ", Shared folders: " << mSharedFolderHandles.size();
    }

    /**
     * @brief Pause client
     */
    void pauseClient(unsigned int apiIndex)
    {
        // Save sessions
        mSessionClients[apiIndex].reset(dumpSession(apiIndex));
        ASSERT_NE(mSessionClients[apiIndex], nullptr);
        // Local logout to stop receiving action packets
        ASSERT_EQ(doRequestLocalLogout(apiIndex), API_OK);
    }

    /**
     * @brief Resume client
     */
    void resumeClient(unsigned int apiIndex)
    {
        ASSERT_NE(mSessionClients[apiIndex], nullptr);
        ASSERT_EQ(synchronousFastLogin(apiIndex, mSessionClients[apiIndex].get()), API_OK);
        fetchnodes(apiIndex, AP_WAIT_TIMEOUT_SECONDS);
    }

    /**
     * @brief Copy massive test files for massive action packets generation
     */
    void copyMassiveTestFiles(const std::string& logPre, MegaHandle dstHandle)
    {
        LOG_info
            << logPre
            << "Downloading test file from artifactory and uploading it to the test root folder";
        ASSERT_NO_FATAL_FAILURE(downloadAndUploadTestFile());

        LOG_info << logPre << "Copying test files";
        ASSERT_NO_FATAL_FAILURE(copyTestFiles(dstHandle));
    }

    /**
     * @brief Delay for random time
     */
    void randomDelay(const std::string& logPre)
    {
        // Random delay to fail AP processing at different points
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(0, 5);
        auto delayMs = dist(gen);
        LOG_info << logPre << "Delaying " << delayMs << "ms before injecting failure...";
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }

    /**
     * @brief Execute network drop test
     */
    void networkDropTest(const std::string& logPre, unsigned int apiIndex)
    {
        // Use interceptSCRequest to detect when the SC channel receives an AP.
        // When the hook fires with actual AP data (JSON object), it signals the network drop
        // thread.
        std::promise<void> actionPacketStart;
        std::future<void> actionPacketStartFuture = actionPacketStart.get_future();

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
        auto origInterceptSCRequest = globalMegaTestHooks.interceptSCRequest;
        globalMegaTestHooks.interceptSCRequest =
            [&actionPacketStart, &origInterceptSCRequest](std::unique_ptr<HttpReq>& pendingsc)
        {
            if (origInterceptSCRequest)
            {
                origInterceptSCRequest(pendingsc);
            }

            if (*pendingsc->in.c_str() == '{')
            {
                // Signal the network drop thread on the first SC response with AP data
                actionPacketStart.set_value();

                // Restore the hook
                globalMegaTestHooks.interceptSCRequest = origInterceptSCRequest;
            }
        };
#endif

        // Launch a background thread that waits until a action packet is being processed,
        // then simulates a network drop.
        std::thread networkDropThread(
            [&, this]()
            {
                LOG_info << logPre << "Network drop thread: waiting for action packets start...";
                actionPacketStartFuture.wait();

                randomDelay(logPre);

                LOG_info << logPre << "Simulating network drop on RECEIVER1";
                megaApi[apiIndex]->retryPendingConnections(true);
            });

        // Clear previous EVENT_NODES_CURRENT before triggering SC channel,
        // so we can detect when action packets are fully up to date after recovery.
        mApi[apiIndex].resetlastEvent();

        LOG_info << logPre << "Resuming receiver client...";
        resumeClient(apiIndex);

        networkDropThread.join();

        LOG_info << logPre << "Waiting for Client " << apiIndex
                 << " action packets to be up to date...";
        ASSERT_TRUE(WaitFor(
            [&, this]()
            {
                return mApi[apiIndex].lastEventsContain(MegaEvent::EVENT_NODES_CURRENT);
            },
            AP_WAIT_TIMEOUT_SECONDS * 1000))
            << "Timeout expired waiting for Client " << apiIndex
            << " action packets to be up to date";
        LOG_info << logPre << "Client " << apiIndex << " action packets are up to date";
    }

    /**
     * @brief Execute network block test
     */
    void networkBlockTest([[maybe_unused]] MegaClient* client,
                          const std::string& logPre,
                          unsigned int apiIndex)
    {
        std::promise<void> actionPacketStart;
        std::future<void> actionPacketStartFuture = actionPacketStart.get_future();
        std::atomic<bool> actionPacketStartSignaled{false};

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
        auto origInterceptSCRequest = globalMegaTestHooks.interceptSCRequest;
        globalMegaTestHooks.interceptSCRequest = [&](std::unique_ptr<HttpReq>& pendingsc)
        {
            if (origInterceptSCRequest)
            {
                origInterceptSCRequest(pendingsc);
            }

            if (*pendingsc->in.c_str() == '{')
            {
                client->megaTestHooks.interceptSCChunk = nullptr;

                if (!actionPacketStartSignaled.exchange(true))
                {
                    // Signal that AP processing has started
                    actionPacketStart.set_value();
                }
            }
        };
#endif

        // Launch a background thread that waits until AP processing starts,
        // then keeps the network blocked until AP timeout, and finally unblocks.
        std::thread networkBlockThread(
            [&]()
            {
                LOG_info << logPre << "Network block thread: waiting for action packets start...";
                actionPacketStartFuture.wait();

                randomDelay(logPre);

                // Block the network
                LOG_info << logPre << "Network block thread: network is now blocked";

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
                client->megaTestHooks.interceptSCChunk = [](std::unique_ptr<HttpReq>& pendingsc)
                {
                    // Clear SC response data to simulate network block. The SDK will see empty
                    // responses and eventually time out.
                    LOG_info << "Network block interceptor: clearing SC response data";
                    pendingsc->status = REQ_INFLIGHT;
                    pendingsc->notifiedbufpos = pendingsc->bufpos;
                };
#endif
            });

        // Clear previous EVENT_NODES_CURRENT before triggering SC channel
        mApi[apiIndex].resetlastEvent();

        LOG_info << logPre << "Resuming receiver client...";
        resumeClient(apiIndex);

        networkBlockThread.join();

        LOG_info << logPre << "Waiting for Client " << apiIndex
                 << " action packets to be up to date...";
        ASSERT_TRUE(WaitFor(
            [&, this]()
            {
                return mApi[apiIndex].lastEventsContain(MegaEvent::EVENT_NODES_CURRENT);
            },
            AP_WAIT_TIMEOUT_SECONDS * 1000))
            << "Timeout expired waiting for Client " << apiIndex
            << " action packets to be up to date";
        LOG_info << logPre << "Client " << apiIndex << " action packets are up to date";

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
        globalMegaTestHooks.interceptSCRequest = origInterceptSCRequest;
        client->megaTestHooks.interceptSCChunk = nullptr;
#endif
    }

    /**
     * @brief Execute network failure test
     *
     * Simulates a network failure (REQ_FAILURE) while the streaming receiver is processing
     * action packets. After detecting that AP data has started arriving, a random delay is
     * applied, then the hook forces pendingsc->status to REQ_FAILURE. The SDK should handle
     * this by resetting pendingsc, clearing the streaming parser, backing off, and retrying.
     * Eventually all action packets should be processed correctly.
     */
    void networkFailureTest([[maybe_unused]] MegaClient* client,
                            const std::string& logPre,
                            unsigned int apiIndex)
    {
        std::promise<void> actionPacketStart;
        std::future<void> actionPacketStartFuture = actionPacketStart.get_future();
        std::atomic<bool> actionPacketStartSignaled{false};

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
        auto origInterceptSCRequest = globalMegaTestHooks.interceptSCRequest;
        globalMegaTestHooks.interceptSCRequest = [&](std::unique_ptr<HttpReq>& pendingsc)
        {
            if (origInterceptSCRequest)
            {
                origInterceptSCRequest(pendingsc);
            }

            if (*pendingsc->in.c_str() == '{')
            {
                if (!actionPacketStartSignaled.exchange(true))
                {
                    // Signal that AP processing has started
                    actionPacketStart.set_value();
                }
            }
        };
#endif

        // Launch a background thread that waits until AP processing starts,
        // then injects a network failure after a random delay.
        std::thread networkFailureThread(
            [&]()
            {
                LOG_info << logPre << "Network failure thread: waiting for action packets start...";
                actionPacketStartFuture.wait();

                randomDelay(logPre);

                // Inject network failure
                LOG_info << logPre << "Network failure thread: injecting REQ_FAILURE";

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
                client->megaTestHooks.interceptSCChunk =
                    [client](std::unique_ptr<HttpReq>& pendingsc)
                {
                    LOG_info << "Network failure interceptor: forcing REQ_FAILURE";
                    pendingsc->status = REQ_FAILURE;
                    pendingsc->httpstatus = 0;
                    // Clear the hook after firing once so the SDK can recover on retry
                    client->megaTestHooks.interceptSCChunk = nullptr;
                };
#endif
            });

        // Clear previous EVENT_NODES_CURRENT before triggering SC channel
        mApi[apiIndex].resetlastEvent();

        LOG_info << logPre << "Resuming receiver client...";
        resumeClient(apiIndex);

        networkFailureThread.join();

        LOG_info << logPre << "Waiting for Client " << apiIndex
                 << " action packets to be up to date...";
        ASSERT_TRUE(WaitFor(
            [&, this]()
            {
                return mApi[apiIndex].lastEventsContain(MegaEvent::EVENT_NODES_CURRENT);
            },
            AP_WAIT_TIMEOUT_SECONDS * 1000))
            << "Timeout expired waiting for Client " << apiIndex
            << " action packets to be up to date";
        LOG_info << logPre << "Client " << apiIndex << " action packets are up to date";

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
        globalMegaTestHooks.interceptSCRequest = origInterceptSCRequest;
        client->megaTestHooks.interceptSCChunk = nullptr;
#endif
    }

    /**
     * @brief Execute catchup test
     */
    void catchupTest(const std::string& logPre, unsigned int apiIndex)
    {
        std::promise<void> actionPacketStart;
        std::future<void> actionPacketStartFuture = actionPacketStart.get_future();

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
        auto origInterceptSCRequest = globalMegaTestHooks.interceptSCRequest;
        globalMegaTestHooks.interceptSCRequest =
            [&actionPacketStart, &origInterceptSCRequest](std::unique_ptr<HttpReq>& pendingsc)
        {
            if (origInterceptSCRequest)
            {
                origInterceptSCRequest(pendingsc);
            }

            if (*pendingsc->in.c_str() == '{')
            {
                // Signal the catchup thread on the first SC response with AP data
                actionPacketStart.set_value();

                // Restore the hook
                globalMegaTestHooks.interceptSCRequest = origInterceptSCRequest;
            }
        };
#endif

        // Launch a background thread that waits until an action packet is being processed,
        // then triggers a catchup.
        std::thread catchupThread(
            [&, this]()
            {
                LOG_info << logPre << "Catchup thread: waiting for action packets start...";
                actionPacketStartFuture.wait();

                randomDelay(logPre);

                LOG_info << logPre << "Triggering catchup on " << apiIndex
                         << " during AP processing";
                RequestTracker rt(megaApi[apiIndex].get());
                megaApi[apiIndex]->catchup(&rt);
                auto err = rt.waitForResult(AP_WAIT_TIMEOUT_SECONDS);
                LOG_info << logPre << "Catchup on " << apiIndex
                         << " completed with result: " << err;
            });

        // Clear previous EVENT_NODES_CURRENT before triggering SC channel,
        // so we can detect when action packets are fully up to date after recovery.
        mApi[apiIndex].resetlastEvent();

        LOG_info << logPre << "Resuming receiver client...";
        resumeClient(apiIndex);

        catchupThread.join();

        LOG_info << logPre << "Waiting for Client " << apiIndex
                 << " action packets to be up to date...";
        ASSERT_TRUE(WaitFor(
            [&, this]()
            {
                return mApi[apiIndex].lastEventsContain(MegaEvent::EVENT_NODES_CURRENT);
            },
            AP_WAIT_TIMEOUT_SECONDS * 1000))
            << "Timeout expired waiting for Client " << apiIndex
            << " action packets to be up to date";
        LOG_info << logPre << "Client " << apiIndex << " action packets are up to date";
    }

    /**
     * @brief Execute incompleted packet test
     */
    void incompletedPacketTest([[maybe_unused]] MegaClient* client,
                               const std::string& logPre,
                               unsigned int apiIndex)
    {
        // Install hook to remove the second half of the first SC response with AP data,
        // making the JSON incomplete while pendingsc status remains REQ_SUCCESS.
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
        client->megaTestHooks.interceptSCChunk = [client](std::unique_ptr<HttpReq>& pendingsc)
        {
            if (pendingsc->status == REQ_SUCCESS && !pendingsc->in.empty())
            {
                // Remove the second half of the response to make the JSON incomplete
                // while keeping the status as REQ_SUCCESS. This simulates a scenario
                // where the HTTP response is marked as successful but the payload is
                // cut short.
                const size_t halfSize = pendingsc->in.size() / 2;
                LOG_info << "Incompleted packet interceptor: removing second half from SC "
                            "response ("
                         << pendingsc->in.size() << " -> " << halfSize << " bytes)";
                pendingsc->in.resize(halfSize);
                // Clear the hook after firing once
                client->megaTestHooks.interceptSCChunk = nullptr;
            }
        };
#endif

        // Clear previous EVENT_NODES_CURRENT before triggering SC channel
        mApi[apiIndex].resetlastEvent();

        LOG_info << logPre << "Resuming receiver client...";
        resumeClient(apiIndex);

        LOG_info << logPre << "Waiting for Client " << apiIndex
                 << " action packets to be up to date...";
        ASSERT_TRUE(WaitFor(
            [&, this]()
            {
                return mApi[apiIndex].lastEventsContain(MegaEvent::EVENT_NODES_CURRENT);
            },
            AP_WAIT_TIMEOUT_SECONDS * 1000))
            << "Timeout expired waiting for Client " << apiIndex
            << " action packets to be up to date";
        LOG_info << logPre << "Client " << apiIndex << " action packets are up to date";
    }

    /**
     * @brief Post processing for network drop test
     */
    void verifyClientsConsistency(const std::string& logPre,
                                  const std::vector<unsigned int>& apiIndexes)
    {
        LOG_info << logPre << "Verifying consistency between clients...";
        std::unique_ptr<MegaNode> rootA(megaApi[EXECUTOR_INDEX]->getRootNode());
        ASSERT_NE(rootA, nullptr);

        for (auto& apiIndex: apiIndexes)
        {
            compareClients(EXECUTOR_INDEX, apiIndex, rootA->getHandle());
        }

        LOG_info << logPre << "All consistency checks passed!";
    }
};

/**
 * @brief Main test: Massive Action Packets Processing
 *
 * This test generates approximately 1000+ action packets through various operations
 * and verifies that all clients correctly receive and process them.
 * It's DISABLED because it would cost more than 20 minutes to complete.
 */
TEST_F(SdkTestMassiveActionPackets, DISABLED_MassiveActionPacketsProcessing)
{
    const std::string logPre = getLogPrefix();
    LOG_info << logPre << "=== Starting Massive Action Packets Test ===";

    // Verify all clients are logged in
    ASSERT_TRUE(megaApi[EXECUTOR_INDEX]->isLoggedIn()) << "Client A not logged in";
    ASSERT_TRUE(megaApi[RECEIVER1_INDEX]->isLoggedIn()) << "Client B not logged in";
    ASSERT_TRUE(megaApi[RECEIVER2_INDEX]->isLoggedIn()) << "Client C not logged in";

    LOG_info << logPre << "All 3 clients logged in successfully";

    // Create test root folder before pausing receivers
    ASSERT_NO_FATAL_FAILURE(createTestRootFolder());

    // Pause receiver clients to accumulate action packets
    LOG_info << logPre << "Pausing receiver clients to accumulate APs...";
    ASSERT_NO_FATAL_FAILURE(pauseReceiverClients());

    // ========== Execute all phases to generate action packets ==========
    LOG_info << logPre << "=== Phase 1: Creating folder structure ===";
    ASSERT_NO_FATAL_FAILURE(phase1_1_CreateLevel1Folders());
    ASSERT_NO_FATAL_FAILURE(phase1_2_CreateLevel2Folders());
    ASSERT_NO_FATAL_FAILURE(phase1_3_CreateLevel3Folders());

    LOG_info << logPre << "=== Phase 2: Uploading files ===";
    ASSERT_NO_FATAL_FAILURE(phase2_1_UploadFiles());

    LOG_info << logPre << "=== Phase 3: Setting node attributes ===";
    ASSERT_NO_FATAL_FAILURE(phase3_1_SetTags());
    ASSERT_NO_FATAL_FAILURE(phase3_2_SetFavorites());
    ASSERT_NO_FATAL_FAILURE(phase3_3_SetSensitive());
    ASSERT_NO_FATAL_FAILURE(phase3_4_SetDescriptions());

    LOG_info << logPre << "=== Phase 4: Sharing and exporting ===";
    ASSERT_NO_FATAL_FAILURE(phase4_1_CreateShares());
    ASSERT_NO_FATAL_FAILURE(phase4_2_ExportLinks());
    ASSERT_NO_FATAL_FAILURE(phase4_3_DisableLinks());

    LOG_info << logPre << "=== Phase 5: Moving and copying ===";
    ASSERT_NO_FATAL_FAILURE(phase5_1_MoveFiles());
    ASSERT_NO_FATAL_FAILURE(phase5_2_CopyFiles());

    LOG_info << logPre << "=== Phase 6: Renaming ===";
    ASSERT_NO_FATAL_FAILURE(phase6_1_RenameFolders());
    ASSERT_NO_FATAL_FAILURE(phase6_2_RenameFiles());

    LOG_info << logPre << "=== Phase 7: Sets operations ===";
    ASSERT_NO_FATAL_FAILURE(phase7_1_CreateSets());
    ASSERT_NO_FATAL_FAILURE(phase7_2_AddElements());
    ASSERT_NO_FATAL_FAILURE(phase7_3_UpdateElementNames());
    ASSERT_NO_FATAL_FAILURE(phase7_4_UpdateElementOrders());
    ASSERT_NO_FATAL_FAILURE(phase7_5_ExportSets());
    ASSERT_NO_FATAL_FAILURE(phase7_6_UpdateSetNames());

    LOG_info << logPre << "=== Phase 8: User attributes ===";
    ASSERT_NO_FATAL_FAILURE(phase8_1_SetUserAttributes());
    ASSERT_NO_FATAL_FAILURE(phase8_2_SetAvatar());
    ASSERT_NO_FATAL_FAILURE(phase8_3_UpdateUserAttributes());

    LOG_info << logPre << "=== Phase 9: Contact invitations ===";
    ASSERT_NO_FATAL_FAILURE(phase9_1_SendContactInvitations());

    LOG_info << logPre << "=== Phase 10: Delete operations ===";
    ASSERT_NO_FATAL_FAILURE(phase10_1_DeleteFiles());
    ASSERT_NO_FATAL_FAILURE(phase10_2_DeleteSetElements());
    ASSERT_NO_FATAL_FAILURE(phase10_3_DeleteSets());
    ASSERT_NO_FATAL_FAILURE(phase10_4_DeleteFolders());

    LOG_info << logPre << "=== Phase 11: Acknowledge alerts ===";
    ASSERT_NO_FATAL_FAILURE(phase11_AcknowledgeAlerts());

    // Log statistics
    logStatistics();

    // ========== Resume receivers and verify ==========
    LOG_info << logPre << "All operations completed. Resuming receiver clients...";
    ASSERT_NO_FATAL_FAILURE(resumeReceiverClients());

    LOG_info << logPre << "Waiting for clients to synchronize...";
    ASSERT_NO_FATAL_FAILURE(waitForClientsSynchronized());

    LOG_info << logPre << "Verifying client consistency...";
    ASSERT_NO_FATAL_FAILURE(verifyClientConsistency());

    LOG_info << logPre << "=== Massive Action Packets Test PASSED ===";
}

/**
 * @brief Test: Incremental Action Packets Processing (without pausing)
 *
 * This test verifies that clients can correctly process action packets
 * incrementally as they are generated (server sends them immediately).
 * It's DISABLED because it would cost more than 15 minutes to complete.
 */
TEST_F(SdkTestMassiveActionPackets, DISABLED_IncrementalActionPacketsProcessing)
{
    const std::string logPre = getLogPrefix();
    LOG_info << logPre << "=== Starting Incremental Action Packets Test ===";

    // Verify all clients are logged in
    ASSERT_TRUE(megaApi[EXECUTOR_INDEX]->isLoggedIn()) << "Client A not logged in";
    ASSERT_TRUE(megaApi[RECEIVER1_INDEX]->isLoggedIn()) << "Client B not logged in";
    ASSERT_TRUE(megaApi[RECEIVER2_INDEX]->isLoggedIn()) << "Client C not logged in";

    LOG_info << logPre << "All 3 clients logged in successfully";

    // Create test root folder
    ASSERT_NO_FATAL_FAILURE(createTestRootFolder());

    // Execute operations WITHOUT pausing receivers
    // This tests incremental AP processing
    LOG_info << logPre << "=== Phase 1: Creating folder structure (incremental) ===";
    ASSERT_NO_FATAL_FAILURE(phase1_1_CreateLevel1Folders());
    ASSERT_NO_FATAL_FAILURE(phase1_2_CreateLevel2Folders());
    ASSERT_NO_FATAL_FAILURE(phase1_3_CreateLevel3Folders());

    LOG_info << logPre << "=== Phase 2: Uploading files (incremental) ===";
    ASSERT_NO_FATAL_FAILURE(phase2_1_UploadFiles());

    LOG_info << logPre << "=== Phase 3: Setting node attributes (incremental) ===";
    ASSERT_NO_FATAL_FAILURE(phase3_1_SetTags());
    ASSERT_NO_FATAL_FAILURE(phase3_2_SetFavorites());

    LOG_info << logPre << "=== Phase 7: Sets operations (incremental) ===";
    ASSERT_NO_FATAL_FAILURE(phase7_1_CreateSets());
    ASSERT_NO_FATAL_FAILURE(phase7_2_AddElements());

    // Log statistics
    logStatistics();

    // Wait for synchronization
    LOG_info << logPre << "Waiting for clients to synchronize...";
    ASSERT_NO_FATAL_FAILURE(waitForClientsSynchronized());

    // Verify consistency
    LOG_info << logPre << "Verifying client consistency...";
    ASSERT_NO_FATAL_FAILURE(verifyClientConsistency());

    LOG_info << logPre << "=== Incremental Action Packets Test PASSED ===";
}

/**
 * @brief Test: Massive Action Packets Processing with Network Drop
 *
 * This test generates approximately 1000+ action packets through node copy operations,
 * then simulates a random network drop while the streaming receiver is
 * processing APs. After the drop, the SDK should reconnect and send an SC POST with
 * the latest ISN (interim sequence number) received before the drop as the sn= parameter.
 */
TEST_F(SdkTestMassiveActionPackets, MassiveActionPacketsProcessingWithNetworkDrop)
{
    CASE_info << "started";

    const std::string logPre = getLogPrefix();

    // Run in action packet streaming parsing mode
    sdk_test::setScParserMode(true);

    // Verify all clients are logged in
    ASSERT_NO_FATAL_FAILURE(verifyClientsLogin(logPre, {EXECUTOR_INDEX, RECEIVER1_INDEX}));

    // Create test root folder before pausing receiver
    ASSERT_NO_FATAL_FAILURE(createTestRootFolder());

    // Pause receiver client to accumulate action packets
    ASSERT_NO_FATAL_FAILURE(pauseClient(RECEIVER1_INDEX));

    // Generate action packets
    ASSERT_NO_FATAL_FAILURE(copyMassiveTestFiles(logPre, mTestRootHandle));

    // Resume client and trigger network drop during AP processing
    ASSERT_NO_FATAL_FAILURE(networkDropTest(logPre, RECEIVER1_INDEX));

    ASSERT_NO_FATAL_FAILURE(verifyClientsConsistency(logPre, {RECEIVER1_INDEX}));

    sdk_test::resetScParserMode();

    CASE_info << "finished";
}

/**
 * @brief Test: Single Large Action Packet Processing withNetworkDrop
 *
 * This test prepares a folder containing massive file copies, then pauses the receiver,
 * copies the entire folder to a new location (generating massive APs from a single
 * copy operation), and simulates a network drop while the receiver processes APs.
 */
TEST_F(SdkTestMassiveActionPackets, SingleLargeActionPacketsProcessingWithNetworkDrop)
{
    CASE_info << "started";

    const std::string logPre = getLogPrefix();

    // Run in action packet streaming parsing mode
    sdk_test::setScParserMode(true);

    // Verify all clients are logged in
    ASSERT_NO_FATAL_FAILURE(verifyClientsLogin(logPre, {EXECUTOR_INDEX, RECEIVER1_INDEX}));

    // Create test root folder before pausing receiver
    ASSERT_NO_FATAL_FAILURE(createTestRootFolder());

    // Create 2 subfolders and make massive copies of the test file inside one of them
    LOG_info << logPre << "Creating source folder with massive file copies";
    std::unique_ptr<MegaNode> testRootNode(
        megaApi[EXECUTOR_INDEX]->getNodeByHandle(mTestRootHandle));
    ASSERT_NE(testRootNode, nullptr);

    MegaHandle srcFolderHandle = createFolder(EXECUTOR_INDEX, "SrcFolder", testRootNode.get());
    ASSERT_NE(srcFolderHandle, INVALID_HANDLE) << "Failed to create SrcFolder";

    MegaHandle dstFolderHandle = createFolder(EXECUTOR_INDEX, "DstFolder", testRootNode.get());
    ASSERT_NE(dstFolderHandle, INVALID_HANDLE) << "Failed to create DstFolder";

    // Prepare massive test files inside SrcFolder
    ASSERT_NO_FATAL_FAILURE(copyMassiveTestFiles(logPre, srcFolderHandle));

    LOG_info << logPre << "Waiting for clients to synchronize...";
    ASSERT_NO_FATAL_FAILURE(waitForClientsSynchronized({EXECUTOR_INDEX, RECEIVER1_INDEX}));

    // Pause receiver client to accumulate action packets
    ASSERT_NO_FATAL_FAILURE(pauseClient(RECEIVER1_INDEX));

    // Generate action packets
    LOG_info << logPre << "Copying source folder to destination (generating massive APs)";

    std::unique_ptr<MegaNode> srcFolderNode(
        megaApi[EXECUTOR_INDEX]->getNodeByHandle(srcFolderHandle));
    ASSERT_NE(srcFolderNode, nullptr);
    std::unique_ptr<MegaNode> dstFolderNode(
        megaApi[EXECUTOR_INDEX]->getNodeByHandle(dstFolderHandle));
    ASSERT_NE(dstFolderNode, nullptr);

    MegaHandle copiedFolderHandle = INVALID_HANDLE;
    int err = doCopyNode(EXECUTOR_INDEX,
                         &copiedFolderHandle,
                         srcFolderNode.get(),
                         dstFolderNode.get(),
                         "SrcFolderCopy");
    ASSERT_EQ(err, API_OK) << "Failed to copy source folder";
    ASSERT_NE(copiedFolderHandle, INVALID_HANDLE);
    LOG_info << logPre << "Folder copy completed";

    // Resume client and trigger network drop during AP processing
    ASSERT_NO_FATAL_FAILURE(networkDropTest(logPre, RECEIVER1_INDEX));

    ASSERT_NO_FATAL_FAILURE(verifyClientsConsistency(logPre, {RECEIVER1_INDEX}));

    sdk_test::resetScParserMode();

    CASE_info << "finished";
}

/**
 * @brief Test: Massive Action Packets Processing with Network Block
 *
 * This test generates massive action packets through node copy operations,
 * then simulates a network block (SC responses are cleared) while the streaming receiver
 * is processing APs. The block is held long enough to trigger the AP timeout. After the
 * timeout, the SDK should reconnect and resume processing with the correct ISN.
 */
TEST_F(SdkTestMassiveActionPackets, MassiveActionPacketsProcessingWithNetworkBlock)
{
    CASE_info << "started";

    const std::string logPre = getLogPrefix();

    // Run in action packet streaming parsing mode
    sdk_test::setScParserMode(true);

    // Verify all clients are logged in
    ASSERT_NO_FATAL_FAILURE(verifyClientsLogin(logPre, {EXECUTOR_INDEX, RECEIVER1_INDEX}));

    // Create test root folder before pausing receiver
    ASSERT_NO_FATAL_FAILURE(createTestRootFolder());

    // Pause receiver client to accumulate action packets
    ASSERT_NO_FATAL_FAILURE(pauseClient(RECEIVER1_INDEX));

    // Generate action packets
    ASSERT_NO_FATAL_FAILURE(copyMassiveTestFiles(logPre, mTestRootHandle));

    // Resume client and trigger network block during AP processing
    ASSERT_NO_FATAL_FAILURE(
        networkBlockTest(megaApi[RECEIVER1_INDEX]->getClient(), logPre, RECEIVER1_INDEX));

    ASSERT_NO_FATAL_FAILURE(verifyClientsConsistency(logPre, {RECEIVER1_INDEX}));

    sdk_test::resetScParserMode();

    CASE_info << "finished";
}

/**
 * @brief Test: Massive Action Packets Processing with Network Failure
 *
 * This test generates massive action packets through node copy operations,
 * then simulates a network failure (REQ_FAILURE) while the streaming receiver
 * is processing APs. The SDK should handle this by resetting the SC connection,
 * backing off, and retrying. After recovery, all action packets should be
 * processed correctly with the right ISN.
 */
TEST_F(SdkTestMassiveActionPackets, MassiveActionPacketsProcessingWithNetworkFailure)
{
    CASE_info << "started";

    const std::string logPre = getLogPrefix();

    // Run in action packet streaming parsing mode
    sdk_test::setScParserMode(true);

    // Verify all clients are logged in
    ASSERT_NO_FATAL_FAILURE(verifyClientsLogin(logPre, {EXECUTOR_INDEX, RECEIVER1_INDEX}));

    // Create test root folder before pausing receiver
    ASSERT_NO_FATAL_FAILURE(createTestRootFolder());

    // Pause receiver client to accumulate action packets
    ASSERT_NO_FATAL_FAILURE(pauseClient(RECEIVER1_INDEX));

    // Generate action packets
    ASSERT_NO_FATAL_FAILURE(copyMassiveTestFiles(logPre, mTestRootHandle));

    // Resume client and trigger network failure during AP processing
    ASSERT_NO_FATAL_FAILURE(
        networkFailureTest(megaApi[RECEIVER1_INDEX]->getClient(), logPre, RECEIVER1_INDEX));

    ASSERT_NO_FATAL_FAILURE(verifyClientsConsistency(logPre, {RECEIVER1_INDEX}));

    sdk_test::resetScParserMode();

    CASE_info << "finished";
}

/**
 * @brief Test: Massive Action Packets Processing and Catchup
 *
 * This test generates massive action packets through node copy operations,
 * then triggers a catchup while the streaming receiver is processing APs.
 * The SDK should handle the concurrent catchup correctly and end up with
 * a consistent state.
 */
TEST_F(SdkTestMassiveActionPackets, MassiveActionPacketsProcessingAndCatchup)
{
    CASE_info << "started";

    const std::string logPre = getLogPrefix();

    // Run in action packet streaming parsing mode
    sdk_test::setScParserMode(true);

    // Verify all clients are logged in
    ASSERT_NO_FATAL_FAILURE(verifyClientsLogin(logPre, {EXECUTOR_INDEX, RECEIVER1_INDEX}));

    // Create test root folder before pausing receiver
    ASSERT_NO_FATAL_FAILURE(createTestRootFolder());

    // Pause receiver client to accumulate action packets
    ASSERT_NO_FATAL_FAILURE(pauseClient(RECEIVER1_INDEX));

    // Generate action packets
    ASSERT_NO_FATAL_FAILURE(copyMassiveTestFiles(logPre, mTestRootHandle));

    ASSERT_NO_FATAL_FAILURE(catchupTest(logPre, RECEIVER1_INDEX));

    ASSERT_NO_FATAL_FAILURE(verifyClientsConsistency(logPre, {RECEIVER1_INDEX}));

    sdk_test::resetScParserMode();

    CASE_info << "finished";
}

/**
 * @brief Test: Massive Action Packets Processing with Incompleted Packet
 *
 * This test generates massive action packets through node copy operations,
 * then simulates receiving an incomplete JSON packet while the HTTP status
 * indicates success (REQ_SUCCESS). The SDK should detect the malformed
 * JSON and log, as incomplete action packets with successful HTTP status
 * indicate a data integrity issue that must not be silently ignored.
 */
TEST_F(SdkTestMassiveActionPackets, MassiveActionPacketsProcessingWithIncompletedPacket)
{
    CASE_info << "started";

    const std::string logPre = getLogPrefix();

    // Run in action packet streaming parsing mode
    sdk_test::setScParserMode(true);

    // Verify all clients are logged in
    ASSERT_NO_FATAL_FAILURE(verifyClientsLogin(logPre, {EXECUTOR_INDEX, RECEIVER1_INDEX}));

    // Create test root folder before pausing receiver
    ASSERT_NO_FATAL_FAILURE(createTestRootFolder());

    // Pause receiver client to accumulate action packets
    ASSERT_NO_FATAL_FAILURE(pauseClient(RECEIVER1_INDEX));

    // Generate action packets
    ASSERT_NO_FATAL_FAILURE(copyMassiveTestFiles(logPre, mTestRootHandle));

    // Resume client and inject incompleted packet during AP processing.
    // The SDK should assert when it detects the incomplete JSON.
    ASSERT_NO_FATAL_FAILURE(
        incompletedPacketTest(megaApi[RECEIVER1_INDEX]->getClient(), logPre, RECEIVER1_INDEX));

    sdk_test::resetScParserMode();

    CASE_info << "finished";
}
