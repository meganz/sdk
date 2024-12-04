#include "SdkTest_test.h"

#include <gmock/gmock.h>

class SdkTestShare: public SdkTest
{
protected:
    struct Party
    {
        unsigned apiIndex;
        bool wait; // wait for response
    };

    struct HandleUserPair
    {
        MegaHandle handle;

        std::string user;

        friend bool operator==(const HandleUserPair& lhs, const HandleUserPair& rhs)
        {
            return lhs.handle == rhs.handle && lhs.user == rhs.user;
        }
    };

    void createShareAtoB(MegaNode* node, const Party& partyA, const Party& partyB);

    // Use mApi[0] as party A and mApi[1] as party B
    void createShareAtoB(MegaNode* node, bool waitForA = true, bool waitForB = true);

    // Remove a share ensuring node changes are notified.
    // Use mApi[0] and mApi[1]
    void removeShareAtoB(MegaNode* node);

    // Reset credential between two accounts
    void resetCredential(unsigned apiIndexA, unsigned apiIndexB);

    void addContactsAndVerifyCredential(unsigned apiIndexA, unsigned apiIndexB);

    std::vector<HandleUserPair> toHandleUserPair(MegaShareList* shareList);

    std::pair<MegaHandle, std::unique_ptr<MegaNode>> createFolder(unsigned int apiIndex,
                                                                  const char* name,
                                                                  MegaNode* parent);

private:
};

void SdkTestShare::createShareAtoB(MegaNode* node, const Party& partyA, const Party& partyB)
{
    assert(partyA.apiIndex < mApi.size());
    assert(partyB.apiIndex < mApi.size());

    // convinience
    auto& apiA = mApi[partyA.apiIndex];
    auto& apiB = mApi[partyB.apiIndex];

    apiA.mOnNodesUpdateCompletion = createOnNodesUpdateLambda(node->getHandle(),
                                                              MegaNode::CHANGE_TYPE_OUTSHARE,
                                                              apiA.nodeUpdated);
    apiB.mOnNodesUpdateCompletion = createOnNodesUpdateLambda(node->getHandle(),
                                                              MegaNode::CHANGE_TYPE_INSHARE,
                                                              apiB.nodeUpdated);
    ASSERT_NO_FATAL_FAILURE(
        shareFolder(node, apiB.email.c_str(), MegaShare::ACCESS_READWRITE, partyA.apiIndex));

    if (partyA.wait)
    {
        ASSERT_TRUE(waitForResponse(&apiA.nodeUpdated))
            << "Node update not received after " << maxTimeout << " seconds";
    }

    if (partyB.wait)
    {
        ASSERT_TRUE(waitForResponse(&apiB.nodeUpdated))
            << "Node update not received after " << maxTimeout << " seconds";
    }

    resetOnNodeUpdateCompletionCBs();
    apiA.nodeUpdated = apiB.nodeUpdated = false;
}

void SdkTestShare::createShareAtoB(MegaNode* node, bool waitForA, bool waitForB)
{
    createShareAtoB(node, {0, waitForA}, {1, waitForB});
}

void SdkTestShare::removeShareAtoB(MegaNode* node)
{
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(node->getHandle(),
                                                                 MegaNode::CHANGE_TYPE_OUTSHARE,
                                                                 mApi[0].nodeUpdated);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(node->getHandle(),
                                                                 MegaNode::CHANGE_TYPE_REMOVED,
                                                                 mApi[1].nodeUpdated);
    ASSERT_NO_FATAL_FAILURE(shareFolder(node, mApi[1].email.c_str(), MegaShare::ACCESS_UNKNOWN));
    ASSERT_TRUE(waitForResponse(&mApi[0].nodeUpdated))
        << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&mApi[1].nodeUpdated))
        << "Node update not received after " << maxTimeout << " seconds";
    resetOnNodeUpdateCompletionCBs();
    mApi[0].nodeUpdated = mApi[1].nodeUpdated = false;
}

void SdkTestShare::resetCredential(unsigned apiIndexA, unsigned apiIndexB)
{
    if (areCredentialsVerified(apiIndexA, mApi[apiIndexB].email))
    {
        ASSERT_NO_FATAL_FAILURE(resetCredentials(apiIndexA, mApi[apiIndexB].email));
        ASSERT_FALSE(areCredentialsVerified(apiIndexA, mApi[apiIndexB].email));
    }
    if (areCredentialsVerified(apiIndexB, mApi[apiIndexA].email))
    {
        ASSERT_NO_FATAL_FAILURE(resetCredentials(apiIndexB, mApi[apiIndexA].email));
        ASSERT_FALSE(areCredentialsVerified(apiIndexB, mApi[apiIndexA].email));
    }
}

