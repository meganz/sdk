/**
 * (c) 2026 by MEGA Privacy Kft, Csomad, Hungary
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * SDK-6298 — unit tests for the websocket-upload quota subsystem:
 *   - the "tfs" reply parser (CommandTfsForWsUpload::procresult), and
 *   - the client-thread-only ledger (ws::UploadQuotaManager).
 */

#include "mega/command.h"
#include "mega/commands_ws.h"
#include "mega/json.h"
#include "mega/megaapp.h"
#include "mega/megaclient.h"
#include "mega/transfer/ws/ws_quota.h"
#include "mega/transfer/ws/ws_quota_types.h"
#include "mega/types.h"
#include "utils.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#ifdef MEGA_USE_WSUPLOAD

// ::mega:: prefixes throughout: under C++20 a bare `mega::` combined with a
// `using namespace std;` in a translation unit resolves ambiguously against
// std::mega (SI prefix helpers). HARD RULE for this codebase.
using ::mega::Command;
using ::mega::CommandTfsForWsUpload;
using ::mega::Error;
using ::mega::JSON;
using ::mega::MegaApp;
using ::mega::MegaClient;
using ::mega::NodeHandle;
using ::mega::WsTfsGroupBalances;
using ::mega::ws::computeWsQuotaQueueFit;
using ::mega::ws::UploadQuotaManager;
using ::mega::ws::WsQuotaPoolFit;
using ::mega::ws::WsQuotaQueueFit;

namespace
{

// -------------------------------------------------------------------------
// Parser harness (U1-U6) — drives the REAL CommandTfsForWsUpload::procresult.
//
// Mechanism mirrors the (now stale) Commands_test.cpp:207 CommandFetchAds +
// ClientMockup pattern, adapted to the current 2-arg procresult(Result, JSON&)
// and the current MegaClient ctor via tests/unit mt::makeClient (worker-thread
// count 0, no network).
//
// JSON-cursor contract note (IMPORTANT — see the report): the strings fed here
// are the "tfs" *result array* verbatim, e.g. `[[bytes,handle],...]`, and we do
// NOT pre-enter that outer array before calling procresult. That is what makes
// this parser extract the groups, because CommandTfsForWsUpload::procresult
// itself opens the outer array (`if (jc.enterarray()) { while (jc.enterarray())
// ... }`). The sibling working command CommandUSCForWsUpload — and every other
// Command — is instead driven by Request::process/processCmdJSON, which ALREADY
// enters the result array one level (Command::CmdArray == "array, and we have
// already entered it") before calling procresult. That one-level offset is
// exercised by ParserCursorContract_* below.
// -------------------------------------------------------------------------

struct TfsParse
{
    bool called{false};
    Error err{::mega::API_EINTERNAL};
    WsTfsGroupBalances groups;
};

// Feed `reply` to the parser. `preEnter` = number of enterarray() calls applied
// to the cursor before procresult. The default 1 reproduces the state the
// standard Command dispatch hands to a CmdArray procresult (result array
// already entered — the parser's contract); 0 feeds it un-entered (see
// ParserConsumesPreEnteredResultArray).
TfsParse runTfsParse(const std::string& reply,
                     int preEnter = 1,
                     Command::Outcome outcome = Command::CmdArray,
                     Error resultError = Error(::mega::API_OK))
{
    MegaApp app;
    auto client = mt::makeClient(app);

    TfsParse out;
    const std::vector<NodeHandle> folders; // request-side only; irrelevant here
    CommandTfsForWsUpload cmd(*client,
                              folders,
                              [&out](Error e, WsTfsGroupBalances&& g)
                              {
                                  out.called = true;
                                  out.err = e;
                                  out.groups = std::move(g);
                              });
    cmd.client = client.get();

    JSON json;
    json.pos = reply.c_str();
    for (int i = 0; i < preEnter; ++i)
    {
        json.enterarray();
    }

    Command::Result r(outcome, resultError);
    cmd.procresult(r, json);
    return out;
}

// Decode an 8-char base64 node-handle string exactly as the parser does
// (JSON::gethandle over a quoted token), so handle-identity assertions stay
// self-consistent with the production decode path.
NodeHandle handleFromB64(const std::string& b64)
{
    const std::string quoted = "\"" + b64 + "\"";
    JSON j;
    j.pos = quoted.c_str();
    return NodeHandle().set6byte(j.gethandle(MegaClient::NODEHANDLE));
}

// -------------------------------------------------------------------------
// Ledger helpers (U7-U14) — direct ws::UploadQuotaManager, no MegaClient.
// -------------------------------------------------------------------------

using Classifier = UploadQuotaManager::PoolClassifier;

// Own-pool classifier: every folder maps to the same non-foreign pool key.
Classifier ownPool()
{
    return [](NodeHandle) -> std::pair<std::uint64_t, bool>
    {
        return std::make_pair(std::uint64_t(1), false);
    };
}

NodeHandle nh(std::uint64_t v)
{
    return NodeHandle().set6byte(v);
}

// Apply groups at a fresh, valid generation (begin/end/apply idiom).
std::uint64_t seed(UploadQuotaManager& mgr,
                   const WsTfsGroupBalances& groups,
                   const Classifier& classify)
{
    const std::uint64_t gen = mgr.beginIssue();
    mgr.endIssue(gen);
    EXPECT_TRUE(mgr.applyGroups(gen, groups, classify));
    return gen;
}

} // namespace

