/**
 * @file WsUploadFileno_test.cpp
 * @brief SDK-6600 fail-first cells: a wss:// upload URL must receive each fileno for at most one
 *        file, across every engine generation that shares the persisted transfer cache.
 *
 * Every cell drives a REAL mega::ws::UploadEngine with synthetic PUT transfers carrying
 * transfer-cache state, then inspects what enqueue() leaves in the Transfer (fileno, URL, resume
 * head) and in the engine (pinned pools and their references). Single-shot cells use a started-
 * then-stopped engine (real pinned pools, no pool worker ever runs, no network); the model uses
 * never-started engines. The cells that need UploadEngine::reserveFileNos live in
 * WsUploadFilenoSeed_test.cpp.
 *
 * (c) 2026 by MEGA Privacy Kft, Csomad, Hungary
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 */

#include <gtest/gtest.h>

#ifdef MEGA_USE_WSUPLOAD

#include "WsUploadFilenoHarness.h"

#include <mega/megaapp.h>

#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

namespace
{

using mt::wsfileno::kResumePos;
using mt::wsfileno::makeStartedQuiescedEngine;
using mt::wsfileno::pinnedPoolExists;
using mt::wsfileno::SyntheticUpload;

const std::string kUrl1{"wss://u1.example/ul/tok1"};
const std::string kUrl2{"wss://u2.example/ul/tok2"};

// A restored transfer whose persisted URL was dropped must restart fresh: no URL, no head.
void expectRestartedFresh(const mega::Transfer& t, const std::string& what)
{
    EXPECT_TRUE(t.ws_session_url.empty()) << what << ": kept its persisted URL '"
                                          << t.ws_session_url << "' under fileno " << t.ws_fileno;
    EXPECT_EQ(t.pos, 0) << what << ": kept its persisted resume head";
    EXPECT_EQ(t.chunkmacs.size(), 0u) << what << ": kept its persisted chunk MACs";
    EXPECT_EQ(t.progresscompleted, 0) << what << ": kept its persisted progress";
}

void expectPinnedPool(const mega::ws::UploadEngine& engine,
                      const std::string& url,
                      const bool expected,
                      const std::string& what)
{
    const auto exists = pinnedPoolExists(engine, url);
    if (!exists)
    {
        std::cout << "[WsUploadFileno] pinned-pool check skipped (no test hooks): " << what
                  << std::endl;
        return;
    }
    EXPECT_EQ(*exists, expected) << what << ": pinned pool for " << url
                                 << (expected ? " is missing" : " was created");
}

} // namespace

// Mechanism (a), Timeline A: a new upload X takes fileno F before the transfer cache is
// restored; restored T carries the same cached F, its URL and a resume head. F is live, so T
// cannot keep it; feeding T's persisted URL under another fileno may hand that URL a number the
// previous engine used there for a different file.
TEST(WsUploadFileno, RenumberedRestoredTransferDropsPersistedUrl)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    SyntheticUpload x(*client, "wsfileno_u1_x.bin");
    SyntheticUpload t(*client, "wsfileno_u1_t.bin");
    // Declared last, destroyed first: the engine never outlives the transfers it tracks.
    auto engine = makeStartedQuiescedEngine(*client);

    engine->enqueue(x.transfer());
    const std::uint32_t f = x.transfer().ws_fileno;
    ASSERT_NE(f, 0u);

    t.setPersisted(f, kUrl1, kResumePos);
    ASSERT_EQ(t.transfer().chunkmacs.size(), 1u) << "harness: resume MAC not set";
    engine->enqueue(t.transfer());

    // Engine invariant on every arm (not the fix): two live files never share a fileno.
    const std::uint32_t renumbered = t.transfer().ws_fileno;
    ASSERT_NE(renumbered, f) << "live fileno " << f << " handed to a second file";
    ASSERT_NE(renumbered, 0u);

    expectRestartedFresh(t.transfer(),
                         "restored transfer renumbered " + std::to_string(f) + "->" +
                             std::to_string(renumbered));
    expectPinnedPool(*engine, kUrl1, false, "restored transfer whose fileno could not be kept");

    engine->remove(t.transfer());
    engine->remove(x.transfer());
    EXPECT_FALSE(engine->isTrackedForTesting(t.transfer()));
    EXPECT_FALSE(engine->isTrackedForTesting(x.transfer()));
}

