/**
 * @file SdkWsQuotaTest.cpp
 * @brief SDK-6298 WS upload-quota hook-driven integration cells (T1–T17, T21).
 *
 * These cells exercise the predictive WS upload-quota ledger through the Q9 test
 * seams (analysis/DESIGN_IMPL_SDK6298.md): H1 onWsTfsIssued, H2 onWsTfsResult
 * (forge balances / Drop / Requeue), H3 onWsQuotaHoldChanged, H4
 * onWsTfsStaleDiscarded, H6 onWsQuotaDeducted, and the M1 client-thread trigger
 * MegaClient::wsQuotaInvalidateAndMarkDirty() (usl-change model).
 *
 * They share the existing SdkWsUploadTest fixture (subclass of SdkTest) so the
 * ci_tier1_gate SdkWsUploadTest.* filter, the Jenkins MR WS filter and the TSAN
 * globs cover them with zero filter changes — same-fixture-new-file precedent
 * SdkWsUploadCrashTest.cpp.
 *
 * PRE-IMPLEMENTATION fail-first discipline: the product wiring does not exist yet
 * (nothing issues a `tfs`, nothing holds a transfer; wsQuotaInvalidateAndMarkDirty
 * is a null-safe no-op). Every cell is written to its POST-implementation contract
 * but is structured so that TODAY it fails FAST on its FIRST behavioral assert —
 * the canonical `ASSERT_TRUE(tfsIssued.waitForFire(60s))` "tfs never issued after
 * enqueue (issuances=0)" — placed BEFORE any long waits on holds/completions. The
 * per-cell hold/completion asserts the plan documents as the conceptual first
 * failure remain present as secondary post-impl asserts; see the module report
 * for this deliberate deviation from the plan's §1 per-cell fail-first messages.
 *
 * Timing cells (T3, T4, T9, T10, T11, T14; T13 recommended) run under
 * --gtest_repeat=15 taskset -c 0 nice -n 19 per HR43. No setenv anywhere (HR58);
 * synchronization is cv-waits with timeouts and bounded quiet-window polls.
 * `::mega::` prefixes in using-decls (C++20, feedback_cxx_standard_per_target.md).
 */

#include "mega/scoped_helpers.h"
#include "mega/testhooks.h"
#include "mega/transfer/ws/ws_quota_types.h"
#include "mega/types.h"
#include "megaapi.h"
#include "sdk_test_utils.h"
#include "SdkTest_test.h"
#include "test.h"
#include "wsupload/headers/SdkWsUploadTest.h"
#include "wsupload/headers/WsQuotaClientThread.h"
#include "wsupload/headers/WsQuotaHoldTracker.h"
#include "wsupload/headers/WsQuotaHookCaptures.h"
#include "wsupload/headers/WsUploadHelpers.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace std;

namespace mega::test::wsupload
{

namespace
{
// Build a masked 6-byte NodeHandle from a MegaApi handle (MegaNode::getHandle /
// createFolder both return clean 6-byte handles).
::mega::NodeHandle toNodeHandle(::MegaHandle h)
{
    return ::mega::NodeHandle().set6byte(h);
}

std::string makeBinName(const char* prefix)
{
    return std::string(prefix) +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
}

std::set<std::uint64_t> handleSet(const std::vector<::mega::NodeHandle>& hs)
{
    std::set<std::uint64_t> s;
    for (const auto& h: hs)
        s.insert(h.as8byte());
    return s;
}

// One quota group: writable bytes shared by folders.
::mega::WsTfsGroupBalances oneGroup(::m_off_t bytes, std::vector<::mega::NodeHandle> folders)
{
    return ::mega::WsTfsGroupBalances{{bytes, std::move(folders)}};
}

constexpr int kThrottleBps = 100000; // matches OverquotaDuringTransfer (SdkWsUploadTest.cpp:2414)
constexpr ::m_off_t kGenerousBytes = ::m_off_t{1} << 42; // 4 TiB — nothing ever held
constexpr std::chrono::seconds kIssueTimeout{60};
constexpr std::chrono::seconds kHoldTimeout{60};
constexpr int kCompleteTimeoutS = 240; // TransferTracker::waitForResult takes int seconds
} // namespace

// ============================================================================
// T1 QuotaPreflightShortfallHoldsUploadNotBlockingStart
//
// Proves both halves of the "predictive shortfall holds the upload but does NOT
// block its start" contract. A live tfs reply applies in ~one cs round-trip
// (~200ms) — sooner than a 100KB/s-throttled 12MiB upload emits its first
// progress snapshot — so progress-before-hold is unobservable against a fast
// reply (the hold would always precede any byte). The cell therefore separates
// the two guarantees into two phases:
//   Phase 1 (start-not-blocked): the first reply (gen 1) is DROPPED, so no
//     balances ever exist and nothing can hold; the throttled upload MUST show
//     WS progress, proving enqueue/start never waits on quota state.
//   Phase 2 (the hold): a shortfall is armed as the default plan and an
//     M1-modelled usl change (wsQuotaInvalidateAndMarkDirty) forces a fresh tfs
//     (gen 2) whose apply holds the running upload with a non-foreign, temporary
//     EOVERQUOTA that never terminally fails it.
// ============================================================================
TEST_F(SdkWsUploadTest, QuotaPreflightShortfallHoldsUploadNotBlockingStart)
{
    LOG_info << "___TEST QuotaPreflightShortfallHoldsUploadNotBlockingStart___";
    WSUPLOAD_REQUIRE_TEST_HOOKS();
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const std::string fileName = makeBinName("ws_quota_t1_");
    constexpr size_t fileSize = kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "1")) << "Couldn't create " << fileName;

    std::vector<std::string> localFiles{fileName};
    std::vector<std::string> rootUploadNames{fileName};
    auto cleanup = makeScopedDestructor(
        [&]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
            if (std::unique_ptr<MegaNode> root{megaApi[0]->getRootNode()})
                for (const auto& nm: rootUploadNames)
                    if (std::unique_ptr<MegaNode> n{
                            megaApi[0]->getNodeByPathOfType(nm.c_str(),
                                                            root.get(),
                                                            MegaNode::TYPE_FILE)})
                        (void)synchronousRemove(0, n.get());
            for (const auto& nm: localFiles)
                deleteFile(nm);
        });

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);
    const ::mega::NodeHandle rootH = toNodeHandle(rootnode->getHandle());

    // Hook captures: install BEFORE the upload so H1/H3 issuance/hold fire is seen.
    WsTfsIssuedCapture tfsIssued;
    WsQuotaHoldChangedCapture holdChanged;
    WsTfsResultScript script;
    WsQuotaHoldTracker holdTracker(megaApi[0].get());

    // Phase 1 (start-not-blocked): DROP the first reply (gen 1 — a fresh session
    // numbers its issues densely from 1). No balances are ever applied, so nothing
    // can hold and the throttled upload must show progress. (A live shortfall reply
    // would apply its hold in ~one round-trip, before the first progress snapshot,
    // so progress-before-hold is unobservable against a fast reply — hence the drop.)
    script.setPlanForGen(1, WsTfsResultScript::drop());

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));

    ScopedUploadSpeedLimit throttle{*megaApi[0], kThrottleBps};

    TransferTracker tracker(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &tracker);
    ASSERT_TRUE(WaitFor(
        [&]
        {
            return tracker.mTag.load() >= 0;
        },
        30000))
        << "transfer tag not captured";
    const int tag = tracker.mTag.load();

    // FAIL-FIRST: the tfs must be issued on enqueue (does not block on quota state).
    ASSERT_TRUE(tfsIssued.waitForFire(kIssueTimeout))
        << "tfs never issued after enqueue (issuances=0)";

    // (a) Phase-1 guarantee: the dropped gen-1 reply left NO balances, so nothing
    // can hold — the throttled upload must show WS progress (enqueue didn't wait).
    WsUploadTransferSnapshot snap{};
    ASSERT_TRUE(waitForFirstUploadTransferSnapshot(
        *megaApi[0],
        snap,
        [](const WsUploadTransferSnapshot& s)
        {
            return s.progressCompleted > 0;
        },
        60,
        200))
        << "upload never showed WS progress — start-immediately violated";

    // (b) H1 fired with the own-root handle.
    ASSERT_TRUE(handleSet(tfsIssued.foldersOfIssuance(0)).count(rootH.as8byte()) == 1)
        << "first tfs issuance did not query the own-root folder";

    // Phase 2 (the hold): now that start-not-blocked is proven, arm a shortfall
    // (own-root balance = fileSize-1, so the file does not fit) as the default plan
    // and model a usl change (M1) to force a fresh tfs (gen 2). Its apply holds the
    // still-running upload — the observable "shortfall holds it" without racing the
    // first progress snapshot.
    script.setDefaultPlan(
        WsTfsResultScript::singleGroup(static_cast<::m_off_t>(fileSize) - 1, {rootH}));
    ASSERT_TRUE(invokeWsQuotaInvalidateOnClientThread(*megaApi[0])) << "M1 dispatch failed";

    // (c) H3 held=true, foreign=false.
    ASSERT_TRUE(holdChanged.waitForHold(tag, kHoldTimeout))
        << "predictive hold never observed on preflight shortfall (holdEvents=0)";
    const auto hold = holdChanged.firstHold(tag);
    ASSERT_TRUE(hold.has_value());
    ASSERT_FALSE(hold->foreign) << "own-account shortfall must not be flagged foreign";

    // (d) onTransferTemporaryError API_EOVERQUOTA + isForeignOverquota()==false.
    ASSERT_TRUE(holdTracker.waitForTemporaryError(tag, kHoldTimeout))
        << "no temporary error surfaced for held transfer";
    const auto seq = holdTracker.sequence(tag);
    ASSERT_FALSE(seq.empty());
    ASSERT_EQ(seq.front().kind, WsQuotaHoldTracker::Event::Kind::TemporaryError);
    ASSERT_EQ(seq.front().code, API_EOVERQUOTA);
    ASSERT_FALSE(seq.front().foreignOverquota) << "own-account overquota must not be foreign";

    // (e) NOT terminally failed/completed within a 15s quiet window.
    ASSERT_FALSE(holdTracker.waitForFinish(tag, std::chrono::seconds(15)))
        << "held transfer terminally finished — a hold must keep it retrying, not fail it";

    // (f) cleanup via RAII (cancel + remove + delete).
    megaApi[0]->cancelTransferByTag(tag);
}

