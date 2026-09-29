/**
 * @file WsUploadFilenoSeed_test.cpp
 * @brief SDK-6600 cells for rule S: UploadEngine::reserveFileNos(highestPersisted) keeps new
 *        uploads above every fileno in the persisted transfer cache, so restored transfers
 *        keep their resume instead of colliding with new uploads.
 *
 * Added by the fix commit together with the API: without reserveFileNos this file cannot
 * compile, which is its fail-first state by construction. Shares WsUploadFilenoHarness.h with
 * WsUploadFileno_test.cpp.
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

} // namespace

// U3: the transfer cache holds fileno 5 (highest). Seeded, a new upload takes a fileno above 5
// and the restored transfer keeps (5, URL, head) and its pinned pool: resumed, not restarted.
TEST(WsUploadFilenoSeed, FilenoFloorKeepsRestoredTransferResumable)
{
    constexpr std::uint32_t kPersisted = 5;
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    SyntheticUpload x(*client, "wsfileno_u3_x.bin");
    SyntheticUpload t(*client, "wsfileno_u3_t.bin");
    auto engine = makeStartedQuiescedEngine(*client);

    engine->reserveFileNos(kPersisted);
    engine->enqueue(x.transfer());
    EXPECT_GT(x.transfer().ws_fileno, kPersisted)
        << "new upload took fileno " << x.transfer().ws_fileno
        << ", at or below the highest persisted fileno " << kPersisted;

    t.setPersisted(kPersisted, kUrl1, kResumePos);
    engine->enqueue(t.transfer());
    EXPECT_EQ(t.transfer().ws_fileno, kPersisted) << "restored transfer lost its fileno";
    EXPECT_EQ(t.transfer().ws_session_url, kUrl1) << "restored transfer lost its URL";
    EXPECT_EQ(t.transfer().pos, kResumePos) << "restored transfer lost its resume head";
    EXPECT_EQ(t.transfer().progresscompleted, kResumePos);
    EXPECT_EQ(t.transfer().chunkmacs.size(), 1u);
    EXPECT_NE(x.transfer().ws_fileno, t.transfer().ws_fileno);
    if (const auto pinned = pinnedPoolExists(*engine, kUrl1))
    {
        EXPECT_TRUE(*pinned) << "no pinned pool for the resumed URL " << kUrl1;
    }

    engine->remove(t.transfer());
    engine->remove(x.transfer());
}

// The seed only ever raises the counter (several cache loads may reach the engine in any
// order), and a corrupted highest value (UINT32_MAX) must not wrap it into 0 or a live number.
TEST(WsUploadFilenoSeed, ReserveFileNosOnlyRaisesAndStaysWrapSafe)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    SyntheticUpload a(*client, "wsfileno_u3b_a.bin");
    SyntheticUpload b(*client, "wsfileno_u3b_b.bin");
    SyntheticUpload c(*client, "wsfileno_u3b_c.bin");
    auto engine = makeStartedQuiescedEngine(*client);

    engine->reserveFileNos(10);
    engine->enqueue(a.transfer());
    const std::uint32_t aNo = a.transfer().ws_fileno;
    EXPECT_GT(aNo, 10u) << "new upload took fileno " << aNo << " at or below the floor 10";

    engine->reserveFileNos(3);
    engine->enqueue(b.transfer());
    const std::uint32_t bNo = b.transfer().ws_fileno;
    EXPECT_GT(bNo, aNo) << "a lower seed moved the counter back: " << bNo << " after " << aNo;

    engine->reserveFileNos(std::numeric_limits<std::uint32_t>::max());
    engine->enqueue(c.transfer());
    const std::uint32_t cNo = c.transfer().ws_fileno;
    EXPECT_NE(cNo, 0u) << "seed UINT32_MAX wrapped the counter to fileno 0";
    EXPECT_NE(cNo, aNo) << "seed UINT32_MAX wrapped the counter onto live fileno " << cNo;
    EXPECT_NE(cNo, bNo) << "seed UINT32_MAX wrapped the counter onto live fileno " << cNo;

    engine->remove(c.transfer());
    engine->remove(b.transfer());
    engine->remove(a.transfer());
}

namespace
{

void seedWithReserveFileNos(mega::ws::UploadEngine& engine, const std::uint32_t highestPersisted)
{
    engine.reserveFileNos(highestPersisted);
}

} // namespace

// U5, seeded first: the model of WsUploadFileno.NoUrlFilenoPairEverServesTwoFiles with every
// restarted engine seeded with the highest persisted fileno before anything is enqueued (the
// transfer cache loads before the app starts uploads). Pairs stay unique AND no restored
// transfer ever loses its resume.
TEST(WsUploadFilenoSeed, NoUrlFilenoPairEverServesTwoFilesWhenSeeded)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    mt::wsfileno::FilenoModelConfig config;
    config.steps = mt::wsfileno::modelSteps(config.steps);
    config.seed = seedWithReserveFileNos;
    const auto report = mt::wsfileno::exploreFilenoModel(*client, config);
    std::cout << "[WsUploadFilenoSeed.NoUrlFilenoPairEverServesTwoFilesWhenSeeded] files="
              << config.files << " steps=" << config.steps << " maxRestarts=" << config.maxRestarts
              << " " << report.summary() << std::endl;

    ASSERT_GT(report.sends, 0u);
    ASSERT_GT(report.restoresWithUrl, 0u) << "the model never restored a resumable transfer";
    ASSERT_GT(report.invalidations, 0u) << "no pinned URL was ever invalidated";

    EXPECT_EQ(report.violatingSequences, 0u)
        << report.violatingSequences << " of " << report.sequences
        << " sequences fed one (URL, fileno) pair to two files; first counterexample: "
        << report.firstViolation;
    EXPECT_EQ(report.collidingRestores, 0u)
        << "a seeded engine let a new upload take a persisted fileno";
    EXPECT_EQ(report.resumeLost, 0u)
        << report.resumeLost
        << " restored transfers lost their resume; first: " << report.firstResumeLoss;
    EXPECT_EQ(report.liveDuplicates, 0u) << report.firstLiveDuplicate;
}

// U5, seed deferred: uploads (e.g. support uploads, which need no parent node) may be enqueued
// before fetchnodes loads the transfer cache and seeds the engine; restores still wait for the
// seed. Such an early upload can take a persisted fileno, so a restored transfer may have to
// restart (resume loss is expected and only reported), but no pair may ever serve two files.
// One step deeper than the other models: the shortest early-upload counterexample to a
// live-only fix (new restart new done seed restore invalidate) has 7 steps.
TEST(WsUploadFilenoSeed, NoUrlFilenoPairEverServesTwoFilesWithUploadsBeforeSeed)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    mt::wsfileno::FilenoModelConfig config;
    config.steps = mt::wsfileno::modelSteps(7);
    config.seed = seedWithReserveFileNos;
    config.deferSeed = true;
    const auto report = mt::wsfileno::exploreFilenoModel(*client, config);
    std::cout << "[WsUploadFilenoSeed.NoUrlFilenoPairEverServesTwoFilesWithUploadsBeforeSeed] "
                 "files="
              << config.files << " steps=" << config.steps << " maxRestarts=" << config.maxRestarts
              << " " << report.summary() << std::endl;

    ASSERT_GT(report.sends, 0u);
    ASSERT_GT(report.restoresWithUrl, 0u) << "the model never restored a resumable transfer";
    ASSERT_GT(report.uploadsBeforeSeed, 0u) << "no upload was ever enqueued before the seed";
    ASSERT_GT(report.claimedRestores, 0u) << "no restore met an already handed-out fileno";

    EXPECT_EQ(report.violatingSequences, 0u)
        << report.violatingSequences << " of " << report.sequences
        << " sequences fed one (URL, fileno) pair to two files; first counterexample: "
        << report.firstViolation;
    EXPECT_EQ(report.liveDuplicates, 0u) << report.firstLiveDuplicate;
}

#else

TEST(WsUploadFilenoSeed, DisabledWithoutWsUpload)
{
    GTEST_SKIP() << "MEGA_USE_WSUPLOAD is disabled";
}

#endif