// =========================================================================
// Parser cells (U1-U6)
// =========================================================================

// U1 — real probe shape (one handle per group) + a genuinely multi-handle group.
TEST(WsQuota, ParseMultiGroup)
{
    // Real probe: two folders of the same account, one group each.
    {
        const auto p = runTfsParse(R"([[3303903592448,"NjggVJ4b"],[3303903592448,"Yz4jCC7Q"]])");
        ASSERT_TRUE(p.called);
        EXPECT_EQ(p.err, ::mega::API_OK);
        ASSERT_EQ(p.groups.size(), 2u);

        EXPECT_EQ(p.groups[0].first, 3303903592448LL);
        ASSERT_EQ(p.groups[0].second.size(), 1u);
        EXPECT_EQ(p.groups[0].second[0], handleFromB64("NjggVJ4b"));

        EXPECT_EQ(p.groups[1].first, 3303903592448LL);
        ASSERT_EQ(p.groups[1].second.size(), 1u);
        EXPECT_EQ(p.groups[1].second[0], handleFromB64("Yz4jCC7Q"));
    }

    // Spec format also permits several handles sharing one group.
    {
        const auto p = runTfsParse(R"([[123,"NjggVJ4b","Yz4jCC7Q"],[456,"Mm5GDZAY"]])");
        ASSERT_TRUE(p.called);
        EXPECT_EQ(p.err, ::mega::API_OK);
        ASSERT_EQ(p.groups.size(), 2u);

        EXPECT_EQ(p.groups[0].first, 123LL);
        ASSERT_EQ(p.groups[0].second.size(), 2u);
        EXPECT_EQ(p.groups[0].second[0], handleFromB64("NjggVJ4b"));
        EXPECT_EQ(p.groups[0].second[1], handleFromB64("Yz4jCC7Q"));

        EXPECT_EQ(p.groups[1].first, 456LL);
        ASSERT_EQ(p.groups[1].second.size(), 1u);
        EXPECT_EQ(p.groups[1].second[0], handleFromB64("Mm5GDZAY"));
    }
}

// U2 — minimal single group / single folder (the canonical probe fixture).
TEST(WsQuota, ParseSingleGroupSingleFolder)
{
    const auto p = runTfsParse(R"([[3303903592448,"NjggVJ4b"]])");
    ASSERT_TRUE(p.called);
    EXPECT_EQ(p.err, ::mega::API_OK);
    ASSERT_EQ(p.groups.size(), 1u);
    EXPECT_EQ(p.groups[0].first, 3303903592448LL);
    ASSERT_EQ(p.groups[0].second.size(), 1u);
    EXPECT_EQ(p.groups[0].second[0], handleFromB64("NjggVJ4b"));
}

