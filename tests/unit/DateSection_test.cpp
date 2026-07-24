/**
 * @file DateSection_test.cpp
 * @brief Unit tests for SDK-6171 `groupAllNodesByDate` + `byTimestampAnchor`.
 *
 * Split from Sqlite_test.cpp; exercises groupAllNodesByDate + byTimestampAnchor
 * against real SQLite. Tests inherit from SearchByPageTest (SearchByPageTestBase.h)
 * and override populateDB() to add a date-bucketed subtree on top of the base
 * dataset.
 */

#include "utils.h"

#include <gtest/gtest.h>
#include <mega/db/sqlite.h>
#include <mega/megaapp.h>
#include <mega/megaclient.h>
#include <mega/nodemanager.h>

#include <mega.h> // brings in config.h which #defines USE_SQLITE when enabled
#include <optional>
#include <string>
#include <vector>

using namespace mega;

#ifdef USE_SQLITE

#include "SearchByPageTestBase.h"

namespace fs = std::filesystem;

namespace
{

using namespace mega::pagetest;

// ═══════════════════════════════════════════════════════════════════════════
//  DateSectionTest – fixture + tests for groupAllNodesByDate +
//  byTimestampAnchor SQL semantics. Real SQLite via SearchByPageTest.
// ═══════════════════════════════════════════════════════════════════════════
//
// Named DateSectionTest to avoid shadowing production mega::DateSection (using
// namespace mega).
//
// Pinned UTC mtimes (seconds since epoch):
//   1704067200  = 2024-01-01 00:00:00 UTC   (jan01.jpg)
//   1709164800  = 2024-02-29 00:00:00 UTC   (feb29.jpg)        // leap day
//   1717200000  = 2024-06-01 00:00:00 UTC   (jun01.jpg)
//   1719792000  = 2024-07-01 00:00:00 UTC   (jul01.jpg)        // July bucket start
//   1720224000  = 2024-07-06 00:00:00 UTC   (julSensitive.jpg) // sensitive
//   1721001600  = 2024-07-15 12:00:00 UTC   (jul15.jpg)
//   1722470399  = 2024-07-31 23:59:59 UTC   (jul31.jpg)        // bucket end - 1s
//   1722470400  = 2024-08-01 00:00:00 UTC   (aug01.jpg)        // next bucket
//   1735603200  = 2024-12-31 00:00:00 UTC   (dec31_2024.jpg)   // year rollover
//   1703980800  = 2023-12-31 00:00:00 UTC   (dec31.jpg)
//   0           = mega_invalid_timestamp    (epoch.jpg)        // must be excluded
//   -1                                       (negMtime.jpg)    // must be excluded
//
// Plus videos for grouped-mime tests:
//   1721001600  = 2024-07-15 12:00:00 UTC   (jul_vid1.mp4)
//   1721174400  = 2024-07-17 12:00:00 UTC   (jul_vid2.mp4)
//
// All photo nodes live under mDateSectionRoot so the base fixture's JPGs
// don't contaminate counts; queries scope to {mDateSectionRoot}.

class DateSectionTest: public SearchByPageTest
{
protected:
    NodeHandle mDateSectionRoot;
    NodeHandle mSubFolderA; ///< subfolder for excludeHandles tests
    NodeHandle mJulSensitive;
    NodeHandle mJulInSubA; ///< July photo placed under mSubFolderA

    void populateDB() override
    {
        SearchByPageTest::populateDB();

        auto root = mClient->mNodeManager.getNodeByHandle(mRootHandle);
        ASSERT_NE(root, nullptr);

        auto subtree = addNode(FOLDERNODE, root, NodeMeta{"date_section_subtree", FOLDERNODE});
        mDateSectionRoot = subtree->nodeHandle();

        auto subA = addNode(FOLDERNODE, subtree, NodeMeta{"subA", FOLDERNODE});
        mSubFolderA = subA->nodeHandle();

        // Distinct sizes per photo so the ORDER_SIZE_DESC orthogonality test
        // produces a stable order.
        addNode(FILENODE, subtree, NodeMeta{"jan01.jpg", FILENODE, 100, 1704067200});
        addNode(FILENODE, subtree, NodeMeta{"feb29.jpg", FILENODE, 110, 1709164800});
        addNode(FILENODE, subtree, NodeMeta{"jun01.jpg", FILENODE, 200, 1717200000});
        addNode(FILENODE, subtree, NodeMeta{"jul01.jpg", FILENODE, 300, 1719792000});
        addNode(FILENODE, subtree, NodeMeta{"jul15.jpg", FILENODE, 400, 1721001600});
        addNode(FILENODE, subtree, NodeMeta{"jul31.jpg", FILENODE, 500, 1722470399});
        addNode(FILENODE, subtree, NodeMeta{"aug01.jpg", FILENODE, 600, 1722470400});
        addNode(FILENODE, subtree, NodeMeta{"dec31.jpg", FILENODE, 700, 1703980800});
        addNode(FILENODE, subtree, NodeMeta{"dec31_2024.jpg", FILENODE, 900, 1735603200});

        // Epoch / negative mtime — must be excluded from all section queries.
        addNode(FILENODE, subtree, NodeMeta{"epoch.jpg", FILENODE, 800, 0});
        addNode(FILENODE, subtree, NodeMeta{"negMtime.jpg", FILENODE, 820, -1});

        // Sensitive photo in July (sized between jul15 and jul31).
        NodeMeta sens{"julSensitive.jpg", FILENODE, 450, 1720224000};
        sens.sensitive = true;
        mJulSensitive = addNode(FILENODE, subtree, sens)->nodeHandle();

        // Photo under subA so excludeHandles=[subA] / explicitAncestors=[subA]
        // tests can target it.
        mJulInSubA = addNode(FILENODE, subA, NodeMeta{"jul_subA.jpg", FILENODE, 460, 1720310400})
                         ->nodeHandle();

        // Videos for grouped-mime tests (.mp4).
        addNode(FILENODE, subtree, NodeMeta{"jul_vid1.mp4", FILENODE, 1000, 1721001600});
        addNode(FILENODE, subtree, NodeMeta{"jul_vid2.mp4", FILENODE, 1100, 1721174400});
    }

    DBTableNodes* tableNodes()
    {
        return dynamic_cast<DBTableNodes*>(mClient->sctable.get());
    }

    // Invoke DBTableNodes::groupAllNodesByDate directly with the date-section
    // subtree as the single root.
    std::vector<DateSection> runSections(DateSectionGranularity g,
                                         int order = OrderByClause::MTIME_DESC,
                                         MimeType_t mime = MIME_TYPE_PHOTO,
                                         bool excludeSensitive = false,
                                         std::vector<NodeHandle> excludeHandles = {},
                                         std::vector<NodeHandle> roots = {},
                                         int64_t tzOffsetSeconds = 0)
    {
        DateSectionParams p;
        p.mimeType = mime;
        p.order = order;
        p.granularity = g;
        p.excludeSensitive = excludeSensitive;
        p.excludeHandles = std::move(excludeHandles);
        p.tzOffsetSeconds = tzOffsetSeconds;

        std::vector<DateSection> out;
        const std::vector<NodeHandle> filesRoots =
            roots.empty() ? std::vector<NodeHandle>{mDateSectionRoot} : std::move(roots);
        tableNodes()->groupAllNodesByDate(p, filesRoots, out, CancelToken{});
        return out;
    }

