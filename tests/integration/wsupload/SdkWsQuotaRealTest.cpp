/**
 * @file SdkWsQuotaRealTest.cpp
 * @brief SDK-6298 WS upload-quota REAL-PATH integration cells (T18, T20) and the
 *        SDK-6610 own-account shortfall cells (T19 successor + exact QA geometry).
 *
 * Unlike the hook-driven SdkWsUploadTest.Quota* cells (SdkWsQuotaTest.cpp), these
 * exercise the predictive WS upload-quota path end-to-end against the LIVE server:
 *  - T18 ProbeTfsCommandOwnRoot  — cheap deployment probe of the committed
 *    production `tfs` command + parser (PASSES today; the command/parser are
 *    committed and validated by unit cells + a live raw probe).
 *  - RealFillOwnAccountShortfallIsNotHeld (replaces T19
 *    RealFillOwnAccountHoldAndRelease) and RealFillOwnAccountQaGeometryIsNotHeld
 *    (SDK-6610, v10.21.0 RC2) — fill the OWN account to a GREEN/ORANGE-but-
 *    insufficient state (1.5 GiB free / 2.25 GiB upload; and the exact QA geometry,
 *    8388608 B free / 10 MiB upload) and assert that the own-account shortfall is
 *    NOT predictively held: no EOVERQUOTA before all bytes without EVENT_STORAGE
 *    RED/PAYWALL, an app-actionable terminal state (finish OK/EOVERQUOTA, or the
 *    legacy RETRYING with EVENT_STORAGE RED), a natural EVENT_STORAGE RED/PAYWALL, and
 *    no temp error MEGAsync cannot act on (OverquotaAppContractRecorder.h). Premise:
 *    the public queue-fit query reports the own shortfall while the upload runs.
 *    The SDK-6298 own hold stranded exactly this upload at 0 bytes with no dialog
 *    (QA, MEGAsync 6.7.0).
 *  - T20 RealFillInshareForeignHold — account B fills its own storage and shares a
 *    folder FULL to A; A's upload into the inshare is predictively held with the
 *    foreign flag; A's own-root control upload is untouched.
 *
 * DELIBERATE tier-1 escape: the fixture is `SdkWsQuotaRealTest` (NOT
 * SdkWsUploadTest) so the ci_tier1_gate SdkWsUploadTest.* filter does not pick up
 * the slow real-fill cells (T19/T20 take minutes). The cheap probe cell (T18) is
 * wired into the gate separately to keep the fixture escape non-silent.
 *
 * HOOK-FREE by design: all three cells assert on the public MegaTransferListener
 * surface (temporary errors + isForeignOverquota() + progress-at-error) and the
 * production `tfs` command, so they run in hooks-OFF builds too — no
 * WSUPLOAD_REQUIRE_TEST_HOOKS gate.
 *
 * Fail-first history: these cells predate the predictive ledger (P3) and landed
 * their first behavioural failures on the discriminator "no predictive hold
 * observed before upload sent all bytes" (foreign variant for T20). Pre-P3 the
 * legacy WS path streamed ALL bytes and surfaced the own-account overquota only at
 * putnodes (transferred == total) — the behaviour the SDK-6610 own-account cells
 * now require again. The old T19 release leg (deletion-driven re-query) has no
 * own-account hold left to release; T20 keeps the foreign discriminator.
 *
 * These cells run on the MR pipeline surface (user ruling 2026-07-30: no nightly
 * filter; the release path is deterministic via the deletion trigger).
 *
 * SHARED-ACCOUNT SAFETY: the real-fill cells fill a shared test account. Every
 * created cloud resource is torn down through a body-scope RAII ScopedDestructor
 * that runs on EVERY exit path (pass / ASSERT-fail / GTEST_SKIP-after-import),
 * mirroring SdkTest.SdkTestUploadsOverquota. A leftover full account would break
 * every subsequent bench/test on this shared account; the own-account cells also
 * make sure the SDK leaves the cached RED state their overflow may cause.
 *
 * Waits on uploads are progress-aware (SDK-6470 followup1: a fixed 420 s budget
 * timed out a healthy 2.25 GiB upload on a 3.95 MB/s macOS uplink): a wait fails
 * only when the upload shows no activity for a bounded time (no change of the app
 * byte count nor of the client-side WS progressCompleted, which, unlike the clamped
 * app count, drops to 0 on a WS restart), under an absolute safety cap.
 *
 * `::mega::` prefixes in using-decls (C++20, feedback_cxx_standard_per_target.md).
 */

#include "mega/megaclient.h" // MegaClient::getMegaURL
#include "mega/scoped_helpers.h"
#include "mega/transfer/ws/ws_quota_types.h"
#include "mega/types.h"
#include "megaapi.h"
#include "SdkTest_test.h"
#include "test.h"
#include "wsupload/headers/OverquotaAppContractRecorder.h"
#include "wsupload/headers/ScopedUploadSpeedLimit.h"
#include "wsupload/headers/WsQuotaClientThread.h"
#include "wsupload/headers/WsQuotaHoldTracker.h"
#include "wsupload/headers/WsTfsCommand.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace std;

namespace mega::test::wsupload
{

namespace
{
// Build a masked 6-byte NodeHandle from a MegaApi handle.
::mega::NodeHandle toNodeHandle(::MegaHandle h)
{
    return ::mega::NodeHandle().set6byte(h);
}

std::string makeBinName(const char* prefix)
{
    return std::string(prefix) +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
}

// The canonical shared 1 GiB public link used by the SdkTest overquota fill cells
// (SdkTest.SdkTestUploadsOverquota, SdkTest.SyncOQTransitions).
std::string seedLinkUrl()
{
    return ::mega::MegaClient::getMegaURL() +
           "/file/gzlQ3DIY#Ak-OW4MP7lhnQxP9nzBU1bOP45xr_7sXnIz8YYqOBUg";
}

constexpr ::m_off_t kUploadSize = 16LL * 1024 * 1024; // 16 MiB control/probe upload
// Discriminator sizing: the fill leaves kFreeToLeave free — enough that the server
// keeps usl OFF RED (RED sits near-full: ~99.9% observed; account-RED machinery
// pre-blocks/kills uploads via activateoverquota and would satisfy the
// discriminator WITHOUT any tfs logic) — and the discriminator upload is larger
// than the free space, so pre-P3 it must stream ALL bytes and fail only at
// putnodes, while the predictive tfs hold stops it at 0 bytes.
constexpr ::m_off_t kFreeToLeave = 1536LL * 1024 * 1024; // 1.5 GiB free after fill
constexpr ::m_off_t kDiscriminatorSize = 2304LL * 1024 * 1024; // 2.25 GiB > kFreeToLeave
constexpr int kDiscriminatorWaitS = 240; // upper bound: 2.25 GiB stream + putnodes (pre-P3 legacy)
constexpr int kReleaseWaitS = 420; // T20 own-root 16 MiB control upload completion budget
constexpr int kForeignReleaseWaitS = 120; // T20 SOFT release window (no foreign re-poll by ruling)

// Exact QA geometry (MEGAsync 6.7.0 RC1 / SDK v10.21.0-rc.1): 8 MiB free, 10 MiB upload.
constexpr ::m_off_t kQaFreeToLeave = 8388608;
constexpr ::m_off_t kQaDiscriminatorSize = 10LL * 1024 * 1024;
// The QA discriminator is throttled so the ledger's enqueue tfs (~one cs round-trip)
// lands long before a fast CI uplink finishes 10 MiB — otherwise a buggy tree could
// complete the upload before its hold engages and pass vacuously. 10 MiB ~ 40 s.
constexpr int kQaThrottleBps = 256 * 1024;
// The QA fill leaves kQaFreeToLeave + kQaFillMargin free by ACCOUNT DETAILS (a drift
// between that figure and tfs can then never overflow the account), and the tfs-driven
// top-up lands the rest exactly on kQaFreeToLeave (the figure the ledger holds on).
constexpr ::m_off_t kQaFillMargin = 256LL * 1024 * 1024;
constexpr ::m_off_t kMaxTopUpBytes = 1LL << 30; // bound: the top-up only corrects drift
constexpr int kMaxTopUpRounds = 3;
constexpr unsigned kTopUpReflectWaitMs = 60000; // tfs must reflect a top-up before the next
// The queue-fit premise (audit M1): the ledger's own enqueue tfs lands ~0.1-1.4 s after
// the upload starts; poll it for at most this long.
constexpr std::chrono::milliseconds kQueueFitPremiseTimeout{10000};

// Progress-aware upload waits (SDK-6470 followup1 §5): fail only when the upload shows
// no activity (app bytes / WS progressCompleted) for this long (a wedge, e.g. an
// indefinite hold), under an absolute cap.
constexpr std::chrono::seconds kUploadNoProgressLimit{90};
constexpr std::chrono::seconds kUploadSafetyCap{1200};
// A natural EVENT_STORAGE RED/PAYWALL must reach the app this soon after the terminal
// state of an own-account overflow (putnodes rejection, or the usl update after an
// accepted overflow). No forced `uq` before it: that would manufacture the event.
constexpr std::chrono::seconds kRedEventWindow{180};
// Restoration: wait this long for a natural non-RED EVENT_STORAGE after freeing the
// fill before refreshing the cached storage state with an account-details request.
constexpr std::chrono::seconds kRestoreNaturalWait{120};
constexpr unsigned kRestoreRefreshWaitMs = 30000;

// Fill-conformance guards: the controlled fill only makes sense on a test-account
// plan shaped like the FREE 25 GiB baseline. A plan too small cannot host the
// discriminator; a plan too large would need an unbounded number of seed copies
// (and hours of fill) — SKIP instead of mis-targeting a live account.
constexpr long long kMaxConformingPlanBytes = 100LL * 1024 * 1024 * 1024; // 100 GiB
constexpr long long kMaxFillCopies = 32; // FREE 25 GiB needs ~22; anything above is non-conforming

// Records, per transfer tag, the transfer's byte progress and foreign flag at the
// FIRST temporary EOVERQUOTA it emits. This is the race-free "was the hold
// predictive (fired BEFORE all bytes were sent)?" discriminator:
//   - predictive P3 hold  -> tfs preflight holds the upload, EOVERQUOTA fires at
//                            transferred ~= 0  (transferred < total)
//   - legacy pre-P3       -> all bytes upload to storage, EOVERQUOTA surfaces only
//                            at putnodes, transferred == total.
// Reading transfer->getTransferredBytes() directly in the error callback avoids
// depending on onTransferUpdate cadence (which can plateau below total).
// WsQuotaHoldTracker records the ordered temp-error/finish sequence but NOT
// progress, so this listener complements it. Global transfer listener.
class OverquotaProgressLatch: public ::mega::MegaTransferListener
{
public:
    struct AtError
    {
        ::m_off_t transferred = 0;
        ::m_off_t total = 0;
        bool foreign = false;
    };

