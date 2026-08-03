#include "megaapi.h"
#include "megaapi_impl.h" // SubCategoryOverrideFilter subclasses MegaListAllNodesFilterPrivate
#include "SdkTestNodesSetUp.h"

#include <gmock/gmock.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace sdk_test;
using namespace testing;
using namespace std::chrono_literals;

namespace
{

std::vector<std::string> toNames(MegaNodeList* nodes)
{
    std::vector<std::string> result;
    result.reserve(static_cast<size_t>(nodes->size()));
    for (int i = 0; i < nodes->size(); ++i)
        result.emplace_back(nodes->get(i)->getName());
    return result;
}

std::vector<MegaHandle> toHandles(MegaNodeList* nodes)
{
    std::vector<MegaHandle> result;
    result.reserve(static_cast<size_t>(nodes->size()));
    for (int i = 0; i < nodes->size(); ++i)
        result.emplace_back(nodes->get(i)->getHandle());
    return result;
}

// Fetch the full unbounded ordered set for a filter and return its handles (empty
// if the call returns null — callers assert on the expected size).
std::vector<MegaHandle> fetchAllHandles(MegaApi* api,
                                        const MegaListAllNodesFilter* filter,
                                        int order)
{
    std::unique_ptr<MegaNodeList> full{
        api->listAllNodesByPageAtOffset(filter, order, nullptr, /*maxElements=*/0, /*offset=*/0)};
    return full ? toHandles(full.get()) : std::vector<MegaHandle>{};
}

// Slice [offset, offset+limit) clamped to size. Casts to ptrdiff_t explicitly so the
// vector-iterator arithmetic does not narrow on Win32 (ptrdiff_t is 32-bit there while
// offset is int64_t).
std::vector<MegaHandle> handleSlice(const std::vector<MegaHandle>& v, int64_t offset, size_t limit)
{
    const size_t from = std::min(static_cast<size_t>(offset), v.size());
    const size_t to = std::min(from + limit, v.size());
    return std::vector<MegaHandle>(v.begin() + static_cast<std::ptrdiff_t>(from),
                                   v.begin() + static_cast<std::ptrdiff_t>(to));
}

/**
 * @brief Build a MegaSearchCursorOffset from the last node of a page.
 *
 * Sets only the fields required by the given sort @p order.
 */
std::unique_ptr<MegaSearchCursorOffset> makeCursor(MegaNode* node, int order)
{
    std::unique_ptr<MegaSearchCursorOffset> cursor(MegaSearchCursorOffset::createInstance());
    cursor->setLastName(node->getName());
    cursor->setLastHandle(node->getHandle());
    switch (order)
    {
        case MegaApi::ORDER_SIZE_ASC:
        case MegaApi::ORDER_SIZE_DESC:
            cursor->setLastSize(node->getSize());
            break;
        case MegaApi::ORDER_MODIFICATION_ASC:
        case MegaApi::ORDER_MODIFICATION_DESC:
            cursor->setLastMtime(node->getModificationTime());
            break;
        case MegaApi::ORDER_LABEL_ASC:
        case MegaApi::ORDER_LABEL_DESC:
            cursor->setLastLabel(node->getLabel());
            break;
        case MegaApi::ORDER_FAV_ASC:
        case MegaApi::ORDER_FAV_DESC:
            cursor->setLastFav(node->isFavourite() ? 1 : 0);
            break;
        default:
            break;
    }
    return cursor;
}

/**
 * @brief Describes one pagination scenario: sort order and the expected page contents.
 *
 * pageSize is fixed at 2 throughout the pagination suite, so:
 *   page1 holds the first two results,
 *   page2 holds the remainder (1 node for the 3-node PHOTO set).
 */
struct PaginationTestCase
{
    const char* name;
    int order;
    std::vector<std::string> page1; // expected names on page 1
    std::vector<std::string> page2; // expected names on page 2
};

// Builds a filter suitable for the filter-based listAllNodesByPage overload.
// Sets byCategory to @p mimeType; leaves byLocationHandles / byLocation /
// byExcludeLocationHandles / bySensitivity at the MegaListAllNodesFilter
// defaults (Cloud + Vault scope, no exclusions, no sensitivity filter).
std::unique_ptr<MegaListAllNodesFilter> makeRootnodeFilter(int mimeType)
{
    std::unique_ptr<MegaListAllNodesFilter> f{MegaListAllNodesFilter::createInstance()};
    f->byCategory(mimeType);
    return f;
}

// Reports an arbitrary sub-category value, bypassing the validating bySubCategory() setter as
// a misbehaving app/binding subclass could. The setter clamps invalid input, so overriding the
// getter is the only way to drive an out-of-range value through parseListAllFilterIntoBase.
class SubCategoryOverrideFilter: public MegaListAllNodesFilterPrivate
{
public:
    explicit SubCategoryOverrideFilter(int subCategory):
        mSubCategory(subCategory)
    {}

    int bySubCategory() const override
    {
        return mSubCategory;
    }

private:
    int mSubCategory;
};

// Reports an arbitrary byFavourite value, bypassing the validating byFavourite() setter as a
// misbehaving app/binding subclass could. Mirrors SubCategoryOverrideFilter above.
class FavouriteOverrideFilter: public MegaListAllNodesFilterPrivate
{
public:
    explicit FavouriteOverrideFilter(int favourite):
        mFavourite(favourite)
    {}

    int byFavourite() const override
    {
        return mFavourite;
    }

private:
    int mFavourite;
};

// Build a MegaHandleList from a brace-init list of handles. Caller owns.
std::unique_ptr<MegaHandleList> handleList(std::initializer_list<MegaHandle> handles)
{
    std::unique_ptr<MegaHandleList> list{MegaHandleList::createInstance()};
    for (MegaHandle h: handles)
        list->addMegaHandle(h);
    return list;
}

/**
 * @brief Drives one full pagination sequence (page1 → page2 → empty) for a single test case.
 *
 * Uses SCOPED_TRACE so failures are attributed to @p tc.name.
 * ASSERT_* calls return from this helper on failure; the caller's loop continues.
 */
void checkPagination(MegaApi* api, int mimeType, const PaginationTestCase& tc, size_t pageSize)
{
    SCOPED_TRACE(tc.name);

    auto filter = makeRootnodeFilter(mimeType);
    std::unique_ptr<MegaNodeList> page1(
        api->listAllNodesByPage(filter.get(), tc.order, nullptr, pageSize, nullptr));
    ASSERT_NE(page1, nullptr);
    ASSERT_EQ(page1->size(), static_cast<int>(tc.page1.size()));
    EXPECT_THAT(toNames(page1.get()), ElementsAreArray(tc.page1));

    ASSERT_GT(page1->size(), 0);
    MegaNode* lastOfPage1 = page1->get(page1->size() - 1);
    ASSERT_NE(lastOfPage1, nullptr);
    auto cursor1 = makeCursor(lastOfPage1, tc.order);
    std::unique_ptr<MegaNodeList> page2(
        api->listAllNodesByPage(filter.get(), tc.order, nullptr, pageSize, cursor1.get()));
    ASSERT_NE(page2, nullptr);
    ASSERT_EQ(page2->size(), static_cast<int>(tc.page2.size()));
    EXPECT_THAT(toNames(page2.get()), ElementsAreArray(tc.page2));

    ASSERT_GT(page2->size(), 0);
    MegaNode* lastOfPage2 = page2->get(page2->size() - 1);
    ASSERT_NE(lastOfPage2, nullptr);
    auto cursor2 = makeCursor(lastOfPage2, tc.order);
    std::unique_ptr<MegaNodeList> page3(
        api->listAllNodesByPage(filter.get(), tc.order, nullptr, pageSize, cursor2.get()));
    ASSERT_NE(page3, nullptr);
    EXPECT_EQ(page3->size(), 0);
}

} // namespace

