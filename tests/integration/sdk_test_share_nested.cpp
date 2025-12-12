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

private:
    // root in the cloud where the tree is created
    const std::string rootTestDir{"locklessCS"};

    // Name of the initial elements in the remote tree
    static constexpr auto FOLDER_A = "folderA";
    static constexpr auto FOLDER_B = "folderB";
    static constexpr auto FOLDER_C = "folderC";
    static constexpr auto FILE_A = "fileA";
    static constexpr auto FILE_B = "fileB";
    static constexpr auto FILE_C = "fileC";

    // It represents the following tree:
    // Cloud
    // └── folderA
    //     ├── fileA
    //     └── folderB
    //         ├── fileB
    //         └── folderC
    //             └── fileC
    const std::vector<NodeInfo> treeElements{
        DirNodeInfo(FOLDER_A)
            .addChild(FileNodeInfo(FILE_A).setSize(100))
            .addChild(
                DirNodeInfo(FOLDER_B)
                    .addChild(FileNodeInfo(FILE_B).setSize(100))
                    .addChild(DirNodeInfo(FOLDER_C).addChild(FileNodeInfo(FILE_C).setSize(100))))};
};

TEST_F(SdkTestShareNested, build)
{
    const auto logPre = getLogPrefix();
}