    explicit OverquotaProgressLatch(::mega::MegaApi* api):
        mApi(api)
    {
        if (mApi)
            mApi->addTransferListener(this);
    }

    ~OverquotaProgressLatch() override
    {
        if (mApi)
            mApi->removeTransferListener(this);
    }

    OverquotaProgressLatch(const OverquotaProgressLatch&) = delete;
    OverquotaProgressLatch& operator=(const OverquotaProgressLatch&) = delete;

    void onTransferTemporaryError(::mega::MegaApi*,
                                  ::mega::MegaTransfer* transfer,
                                  ::mega::MegaError* error) override
    {
        if (!transfer)
            return;
        const int code = error ? error->getErrorCode() : ::mega::API_EINTERNAL;
        if (code != ::mega::API_EOVERQUOTA)
            return;
        std::lock_guard<std::mutex> lk(mMutex);
        const int tag = transfer->getTag();
        if (mByTag.find(tag) != mByTag.end())
            return; // first EOVERQUOTA per tag only
        mByTag[tag] = AtError{transfer->getTransferredBytes(),
                              transfer->getTotalBytes(),
                              transfer->isForeignOverquota()};
    }

    bool has(int tag) const
    {
        std::lock_guard<std::mutex> lk(mMutex);
        return mByTag.find(tag) != mByTag.end();
    }

    AtError at(int tag) const
    {
        std::lock_guard<std::mutex> lk(mMutex);
        auto it = mByTag.find(tag);
        return it == mByTag.end() ? AtError{} : it->second;
    }

    // Discriminator: a temp EOVERQUOTA fired for `tag` while NOT all bytes had
    // been transferred to storage yet (i.e. the hold was predictive).
    bool sawOverquotaBeforeAllBytes(int tag) const
    {
        std::lock_guard<std::mutex> lk(mMutex);
        auto it = mByTag.find(tag);
        return it != mByTag.end() && it->second.total > 0 &&
               it->second.transferred < it->second.total;
    }

private:
    ::mega::MegaApi* mApi = nullptr;
    mutable std::mutex mMutex;
    std::map<int, AtError> mByTag;
};
} // namespace

// ============================================================================
// Real-path fixture. Deliberately NOT named SdkWsUploadTest (tier-1 escape).
// Subclasses SdkTest directly — the fill mechanics it clones live on SdkTest
// (SdkTestUploadsOverquota is a TEST_F(SdkTest, ...)).
// ============================================================================
class SdkWsQuotaRealTest: public SdkTest
{
protected:
    // Returns "" if `tfs` is usable for apiIndex's own root; otherwise a
    // human-readable skip reason. One cheap tfs round-trip.
    std::string tfsProbeSkipReason(unsigned apiIndex)
    {
        std::unique_ptr<MegaNode> root{megaApi[apiIndex]->getRootNode()};
        if (!root)
            return "cannot resolve own root node";
        ::mega::WsTfsGroupBalances groups;
        ::mega::Error err(::mega::API_EINTERNAL);
        if (!fetchTfsGroups(*megaApi[apiIndex],
                            {toNodeHandle(root->getHandle())},
                            groups,
                            err,
                            std::chrono::seconds(60)))
            return "tfs command timed out / did not dispatch";
        if (err != ::mega::API_OK)
            return std::string("err=") + std::to_string(static_cast<int>(err));
        if (groups.empty())
            return "tfs returned no groups for own root";
        return "";
    }

