/**
 * @file sdk_test_file_path.cpp
 * @brief This file defines tests related to path handling functions
 */

#include "sdk_test_utils.h"
#include "SdkTestNodesSetUp.h"

#include <gmock/gmock.h>

#include <regex>

using namespace sdk_test;

/**
 * @class SdkTestPath
 * @brief Test suite for path handling functions
 *
 */
class SdkTestPath: public SdkTestNodesSetUp
{
public:
    const std::vector<NodeInfo>& getElements() const override
    {
        static const std::vector<NodeInfo> TEST_NODES
        {
            FileNodeInfo("rootTestFile"),
                DirNodeInfo("dir1")
                    .addChild(FileNodeInfo("testFile1"))
#if !defined(WIN32) // Windows does not allow ':' character in file names
                    .addChild(FileNodeInfo("testFile1:"))
                    .addChild(FileNodeInfo("test:File1"))
                    .addChild(FileNodeInfo(":testFile1")),
                FileNodeInfo("rootTestFile:"), FileNodeInfo("rootTest:File"),
                FileNodeInfo(":rootTestFile"),
                DirNodeInfo("dir2:")
                    .addChild(DirNodeInfo("dir3:").addChild(FileNodeInfo("testFile3:")))
                    .addChild(FileNodeInfo("testFile2:"))
                    .addChild(FileNodeInfo("test:File2"))
                    .addChild(FileNodeInfo(":testFile2"))
                    .addChild(FileNodeInfo("testFile2")),
#endif
        };
        return TEST_NODES;
    }

    const std::string& getRootTestDir() const override
    {
        static const std::string dirName{"SDK_TEST_PATH_AUX_DIR"};
        return dirName;
    }

    const std::vector<MegaHandle> getAllNodesHandles()
    {
        std::vector<MegaHandle> result;

        std::function<void(MegaNode*)> collectHandles = [&](MegaNode* node)
        {
            if (!node)
                return;
            result.push_back(node->getHandle());

            std::unique_ptr<MegaNodeList> children(megaApi[0].get()->getChildren(node));
            if (children)
            {
                for (int i = 0; i < children->size(); ++i)
                {
                    collectHandles(children->get(i));
                }
            }
        };

        collectHandles(getRootTestDirectory());
        return result;
    }
};

/**
 * @brief SdkTestPath.PathFunctions
 *
 * Verifies that path functions when paths include colons.
 * It checks consistency between:
 * - `getNodePath` and `getNodeByPath`
 * - `getNodePathByNodeHandle` and `getNodeByPath`
 * - `getNodePath` and `getNodeByPathOfType`
 * - `getNodePathByNodeHandle` and `getNodeByPathOfType`
 */
TEST_F(SdkTestPath, PathFunctions)
{
    auto handles = getAllNodesHandles();

    for (auto handle: handles)
    {
        std::unique_ptr<MegaNode> originalNode =
            std::unique_ptr<MegaNode>(megaApi[0]->getNodeByHandle(handle));
        ASSERT_NE(originalNode, nullptr) << "Failed to retrieve node by handle.";

        std::string pathFromNode = megaApi[0]->getNodePath(originalNode.get());
        std::string pathFromHandle = megaApi[0]->getNodePathByNodeHandle(handle);
        std::string escapedColonsPathFromNode =
            std::regex_replace(pathFromNode, std::regex(":"), "\\:");
        std::string escapedColonsPathFromHandle =
            std::regex_replace(pathFromHandle, std::regex(":"), "\\:");

        std::unique_ptr<MegaNode> nodeFromPath(
            megaApi[0]->getNodeByPath(escapedColonsPathFromNode.c_str()));
        ASSERT_NE(nodeFromPath, nullptr) << "Failed to retrieve node by path.";
        EXPECT_EQ(nodeFromPath->getHandle(), originalNode->getHandle())
            << escapedColonsPathFromNode;

        nodeFromPath = std::unique_ptr<MegaNode>(
            megaApi[0]->getNodeByPath(escapedColonsPathFromHandle.c_str()));
        ASSERT_NE(nodeFromPath, nullptr) << "Failed to retrieve node by path.";
        EXPECT_EQ(nodeFromPath->getHandle(), handle) << escapedColonsPathFromHandle;

        nodeFromPath = std::unique_ptr<MegaNode>(
            megaApi[0]->getNodeByPathOfType(escapedColonsPathFromNode.c_str(),
                                            nullptr,
                                            originalNode->getType()));
        ASSERT_NE(nodeFromPath, nullptr) << "Failed to retrieve node by path and type.";
        EXPECT_EQ(nodeFromPath->getHandle(), originalNode->getHandle())
            << escapedColonsPathFromNode;

        nodeFromPath = std::unique_ptr<MegaNode>(
            megaApi[0]->getNodeByPathOfType(escapedColonsPathFromHandle.c_str(),
                                            nullptr,
                                            originalNode->getType()));
        ASSERT_NE(nodeFromPath, nullptr) << "Failed to retrieve node by path and type.";
        EXPECT_EQ(nodeFromPath->getHandle(), handle) << escapedColonsPathFromHandle;
    }
}
