/**
 * @file SdkWsQuotaRealTest.cpp
 * @brief SDK-6298 WS upload-quota REAL-PATH integration cells (T18, T19, T20).
 *
 * Unlike the hook-driven SdkWsUploadTest.Quota* cells (SdkWsQuotaTest.cpp), these
 * exercise the predictive WS upload-quota path end-to-end against the LIVE server:
 *  - T18 ProbeTfsCommandOwnRoot  — cheap deployment probe of the committed
 *    production `tfs` command + parser (PASSES today; the command/parser are
 *    committed and validated by unit cells + a live raw probe).
 *  - T19 RealFillOwnAccountHoldAndRelease — fills the OWN account to < 16 MiB free
 *    then asserts a predictive own-pool hold (temp EOVERQUOTA, non-foreign, BEFORE
 *    all bytes are sent) and release-on-free.
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
 * PRE-IMPLEMENTATION fail-first discipline: the predictive ledger (P3) does not
 * exist yet, so pre-P3 the real overquota surfaces only at putnodes AFTER all
 * bytes have been uploaded to storage. T18 PASSES today; T19/T20 are written to
 * their POST-implementation contract but land their FIRST behavioural failure on
 * the discriminator "no predictive hold observed before upload sent all bytes"
 * (foreign variant for T20), then run their mandatory cleanup.
 *
 * SHARED-ACCOUNT SAFETY: T19/T20 fill a shared test account. Every created cloud
 * resource is torn down through a body-scope RAII ScopedDestructor that runs on
 * EVERY exit path (pass / ASSERT-fail / GTEST_SKIP-after-import), mirroring
 * SdkTest.SdkTestUploadsOverquota. A leftover full account would break every
 * subsequent bench/test on this shared account.
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

constexpr ::m_off_t kUploadSize = 16LL * 1024 * 1024; // 16 MiB predictive-hold probe upload
constexpr int kDiscriminatorWaitS = 180; // upper bound: 16 MiB upload + putnodes (pre-P3 legacy)
constexpr int kReleaseWaitS = 300; // T19 release: wait for the freed upload to complete
constexpr int kForeignReleaseWaitS = 120; // T20 SOFT release window (no foreign re-poll by ruling)

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

    // Fills apiIndex's storage to leave < kUploadSize (16 MiB) free, cloning the
    // SdkTestUploadsOverquota mechanics (import gzlQ3DIY 1 GB link + doCopyNode +
    // filler upload). Records the created fill-folder handle (set BEFORE the first
    // fallible assert) and any local filler file into the caller's cleanup state.
    // Sets alreadyOverquota=true and returns WITHOUT importing if the account is
    // already at/over quota. Uses ASSERT_* — invoke via ASSERT_NO_FATAL_FAILURE.
    void fillStorageLeavingUnder16MB(unsigned apiIndex,
                                     const std::string& fillFolderName,
                                     ::MegaHandle& fillFolderHandleOut,
                                     std::vector<std::string>& localFilesOut,
                                     bool& alreadyOverquota)
    {
        alreadyOverquota = false;

        std::unique_ptr<MegaNode> root{megaApi[apiIndex]->getRootNode()};
        ASSERT_TRUE(root) << "cannot resolve own root node";

        // Check OQ BEFORE importing — an import into an already-OQ account would
        // itself fail with EOVERQUOTA and confuse the fill.
        ASSERT_NO_FATAL_FAILURE(synchronousGetSpecificAccountDetails(apiIndex, true, false, false));
        ASSERT_NE(mApi[apiIndex].accountDetails, nullptr);
        const long long storageMax = mApi[apiIndex].accountDetails->getStorageMax();
        const long long storageUsed = mApi[apiIndex].accountDetails->getStorageUsed();
        ASSERT_GT(storageMax, 0);
        if (storageUsed >= storageMax)
        {
            alreadyOverquota = true;
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

        // Copy the seed until < kUploadSize remains free.
        const long long remaining = storageMax - storageUsed;
        const long long copies = remaining / copySize;
        for (long long i = 1; i <= copies; ++i)
        {
            const std::string copyName = seedName + std::to_string(i);
            ASSERT_EQ(
                API_OK,
                doCopyNode(apiIndex, nullptr, seedNode.get(), fillNode.get(), copyName.c_str()))
                << "copying fill node failed (i=" << i << ")";
        }

        // Top up with a filler upload so exactly kUploadSize-1 bytes remain free.
        const long long remainingAfterCopies = remaining - (copies * copySize);
        if (remainingAfterCopies >= kUploadSize)
        {
            const long long fillerSize = remainingAfterCopies - (kUploadSize - 1);
            const std::string fillerName = fillFolderName + "_filler.bin";
            localFilesOut.push_back(fillerName);
            ASSERT_TRUE(createFileWithSize(fillerName, static_cast<size_t>(fillerSize), "F"))
                << "creating filler file failed";
            TransferTracker fillTracker(megaApi[apiIndex].get());
            MegaUploadOptions fillOptions;
            fillOptions.mtime = MegaUploadOptions::INVALID_CUSTOM_MOD_TIME;
            megaApi[apiIndex]->startUpload(fillerName,
                                           fillNode.get(),
                                           nullptr,
                                           &fillOptions,
                                           &fillTracker);
            ASSERT_EQ(API_OK, fillTracker.waitForResult(kReleaseWaitS)) << "filler upload failed";
            deleteFile(fillerName);
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
// T19 RealFillOwnAccountHoldAndRelease — real end-to-end, OWN pool. Hook-free.
// Minutes; nightly-only. Fails pre-P3 on the discriminator, then cleans up.
// ============================================================================
TEST_F(SdkWsQuotaRealTest, RealFillOwnAccountHoldAndRelease)
{
    LOG_info << "___TEST RealFillOwnAccountHoldAndRelease___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Guard 1: tfs availability (probe-style SKIP).
    {
        const std::string reason = tfsProbeSkipReason(0);
        if (!reason.empty())
            GTEST_SKIP() << "tfs not available in this environment (" << reason << ")";
    }

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    // ---- Cleanup state + RAII teardown (runs on EVERY exit path) ----
    ::MegaHandle fillFolderHandle = ::mega::UNDEF;
    ::MegaHandle uploadNodeHandle = ::mega::UNDEF;
    std::string uploadName;
    std::vector<std::string> localFiles;
    auto cleanup = makeScopedDestructor(
        [&]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
            // Remove the uploaded 16 MiB node (by handle, else by name at root).
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
            // Remove the fill folder (drops the imported seed + copies + filler).
            if (fillFolderHandle != ::mega::UNDEF)
                if (std::unique_ptr<MegaNode> f{megaApi[0]->getNodeByHandle(fillFolderHandle)})
                    (void)synchronousRemove(0, f.get());
            for (const auto& nm: localFiles)
                deleteFile(nm);
            warnIfStorageStillHigh(0);
        });

    // Guard 2 + fill: SKIP if the account is already OQ (cannot set the controlled
    // < 16 MiB-free state), else fill to leave < 16 MiB.
    bool alreadyOverquota = false;
    ASSERT_NO_FATAL_FAILURE(fillStorageLeavingUnder16MB(0,
                                                        makeBinName("ws_quota_t19_fill_"),
                                                        fillFolderHandle,
                                                        localFiles,
                                                        alreadyOverquota));
    if (alreadyOverquota)
        GTEST_SKIP() << "account already overquota/full — cannot set up the controlled fill";

    // ---- 16 MiB predictive-hold probe upload to own root ----
    uploadName = makeBinName("ws_quota_t19_upload_");
    localFiles.push_back(uploadName);
    ASSERT_TRUE(createFileWithSize(uploadName, static_cast<size_t>(kUploadSize), "U"))
        << "creating the 16 MiB upload file failed";

    WsQuotaHoldTracker holdTracker(megaApi[0].get());
    OverquotaProgressLatch latch(megaApi[0].get());
    TransferTracker tracker(megaApi[0].get());
    MegaUploadOptions uploadOptions;
    uploadOptions.mtime = MegaUploadOptions::INVALID_CUSTOM_MOD_TIME;
    megaApi[0]->startUpload(uploadName, rootnode.get(), nullptr, &uploadOptions, &tracker);
    ASSERT_TRUE(WaitFor(
        [&]
        {
            return tracker.mTag.load() >= 0;
        },
        30000))
        << "transfer tag not captured";
    const int tag = tracker.mTag.load();

    // DISCRIMINATOR (pre-P3 FAILS here): predictive own-pool hold before all bytes.
    ASSERT_NO_FATAL_FAILURE(assertPredictiveHoldDiscriminator(holdTracker,
                                                              latch,
                                                              tracker,
                                                              tag,
                                                              /*expectForeign*/ false,
                                                              kDiscriminatorWaitS));

    // ---- Release: free space → the held upload completes OK ----
    if (std::unique_ptr<MegaNode> f{megaApi[0]->getNodeByHandle(fillFolderHandle)})
    {
        ASSERT_EQ(API_OK, synchronousRemove(0, f.get())) << "freeing fill space failed";
        fillFolderHandle = ::mega::UNDEF; // freed; cleanup must not re-remove
    }
    ASSERT_EQ(tracker.waitForResult(kReleaseWaitS), API_OK)
        << "held upload did not complete within " << kReleaseWaitS << "s after freeing space";
    uploadNodeHandle = tracker.resultNodeHandle;

    // HoldTracker sanity: EOVERQUOTA temp error(s) then a terminal Finish OK.
    const auto seq = holdTracker.sequence(tag);
    ASSERT_FALSE(seq.empty());
    ASSERT_EQ(seq.back().kind, WsQuotaHoldTracker::Event::Kind::Finish);
    ASSERT_EQ(seq.back().code, API_OK);
}

