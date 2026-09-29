/**
 * Integration tests for MegaApi::groupAllNodesByDate +
 * MegaListAllNodesFilter::byTimestampAnchor public-API plumbing.
 *
 * Scope: public-API ↔ MegaApiImpl ↔ NodeManager wiring only. SQL semantics
 * (bucket counts, bound correctness, half-bounded behavior, filters) live
 * in tests/unit/Sqlite_test.cpp. Each test here fails only if a wiring
 * step is broken — never if a SQL detail changes.
 */

#include "megaapi.h"
#include "SdkTestNodesSetUp.h"

#include <gmock/gmock.h>

#include <chrono>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace sdk_test;
using namespace testing;
using namespace std::chrono_literals;

namespace
{

// Reference epoch seconds for the pinned-mtime fixture below.
constexpr int64_t kJul01Sec = 1719792000; // 2024-07-01 00:00:00 UTC
constexpr int64_t kJul15Sec = 1721001600; // 2024-07-15 00:00:00 UTC
constexpr int64_t kJun15Sec = 1718452800; // 2024-06-15 00:00:00 UTC
constexpr int64_t kJul20Sec = 1721433600; // 2024-07-20 00:00:00 UTC
constexpr int64_t kAug01Sec = 1722470400; // 2024-08-01 00:00:00 UTC (= July end exclusive)
constexpr int64_t kNov14Sec = 1700000001; // 2023-11-14 22:13:21 UTC — capture time

std::chrono::system_clock::time_point toTimePoint(int64_t epochSec)
{
    return std::chrono::system_clock::from_time_t(static_cast<time_t>(epochSec));
}

std::unique_ptr<MegaListAllNodesFilter> makePhotoListAllFilter()
{
    std::unique_ptr<MegaListAllNodesFilter> f{MegaListAllNodesFilter::createInstance()};
    f->byCategory(MegaApi::FILE_TYPE_PHOTO);
    return f;
}

std::unique_ptr<MegaGroupNodesByDateFilter>
    makePhotoGroupFilter(int granularity = MegaGroupNodesByDateFilter::SECTION_GRANULARITY_MONTH)
{
    std::unique_ptr<MegaGroupNodesByDateFilter> f{MegaGroupNodesByDateFilter::createInstance()};
    f->byCategory(MegaApi::FILE_TYPE_PHOTO);
    f->byGranularity(granularity);
    return f;
}

// Convert MegaDateSectionList → vector of group-id strings, for compact
// assertions on the section list.
std::vector<std::string> toGids(MegaDateSectionList* list)
{
    std::vector<std::string> result;
    if (!list)
        return result;
    result.reserve(static_cast<size_t>(list->size()));
    for (int i = 0; i < list->size(); ++i)
    {
        const MegaDateSection* s = list->get(i);
        if (s && s->getGroupId())
            result.emplace_back(s->getGroupId());
    }
    return result;
}

// Pick the section with @p gid out of @p list, or nullptr if missing.
const MegaDateSection* findSection(MegaDateSectionList* list, const std::string& gid)
{
    if (!list)
        return nullptr;
    for (int i = 0; i < list->size(); ++i)
    {
        const MegaDateSection* s = list->get(i);
        if (s && s->getGroupId() && gid == s->getGroupId())
            return s;
    }
    return nullptr;
}

std::set<std::string> nodeNames(MegaNodeList* nodes)
{
    std::set<std::string> result;
    if (!nodes)
        return result;
    for (int i = 0; i < nodes->size(); ++i)
        result.emplace(nodes->get(i)->getName());
    return result;
}

MegaNode* nodeNamed(MegaNodeList* nodes, const std::string& name)
{
    if (!nodes)
        return nullptr;
    for (int i = 0; i < nodes->size(); ++i)
        if (name == nodes->get(i)->getName())
            return nodes->get(i);
    return nullptr;
}

} // namespace