// Mechanism (g): a new upload X takes fileno F, feeds (normal-pool URL, F) and completes. T is
// restored with cached F but WITHOUT a URL (never started, or invalidatePinnedSessionUrl cleared
// it), so it goes to the same normal pool: keeping F would feed that URL F for a second file.
TEST(WsUploadFileno, CachedFilenoWithoutUrlIsNotReusedAfterAnotherFileUsedIt)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    SyntheticUpload x(*client, "wsfileno_u2_x.bin");
    SyntheticUpload t(*client, "wsfileno_u2_t.bin");
    auto engine = makeStartedQuiescedEngine(*client);

    engine->enqueue(x.transfer());
    const std::uint32_t f = x.transfer().ws_fileno;
    ASSERT_NE(f, 0u);
    engine->remove(x.transfer());
    ASSERT_FALSE(engine->isTrackedForTesting(x.transfer()))
        << "X must be gone (completed): F is no longer live";

    t.setPersisted(f, "", 0);
    engine->enqueue(t.transfer());

    EXPECT_NE(t.transfer().ws_fileno, f)
        << "restored transfer without a URL re-used fileno " << f
        << ", which completed file X already fed to this engine's normal-pool URL";
    EXPECT_NE(t.transfer().ws_fileno, 0u);

    engine->remove(t.transfer());
    EXPECT_FALSE(engine->isTrackedForTesting(t.transfer()));
}

// Legacy caches: users already hold transfer caches written by the buggy engine, where two
// uploads may carry one cached fileno with different URLs. The first keeps its resumable
// identity; the second must not feed its URL under a foreign fileno.
TEST(WsUploadFileno, LegacyDuplicateCachedFilenosNeverShareAUrl)
{
    constexpr std::uint32_t kLegacyFileno = 7;
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    SyntheticUpload first(*client, "wsfileno_u4_first.bin");
    SyntheticUpload second(*client, "wsfileno_u4_second.bin");
    auto engine = makeStartedQuiescedEngine(*client);

    first.setPersisted(kLegacyFileno, kUrl1, kResumePos);
    second.setPersisted(kLegacyFileno, kUrl2, kResumePos);
    engine->enqueue(first.transfer());
    engine->enqueue(second.transfer());

    EXPECT_EQ(first.transfer().ws_fileno, kLegacyFileno);
    EXPECT_EQ(first.transfer().ws_session_url, kUrl1);
    EXPECT_EQ(first.transfer().pos, kResumePos);
    EXPECT_EQ(first.transfer().progresscompleted, kResumePos);
    EXPECT_EQ(first.transfer().chunkmacs.size(), 1u);
    expectPinnedPool(*engine, kUrl1, true, "first legacy duplicate (resumable)");

    const std::uint32_t renumbered = second.transfer().ws_fileno;
    ASSERT_NE(renumbered, kLegacyFileno) << "live fileno handed to a second file";
    ASSERT_NE(renumbered, 0u);
    expectRestartedFresh(second.transfer(),
                         "second legacy duplicate renumbered " + std::to_string(kLegacyFileno) +
                             "->" + std::to_string(renumbered));
    expectPinnedPool(*engine, kUrl2, false, "second legacy duplicate (URL dropped)");

    engine->remove(second.transfer());
    engine->remove(first.transfer());
    EXPECT_FALSE(engine->isTrackedForTesting(first.transfer()));
    EXPECT_FALSE(engine->isTrackedForTesting(second.transfer()));
}

