/**
 * @file
 * @brief Test CRUD operation on credit card nodes
 */

#include "mega/totp.h"
#include "mock_listeners.h"
#include "SdkTestPasswordManager.h"

#include <gmock/gmock.h>

using namespace testing;

// Convenience
using CreditCardNodeData = MegaNode::CreditCardNodeData;

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
 * @brief Helper matcher for CreditCardNodeData pointers
 */
MATCHER_P(CreditCardNodeDataEquals, expected, "Matches CreditCardNodeData object")
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
    eq &= check(expected->cardNumber(), arg->cardNumber(), "cardNumber");
    eq &= check(expected->notes(), arg->notes(), "notes");
    eq &= check(expected->cardHolderName(), arg->cardHolderName(), "cardHolderName");
    eq &= check(expected->cvv(), arg->cvv(), "cvv");
    eq &= check(expected->expirationDate(), arg->expirationDate(), "expirationDate");
    return eq;
}

class SdkTestPasswordManagerCreditCardNodeCRUD: public SdkTestPasswordManager
{
public:
    handle createCreditCardNode(const std::string& name = {},
                                const CreditCardNodeData* data = nullptr)
    {
        const auto nameFinal = name.empty() ? getFilePrefix() : name;
        const auto* dataFinal = data ? data : predefinedCreditCardDataOwned();
        return sdk_test::createCreditCardNode(mApi, nameFinal, dataFinal, getBaseHandle());
    }

    handle createDummyPasswordNode()
    {
        auto pwdData = std::unique_ptr<MegaNode::PasswordNodeData>{
            MegaNode::PasswordNodeData::createInstance("password",
                                                       nullptr,
                                                       nullptr,
                                                       nullptr,
                                                       nullptr)};

        return sdk_test::createPasswordNode(mApi,
                                            getFilePrefix().c_str(),
                                            pwdData.get(),
                                            getBaseHandle());
    }

    /**
     * @brief Returns a pointer to a default password data owned by the fixture
     */
    const CreditCardNodeData* predefinedCreditCardDataOwned() const
    {
        static const auto defaultData{predefinedCreditCardData()};
        return defaultData.get();
    }

    std::unique_ptr<CreditCardNodeData> predefinedCreditCardData() const
    {
        return std::unique_ptr<CreditCardNodeData>{
            CreditCardNodeData::createInstance("123456789",
                                               "notes",
                                               "TEST CARD HOLDER NAME",
                                               "123",
                                               "02/24")};
    }

    std::unique_ptr<CreditCardNodeData> emptyCreditCardData() const
    {
        return std::unique_ptr<CreditCardNodeData>{
            CreditCardNodeData::createInstance(nullptr, nullptr, nullptr, nullptr, nullptr)};
    }