    std::vector<std::pair<NodeHandle, NodeSerialized>>
        runListAll(int order,
                   std::optional<TimestampAnchorFilter> anchor = std::nullopt,
                   size_t maxElements = 0,
                   MimeType_t mime = MIME_TYPE_PHOTO,
                   bool excludeSensitive = false)
    {
        auto p = makeParams(mime,
                            order,
                            maxElements,
                            excludeSensitive,
                            /*cursor=*/std::nullopt,
                            /*explicitAncestors=*/{mDateSectionRoot},
                            /*excludeHandles=*/{},
                            /*locationScope=*/1);
        p.timestampAnchor = anchor;

        std::vector<std::pair<NodeHandle, NodeSerialized>> out;
        tableNodes()->listAllNodesByPage(p, {mDateSectionRoot}, out, CancelToken{});
        return out;
    }

    // (gid, count) tuples for compact section-list assertions.
    static std::vector<std::pair<std::string, int64_t>>
        gidCounts(const std::vector<DateSection>& sections)
    {
        std::vector<std::pair<std::string, int64_t>> result;
        result.reserve(sections.size());
        for (const auto& s: sections)
            result.emplace_back(s.mGroupId, s.mCount);
        return result;
    }

    static const DateSection* find(const std::vector<DateSection>& v, const std::string& gid)
    {
        for (const auto& s: v)
            if (s.mGroupId == gid)
                return &s;
        return nullptr;
    }

    // NodeSerialized.mNode is a serialized blob; for mtime/size assertions
    // we look the node up live via NodeManager.
    bool hasMtime(const std::vector<std::pair<NodeHandle, NodeSerialized>>& rows, int64_t mtime)
    {
        for (const auto& [h, _]: rows)
        {
            auto n = mClient->mNodeManager.getNodeByHandle(h);
            if (n && n->mtime == mtime)
                return true;
        }
        return false;
    }

    // mtimes in row order, looked up via NodeManager.
    std::vector<int64_t> mtimes(const std::vector<std::pair<NodeHandle, NodeSerialized>>& rows)
    {
        std::vector<int64_t> result;
        result.reserve(rows.size());
        for (const auto& [h, _]: rows)
        {
            auto n = mClient->mNodeManager.getNodeByHandle(h);
            result.push_back(n ? n->mtime : -1);
        }
        return result;
    }

    // sizes in row order, looked up via NodeManager.
    std::vector<int64_t> sizes(const std::vector<std::pair<NodeHandle, NodeSerialized>>& rows)
    {
        std::vector<int64_t> result;
        result.reserve(rows.size());
        for (const auto& [h, _]: rows)
        {
            auto n = mClient->mNodeManager.getNodeByHandle(h);
            result.push_back(n ? n->size : -1);
        }
        return result;
    }
};

// ─── B1: section shape (gids, counts, order) ────────────────────────────────

TEST_F(DateSectionTest, DateSection_MonthGranularity_PhotoFilter)
{
    // 11 photos (epoch + negMtime excluded; sensitive included by default).
    // July bucket holds 5: jul01, jul15, jul31, julSensitive, jul_subA.
    const auto v = runSections(DateSectionGranularity::Month);
    const auto tuples = gidCounts(v);

    EXPECT_EQ(tuples,
              (std::vector<std::pair<std::string, int64_t>>{{"2024-12", 1},
                                                            {"2024-08", 1},
                                                            {"2024-07", 5},
                                                            {"2024-06", 1},
                                                            {"2024-02", 1},
                                                            {"2024-01", 1},
                                                            {"2023-12", 1}}));
}

TEST_F(DateSectionTest, DateSection_YearGranularity_Aggregation)
{
    const auto v = runSections(DateSectionGranularity::Year);
    const auto tuples = gidCounts(v);

    // 2024 has 10 photos: jan01, feb29, jun01, jul01, jul15, jul31, aug01,
    // dec31_2024, julSensitive, jul_subA. 2023 has 1: dec31.
    EXPECT_EQ(tuples, (std::vector<std::pair<std::string, int64_t>>{{"2024", 10}, {"2023", 1}}));
}

TEST_F(DateSectionTest, DateSection_DayGranularity_PerDayBuckets)
{
    const auto v = runSections(DateSectionGranularity::Day);
    ASSERT_FALSE(v.empty());

    // Three distinct July days plus 1 in late July: jul01, jul15, jul31, jul06 (sens), jul07 (subA)
    auto* jul01 = find(v, "2024-07-01");
    ASSERT_NE(jul01, nullptr);
    EXPECT_EQ(jul01->mCount, 1);

    auto* jul15 = find(v, "2024-07-15");
    ASSERT_NE(jul15, nullptr);
    EXPECT_EQ(jul15->mCount, 1);

    auto* jul31 = find(v, "2024-07-31");
    ASSERT_NE(jul31, nullptr);
    EXPECT_EQ(jul31->mCount, 1);

    // jul06 (julSensitive) and jul07 (jul_subA) must land in their own day buckets,
    // not collapse into jul01: the julyTotal==5 sum below would survive that bug.
    auto* jul06 = find(v, "2024-07-06");
    ASSERT_NE(jul06, nullptr);
    EXPECT_EQ(jul06->mCount, 1);

    auto* jul07 = find(v, "2024-07-07");
    ASSERT_NE(jul07, nullptr);
    EXPECT_EQ(jul07->mCount, 1);

    // Total count must equal MONTH "2024-07" count (5).
    int64_t julyTotal = 0;
    for (const auto& s: v)
        if (s.mGroupId.find("2024-07-") == 0)
            julyTotal += s.mCount;
    EXPECT_EQ(julyTotal, 5);
}

TEST_F(DateSectionTest, DateSection_OrderAsc_OldestFirst)
{
    const auto v = runSections(DateSectionGranularity::Month, OrderByClause::MTIME_ASC);
    const auto tuples = gidCounts(v);

    EXPECT_EQ(tuples,
              (std::vector<std::pair<std::string, int64_t>>{{"2023-12", 1},
                                                            {"2024-01", 1},
                                                            {"2024-02", 1},
                                                            {"2024-06", 1},
                                                            {"2024-07", 5},
                                                            {"2024-08", 1},
                                                            {"2024-12", 1}}));
}

TEST_F(DateSectionTest, DateSection_GidStrftimeFormat_ZeroPadded)
{
    // January / February / single-digit days must be zero-padded.
    const auto month = runSections(DateSectionGranularity::Month);
    EXPECT_NE(find(month, "2024-01"), nullptr);
    EXPECT_NE(find(month, "2024-02"), nullptr);
    EXPECT_EQ(find(month, "2024-1"), nullptr);
    EXPECT_EQ(find(month, "2024-2"), nullptr);

    const auto day = runSections(DateSectionGranularity::Day);
    EXPECT_NE(find(day, "2024-07-01"), nullptr);
    EXPECT_EQ(find(day, "2024-7-1"), nullptr);
}

// ─── B2: bucket bounds correctness (the central new SQL responsibility) ────

TEST_F(DateSectionTest, DateSection_DayBounds_PinnedValues)
{
    const auto v = runSections(DateSectionGranularity::Day);
    auto* s = find(v, "2024-07-15");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->mStartDate, 1721001600); // 2024-07-15 00:00:00 UTC
    EXPECT_EQ(s->mEndDate, 1721088000); // 2024-07-16 00:00:00 UTC
}