/**
 * Fixture:
 *
 *  jul01.jpg                  mtime = 2024-07-01 00:00:00 UTC   size 100
 *  jul15.jpg                  mtime = 2024-07-15 00:00:00 UTC   size 200
 *  jun15.jpg                  mtime = 2024-06-15 00:00:00 UTC   size 300
 *  jul20.mp4                  mtime = 2024-07-20 00:00:00 UTC   size 400
 *  IMG_20231114_221321.jpg    mtime = 2024-07-01 00:00:00 UTC   size 500
 *                             mediats (from filename) = 2023-11-14 22:13:21 UTC
 *  notes.txt                  mtime = 2024-07-01 00:00:00 UTC   size 600
 *
 * PHOTO filter matches: jul01, jul15, jun15, IMG_20231114_221321 (4 nodes;
 * jul20.mp4 is a video, notes.txt is a document, both excluded).
 * DOCUMENT filter matches: notes.txt only.
 */
class SdkTestNodeDateSections: public SdkTestNodesSetUp
{
    const std::vector<NodeInfo>& getElements() const override
    {
        static const std::vector<NodeInfo> ELEMENTS{
            FileNodeInfo("jul01.jpg", {}, false, 100).setMtime(toTimePoint(kJul01Sec)),
            FileNodeInfo("jul15.jpg", {}, false, 200).setMtime(toTimePoint(kJul15Sec)),
            FileNodeInfo("jun15.jpg", {}, false, 300).setMtime(toTimePoint(kJun15Sec)),
            FileNodeInfo("jul20.mp4", {}, false, 400).setMtime(toTimePoint(kJul20Sec)),
            // Filename carries a capture timestamp in Nov 2023 while its mtime sits in
            // the existing 2024-07-01 bucket, so mediats and mtime group it differently.
            FileNodeInfo("IMG_20231114_221321.jpg", {}, false, 500)
                .setMtime(toTimePoint(kJul01Sec)),
            // A FILE_TYPE_DOCUMENT node, so a modification-order grouping over the
            // document category returns a real section while the mediats-order
            // grouping over the same category is rejected — empty vs non-empty is
            // what makes that distinction observable.
            FileNodeInfo("notes.txt", {}, false, 600).setMtime(toTimePoint(kJul01Sec)),
        };
        return ELEMENTS;
    }

    const std::string& getRootTestDir() const override
    {
        static const std::string dirName{"SDK_TEST_NODE_DATE_SECTIONS"};
        return dirName;
    }

    bool keepDifferentCreationTimes() override
    {
        return false;
    }
};

// ─── 1: section-list wrapper end-to-end ─────────────────────────────────────

TEST_F(SdkTestNodeDateSections, Public_GroupAllNodesByDate_ReturnsMegaDateSectionList)
{
    auto filter = makePhotoGroupFilter();
    std::unique_ptr<MegaDateSectionList> list(
        megaApi[0]->groupAllNodesByDate(filter.get(), MegaApi::ORDER_MODIFICATION_DESC, nullptr));

    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->size(), 2) << "expected one bucket per month (2024-07, 2024-06)";

    // gids are present and sorted DESC (newest first).
    const auto gids = toGids(list.get());
    EXPECT_THAT(gids, ElementsAre("2024-07", "2024-06"));
}

// ─── 1b: SECTION_GRANULARITY_DAY wiring ─────────────────────────────────────

TEST_F(SdkTestNodeDateSections, Public_GroupAllNodesByDate_Day)
{
    auto filter = makePhotoGroupFilter(MegaGroupNodesByDateFilter::SECTION_GRANULARITY_DAY);
    std::unique_ptr<MegaDateSectionList> list(
        megaApi[0]->groupAllNodesByDate(filter.get(), MegaApi::ORDER_MODIFICATION_DESC, nullptr));

    ASSERT_NE(list, nullptr);
    EXPECT_THAT(toGids(list.get()), ElementsAre("2024-07-15", "2024-07-01", "2024-06-15"));
}

// ─── 1c: SECTION_GRANULARITY_YEAR wiring ────────────────────────────────────