/**
 * Test suite for MegaApi::listAllNodesByPage().
 *
 * Node tree (3 photos, 2 videos, 2 docs):
 *
 *  Name             Ext    Type      Size  Mtime  Label         Fav
 *  alpha            .jpg   PHOTO      100    7h   RED(1)        false
 *  bravo            .mp4   VIDEO      200    6h   ORANGE(2)     false
 *  charlie          .docx  DOCUMENT   300    5h   YELLOW(3)     true
 *  delta            .jpg   PHOTO      400    4h   GREEN(4)      true
 *  echo             .mp4   VIDEO      500    3h   BLUE(5)       false
 *  foxtrot          .docx  DOCUMENT   600    2h   PURPLE(6)     false
 *  golf             .jpg   PHOTO      700    1h   GREY(7)       true
 *
 * PHOTO filter matches: alpha.jpg, delta.jpg, golf.jpg
 * ALL_VISUAL_MEDIA filter matches: alpha.jpg, bravo.mp4, delta.jpg, echo.mp4, golf.jpg
 * Docs (charlie.docx, foxtrot.docx) must not appear in either query.
 */
class SdkTestListAllNodesByPage: public SdkTestNodesSetUp
{
    const std::vector<NodeInfo>& getElements() const override
    {
        // name               label                        fav    size  mtime-age
        static const std::vector<NodeInfo> ELEMENTS{
            FileNodeInfo("alpha.jpg", MegaNode::NODE_LBL_RED, false, 100, 7h),
            FileNodeInfo("bravo.mp4", MegaNode::NODE_LBL_ORANGE, false, 200, 6h),
            FileNodeInfo("charlie.docx", MegaNode::NODE_LBL_YELLOW, true, 300, 5h),
            FileNodeInfo("delta.jpg", MegaNode::NODE_LBL_GREEN, true, 400, 4h),
            FileNodeInfo("echo.mp4", MegaNode::NODE_LBL_BLUE, false, 500, 3h),
            FileNodeInfo("foxtrot.docx", MegaNode::NODE_LBL_PURPLE, false, 600, 2h),
            FileNodeInfo("golf.jpg", MegaNode::NODE_LBL_GREY, true, 700, 1h),
        };
        return ELEMENTS;
    }

    const std::string& getRootTestDir() const override
    {
        static const std::string dirName{"SDK_TEST_LISTALLNODESBYPAGE"};
        return dirName;
    }

    bool keepDifferentCreationTimes() override
    {
        return false;
    }
};

// ─── Group 1: MIME type filtering, no cursor (all results) ─────

TEST_F(SdkTestListAllNodesByPage, MimeFilter_AllResults)
{
    struct AllResultsCase
    {
        const char* name;
        int mimeType;
        int order;
        std::vector<std::string> expected;
    };

    // clang-format off
    static const std::vector<AllResultsCase> kCases{
        {"PHOTO/DEFAULT_ASC",
         MegaApi::FILE_TYPE_PHOTO, MegaApi::ORDER_DEFAULT_ASC,
         {"alpha.jpg", "delta.jpg", "golf.jpg"}},
        {"ALL_VISUAL_MEDIA/DEFAULT_ASC",
         MegaApi::FILE_TYPE_ALL_VISUAL_MEDIA, MegaApi::ORDER_DEFAULT_ASC,
         {"alpha.jpg", "bravo.mp4", "delta.jpg", "echo.mp4", "golf.jpg"}},
    };
    // clang-format on

    for (const auto& tc: kCases)
    {
        SCOPED_TRACE(tc.name);
        auto filter = makeRootnodeFilter(tc.mimeType);
        std::unique_ptr<MegaNodeList> results(
            megaApi[0]->listAllNodesByPage(filter.get(), tc.order, nullptr, 0, nullptr));
        ASSERT_NE(results, nullptr);
        EXPECT_THAT(toNames(results.get()), ElementsAreArray(tc.expected));
    }
}

// ─── Group 2: All orders, PHOTO filter, pageSize=2 ───────────────────────────
//
// All 10 sort orders are driven by the table below.  Each row specifies the
// sort order and the two non-empty pages expected from the 3-node PHOTO set
// {alpha.jpg(size=100,mtime=-7h,label=1,fav=0),
//  delta.jpg(size=400,mtime=-4h,label=4,fav=1),
//  golf.jpg (size=700,mtime=-1h,label=7,fav=1)}.

