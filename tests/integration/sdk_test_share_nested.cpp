/**
 * @file sdk_test_share_nested.cpp
 * @brief This file defines tests related with nested shares
 */

#include "sdk_test_share.h"
#include "SdkTestNodesSetUp.h"

using namespace sdk_test;

class SdkTestShareNested: public virtual SdkTestShare, public virtual SdkTestNodesSetUp
{
public:
    void SetUp() override
    {
        SdkTestShare::SetUp();
        ASSERT_NO_FATAL_FAILURE(getAccountsForTest(3));
        ASSERT_NO_FATAL_FAILURE(createRootTestDir());
        createNodes(getElements(), getRootTestDirectory());
    }

    const std::string& getRootTestDir() const override
    {
        return rootTestDir;
    }

    const std::vector<sdk_test::NodeInfo>& getElements() const override
    {
        return treeElements;
    }

    // Override, we don't need to have different creation time.
    bool keepDifferentCreationTimes() override
    {
        return false;
    }

    // Create a file node in the remote account.
    // Optionally, it will be validate in apiIndexB acount.
    void createRemoteFileNode(unsigned apiIndexA,
                              const sdk_test::FileNodeInfo& fileInfo,
                              MegaNode* rootnode,
                              std::optional<unsigned> apiIndexB)
    {
        bool checkA{false};
        bool checkB{false};
        mApi[apiIndexA].mOnNodesUpdateCompletion =
            createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, checkA);
        if (apiIndexB)
        {
            mApi[*apiIndexB].mOnNodesUpdateCompletion =
                createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, checkB);
        }
        sdk_test::LocalTempFile localFile{fileInfo.name, fileInfo.size};
        MegaHandle file1Handle = INVALID_HANDLE;
        ASSERT_EQ(MegaError::API_OK,
                  doStartUpload(apiIndexA,
                                &file1Handle,
                                fileInfo.name.c_str(),
                                rootnode,
                                nullptr /*fileName*/,
                                fileInfo.mtime,
                                nullptr /*appData*/,
                                false /*isSourceTemporary*/,
                                false /*startFirst*/,
                                nullptr /*cancelToken*/))
            << "Failure uploading a file";

        ASSERT_TRUE(waitForResponse(&checkA)) << "New node not received on client " << apiIndexA
                                              << " after " << maxTimeout << " seconds";
        if (apiIndexB)
        {
            ASSERT_TRUE(waitForResponse(&checkB))
                << "New node not received on client " << *apiIndexB << " after " << maxTimeout
                << " seconds";
        }
        resetOnNodeUpdateCompletionCBs();
        std::unique_ptr<MegaNode> nodeFile{megaApi[apiIndexA]->getNodeByHandle(file1Handle)};
        ASSERT_NE(nodeFile, nullptr)
            << "Cannot get the node for the updated file (error: " << mApi[apiIndexA].lastError
            << ")";
        setNodeAdditionalAttributes(fileInfo, nodeFile);
    }

    void matchTree(const MegaHandle rootHandle, unsigned apiIndexA, unsigned apiIndexB) const
    {
        const std::unique_ptr<MegaNode> rootNodeA{megaApi[apiIndexA]->getNodeByHandle(rootHandle)};
        const std::unique_ptr<MegaNode> rootNodeB{megaApi[apiIndexB]->getNodeByHandle(rootHandle)};
        ASSERT_TRUE(rootNodeA) << "Node not present the accout #" << apiIndexA
                               << ". Handle: " << toNodeHandle(rootHandle);
        ASSERT_TRUE(rootNodeB) << "Node not present the second accout#" << apiIndexB
                               << ". Handle: " << toNodeHandle(rootHandle);

        ASSERT_NO_FATAL_FAILURE(
            matchTreeRecurse(rootNodeA.get(), rootNodeB.get(), apiIndexA, apiIndexB));
    }

protected:
    // Name of the initial elements in the remote tree
    static constexpr auto FOLDER_A = "folderA";
    static constexpr auto FOLDER_B = "folderB";
    static constexpr auto FOLDER_C = "folderC";
    static constexpr auto FILE_A = "fileA";
    static constexpr auto FILE_B = "fileB";
    static constexpr auto FILE_C = "fileC";

    static constexpr unsigned sharerIndex{0};
    static constexpr unsigned shareeAliceIndex{1};
    static constexpr unsigned shareeBobIndex{2};