void SdkTestShare::addContactsAndVerifyCredential(unsigned fromApiIndex, unsigned toApiIndex)
{
    mApi[fromApiIndex].contactRequestUpdated = mApi[toApiIndex].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE(inviteContact(fromApiIndex,
                                          mApi[toApiIndex].email,
                                          "TestSharesContactVerification contact request A to B",
                                          MegaContactRequest::INVITE_ACTION_ADD));
    ASSERT_TRUE(waitForResponse(&mApi[fromApiIndex].contactRequestUpdated))
        << "Inviting contact timeout: " << maxTimeout << " seconds.";
    ASSERT_TRUE(waitForResponse(&mApi[toApiIndex].contactRequestUpdated))
        << "Waiting for invitation timeout: " << maxTimeout << " seconds.";
    ASSERT_NO_FATAL_FAILURE(getContactRequest(toApiIndex, false));

    mApi[fromApiIndex].contactRequestUpdated = mApi[toApiIndex].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE(
        replyContact(mApi[toApiIndex].cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT));
    ASSERT_TRUE(waitForResponse(&mApi[toApiIndex].contactRequestUpdated))
        << "Accepting contact timeout: " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&mApi[fromApiIndex].contactRequestUpdated))
        << "Waiting for invitation acceptance timeout: " << maxTimeout << " seconds";

    // Verify credentials:
    LOG_verbose << "TestSharesContactVerification :  Verify A and B credentials";
    ASSERT_NO_FATAL_FAILURE(verifyCredentials(fromApiIndex, mApi[toApiIndex].email));
    ASSERT_NO_FATAL_FAILURE(verifyCredentials(toApiIndex, mApi[fromApiIndex].email));
    ASSERT_TRUE(areCredentialsVerified(fromApiIndex, mApi[toApiIndex].email));
    ASSERT_TRUE(areCredentialsVerified(toApiIndex, mApi[fromApiIndex].email));
}

std::vector<SdkTestShare::HandleUserPair> SdkTestShare::toHandleUserPair(MegaShareList* shareList)
{
    std::vector<SdkTestShare::HandleUserPair> ret;

    if (!shareList)
    {
        return ret;
    }

    for (int i = 0; i < shareList->size(); ++i)
    {
        auto share = shareList->get(i);
        ret.push_back({share->getNodeHandle(), share->getUser()});
    }
    return ret;
}

std::pair<MegaHandle, std::unique_ptr<MegaNode>> SdkTestShare::createFolder(unsigned int apiIndex,
                                                                            const char* name,
                                                                            MegaNode* parent)
{
    MegaHandle nh = SdkTest::createFolder(apiIndex, name, parent);
    std::unique_ptr<MegaNode> node{megaApi[apiIndex]->getNodeByHandle(nh)};
    return {nh, std::move(node)};
}
/**
 * @brief TEST_F TestSharesContactVerification
 *
 * Test contact verification for shares
 *
 */