// U3 — empty outer array is valid: API_OK + no data (fail-open downstream).
TEST(WsQuota, ParseEmptyArray)
{
    const auto p = runTfsParse(R"([])");
    ASSERT_TRUE(p.called);
    EXPECT_EQ(p.err, ::mega::API_OK);
    EXPECT_TRUE(p.groups.empty());
}

// U4 — malformed groups skipped (fail-open); structural/error replies rejected.
TEST(WsQuota, ParseMalformedGroupSkippedStructuralRejected)
{
    // (a) string-where-balance: malformed group skipped, the good one kept.
    {
        const auto p = runTfsParse(R"([["abc","NjggVJ4b"],[456,"Mm5GDZAY"]])");
        ASSERT_TRUE(p.called);
        EXPECT_EQ(p.err, ::mega::API_OK);
        ASSERT_EQ(p.groups.size(), 1u);
        EXPECT_EQ(p.groups[0].first, 456LL);
        ASSERT_EQ(p.groups[0].second.size(), 1u);
        EXPECT_EQ(p.groups[0].second[0], handleFromB64("Mm5GDZAY"));
    }

    // (b) negative balance group skipped, the good one kept.
    {
        const auto p = runTfsParse(R"([[-5,"NjggVJ4b"],[456,"Mm5GDZAY"]])");
        ASSERT_TRUE(p.called);
        EXPECT_EQ(p.err, ::mega::API_OK);
        ASSERT_EQ(p.groups.size(), 1u);
        EXPECT_EQ(p.groups[0].first, 456LL);
    }

    // (c) balance but zero handles skipped, the good one kept.
    {
        const auto p = runTfsParse(R"([[100],[456,"Mm5GDZAY"]])");
        ASSERT_TRUE(p.called);
        EXPECT_EQ(p.err, ::mega::API_OK);
        ASSERT_EQ(p.groups.size(), 1u);
        EXPECT_EQ(p.groups[0].first, 456LL);
    }

    // (d) error-shaped Result (numeric error delivered as CmdError): the
    //     completion receives the error verbatim + empty groups, json untouched.
    {
        const auto p = runTfsParse(/*reply*/ "",
                                   /*preEnter*/ 0,
                                   Command::CmdError,
                                   Error(::mega::API_EACCESS));
        ASSERT_TRUE(p.called);
        EXPECT_EQ(p.err, ::mega::API_EACCESS);
        EXPECT_TRUE(p.groups.empty());
    }
    // (d') a CmdError carrying API_OK (the "st-only, implicitly successful"
    //      dispatch shape) is remapped to API_EINTERNAL for a data command.
    {
        const auto p = runTfsParse("", 0, Command::CmdError, Error(::mega::API_OK));
        ASSERT_TRUE(p.called);
        EXPECT_EQ(p.err, ::mega::API_EINTERNAL);
        EXPECT_TRUE(p.groups.empty());
    }

    // (e) PINNED fail-open behavior: a non-array element mid-stream stops group
    //     parsing at that point — the trailing well-formed group is NOT
    //     recovered. This documents the current prefix-parse semantics; if the
    //     parser is later hardened to skip-and-continue, update this pin.
    {
        const auto p = runTfsParse(R"(["x",[456,"Mm5GDZAY"]])");
        ASSERT_TRUE(p.called);
        EXPECT_EQ(p.err, ::mega::API_OK);
        EXPECT_TRUE(p.groups.empty());
    }
}

// U5 — duplicate handle across groups: the PARSER does not dedup (two entries).
//      First-wins dedup is a LEDGER concern (asserted in WsQuotaLedger below).
TEST(WsQuota, ParseDuplicateHandleAcrossGroups)
{
    const auto p = runTfsParse(R"([[100,"NjggVJ4b"],[200,"NjggVJ4b"]])");
    ASSERT_TRUE(p.called);
    EXPECT_EQ(p.err, ::mega::API_OK);
    ASSERT_EQ(p.groups.size(), 2u);
    EXPECT_EQ(p.groups[0].first, 100LL);
    EXPECT_EQ(p.groups[1].first, 200LL);
    ASSERT_EQ(p.groups[0].second.size(), 1u);
    ASSERT_EQ(p.groups[1].second.size(), 1u);
    EXPECT_EQ(p.groups[0].second[0], handleFromB64("NjggVJ4b"));
    EXPECT_EQ(p.groups[1].second[0], handleFromB64("NjggVJ4b"));
}

