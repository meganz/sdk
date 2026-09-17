/**
 * @file
 * @brief Tests for TransferQuotaChecker, the transfer-quota (download bandwidth) traffic light.
 */

#include "mega/command.h"
#include "mega/file.h"
#include "mega/megaapp.h"
#include "mega/megaclient.h"
#include "mega/transfer.h"
#include "utils.h"

#include <gtest/gtest.h>

using namespace mega;

namespace
{
TEST(TransferQuotaIsProAccount, OnlyPaidAllowancePlansCount)
{
    MegaApp app;
    auto client = mt::makeClient(app);
    const auto isPro = [&client](const AccountType level)
    {
        client->mMyAccount.setProLevel(level);
        return client->transferQuotaChecker().isProAccount();
    };

    // Free (and the not-yet-known state) has a per-IP quota, not an allowance
    EXPECT_FALSE(isPro(ACCOUNT_TYPE_UNKNOWN));
    EXPECT_FALSE(isPro(ACCOUNT_TYPE_FREE));

    // A feature-only plan (e.g. VPN/PWM) keeps the free per-IP quota, so it is not "pro" here
    EXPECT_FALSE(isPro(ACCOUNT_TYPE_FEATURE));

    // Paid plans with a fixed transfer allowance
    EXPECT_TRUE(isPro(ACCOUNT_TYPE_PROI));
    EXPECT_TRUE(isPro(ACCOUNT_TYPE_PROII));
    EXPECT_TRUE(isPro(ACCOUNT_TYPE_LITE));
    EXPECT_TRUE(isPro(ACCOUNT_TYPE_BUSINESS));
}

TEST(TransferQuotaGetProTransferState, ServesTheCacheWithoutTheBackend)
{
    MegaApp app;
    auto client = mt::makeClient(app);
    client->mMyAccount.setProLevel(ACCOUNT_TYPE_PROI);

    // Runs the completion synchronously with the cached snapshot and API_OK, if the cache is
    // primed.
    const auto readCache = [&client]()
    {
        TransferQuotaChecker::ProTransferQuota served;
        served.status = TRANSFER_QUOTA_RED; // a value the cases below never produce
        Error err = API_EINTERNAL;
        bool ran = false;
        client->transferQuotaChecker().getProTransferState(
            [&](const TransferQuotaChecker::ProTransferQuota& q, Error e)
            {
                served = q;
                err = e;
                ran = true;
            });
        return std::make_tuple(ran, served, err);
    };

    // A settled poll caches the status, then it is served locally without a round trip.
    AccountDetails orange;
    orange.transfer_max = 1000;
    orange.transfer_own_used = 850;
    orange.transfer_hist = {50, 50}; // 950/1000 -> 95% -> ORANGE
    client->transferQuotaChecker().cacheStatusFromDetails(orange);

    auto [ran, served, err] = readCache();
    EXPECT_TRUE(ran) << "a cache hit answers immediately";
    EXPECT_EQ(TRANSFER_QUOTA_ORANGE, served.status);
    EXPECT_EQ(API_OK, err);

    // The figures the light was derived from are served too, the per-IP window record by record: a
    // caller rebuilding the ratio from them must land on the same band, and see the same window
    // length the reply carried.
    EXPECT_EQ(1000, served.max);
    EXPECT_EQ(850, served.ownUsed);
    EXPECT_EQ(0, served.srvUsed);
    EXPECT_EQ(std::vector<m_off_t>({50, 50}), served.hist);
}

TEST(TransferQuotaGetProTransferState, RequeriesWhenCachedStatusIsUnknown)
{
    MegaApp app;
    auto client = mt::makeClient(app);
    client->mMyAccount.setProLevel(ACCOUNT_TYPE_PROI);

    // A poll that settled UNKNOWN (e.g. a reply with no allowance) is not a real answer, so a later
    // read must re-ask the backend rather than serve it. That query is async, so the completion
    // does not run synchronously here.
    AccountDetails noAllowance;
    noAllowance.transfer_max = 0;
    client->transferQuotaChecker().cacheStatusFromDetails(noAllowance);
    ASSERT_EQ(TRANSFER_QUOTA_UNKNOWN, client->transferQuotaChecker().status());

    bool ran = false;
    client->transferQuotaChecker().getProTransferState(
        [&](const TransferQuotaChecker::ProTransferQuota&, Error)
        {
            ran = true;
        });
    EXPECT_FALSE(ran)
        << "a cached UNKNOWN re-queries the backend instead of a synchronous cache hit";
}

TEST(TransferQuotaCheckProAccount, IgnoresBandwidthOverquota)
{
    MegaApp app;
    auto client = mt::makeClient(app);
    client->mMyAccount.setProLevel(ACCOUNT_TYPE_PROI);

    // Throttled right now, but a Pro account's light is the allowance ratio alone: a bandwidth
    // overquota is the free/feature path's concern, so nothing here reads overquotauntil.
    client->overquotauntil = Waiter::ds + 100;

    AccountDetails lowUsage;
    lowUsage.transfer_max = 1000;
    lowUsage.transfer_own_used = 500; // 50% -> GREEN
    client->transferQuotaChecker().cacheStatusFromDetails(lowUsage);
    ASSERT_EQ(TRANSFER_QUOTA_GREEN, client->transferQuotaChecker().status());

    // The periodic check does not override it either
    client->transferQuotaChecker().check({}, {}, {});
    EXPECT_EQ(TRANSFER_QUOTA_GREEN, client->transferQuotaChecker().status())
        << "an active overquota must not touch a Pro account's light";

    // And it is served as cached, without a round trip
    TransferQuotaChecker::ProTransferQuota served;
    Error err = API_EINTERNAL;
    bool ran = false;
    client->transferQuotaChecker().getProTransferState(
        [&](const TransferQuotaChecker::ProTransferQuota& q, Error e)
        {
            served = q;
            err = e;
            ran = true;
        });
    EXPECT_TRUE(ran) << "the cache is served, not re-queried, while throttled";
    EXPECT_EQ(TRANSFER_QUOTA_GREEN, served.status);
    EXPECT_EQ(1000, served.max);
    EXPECT_EQ(500, served.ownUsed);
    EXPECT_EQ(API_OK, err);
}

// Records what MegaClient pushes at the app, which is the only way a streaming read's verdict
// reaches one.
struct QuotaStateApp: MegaApp
{
    void notify_transfer_quota_state(int state) override
    {
        mStates.push_back(state);
    }

