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
 * @brief Test: a stalled pre-response sc request is reset by the new 15s heartbeat timeout.
 *        The request is forced to look in-flight with no data, and its lastdata is aged
 *        (via the hook) to a point that is past HEARTBEATTIMEOUT (15s) but short of the legacy
 *        SCREQUESTTIMEOUT (40s). So the reset proves the 15s heartbeat path is in effect - it
 *        would NOT fire if the pre-response phase still used the 40s timeout. Ageing lastdata
 *        makes the timeout fire on the next exec() with no real-time wait, keeping the test
 *        deterministic. The reset is observed as a different sc request (new id) taking over.
 */
TEST_P(SdkTestScChannel, HeartbeatTimeoutResetsStalledScRequest)
{
    CASE_info << "started";

    // Midpoint of the two timeouts: safely > HEARTBEATTIMEOUT (15s) and < SCREQUESTTIMEOUT (40s).
    const dstime age = (HttpIO::HEARTBEATTIMEOUT + HttpIO::SCREQUESTTIMEOUT) / 2;

    std::mutex mtx;
    bool haveTarget = false;
    uint32_t stalledId = 0;
    bool resetObserved = false;

    // Runs on the client thread inside exec() (chooseScParsingMode), so there is no cross-thread
    // access to pendingsc; only the small shared state below is guarded by the mutex.
    globalMegaTestHooks.interceptSCRequest = [&](std::unique_ptr<HttpReq>& pendingsc)
    {
        std::lock_guard<std::mutex> g{mtx};
        if (resetObserved)
        {
            return;
        }
        if (!haveTarget)
        {
            haveTarget = true;
            stalledId = pendingsc->getId();
        }

        if (pendingsc->getId() == stalledId)
        {
            // Force the pre-response, no-heartbeat condition and age lastdata past the heartbeat
            // timeout. Re-applied every time the hook sees this request until it is reset.
            pendingsc->status = REQ_INFLIGHT;
            pendingsc->mResponseStarted = false;
            pendingsc->in.clear();
            pendingsc->lastdata = Waiter::ds - age;
        }
        else
        {
            // A different sc request is now in flight -> the stalled one was reset and retried.
            resetObserved = true;
        }
    };

    // Ensure there is an sc request in flight to target.
    megaApi[0]->catchup();

    const bool ok = WaitFor(
        [&]()
        {
            std::lock_guard<std::mutex> g{mtx};
            return resetObserved;
        },
        defaultTimeoutMs);

    globalMegaTestHooks.interceptSCRequest = nullptr; // stop before the captured locals go away

    ASSERT_TRUE(ok)
        << "The stalled pre-response sc request was not reset by the 15s heartbeat timeout";

    CASE_info << "finished";
}

/**
 * @brief Test: lower-bound counterpart of HeartbeatTimeoutResetsStalledScRequest. The pre-response
 *        sc request is held in flight but its lastdata is aged only to HEARTBEATTIMEOUT/2 (7.5s) -
 *        short of the 15s heartbeat timeout - so it must NOT be reset. Guards against the timeout
 *        firing too eagerly (a mis-set HEARTBEATTIMEOUT or wrong comparison) which would churn a
 *        still-fresh sc request. The hook keeps re-pinning lastdata below the threshold every
 *        exec(), so elapsed real time never crosses it: waiting a short window and seeing no reset
 *        (no new request id) is enough - no need to wait 15s.
 */
