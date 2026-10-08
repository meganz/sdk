/**
 * @file SqliteDateSectionsPerf_test.cpp
 * @brief Performance tests for the date-section + timestamp-anchor queries (SDK-6171, SDK-6582).
 *
 * Separate from SqliteNodesPerf_test.cpp because that fixture packs every node
 * into one date bucket, so it can't exercise the date-granularity code paths.
 *
 * Every dataset (DateSpreadDataset) uses pseudo-random handles and is committed
 * after populating, as a real client is after fetchnodes.
 *
 * - Phases 1, 3, 5 and the tz sweep: 100K files over 730 days (SetUp ~30 s per test).
 * - Phase 2 (SqliteNodesPerfGroupByDateTest): six 1M-2M shapes x 9 cells, one
 *   dataset per shape. Takes about 2.5 hours with the per-candidate up-walk SQL and
 *   about 20 minutes with the per-parent one, on a desktop Release build.
 * - Phase 4 (cache effects): 5K files.
 *
 * All tests are DISABLED_*; CI skips them. Run a Release build with:
 *   ./tests/unit/test_unit --gtest_also_run_disabled_tests \
 *       --gtest_filter='*SqliteDateSections*:*GroupByDate*:*ListAllByAnchor*:*CacheEffects*'
 *
 * Numbers are logged, not asserted. Results, method and history:
 * tests/unit/SqliteDateSectionsPerf_baseline.md. Compare only on the SAME machine.
 *
 * This file holds several suites sharing DateSpreadDataset; the one-suite-per-file
 * rule (tests/README.md) is relaxed to avoid duplicating the dataset builder.
 */

#include "utils.h"

#include <gtest/gtest.h>
#include <mega/db/sqlite.h>
#include <mega/megaapp.h>
#include <mega/megaclient.h>
#include <mega/nodemanager.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <mega.h>
#include <optional>
#include <sstream>
#include <stdfs.h>
#include <string>
#include <vector>

#ifdef USE_SQLITE

namespace fs = std::filesystem;

using namespace mega;

namespace
{

// ─── Shared iteration / timing helpers (duplicated from SqliteNodesPerf_test ─
// to avoid a cross-file shared header for just two consumers; ~15 lines) ─────

constexpr int SIMPLE_ITERS = 1000;
constexpr int COMPLEX_ITERS = 100;

template<typename Fn>
long long measureUs(int iters, Fn&& fn)
{
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i)
        fn();
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
}

// ─── Dataset dimensions (large fixture) ─────────────────────────────────────
// Tunable as constexpr so a future regression-bisect can scale down quickly.

constexpr int64_t kBaseEpoch = 1700000000LL; // 2023-11-14 22:13:20 UTC
// numDays is intentionally coprime with the mime-distribution period (10) so
// (day % numDays, idx % 10) walks every combination — otherwise every day
// would have a fixed mime and `ALL_VISUAL_MEDIA` would cover only ~60% of
// days, defeating the perf test's goal of many distinct buckets.
constexpr int kLargeNumDays = 731; // 2 years + 1 day; coprime w/ 10 (731 = 17*43)
constexpr int kLargeNumFiles = 100'000;
constexpr int kLargeNumTopFolders = 10;
constexpr int kLargeNumSubPerTop = 10; // → 100 sub-folders
constexpr int kIntraDaySeconds = 86400 / 137; // ~630 s; spreads ~137 files / day

// ─── Smaller dataset for the cache-effects fixture ──────────────────────────
// Phase 4 needs to rebuild SqliteAccountState per iter to measure cold
// prepare cost. At 100K nodes a rebuild takes ~30s; at 5K it's < 0.5s,
// making 30-50 iters practical.

constexpr int kSmallNumDays = 30;
constexpr int kSmallNumFiles = 5'000;
constexpr int kSmallNumTopFolders = 5;
constexpr int kSmallNumSubPerTop = 5;
// ~2618 s per file. The intra-day step times the round count overflows a day, so
// late-round files spill a few days forward — actual bucket count slightly exceeds
// kSmallNumDays. Harmless here (Phase 4 asserts no bucket count); don't add one
// without snapping mtimes to day boundaries first.
constexpr int kSmallIntraDaySeconds = 86400 / 33;

// ─── Common dataset-build helpers ───────────────────────────────────────────

struct DateSpreadParams
{
    int numDays;
    int numFiles;
    int numTopFolders;
    int numSubPerTop;
    int intraDaySeconds;
    int rubbishRatioDenom; // numFiles / rubbishRatioDenom rubbish files
    int extraDepth = 0; // folders chained between the root and the top folders
    int videoEvery = 0; // > 0: about one video every videoEvery files, the rest documents
    int sensitiveSubFolderEvery = 0; // > 0: every Nth sub-folder is marked sensitive
};

constexpr DateSpreadParams kLargeParams{kLargeNumDays,
                                        kLargeNumFiles,
                                        kLargeNumTopFolders,
                                        kLargeNumSubPerTop,
                                        kIntraDaySeconds,
                                        10};

constexpr DateSpreadParams kSmallParams{kSmallNumDays,
                                        kSmallNumFiles,
                                        kSmallNumTopFolders,
                                        kSmallNumSubPerTop,
                                        kSmallIntraDaySeconds,
                                        10};

// ─── Dataset ────────────────────────────────────────────────────────────────
// One populated, committed date-spread DB and the handles the tests address. Not copyable (its
// MegaClient keeps a reference to mApp): hold it by unique_ptr.

struct DateSpreadDataset
{
    mega::MegaApp mApp;
    NodeManager::MissingParentNodes mMissingParentNodes;
    fs::path mDir;
    std::shared_ptr<MegaClient> mClient;
    uint64_t mNextHandle = 1;

    NodeHandle mRootHandle;
    std::vector<NodeHandle> mTopFolderHandles;
    std::vector<NodeHandle> mSubFolderHandles;
    std::vector<NodeHandle> mFileHandles;

    // Bucket-bound tracking, so anchor tests can pick "first" / "mid" / "last" bucket mtimes.
    int64_t mFirstBucketStartSec = 0;
    int64_t mLastBucketStartSec = 0;
    int64_t mMidBucketStartSec = 0;

