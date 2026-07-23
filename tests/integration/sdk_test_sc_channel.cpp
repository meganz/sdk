/**
 * @file sdk_test_sc_channel.cpp
 * @brief Integration tests for the SC (Server-Client) channel handling
 */

#include "mega/testhooks.h"
#include "SdkTest_test.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mutex>
#include <vector>

using namespace mega;
using namespace testing;

namespace
{

/**
 * @brief Captured network activity event
 */
struct NetworkActivityEvent
{
    int channel;
    int activityType;
    int errorCode;
};

/**
 * @brief Listener that captures EVENT_NETWORK_ACTIVITY events
 */
class NetworkActivityListener: public MegaListener
{
public:
    void onEvent(MegaApi*, MegaEvent* event) override
    {
        if (!event || event->getType() != MegaEvent::EVENT_NETWORK_ACTIVITY)
        {
            return;
        }

        if (!event->hasNumber("channel") || !event->hasNumber("activity_type") ||
            !event->hasNumber("error_code"))
        {
            return;
        }

        std::lock_guard<std::mutex> lock(mMutex);
        mEvents.push_back({static_cast<int>(event->getNumber("channel")),
                           static_cast<int>(event->getNumber("activity_type")),
                           static_cast<int>(event->getNumber("error_code"))});
    }

    /**
     * @brief Check if any captured event matches the given parameters
     */
    bool hasEvent(int channel, int activityType, int errorCode) const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        for (const auto& e: mEvents)
        {
            if (e.channel == channel && e.activityType == activityType && e.errorCode == errorCode)
            {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Clear all captured events
     */
    void clear()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mEvents.clear();
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mEvents.size();
    }

private:
    mutable std::mutex mMutex;
    std::vector<NetworkActivityEvent> mEvents;
};

} // anonymous namespace

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED

/**
 * @class SdkTestScChannel
 * @brief Parameterized test fixture for SC channel handling tests.
 *
 * The bool parameter controls the SC action packet parsing mode:
 *   false: non-streaming
 *   true: streaming
 *
 * Every test case runs once in each mode.
 */
class SdkTestScChannel: public SdkTest, public ::testing::WithParamInterface<bool>
{
public:
    void SetUp() override
    {
        SdkTest::SetUp();
        ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

        if (GetParam())
        {
            megaApi[0]->getClient()->enableStreaming();
        }
        else
        {
            megaApi[0]->getClient()->disableStreaming();
        }

        megaApi[0]->addListener(&mNetworkListener);
    }

    void TearDown() override
    {
        globalMegaTestHooks.interceptSCRequest = nullptr;
        globalMegaTestHooks.onHeartbeatReceived = nullptr;
        megaApi[0]->removeListener(&mNetworkListener);
        SdkTest::TearDown();
    }

protected:
    NetworkActivityListener mNetworkListener;

    /**
     * @brief Install test hook to simulate an error response
     */
    void installErrorResponseHook(int error)
    {
        globalMegaTestHooks.interceptSCRequest = [error, this](std::unique_ptr<HttpReq>& pendingsc)
        {
            LOG_info << "SC channel hook: injecting error response: " << error;
            mNetworkListener.clear();
            pendingsc->status = REQ_SUCCESS;
            pendingsc->in = std::to_string(error);
            pendingsc->httpstatus = 200;
            globalMegaTestHooks.interceptSCRequest = nullptr;
        };
    }

    /**
     * @brief Install interceptSCRequest hook to simulate SSL check failure
     */
    void installSslFailureHook()
    {
        globalMegaTestHooks.interceptSCRequest = [this](std::unique_ptr<HttpReq>& pendingsc)
        {
            LOG_info << "SC channel hook: injecting SSL check failure";
            mNetworkListener.clear();
            pendingsc->status = REQ_FAILURE;
            pendingsc->httpstatus = 500;
            pendingsc->sslcheckfailed = true;
            globalMegaTestHooks.interceptSCRequest = nullptr;
        };
    }

