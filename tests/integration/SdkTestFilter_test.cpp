/**
 * @file SdkTestFilter_test.cpp
 * @brief This file defines some tests that involve interactions with the MegaSearchFilter
 * object. This includes operations like:
 *   - Searching nodes with different filters
 *   - Ordering search results with different criteria
 *   - Apply different kinds of conditions in the filters
 */

#include "megaapi.h"
#include "sdk_test_utils.h"
#include "SdkTest_test.h"

#include <gmock/gmock.h>

#include <chrono>
#include <optional>

using namespace std::chrono_literals;
using namespace sdk_test;

namespace
{

// Needed forward declaration for recursive call
void processDirChildName(const DirNodeInfo&, std::vector<std::string>&);

/**
 * @brief Put the name of the node and their children (if any) in the given vector
 */
void processNodeName(const NodeInfo& node, std::vector<std::string>& names)
{
    std::visit(
        [&names](const auto& arg)
        {
            names.push_back(arg.name);
            if constexpr (std::is_same_v<decltype(arg), const DirNodeInfo&>)
            {
                processDirChildName(arg, names);
            }
        },
        node);
}

/**
 * @brief Aux function to call inside the visitor. Puts the names of the children in names.
 */
void processDirChildName(const DirNodeInfo& dir, std::vector<std::string>& names)
{
    std::for_each(dir.childs.begin(),
                  dir.childs.end(),
                  [&names](const auto& child)
                  {
                      processNodeName(child, names);
                  });
}

/**
 * @brief Wrapper around processNodeName. Returns the names in the tree specified by node
 *
 * @note The tree is iterated using a depth-first approach
 */
std::vector<std::string> getNodeNames(const NodeInfo& node)
{
    std::vector<std::string> result;
    processNodeName(node, result);
    return result;
}
}

/**
 * @class SdkTestNodesSetUp
 * @brief An abstract class that provides a template fixture/test suite to setup an account with a
 * certain node tree.
 *
 * Child classes need to implement:
 *   - getRootTestDir(): Returns a string that will be the name of a directory that will be created
 *   inside which all the nodes will be created. This prevents collisions with other test suites.
 *   - getElements(): Returns a vector of NodeInfo object that will be used to create the nodes.
 *
 */
class SdkTestNodesSetUp: public SdkTest
{
private:
    std::unique_ptr<MegaNode> rootTestDirNode;

public:
    virtual const std::string& getRootTestDir() const = 0;

    virtual const std::vector<NodeInfo>& getElements() const = 0;

    void SetUp() override
    {
        SdkTest::SetUp();
        ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
        ASSERT_NO_FATAL_FAILURE(createRootTestDir());
        createNodes(getElements(), rootTestDirNode.get());
    }

    /**
     * @brief Get a vector with all the names of the nodes created inside the getRootTestDir()
     */
    std::vector<std::string> getAllNodesNames() const
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

    /**
     * @brief Get a filter with the byLocationHandle set up properly to point to the root directory
     * for the tests suite (getRootTestDir())
     */
    std::unique_ptr<MegaSearchFilter> getDefaultfilter() const
    {
        std::unique_ptr<MegaSearchFilter> filteringInfo(MegaSearchFilter::createInstance());
        filteringInfo->byLocationHandle(rootTestDirNode->getHandle());
        return filteringInfo;
    }

    /**
     * @brief get a raw pointer to the root node for this test (the one created in root of the
     * account with the name given by getRootTestDir()).
     *
     * The class retains the ownership of the object.
     */
    MegaNode* getRootTestDirectory() const
    {
        return rootTestDirNode.get();
    }

protected:
    /**
     * @brief Create the getRootTestDir() and sets store it internally
     */
    void createRootTestDir()
    {
        const std::unique_ptr<MegaNode> rootnode(megaApi[0]->getRootNode());
        rootTestDirNode = createRemoteDir(getRootTestDir(), rootnode.get());
        ASSERT_NE(rootTestDirNode, nullptr) << "Unable to create root node at " + getRootTestDir();
    }

    /**
     * @brief Creates the file tree given by the vector of NodeInfo starting from the rootnode
     *
     * @param elements NodeInfo vector to create
     * @param rootnode A pointer to the root node
     */
    void createNodes(const std::vector<NodeInfo>& elements, MegaNode* rootnode)
    {
        for (const auto& element: elements)
        {
            std::this_thread::sleep_for(1s); // Make sure creation time is different
            std::visit(
                [this, rootnode](const auto& nodeInfo)
                {
                    createNode(nodeInfo, rootnode);
                },
                element);
        }
    }