TEST_F(SdkTestListAllNodesByPage, AllOrders_Photo_Pagination)
{
    // clang-format off
    static const std::vector<PaginationTestCase> kCases{
        // name             order                            page1                          page2
        {"DEFAULT_ASC",  MegaApi::ORDER_DEFAULT_ASC,  {"alpha.jpg", "delta.jpg"}, {"golf.jpg"}},
        {"DEFAULT_DESC", MegaApi::ORDER_DEFAULT_DESC, {"golf.jpg",  "delta.jpg"}, {"alpha.jpg"}},
        {"SIZE_ASC",     MegaApi::ORDER_SIZE_ASC,     {"alpha.jpg", "delta.jpg"}, {"golf.jpg"}},
        {"SIZE_DESC",    MegaApi::ORDER_SIZE_DESC,    {"golf.jpg",  "delta.jpg"}, {"alpha.jpg"}},
        {"MTIME_ASC",    MegaApi::ORDER_MODIFICATION_ASC,  {"alpha.jpg", "delta.jpg"}, {"golf.jpg"}},
        {"MTIME_DESC",   MegaApi::ORDER_MODIFICATION_DESC, {"golf.jpg",  "delta.jpg"}, {"alpha.jpg"}},
        {"LABEL_ASC",    MegaApi::ORDER_LABEL_ASC,    {"alpha.jpg", "delta.jpg"}, {"golf.jpg"}},
        {"LABEL_DESC",   MegaApi::ORDER_LABEL_DESC,   {"golf.jpg",  "delta.jpg"}, {"alpha.jpg"}},
        // FAV_ASC  = ORDER BY fav DESC, name ASC → favs (delta,golf) first, then alpha
        {"FAV_ASC",      MegaApi::ORDER_FAV_ASC,      {"delta.jpg", "golf.jpg"},  {"alpha.jpg"}},
        // FAV_DESC = ORDER BY fav ASC,  name ASC → non-fav (alpha) first, then delta,golf
        {"FAV_DESC",     MegaApi::ORDER_FAV_DESC,     {"alpha.jpg", "delta.jpg"}, {"golf.jpg"}},
    };
    // clang-format on

    for (const auto& tc: kCases)
        ASSERT_NO_FATAL_FAILURE(checkPagination(megaApi[0].get(), MegaApi::FILE_TYPE_PHOTO, tc, 2));
}

// ─── Group 3: Invalid inputs ──────────────────────────────────────────────────

TEST_F(SdkTestListAllNodesByPage, InvalidInputs_ReturnEmpty)
{
    auto expectEmpty = [&](int mimeType, int order, MegaSearchCursorOffset* cursor)
    {
        auto filter = makeRootnodeFilter(mimeType);
        std::unique_ptr<MegaNodeList> r(
            megaApi[0]->listAllNodesByPage(filter.get(), order, nullptr, 0, cursor));
        ASSERT_NE(r, nullptr);
        EXPECT_EQ(r->size(), 0);
    };

    // ── nullptr filter ────────────────────────────────────────────────────────
    // buildListAllParams returns nullopt → MegaApiImpl returns an empty
    // MegaNodeList rather than crashing. Pin the public-API contract.
    {
        SCOPED_TRACE("nullptr filter");
        std::unique_ptr<MegaNodeList> r(
            megaApi[0]->listAllNodesByPage(static_cast<MegaListAllNodesFilter*>(nullptr),
                                           MegaApi::ORDER_DEFAULT_ASC,
                                           nullptr,
                                           0,
                                           nullptr));
        ASSERT_NE(r, nullptr) << "API must return a non-null empty list, not crash";
        EXPECT_EQ(r->size(), 0);
    }

    // ── Invalid MIME types ────────────────────────────────────────────────────
    // mimeType <= FILE_TYPE_DEFAULT (0), > FILE_TYPE_LAST, or negative are all disallowed.

    struct InvalidMimeCase
    {
        const char* name;
        int mimeType;
    };

    // clang-format off
    const std::vector<InvalidMimeCase> mimeCases{
        {"FILE_TYPE_DEFAULT", MegaApi::FILE_TYPE_DEFAULT},
        {"FILE_TYPE_LAST+1",  MegaApi::FILE_TYPE_LAST + 1},
        {"negative (-1)",     -1},
    };
    // clang-format on

    for (const auto& tc: mimeCases)
    {
        SCOPED_TRACE(tc.name);
        expectEmpty(tc.mimeType, MegaApi::ORDER_DEFAULT_ASC, nullptr);
    }

    // ── Out-of-range sub-category ─────────────────────────────────────────────
    // The bySubCategory setter clamps invalid input, but a misbehaving app/binding subclass
    // could override the getter. parseListAllFilterIntoBase must reject any value outside
    // [FILE_SUBTYPE_NONE, FILE_SUBTYPE_RAW] → empty. Boundaries around the valid range.

    struct InvalidSubCategoryCase
    {
        const char* name;
        int subCategory;
    };

    // clang-format off
    const std::vector<InvalidSubCategoryCase> subCategoryCases{
        {"just below NONE (-1)", MegaNodeScopeFilter::FILE_SUBTYPE_NONE - 1},
        {"just above RAW",       MegaNodeScopeFilter::FILE_SUBTYPE_RAW + 1},
        {"large positive",       999},
        {"INT_MIN",              std::numeric_limits<int>::min()},
        {"INT_MAX",              std::numeric_limits<int>::max()},
    };
    // clang-format on

    for (const auto& tc: subCategoryCases)
    {
        SCOPED_TRACE(tc.name);
        SubCategoryOverrideFilter filter(tc.subCategory);
        filter.byCategory(MegaApi::FILE_TYPE_PHOTO);
        std::unique_ptr<MegaNodeList> r(megaApi[0]->listAllNodesByPage(&filter,
                                                                       MegaApi::ORDER_DEFAULT_ASC,
                                                                       nullptr,
                                                                       0,
                                                                       nullptr));
        ASSERT_NE(r, nullptr);
        EXPECT_EQ(r->size(), 0);
    }

    // ── Unsupported order ─────────────────────────────────────────────────────
    // Rejected regardless of whether a cursor is supplied.
    expectEmpty(MegaApi::FILE_TYPE_PHOTO, MegaApi::ORDER_CREATION_ASC, nullptr);

    // ── Invalid cursors ───────────────────────────────────────────────────────

    const std::unique_ptr<MegaNode> alpha(
        megaApi[0]->getNodeByPath(convertToTestPath("alpha.jpg").c_str()));
    ASSERT_NE(alpha, nullptr);

    // Each case: a description, the sort order, and a lambda that configures the cursor.
    // Fields not set by the lambda stay at their invalid default values.
    struct InvalidCursorCase
    {
        const char* name;
        int order;
        std::function<void(MegaSearchCursorOffset*)> setup;
    };

    // clang-format off
    const std::vector<InvalidCursorCase> cursorCases{
        {"empty name (omit setLastName)",         MegaApi::ORDER_DEFAULT_ASC,
         [&](MegaSearchCursorOffset* c){ c->setLastHandle(alpha->getHandle()); }},
        {"invalid handle (omit setLastHandle)",   MegaApi::ORDER_DEFAULT_ASC,
         [&](MegaSearchCursorOffset* c){ c->setLastName(alpha->getName()); }},
        {"missing size for SIZE order",           MegaApi::ORDER_SIZE_ASC,
         [&](MegaSearchCursorOffset* c){ c->setLastName(alpha->getName()); c->setLastHandle(alpha->getHandle()); }},
        {"missing mtime for MODIFICATION order",  MegaApi::ORDER_MODIFICATION_ASC,
         [&](MegaSearchCursorOffset* c){ c->setLastName(alpha->getName()); c->setLastHandle(alpha->getHandle()); }},
        {"out-of-range label (99) for LABEL order", MegaApi::ORDER_LABEL_ASC,
         [&](MegaSearchCursorOffset* c){ c->setLastName(alpha->getName()); c->setLastHandle(alpha->getHandle()); c->setLastLabel(99); }},
        {"invalid fav (2) for FAV order",         MegaApi::ORDER_FAV_ASC,
         [&](MegaSearchCursorOffset* c){ c->setLastName(alpha->getName()); c->setLastHandle(alpha->getHandle()); c->setLastFav(2); }},
        {"unsupported order with cursor (ORDER_CREATION_ASC)", MegaApi::ORDER_CREATION_ASC,
         [&](MegaSearchCursorOffset* c){ c->setLastName(alpha->getName()); c->setLastHandle(alpha->getHandle()); }},
    };
    // clang-format on

    for (const auto& tc: cursorCases)
    {
        SCOPED_TRACE(tc.name);
        std::unique_ptr<MegaSearchCursorOffset> cursor(MegaSearchCursorOffset::createInstance());
        tc.setup(cursor.get());
        expectEmpty(MegaApi::FILE_TYPE_PHOTO, tc.order, cursor.get());
    }
}