// ============================================================================
// T2 QuotaShortfallArisingDuringRunningUploadHolds
// Shortfall arises mid-upload.
// ============================================================================
TEST_F(SdkWsUploadTest, QuotaShortfallArisingDuringRunningUploadHolds)
{
    LOG_info << "___TEST QuotaShortfallArisingDuringRunningUploadHolds___";
    WSUPLOAD_REQUIRE_TEST_HOOKS();
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const std::string mainName = makeBinName("ws_quota_t2_main_");
    const std::string smallName = makeBinName("ws_quota_t2_small_");
    constexpr size_t mainSize = kWsUploadDefaultFileSize;
    constexpr size_t smallSize = 1024;
    ASSERT_TRUE(createFileWithSize(mainName, mainSize, "M"));
    ASSERT_TRUE(createFileWithSize(smallName, smallSize, "s"));

    std::vector<std::string> localFiles{mainName, smallName};
    std::vector<std::string> rootUploadNames{mainName};
    std::vector<::MegaHandle> createdFolders;
    auto cleanup = makeScopedDestructor(
        [&]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
            for (auto h: createdFolders)
                if (h != ::mega::UNDEF)
                    if (std::unique_ptr<MegaNode> n{megaApi[0]->getNodeByHandle(h)})
                        (void)synchronousRemove(0, n.get());
            if (std::unique_ptr<MegaNode> root{megaApi[0]->getRootNode()})
                for (const auto& nm: rootUploadNames)
                    if (std::unique_ptr<MegaNode> n{
                            megaApi[0]->getNodeByPathOfType(nm.c_str(),
                                                            root.get(),
                                                            MegaNode::TYPE_FILE)})
                        (void)synchronousRemove(0, n.get());
            for (const auto& nm: localFiles)
                deleteFile(nm);
        });

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);
    const ::mega::NodeHandle rootH = toNodeHandle(rootnode->getHandle());

    const ::MegaHandle folder2Handle =
        createFolder(0, makeBinName("t2folder_").c_str(), rootnode.get());
    ASSERT_NE(folder2Handle, ::mega::UNDEF);
    createdFolders.push_back(folder2Handle);
    const ::mega::NodeHandle folder2H = toNodeHandle(folder2Handle);
    std::unique_ptr<MegaNode> folder2{megaApi[0]->getNodeByHandle(folder2Handle)};
    ASSERT_TRUE(folder2);

    WsTfsIssuedCapture tfsIssued;
    WsQuotaHoldChangedCapture holdChanged;
    WsTfsResultScript script;
    WsQuotaHoldTracker holdTracker(megaApi[0].get());

    // Phase 1: generous first reply for the own root.
    script.setDefaultPlan(WsTfsResultScript::generous({rootH}));

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));
    ScopedUploadSpeedLimit throttle{*megaApi[0], kThrottleBps};

    TransferTracker mainTracker(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(mainName, rootnode.get(), nullptr, &uploadOptions, &mainTracker);
    ASSERT_TRUE(WaitFor(
        [&]
        {
            return mainTracker.mTag.load() >= 0;
        },
        30000));
    const int mainTag = mainTracker.mTag.load();

    ASSERT_TRUE(tfsIssued.waitForFire(kIssueTimeout))
        << "tfs never issued after enqueue (issuances=0)";

    // Phase 2: re-arm shortfall for root (main no longer fits) + generous folder2, then
    // force a re-issue by enqueueing the tiny second file to folder2.
    script.setDefaultPlan(WsTfsResultScript::withGroups(::mega::WsTfsGroupBalances{
        {static_cast<::m_off_t>(mainSize) / 2, {rootH}},
        {kGenerousBytes, {folder2H}},
    }));

    TransferTracker smallTracker(megaApi[0].get());
    megaApi[0]->startUpload(smallName, folder2.get(), nullptr, &uploadOptions, &smallTracker);

    ASSERT_TRUE(holdChanged.waitForHold(mainTag, kHoldTimeout))
        << "hold never observed on running upload within 60s (holdEvents=0)";
    ASSERT_TRUE(holdTracker.waitForTemporaryError(mainTag, kHoldTimeout))
        << "no temporary EOVERQUOTA surfaced for the newly-held running upload";

    // The tiny file to the generous folder2 completes untouched.
    megaApi[0]->setMaxUploadSpeed(-1);
    ASSERT_EQ(smallTracker.waitForResult(kCompleteTimeoutS), API_OK)
        << "small upload to generous folder did not complete";

    // Phase 3: release via generous + M1, running upload completes.
    script.setDefaultPlan(WsTfsResultScript::generous({rootH}));
    ASSERT_TRUE(invokeWsQuotaInvalidateOnClientThread(*megaApi[0])) << "M1 dispatch failed";
    ASSERT_TRUE(holdChanged.waitForRelease(mainTag, kHoldTimeout))
        << "hold never released after usl change";
    ASSERT_EQ(mainTracker.waitForResult(kCompleteTimeoutS), API_OK)
        << "running upload did not complete after release";
}

// ============================================================================
// T3 QuotaCrossPoolHoldDoesNotBlockOtherPool  (HR43 x15)
// ============================================================================
TEST_F(SdkWsUploadTest, QuotaCrossPoolHoldDoesNotBlockOtherPool)
{
    LOG_info << "___TEST QuotaCrossPoolHoldDoesNotBlockOtherPool___";
    WSUPLOAD_REQUIRE_TEST_HOOKS();
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Size classes — clone RepeatedPauseResumeMixedPools sizing + skip guard.
    std::vector<::m_off_t> sizeClasses;
    ASSERT_TRUE(fetchUscSizeClasses(*megaApi[0], sizeClasses, 60))
        << "Unable to fetch USC size classes";
    std::vector<::m_off_t> finiteClasses;
    bool hasOpenEnded = false;
    for (const ::m_off_t c: sizeClasses)
    {
        if (c == 0)
            hasOpenEnded = true;
        else if (c > 1)
            finiteClasses.push_back(c);
    }
    std::sort(finiteClasses.begin(), finiteClasses.end());
    finiteClasses.erase(std::unique(finiteClasses.begin(), finiteClasses.end()),
                        finiteClasses.end());
    if (finiteClasses.empty())
        GTEST_SKIP() << "Could not derive a usable first USC class boundary";
    const ::m_off_t firstClassMax = finiteClasses.front();
    if (!(hasOpenEnded || finiteClasses.size() >= 2))
        GTEST_SKIP() << "Could not derive a second USC class boundary from first max="
                     << firstClassMax;

    const size_t sizeA = static_cast<size_t>(firstClassMax - 1); // class #1
    const size_t sizeB = static_cast<size_t>(firstClassMax); // class #2

    const std::string fileA = makeBinName("ws_quota_t3_A_");
    const std::string fileB = makeBinName("ws_quota_t3_B_");
    ASSERT_TRUE(createFileWithSize(fileA, sizeA, "A"));
    ASSERT_TRUE(createFileWithSize(fileB, sizeB, "B"));

    std::vector<std::string> localFiles{fileA, fileB};
    std::vector<::MegaHandle> createdFolders;
    auto cleanup = makeScopedDestructor(
        [&]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
            for (auto h: createdFolders)
                if (h != ::mega::UNDEF)
                    if (std::unique_ptr<MegaNode> n{megaApi[0]->getNodeByHandle(h)})
                        (void)synchronousRemove(0, n.get());
            for (const auto& nm: localFiles)
                deleteFile(nm);
        });

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);
    const ::MegaHandle f1Handle = createFolder(0, makeBinName("t3F1_").c_str(), rootnode.get());
    const ::MegaHandle f2Handle = createFolder(0, makeBinName("t3F2_").c_str(), rootnode.get());
    ASSERT_NE(f1Handle, ::mega::UNDEF);
    ASSERT_NE(f2Handle, ::mega::UNDEF);
    createdFolders = {f1Handle, f2Handle};
    const ::mega::NodeHandle f1H = toNodeHandle(f1Handle);
    const ::mega::NodeHandle f2H = toNodeHandle(f2Handle);
    std::unique_ptr<MegaNode> f1{megaApi[0]->getNodeByHandle(f1Handle)};
    std::unique_ptr<MegaNode> f2{megaApi[0]->getNodeByHandle(f2Handle)};
    ASSERT_TRUE(f1 && f2);

    WsTfsIssuedCapture tfsIssued;
    WsQuotaHoldChangedCapture holdChanged;
    WsTfsResultScript script;
    WsQuotaHoldTracker holdTracker(megaApi[0].get());

    // Single own pool covering both folders; balance sizeB-1 so small A fits, big B doesn't.
    script.setDefaultPlan(
        WsTfsResultScript::withGroups(oneGroup(static_cast<::m_off_t>(sizeB) - 1, {f1H, f2H})));

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(2, &ct); // two pools progress concurrently
    ASSERT_EQ(API_OK, ct.waitForResult(60));
    ScopedUploadSpeedLimit throttle{*megaApi[0], kThrottleBps};

    TransferTracker trackerB(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileB, f1.get(), nullptr, &uploadOptions, &trackerB);
    ASSERT_TRUE(WaitFor(
        [&]
        {
            return trackerB.mTag.load() >= 0;
        },
        30000));
    const int bTag = trackerB.mTag.load();

    ASSERT_TRUE(tfsIssued.waitForFire(kIssueTimeout))
        << "tfs never issued after enqueue (issuances=0)";

    // Capture B's pool id before it is held.
    std::uintptr_t bPoolId = 0;
    {
        std::vector<WsUploadTransferSnapshot> snaps;
        second_timer t;
        while (t.elapsed() < 30)
        {
            if (fetchWsUploadTransferSnapshots(*megaApi[0], snaps, 1))
                for (const auto& s: snaps)
                    if (s.found && s.fileName == fileB && s.poolId != 0)
                        bPoolId = s.poolId;
            if (bPoolId != 0)
                break;
            WaitMillisec(200);
        }
    }

    // Big B held (does not fit).
    ASSERT_TRUE(holdChanged.waitForHold(bTag, kHoldTimeout))
        << "hold never observed for large upload (holdEvents=0)";

    // A started exactly on H3(B held) — proceeds in a different pool and completes.
    TransferTracker trackerA(megaApi[0].get());
    megaApi[0]->startUpload(fileA, f2.get(), nullptr, &uploadOptions, &trackerA);
    ASSERT_TRUE(WaitFor(
        [&]
        {
            return trackerA.mTag.load() >= 0;
        },
        30000));

    std::uintptr_t aPoolId = 0;
    {
        std::vector<WsUploadTransferSnapshot> snaps;
        second_timer t;
        while (t.elapsed() < 120)
        {
            if (fetchWsUploadTransferSnapshots(*megaApi[0], snaps, 1))
                for (const auto& s: snaps)
                    if (s.found && s.fileName == fileA && s.poolId != 0)
                        aPoolId = s.poolId;
            if (aPoolId != 0 || trackerA.finished.load())
                break;
            WaitMillisec(200);
        }
    }
    megaApi[0]->setMaxUploadSpeed(-1);
    ASSERT_EQ(trackerA.waitForResult(kCompleteTimeoutS), API_OK)
        << "small upload in the other pool did not complete while big upload held";
    if (aPoolId != 0 && bPoolId != 0)
    {
        ASSERT_NE(aPoolId, bPoolId) << "A and B unexpectedly shared a pool";
    }

    // B stays held until released.
    ASSERT_FALSE(trackerB.finished.load()) << "held big upload must not have finished";
    script.setDefaultPlan(WsTfsResultScript::generous({f1H, f2H}));
    ASSERT_TRUE(invokeWsQuotaInvalidateOnClientThread(*megaApi[0])) << "M1 dispatch failed";
    ASSERT_TRUE(holdChanged.waitForRelease(bTag, kHoldTimeout)) << "big upload hold never released";
    ASSERT_EQ(trackerB.waitForResult(kCompleteTimeoutS), API_OK)
        << "big upload did not complete after release";
}