    /**
     * @brief Creates a file node as a child of the rootnode using the input info.
     */
    void createNode(const FileNodeInfo& fileInfo, MegaNode* rootnode)
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
        ASSERT_NE(nodeFile, nullptr)
            << "Cannot get the node for the updated file (error: " << mApi[0].lastError << ")";
        setNodeAdditionalAttributes(fileInfo, nodeFile);
    }

    /**
     * @brief Creates a directory node as a child of the rootnode using the input info.
     */
    void createNode(const DirNodeInfo& dirInfo, MegaNode* rootnode)
    {
        auto dirNode = createRemoteDir(dirInfo.name, rootnode);
        ASSERT_TRUE(dirNode) << "Unable to create directory node with name: " << dirInfo.name;
        setNodeAdditionalAttributes(dirInfo, dirNode);
        createNodes(dirInfo.childs, dirNode.get());
    }

    /**
     * @brief Aux method to create a directory node with the given name inside the given rootnode
     *
     * NOTE: You must check that the output value is not a nullptr. If it is, there was a failure in
     * the creation.
     */
    std::unique_ptr<MegaNode> createRemoteDir(const std::string& dirName, MegaNode* rootnode)
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

    /**
     * @brief Sets special info such as fav, label, tags or description for a given node
     */
    template<typename Derived>
    void setNodeAdditionalAttributes(const NodeCommonInfo<Derived>& nodeInfo,
                                     const std::unique_ptr<MegaNode>& node)
    {
        ASSERT_NE(node, nullptr) << "Trying to set attributes of an invalide node pointer.";

        // Fav
        ASSERT_EQ(API_OK, synchronousSetNodeFavourite(0, node.get(), nodeInfo.fav))
            << "Error setting fav";

        // Label
        if (nodeInfo.label)
        {
            ASSERT_EQ(API_OK, synchronousSetNodeLabel(0, node.get(), *nodeInfo.label))
                << "Error setting label";
        }
        else
        {
            ASSERT_EQ(API_OK, synchronousResetNodeLabel(0, node.get())) << "Error resetting label";
        }

        // Sensitivity
        if (nodeInfo.sensitive)
        {
            ASSERT_EQ(API_OK, synchronousSetNodeSensitive(0, node.get(), true))
                << "Error setting sensitive node";
        }

        // Set tags
        std::for_each(std::begin(nodeInfo.tags),
                      std::end(nodeInfo.tags),
                      [this, &node](auto&& tag)
                      {
                          ASSERT_NO_FATAL_FAILURE(setNodeTag(node, tag));
                      });
        // Description
        ASSERT_NO_FATAL_FAILURE(setNodeDescription(node, nodeInfo.description));
    }

    void setNodeTag(const std::unique_ptr<MegaNode>& node, const std::string& tag)
    {
        RequestTracker trackerAddTag(megaApi[0].get());
        megaApi[0]->addNodeTag(node.get(), tag.c_str(), &trackerAddTag);
        ASSERT_EQ(trackerAddTag.waitForResult(), API_OK);
    }

    void setNodeDescription(const std::unique_ptr<MegaNode>& node, const std::string& description)
    {
        RequestTracker trackerSetDescription(megaApi[0].get());
        megaApi[0]->setNodeDescription(node.get(), description.c_str(), &trackerSetDescription);
        ASSERT_EQ(trackerSetDescription.waitForResult(), API_OK);
    }
};

/**
 * @class SdkTestOrder
 * @brief Test suite designed to test the ordering of results from search and getChildren methods
 *
 */
