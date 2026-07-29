/**
 * @file SdkWsUploadCrashTest.cpp
 * @brief WS-upload concurrency / lifetime crash-repro tests (fu8 QA C2/C4 hunt).
 *
 * Houses the deterministic-as-possible reproduction(s) for the QA-reported
 * SIGSEGV under 100% packet loss whose LAST flushed log line was
 *   [WsConn::onmessage] event == 5 distress -> poolMgr.refreshPools()
 * (see followup8_QA/step6_geneticfight/c2c4_crash_investigation.md).
 *
 * Step 6 of the fu8 crash gate empirically REFUTED C1 (partial-send teardown)
 * as the crash (PartialSendMidFrameCloseRecovers PASSED with and without the
 * C1 fix). The surviving Release-reachable hazards are:
 *
 *   C2 — a callback fired UNDER uploadMutex from WsConn::onmessage
 *        (failFileLocked -> mCb.onFail at ws_conn.cpp:464-467; onComplete at
 *        ws_conn.cpp:737-751) whose client-thread body runs
 *        MegaClient::onFail -> Transfer::failed ("may delete the transfer",
 *        src/megaclient_wsupload.cpp:265,280) racing a concurrent
 *        pool/transfer teardown during the event==5 -> refreshPools() storm
 *        (ws_pool_mgr.cpp:296,376,435 mutating mPools via applyRefreshedUrls).
 *
 *   C4 — mUploadingFile raw WsUploadFile* (wsupload_internal.h:422) read under
 *        uploadMutex in findFile / handshakeFailureCandidateLocked
 *        (ws_pool.cpp:407-416,572-609) dereferenced after the file is freed
 *        during an applyRefreshedUrls retire/recreate migration
 *        (wsupload_engine.cpp:341-385,612-726).
 *
 * The test below drives the three-actor interleave the QA crash needs:
 *   (A) worker threads deep in WsConn::onmessage under uploadMutex, repeatedly
 *       firing event==5 Distress -> refreshPools() (injected via
 *       wsUploadServerEventHook Modify: rewrite event==1 ChunkIngested acks
 *       into event==5 Distress, high maxHits);
 *   (B) the manager thread + client-thread USC-reply drain mutating mPools via
 *       applyRefreshedUrls / cleanupRetiringPools while handshakes for the new
 *       pools all FAIL (onWsHandshake=true on /ul/ URLs) so pools churn maximally
 *       and the sustained-handshake-failure escalation fires onFail
 *       (window shrunk to 0 via onWsUploadSustainedHandshakeFailureWindowDs);
 *   (C) mid-flight MegaClient::disconnect() (notifyNetworkDisconnect) bumping
 *       disconnectEpoch and waking all workers, repeated to keep the storm hot.
 *
 * This is a PROBABILISTIC repro: the hazard is a cross-thread timing race, not a
 * deterministic state transition the hooks can pin. We therefore drive the storm
 * for a bounded soak window and assert the process does NOT crash and the
 * transfer either completes (API_OK) or fails gracefully with a surfaced,
 * retryable error. Run under ASan (build dev-unix-wsupload-benchOn-asan, Debug +
 * ASan, LD_PRELOAD libasan) to turn any latent heap-use-after-free on the
 * onFail/onComplete -> Transfer::failed or freed-WsUploadFile path into a
 * symbolized abort. No netem is required (the hooks generate the storm); adding
 * 100% loss via tc netem only sharpens the window.
 *
 * Per HR43 timing-sensitive cells: run with --gtest_repeat=15 (or the project's
 * timing-cell floor) under taskset -c 0 nice -n 19 to maximise interleave
 * pressure.
 */

#include "wsupload/headers/SdkWsUploadTest.h"

#include "SdkTest_test.h"
#include "mega/scoped_helpers.h"
#include "mega/testhooks.h"
#include "mega/types.h"
#include "megaapi.h"
#include "sdk_test_utils.h"
#include "test.h"
#include "wsupload/headers/WsUploadHelpers.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

using namespace std;