    /**
     * @brief Install interceptSCRequest hook to simulate DNS failure
     */
    void installDnsFailureHook()
    {
        globalMegaTestHooks.interceptSCRequest = [this](std::unique_ptr<HttpReq>& pendingsc)
        {
            LOG_info << "SC channel hook: injecting DNS failure";
            mNetworkListener.clear();
            pendingsc->status = REQ_FAILURE;
            pendingsc->httpstatus = 0;
            pendingsc->mDnsFailure = true;
            globalMegaTestHooks.interceptSCRequest = nullptr;
        };
    }

    /**
     * @brief Wait until the listener captures all expected events and no others.
     */
    bool waitForNetworkEvents(std::vector<NetworkActivityEvent> expectedEvents,
                              unsigned int timeoutMs,
                              bool requireSizeMatch = true)
    {
        return WaitFor(
            [&, this]()
            {
                if (mNetworkListener.size() < expectedEvents.size())
                {
                    return false; // still waiting for events
                }

                // All expected events must be present
                for (const auto& exp: expectedEvents)
                {
                    if (!mNetworkListener.hasEvent(exp.channel, exp.activityType, exp.errorCode))
                    {
                        return false;
                    }
                }

                // No extra events beyond the expected ones
                return requireSizeMatch ? mNetworkListener.size() == expectedEvents.size() : true;
            },
            timeoutMs);
    }

    // Shared driver for the pre-response heartbeat-timeout cases on a long-lived request. The hook
    // forces the first request it sees into the pre-response, no-data state and ages its lastdata
    // by `age` (re-applied every exec() until a reset), so the timeout fires with no real-time wait
    // and the test is deterministic. A reset is observed as a different request id taking over.
    //   age > HEARTBEATTIMEOUT, expectReset = true  -> the stalled request must be reset+retried.
    //   age < HEARTBEATTIMEOUT, expectReset = false -> a still-fresh request must NOT be reset.
    // `installHook(apply)` installs the channel-specific intercept hook; `apply(req)` freezes the
    // request and returns true while it is still the target, false once a different id is seen.
    // `detachServer` detaches the target from the network on first sight (see below) so no real
    // response arrives and no genuine completion/turnover gets misread as a reset. All channels
    // pass it: cs (large prompt response), wsc (fast sc/wsc bootstrap handoff), lockless ("g").
    // Callers wrap this in ASSERT_NO_FATAL_FAILURE (the ASSERTs below only return from the helper).
    template<typename InstallHook, typename ClearHook>
    void runHeartbeatTimeoutCase(dstime age,
                                 bool expectReset,
                                 const char* what,
                                 const InstallHook& installHook,
                                 const ClearHook& clearHook,
                                 const std::function<void()>& trigger,
                                 bool detachServer = false)
    {
        std::mutex mtx;
        bool haveTarget = false;
        uint32_t stalledId = 0;
        bool resetObserved = false;
        std::string capturedUrl;

        installHook(
            [&](HttpReq* req) -> void
            {
                std::lock_guard<std::mutex> g{mtx};
                if (resetObserved || !req)
                {
                    return;
                }
                if (!haveTarget)
                {
                    haveTarget = true;
                    stalledId = req->getId();
                    capturedUrl = req->posturl; // checked on the test thread below
                    if (detachServer)
                    {
                        // Detach this request from the network so no real response arrives that
                        // the test would misread as a heartbeat reset.
                        req->disconnect();
                    }
                }
                if (req->getId() == stalledId)
                {
                    // Freeze in the pre-response, no-heartbeat state and age lastdata by `age`.
                    req->status = REQ_INFLIGHT;
                    req->mResponseStarted = false;
                    req->lastdata = Waiter::ds - age;
                }
                else
                {
                    // A different request took over -> the target was reset and retried.
                    resetObserved = true;
                }
            });

        trigger(); // put a request of this channel in flight to target

        // For the reset case, allow the full timeout to observe the retry. For the no-reset case, a
        // too-eager timeout fires on the first exec(), so a short window with no reset is enough.
        const bool sawReset = WaitFor(
            [&]()
            {
                std::lock_guard<std::mutex> g{mtx};
                return resetObserved;
            },
            expectReset ? defaultTimeoutMs : 6000u);

        clearHook(); // stop before the captured locals go away

        std::lock_guard<std::mutex> g{mtx};
        ASSERT_TRUE(haveTarget) << "no " << what << " request was observed - the test is vacuous";
        EXPECT_NE(capturedUrl.find("&h=1"), std::string::npos)
            << "expected the &h=1 heartbeat flag on the " << what << " URL, got: " << capturedUrl;
        if (expectReset)
        {
            ASSERT_TRUE(sawReset) << "the stalled pre-response " << what
                                  << " request was not reset by the 20s heartbeat timeout";
        }
        else
        {
            ASSERT_FALSE(sawReset) << "a fresh (< 20s stale) " << what
                                   << " request was reset before the 20s heartbeat timeout";
        }
    }