class SdkTestOrder: public SdkTestNodesSetUp
{
    const std::vector<NodeInfo>& getElements() const override
    {
        static const std::vector<NodeInfo> ELEMENTS{
            FileNodeInfo("testFile1").setLabel(MegaNode::NODE_LBL_RED),
            DirNodeInfo("Dir1")
                .setLabel(MegaNode::NODE_LBL_PURPLE)
                .setFav(true)
                .addChild(FileNodeInfo("testFile2")
                              .setLabel(MegaNode::NODE_LBL_ORANGE)
                              .setFav(true)
                              .setSize(15)
                              .setMtime(100s)
                              .setSensitive(true))
                .addChild(FileNodeInfo("testFile3")
                              .setLabel(MegaNode::NODE_LBL_YELLOW)
                              .setSize(35)
                              .setMtime(500s))
                .addChild(DirNodeInfo("Dir11")
                              .setLabel(MegaNode::NODE_LBL_YELLOW)
                              .addChild(FileNodeInfo("testFile4"))),
            DirNodeInfo("Dir2").setSensitive(true).addChild(FileNodeInfo("testFile5")
                                                                .setLabel(MegaNode::NODE_LBL_BLUE)
                                                                .setFav(true)
                                                                .setSize(20)
                                                                .setMtime(200s)),
            FileNodeInfo("testFile6").setFav(true).setSize(10).setMtime(300s),
            FileNodeInfo("TestFile5Uppercase"),
        };
        return ELEMENTS;
    }

    const std::string& getRootTestDir() const override
    {
        static const std::string dirName{"SDK_TEST_ORDER_AUX_DIR"};
        return dirName;
    }
};

/**
 * @brief Helper parameterized matcher that checks if the test container contains all the elements
 * in the same order.
 *
 * Example:
 *     std::vector arg {1, 5, 7, 8};
 *     ASSERT_THAT(arg, ContainsInOrder(std::vector {1, 7, 8}));
 *     ASSERT_THAT(arg, testing::Not(ContainsInOrder(std::vector {1, 7, 5})));
 *     ASSERT_THAT(arg, testing::Not(ContainsInOrder(std::vector {1, 7, 7, 8})));
 *
 */
MATCHER_P(ContainsInOrder, elements, "")
{
    if (elements.size() > arg.size())
        return false;

    return std::all_of(
        elements.begin(),
        elements.end(),
        [currentEntry = arg.begin(), allEntriesEnd = arg.cend()](const auto& element) mutable
        {
            while (currentEntry != allEntriesEnd && *currentEntry != element)
                ++currentEntry;

            return currentEntry++ != allEntriesEnd;
        });
}

/**
 * @brief SdkTestOrder.SdkGetNodesInOrder
 *
 * Tests all the sorting options available for the MegaApi.search method.
 */