    std::vector<int> mStates;
};

// Fixture that queues download transfers and streaming reads, each naming a node, and cleans them
// up afterwards.
class DownloadProjectionTest: public ::testing::Test
{
protected:
    void TearDown() override
    {
#if defined(MEGA_MEASURE_CODE) || defined(DEBUG)
        // The installed hook captures a local of the test body, which is already gone
        client->reqs.deferRequests = nullptr;
#endif

        // Clear the deque first so ~Transfer's removetransfer() is a no-op. Transfers are deleted
        // before their files: ~Transfer walks its file list and nulls each file's back-pointer, so
        // the files must still be alive here, and ~File then skips its stopxfer() cleanup.
        client->transferlist.transfers[GET].clear();
        for (Transfer* t: mTransfers)
        {
            delete t;
        }
        for (File* f: mFiles)
        {
            delete f;
        }

        // ~DirectReadNode deletes its reads and erases itself from hdrns, so this empties the map
        while (!client->hdrns.empty())
        {
            delete client->hdrns.begin()->second;
        }
    }

    // Queue a download of the node `h` (own unless isPublic), of which `done` bytes are fetched.
    void queueDownload(const m_off_t size, const m_off_t done, const handle h, const bool isPublic)
    {
        Transfer* t = new Transfer(client.get(), GET);
        t->size = size;
        t->setProgresscompleted(done);
        t->tag = mNextTag++; // each transfer is a distinct consumption, keyed by its unique tag
        mTransfers.push_back(t);
        client->transferlist.transfers[GET].push_back(t);

        File* f = new File();
        f->transfer = t;
        f->h = NodeHandle().set6byte(h);
        f->hprivate = !isPublic; // a private handle is an own node, a public one is a link
        f->file_it = t->files.insert(t->files.end(), f);
        mFiles.push_back(f);
    }