    // Fills apiIndex's storage to leave exactly `freeToLeave` free (default
    // kFreeToLeave: GREEN/ORANGE — deliberately NOT near-full, so usl stays off RED
    // and the legacy account-RED machinery cannot pre-block the discriminator
    // upload), cloning the SdkTestUploadsOverquota mechanics (import gzlQ3DIY 1 GB
    // link + doCopyNode + filler upload). `discriminatorSize` only sizes the plan
    // conformance guard. `fillMargin` (default 0: T20 unchanged) leaves that much MORE
    // free by account details, for a caller that lands the exact target by tfs
    // (topUpFillToTfsTarget). Records the created fill-folder handle (set BEFORE the first
    // fallible assert) and any local filler file into the caller's cleanup state.
    // Sets a non-empty skipReason and returns WITHOUT filling if the account
    // cannot reach the controlled state (already full, non-conforming plan, or a
    // fill that would need more than kMaxFillCopies copies). Uses ASSERT_* —
    // invoke via ASSERT_NO_FATAL_FAILURE.
    void fillStorageLeavingInsufficient(unsigned apiIndex,
                                        const std::string& fillFolderName,
                                        ::MegaHandle& fillFolderHandleOut,
                                        std::vector<std::string>& localFilesOut,
                                        std::string& skipReason,
                                        ::m_off_t freeToLeave = kFreeToLeave,
                                        ::m_off_t discriminatorSize = kDiscriminatorSize,
                                        ::m_off_t fillMargin = 0)
    {
        skipReason.clear();
        const ::m_off_t fillTarget = freeToLeave + fillMargin; // free space the fill leaves

        std::unique_ptr<MegaNode> root{megaApi[apiIndex]->getRootNode()};
        ASSERT_TRUE(root) << "cannot resolve own root node";

        // Check headroom BEFORE importing — an import into an already-(near-)OQ
        // account would itself fail with EOVERQUOTA and confuse the fill.
        ASSERT_NO_FATAL_FAILURE(synchronousGetSpecificAccountDetails(apiIndex, true, false, false));
        ASSERT_NE(mApi[apiIndex].accountDetails, nullptr);
        const long long storageMax = mApi[apiIndex].accountDetails->getStorageMax();
        const long long storageUsed = mApi[apiIndex].accountDetails->getStorageUsed();
        ASSERT_GT(storageMax, 0);
        if (storageMax < discriminatorSize + freeToLeave || storageMax > kMaxConformingPlanBytes)
        {
            skipReason = "non-conforming account plan (storageMax=" + std::to_string(storageMax) +
                         ") — controlled fill targets the FREE 25 GiB baseline";
            return;
        }
        // The seed import below consumes ~1 GiB on top of the fill headroom.
        if (storageUsed + fillTarget + (1LL << 30) >= storageMax)
        {
            skipReason =
                "account too full for the controlled fill (used=" + std::to_string(storageUsed) +
                " of " + std::to_string(storageMax) + ")";
            return;
        }

        // Create the fill folder (record handle for cleanup BEFORE any later assert).
        const ::MegaHandle fillHandle = createFolder(apiIndex, fillFolderName.c_str(), root.get());
        fillFolderHandleOut = fillHandle;
        ASSERT_NE(fillHandle, ::mega::UNDEF) << "creating fill folder failed";
        std::unique_ptr<MegaNode> fillNode{megaApi[apiIndex]->getNodeByHandle(fillHandle)};
        ASSERT_TRUE(fillNode);

        // Import the 1 GiB seed.
        const ::MegaHandle importHandle = importPublicLink(apiIndex, seedLinkUrl(), fillNode.get());
        std::unique_ptr<MegaNode> seedNode{megaApi[apiIndex]->getNodeByHandle(importHandle)};
        ASSERT_TRUE(seedNode) << "importing the 1 GiB seed link failed";
        const long long copySize = seedNode->getSize();
        ASSERT_GT(copySize, 0);
        const std::string seedName = seedNode->getName() ? seedNode->getName() : "seed";

        // Copy the seed until just over fillTarget remains free (the filler
        // below tops the state up to exactly fillTarget). The seed import
        // itself already consumed copySize — subtract it, or the fill overshoots
        // by exactly one copy (512 MiB free at 98% instead of 1.5 GiB at 94%;
        // the linux_9684 Jenkins forensics caught the original overshoot).
        const long long remaining = storageMax - storageUsed - copySize;
        const long long copies = std::max<long long>(0, (remaining - fillTarget) / copySize);
        if (copies > kMaxFillCopies)
        {
            skipReason = "controlled fill would need " + std::to_string(copies) + " copies (cap " +
                         std::to_string(kMaxFillCopies) + ") — non-conforming account";
            return;
        }
        for (long long i = 1; i <= copies; ++i)
        {
            const std::string copyName = seedName + std::to_string(i);
            ASSERT_EQ(
                API_OK,
                doCopyNode(apiIndex, nullptr, seedNode.get(), fillNode.get(), copyName.c_str()))
                << "copying fill node failed (i=" << i << ")";
        }

        // Top up with a filler upload so exactly fillTarget bytes remain free.
        const long long remainingAfterCopies = remaining - (copies * copySize);
        if (remainingAfterCopies > fillTarget)
        {
            const long long fillerSize = remainingAfterCopies - fillTarget;
            const std::string fillerName = fillFolderName + "_filler.bin";
            localFilesOut.push_back(fillerName);
            ASSERT_TRUE(createFileWithSize(fillerName, static_cast<size_t>(fillerSize), "F"))
                << "creating filler file failed";
            ASSERT_NO_FATAL_FAILURE(
                uploadFillerProgressAware(apiIndex, fillerName, fillNode.get()));
            deleteFile(fillerName);
        }
    }

    // Uploads a fill file and waits progress-aware (SDK-6470 followup1 latent sibling:
    // the old fixed kReleaseWaitS budget needed >= 1.28 MB/s). The filler always fits
    // the free space, so no quota logic may hold it. Uses ASSERT_* — invoke via
    // ASSERT_NO_FATAL_FAILURE.
    void uploadFillerProgressAware(unsigned apiIndex,
                                   const std::string& localName,
                                   MegaNode* parent)
    {
        ASSERT_TRUE(parent) << "fill parent folder missing";
        OverquotaAppContractRecorder fillRecorder(megaApi[apiIndex].get());
        TransferTracker fillTracker(megaApi[apiIndex].get());
        MegaUploadOptions fillOptions;
        fillOptions.mtime = MegaUploadOptions::INVALID_CUSTOM_MOD_TIME;
        megaApi[apiIndex]->startUpload(localName, parent, nullptr, &fillOptions, &fillTracker);
        ASSERT_TRUE(WaitFor(
            [&]
            {
                return fillTracker.mTag.load() >= 0;
            },
            30000))
            << "filler transfer tag not captured";
        const auto r = fillRecorder.waitForFinishProgressAware(fillTracker.mTag.load(),
                                                               kUploadNoProgressLimit,
                                                               kUploadSafetyCap);
        ASSERT_TRUE(r.finished && r.code == API_OK)
            << "filler upload failed: " << r.reason << "; code=" << r.code
            << " bytes=" << r.lastBytes << "/" << r.total;
    }

    // Cached storage state (MegaClient::ststatus) of apiIndex, read on the client
    // thread (WsQuotaClientThread.h; always compiled, no hooks).
    bool fetchSdkStorageStatus(unsigned apiIndex, int& statusOut)
    {
        return fetchSdkStorageStatusOnClientThread(*megaApi[apiIndex], statusOut);
    }

    // Non-fatal variant for polling: true iff an API_OK reply carried the own root.
    bool tryOwnRootTfsAvailable(unsigned apiIndex, ::m_off_t& availOut)
    {
        availOut = -1;
        std::unique_ptr<MegaNode> root{megaApi[apiIndex]->getRootNode()};
        if (!root)
            return false;
        const ::mega::NodeHandle rootH = toNodeHandle(root->getHandle());
        ::mega::WsTfsGroupBalances groups;
        ::mega::Error err(::mega::API_EINTERNAL);
        if (!fetchTfsGroups(*megaApi[apiIndex], {rootH}, groups, err, std::chrono::seconds(60)) ||
            err != ::mega::API_OK)
            return false;
        for (const auto& g: groups)
            for (const auto& h: g.second)
                if (h == rootH)
                    availOut = g.first;
        return availOut >= 0;
    }

    // Production `tfs` (hook-free) for apiIndex's own root: the balance the predictive
    // ledger sees. Uses ASSERT_* — invoke via ASSERT_NO_FATAL_FAILURE.
    void queryOwnRootTfsAvailable(unsigned apiIndex, ::m_off_t& availOut)
    {
        availOut = -1;
        std::unique_ptr<MegaNode> root{megaApi[apiIndex]->getRootNode()};
        ASSERT_TRUE(root) << "cannot resolve own root node";
        const ::mega::NodeHandle rootH = toNodeHandle(root->getHandle());
        ::mega::WsTfsGroupBalances groups;
        ::mega::Error err(::mega::API_EINTERNAL);
        ASSERT_TRUE(
            fetchTfsGroups(*megaApi[apiIndex], {rootH}, groups, err, std::chrono::seconds(60)))
            << "PREMISE: tfs query for the own root timed out / did not dispatch";
        ASSERT_EQ(err, ::mega::API_OK)
            << "PREMISE: tfs query for the own root failed (err=" << static_cast<int>(err) << ")";
        for (const auto& g: groups)
            for (const auto& h: g.second)
                if (h == rootH)
                    availOut = g.first;
        ASSERT_GE(availOut, 0) << "PREMISE: own root absent from the tfs reply";
    }

