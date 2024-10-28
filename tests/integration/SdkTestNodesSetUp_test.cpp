#include "SdkTestNodesSetUp_test.h"

void SdkTestNodesSetUp::SetUp()
{
    SdkTest::SetUp();
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    ASSERT_NO_FATAL_FAILURE(createRootTestDir());
    createNodes(getElements(), rootTestDirNode.get());
}

std::vector<std::string> SdkTestNodesSetUp::getAllNodesNames() const
{
    std::vector<std::string> result;
    std::for_each(getElements().begin(),
                  getElements().end(),
                  [&result](const auto& el)
                  {
                      const auto partial = getNodeNames(el);
                      result.insert(result.end(), partial.begin(), partial.end());
                  });
    return result;
}

std::unique_ptr<MegaSearchFilter> SdkTestNodesSetUp::getDefaultfilter() const
{
    std::unique_ptr<MegaSearchFilter> filteringInfo(MegaSearchFilter::createInstance());
    filteringInfo->byLocationHandle(rootTestDirNode->getHandle());
    return filteringInfo;
}

std::unique_ptr<MegaNode> SdkTestNodesSetUp::getNodeByPath(const std::string& path) const
{
    const auto testPath = convertToTestPath(path);
    return std::unique_ptr<MegaNode>(megaApi[0]->getNodeByPath(testPath.c_str()));
}

std::optional<MegaHandle> SdkTestNodesSetUp::getNodeHandleByPath(const std::string& path) const
{
    const auto testPath = convertToTestPath(path);
    if (std::unique_ptr<MegaNode> node(megaApi[0]->getNodeByPath(testPath.c_str())); node)
        return node->getHandle();
    return {};
}

void SdkTestNodesSetUp::createRootTestDir()
{
    const std::unique_ptr<MegaNode> rootnode(megaApi[0]->getRootNode());
    rootTestDirNode = createRemoteDir(getRootTestDir(), rootnode.get());
    ASSERT_NE(rootTestDirNode, nullptr) << "Unable to create root node at " + getRootTestDir();
}

void SdkTestNodesSetUp::createNodes(const std::vector<sdk_test::NodeInfo>& elements,
                                    MegaNode* rootnode)
{
    for (const auto& element: elements)
    {
        if (keepDifferentCreationTimes())
        {
            std::this_thread::sleep_for(1s); // Make sure creation time is different
        }
        std::visit(
            [this, rootnode](const auto& nodeInfo)
            {
                createNode(nodeInfo, rootnode);
            },
            element);
    }
}

void SdkTestNodesSetUp::createNode(const sdk_test::FileNodeInfo& fileInfo, MegaNode* rootnode)
{
    bool check = false;
    mApi[0].mOnNodesUpdateCompletion =
        createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check);
    sdk_test::LocalTempFile localFile(fileInfo.name, fileInfo.size);
    MegaHandle file1Handle = INVALID_HANDLE;
    ASSERT_EQ(MegaError::API_OK,
              doStartUpload(0,
                            &file1Handle,
                            fileInfo.name.c_str(),
                            rootnode,
                            nullptr /*fileName*/,
                            fileInfo.mtime,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/))
        << "Cannot upload a test file";

    waitForResponse(&check);
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    std::unique_ptr<MegaNode> nodeFile(megaApi[0]->getNodeByHandle(file1Handle));
    ASSERT_NE(nodeFile, nullptr) << "Cannot get the node for the updated file (error: "
                                 << mApi[0].lastError << ")";
    setNodeAdditionalAttributes(fileInfo, nodeFile);
}

void SdkTestNodesSetUp::createNode(const sdk_test::DirNodeInfo& dirInfo, MegaNode* rootnode)
{
    auto dirNode = createRemoteDir(dirInfo.name, rootnode);
    ASSERT_TRUE(dirNode) << "Unable to create directory node with name: " << dirInfo.name;
    setNodeAdditionalAttributes(dirInfo, dirNode);
    createNodes(dirInfo.childs, dirNode.get());
}

std::unique_ptr<MegaNode> SdkTestNodesSetUp::createRemoteDir(const std::string& dirName,
                                                             MegaNode* rootnode)
{
    bool check = false;
    mApi[0].mOnNodesUpdateCompletion =
        createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check);
    auto folderHandle = createFolder(0, dirName.c_str(), rootnode);
    if (folderHandle == INVALID_HANDLE)
    {
        return {};
    }
    waitForResponse(&check);
    std::unique_ptr<MegaNode> dirNode(megaApi[0]->getNodeByHandle(folderHandle));
    resetOnNodeUpdateCompletionCBs();
    return dirNode;
}

void SdkTestNodesSetUp::setNodeTag(const std::unique_ptr<MegaNode>& node, const std::string& tag)
{
    RequestTracker trackerAddTag(megaApi[0].get());
    megaApi[0]->addNodeTag(node.get(), tag.c_str(), &trackerAddTag);
    ASSERT_EQ(trackerAddTag.waitForResult(), API_OK);
}

void SdkTestNodesSetUp::setNodeDescription(const std::unique_ptr<MegaNode>& node,
                                           const std::string& description)
{
    RequestTracker trackerSetDescription(megaApi[0].get());
    megaApi[0]->setNodeDescription(node.get(), description.c_str(), &trackerSetDescription);
    ASSERT_EQ(trackerSetDescription.waitForResult(), API_OK);
}