TEST_F(SdkTestShare, TestSharesContactVerification)
{
    // What we are going to test here:
    // 1: Create a share between A and B, being A and B already contacts in the following scenarios:
    //    1-1: A and B credentials already verified by both.
    //    1-2: A has verified B, but B has not verified A. B verifies A after creating the share.
    //    1-3: None are verified. Then A verifies B and later B verifies A.
    // 2: Create a share between A and B, being A and B no contacts.

    LOG_info << "___TEST TestSharesContactVerification___";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));
    // The idea of the test is to ensure manual verification works as expected, so force it to true
    megaApi[0]->setManualVerificationFlag(true);
    megaApi[1]->setManualVerificationFlag(true);

    // Define all folers needed for the tests.
    string folder11 = "EnhancedSecurityShares-1";
    string folder12 = "EnhancedSecurityShares-21";
    string folder13 = "EnhancedSecurityShares-22";
    string folder2 = "EnhancedSecurityShares-23";

    std::unique_ptr<MegaNode> remoteRootNode(megaApi[0]->getRootNode());
    ASSERT_NE(remoteRootNode.get(), nullptr);

    //
    // 1: Create a share between A and B, being A and B already contacts.
    //

    // Make accounts contacts
    LOG_verbose << "TestSharesContactVerification :  Make account contacts";
    mApi[0].contactRequestUpdated = mApi[1].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE(inviteContact(0,
                                          mApi[1].email,
                                          "TestSharesContactVerification contact request A to B",
                                          MegaContactRequest::INVITE_ACTION_ADD));
    ASSERT_TRUE(waitForResponse(&mApi[0].contactRequestUpdated))
        << "Inviting contact timeout: " << maxTimeout << " seconds.";
    ASSERT_TRUE(waitForResponse(&mApi[1].contactRequestUpdated))
        << "Waiting for invitation timeout: " << maxTimeout << " seconds.";
    ASSERT_NO_FATAL_FAILURE(getContactRequest(1, false));

    mApi[0].contactRequestUpdated = mApi[1].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE(
        replyContact(mApi[1].cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT));
    ASSERT_TRUE(waitForResponse(&mApi[1].contactRequestUpdated))
        << "Accepting contact timeout: " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&mApi[0].contactRequestUpdated))
        << "Waiting for invitation acceptance timeout: " << maxTimeout << " seconds";

    mApi[0].cr.reset();
    mApi[1].cr.reset();

    // Ensure no account has the other verified from previous unfinished tests.
    if (areCredentialsVerified(0, mApi[1].email))
    {
        ASSERT_NO_FATAL_FAILURE(resetCredentials(0, mApi[1].email));
    }
    if (areCredentialsVerified(1, mApi[0].email))
    {
        ASSERT_NO_FATAL_FAILURE(resetCredentials(1, mApi[0].email));
    }

    //
    // 1-1: A and B credentials already verified by both.
    //

    fs::path basePath = fs::u8path(folder11.c_str());

    auto [nh, remoteBaseNode] = createFolder(0, basePath.u8string().c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote basePath";
    ASSERT_NE(remoteBaseNode.get(), nullptr);

    // Verify credentials:
    LOG_verbose << "TestSharesContactVerification :  Verify A and B credentials";
    ASSERT_NO_FATAL_FAILURE(verifyCredentials(0, mApi[1].email));
    ASSERT_NO_FATAL_FAILURE(verifyCredentials(1, mApi[0].email));
    ASSERT_TRUE(areCredentialsVerified(0, mApi[1].email));
    ASSERT_TRUE(areCredentialsVerified(1, mApi[0].email));

    // Create share.
    // B should end with a new inshare and able to decrypt the new node.
    LOG_verbose << "TestSharesContactVerification :  Share a folder from A to B";
    ASSERT_NO_FATAL_FAILURE(createShareAtoB(remoteBaseNode.get()));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getOutShares())->size() == 1;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getUnverifiedOutShares())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 1;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getUnverifiedInShares())->size() == 0;
        },
        60 * 1000));
    std::unique_ptr<MegaNode> inshareNode(megaApi[1]->getNodeByHandle(nh));
    ASSERT_NE(inshareNode.get(), nullptr);
    ASSERT_TRUE(WaitFor(
        [this, nh = nh]()
        {
            return unique_ptr<MegaNode>(megaApi[1]->getNodeByHandle(nh))->isNodeKeyDecrypted();
        },
        60 * 1000))
        << "Cannot decrypt inshare in B account.";

    // Remove share
    LOG_verbose << "TestSharesContactVerification :  Remove shared folder from A to B";
    ASSERT_NO_FATAL_FAILURE(removeShareAtoB(remoteBaseNode.get()));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getOutShares())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getUnverifiedOutShares())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getUnverifiedInShares())->size() == 0;
        },
        60 * 1000));
    inshareNode.reset(megaApi[1]->getNodeByHandle(nh));
    ASSERT_EQ(inshareNode.get(), nullptr);

    // Share the same node again
    LOG_verbose << "TestSharesContactVerification :  Share again the same folder from A to B";
    ASSERT_NO_FATAL_FAILURE(createShareAtoB(remoteBaseNode.get()));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getOutShares())->size() == 1;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getUnverifiedOutShares())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 1;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getUnverifiedInShares())->size() == 0;
        },
        60 * 1000));
    inshareNode.reset(megaApi[1]->getNodeByHandle(nh));
    ASSERT_NE(inshareNode.get(), nullptr);
    ASSERT_TRUE(WaitFor(
        [this, nh = nh]()
        {
            return unique_ptr<MegaNode>(megaApi[1]->getNodeByHandle(nh))->isNodeKeyDecrypted();
        },
        60 * 1000))
        << "Cannot decrypt inshare in B account.";

    /*
     * TODO: uncomment this block to test "Reset credentials" when SDK supports APIv3 for up2/upv
     * commands
     *
     * This test is prone to a race condition that may result on having no inshare, but unverified
     * inshare.
     *
     * It happens when the client receives a "pk" action packet after reset credentials. Why that
     * "pk"? because currently the SDK cannot differentiate between action packets related to its
     * own user's attribute updates
     * (^!keys) and other client's updates. In consequence, if the action packet is received before
     * the response to the "upv", the SDK will fetch the attribute ("uga") and upon receiving the
     * value, it will reapply the promotion of the outshare, sending a duplicated "pk" for the same
     * share handle.
     *
     * This race between the sc and cs channels will be removed when the SDK adds support for the
     * APIv3 / sn-tagging, since the "upv" will be matched with the corresponding action packet,
     * eliminating the race.
     * */

    // // Reset credentials
    // LOG_verbose << "TestSharesContactVerification :  Reset credentials";
    // ASSERT_NO_FATAL_FAILURE(resetAllCredentials());
    // // Established share remains in the same status.
    // ASSERT_TRUE(WaitFor([this]() { return
    // unique_ptr<MegaShareList>(megaApi[0]->getOutShares())->size() == 1; }, 60*1000));
    // ASSERT_TRUE(WaitFor([this]() { return
    // unique_ptr<MegaShareList>(megaApi[0]->getUnverifiedOutShares())->size() == 0; }, 60*1000));
    // ASSERT_TRUE(WaitFor([this]() { return
    // unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 1; }, 60*1000));
    // ASSERT_TRUE(WaitFor([this]() { return
    // unique_ptr<MegaShareList>(megaApi[1]->getUnverifiedInShares())->size() == 0; }, 60*1000));
    // inshareNode.reset(megaApi[1]->getNodeByHandle(nh));
    // ASSERT_NE(inshareNode.get(), nullptr);
    // ASSERT_TRUE(inshareNode->isNodeKeyDecrypted()) << "Cannot decrypt inshare in B account.";

    // Remove share
    LOG_verbose << "TestSharesContactVerification :  Remove shared folder from A to B";
    ASSERT_NO_FATAL_FAILURE(removeShareAtoB(remoteBaseNode.get()));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getOutShares())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getUnverifiedOutShares())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getUnverifiedInShares())->size() == 0;
        },
        60 * 1000));
    inshareNode.reset(megaApi[1]->getNodeByHandle(nh));
    ASSERT_EQ(inshareNode.get(), nullptr);

    ASSERT_NO_FATAL_FAILURE(resetCredential(0, 1));

    //
    // 1-2: A has verified B, but B has not verified A. B verifies A after creating the share.
    //

    basePath = fs::u8path(folder12.c_str()); // Use a different node

    std::tie(nh, remoteBaseNode) =
        createFolder(0, basePath.u8string().c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote basePath";
    ASSERT_NE(remoteBaseNode.get(), nullptr);

    // Verify credentials
    LOG_verbose << "TestSharesContactVerification :  Verify B credentials:";
    ASSERT_NO_FATAL_FAILURE(verifyCredentials(0, mApi[1].email));
    ASSERT_TRUE(areCredentialsVerified(0, mApi[1].email));
    ASSERT_FALSE(areCredentialsVerified(1, mApi[0].email));

    // Create share
    // B should end with an unverified inshare and be unable to decrypt the node.
    LOG_verbose << "TestSharesContactVerification :  Share a folder from A to B";
    ASSERT_NO_FATAL_FAILURE(createShareAtoB(remoteBaseNode.get()));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getOutShares())->size() == 1;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getUnverifiedOutShares())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getUnverifiedInShares())->size() == 1;
        },
        60 * 1000));
    inshareNode.reset(megaApi[1]->getNodeByHandle(nh));
    ASSERT_NE(inshareNode.get(), nullptr);
    ASSERT_FALSE(inshareNode->isNodeKeyDecrypted())
        << "Inshare is decrypted in B account, and it should be not.";

    // Remove share
    LOG_verbose << "TestSharesContactVerification :  Remove shared folder from A to B";
    ASSERT_NO_FATAL_FAILURE(removeShareAtoB(remoteBaseNode.get()));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getOutShares())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getUnverifiedOutShares())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getUnverifiedInShares())->size() == 0;
        },
        60 * 1000));
    inshareNode.reset(megaApi[1]->getNodeByHandle(nh));
    ASSERT_EQ(inshareNode.get(), nullptr);

    // Share the same node again
    LOG_verbose << "TestSharesContactVerification :  Share again the same folder from A to B";
    ASSERT_NO_FATAL_FAILURE(createShareAtoB(remoteBaseNode.get()));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getOutShares())->size() == 1;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getUnverifiedOutShares())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getUnverifiedInShares())->size() == 1;
        },
        60 * 1000));
    inshareNode.reset(megaApi[1]->getNodeByHandle(nh));
    ASSERT_NE(inshareNode.get(), nullptr);
    ASSERT_FALSE(inshareNode->isNodeKeyDecrypted())
        << "Inshare is decrypted in B account, and it should be not.";

    // Verify A credentials in B account
    // B unverified inshare should end as a functional inshare. It should be able to decrypt the new
    // node.
    LOG_verbose << "TestSharesContactVerification :  Verify A credentials";
    mApi[1].mOnNodesUpdateCompletion =
        createOnNodesUpdateLambda(nh,
                                  MegaNode::CHANGE_TYPE_NAME,
                                  mApi[1].nodeUpdated); // file name is set when decrypted.
    ASSERT_NO_FATAL_FAILURE(verifyCredentials(1, mApi[0].email));
    ASSERT_TRUE(areCredentialsVerified(1, mApi[0].email));
    ASSERT_TRUE(waitForResponse(&mApi[1].nodeUpdated))
        << "Node update not received after " << maxTimeout << " seconds";
    resetOnNodeUpdateCompletionCBs();
    mApi[0].nodeUpdated = mApi[1].nodeUpdated = false;
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 1;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getUnverifiedInShares())->size() == 0;
        },
        60 * 1000));
    inshareNode.reset(megaApi[1]->getNodeByHandle(nh));
    ASSERT_NE(inshareNode.get(), nullptr);
    ASSERT_TRUE(WaitFor(
        [this, nh = nh]()
        {
            return unique_ptr<MegaNode>(megaApi[1]->getNodeByHandle(nh))->isNodeKeyDecrypted();
        },
        60 * 1000))
        << "Cannot decrypt inshare in B account.";

    // Remove share
    LOG_verbose << "TestSharesContactVerification :  Remove shared folder from A to B";
    ASSERT_NO_FATAL_FAILURE(removeShareAtoB(remoteBaseNode.get()));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getOutShares())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getUnverifiedOutShares())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getUnverifiedInShares())->size() == 0;
        },
        60 * 1000));
    inshareNode.reset(megaApi[1]->getNodeByHandle(nh));
    ASSERT_EQ(inshareNode.get(), nullptr);

    // Reset credentials:
    LOG_verbose << "TestSharesContactVerification :  Reset credentials";
    ASSERT_NO_FATAL_FAILURE(resetCredential(0, 1));

    //
    // 1-3: None are verified. Then A verifies B and later B verifies A.
    //

    basePath = fs::u8path(folder13.c_str()); // Use a different node

    std::tie(nh, remoteBaseNode) =
        createFolder(0, basePath.u8string().c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote basePath";
    ASSERT_NE(remoteBaseNode.get(), nullptr);

    // Create share.
    // A should end with an unverified outshare and B should have an unverified inshare and unable
    // to decrypt the new node.
    LOG_verbose << "TestSharesContactVerification :  Share a folder from A to B";
    ASSERT_NO_FATAL_FAILURE(createShareAtoB(remoteBaseNode.get()));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getOutShares())->size() == 1;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getUnverifiedOutShares())->size() == 1;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 1;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getUnverifiedInShares())->size() == 0;
        },
        60 * 1000));
    inshareNode.reset(megaApi[1]->getNodeByHandle(nh));
    ASSERT_NE(inshareNode.get(), nullptr);
    ASSERT_FALSE(inshareNode->isNodeKeyDecrypted())
        << "Inshare is decrypted in B account, and it should be not.";

    // Remove share
    LOG_verbose << "TestSharesContactVerification :  Remove shared folder from A to B";
    ASSERT_NO_FATAL_FAILURE(removeShareAtoB(remoteBaseNode.get()));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getOutShares())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getUnverifiedOutShares())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getUnverifiedInShares())->size() == 0;
        },
        60 * 1000));
    inshareNode.reset(megaApi[1]->getNodeByHandle(nh));
    ASSERT_EQ(inshareNode.get(), nullptr);

    // Share the same node again
    LOG_verbose << "TestSharesContactVerification :  Share again the same folder from A to B";
    ASSERT_NO_FATAL_FAILURE(createShareAtoB(remoteBaseNode.get()));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getOutShares())->size() == 1;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getUnverifiedOutShares())->size() == 1;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 1;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getUnverifiedInShares())->size() == 0;
        },
        60 * 1000));
    inshareNode.reset(megaApi[1]->getNodeByHandle(nh));
    ASSERT_NE(inshareNode.get(), nullptr);
    ASSERT_FALSE(inshareNode->isNodeKeyDecrypted())
        << "Inshare is decrypted in B account, and it should be not.";

    // Verify B credentials in A account
    // Unverified outshare in A should disappear and be a regular outshare. No changes expected in
    // B, the share should still be unverified.
    LOG_verbose << "TestSharesContactVerification :  Verify B credentials";
    ASSERT_NO_FATAL_FAILURE(verifyCredentials(0, mApi[1].email));
    ASSERT_TRUE(areCredentialsVerified(0, mApi[1].email));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getOutShares())->size() == 1;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getUnverifiedOutShares())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getUnverifiedInShares())->size() == 1;
        },
        60 * 1000));
    inshareNode.reset(megaApi[1]->getNodeByHandle(nh));
    ASSERT_NE(inshareNode.get(), nullptr);
    ASSERT_FALSE(inshareNode->isNodeKeyDecrypted())
        << "Inshare is decrypted in B account, and it should be not.";

    // Verify A credentials in B account
    // B unverified inshare should end as a functional inshare. It should be able to decrypt the new
    // node.
    LOG_verbose << "TestSharesContactVerification :  Verify A credentials";
    mApi[1].mOnNodesUpdateCompletion =
        createOnNodesUpdateLambda(nh, MegaNode::CHANGE_TYPE_NAME, mApi[1].nodeUpdated);
    ASSERT_NO_FATAL_FAILURE(verifyCredentials(1, mApi[0].email));
    ASSERT_TRUE(areCredentialsVerified(1, mApi[0].email));
    ASSERT_TRUE(waitForResponse(&mApi[1].nodeUpdated))
        << "Node update not received after " << maxTimeout << " seconds";
    resetOnNodeUpdateCompletionCBs();
    mApi[0].nodeUpdated = mApi[1].nodeUpdated = false;
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 1;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getUnverifiedInShares())->size() == 0;
        },
        60 * 1000));
    inshareNode.reset(megaApi[1]->getNodeByHandle(nh));
    ASSERT_NE(inshareNode.get(), nullptr);
    ASSERT_TRUE(WaitFor(
        [this, nh = nh]()
        {
            return unique_ptr<MegaNode>(megaApi[1]->getNodeByHandle(nh))->isNodeKeyDecrypted();
        },
        60 * 1000))
        << "Cannot decrypt inshare in B account.";

    // Remove share
    LOG_verbose << "TestSharesContactVerification :  Remove shared folder from A to B";
    ASSERT_NO_FATAL_FAILURE(removeShareAtoB(remoteBaseNode.get()));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getOutShares())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getUnverifiedOutShares())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getUnverifiedInShares())->size() == 0;
        },
        60 * 1000));
    inshareNode.reset(megaApi[1]->getNodeByHandle(nh));
    ASSERT_EQ(inshareNode.get(), nullptr);

    // Reset credentials
    LOG_verbose << "TestSharesContactVerification :  Reset credentials";
    ASSERT_NO_FATAL_FAILURE(resetCredential(0, 1));

    // Delete contacts
    LOG_verbose << "TestSharesContactVerification :  Remove Contact";
    ASSERT_EQ(API_OK, removeContact(0, mApi[1].email));
    unique_ptr<MegaUser> user(megaApi[0]->getContact(mApi[1].email.c_str()));
    ASSERT_FALSE(user == nullptr) << "Not user for contact email: " << mApi[1].email;
    ASSERT_EQ(MegaUser::VISIBILITY_HIDDEN, user->getVisibility())
        << "Contact is still visible after removing it." << mApi[1].email;

    //
    // 2: Create a share between A and B, being A and B no contacts.
    //

    basePath = fs::u8path(folder2.c_str()); // Use a different node

    std::tie(nh, remoteBaseNode) =
        createFolder(0, basePath.u8string().c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote basePath";
    remoteBaseNode.reset(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteBaseNode.get(), nullptr);

    // Create share.
    // Since are no contacts, B should receive a contact request.
    LOG_verbose << "TestSharesContactVerification :  Share a folder from A to B";
    mApi[0].contactRequestUpdated = mApi[1].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE(createShareAtoB(remoteBaseNode.get(), false, false));
    ASSERT_TRUE(waitForResponse(&mApi[0].contactRequestUpdated))
        << "Inviting contact timeout: " << maxTimeout << " seconds.";
    ASSERT_TRUE(waitForResponse(&mApi[1].contactRequestUpdated))
        << "Waiting for invitation timeout: " << maxTimeout << " seconds.";
    ASSERT_NO_FATAL_FAILURE(getContactRequest(1, false));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getOutShares())->size() == 1;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getUnverifiedOutShares())->size() == 1;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getUnverifiedInShares())->size() == 0;
        },
        60 * 1000));
    inshareNode.reset(megaApi[1]->getNodeByHandle(nh));
    ASSERT_EQ(inshareNode.get(), nullptr);

    // B accepts the contact request.
    // Now B should end with an inshare without 'pk' yet, so "verified".
    mApi[0].contactRequestUpdated = mApi[1].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE(
        replyContact(mApi[1].cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT));
    ASSERT_TRUE(waitForResponse(&mApi[1].contactRequestUpdated))
        << "Accepting contact timeout: " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&mApi[0].contactRequestUpdated))
        << "Waiting for invitation acceptance timeout: " << maxTimeout << " seconds";
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getOutShares())->size() == 1;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getUnverifiedOutShares())->size() == 1;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 1;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getUnverifiedInShares())->size() == 0;
        },
        60 * 1000));

    // Verify B credentials in A account
    // Unverified outshare in A should disappear and be a regular outshare. No changes expected in
    // B, the share should still be unverified.
    LOG_verbose << "TestSharesContactVerification :  Verify B credentials";
    ASSERT_NO_FATAL_FAILURE(verifyCredentials(0, mApi[1].email));
    ASSERT_TRUE(areCredentialsVerified(0, mApi[1].email));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getOutShares())->size() == 1;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getUnverifiedOutShares())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getUnverifiedInShares())->size() == 1;
        },
        60 * 1000));
    inshareNode.reset(megaApi[1]->getNodeByHandle(nh));
    ASSERT_NE(inshareNode.get(), nullptr);
    ASSERT_FALSE(inshareNode->isNodeKeyDecrypted())
        << "Inshare is decrypted in B account, and it should be not.";

    // Verify A credentials in B account
    // B unverified inshare should end as a functional inshare. It should be able to decrypt the new
    // node.
    LOG_verbose << "TestSharesContactVerification :  Verify A credentials";
    mApi[1].mOnNodesUpdateCompletion =
        createOnNodesUpdateLambda(nh, MegaNode::CHANGE_TYPE_NAME, mApi[1].nodeUpdated);
    ASSERT_NO_FATAL_FAILURE(verifyCredentials(1, mApi[0].email));
    ASSERT_TRUE(areCredentialsVerified(1, mApi[0].email));
    ASSERT_TRUE(waitForResponse(&mApi[1].nodeUpdated))
        << "Node update not received after " << maxTimeout << " seconds";
    resetOnNodeUpdateCompletionCBs();
    mApi[0].nodeUpdated = mApi[1].nodeUpdated = false;
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 1;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getUnverifiedInShares())->size() == 0;
        },
        60 * 1000));
    inshareNode.reset(megaApi[1]->getNodeByHandle(nh));
    ASSERT_NE(inshareNode.get(), nullptr);
    ASSERT_TRUE(WaitFor(
        [this, nh = nh]()
        {
            return unique_ptr<MegaNode>(megaApi[1]->getNodeByHandle(nh))->isNodeKeyDecrypted();
        },
        60 * 1000))
        << "Cannot decrypt inshare in B account.";

    // Remove share
    LOG_verbose << "TestSharesContactVerification :  Remove shared folder from A to B";
    ASSERT_NO_FATAL_FAILURE(removeShareAtoB(remoteBaseNode.get()));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getOutShares())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[0]->getUnverifiedOutShares())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 0;
        },
        60 * 1000));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return unique_ptr<MegaShareList>(megaApi[1]->getUnverifiedInShares())->size() == 0;
        },
        60 * 1000));
    inshareNode.reset(megaApi[1]->getNodeByHandle(nh));
    ASSERT_EQ(inshareNode.get(), nullptr);

    // Reset credentials
    LOG_verbose << "TestSharesContactVerification :  Reset credentials";
    ASSERT_NO_FATAL_FAILURE(resetCredential(0, 1));

    // Delete contacts
    LOG_verbose << "TestSharesContactVerification :  Remove Contact";
    ASSERT_EQ(API_OK, removeContact(0, mApi[1].email));
    user.reset(megaApi[0]->getContact(mApi[1].email.c_str()));
    ASSERT_FALSE(user == nullptr) << "Not user for contact email: " << mApi[1].email;
    ASSERT_EQ(MegaUser::VISIBILITY_HIDDEN, user->getVisibility())
        << "Contact is still visible after removing it." << mApi[1].email;
}