    // Start streaming the node `h` of `size` bytes, reading `count` bytes from `offset` of which
    // `done` are fetched. Mirrors MegaClient::pread: the handle is type-tagged before it keys
    // hdrns.
    DirectRead* queueStream(const m_off_t size,
                            const m_off_t offset,
                            const m_off_t count,
                            const m_off_t done,
                            handle h,
                            const bool isPublic)
    {
        client->encodeHandleType(&h, isPublic);

        auto it = client->hdrns.find(h);
        if (it == client->hdrns.end())
        {
            SymmCipher cipher;
            it = client->hdrns.insert(client->hdrns.end(),
                                      std::make_pair(h,
                                                     new DirectReadNode(client.get(),
                                                                        h,
                                                                        isPublic,
                                                                        &cipher,
                                                                        0,
                                                                        nullptr,
                                                                        nullptr,
                                                                        nullptr)));
            it->second->hdrn_it = it;
            it->second->size = size;
        }

        // No tempurls, so the read only registers itself in drn->reads: nothing is dispatched
        DirectRead* dr = it->second->enqueue(offset, count, 0, {});
        dr->progress = done;
        return dr;
    }

    TransferQuotaChecker::DownloadProjection projection() const
    {
        TransferQuotaChecker::DownloadProjection p;
        client->transferQuotaChecker().projectStreamDownloads(p);
        client->transferQuotaChecker().projectTransferDownloads(p);
        return p;
    }

