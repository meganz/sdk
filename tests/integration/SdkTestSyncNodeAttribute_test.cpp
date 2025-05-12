/**
 * @file SdkTestSyncNodeAttribute.cpp
 * @brief This file is expected to contain SdkTestSyncNodeAttributes class definition and tests.
 */

#ifdef ENABLE_SYNC

#include "integration_test_utils.h"
#include "mega/utils.h"
#include "megautils.h"
#include "sdk_test_utils.h"
#include "SdkTestSyncNodesOperations.h"

#include <gmock/gmock.h>

using namespace sdk_test;
using namespace testing;

class SdkTestSyncNodeAttributes: public SdkTestSyncNodesOperations
{
public:
    const std::vector<NodeInfo>& getElements() const override
    {
        static const std::vector<NodeInfo> ELEMENTS{DirNodeInfo(DEFAULT_SYNC_REMOTE_PATH)
                                                        .addChild(FileNodeInfo("test.txt")
                                                                      .setSize(10)
                                                                      .setFav(true)
                                                                      .setDescription("description")
                                                                      .setTags({"tag1", "tag2"}))};
        return ELEMENTS;
    }

    /**
     * @brief Waits until the test file got sync to cloud - checking if size is same.
     *
     * Asserts false if a timeout is exceeded.
     */
    void waitForFileToSync(const std::string_view fileName)
    {
        const auto areLocalAndCloudSynched = [this, fileName]() -> bool
        {
            const string filePath = "dir1/" + string(fileName);
            const auto syncNode = getNodeByPath(filePath);
            const auto syncNodeSize =
                static_cast<std::uintmax_t>(megaApi[0]->getSize(syncNode.get()));
            return fs::file_size(getLocalTmpDir() / std::string(fileName)) == syncNodeSize;
        };
        ASSERT_TRUE(waitFor(areLocalAndCloudSynched, COMMON_TIMEOUT, 10s));
    }
};

/**
 * @brief SdkTestSyncNodeAttributes.VerifyAttributeAfterSync
 *
 * Check file attributes consistency after sync completes
 */
TEST_F(SdkTestSyncNodeAttributes, VerifyAttributeAfterSync)
{
    static const auto logPre = getLogPrefix();
    const auto remoteFilePath = "/SDK_TEST_SYNC_NODE_OPERATIONS_AUX_DIR/dir1/test.txt";

    LOG_verbose << logPre << "Ensuring sync is running on dir1";
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir1"));

    LOG_verbose << logPre << "Waiting for sync remote and local roots to have the same content";
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());

    LOG_verbose << logPre << "Check if the contents match expectations";
    ASSERT_NO_FATAL_FAILURE(checkCurrentLocalMatchesOriginal("dir1"));

    const auto megaNode = megaApi[0]->getNodeByPath(remoteFilePath, nullptr);
    const auto description = megaNode->getDescription();
    const auto lable = megaNode->getLabel();
    const auto fav = megaNode->isFavourite();
    const auto tags = megaNode->getTags();

    LOG_verbose << logPre << "Update the existing file size by appending extra data locally";
    const auto localTestFilePath = getLocalTmpDir() / "test.txt";
    appendToFile(localTestFilePath, 20);

    LOG_verbose << logPre << "Ensuring sync is running on dir1";
    ASSERT_NO_FATAL_FAILURE(ensureSyncNodeIsRunning("dir1"));

    LOG_verbose << logPre << "Waiting for sync remote and local nodes to have the same size";
    ASSERT_NO_FATAL_FAILURE(waitForFileToSync("test.txt"));

    const auto afterSync = megaApi[0]->getNodeByPath(remoteFilePath, nullptr);
    const auto asDescription =
        afterSync->getDescription() == nullptr ? "" : afterSync->getDescription();
    const auto asLable = afterSync->getLabel();
    const auto asFav = afterSync->isFavourite();
    const auto asTags = afterSync->getTags();

    ASSERT_STREQ(description, asDescription) << "Description attribute mismatched after sync";
    ASSERT_EQ(lable, asLable) << "Lable attribute mismatched after sync";
    ASSERT_EQ(fav, asFav) << "Favourite attribute mismatched after sync";
    ASSERT_EQ(tags->size(), asTags->size()) << "Node tags are not same after sync";
    for (int i = 0; i < tags->size(); i++)
    {
        ASSERT_STREQ(tags->get(i), asTags->get(i)) << "Tag attribute mismatched after sync";
    }
}

#endif