    // wsc (pendingsc) variant: interceptSCRequest + catchup() to put an sc request in flight.
    void runScHeartbeatTimeoutCase(dstime age, bool expectReset)
    {
        runHeartbeatTimeoutCase(
            age,
            expectReset,
            "sc",
            [](const std::function<void(HttpReq*)>& apply)
            {
                globalMegaTestHooks.interceptSCRequest =
                    [apply](std::unique_ptr<HttpReq>& pendingsc)
                {
                    apply(pendingsc.get());
                };
            },
            []()
            {
                globalMegaTestHooks.interceptSCRequest = nullptr;
            },
            [this]()
            {
                megaApi[0]->catchup();
            },
            true /*detachServer: hold the first sc request so the sc/wsc -> wsc bootstrap
                    handoff (a new request id) isn't misread as a reset*/);
    }

    // lockless-CS (mPendingLocklessCS) variant: upload a small file, then start an async download
    // whose idempotent "g" command runs on the lockless CS channel; interceptLocklessCSRequest
    // freezes it. The download is cancelled and the files removed afterwards.
    void runLocklessCsHeartbeatTimeoutCase(dstime age, bool expectReset)
    {
        ASSERT_TRUE(createFile(UPFILE, false)) << "could not create local file " << UPFILE;
        std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
        ASSERT_TRUE(rootnode) << "no root node";
        MegaHandle uploadedHandle = UNDEF;
        ASSERT_EQ(API_OK,
                  doStartUpload(0,
                                &uploadedHandle,
                                UPFILE.c_str(),
                                rootnode.get(),
                                nullptr /*fileName*/,
                                ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                nullptr /*appData*/,
                                false /*isSourceTemporary*/,
                                false /*startFirst*/,
                                nullptr /*cancelToken*/))
            << "upload of " << UPFILE << " failed";
        std::unique_ptr<MegaNode> node{megaApi[0]->getNodeByHandle(uploadedHandle)};
        ASSERT_TRUE(node) << "uploaded node not found";

        ASSERT_NO_FATAL_FAILURE(runHeartbeatTimeoutCase(
            age,
            expectReset,
            "lockless CS",
            [](const std::function<void(HttpReq*)>& apply)
            {
                globalMegaTestHooks.interceptLocklessCSRequest =
                    [apply](std::unique_ptr<HttpReq>& pendingLocklessCS)
                {
                    apply(pendingLocklessCS.get());
                };
            },
            []()
            {
                globalMegaTestHooks.interceptLocklessCSRequest = nullptr;
            },
            [this, &node]()
            {
                // Async (do not wait): the hook keeps the "g" stalled; cancelled below regardless.
                megaApi[0]->startDownload(node.get(),
                                          DOWNFILE.c_str(),
                                          nullptr /*customName*/,
                                          nullptr /*appData*/,
                                          false /*startFirst*/,
                                          nullptr /*cancelToken*/,
                                          MegaTransfer::COLLISION_CHECK_ASSUMEDIFFERENT,
                                          MegaTransfer::COLLISION_RESOLUTION_OVERWRITE,
                                          false /*undelete*/,
                                          nullptr /*listener*/);
            },
            true /*detachServer: hold the "g" so a real completion/turnover isn't misread*/));

        megaApi[0]->cancelTransfers(MegaTransfer::TYPE_DOWNLOAD);
        deleteFile(UPFILE);
        deleteFile(DOWNFILE);
    }