// ============================================================================
// T20 RealFillInshareForeignHold — real end-to-end, FOREIGN pool. 2 accounts.
// A = megaApi[0] (uploader); B = megaApi[1] (owner who fills + shares). Hook-free.
// Nightly-only. Fails pre-P3 on the foreign discriminator, then cleans up BOTH
// accounts. Release phase is a SOFT assert (no foreign re-poll per user ruling).
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

    // ---- Guard 2 + B fill: SKIP if B already OQ, else fill B to < 16 MiB free ----
    bool alreadyOverquota = false;
    ASSERT_NO_FATAL_FAILURE(fillStorageLeavingUnder16MB(1,
                                                        makeBinName("ws_quota_t20_fill_"),
                                                        bFillFolderHandle,
                                                        localFiles,
                                                        alreadyOverquota));
    if (alreadyOverquota)
        GTEST_SKIP() << "account B already overquota/full — cannot set up the controlled fill";

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

    // ---- A uploads 16 MiB into the inshare (A's own storage untouched) ----
    const std::string foreignName = makeBinName("ws_quota_t20_foreign_");
    localFiles.push_back(foreignName);
    ASSERT_TRUE(createFileWithSize(foreignName, static_cast<size_t>(kUploadSize), "G"))
        << "creating the 16 MiB foreign upload file failed";

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

    // DISCRIMINATOR (pre-P3 FAILS here): predictive FOREIGN hold before all bytes.
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