// U6 — negative availableBytes skips the group (fail-open, NOT clamp-to-zero).
TEST(WsQuota, ParseNegativeAvailableSkipsGroup)
{
    const auto p = runTfsParse(R"([[-5,"NjggVJ4b"]])");
    ASSERT_TRUE(p.called);
    EXPECT_EQ(p.err, ::mega::API_OK);
    EXPECT_TRUE(p.groups.empty());
}

// =========================================================================
// Ledger cells (U7-U14)
// =========================================================================

// U7 — apply one entry, then availableFor/hasBalanceFor; unknown folder => none.
TEST(WsQuotaLedger, ApplyThenAvailableFor)
{
    UploadQuotaManager mgr;
    const NodeHandle a = nh(0xA1);
    const NodeHandle unknown = nh(0xB2);

    seed(mgr, WsTfsGroupBalances{{100, {a}}}, ownPool());

    EXPECT_TRUE(mgr.haveBalances());
    EXPECT_TRUE(mgr.hasBalanceFor(a));
    EXPECT_EQ(mgr.availableFor(a), 100LL);

    EXPECT_FALSE(mgr.hasBalanceFor(unknown));
    EXPECT_EQ(mgr.availableFor(unknown), 0LL);
}

// U7b — DECISION-P1-POOLKEY: two entries with DIFFERENT balances whose folders
//       classify to the SAME pool key merge (min), and deductions are shared.
TEST(WsQuotaLedger, PoolMergeAcrossEntries)
{
    UploadQuotaManager mgr;
    const NodeHandle a = nh(0xA1);
    const NodeHandle b = nh(0xB2);

    // Both folders -> same pool key 1; reported balances 100 and 60.
    seed(mgr, WsTfsGroupBalances{{100, {a}}, {60, {b}}}, ownPool());

    // Conservative merge keeps the minimum reported balance for the pool.
    EXPECT_EQ(mgr.availableFor(a), 60LL);
    EXPECT_EQ(mgr.availableFor(b), 60LL);

    // A completion charged via folder A is visible through folder B (same pool).
    mgr.deductOnCompletion(a, 10);
    EXPECT_EQ(mgr.availableFor(a), 50LL);
    EXPECT_EQ(mgr.availableFor(b), 50LL);
}

// U7c — different pool keys stay independent; foreign flag is per pool.
TEST(WsQuotaLedger, ForeignPoolSeparation)
{
    UploadQuotaManager mgr;
    const NodeHandle own = nh(0xA1);
    const NodeHandle foreign = nh(0xB2);

    const Classifier byHandle = [own](NodeHandle h) -> std::pair<std::uint64_t, bool>
    {
        if (h == own)
            return std::make_pair(std::uint64_t(1), false);
        return std::make_pair(std::uint64_t(2), true); // foreign pool
    };

    seed(mgr, WsTfsGroupBalances{{100, {own}}, {200, {foreign}}}, byHandle);

    EXPECT_EQ(mgr.availableFor(own), 100LL);
    EXPECT_EQ(mgr.availableFor(foreign), 200LL);
    EXPECT_FALSE(mgr.isForeignGroup(own));
    EXPECT_TRUE(mgr.isForeignGroup(foreign));

    // Deducting one pool does not touch the other.
    mgr.deductOnCompletion(own, 40);
    EXPECT_EQ(mgr.availableFor(own), 60LL);
    EXPECT_EQ(mgr.availableFor(foreign), 200LL);
}