    DateSpreadDataset(const DateSpreadParams& p, fs::path dir):
        mDir(std::move(dir))
    {
        fs::remove_all(mDir);
        fs::create_directories(mDir);

        auto* dbAccess = new SqliteDbAccess(LocalPath::fromAbsolutePath(path_u8string(mDir)));
        mClient = mt::makeClient(mApp, dbAccess);
        mClient->sid =
            "AWA5YAbtb4JO-y2zWxmKZpSe5-6XM7CTEkA-3Nv7J4byQUpOazdfSC1ZUFlS-kah76gPKUEkTF9g7MeE";
        mClient->opensctable();

        populate(p);

        if (auto* sa = dynamic_cast<SqliteAccountState*>(mClient->sctable.get()))
            sa->createIndexes(/*enableSearch=*/true, /*enableLexi=*/true);

        // opensctable() leaves a transaction open, and a real client commits it after
        // fetchnodes (MegaClient::sc_purgeAndCommit). Without this, every query reads the
        // uncommitted pages out of a multi-GB WAL (seen: 4 KB main file, 1.5 GB WAL).
        mClient->sctable->commit();
        mClient->sctable->begin();
        logDbFileSizes();
    }

    ~DateSpreadDataset()
    {
        mClient.reset(); // release the DB (and its WAL) before removing the dir
        std::error_code ec;
        fs::remove_all(mDir, ec);
    }

    DateSpreadDataset(const DateSpreadDataset&) = delete;
    DateSpreadDataset& operator=(const DateSpreadDataset&) = delete;

    DBTableNodes* nodesTable() const
    {
        return dynamic_cast<DBTableNodes*>(mClient->sctable.get());
    }

private:
    // ── Node factory (trimmed copy of SqliteNodesPerfTest's; no fav/label) ──
    std::shared_ptr<Node> addNode(nodetype_t nodeType,
                                  std::shared_ptr<Node> parent,
                                  const std::string& name,
                                  m_time_t mtime = 0,
                                  bool sensitive = false)
    {
        // Real accounts have random handles; sequential ones would give the table a locality real
        // DBs lack. An odd multiplier mod 2^48 is a bijection: handles stay unique, and never 0.
        constexpr uint64_t kHandleMixer = 0x9E3779B97F4Bull;
        const uint64_t seq = mNextHandle++;
        const NodeHandle handle = NodeHandle().set6byte((seq * kHandleMixer) & 0xFFFFFFFFFFFFull);
        assert(!handle.isUndef());
        auto node = mt::makeNode(*mClient, nodeType, handle, parent.get());

        static const nameid nameId = AttrMap::string2nameid("n");
        node->attrs.map[nameId] = name;
        if (sensitive)
            node->attrs.map[AttrMap::string2nameid("sen")] = "1";

        if (nodeType == FILENODE)
        {
            node->size = static_cast<m_off_t>(seq * 512);
            node->ctime = static_cast<m_time_t>(kBaseEpoch + static_cast<int64_t>(seq));
            node->mtime = mtime;
            node->crc[0] = static_cast<int32_t>(seq);
            node->crc[1] = static_cast<int32_t>(seq >> 8u);
            node->crc[2] = static_cast<int32_t>(seq >> 16u);
            node->crc[3] = static_cast<int32_t>(seq >> 24u);
            node->isvalid = true;
            node->serializefingerprint(&node->attrs.map['c']);
            node->setfingerprint();

            NodeCounter nc;
            nc.files = 1;
            nc.storage = node->size;
            node->setCounter(nc);
        }

        mClient->mNodeManager.addNode(node,
                                      /*notify=*/false,
                                      /*isFetching=*/true,
                                      mMissingParentNodes);
        mClient->mNodeManager.saveNodeInDb(node.get());
        return node;
    }

    void populate(const DateSpreadParams& p)
    {
        auto rootNode = addNode(ROOTNODE, nullptr, "ROOT");
        mRootHandle = rootNode->nodeHandle();
        addNode(VAULTNODE, nullptr, "VAULT");
        auto rubbishNode = addNode(RUBBISHNODE, nullptr, "RUBBISH");

        std::shared_ptr<Node> topParent = rootNode;
        for (int d = 0; d < p.extraDepth; ++d)
            topParent = addNode(FOLDERNODE, topParent, "Chain_" + std::to_string(d));

        std::vector<std::shared_ptr<Node>> subFolders;
        subFolders.reserve(static_cast<size_t>(p.numTopFolders * p.numSubPerTop));

        for (int i = 0; i < p.numTopFolders; ++i)
        {
            auto topFolder = addNode(FOLDERNODE, topParent, "Folder_" + std::to_string(i));
            mTopFolderHandles.push_back(topFolder->nodeHandle());

            for (int j = 0; j < p.numSubPerTop; ++j)
            {
                const bool sensitive =
                    p.sensitiveSubFolderEvery > 0 &&
                    subFolders.size() % static_cast<size_t>(p.sensitiveSubFolderEvery) == 0;
                auto subFolder = addNode(FOLDERNODE,
                                         topFolder,
                                         "SubFolder_" + std::to_string(i) + "_" + std::to_string(j),
                                         /*mtime=*/0,
                                         sensitive);
                mSubFolderHandles.push_back(subFolder->nodeHandle());
                subFolders.push_back(subFolder);
            }
        }

        // Spread numFiles uniformly across numDays: dayOffset = idx % numDays
        // (1 file/day/round), and the round number sets the in-day offset.
        // Yields numDays distinct Day buckets, ceil(numFiles/numDays) max each.
        const int filesPerSub = p.numFiles / static_cast<int>(subFolders.size());
        int globalFileIdx = 0;
        for (size_t s = 0; s < subFolders.size(); ++s)
        {
            for (int k = 0; k < filesPerSub; ++k, ++globalFileIdx)
            {
                const int dayOffset = globalFileIdx % p.numDays;
                const int intraDayRound = globalFileIdx / p.numDays;
                const m_time_t mtime = kBaseEpoch + static_cast<m_time_t>(dayOffset) * 86400LL +
                                       static_cast<m_time_t>(intraDayRound) * p.intraDaySeconds;

                // Same MIME mix as SqliteNodesPerf: 50% jpg / 10% mp4 / 40% txt
                const char* ext = (globalFileIdx % 10 < 5)  ? ".jpg" :
                                  (globalFileIdx % 10 == 5) ? ".mp4" :
                                                              ".txt";
                // Sparse shape: place videos by a hash of the index; a plain modulo put every
                // video in one of the every-Nth sensitive sub-folders.
                if (p.videoEvery > 0)
                    ext = ((static_cast<uint64_t>(globalFileIdx) * 0x9E3779B97F4A7C15ull >> 32) %
                               static_cast<uint64_t>(p.videoEvery) ==
                           0) ?
                              ".mp4" :
                              ".txt";
                std::string fname = "f_" + std::to_string(s) + "_" + std::to_string(k) + ext;
                auto file = addNode(FILENODE, subFolders[s], fname, mtime);
                mFileHandles.push_back(file->nodeHandle());
            }
        }

        // Day-bucket boundary mtimes for the byTimestampAnchor tests. Snapped
        // to midnight UTC to match production bounds (strftime '%Y-%m-%d' →
        // 00:00:00 UTC); kBaseEpoch is not midnight-aligned by itself.
        constexpr int64_t kSecondsPerDay = 86400LL;
        const int64_t midnightUtc = (kBaseEpoch / kSecondsPerDay) * kSecondsPerDay;
        mFirstBucketStartSec = midnightUtc;
        mLastBucketStartSec = midnightUtc + static_cast<int64_t>(p.numDays - 1) * kSecondsPerDay;
        mMidBucketStartSec = midnightUtc + static_cast<int64_t>(p.numDays / 2) * kSecondsPerDay;

        // Rubbish files: same mime distribution, separate root.
        const int rubbishCount = p.numFiles / p.rubbishRatioDenom;
        for (int k = 0; k < rubbishCount; ++k)
        {
            const int dayOffset = k % p.numDays;
            const m_time_t mtime = kBaseEpoch + static_cast<m_time_t>(dayOffset) * 86400LL +
                                   static_cast<m_time_t>(k / p.numDays) * p.intraDaySeconds;
            const char* ext = (k % 10 < 5) ? ".jpg" : (k % 10 == 5) ? ".mp4" : ".txt";
            std::string fname = "rubbish_" + std::to_string(k) + ext;
            addNode(FILENODE, rubbishNode, fname, mtime);
        }
    }

