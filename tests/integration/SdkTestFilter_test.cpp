/**
 * @file SdkTestFilter_test.cpp
 * @brief This file defines some tests that involve interactions with the MegaSearchFilter
 * object. This includes operations like:
 *   - Searching nodes with different filters
 *   - Ordering search results with different criteria
 *   - Apply different kinds of conditions in the filters
 */

#include "megaapi.h"
#include "megautils.h"
#include "sdk_test_utils.h"
#include "SdkTestNodesSetUp.h"

#include <gmock/gmock.h>

#include <chrono>
#include <optional>

using namespace std::chrono_literals;
using namespace sdk_test;

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
 * @brief SdkTestOrder.SdkGetVersions
 *
 * Tests if file versioning is properly working.
 */
TEST_F(SdkTestOrder, SdkGetVersions)
{
    MegaHandle fileHandle = 0;
    const auto remoteDir = "/SDK_TEST_ORDER_AUX_DIR";
    auto dirNode = megaApi[0]->getNodeByPath(remoteDir);
    const auto fileName = "testFile1";
    ASSERT_TRUE(createFile(fileName, false));
    const auto uploadVersions = 3;
    // First version is already uploaded during the setup.
    for (auto i = 1; i < uploadVersions; i++)
    {
        appendToFile(fileName, 20);
        ASSERT_EQ(MegaError::API_OK,
                  doStartUpload(0,
                                &fileHandle,
                                fileName,
                                dirNode,
                                nullptr /*fileName*/,
                                ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                nullptr /*appData*/,
                                false /*isSourceTemporary*/,
                                false /*startFirst*/,
                                nullptr /*cancelToken*/))
            << "Cannot upload " << fileName;

        ASSERT_NE(fileHandle, INVALID_HANDLE);
    }
    const auto fileNode(megaApi[0]->getNodeByHandle(fileHandle));
    ASSERT_TRUE(fileNode) << "Unable to retrieve the file handle";
    const auto versions(megaApi[0]->getVersions(fileNode));
    ASSERT_EQ(versions->size(), uploadVersions);
    const auto versionCount = megaApi[0]->getNumVersions(fileNode);
    ASSERT_EQ(versionCount, uploadVersions);
    auto session = dumpSession(0);
    locallogout(0);
    resumeSession(session, 0);
    fetchnodes(0);
    const auto remoteFileNode(megaApi[0]->getNodeByPath("/SDK_TEST_ORDER_AUX_DIR/testFile1"));
    ASSERT_TRUE(remoteFileNode) << "Unable to retrieve the remote file handle";
    const auto versionsAfterResume(megaApi[0]->getVersions(remoteFileNode));
    ASSERT_EQ(versionsAfterResume->size(), uploadVersions);
    const auto versionCountAfterResume = megaApi[0]->getNumVersions(remoteFileNode);
    ASSERT_EQ(versionCountAfterResume, uploadVersions);
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