// U8 — per-upload individual-fit + deduct-on-completion-only.
TEST(WsQuotaLedger, PerUploadIndependentFitArithmetic)
{
    const m_off_t GB = m_off_t(1) << 30;
    const m_off_t tenGB = 10 * GB;
    const m_off_t sixGB = 6 * GB;

    UploadQuotaManager mgr;
    const NodeHandle a = nh(0xA1);
    seed(mgr, WsTfsGroupBalances{{tenGB, {a}}}, ownPool());

    // Both 6GB files fit individually BEFORE any completion (no reservation).
    EXPECT_GE(mgr.availableFor(a), sixGB); // file1 fits
    EXPECT_GE(mgr.availableFor(a), sixGB); // file2 also fits (individual-fit)

    // First file completes -> deduct on completion; only now does the pool drop.
    mgr.deductOnCompletion(a, sixGB);
    EXPECT_EQ(mgr.availableFor(a), tenGB - sixGB); // 4GB remain
    EXPECT_LT(mgr.availableFor(a), sixGB); // the second 6GB no longer fits
}

// U9 — deduct clamps at zero, never negative; further deducts stay clamped.
TEST(WsQuotaLedger, DeductClampsAtZero)
{
    UploadQuotaManager mgr;
    const NodeHandle a = nh(0xA1);
    seed(mgr, WsTfsGroupBalances{{100, {a}}}, ownPool());

    mgr.deductOnCompletion(a, 150); // more than remaining
    EXPECT_EQ(mgr.availableFor(a), 0LL);
    EXPECT_TRUE(mgr.hasBalanceFor(a)); // still a known (exhausted) pool

    mgr.deductOnCompletion(a, 10); // second deduct is harmless
    EXPECT_EQ(mgr.availableFor(a), 0LL);
}

// U10 — foreign flag from the classifier propagates to isForeignGroup().
TEST(WsQuotaLedger, ForeignGroupFlagPropagates)
{
    UploadQuotaManager mgr;
    const NodeHandle a = nh(0xA1);

    const Classifier foreignPool = [](NodeHandle) -> std::pair<std::uint64_t, bool>
    {
        return std::make_pair(std::uint64_t(7), true);
    };
    seed(mgr, WsTfsGroupBalances{{100, {a}}}, foreignPool);

    EXPECT_TRUE(mgr.isForeignGroup(a));
    EXPECT_EQ(mgr.availableFor(a), 100LL);
}

// U11 — applyGroups with a stale generation is rejected, state untouched.
TEST(WsQuotaLedger, StaleGenApplyRejected)
{
    UploadQuotaManager mgr;
    const NodeHandle a = nh(0xA1);

    const std::uint64_t gen1 = mgr.beginIssue(); // orphaned below
    mgr.invalidate(); // bumps gen, orphans gen1
    const std::uint64_t gen2 = mgr.beginIssue(); // current generation
    ASSERT_NE(gen1, gen2);

    // Apply the current generation with real balances.
    mgr.endIssue(gen2);
    EXPECT_TRUE(mgr.applyGroups(gen2, WsTfsGroupBalances{{200, {a}}}, ownPool()));
    EXPECT_EQ(mgr.availableFor(a), 200LL);

    // The stale generation must be rejected and must NOT overwrite state.
    EXPECT_FALSE(mgr.applyGroups(gen1, WsTfsGroupBalances{{999, {a}}}, ownPool()));
    EXPECT_EQ(mgr.availableFor(a), 200LL);
}

// U5 (ledger half) — duplicate handle across reply entries: first mapping wins.
TEST(WsQuotaLedger, DuplicateHandleFirstMappingWins)
{
    UploadQuotaManager mgr;
    const NodeHandle a = nh(0xA1);

    // Same folder in two entries, DIFFERENT pool keys -> the first entry's pool
    // owns the folder; the second mapping is dropped (a warning is logged).
    const Classifier perEntry = [](NodeHandle) -> std::pair<std::uint64_t, bool>
    {
        return std::make_pair(std::uint64_t(1), false);
    };
    // Two entries, same handle, balances 100 then 200; both classify to key 1 so
    // they MERGE (min) — the folder resolves to a single pool, min(100,200)=100.
    seed(mgr, WsTfsGroupBalances{{100, {a}}, {200, {a}}}, perEntry);
    EXPECT_TRUE(mgr.hasBalanceFor(a));
    EXPECT_EQ(mgr.availableFor(a), 100LL);
}