    // The QA geometry needs the tfs figure (what the ledger holds on) exactly at
    // `target`, not only the account-details figure the fill is computed from; the fill
    // therefore leaves `target + kQaFillMargin` and this lands the rest by tfs. Both
    // directions are handled: above the target, upload `avail - target` into the fill
    // folder (< avail, so it always fits: the fill can never overflow into RED) and
    // wait until tfs reflects it before any further round (a stale figure would
    // double-count); below the target (drift beyond the margin), fail with PREMISE
    // (nothing is deleted or guessed). Logs the final figure. Uses ASSERT_* — invoke
    // via ASSERT_NO_FATAL_FAILURE.
    void topUpFillToTfsTarget(unsigned apiIndex,
                              ::MegaHandle fillFolderHandle,
                              ::m_off_t target,
                              const std::string& namePrefix,
                              std::vector<std::string>& localFilesOut)
    {
        ::m_off_t avail = -1;
        ASSERT_NO_FATAL_FAILURE(queryOwnRootTfsAvailable(apiIndex, avail));
        for (int round = 1; round <= kMaxTopUpRounds && avail != target; ++round)
        {
            ASSERT_GT(avail, target)
                << "PREMISE: the fill overshot the tfs target: tfs reports " << avail
                << " B free, below the " << target << " B target despite the " << kQaFillMargin
                << " B fill margin (account-details vs tfs drift)";
            const ::m_off_t topUp = avail - target;
            ASSERT_LE(topUp, kMaxTopUpBytes)
                << "PREMISE: tfs reports " << avail << " B free, " << topUp
                << " B above the target: beyond the top-up bound (account-details vs tfs drift)";
            std::unique_ptr<MegaNode> fillNode{
                megaApi[apiIndex]->getNodeByHandle(fillFolderHandle)};
            ASSERT_TRUE(fillNode) << "fill folder vanished before the top-up";
            const std::string name = namePrefix + "_topup" + std::to_string(round) + ".bin";
            localFilesOut.push_back(name);
            LOG_info << "[fill top-up] round " << round << ": tfs reports " << avail
                     << " B free; uploading " << topUp << " B to reach " << target;
            ASSERT_TRUE(createFileWithSize(name, static_cast<size_t>(topUp), "T"))
                << "creating top-up file failed";
            ASSERT_NO_FATAL_FAILURE(uploadFillerProgressAware(apiIndex, name, fillNode.get()));
            deleteFile(name);

            const ::m_off_t before = avail;
            ::m_off_t now = -1;
            ASSERT_TRUE(WaitFor(
                [&]
                {
                    return tryOwnRootTfsAvailable(apiIndex, now) && now < before;
                },
                kTopUpReflectWaitMs))
                << "PREMISE: tfs did not reflect the " << topUp << " B top-up within "
                << kTopUpReflectWaitMs / 1000 << "s (still " << now
                << " B free); refusing another round (double-count risk)";
            avail = now;
        }
        if (avail == target)
            LOG_info << "[fill top-up] tfs reports exactly " << avail << " B free (target)";
        else
            LOG_warn << "[fill top-up] tfs reports " << avail << " B free, not the " << target
                     << " B target, after " << kMaxTopUpRounds << " rounds";
    }

    // Restoration after an own-account overflow: the putnodes rejection (or the usl
    // update after an accepted overflow) leaves the SDK's cached storage state RED,
    // which blocks every later upload on this shared account. After the fill is freed,
    // wait for the natural non-RED EVENT_STORAGE; else refresh the cached state like
    // SdkTestSyncOverquotaReason::TearDown; fail loudly only if it is still RED/PAYWALL.
    void restoreStorageStateAfterOwnFill(unsigned apiIndex,
                                         const OverquotaAppContractRecorder& recorder)
    {
        int status = ::mega::STORAGE_UNKNOWN;
        if (!fetchSdkStorageStatus(apiIndex, status))
            LOG_warn << "[restore] could not read the cached storage state of account " << apiIndex;
        if (!isFullStorageStatus(status))
            return;

        LOG_info << "[restore] cached storage state " << status << " after cleanup; waiting up to "
                 << kRestoreNaturalWait.count() << "s for a natural non-RED EVENT_STORAGE";
        const auto since = OverquotaAppContractRecorder::Clock::now();
        const auto natural = recorder.waitForStorageEvent(
            {::mega::MegaApi::STORAGE_STATE_GREEN, ::mega::MegaApi::STORAGE_STATE_ORANGE},
            kRestoreNaturalWait,
            since);
        for (int refresh = 0; refresh < 2; ++refresh)
        {
            if (fetchSdkStorageStatus(apiIndex, status) && !isFullStorageStatus(status))
                break;
            LOG_warn << "[restore] "
                     << (natural ? "storage still RED/PAYWALL" : "no natural non-RED EVENT_STORAGE")
                     << "; refreshing the cached state via account details (attempt " << refresh + 1
                     << ")";
            (void)synchronousGetSpecificAccountDetails(apiIndex, true, false, false);
            (void)WaitFor(
                [&]
                {
                    return fetchSdkStorageStatus(apiIndex, status) && !isFullStorageStatus(status);
                },
                kRestoreRefreshWaitMs);
        }
        if (fetchSdkStorageStatus(apiIndex, status) && isFullStorageStatus(status))
        {
            LOG_err << "[restore] account " << apiIndex << " storage state is still " << status
                    << " (RED/PAYWALL) after cleanup + refresh: later tests on this shared "
                       "account will be blocked; verify and free the account manually";
            ADD_FAILURE() << "RESTORE: account " << apiIndex
                          << " left in storage RED/PAYWALL after cleanup + account-details refresh "
                             "(shared account; free it manually)";
        }
        else
        {
            LOG_info << "[restore] account " << apiIndex << " storage state back to " << status;
        }
    }

    // Blocks (<= waitSeconds) for the predictive hold to surface, then asserts the
    // discriminator. Uses ASSERT_* — invoke via ASSERT_NO_FATAL_FAILURE.
    void assertPredictiveHoldDiscriminator(const WsQuotaHoldTracker& holdTracker,
                                           const OverquotaProgressLatch& latch,
                                           const TransferTracker& tracker,
                                           int tag,
                                           bool expectForeign,
                                           int waitSeconds)
    {
        // cv-wait for the first temporary error on this tag (fires ~instantly for a
        // predictive hold; only after all bytes + putnodes for a pre-P3 legacy OQ).
        holdTracker.waitForTemporaryError(tag, std::chrono::seconds(waitSeconds));

        ASSERT_TRUE(latch.sawOverquotaBeforeAllBytes(tag))
            << (expectForeign ? "no predictive foreign hold observed before upload sent all bytes" :
                                "no predictive hold observed before upload sent all bytes");

        const auto seq = holdTracker.sequence(tag);
        ASSERT_FALSE(seq.empty()) << "hold tracker recorded no events for the held upload";
        bool sawOverquota = false;
        bool foreignSeen = false;
        for (const auto& e: seq)
        {
            if (e.kind == WsQuotaHoldTracker::Event::Kind::TemporaryError &&
                e.code == API_EOVERQUOTA)
            {
                sawOverquota = true;
                foreignSeen = e.foreignOverquota;
                break;
            }
        }
        ASSERT_TRUE(sawOverquota) << "no EOVERQUOTA temporary error recorded for the held upload";
        ASSERT_EQ(foreignSeen, expectForeign)
            << "unexpected isForeignOverquota() on the predictive hold";
        ASSERT_FALSE(tracker.finished.load())
            << "held upload must not terminally finish before quota is released";
    }

    // One own-account shortfall cell (RealFillOwnAccount*IsNotHeld).
    struct OwnShortfallCell
    {
        const char* label; // file/folder name prefix + log tag
        ::m_off_t freeToLeave; // free space the fill leaves
        ::m_off_t discriminatorSize; // upload size (> freeToLeave)
        int throttleBps; // 0 = unthrottled
        bool topUpToTfsTarget; // fill to freeToLeave + kQaFillMargin, then land it by tfs
    };

    // Shared body of the own-account shortfall cells (defined below). Sets a non-empty
    // skipReason instead of skipping (the caller GTEST_SKIPs). Uses ASSERT_* — invoke
    // via ASSERT_NO_FATAL_FAILURE.
    void runOwnShortfallIsNotHeld(const OwnShortfallCell& cell, std::string& skipReason);