TEST_F(SdkTestNodeDateSections, Public_GroupAllNodesByDate_Year)
{
    auto filter = makePhotoGroupFilter(MegaGroupNodesByDateFilter::SECTION_GRANULARITY_YEAR);
    std::unique_ptr<MegaDateSectionList> list(
        megaApi[0]->groupAllNodesByDate(filter.get(), MegaApi::ORDER_MODIFICATION_DESC, nullptr));

    ASSERT_NE(list, nullptr);
    EXPECT_THAT(toGids(list.get()), ElementsAre("2024"));
}

// ─── 2: MegaDateSection accessor round-trip ─────────────────────────────────

TEST_F(SdkTestNodeDateSections, Public_MegaDateSection_AccessorsRoundTrip)
{
    auto filter = makePhotoGroupFilter();
    std::unique_ptr<MegaDateSectionList> list(
        megaApi[0]->groupAllNodesByDate(filter.get(), MegaApi::ORDER_MODIFICATION_DESC, nullptr));
    ASSERT_NE(list, nullptr);
    ASSERT_GT(list->size(), 0);

    const MegaDateSection* first = list->get(0);
    ASSERT_NE(first, nullptr);
    EXPECT_NE(first->getGroupId(), nullptr);
    EXPECT_GT(first->getCount(), 0);
    EXPECT_GT(first->getStartDate(), 0);
    EXPECT_GT(first->getEndDate(), first->getStartDate());
}

// ─── 3: DB-computed bounds surface through the wrapper chain ───────────────

TEST_F(SdkTestNodeDateSections, Public_StartEndDate_SurfaceFromDB)
{
    auto filter = makePhotoGroupFilter();
    std::unique_ptr<MegaDateSectionList> list(
        megaApi[0]->groupAllNodesByDate(filter.get(), MegaApi::ORDER_MODIFICATION_DESC, nullptr));
    ASSERT_NE(list, nullptr);

    const MegaDateSection* july = findSection(list.get(), "2024-07");
    ASSERT_NE(july, nullptr);
    EXPECT_EQ(july->getStartDate(), kJul01Sec); // 2024-07-01 UTC inclusive
    EXPECT_EQ(july->getEndDate(), kAug01Sec); // 2024-08-01 UTC exclusive
}

// ─── 4: byTimestampAnchor narrows + half-bound continues into earlier sections

TEST_F(SdkTestNodeDateSections, Public_ByTimestampAnchor_NarrowsListAllByPage)
{
    // Sections come from the group filter; the anchored page uses the listAll filter.
    auto groupFilter = makePhotoGroupFilter();
    std::unique_ptr<MegaDateSectionList> sections(
        megaApi[0]->groupAllNodesByDate(groupFilter.get(),
                                        MegaApi::ORDER_MODIFICATION_DESC,
                                        nullptr));
    ASSERT_NE(sections, nullptr);
    const MegaDateSection* june = findSection(sections.get(), "2024-06");
    ASSERT_NE(june, nullptr);

    auto filter = makePhotoListAllFilter();
    filter->byTimestampAnchor(june->getStartDate(),
                              june->getEndDate(),
                              MegaApi::ORDER_MODIFICATION_DESC);

    std::unique_ptr<MegaNodeList> page(
        megaApi[0]->listAllNodesByPage(filter.get(),
                                       MegaApi::ORDER_MODIFICATION_DESC,
                                       nullptr,
                                       /*maxElements=*/0,
                                       /*cursor=*/nullptr));
    ASSERT_NE(page, nullptr);

    const auto names = nodeNames(page.get());
    // DESC half-bound on the June section enforces mtime < endDate (= 2024-07-01),
    // so the newer July photos are excluded and only jun15 remains. If the anchor
    // were a no-op the July photos would leak in — that's what this pins.
    EXPECT_EQ(names.count("jun15.jpg"), 1u);
    EXPECT_EQ(names.count("jul01.jpg"), 0u);
    EXPECT_EQ(names.count("jul15.jpg"), 0u);
}

// ─── 5: null filter → empty list (no crash) ─────────────────────────────────