// ============================================================================
// T4 QuotaBatchEnqueueCoalescesSingleTfs  (HR43 x15)
//
// Proves the burst-coalescing contract as three observable properties, NOT as a
// single tfs: (a) full COVERAGE — the union of all issuances' queried folders is
// exactly the deduped target set; (b) a bounded ANTI-STORM issuance count (<= 3;
// 8 naive per-file issues would be a storm — the design's worst case for a burst
// is one exec-cycle snapshot plus one in-flight-guarded follow-up); (c) per-issuance
// DEDUP. "Exactly one tfs covering the whole burst" is deliberately NOT asserted:
// it is a scheduling accident, not a guarantee. Through the public async API each
// startUpload independently notifies the client-thread waiter, and the loop drains
// whatever is queued at that instant then flushes once (megaapi_impl.cpp:8136-8149;
// sendPendingTransfers drains <=100 transfers / 100ms, :20709), so whether all 8
// enqueues land in one snapshot before the first flush is pure timing the design
// never promised. Files are pre-created so the enqueue loop is genuinely tight (no
// interleaved disk I/O spreading the burst across client-thread wake-ups).
// ============================================================================
TEST_F(SdkWsUploadTest, QuotaBatchEnqueueCoalescesSingleTfs)
{
    LOG_info << "___TEST QuotaBatchEnqueueCoalescesSingleTfs___";
    WSUPLOAD_REQUIRE_TEST_HOOKS();
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    std::vector<std::string> localFiles;
    std::vector<::MegaHandle> createdFolders;
    auto cleanup = makeScopedDestructor(
        [&]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
            for (auto h: createdFolders)
                if (h != ::mega::UNDEF)
                    if (std::unique_ptr<MegaNode> n{megaApi[0]->getNodeByHandle(h)})
                        (void)synchronousRemove(0, n.get());
            for (const auto& nm: localFiles)
                deleteFile(nm);
        });

    // 3 distinct cloud folders.
    std::vector<::mega::NodeHandle> folderHandles;
    std::vector<std::unique_ptr<MegaNode>> folderNodes;
    for (int i = 0; i < 3; ++i)
    {
        const ::MegaHandle h = createFolder(0, makeBinName("t4folder_").c_str(), rootnode.get());
        ASSERT_NE(h, ::mega::UNDEF);
        createdFolders.push_back(h);
        folderHandles.push_back(toNodeHandle(h));
        folderNodes.emplace_back(megaApi[0]->getNodeByHandle(h));
        ASSERT_TRUE(folderNodes.back());
    }

    WsTfsIssuedCapture tfsIssued;
    WsTfsResultScript script;
    script.setDefaultPlan(WsTfsResultScript::generous(folderHandles));

    // Pre-create ALL 8 small files (across the 3 folders) BEFORE enqueuing, so the
    // enqueue loop below is genuinely tight — startUpload calls only, no interleaved
    // 1 MiB disk writes to spread the burst across client-thread wake-ups.
    constexpr size_t fileSize = 1 * 1024 * 1024;
    std::vector<std::string> uploadNames;
    for (int i = 0; i < 8; ++i)
    {
        const std::string nm = makeBinName("ws_quota_t4_") + "_" + std::to_string(i);
        ASSERT_TRUE(createFileWithSize(nm, fileSize, std::to_string(i)));
        localFiles.push_back(nm);
        uploadNames.push_back(nm);
    }

    std::vector<std::unique_ptr<TransferTracker>> trackers;
    auto uploadOptions = makeDefaultUploadOptions();
    for (int i = 0; i < 8; ++i)
    {
        trackers.push_back(std::make_unique<TransferTracker>(megaApi[0].get()));
        megaApi[0]->startUpload(uploadNames[static_cast<size_t>(i)],
                                folderNodes[static_cast<size_t>(i) % 3].get(),
                                nullptr,
                                &uploadOptions,
                                trackers.back().get());
    }

    ASSERT_TRUE(tfsIssued.waitForFire(kIssueTimeout)) << "no tfs issued for batch (issuances=0)";

    std::set<std::uint64_t> want;
    for (const auto& h: folderHandles)
        want.insert(h.as8byte());

    // (a) COVERAGE: the UNION of every issuance's queried folders is exactly the 3
    // deduped targets. A burst may be snapshotted by the first tfs (single exec
    // cycle) or spill its remainder into one in-flight-guarded follow-up tfs, so
    // wait for the union to settle rather than pinning the first issuance.
    ASSERT_TRUE(WaitFor(
        [&]
        {
            return handleSet(tfsIssued.unionFoldersFrom(0)).size() >= folderHandles.size();
        },
        60000))
        << "tfs issuances never covered all target folders (union="
        << handleSet(tfsIssued.unionFoldersFrom(0)).size() << ")";
    ASSERT_EQ(handleSet(tfsIssued.unionFoldersFrom(0)), want)
        << "union of issuance folder sets != the 3 deduped folder handles";

    // (b) ANTI-STORM BOUND: the whole batch produces a bounded number of issuances.
    // 8 naive per-file issues would be a storm; the design's worst case for a burst
    // is one snapshot + one follow-up, so <= 3 is robust headroom without encoding
    // scheduler timing.
    ASSERT_LE(tfsIssued.issuanceCount(), 3u)
        << "batch produced a tfs storm (issuances=" << tfsIssued.issuanceCount() << ")";

    // (c) PER-ISSUANCE DEDUP: the first issuance never lists a folder handle twice.
    const auto firstIssuance = tfsIssued.foldersOfIssuance(0);
    ASSERT_EQ(handleSet(firstIssuance).size(), firstIssuance.size())
        << "first tfs issuance queried a duplicate folder handle (dedup not applied)";

    // All 8 uploads complete (generous balance => never held; fail-open).
    for (auto& t: trackers)
        ASSERT_EQ(t->waitForResult(kCompleteTimeoutS), API_OK) << "batch upload failed";
}

// ============================================================================
// T5 QuotaCompletionDeductionHoldsSubsequentUpload
// ============================================================================
TEST_F(SdkWsUploadTest, QuotaCompletionDeductionHoldsSubsequentUpload)
{
    LOG_info << "___TEST QuotaCompletionDeductionHoldsSubsequentUpload___";
    WSUPLOAD_REQUIRE_TEST_HOOKS();
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    std::vector<std::string> localFiles;
    std::vector<::MegaHandle> createdFolders;
    auto cleanup = makeScopedDestructor(
        [&]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
            for (auto h: createdFolders)
                if (h != ::mega::UNDEF)
                    if (std::unique_ptr<MegaNode> n{megaApi[0]->getNodeByHandle(h)})
                        (void)synchronousRemove(0, n.get());
            for (const auto& nm: localFiles)
                deleteFile(nm);
        });

    const ::MegaHandle folderHandle =
        createFolder(0, makeBinName("t5folder_").c_str(), rootnode.get());
    ASSERT_NE(folderHandle, ::mega::UNDEF);
    createdFolders.push_back(folderHandle);
    const ::mega::NodeHandle folderH = toNodeHandle(folderHandle);
    std::unique_ptr<MegaNode> folder{megaApi[0]->getNodeByHandle(folderHandle)};
    ASSERT_TRUE(folder);

    const std::string file1 = makeBinName("ws_quota_t5_1_");
    const std::string file2 = makeBinName("ws_quota_t5_2_");
    constexpr ::m_off_t sizeS = 2 * 1024 * 1024; // each individually fits balance S
    ASSERT_TRUE(createFileWithSize(file1, static_cast<size_t>(sizeS), "1"));
    ASSERT_TRUE(createFileWithSize(file2, static_cast<size_t>(sizeS), "2"));
    localFiles = {file1, file2};

    WsTfsIssuedCapture tfsIssued;
    WsQuotaHoldChangedCapture holdChanged;
    WsQuotaDeductedCapture deducted;
    WsTfsResultScript script;
    WsQuotaHoldTracker holdTracker(megaApi[0].get());

    // Balance = S: exactly one of the two S-sized files fits at a time (individual-fit).
    script.setDefaultPlan(WsTfsResultScript::singleGroup(sizeS, {folderH}));

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));
    ScopedUploadSpeedLimit throttle{*megaApi[0], kThrottleBps};

    TransferTracker tracker1(megaApi[0].get());
    TransferTracker tracker2(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(file1, folder.get(), nullptr, &uploadOptions, &tracker1);
    megaApi[0]->startUpload(file2, folder.get(), nullptr, &uploadOptions, &tracker2);
    ASSERT_TRUE(WaitFor(
        [&]
        {
            return tracker2.mTag.load() >= 0;
        },
        30000));
    const int tag2 = tracker2.mTag.load();

    ASSERT_TRUE(tfsIssued.waitForFire(kIssueTimeout))
        << "tfs never issued after enqueue (issuances=0)";

    // Neither held before completion (individual-fit; U8 pins). file1 completes.
    megaApi[0]->setMaxUploadSpeed(-1);
    ASSERT_EQ(tracker1.waitForResult(kCompleteTimeoutS), API_OK)
        << "first S-sized file did not complete";

    // The completion debits the pool (H6) — deduct evidence, discriminated from a re-issue.
    ASSERT_TRUE(deducted.waitForFire(std::chrono::seconds(30)))
        << "no completion deduction observed (H6 count=0)";
    const std::size_t issuancesBeforeHold = tfsIssued.issuanceCount();

    // Balance now exhausted -> file2 held.
    ASSERT_TRUE(holdChanged.waitForHold(tag2, kHoldTimeout))
        << "subsequent upload never held after balance-exhausting completion";
    const auto hold = holdChanged.firstHold(tag2);
    ASSERT_TRUE(hold.has_value());
    ASSERT_EQ(hold->availableBytes, 0) << "held file should see availableBytes==0";
    ASSERT_TRUE(holdTracker.waitForTemporaryError(tag2, kHoldTimeout));

    // Discriminator: the hold came from the deduction, NOT a fresh tfs.
    ASSERT_EQ(tfsIssued.issuanceCount(), issuancesBeforeHold)
        << "a fresh tfs was issued between completion and hold — deduction was not the cause";

    // Release via generous + M1 -> file2 completes.
    script.setDefaultPlan(WsTfsResultScript::generous({folderH}));
    ASSERT_TRUE(invokeWsQuotaInvalidateOnClientThread(*megaApi[0])) << "M1 dispatch failed";
    ASSERT_TRUE(holdChanged.waitForRelease(tag2, kHoldTimeout));
    ASSERT_EQ(tracker2.waitForResult(kCompleteTimeoutS), API_OK)
        << "second file did not complete after release";
}