    // main-CS (pendingcs) variant: interceptCSRequest + a read-only cs command (getUserData)
    // to put a request on the main cs channel in flight. No cleanup needed - the command is
    // idempotent and completes/retries once the hook is cleared.
    void runCsHeartbeatTimeoutCase(dstime age, bool expectReset)
    {
        runHeartbeatTimeoutCase(
            age,
            expectReset,
            "cs",
            [](const std::function<void(HttpReq*)>& apply)
            {
                globalMegaTestHooks.interceptCSRequest = [apply](HttpReq* pendingcs)
                {
                    apply(pendingcs);
                };
            },
            []()
            {
                globalMegaTestHooks.interceptCSRequest = nullptr;
            },
            [this]()
            {
                megaApi[0]->getUserData();
            },
            true /*detachServer: cs returns a real response promptly, detach it from the network*/);
    }
};

/**
 * @brief Test: Process API_ESID
 */
TEST_P(SdkTestScChannel, ProcessSidError)
{
    CASE_info << "started";

    installErrorResponseHook(API_ESID);

    // Force SC channel catchup to trigger the hook
    megaApi[0]->catchup();

    // Prepare to detect the logout triggered by request_error(API_ESID)
    mApi[0].requestFlags[MegaRequest::TYPE_LOGOUT] = false;

    // Verify events
    EXPECT_TRUE(waitForNetworkEvents({{MegaEvent::SC, MegaEvent::REQUEST_RECEIVED, API_ESID}},
                                     defaultTimeoutMs))
        << "Not expected events received";

    // Verify logout should be triggered
    EXPECT_TRUE(waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_LOGOUT], defaultTimeoutMs))
        << "Expected logout";

    CASE_info << "finished";
}

/**
 * @brief Test: Process API_ENOENT when logged into a folder link
 */
TEST_P(SdkTestScChannel, ProcessNoEntryErrorInFolderLink)
{
    CASE_info << "started";

    // Create a folder and export it as a public link
    std::unique_ptr<MegaNode> rootNode{megaApi[0]->getRootNode()};
    ASSERT_THAT(rootNode, ::testing::NotNull());

    auto folderHandle = createFolder(0, "TestFolder", rootNode.get());
    ASSERT_NE(folderHandle, UNDEF);

    std::unique_ptr<MegaNode> folderNode{megaApi[0]->getNodeByHandle(folderHandle)};
    ASSERT_THAT(folderNode, ::testing::NotNull());

    const auto folderLink = createPublicLink(0, folderNode.get(), 0, maxTimeout, false);

    // Logout, then re-login via the folder link
    logout(0, false, maxTimeout);

    auto loginTracker = asyncRequestLoginToFolder(0u, folderLink.c_str());
    ASSERT_EQ(loginTracker->waitForResult(), API_OK) << "Failed to login to folder";
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    installErrorResponseHook(API_ENOENT);

    // Force SC channel catchup to trigger the hook
    megaApi[0]->catchup();

    // Prepare to detect the logout triggered by request_error(API_ENOENT)
    mApi[0].requestFlags[MegaRequest::TYPE_LOGOUT] = false;

    // Verify events
    EXPECT_TRUE(waitForNetworkEvents({{MegaEvent::SC, MegaEvent::REQUEST_RECEIVED, API_ENOENT}},
                                     defaultTimeoutMs))
        << "Not expected events received";

    EXPECT_TRUE(waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_LOGOUT], defaultTimeoutMs))
        << "Expected logout";

    CASE_info << "finished";
}

/**
 * @brief Test: Process API_ETOOMANY
 */
