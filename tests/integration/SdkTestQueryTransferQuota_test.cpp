/**
 * @file
 * @brief Tests for MegaApi::estimateFreeTransferQuotaState, which asks the API (`qbq`) whether the
 * account's transfer quota can still cover a given amount of bytes.
 */

#include "api_request_hook.h"
#include "mega/scoped_helpers.h"
#include "mega/testhooks.h"
#include "SdkTest_test.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <string_view>

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED

namespace
{

struct QuotaReplyCase
{
    std::string_view apiReply;
    int expectedState;
    error expectedError;
};

// A `qbq` reply as the API sends it, and the state and error the app must be given for it. A bare
// positive code is a value: 0 enough quota, 1 the unregistered-user allowance would be nearly
// exhausted, 2 the registered-user one would be, 3 not enough or exceeded. A negative code is the
// query failing, which the request reports as its error, with no state to give.
constexpr QuotaReplyCase QUOTA_REPLY_CASES[] = {
    {"[0]", MegaApi::TRANSFER_QUOTA_STATE_GREEN, API_OK},
    {"[1]", MegaApi::TRANSFER_QUOTA_STATE_ORANGE, API_OK},
    {"[2]", MegaApi::TRANSFER_QUOTA_STATE_ORANGE, API_OK},
    {"[3]", MegaApi::TRANSFER_QUOTA_STATE_RED, API_OK},
    {"[-2]", MegaApi::TRANSFER_QUOTA_STATE_UNKNOWN, API_EARGS},
};

// Serves the `qbq` command locally, so the answer does not depend on the account's real quota.
class ScopedQbqReply
{
public:
    explicit ScopedQbqReply(const std::string_view reply):
        mReply{reply},
        mPrev{globalMegaTestHooks.onHttpReqPost}
    {
        globalMegaTestHooks.onHttpReqPost = [this](HttpReq* req)
        {
            return answer(req);
        };
    }

    ~ScopedQbqReply()
    {
        globalMegaTestHooks.onHttpReqPost = std::move(mPrev);
    }

    bool answered() const
    {
        return mAnswered.load();
    }

private:
    // One reply stands for the whole batch, so it must carry a slot per command: the canned answer
    // at the query's own position, and a benign error at every other, which each command's
    // procresult takes as a plain CmdError. Without this a batch that happened to carry anything
    // besides the query could not be answered at all.
    bool answer(HttpReq* const req)
    {
        if (!req || req->type != REQ_JSON || !req->out)
            return false;

        const auto isQuery = [](const std::string& command)
        {
            return command.find(R"("a":"qbq")") != std::string::npos;
        };

        const auto commands = sdk_test::splitApiCommands(*req->out);
        const auto query = std::find_if(commands.begin(), commands.end(), isQuery);
        if (query == commands.end())
            return false;

        std::string reply{"["};
        for (auto it = commands.begin(); it != commands.end(); ++it)
        {
            if (it != commands.begin())
                reply += ',';

            // The canned reply is written as the whole cs response, so strip its brackets to use it
            // as one slot
            reply += (it == query) ? mReply.substr(1, mReply.size() - 2) :
                                     std::to_string(static_cast<int>(API_EINTERNAL));
        }
        reply += ']';

        req->in = std::move(reply);
        req->httpstatus = 200;
        req->contentlength = static_cast<m_off_t>(req->in.size());
        req->bufpos = req->contentlength;
        req->status = REQ_SUCCESS;
        mAnswered = true;
        return true; // the reply is in place, so skip the real request
    }

    const std::string mReply;
    std::function<bool(HttpReq*)> mPrev;
    std::atomic<bool> mAnswered{false};
};

// Counts the quota-related commands the SDK sends, so a download can be checked for issuing a
// bounded number of them rather than one per transfer.
class QuotaCommandCounter
{
public:
    QuotaCommandCounter():
        mPrev{globalMegaTestHooks.onHttpReqPost}
    {
        globalMegaTestHooks.onHttpReqPost = [this](HttpReq* req)
        {
            count(req);
            return false; // let the request go out as usual
        };
    }

    ~QuotaCommandCounter()
    {
        globalMegaTestHooks.onHttpReqPost = std::move(mPrev);
    }