private:
    // root in the cloud where the tree is created
    const std::string rootTestDir{"locklessCS"};

    // It represents the following tree:
    // RemoteRoot
    // └── "folderA"
    //     ├── "fileA"
    //     └── "folderB"
    //         ├── "fileB"
    //         └── "folderC"
    //             └── "fileC"
    const std::vector<NodeInfo> treeElements{
        DirNodeInfo(FOLDER_A)
            .addChild(FileNodeInfo(FILE_A).setSize(100))
            .addChild(
                DirNodeInfo(FOLDER_B)
                    .addChild(FileNodeInfo(FILE_B).setSize(100))
                    .addChild(DirNodeInfo(FOLDER_C).addChild(FileNodeInfo(FILE_C).setSize(100))))};

    // Check if the passed nodes have the same handle, if they are are decrypted and if they have
    // the same name. Print meaninful messages depending on the assert.
    void verifySameNodes(MegaNode* nodeA,
                         unsigned apiIndexA,
                         MegaNode* nodeB,
                         unsigned apiIndexB) const
    {
        ASSERT_TRUE(nodeA && nodeB) << "Invalid nodes in the comparision.";
        ASSERT_EQ(nodeA->getHandle(), nodeB->getHandle())
            << "Handles don't match. " << nodeA->getHandle() << " vs " << nodeB->getHandle();
        ASSERT_TRUE(nodeA->isNodeKeyDecrypted() || nodeB->isNodeKeyDecrypted())
            << "Node is not decryptable in both accounts " << apiIndexA << " and " << apiIndexB;
        ASSERT_FALSE(nodeA->isNodeKeyDecrypted() && !nodeB->isNodeKeyDecrypted())
            << "Account " << apiIndexB << " can't decrypt " << nodeA->getName();
        ASSERT_FALSE(!nodeA->isNodeKeyDecrypted() && nodeB->isNodeKeyDecrypted())
            << "Account " << apiIndexA << " can't decrypt " << nodeB->getName();
        ASSERT_STREQ(nodeA->getName(), nodeB->getName())
            << "Node names don't match in in both accounts.";
    };

    // It validates the passed nodes and their descents.
    // The function is called recursively for folders.
    void matchTreeRecurse(MegaNode* rootNodeA,
                          MegaNode* rootNodeB,
                          unsigned apiIndexA,
                          unsigned apiIndexB) const
    {
        ASSERT_NO_FATAL_FAILURE(verifySameNodes(rootNodeA, apiIndexA, rootNodeB, apiIndexB));

        std::unique_ptr<MegaNodeList> childrenListA{megaApi[apiIndexA]->getChildren(rootNodeA)};
        std::unique_ptr<MegaNodeList> childrenListB{megaApi[apiIndexB]->getChildren(rootNodeB)};
        std::unordered_map<MegaHandle, MegaNode*>
            indexB; // Index for childrenListB using the handles.

        for (auto j = 0; j < childrenListB->size(); ++j)
        {
            auto childNodeB{childrenListB->get(j)};
            ASSERT_TRUE(childNodeB) << "null node in the list of childs of " << rootNodeB->getName()
                                    << "in the " << apiIndexB << " account.";
            indexB.emplace(childNodeB->getHandle(), childNodeB);
        }

        for (auto i = 0; i < childrenListA->size(); ++i)
        {
            auto childNodeA = childrenListA->get(i);
            ASSERT_TRUE(childNodeA) << "null node in the list of childs of " << rootNodeA->getName()
                                    << "in the " << apiIndexA << " account.";
            auto itChildNodeB = indexB.find(childNodeA->getHandle());
            ASSERT_NE(itChildNodeB, indexB.end())
                << "Can't find " << childNodeA->getName() << "in the " << apiIndexB << " account";
            auto childNodeB = itChildNodeB->second;
            if (childNodeA->isFolder() && childNodeB->isFolder())
            {
                ASSERT_NO_FATAL_FAILURE(
                    matchTreeRecurse(childNodeA, childNodeB, apiIndexA, apiIndexB));
            }
            else
            {
                ASSERT_NO_FATAL_FAILURE(
                    verifySameNodes(childNodeA, apiIndexA, childNodeB, apiIndexB));
            }
            indexB.erase(itChildNodeB);
        }

        std::string extraNodes;
        for (auto [_, unmatchedChildNodeB]: indexB)
        {
            extraNodes += " " + toNodeHandle(unmatchedChildNodeB->getHandle());
            if (unmatchedChildNodeB->isNodeKeyDecrypted())
                extraNodes += ":" + string(unmatchedChildNodeB->getName());
        }
        ASSERT_EQ(indexB.size(), 0) << "Unexpected " << indexB.size() << "node(s) found in the "
                                    << apiIndexB << " account: " << extraNodes;
    }
};

