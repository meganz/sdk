/**
 * @file
 * @brief This file is expected to contain tests involving sync root paths (local
 * and remote), e.g., what happens when the remote root of a sync gets deleted.
 */

#include <string_view>
#ifdef ENABLE_SYNC

#include "mock_listeners.h"
#include "SdkTestSyncNodesOperations.h"

#include <gmock/gmock.h>

using namespace sdk_test;
using namespace testing;

namespace
{
bool thereIsRenamedNode(const MegaNodeList* const nodes, const std::string_view targetName)
{
    if (!nodes)
        return false;

    const auto nodeIsRenamed = [targetName](auto* const node)
    {
        return node && node->getName() == targetName &&
               node->hasChanged(MegaNode::CHANGE_TYPE_NAME);
    };

    for (auto i = nodes->size(); i--;)
    {
        if (nodeIsRenamed(nodes->get(i)))
        {
            return true;
        }
    }
    return false;
}
} // namespace

/**
 * @class SdkTestSyncRootOperations
 * @brief Test fixture designed to test operations involving sync root local and remote paths.
 */
class SdkTestSyncLocalOperations: public SdkTestSyncNodesOperations
{
public:
    static constexpr std::string_view source{"test.cvj"};
    static constexpr std::string_view target{"test.bak"};

    const std::vector<NodeInfo>& getElements() const override
    {
        // To ensure "testCommonFile" is identical in both dirs
        static const std::vector<NodeInfo> ELEMENTS{
            DirNodeInfo(DEFAULT_SYNC_REMOTE_PATH)
                .addChild(FileNodeInfo(std::string{target}).setSize(1000))
                .addChild(FileNodeInfo(std::string{source}).setSize(900))};
        return ELEMENTS;
    }

    void renameAndCreate() const
    {
        using clock = std::chrono::steady_clock;
        const auto nodeRenamed = [](const MegaNodeList* nodes) -> bool
        {
            return thereIsRenamedNode(nodes, target);
        };
        // Track putnodes complete (move)
        std::promise<clock::time_point> finishedRename;
        NiceMock<MockNodesUpdateListener> mockNodesListener{megaApi[0].get()};
        EXPECT_CALL(mockNodesListener, onNodesUpdate).Times(AnyNumber());
        EXPECT_CALL(mockNodesListener, onNodesUpdate(_, Truly(nodeRenamed)))
            .WillOnce(
                [&finishedRename]
                {
                    finishedRename.set_value(clock::now());
                });

        // Track new test.cvj upload. On first update, wait until move ends
        const auto ExpectedTransfer = Pointer(Property(&MegaTransfer::getFileName, source));
        const auto OkError = Pointer(Property(&MegaError::getErrorCode, API_OK));
        std::promise<clock::time_point> finishedTransfer;
        MockTransferListener mockTransferListener{megaApi[0].get()};
        EXPECT_CALL(mockTransferListener, onTransferStart(_, ExpectedTransfer)).Times(1);
        EXPECT_CALL(mockTransferListener, onTransferUpdate(_, ExpectedTransfer)).Times(AnyNumber());
        EXPECT_CALL(mockTransferListener, onTransferFinish(_, ExpectedTransfer, OkError))
            .WillOnce(
                [&finishedTransfer]
                {
                    finishedTransfer.set_value(clock::now());
                });

        megaApi[0]->addListener(&mockNodesListener);
        megaApi[0]->addListener(&mockTransferListener);

        std::filesystem::rename(localTmpPath() / source, localTmpPath() / target);
        sdk_test::createFile(localTmpPath() / source, 950);

        auto trFut = finishedTransfer.get_future();
        auto rnFut = finishedRename.get_future();
        trFut.wait();
        rnFut.wait();

        megaApi[0]->removeListener(&mockNodesListener);
        megaApi[0]->removeListener(&mockTransferListener);

        // Verify expectations on the mocks. If any expectation failed, this returns false.
        bool nodesOk = ::testing::Mock::VerifyAndClearExpectations(&mockNodesListener);
        bool transferOk = ::testing::Mock::VerifyAndClearExpectations(&mockTransferListener);

        ASSERT_TRUE(nodesOk) << "Expectations on nodes listener failed.";
        ASSERT_TRUE(transferOk) << "Expectations on transfer listener failed.";

        const auto transferFinishTime = trFut.get();
        const auto renameFinishTime = rnFut.get();
        ASSERT_GT(transferFinishTime, renameFinishTime)
            << "Test is invalid, putnodes ended after the transfer finished";

        std::this_thread::sleep_for(5s);
    }
};

/**
 * @brief Renames A to B (B already exists, so it's replaced) and creates a new A. After the move
 * and the transfer finish, repeat the operation.
 *
 * The first time that a move operation takes place, the sync debris folder is not created yet,
 * affecting the sequence of requests sent to the API:
 *    1. The request to move (rename) the node to-be-displaced along with the request will be sent
 * to create the daily SyncDebris.
 *    2. Action packets are received, node to-be-displaced is not yet fully updated as there are now
 * 2 duplicated nodes in the cloud, the renamed one and the old one that still needs to be sent to
 * debris.
 *    3. After receiving the action packets, the request to move to debris the node-to-be-displaced
 * will be sent.
 *    4. Immediately after, the move operation completion will be checked: the row.cloudNode still
 * has the old handle (as it not has yet been moved to debris, that cloudNode is outdated). So the
 * move operation is reset for evaluation.
 *    5. The operation to move the node-to-be-displaced to debris will be finished, but the
 * checkMoves will wait a bit (it considers the file is still changing, as it has stats that it
 * didn't have before).
 *    6. When the checkMoves takes place again, all the move operation in the cloud has been
 * completed, so it doesn't need to start a move operation again.
 *
 * The second time a move operation takes place, the sync debris is created already:
 *    1. The request to move (rename) the node-to-be-displaced will the request to move the
 * node-to-be-displaced to the daily SyncDebris.
 *    2. Action packets are received, updating the current cloud nodes accordingly, and the
 * displaced node with the previous handle does not exist anymore.
 *    3. Immediately after, the move operation completion will be checked: the row.cloudNode has the
 * updated handle.
 *    4. The move operation is completed from the sync engine: it takes all the data from the
 * sourceSyncNode, including the transfer in flight, and marks the row as synced.
 *
 * Expectations are that only one upload transfer (the one to create the new A) is started in each
 * iteration:
 *    1. First iteration, there is a move operation which is cancelled. The upload transfer is never
 * moved to another sync node.
 *    2. Second iteration, the move operation is completed from the sync engine, and the upload
 * transfer is moved to the target sync node (B). The fix must prevent this from happening for this
 * scenario, avoiding a new upload to be started again from the new file A.
 */
TEST_F(SdkTestSyncLocalOperations, RenameAndCreateNew)
{
    static const auto logPre{getLogPrefix()};
    LOG_debug << logPre << "Starting";
    for (const auto i: range(2))
    {
        LOG_debug << logPre << "rename n" << (i + 1);
        ASSERT_NO_FATAL_FAILURE(renameAndCreate())
            << "Unexpected behaviour on the rename & create operation n" << i;
    }
    LOG_debug << logPre << "Finishing";
}
#endif