// ============================================================================
// T6 QuotaUslChangeReleasesHold
// ============================================================================
TEST_F(SdkWsUploadTest, QuotaUslChangeReleasesHold)
{
    LOG_info << "___TEST QuotaUslChangeReleasesHold___";
    WSUPLOAD_REQUIRE_TEST_HOOKS();
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const std::string fileName = makeBinName("ws_quota_t6_");
    constexpr size_t fileSize = kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "6"));

    std::vector<std::string> localFiles{fileName};
    std::vector<std::string> rootUploadNames{fileName};
    auto cleanup = makeScopedDestructor(
        [&]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
            if (std::unique_ptr<MegaNode> root{megaApi[0]->getRootNode()})
                for (const auto& nm: rootUploadNames)
                    if (std::unique_ptr<MegaNode> n{
                            megaApi[0]->getNodeByPathOfType(nm.c_str(),
                                                            root.get(),
                                                            MegaNode::TYPE_FILE)})
                        (void)synchronousRemove(0, n.get());
            for (const auto& nm: localFiles)
                deleteFile(nm);
        });

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);
    const ::mega::NodeHandle rootH = toNodeHandle(rootnode->getHandle());

    WsTfsIssuedCapture tfsIssued;
    WsQuotaHoldChangedCapture holdChanged;
    WsTfsResultScript script;
    WsQuotaHoldTracker holdTracker(megaApi[0].get());
    script.setDefaultPlan(
        WsTfsResultScript::singleGroup(static_cast<::m_off_t>(fileSize) - 1, {rootH}));

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));
    ScopedUploadSpeedLimit throttle{*megaApi[0], kThrottleBps};

    TransferTracker tracker(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &tracker);
    ASSERT_TRUE(WaitFor(
        [&]
        {
            return tracker.mTag.load() >= 0;
        },
        30000));
    const int tag = tracker.mTag.load();

    ASSERT_TRUE(tfsIssued.waitForFire(kIssueTimeout))
        << "tfs never issued after enqueue (issuances=0)";
    ASSERT_TRUE(holdChanged.waitForHold(tag, kHoldTimeout))
        << "predictive hold never observed (holdEvents=0)";
    ASSERT_TRUE(holdTracker.waitForTemporaryError(tag, kHoldTimeout));
    const std::uint64_t genBeforeRelease = tfsIssued.genOfIssuance(0);

    // usl change: generous + M1 -> fresh tfs (gen bump), hold released, completes.
    script.setDefaultPlan(WsTfsResultScript::generous({rootH}));
    ASSERT_TRUE(invokeWsQuotaInvalidateOnClientThread(*megaApi[0])) << "M1 dispatch failed";
    ASSERT_TRUE(tfsIssued.waitForIssuance(2, kIssueTimeout))
        << "usl change did not produce a fresh tfs (gen bump)";
    ASSERT_GT(tfsIssued.genOfIssuance(1), genBeforeRelease) << "generation did not advance";
    ASSERT_TRUE(holdChanged.waitForRelease(tag, kHoldTimeout))
        << "hold never released after usl change";
    megaApi[0]->setMaxUploadSpeed(-1);
    ASSERT_EQ(tracker.waitForResult(kCompleteTimeoutS), API_OK);

    // HoldTracker sequence: [tempError EOVERQUOTA...], then finish OK.
    const auto seq = holdTracker.sequence(tag);
    ASSERT_FALSE(seq.empty());
    ASSERT_EQ(seq.front().kind, WsQuotaHoldTracker::Event::Kind::TemporaryError);
    ASSERT_EQ(seq.front().code, API_EOVERQUOTA);
    ASSERT_EQ(seq.back().kind, WsQuotaHoldTracker::Event::Kind::Finish);
    ASSERT_EQ(seq.back().code, API_OK);
}

// ============================================================================
// T7 QuotaUslChangeCreatesHold
// ============================================================================
TEST_F(SdkWsUploadTest, QuotaUslChangeCreatesHold)
{
    LOG_info << "___TEST QuotaUslChangeCreatesHold___";
    WSUPLOAD_REQUIRE_TEST_HOOKS();
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const std::string fileName = makeBinName("ws_quota_t7_");
    constexpr size_t fileSize = kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "7"));

    std::vector<std::string> localFiles{fileName};
    std::vector<std::string> rootUploadNames{fileName};
    auto cleanup = makeScopedDestructor(
        [&]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
            if (std::unique_ptr<MegaNode> root{megaApi[0]->getRootNode()})
                for (const auto& nm: rootUploadNames)
                    if (std::unique_ptr<MegaNode> n{
                            megaApi[0]->getNodeByPathOfType(nm.c_str(),
                                                            root.get(),
                                                            MegaNode::TYPE_FILE)})
                        (void)synchronousRemove(0, n.get());
            for (const auto& nm: localFiles)
                deleteFile(nm);
        });

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);
    const ::mega::NodeHandle rootH = toNodeHandle(rootnode->getHandle());

    WsTfsIssuedCapture tfsIssued;
    WsQuotaHoldChangedCapture holdChanged;
    WsTfsResultScript script;
    WsQuotaHoldTracker holdTracker(megaApi[0].get());
    // Running upload under generous balance.
    script.setDefaultPlan(WsTfsResultScript::generous({rootH}));

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));
    ScopedUploadSpeedLimit throttle{*megaApi[0], kThrottleBps};

    TransferTracker tracker(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &tracker);
    ASSERT_TRUE(WaitFor(
        [&]
        {
            return tracker.mTag.load() >= 0;
        },
        30000));
    const int tag = tracker.mTag.load();

    ASSERT_TRUE(tfsIssued.waitForFire(kIssueTimeout))
        << "tfs never issued after enqueue (issuances=0)";
    // Confirm the upload is genuinely running (not held) before the usl change.
    WsUploadTransferSnapshot snap{};
    ASSERT_TRUE(waitForFirstUploadTransferSnapshot(
        *megaApi[0],
        snap,
        [](const WsUploadTransferSnapshot& s)
        {
            return s.progressCompleted > 0;
        },
        60,
        200))
        << "upload never made progress under generous balance";

    // usl change introduces a shortfall -> hold appears, temp EOVERQUOTA, not terminal.
    script.setDefaultPlan(
        WsTfsResultScript::singleGroup(static_cast<::m_off_t>(fileSize) / 2, {rootH}));
    ASSERT_TRUE(invokeWsQuotaInvalidateOnClientThread(*megaApi[0])) << "M1 dispatch failed";
    ASSERT_TRUE(holdChanged.waitForHold(tag, kHoldTimeout)) << "usl reevaluation produced no hold";
    ASSERT_TRUE(holdTracker.waitForTemporaryError(tag, kHoldTimeout));
    ASSERT_FALSE(holdTracker.waitForFinish(tag, std::chrono::seconds(15)))
        << "newly-held upload must not terminally finish";

    // Release -> completes.
    script.setDefaultPlan(WsTfsResultScript::generous({rootH}));
    ASSERT_TRUE(invokeWsQuotaInvalidateOnClientThread(*megaApi[0])) << "M1 dispatch failed";
    ASSERT_TRUE(holdChanged.waitForRelease(tag, kHoldTimeout));
    megaApi[0]->setMaxUploadSpeed(-1);
    ASSERT_EQ(tracker.waitForResult(kCompleteTimeoutS), API_OK);
}