// A cached fileno F that THIS engine already handed to another file X (now completed) is not
// live any more, but it is not free either: X fed (normal-pool URL, F). If T keeps F and its
// pinned URL is later invalidated (invalidatePinnedSessionUrl keeps the fileno and moves the
// file to normal pools), T feeds (normal-pool URL, F) a second time. Realistic trigger: a
// support upload (no parent node needed) enqueued before fetchnodes loads the transfer cache.
TEST(WsUploadFileno, RestoredFilenoAlreadyHandedOutByThisEngineIsNotKept)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    SyntheticUpload x(*client, "wsfileno_u6_x.bin");
    SyntheticUpload t(*client, "wsfileno_u6_t.bin");
    auto engine = makeStartedQuiescedEngine(*client);

    engine->enqueue(x.transfer());
    const std::uint32_t f = x.transfer().ws_fileno;
    ASSERT_NE(f, 0u);
    engine->remove(x.transfer());
    ASSERT_FALSE(engine->isTrackedForTesting(x.transfer()))
        << "X must be gone (completed): F is no longer live";

    t.setPersisted(f, kUrl1, kResumePos);
    engine->enqueue(t.transfer());

    EXPECT_NE(t.transfer().ws_fileno, f)
        << "restored transfer kept fileno " << f
        << ", which this engine already handed to completed file X";
    EXPECT_NE(t.transfer().ws_fileno, 0u);
    expectRestartedFresh(t.transfer(), "restored transfer whose cached fileno was already claimed");
    expectPinnedPool(*engine, kUrl1, false, "restored transfer whose cached fileno was claimed");

    engine->remove(t.transfer());
    EXPECT_FALSE(engine->isTrackedForTesting(t.transfer()));
}

// Legacy caches may hold two records with the SAME (fileno, URL). Twin 1 keeps it and completes;
// twin 2 must not be fed on that URL with that fileno again.
TEST(WsUploadFileno, LegacyTwinOnSameUrlIsNotKeptAfterFirstTwinCompleted)
{
    constexpr std::uint32_t kLegacyFileno = 9;
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    SyntheticUpload twin1(*client, "wsfileno_u7_twin1.bin");
    SyntheticUpload twin2(*client, "wsfileno_u7_twin2.bin");
    auto engine = makeStartedQuiescedEngine(*client);

    twin1.setPersisted(kLegacyFileno, kUrl1, kResumePos);
    engine->enqueue(twin1.transfer());
    EXPECT_EQ(twin1.transfer().ws_fileno, kLegacyFileno);
    EXPECT_EQ(twin1.transfer().ws_session_url, kUrl1);
    EXPECT_EQ(twin1.transfer().pos, kResumePos);
    engine->remove(twin1.transfer());
    ASSERT_FALSE(engine->isTrackedForTesting(twin1.transfer()))
        << "twin 1 must be gone (completed)";

    twin2.setPersisted(kLegacyFileno, kUrl1, kResumePos);
    engine->enqueue(twin2.transfer());

    EXPECT_NE(twin2.transfer().ws_fileno, kLegacyFileno)
        << "twin 2 kept (fileno " << kLegacyFileno << ", " << kUrl1
        << "), which twin 1 already fed";
    EXPECT_NE(twin2.transfer().ws_fileno, 0u);
    expectRestartedFresh(twin2.transfer(), "legacy twin 2");
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    // Twin 1's pinned pool outlives twin 1 (only the manager's checkPools retires it). With
    // twin 1 gone, only twin 2's engine-side file could still reference it: it must not, or the
    // URL was cleared on the Transfer after the WsUploadFile had already been built from it.
    {
        mega::ws::UploadEngine::PoolStateForTesting pool;
        ASSERT_TRUE(engine->getPoolStateForTesting(kUrl1, pool) && pool.found && pool.pinned)
            << "harness: twin 1's pinned pool for " << kUrl1 << " is missing";
        EXPECT_FALSE(pool.hasReference)
            << "twin 2's engine-side file still targets " << kUrl1
            << " (URL cleared after the WsUploadFile was built, or never cleared)";
    }
#endif

    engine->remove(twin2.transfer());
    EXPECT_FALSE(engine->isTrackedForTesting(twin2.transfer()));
}

