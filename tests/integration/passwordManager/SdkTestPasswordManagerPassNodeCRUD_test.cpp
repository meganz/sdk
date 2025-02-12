/**
 * @file
 * @brief Test CRUD operation on password nodes (not folders)
 */

#include "mock_listeners.h"
#include "SdkTestPasswordManager.h"

#include <gmock/gmock.h>

using namespace testing;

/**
 * @brief Helper matcher to compare two const char* (including nullptr check)
 */
MATCHER_P(CCharEq, expected, "const char* is eq to " + std::string{expected ? expected : "nullptr"})
{
    if (expected == nullptr || arg == nullptr)
        return expected == arg;
    return ExplainMatchResult(StrEq(expected), arg, result_listener);
}

/**
 * @brief Helper matcher for PasswordNodeData pointers
 */
MATCHER_P(PasswordNodeDataEquals, expected, "Matches PasswordNodeData object")
{
    if (expected == nullptr || arg == nullptr)
    {
        *result_listener << "Expected: " << (expected ? "non-null" : "nullptr")
                         << ", but got: " << (arg ? "non-null" : "nullptr");
        return expected == arg;
    }

    const auto check = [&result_listener](const char* exp,
                                          const char* val,
                                          const std::string_view fieldName) -> bool
    {
        if (!ExplainMatchResult(CCharEq(exp), val, result_listener))
        {
            *result_listener << "\nMismatch in field '" << fieldName << "': expected ["
                             << (exp ? exp : "nullptr") << "], but got [" << (val ? val : "nullptr")
                             << "]";
            return false;
        }
        return true;
    };

    bool eq = true;
    eq &= check(expected->password(), arg->password(), "password");
    eq &= check(expected->notes(), arg->notes(), "notes");
    eq &= check(expected->url(), arg->url(), "url");
    eq &= check(expected->userName(), arg->userName(), "userName");
    return eq;
}

class SdkTestPasswordManagerPassNodeCRUD: public SdkTestPasswordManager
{
public:
    handle createPasswordNode(const std::string& name = {},
                              const MegaNode::PasswordNodeData* data = nullptr)
    {
        const auto nameFinal = name.empty() ? getFilePrefix() : name;
        const auto* dataFinal = data ? data : predefinedPwdDataOwned();
        return sdk_test::createPasswordNode(mApi, nameFinal, dataFinal, getBaseHandle());
    }

    /**
     * @brief Returns a pointer to a default password data owned by the fixture
     */
    const MegaNode::PasswordNodeData* predefinedPwdDataOwned() const
    {
        static const auto defaultData{predefinedPwdData()};
        return defaultData.get();
    }

    std::unique_ptr<MegaNode::PasswordNodeData> predefinedPwdData() const
    {
        return std::unique_ptr<MegaNode::PasswordNodeData>{
            MegaNode::PasswordNodeData::createInstance("12},\" '34",
                                                       "notes",
                                                       "url",
                                                       "userName",
                                                       nullptr)};
    }

    std::unique_ptr<MegaNode::PasswordNodeData> emptyPwdData() const
    {
        return std::unique_ptr<MegaNode::PasswordNodeData>{
            MegaNode::PasswordNodeData::createInstance(nullptr,
                                                       nullptr,
                                                       nullptr,
                                                       nullptr,
                                                       nullptr)};
    }