    void updateCreditCardNode(const handle nh, const CreditCardNodeData* data)
    {
        NiceMock<MockRequestListener> rl;
        rl.setErrorExpectations(API_OK, _, MegaRequest::TYPE_UPDATE_PASSWORD_NODE);
        mApi->updateCreditCardNode(nh, data, &rl);
        ASSERT_TRUE(rl.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    /**
     * @brief Checks that the password data of the node with the given handle matches the one
     * provided
     */
    void validateCreditCardNodeData(const handle nh, const CreditCardNodeData* data)
    {
        std::unique_ptr<MegaNode> retrievedNode{mApi->getNodeByHandle(nh)};
        ASSERT_TRUE(retrievedNode);
        ASSERT_TRUE(retrievedNode->isCreditCardNode());
        std::unique_ptr<CreditCardNodeData> retrievedData{retrievedNode->getCreditCardData()};
        ASSERT_THAT(retrievedData.get(), CreditCardNodeDataEquals(data));
    }
};

TEST_F(SdkTestPasswordManagerCreditCardNodeCRUD, CreateNewCreditCardNode)
{
    static const auto logPre = getLogPrefix();

    const auto mnBase = getBaseNode();
    ASSERT_NE(mnBase, nullptr);

    LOG_debug << logPre << "Checking node is not present already";
    const auto creditCardNodeName = getFilePrefix();
    std::unique_ptr<MegaNode> node{mApi->getChildNode(mnBase.get(), creditCardNodeName.c_str())};
    ASSERT_EQ(node, nullptr) << "There was already a password manager node with the name "
                             << creditCardNodeName << ". We can't test node creation";

    LOG_debug << logPre << "Creating new node";
    const auto newCreditCardNodeHandle = createCreditCardNode(creditCardNodeName);

    LOG_debug << logPre << "Validating new node";
    ASSERT_NE(newCreditCardNodeHandle, UNDEF);
    const std::unique_ptr<MegaNode> newCcNode{mApi->getNodeByHandle(newCreditCardNodeHandle)};
    ASSERT_NE(newCcNode, nullptr) << "New node could not be retrieved";
    ASSERT_TRUE(newCcNode->isPasswordManagerNode());
    ASSERT_TRUE(newCcNode->isCreditCardNode());
    ASSERT_FALSE(mApi->isPasswordManagerNodeFolder(newCcNode->getHandle()));

    LOG_debug << logPre << "Validating node name and data";
    EXPECT_STREQ(newCcNode->getName(), creditCardNodeName.c_str());
    std::unique_ptr<CreditCardNodeData> receivedCreditCardData{newCcNode->getCreditCardData()};
    EXPECT_THAT(receivedCreditCardData.get(),
                CreditCardNodeDataEquals(predefinedCreditCardDataOwned()));
}

TEST_F(SdkTestPasswordManagerCreditCardNodeCRUD, CreateNewCreditCardNodeWithEmptyField)
{
    static const auto logPre = getLogPrefix();

    auto ccData = emptyCreditCardData();
    ccData->setCardNumber("123456789");
    ccData->setNotes("");

    LOG_debug << logPre << "Creating new Credit Card Node";
    const auto newCcNodeHandle = createCreditCardNode({}, ccData.get());

    LOG_debug << logPre << "Getting created Credit Card Node";
    ASSERT_NE(newCcNodeHandle, UNDEF);
    const std::unique_ptr<MegaNode> newCcNode{mApi->getNodeByHandle(newCcNodeHandle)};
    ASSERT_NE(newCcNode, nullptr) << "New node could not be retrieved";

    LOG_debug << logPre << "Validating node name and data";
    std::unique_ptr<CreditCardNodeData> receivedCreditCardData{newCcNode->getCreditCardData()};
    ccData->setNotes(nullptr);
    EXPECT_THAT(receivedCreditCardData.get(), CreditCardNodeDataEquals(ccData.get()));
}

TEST_F(SdkTestPasswordManagerCreditCardNodeCRUD, CopyCreditCardNode)
{
    static const auto logPre = getLogPrefix();

    LOG_debug << logPre << "Creating new node to be cloned";
    const auto newCcNodeHandle = createCreditCardNode();
    ASSERT_NE(newCcNodeHandle, UNDEF);
    const std::unique_ptr<MegaNode> newCcNode{mApi->getNodeByHandle(newCcNodeHandle)};
    ASSERT_NE(newCcNode, nullptr) << "New node could not be retrieved";

    LOG_debug << logPre << "Clonning the node";
    std::unique_ptr<MegaNode> clonedNode{newCcNode->copy()};
    std::unique_ptr<CreditCardNodeData> clonedcCData{clonedNode->getCreditCardData()};

    LOG_debug << logPre << "Validating cloned node";
    ASSERT_TRUE(clonedNode->isCreditCardNode());
    ASSERT_FALSE(mApi->isPasswordManagerNodeFolder(clonedNode->getHandle()));

    EXPECT_STREQ(clonedNode->getName(), getFilePrefix().c_str());
    EXPECT_THAT(clonedcCData.get(), CreditCardNodeDataEquals(predefinedCreditCardDataOwned()));
}

TEST_F(SdkTestPasswordManagerCreditCardNodeCRUD, CreateErrorSameName)
{
    static const auto logPre = getLogPrefix();
    LOG_debug << logPre << "Creating a password node node";

    const auto newPwdNodeHandle = createDummyPasswordNode();
    ASSERT_NE(newPwdNodeHandle, UNDEF);

    LOG_debug << logPre
              << "Expecting error when creating a credit card node with the same name "
                 " as the previous password node";
    NiceMock<MockRequestListener> rl;
    rl.setErrorExpectations(API_EEXIST, _, MegaRequest::TYPE_CREATE_PASSWORD_NODE);
    mApi->createCreditCardNode(getFilePrefix().c_str(),
                               predefinedCreditCardDataOwned(),
                               getBaseHandle(),
                               &rl);
    EXPECT_TRUE(rl.waitForFinishOrTimeout(MAX_TIMEOUT));
}

TEST_F(SdkTestPasswordManagerCreditCardNodeCRUD, CreateErrorArgs)
{
    static const auto logPre = getLogPrefix();
    {
        LOG_debug << logPre
                  << "#### Test1: Creating a node with invalid arguments, expecting API_EARGS ####";
        NiceMock<MockRequestListener> rl;
        rl.setErrorExpectations(API_EARGS, _, MegaRequest::TYPE_CREATE_PASSWORD_NODE);
        mApi->createCreditCardNode(nullptr, nullptr, INVALID_HANDLE, &rl);
        EXPECT_TRUE(rl.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    {
        LOG_debug
            << logPre
            << "#### Test2: Creating a node with invalid card number, expecting API_EARGS ####";
        NiceMock<MockRequestListener> rl;
        const auto cCData = predefinedCreditCardData();
        cCData->setCardNumber("A12345");
        rl.setErrorExpectations(API_EAPPKEY, _, MegaRequest::TYPE_CREATE_PASSWORD_NODE);
        mApi->createCreditCardNode(getFilePrefix().c_str(), cCData.get(), getBaseHandle(), &rl);
        EXPECT_TRUE(rl.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    {
        LOG_debug << logPre
                  << "#### Test3: Creating a node with invalid cvv, expecting API_EARGS ####";
        NiceMock<MockRequestListener> rl;
        const auto cCData = predefinedCreditCardData();
        cCData->setCvv("A12");
        rl.setErrorExpectations(API_EAPPKEY, _, MegaRequest::TYPE_CREATE_PASSWORD_NODE);
        mApi->createCreditCardNode(getFilePrefix().c_str(), cCData.get(), getBaseHandle(), &rl);
        EXPECT_TRUE(rl.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    {
        LOG_debug
            << logPre
            << "#### Test4: Creating a node with invalid expiration date, expecting API_EARGS ####";
        NiceMock<MockRequestListener> rl;
        const auto cCData = predefinedCreditCardData();
        cCData->setExpirationDate("15/03"); // invalid month
        rl.setErrorExpectations(API_EAPPKEY, _, MegaRequest::TYPE_CREATE_PASSWORD_NODE);
        mApi->createCreditCardNode(getFilePrefix().c_str(), cCData.get(), getBaseHandle(), &rl);
        EXPECT_TRUE(rl.waitForFinishOrTimeout(MAX_TIMEOUT));
    }
}

TEST_F(SdkTestPasswordManagerCreditCardNodeCRUD, UpdateAllFields)
{
    static const auto logPre = getLogPrefix();
    LOG_debug << logPre << "Creating a node";
    const auto newCcNodeHandle = createCreditCardNode();
    ASSERT_NE(newCcNodeHandle, UNDEF);

    LOG_debug << logPre << "Updating just the notes";
    const auto cCData = emptyCreditCardData();
    cCData->setCardNumber("456789");
    cCData->setNotes("Updated Notes (2)");
    cCData->setCvv("987");
    cCData->setCardHolderName("NEW CARD HOLDER NAME");
    cCData->setExpirationDate(""); // set this field to empty
    updateCreditCardNode(newCcNodeHandle, cCData.get());

    LOG_debug << logPre << "Validating data";
    cCData->setExpirationDate(nullptr);
    validateCreditCardNodeData(newCcNodeHandle, cCData.get());
}

TEST_F(SdkTestPasswordManagerCreditCardNodeCRUD, UpdateErrorArgs)
{
    static const auto logPre = getLogPrefix();
    LOG_debug << logPre << "Creating a node";
    const auto newCcNodeHandle = createCreditCardNode();
    ASSERT_NE(newCcNodeHandle, UNDEF);

    {
        LOG_debug << logPre << "#### Test1: Updating Credit Card Node with empty data ####";
        const auto emptyData = emptyCreditCardData();
        NiceMock<MockRequestListener> rl;
        rl.setErrorExpectations(API_EARGS, _, MegaRequest::TYPE_UPDATE_PASSWORD_NODE);
        mApi->updateCreditCardNode(newCcNodeHandle, emptyData.get(), &rl);
        ASSERT_TRUE(rl.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    {
        LOG_debug << logPre
                  << "#### Test2: Updating Credit Card Node with invalid credit card number ####";
        const auto emptyData = emptyCreditCardData();
        emptyData->setCardNumber("A12345");
        NiceMock<MockRequestListener> rl;
        rl.setErrorExpectations(API_EAPPKEY, _, MegaRequest::TYPE_UPDATE_PASSWORD_NODE);
        mApi->updateCreditCardNode(newCcNodeHandle, emptyData.get(), &rl);
        ASSERT_TRUE(rl.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    {
        LOG_debug << logPre << "#### Test3: Updating Credit Card Node with invalid cvv ####";
        const auto emptyData = emptyCreditCardData();
        emptyData->setCvv("A12");
        NiceMock<MockRequestListener> rl;
        rl.setErrorExpectations(API_EAPPKEY, _, MegaRequest::TYPE_UPDATE_PASSWORD_NODE);
        mApi->updateCreditCardNode(newCcNodeHandle, emptyData.get(), &rl);
        ASSERT_TRUE(rl.waitForFinishOrTimeout(MAX_TIMEOUT));
    }

    {
        LOG_debug << logPre
                  << "#### Test4: Updating Credit Card Node with invalid expiration date ####";
        const auto emptyData = emptyCreditCardData();
        emptyData->setExpirationDate("01-02");
        NiceMock<MockRequestListener> rl;
        rl.setErrorExpectations(API_EAPPKEY, _, MegaRequest::TYPE_UPDATE_PASSWORD_NODE);
        mApi->updateCreditCardNode(newCcNodeHandle, emptyData.get(), &rl);
        ASSERT_TRUE(rl.waitForFinishOrTimeout(MAX_TIMEOUT));
    }
}

TEST_F(SdkTestPasswordManagerCreditCardNodeCRUD, DeleteCreditCardNode)
{
    static const auto logPre = getLogPrefix();
    LOG_debug << logPre << "Creating a node";
    const auto newCcNodeHandle = createCreditCardNode();
    ASSERT_NE(newCcNodeHandle, UNDEF);
    std::unique_ptr<MegaNode> retrievedCcNode{mApi->getNodeByHandle(newCcNodeHandle)};
    ASSERT_NE(retrievedCcNode, nullptr);

    LOG_debug << logPre << "Deleting the node";
    ASSERT_EQ(API_OK, doDeleteNode(0, retrievedCcNode.get()));
    retrievedCcNode.reset(mApi->getNodeByHandle(newCcNodeHandle));
    ASSERT_EQ(nullptr, retrievedCcNode.get());
}
