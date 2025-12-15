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

TEST_F(SdkTestShareNested, build)
{
    const auto logPre = getLogPrefix();
}