TEST_P(SdkTestScChannel, HeartbeatTimeoutDoesNotResetFreshScRequest)
{
    CASE_info << "started";

    // Half the heartbeat timeout: safely < HEARTBEATTIMEOUT (15s), so the timeout must not fire.
    const dstime age = HttpIO::HEARTBEATTIMEOUT / 2;

    std::mutex mtx;
    bool haveTarget = false;
    uint32_t stalledId = 0;
    bool resetObserved = false;

    // Same freezing mechanism as the positive test, but with a sub-threshold age. Runs on the
    // client thread inside exec() (chooseScParsingMode); only the small shared state below is
    // guarded.
    globalMegaTestHooks.interceptSCRequest = [&](std::unique_ptr<HttpReq>& pendingsc)
    {
        std::lock_guard<std::mutex> g{mtx};
        if (resetObserved)
        {
            return;
        }
        if (!haveTarget)
        {
            haveTarget = true;
            stalledId = pendingsc->getId();
        }

        if (pendingsc->getId() == stalledId)
        {
            // Keep the request pre-response and in flight, but only mildly stale (< 15s). This also
            // prevents it from completing naturally, so a new id could only mean a premature reset.
            pendingsc->status = REQ_INFLIGHT;
            pendingsc->mResponseStarted = false;
            pendingsc->in.clear();
            pendingsc->lastdata = Waiter::ds - age;
        }
        else
        {
            // A different sc request took over -> the fresh request was reset prematurely.
            resetObserved = true;
        }
    };

    // Ensure there is an sc request in flight to target.
    megaApi[0]->catchup();

    // Observe a window spanning many exec() cycles. A too-eager timeout fires on the first exec, so
    // a few seconds with no reset is strong evidence. resetHappened must stay false (WaitFor times
    // out); if it returns true the fresh request was wrongly reset.
    const bool resetHappened = WaitFor(
        [&]()
        {
            std::lock_guard<std::mutex> g{mtx};
            return resetObserved;
        },
        6000);

    globalMegaTestHooks.interceptSCRequest = nullptr; // stop before the captured locals go away

    std::lock_guard<std::mutex> g{mtx};
    ASSERT_TRUE(haveTarget) << "no sc request was observed - the test would be vacuous";
    ASSERT_FALSE(resetHappened)
        << "a fresh (< 15s stale) sc request was reset before the 15s heartbeat timeout";

    CASE_info << "finished";
}

/**
 * @brief Test: lockless-CS counterpart of HeartbeatTimeoutResetsStalledScRequest. A file download
 *        issues the idempotent "g" command on the lockless CS channel (mPendingLocklessCS). While
 *        that request is in flight and before its response starts, ageing its lastdata past
 *        HEARTBEATTIMEOUT (15s) - but short of the post-response REQUESTTIMEOUT (120s), so the
 *        reset can only be the 15s heartbeat path - must reset and retry it.
 *        Unlike the megaclient_test unit tests, this runs the real client so mReqsLockless
 *        genuinely has an in-flight request (inflightFailure's invariants hold).
 *        The reset is observed as a different lockless request (new id) taking over.
 *        Ageing lastdata makes the timeout fire on the next exec() with no real-time wait,
 *        keeping the test deterministic.
 */
TEST_P(SdkTestScChannel, HeartbeatTimeoutResetsStalledLocklessCsRequest)
{
    CASE_info << "started";

    // Upload a small file so we can download it; the download issues the lockless "g".
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

    // Midpoint of the two timeouts: safely > HEARTBEATTIMEOUT (15s) and < REQUESTTIMEOUT (120s).
    const dstime age = (HttpIO::HEARTBEATTIMEOUT + HttpIO::REQUESTTIMEOUT) / 2;

    std::mutex mtx;
    bool haveTarget = false;
    uint32_t stalledId = 0;
    bool resetObserved = false;

    // Runs on the client thread inside exec(), so there is no cross-thread access to
    // mPendingLocklessCS; only the small shared state below is guarded by the mutex.
    globalMegaTestHooks.interceptLocklessCSRequest =
        [&](std::unique_ptr<HttpReq>& pendingLocklessCS)
    {
        std::lock_guard<std::mutex> g{mtx};
        if (resetObserved || !pendingLocklessCS)
        {
            return;
        }
        if (!haveTarget)
        {
            haveTarget = true;
            stalledId = pendingLocklessCS->getId();
        }

        if (pendingLocklessCS->getId() == stalledId)
        {
            // Force the pre-response, no-heartbeat condition and age lastdata past the heartbeat
            // timeout. Re-applied every time the hook sees this request until it is reset.
            pendingLocklessCS->status = REQ_INFLIGHT;
            pendingLocklessCS->mResponseStarted = false;
            pendingLocklessCS->in.clear();
            pendingLocklessCS->lastdata = Waiter::ds - age;
        }
        else
        {
            // A different lockless request is now in flight -> the stalled one was reset and
            // retried.
            resetObserved = true;
        }
    };

    // Start the download asynchronously (do not wait: the hook keeps the "g" stalled). No listener,
    // so nothing on this stack outlives the transfer - it is cancelled below regardless.
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

    const bool ok = WaitFor(
        [&]()
        {
            std::lock_guard<std::mutex> g{mtx};
            return resetObserved;
        },
        defaultTimeoutMs);

    globalMegaTestHooks.interceptLocklessCSRequest =
        nullptr; // stop before the captured locals go away

    megaApi[0]->cancelTransfers(MegaTransfer::TYPE_DOWNLOAD);
    deleteFile(UPFILE);
    deleteFile(DOWNFILE);

    ASSERT_TRUE(ok) << "The stalled pre-response lockless CS request was not reset by the 15s "
                       "heartbeat timeout";

    CASE_info << "finished";
}