    void updatePwdNode(const handle nh, const MegaNode::PasswordNodeData* data)
    {
        NiceMock<MockRequestListener> rl;
        rl.setErrorExpectations(API_OK, _, MegaRequest::TYPE_UPDATE_PASSWORD_NODE);
        mApi->updatePasswordNode(nh, data, &rl);
        ASSERT_TRUE(rl.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    /**
     * @brief Checks that the password data of the node with the given handle matches the one
     * provided
     */
    void validatePwdNodeData(const handle nh, const MegaNode::PasswordNodeData* data)
    {
        std::unique_ptr<MegaNode> retrievedNode{mApi->getNodeByHandle(nh)};
        ASSERT_TRUE(retrievedNode);
        ASSERT_TRUE(retrievedNode->isPasswordNode());
        std::unique_ptr<MegaNode::PasswordNodeData> retrievedData{retrievedNode->getPasswordData()};
        ASSERT_THAT(retrievedData.get(), PasswordNodeDataEquals(data));
    }
};

TEST_F(SdkTestPasswordManagerPassNodeCRUD, CreateNewPassNode)
{
    static const auto logPre = getLogPrefix();

    const auto mnBase = getBaseNode();
    ASSERT_NE(mnBase, nullptr);

    LOG_debug << logPre << "Checking node is not present already";
    const auto pwdNodeName = getFilePrefix();
    std::unique_ptr<MegaNode> node{mApi->getChildNode(mnBase.get(), pwdNodeName.c_str())};
    ASSERT_EQ(node, nullptr) << "There was already a password node with the name " << pwdNodeName
                             << ". We can't test node creation";

    LOG_debug << logPre << "Creating new node";
    const auto newPwdNodeHandle = createPasswordNode(pwdNodeName);

    LOG_debug << logPre << "Validating new node";
    ASSERT_NE(newPwdNodeHandle, UNDEF);
    const std::unique_ptr<MegaNode> newPwdNode{mApi->getNodeByHandle(newPwdNodeHandle)};
    ASSERT_NE(newPwdNode, nullptr) << "New node could not be retrieved";
    ASSERT_TRUE(newPwdNode->isPasswordNode());
    ASSERT_FALSE(mApi->isPasswordNodeFolder(newPwdNode->getHandle()));

    LOG_debug << logPre << "Validating node name and data";
    EXPECT_STREQ(newPwdNode->getName(), pwdNodeName.c_str());
    std::unique_ptr<MegaNode::PasswordNodeData> receivedPwdData{newPwdNode->getPasswordData()};
    EXPECT_THAT(receivedPwdData.get(), PasswordNodeDataEquals(predefinedPwdDataOwned()));
}

TEST_F(SdkTestPasswordManagerPassNodeCRUD, CopyPassNode)
{
    static const auto logPre = getLogPrefix();

    LOG_debug << logPre << "Creating new node to be cloned";
    const auto pwdNodeName = getFilePrefix();
    const auto newPwdNodeHandle = createPasswordNode(pwdNodeName);
    ASSERT_NE(newPwdNodeHandle, UNDEF);
    const std::unique_ptr<MegaNode> newPwdNode{mApi->getNodeByHandle(newPwdNodeHandle)};
    ASSERT_NE(newPwdNode, nullptr) << "New node could not be retrieved";

    LOG_debug << logPre << "Clonning the node";
    std::unique_ptr<MegaNode> clonedNode{newPwdNode->copy()};
    std::unique_ptr<MegaNode::PasswordNodeData> clonedPwdData{clonedNode->getPasswordData()};

    LOG_debug << logPre << "Validating cloned node";
    ASSERT_TRUE(clonedNode->isPasswordNode());
    ASSERT_FALSE(mApi->isPasswordNodeFolder(clonedNode->getHandle()));

    EXPECT_STREQ(clonedNode->getName(), pwdNodeName.c_str());
    EXPECT_THAT(clonedPwdData.get(), PasswordNodeDataEquals(predefinedPwdDataOwned()));
}

TEST_F(SdkTestPasswordManagerPassNodeCRUD, CreateErrorSameName)
{
    static const auto logPre = getLogPrefix();
    LOG_debug << logPre << "Creating a node";
    const auto pwdNodeName = getFilePrefix();
    const auto newPwdNodeHandle = createPasswordNode(pwdNodeName);
    ASSERT_NE(newPwdNodeHandle, UNDEF);

    LOG_debug << logPre << "Expecting error when creating a node with the same name";
    NiceMock<MockRequestListener> rl;
    rl.setErrorExpectations(API_EEXIST, _, MegaRequest::TYPE_CREATE_PASSWORD_NODE);
    mApi->createPasswordNode(pwdNodeName.c_str(), predefinedPwdDataOwned(), getBaseHandle(), &rl);
    EXPECT_TRUE(rl.waitForFinishOrTimeout(MAX_TIMEOUT));
}

TEST_F(SdkTestPasswordManagerPassNodeCRUD, CreateErrorArgs)
{
    static const auto logPre = getLogPrefix();
    LOG_debug << logPre << "Creating a node with invalid arguments, expecting API_EARGS";
    NiceMock<MockRequestListener> rl;
    rl.setErrorExpectations(API_EARGS, _, MegaRequest::TYPE_CREATE_PASSWORD_NODE);
    mApi->createPasswordNode(nullptr, nullptr, INVALID_HANDLE, &rl);
    EXPECT_TRUE(rl.waitForFinishOrTimeout(MAX_TIMEOUT));
}

TEST_F(SdkTestPasswordManagerPassNodeCRUD, GetPassNodeByHandle)
{
    static const auto logPre = getLogPrefix();
    LOG_debug << logPre << "Creating a node";
    const auto newPwdNodeHandle = createPasswordNode();
    ASSERT_NE(newPwdNodeHandle, UNDEF);

    LOG_debug << logPre << "Getting node by handle";
    const std::unique_ptr<MegaNode> retrievedPwdNode{mApi->getNodeByHandle(newPwdNodeHandle)};
    ASSERT_NE(retrievedPwdNode, nullptr);
    ASSERT_EQ(retrievedPwdNode->getHandle(), newPwdNodeHandle);
}

TEST_F(SdkTestPasswordManagerPassNodeCRUD, UpdateRenameNode)
{
    static const auto logPre = getLogPrefix();
    LOG_debug << logPre << "Creating a node";
    const auto newPwdNodeHandle = createPasswordNode();
    ASSERT_NE(newPwdNodeHandle, UNDEF);
    std::unique_ptr<MegaNode> newPwdNode{mApi->getNodeByHandle(newPwdNodeHandle)};
    ASSERT_NE(newPwdNode, nullptr);

    LOG_debug << logPre << "Renaming the node";
    const char* newName = "SecondPwd";
    ASSERT_EQ(API_OK, doRenameNode(0, newPwdNode.get(), newName));

    LOG_debug << logPre << "Validating node new name and data";
    newPwdNode.reset(mApi->getNodeByHandle(newPwdNodeHandle));
    ASSERT_NE(nullptr, newPwdNode.get());

    ASSERT_TRUE(newPwdNode->isPasswordNode());
    ASSERT_STREQ(newName, newPwdNode->getName());
    std::unique_ptr<MegaNode::PasswordNodeData> pwdData{newPwdNode->getPasswordData()};
    EXPECT_THAT(pwdData.get(), PasswordNodeDataEquals(predefinedPwdDataOwned()));
}

TEST_F(SdkTestPasswordManagerPassNodeCRUD, UpdateChangeJustPwdFromSameData)
{
    static const auto logPre = getLogPrefix();
    LOG_debug << logPre << "Creating a node";
    const auto newPwdNodeHandle = createPasswordNode();
    ASSERT_NE(newPwdNodeHandle, UNDEF);

    LOG_debug << logPre << "Updating just the password";
    const auto pwdData = predefinedPwdData();
    pwdData->setPassword("5678");
    updatePwdNode(newPwdNodeHandle, pwdData.get());

    LOG_debug << logPre << "Validating data";
    validatePwdNodeData(newPwdNodeHandle, pwdData.get());
}

TEST_F(SdkTestPasswordManagerPassNodeCRUD, UpdateChangeJustNotesFromEmtpyData)
{
    static const auto logPre = getLogPrefix();
    LOG_debug << logPre << "Creating a node";
    const auto newPwdNodeHandle = createPasswordNode();
    ASSERT_NE(newPwdNodeHandle, UNDEF);

    LOG_debug << logPre << "Updating just the notes";
    const auto pwdData = emptyPwdData();
    pwdData->setNotes("Updated Notes");
    updatePwdNode(newPwdNodeHandle, pwdData.get());

    LOG_debug << logPre << "Validating data";
    const auto pwdDataToCompare = predefinedPwdData();
    pwdDataToCompare->setNotes(pwdData->notes());
    validatePwdNodeData(newPwdNodeHandle, pwdDataToCompare.get());
}

TEST_F(SdkTestPasswordManagerPassNodeCRUD, UpdateChangeJustURLFromEmtpyData)
{
    static const auto logPre = getLogPrefix();
    LOG_debug << logPre << "Creating a node";
    const auto newPwdNodeHandle = createPasswordNode();
    ASSERT_NE(newPwdNodeHandle, UNDEF);

    LOG_debug << logPre << "Updating just the url";
    const auto pwdData = emptyPwdData();
    pwdData->setUrl("Updated url");
    updatePwdNode(newPwdNodeHandle, pwdData.get());

    LOG_debug << logPre << "Validating data";
    const auto pwdDataToCompare = predefinedPwdData();
    pwdDataToCompare->setUrl(pwdData->url());
    validatePwdNodeData(newPwdNodeHandle, pwdDataToCompare.get());
}

TEST_F(SdkTestPasswordManagerPassNodeCRUD, UpdateChangeJustUserNameFromEmtpyData)
{
    static const auto logPre = getLogPrefix();
    LOG_debug << logPre << "Creating a node";
    const auto newPwdNodeHandle = createPasswordNode();
    ASSERT_NE(newPwdNodeHandle, UNDEF);

    LOG_debug << logPre << "Updating just the userName";
    const auto pwdData = emptyPwdData();
    pwdData->setUserName("Updated userName");
    updatePwdNode(newPwdNodeHandle, pwdData.get());

    LOG_debug << logPre << "Validating data";
    const auto pwdDataToCompare = predefinedPwdData();
    pwdDataToCompare->setUserName(pwdData->userName());
    validatePwdNodeData(newPwdNodeHandle, pwdDataToCompare.get());
}

TEST_F(SdkTestPasswordManagerPassNodeCRUD, UpdateErrorNoData)
{
    static const auto logPre = getLogPrefix();
    LOG_debug << logPre << "Creating a node";
    const auto newPwdNodeHandle = createPasswordNode();
    ASSERT_NE(newPwdNodeHandle, UNDEF);

    LOG_debug << logPre << "Updating with invalid data";
    NiceMock<MockRequestListener> rl;
    rl.setErrorExpectations(API_EARGS, _, MegaRequest::TYPE_UPDATE_PASSWORD_NODE);
    mApi->updatePasswordNode(newPwdNodeHandle, nullptr, &rl);
    ASSERT_TRUE(rl.waitForFinishOrTimeout(MAX_TIMEOUT));
}

TEST_F(SdkTestPasswordManagerPassNodeCRUD, UpdateErrorEptyData)
{
    static const auto logPre = getLogPrefix();
    LOG_debug << logPre << "Creating a node";
    const auto newPwdNodeHandle = createPasswordNode();
    ASSERT_NE(newPwdNodeHandle, UNDEF);

    LOG_debug << logPre << "Updating with empty data";
    const auto emptyData = emptyPwdData();
    NiceMock<MockRequestListener> rl;
    rl.setErrorExpectations(API_EARGS, _, MegaRequest::TYPE_UPDATE_PASSWORD_NODE);
    mApi->updatePasswordNode(newPwdNodeHandle, emptyData.get(), &rl);
    ASSERT_TRUE(rl.waitForFinishOrTimeout(MAX_TIMEOUT));
}

TEST_F(SdkTestPasswordManagerPassNodeCRUD, DeletePwdNode)
{
    static const auto logPre = getLogPrefix();
    LOG_debug << logPre << "Creating a node";
    const auto newPwdNodeHandle = createPasswordNode();
    ASSERT_NE(newPwdNodeHandle, UNDEF);
    std::unique_ptr<MegaNode> retrievedPwdNode{mApi->getNodeByHandle(newPwdNodeHandle)};
    ASSERT_NE(retrievedPwdNode, nullptr);

    LOG_debug << logPre << "Deleting the node";
    ASSERT_EQ(API_OK, doDeleteNode(0, retrievedPwdNode.get()));
    retrievedPwdNode.reset(mApi->getNodeByHandle(newPwdNodeHandle));
    ASSERT_EQ(nullptr, retrievedPwdNode.get());
}