// Out-of-range byFavourite value, injected via FavouriteOverrideFilter bypassing the validating
// setter. parseListAllFilterIntoBase must reject any value outside
// [BOOL_FILTER_DISABLED, BOOL_FILTER_ONLY_FALSE] → empty. Mirrors the subCategoryCases loop above.
TEST_F(SdkTestListAllNodesByPage, FavouriteInvalidValueReturnsEmpty)
{
    struct InvalidFavouriteCase
    {
        const char* name;
        int favourite;
    };

    // clang-format off
    const std::vector<InvalidFavouriteCase> favouriteCases{
        {"just below DISABLED (-1)", MegaNodeScopeFilter::BOOL_FILTER_DISABLED - 1},
        {"just above ONLY_FALSE",    MegaNodeScopeFilter::BOOL_FILTER_ONLY_FALSE + 1},
        {"INT_MIN",                  std::numeric_limits<int>::min()},
        {"INT_MAX",                  std::numeric_limits<int>::max()},
    };
    // clang-format on

    for (const auto& tc: favouriteCases)
    {
        SCOPED_TRACE(tc.name);
        FavouriteOverrideFilter filter(tc.favourite);
        filter.byCategory(MegaApi::FILE_TYPE_PHOTO);
        std::unique_ptr<MegaNodeList> r(megaApi[0]->listAllNodesByPage(&filter,
                                                                       MegaApi::ORDER_DEFAULT_ASC,
                                                                       nullptr,
                                                                       0,
                                                                       nullptr));
        ASSERT_NE(r, nullptr);
        EXPECT_EQ(r->size(), 0);
    }
}

// ─── Group 4: Legacy 5-arg API forwards to filter overload ───────────────────
//
// The 5-arg legacy overload internally constructs a MegaListAllNodesFilter with
// byCategory(mimeType) only — the default (unset) byLocationHandle implicitly
// applies the Cloud + Vault rootnode scope. A single-page equality assertion
// is enough; cursor behaviour is covered by checkPagination() which runs
// against the filter overload directly on the same dataset.
// pageSize=2 (rather than 0/unbounded) keeps the implicit LIMIT path on the
// legacy overload covered; both calls then return the same first-page slice.

TEST_F(SdkTestListAllNodesByPage, LegacyApi_ForwardsToFilter)
{
    const int mimeType = MegaApi::FILE_TYPE_PHOTO;
    const int order = MegaApi::ORDER_DEFAULT_ASC;
    const size_t pageSize = 2;

    std::unique_ptr<MegaNodeList> legacy(
        megaApi[0]->listAllNodesByPage(mimeType, order, nullptr, pageSize, nullptr));

    auto filter = makeRootnodeFilter(mimeType);
    std::unique_ptr<MegaNodeList> modern(
        megaApi[0]->listAllNodesByPage(filter.get(), order, nullptr, pageSize, nullptr));

    ASSERT_NE(legacy, nullptr);
    ASSERT_NE(modern, nullptr);
    EXPECT_THAT(toNames(legacy.get()), ElementsAreArray(toNames(modern.get())));
}

// ─── Group 5: Filter API — sub-folder scope and bySensitivity ────────────────
//
// (Moved up from former Group 7. Former Group 5 — single-handle byLocationHandles
// equal to testRoot — was removed: it asserted the default-scope full set on a
// flat fixture, fully covered by Groups 7-8 below in the nested-tree fixture
// and by Sqlite_test.cpp::ExplicitAncestor_CloudHandle_OnlyCloudNodesReturned
// at the SQL layer.)
//
// A separate fixture with a nested tree and sensitive nodes so the byCategory
// dataset of SdkTestListAllNodesByPage stays flat. This suite exercises:
//   - byLocationHandle pointing at a strict descendant of the test root, so the
//     result set is genuinely narrower than the default Cloud+Vault scope.
//   - bySensitivity SENSITIVITY_HIDE_SENSITIVE, covering both own-flag
//     exclusion and ancestor-flag inheritance.
//
// Node tree:
//
//   outer.jpg                  PHOTO   sensitive=false
//   sensitive_top.jpg          PHOTO   sensitive=true   (own flag)
//   sub_normal/                DIR     sensitive=false
//     inner1.jpg               PHOTO   sensitive=false
//     inner2.jpg               PHOTO   sensitive=false
//   sub_sensitive/             DIR     sensitive=true
//     sub_inherited.jpg        PHOTO   sensitive=false  (inherits from parent)

class SdkTestListAllNodesByPageScope: public SdkTestNodesSetUp
{
    const std::vector<NodeInfo>& getElements() const override
    {
        static const std::vector<NodeInfo> ELEMENTS{
            FileNodeInfo("outer.jpg"),
            FileNodeInfo("sensitive_top.jpg").setSensitive(true),
            DirNodeInfo("sub_normal")
                .addChild(FileNodeInfo("inner1.jpg"))
                .addChild(FileNodeInfo("inner2.jpg")),
            DirNodeInfo("sub_sensitive")
                .setSensitive(true)
                .addChild(FileNodeInfo("sub_inherited.jpg")),
        };
        return ELEMENTS;
    }

    const std::string& getRootTestDir() const override
    {
        static const std::string dirName{"SDK_TEST_LISTALLNODESBYPAGE_SCOPE"};
        return dirName;
    }

    bool keepDifferentCreationTimes() override
    {
        return false;
    }
};