/**
 * @brief Test: lower-bound counterpart of HeartbeatTimeoutResetsStalledLocklessCsRequest. Holds the
 *        download's lockless "g" request in the pre-response state but ages its lastdata only to
 *        HEARTBEATTIMEOUT/2 (7.5s) - short of the 15s heartbeat timeout - so the request must NOT
 *        be reset. Guards against the timeout firing too eagerly (e.g. a mis-set HEARTBEATTIMEOUT
 *        or a wrong comparison) which would churn a still-fresh request. The hook keeps re-pinning
 *        lastdata below the threshold every exec(), so elapsed real time never crosses it: waiting
 *        a short window and seeing no reset (no new request id) is enough - no need to wait 15s.
 */
TEST_P(SdkTestScChannel, HeartbeatTimeoutDoesNotResetFreshLocklessCsRequest)
{
    CASE_info << "started";

    // Upload a small file so we can download it; the download issues the lockless "g".
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

    // Half the heartbeat timeout: safely < HEARTBEATTIMEOUT (15s), so the timeout must not fire.
    const dstime age = HttpIO::HEARTBEATTIMEOUT / 2;

    std::mutex mtx;
    bool haveTarget = false;
    uint32_t stalledId = 0;
    bool resetObserved = false;

    // Same freezing mechanism as the positive test, but with a sub-threshold age. Runs on the
    // client thread inside exec(); only the small shared state below is guarded by the mutex.
    globalMegaTestHooks.interceptLocklessCSRequest =
        [&](std::unique_ptr<HttpReq>& pendingLocklessCS)
    {
        std::lock_guard<std::mutex> g{mtx};
        if (resetObserved || !pendingLocklessCS)
        {
            return;
        }
        if (!haveTarget)
        {
            haveTarget = true;
            stalledId = pendingLocklessCS->getId();
        }

        if (pendingLocklessCS->getId() == stalledId)
        {
            // Keep the request pre-response and in flight, but only mildly stale (< 15s). This also
            // prevents it from completing naturally, so a new id could only mean a premature reset.
            pendingLocklessCS->status = REQ_INFLIGHT;
            pendingLocklessCS->mResponseStarted = false;
            pendingLocklessCS->in.clear();
            pendingLocklessCS->lastdata = Waiter::ds - age;
        }
        else
        {
            // A different lockless request took over -> the fresh request was reset prematurely.
            resetObserved = true;
        }
    };

    // Start the download asynchronously (the hook keeps the "g" frozen). No listener, so nothing on
    // this stack outlives the transfer - it is cancelled below regardless.
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

    // Observe a window spanning many exec() cycles. A too-eager timeout fires on the first exec, so
    // a few seconds with no reset is strong evidence. resetHappened must stay false (WaitFor times
    // out); if it returns true the fresh request was wrongly reset.
    const bool resetHappened = WaitFor(
        [&]()
        {
            std::lock_guard<std::mutex> g{mtx};
            return resetObserved;
        },
        6000);

    globalMegaTestHooks.interceptLocklessCSRequest =
        nullptr; // stop before the captured locals go away

    megaApi[0]->cancelTransfers(MegaTransfer::TYPE_DOWNLOAD);
    deleteFile(UPFILE);
    deleteFile(DOWNFILE);

    std::lock_guard<std::mutex> g{mtx};
    ASSERT_TRUE(haveTarget)
        << "the lockless g request was never observed - the test would be vacuous";
    ASSERT_FALSE(resetHappened)
        << "a fresh (< 15s stale) lockless CS request was reset before the 15s heartbeat timeout";

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
    // the heartbeats actually kept that request alive past the 15s heartbeat timeout: had the
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
    ASSERT_TRUE(survived) << "A single wsc request did not stay alive across the 15s heartbeat "
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