TEST_F(SdkTestOrder, SdkGetNodesInOrder)
{
    using testing::AllOf;
    using testing::NotNull;
    using testing::UnorderedElementsAreArray;

    // Load the default filter to search from ROOT_TEST_NODE_DIR
    std::unique_ptr<MegaSearchFilter> filteringInfo(getDefaultfilter());

    // Default (ORDER_NONE -> Undefined)
    std::unique_ptr<MegaNodeList> searchResults(megaApi[0]->search(filteringInfo.get()));
    ASSERT_THAT(searchResults, NotNull()) << "search() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), UnorderedElementsAreArray(getAllNodesNames()))
        << "Unexpected sorting for ORDER_NONE";

    // Alphabetical, dirs first
    std::vector<std::string_view> expected{"Dir1",
                                           "Dir2",
                                           "Dir11",
                                           "testFile1",
                                           "testFile5",
                                           "TestFile5Uppercase",
                                           "testFile6"};
    searchResults.reset(megaApi[0]->search(filteringInfo.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_THAT(searchResults, NotNull()) << "search() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_DEFAULT_ASC";

    // Alphabetical inverted, dirs first (reverse independently)
    std::reverse(expected.begin(), expected.begin() + 3);
    std::reverse(expected.begin() + 3, expected.end());
    searchResults.reset(megaApi[0]->search(filteringInfo.get(), MegaApi::ORDER_DEFAULT_DESC));
    ASSERT_THAT(searchResults, NotNull()) << "search() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_DEFAULT_DESC";

    // By size, dirs first. Ties break by natural
    // sorting
    expected = {
        "Dir11",
        "Dir2",
        "Dir1",
        "testFile1", // 0
        "testFile4", // 0
        "TestFile5Uppercase", // 0
        "testFile6", // 10
        "testFile2", // 15
        "testFile5", // 20
        "testFile3" // 35
    };
    searchResults.reset(megaApi[0]->search(filteringInfo.get(), MegaApi::ORDER_SIZE_ASC));
    ASSERT_THAT(searchResults, NotNull()) << "search() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_SIZE_ASC";

    // By size inverted, dirs first
    std::reverse(expected.begin(), expected.begin() + 3);
    std::reverse(expected.begin() + 3, expected.end());
    searchResults.reset(megaApi[0]->search(filteringInfo.get(), MegaApi::ORDER_SIZE_DESC));
    ASSERT_THAT(searchResults, NotNull()) << "search() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_SIZE_DESC";

    // By creation time, dirs first
    expected = {"Dir1", "Dir11", "testFile1", "testFile3", "testFile5", "testFile6"};
    searchResults.reset(megaApi[0]->search(filteringInfo.get(), MegaApi::ORDER_CREATION_ASC));
    ASSERT_THAT(searchResults, NotNull()) << "search() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_CREATION_ASC";

    // By creation inverted
    std::reverse(expected.begin() + 2, expected.end());
    std::reverse(expected.begin(), expected.begin() + 2);
    searchResults.reset(megaApi[0]->search(filteringInfo.get(), MegaApi::ORDER_CREATION_DESC));
    ASSERT_THAT(searchResults, NotNull()) << "search() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_CREATION_DES";

    // By modification time, dirs first but ordered naturally
    expected = {
        "Dir1",
        "Dir2",
        "Dir11",
        "testFile3", // 500 s ago
        "testFile6", // 300 s ago
        "testFile5", // 200 s ago
        "testFile2", // 100 s ago
        "testFile1", // Undef (upload time)
    };
    searchResults.reset(megaApi[0]->search(filteringInfo.get(), MegaApi::ORDER_MODIFICATION_ASC));
    ASSERT_THAT(searchResults, NotNull()) << "search() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_MODIFICATION_ASC";

    // By modification inverted
    std::reverse(expected.begin(), expected.begin() + 3);
    std::reverse(expected.begin() + 3, expected.end());
    searchResults.reset(megaApi[0]->search(filteringInfo.get(), MegaApi::ORDER_MODIFICATION_DESC));
    ASSERT_THAT(searchResults, NotNull()) << "search() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_MODIFICATION_DES";

    // By label, dirs and  natural sort
    expected = {
        "testFile1", // Red (1)
        "testFile2", // Orange (2)
        "Dir11", // Yellow (3)
        "testFile3", // Yellow (3)
        "testFile5", // Blue (5)
        "Dir1", // Purple (6)
        "Dir2", // Nothing
        "testFile4", // Nothing
        "testFile6" // Nothing
    };
    searchResults.reset(megaApi[0]->search(filteringInfo.get(), MegaApi::ORDER_LABEL_ASC));
    ASSERT_THAT(searchResults, NotNull()) << "search() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_LABEL_ASC";

    expected = {
        "Dir1", // Purple (6)
        "testFile5", // Blue (5)
        "Dir11", // Yellow (3)
        "testFile3", // Yellow (3)
        "testFile2", // Orange (2)
        "testFile1", // Red (1)
        "Dir2", // Nothing
        "testFile4", // Nothing
        "testFile6" // Nothing
    };
    searchResults.reset(megaApi[0]->search(filteringInfo.get(), MegaApi::ORDER_LABEL_DESC));
    ASSERT_THAT(searchResults, NotNull()) << "search() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_LABEL_DESC";

    // By fav, dirs and natural sort
    expected = {
        "Dir1", // fav
        "testFile5", // fav
        "testFile6", // fav
        "Dir2", // not fav
        "Dir11", // not fav
        "testFile1", // not fav
        "TestFile5Uppercase", // not fav
    };
    searchResults.reset(megaApi[0]->search(filteringInfo.get(), MegaApi::ORDER_FAV_ASC));
    ASSERT_THAT(searchResults, NotNull()) << "search() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_FAV_ASC";

    // By fav inverted, dirs first
    std::rotate(expected.begin(), expected.begin() + 3, expected.end());
    searchResults.reset(megaApi[0]->search(filteringInfo.get(), MegaApi::ORDER_FAV_DESC));
    ASSERT_THAT(searchResults, NotNull()) << "search() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_FAV_DESC";
}

/**
 * @brief SdkTestOrder.SdkGetNodesInOrder
 *
 * Tests all the sorting options available for the MegaApi.getChildren method.
 */
TEST_F(SdkTestOrder, SdkGetChildrenInOrder)
{
    using testing::AllOf;
    using testing::NotNull;
    using testing::UnorderedElementsAreArray;

    // Load the default filter to getChildren from ROOT_TEST_NODE_DIR
    std::unique_ptr<MegaSearchFilter> filteringInfo(getDefaultfilter());

    // Alphabetical, dirs first
    std::vector<std::string_view> expected{"testFile1",
                                           "Dir1",
                                           "Dir2",
                                           "TestFile5Uppercase",
                                           "testFile6"};

    // Default (ORDER_NONE -> Undefined)
    std::unique_ptr<MegaNodeList> children(megaApi[0]->getChildren(filteringInfo.get()));
    ASSERT_THAT(children, NotNull()) << "getChildren() returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), UnorderedElementsAreArray(expected))
        << "Unexpected sorting for ORDER_NONE";

    expected = {"Dir1", "Dir2", "testFile1", "TestFile5Uppercase", "testFile6"};

    children.reset(megaApi[0]->getChildren(filteringInfo.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_THAT(children, NotNull()) << "getChildren() returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_DEFAULT_ASC";

    children.reset(megaApi[0]->getChildren(getRootTestDirectory(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_THAT(children, NotNull()) << "getChildren() with parent returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_DEFAULT_ASC getChildren with parent";

    // Alphabetical inverted, dirs first (reverse independently)
    std::reverse(expected.begin(), expected.begin() + 2);
    std::reverse(expected.begin() + 2, expected.end());
    children.reset(megaApi[0]->getChildren(filteringInfo.get(), MegaApi::ORDER_DEFAULT_DESC));
    ASSERT_THAT(children, NotNull()) << "getChildren() returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_DEFAULT_DESC";

    children.reset(megaApi[0]->getChildren(getRootTestDirectory(), MegaApi::ORDER_DEFAULT_DESC));
    ASSERT_THAT(children, NotNull()) << "getChildren() with parent returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_DEFAULT_DESC getChildren with parent";

    // By size, dirs first (but not relevant order for now as size is 0). Ties break by natural
    // sorting
    expected = {
        "Dir2",
        "Dir1",
        "testFile1", // 0
        "TestFile5Uppercase", // 0
        "testFile6", // 10
    };
    children.reset(megaApi[0]->getChildren(filteringInfo.get(), MegaApi::ORDER_SIZE_ASC));
    ASSERT_THAT(children, NotNull()) << "getChildren() returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_SIZE_ASC";

    children.reset(megaApi[0]->getChildren(getRootTestDirectory(), MegaApi::ORDER_SIZE_ASC));
    ASSERT_THAT(children, NotNull()) << "getChildren() with parent returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_SIZE_ASC getChildren with parent";

    // By size inverted, dirs first
    std::reverse(expected.begin(), expected.begin() + 2);
    std::reverse(expected.begin() + 2, expected.end());
    children.reset(megaApi[0]->getChildren(filteringInfo.get(), MegaApi::ORDER_SIZE_DESC));
    ASSERT_THAT(children, NotNull()) << "getChildren() returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_SIZE_DESC";

    children.reset(megaApi[0]->getChildren(getRootTestDirectory(), MegaApi::ORDER_SIZE_DESC));
    ASSERT_THAT(children, NotNull()) << "getChildren() with parent returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_SIZE_DESC getChildren with parent";

    // By creation time, dirs first
    expected = {"Dir1", "testFile1", "testFile6"};
    children.reset(megaApi[0]->getChildren(filteringInfo.get(), MegaApi::ORDER_CREATION_ASC));
    ASSERT_THAT(children, NotNull()) << "getChildren() returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_CREATION_ASC";

    children.reset(megaApi[0]->getChildren(getRootTestDirectory(), MegaApi::ORDER_CREATION_ASC));
    ASSERT_THAT(children, NotNull()) << "getChildren() with parent returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_CREATION_ASC getChildren with parent";

    // By creation inverted
    std::reverse(expected.begin() + 1, expected.end());
    std::reverse(expected.begin(), expected.begin() + 1);
    children.reset(megaApi[0]->getChildren(filteringInfo.get(), MegaApi::ORDER_CREATION_DESC));
    ASSERT_THAT(children, NotNull()) << "getChildren() returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_CREATION_DES";

    children.reset(megaApi[0]->getChildren(getRootTestDirectory(), MegaApi::ORDER_CREATION_DESC));
    ASSERT_THAT(children, NotNull()) << "getChildren() with parent returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_CREATION_DESC getChildren with parent";

    // By modification time, dirs first but ordered naturally ASC
    expected = {
        "Dir1",
        "Dir2",
        "testFile6", // 300 s ago
        "testFile1", // Undef (upload time)
    };
    children.reset(megaApi[0]->getChildren(filteringInfo.get(), MegaApi::ORDER_MODIFICATION_ASC));
    ASSERT_THAT(children, NotNull()) << "getChildren() returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_MODIFICATION_ASC";

    children.reset(
        megaApi[0]->getChildren(getRootTestDirectory(), MegaApi::ORDER_MODIFICATION_ASC));
    ASSERT_THAT(children, NotNull()) << "getChildren() with parent returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_MODIFICATION_ASC getChildren with parent";

    std::reverse(expected.begin(), expected.begin() + 2);
    std::reverse(expected.begin() + 2, expected.end());
    children.reset(megaApi[0]->getChildren(filteringInfo.get(), MegaApi::ORDER_MODIFICATION_DESC));
    ASSERT_THAT(children, NotNull()) << "getChildren() returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_MODIFICATION_DES";

    children.reset(
        megaApi[0]->getChildren(getRootTestDirectory(), MegaApi::ORDER_MODIFICATION_DESC));
    ASSERT_THAT(children, NotNull()) << "getChildren() with parent returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_MODIFICATION_DESC getChildren with parent";

    // By label, dirs and  natural sort
    expected = {
        "testFile1", // Red (1)
        "Dir1", // Purple (6)
        "Dir2", // Nothing
        "TestFile5Uppercase", // Nothing
        "testFile6", // Nothing
    };

    children.reset(megaApi[0]->getChildren(filteringInfo.get(), MegaApi::ORDER_LABEL_ASC));
    ASSERT_THAT(children, NotNull()) << "getChildren() returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_LABEL_ASC";

    children.reset(megaApi[0]->getChildren(getRootTestDirectory(), MegaApi::ORDER_LABEL_ASC));
    ASSERT_THAT(children, NotNull()) << "getChildren() with parent returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_LABEL_ASC getChildren with parent";

    expected = {
        "Dir1", // Purple (6)
        "testFile1", // Red (1)
        "Dir2", // Nothing
        "TestFile5Uppercase", // Nothing
        "testFile6" // Nothing
    };

    children.reset(megaApi[0]->getChildren(filteringInfo.get(), MegaApi::ORDER_LABEL_DESC));
    ASSERT_THAT(children, NotNull()) << "getChildren() returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_LABEL_DESC";

    children.reset(megaApi[0]->getChildren(getRootTestDirectory(), MegaApi::ORDER_LABEL_DESC));
    ASSERT_THAT(children, NotNull()) << "getChildren() with parent returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_LABEL_DESC getChildren with parent";

    // By fav, dirs first. Ties broken by natural sort
    expected = {
        "Dir1", // fav
        "testFile6", // fav
        "Dir2", // not fav
        "testFile1", // not fav
        "TestFile5Uppercase", // not fav
    };
    children.reset(megaApi[0]->getChildren(filteringInfo.get(), MegaApi::ORDER_FAV_ASC));
    ASSERT_THAT(children, NotNull()) << "getChildren() returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_FAV_ASC";

    children.reset(megaApi[0]->getChildren(getRootTestDirectory(), MegaApi::ORDER_FAV_ASC));
    ASSERT_THAT(children, NotNull()) << "getChildren() with parent returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_FAV_ASC getChildren with parent";

    // By fav inverted, dirs first
    std::rotate(expected.begin(), expected.begin() + 2, expected.end());
    children.reset(megaApi[0]->getChildren(filteringInfo.get(), MegaApi::ORDER_FAV_DESC));
    ASSERT_THAT(children, NotNull()) << "getChildren() returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_FAV_DESC";

    children.reset(megaApi[0]->getChildren(getRootTestDirectory(), MegaApi::ORDER_FAV_DESC));
    ASSERT_THAT(children, NotNull()) << "getChildren() with parent returned a nullptr";
    EXPECT_THAT(toNamesVector(*children), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_FAV_DESC getChildren with parent";
}

/**
 * @class SdkTestFilter
 * @brief Test suite to test some filtering options for the searching methods
 *
 */
class SdkTestFilter: public SdkTestNodesSetUp
{
    const std::vector<NodeInfo>& getElements() const override
    {
        static const std::vector<NodeInfo> ELEMENTS{
            FileNodeInfo("testFile1")
                .setDescription("This is a test description")
                .setTags({"foo", "bar"})
                .setFav(true),
            DirNodeInfo("Dir1")
                .setSensitive(true)
                .addChild(FileNodeInfo("testFile2")
                              .setDescription("description of file 2")
                              .setTags({"bar", "testTag"}))
                .addChild(FileNodeInfo("F3").setFav(true)),
        };
        return ELEMENTS;
    }

    const std::string& getRootTestDir() const override
    {
        static const std::string dirName{"SDK_TEST_FILTER_AUX_DIR"};
        return dirName;
    }
};

/**
 * @brief SdkTestFilter.SdkFilterByFav
 *
 * Filter search results by favourite
 */
TEST_F(SdkTestFilter, SdkFilterByFav)
{
    using testing::NotNull;
    using testing::UnorderedElementsAreArray;

    const auto allNodesNames = getAllNodesNames();

    // By fav: All
    std::unique_ptr<MegaSearchFilter> filteringInfo(getDefaultfilter());
    filteringInfo->byFavourite(MegaSearchFilter::BOOL_FILTER_DISABLED);
    std::unique_ptr<MegaNodeList> searchResults(megaApi[0]->search(filteringInfo.get()));
    ASSERT_THAT(searchResults, NotNull()) << "search() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), UnorderedElementsAreArray(allNodesNames))
        << "Unexpected filtering results for byFavourite(BOOL_FILTER_DISABLED)";

    // By fav: Only favs
    filteringInfo = getDefaultfilter();
    filteringInfo->byFavourite(MegaSearchFilter::BOOL_FILTER_ONLY_TRUE);
    std::vector<std::string> expectedFavs{
        "testFile1", // fav
        "F3", // fav
    };
    searchResults.reset(megaApi[0]->search(filteringInfo.get()));
    ASSERT_THAT(searchResults, NotNull()) << "search() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), UnorderedElementsAreArray(expectedFavs))
        << "Unexpected filtering results for byFavourite(BOOL_FILTER_ONLY_TRUE)";

    // By fav: Only not favs (non-favs + favs = all)
    filteringInfo = getDefaultfilter();
    filteringInfo->byFavourite(MegaSearchFilter::BOOL_FILTER_ONLY_FALSE);
    searchResults.reset(megaApi[0]->search(filteringInfo.get()));
    ASSERT_THAT(searchResults, NotNull()) << "search() returned a nullptr";
    auto obtainedNonFavs = toNamesVector(*searchResults);
    EXPECT_EQ(obtainedNonFavs.size() + expectedFavs.size(), allNodesNames.size())
        << "The number of non fav nodes + fav nodes is different from the total number of nodes";
    std::for_each(
        obtainedNonFavs.begin(),
        obtainedNonFavs.end(),
        [&allFavs = std::as_const(expectedFavs),
         &all = std::as_const(allNodesNames)](const auto& noFav)
        {
            EXPECT_THAT(all, testing::Contains(noFav))
                << "byFavourite(BOOL_FILTER_ONLY_FALSE) gave an node that should not exist";
            EXPECT_THAT(allFavs, testing::Not(testing::Contains(noFav)))
                << "byFavourite(BOOL_FILTER_ONLY_FALSE) gave a favourite node as result";
        });
}

/**
 * @brief SdkTestFilter.SdkFilterBySensitivity
 *
 * Filter search results by sensitivity
 *
 * @note To get only nodes marked as sensitive use BOOL_FILTER_ONLY_FALSE
 * @note To get only nodes that are not sensitive and do not have any sensitive ancestors use
 * BOOL_FILTER_ONLY_TRUE
 */
TEST_F(SdkTestFilter, SdkFilterBySensitivity)
{
    using testing::NotNull;
    using testing::UnorderedElementsAreArray;

    const auto allNodesNames = getAllNodesNames();

    std::unique_ptr<MegaSearchFilter> filteringInfo(getDefaultfilter());
    std::vector<std::string> expectedSens{"Dir1"};
    filteringInfo = getDefaultfilter();
    filteringInfo->bySensitivity(MegaSearchFilter::BOOL_FILTER_ONLY_FALSE);
    std::unique_ptr<MegaNodeList> searchResults(megaApi[0]->search(filteringInfo.get()));
    ASSERT_THAT(searchResults, NotNull()) << "search() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), UnorderedElementsAreArray(expectedSens))
        << "Unexpected filtering results for bySensitivity(BOOL_FILTER_ONLY_FALSE)";

    auto expectedNoSens = getAllNodesNames();
    expectedNoSens.erase(std::remove_if(expectedNoSens.begin(),
                                        expectedNoSens.end(),
                                        [](const auto& e)
                                        {
                                            static const std::array sens{"Dir1", "testFile2", "F3"};
                                            return std::find(sens.begin(), sens.end(), e) !=
                                                   sens.end();
                                        }),
                         expectedNoSens.end());
    filteringInfo = getDefaultfilter();
    filteringInfo->bySensitivity(MegaSearchFilter::BOOL_FILTER_ONLY_TRUE);
    searchResults.reset(megaApi[0]->search(filteringInfo.get()));
    ASSERT_THAT(searchResults, NotNull()) << "search() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), UnorderedElementsAreArray(expectedNoSens))
        << "Unexpected filtering results for bySensitivity(BOOL_FILTER_ONLY_TRUE)";
}

/**
 * @brief SdkTestFilter.SdkAndOrSwitchCombinaion
 *
 * Filter search results by text conditions (name, description, tags) combining conditions with AND
 * or OR logic operations.
 */
TEST_F(SdkTestFilter, SdkAndOrSwitchCombinaion)
{
    using testing::NotNull;
    using testing::UnorderedElementsAreArray;

    // Load the default filter
    std::unique_ptr<MegaSearchFilter> filteringInfo(getDefaultfilter());

    //// Combine by AND (this is the default already tested in other tests)
    // Two nodes matching tag but only one matches description
    filteringInfo->useAndForTextQuery(true);
    filteringInfo->byTag("bar");
    filteringInfo->byDescription("test");

    std::unique_ptr<MegaNodeList> results(megaApi[0]->search(filteringInfo.get()));
    ASSERT_THAT(results, NotNull()) << "search() returned a nullptr";
    EXPECT_THAT(toNamesVector(*results), UnorderedElementsAreArray({"testFile1"}));

    // Two nodes match name but none matches description
    filteringInfo = getDefaultfilter();
    filteringInfo->useAndForTextQuery(true);
    filteringInfo->byName("testFile");
    filteringInfo->byDescription("Foo");

    results.reset(megaApi[0]->search(filteringInfo.get()));
    ASSERT_THAT(results, NotNull()) << "search() returned a nullptr";
    EXPECT_EQ(results->size(), 0);

    // No filters should return all
    filteringInfo = getDefaultfilter();
    filteringInfo->useAndForTextQuery(true);

    results.reset(megaApi[0]->search(filteringInfo.get()));
    ASSERT_THAT(results, NotNull()) << "search() returned a nullptr";
    EXPECT_THAT(toNamesVector(*results), UnorderedElementsAreArray(getAllNodesNames()));

    //// Combine by OR
    // Two nodes matching tag but only one matches description
    filteringInfo = getDefaultfilter();
    filteringInfo->useAndForTextQuery(false);
    filteringInfo->byTag("bar");
    filteringInfo->byDescription("test");

    results.reset(megaApi[0]->search(filteringInfo.get()));
    ASSERT_THAT(results, NotNull()) << "search() returned a nullptr";
    EXPECT_THAT(toNamesVector(*results), UnorderedElementsAreArray({"testFile1", "testFile2"}));

    // One node matches tag and other matches description
    filteringInfo = getDefaultfilter();
    filteringInfo->useAndForTextQuery(false);
    filteringInfo->byTag("testTag");
    filteringInfo->byDescription("test");

    results.reset(megaApi[0]->search(filteringInfo.get()));
    ASSERT_THAT(results, NotNull()) << "search() returned a nullptr";
    EXPECT_THAT(toNamesVector(*results), UnorderedElementsAreArray({"testFile1", "testFile2"}));

    // Two nodes match name, none matches description
    filteringInfo = getDefaultfilter();
    filteringInfo->useAndForTextQuery(false);
    filteringInfo->byName("testFile");
    filteringInfo->byDescription("Foo");

    results.reset(megaApi[0]->search(filteringInfo.get()));
    ASSERT_THAT(results, NotNull()) << "search() returned a nullptr";
    EXPECT_THAT(toNamesVector(*results), UnorderedElementsAreArray({"testFile1", "testFile2"}));

    // No filters should return all
    filteringInfo = getDefaultfilter();
    filteringInfo->useAndForTextQuery(false);

    results.reset(megaApi[0]->search(filteringInfo.get()));
    ASSERT_THAT(results, NotNull()) << "search() returned a nullptr";
    EXPECT_THAT(toNamesVector(*results), UnorderedElementsAreArray(getAllNodesNames()));
}