// U6 with the restored transfer built IN the finished transfer's storage: a new Transfer at a
// recycled address is not that transfer's retry. Kills an ownership check keyed on the Transfer
// address deterministically (independent of the allocator, and of ASAN's quarantine).
TEST(WsUploadFileno, RestoredTransferAtAFinishedTransfersAddressIsNotItsRetry)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    SyntheticUpload upload(*client, "wsfileno_u10_x.bin");
    auto engine = makeStartedQuiescedEngine(*client);

    engine->enqueue(upload.transfer());
    const std::uint32_t f = upload.transfer().ws_fileno;
    ASSERT_NE(f, 0u);
    engine->remove(upload.transfer());
    ASSERT_FALSE(engine->isTrackedForTesting(upload.transfer()))
        << "X must be gone (completed): F is no longer live";

    const mega::Transfer* const finished = &upload.transfer();
    mega::Transfer& restored = upload.reincarnate(*client, "wsfileno_u10_t.bin");
    ASSERT_EQ(&restored, finished) << "harness: the restored transfer must reuse X's storage";
    upload.setPersisted(f, kUrl1, kResumePos);
    engine->enqueue(restored);

    EXPECT_NE(restored.ws_fileno, f) << "restored transfer at completed X's address kept fileno "
                                     << f << ", which this engine already handed to X";
    EXPECT_NE(restored.ws_fileno, 0u);
    expectRestartedFresh(restored, "restored transfer at a finished transfer's address");
    expectPinnedPool(*engine, kUrl1, false, "restored transfer at a finished transfer's address");

    engine->remove(restored);
    EXPECT_FALSE(engine->isTrackedForTesting(restored));
}

// GUARD, not a fail-first cell (GREEN on base; must stay GREEN on the fix): a whole-file retry
// (Transfer::failed resets the head, the transfer is removed and re-enqueued) keeps the fileno
// this engine gave it, and its URL when it had one. Retry semantics are out of the SDK-6600 scope.
TEST(WsUploadFileno, RetryReenqueueKeepsOwnFilenoAndUrl)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    SyntheticUpload started(*client, "wsfileno_u8_started.bin");
    SyntheticUpload queued(*client, "wsfileno_u8_queued.bin");
    auto engine = makeStartedQuiescedEngine(*client);

    engine->enqueue(started.transfer());
    const std::uint32_t startedNo = started.transfer().ws_fileno;
    ASSERT_NE(startedNo, 0u);
    started.transfer().ws_session_url = kUrl1; // as onStart records the pool URL
    engine->enqueue(queued.transfer());
    const std::uint32_t queuedNo = queued.transfer().ws_fileno;
    ASSERT_NE(queuedNo, 0u);

    for (SyntheticUpload* retried: {&started, &queued})
    {
        engine->remove(retried->transfer());
        retried->setResumeHead(0);
        engine->enqueue(retried->transfer());
    }

    EXPECT_EQ(started.transfer().ws_fileno, startedNo) << "retry lost its own fileno";
    EXPECT_EQ(started.transfer().ws_session_url, kUrl1) << "retry lost its own URL";
    expectPinnedPool(*engine, kUrl1, true, "retry with its own URL");
    EXPECT_EQ(queued.transfer().ws_fileno, queuedNo) << "URL-less retry lost its own fileno";
    EXPECT_TRUE(queued.transfer().ws_session_url.empty());

    engine->remove(queued.transfer());
    engine->remove(started.transfer());
}

// GUARD, not a fail-first cell (GREEN on base; must stay GREEN on the fix): the retry of a
// RESTORED transfer whose cached (fileno, URL) was kept is still its own retry. An engine that
// only recognises the filenos it allocated fresh would renumber it and drop its URL.
TEST(WsUploadFileno, RetryOfARestoredTransferKeepsItsFilenoAndUrl)
{
    constexpr std::uint32_t kPersisted = 5;
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    SyntheticUpload t(*client, "wsfileno_u8b_t.bin");
    auto engine = makeStartedQuiescedEngine(*client);

    t.setPersisted(kPersisted, kUrl1, kResumePos);
    engine->enqueue(t.transfer());
    ASSERT_EQ(t.transfer().ws_fileno, kPersisted) << "a clean restore must keep its cached fileno";
    ASSERT_EQ(t.transfer().ws_session_url, kUrl1) << "a clean restore must keep its cached URL";

    engine->remove(t.transfer());
    t.setResumeHead(0);
    engine->enqueue(t.transfer());

    EXPECT_EQ(t.transfer().ws_fileno, kPersisted) << "retry of a restored transfer lost its fileno";
    EXPECT_EQ(t.transfer().ws_session_url, kUrl1) << "retry of a restored transfer lost its URL";
    expectPinnedPool(*engine, kUrl1, true, "retry of a restored transfer with its own URL");

    engine->remove(t.transfer());
}