    // Non-fatal shared-account hygiene check: warn if apiIndex's storage is still
    // above 50% of max after cleanup (best-effort; server usl update may lag).
    void warnIfStorageStillHigh(unsigned apiIndex)
    {
        if (synchronousGetSpecificAccountDetails(apiIndex, true, false, false) != API_OK)
            return;
        if (!mApi[apiIndex].accountDetails)
            return;
        const long long storageMax = mApi[apiIndex].accountDetails->getStorageMax();
        const long long storageUsed = mApi[apiIndex].accountDetails->getStorageUsed();
        if (storageMax > 0 && storageUsed * 2 > storageMax)
        {
            LOG_warn << "SdkWsQuotaRealTest: shared account " << apiIndex
                     << " storageUsed=" << storageUsed
                     << " still > 50% of storageMax=" << storageMax
                     << " after cleanup (server usl may lag; verify the account is not left full)";
        }
    }
};

// ============================================================================
// T18 ProbeTfsCommandOwnRoot — Phase-0/P1 deployment probe. Hook-free. <30s.
// PASSES today: the production tfs command + parser are committed and correct.
// ============================================================================
TEST_F(SdkWsQuotaRealTest, ProbeTfsCommandOwnRoot)
{
    LOG_info << "___TEST ProbeTfsCommandOwnRoot___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode) << "cannot resolve own root node";
    const ::mega::NodeHandle rootH = toNodeHandle(rootnode->getHandle());

    ::mega::WsTfsGroupBalances groups;
    ::mega::Error err(::mega::API_EINTERNAL);
    const bool got = fetchTfsGroups(*megaApi[0], {rootH}, groups, err, std::chrono::seconds(60));

    // SKIP guard (per plan): timeout or API_EARGS/ENOENT/EACCESS/EINTERNAL (or any
    // non-OK reply) => the command/deployment is unavailable in this environment.
    if (!got)
        GTEST_SKIP() << "tfs not available in this environment (timeout / no dispatch)";
    if (err != ::mega::API_OK)
        GTEST_SKIP() << "tfs not available in this environment (err=" << static_cast<int>(err)
                     << ")";

    // Reply parsed into >= 1 group.
    ASSERT_FALSE(groups.empty()) << "tfs returned zero groups for own root";

    // Own root present in exactly one group; capture that group's balance.
    int groupsWithRoot = 0;
    ::m_off_t rootAvail = -1;
    for (const auto& g: groups)
        for (const auto& h: g.second)
            if (h.as8byte() == rootH.as8byte())
            {
                ++groupsWithRoot;
                rootAvail = g.first;
            }
    ASSERT_EQ(groupsWithRoot, 1) << "own root must appear in exactly one tfs group";

    // availableBytes >= 0.
    ASSERT_GE(rootAvail, 0) << "own-root available bytes must be non-negative";

    // Consistent within slack with getStorageMax - getStorageUsed. Slack = max(10%
    // of free space, 1 GiB): concurrent test traffic moves usage on this shared
    // account, so a tight equality would flake.
    ASSERT_NO_FATAL_FAILURE(synchronousGetSpecificAccountDetails(0, true, false, false));
    ASSERT_NE(mApi[0].accountDetails, nullptr);
    const long long storageMax = mApi[0].accountDetails->getStorageMax();
    const long long storageUsed = mApi[0].accountDetails->getStorageUsed();
    ASSERT_GT(storageMax, 0);
    const long long freeSpace = storageMax - storageUsed;
    const long long slack = std::max<long long>(freeSpace / 10, 1LL << 30);
    const long long diff = rootAvail > freeSpace ? static_cast<long long>(rootAvail) - freeSpace :
                                                   freeSpace - static_cast<long long>(rootAvail);
    ASSERT_LE(diff, slack) << "tfs availableBytes (" << rootAvail
                           << ") inconsistent with storageMax-storageUsed (" << freeSpace
                           << "), slack=" << slack;
}

// ============================================================================
// SDK-6610 (v10.21.0 RC2) own-account shortfall cells — real end-to-end, OWN pool.
// Hook-free (run in hooks-OFF builds). Replace T19 RealFillOwnAccountHoldAndRelease:
// an own-account `tfs` shortfall must NOT be predictively held any more; the upload
// streams and the server plus the legacy account-RED path decide, so the app sees
// EVENT_STORAGE RED ("Storage full") instead of a silent value-0 EOVERQUOTA.
//   A1 no temp EOVERQUOTA while transferred < total without a RED/PAYWALL storage
//      context (the buggy tree holds at ~0 bytes and never emits EVENT_STORAGE); one
//      WITH RED/PAYWALL is a legacy server-side rejection, logged as [A1-server]
//   A2 progress-aware wait (90 s without activity fails; 1200 s cap) for an
//      app-actionable terminal state: onTransferFinish API_OK or API_EOVERQUOTA, or
//      the legacy RETRYING with EVENT_STORAGE RED/PAYWALL (at any byte count)
//   A3 a natural EVENT_STORAGE RED/PAYWALL within 180 s of that terminal state
//   A4 no temp error MEGAsync 6.7.0 cannot act on (OverquotaAppContractRecorder rule)
// Outcome checks are EXPECTs (every signature is reported); premises are ASSERTs.
// ============================================================================
void SdkWsQuotaRealTest::runOwnShortfallIsNotHeld(const OwnShortfallCell& cell,
                                                  std::string& skipReason)
{
    using Clock = OverquotaAppContractRecorder::Clock;
    skipReason.clear();

    // Guard 1: tfs availability (probe-style SKIP).
    {
        const std::string reason = tfsProbeSkipReason(0);
        if (!reason.empty())
        {
            skipReason = "tfs not available in this environment (" + reason + ")";
            return;
        }
    }

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    // Installed before the fill (fill-time EVENT_STORAGE is on record) and declared
    // before the cleanup so the restoration below can wait on it.
    OverquotaAppContractRecorder recorder(megaApi[0].get());

    // ---- Cleanup state + RAII teardown (runs on EVERY exit path) ----
    ::MegaHandle fillFolderHandle = ::mega::UNDEF;
    ::MegaHandle uploadNodeHandle = ::mega::UNDEF;
    std::string uploadName;
    std::vector<std::string> localFiles;
    std::optional<ScopedUploadSpeedLimit> throttle;
    auto cleanup = makeScopedDestructor(
        [&]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
            throttle.reset();
            // Remove the discriminator node (by handle, else by name at root).
            std::unique_ptr<MegaNode> root{megaApi[0]->getRootNode()};
            if (uploadNodeHandle != ::mega::UNDEF)
                if (std::unique_ptr<MegaNode> n{megaApi[0]->getNodeByHandle(uploadNodeHandle)})
                    (void)synchronousRemove(0, n.get());
            if (root && !uploadName.empty())
                if (std::unique_ptr<MegaNode> n{
                        megaApi[0]->getNodeByPathOfType(uploadName.c_str(),
                                                        root.get(),
                                                        MegaNode::TYPE_FILE)})
                    (void)synchronousRemove(0, n.get());
            // Remove the fill folder (drops the imported seed + copies + fillers). A
            // failure leaves the shared account (near-)full for every later test: loud.
            if (fillFolderHandle != ::mega::UNDEF)
                if (std::unique_ptr<MegaNode> f{megaApi[0]->getNodeByHandle(fillFolderHandle)})
                    if (const int r = synchronousRemove(0, f.get()); r != API_OK)
                    {
                        LOG_err << "[restore] removing the fill folder failed (err=" << r
                                << "): the shared account stays (near-)full; free it manually";
                        ADD_FAILURE() << "RESTORE: fill folder removal failed (err=" << r
                                      << "); the shared account stays (near-)full";
                    }
            for (const auto& nm: localFiles)
                deleteFile(nm);
            // Leave no cached RED behind for later tests on this shared account.
            restoreStorageStateAfterOwnFill(0, recorder);
            warnIfStorageStillHigh(0);
        });

    // Guard 2 + fill: SKIP on any non-conforming account state, else fill to leave
    // exactly cell.freeToLeave free.
    const std::string fillName = makeBinName((std::string(cell.label) + "_fill_").c_str());
    ASSERT_NO_FATAL_FAILURE(
        fillStorageLeavingInsufficient(0,
                                       fillName,
                                       fillFolderHandle,
                                       localFiles,
                                       skipReason,
                                       cell.freeToLeave,
                                       cell.discriminatorSize,
                                       cell.topUpToTfsTarget ? kQaFillMargin : 0));
    if (!skipReason.empty())
        return;
    if (cell.topUpToTfsTarget)
    {
        ASSERT_NO_FATAL_FAILURE(
            topUpFillToTfsTarget(0, fillFolderHandle, cell.freeToLeave, fillName, localFiles));
    }