    // Evidence for the commit above: after it, the data sits in the main file.
    void logDbFileSizes() const
    {
        uintmax_t dbBytes = 0;
        uintmax_t walBytes = 0;
        std::error_code ec;
        for (const auto& e: fs::directory_iterator(mDir, ec))
        {
            const std::string name = e.path().filename().string();
            if (name.find("statecache") == std::string::npos ||
                name.find("prefs") != std::string::npos)
                continue;
            if (name.size() > 3 && name.compare(name.size() - 3, 3, ".db") == 0)
                dbBytes += e.file_size(ec);
            else if (name.size() > 4 && name.compare(name.size() - 4, 4, "-wal") == 0)
                walBytes += e.file_size(ec);
        }
        GTEST_LOG_(INFO) << "date-spread DB committed: db=" << (dbBytes >> 20)
                         << " MB, wal=" << (walBytes >> 20) << " MB";
    }
};

// ─── Common fixture base ────────────────────────────────────────────────────
// Subclasses pick a dataset scale via params(); SetUp() builds a fresh dataset per test.

class DateSpreadFixtureBase: public ::testing::Test
{
protected:
    std::unique_ptr<DateSpreadDataset> mDataset;

    // Copies of the dataset's client and handles, under the names the tests use.
    std::shared_ptr<MegaClient> mClient;
    NodeHandle mRootHandle;
    std::vector<NodeHandle> mTopFolderHandles;
    std::vector<NodeHandle> mSubFolderHandles;
    std::vector<NodeHandle> mFileHandles;
    int64_t mFirstBucketStartSec = 0;
    int64_t mLastBucketStartSec = 0;
    int64_t mMidBucketStartSec = 0;

    virtual const DateSpreadParams& params() const = 0;
    virtual const char* fixtureName() const = 0;

    void SetUp() override
    {
        mDataset =
            std::make_unique<DateSpreadDataset>(params(), fs::current_path() / fixtureName());
        mClient = mDataset->mClient;
        mRootHandle = mDataset->mRootHandle;
        mTopFolderHandles = mDataset->mTopFolderHandles;
        mSubFolderHandles = mDataset->mSubFolderHandles;
        mFileHandles = mDataset->mFileHandles;
        mFirstBucketStartSec = mDataset->mFirstBucketStartSec;
        mLastBucketStartSec = mDataset->mLastBucketStartSec;
        mMidBucketStartSec = mDataset->mMidBucketStartSec;
    }

    void TearDown() override
    {
        mClient.reset(); // drop our copy first, so the dataset really closes the DB
        mDataset.reset();
    }

    DBTableNodes* nodesTable()
    {
        return mDataset->nodesTable();
    }
};

// ═══════════════════════════════════════════════════════════════════════════
//  Large fixture (100K nodes / 730 days) — used by Phases 1, 3, 5 and the tz sweep
// ═══════════════════════════════════════════════════════════════════════════

class SqliteDateSectionsPerfFixture: public DateSpreadFixtureBase
{
protected:
    const DateSpreadParams& params() const override
    {
        return kLargeParams;
    }

    const char* fixtureName() const override
    {
        return "sqlite_date_sections_perf";
    }
};

using DISABLED_SqliteDateSectionsPerfTest = SqliteDateSectionsPerfFixture;

// ─── Phase 1: dataset sanity ────────────────────────────────────────────────

