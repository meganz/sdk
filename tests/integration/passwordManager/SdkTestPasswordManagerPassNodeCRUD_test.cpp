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
 * @brief Helper matcher for TotpData
 */
MATCHER_P(TotpDataEquals, expected, "Matches TotpData object")
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
    eq &= check(expected->sharedSecret(), arg->sharedSecret(), "shared secret");
    eq &=
        ExplainMatchResult(Eq(expected->expirationTime()), arg->expirationTime(), result_listener);
    eq &= ExplainMatchResult(Eq(expected->hashAlgorithm()), arg->hashAlgorithm(), result_listener);
    eq &= ExplainMatchResult(Eq(expected->nDigits()), arg->nDigits(), result_listener);
    return eq;
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
    eq &=
        ExplainMatchResult(TotpDataEquals(expected->totpData()), arg->totpData(), result_listener);
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

    std::unique_ptr<MegaNode::PasswordNodeData::TotpData> predefinedPwdTotpData() const
    {
        using TotpData = MegaNode::PasswordNodeData::TotpData;
        std::unique_ptr<TotpData> totpData{
            TotpData::createInstance("abcd", 20, TotpData::HASH_ALGO_SHA256, 8)};
        return totpData;
    }

    std::unique_ptr<MegaNode::PasswordNodeData::TotpData> emptyPwdTotpData() const
    {
        using TotpData = MegaNode::PasswordNodeData::TotpData;
        std::unique_ptr<TotpData> totpData{TotpData::createInstance(nullptr,
                                                                    TotpData::TOTPNULLOPT,
                                                                    TotpData::TOTPNULLOPT,
                                                                    TotpData::TOTPNULLOPT)};
        return totpData;
    }

    std::unique_ptr<MegaNode::PasswordNodeData> predefinedPwdData() const
    {
        auto totpData = predefinedPwdTotpData();
        return std::unique_ptr<MegaNode::PasswordNodeData>{
            MegaNode::PasswordNodeData::createInstance("12},\" '34",
                                                       "notes",
                                                       "url",
                                                       "userName",
                                                       totpData.get())};
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

    std::unique_ptr<MegaNode::PasswordNodeData>
        getCustomTotpData(std::unique_ptr<MegaNode::PasswordNodeData>&& pwdData,
                          std::function<void(MegaNode::PasswordNodeData::TotpData&)> modifyTotpData)
    {
        std::unique_ptr<MegaNode::PasswordNodeData::TotpData> totpData;
        if (auto auxTotpData = pwdData->totpData(); !auxTotpData)
        {
            totpData = emptyPwdTotpData();
        }
        else
        {
            totpData.reset(auxTotpData->copy());
        }

        modifyTotpData(*totpData);
        pwdData->setTotpData(totpData.get());
        return std::move(pwdData);
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

TEST_F(SdkTestPasswordManagerPassNodeCRUD, UpdateErrorEmptyData)
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

/**
 * @brief Ensures that TotpData must be valid (shared secret must be present) to add totp field on a
 * password node that had no totp information stored.
 */
TEST_F(SdkTestPasswordManagerPassNodeCRUD, UpdateTotpDataFromNullError)
{
    static const auto logPre = getLogPrefix();
    LOG_debug << logPre << "Creating a node with no totp data";
    auto pwdData = predefinedPwdData();
    pwdData->setTotpData(nullptr);
    const auto newPwdNodeHandle = createPasswordNode({}, pwdData.get());

    LOG_debug << logPre << "Preparing totp data with no shared secret";
    pwdData = emptyPwdData();
    const auto totpData = predefinedPwdTotpData();
    totpData->setSharedSecret(nullptr);
    pwdData->setTotpData(totpData.get());

    LOG_debug << logPre << "Update node expecting an error";
    NiceMock<MockRequestListener> rl;
    rl.setErrorExpectations(API_EAPPKEY, _, MegaRequest::TYPE_UPDATE_PASSWORD_NODE);
    mApi->updatePasswordNode(newPwdNodeHandle, pwdData.get(), &rl);
    ASSERT_TRUE(rl.waitForFinishOrTimeout(MAX_TIMEOUT));
}

/**
 * @brief Same operation as UpdateTotpDataFromNullError but passing valid totp data
 */
TEST_F(SdkTestPasswordManagerPassNodeCRUD, UpdateTotpDataFromNullOk)
{
    static const auto logPre = getLogPrefix();
    LOG_debug << logPre << "Creating a node with no totp data";
    auto pwdData = predefinedPwdData();
    pwdData->setTotpData(nullptr);
    const auto newPwdNodeHandle = createPasswordNode({}, pwdData.get());

    LOG_debug << logPre << "Preparing valid totp data with no shared secret";
    pwdData = emptyPwdData();
    const auto totpData = predefinedPwdTotpData();
    pwdData->setTotpData(totpData.get());

    LOG_debug << logPre << "Update node";
    ASSERT_NO_FATAL_FAILURE(updatePwdNode(newPwdNodeHandle, pwdData.get()));
}

TEST_F(SdkTestPasswordManagerPassNodeCRUD, UpdateTotpDataNDigitsFromSameData)
{
    static const auto logPre = getLogPrefix();
    LOG_debug << logPre << "Creating a node";
    const auto newPwdNodeHandle = createPasswordNode();
    ASSERT_NE(newPwdNodeHandle, UNDEF);

    LOG_debug << logPre << "Updating just the Ndigits";
    const auto pwdData = getCustomTotpData(predefinedPwdData(),
                                           [](auto& totpData)
                                           {
                                               totpData.setNdigits(9);
                                           });

    updatePwdNode(newPwdNodeHandle, pwdData.get());

    LOG_debug << logPre << "Validating data";
    validatePwdNodeData(newPwdNodeHandle, pwdData.get());
}

TEST_F(SdkTestPasswordManagerPassNodeCRUD, UpdateTotpDataAlgorithmFromEmptyData)
{
    static const auto logPre = getLogPrefix();
    LOG_debug << logPre << "Creating a node";
    const auto newPwdNodeHandle = createPasswordNode();
    ASSERT_NE(newPwdNodeHandle, UNDEF);

    LOG_debug << logPre << "Updating just the Ndigits";
    const auto pwdData = getCustomTotpData(
        emptyPwdData(),
        [](auto& totpData)
        {
            totpData.setHashAlgorithm(MegaNode::PasswordNodeData::TotpData::HASH_ALGO_SHA512);
        });

    ASSERT_NO_FATAL_FAILURE(updatePwdNode(newPwdNodeHandle, pwdData.get()));

    LOG_debug << logPre << "Validating data";
    const auto pwdDataPred = getCustomTotpData(
        predefinedPwdData(),
        [](auto& totpData)
        {
            totpData.setHashAlgorithm(MegaNode::PasswordNodeData::TotpData::HASH_ALGO_SHA512);
        });
    validatePwdNodeData(newPwdNodeHandle, pwdDataPred.get());
}

TEST_F(SdkTestPasswordManagerPassNodeCRUD, UpdateTotpDataExptFromEmptyData)
{
    static const auto logPre = getLogPrefix();
    LOG_debug << logPre << "Creating a node";
    const auto newPwdNodeHandle = createPasswordNode();
    ASSERT_NE(newPwdNodeHandle, UNDEF);

    LOG_debug << logPre << "Updating just the Expt";
    const auto pwdData = getCustomTotpData(emptyPwdData(),
                                           [](auto& totpData)
                                           {
                                               totpData.setExpirationTime(120);
                                           });

    ASSERT_NO_FATAL_FAILURE(updatePwdNode(newPwdNodeHandle, pwdData.get()));

    LOG_debug << logPre << "Validating data";
    const auto pwdDataPred = getCustomTotpData(predefinedPwdData(),
                                               [](auto& totpData)
                                               {
                                                   totpData.setExpirationTime(120);
                                               });
    validatePwdNodeData(newPwdNodeHandle, pwdDataPred.get());
}

TEST_F(SdkTestPasswordManagerPassNodeCRUD, UpdateTotpDataShseFromEmptyData)
{
    static const auto logPre = getLogPrefix();
    LOG_debug << logPre << "Creating a node";
    const auto newPwdNodeHandle = createPasswordNode();
    ASSERT_NE(newPwdNodeHandle, UNDEF);

    LOG_debug << logPre << "Updating just the Shared secret";
    const auto pwdData = getCustomTotpData(emptyPwdData(),
                                           [](auto& totpData)
                                           {
                                               totpData.setSharedSecret("3456");
                                           });

    ASSERT_NO_FATAL_FAILURE(updatePwdNode(newPwdNodeHandle, pwdData.get()));

    LOG_debug << logPre << "Validating data";
    const auto pwdDataPred = getCustomTotpData(predefinedPwdData(),
                                               [](auto& totpData)
                                               {
                                                   totpData.setSharedSecret("3456");
                                               });
    validatePwdNodeData(newPwdNodeHandle, pwdDataPred.get());
}

TEST_F(SdkTestPasswordManagerPassNodeCRUD, UpdateTotpDataWithWrongValues)
{
    static const auto logPre = getLogPrefix();
    LOG_debug << logPre << "Creating a node";
    const auto newPwdNodeHandle = createPasswordNode();
    ASSERT_NE(newPwdNodeHandle, UNDEF);

    LOG_debug << logPre << "Updating just the Shared secret";
    auto pwdData = getCustomTotpData(emptyPwdData(),
                                     [](auto& totpData)
                                     {
                                         totpData.setSharedSecret("1234");
                                     });

    auto expectFailureOnPwdDataUpdate = [this, &pwdData, &newPwdNodeHandle]()
    {
        NiceMock<MockRequestListener> rl;
        rl.setErrorExpectations(API_EAPPKEY, _, MegaRequest::TYPE_UPDATE_PASSWORD_NODE);
        mApi->updatePasswordNode(newPwdNodeHandle, pwdData.get(), &rl);
        ASSERT_TRUE(rl.waitForFinishOrTimeout(MAX_TIMEOUT));
    };
    EXPECT_NO_FATAL_FAILURE(expectFailureOnPwdDataUpdate());

    LOG_debug << logPre << "#### Test1: Update Totpdata with invalid Shared secret";
    pwdData = getCustomTotpData(emptyPwdData(),
                                [](auto& totpData)
                                {
                                    totpData.setSharedSecret("5=34");
                                });
    EXPECT_NO_FATAL_FAILURE(expectFailureOnPwdDataUpdate());

    LOG_debug << logPre << "#### Test2: Update Totpdata with invalid Ndigits";
    pwdData = getCustomTotpData(emptyPwdData(),
                                [](auto& totpData)
                                {
                                    totpData.setNdigits(40);
                                });
    EXPECT_NO_FATAL_FAILURE(expectFailureOnPwdDataUpdate());

    LOG_debug << logPre << "#### Test3: Update Totpdata with Ndigits equal Zero";
    pwdData = getCustomTotpData(emptyPwdData(),
                                [](auto& totpData)
                                {
                                    totpData.setNdigits(0);
                                });
    EXPECT_NO_FATAL_FAILURE(expectFailureOnPwdDataUpdate());

    LOG_debug << logPre << "#### Test4: Update Totpdata with expiration time equal Zero";
    pwdData = getCustomTotpData(emptyPwdData(),
                                [](auto& totpData)
                                {
                                    totpData.setExpirationTime(0);
                                });
    EXPECT_NO_FATAL_FAILURE(expectFailureOnPwdDataUpdate());

    LOG_debug << logPre << "#### Test4: Update Totpdata with invalid hash algorithm";
    pwdData = getCustomTotpData(emptyPwdData(),
                                [](auto& totpData)
                                {
                                    totpData.setHashAlgorithm(100);
                                });
    EXPECT_NO_FATAL_FAILURE(expectFailureOnPwdDataUpdate());
}

TEST_F(SdkTestPasswordManagerPassNodeCRUD, DeleteTotpData)
{
    static const auto logPre = getLogPrefix();
    LOG_debug << logPre << "Creating a node";
    const auto newPwdNodeHandle = createPasswordNode();
    ASSERT_NE(newPwdNodeHandle, UNDEF);

    LOG_debug << logPre << "Remove Totp data";
    using TotpData = MegaNode::PasswordNodeData::TotpData;
    std::unique_ptr<TotpData> totpData{TotpData::createRemovalInstance()};

    const auto pwdData = emptyPwdData();
    pwdData->setTotpData(totpData.get());
    ASSERT_NO_FATAL_FAILURE(updatePwdNode(newPwdNodeHandle, pwdData.get()));

    const auto predPwdData = predefinedPwdData();
    predPwdData->setTotpData(nullptr);

    validatePwdNodeData(newPwdNodeHandle, predPwdData.get());
}

TEST(SdkTestPasswordManagerTotpValidation, ValidateTotpFields)
{
    static const auto logPre = "SdkTestPasswordManagerTotpValidation::ValidateTotpFields: ";
    using TotpData = MegaNode::PasswordNodeData::TotpData;
    std::unique_ptr<TotpData> totpData{
        TotpData::createInstance("abcd", 20, TotpData::HASH_ALGO_SHA256, 8)};
    ASSERT_TRUE(totpData) << "Cannot create TotpData instance";

    LOG_debug << logPre << "#### Test1 Validate Totp data with all valid fields";
    std::unique_ptr<TotpData::Validation> val(totpData->getValidation());
    ASSERT_TRUE(val) << "Cannot get TotpData validation";
    EXPECT_TRUE(val->isValidForCreate());
    EXPECT_TRUE(val->isValidForUpdate());

    LOG_debug << logPre << "#### Test2 Validate Totp data with all field wrong";
    totpData.reset(TotpData::createInstance("1234", 0, 100, 5));
    val.reset(totpData->getValidation());
    ASSERT_TRUE(val) << "Cannot get TotpData validation";
    EXPECT_TRUE(!val->isValidForCreate());
    EXPECT_TRUE(!val->isValidForUpdate());
    EXPECT_TRUE(!val->sharedSecretValid());
    EXPECT_TRUE(!val->algorithmValid());
    EXPECT_TRUE(!val->nDigitsValid());
    EXPECT_TRUE(!val->expirationTimeValid());

    LOG_debug << logPre
              << "#### Test3 Validate Totp data with valid fields for update but not for creation";
    totpData.reset(TotpData::createInstance(nullptr, 10, -1, 6));
    val.reset(totpData->getValidation());
    ASSERT_TRUE(val) << "Cannot get TotpData validation";
    EXPECT_TRUE(!val->isValidForCreate());
    EXPECT_TRUE(val->isValidForUpdate());
    EXPECT_TRUE(val->sharedSecretValid());
    EXPECT_TRUE(val->algorithmValid());
    EXPECT_TRUE(val->nDigitsValid());
    EXPECT_TRUE(val->expirationTimeValid());

    LOG_debug << logPre << "#### Test4 Validate Totp data with wrong NDigits";
    totpData.reset(TotpData::createInstance("abcd", 20, TotpData::HASH_ALGO_SHA256, 11));
    val.reset(totpData->getValidation());
    ASSERT_TRUE(val) << "Cannot get TotpData validation";
    EXPECT_TRUE(!val->isValidForCreate());
    EXPECT_TRUE(!val->isValidForUpdate());
    EXPECT_TRUE(val->sharedSecretValid());
    EXPECT_TRUE(val->algorithmValid());
    EXPECT_TRUE(val->expirationTimeValid());
    EXPECT_TRUE(!val->nDigitsValid());
}