    // PREMISE A: the account really is short — the production tfs (what the
    // predictive ledger holds on) reports less than the discriminator for the own root.
    ::m_off_t ownAvail = -1;
    ASSERT_NO_FATAL_FAILURE(queryOwnRootTfsAvailable(0, ownAvail));
    LOG_info << "[" << cell.label << "] premise: own-root tfs available=" << ownAvail
             << " B (target " << cell.freeToLeave << "), discriminator=" << cell.discriminatorSize
             << " B";
    ASSERT_LT(ownAvail, cell.discriminatorSize)
        << "PREMISE: the account is not short: tfs reports " << ownAvail
        << " B available for the own root, the discriminator is " << cell.discriminatorSize << " B";
    // PREMISE B: GREEN/ORANGE, as QA's account (RED/PAYWALL is the legacy path).
    {
        int status = ::mega::STORAGE_UNKNOWN;
        ASSERT_TRUE(fetchSdkStorageStatus(0, status))
            << "PREMISE: could not read the cached storage state";
        const auto lastEvent = recorder.lastStorageEventNumber();
        LOG_info << "[" << cell.label << "] premise: cached storage state=" << status
                 << " last EVENT_STORAGE=" << (lastEvent ? std::to_string(*lastEvent) : "none");
        ASSERT_FALSE(isFullStorageStatus(status))
            << "PREMISE: the SDK already reports storage RED/PAYWALL (" << status
            << ") before the discriminator upload; QA's account was ORANGE";
    }

    // ---- discriminator upload to the own root: larger than the free space ----
    uploadName = makeBinName((std::string(cell.label) + "_upload_").c_str());
    localFiles.push_back(uploadName);
    ASSERT_TRUE(createFileWithSize(uploadName, static_cast<size_t>(cell.discriminatorSize), "U"))
        << "creating the discriminator upload file failed";
    if (cell.throttleBps > 0)
        throttle.emplace(*megaApi[0], cell.throttleBps);

    TransferTracker tracker(megaApi[0].get());
    MegaUploadOptions uploadOptions;
    uploadOptions.mtime = MegaUploadOptions::INVALID_CUSTOM_MOD_TIME;
    const auto t0 = Clock::now();
    megaApi[0]->startUpload(uploadName, rootnode.get(), nullptr, &uploadOptions, &tracker);
    ASSERT_TRUE(WaitFor(
        [&]
        {
            return tracker.mTag.load() >= 0;
        },
        30000))
        << "transfer tag not captured";
    const int tag = tracker.mTag.load();
    const std::set<int> fullStates{::mega::MegaApi::STORAGE_STATE_RED,
                                   ::mega::MegaApi::STORAGE_STATE_PAYWALL};

    // PREMISE C (audit M1): the LEDGER holds the own-account shortfall while the upload
    // runs. Public queue-fit (observational, unchanged by the fix: holds on both arms):
    // STATE_SHORTFALL, not foreign, > 0 bytes; read first, then "still unfinished with
    // bytes < total" proves the ledger's enqueue tfs landed while the upload ran.
    {
        const auto fit = waitForOwnQueueShortfall(*megaApi[0], 0, kQueueFitPremiseTimeout);
        LOG_info << "[" << cell.label << "] premise: queue-fit state=" << fit.state
                 << " shortfall=" << fit.shortfallBytes << " B foreign=" << fit.foreign;
        ASSERT_EQ(fit.state, MegaWsUploadQuotaFit::STATE_SHORTFALL)
            << "PREMISE: the ledger reports no queue shortfall while the upload runs (queue-fit "
               "state="
            << fit.state << ")";
        ASSERT_FALSE(fit.foreign) << "PREMISE: the queue-fit shortfall is reported as foreign";
        ASSERT_GT(fit.shortfallBytes, 0) << "PREMISE: queue-fit reports a zero shortfall";
        std::unique_ptr<MegaTransfer> t{megaApi[0]->getTransferByTag(tag)};
        ASSERT_TRUE(t && !tracker.finished.load())
            << "PREMISE: upload finished before the ledger shortfall was observed (vacuous run)";
        ASSERT_LT(t->getTransferredBytes(), t->getTotalBytes())
            << "PREMISE: all bytes were sent before the ledger shortfall was observed";
    }

    // A2 wait. The legacy path may park the upload RETRYING with EVENT_STORAGE
    // RED/PAYWALL instead of finishing it (after all bytes at putnodes, or mid-stream
    // when the upload server answers -17): stop waiting as soon as that is visible.
    bool retryingWithRed = false;
    long long retryingBytes = 0;
    long long retryingTotal = 0;
    auto legacyRetryingWithRed = [&]()
    {
        std::unique_ptr<MegaTransfer> t{megaApi[0]->getTransferByTag(tag)};
        if (!t || t->getState() != MegaTransfer::STATE_RETRYING ||
            !recorder.storageEventSince(fullStates, t0))
            return false;
        retryingWithRed = true;
        retryingBytes = t->getTransferredBytes();
        retryingTotal = t->getTotalBytes();
        return true;
    };
    const auto a2 = recorder.waitForFinishProgressAware(tag,
                                                        kUploadNoProgressLimit,
                                                        kUploadSafetyCap,
                                                        legacyRetryingWithRed);
    const bool finishedActionably = a2.finished && (a2.code == API_OK || a2.code == API_EOVERQUOTA);
    // Audit M4: RETRYING with RED/PAYWALL is app-actionable at ANY byte count (the
    // "Storage full" dialog is up); only the log line tells the two apart.
    const bool legacyTerminal = !a2.finished && retryingWithRed;
    const bool legacyAfterAllBytes =
        legacyTerminal && retryingTotal > 0 && retryingBytes >= retryingTotal;
    const bool terminal = finishedActionably || legacyTerminal;
    Clock::time_point terminalTs = Clock::now();
    if (const auto fin = recorder.finishOf(tag))
        terminalTs = fin->ts;
    const auto sinceStartMs = [&](Clock::time_point t)
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(t - t0).count();
    };

    if (a2.finished)
        LOG_info << "[" << cell.label
                 << "] server verdict: terminal state = onTransferFinish code=" << a2.code
                 << " bytes=" << a2.lastBytes << "/" << a2.total << " at +"
                 << sinceStartMs(terminalTs) << "ms";
    else if (legacyTerminal)
        LOG_info << "[" << cell.label << "] server verdict: terminal state = legacy RETRYING "
                 << "with EVENT_STORAGE RED/PAYWALL "
                 << (legacyAfterAllBytes ? "after all bytes (putnodes rejection)" :
                                           "BEFORE all bytes (server-side mid-stream rejection)")
                 << ", bytes=" << retryingBytes << "/" << retryingTotal << " at +"
                 << sinceStartMs(terminalTs) << "ms";
    else
        LOG_info << "[" << cell.label << "] server verdict: no terminal state: " << a2.reason;

    // A1: no EOVERQUOTA before all bytes WITHOUT a RED/PAYWALL storage context (the
    // predictive hold never emits EVENT_STORAGE). Audit M4: one with a RED/PAYWALL
    // EVENT_STORAGE within ±5 s, or while the app state is RED/PAYWALL, is a legacy
    // server-side rejection the app can act on: logged as [A1-server], not a failure.
    {
        std::vector<OverquotaAppContractRecorder::TempError> predictive;
        for (const auto& te: recorder.overquotaTempErrorsBeforeAllBytes(tag))
        {
            if (recorder.isStorageContextActionable(te.ts))
                LOG_info << "[A1-server] [" << cell.label << "] EOVERQUOTA before all bytes "
                         << "with EVENT_STORAGE RED/PAYWALL (legacy server-side rejection, "
                         << "app-actionable): bytes=" << te.transferred << "/" << te.total
                         << " value=" << te.value << " at +" << sinceStartMs(te.ts) << "ms";
            else
                predictive.push_back(te);
        }
        std::string detail;
        if (!predictive.empty())
        {
            const auto& early = predictive.front();
            detail = "bytes=" + std::to_string(early.transferred) + "/" +
                     std::to_string(early.total) + " value=" + std::to_string(early.value) +
                     " foreign=" + std::to_string(early.foreign ? 1 : 0) +
                     " RED/PAYWALL EVENT_STORAGE within 5s=" +
                     std::to_string(recorder.fullStorageEventNear(early.ts) ? 1 : 0) +
                     " count=" + std::to_string(predictive.size());
        }
        EXPECT_TRUE(predictive.empty())
            << "[A1] EOVERQUOTA temporary error before all bytes were sent (own-account "
               "predictive hold, the QA MEGAsync 6.7.0 hang): "
            << detail << " " << recorder.describe(tag);
    }

    // A2: an app-actionable terminal state.
    EXPECT_TRUE(terminal) << "[A2] no app-actionable terminal state for the own-account "
                             "shortfall upload: "
                          << a2.reason << "; finished=" << a2.finished << " code=" << a2.code
                          << " bytes=" << a2.lastBytes << "/" << a2.total
                          << " retryingWithRed=" << retryingWithRed << " "
                          << recorder.describe(tag);

    // A3: the natural EVENT_STORAGE RED/PAYWALL (no forced uq before this point).
    if (terminal)
    {
        const auto deadline = terminalTs + kRedEventWindow;
        const auto now = Clock::now();
        const auto remaining =
            deadline > now ? std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now) :
                             std::chrono::milliseconds(0);
        const auto red = recorder.waitForStorageEvent(fullStates, remaining, t0);
        if (red)
            LOG_info << "[" << cell.label << "] EVENT_STORAGE " << red->number << " at +"
                     << sinceStartMs(red->ts) << "ms (latency vs terminal state "
                     << std::chrono::duration_cast<std::chrono::milliseconds>(red->ts - terminalTs)
                            .count()
                     << "ms)";
        EXPECT_TRUE(red.has_value())
            << "[A3] no natural EVENT_STORAGE RED/PAYWALL within " << kRedEventWindow.count()
            << "s of the terminal state (finish code=" << a2.code << "): the app never learns the "
            << "account is full " << recorder.describe(tag);
    }
    else
    {
        LOG_info << "[" << cell.label << "] A3 not evaluated: no terminal state (see A2)";
    }

    // A4: nothing MEGAsync 6.7.0 cannot act on.
    const auto unactionable = recorder.unactionableOwnOverquota(tag);
    EXPECT_TRUE(unactionable.empty())
        << "[A4] apps cannot act on EOVERQUOTA value 0, non-foreign, without EVENT_STORAGE "
           "RED/PAYWALL (MEGAsync 6.7.0 shows nothing): unactionable="
        << unactionable.size() << " " << recorder.describe(tag);

    // Read the node handle only after the tracker's future (no race with the SDK thread).
    if (a2.finished && a2.code == API_OK && tracker.waitForResult(30) == API_OK)
        uploadNodeHandle = tracker.resultNodeHandle;
}