// ============================================================================
// T8 QuotaInshareHoldSetsForeignOverquota  (2 accounts)
// A = account 0 (uploader/recipient); B = account 1 (folder owner).
// ============================================================================
TEST_F(SdkWsUploadTest, QuotaInshareHoldSetsForeignOverquota)
{
    LOG_info << "___TEST QuotaInshareHoldSetsForeignOverquota___";
    WSUPLOAD_REQUIRE_TEST_HOOKS();
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    // --- B (account 1) creates a folder in its own cloud ---
    std::unique_ptr<MegaNode> bRoot{megaApi[1]->getRootNode()};
    ASSERT_TRUE(bRoot);
    const std::string folderName = makeBinName("t8_inshare_");
    const ::MegaHandle bFolderHandle = createFolder(1, folderName.c_str(), bRoot.get());
    ASSERT_NE(bFolderHandle, ::mega::UNDEF);

    std::vector<std::string> localFiles;
    auto cleanup = makeScopedDestructor(
        [&]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
            if (std::unique_ptr<MegaNode> n{megaApi[1]->getNodeByHandle(bFolderHandle)})
                (void)synchronousRemove(1, n.get()); // removes the inshare + its contents
            if (std::unique_ptr<MegaNode> aRoot{megaApi[0]->getRootNode()})
                for (const auto& nm: localFiles)
                    if (std::unique_ptr<MegaNode> n{
                            megaApi[0]->getNodeByPathOfType(nm.c_str(),
                                                            aRoot.get(),
                                                            MegaNode::TYPE_FILE)})
                        (void)synchronousRemove(0, n.get());
            for (const auto& nm: localFiles)
                deleteFile(nm);
        });

    // --- ensure A<->B contact (tolerant of pre-existing contact state) ---
    std::unique_ptr<MegaUser> bContactOfA{megaApi[0]->getContact(mApi[1].email.c_str())};
    const bool alreadyContacts =
        bContactOfA && bContactOfA->getVisibility() == MegaUser::VISIBILITY_VISIBLE;
    if (!alreadyContacts)
    {
        const std::string msg = "SDK-6298 T8 inshare quota";
        mApi[0].contactRequestUpdated = false;
        ASSERT_NO_FATAL_FAILURE(
            inviteContact(1, mApi[0].email, msg, MegaContactRequest::INVITE_ACTION_ADD));
        ASSERT_TRUE(waitForResponse(&mApi[0].contactRequestUpdated))
            << "A did not receive B's contact request";
        ASSERT_NO_FATAL_FAILURE(getContactRequest(0, false));
        mApi[0].contactRequestUpdated = mApi[1].contactRequestUpdated = false;
        ASSERT_NO_FATAL_FAILURE(
            replyContact(mApi[0].cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT, 0));
        ASSERT_TRUE(waitForResponse(&mApi[0].contactRequestUpdated));
        ASSERT_TRUE(waitForResponse(&mApi[1].contactRequestUpdated));
        mApi[0].cr.reset();
    }

    // --- B shares the folder FULL with A; A waits for the inshare ---
    std::unique_ptr<MegaNode> bFolder{megaApi[1]->getNodeByHandle(bFolderHandle)};
    ASSERT_TRUE(bFolder);
    ASSERT_NO_FATAL_FAILURE(
        shareFolder(bFolder.get(), mApi[0].email.c_str(), MegaShare::ACCESS_FULL, 1));
    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return std::unique_ptr<MegaShareList>(megaApi[0]->getInSharesList())->size() >= 1;
        },
        60000))
        << "inshare never became visible to A";
    std::unique_ptr<MegaNode> inshareNode{megaApi[0]->getNodeByHandle(bFolderHandle)};
    ASSERT_TRUE(inshareNode) << "A cannot resolve the inshare folder node";

    std::unique_ptr<MegaNode> aRoot{megaApi[0]->getRootNode()};
    ASSERT_TRUE(aRoot);
    const ::mega::NodeHandle inshareH = toNodeHandle(bFolderHandle);
    const ::mega::NodeHandle aRootH = toNodeHandle(aRoot->getHandle());

    const std::string fileX = makeBinName("ws_quota_t8_X_");
    const std::string fileY = makeBinName("ws_quota_t8_Y_");
    constexpr size_t fileSize = kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileX, fileSize, "X"));
    ASSERT_TRUE(createFileWithSize(fileY, fileSize, "Y"));
    localFiles = {fileX, fileY};

    WsTfsIssuedCapture tfsIssued;
    WsQuotaHoldChangedCapture holdChanged;
    WsTfsResultScript script;
    WsQuotaHoldTracker holdTracker(megaApi[0].get());
    // Inshare (foreign) shortfall; own root generous.
    script.setDefaultPlan(WsTfsResultScript::withGroups(::mega::WsTfsGroupBalances{
        {static_cast<::m_off_t>(fileSize) - 1, {inshareH}},
        {kGenerousBytes, {aRootH}},
    }));

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(2, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));
    ScopedUploadSpeedLimit throttle{*megaApi[0], kThrottleBps};

    TransferTracker trackerX(megaApi[0].get()); // -> inshare (foreign)
    TransferTracker trackerY(megaApi[0].get()); // -> own root
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileX, inshareNode.get(), nullptr, &uploadOptions, &trackerX);
    megaApi[0]->startUpload(fileY, aRoot.get(), nullptr, &uploadOptions, &trackerY);
    ASSERT_TRUE(WaitFor(
        [&]
        {
            return trackerX.mTag.load() >= 0;
        },
        30000));
    const int xTag = trackerX.mTag.load();

    ASSERT_TRUE(tfsIssued.waitForFire(kIssueTimeout))
        << "tfs never issued after enqueue (issuances=0)";

    // X held with foreign flag set.
    ASSERT_TRUE(holdChanged.waitForHold(xTag, kHoldTimeout)) << "no hold on inshare-target upload";
    const auto hold = holdChanged.firstHold(xTag);
    ASSERT_TRUE(hold.has_value());
    ASSERT_TRUE(hold->foreign) << "inshare shortfall must be flagged foreign";
    ASSERT_TRUE(holdTracker.waitForTemporaryError(xTag, kHoldTimeout));
    const auto seqX = holdTracker.sequence(xTag);
    ASSERT_FALSE(seqX.empty());
    ASSERT_EQ(seqX.front().code, API_EOVERQUOTA);
    ASSERT_TRUE(seqX.front().foreignOverquota) << "isForeignOverquota() must be true for inshare";

    // Y (own root, generous) completes untouched.
    megaApi[0]->setMaxUploadSpeed(-1);
    ASSERT_EQ(trackerY.waitForResult(kCompleteTimeoutS), API_OK)
        << "own-root upload should be unaffected by the inshare hold";

    // Release X.
    script.setDefaultPlan(WsTfsResultScript::generous({inshareH, aRootH}));
    ASSERT_TRUE(invokeWsQuotaInvalidateOnClientThread(*megaApi[0])) << "M1 dispatch failed";
    ASSERT_TRUE(holdChanged.waitForRelease(xTag, kHoldTimeout));
    ASSERT_EQ(trackerX.waitForResult(kCompleteTimeoutS), API_OK)
        << "inshare upload did not complete after release";
}

// ============================================================================
// T9 QuotaLateTfsReplyAfterUploadCompleted  (HR43 x15)
// ============================================================================
TEST_F(SdkWsUploadTest, QuotaLateTfsReplyAfterUploadCompleted)
{
    LOG_info << "___TEST QuotaLateTfsReplyAfterUploadCompleted___";
    WSUPLOAD_REQUIRE_TEST_HOOKS();
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const std::string fileName = makeBinName("ws_quota_t9_");
    constexpr size_t fileSize = 1 * 1024 * 1024; // fast small file
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "9"));

    std::vector<std::string> localFiles{fileName};
    std::vector<std::string> rootUploadNames{fileName};
    auto cleanup = makeScopedDestructor(
        [&]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
            if (std::unique_ptr<MegaNode> root{megaApi[0]->getRootNode()})
                for (const auto& nm: rootUploadNames)
                    if (std::unique_ptr<MegaNode> n{
                            megaApi[0]->getNodeByPathOfType(nm.c_str(),
                                                            root.get(),
                                                            MegaNode::TYPE_FILE)})
                        (void)synchronousRemove(0, n.get());
            for (const auto& nm: localFiles)
                deleteFile(nm);
        });

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);
    const ::mega::NodeHandle rootH = toNodeHandle(rootnode->getHandle());

    WsTfsIssuedCapture tfsIssued;
    WsQuotaHoldChangedCapture holdChanged;
    WsTfsResultScript script;
    // gen 1 reply "lost" (Drop); later replies generous.
    script.setDefaultPlan(WsTfsResultScript::generous({rootH}));
    script.setPlanForGen(1, WsTfsResultScript::drop());

    TransferTracker tracker(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &tracker);
    ASSERT_TRUE(WaitFor(
        [&]
        {
            return tracker.mTag.load() >= 0;
        },
        30000));
    const int tag = tracker.mTag.load();

    ASSERT_TRUE(tfsIssued.waitForFire(kIssueTimeout))
        << "tfs never issued after enqueue (issuances=0)";

    // Upload completes API_OK (never blocks on tfs).
    ASSERT_EQ(tracker.waitForResult(kCompleteTimeoutS), API_OK)
        << "fast upload did not complete despite a dropped tfs reply";

    // No crash, and zero H3 hold events for the completed tag within a 10s quiet window.
    ASSERT_FALSE(holdChanged.waitForHold(tag, std::chrono::seconds(10)))
        << "a completed transfer must never be held by a late/lost reply";
    ASSERT_EQ(holdChanged.countFor(tag), 0);

    // A subsequent upload is evaluated against a fresh reply and completes.
    script.clearGenPlans(); // no more drops
    const std::string file2 = makeBinName("ws_quota_t9b_");
    ASSERT_TRUE(createFileWithSize(file2, fileSize, "b"));
    localFiles.push_back(file2);
    rootUploadNames.push_back(file2);
    TransferTracker tracker2(megaApi[0].get());
    megaApi[0]->startUpload(file2, rootnode.get(), nullptr, &uploadOptions, &tracker2);
    ASSERT_EQ(tracker2.waitForResult(kCompleteTimeoutS), API_OK)
        << "subsequent upload did not complete under a fresh reply";
}

// ============================================================================
// T10 QuotaStaleTfsReplyDiscardedOnUslRace  (HR43 x15)
// ============================================================================
TEST_F(SdkWsUploadTest, QuotaStaleTfsReplyDiscardedOnUslRace)
{
    LOG_info << "___TEST QuotaStaleTfsReplyDiscardedOnUslRace___";
    WSUPLOAD_REQUIRE_TEST_HOOKS();
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const std::string fileName = makeBinName("ws_quota_t10_");
    constexpr size_t fileSize = kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "A"));

    std::vector<std::string> localFiles{fileName};
    std::vector<std::string> rootUploadNames{fileName};
    auto cleanup = makeScopedDestructor(
        [&]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
            if (std::unique_ptr<MegaNode> root{megaApi[0]->getRootNode()})
                for (const auto& nm: rootUploadNames)
                    if (std::unique_ptr<MegaNode> n{
                            megaApi[0]->getNodeByPathOfType(nm.c_str(),
                                                            root.get(),
                                                            MegaNode::TYPE_FILE)})
                        (void)synchronousRemove(0, n.get());
            for (const auto& nm: localFiles)
                deleteFile(nm);
        });

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);
    const ::mega::NodeHandle rootH = toNodeHandle(rootnode->getHandle());

    WsTfsIssuedCapture tfsIssued;
    WsQuotaHoldChangedCapture holdChanged;
    WsTfsStaleDiscardedCapture staleDiscarded;
    WsTfsResultScript script;
    // gen-1: Requeue (bounded defer) + SHORTFALL so, if it ever applied, it would hold;
    // gen-2: generous. The requeue lands after gen-2 is current -> stale-discarded.
    script.setDefaultPlan(WsTfsResultScript::generous({rootH}));
    script.setPlanForGen(
        1,
        WsTfsResultScript::requeue(oneGroup(static_cast<::m_off_t>(fileSize) / 2, {rootH})));
    script.setPlanForGen(2, WsTfsResultScript::generous({rootH}));

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));
    ScopedUploadSpeedLimit throttle{*megaApi[0], kThrottleBps};

    TransferTracker tracker(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &tracker);
    ASSERT_TRUE(WaitFor(
        [&]
        {
            return tracker.mTag.load() >= 0;
        },
        30000));
    const int tag = tracker.mTag.load();

    ASSERT_TRUE(tfsIssued.waitForFire(kIssueTimeout))
        << "tfs never issued after enqueue (issuances=0)";

    // On H1(gen-1): M1 bumps the generation and issues #2.
    ASSERT_TRUE(invokeWsQuotaInvalidateOnClientThread(*megaApi[0])) << "M1 dispatch failed";
    ASSERT_TRUE(tfsIssued.waitForIssuance(2, kIssueTimeout))
        << "usl race did not issue a second tfs";

    // The requeued stale gen-1 application lands last -> discarded by the gen check (H4).
    ASSERT_TRUE(staleDiscarded.waitForFire(kHoldTimeout))
        << "stale tfs reply was not discarded (H4 never fired)";

    // No held=true events from the stale shortfall; completes API_OK.
    ASSERT_EQ(holdChanged.heldCountFor(tag), 0)
        << "a discarded stale shortfall must never produce a hold";
    megaApi[0]->setMaxUploadSpeed(-1);
    ASSERT_EQ(tracker.waitForResult(kCompleteTimeoutS), API_OK);
}