    QuotaStateApp app;
    std::shared_ptr<MegaClient> client = mt::makeClient(app);
    std::vector<Transfer*> mTransfers;
    std::vector<File*> mFiles;
    int mNextTag = 1;
};

TEST_F(DownloadProjectionTest, CountsEveryQueuedTransferAndWhatItHasFetched)
{
    const auto empty = projection();
    EXPECT_TRUE(empty.ownNodes.empty()) << "nothing queued";
    EXPECT_EQ(0, empty.transferred);

    // Each transfer contributes its handle, and what it has fetched is the `s` discount
    queueDownload(1000, 400, 1, false);
    const auto one = projection();
    ASSERT_EQ(1u, one.ownNodes.size());
    EXPECT_EQ(400, one.transferred) << "already-downloaded bytes are the discount for `s`";

    queueDownload(500, 0, 2, false);
    const auto twoQueued = projection();
    EXPECT_EQ(2u, twoQueued.ownNodes.size());
    EXPECT_EQ(400, twoQueued.transferred) << "a transfer with no progress discounts nothing";

    // Progress past the size is a state the engine detects and clamps when it next dispatches the
    // transfer. Until then it must not discount more than the node is worth.
    mTransfers[1]->setProgresscompleted(mTransfers[1]->size * 3);
    EXPECT_EQ(400 + 500, projection().transferred) << "progress past the size was not clamped";
    mTransfers[1]->setProgresscompleted(0);

    // The whole queue is counted, not just a window: add many more and every one contributes.
    constexpr unsigned extra = 500;
    for (unsigned i = 0; i < extra; ++i)
    {
        queueDownload(10, 5, static_cast<handle>(i + 3), false);
    }
    const auto whole = projection();
    EXPECT_EQ(2u + extra, whole.ownNodes.size());
    EXPECT_EQ(400 + 5 * extra, whole.transferred);
}

// The seam that captures queued commands only exists in these builds.
#if defined(MEGA_MEASURE_CODE) || defined(DEBUG)
TEST_F(DownloadProjectionTest, QueryNowCombinesNewNodesWithWhatIsAlreadyInFlight)
{
    // check() is what a starting download calls for itself. The nodes it names have fetched
    // nothing, so they add only their handles; what is already downloading or streaming is
    // projected as usual, and its fetched bytes come off the total as one adjustment.
    client->mMyAccount.setProLevel(ACCOUNT_TYPE_FREE);
    queueDownload(1000, 400, 1, false); // in flight, 400 already fetched
    queueStream(4000, 0, 1000, 100, 7, true); // a link being streamed, 100 fetched

    std::vector<Command*> queued;
    client->reqs.deferRequests = [&queued](Command* c)
    {
        queued.push_back(c);
        return true;
    };

    transferquotastatus_t reported = TRANSFER_QUOTA_UNKNOWN;
    Error err = API_EINTERNAL;
    const bool asked =
        client->transferQuotaChecker().check({2, 3},
                                             {4},
                                             [&reported, &err](transferquotastatus_t state, Error e)
                                             {
                                                 reported = state;
                                                 err = e;
                                             });

    ASSERT_TRUE(asked) << "a free account with nodes to ask about did not ask";
    ASSERT_EQ(1u, queued.size());
    ASSERT_NE(nullptr, dynamic_cast<CommandQueryTransferQuota*>(queued.front()));

    // The reply reaches the caller's completion, which is the whole delivery for a free account:
    // mStatus is the Pro snapshot's light and is left alone.
    JSON json;
    json.pos = "1]";
    queued.front()->procresult(Command::Result(Command::CmdItem), json);

    EXPECT_EQ(TRANSFER_QUOTA_ORANGE, reported) << "the caller was not given the state";
    EXPECT_EQ(API_OK, err) << "a positive code is a value, not an error";
    EXPECT_EQ(TRANSFER_QUOTA_UNKNOWN, client->transferQuotaChecker().status())
        << "a free verdict wrote the Pro snapshot's light";
}
#else
TEST_F(DownloadProjectionTest, QueryNowCombinesNewNodesWithWhatIsAlreadyInFlight)
{
    GTEST_SKIP() << "needs MegaClient::reqs.deferRequests, built only in Debug";
}
#endif

#if defined(MEGA_MEASURE_CODE) || defined(DEBUG)
TEST_F(DownloadProjectionTest, AFailedQueryIsReportedAsItsOwnError)
{
    // A negative code is the query failing, not a verdict. The state is undetermined either way, so
    // the error is the only thing that tells a caller which of the two happened.
    client->mMyAccount.setProLevel(ACCOUNT_TYPE_FREE);
    queueDownload(1000, 0, 1, false);

    std::vector<Command*> queued;
    client->reqs.deferRequests = [&queued](Command* c)
    {
        queued.push_back(c);
        return true;
    };

    transferquotastatus_t reported = TRANSFER_QUOTA_GREEN;
    Error err = API_OK;
    ASSERT_TRUE(
        client->transferQuotaChecker().check({},
                                             {},
                                             [&reported, &err](transferquotastatus_t state, Error e)
                                             {
                                                 reported = state;
                                                 err = e;
                                             }));
    ASSERT_EQ(1u, queued.size());

    JSON json;
    json.pos = "-2";
    queued.front()->procresult(Command::Result(Command::CmdError, API_EARGS), json);

    EXPECT_EQ(TRANSFER_QUOTA_UNKNOWN, reported) << "a failed query has no state to report";
    EXPECT_EQ(API_EARGS, err) << "the query's own error did not reach the caller";
}
#else
TEST_F(DownloadProjectionTest, AFailedQueryIsReportedAsItsOwnError)
{
    GTEST_SKIP() << "needs MegaClient::reqs.deferRequests, built only in Debug";
}
#endif

TEST_F(DownloadProjectionTest, CheckRunsNoCompletionWhenNoQbqGoesOut)
{
    int calls = 0;
    const auto completion = [&calls](transferquotastatus_t, Error)
    {
        ++calls;
    };

    // A Pro account refreshes its allowance snapshot instead of asking `qbq`, so the completion is
    // not part of that path even though the state was evaluated.
    client->mMyAccount.setProLevel(ACCOUNT_TYPE_PROI);
    queueDownload(1000, 0, 1, false);
    EXPECT_TRUE(client->transferQuotaChecker().check({2}, {}, completion))
        << "a Pro account did not evaluate its state";

    // A free account with no node named anywhere has nothing to ask about
    client->mMyAccount.setProLevel(ACCOUNT_TYPE_FREE);
    client->transferlist.transfers[GET].clear();
    EXPECT_FALSE(client->transferQuotaChecker().check({}, {}, completion))
        << "an empty projection was asked about";

    EXPECT_EQ(0, calls) << "the completion ran though no `qbq` went out";
}

#if defined(MEGA_MEASURE_CODE) || defined(DEBUG)
TEST_F(DownloadProjectionTest, QbqReplyAfterAnAccountTypeFlipIsDropped)
{
    client->mMyAccount.setProLevel(ACCOUNT_TYPE_FREE);
    queueDownload(1000, 0, 1, false);

    std::vector<Command*> queued;
    client->reqs.deferRequests = [&queued](Command* c)
    {
        queued.push_back(c);
        return true;
    };

    transferquotastatus_t reported = TRANSFER_QUOTA_GREEN;
    ASSERT_TRUE(client->transferQuotaChecker().check({},
                                                     {},
                                                     [&reported](transferquotastatus_t state, Error)
                                                     {
                                                         reported = state;
                                                     }));
    ASSERT_EQ(1u, queued.size());

    // The account gains an allowance before the reply lands
    client->mMyAccount.setProLevel(ACCOUNT_TYPE_PROI);

    // That reply describes a per-IP quota, so it must not settle a Pro account's light, and must
    // not raise the event the docs promise allowance accounts never get
    JSON json;
    json.pos = "3]";
    queued.front()->procresult(Command::Result(Command::CmdItem), json);

    EXPECT_EQ(TRANSFER_QUOTA_UNKNOWN, client->transferQuotaChecker().status())
        << "a `qbq` verdict settled a Pro account's light";
    EXPECT_EQ(TRANSFER_QUOTA_UNKNOWN, reported)
        << "the caller was handed a verdict that no longer applies";
}
#else
TEST_F(DownloadProjectionTest, QbqReplyAfterAnAccountTypeFlipIsDropped)
{
    GTEST_SKIP() << "needs MegaClient::reqs.deferRequests, built only in Debug";
}
#endif

#if defined(MEGA_MEASURE_CODE) || defined(DEBUG)
TEST_F(DownloadProjectionTest, AnUnsettledAccountTypeIsAskedAboutLikeAFreeOne)
{
    // Not logged in at all, or logged in with the plan not resolved yet: both read as
    // ACCOUNT_TYPE_UNKNOWN. A file- or folder-link download still spends the per-IP quota, which is
    // exactly what `qbq` answers about, so it must not be skipped.
    ASSERT_EQ(ACCOUNT_TYPE_UNKNOWN, client->mMyAccount.getProLevel());
    queueDownload(1000, 0, 1, true); // a public link, as an anonymous session would have

    std::vector<Command*> queued;
    client->reqs.deferRequests = [&queued](Command* c)
    {
        queued.push_back(c);
        return true;
    };

    EXPECT_TRUE(client->transferQuotaChecker().check({}, {}, {}))
        << "a link download on an unsettled account type did not ask `qbq`";
    ASSERT_EQ(1u, queued.size());
    EXPECT_NE(nullptr, dynamic_cast<CommandQueryTransferQuota*>(queued.front()));
}
#else
TEST_F(DownloadProjectionTest, AnUnsettledAccountTypeIsAskedAboutLikeAFreeOne)
{
    GTEST_SKIP() << "needs MegaClient::reqs.deferRequests, built only in Debug";
}
#endif

#if defined(MEGA_MEASURE_CODE) || defined(DEBUG)
TEST_F(DownloadProjectionTest, AShortStreamIsAskedAboutBeforeItsSizeArrives)
{
    // A stream queried the tick it starts has no size yet, so it projects no outstanding bytes. It
    // must still be asked about: `qbq` charges the node by handle, and a short read can be over
    // before anything else would ask, so skipping it means the app is never told about the stream.
    client->mMyAccount.setProLevel(ACCOUNT_TYPE_FREE);
    queueStream(0, 0, 1000, 0, 11, false);

    std::vector<Command*> queued;
    client->reqs.deferRequests = [&queued](Command* c)
    {
        queued.push_back(c);
        return true;
    };

    transferquotastatus_t reported = TRANSFER_QUOTA_UNKNOWN;
    ASSERT_TRUE(client->transferQuotaChecker().check({},
                                                     {},
                                                     [&reported](transferquotastatus_t state, Error)
                                                     {
                                                         reported = state;
                                                     }))
        << "a stream was not asked about before its size arrived";
    ASSERT_EQ(1u, queued.size());
    ASSERT_NE(nullptr, dynamic_cast<CommandQueryTransferQuota*>(queued.front()));

    // The verdict is delivered while the read is still in flight
    JSON json;
    json.pos = "3]";
    queued.front()->procresult(Command::Result(Command::CmdItem), json);

    EXPECT_EQ(TRANSFER_QUOTA_RED, reported)
        << "the caller was never told the quota state for the stream";
}
#else
TEST_F(DownloadProjectionTest, AShortStreamIsAskedAboutBeforeItsSizeArrives)
{
    GTEST_SKIP() << "needs MegaClient::reqs.deferRequests, built only in Debug";
}
#endif

TEST_F(DownloadProjectionTest, SplitsOwnFromPublicAndCountsEachTransfer)
{
    // Own and public nodes go into the two arrays `qbq` takes them in
    queueDownload(1000, 0, 1, false); // own
    queueDownload(2000, 0, 2, true); // public link

    const auto split = projection();
    ASSERT_EQ(1u, split.ownNodes.size());
    ASSERT_EQ(1u, split.publicNodes.size());
    EXPECT_EQ(1, split.ownNodes.front());
    EXPECT_EQ(2, split.publicNodes.front());

    // Each transfer is a distinct consumption keyed by its unique tag, so the same node named by
    // two transfers is listed once per transfer (not merged): `qbq` counts each occurrence.
    queueDownload(1000, 100, 1, false); // node 1 again, a further 100 fetched of it
    const auto perTransfer = projection();
    EXPECT_EQ(2u, perTransfer.ownNodes.size()) << "node 1 listed once per transfer";
    EXPECT_EQ(1, perTransfer.ownNodes[0]);
    EXPECT_EQ(1, perTransfer.ownNodes[1]);
    EXPECT_EQ(1u, perTransfer.publicNodes.size());
    EXPECT_EQ(100, perTransfer.transferred);
}

TEST_F(DownloadProjectionTest, ProjectsTheWholeStreamedNodeNotTheRequestedRange)
{
    // A short read of a big file: the projection deliberately charges the whole node, as a queued
    // download charges its whole file. `qbq` names nodes, and the requested range is not what will
    // ultimately be read — a player seeks on, so projecting the range would warn too late. Do not
    // "fix" this to dr->count without revisiting that trade.
    constexpr m_off_t nodeSize = 100'000;
    queueStream(nodeSize, 0, 1000, 0, 7, false);

    const auto oneRange = projection();
    ASSERT_EQ(1u, oneRange.ownNodes.size()) << "the whole node, not the range asked for";
    EXPECT_EQ(7, oneRange.ownNodes.front()) << "the bare handle, with the type tag stripped";
    EXPECT_EQ(0, oneRange.transferred);

    // Further reads of the same node do not multiply it: hdrns holds one DirectReadNode per node,
    // so the projection stays the node size however many ranges are in flight.
    queueStream(nodeSize, 50'000, 2000, 500, 7, false);
    const auto twoRanges = projection();
    EXPECT_EQ(1u, twoRanges.ownNodes.size()) << "still one node, however many ranges are open";
    EXPECT_EQ(500, twoRanges.transferred) << "the reads of that node are summed";

    // A public stream is a link, so it goes into the other array `qbq` takes
    queueStream(4000, 0, 4000, 0, 8, true);
    const auto withPublic = projection();
    ASSERT_EQ(1u, withPublic.publicNodes.size());
    EXPECT_EQ(8, withPublic.publicNodes.front());
}

TEST_F(DownloadProjectionTest, IgnoresAStreamedNodeWhoseReadsHaveFinished)
{
    // A DirectReadNode stays in hdrns until its scheduled expiry, minutes after its last read
    // completed, and each finished read is deleted with its progress. Projecting such a node would
    // charge the whole file again long after the playback that fetched it.
    constexpr m_off_t nodeSize = 100'000;
    DirectRead* read = queueStream(nodeSize, 0, 1000, 400, 9, false);

    const auto streaming = projection();
    ASSERT_EQ(1u, streaming.ownNodes.size()) << "a read in flight is projected";
    EXPECT_EQ(400, streaming.transferred);

    // The read finishes: it is deleted and leaves drn->reads, but the node stays in hdrns
    delete read;
    ASSERT_EQ(1u, client->hdrns.size()) << "the node outlives its reads";

    const auto idle = projection();
    EXPECT_TRUE(idle.ownNodes.empty()) << "nothing is pending on a node with no reads left";
    EXPECT_EQ(0, idle.transferred);
}

TEST_F(DownloadProjectionTest, AStreamIsProjectedBeforeItsSizeIsKnown)
{
    // DirectReadNode::size arrives with the `g` reply, so a read queued this tick has none. It
    // contributes no outstanding bytes yet, but its handle must still be projected: `qbq` charges
    // the node by handle regardless of what we know of its size.
    queueStream(0, 0, 1000, 0, 11, false);

    const auto unsized = projection();
    ASSERT_EQ(1u, unsized.ownNodes.size()) << "the node is still worth asking `qbq` about";
    EXPECT_EQ(11, unsized.ownNodes.front());
    EXPECT_EQ(0, unsized.transferred) << "nothing has been fetched of it yet";

    // The `g` reply lands and the size is known: an ordinary projection from there on
    client->hdrns.begin()->second->size = 4000;
    const auto sized = projection();
    ASSERT_EQ(1u, sized.ownNodes.size());
}

#if defined(MEGA_MEASURE_CODE) || defined(DEBUG)
TEST_F(DownloadProjectionTest, AStreamingReadAsksAboutItselfAndTellsTheApp)
{
    // MegaClient::queueread is where a streaming read enters the projection: it inserts the
    // DirectReadNode, asks about what is now in flight, and hands the verdict to the app as an
    // event. Driven through pread rather than the fixture's helper, which builds hdrns by hand.
    client->mMyAccount.setProLevel(ACCOUNT_TYPE_FREE);

    std::vector<Command*> queued;
    client->reqs.deferRequests = [&queued](Command* c)
    {
        queued.push_back(c);
        return true;
    };

    // The read dispatches a `g` of its own, so the quota query is not the only command deferred
    const auto quotaQuery = [&queued]() -> CommandQueryTransferQuota*
    {
        for (Command* c: queued)
        {
            if (auto* q = dynamic_cast<CommandQueryTransferQuota*>(c))
            {
                return q;
            }
        }
        return nullptr;
    };

    SymmCipher cipher;
    const auto read = [&client = client, &cipher](const m_off_t offset)
    {
        client->pread(
            7,
            &cipher,
            0 /*iv*/,
            offset,
            1000 /*count*/,
            [](DirectRead::CallbackParam&) {},
            true /*isPublicHandle*/);
    };

    read(0);

    CommandQueryTransferQuota* qbq = quotaQuery();
    ASSERT_NE(nullptr, qbq) << "a streaming read did not ask about the quota it is about to spend";

    JSON json;
    json.pos = "1]";
    qbq->procresult(Command::Result(Command::CmdItem), json);

    ASSERT_EQ(1u, app.mStates.size()) << "the verdict never reached the app";
    EXPECT_EQ(TRANSFER_QUOTA_ORANGE, app.mStates.front());

    // A second read of the same node joins the DirectReadNode already in hdrns, so it is not a new
    // consumption: only a node's first read asks.
    queued.clear();
    read(1000);
    EXPECT_EQ(nullptr, quotaQuery()) << "a further read of the same node asked again";
    EXPECT_EQ(1u, app.mStates.size()) << "the app was told twice about one node";
}
#else
TEST_F(DownloadProjectionTest, AStreamingReadAsksAboutItselfAndTellsTheApp)
{
    GTEST_SKIP() << "needs MegaClient::reqs.deferRequests, built only in Debug";
}
#endif

TEST(TransferQuotaStatusForUsage, TrafficLightBands)
{
    constexpr m_off_t allowance = 1000;
    const auto stateFor = [](const m_off_t used, const m_off_t max)
    {
        return TransferQuotaChecker::statusForUsage(used, max);
    };

    EXPECT_EQ(TRANSFER_QUOTA_GREEN, stateFor(0, allowance));
    EXPECT_EQ(TRANSFER_QUOTA_GREEN, stateFor(899, allowance)); // 89.9%
    EXPECT_EQ(TRANSFER_QUOTA_ORANGE, stateFor(900, allowance)); // 90%, the warning threshold
    EXPECT_EQ(TRANSFER_QUOTA_ORANGE, stateFor(990, allowance)); // 99% exactly, the last orange
    // Not floored, so any part of the last percent is already red
    EXPECT_EQ(TRANSFER_QUOTA_RED, stateFor(9901, 10 * allowance)); // 99.01%
    EXPECT_EQ(TRANSFER_QUOTA_RED, stateFor(995, allowance)); // 99.5%
    EXPECT_EQ(TRANSFER_QUOTA_RED, stateFor(allowance, allowance));

    // The estimate can exceed the allowance between fetches; it saturates rather than wrapping
    EXPECT_EQ(TRANSFER_QUOTA_RED, stateFor(10 * allowance, allowance));

    // No allowance configured (free accounts): no ratio describes a per-IP quota
    EXPECT_EQ(TRANSFER_QUOTA_UNKNOWN, stateFor(0, 0));
    EXPECT_EQ(TRANSFER_QUOTA_UNKNOWN, stateFor(10 * allowance, 0));
    EXPECT_EQ(TRANSFER_QUOTA_UNKNOWN, stateFor(0, -1)); // never fetched
}

TEST(TransferQuotaStatusForUsage, DetailsAnswerByRatioAlone)
{
    AccountDetails details;
    details.transfer_max = 1000;
    details.transfer_own_used = 500; // 50%
    ASSERT_EQ(TRANSFER_QUOTA_GREEN, TransferQuotaChecker::statusForUsage(details));

    // Served bytes count towards the ratio, as they do for the engine's own light
    details.transfer_srv_used = 300; // 800/1000 -> 80%
    EXPECT_EQ(TRANSFER_QUOTA_GREEN, TransferQuotaChecker::statusForUsage(details));

    // So does the per-IP history
    details.transfer_hist = {100}; // 900/1000 -> 90%
    EXPECT_EQ(TRANSFER_QUOTA_ORANGE, TransferQuotaChecker::statusForUsage(details));

    details.transfer_hist = {200}; // 1000/1000
    EXPECT_EQ(TRANSFER_QUOTA_RED, TransferQuotaChecker::statusForUsage(details));
}

TEST(TransferQuotaStatusForApiCode, ApiScaleIsMappedToTrafficLight)
{
    const auto stateFor = [](const int code)
    {
        return TransferQuotaChecker::statusForApiCode(code);
    };

    EXPECT_EQ(TRANSFER_QUOTA_GREEN, stateFor(0));
    EXPECT_EQ(TRANSFER_QUOTA_ORANGE, stateFor(1)); // unregistered-user allowance nearly exhausted
    EXPECT_EQ(TRANSFER_QUOTA_ORANGE, stateFor(2)); // registered-user one nearly exhausted
    EXPECT_EQ(TRANSFER_QUOTA_RED, stateFor(3)); // exceeded

    // Anything else is an error code, so the state could not be determined
    EXPECT_EQ(TRANSFER_QUOTA_UNKNOWN, stateFor(API_EINTERNAL));
    EXPECT_EQ(TRANSFER_QUOTA_UNKNOWN, stateFor(API_EOVERQUOTA));
    EXPECT_EQ(TRANSFER_QUOTA_UNKNOWN, stateFor(4));
}

} // namespace