// ============================================================================
// RealFillOwnAccountShortfallIsNotHeld — T19 geometry (1.5 GiB free, 2.25 GiB upload).
// ============================================================================
TEST_F(SdkWsQuotaRealTest, RealFillOwnAccountShortfallIsNotHeld)
{
    LOG_info << "___TEST RealFillOwnAccountShortfallIsNotHeld___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::string skipReason;
    ASSERT_NO_FATAL_FAILURE(runOwnShortfallIsNotHeld(OwnShortfallCell{"ws_quota_own_big",
                                                                      kFreeToLeave,
                                                                      kDiscriminatorSize,
                                                                      /*throttleBps*/ 0,
                                                                      /*topUpToTfsTarget*/ false},
                                                     skipReason));
    if (!skipReason.empty())
        GTEST_SKIP() << skipReason;
}

// ============================================================================
// RealFillOwnAccountQaGeometryIsNotHeld — the exact QA reproduction: 8388608 B free
// (by tfs), a 10 MiB upload to the own root (throttled, see kQaThrottleBps).
// ============================================================================
TEST_F(SdkWsQuotaRealTest, RealFillOwnAccountQaGeometryIsNotHeld)
{
    LOG_info << "___TEST RealFillOwnAccountQaGeometryIsNotHeld___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::string skipReason;
    ASSERT_NO_FATAL_FAILURE(runOwnShortfallIsNotHeld(OwnShortfallCell{"ws_quota_own_qa",
                                                                      kQaFreeToLeave,
                                                                      kQaDiscriminatorSize,
                                                                      kQaThrottleBps,
                                                                      /*topUpToTfsTarget*/ true},
                                                     skipReason));
    if (!skipReason.empty())
        GTEST_SKIP() << skipReason;
}