// ============================================================================
// T11 QuotaEnqueueWhileTfsInFlightIssuesFollowUp  (HR43 x15)
// ============================================================================
TEST_F(SdkWsUploadTest, QuotaEnqueueWhileTfsInFlightIssuesFollowUp)
{
    LOG_info << "___TEST QuotaEnqueueWhileTfsInFlightIssuesFollowUp___";
    WSUPLOAD_REQUIRE_TEST_HOOKS();
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    std::vector<std::string> localFiles;
    std::vector<::MegaHandle> createdFolders;
    auto cleanup = makeScopedDestructor(
        [&]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
            for (auto h: createdFolders)
                if (h != ::mega::UNDEF)
                    if (std::unique_ptr<MegaNode> n{megaApi[0]->getNodeByHandle(h)})
                        (void)synchronousRemove(0, n.get());
            for (const auto& nm: localFiles)
                deleteFile(nm);
        });

    const ::MegaHandle f1Handle = createFolder(0, makeBinName("t11F1_").c_str(), rootnode.get());
    const ::MegaHandle f2Handle = createFolder(0, makeBinName("t11F2_").c_str(), rootnode.get());
    ASSERT_NE(f1Handle, ::mega::UNDEF);
    ASSERT_NE(f2Handle, ::mega::UNDEF);
    createdFolders = {f1Handle, f2Handle};
    const ::mega::NodeHandle f1H = toNodeHandle(f1Handle);
    const ::mega::NodeHandle f2H = toNodeHandle(f2Handle);
    std::unique_ptr<MegaNode> f1{megaApi[0]->getNodeByHandle(f1Handle)};
    std::unique_ptr<MegaNode> f2{megaApi[0]->getNodeByHandle(f2Handle)};
    ASSERT_TRUE(f1 && f2);

    const std::string file1 = makeBinName("ws_quota_t11_1_");
    const std::string file2 = makeBinName("ws_quota_t11_2_");
    constexpr size_t fileSize = 2 * 1024 * 1024;
    ASSERT_TRUE(createFileWithSize(file1, fileSize, "1"));
    ASSERT_TRUE(createFileWithSize(file2, fileSize, "2"));
    localFiles = {file1, file2};

    WsTfsIssuedCapture tfsIssued;
    WsTfsResultScript script;
    script.setDefaultPlan(WsTfsResultScript::generous({f1H, f2H}));
    script.setPlanForGen(1, WsTfsResultScript::drop()); // gen 1 "lost" while file2 enqueued

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));
    ScopedUploadSpeedLimit throttle{*megaApi[0], kThrottleBps};

    TransferTracker tracker1(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(file1, f1.get(), nullptr, &uploadOptions, &tracker1);
    ASSERT_TRUE(WaitFor(
        [&]
        {
            return tracker1.mTag.load() >= 0;
        },
        30000));

    ASSERT_TRUE(tfsIssued.waitForFire(kIssueTimeout))
        << "no tfs issued for first enqueue (issuances=0)";
    ASSERT_EQ(handleSet(tfsIssued.foldersOfIssuance(0)), (std::set<std::uint64_t>{f1H.as8byte()}))
        << "first issuance should query only F1";

    // On H1(#1) enqueue file2 to F2 -> a follow-up tfs whose n includes F2.
    TransferTracker tracker2(megaApi[0].get());
    megaApi[0]->startUpload(file2, f2.get(), nullptr, &uploadOptions, &tracker2);
    ASSERT_TRUE(tfsIssued.waitForIssuance(2, kIssueTimeout))
        << "enqueue during in-flight did not issue a follow-up tfs";
    const auto unionAfter = handleSet(tfsIssued.unionFoldersFrom(1));
    ASSERT_TRUE(unionAfter.count(f2H.as8byte()) == 1)
        << "F2 not present in any post-enqueue issuance set";

    // Both complete under the generous applied balances.
    megaApi[0]->setMaxUploadSpeed(-1);
    ASSERT_EQ(tracker1.waitForResult(kCompleteTimeoutS), API_OK);
    ASSERT_EQ(tracker2.waitForResult(kCompleteTimeoutS), API_OK);
}

// ============================================================================
// T12 QuotaHoldInterplayWithUserPauseAndCancel
// ============================================================================
TEST_F(SdkWsUploadTest, QuotaHoldInterplayWithUserPauseAndCancel)
{
    LOG_info << "___TEST QuotaHoldInterplayWithUserPauseAndCancel___";
    WSUPLOAD_REQUIRE_TEST_HOOKS();
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    std::vector<std::string> localFiles;
    std::vector<::MegaHandle> createdFolders;
    auto cleanup = makeScopedDestructor(
        [&]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
            for (auto h: createdFolders)
                if (h != ::mega::UNDEF)
                    if (std::unique_ptr<MegaNode> n{megaApi[0]->getNodeByHandle(h)})
                        (void)synchronousRemove(0, n.get());
            for (const auto& nm: localFiles)
                deleteFile(nm);
        });

    const ::MegaHandle folderHandle =
        createFolder(0, makeBinName("t12folder_").c_str(), rootnode.get());
    ASSERT_NE(folderHandle, ::mega::UNDEF);
    createdFolders.push_back(folderHandle);
    const ::mega::NodeHandle folderH = toNodeHandle(folderHandle);
    std::unique_ptr<MegaNode> folder{megaApi[0]->getNodeByHandle(folderHandle)};
    ASSERT_TRUE(folder);

    const std::string file1 = makeBinName("ws_quota_t12_1_");
    const std::string file2 = makeBinName("ws_quota_t12_2_");
    const std::string file3 = makeBinName("ws_quota_t12_3_");
    constexpr size_t fileSize = kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(file1, fileSize, "1"));
    ASSERT_TRUE(createFileWithSize(file2, fileSize, "2"));
    ASSERT_TRUE(createFileWithSize(file3, fileSize, "3"));
    localFiles = {file1, file2, file3};

    WsTfsIssuedCapture tfsIssued;
    WsQuotaHoldChangedCapture holdChanged;
    WsTfsResultScript script;
    WsQuotaHoldTracker holdTracker(megaApi[0].get());
    script.setDefaultPlan(
        WsTfsResultScript::singleGroup(static_cast<::m_off_t>(fileSize) - 1, {folderH}));

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));
    ScopedUploadSpeedLimit throttle{*megaApi[0], kThrottleBps};
    auto uploadOptions = makeDefaultUploadOptions();

    // ---- Phase A: hold file1, user-pause, release quota -> stays paused, unpause -> completes.
    TransferTracker tracker1(megaApi[0].get());
    megaApi[0]->startUpload(file1, folder.get(), nullptr, &uploadOptions, &tracker1);
    ASSERT_TRUE(WaitFor(
        [&]
        {
            return tracker1.mTag.load() >= 0;
        },
        30000));
    const int tag1 = tracker1.mTag.load();

    ASSERT_TRUE(tfsIssued.waitForFire(kIssueTimeout))
        << "tfs never issued after enqueue (issuances=0)";
    ASSERT_TRUE(holdChanged.waitForHold(tag1, kHoldTimeout))
        << "hold never observed (holdEvents=0)";

    RequestTracker pauseReq(megaApi[0].get());
    megaApi[0]->pauseTransferByTag(tag1, true, &pauseReq);
    ASSERT_EQ(API_OK, pauseReq.waitForResult(60));

    // Release quota while user-paused: transfer must REMAIN paused (quiet window).
    script.setDefaultPlan(WsTfsResultScript::generous({folderH}));
    ASSERT_TRUE(invokeWsQuotaInvalidateOnClientThread(*megaApi[0])) << "M1 dispatch failed";
    {
        second_timer quiet;
        while (quiet.elapsed() < 10)
        {
            std::unique_ptr<MegaTransfer> t{megaApi[0]->getTransferByTag(tag1)};
            ASSERT_TRUE(!t || t->getState() == MegaTransfer::STATE_PAUSED)
                << "quota release must not resume a user-paused transfer";
            ASSERT_FALSE(tracker1.finished.load())
                << "user-paused transfer must not finish on quota release";
            WaitMillisec(300);
        }
    }

    RequestTracker unpauseReq(megaApi[0].get());
    megaApi[0]->pauseTransferByTag(tag1, false, &unpauseReq);
    ASSERT_EQ(API_OK, unpauseReq.waitForResult(60));
    megaApi[0]->setMaxUploadSpeed(-1);
    ASSERT_EQ(tracker1.waitForResult(kCompleteTimeoutS), API_OK)
        << "unpaused transfer did not complete";

    // ---- Phase B: hold file2, cancel -> EINCOMPLETE; file3 generous -> completes.
    ScopedUploadSpeedLimit throttleB{*megaApi[0], kThrottleBps};
    script.setDefaultPlan(
        WsTfsResultScript::singleGroup(static_cast<::m_off_t>(fileSize) - 1, {folderH}));
    ASSERT_TRUE(invokeWsQuotaInvalidateOnClientThread(*megaApi[0])) << "M1 dispatch failed";

    TransferTracker tracker2(megaApi[0].get());
    megaApi[0]->startUpload(file2, folder.get(), nullptr, &uploadOptions, &tracker2);
    ASSERT_TRUE(WaitFor(
        [&]
        {
            return tracker2.mTag.load() >= 0;
        },
        30000));
    const int tag2 = tracker2.mTag.load();
    ASSERT_TRUE(holdChanged.waitForHold(tag2, kHoldTimeout))
        << "phase B: file2 hold never observed";

    megaApi[0]->cancelTransferByTag(tag2);
    ASSERT_EQ(tracker2.waitForResult(kCompleteTimeoutS), API_EINCOMPLETE)
        << "cancelled held transfer should finish API_EINCOMPLETE";

    // file3 to the same folder under a generous balance completes (no leaked hold entry).
    script.setDefaultPlan(WsTfsResultScript::generous({folderH}));
    ASSERT_TRUE(invokeWsQuotaInvalidateOnClientThread(*megaApi[0])) << "M1 dispatch failed";
    megaApi[0]->setMaxUploadSpeed(-1);
    TransferTracker tracker3(megaApi[0].get());
    megaApi[0]->startUpload(file3, folder.get(), nullptr, &uploadOptions, &tracker3);
    ASSERT_EQ(tracker3.waitForResult(kCompleteTimeoutS), API_OK)
        << "file3 did not complete — cancelled transfer's ledger entry may have leaked";
}