// byLocationHandles={sub_normal} must restrict the result set to that
// sub-folder's photos only — narrower than the default Cloud+Vault scope which
// would also include outer.jpg, sensitive_top.jpg, and sub_inherited.jpg.

TEST_F(SdkTestListAllNodesByPageScope, FilterApi_ByLocationHandles_NarrowerSubFolder)
{
    const std::unique_ptr<MegaNode> subNormal(
        megaApi[0]->getNodeByPath(convertToTestPath("sub_normal").c_str()));
    ASSERT_NE(subNormal, nullptr);

    std::unique_ptr<MegaListAllNodesFilter> filter{MegaListAllNodesFilter::createInstance()};
    filter->byCategory(MegaApi::FILE_TYPE_PHOTO);
    auto includes = handleList({subNormal->getHandle()});
    filter->byLocationHandles(includes.get());

    std::unique_ptr<MegaNodeList> results(megaApi[0]->listAllNodesByPage(filter.get(),
                                                                         MegaApi::ORDER_DEFAULT_ASC,
                                                                         nullptr,
                                                                         0,
                                                                         nullptr));
    ASSERT_NE(results, nullptr);
    EXPECT_THAT(toNames(results.get()), ElementsAreArray({"inner1.jpg", "inner2.jpg"}));
}

// bySensitivity SENSITIVITY_HIDE_SENSITIVE must drop nodes whose own flag is
// set (sensitive_top.jpg) and nodes inheriting a sensitive strict ancestor
// below the matched root (sub_inherited.jpg under sub_sensitive).

TEST_F(SdkTestListAllNodesByPageScope, FilterApi_BySensitivity_ExcludeSensitive)
{
    std::unique_ptr<MegaListAllNodesFilter> filter{MegaListAllNodesFilter::createInstance()};
    filter->byCategory(MegaApi::FILE_TYPE_PHOTO);
    filter->bySensitivity(MegaListAllNodesFilter::SENSITIVITY_HIDE_SENSITIVE);

    std::unique_ptr<MegaNodeList> results(megaApi[0]->listAllNodesByPage(filter.get(),
                                                                         MegaApi::ORDER_DEFAULT_ASC,
                                                                         nullptr,
                                                                         0,
                                                                         nullptr));
    ASSERT_NE(results, nullptr);
    EXPECT_THAT(toNames(results.get()),
                ElementsAreArray({"inner1.jpg", "inner2.jpg", "outer.jpg"}));
}

// ─── Group 6: Filter API — byLocationHandles (multiple handles) ──────────────
//
// End-to-end path coverage for the list-form replacement of byLocationHandle.
// Confirms the wire-up MegaApi → buildListAllParams → resolveListAllRoots →
// SQL bind loop works for two ancestors at once. Nested-tree fixture so
// sub_normal and sub_sensitive yield distinct, observable subsets.

TEST_F(SdkTestListAllNodesByPageScope, FilterApi_ByLocationHandles_TwoAncestors_PathCoverage)
{
    const std::unique_ptr<MegaNode> subNormal(
        megaApi[0]->getNodeByPath(convertToTestPath("sub_normal").c_str()));
    const std::unique_ptr<MegaNode> subSensitive(
        megaApi[0]->getNodeByPath(convertToTestPath("sub_sensitive").c_str()));
    ASSERT_NE(subNormal, nullptr);
    ASSERT_NE(subSensitive, nullptr);

    std::unique_ptr<MegaListAllNodesFilter> filter{MegaListAllNodesFilter::createInstance()};
    filter->byCategory(MegaApi::FILE_TYPE_PHOTO);
    auto includes = handleList({subNormal->getHandle(), subSensitive->getHandle()});
    filter->byLocationHandles(includes.get());

    std::unique_ptr<MegaNodeList> results(megaApi[0]->listAllNodesByPage(filter.get(),
                                                                         MegaApi::ORDER_DEFAULT_ASC,
                                                                         nullptr,
                                                                         0,
                                                                         nullptr));
    ASSERT_NE(results, nullptr);
    EXPECT_THAT(toNames(results.get()),
                ElementsAreArray({"inner1.jpg", "inner2.jpg", "sub_inherited.jpg"}))
        << "union of sub_normal and sub_sensitive subtrees expected; outer.jpg / "
           "sensitive_top.jpg must NOT appear because they are not under either ancestor";
}

// ─── Group 7: Filter API — byExcludeLocationHandles ──────────────────────────
//
// End-to-end path coverage for the new exclude list. Excluding sub_normal at
// the API surface must reach the SQL excSeen accumulator: every descendant of
// sub_normal (inner1.jpg, inner2.jpg) is dropped while the rest of the tree
// remains.

TEST_F(SdkTestListAllNodesByPageScope, FilterApi_ByExcludeLocationHandles_PathCoverage)
{
    const std::unique_ptr<MegaNode> subNormal(
        megaApi[0]->getNodeByPath(convertToTestPath("sub_normal").c_str()));
    ASSERT_NE(subNormal, nullptr);

    std::unique_ptr<MegaListAllNodesFilter> filter{MegaListAllNodesFilter::createInstance()};
    filter->byCategory(MegaApi::FILE_TYPE_PHOTO);
    auto excludes = handleList({subNormal->getHandle()});
    filter->byExcludeLocationHandles(excludes.get());

    std::unique_ptr<MegaNodeList> results(megaApi[0]->listAllNodesByPage(filter.get(),
                                                                         MegaApi::ORDER_DEFAULT_ASC,
                                                                         nullptr,
                                                                         0,
                                                                         nullptr));
    ASSERT_NE(results, nullptr);
    const auto names = toNames(results.get());
    EXPECT_THAT(names, Not(Contains(std::string("inner1.jpg"))));
    EXPECT_THAT(names, Not(Contains(std::string("inner2.jpg"))));
    EXPECT_THAT(names, Contains(std::string("outer.jpg")))
        << "Sibling files outside the excluded folder must remain";
    EXPECT_THAT(names, Contains(std::string("sub_inherited.jpg")))
        << "Other subtrees must remain unaffected";
}

// ─── Group 8: Filter API — byLocation scope selector ─────────────────────────
//
// End-to-end path coverage for byLocation. Three values map to three distinct
// rootnode sets at the NodeManager layer; here we pin the public-API path.
// The Cloud-only mode is the user-visible answer to "exclude My Backups".