TEST_P(SdkTestScChannel, ProcessTooManyError)
{
    CASE_info << "started";

    mApi[0].resetlastEvent();

    installErrorResponseHook(API_ETOOMANY);

    // Force SC channel catchup to trigger the hook
    megaApi[0]->catchup();

    // Verify events
    EXPECT_TRUE(waitForNetworkEvents({{MegaEvent::SC, MegaEvent::REQUEST_RECEIVED, API_ETOOMANY}},
                                     defaultTimeoutMs))
        << "Not expected events received";

    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return mApi[0].lastEventsContain(MegaEvent::EVENT_RELOADING);
        },
        defaultTimeoutMs))
        << "Expected EVENT_RELOADING";

    CASE_info << "finished";
}

/**
 * @brief Test: Process API_EAGAIN
 */
TEST_P(SdkTestScChannel, ProcessAgainError)
{
    CASE_info << "started";

    installErrorResponseHook(API_EAGAIN);

    // Force SC channel catchup to trigger the hook
    megaApi[0]->catchup();

    // Verify events
    // The subsequent SC request could trigger another network event, so can't expect the network
    // events number here.
    EXPECT_TRUE(waitForNetworkEvents({{MegaEvent::SC, MegaEvent::REQUEST_RECEIVED, API_EAGAIN}},
                                     defaultTimeoutMs,
                                     false))
        << "Not expected events received";

    CASE_info << "finished";
}

/**
 * @brief Test: Process API_ERATELIMIT
 */
TEST_P(SdkTestScChannel, ProcessRateLimitError)
{
    CASE_info << "started";

    installErrorResponseHook(API_ERATELIMIT);

    // Force SC channel catchup to trigger the hook
    megaApi[0]->catchup();

    // Verify events
    // The subsequent SC request could trigger another network event, so can't expect the network
    // events number here.
    EXPECT_TRUE(waitForNetworkEvents({{MegaEvent::SC, MegaEvent::REQUEST_RECEIVED, API_ERATELIMIT}},
                                     defaultTimeoutMs,
                                     false))
        << "Not expected events received";

    CASE_info << "finished";
}

/**
 * @brief Test: Process API_EBLOCKED
 */
TEST_P(SdkTestScChannel, ProcessBlockedError)
{
    CASE_info << "started";

    installErrorResponseHook(API_EBLOCKED);

    // Force SC channel catchup to trigger the hook
    megaApi[0]->catchup();

    // Verify events
    // The subsequent SC request could trigger another network event, so can't expect the network
    // events number here.
    EXPECT_TRUE(waitForNetworkEvents({{MegaEvent::SC, MegaEvent::REQUEST_RECEIVED, API_EBLOCKED}},
                                     defaultTimeoutMs,
                                     false))
        << "Not expected events received";

    EXPECT_TRUE(waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_WHY_AM_I_BLOCKED],
                                defaultTimeoutMs))
        << "Not expected events received";

    CASE_info << "finished";
}

/**
 * @brief Test: Process unexpected error code
 */
TEST_P(SdkTestScChannel, ProcessUnexpectedError)
{
    CASE_info << "started";

    installErrorResponseHook(1);

    // Force SC channel catchup to trigger the hook
    megaApi[0]->catchup();

    // Verify events
    EXPECT_TRUE(
        waitForNetworkEvents({{MegaEvent::SC, MegaEvent::REQUEST_ERROR, 1}}, defaultTimeoutMs))
        << "Not expected events received";

    EXPECT_TRUE(megaApi[0]->getClient()->scsn.stopped()) << "Expected SCSN to be stopped";

    CASE_info << "finished";
}

/**
 * @brief Test: Process SSL failure without retry
 */
TEST_P(SdkTestScChannel, ProcessSslFailureWithoutRetry)
{
    CASE_info << "started";

    // Ensure retryessl is disabled (default)
    megaApi[0]->retrySSLerrors(false);

    installSslFailureHook();

    // Force SC channel catchup to trigger the hook
    megaApi[0]->catchup();

    // Verify events
    EXPECT_TRUE(waitForNetworkEvents({{MegaEvent::SC, MegaEvent::REQUEST_ERROR, API_ESSL}},
                                     defaultTimeoutMs))
        << "Not expected events received";

    // Verify logout should be triggered
    EXPECT_TRUE(waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_LOGOUT], defaultTimeoutMs))
        << "Expected logout";

    EXPECT_TRUE(megaApi[0]->getClient()->sslfakeissuer.empty())
        << "Expected sslfakeissuer to be cleared";

    CASE_info << "finished";
}