// U13 — issue lifecycle: dirty/in-flight interplay + stale-endIssue guard.
TEST(WsQuotaLedger, IssueLifecycle)
{
    UploadQuotaManager mgr;

    EXPECT_FALSE(mgr.shouldIssue()); // fresh: nothing dirty

    mgr.markDirty();
    EXPECT_TRUE(mgr.shouldIssue()); // dirty, not in-flight

    const std::uint64_t gen1 = mgr.beginIssue();
    EXPECT_FALSE(mgr.shouldIssue()); // in-flight
    EXPECT_EQ(gen1, 1u);

    mgr.endIssue(gen1);
    EXPECT_FALSE(mgr.shouldIssue()); // dirty was consumed by beginIssue

    // markDirty while a fresh issue is in flight: stays suppressed until endIssue.
    const std::uint64_t gen2 = mgr.beginIssue();
    EXPECT_FALSE(mgr.shouldIssue()); // in-flight
    mgr.markDirty();
    EXPECT_FALSE(mgr.shouldIssue()); // dirty set, but still in-flight
    mgr.endIssue(gen2);
    EXPECT_TRUE(mgr.shouldIssue()); // now dirty && !in-flight

    // Stale-endIssue guard: an endIssue for an orphaned generation must NOT clear
    // the newer issue's in-flight flag.
    UploadQuotaManager g;
    const std::uint64_t a = g.beginIssue();
    g.invalidate(); // orphans a, sets dirty
    const std::uint64_t b = g.beginIssue();
    ASSERT_NE(a, b);
    g.markDirty(); // set dirty so shouldIssue()==false isolates the in-flight bit
    EXPECT_FALSE(g.shouldIssue()); // in-flight for b
    g.endIssue(a); // STALE: must be a no-op
    EXPECT_FALSE(g.shouldIssue()); // STILL in-flight (dirty is true, so purely in-flight)
    g.endIssue(b); // clears in-flight for the current generation
    EXPECT_TRUE(g.shouldIssue()); // dirty && !in-flight
}

// U15 — applyGroups enforces the parser's fail-open contract at the LEDGER: an
//       entry with a negative balance, or an empty folder set, is skipped while
//       a valid sibling entry still applies. Folders of skipped entries report
//       no balance (hasBalanceFor()==false), so downstream fail-open never holds
//       them. This is defense-in-depth for the H2 hook (which injects groups
//       directly into applyGroups, bypassing the parser) and any future direct
//       caller: a negative remaining must never reach a pool.
TEST(WsQuotaLedger, ApplyGroupsSkipsMalformedEntries)
{
    UploadQuotaManager mgr;
    const NodeHandle good = nh(0xA1);
    const NodeHandle neg = nh(0xB2); // sits in a negative-balance entry -> skipped

    // Distinct pool key per folder, so a (wrongly) applied negative entry would
    // form its own pool with remaining < 0 rather than being masked by a merge.
    const Classifier perFolder = [](NodeHandle h) -> std::pair<std::uint64_t, bool>
    {
        return std::make_pair(static_cast<std::uint64_t>(h.as8byte()), false);
    };

    seed(mgr,
         WsTfsGroupBalances{
             {-5, {neg}}, // negative balance -> entry skipped (fail-open)
             {0, {}}, // empty folder set -> entry skipped (fail-open)
             {100, {good}}, // valid sibling -> applies
         },
         perFolder);

    // The valid sibling applied normally.
    EXPECT_TRUE(mgr.hasBalanceFor(good));
    EXPECT_EQ(mgr.availableFor(good), 100LL);

    // The negative-balance entry's folder was skipped: no pool, fail-open (a
    // wrongly-applied entry would give hasBalanceFor()==true, availableFor()==-5).
    EXPECT_FALSE(mgr.hasBalanceFor(neg));
    EXPECT_EQ(mgr.availableFor(neg), 0LL);

    // Only the good pool exists: unconstrained pre-accumulation. A negative
    // remaining slipping through would have flipped this to false.
    EXPECT_TRUE(mgr.unconstrained());
}

