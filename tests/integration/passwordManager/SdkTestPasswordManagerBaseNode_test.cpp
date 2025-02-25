/**
 * @file
 * @brief Tests for operations on the base node for the password manager
 */

#include "mock_listeners.h"
#include "SdkTestPasswordManager.h"

#include <gmock/gmock.h>

using namespace testing;

class SdkTestPasswordManagerBaseNode: public SdkTestPasswordManager
{};

TEST_F(SdkTestPasswordManagerBaseNode, GetPWMBaseNode)
{
    LOG_debug << getLogPrefix() << "get Password Manager Base node by handle";
    ASSERT_NE(getBaseNode(), nullptr) << "Error retrieving MegaNode for Password Base with handle "
                                      << toNodeHandle(getBaseHandle());
}

TEST_F(SdkTestPasswordManagerBaseNode, GetPWMBaseNodeByUserAttr)
{
    LOG_debug << getLogPrefix() << "get Password Manager Base via get user's attribute command";
    testing::NiceMock<MockRequestListener> rl{mApi};
    handle reqHandle{UNDEF};
    const auto captureHandle = [&reqHandle](const MegaRequest& req)
    {
        reqHandle = req.getNodeHandle();
    };
    rl.setErrorExpectations(API_OK, _, MegaRequest::TYPE_GET_ATTR_USER, std::move(captureHandle));
    mApi->getUserAttribute(MegaApi::USER_ATTR_PWM_BASE, &rl);
    ASSERT_TRUE(rl.waitForFinishOrTimeout(MAX_TIMEOUT));
    ASSERT_EQ(reqHandle, getBaseHandle()) << "Mismatch in user attribute pwmh retrieved";
}

TEST_F(SdkTestPasswordManagerBaseNode, DeletePWMBaseNode)
{
    const auto baseNode = getBaseNode();
    ASSERT_NE(nullptr, baseNode);
    ASSERT_EQ(API_EARGS, doDeleteNode(0, baseNode.get()));
}