TEST_F(DateSectionTest, DateSection_MonthBounds_PinnedValues)
{
    const auto v = runSections(DateSectionGranularity::Month);
    auto* s = find(v, "2024-07");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->mStartDate, 1719792000); // 2024-07-01 00:00:00 UTC
    EXPECT_EQ(s->mEndDate, 1722470400); // 2024-08-01 00:00:00 UTC
}

TEST_F(DateSectionTest, DateSection_YearBounds_PinnedValues)
{
    const auto v = runSections(DateSectionGranularity::Year);
    auto* s = find(v, "2024");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->mStartDate, 1704067200); // 2024-01-01 00:00:00 UTC
    EXPECT_EQ(s->mEndDate, 1735689600); // 2025-01-01 00:00:00 UTC
}

TEST_F(DateSectionTest, DateSection_MonthRollover_BucketEndIsNextYear)
{
    const auto v = runSections(DateSectionGranularity::Month);
    auto* s = find(v, "2024-12");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->mEndDate, 1735689600); // 2025-01-01 00:00:00 UTC
}

TEST_F(DateSectionTest, DateSection_DayRollover_BucketEndIsNextMonth)
{
    const auto v = runSections(DateSectionGranularity::Day);
    auto* s = find(v, "2024-12-31");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->mEndDate, 1735689600); // 2025-01-01 00:00:00 UTC
}

TEST_F(DateSectionTest, DateSection_LeapDay_BucketsAreAdjacent)
{
    const auto v = runSections(DateSectionGranularity::Day);

    auto* feb29 = find(v, "2024-02-29");
    ASSERT_NE(feb29, nullptr);
    EXPECT_EQ(feb29->mStartDate, 1709164800); // 2024-02-29 00:00:00 UTC
    EXPECT_EQ(feb29->mEndDate, 1709251200); // 2024-03-01 00:00:00 UTC
}