/**
 * @brief Basic test for nested shares
 *
 * It tests the basic functionality, creating a nested share and ensuring
 * that all peers can see their respective files.
 *
 */
TEST_F(SdkTestShareNested, BasicNestedShares)
{
    const auto logPre = getLogPrefix();

    LOG_info << "Starting body of " << logPre;

    // Make sharer and sharees contacts.
    ASSERT_NO_FATAL_FAILURE(
        inviteTestAccount(sharerIndex, shareeAliceIndex, "Sharer inviting Alice"))
        << "Failure inviting Alice";
    ASSERT_NO_FATAL_FAILURE(inviteTestAccount(sharerIndex, shareeBobIndex, "Sharer inviting Bob"))
        << "Failure inviting Bob";

    if (gManualVerification)
    {
        ASSERT_NO_FATAL_FAILURE(verifyContactCredentials(sharerIndex, shareeAliceIndex));
        ASSERT_NO_FATAL_FAILURE(verifyContactCredentials(sharerIndex, shareeBobIndex));
    }

    // Share folder "folderA" to Alice and subfolder "folderB" to Bob
    auto sharerFolderANode = getNodeByPath(FOLDER_A);
    auto sharerFolderBNode = getNodeByPath(string(FOLDER_A) + "/" + FOLDER_B);
    ASSERT_TRUE(sharerFolderANode) << "folder \"folderA\" not found.";
    ASSERT_TRUE(sharerFolderBNode) << "folder \"folderB\" not found.";
    ASSERT_NO_FATAL_FAILURE(createShareAtoB(sharerFolderANode.get(),
                                            {sharerIndex, true},
                                            {shareeAliceIndex, true},
                                            MegaShare::ACCESS_FULL));
    ASSERT_NO_FATAL_FAILURE(createShareAtoB(sharerFolderBNode.get(),
                                            {sharerIndex, true},
                                            {shareeBobIndex, true},
                                            MegaShare::ACCESS_FULL));

    // Ensure that the sharer, Alice and Bob can see the same nodes and that the tree is decrypted.
    ASSERT_NO_FATAL_FAILURE(
        matchTree(sharerFolderANode->getHandle(), sharerIndex, shareeAliceIndex));
    ASSERT_NO_FATAL_FAILURE(matchTree(sharerFolderBNode->getHandle(), sharerIndex, shareeBobIndex));
    ASSERT_NO_FATAL_FAILURE(
        matchTree(sharerFolderBNode->getHandle(), shareeAliceIndex, shareeBobIndex));

    // Logout and resume session to ensure that all is correct after fetching nodes.
    ASSERT_NO_FATAL_FAILURE(logout(sharerIndex, false, maxTimeout));
    ASSERT_NO_FATAL_FAILURE(login(sharerIndex));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(sharerIndex));
    ASSERT_NO_FATAL_FAILURE(logout(shareeAliceIndex, false, maxTimeout));
    ASSERT_NO_FATAL_FAILURE(login(shareeAliceIndex));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(shareeAliceIndex));
    ASSERT_NO_FATAL_FAILURE(logout(shareeBobIndex, false, maxTimeout));
    ASSERT_NO_FATAL_FAILURE(login(shareeBobIndex));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(shareeBobIndex));

    // Check again that the sharer, Alice and Bob can see the same nodes and that the tree is
    // decrypted.
    ASSERT_NO_FATAL_FAILURE(
        matchTree(sharerFolderANode->getHandle(), sharerIndex, shareeAliceIndex));
    ASSERT_NO_FATAL_FAILURE(matchTree(sharerFolderBNode->getHandle(), sharerIndex, shareeBobIndex));
    ASSERT_NO_FATAL_FAILURE(
        matchTree(sharerFolderBNode->getHandle(), shareeAliceIndex, shareeBobIndex));
}