TEST_F(DISABLED_SqliteDateSectionsPerfTest, DateSpreadSanity)
{
    // Confirm the fixture produced exactly kLargeNumDays distinct Day buckets,
    // 24 Month buckets (≈ 2 years), and 2-3 Year buckets.
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);

    DateSectionParams sp;
    sp.mimeType = MIME_TYPE_ALL_VISUAL_MEDIA;
    sp.order = OrderByClause::MTIME_DESC;

    const std::vector<NodeHandle> roots{mRootHandle};

    for (auto gran:
         {DateSectionGranularity::Day, DateSectionGranularity::Month, DateSectionGranularity::Year})
    {
        std::vector<DateSection> sections;
        CancelToken ct;
        sp.granularity = gran;
        ASSERT_TRUE(table->groupAllNodesByDate(sp, roots, sections, ct));

        size_t totalCount = 0;
        for (const auto& s: sections)
            totalCount += static_cast<size_t>(s.mCount);

        const char* name = gran == DateSectionGranularity::Day ?
                               "Day" :
                               (gran == DateSectionGranularity::Month ? "Month" : "Year");

        GTEST_LOG_(INFO) << "Sanity " << name << ": sections=" << sections.size()
                         << ", total nodes counted=" << totalCount;

        // Range assertions, not exact: the bucket count varies with the
        // mimetypeVirtual classifier. We only need "many buckets" (vs the old
        // fixture's "1") to confirm the spread is meaningful.
        if (gran == DateSectionGranularity::Day)
        {
            EXPECT_GE(sections.size(), 700u); // ~all 731 days for coprime numDays
        }
        if (gran == DateSectionGranularity::Month)
        {
            EXPECT_GE(sections.size(), 23u); // 731 days ≈ 24 months
            EXPECT_LE(sections.size(), 26u);
        }
        if (gran == DateSectionGranularity::Year)
        {
            EXPECT_LE(sections.size(), 3u);
            EXPECT_GE(sections.size(), 2u);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Phase 2: groupAllNodesByDate at 1M–2M nodes
// ═══════════════════════════════════════════════════════════════════════════
//
// One dataset per shape, shared by its cells (a 2M populate takes ~70 s); params are grouped by
// shape so only one dataset is alive at a time. Each cell logs its first run, the median of 3
// more and a section checksum, which an SQL change must not alter.

static const char* mimeName(MimeType_t m)
{
    switch (m)
    {
        case MIME_TYPE_VIDEO:
            return "VIDEO";
        case MIME_TYPE_ALL_VISUAL_MEDIA:
            return "ALL_VISUAL_MEDIA";
        case MIME_TYPE_ALL_DOCS:
            return "ALL_DOCS";
        default:
            return "OTHER";
    }
}

static const char* granName(DateSectionGranularity g)
{
    switch (g)
    {
        case DateSectionGranularity::Day:
            return "Day";
        case DateSectionGranularity::Month:
            return "Month";
        case DateSectionGranularity::Year:
            return "Year";
    }
    return "?";
}

constexpr int kShapeDays = 3653; // ~10 years; coprime with the 10-file mime period

constexpr int intraDaySecondsFor(int numFiles)
{
    return std::max(1, 86400 / std::max(1, numFiles / kShapeDays));
}

struct GroupByDateShape
{
    const char* name;
    DateSpreadParams params;
};

// clang-format off
const GroupByDateShape kGroupByDateShapes[] = {
    {"1M-5Kfolders",        {.numDays = kShapeDays, .numFiles = 1'000'000, .numTopFolders = 50,  .numSubPerTop = 100,  .intraDaySeconds = intraDaySecondsFor(1'000'000), .rubbishRatioDenom = 10, .sensitiveSubFolderEvery = 100}},
    {"1M-200Kfolders",      {.numDays = kShapeDays, .numFiles = 1'000'000, .numTopFolders = 200, .numSubPerTop = 1000, .intraDaySeconds = intraDaySecondsFor(1'000'000), .rubbishRatioDenom = 10, .sensitiveSubFolderEvery = 100}},
    {"2M-5Kfolders",        {.numDays = kShapeDays, .numFiles = 2'000'000, .numTopFolders = 50,  .numSubPerTop = 100,  .intraDaySeconds = intraDaySecondsFor(2'000'000), .rubbishRatioDenom = 10, .sensitiveSubFolderEvery = 100}},
    {"2M-200Kfolders",      {.numDays = kShapeDays, .numFiles = 2'000'000, .numTopFolders = 200, .numSubPerTop = 1000, .intraDaySeconds = intraDaySecondsFor(2'000'000), .rubbishRatioDenom = 10, .sensitiveSubFolderEvery = 100}},
    {"2M-200Kfolders-depth8", {.numDays = kShapeDays, .numFiles = 2'000'000, .numTopFolders = 200, .numSubPerTop = 1000, .intraDaySeconds = intraDaySecondsFor(2'000'000), .rubbishRatioDenom = 10, .extraDepth = 5, .sensitiveSubFolderEvery = 100}},
    {"1M-200Kfolders-video0.1pct", {.numDays = kShapeDays, .numFiles = 1'000'000, .numTopFolders = 200, .numSubPerTop = 1000, .intraDaySeconds = intraDaySecondsFor(1'000'000), .rubbishRatioDenom = 10, .videoEvery = 1000, .sensitiveSubFolderEvery = 100}},
};
// clang-format on

enum class GroupByDateScope
{
    Root,
    SubFolderAncestor,
    ExcludeTopFolderHideSensitive,
};

static const char* scopeName(GroupByDateScope s)
{
    switch (s)
    {
        case GroupByDateScope::Root:
            return "root";
        case GroupByDateScope::SubFolderAncestor:
            return "sub-folder";
        case GroupByDateScope::ExcludeTopFolderHideSensitive:
            return "exclude+hide-sensitive";
    }
    return "?";
}

static const char* groupOrderName(int order)
{
    return order == OrderByClause::MEDIATS_DESC ? "MEDIATS_DESC" : "MTIME_DESC";
}

struct GroupByDateParam
{
    const GroupByDateShape* shape;
    MimeType_t mimeType;
    int order;
    GroupByDateScope scope;
    DateSectionGranularity granularity;
};

std::vector<GroupByDateParam> makeGroupByDateParams()
{
    std::vector<GroupByDateParam> params;
    for (const auto& shape: kGroupByDateShapes)
    {
        for (const int order: {OrderByClause::MTIME_DESC, OrderByClause::MEDIATS_DESC})
        {
            params.push_back({&shape,
                              MIME_TYPE_VIDEO,
                              order,
                              GroupByDateScope::Root,
                              DateSectionGranularity::Month});
            for (const auto scope: {GroupByDateScope::Root,
                                    GroupByDateScope::SubFolderAncestor,
                                    GroupByDateScope::ExcludeTopFolderHideSensitive})
                params.push_back({&shape,
                                  MIME_TYPE_ALL_VISUAL_MEDIA,
                                  order,
                                  scope,
                                  DateSectionGranularity::Month});
        }
        // Day gives the most (parent, bucket) rows per parent, i.e. the largest `candidates`.
        params.push_back({&shape,
                          MIME_TYPE_ALL_VISUAL_MEDIA,
                          OrderByClause::MEDIATS_DESC,
                          GroupByDateScope::Root,
                          DateSectionGranularity::Day});
    }
    return params;
}

// FNV-1a over every field of every section: equal checksums mean equal section lists.
uint64_t sectionsChecksum(const std::vector<DateSection>& sections)
{
    uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](const void* data, size_t len)
    {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < len; ++i)
        {
            h ^= bytes[i];
            h *= 1099511628211ull;
        }
    };
    for (const auto& s: sections)
    {
        mix(s.mGroupId.data(), s.mGroupId.size());
        mix(&s.mStartDate, sizeof s.mStartDate);
        mix(&s.mEndDate, sizeof s.mEndDate);
        mix(&s.mCount, sizeof s.mCount);
    }
    return h;
}

class SqliteNodesPerfGroupByDateTest: public ::testing::TestWithParam<GroupByDateParam>
{
protected:
    static inline std::unique_ptr<DateSpreadDataset> sDataset;
    static inline const GroupByDateShape* sShape = nullptr;

    void SetUp() override
    {
        const GroupByDateShape* shape = GetParam().shape;
        if (sShape == shape)
            return;
        sDataset.reset(); // one dataset in memory at a time
        sShape = nullptr;
        sDataset =
            std::make_unique<DateSpreadDataset>(shape->params,
                                                fs::current_path() / "sqlite_group_by_date_perf");
        sShape = shape;
    }

    static void TearDownTestSuite()
    {
        sDataset.reset();
        sShape = nullptr;
    }
};