TEST_F(DateSectionTest, DateSection_BoundsAreInt64NotText)
{
    // Mirror the outer SELECT's CAST(... AS INTEGER) and confirm the column
    // type is SQLITE_INTEGER. Without the CAST it would be TEXT and
    // sqlite3_column_int64 would silently parse, hiding regressions.
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(":memory:", &db), SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT CAST(strftime('%s', '2024-07' || '-01 00:00:00') AS INTEGER)";
    ASSERT_EQ(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_EQ(sqlite3_column_type(stmt, 0), SQLITE_INTEGER);
    EXPECT_EQ(sqlite3_column_int64(stmt, 0), 1719792000);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

// ─── B2b: UTC offset shifts buckets and bounds ─────────────────────────────

// +09:00 pushes the last second of Jul 31 UTC into the Aug 1 local bucket,
// which then also contains aug01.jpg → count 2. At UTC that bucket has count 1.
TEST_F(DateSectionTest, DateSection_UtcOffset_PositiveOffset_MovesToNextDay)
{
    const int64_t tz = 9 * 3600; // +09:00
    auto sections = runSections(DateSectionGranularity::Day,
                                OrderByClause::MTIME_DESC,
                                MIME_TYPE_PHOTO,
                                false,
                                {},
                                {},
                                tz);

    const auto* aug01 = find(sections, "2024-08-01");
    ASSERT_NE(aug01, nullptr);
    EXPECT_EQ(aug01->mCount, 2); // jul31 (23:59:59Z) joined aug01 (UTC: count 1)
    // 2024-08-01 00:00 local == 2024-07-31T15:00:00Z == 1722470400 - 32400.
    EXPECT_EQ(aug01->mStartDate, 1722470400LL - tz); // UTC value would be 1722470400
    EXPECT_EQ(aug01->mEndDate, 1722556800LL - tz); // +1 day
    EXPECT_EQ(find(sections, "2024-07-31"), nullptr); // jul31 left Jul 31 (UTC: present)
}

// -05:30 pulls midnight-UTC Jul 15 back into Jul 14 local. jul15 was the only
// photo in 2024-07-15, so that bucket disappears entirely.
TEST_F(DateSectionTest, DateSection_UtcOffset_NegativeHalfHour_MovesToPrevDay)
{
    const int64_t tz = -(5 * 3600 + 30 * 60); // -05:30
    auto sections = runSections(DateSectionGranularity::Day,
                                OrderByClause::MTIME_DESC,
                                MIME_TYPE_PHOTO,
                                false,
                                {},
                                {},
                                tz);

    const auto* jul14 = find(sections, "2024-07-14");
    ASSERT_NE(jul14, nullptr); // UTC: no photo maps to 2024-07-14 → null
    // 2024-07-14 00:00 local == 2024-07-14T05:30:00Z == 1720915200 - (-19800).
    EXPECT_EQ(jul14->mStartDate, 1720915200LL - tz);
    EXPECT_EQ(find(sections, "2024-07-15"), nullptr); // jul15 left (UTC: present)
}

// :45 minutes must be honoured (not truncated to :00/:30). The bound value
// uniquely identifies +05:45: :30 would give 1722450600, UTC gives 1722470400.
TEST_F(DateSectionTest, DateSection_UtcOffset_FortyFiveMinuteZone)
{
    const int64_t tz = 5 * 3600 + 45 * 60; // +05:45 (Nepal) == 20700
    auto sections = runSections(DateSectionGranularity::Day,
                                OrderByClause::MTIME_DESC,
                                MIME_TYPE_PHOTO,
                                false,
                                {},
                                {},
                                tz);
    const auto* aug01 = find(sections, "2024-08-01");
    ASSERT_NE(aug01, nullptr);
    EXPECT_EQ(aug01->mStartDate, 1722470400LL - tz); // == 1722449700, only :45 gives this
}

// Month granularity: -01:00 pulls aug01 (00:00Z on the 1st) back into July local.
// aug01 was the only August photo, so the 2024-08 bucket disappears.
TEST_F(DateSectionTest, DateSection_UtcOffset_MonthGranularity_CrossesMonth)
{
    const int64_t tz = -3600; // -01:00
    auto sections = runSections(DateSectionGranularity::Month,
                                OrderByClause::MTIME_DESC,
                                MIME_TYPE_PHOTO,
                                false,
                                {},
                                {},
                                tz);

    EXPECT_EQ(find(sections, "2024-08"), nullptr); // aug01 → July local (UTC: present)
    const auto* jul = find(sections, "2024-07");
    ASSERT_NE(jul, nullptr);
    // 2024-07 month start local-midnight == 2024-07-01T01:00:00Z == 1719792000 - (-3600).
    EXPECT_EQ(jul->mStartDate, 1719792000LL - tz);
}

// Year granularity: -01:00 pulls jan01 (00:00Z on Jan 1) back into 2023 local,
// where it joins dec31 → the 2023 bucket goes from count 1 (UTC) to 2.
TEST_F(DateSectionTest, DateSection_UtcOffset_YearGranularity_CrossesYear)
{
    const int64_t tz = -3600; // -01:00
    auto sections = runSections(DateSectionGranularity::Year,
                                OrderByClause::MTIME_DESC,
                                MIME_TYPE_PHOTO,
                                false,
                                {},
                                {},
                                tz);

    const auto* y2023 = find(sections, "2023");
    ASSERT_NE(y2023, nullptr);
    EXPECT_EQ(y2023->mCount, 2); // dec31 + jan01 (UTC: only dec31, count 1)
    // 2023 start local-midnight == 2023-01-01T01:00:00Z == 1672531200 - (-3600).
    EXPECT_EQ(y2023->mStartDate, 1672531200LL - tz);
}

// Offset combined with an exclude: subA is dropped AND +09:00 is applied. This
// exercises the tz bind slot AFTER the exclude-handle run (numExcludes=1 shifts
// it right), the path the simple no-exclude tests above never reach.
TEST_F(DateSectionTest, DateSection_UtcOffset_WithExcludeHandles_ShiftedSlot)
{
    const int64_t tz = 9 * 3600; // +09:00
    auto sections = runSections(DateSectionGranularity::Day,
                                OrderByClause::MTIME_DESC,
                                MIME_TYPE_PHOTO,
                                false,
                                {mSubFolderA}, // exclude subA → drops jul_subA
                                {},
                                tz);

    // Offset still correct at the shifted slot: jul31 (23:59:59Z) joins aug01.
    const auto* aug01 = find(sections, "2024-08-01");
    ASSERT_NE(aug01, nullptr);
    EXPECT_EQ(aug01->mCount, 2);
    EXPECT_EQ(aug01->mStartDate, 1722470400LL - tz);
    // jul_subA (the only 2024-07-07 photo) was excluded → its local bucket is gone.
    EXPECT_EQ(find(sections, "2024-07-07"), nullptr);
}

// ─── B3: mtime <= 0 exclusion ──────────────────────────────────────────────

TEST_F(DateSectionTest, DateSection_MtimeZero_NoEpochBucket)
{
    const auto v = runSections(DateSectionGranularity::Month);
    for (const auto& s: v)
    {
        EXPECT_NE(s.mGroupId.substr(0, 4), "1970")
            << "epoch.jpg leaked into the section list: " << s.mGroupId;
    }
}

TEST_F(DateSectionTest, DateSection_MtimeNegative_Excluded)
{
    // epoch.jpg (mtime=0) and negMtime.jpg (mtime=-1) must be excluded by the
    // `> invalidSentinel` guard, leaving 11. The year-range pins catch a buggy
    // guard letting mtime=-1 through, which would yield a far-future or
    // negative-year gid from strftime.
    const auto v = runSections(DateSectionGranularity::Year);
    for (const auto& s: v)
    {
        EXPECT_GE(s.mGroupId, "1970");
        EXPECT_LT(s.mGroupId, "2100");
    }
    int64_t total = 0;
    for (const auto& s: v)
        total += s.mCount;
    EXPECT_EQ(total, 11);
}

// ─── B4: WHERE-clause filters ──────────────────────────────────────────────

TEST_F(DateSectionTest, DateSection_ExcludeSensitive_HidesNodes)
{
    const auto v = runSections(DateSectionGranularity::Month,
                               OrderByClause::MTIME_DESC,
                               MIME_TYPE_PHOTO,
                               /*excludeSensitive=*/true);

    auto* jul = find(v, "2024-07");
    ASSERT_NE(jul, nullptr);
    EXPECT_EQ(jul->mCount, 4); // 5 normally, minus julSensitive
}

TEST_F(DateSectionTest, DateSection_ExcludeHandles_DropsSubtree)
{
    // Excluding subA must remove its July photo (jul_subA.jpg) from "2024-07".
    const auto v = runSections(DateSectionGranularity::Month,
                               OrderByClause::MTIME_DESC,
                               MIME_TYPE_PHOTO,
                               /*excludeSensitive=*/false,
                               /*excludeHandles=*/{mSubFolderA});

    auto* jul = find(v, "2024-07");
    ASSERT_NE(jul, nullptr);
    EXPECT_EQ(jul->mCount, 4); // 5 normally, minus jul_subA
}

TEST_F(DateSectionTest, DateSection_ByLocationHandles_NarrowToSubfolder)
{
    // Scope to subA only — should contain just jul_subA.jpg.
    const auto v = runSections(DateSectionGranularity::Month,
                               OrderByClause::MTIME_DESC,
                               MIME_TYPE_PHOTO,
                               /*excludeSensitive=*/false,
                               /*excludeHandles=*/{},
                               /*roots=*/{mSubFolderA});

    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v.front().mGroupId, "2024-07");
    EXPECT_EQ(v.front().mCount, 1);
}

TEST_F(DateSectionTest, DateSection_EmptyFilesRoots_ReturnsFalse)
{
    DateSectionParams p;
    p.mimeType = MIME_TYPE_PHOTO;
    p.order = OrderByClause::MTIME_DESC;
    p.granularity = DateSectionGranularity::Month;

    std::vector<DateSection> out;
    EXPECT_FALSE(tableNodes()->groupAllNodesByDate(p, /*filesRoots=*/{}, out, CancelToken{}));
    EXPECT_TRUE(out.empty());
}

TEST_F(DateSectionTest, DateSection_InvalidMimeType_ReturnsFalse)
{
    DateSectionParams p;
    p.mimeType = MIME_TYPE_UNKNOWN;
    p.order = OrderByClause::MTIME_DESC;
    p.granularity = DateSectionGranularity::Month;

    std::vector<DateSection> out;
    EXPECT_FALSE(tableNodes()->groupAllNodesByDate(p, {mDateSectionRoot}, out, CancelToken{}));
    EXPECT_TRUE(out.empty());
}

TEST_F(DateSectionTest, DateSection_OutOfRangeMimeType_ReturnsFalse)
{
    // The API layer caps mimeType at FILE_TYPE_LAST, but a direct DB caller could pass
    // an out-of-range value. The DB entry must reject it (it is the cache-key's leading
    // digit) rather than packing an out-of-range digit deeper in computeDateSectionsCacheId.
    DateSectionParams p;
    p.mimeType = static_cast<MimeType_t>(MIME_TYPE_ALL_VISUAL_MEDIA + 1);
    p.order = OrderByClause::MTIME_DESC;
    p.granularity = DateSectionGranularity::Month;

    std::vector<DateSection> out;
    EXPECT_FALSE(tableNodes()->groupAllNodesByDate(p, {mDateSectionRoot}, out, CancelToken{}));
    EXPECT_TRUE(out.empty());
}

TEST_F(DateSectionTest, DateSection_OutOfRangeGranularity_ReturnsFalse)
{
    // The API layer caps granularity to [Day, Year], but a direct DB caller could cast an
    // out-of-range value. The DB entry must reject it (it is a cache-key digit) rather than
    // relying on the assert/fallback in buildDateSectionGidExpr / buildDateSectionBoundExprs.
    DateSectionParams p;
    p.mimeType = MIME_TYPE_PHOTO;
    p.order = OrderByClause::MTIME_DESC;
    p.granularity =
        static_cast<DateSectionGranularity>(static_cast<int>(DateSectionGranularity::Year) + 1);

    std::vector<DateSection> out;
    EXPECT_FALSE(tableNodes()->groupAllNodesByDate(p, {mDateSectionRoot}, out, CancelToken{}));
    EXPECT_TRUE(out.empty());
}

TEST_F(DateSectionTest, DateSection_UnsupportedOrder_ReturnsFalse)
{
    // A non-timestamp order (DEFAULT_ASC) has no timestamp column, so the DB
    // entry point must reject it up front rather than failing deep in prepare.
    DateSectionParams p;
    p.mimeType = MIME_TYPE_PHOTO;
    p.order = OrderByClause::DEFAULT_ASC;
    p.granularity = DateSectionGranularity::Month;

    std::vector<DateSection> out;
    EXPECT_FALSE(tableNodes()->groupAllNodesByDate(p, {mDateSectionRoot}, out, CancelToken{}));
    EXPECT_TRUE(out.empty());
}

TEST_F(DateSectionTest, DateSection_FileVersions_Excluded)
{
    // Versioned files come from the base SearchByPageTest fixture (head.jpg
    // chain under normal_folder). Scope to the cloud root to include them in
    // the candidate set, then confirm they don't appear in the section list.
    const auto v = runSections(DateSectionGranularity::Month,
                               OrderByClause::MTIME_DESC,
                               MIME_TYPE_PHOTO,
                               /*excludeSensitive=*/false,
                               /*excludeHandles=*/{},
                               /*roots=*/{hFilesRoot});

    // The base fixture's hVersionV1 / hVersionV2 are FILENODEs flagged as
    // versions and must never appear. Total = base non-version photos + photos
    // added here; named constants keep the count tied to the fixture.
    constexpr int64_t kBaseNonVersionPhotos = 4; // clean, self_sens, head HEAD, under_sens
    constexpr int64_t kDateSectionPhotos = 11; // see DateSection::SetUp() photos table
    constexpr int64_t kExpectedTotal = kBaseNonVersionPhotos + kDateSectionPhotos;
    int64_t total = 0;
    for (const auto& s: v)
        total += s.mCount;
    EXPECT_EQ(total, kExpectedTotal);
}

// ─── B5: grouped mime types ───────────────────────────────────────────────

TEST_F(DateSectionTest, DateSection_AllVisualMedia_AggregatesPhotosVideos)
{
    // July: 5 photos + 2 videos = 7 in 2024-07 bucket.
    const auto v = runSections(DateSectionGranularity::Month,
                               OrderByClause::MTIME_DESC,
                               MIME_TYPE_ALL_VISUAL_MEDIA);

    auto* jul = find(v, "2024-07");
    ASSERT_NE(jul, nullptr);
    EXPECT_EQ(jul->mCount, 7);
    EXPECT_EQ(jul->mStartDate, 1719792000);
    EXPECT_EQ(jul->mEndDate, 1722470400);
}

TEST_F(DateSectionTest, DateSection_AllVisualMedia_PerRouteAggregationConsistent)
{
    // sum(buckets in grouped) == sum(photos) + sum(videos)
    const auto photos =
        runSections(DateSectionGranularity::Year, OrderByClause::MTIME_DESC, MIME_TYPE_PHOTO);
    const auto videos =
        runSections(DateSectionGranularity::Year, OrderByClause::MTIME_DESC, MIME_TYPE_VIDEO);
    const auto grouped = runSections(DateSectionGranularity::Year,
                                     OrderByClause::MTIME_DESC,
                                     MIME_TYPE_ALL_VISUAL_MEDIA);

    auto sumOf = [](const std::vector<DateSection>& v)
    {
        int64_t s = 0;
        for (const auto& d: v)
            s += d.mCount;
        return s;
    };
    EXPECT_EQ(sumOf(grouped), sumOf(photos) + sumOf(videos));
}

TEST_F(DateSectionTest, DateSection_AllVisualMedia_BoundsMatchSimpleMime)
{
    const auto photos =
        runSections(DateSectionGranularity::Month, OrderByClause::MTIME_DESC, MIME_TYPE_PHOTO);
    const auto grouped = runSections(DateSectionGranularity::Month,
                                     OrderByClause::MTIME_DESC,
                                     MIME_TYPE_ALL_VISUAL_MEDIA);

    auto* photosJul = find(photos, "2024-07");
    auto* groupedJul = find(grouped, "2024-07");
    ASSERT_NE(photosJul, nullptr);
    ASSERT_NE(groupedJul, nullptr);

    EXPECT_EQ(photosJul->mStartDate, groupedJul->mStartDate);
    EXPECT_EQ(photosJul->mEndDate, groupedJul->mEndDate);
}

// ─── B6: byTimestampAnchor half-bounded clause on listAllNodesByPage ──────

namespace
{
TimestampAnchorFilter julyMtimeAnchorAsc()
{
    TimestampAnchorFilter ta;
    ta.mOrder = OrderByClause::MTIME_ASC; // ASC anchor → enforce mtime >= startSec
    ta.mStartSeconds = 1719792000; // 2024-07-01 UTC inclusive
    ta.mEndSeconds = 1722470400; // 2024-08-01 UTC exclusive
    return ta;
}

TimestampAnchorFilter julyMtimeAnchorDesc()
{
    TimestampAnchorFilter ta;
    ta.mOrder = OrderByClause::MTIME_DESC; // DESC anchor → enforce mtime < endSec
    ta.mStartSeconds = 1719792000;
    ta.mEndSeconds = 1722470400;
    return ta;
}
} // namespace

TEST_F(DateSectionTest, ListAllByPage_AnchorAsc_LowerBoundEnforced)
{
    // ASC anchor → mtime >= startSec (1719792000). Excludes jun01 / feb29 / jan01 / dec31_2023.
    const auto rows = runListAll(OrderByClause::MTIME_ASC, julyMtimeAnchorAsc());

    EXPECT_TRUE(hasMtime(rows, 1719792000)); // jul01 (== start, inclusive)
    EXPECT_TRUE(hasMtime(rows, 1722470400)); // aug01 (>= start, end is NOT enforced)
    EXPECT_TRUE(hasMtime(rows, 1735603200)); // dec31_2024 (later, included)
    EXPECT_FALSE(hasMtime(rows, 1717200000)); // jun01 — before start
    EXPECT_FALSE(hasMtime(rows, 1704067200)); // jan01 — before start
}

TEST_F(DateSectionTest, ListAllByPage_AnchorDesc_UpperBoundEnforced)
{
    // DESC anchor → mtime < endSec (1722470400). Excludes aug01 / dec31_2024.
    const auto rows = runListAll(OrderByClause::MTIME_DESC, julyMtimeAnchorDesc());

    EXPECT_TRUE(hasMtime(rows, 1722470399)); // jul31 (< end)
    EXPECT_TRUE(hasMtime(rows, 1717200000)); // jun01 (older, included — start NOT enforced)
    EXPECT_TRUE(hasMtime(rows, 1704067200)); // jan01 (older still)
    EXPECT_FALSE(hasMtime(rows, 1722470400)); // aug01 — exactly at end, excluded
    EXPECT_FALSE(hasMtime(rows, 1735603200)); // dec31_2024 — after end
}

TEST_F(DateSectionTest, ListAllByPage_AnchorAsc_StartInclusive)
{
    // jul01.jpg's mtime == startSec; must be included with ASC anchor.
    const auto rows = runListAll(OrderByClause::MTIME_ASC, julyMtimeAnchorAsc());
    EXPECT_TRUE(hasMtime(rows, 1719792000)); // jul01
}

TEST_F(DateSectionTest, ListAllByPage_AnchorDesc_EndExclusive)
{
    // aug01.jpg's mtime == endSec; must be excluded with DESC anchor.
    const auto rows = runListAll(OrderByClause::MTIME_DESC, julyMtimeAnchorDesc());
    EXPECT_FALSE(hasMtime(rows, 1722470400));
}

TEST_F(DateSectionTest, ListAllByPage_AnchorAsc_Mtime0_Excluded)
{
    // ASC anchor at start=0: the anchor path's `> invalidSentinel` guard
    // excludes epoch.jpg (mtime=0) and negMtime.jpg (mtime=-1) — they're in
    // no section, so anchored pages must not surface them.
    TimestampAnchorFilter ta;
    ta.mOrder = OrderByClause::MTIME_ASC;
    ta.mStartSeconds = 0;
    ta.mEndSeconds = 1735689600;
    const auto rows = runListAll(OrderByClause::MTIME_ASC, ta);

    EXPECT_FALSE(hasMtime(rows, 0));
    EXPECT_FALSE(hasMtime(rows, -1));
}

TEST_F(DateSectionTest, ListAllByPage_AnchorDesc_Mtime0_Excluded)
{
    // Regression: a DESC anchor (`< end`) has no lower bound, so without the
    // `> invalidSentinel` guard mtime=0 / -1 nodes would leak into the tail —
    // even though no section counts them. Must be excluded like the ASC case.
    TimestampAnchorFilter ta;
    ta.mOrder = OrderByClause::MTIME_DESC;
    ta.mStartSeconds = 0;
    ta.mEndSeconds = 1735689600; // after every real node, so the tail is reached
    const auto rows = runListAll(OrderByClause::MTIME_DESC, ta);

    EXPECT_FALSE(hasMtime(rows, 0));
    EXPECT_FALSE(hasMtime(rows, -1));
}

TEST_F(DateSectionTest, ListAllByPage_Anchor_OrthogonalToPageOrder)
{
    // DESC anchor (mtime < endSec) + page order = SIZE_DESC (ORDER BY size DESC).
    // Pins the orthogonality contract: anchor's sectionOrder controls the
    // half-bound; page order controls only ORDER BY. The two CAN be different
    // columns / directions and the SQL composes cleanly.
    const auto rows = runListAll(OrderByClause::SIZE_DESC, julyMtimeAnchorDesc());

    // Half-bound stays in effect even when page order isn't a timestamp.
    EXPECT_FALSE(hasMtime(rows, 1722470400)); // aug01 (mtime == endSec)
    EXPECT_FALSE(hasMtime(rows, 1735603200)); // dec31_2024 (after endSec)
    EXPECT_TRUE(hasMtime(rows, 1717200000)); // jun01 (start NOT enforced)

    // Sorted by size DESC.
    const auto rowSizes = sizes(rows);
    for (size_t i = 1; i < rowSizes.size(); ++i)
        EXPECT_LE(rowSizes[i], rowSizes[i - 1]);
}

TEST_F(DateSectionTest, ListAllByPage_Anchor_DirectionFromSectionOrder_NotPageOrder)
{
    // sectionOrder=DESC sets the half-bound (mtime<end) independent of page
    // order=ASC (ORDER BY).
    const auto rows = runListAll(OrderByClause::MTIME_ASC, julyMtimeAnchorDesc());

    EXPECT_TRUE(hasMtime(rows, 1704067200)); // jan01 (< endSec, included)
    EXPECT_TRUE(hasMtime(rows, 1717200000)); // jun01 (< endSec, included)
    EXPECT_TRUE(hasMtime(rows, 1722470399)); // jul31 (< endSec, included)
    EXPECT_FALSE(hasMtime(rows, 1722470400)); // aug01 (== endSec, excluded)
    EXPECT_FALSE(hasMtime(rows, 1735603200)); // dec31_2024 (> endSec, excluded)

    // ORDER BY mtime ASC: each successive row has a non-decreasing mtime.
    const auto rowMtimes = mtimes(rows);
    for (size_t i = 1; i < rowMtimes.size(); ++i)
        EXPECT_LE(rowMtimes[i - 1], rowMtimes[i]);
}

TEST_F(DateSectionTest, ListAllByPage_Anchor_PagesCrossSectionBoundary)
{
    // Small page (3) + DESC anchor on July → first page should yield 3
    // newest-mtime photos with mtime < endSec, all from 2024-07 (5 July
    // photos available, none from August in the result).
    const auto rows = runListAll(OrderByClause::MTIME_DESC,
                                 julyMtimeAnchorDesc(),
                                 /*maxElements=*/3);
    ASSERT_EQ(rows.size(), 3u);

    const auto rowMtimes = mtimes(rows);
    // All three should be within July (mtime in [1719792000, 1722470400)).
    for (int64_t mt: rowMtimes)
    {
        EXPECT_GE(mt, 1719792000);
        EXPECT_LT(mt, 1722470400);
    }

    // Newest first (DESC) — strict descending.
    EXPECT_GT(rowMtimes[0], rowMtimes[1]);
    EXPECT_GT(rowMtimes[1], rowMtimes[2]);
}

// ═══════════════════════════════════════════════════════════════════════════
//  GifRawFilterTest – the gif/raw sub-category filter on listAllNodesByPage +
//  groupAllNodesByDate. Real SQLite via SearchByPageTest.
// ═══════════════════════════════════════════════════════════════════════════
class GifRawFilterTest: public SearchByPageTest
{
protected:
    // Seeds `photos` files under a fresh folder: one .gif every gifEvery, one .cr2 (raw)
    // every rawEvery (offset by 1 so they don't overlap), the rest .jpg. Returns the
    // exact gif/raw counts so callers can assert membership.
    NodeHandle
        seedPhotoTree(int photos, int gifEvery, int rawEvery, size_t& gifCount, size_t& rawCount)
    {
        auto root = mClient->mNodeManager.getNodeByHandle(mRootHandle);
        EXPECT_NE(root, nullptr);
        auto folder = addNode(FOLDERNODE, root, NodeMeta{"GifRawFolder", FOLDERNODE});
        gifCount = rawCount = 0;
        for (int k = 0; k < photos; ++k)
        {
            // Increment the count in the same branch that assigns the extension, so the
            // expectations stay correct if an extension string is ever changed (e.g. .cr2 -> .dng).
            const char* ext;
            if (k % gifEvery == 0)
            {
                ext = ".gif";
                ++gifCount;
            }
            else if (k % rawEvery == 1)
            {
                ext = ".cr2";
                ++rawCount;
            }
            else
            {
                ext = ".jpg";
            }
            // mtime > 0 so the nodes are not dropped by the date-section epoch/negative guard.
            NodeMeta meta{"p_" + std::to_string(k) + ext, FILENODE, 100, 1'700'000'000LL + k + 1};
            addNode(FILENODE, folder, meta);
        }
        if (auto* sa = dynamic_cast<SqliteAccountState*>(mClient->sctable.get()))
            sa->createIndexes(/*enableSearch=*/true, /*enableLexi=*/true);
        return folder->nodeHandle();
    }

    // Counts nodes of `mime` (optionally narrowed by subtype) within `ancestor`.
    size_t countPhotos(FileSubType_t sub, NodeHandle ancestor, MimeType_t mime = MIME_TYPE_PHOTO)
    {
        ListAllNodesParams p;
        p.mimeType = mime;
        p.fileSubType = sub;
        p.order = OrderByClause::MTIME_DESC;
        p.maxElements = 0; // no limit
        p.explicitAncestors = {ancestor};
        const std::vector<NodeHandle> filesRoots{ancestor};
        std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
        CancelToken ct;
        table()->listAllNodesByPage(p, filesRoots, nodes, ct);
        return nodes.size();
    }

    size_t sumSectionCounts(FileSubType_t sub, NodeHandle ancestor)
    {
        DateSectionParams params;
        params.mimeType = MIME_TYPE_PHOTO;
        params.fileSubType = sub;
        params.order = OrderByClause::MTIME_DESC;
        params.granularity = DateSectionGranularity::Month;
        params.explicitAncestors = {ancestor};
        const std::vector<NodeHandle> filesRoots{ancestor};
        std::vector<DateSection> out;
        CancelToken ct;
        table()->groupAllNodesByDate(params, filesRoots, out, ct);
        size_t total = 0;
        for (const auto& s: out)
            total += static_cast<size_t>(s.mCount);
        return total;
    }

    SqliteAccountState* table()
    {
        return dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    }
};

// The subtype filter must return exactly the gif/raw nodes, while a plain
// FILE_TYPE_PHOTO query still returns the whole photo set (gif+raw+jpg).
TEST_F(GifRawFilterTest, SubCategoryFiltersCorrectly)
{
    size_t gifCount = 0, rawCount = 0;
    const NodeHandle folder = seedPhotoTree(/*photos=*/40,
                                            /*gifEvery=*/5,
                                            /*rawEvery=*/5,
                                            gifCount,
                                            rawCount);
    ASSERT_GT(gifCount, 0u);
    ASSERT_GT(rawCount, 0u);

    const size_t all = countPhotos(FILE_SUBTYPE_NONE, folder);
    EXPECT_EQ(countPhotos(FILE_SUBTYPE_GIF, folder), gifCount);
    EXPECT_EQ(countPhotos(FILE_SUBTYPE_RAW, folder), rawCount);
    // FILE_TYPE_PHOTO semantics unchanged: still a superset that includes gif+raw.
    EXPECT_EQ(all, 40u);
    EXPECT_GT(all, gifCount + rawCount);
    // Cross-category: a non-photo category + GIF matches nothing (never widens).
    EXPECT_EQ(countPhotos(FILE_SUBTYPE_GIF, folder, MIME_TYPE_VIDEO), 0u);
}

// The residual sub-category predicate must also drive the date-section counts:
// GIF/RAW sections sum to the gif/raw totals, while an unfiltered query still
// counts the whole photo set (gif+raw+jpg) into its sections.
TEST_F(GifRawFilterTest, DateSectionSubCategoryCounts)
{
    size_t gifCount = 0, rawCount = 0;
    const NodeHandle folder = seedPhotoTree(/*photos=*/40,
                                            /*gifEvery=*/5,
                                            /*rawEvery=*/5,
                                            gifCount,
                                            rawCount);
    EXPECT_EQ(sumSectionCounts(FILE_SUBTYPE_GIF, folder), gifCount);
    EXPECT_EQ(sumSectionCounts(FILE_SUBTYPE_RAW, folder), rawCount);
    EXPECT_EQ(sumSectionCounts(FILE_SUBTYPE_NONE, folder), 40u);
}

// Grouped-mime path: ALL_VISUAL_MEDIA builds one CTE per route (photo, video) in
// buildGroupedListAllQuery — the residual must ride each route. gif/raw are photos ⊂ visual
// media, so the counts match the simple-PHOTO path.
TEST_F(GifRawFilterTest, SubCategoryFiltersGroupedMime)
{
    size_t gifCount = 0, rawCount = 0;
    const NodeHandle folder = seedPhotoTree(/*photos=*/40,
                                            /*gifEvery=*/5,
                                            /*rawEvery=*/5,
                                            gifCount,
                                            rawCount);
    EXPECT_EQ(countPhotos(FILE_SUBTYPE_GIF, folder, MIME_TYPE_ALL_VISUAL_MEDIA), gifCount);
    EXPECT_EQ(countPhotos(FILE_SUBTYPE_RAW, folder, MIME_TYPE_ALL_VISUAL_MEDIA), rawCount);
    EXPECT_EQ(countPhotos(FILE_SUBTYPE_NONE, folder, MIME_TYPE_ALL_VISUAL_MEDIA), 40u);
}

// Cursor path: paging PHOTO+GIF with a name cursor must return only the gif set across all
// pages, not the whole photo set — i.e. the residual rides the cursor query, not just page 1.
TEST_F(GifRawFilterTest, SubCategoryFilterHonouredWithCursor)
{
    size_t gifCount = 0, rawCount = 0;
    const NodeHandle folder = seedPhotoTree(/*photos=*/40,
                                            /*gifEvery=*/5,
                                            /*rawEvery=*/5,
                                            gifCount,
                                            rawCount);
    ASSERT_GT(gifCount, 2u); // need several pages at size 2

    ListAllNodesParams p;
    p.mimeType = MIME_TYPE_PHOTO;
    p.fileSubType = FILE_SUBTYPE_GIF;
    p.order = OrderByClause::DEFAULT_ASC; // name-only cursor
    p.maxElements = 2;
    p.explicitAncestors = {folder};
    const std::vector<NodeHandle> filesRoots{folder};

    size_t paged = 0;
    std::optional<NodeSearchCursorOffset> cursor;
    for (int guard = 0; guard < 100; ++guard)
    {
        p.cursor = cursor;
        std::vector<std::pair<NodeHandle, NodeSerialized>> page;
        CancelToken ct;
        table()->listAllNodesByPage(p, filesRoots, page, ct);
        if (page.empty())
            break;
        paged += page.size();
        const auto last = mClient->mNodeManager.getNodeByHandle(page.back().first);
        ASSERT_NE(last, nullptr);
        NodeSearchCursorOffset c;
        c.mLastName = last->displayname();
        c.mLastHandle = page.back().first.as8byte();
        cursor = c;
    }
    EXPECT_EQ(paged, gifCount);
}

// ═══════════════════════════════════════════════════════════════════════════
//  FavouriteFilterTest – the tri-state favourite filter on listAllNodesByPage +
//  groupAllNodesByDate. Real SQLite via SearchByPageTest.
// ═══════════════════════════════════════════════════════════════════════════
class FavouriteFilterTest: public SearchByPageTest
{
protected:
    // includeK0=false suppresses the k==0 favourite so a genuinely favourite-free
    // subtree can be seeded (k==0 always satisfies k % favEvery == 0 otherwise).
    NodeHandle seedFavTree(int photos, int favEvery, size_t& favCount, bool includeK0 = true)
    {
        auto root = mClient->mNodeManager.getNodeByHandle(mRootHandle);
        EXPECT_NE(root, nullptr);
        auto folder = addNode(FOLDERNODE, root, NodeMeta{"FavFolder", FOLDERNODE});
        favCount = 0;
        for (int k = 0; k < photos; ++k)
        {
            NodeMeta meta{"p_" + std::to_string(k) + ".jpg",
                          FILENODE,
                          100,
                          1'700'000'000LL + k + 1};
            if (k % favEvery == 0 && (k != 0 || includeK0))
            {
                meta.fav = 1;
                ++favCount;
            }
            addNode(FILENODE, folder, meta);
        }
        if (auto* sa = dynamic_cast<SqliteAccountState*>(mClient->sctable.get()))
            sa->createIndexes(/*enableSearch=*/true, /*enableLexi=*/true);
        return folder->nodeHandle();
    }

    size_t countRows(FavouriteFilter_t fav,
                     NodeHandle ancestor,
                     MimeType_t mime = MIME_TYPE_PHOTO,
                     int order = OrderByClause::MTIME_DESC)
    {
        ListAllNodesParams p;
        p.mimeType = mime;
        p.favouriteFilter = fav;
        p.order = order;
        p.maxElements = 0;
        p.explicitAncestors = {ancestor};
        std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
        CancelToken ct;
        table()->listAllNodesByPage(p, {ancestor}, nodes, ct);
        return nodes.size();
    }

    size_t sumSections(FavouriteFilter_t fav,
                       NodeHandle ancestor,
                       MimeType_t mime = MIME_TYPE_PHOTO)
    {
        DateSectionParams params;
        params.mimeType = mime;
        params.favouriteFilter = fav;
        params.order = OrderByClause::MTIME_DESC;
        params.granularity = DateSectionGranularity::Month;
        params.explicitAncestors = {ancestor};
        std::vector<DateSection> out;
        CancelToken ct;
        table()->groupAllNodesByDate(params, {ancestor}, out, ct);
        size_t total = 0;
        for (const auto& s: out)
            total += static_cast<size_t>(s.mCount);
        return total;
    }

    SqliteAccountState* table()
    {
        return dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    }

    // Pages the whole result set at pageSize 2 and returns the total rows seen.
    // lastFav >= 0 populates the cursor's mLastFav, required for ORDER_FAV_* cursors.
    size_t pageAllAtSize2(FavouriteFilter_t fav, int order, NodeHandle folder, int lastFav)
    {
        ListAllNodesParams p;
        p.mimeType = MIME_TYPE_PHOTO;
        p.favouriteFilter = fav;
        p.order = order;
        p.maxElements = 2;
        p.explicitAncestors = {folder};

        size_t paged = 0;
        std::optional<NodeSearchCursorOffset> cursor;
        for (int guard = 0; guard < 100; ++guard)
        {
            p.cursor = cursor;
            std::vector<std::pair<NodeHandle, NodeSerialized>> page;
            CancelToken ct;
            table()->listAllNodesByPage(p, {folder}, page, ct);
            if (page.empty())
                break;
            paged += page.size();
            const auto last = mClient->mNodeManager.getNodeByHandle(page.back().first);
            if (!last)
            {
                ADD_FAILURE() << "paged node not found in NodeManager";
                break;
            }
            NodeSearchCursorOffset c;
            c.mLastName = last->displayname();
            c.mLastHandle = page.back().first.as8byte();
            if (lastFav >= 0)
                c.mLastFav = lastFav;
            cursor = c;
        }
        return paged;
    }
};

TEST_F(FavouriteFilterTest, FiltersRowsCorrectly)
{
    size_t favCount = 0;
    const NodeHandle folder = seedFavTree(/*photos=*/40, /*favEvery=*/5, favCount);
    ASSERT_GT(favCount, 0u);
    EXPECT_EQ(countRows(FAVOURITE_FILTER_DISABLED, folder), 40u);
    EXPECT_EQ(countRows(FAVOURITE_FILTER_ONLY_TRUE, folder), favCount);
    EXPECT_EQ(countRows(FAVOURITE_FILTER_ONLY_FALSE, folder), 40u - favCount);
}

// All-favourite subtree: ONLY_FALSE must be empty, ONLY_TRUE must be all.
TEST_F(FavouriteFilterTest, AllOrNoneBoundaries)
{
    size_t favCount = 0;
    const NodeHandle allFav =
        seedFavTree(/*photos=*/10, /*favEvery=*/1, favCount); // every file fav
    ASSERT_EQ(favCount, 10u);
    EXPECT_EQ(countRows(FAVOURITE_FILTER_ONLY_FALSE, allFav), 0u);
    EXPECT_EQ(countRows(FAVOURITE_FILTER_ONLY_TRUE, allFav), 10u);

    size_t none = 0;
    const NodeHandle noFav =
        seedFavTree(/*photos=*/10, /*favEvery=*/100, none, /*includeK0=*/false);
    ASSERT_EQ(none, 0u);
    EXPECT_EQ(countRows(FAVOURITE_FILTER_ONLY_TRUE, noFav), 0u);
    EXPECT_EQ(countRows(FAVOURITE_FILTER_ONLY_FALSE, noFav), 10u);
}

// Grouped-mime path: ALL_VISUAL_MEDIA builds one CTE per route — the fav predicate must ride each.
TEST_F(FavouriteFilterTest, FiltersGroupedMimeRows)
{
    size_t favCount = 0;
    const NodeHandle folder = seedFavTree(40, 5, favCount);
    EXPECT_EQ(countRows(FAVOURITE_FILTER_ONLY_TRUE, folder, MIME_TYPE_ALL_VISUAL_MEDIA), favCount);
    EXPECT_EQ(countRows(FAVOURITE_FILTER_ONLY_FALSE, folder, MIME_TYPE_ALL_VISUAL_MEDIA),
              40u - favCount);
}

// Date-section counts must honour favourite on BOTH the simple-PHOTO and the grouped
// ALL_VISUAL_MEDIA (separate IN-list) route. And the section-query fold (site 2) must
// agree with the row-query fold (site 1): sumSections == countRows for each state.
TEST_F(FavouriteFilterTest, DateSectionCountsHonourFavourite)
{
    size_t favCount = 0;
    const NodeHandle folder = seedFavTree(40, 5, favCount);
    EXPECT_EQ(sumSections(FAVOURITE_FILTER_ONLY_TRUE, folder), favCount);
    EXPECT_EQ(sumSections(FAVOURITE_FILTER_ONLY_FALSE, folder), 40u - favCount);
    EXPECT_EQ(sumSections(FAVOURITE_FILTER_DISABLED, folder), 40u);
    // grouped route
    EXPECT_EQ(sumSections(FAVOURITE_FILTER_ONLY_TRUE, folder, MIME_TYPE_ALL_VISUAL_MEDIA),
              favCount);
    // cross-fold consistency (site1 rows vs site2 sections)
    EXPECT_EQ(sumSections(FAVOURITE_FILTER_ONLY_TRUE, folder),
              countRows(FAVOURITE_FILTER_ONLY_TRUE, folder));
    EXPECT_EQ(sumSections(FAVOURITE_FILTER_ONLY_FALSE, folder),
              countRows(FAVOURITE_FILTER_ONLY_FALSE, folder));
}

// Cursor path (name-only, ORDER_DEFAULT_ASC): residual must ride the cursor query, both states.
TEST_F(FavouriteFilterTest, FavouriteHonouredWithNameCursor)
{
    for (FavouriteFilter_t state: {FAVOURITE_FILTER_ONLY_TRUE, FAVOURITE_FILTER_ONLY_FALSE})
    {
        size_t favCount = 0;
        const NodeHandle folder = seedFavTree(40, 5, favCount);
        const size_t expected = (state == FAVOURITE_FILTER_ONLY_TRUE) ? favCount : 40u - favCount;
        ASSERT_GT(expected, 2u);
        EXPECT_EQ(pageAllAtSize2(state, OrderByClause::DEFAULT_ASC, folder, /*lastFav=*/-1),
                  expected)
            << "state=" << static_cast<int>(state);
    }
}

// ORDER_FAV_ASC degenerate cursor: fav is the primary sort key. With ONLY_TRUE every row
// shares fav=1, so the FAV keyset predicate degenerates to name>tiebreak. The cursor MUST
// populate mLastFav or bindCursorParamsForListAll rejects it — this exercises that path.
TEST_F(FavouriteFilterTest, FavouriteHonouredWithFavOrderCursor)
{
    size_t favCount = 0;
    const NodeHandle folder = seedFavTree(40, 5, favCount);
    ASSERT_GT(favCount, 2u);
    EXPECT_EQ(
        pageAllAtSize2(FAVOURITE_FILTER_ONLY_TRUE, OrderByClause::FAV_ASC, folder, /*lastFav=*/1),
        favCount);
}

} // anonymous namespace

#endif // USE_SQLITE