    int queries() const
    {
        return mQueries.load();
    }

    int accountDetails() const
    {
        return mAccountDetails.load();
    }

private:
    void count(const HttpReq* req)
    {
        if (!req || req->type != REQ_JSON || !req->out)
        {
            return;
        }

        // One batch can carry several commands, so every occurrence counts
        mQueries += occurrences(*req->out, R"("a":"qbq")");
        mAccountDetails += occurrences(*req->out, R"("a":"uq")");
    }

    static int occurrences(const std::string& body, const std::string_view command)
    {
        int found = 0;
        for (auto at = body.find(command); at != std::string::npos;
             at = body.find(command, at + command.size()))
        {
            ++found;
        }
        return found;
    }

    std::function<bool(HttpReq*)> mPrev;
    std::atomic<int> mQueries{0};
    std::atomic<int> mAccountDetails{0};
};

} // namespace

#endif // MEGASDK_DEBUG_TEST_HOOKS_ENABLED

/**
 * @class SdkTestQueryTransferQuota
 * @brief Fixture with a single logged-in account to run transfer quota queries against.
 */
class SdkTestQueryTransferQuota: public SdkTest
{
protected:
    void SetUp() override
    {
        SdkTest::SetUp();
        ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    }

    void TearDown() override
    {
        // Remove what was uploaded, so repeated runs do not accumulate probe files in the account
        // root. Here rather than in the tests, so it still happens when one returns early.
        for (const MegaHandle uploaded: mUploaded)
        {
            if (std::unique_ptr<MegaNode> node{megaApi[0]->getNodeByHandle(uploaded)})
                doDeleteNode(0, node.get());
        }
        mUploaded.clear();

        SdkTest::TearDown();
    }

    // Poses as a free account for the caller's scope. `qbq` is the free/feature path, so a Pro test
    // account would take the allowance path and send none, leaving the counts below about the
    // account rather than the code. Nothing here fetches plans, so no `uq` can put the level back.
    //
    // Written from the test thread while the engine reads it from exec(), as other tests here do
    // (sdk_test_share_nested.cpp). A single aligned store, but TSAN will call it a race.
    [[nodiscard]] ScopedDestructor poseAsFreeAccount()
    {
        MegaClient* const client = megaApi[0]->getClient();
        const AccountType realProLevel = client->mMyAccount.getProLevel();
        client->mMyAccount.setProLevel(AccountType::ACCOUNT_TYPE_FREE);

        return makeScopedDestructor(
            [client, realProLevel]()
            {
                client->mMyAccount.setProLevel(realProLevel);
            });
    }

    // Queries the quota for a node and returns the request as reported to the app, or nullptr if it
    // did not complete. The state is carried by number; the error is the query's own.
    std::unique_ptr<MegaRequest> runQuery(MegaNode* node, const error expectedError = API_OK)
    {
        // Split by ownership, as the API takes them: the caller does this now, so the list is of
        // handles rather than nodes.
        std::unique_ptr<MegaHandleList> ownNodes{MegaHandleList::createInstance()};
        std::unique_ptr<MegaHandleList> publicNodes{MegaHandleList::createInstance()};
        (node->isPublic() ? publicNodes : ownNodes)->addMegaHandle(node->getHandle());

        RequestTracker tracker{megaApi[0].get()};
        megaApi[0]->estimateFreeTransferQuotaState(ownNodes.get(), publicNodes.get(), &tracker);

        EXPECT_EQ(expectedError, tracker.waitForResult()) << "Unexpected transfer quota result";
        if (!tracker.request)
            return nullptr;

        EXPECT_EQ(MegaRequest::TYPE_ESTIMATE_FREE_TRANSFER_QUOTA_STATE, tracker.request->getType());
        return std::move(tracker.request);
    }