// PINS the parser's cursor contract: the standard Command reply dispatch
// (Request::process -> processCmdJSON) ALREADY enters the result array one level
// before calling a CmdArray procresult (Command::CmdArray == "an array, and we
// have already entered it"; confirmed against the raw 3-level "tfs" body
// [[[bytes,handle]]], and by the sibling command CommandUSCForWsUpload, which
// enters exactly ONE level per element). The parser therefore consumes groups
// from the pre-entered cursor directly; feeding the result array UN-entered must
// recover nothing. An early revision opened an extra array level and silently
// extracted zero groups under the real dispatch — this test trips loudly if the
// enter-depth ever drifts again in either direction.
TEST(WsQuota, ParserConsumesPreEnteredResultArray)
{
    const std::string result = R"([[3303903592448,"NjggVJ4b"]])";

    // As the standard Command dispatch hands it to a CmdArray procresult
    // (result array already entered once): one group extracted.
    const auto preentered = runTfsParse(result, /*preEnter*/ 1);
    ASSERT_TRUE(preentered.called);
    EXPECT_EQ(preentered.err, ::mega::API_OK);
    EXPECT_EQ(preentered.groups.size(), 1u);

    // Fed un-entered, the first enterarray() lands on the OUTER array, the sole
    // "group" then has no leading numeric balance, and fail-open skips it.
    const auto unentered = runTfsParse(result, /*preEnter*/ 0);
    ASSERT_TRUE(unentered.called);
    EXPECT_EQ(unentered.err, ::mega::API_OK);
    EXPECT_EQ(unentered.groups.size(), 0u);
}

// =========================================================================
// U12 — pure per-pool queue-fit arithmetic (computeWsQuotaQueueFit)
//
// The observational, app-facing "does the WS upload queue fit?" answer
// (SDK-6298 P5) that backs MegaApi::getWsUploadQueueQuotaFit. The client-side
// scan builds the per-pool (sum, remaining, foreign) vector; this pure reducer
// is the arithmetic core, unit-tested here without any MegaClient / harness.
// =========================================================================
TEST(WsQuota, QueueFitComputation)
{
    const m_off_t GB = m_off_t(1) << 30;

    // haveBalances=false => Unknown, regardless of any pools present.
    {
        const WsQuotaQueueFit r =
            computeWsQuotaQueueFit(false, {WsQuotaPoolFit{10 * GB, 1 * GB, false}});
        EXPECT_EQ(r.state, WsQuotaQueueFit::State::Unknown);
        EXPECT_EQ(r.shortfallBytes, 0LL);
        EXPECT_FALSE(r.foreignShortfall);
    }

    // haveBalances=true + no pools (empty/inbox-only queue) => trivially Fits.
    {
        const WsQuotaQueueFit r = computeWsQuotaQueueFit(true, {});
        EXPECT_EQ(r.state, WsQuotaQueueFit::State::Fits);
        EXPECT_EQ(r.shortfallBytes, 0LL);
        EXPECT_FALSE(r.foreignShortfall);
    }

    // Every pool has sum <= remaining (incl. the sum==remaining boundary) => Fits.
    {
        const WsQuotaQueueFit r = computeWsQuotaQueueFit(
            true,
            {WsQuotaPoolFit{3 * GB, 10 * GB, false}, WsQuotaPoolFit{5 * GB, 5 * GB, true}});
        EXPECT_EQ(r.state, WsQuotaQueueFit::State::Fits);
        EXPECT_EQ(r.shortfallBytes, 0LL);
        EXPECT_FALSE(r.foreignShortfall); // a FITTING foreign pool never flags foreign
    }

    // Single own (non-foreign) short pool => Shortfall; shortfall = sum - remaining.
    {
        const WsQuotaQueueFit r =
            computeWsQuotaQueueFit(true, {WsQuotaPoolFit{10 * GB, 4 * GB, false}});
        EXPECT_EQ(r.state, WsQuotaQueueFit::State::Shortfall);
        EXPECT_EQ(r.shortfallBytes, 6 * GB);
        EXPECT_FALSE(r.foreignShortfall);
    }

    // Multiple short pools: shortfallBytes sums across them; foreignShortfall is
    // true iff ANY short pool is foreign (a fitting foreign pool does not count).
    {
        const WsQuotaQueueFit r =
            computeWsQuotaQueueFit(true,
                                   {
                                       WsQuotaPoolFit{10 * GB, 4 * GB, false}, // short by 6, own
                                       WsQuotaPoolFit{8 * GB, 3 * GB, true}, // short by 5, foreign
                                       WsQuotaPoolFit{2 * GB, 9 * GB, true}, // fits (foreign)
                                   });
        EXPECT_EQ(r.state, WsQuotaQueueFit::State::Shortfall);
        EXPECT_EQ(r.shortfallBytes, 11 * GB); // 6 + 5
        EXPECT_TRUE(r.foreignShortfall); // the 8GB foreign pool is short
    }

    // A short pool that is foreign, with every other pool fitting => foreign flag set.
    {
        const WsQuotaQueueFit r =
            computeWsQuotaQueueFit(true,
                                   {
                                       WsQuotaPoolFit{1 * GB, 100 * GB, false}, // fits (own)
                                       WsQuotaPoolFit{7 * GB, 2 * GB, true}, // short by 5, foreign
                                   });
        EXPECT_EQ(r.state, WsQuotaQueueFit::State::Shortfall);
        EXPECT_EQ(r.shortfallBytes, 5 * GB);
        EXPECT_TRUE(r.foreignShortfall);
    }
}

