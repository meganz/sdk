#include "SdkTest_test.h"

class SdkTestShare: public SdkTest
{};

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

    // Auxiliar function to create a share ensuring node changes are notified.
    auto createShareAtoB = [this](MegaNode* node, bool waitForA = true, bool waitForB = true)
    {
        mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(node->getHandle(),
                                                                     MegaNode::CHANGE_TYPE_OUTSHARE,
                                                                     mApi[0].nodeUpdated);
        mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(node->getHandle(),
                                                                     MegaNode::CHANGE_TYPE_INSHARE,
                                                                     mApi[1].nodeUpdated);
        ASSERT_NO_FATAL_FAILURE(
            shareFolder(node, mApi[1].email.c_str(), MegaShare::ACCESS_READWRITE));
        if (waitForA)
        {
            ASSERT_TRUE(waitForResponse(&mApi[0].nodeUpdated))
                << "Node update not received after " << maxTimeout << " seconds";
        }
        if (waitForB)
        {
            ASSERT_TRUE(waitForResponse(&mApi[1].nodeUpdated))
                << "Node update not received after " << maxTimeout << " seconds";
        }
        resetOnNodeUpdateCompletionCBs();
        mApi[0].nodeUpdated = mApi[1].nodeUpdated = false;
    };

    // Auxiliar function to remove a share ensuring node changes are notified.
    auto removeShareAtoB = [this](MegaNode* node)
    {
        mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(node->getHandle(),
                                                                     MegaNode::CHANGE_TYPE_OUTSHARE,
                                                                     mApi[0].nodeUpdated);
        mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(node->getHandle(),
                                                                     MegaNode::CHANGE_TYPE_REMOVED,
                                                                     mApi[1].nodeUpdated);
        ASSERT_NO_FATAL_FAILURE(
            shareFolder(node, mApi[1].email.c_str(), MegaShare::ACCESS_UNKNOWN));
        ASSERT_TRUE(waitForResponse(&mApi[0].nodeUpdated))
            << "Node update not received after " << maxTimeout << " seconds";
        ASSERT_TRUE(waitForResponse(&mApi[1].nodeUpdated))
            << "Node update not received after " << maxTimeout << " seconds";
        resetOnNodeUpdateCompletionCBs();
        mApi[0].nodeUpdated = mApi[1].nodeUpdated = false;
    };

    // Auxiliar function to reset both accounts credentials verification
    auto resetAllCredentials = [this]()
    {
        ASSERT_NO_FATAL_FAILURE(resetCredentials(0, mApi[1].email));
        ASSERT_NO_FATAL_FAILURE(resetCredentials(1, mApi[0].email));
        ASSERT_FALSE(areCredentialsVerified(0, mApi[1].email));
        ASSERT_FALSE(areCredentialsVerified(1, mApi[0].email));
    };

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

    MegaHandle nh = createFolder(0, basePath.u8string().c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote basePath";
    std::unique_ptr<MegaNode> remoteBaseNode(megaApi[0]->getNodeByHandle(nh));
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
        [this, nh]()
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
        [this, nh]()
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

    ASSERT_NO_FATAL_FAILURE(resetAllCredentials());

    //
    // 1-2: A has verified B, but B has not verified A. B verifies A after creating the share.
    //

    basePath = fs::u8path(folder12.c_str()); // Use a different node

    nh = createFolder(0, basePath.u8string().c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote basePath";
    remoteBaseNode.reset(megaApi[0]->getNodeByHandle(nh));
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
        [this, nh]()
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
    ASSERT_NO_FATAL_FAILURE(resetAllCredentials());

    //
    // 1-3: None are verified. Then A verifies B and later B verifies A.
    //

    basePath = fs::u8path(folder13.c_str()); // Use a different node

    nh = createFolder(0, basePath.u8string().c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote basePath";
    remoteBaseNode.reset(megaApi[0]->getNodeByHandle(nh));
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
        [this, nh]()
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
    ASSERT_NO_FATAL_FAILURE(resetAllCredentials());

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

    nh = createFolder(0, basePath.u8string().c_str(), remoteRootNode.get());
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
        [this, nh]()
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
    ASSERT_NO_FATAL_FAILURE(resetAllCredentials());

    // Delete contacts
    LOG_verbose << "TestSharesContactVerification :  Remove Contact";
    ASSERT_EQ(API_OK, removeContact(0, mApi[1].email));
    user.reset(megaApi[0]->getContact(mApi[1].email.c_str()));
    ASSERT_FALSE(user == nullptr) << "Not user for contact email: " << mApi[1].email;
    ASSERT_EQ(MegaUser::VISIBILITY_HIDDEN, user->getVisibility())
        << "Contact is still visible after removing it." << mApi[1].email;
}