TEST_P(SqliteNodesPerfGroupByDateTest, DISABLED_Perf)
{
    ASSERT_NE(sDataset, nullptr);
    const auto& p = GetParam();
    const DateSpreadDataset& d = *sDataset;

    DateSectionParams sp;
    sp.mimeType = p.mimeType;
    sp.order = p.order;
    sp.granularity = p.granularity;
    std::vector<NodeHandle> roots{d.mRootHandle};
    switch (p.scope)
    {
        case GroupByDateScope::Root:
            break;
        case GroupByDateScope::SubFolderAncestor:
            roots = {d.mSubFolderHandles.front()};
            break;
        case GroupByDateScope::ExcludeTopFolderHideSensitive:
            sp.excludeHandles = {d.mTopFolderHandles.at(1)};
            sp.excludeSensitive = true;
            break;
    }

    std::vector<DateSection> sections;
    const auto run = [&]
    {
        sections.clear();
        CancelToken ct;
        ASSERT_TRUE(d.nodesTable()->groupAllNodesByDate(sp, roots, sections, ct));
    };

    const long long firstUs = measureUs(1, run);
    std::array<long long, 3> us{};
    for (auto& t: us)
        t = measureUs(1, run);
    std::sort(us.begin(), us.end());
    if (HasFatalFailure())
        return; // the ASSERT in `run` only left the lambda; don't log a bogus timing

    int64_t matched = 0;
    for (const auto& s: sections)
        matched += s.mCount;

    GTEST_LOG_(INFO) << "groupAllNodesByDate [shape=" << p.shape->name
                     << ", mime=" << mimeName(p.mimeType) << ", order=" << groupOrderName(p.order)
                     << ", scope=" << scopeName(p.scope) << ", gran=" << granName(p.granularity)
                     << "]: first " << firstUs / 1000 << " ms, median " << us[1] / 1000
                     << " ms, sections=" << sections.size() << ", matched=" << matched
                     << ", checksum=0x" << std::hex << sectionsChecksum(sections) << std::dec;
}

INSTANTIATE_TEST_SUITE_P(All,
                         SqliteNodesPerfGroupByDateTest,
                         ::testing::ValuesIn(makeGroupByDateParams()));

// ═══════════════════════════════════════════════════════════════════════════
//  Phase 3: listAllNodesByPage + byTimestampAnchor parametric perf
// ═══════════════════════════════════════════════════════════════════════════
//
// Query-plan note: no post-populate ANALYZE (would need a production API
// change). SQLite defaults + createIndexes() match what production sees on a
// fresh DB.

enum class AnchorPosition
{
    None,
    First, // anchor at first bucket — DESC anchor returns ~all rows
    Mid, // anchor at mid bucket — DESC anchor returns ~half
    Last, // anchor at last bucket — DESC anchor returns ~few
};

struct ListAllAnchorParam
{
    MimeType_t mimeType;
    int order; // OrderByClause value
    AnchorPosition anchor;
    bool hasCursor;
};

class SqliteNodesPerfListAllByAnchorTest:
    public SqliteDateSectionsPerfFixture,
    public ::testing::WithParamInterface<ListAllAnchorParam>
{};

static const char* anchorName(AnchorPosition a)
{
    switch (a)
    {
        case AnchorPosition::None:
            return "None";
        case AnchorPosition::First:
            return "First";
        case AnchorPosition::Mid:
            return "Mid";
        case AnchorPosition::Last:
            return "Last";
    }
    return "?";
}

static const char* orderName(int o)
{
    switch (o)
    {
        case OrderByClause::MTIME_ASC:
            return "MTIME_ASC";
        case OrderByClause::MTIME_DESC:
            return "MTIME_DESC";
        case OrderByClause::SIZE_DESC:
            return "SIZE_DESC";
        default:
            return "OTHER";
    }
}

TEST_P(SqliteNodesPerfListAllByAnchorTest, DISABLED_Perf)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);

    const auto& p = GetParam();
    constexpr size_t pageSize = 50;

    ListAllNodesParams lparams;
    lparams.mimeType = p.mimeType;
    lparams.order = p.order;
    lparams.maxElements = pageSize;
    lparams.excludeSensitive = false;

    // Anchor selection — picks a bucket-start mtime; the half-bound's
    // selectivity is determined by the bucket position + anchor direction.
    if (p.anchor != AnchorPosition::None)
    {
        TimestampAnchorFilter anchor;
        anchor.mOrder = p.order; // anchor direction follows page order
        switch (p.anchor)
        {
            case AnchorPosition::First:
                anchor.mStartSeconds = mFirstBucketStartSec;
                anchor.mEndSeconds = mFirstBucketStartSec + 86400;
                break;
            case AnchorPosition::Mid:
                anchor.mStartSeconds = mMidBucketStartSec;
                anchor.mEndSeconds = mMidBucketStartSec + 86400;
                break;
            case AnchorPosition::Last:
                anchor.mStartSeconds = mLastBucketStartSec;
                anchor.mEndSeconds = mLastBucketStartSec + 86400;
                break;
            case AnchorPosition::None:
                break;
        }
        lparams.timestampAnchor = anchor;
    }

    const std::vector<NodeHandle> roots{mRootHandle};

    // Build a cursor anchored mid-way for the hasCursor variants.
    if (p.hasCursor)
    {
        NodeSearchCursorOffset c;
        c.mLastHandle = mFileHandles[mFileHandles.size() / 2].as8byte();
        c.mLastName = "f_50_500.jpg"; // approximate; cursor just needs a row past which to walk
        if (p.order == OrderByClause::MTIME_ASC || p.order == OrderByClause::MTIME_DESC)
            c.mLastMtime = mMidBucketStartSec;
        lparams.cursor = c;
    }

    // Warm prepare.
    {
        std::vector<std::pair<NodeHandle, NodeSerialized>> warm;
        CancelToken ct;
        table->listAllNodesByPage(lparams, roots, warm, ct);
    }

    size_t lastResultSize = 0;
    const long long us = measureUs(COMPLEX_ITERS,
                                   [&]
                                   {
                                       std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
                                       CancelToken ct;
                                       table->listAllNodesByPage(lparams, roots, nodes, ct);
                                       lastResultSize = nodes.size();
                                   });

    GTEST_LOG_(INFO) << "listAllByPage [mime=" << mimeName(p.mimeType)
                     << ", order=" << orderName(p.order) << ", anchor=" << anchorName(p.anchor)
                     << ", cursor=" << (p.hasCursor ? "1" : "0") << ", page=" << pageSize
                     << ", returned=" << lastResultSize << "]: " << COMPLEX_ITERS
                     << " iters, total " << us << " us, avg " << us / COMPLEX_ITERS << " us/iter";
}