// U16 — a multi-target transfer counts once per TARGET, not once per pool, and the
// documented `remaining >= outstanding` invariant survives its completion.
// (followup1 Goal-0 audit: outstanding used to dedup by pool while
// deductOnCompletion debited per File, so a same-pool 2-target transfer credited
// 1x and debited 2x — unconstrained() could read true for a pool about to be
// overshot, silently disarming the predictive hold.)
TEST(WsQuotaLedger, MultiTargetSamePoolCountsPerTarget)
{
    const m_off_t size = 100;
    UploadQuotaManager mgr;
    const NodeHandle a = nh(0xA1); // two own-account folders...
    const NodeHandle b = nh(0xB2); // ...that share ONE physical pool

    seed(mgr, WsTfsGroupBalances{{150, {a, b}}}, ownPool());
    EXPECT_TRUE(mgr.unconstrained()); // nothing queued yet

    // One transfer, two targets in the same pool: 2 nodes will be created, so the
    // pool must be charged 2 * size = 200 against remaining 150 -> constrained.
    mgr.beginOutstandingAccumulation();
    mgr.addOutstandingForTargets({a, b}, size);
    mgr.finishOutstandingAccumulation();
    EXPECT_FALSE(mgr.unconstrained())
        << "a 2-target transfer of 100 into a 150-byte pool must read as constrained";

    // Completion debits per File (2 x 100): remaining 150 -> clamped 0, and the
    // matching outstanding credit is fully consumed, so the invariant holds.
    mgr.deductOnCompletion(a, size);
    mgr.deductOnCompletion(b, size);
    EXPECT_EQ(mgr.availableFor(a), 0);
    EXPECT_EQ(mgr.availableFor(b), 0);
    mgr.beginOutstandingAccumulation();
    mgr.finishOutstandingAccumulation();
    EXPECT_TRUE(mgr.unconstrained()) // queue is empty again
        << "remaining >= outstanding must hold after the completion";

    // Control: two targets in DIFFERENT pools are charged size each, not 2x in one.
    UploadQuotaManager split;
    const Classifier byHandle = [](NodeHandle h) -> std::pair<std::uint64_t, bool>
    {
        return std::make_pair(h.as8byte(), false);
    };
    seed(split, WsTfsGroupBalances{{150, {a}}, {150, {b}}}, byHandle);
    split.beginOutstandingAccumulation();
    split.addOutstandingForTargets({a, b}, size);
    split.finishOutstandingAccumulation();
    EXPECT_TRUE(split.unconstrained())
        << "one target per pool at 100 into 150-byte pools must stay unconstrained";
}

#endif // MEGA_USE_WSUPLOAD