// Exhaustive small-scope model (see WsUploadFilenoHarness.h): every sequence of new / queue /
// done / restart / restore / invalidate over 3 files, replayed on real engines. No
// (URL, fileno) pair may ever be fed to two files. Unseeded: see WsUploadFilenoSeed.
TEST(WsUploadFileno, NoUrlFilenoPairEverServesTwoFiles)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    mt::wsfileno::FilenoModelConfig config;
    config.steps = mt::wsfileno::modelSteps(config.steps);
    const auto report = mt::wsfileno::exploreFilenoModel(*client, config);
    std::cout << "[WsUploadFileno.NoUrlFilenoPairEverServesTwoFiles] files=" << config.files
              << " steps=" << config.steps << " maxRestarts=" << config.maxRestarts << " "
              << report.summary() << std::endl;
    if (!report.firstViolation.empty())
    {
        std::cout << "[WsUploadFileno.NoUrlFilenoPairEverServesTwoFiles] first counterexample: "
                  << report.firstViolation << std::endl;
    }

    // Anti-vacuity: the explored space reaches every violating precondition. collidingRestores,
    // claimedRestores and reusedPairRestores are probed on the model state before the enqueue
    // under test; invalidations counts restored files that still carried a pinned URL after
    // the engine decided, so it depends on the engine (a fix that never keeps a cached URL
    // reads 0 here).
    ASSERT_GT(report.sends, 0u);
    ASSERT_GT(report.restoresWithUrl, 0u);
    ASSERT_GT(report.invalidations, 0u) << "no pinned URL was ever invalidated";
    ASSERT_GT(report.collidingRestores, 0u) << "no restore met a live fileno (mechanism a)";
    ASSERT_GT(report.claimedRestores, 0u) << "no restore met an already handed-out fileno";
    ASSERT_GT(report.reusedPairRestores, 0u) << "no URL-less restore met a fed pair (mechanism g)";

    EXPECT_EQ(report.violatingSequences, 0u)
        << report.violatingSequences << " of " << report.sequences
        << " sequences fed one (URL, fileno) pair to two files; first counterexample: "
        << report.firstViolation;
    EXPECT_EQ(report.liveDuplicates, 0u) << report.firstLiveDuplicate;
}

// Wrap safety: a cached fileno of UINT32_MAX (corrupted cache) must not wrap the counter into a
// number that is still live, which would feed one normal-pool URL a fileno for two files at once.
TEST(WsUploadFileno, WrappedCachedFilenoNeverDuplicatesALiveFileno)
{
    constexpr std::uint32_t kMaxFileno = std::numeric_limits<std::uint32_t>::max();
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    SyntheticUpload low(*client, "wsfileno_u9_low.bin");
    SyntheticUpload high(*client, "wsfileno_u9_high.bin");
    SyntheticUpload x(*client, "wsfileno_u9_x.bin");
    auto engine = makeStartedQuiescedEngine(*client);

    low.setPersisted(1, kUrl1, kResumePos);
    high.setPersisted(kMaxFileno, kUrl2, kResumePos);
    engine->enqueue(low.transfer());
    engine->enqueue(high.transfer());
    engine->enqueue(x.transfer());

    const std::uint32_t lowNo = low.transfer().ws_fileno;
    const std::uint32_t highNo = high.transfer().ws_fileno;
    const std::uint32_t newNo = x.transfer().ws_fileno;
    EXPECT_NE(lowNo, highNo);
    EXPECT_NE(newNo, 0u) << "new upload got fileno 0";
    EXPECT_NE(newNo, lowNo) << "new upload got fileno " << newNo
                            << ", still live for a restored transfer (counter wrapped past "
                            << kMaxFileno << ")";
    EXPECT_NE(newNo, highNo) << "new upload got the live fileno " << newNo;

    engine->remove(x.transfer());
    engine->remove(high.transfer());
    engine->remove(low.transfer());
}

#else

TEST(WsUploadFileno, DisabledWithoutWsUpload)
{
    GTEST_SKIP() << "MEGA_USE_WSUPLOAD is disabled";
}

#endif