// clang-format off
static const ListAllAnchorParam kListAllAnchorParams[] = {
    // 16 cells without cursor: 2 mime × 2 order × 4 anchor positions
    {MIME_TYPE_VIDEO,             OrderByClause::MTIME_DESC, AnchorPosition::None,  false},
    {MIME_TYPE_VIDEO,             OrderByClause::MTIME_DESC, AnchorPosition::First, false},
    {MIME_TYPE_VIDEO,             OrderByClause::MTIME_DESC, AnchorPosition::Mid,   false},
    {MIME_TYPE_VIDEO,             OrderByClause::MTIME_DESC, AnchorPosition::Last,  false},
    {MIME_TYPE_VIDEO,             OrderByClause::MTIME_ASC,  AnchorPosition::None,  false},
    {MIME_TYPE_VIDEO,             OrderByClause::MTIME_ASC,  AnchorPosition::First, false},
    {MIME_TYPE_VIDEO,             OrderByClause::MTIME_ASC,  AnchorPosition::Mid,   false},
    {MIME_TYPE_VIDEO,             OrderByClause::MTIME_ASC,  AnchorPosition::Last,  false},
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::MTIME_DESC, AnchorPosition::None,  false},
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::MTIME_DESC, AnchorPosition::First, false},
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::MTIME_DESC, AnchorPosition::Mid,   false},
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::MTIME_DESC, AnchorPosition::Last,  false},
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::MTIME_ASC,  AnchorPosition::None,  false},
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::MTIME_ASC,  AnchorPosition::First, false},
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::MTIME_ASC,  AnchorPosition::Mid,   false},
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::MTIME_ASC,  AnchorPosition::Last,  false},
    // 4 cursor spot checks at Anchor=Mid (hardest planner case)
    {MIME_TYPE_VIDEO,             OrderByClause::MTIME_DESC, AnchorPosition::Mid,   true},
    {MIME_TYPE_VIDEO,             OrderByClause::MTIME_ASC,  AnchorPosition::Mid,   true},
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::MTIME_DESC, AnchorPosition::Mid,   true},
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::MTIME_ASC,  AnchorPosition::Mid,   true},
};
// clang-format on

INSTANTIATE_TEST_SUITE_P(All,
                         SqliteNodesPerfListAllByAnchorTest,
                         ::testing::ValuesIn(kListAllAnchorParams));

// ═══════════════════════════════════════════════════════════════════════════
//  Phase 4: Prepared-statement cache effects (smaller fixture)
// ═══════════════════════════════════════════════════════════════════════════
//
// Uses kSmallParams (5000 nodes / 30 days) so the per-iter rebuild of
// SqliteAccountState stays cheap (<0.5s vs ~30s at 100K), allowing ~30-50 iters.
//
// "Cold prepare" measures the first query against a freshly-rebuilt state: a
// cache-miss sqlite3_prepare_v2 plus the first exec. The client rebuild + DB open
// happen in SetUp(), OUTSIDE the timed window — only the first prepare+exec is timed.

class SqliteCacheEffectsPerfFixture: public DateSpreadFixtureBase
{
protected:
    const DateSpreadParams& params() const override
    {
        return kSmallParams;
    }

    const char* fixtureName() const override
    {
        return "sqlite_cache_effects_perf";
    }
};

using DISABLED_SqliteCacheEffectsPerfTest = SqliteCacheEffectsPerfFixture;

TEST_F(DISABLED_SqliteCacheEffectsPerfTest, GroupByDate_WarmReuse)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);

    DateSectionParams sp;
    sp.mimeType = MIME_TYPE_ALL_VISUAL_MEDIA;
    sp.order = OrderByClause::MTIME_DESC;
    sp.granularity = DateSectionGranularity::Day;

    const std::vector<NodeHandle> roots{mRootHandle};

    {
        std::vector<DateSection> warm;
        CancelToken ct;
        table->groupAllNodesByDate(sp, roots, warm, ct);
    }

    const long long us = measureUs(SIMPLE_ITERS,
                                   [&]
                                   {
                                       std::vector<DateSection> sections;
                                       CancelToken ct;
                                       table->groupAllNodesByDate(sp, roots, sections, ct);
                                   });

    GTEST_LOG_(INFO) << "GroupByDate_WarmReuse [" << kSmallNumFiles << " nodes / " << kSmallNumDays
                     << " days, Day granularity]: " << SIMPLE_ITERS << " iters, total " << us
                     << " us, avg " << us / SIMPLE_ITERS << " us/iter";
}

TEST_F(DISABLED_SqliteCacheEffectsPerfTest, GroupByDate_ColdPrepareViaRebuild)
{
    // Cold prepare = full first-call cost (client rebuild + DB open + first prepare).
    constexpr int kColdIters = 30;
    long long totalUs = 0;

    for (int iter = 0; iter < kColdIters; ++iter)
    {
        TearDown();
        SetUp();

        auto* table = nodesTable();
        ASSERT_NE(table, nullptr);

        DateSectionParams sp;
        sp.mimeType = MIME_TYPE_ALL_VISUAL_MEDIA;
        sp.order = OrderByClause::MTIME_DESC;
        sp.granularity = DateSectionGranularity::Day;
        const std::vector<NodeHandle> roots{mRootHandle};

        std::vector<DateSection> sections;
        CancelToken ct;
        auto t0 = std::chrono::steady_clock::now();
        table->groupAllNodesByDate(sp, roots, sections, ct);
        auto t1 = std::chrono::steady_clock::now();
        totalUs += std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    }

    GTEST_LOG_(INFO) << "GroupByDate_ColdPrepareViaRebuild [" << kSmallNumFiles << " nodes / "
                     << kSmallNumDays << " days, Day granularity]: " << kColdIters
                     << " full-rebuild iters, avg cold first-call " << totalUs / kColdIters
                     << " us/iter (includes SqliteAccountState ctor + DB open + prepare)";
}

TEST_F(DISABLED_SqliteCacheEffectsPerfTest, GroupByDate_ShapeSwapBothCached)
{
    // Alternates between two distinct cache keys (different granularities). Both
    // shapes are pre-warmed below, so the timed loop measures the cost of
    // alternating between two CACHED statements (cache hits + lookup), NOT a miss.
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);

    DateSectionParams day;
    day.mimeType = MIME_TYPE_ALL_VISUAL_MEDIA;
    day.order = OrderByClause::MTIME_DESC;
    day.granularity = DateSectionGranularity::Day;

    DateSectionParams month = day;
    month.granularity = DateSectionGranularity::Month;

    const std::vector<NodeHandle> roots{mRootHandle};

    // Warm prepare; the timed loop measures execution only.
    {
        std::vector<DateSection> s;
        CancelToken ct;
        table->groupAllNodesByDate(day, roots, s, ct);
        table->groupAllNodesByDate(month, roots, s, ct);
    }

    const long long us = measureUs(SIMPLE_ITERS,
                                   [&]
                                   {
                                       std::vector<DateSection> sections;
                                       CancelToken ct;
                                       table->groupAllNodesByDate(day, roots, sections, ct);
                                       table->groupAllNodesByDate(month, roots, sections, ct);
                                   });

    GTEST_LOG_(INFO) << "GroupByDate_ShapeSwapBothCached [Day↔Month alternating, both pre-warmed]: "
                     << SIMPLE_ITERS << " swap pairs, total " << us << " us, avg "
                     << us / (SIMPLE_ITERS * 2) << " us per single-shape call";
}

