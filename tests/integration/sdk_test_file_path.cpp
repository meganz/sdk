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
        static const std::vector<NodeInfo> TEST_NODES{
            FileNodeInfo("rootTestFile"),
            DirNodeInfo("dir1")
                .addChild(FileNodeInfo("testFile1"))
#if !defined(WIN32) // Windows does not allow ':' character in file names
                .addChild(FileNodeInfo("testFile1:"))
                .addChild(FileNodeInfo("test:File1"))
                .addChild(FileNodeInfo(":testFile1")),
            FileNodeInfo("rootTestFile:"),
            FileNodeInfo("rootTest:File"),
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
 * @brief SdkTestPath.GetNodeByPathResolvesPathFromGetNodePath
 *
 * Verifies that a node retrieved by handle can return its path using getNodePath
 * and then resolved back to the original handle using getNodeByPath.
 *
 * Steps for each handle defined in the suite:
 * 1. Get a node by handle.
 * 2. Get the node path by using getNodePath.
 * 3. Escape colons in the path (required to use getNodeByPath when the paths has colons).
 * 4. Use getNodeByPath to resolve the escaped path.
 * 5. Confirm that the resolved node has the same handle as the original.
 */
TEST_F(SdkTestPath, GetNodeByPathResolvesPathFromGetNodePath)
{
    auto handles = getAllNodesHandles();

    for (auto handle: handles)
    {
        std::unique_ptr<MegaNode> node(megaApi[0]->getNodeByHandle(handle));
        ASSERT_NE(node, nullptr) << "Failed to retrieve node by handle.";

        std::string path = megaApi[0]->getNodePath(node.get());
        std::string escapedPath = std::regex_replace(path, std::regex(":"), "\\:");

        std::unique_ptr<MegaNode> fromPath(megaApi[0]->getNodeByPath(escapedPath.c_str()));
        ASSERT_NE(fromPath, nullptr) << "Failed to retrieve node by path.";
        EXPECT_EQ(fromPath->getHandle(), handle) << escapedPath;
    }
}

/**
 * @brief SdkTestPath.GetNodeByPathResolvesPathFromGetNodePathByNodeHandle
 *
 * Verifies that a path obtained from a handle using getNodePathByNodeHandle
 * can be resolved back to the original handle using getNodeByPath.
 *
 * Steps for each handle defined in the suite:
 * 1. Get the node path by using getNodePathByNodeHandle.
 * 2. Escape colons in the path (required to use getNodeByPath when the path has colons).
 * 3. Use getNodeByPath to resolve the escaped path.
 * 4. Confirm that the resolved node has the same handle as the original.
 */
TEST_F(SdkTestPath, GetNodeByPathResolvesPathFromGetNodePathByNodeHandle)
{
    auto handles = getAllNodesHandles();

    for (auto handle: handles)
    {
        std::string path = megaApi[0]->getNodePathByNodeHandle(handle);
        std::string escapedPath = std::regex_replace(path, std::regex(":"), "\\:");

        std::unique_ptr<MegaNode> fromPath(megaApi[0]->getNodeByPath(escapedPath.c_str()));
        ASSERT_NE(fromPath, nullptr) << "Failed to retrieve node by path.";
        EXPECT_EQ(fromPath->getHandle(), handle) << escapedPath;
    }
}

/**
 * @brief SdkTestPath.GetNodeByPathOfTypeResolvesPathFromGetNodePath
 *
 * Verifies that a node retrieved by handle can return its path using getNodePath
 * and then be resolved back to the original handle using getNodeByPathOfType.
 *
 * Steps for each handle defined in the suite:
 * 1. Get a node by handle.
 * 2. Get the node path by using getNodePath.
 * 3. Escape colons in the path (required to use getNodeByPathOfType when the path has colons).
 * 4. Use getNodeByPathOfType to resolve the escaped path, providing the node's type.
 * 5. Confirm that the resolved node has the same handle as the original.
 */
TEST_F(SdkTestPath, GetNodeByPathOfTypeResolvesPathFromGetNodePath)
{
    auto handles = getAllNodesHandles();

    for (auto handle: handles)
    {
        std::unique_ptr<MegaNode> node(megaApi[0]->getNodeByHandle(handle));
        ASSERT_NE(node, nullptr) << "Failed to retrieve node by handle.";

        std::string path = megaApi[0]->getNodePath(node.get());
        std::string escapedPath = std::regex_replace(path, std::regex(":"), "\\:");

        std::unique_ptr<MegaNode> fromPath(
            megaApi[0]->getNodeByPathOfType(escapedPath.c_str(), nullptr, node->getType()));
        ASSERT_NE(fromPath, nullptr) << "Failed to retrieve node by path and type.";
        EXPECT_EQ(fromPath->getHandle(), handle) << escapedPath;
    }
}

/**
 * @brief SdkTestPath.GetNodeByPathOfTypeResolvesPathFromGetNodePathByNodeHandle
 *
 * Verifies that a path obtained from a handle using getNodePathByNodeHandle
 * can be resolved back to the original handle using getNodeByPathOfType.
 *
 * Steps for each handle defined in the suite:
 * 1. Get a node by handle (to retrieve the type).
 * 2. Get the node path by using getNodePathByNodeHandle.
 * 3. Escape colons in the path (required to use getNodeByPathOfType when the path has colons).
 * 4. Use getNodeByPathOfType to resolve the escaped path, providing the node's type.
 * 5. Confirm that the resolved node has the same handle as the original.
 */
TEST_F(SdkTestPath, GetNodeByPathOfTypeResolvesPathFromGetNodePathByNodeHandle)
{
    auto handles = getAllNodesHandles();

    for (auto handle: handles)
    {
        std::unique_ptr<MegaNode> node(megaApi[0]->getNodeByHandle(handle));
        ASSERT_NE(node, nullptr) << "Failed to retrieve node by handle.";

        std::string path = megaApi[0]->getNodePathByNodeHandle(handle);
        std::string escapedPath = std::regex_replace(path, std::regex(":"), "\\:");

        std::unique_ptr<MegaNode> fromPath(
            megaApi[0]->getNodeByPathOfType(escapedPath.c_str(), nullptr, node->getType()));
        ASSERT_NE(fromPath, nullptr) << "Failed to retrieve node by path and type.";
        EXPECT_EQ(fromPath->getHandle(), handle) << escapedPath;
    }
}