/**
 * @brief Test: Process SSL failure with retry
 */
TEST_P(SdkTestScChannel, ProcessSslFailureWithRetry)
{
    CASE_info << "started";

    // Enable retryessl
    megaApi[0]->retrySSLerrors(true);

    installSslFailureHook();

    // Force SC channel catchup to trigger the hook
    megaApi[0]->catchup();

    // Verify events
    // The subsequent SC request could trigger another network event, so can't expect the network
    // events number here.
    EXPECT_TRUE(waitForNetworkEvents({{MegaEvent::SC, MegaEvent::REQUEST_RECEIVED, 500}},
                                     defaultTimeoutMs,
                                     false))
        << "Not expected events received";

    EXPECT_TRUE(megaApi[0]->getClient()->sslfakeissuer.empty())
        << "Expected sslfakeissuer to be cleared";

    CASE_info << "finished";
}

/**
 * @brief Test: Process DNS failure
 */
TEST_P(SdkTestScChannel, ProcessDnsFailure)
{
    CASE_info << "started";

    // Enable retryessl
    megaApi[0]->retrySSLerrors(true);

    installDnsFailureHook();

    // Force SC channel catchup to trigger the hook
    megaApi[0]->catchup();

    // Verify events
    // The subsequent SC request could trigger another network event, so can't expect the network
    // events number here.
    EXPECT_TRUE(waitForNetworkEvents({{MegaEvent::SC, MegaEvent::REQUEST_SENT, LOCAL_ENETWORK}},
                                     defaultTimeoutMs,
                                     false))
        << "Not expected events received";

    CASE_info << "finished";
}

/**
 * @brief Test: the wsc URL carries the &h=1 heartbeat flag so the server enables
 *        the HTTP 103 heartbeats.
 */
TEST_P(SdkTestScChannel, HeartbeatFlagPresentOnScUrl)
{
    CASE_info << "started";

    std::mutex urlMutex;
    std::string capturedUrl;
    globalMegaTestHooks.interceptSCRequest =
        [&urlMutex, &capturedUrl](std::unique_ptr<HttpReq>& pendingsc)
    {
        std::lock_guard<std::mutex> g{urlMutex};
        if (capturedUrl.empty())
        {
            capturedUrl = pendingsc->posturl;
            globalMegaTestHooks.interceptSCRequest = nullptr;
        }
    };

    // Force an sc/wsc request so the hook fires
    megaApi[0]->catchup();

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            std::lock_guard<std::mutex> g{urlMutex};
            return !capturedUrl.empty();
        },
        defaultTimeoutMs))
        << "SC request hook did not fire";

    std::string url;
    {
        std::lock_guard<std::mutex> g{urlMutex};
        url = capturedUrl;
    }
    CASE_info << "captured sc url: " << url;

    EXPECT_NE(url.find("wsc"), std::string::npos) << "expected a wsc URL, got: " << url;
    EXPECT_NE(url.find("&h=1"), std::string::npos)
        << "expected the &h=1 heartbeat flag on the sc URL, got: " << url;

    CASE_info << "finished";
}

/**
 * @brief Test: support for the legacy wsc "0" keep-alive response has been removed.
 *        A "0" response is now treated as an unexpected SC response (REQUEST_ERROR),
 *        instead of being silently swallowed as a keep-alive.
 */