TEST_F(DISABLED_SqliteCacheEffectsPerfTest, GroupByDate_MapGrowthObservation)
{
    // Observational: logs distinct-shape count; no assertion.
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);

    const std::vector<NodeHandle> roots1{mRootHandle};
    std::vector<NodeHandle> roots2{mRootHandle};
    if (!mTopFolderHandles.empty())
        roots2.push_back(mTopFolderHandles[0]);

    int shapesIssued = 0;
    for (auto mime: {MIME_TYPE_VIDEO, MIME_TYPE_ALL_VISUAL_MEDIA, MIME_TYPE_ALL_DOCS})
        for (auto gran: {DateSectionGranularity::Day,
                         DateSectionGranularity::Month,
                         DateSectionGranularity::Year})
            for (bool sens: {false, true})
            {
                DateSectionParams sp;
                sp.mimeType = mime;
                sp.order = OrderByClause::MTIME_DESC;
                sp.granularity = gran;
                sp.excludeSensitive = sens;

                std::vector<DateSection> sections;
                CancelToken ct;
                table->groupAllNodesByDate(sp, roots1, sections, ct);
                table->groupAllNodesByDate(sp, roots2, sections, ct);
                shapesIssued += 2;
            }

    GTEST_LOG_(INFO) << "GroupByDate_MapGrowthObservation: issued " << shapesIssued
                     << " distinct shape calls (mime × gran × sens × roots). "
                     << "mStmtDateSections map size is private; reviewer should sample with a "
                     << "debugger if exact value matters. Each cached entry holds one "
                     << "sqlite3_stmt* (~few KB SQLite-internal). At ~50 entries the "
                     << "total memory is small but unbounded growth in a long-running client "
                     << "could matter — track separately if customer-facing concern surfaces.";
}

// ═══════════════════════════════════════════════════════════════════════════
//  Phase 5: offset-depth cost (real query latency vs offset)
// ═══════════════════════════════════════════════════════════════════════════
//
// Arm A: global offset depth curve (no anchor).
//   Sweeps offset values {0, 100, 1000, 10000, 90000} with a fixed page of
//   60 photos ordered MTIME_DESC against the full 100K-node dataset.
//   Expected: latency grows linearly with offset (O(offset) SQLite OFFSET).
//
// Arm B: anchored deep jump.
//   Fixes offset=100 and varies the timestamp anchor across early/mid/late day
//   buckets, using bounds from DateSpreadFixtureBase::populateDB:
//     mFirstBucketStartSec / mMidBucketStartSec / mLastBucketStartSec = midnight
//     UTC of day 0 / numDays/2 (~365) / numDays-1 (~730); each window is one day.
//   Expected: the anchor collapses an otherwise-deep seek (oldest bucket) to
//   offset=100 within one day — O(1 bucket), independent of bucket depth.
//
// Arm C: keyset (cursor) page cost at the same depths as Arm A.
//   Positions a cursor at each depth and times one page; expected to stay flat
//   (index seek), the direct contrast to Arm A's O(offset) growth.

class SqliteDateSectionsPerfPhase5Fixture: public SqliteDateSectionsPerfFixture
{};

using DISABLED_Offset_GlobalDepthCurve = SqliteDateSectionsPerfPhase5Fixture;
using DISABLED_Offset_AnchoredDeepJump = SqliteDateSectionsPerfPhase5Fixture;
using DISABLED_Offset_CursorDepthCurve = SqliteDateSectionsPerfPhase5Fixture;

TEST_F(DISABLED_Offset_GlobalDepthCurve, GlobalOffsetDepthCurve)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);

    const std::vector<NodeHandle> roots{mRootHandle};
    constexpr size_t pageSize = 60;

    // Each offset is measured once (not averaged): this is a single-shot
    // cost probe, not a steady-state benchmark.  The DB call itself is what
    // is timed — no volatile sink, no dead-code elimination concern.
    for (const int64_t offset: {0LL, 100LL, 1000LL, 10000LL, 90000LL})
    {
        ListAllNodesParams lparams;
        lparams.mimeType = MIME_TYPE_PHOTO;
        lparams.order = OrderByClause::MTIME_DESC;
        lparams.maxElements = pageSize;
        lparams.excludeSensitive = false;
        lparams.offset = offset;

        // Warm prepare on offset=0 so statement caching doesn't inflate the
        // first probe (only relevant for the very first iteration).
        if (offset == 0)
        {
            std::vector<std::pair<NodeHandle, NodeSerialized>> warm;
            CancelToken ct;
            table->listAllNodesByPage(lparams, roots, warm, ct);
        }

        std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
        CancelToken ct;
        const auto t0 = std::chrono::steady_clock::now();
        table->listAllNodesByPage(lparams, roots, nodes, ct);
        const auto t1 = std::chrono::steady_clock::now();
        const long long us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

        GTEST_LOG_(INFO) << "GlobalOffsetDepthCurve [offset=" << offset
                         << ", returned=" << nodes.size() << "]: " << us << " us";
    }
}

TEST_F(DISABLED_Offset_AnchoredDeepJump, AnchoredDeepJump)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);

    const std::vector<NodeHandle> roots{mRootHandle};
    constexpr size_t pageSize = 60;
    constexpr int64_t kFixedOffset = 100;
    constexpr int64_t kOneDaySecs = 86400LL;

    // Anchor positions: early (day 0), mid (~day 365), late (day 730).
    // Bucket bounds are snapped-to-midnight values computed in populateDB.
    struct AnchorCase
    {
        const char* label;
        int64_t startSec;
    };

    const AnchorCase cases[] = {
        {"early(day0)", mFirstBucketStartSec},
        {"mid(day~365)", mMidBucketStartSec},
        {"late(day730)", mLastBucketStartSec},
    };

    // Warm prepare (no anchor, no offset) so the prepared-statement cache is
    // populated before we enter the timed section.
    {
        ListAllNodesParams warm;
        warm.mimeType = MIME_TYPE_PHOTO;
        warm.order = OrderByClause::MTIME_DESC;
        warm.maxElements = pageSize;
        std::vector<std::pair<NodeHandle, NodeSerialized>> warmOut;
        CancelToken ct;
        table->listAllNodesByPage(warm, roots, warmOut, ct);
    }

    for (const auto& c: cases)
    {
        ListAllNodesParams lparams;
        lparams.mimeType = MIME_TYPE_PHOTO;
        lparams.order = OrderByClause::MTIME_DESC;
        lparams.maxElements = pageSize;
        lparams.excludeSensitive = false;
        lparams.offset = kFixedOffset;

        TimestampAnchorFilter anchor;
        anchor.mOrder = OrderByClause::MTIME_DESC;
        anchor.mStartSeconds = c.startSec;
        anchor.mEndSeconds = c.startSec + kOneDaySecs;
        lparams.timestampAnchor = anchor;

        std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
        CancelToken ct;
        const auto t0 = std::chrono::steady_clock::now();
        table->listAllNodesByPage(lparams, roots, nodes, ct);
        const auto t1 = std::chrono::steady_clock::now();
        const long long us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

        GTEST_LOG_(INFO) << "AnchoredDeepJump [anchor=" << c.label << ", offset=" << kFixedOffset
                         << ", returned=" << nodes.size() << "]: " << us << " us";
    }
}