TEST_F(SdkTestListAllNodesByPageScope, FilterApi_ByLocation_CloudDriveOnly_PathCoverage)
{
    std::unique_ptr<MegaListAllNodesFilter> filter{MegaListAllNodesFilter::createInstance()};
    filter->byCategory(MegaApi::FILE_TYPE_PHOTO);
    filter->byLocation(MegaListAllNodesFilter::LOCATION_CLOUD_DRIVE);

    std::unique_ptr<MegaNodeList> results(megaApi[0]->listAllNodesByPage(filter.get(),
                                                                         MegaApi::ORDER_DEFAULT_ASC,
                                                                         nullptr,
                                                                         0,
                                                                         nullptr));
    ASSERT_NE(results, nullptr);
    // Sanity: at least one of the fixture's Cloud-Drive photos must be returned.
    // Vault content cannot be asserted here because the integration fixture does
    // not provision Vault rootnodes via SDS — covered exhaustively in
    // tests/unit/Sqlite_test.cpp::ListAllNodesByPageTest.LocationScope_*.
    EXPECT_GT(results->size(), 0)
        << "Cloud-Drive-scoped query must return the fixture's Cloud photos";
}

// ─── Group 9: Filter API — boundary / validation ─────────────────────────────
//
// All boundary cases pin the contract that buildListAllParams validation kicks
// in BEFORE any DB work — the public API returns an empty MegaNodeList instead
// of crashing or running a partially-valid query. Per Phase 2 plan, SQL
// semantics correctness is covered exhaustively in the unit tests.

// Consolidated reject-path test. All five inputs trip buildListAllParams
// validation: oversize include / exclude lists (>MAX_LOCATION_HANDLES), an
// INVALID_HANDLE entry in either list, and an out-of-range byLocation enum.
// All cases must return a non-null empty MegaNodeList. Bundled into one
// TEST_F to avoid paying the integration fixture login + tree setup cost
// (~40s) per case — mirrors the existing InvalidInputs_ReturnEmpty pattern.
TEST_F(SdkTestListAllNodesByPageScope, Boundary_RejectInvalidInputs_ReturnEmpty)
{
    const std::unique_ptr<MegaNode> rootDir(getRootTestDirectory()->copy());
    ASSERT_NE(rootDir, nullptr);

    auto expectEmpty =
        [&](const char* label, const std::function<void(MegaListAllNodesFilter*)>& configure)
    {
        SCOPED_TRACE(label);
        std::unique_ptr<MegaListAllNodesFilter> filter{MegaListAllNodesFilter::createInstance()};
        filter->byCategory(MegaApi::FILE_TYPE_PHOTO);
        configure(filter.get());

        std::unique_ptr<MegaNodeList> results(
            megaApi[0]->listAllNodesByPage(filter.get(),
                                           MegaApi::ORDER_DEFAULT_ASC,
                                           nullptr,
                                           0,
                                           nullptr));
        ASSERT_NE(results, nullptr);
        EXPECT_EQ(results->size(), 0);
    };

    // 1. byLocationHandles size > MAX_LOCATION_HANDLES (=3) → rejected.
    expectEmpty("oversize byLocationHandles (4 entries)",
                [&](MegaListAllNodesFilter* f)
                {
                    auto inc = handleList({rootDir->getHandle(),
                                           rootDir->getHandle(),
                                           rootDir->getHandle(),
                                           rootDir->getHandle()});
                    f->byLocationHandles(inc.get());
                });

    // 2. byExcludeLocationHandles size > MAX_LOCATION_HANDLES → rejected.
    expectEmpty("oversize byExcludeLocationHandles (4 entries)",
                [&](MegaListAllNodesFilter* f)
                {
                    auto exc = handleList({rootDir->getHandle(),
                                           rootDir->getHandle(),
                                           rootDir->getHandle(),
                                           rootDir->getHandle()});
                    f->byExcludeLocationHandles(exc.get());
                });

    // 3. INVALID_HANDLE in include list → rejected.
    expectEmpty("INVALID_HANDLE in byLocationHandles",
                [&](MegaListAllNodesFilter* f)
                {
                    auto inc = handleList({INVALID_HANDLE});
                    f->byLocationHandles(inc.get());
                });

    // 4. INVALID_HANDLE in exclude list → rejected.
    expectEmpty("INVALID_HANDLE in byExcludeLocationHandles",
                [&](MegaListAllNodesFilter* f)
                {
                    auto exc = handleList({INVALID_HANDLE});
                    f->byExcludeLocationHandles(exc.get());
                });

    // 5. byLocation outside the LOCATION_* enum range → rejected.
    expectEmpty("byLocation out of range (99)",
                [&](MegaListAllNodesFilter* f)
                {
                    f->byLocation(99);
                });
}

TEST_F(SdkTestListAllNodesByPageScope, Boundary_NullHandleLists_AreEquivalentToUnset)
{
    // Pass nullptr to either list setter — must behave exactly like never
    // calling the setter (default scope, no exclusions).
    std::unique_ptr<MegaListAllNodesFilter> filter{MegaListAllNodesFilter::createInstance()};
    filter->byCategory(MegaApi::FILE_TYPE_PHOTO);
    filter->byLocationHandles(nullptr);
    filter->byExcludeLocationHandles(nullptr);

    std::unique_ptr<MegaNodeList> results(megaApi[0]->listAllNodesByPage(filter.get(),
                                                                         MegaApi::ORDER_DEFAULT_ASC,
                                                                         nullptr,
                                                                         0,
                                                                         nullptr));
    ASSERT_NE(results, nullptr);
    // Default Cloud+Vault scope returns the entire fixture tree (4 photos).
    EXPECT_THAT(
        toNames(results.get()),
        ElementsAreArray(
            {"inner1.jpg", "inner2.jpg", "outer.jpg", "sensitive_top.jpg", "sub_inherited.jpg"}));
}

// ─── Group 10: Offset-based windowing (fast-scroller support) ────────────────
//
// These four tests cover the new offset overload. They use
// the SdkTestListAllNodesByPage fixture (3 photos: alpha.jpg, delta.jpg, golf.jpg in
// ORDER_MODIFICATION_DESC → golf, delta, alpha; plus 2 videos bravo.mp4, echo.mp4 so
// FILE_TYPE_ALL_VISUAL_MEDIA spans both grouped-CTE routes).

// Negative offset must be rejected: the API returns a non-null empty list
// instead of crashing or running a partially-valid query.
TEST_F(SdkTestListAllNodesByPage, Offset_NegativeRejected)
{
    std::unique_ptr<MegaListAllNodesFilter> filter{MegaListAllNodesFilter::createInstance()};
    filter->byCategory(MegaApi::FILE_TYPE_PHOTO);
    std::unique_ptr<MegaNodeList> r{
        megaApi[0]->listAllNodesByPageAtOffset(filter.get(),
                                               MegaApi::ORDER_MODIFICATION_DESC,
                                               nullptr,
                                               /*maxElements=*/10,
                                               /*offset=*/-1)};
    ASSERT_NE(r, nullptr) << "API must return a non-null empty list, not crash";
    EXPECT_EQ(r->size(), 0);
}