    // Uploads a small file and returns its node, or nullptr on failure. The query needs a file
    // node, as folder nodes are not supported.
    std::unique_ptr<MegaNode> uploadProbeFile()
    {
        std::unique_ptr<MegaNode> rootNode{megaApi[0]->getRootNode()};
        EXPECT_TRUE(rootNode);
        if (!rootNode)
            return nullptr;

        // Small on purpose: the download has to finish well inside one check interval, or the
        // bounded-query assertions become a measure of the link speed.
        EXPECT_TRUE(createFile(PROBE_FILE_NAME, false)) << "Could not create the probe file";

        MegaHandle uploaded = INVALID_HANDLE;
        EXPECT_EQ(API_OK,
                  doStartUpload(0,
                                &uploaded,
                                PROBE_FILE_NAME,
                                rootNode.get(),
                                nullptr,
                                ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                nullptr,
                                false,
                                false,
                                nullptr))
            << "Could not upload the probe file";

        if (uploaded != INVALID_HANDLE)
            mUploaded.push_back(uploaded);

        return std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(uploaded)};
    }

    static constexpr char PROBE_FILE_NAME[] = "transfer_quota_probe.txt";

    // Handles TearDown removes
    std::vector<MegaHandle> mUploaded;
};

/**
 * @brief SdkTestQueryTransferQuota.OrdinaryDownloadDoesNotQueryTheQuota
 *
 * Only a batch download (MegaApi::startDownloadBatch) and a streaming read ask about the quota.
 * An ordinary download asks nothing: there is no periodic poll behind it any more, so it must not
 * cost a single `qbq`, however long it runs.
 */
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
TEST_F(SdkTestQueryTransferQuota, OrdinaryDownloadDoesNotQueryTheQuota)
{
    LOG_info << "___TEST OrdinaryDownloadDoesNotQueryTheQuota___";

    const std::unique_ptr<MegaNode> node{uploadProbeFile()};
    ASSERT_TRUE(node);

    const std::string downloadPath = std::string{"./"} + PROBE_FILE_NAME + ".downloaded";

    // Removed even if an assertion below returns early
    const auto removeDownload = makeScopedDestructor(
        [this, &downloadPath]()
        {
            deleteFile(downloadPath);
        });

    const auto posingAsFree = poseAsFreeAccount();

    // Counted only over the download, so the upload's own commands are not included
    const QuotaCommandCounter counter;

    ASSERT_EQ(API_OK,
              doStartDownload(0,
                              node.get(),
                              downloadPath.c_str(),
                              nullptr,
                              nullptr,
                              false,
                              nullptr,
                              MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                              MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N,
                              false))
        << "Could not download the file";

    // Nothing at all: an ordinary download reports no quota state, so it has nothing to ask.
    // A trigger added here without updating the public docs would show up as a failure.
    EXPECT_EQ(0, counter.queries())
        << "an ordinary download sent " << counter.queries()
        << " `qbq` request(s); only a batch download and a streaming read should query";
}
#else
TEST_F(SdkTestQueryTransferQuota, OrdinaryDownloadDoesNotQueryTheQuota)
{
    GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED";
}
#endif

/**
 * @brief SdkTestQueryTransferQuota.ApiReplyIsReportedAsQuotaState
 *
 * Every `qbq` reply must reach the app as the matching state through MegaRequest::getNumber,
 * which is what apps read to warn before starting a transfer they cannot finish. The replies are
 * served locally because the live API answers 0 for any size, however large.
 */
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
TEST_F(SdkTestQueryTransferQuota, ApiReplyIsReportedAsQuotaState)
{
    // The query is expressed as file nodes, so it needs one to ask about even though the reply is
    // served locally. The size the API would charge for it is irrelevant for the same reason.
    const std::unique_ptr<MegaNode> node{uploadProbeFile()};
    ASSERT_TRUE(node);

    // ScopedQbqReply only answers a batch carrying nothing else, and a Pro account's periodic `uq`
    // could share one. Posing as free keeps that poll from running at all.
    const auto posingAsFree = poseAsFreeAccount();

    for (const auto& [apiReply, expectedState, expectedError]: QUOTA_REPLY_CASES)
    {
        const ScopedQbqReply reply{apiReply};

        const auto request = runQuery(node.get(), expectedError);
        ASSERT_TRUE(reply.answered())
            << "No `qbq` request was sent, so " << apiReply << " was not simulated";
        ASSERT_TRUE(request);
        EXPECT_EQ(expectedState, request->getNumber())
            << "The " << apiReply << " reply was not reported as state " << expectedState;
    }
}
#else
TEST_F(SdkTestQueryTransferQuota, ApiReplyIsReportedAsQuotaState)
{
    GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED";
}
#endif