// ============================================================================
// T13 QuotaEngineStopStartReevaluatesHolds
// ============================================================================
TEST_F(SdkWsUploadTest, QuotaEngineStopStartReevaluatesHolds)
{
    LOG_info << "___TEST QuotaEngineStopStartReevaluatesHolds___";
    WSUPLOAD_REQUIRE_TEST_HOOKS();
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const std::string fileName = makeBinName("ws_quota_t13_");
    constexpr size_t fileSize = kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "D"));

    std::vector<std::string> localFiles{fileName};
    std::vector<std::string> rootUploadNames{fileName};
    auto cleanup = makeScopedDestructor(
        [&]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
            if (std::unique_ptr<MegaNode> root{megaApi[0]->getRootNode()})
                for (const auto& nm: rootUploadNames)
                    if (std::unique_ptr<MegaNode> n{
                            megaApi[0]->getNodeByPathOfType(nm.c_str(),
                                                            root.get(),
                                                            MegaNode::TYPE_FILE)})
                        (void)synchronousRemove(0, n.get());
            for (const auto& nm: localFiles)
                deleteFile(nm);
        });

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);
    const ::mega::NodeHandle rootH = toNodeHandle(rootnode->getHandle());

    WsTfsIssuedCapture tfsIssued;
    WsQuotaHoldChangedCapture holdChanged;
    WsTfsResultScript script;
    WsQuotaHoldTracker holdTracker(megaApi[0].get());
    script.setDefaultPlan(
        WsTfsResultScript::singleGroup(static_cast<::m_off_t>(fileSize) - 1, {rootH}));

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));
    ScopedUploadSpeedLimit throttle{*megaApi[0], kThrottleBps};

    TransferTracker tracker(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &tracker);
    ASSERT_TRUE(WaitFor(
        [&]
        {
            return tracker.mTag.load() >= 0;
        },
        30000));
    const int tag = tracker.mTag.load();

    ASSERT_TRUE(tfsIssued.waitForFire(kIssueTimeout))
        << "tfs never issued after enqueue (issuances=0)";
    ASSERT_TRUE(holdChanged.waitForHold(tag, kHoldTimeout))
        << "predictive hold never observed (holdEvents=0)";
    const std::size_t issuancesBeforeRestart = tfsIssued.issuanceCount();

    // Re-arm generous, then stop/start the same engine (mechanism from
    // StopStartSameEngineDuringTransfer). A fresh tfs after restart re-evaluates the hold.
    script.setDefaultPlan(WsTfsResultScript::generous({rootH}));
    ASSERT_TRUE(restartWsUploadEngineForTesting(*megaApi[0], 10))
        << "failed to restart the WS engine";
    ASSERT_TRUE(tfsIssued.waitForIssuance(issuancesBeforeRestart + 1, kIssueTimeout))
        << "no fresh tfs issued after engine restart";
    ASSERT_TRUE(holdChanged.waitForRelease(tag, kHoldTimeout))
        << "hold not re-evaluated/released after engine restart";
    megaApi[0]->setMaxUploadSpeed(-1);
    ASSERT_EQ(tracker.waitForResult(kCompleteTimeoutS), API_OK);

    // HoldTracker sane: >=1 EOVERQUOTA temp, exactly 1 finish OK.
    ASSERT_GE(holdTracker.temporaryErrorCount(tag), 1);
    ASSERT_EQ(holdTracker.finishCount(tag), 1);
    const auto seq = holdTracker.sequence(tag);
    ASSERT_FALSE(seq.empty());
    ASSERT_EQ(seq.back().kind, WsQuotaHoldTracker::Event::Kind::Finish);
    ASSERT_EQ(seq.back().code, API_OK);
}

// ============================================================================
// T14 QuotaAccountRedOverquotaCoexistsWithPredictiveHolds  (HR43 x15)
// ============================================================================
TEST_F(SdkWsUploadTest, QuotaAccountRedOverquotaCoexistsWithPredictiveHolds)
{
    LOG_info << "___TEST QuotaAccountRedOverquotaCoexistsWithPredictiveHolds___";
    WSUPLOAD_REQUIRE_TEST_HOOKS();
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    std::vector<std::string> localFiles;
    std::vector<::MegaHandle> createdFolders;
    auto cleanup = makeScopedDestructor(
        [&]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
            for (auto h: createdFolders)
                if (h != ::mega::UNDEF)
                    if (std::unique_ptr<MegaNode> n{megaApi[0]->getNodeByHandle(h)})
                        (void)synchronousRemove(0, n.get());
            for (const auto& nm: localFiles)
                deleteFile(nm);
        });

    const ::MegaHandle f1Handle = createFolder(0, makeBinName("t14F1_").c_str(), rootnode.get());
    const ::MegaHandle f2Handle = createFolder(0, makeBinName("t14F2_").c_str(), rootnode.get());
    ASSERT_NE(f1Handle, ::mega::UNDEF);
    ASSERT_NE(f2Handle, ::mega::UNDEF);
    createdFolders = {f1Handle, f2Handle};
    const ::mega::NodeHandle f1H = toNodeHandle(f1Handle);
    const ::mega::NodeHandle f2H = toNodeHandle(f2Handle);
    std::unique_ptr<MegaNode> f1{megaApi[0]->getNodeByHandle(f1Handle)};
    std::unique_ptr<MegaNode> f2{megaApi[0]->getNodeByHandle(f2Handle)};
    ASSERT_TRUE(f1 && f2);

    const std::string fileX = makeBinName("ws_quota_t14_X_"); // -> F1 generous, running
    const std::string fileY = makeBinName("ws_quota_t14_Y_"); // -> F2 shortfall, held
    constexpr size_t fileSize = kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileX, fileSize, "X"));
    ASSERT_TRUE(createFileWithSize(fileY, fileSize, "Y"));
    localFiles = {fileX, fileY};

    WsTfsIssuedCapture tfsIssued;
    WsQuotaHoldChangedCapture holdChanged;
    WsTfsResultScript script;
    WsQuotaHoldTracker holdTracker(megaApi[0].get());
    script.setDefaultPlan(WsTfsResultScript::withGroups(::mega::WsTfsGroupBalances{
        {kGenerousBytes, {f1H}},
        {static_cast<::m_off_t>(fileSize) - 1, {f2H}},
    }));

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(2, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));
    ScopedUploadSpeedLimit throttle{*megaApi[0], kThrottleBps};

    TransferTracker trackerY(megaApi[0].get());
    TransferTracker trackerX(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileY, f2.get(), nullptr, &uploadOptions, &trackerY);
    megaApi[0]->startUpload(fileX, f1.get(), nullptr, &uploadOptions, &trackerX);
    ASSERT_TRUE(WaitFor(
        [&]
        {
            return trackerY.mTag.load() >= 0 && trackerX.mTag.load() >= 0;
        },
        30000));
    const int yTag = trackerY.mTag.load();
    const int xTag = trackerX.mTag.load();

    ASSERT_TRUE(tfsIssued.waitForFire(kIssueTimeout))
        << "tfs never issued after enqueue (issuances=0)";
    ASSERT_TRUE(holdChanged.waitForHold(yTag, kHoldTimeout)) << "predictive hold never observed";
    const int yHeldEventsBefore = holdChanged.heldCountFor(yTag);

    // Inject the legacy WS chunk-send OVERQUOTA on X (existing activateoverquota path).
    WsChunkSendOverquotaCapture legacyOverquota(xTag);
    megaApi[0]->setMaxUploadSpeed(-1);
    ASSERT_TRUE(legacyOverquota.waitForFire(std::chrono::seconds(60)))
        << "legacy chunk-send overquota hook never fired for X";
    ASSERT_TRUE(holdTracker.waitForTemporaryError(xTag, kHoldTimeout));

    // Y's predictive hold intact + correctly attributed (H3 events for Y unchanged).
    ASSERT_EQ(holdChanged.heldCountFor(yTag), yHeldEventsBefore)
        << "Y's predictive hold perturbed by X's account-RED overquota";
    ASSERT_FALSE(trackerY.finished.load()) << "Y must remain held, not finish";

    // Cancel X; release Y -> Y completes; release must not resurrect X.
    megaApi[0]->cancelTransferByTag(xTag);
    ASSERT_EQ(trackerX.waitForResult(kCompleteTimeoutS), API_EINCOMPLETE);

    script.setDefaultPlan(WsTfsResultScript::generous({f1H, f2H}));
    ASSERT_TRUE(invokeWsQuotaInvalidateOnClientThread(*megaApi[0])) << "M1 dispatch failed";
    ASSERT_TRUE(holdChanged.waitForRelease(yTag, kHoldTimeout));
    ASSERT_EQ(trackerY.waitForResult(kCompleteTimeoutS), API_OK);
    ASSERT_EQ(trackerX.result.load(), static_cast<ErrorCodes>(API_EINCOMPLETE))
        << "cancelled X must not be resurrected by Y's release";
}

// ============================================================================
// T15 QuotaTfsApiErrorFailsOpen
// ============================================================================
TEST_F(SdkWsUploadTest, QuotaTfsApiErrorFailsOpen)
{
    LOG_info << "___TEST QuotaTfsApiErrorFailsOpen___";
    WSUPLOAD_REQUIRE_TEST_HOOKS();
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    std::vector<std::string> localFiles;
    std::vector<std::string> rootUploadNames;
    auto cleanup = makeScopedDestructor(
        [&]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
            if (std::unique_ptr<MegaNode> root{megaApi[0]->getRootNode()})
                for (const auto& nm: rootUploadNames)
                    if (std::unique_ptr<MegaNode> n{
                            megaApi[0]->getNodeByPathOfType(nm.c_str(),
                                                            root.get(),
                                                            MegaNode::TYPE_FILE)})
                        (void)synchronousRemove(0, n.get());
            for (const auto& nm: localFiles)
                deleteFile(nm);
        });

    WsTfsIssuedCapture tfsIssued;
    WsQuotaHoldChangedCapture holdChanged;
    WsTfsResultScript script;
    // Force a command-level error on every gen -> apply nothing, fail-open.
    script.setDefaultPlan(WsTfsResultScript::forceError(API_EACCESS));

    constexpr size_t fileSize = 1 * 1024 * 1024;
    std::vector<std::unique_ptr<TransferTracker>> trackers;
    auto uploadOptions = makeDefaultUploadOptions();
    for (int i = 0; i < 3; ++i)
    {
        const std::string nm = makeBinName("ws_quota_t15_") + "_" + std::to_string(i);
        ASSERT_TRUE(createFileWithSize(nm, fileSize, std::to_string(i)));
        localFiles.push_back(nm);
        rootUploadNames.push_back(nm);
        trackers.push_back(std::make_unique<TransferTracker>(megaApi[0].get()));
        megaApi[0]->startUpload(nm, rootnode.get(), nullptr, &uploadOptions, trackers.back().get());
    }

    ASSERT_TRUE(tfsIssued.waitForFire(kIssueTimeout)) << "no tfs issued (issuances=0)";

    // All complete API_OK; zero holds; bounded issuance volume (no retry storm).
    for (auto& t: trackers)
        ASSERT_EQ(t->waitForResult(kCompleteTimeoutS), API_OK)
            << "upload should fail-open and complete despite the tfs API error";
    ASSERT_EQ(holdChanged.totalEvents(), 0u) << "API-error replies must produce zero holds";
    ASSERT_LE(tfsIssued.issuanceCount(), 8u)
        << "tfs API error triggered a retry storm (issuances=" << tfsIssued.issuanceCount() << ")";

    // Sub-phase: a transient API error variant likewise fails open.
    script.setDefaultPlan(WsTfsResultScript::forceError(API_ETEMPUNAVAIL));
    const std::string nm = makeBinName("ws_quota_t15b_");
    ASSERT_TRUE(createFileWithSize(nm, fileSize, "b"));
    localFiles.push_back(nm);
    rootUploadNames.push_back(nm);
    TransferTracker t2(megaApi[0].get());
    megaApi[0]->startUpload(nm, rootnode.get(), nullptr, &uploadOptions, &t2);
    ASSERT_EQ(t2.waitForResult(kCompleteTimeoutS), API_OK);
}