TEST_F(SdkTestShare, GetOutSharesOrUnverifiedOutSharesOrderedByCreationTime)
{
    LOG_info << "___TEST TestSharesContactVerification___";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(3));
    megaApi[0]->setManualVerificationFlag(true);
    megaApi[1]->setManualVerificationFlag(true);
    megaApi[2]->setManualVerificationFlag(true);

    // Ensure no account has the other verified from previous unfinished tests.
    ASSERT_NO_FATAL_FAILURE(resetCredential(0, 1));
    ASSERT_NO_FATAL_FAILURE(resetCredential(0, 2));

    LOG_info << "Invite from account 0 to 1 and verify credential";
    ASSERT_NO_FATAL_FAILURE(addContactsAndVerifyCredential(0, 1));

    // Root node
    std::unique_ptr<MegaNode> remoteRootNode(megaApi[0]->getRootNode());
    ASSERT_NE(remoteRootNode.get(), nullptr);

    LOG_info << "Create share folders";
    auto [handle1, shareNode1] = createFolder(0, "share1", remoteRootNode.get());
    auto [handle2, shareNode2] = createFolder(0, "share2", remoteRootNode.get());
    auto [handle3, shareNode3] = createFolder(0, "share3", remoteRootNode.get());
    ASSERT_THAT(shareNode1, testing::NotNull());
    ASSERT_THAT(shareNode2, testing::NotNull());
    ASSERT_THAT(shareNode3, testing::NotNull());

    LOG_info << "Share folders from account 0 to account 1 share node 2,1 and 3 in order";
    ASSERT_NO_FATAL_FAILURE(createShareAtoB(shareNode2.get(), false, false));
    ASSERT_NO_FATAL_FAILURE(createShareAtoB(shareNode1.get(), false, false));
    ASSERT_NO_FATAL_FAILURE(createShareAtoB(shareNode3.get(), false, false));

    LOG_info << "Share folders from account 0 to account 2 share node 2,1 and 3 in order";
    ASSERT_NO_FATAL_FAILURE(createShareAtoB(shareNode2.get(), {0, false}, {2, false}));
    ASSERT_NO_FATAL_FAILURE(createShareAtoB(shareNode1.get(), {0, false}, {2, false}));
    ASSERT_NO_FATAL_FAILURE(createShareAtoB(shareNode3.get(), {0, false}, {2, false}));

    auto user1 = mApi[1].email;
    auto user2 = mApi[2].email;
    std::vector<HandleUserPair> shares;
    ASSERT_TRUE(WaitFor(
        [this, &shares]()
        {
            shares = toHandleUserPair(megaApi[0]->getOutShares(MegaApi::ORDER_SHARE_CREATION_ASC));
            return shares.size() == 6;
        },
        60 * 1000));
    ASSERT_THAT(shares,
                testing::ElementsAreArray(std::vector<HandleUserPair>{
                    {handle2, user1},
                    {handle1, user1},
                    {handle3, user1},
                    {handle2, user2},
                    {handle1, user2},
                    {handle3, user2},
    }));

    shares =
        toHandleUserPair(megaApi[0]->getUnverifiedOutShares(MegaApi::ORDER_SHARE_CREATION_ASC));
    ASSERT_THAT(shares,
                testing::ElementsAreArray(std::vector<HandleUserPair>{
                    {handle2, user2},
                    {handle1, user2},
                    {handle3, user2},
    }));

    shares = toHandleUserPair(megaApi[0]->getOutShares(MegaApi::ORDER_SHARE_CREATION_DESC));
    ASSERT_THAT(shares,
                testing::ElementsAreArray(std::vector<HandleUserPair>{
                    {handle3, user2},
                    {handle1, user2},
                    {handle2, user2},
                    {handle3, user1},
                    {handle1, user1},
                    {handle2, user1},
    }));

    shares =
        toHandleUserPair(megaApi[0]->getUnverifiedOutShares(MegaApi::ORDER_SHARE_CREATION_DESC));
    ASSERT_THAT(shares,
                testing::ElementsAreArray(std::vector<HandleUserPair>{
                    {handle3, user2},
                    {handle1, user2},
                    {handle2, user2},
    }));
}