// Arm C: keyset (cursor) page cost at the same depths as Arm A. A cursor seeks via
// index to its position and reads pageSize rows, so the page cost should stay flat
// regardless of depth — the contrast against Arm A's O(offset) growth.
TEST_F(DISABLED_Offset_CursorDepthCurve, CursorDepthCurve)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);

    const std::vector<NodeHandle> roots{mRootHandle};
    constexpr size_t pageSize = 60;

    auto fetch = [&](const ListAllNodesParams& p)
    {
        std::vector<std::pair<NodeHandle, NodeSerialized>> out;
        CancelToken ct;
        table->listAllNodesByPage(p, roots, out, ct);
        return out;
    };

    // Warm prepare (first page, no cursor).
    {
        ListAllNodesParams warm;
        warm.mimeType = MIME_TYPE_PHOTO;
        warm.order = OrderByClause::MTIME_DESC;
        warm.maxElements = pageSize;
        fetch(warm);
    }

    for (const int64_t depth: {0LL, 100LL, 1000LL, 10000LL})
    {
        ListAllNodesParams lparams;
        lparams.mimeType = MIME_TYPE_PHOTO;
        lparams.order = OrderByClause::MTIME_DESC;
        lparams.maxElements = pageSize;

        // Untimed setup: position a keyset cursor just before `depth` by finding the
        // node at rank depth-1 (offset probe) and building a cursor from it. Only the
        // cursor fetch below is timed.
        if (depth > 0)
        {
            ListAllNodesParams probe;
            probe.mimeType = MIME_TYPE_PHOTO;
            probe.order = OrderByClause::MTIME_DESC;
            probe.maxElements = 1;
            probe.offset = depth - 1;
            const auto boundary = fetch(probe);
            ASSERT_EQ(boundary.size(), 1u) << "depth=" << depth;

            const auto n = mClient->mNodeManager.getNodeByHandle(boundary.front().first);
            ASSERT_NE(n, nullptr);

            NodeSearchCursorOffset c;
            c.mLastName = n->displayname();
            c.mLastHandle = n->nodeHandle().as8byte();
            c.mLastMtime = n->mtime;
            lparams.cursor = c;
        }

        std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
        CancelToken ct;
        const auto t0 = std::chrono::steady_clock::now();
        table->listAllNodesByPage(lparams, roots, nodes, ct);
        const auto t1 = std::chrono::steady_clock::now();
        const long long us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

        GTEST_LOG_(INFO) << "CursorDepthCurve [depth=" << depth << ", returned=" << nodes.size()
                         << "]: " << us << " us";
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Phase 6: UTC-offset latency (offset must not shift the query plan)
// ═══════════════════════════════════════════════════════════════════════════
//
// The tz offset is a BOUND parameter, so the SQL text — hence the plan and the
// prepared-statement cache — is identical across offsets and latency should be
// flat. This sweep guards against a future change embedding it as a literal,
// which would fragment the cache and make these cells diverge.

struct TzSweepParam
{
    int64_t tzOffsetSeconds;
    const char* label;
    DateSectionGranularity granularity;
};

class SqliteNodesPerfTzSweepTest:
    public SqliteDateSectionsPerfFixture,
    public ::testing::WithParamInterface<TzSweepParam>
{};

TEST_P(SqliteNodesPerfTzSweepTest, DISABLED_Perf)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);

    const auto& tp = GetParam();

    DateSectionParams sp;
    sp.mimeType = MIME_TYPE_ALL_VISUAL_MEDIA;
    sp.order = OrderByClause::MTIME_DESC;
    sp.granularity = tp.granularity;
    sp.tzOffsetSeconds = tp.tzOffsetSeconds;

    const std::vector<NodeHandle> roots{mRootHandle};

    // Warm prepare; the timed loop measures execution only.
    {
        std::vector<DateSection> warm;
        CancelToken ct;
        table->groupAllNodesByDate(sp, roots, warm, ct);
    }

    size_t sectionCount = 0;
    const long long us = measureUs(COMPLEX_ITERS,
                                   [&]
                                   {
                                       std::vector<DateSection> sections;
                                       CancelToken ct;
                                       table->groupAllNodesByDate(sp, roots, sections, ct);
                                       sectionCount = sections.size();
                                   });

    GTEST_LOG_(INFO) << "groupAllNodesByDate tz-sweep [tz=" << tp.label
                     << ", gran=" << granName(tp.granularity) << ", sections=" << sectionCount
                     << "]: " << COMPLEX_ITERS << " iters, avg " << us / COMPLEX_ITERS
                     << " us/iter";
}

// clang-format off
// Full 4 offsets × 3 granularities = 12 cells.
static const TzSweepParam kTzSweepParams[] = {
    {0LL,                "UTC",    DateSectionGranularity::Day},
    {9 * 3600LL,         "+09:00", DateSectionGranularity::Day},
    {-(5 * 3600 + 1800), "-05:30", DateSectionGranularity::Day},
    {14 * 3600LL,        "+14:00", DateSectionGranularity::Day},
    {0LL,                "UTC",    DateSectionGranularity::Month},
    {9 * 3600LL,         "+09:00", DateSectionGranularity::Month},
    {-(5 * 3600 + 1800), "-05:30", DateSectionGranularity::Month},
    {14 * 3600LL,        "+14:00", DateSectionGranularity::Month},
    {0LL,                "UTC",    DateSectionGranularity::Year},
    {9 * 3600LL,         "+09:00", DateSectionGranularity::Year},
    {-(5 * 3600 + 1800), "-05:30", DateSectionGranularity::Year},
    {14 * 3600LL,        "+14:00", DateSectionGranularity::Year},
};
// clang-format on

INSTANTIATE_TEST_SUITE_P(All, SqliteNodesPerfTzSweepTest, ::testing::ValuesIn(kTzSweepParams));

} // anonymous namespace

#endif // USE_SQLITE