TEST_P(SdkTestScChannel, ZeroResponseNoLongerKeepAlive)
{
    CASE_info << "started";

    // Inject a legacy "0" keep-alive response, exactly as the old server used to send it
    // (Content-Length: 1, body "0"). Before SDK-6336 this was swallowed silently; now it must
    // surface as an unexpected SC error.
    globalMegaTestHooks.interceptSCRequest = [this](std::unique_ptr<HttpReq>& pendingsc)
    {
        LOG_info << "SC channel hook: injecting legacy '0' keep-alive response";
        mNetworkListener.clear();
        pendingsc->status = REQ_SUCCESS;
        pendingsc->httpstatus = 200;
        pendingsc->contentlength = 1;
        pendingsc->in = "0";
        globalMegaTestHooks.interceptSCRequest = nullptr;
    };

    // Force SC channel catchup to trigger the hook
    megaApi[0]->catchup();

    // A "0" parses to error code 0 and hits the "Unexpected sc response" path.
    EXPECT_TRUE(waitForNetworkEvents({{MegaEvent::SC, MegaEvent::REQUEST_ERROR, API_OK}},
                                     defaultTimeoutMs,
                                     false))
        << "Expected the '0' response to be treated as an unexpected SC error";

    CASE_info << "finished";
}

/**
 * @brief Test: a stalled pre-response sc request is reset by the new 20s heartbeat timeout. The
 *        request is aged (via the hook) past HEARTBEATTIMEOUT (20s) but short of the legacy
 *        SCREQUESTTIMEOUT (40s), so the reset proves the 20s heartbeat path is in effect - it would
 *        not fire if the pre-response phase still used the 40s timeout.
 */
TEST_P(SdkTestScChannel, HeartbeatTimeoutResetsStalledScRequest)
{
    CASE_info << "started";
    ASSERT_NO_FATAL_FAILURE(
        runScHeartbeatTimeoutCase((HttpIO::HEARTBEATTIMEOUT + HttpIO::SCREQUESTTIMEOUT) / 2,
                                  /*expectReset*/ true));
    CASE_info << "finished";
}

/**
 * @brief Test: lower-bound counterpart - a pre-response sc request only HEARTBEATTIMEOUT/2 (10s)
 *        stale must NOT be reset, guarding against the timeout firing too eagerly.
 */
TEST_P(SdkTestScChannel, HeartbeatTimeoutDoesNotResetFreshScRequest)
{
    CASE_info << "started";
    ASSERT_NO_FATAL_FAILURE(
        runScHeartbeatTimeoutCase(HttpIO::HEARTBEATTIMEOUT / 2, /*expectReset*/ false));
    CASE_info << "finished";
}

/**
 * @brief Test: lockless-CS counterpart. A file download issues the idempotent "g" command on the
 *        lockless CS channel; aged past HEARTBEATTIMEOUT (20s) but short of REQUESTTIMEOUT (120s),
 *        it must be reset+retried. Runs the real client so mReqsLockless has a genuine in-flight
 *        request (inflightFailure's invariants hold), unlike the megaclient_test unit tests.
 */
TEST_P(SdkTestScChannel, HeartbeatTimeoutResetsStalledLocklessCsRequest)
{
    CASE_info << "started";
    ASSERT_NO_FATAL_FAILURE(
        runLocklessCsHeartbeatTimeoutCase((HttpIO::HEARTBEATTIMEOUT + HttpIO::REQUESTTIMEOUT) / 2,
                                          /*expectReset*/ true));
    CASE_info << "finished";
}

/**
 * @brief Test: lower-bound counterpart of the lockless-CS case - a "g" request only
 *        HEARTBEATTIMEOUT/2 (10s) stale must NOT be reset.
 */
TEST_P(SdkTestScChannel, HeartbeatTimeoutDoesNotResetFreshLocklessCsRequest)
{
    CASE_info << "started";
    ASSERT_NO_FATAL_FAILURE(
        runLocklessCsHeartbeatTimeoutCase(HttpIO::HEARTBEATTIMEOUT / 2, /*expectReset*/ false));
    CASE_info << "finished";
}