// End-to-end check: skipping offset=1 with maxElements=2 must yield the 2nd
// and 3rd items of the unbounded ORDER_MODIFICATION_DESC result set.
TEST_F(SdkTestListAllNodesByPage, Offset_PublicOverload_EndToEnd)
{
    std::unique_ptr<MegaListAllNodesFilter> filter{MegaListAllNodesFilter::createInstance()};
    filter->byCategory(MegaApi::FILE_TYPE_PHOTO);
    const int order = MegaApi::ORDER_MODIFICATION_DESC;

    const std::vector<MegaHandle> expected = fetchAllHandles(megaApi[0].get(), filter.get(), order);
    ASSERT_GE(expected.size(), 3u);

    // Skip the first item and take the next two.
    const int64_t offset = 1;
    const size_t limit = 2;
    std::unique_ptr<MegaNodeList> window{
        megaApi[0]->listAllNodesByPageAtOffset(filter.get(),
                                               order,
                                               nullptr,
                                               /*maxElements=*/limit,
                                               offset)};
    ASSERT_NE(window, nullptr);
    EXPECT_EQ(toHandles(window.get()), handleSlice(expected, offset, limit));
}

// Grouped category (ALL_VISUAL_MEDIA = photos + videos) drives the UNION-ALL CTE
// path through the full public stack. The fixture has both photos and videos, so
// this exercises a real multi-route merge + offset end-to-end, not just the
// single-route PHOTO path of the test above.
TEST_F(SdkTestListAllNodesByPage, Offset_GroupedMime_EndToEnd)
{
    std::unique_ptr<MegaListAllNodesFilter> filter{MegaListAllNodesFilter::createInstance()};
    filter->byCategory(MegaApi::FILE_TYPE_ALL_VISUAL_MEDIA);
    const int order = MegaApi::ORDER_MODIFICATION_DESC;

    const std::vector<MegaHandle> expected = fetchAllHandles(megaApi[0].get(), filter.get(), order);
    ASSERT_GE(expected.size(), 3u) << "fixture should have >=3 visual-media nodes";

    const int64_t offset = 1;
    const size_t limit = 2;
    std::unique_ptr<MegaNodeList> window{
        megaApi[0]->listAllNodesByPageAtOffset(filter.get(),
                                               order,
                                               nullptr,
                                               /*maxElements=*/limit,
                                               offset)};
    ASSERT_NE(window, nullptr);
    EXPECT_EQ(toHandles(window.get()), handleSlice(expected, offset, limit));
}

// Fast-scroller flow: obtain date sections from groupAllNodesByDate, then use
// byTimestampAnchor + offset to fetch the window for the first section.
TEST_F(SdkTestListAllNodesByPage, Offset_FastScrollerFlow)
{
    const int order = MegaApi::ORDER_MODIFICATION_DESC;

    // Build a grouping filter (default granularity = SECTION_GRANULARITY_MONTH).
    std::unique_ptr<MegaGroupNodesByDateFilter> gfilter{
        MegaGroupNodesByDateFilter::createInstance()};
    gfilter->byCategory(MegaApi::FILE_TYPE_PHOTO);

    std::unique_ptr<MegaDateSectionList> sections{
        megaApi[0]->groupAllNodesByDate(gfilter.get(), order, nullptr)};
    ASSERT_NE(sections, nullptr);
    ASSERT_GT(sections->size(), 0);

    // Use the first section's bounds as a timestamp anchor.
    const MegaDateSection* sec = sections->get(0);
    ASSERT_NE(sec, nullptr);

    std::unique_ptr<MegaListAllNodesFilter> filter{MegaListAllNodesFilter::createInstance()};
    filter->byCategory(MegaApi::FILE_TYPE_PHOTO);
    filter->byTimestampAnchor(sec->getStartDate(), sec->getEndDate(), order);

    // Ground truth: the full anchored set (no limit), in order.
    const std::vector<MegaHandle> expected = fetchAllHandles(megaApi[0].get(), filter.get(), order);
    ASSERT_GE(expected.size(), 2u);

    // The anchored offset window must equal that set sliced at [offset, offset+limit):
    // validates node identity and order, not just cardinality.
    const int64_t offset = 1;
    const size_t limit = 2;
    std::unique_ptr<MegaNodeList> window{
        megaApi[0]->listAllNodesByPageAtOffset(filter.get(),
                                               order,
                                               nullptr,
                                               /*maxElements=*/limit,
                                               offset)};
    ASSERT_NE(window, nullptr);
    EXPECT_EQ(toHandles(window.get()), handleSlice(expected, offset, limit))
        << "anchored offset window must equal the unbounded set sliced at [offset, offset+limit)";
}

// ─── Group 5: sub-category (GIF / RAW) filtering ──────────────────────────────
//
// A dedicated fixture whose photo set spans all three sub-categories. gif and raw
// remain MIME_TYPE_PHOTO, so a plain FILE_TYPE_PHOTO query returns all three; the
// bySubCategory knob narrows it to just the gif or just the raw member.
//
//  Name        Ext    SubType  Mtime
//  clip.gif    .gif   GIF        3h
//  raw.cr2     .cr2   RAW        2h
//  pic.jpg     .jpg   NONE       1h
class SdkTestListAllNodesBySubCategory: public SdkTestNodesSetUp
{
    const std::vector<NodeInfo>& getElements() const override
    {
        static const std::vector<NodeInfo> ELEMENTS{
            FileNodeInfo("clip.gif", MegaNode::NODE_LBL_RED, false, 100, 3h),
            FileNodeInfo("raw.cr2", MegaNode::NODE_LBL_ORANGE, false, 200, 2h),
            FileNodeInfo("pic.jpg", MegaNode::NODE_LBL_YELLOW, false, 300, 1h),
        };
        return ELEMENTS;
    }

    const std::string& getRootTestDir() const override
    {
        static const std::string dirName{"SDK_TEST_LISTALLNODESBYSUBCATEGORY"};
        return dirName;
    }

    bool keepDifferentCreationTimes() override
    {
        return false;
    }
};