// ============================================================================
// T20 RealFillInshareForeignHold — real end-to-end, FOREIGN pool. 2 accounts.
// A = megaApi[0] (uploader); B = megaApi[1] (owner who fills + shares). Hook-free.
// Runs on the MR surface (user ruling 2026-07-30). Pins the foreign-flagged
// predictive discriminator; cleans up BOTH accounts. Release phase stays a SOFT
// assert: B's deletion happens OUTSIDE A's visible share, so no `d` packet
// reaches A and no foreign re-poll exists (user ruling) — the deletion-driven
// trigger deliberately does not cover this leg.
// ============================================================================
TEST_F(SdkWsQuotaRealTest, RealFillInshareForeignHold)
{
    LOG_info << "___TEST RealFillInshareForeignHold___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    // Guard 1: tfs availability for A (whose engine evaluates the foreign hold).
    {
        const std::string reason = tfsProbeSkipReason(0);
        if (!reason.empty())
            GTEST_SKIP() << "tfs not available in this environment (" << reason << ")";
    }

    // ---- Cleanup state + RAII teardown (BOTH accounts; every exit path) ----
    ::MegaHandle bFillFolderHandle = ::mega::UNDEF;
    ::MegaHandle bShareFolderHandle = ::mega::UNDEF;
    ::MegaHandle aControlNodeHandle = ::mega::UNDEF;
    std::string aControlName;
    std::vector<std::string> localFiles;
    bool createdContact = false;
    auto cleanup = makeScopedDestructor(
        [&]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
            // A: remove its own-root control upload (by handle, else by name).
            if (std::unique_ptr<MegaNode> aRoot{megaApi[0]->getRootNode()})
            {
                if (aControlNodeHandle != ::mega::UNDEF)
                    if (std::unique_ptr<MegaNode> n{
                            megaApi[0]->getNodeByHandle(aControlNodeHandle)})
                        (void)synchronousRemove(0, n.get());
                if (!aControlName.empty())
                    if (std::unique_ptr<MegaNode> n{
                            megaApi[0]->getNodeByPathOfType(aControlName.c_str(),
                                                            aRoot.get(),
                                                            MegaNode::TYPE_FILE)})
                        (void)synchronousRemove(0, n.get());
            }
            // B: remove the shared folder (drops A's foreign upload inside it +
            // implicitly unshares) then the fill folder (frees B).
            if (bShareFolderHandle != ::mega::UNDEF)
                if (std::unique_ptr<MegaNode> n{megaApi[1]->getNodeByHandle(bShareFolderHandle)})
                    (void)synchronousRemove(1, n.get());
            if (bFillFolderHandle != ::mega::UNDEF)
                if (std::unique_ptr<MegaNode> n{megaApi[1]->getNodeByHandle(bFillFolderHandle)})
                    (void)synchronousRemove(1, n.get());
            // Remove the A<->B contact relationship ONLY if this test created it.
            if (createdContact)
                if (std::unique_ptr<MegaUser> u{megaApi[0]->getContact(mApi[1].email.c_str())})
                    (void)synchronousRemoveContact(0, u.get());
            for (const auto& nm: localFiles)
                deleteFile(nm);
            warnIfStorageStillHigh(1);
        });

    // ---- B creates the (empty) share folder BEFORE filling (avoid putnodes-OQ) ----
    std::unique_ptr<MegaNode> bRoot{megaApi[1]->getRootNode()};
    ASSERT_TRUE(bRoot);
    const std::string shareFolderName = makeBinName("ws_quota_t20_share_");
    const ::MegaHandle shareHandle = createFolder(1, shareFolderName.c_str(), bRoot.get());
    bShareFolderHandle = shareHandle;
    ASSERT_NE(shareHandle, ::mega::UNDEF) << "creating B's share folder failed";

    // ---- Guard 2 + B fill: SKIP on non-conforming B state, else fill B to
    // leave exactly kFreeToLeave free ----
    std::string fillSkipReason;
    ASSERT_NO_FATAL_FAILURE(fillStorageLeavingInsufficient(1,
                                                           makeBinName("ws_quota_t20_fill_"),
                                                           bFillFolderHandle,
                                                           localFiles,
                                                           fillSkipReason));
    if (!fillSkipReason.empty())
        GTEST_SKIP() << "account B: " << fillSkipReason;

    // ---- Ensure A<->B contact (tolerant of pre-existing state; B invites A) ----
    std::unique_ptr<MegaUser> bContactOfA{megaApi[0]->getContact(mApi[1].email.c_str())};
    const bool alreadyContacts =
        bContactOfA && bContactOfA->getVisibility() == MegaUser::VISIBILITY_VISIBLE;
    if (!alreadyContacts)
    {
        const std::string msg = "SDK-6298 T20 inshare foreign quota";
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
        createdContact = true; // cleanup removes it
    }

    // ---- B shares the folder FULL with A; A waits for the decrypted inshare ----
    std::unique_ptr<MegaNode> bShareFolder{megaApi[1]->getNodeByHandle(shareHandle)};
    ASSERT_TRUE(bShareFolder);
    ASSERT_NO_FATAL_FAILURE(
        shareFolder(bShareFolder.get(), mApi[0].email.c_str(), MegaShare::ACCESS_FULL, 1));
    ASSERT_TRUE(WaitFor(
        [&]()
        {
            std::unique_ptr<MegaNode> n{megaApi[0]->getNodeByHandle(shareHandle)};
            return n && n->isNodeKeyDecrypted();
        },
        60000))
        << "inshare never became visible/decrypted to A";
    std::unique_ptr<MegaNode> inshareNode{megaApi[0]->getNodeByHandle(shareHandle)};
    ASSERT_TRUE(inshareNode) << "A cannot resolve the inshare folder node";

    std::unique_ptr<MegaNode> aRoot{megaApi[0]->getRootNode()};
    ASSERT_TRUE(aRoot);

    // ---- MULTI-OWNER reply-shape probe (closes the "multi-owner tfs reply shape
    // never captured" carryover; previously pinned only behaviourally). A single
    // `tfs` for A's OWN root + B's inshare root crosses two owner pools. The SDK
    // is grouping-independent by design (DECISION-P1-POOLKEY classifies pools
    // client-side), so this asserts only the SEMANTICS we may legitimately
    // require — both handles answered, and B's (full) balance far below A's —
    // and LOGS the grouping shape for the record rather than pinning a server
    // behaviour we do not control.
    {
        const ::mega::NodeHandle aRootH = toNodeHandle(aRoot->getHandle());
        const ::mega::NodeHandle inshareH = toNodeHandle(shareHandle);
        ::mega::WsTfsGroupBalances groups;
        ::mega::Error tfsErr{::mega::API_OK};
        ASSERT_TRUE(fetchTfsGroups(*megaApi[0],
                                   {aRootH, inshareH},
                                   groups,
                                   tfsErr,
                                   std::chrono::seconds{60}))
            << "multi-owner tfs probe did not receive a reply";
        ASSERT_EQ(tfsErr, ::mega::API_OK) << "multi-owner tfs probe returned an error";

        ::m_off_t availOwn = -1;
        ::m_off_t availForeign = -1;
        std::string shape;
        for (const auto& [bytes, handles]: groups)
        {
            shape += "[" + std::to_string(bytes) + ":";
            for (const auto& h: handles)
            {
                shape += " " + std::to_string(h.as8byte());
                if (h == aRootH)
                    availOwn = bytes;
                if (h == inshareH)
                    availForeign = bytes;
            }
            shape += "]";
        }
        LOG_info << "[T20 multi-owner tfs shape] groups=" << groups.size() << " " << shape;
        ASSERT_GE(availOwn, 0) << "own root absent from the multi-owner tfs reply";
        ASSERT_GE(availForeign, 0) << "inshare root absent from the multi-owner tfs reply";
        ASSERT_LT(availForeign, availOwn)
            << "B is filled to kFreeToLeave, so the foreign pool's balance must be well below "
               "A's own pool (foreign="
            << availForeign << " own=" << availOwn << ")";
        ASSERT_LT(availForeign, static_cast<::m_off_t>(kDiscriminatorSize))
            << "foreign balance must be smaller than the discriminator upload";
    }

    // ---- A uploads the discriminator file into the inshare (larger than B's
    // remaining space; A's own storage untouched) ----
    const std::string foreignName = makeBinName("ws_quota_t20_foreign_");
    localFiles.push_back(foreignName);
    ASSERT_TRUE(createFileWithSize(foreignName, static_cast<size_t>(kDiscriminatorSize), "G"))
        << "creating the discriminator foreign upload file failed";

    WsQuotaHoldTracker holdTracker(megaApi[0].get());
    OverquotaProgressLatch latch(megaApi[0].get());
    TransferTracker foreignTracker(megaApi[0].get());
    MegaUploadOptions foreignOptions;
    foreignOptions.mtime = MegaUploadOptions::INVALID_CUSTOM_MOD_TIME;
    megaApi[0]->startUpload(foreignName,
                            inshareNode.get(),
                            nullptr,
                            &foreignOptions,
                            &foreignTracker);
    ASSERT_TRUE(WaitFor(
        [&]
        {
            return foreignTracker.mTag.load() >= 0;
        },
        30000))
        << "foreign transfer tag not captured";
    const int foreignTag = foreignTracker.mTag.load();

    // DISCRIMINATOR (fail-first anchor pre-P3; PASSES post-P3): predictive
    // FOREIGN hold before all bytes.
    ASSERT_NO_FATAL_FAILURE(assertPredictiveHoldDiscriminator(holdTracker,
                                                              latch,
                                                              foreignTracker,
                                                              foreignTag,
                                                              /*expectForeign*/ true,
                                                              kDiscriminatorWaitS));

    // ---- Control: A uploads 16 MiB to its OWN root → completes OK (unaffected) ----
    aControlName = makeBinName("ws_quota_t20_control_");
    localFiles.push_back(aControlName);
    ASSERT_TRUE(createFileWithSize(aControlName, static_cast<size_t>(kUploadSize), "C"))
        << "creating the 16 MiB control upload file failed";
    TransferTracker controlTracker(megaApi[0].get());
    MegaUploadOptions controlOptions;
    controlOptions.mtime = MegaUploadOptions::INVALID_CUSTOM_MOD_TIME;
    megaApi[0]->startUpload(aControlName, aRoot.get(), nullptr, &controlOptions, &controlTracker);
    ASSERT_EQ(controlTracker.waitForResult(kReleaseWaitS), API_OK)
        << "A's own-root control upload should be unaffected by the foreign hold";
    aControlNodeHandle = controlTracker.resultNodeHandle;

    // ---- SOFT release: B frees → bounded wait for A's held upload; tolerate ----
    if (std::unique_ptr<MegaNode> bFill{megaApi[1]->getNodeByHandle(bFillFolderHandle)})
    {
        ASSERT_EQ(API_OK, synchronousRemove(1, bFill.get())) << "freeing B's fill space failed";
        bFillFolderHandle = ::mega::UNDEF; // freed; cleanup must not re-remove
    }
    const ErrorCodes foreignResult =
        foreignTracker.waitForResult(kForeignReleaseWaitS, /*unregisterListenerOnTimeout*/ false);
    if (foreignResult == API_OK)
    {
        LOG_info << "T20: A's foreign upload completed after B freed space "
                 << "(release rode a natural quota trigger)";
    }
    else
    {
        // Documented tolerance: per the SDK-6298 user ruling there is NO foreign
        // re-poll — release rides A's next natural quota trigger — so a
        // non-completion here is NOT a cell failure.
        LOG_warn << "T20 TOLERANCE: A's foreign upload did not complete within "
                 << kForeignReleaseWaitS << "s after B freed space (result=" << foreignResult
                 << "). No foreign re-poll by design; not failing the cell on this alone.";
    }
    // Cancel A's foreign upload for cleanup regardless of the soft outcome.
    if (foreignTracker.mTag.load() >= 0)
        megaApi[0]->cancelTransferByTag(foreignTracker.mTag.load());
}

} // namespace mega::test::wsupload