// ============================================================================
// T16 QuotaMalformedTfsReplyIgnoredFailsOpen
// ============================================================================
TEST_F(SdkWsUploadTest, QuotaMalformedTfsReplyIgnoredFailsOpen)
{
    LOG_info << "___TEST QuotaMalformedTfsReplyIgnoredFailsOpen___";
    WSUPLOAD_REQUIRE_TEST_HOOKS();
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);
    const ::mega::NodeHandle rootH = toNodeHandle(rootnode->getHandle());

    std::vector<std::string> localFiles;
    std::vector<std::string> rootUploadNames;
    auto cleanup = makeScopedDestructor(
        [&]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
            if (std::unique_ptr<MegaNode> root{megaApi[0]->getRootNode()})
                for (const auto& nm: rootUploadNames)
                    if (std::unique_ptr<MegaNode> n{
                            megaApi[0]->getNodeByPathOfType(nm.c_str(),
                                                            root.get(),
                                                            MegaNode::TYPE_FILE)})
                        (void)synchronousRemove(0, n.get());
            for (const auto& nm: localFiles)
                deleteFile(nm);
        });

    WsTfsIssuedCapture tfsIssued;
    WsQuotaHoldChangedCapture holdChanged;
    WsTfsResultScript script;
    // Garbage groups: empty vector, negative-balance group, unknown-handle group.
    const ::mega::NodeHandle unknownHandle = ::mega::NodeHandle().set6byte(0x0000ABCDEF12);
    script.setDefaultPlan(WsTfsResultScript::withGroups(::mega::WsTfsGroupBalances{
        {-5, {rootH}}, // negative -> group skipped (fail-open)
        {100, {unknownHandle}}, // unknown handle -> ignored
        {0, {}}, // empty handle set
    }));

    constexpr size_t fileSize = 1 * 1024 * 1024;
    std::vector<std::unique_ptr<TransferTracker>> trackers;
    auto uploadOptions = makeDefaultUploadOptions();
    for (int i = 0; i < 3; ++i)
    {
        const std::string nm = makeBinName("ws_quota_t16_") + "_" + std::to_string(i);
        ASSERT_TRUE(createFileWithSize(nm, fileSize, std::to_string(i)));
        localFiles.push_back(nm);
        rootUploadNames.push_back(nm);
        trackers.push_back(std::make_unique<TransferTracker>(megaApi[0].get()));
        megaApi[0]->startUpload(nm, rootnode.get(), nullptr, &uploadOptions, trackers.back().get());
    }

    ASSERT_TRUE(tfsIssued.waitForFire(kIssueTimeout)) << "no tfs issued (issuances=0)";

    for (auto& t: trackers)
        ASSERT_EQ(t->waitForResult(kCompleteTimeoutS), API_OK)
            << "malformed reply must be ignored (fail-open) and uploads complete";
    ASSERT_EQ(holdChanged.totalEvents(), 0u) << "malformed reply produced a false hold";
}

// ============================================================================
// T17 QuotaPartialReplyMissingFoldersFailsOpen
// ============================================================================
TEST_F(SdkWsUploadTest, QuotaPartialReplyMissingFoldersFailsOpen)
{
    LOG_info << "___TEST QuotaPartialReplyMissingFoldersFailsOpen___";
    WSUPLOAD_REQUIRE_TEST_HOOKS();
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    std::vector<std::string> localFiles;
    std::vector<::MegaHandle> createdFolders;
    auto cleanup = makeScopedDestructor(
        [&]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
            for (auto h: createdFolders)
                if (h != ::mega::UNDEF)
                    if (std::unique_ptr<MegaNode> n{megaApi[0]->getNodeByHandle(h)})
                        (void)synchronousRemove(0, n.get());
            for (const auto& nm: localFiles)
                deleteFile(nm);
        });

    const ::MegaHandle f1Handle = createFolder(0, makeBinName("t17F1_").c_str(), rootnode.get());
    const ::MegaHandle f2Handle = createFolder(0, makeBinName("t17F2_").c_str(), rootnode.get());
    ASSERT_NE(f1Handle, ::mega::UNDEF);
    ASSERT_NE(f2Handle, ::mega::UNDEF);
    createdFolders = {f1Handle, f2Handle};
    const ::mega::NodeHandle f1H = toNodeHandle(f1Handle);
    std::unique_ptr<MegaNode> f1{megaApi[0]->getNodeByHandle(f1Handle)};
    std::unique_ptr<MegaNode> f2{megaApi[0]->getNodeByHandle(f2Handle)};
    ASSERT_TRUE(f1 && f2);

    const std::string file1 = makeBinName("ws_quota_t17_1_");
    const std::string file2 = makeBinName("ws_quota_t17_2_");
    constexpr size_t fileSize = 2 * 1024 * 1024;
    ASSERT_TRUE(createFileWithSize(file1, fileSize, "1"));
    ASSERT_TRUE(createFileWithSize(file2, fileSize, "2"));
    localFiles = {file1, file2};

    WsTfsIssuedCapture tfsIssued;
    WsQuotaHoldChangedCapture holdChanged;
    WsTfsResultScript script;
    // Cover only folder 1 (generous); folder 2 absent from the reply -> fail-open, never held.
    script.setDefaultPlan(WsTfsResultScript::generous({f1H}));

    auto uploadOptions = makeDefaultUploadOptions();
    TransferTracker tracker1(megaApi[0].get());
    TransferTracker tracker2(megaApi[0].get());
    megaApi[0]->startUpload(file1, f1.get(), nullptr, &uploadOptions, &tracker1);
    megaApi[0]->startUpload(file2, f2.get(), nullptr, &uploadOptions, &tracker2);

    ASSERT_TRUE(tfsIssued.waitForFire(kIssueTimeout)) << "no tfs issued (issuances=0)";

    // Upload to the uncovered folder 2 proceeds + completes; upload 1 evaluated normally.
    ASSERT_EQ(tracker2.waitForResult(kCompleteTimeoutS), API_OK)
        << "upload to a folder absent from the reply must fail-open and complete";
    ASSERT_EQ(tracker1.waitForResult(kCompleteTimeoutS), API_OK);
    ASSERT_EQ(holdChanged.totalEvents(), 0u) << "partial reply produced a false hold";
}

// ============================================================================
// T21 QuotaManyUploadsGenerousBalanceSoak
// ============================================================================
TEST_F(SdkWsUploadTest, QuotaManyUploadsGenerousBalanceSoak)
{
    LOG_info << "___TEST QuotaManyUploadsGenerousBalanceSoak___";
    WSUPLOAD_REQUIRE_TEST_HOOKS();
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Mixed-size files from the USC size classes (reuse RepeatedPauseResumeMixedPools sizing).
    std::vector<::m_off_t> sizeClasses;
    ASSERT_TRUE(fetchUscSizeClasses(*megaApi[0], sizeClasses, 60))
        << "Unable to fetch USC size classes";
    std::vector<::m_off_t> finiteClasses;
    for (const ::m_off_t c: sizeClasses)
        if (c > 1)
            finiteClasses.push_back(c);
    std::sort(finiteClasses.begin(), finiteClasses.end());
    finiteClasses.erase(std::unique(finiteClasses.begin(), finiteClasses.end()),
                        finiteClasses.end());
    if (finiteClasses.empty())
        GTEST_SKIP() << "Could not derive a usable USC class boundary";
    const size_t sizeA = static_cast<size_t>(finiteClasses.front() - 1);
    const size_t sizeB = static_cast<size_t>(finiteClasses.front());

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    std::vector<std::string> localFiles;
    std::vector<::MegaHandle> createdFolders;
    auto cleanup = makeScopedDestructor(
        [&]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
            for (auto h: createdFolders)
                if (h != ::mega::UNDEF)
                    if (std::unique_ptr<MegaNode> n{megaApi[0]->getNodeByHandle(h)})
                        (void)synchronousRemove(0, n.get());
            for (const auto& nm: localFiles)
                deleteFile(nm);
        });

    // 3 folders.
    std::vector<::mega::NodeHandle> folderHandles;
    std::vector<std::unique_ptr<MegaNode>> folderNodes;
    for (int i = 0; i < 3; ++i)
    {
        const ::MegaHandle h = createFolder(0, makeBinName("t21folder_").c_str(), rootnode.get());
        ASSERT_NE(h, ::mega::UNDEF);
        createdFolders.push_back(h);
        folderHandles.push_back(toNodeHandle(h));
        folderNodes.emplace_back(megaApi[0]->getNodeByHandle(h));
        ASSERT_TRUE(folderNodes.back());
    }

    WsTfsIssuedCapture tfsIssued;
    WsQuotaHoldChangedCapture holdChanged;
    WsTfsResultScript script; // observe-only passthrough (fail-open default)

    // 10 mixed-size files across the 3 folders, enqueued in one batch.
    std::vector<std::unique_ptr<TransferTracker>> trackers;
    auto uploadOptions = makeDefaultUploadOptions();
    for (int i = 0; i < 10; ++i)
    {
        const size_t sz = (i % 2 == 0) ? sizeA : sizeB;
        const std::string nm = makeBinName("ws_quota_t21_") + "_" + std::to_string(i);
        ASSERT_TRUE(createFileWithSize(nm, sz, std::to_string(i)));
        localFiles.push_back(nm);
        trackers.push_back(std::make_unique<TransferTracker>(megaApi[0].get()));
        megaApi[0]->startUpload(nm,
                                folderNodes[static_cast<size_t>(i) % 3].get(),
                                nullptr,
                                &uploadOptions,
                                trackers.back().get());
    }

    ASSERT_TRUE(tfsIssued.waitForFire(kIssueTimeout)) << "no tfs issued (issuances=0)";

    // All complete API_OK; zero holds; coalescing bound (<= number of enqueue batches).
    for (auto& t: trackers)
        ASSERT_EQ(t->waitForResult(kCompleteTimeoutS), API_OK)
            << "generous-balance soak upload did not complete";
    ASSERT_EQ(holdChanged.totalEvents(), 0u)
        << "generous balance must never hold (false-positive hold)";
    ASSERT_LE(tfsIssued.issuanceCount(), 10u)
        << "coalescing bound exceeded (issuances=" << tfsIssued.issuanceCount() << ")";
}

} // namespace mega::test::wsupload