TEST_F(SdkTestListAllNodesBySubCategory, SubCategoryGifFilter)
{
    const int order = MegaApi::ORDER_MODIFICATION_DESC;
    auto names = [this](int category, int subCategory)
    {
        auto f = makeRootnodeFilter(category);
        f->bySubCategory(subCategory);
        std::unique_ptr<MegaNodeList> page(
            megaApi[0]->listAllNodesByPageAtOffset(f.get(), order, nullptr, 50, 0));
        return page ? toNames(page.get()) : std::vector<std::string>{};
    };

    // gif and raw stay part of FILE_TYPE_PHOTO: the unfiltered query returns all three.
    EXPECT_THAT(names(MegaApi::FILE_TYPE_PHOTO, MegaNodeScopeFilter::FILE_SUBTYPE_NONE),
                UnorderedElementsAre("clip.gif", "raw.cr2", "pic.jpg"));
    // The sub-category narrows to exactly the matching member.
    EXPECT_THAT(names(MegaApi::FILE_TYPE_PHOTO, MegaNodeScopeFilter::FILE_SUBTYPE_GIF),
                ElementsAre("clip.gif"));
    EXPECT_THAT(names(MegaApi::FILE_TYPE_PHOTO, MegaNodeScopeFilter::FILE_SUBTYPE_RAW),
                ElementsAre("raw.cr2"));
    // Compatible group category (photos ⊂ visual media) is accepted, not over-rejected.
    EXPECT_THAT(names(MegaApi::FILE_TYPE_ALL_VISUAL_MEDIA, MegaNodeScopeFilter::FILE_SUBTYPE_GIF),
                ElementsAre("clip.gif"));
    // Incompatible category is rejected → empty: a non-group category (VIDEO) and a group that
    // doesn't contain photos (ALL_DOCS) both reject a photo sub-category.
    EXPECT_THAT(names(MegaApi::FILE_TYPE_VIDEO, MegaNodeScopeFilter::FILE_SUBTYPE_GIF), IsEmpty());
    EXPECT_THAT(names(MegaApi::FILE_TYPE_ALL_DOCS, MegaNodeScopeFilter::FILE_SUBTYPE_GIF),
                IsEmpty());
}

// ─── Group 11: Filter API — byFavourite tri-state ────────────────────────────
//
// The DISABLED/ONLY_TRUE/ONLY_FALSE calls share one session on purpose, so a
// stale per-connection prepared statement would surface as a wrong count. Extra
// nodes for the folder / subtree / sub-category cases are added ad hoc rather than
// via getElements(), so sibling tests asserting exact node lists stay unaffected.
TEST_F(SdkTestListAllNodesByPage, FavouriteFilterTriState)
{
    const int order = MegaApi::ORDER_DEFAULT_ASC;

    // Builds a FILE_TYPE_PHOTO filter with the given byFavourite state, optionally
    // restricted to a location handle, and returns the matching names.
    auto namesFor =
        [this](int favouriteState, std::initializer_list<MegaHandle> locationHandles = {})
    {
        std::unique_ptr<MegaListAllNodesFilter> filter{MegaListAllNodesFilter::createInstance()};
        filter->byCategory(MegaApi::FILE_TYPE_PHOTO);
        filter->byFavourite(favouriteState);
        std::unique_ptr<MegaHandleList> locations;
        if (locationHandles.size() > 0)
        {
            locations = handleList(locationHandles);
            filter->byLocationHandles(locations.get());
        }
        std::unique_ptr<MegaNodeList> results(
            megaApi[0]->listAllNodesByPage(filter.get(), order, nullptr, 0, nullptr));
        return results ? toNames(results.get()) : std::vector<std::string>{};
    };

    // ── Tri-state counts over the fixture's PHOTO set: alpha.jpg (non-fav),
    // delta.jpg and golf.jpg (fav) ────────────────────────────────
    EXPECT_THAT(namesFor(MegaNodeScopeFilter::BOOL_FILTER_DISABLED),
                UnorderedElementsAre("alpha.jpg", "delta.jpg", "golf.jpg"));
    EXPECT_THAT(namesFor(MegaNodeScopeFilter::BOOL_FILTER_ONLY_TRUE),
                UnorderedElementsAre("delta.jpg", "golf.jpg"));
    EXPECT_THAT(namesFor(MegaNodeScopeFilter::BOOL_FILTER_ONLY_FALSE), ElementsAre("alpha.jpg"));

    // ── Extra nodes for the folder / subtree / sub-category cases ─────────
    //  favDir/          DIR    fav=true   folder itself favourited
    //  freshSub/        DIR    fav=false  no favourites anywhere below
    //    subA.jpg       PHOTO  fav=false
    //    subB.jpg       PHOTO  fav=false
    //  clip.gif         PHOTO  fav=true   GIF sub-category
    //  clip2.gif        PHOTO  fav=false  GIF sub-category
    const std::vector<NodeInfo> extraElements{
        DirNodeInfo("favDir").setFav(true),
        DirNodeInfo("freshSub")
            .addChild(FileNodeInfo("subA.jpg"))
            .addChild(FileNodeInfo("subB.jpg")),
        FileNodeInfo("clip.gif").setFav(true),
        FileNodeInfo("clip2.gif"),
    };
    ASSERT_NO_FATAL_FAILURE(createNodes(extraElements, getRootTestDirectory()));

    // (b) favDir is itself favourited but is a folder, not a PHOTO file: it must
    // never surface under ONLY_TRUE.
    EXPECT_THAT(namesFor(MegaNodeScopeFilter::BOOL_FILTER_ONLY_TRUE),
                Not(Contains(std::string("favDir"))));

    // (a) freshSub has no favourites at all: ONLY_TRUE restricted to that subtree
    // must be empty.
    const std::unique_ptr<MegaNode> freshSub(
        megaApi[0]->getNodeByPath(convertToTestPath("freshSub").c_str()));
    ASSERT_NE(freshSub, nullptr);
    EXPECT_THAT(namesFor(MegaNodeScopeFilter::BOOL_FILTER_ONLY_TRUE, {freshSub->getHandle()}),
                IsEmpty());

    // (c) ONLY_TRUE + bySubCategory(FILE_SUBTYPE_GIF) must return only the
    // favourite GIF (clip.gif), excluding the non-favourite clip2.gif.
    std::unique_ptr<MegaListAllNodesFilter> gifFilter{MegaListAllNodesFilter::createInstance()};
    gifFilter->byCategory(MegaApi::FILE_TYPE_PHOTO);
    gifFilter->bySubCategory(MegaNodeScopeFilter::FILE_SUBTYPE_GIF);
    gifFilter->byFavourite(MegaNodeScopeFilter::BOOL_FILTER_ONLY_TRUE);
    std::unique_ptr<MegaNodeList> gifResults(
        megaApi[0]->listAllNodesByPage(gifFilter.get(), order, nullptr, 0, nullptr));
    ASSERT_NE(gifResults, nullptr);
    EXPECT_THAT(toNames(gifResults.get()), ElementsAre("clip.gif"));
}