TEST_F(SdkTestNodeDateSections, Public_GroupAllNodesByDate_NullFilter_EmptyList)
{
    std::unique_ptr<MegaDateSectionList> list(
        megaApi[0]->groupAllNodesByDate(/*filter=*/nullptr,
                                        MegaApi::ORDER_MODIFICATION_DESC,
                                        nullptr));
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->size(), 0);
}

// ─── 6: unsupported order → empty list ──────────────────────────────────────

TEST_F(SdkTestNodeDateSections, Public_GroupAllNodesByDate_UnsupportedOrder_EmptyList)
{
    auto filter = makePhotoGroupFilter();
    std::unique_ptr<MegaDateSectionList> list(
        megaApi[0]->groupAllNodesByDate(filter.get(),
                                        MegaApi::ORDER_SIZE_DESC, // not a timestamp order
                                        nullptr));
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->size(), 0);
}

// ─── 6b: malformed UTC offset → empty list ──────────────────────────────────

TEST_F(SdkTestNodeDateSections, Public_GroupAllNodesByDate_InvalidUtcOffset_EmptyList)
{
    auto filter = makePhotoGroupFilter();
    filter->byUtcOffset("+9"); // malformed: not ±HH:MM
    std::unique_ptr<MegaDateSectionList> list(
        megaApi[0]->groupAllNodesByDate(filter.get(), MegaApi::ORDER_MODIFICATION_DESC, nullptr));
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->size(), 0) << "malformed UTC offset must yield an empty list";
}

// ─── 6c: valid UTC offset → non-empty list ──────────────────────────────────

TEST_F(SdkTestNodeDateSections, Public_GroupAllNodesByDate_ValidUtcOffset_NonEmpty)
{
    // A well-formed offset must NOT be rejected: the public path still returns
    // the section list (smoke test for the accept branch).
    auto filter = makePhotoGroupFilter();
    filter->byUtcOffset("+14:00");
    std::unique_ptr<MegaDateSectionList> list(
        megaApi[0]->groupAllNodesByDate(filter.get(), MegaApi::ORDER_MODIFICATION_DESC, nullptr));
    ASSERT_NE(list, nullptr);
    EXPECT_GT(list->size(), 0) << "valid UTC offset must be accepted";
}

// ─── 7: inverted byTimestampAnchor range → empty page ───────────────────────

TEST_F(SdkTestNodeDateSections, Public_ByTimestampAnchor_InvertedRange_EmptyPage)
{
    auto filter = makePhotoListAllFilter();
    // start > end is rejected by buildListAllParams (`startDate >= endDate`).
    filter->byTimestampAnchor(kAug01Sec, // start
                              kJul01Sec, // end (inverted)
                              MegaApi::ORDER_MODIFICATION_DESC);

    std::unique_ptr<MegaNodeList> page(
        megaApi[0]->listAllNodesByPage(filter.get(),
                                       MegaApi::ORDER_MODIFICATION_DESC,
                                       nullptr,
                                       /*maxElements=*/0,
                                       /*cursor=*/nullptr));
    ASSERT_NE(page, nullptr);
    EXPECT_EQ(page->size(), 0);
}

// ─── 4b: capture-date sections differ from modification-date sections ───────

TEST_F(SdkTestNodeDateSections, Public_GroupAllNodesByDate_MediaTsMonths)
{
    auto filter = makePhotoGroupFilter();
    std::unique_ptr<MegaDateSectionList> byMediaTs(
        megaApi[0]->groupAllNodesByDate(filter.get(), MegaApi::ORDER_MEDIATS_DESC, nullptr));
    ASSERT_NE(byMediaTs, nullptr);

    // IMG_20231114_221321.jpg buckets by its filename capture time, not its
    // 2024-07 mtime, so a month the modification-time grouping never reports appears.
    EXPECT_THAT(toGids(byMediaTs.get()), ElementsAre("2024-07", "2024-06", "2023-11"));

    const MegaDateSection* nov = findSection(byMediaTs.get(), "2023-11");
    ASSERT_NE(nov, nullptr);
    EXPECT_EQ(nov->getCount(), 1);

    std::unique_ptr<MegaDateSectionList> byMtime(
        megaApi[0]->groupAllNodesByDate(filter.get(), MegaApi::ORDER_MODIFICATION_DESC, nullptr));
    ASSERT_NE(byMtime, nullptr);
    EXPECT_EQ(findSection(byMtime.get(), "2023-11"), nullptr);
}