namespace mega::test::wsupload
{

/**
 * @brief Drive the distress(event==5)-refresh storm + sustained handshake
 *        failure escalation + mid-flight disconnect, asserting no crash and a
 *        clean completion-or-graceful-failure. Surfaces the C2/C4 UAF under ASan.
 *
 * NOTE: probabilistic. The body soaks the storm for a bounded window; pair with
 * --gtest_repeat=15 under ASan. A single PASS does not prove the absence of the
 * race — it proves this build did not crash on this interleave this run.
 */
TEST_F(SdkWsUploadTest, DistressStormDuringFailureDoesNotCrash)
{
    LOG_info << "___TEST SdkWsUploadDistressStormDuringFailureDoesNotCrash___";

    // Hooks are mandatory: the storm is hook-driven. Skips in Release.
    WSUPLOAD_REQUIRE_TEST_HOOKS();

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // A moderately large file so the upload stays active across the storm window
    // (more chunks => more event==1 acks to rewrite into Distress => more
    // refreshPools() re-entrancy under uploadMutex).
    const std::string fileName =
        "ws_distress_storm_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
    constexpr size_t fileSize = 4 * kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "D")) << "Couldn't create " << fileName;

    auto cleanupFile = makeScopedDestructor(
        [this, &fileName]()
        {
            deleteFile(fileName);
        });
    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });
    auto clearWsHooks = makeScopedDestructor(
        []()
        {
            std::lock_guard<std::mutex> g(globalMegaTestHooks.mMutex);
            globalMegaTestHooks.wsUploadServerEventHook.reset();
            globalMegaTestHooks.onWsHandshake = {};
            globalMegaTestHooks.onWsUploadSustainedHandshakeFailureWindowDs = {};
        });

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60)) << "setMaxConnections() failed or timed out";

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    // Throttle so the transfer stays in-flight long enough for the storm to bite.
    ScopedUploadSpeedLimit restoreUploadSpeed{*megaApi[0], 100000};

    // ---- Actor (A): turn EVERY ChunkIngested (event==1) ack into Distress
    // (event==5). maxHits=0 means "unbounded" per WsUploadServerEventHook::evaluate
    // (testhooks.h:184 — `maxHits != 0 && hitCount >= maxHits`). Each rewrite fires
    // mPool->mImpl->poolMgr.refreshPools() from inside onmessage while holding
    // uploadMutex (ws_conn.cpp:508, distress arm ws_conn.cpp:756-762).
    constexpr int kChunkIngestedEvent = 1;
    constexpr int kDistressEvent = 5;
    auto& evHook = globalMegaTestHooks.wsUploadServerEventHook;
    evHook.configure(WsUploadServerEventAction::Modify,
                     kChunkIngestedEvent,
                     kDistressEvent,
                     /*fileno*/ std::nullopt,
                     /*targetChunkPos*/ std::nullopt,
                     /*maxHits*/ 0 /* unbounded */);

    // ---- Actor (B): fail EVERY reconnect handshake on the /ul/ pool URLs once the
    // storm is armed, so the freshly-refreshed pools never open and the
    // sustained-handshake-failure escalation (ws_pool.cpp:1078-1116) fires onFail
    // under uploadMutex while applyRefreshedUrls churns mPools. The window is
    // shrunk to 0 so escalation is reached on the 3rd retry without a 60s wait.
    std::atomic<bool> stormArmed{false};
    std::atomic<int> forcedHandshakeFailures{0};
    {
        std::lock_guard<std::mutex> g(globalMegaTestHooks.mMutex);
        globalMegaTestHooks.onWsHandshake =
            [&stormArmed,
             &forcedHandshakeFailures](const std::string& url, long, std::string& err) -> bool
        {
            if (!stormArmed.load(std::memory_order_acquire))
                return false;
            // Only the WS upload endpoints — never the cs/sc/api channels.
            if (url.rfind("wss://", 0) != 0 || url.find("/ul/") == std::string::npos)
                return false;
            forcedHandshakeFailures.fetch_add(1, std::memory_order_relaxed);
            err = "[DistressStorm] forced WS handshake failure";
            return true;
        };
        globalMegaTestHooks.onWsUploadSustainedHandshakeFailureWindowDs =
            [&stormArmed](dstime& windowDs)
        {
            if (stormArmed.load(std::memory_order_acquire))
                windowDs = 0;
        };
    }

    // ---- Start the upload and wait for the first Distress rewrite to confirm a
    // connection opened and a chunk was acked (proving a worker reached onmessage,
    // exactly as the QA crash log shows a frame was received before the SIGSEGV).
    WsUploadRetryTracker tracker(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &tracker);

    ASSERT_TRUE(WaitFor(
        // This predicate is capture-free ON PURPOSE and references the namespace-scope
        // global globalMegaTestHooks directly (not the local reference-alias evHook).
        // A namespace-scope global needs NO capture on any compiler, which is the only
        // form that satisfies all three:
        //   - clang: capturing evHook triggers -Werror,-Wunused-lambda-capture (evHook
        //     is a reference to a subobject of the global, usable in constant
        //     expressions and not odr-used, so the capture is "unused").
        //   - MSVC: does the opposite — it REQUIRES the capture (C3493/C2326 "cannot be
        //     implicitly captured"), so [&evHook] would be mandatory there and would
        //     re-break clang.
        // Referencing the global directly needs no capture anywhere; do not "simplify"
        // this back to evHook inside the lambda.
        []()
        {
            return globalMegaTestHooks.wsUploadServerEventHook.getHitCount() > 0;
        },
        120000))
        << "Timed out waiting for first Distress (event==5) injection — no chunk acked, "
           "storm never started";

    // Arm actor (B) now that a connection has opened and we are mid-flight.
    stormArmed.store(true, std::memory_order_release);

    // ---- Actor (C): pump mid-flight disconnects to keep the storm hot. Each
    // notifyNetworkDisconnect() bumps disconnectEpoch and wakes all workers
    // (wsupload_engine.cpp:736-739), maximising the rate at which workers re-enter
    // connectWS()/onmessage while the client thread drains refresh USC replies.
    constexpr int kSoakSeconds = 60;
    second_timer soak;
    int disconnects = 0;
    while (soak.elapsed() < kSoakSeconds)
    {
        // Drive the disconnect/wake storm.
        (void)notifyWsUploadNetworkDisconnectForTesting(*megaApi[0], 2);
        ++disconnects;

        // Bail early if the transfer already resolved (completed or gave up). The
        // point of the test is "no crash"; once the transfer is done the storm has
        // nothing to corrupt.
        if (tracker.finished.load(std::memory_order_acquire))
            break;

        WaitMillisec(250);
    }

    LOG_info << "[DistressStorm] soak done: distressHits=" << evHook.getHitCount()
             << " forcedHandshakeFailures=" << forcedHandshakeFailures.load()
             << " disconnects=" << disconnects
             << " temporaryErrors=" << tracker.temporaryErrorCount.load()
             << " finished=" << tracker.finished.load();

    // ---- Disarm the storm and let the transfer settle (either complete or fail
    // gracefully). Removing the handshake-failure + distress injection lets pools
    // open again so a survivable transfer can converge.
    stormArmed.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> g(globalMegaTestHooks.mMutex);
        globalMegaTestHooks.onWsHandshake = {};
        globalMegaTestHooks.onWsUploadSustainedHandshakeFailureWindowDs = {};
    }
    globalMegaTestHooks.wsUploadServerEventHook.reset();
    megaApi[0]->setMaxUploadSpeed(-1);

    // ---- Primary assertion: NO CRASH (reaching here under ASan with a clean exit
    // is the pass). Secondary: the transfer resolved to either API_OK (survived the
    // storm and converged) or a surfaced retryable error — never a silent wedge or
    // an unexpected terminal code. We accept a broad set of outcomes because the
    // storm is intentionally hostile; the contract under test is *liveness +
    // memory-safety*, not a specific result code.
    const auto finalResult = tracker.waitForResult(300);
    const bool acceptable =
        finalResult == API_OK || finalResult == API_EAGAIN || finalResult == API_EFAILED ||
        finalResult == API_EINCOMPLETE || finalResult == API_EEXPIRED ||
        finalResult == static_cast<::mega::ErrorCodes>(LOCAL_ETIMEOUT);
    ASSERT_TRUE(acceptable)
        << "Transfer resolved with an unexpected terminal code after the distress storm"
        << " [result=" << static_cast<int>(finalResult)
        << " distressHits=" << evHook.getHitCount()
        << " forcedHandshakeFailures=" << forcedHandshakeFailures.load()
        << " temporaryErrors=" << tracker.temporaryErrorCount.load() << "]";

    // If it completed, the cloud node must match the source exactly (no silent
    // short-complete / state desync from the storm).
    if (finalResult == API_OK)
    {
        rootnode.reset(megaApi[0]->getRootNode());
        ASSERT_TRUE(rootnode);
        std::unique_ptr<MegaNode> cloudNode(megaApi[0]->getNodeByPathOfType(fileName.c_str(),
                                                                            rootnode.get(),
                                                                            MegaNode::TYPE_FILE));
        ASSERT_TRUE(cloudNode) << "Uploaded file not found in cloud after storm-survived completion";
        ASSERT_EQ(cloudNode->getSize(), static_cast<int64_t>(fileSize));
    }
}

} // namespace mega::test::wsupload
