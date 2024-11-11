/**
 * @file
 * @brief This header defines the SdkTestNodesSetUp fixtures to be used as base class for tests.
 */

#ifndef INCLUDE_INTEGRATION_SDKTESTNODESSETUP_H_
#define INCLUDE_INTEGRATION_SDKTESTNODESSETUP_H_

#include "sdk_test_utils.h"
#include "SdkTest_test.h"

/**
 * @class SdkTestNodesSetUp
 * @brief An abstract class that provides a template fixture/test suite to setup an account with a
 * certain node tree.
 *
 * @note This class is thought to only modify anything during the initialization and destruction,
 * i.e., only read and const operations are implemented once the object is being used.
 *
 * Child classes need to implement:
 *   - getRootTestDir(): Returns a string that will be the name of a directory that will be created
 *   inside which all the nodes will be created. This prevents collisions with other test suites.
 *   - getElements(): Returns a vector of NodeInfo object that will be used to create the nodes.
 *
 * Also, optionally:
 *   - keepDifferentCreationTimes()
 *
 */
class SdkTestNodesSetUp: public SdkTest
{
private:
    std::unique_ptr<MegaNode> rootTestDirNode;

public:
    virtual const std::string& getRootTestDir() const = 0;

    virtual const std::vector<sdk_test::NodeInfo>& getElements() const = 0;

    /**
     * @brief Determines if we should wait 1 second between node creation to keep different creation
     * times.
     *
     * Note: The base implementation returns true by default. Overload this method if you don't need
     * different creation times.
     *
     * @return true if we should wait, false otherwise.
     */
    virtual bool keepDifferentCreationTimes()
    {
        return true;
    }

    /**
     * @brief Give an path relative to the root node created for the tests (the one named with
     * getRootTestDir()), returns the fixed absolute path in the cloud.
     */
    std::string convertToTestPath(const std::string& path) const
    {
        return "/" + getRootTestDir() + "/" + path;
    }

    void SetUp() override;

    /**
     * @brief Get a vector with all the names of the nodes created inside the getRootTestDir()
     */
    std::vector<std::string> getAllNodesNames() const;

    /**
     * @brief Get a filter with the byLocationHandle set up properly to point to the root directory
     * for the tests suite (getRootTestDir())
     */
    std::unique_ptr<MegaSearchFilter> getDefaultfilter() const;

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

    /**
     * @brief Given the path relative to the root of the test dir, returns the MegaNode with that
     * path (if exists, nullptr otherwise)
     */
    std::unique_ptr<MegaNode> getNodeByPath(const std::string& path) const;

protected:
    /**
     * @brief Create the getRootTestDir() and sets store it internally
     */
    void createRootTestDir();

    /**
     * @brief Creates the file tree given by the vector of NodeInfo starting from the rootnode
     *
     * @param elements NodeInfo vector to create
     * @param rootnode A pointer to the root node
     */
    void createNodes(const std::vector<sdk_test::NodeInfo>& elements, MegaNode* rootnode);

    /**
     * @brief Creates a file node as a child of the rootnode using the input info.
     */
    void createNode(const sdk_test::FileNodeInfo& fileInfo, MegaNode* rootnode);

    /**
     * @brief Creates a directory node as a child of the rootnode using the input info.
     */
    void createNode(const sdk_test::DirNodeInfo& dirInfo, MegaNode* rootnode);

    /**
     * @brief Aux method to create a directory node with the given name inside the given rootnode
     *
     * NOTE: You must check that the output value is not a nullptr. If it is, there was a failure in
     * the creation.
     */
    std::unique_ptr<MegaNode> createRemoteDir(const std::string& dirName, MegaNode* rootnode);

    /**
     * @brief Sets special info such as fav, label, tags or description for a given node
     */
    template<typename Derived>
    void setNodeAdditionalAttributes(const sdk_test::NodeCommonInfo<Derived>& nodeInfo,
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

    void setNodeTag(const std::unique_ptr<MegaNode>& node, const std::string& tag);

    void setNodeDescription(const std::unique_ptr<MegaNode>& node, const std::string& description);
};

#endif // INCLUDE_INTEGRATION_SDKTESTNODESSETUP_H_