// ─── 4c: a mediats section anchors a mediats page ───────────────────────────

TEST_F(SdkTestNodeDateSections, Public_ByTimestampAnchor_MediaTsSectionScopesPage)
{
    auto groupFilter = makePhotoGroupFilter();
    std::unique_ptr<MegaDateSectionList> sections(
        megaApi[0]->groupAllNodesByDate(groupFilter.get(), MegaApi::ORDER_MEDIATS_DESC, nullptr));
    ASSERT_NE(sections, nullptr);
    const MegaDateSection* nov = findSection(sections.get(), "2023-11");
    ASSERT_NE(nov, nullptr);

    auto filter = makePhotoListAllFilter();
    filter->byTimestampAnchor(nov->getStartDate(), nov->getEndDate(), MegaApi::ORDER_MEDIATS_DESC);

    std::unique_ptr<MegaNodeList> page(megaApi[0]->listAllNodesByPage(filter.get(),
                                                                      MegaApi::ORDER_MEDIATS_DESC,
                                                                      nullptr,
                                                                      /*maxElements=*/0,
                                                                      /*cursor=*/nullptr));
    ASSERT_NE(page, nullptr);

    // The DESC half-bound enforces mediats < endDate, so the 2024 photos are gone.
    // Bounds leave the DB in UTC seconds and are scaled to ms at bind time — a
    // unit slip here would either match everything or nothing.
    const auto names = nodeNames(page.get());
    EXPECT_EQ(names.count("IMG_20231114_221321.jpg"), 1u);
    EXPECT_EQ(names.count("jul01.jpg"), 0u);
    EXPECT_EQ(names.count("jul15.jpg"), 0u);
    EXPECT_EQ(names.count("jun15.jpg"), 0u);
    EXPECT_EQ(static_cast<int64_t>(names.size()), nov->getCount()); // getCount() is int64_t

    // The capture time the public getter reports, in ms, derived from the filename while
    // this node's mtime is pinned to July — so it cannot come from mtime.
    MegaNode* nov14 = nodeNamed(page.get(), "IMG_20231114_221321.jpg");
    ASSERT_NE(nov14, nullptr);
    EXPECT_EQ(nov14->getMediaCaptureTimeMs(), kNov14Sec * 1000);
}

// ─── 4d: a mediats grouping over a non-media category is rejected ───────────

TEST_F(SdkTestNodeDateSections, Public_GroupAllNodesByDate_MediaTsNonMediaCategory_EmptyList)
{
    std::unique_ptr<MegaGroupNodesByDateFilter> filter{
        MegaGroupNodesByDateFilter::createInstance()};
    filter->byCategory(MegaApi::FILE_TYPE_DOCUMENT);
    filter->byGranularity(MegaGroupNodesByDateFilter::SECTION_GRANULARITY_MONTH);

    std::unique_ptr<MegaDateSectionList> list(
        megaApi[0]->groupAllNodesByDate(filter.get(), MegaApi::ORDER_MEDIATS_DESC, nullptr));
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->size(), 0);

    // The same category with a modification order IS accepted and returns a real
    // section, so the empty list above is the rejection and not merely "nothing
    // matched this category".
    std::unique_ptr<MegaDateSectionList> byMtime(
        megaApi[0]->groupAllNodesByDate(filter.get(), MegaApi::ORDER_MODIFICATION_DESC, nullptr));
    ASSERT_NE(byMtime, nullptr);
    EXPECT_GT(byMtime->size(), 0) << "modification-order grouping over FILE_TYPE_DOCUMENT "
                                     "must still be accepted";
    EXPECT_THAT(toGids(byMtime.get()), ElementsAre("2024-07"));
}