/**
 * @brief Test: main-CS counterpart. A read-only cs command (getUserData) puts a request on the
 *        cs channel (pendingcs); aged past HEARTBEATTIMEOUT (20s) but short of REQUESTTIMEOUT
 *        (120s) it must be reset+retried. This covers the pendingcs heartbeat path (fires even
 *        during fetchingnodes) that previously had only live/manual verification.
 */
TEST_P(SdkTestScChannel, HeartbeatTimeoutResetsStalledCsRequest)
{
    CASE_info << "started";
    ASSERT_NO_FATAL_FAILURE(
        runCsHeartbeatTimeoutCase((HttpIO::HEARTBEATTIMEOUT + HttpIO::REQUESTTIMEOUT) / 2,
                                  /*expectReset*/ true));
    CASE_info << "finished";
}

/**
 * @brief Test: lower-bound counterpart of the main-CS case - a cs request only HEARTBEATTIMEOUT/2
 *        (10s) stale must NOT be reset.
 */
TEST_P(SdkTestScChannel, HeartbeatTimeoutDoesNotResetFreshCsRequest)
{
    CASE_info << "started";
    ASSERT_NO_FATAL_FAILURE(
        runCsHeartbeatTimeoutCase(HttpIO::HEARTBEATTIMEOUT / 2, /*expectReset*/ false));
    CASE_info << "finished";
}

/**
 * @brief Test: live end-to-end check that real HTTP 103 heartbeats are received on the wsc
 *        long-poll. The wsc request idles while the server holds it, so a 103-capable server
 *        emits a heartbeat every interval - waiting a window well beyond the interval yields a
 *        heartbeat reliably (no timing coin-flip).
 */
TEST_P(SdkTestScChannel, ReceivesServerHeartbeatLive)
{
    CASE_info << "started";

    std::mutex mtx;
    int total = 0;
    uint32_t currentId = 0;
    dstime firstDsForCurrent = 0;
    dstime maxSpan = 0; // longest heartbeat span (ds) seen on a single request id

    globalMegaTestHooks.onHeartbeatReceived = [&](int /*statusCode*/, uint32_t reqId)
    {
        std::lock_guard<std::mutex> g{mtx};
        ++total;
        const dstime now = Waiter::ds;
        if (reqId != currentId) // first heartbeat we see for this request
        {
            currentId = reqId;
            firstDsForCurrent = now;
        }
        if (now - firstDsForCurrent > maxSpan)
        {
            maxSpan = now - firstDsForCurrent;
        }
    };

    // Login/fetchnodes are done; the sc channel is now long-polling wsc. Wait until a single wsc
    // request (same id) has received heartbeats spanning more than HEARTBEATTIMEOUT. That proves
    // the heartbeats actually kept that request alive past the 20s heartbeat timeout: had the
    // timeout fired, the request would have been reset and got a new id, restarting the span.
    // (Requires a server that holds the wsc long-poll past the window.)
    const bool survived = WaitFor(
        [&]()
        {
            std::lock_guard<std::mutex> g{mtx};
            return maxSpan >= static_cast<dstime>(HttpIO::HEARTBEATTIMEOUT);
        },
        90000);

    globalMegaTestHooks.onHeartbeatReceived = nullptr; // stop before the captured locals go away

    std::lock_guard<std::mutex> g{mtx};
    ASSERT_GT(total, 0) << "No HTTP 103 heartbeat received on the wsc channel";
    ASSERT_TRUE(survived) << "A single wsc request did not stay alive across the 20s heartbeat "
                             "window - the heartbeats did not defer the timeout (received "
                          << total << " heartbeat(s), max single-request span " << maxSpan
                          << " ds, need >= " << HttpIO::HEARTBEATTIMEOUT << ")";
    CASE_info << "finished (" << total << " heartbeat(s), max span " << maxSpan << " ds)";
}

INSTANTIATE_TEST_SUITE_P(ScChannel,
                         SdkTestScChannel,
                         ::testing::Bool(),
                         [](const ::testing::TestParamInfo<bool>& info)
                         {
                             return info.param ? "Streaming" : "NonStreaming";
                         });

#endif // MEGASDK_DEBUG_TEST_HOOKS_ENABLED
