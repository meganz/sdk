/**
 * @file SearchByPageTestBase.h
 * @brief Shared base fixture for SQLite paginated-search tests.
 *
 * `SearchByPageTest` builds a small reference dataset (folders, .txt files,
 * plus a normal/sens subtree and Vault/Rubbish roots) for cursor- and
 * section-based pagination tests. Extracted from tests/unit/Sqlite_test.cpp so
 * DateSection_test.cpp and future split files can share populateDB().
 *
 * Only built under USE_SQLITE.
 */

#pragma once

#ifdef USE_SQLITE

#include "utils.h"

#include <gtest/gtest.h>
#include <mega/db/sqlite.h>
#include <mega/megaapp.h>
#include <mega/megaclient.h>
#include <mega/nodemanager.h>

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <stdfs.h> // path_u8string
#include <string>
#include <vector>

namespace mega
{
namespace pagetest
{

namespace fs = std::filesystem;

// Builds ListAllNodesParams. Defaults to "Cloud + Vault" scope; pass explicit
// ancestor handles to scope to a specific subtree.
inline ListAllNodesParams makeParams(MimeType_t mime,
                                     int order,
                                     size_t maxElements,
                                     bool excludeSensitive = false,
                                     std::optional<NodeSearchCursorOffset> cursor = std::nullopt,
                                     std::vector<NodeHandle> explicitAncestors = {},
                                     std::vector<NodeHandle> excludeHandles = {},
                                     int locationScope = 1,
                                     int64_t offset = 0)
{
    ListAllNodesParams p;
    p.mimeType = mime;
    p.order = order;
    p.maxElements = maxElements;
    p.excludeSensitive = excludeSensitive;
    p.cursor = std::move(cursor);
    p.explicitAncestors = std::move(explicitAncestors);
    p.excludeHandles = std::move(excludeHandles);
    p.locationScope = locationScope;
    p.offset = offset;
    return p;
}

// Collects handles from a NodeManager-layer result into a set for
// membership assertions.
inline std::set<NodeHandle> handlesOf(const sharedNode_vector& nodes)
{
    std::set<NodeHandle> result;
    for (const auto& n: nodes)
        result.insert(n->nodeHandle());
    return result;
}

// DbTable-layer overload: rows are (handle, serialised) pairs.
inline std::set<NodeHandle>
    handlesOf(const std::vector<std::pair<NodeHandle, NodeSerialized>>& rows)
{
    std::set<NodeHandle> result;
    for (const auto& p: rows)
        result.insert(p.first);
    return result;
}

// ─── Per-node metadata captured at insertion time ────────────────────────────
// Used to build NodeSearchCursorOffset without relying on attribute decryption.
struct NodeMeta
{
    std::string name;
    nodetype_t type = FILENODE;
    int64_t size = 0;
    int64_t mtime = 0;
    int label = 0; ///< 0 = unlabelled, 1-7 = colour
    int fav = 0; ///< 0 or 1
    bool sensitive = false; ///< sets the "sen" attribute when true
};

// Attribute name IDs used across multiple test fixtures and test cases.
inline const nameid kNameId = AttrMap::string2nameid("n");
inline const nameid kFavId = AttrMap::string2nameid("fav");
inline const nameid kLabelId = AttrMap::string2nameid("lbl");

// Sort order + page size pair used by parameterised pagination tests.
struct OrderAndPageSize
{
    int order;
    size_t pageSize;
};

// ─── Shared test fixture ───────────────────────────────────────────────────────
/**
 * Base fixture used by ListAllNodesByPageTest, TieBreakTest,
 * GroupedListAllNodesByPageTest, and DateSectionTest.
 *
 * Dataset (created in SetUp):
 *   ROOT folder
 *   5 sub-folders   "Folder_A" … "Folder_E"
 *   20 file nodes   "file_01.txt" … "file_20.txt"
 *     size  = i * 100  (i = 1 … 20)
 *     mtime = 1'700'000'000 + i
 *     label = i % 4      (0 = unlabelled, 1 / 2 / 3 cycling)
 *     fav   = (i % 5 == 0) ? 1 : 0   (files 5, 10, 15, 20)
 */
class SearchByPageTest: public ::testing::Test
{
protected:
    mega::MegaApp mApp;
    NodeManager::MissingParentNodes mMissingParentNodes;
    std::shared_ptr<MegaClient> mClient;
    fs::path mTestDir;

    uint64_t mNextHandle = 1;
    NodeHandle mRootHandle;
    std::map<handle, NodeMeta> mMeta;

    static constexpr int NUM_FILES = 20;
    static constexpr int NUM_FOLDERS = 5;

    // Handles exposed for the Cloud-Drive / version / sensitive filter tests.
    // These reference the jpg + Vault/Rubbish subtrees built by populateDB().
    NodeHandle hFilesRoot; // alias of mRootHandle, kept for readability
    NodeHandle hVault, hRubbish;
    NodeHandle hNormalFolder, hSensFolder;
    NodeHandle hClean, hSelfSensitive, hHead, hVersionV1, hVersionV2, hUnderSens;
    NodeHandle hVaultFile, hRubbishFile;

    void SetUp() override
    {
        mTestDir = fs::current_path() / "search_by_page_test";
        // Remove any leftover directory from a previous crashed run to avoid
        // SQLite "database is locked" errors caused by stale WAL files.
        fs::remove_all(mTestDir);
        fs::create_directories(mTestDir);

        auto* dbAccess = new SqliteDbAccess(LocalPath::fromAbsolutePath(path_u8string(mTestDir)));

        mClient = mt::makeClient(mApp, dbAccess);
        mClient->sid =
            "AWA5YAbtb4JO-y2zWxmKZpSe5-6XM7CTEkA-3Nv7J4byQUpOazdfSC1ZUFlS-kah76gPKUEkTF9g7MeE";
        mClient->opensctable();

        populateDB();

        if (auto* sa = dynamic_cast<SqliteAccountState*>(mClient->sctable.get()))
            sa->createIndexes(/*enableSearch=*/true, /*enableLexi=*/true);
    }

    void TearDown() override
    {
        mClient.reset();
        fs::remove_all(mTestDir);
    }

    // ── Low-level node factory ────────────────────────────────────────────────
    //
    // `isFetching` is forwarded to NodeManager::addNode. Pass false when a
    // subtree's parent pointer must stay live in RAM — notably version chains,
    // whose FLAGS_IS_VERSION bit is derived from the immediate parent.
    std::shared_ptr<Node> addNode(nodetype_t type,
                                  std::shared_ptr<Node> parent,
                                  const NodeMeta& meta,
                                  bool isFetching = true)
    {
        NodeHandle h = NodeHandle().set6byte(mNextHandle++);
        auto node = mt::makeNode(*mClient, type, h, parent.get());

        node->attrs.map[kNameId] = meta.name;

        if (type == FILENODE)
        {
            node->size = static_cast<m_off_t>(meta.size);
            node->mtime = static_cast<m_time_t>(meta.mtime);
            node->ctime = node->mtime;
            node->crc[0] = static_cast<int32_t>(mNextHandle);
            node->isvalid = true;
            node->serializefingerprint(&node->attrs.map['c']);
            node->setfingerprint();

            // sizeVirtual is derived from NodeCounter.storage; without it,
            // ORDER BY sizeVirtual is 0 and SIZE-sort cursors never advance.
            NodeCounter nc;
            nc.files = 1;
            nc.storage = meta.size;
            node->setCounter(nc);
        }

        if (meta.fav)
            node->attrs.map[kFavId] = "1";

        if (meta.label > 0)
            node->attrs.map[kLabelId] = std::to_string(meta.label);

        if (meta.sensitive)
            node->attrs.map[AttrMap::string2nameid("sen")] = "1";

        mClient->mNodeManager.addNode(node,
                                      /*notify=*/false,
                                      isFetching,
                                      mMissingParentNodes);
        mClient->mNodeManager.saveNodeInDb(node.get());

        auto& stored = mMeta[h.as8byte()] = meta;
        stored.type = type;
        return node;
    }

    // ── Dataset construction ──────────────────────────────────────────────────
    //
    // Builds the shared base dataset used by every SearchByPageTest subclass:
    //
    //   CloudDrive (ROOTNODE, mRootHandle/hFilesRoot)
    //   ├── Folder_A..E                           (5 folders)
    //   ├── file_01.txt .. file_20.txt            (20 documents)
    //   ├── normal_folder/                        (non-sensitive)
    //   │   ├── clean.jpg
    //   │   ├── self_sensitive.jpg   [sen=1]
    //   │   └── head.jpg                          (HEAD)
    //   │       ├── head.jpg         (version v1, FILENODE child of FILENODE)
    //   │       └── head.jpg         (version v2)
    //   └── sens_folder/             [sen=1]      (sensitive ancestor)
    //       └── under_sens.jpg
    //   Vault (VAULTNODE) / vault_file.jpg
    //   Rubbish (RUBBISHNODE) / rubbish_file.jpg
    //
    // The .jpg / Vault / Rubbish / version additions are invisible to
    // DOCUMENT-mime pagination tests (see per-node annotations above).
    virtual void populateDB()
    {
        NodeMeta rootMeta{"ROOT", ROOTNODE, 0, 0, 0, 0};
        auto root = addNode(ROOTNODE, nullptr, rootMeta);
        mRootHandle = root->nodeHandle();
        hFilesRoot = mRootHandle;

        const std::string alpha = "ABCDE";
        for (int i = 0; i < NUM_FOLDERS; ++i)
        {
            NodeMeta fm;
            fm.name = "Folder_" + std::string(1, alpha[static_cast<size_t>(i)]);
            fm.type = FOLDERNODE;
            addNode(FOLDERNODE, root, fm);
        }

        // File nodes
        for (int i = 1; i <= NUM_FILES; ++i)
        {
            NodeMeta fm;
            // Zero-padded name so lexicographic order == numeric order
            const std::string pad = (i < 10 ? "0" : "");
            fm.name = "file_" + pad + std::to_string(i) + ".txt";
            fm.size = static_cast<int64_t>(i) * 100;
            fm.mtime = 1'700'000'000LL + i;
            fm.label = i % 4; // 0 = unlabelled, 1/2/3 cycling
            fm.fav = (i % 5 == 0) ? 1 : 0;
            addNode(FILENODE, root, fm);
        }

        // Vault + Rubbish roots. NodeManager auto-registers them via
        // setrootnode_internal so the rootnodes.{vault,rubbish} columns are set.
        auto vault = addNode(VAULTNODE,
                             nullptr,
                             NodeMeta{"Vault", VAULTNODE},
                             /*isFetching=*/false);
        auto rubbish = addNode(RUBBISHNODE,
                               nullptr,
                               NodeMeta{"Rubbish", RUBBISHNODE},
                               /*isFetching=*/false);
        hVault = vault->nodeHandle();
        hRubbish = rubbish->nodeHandle();

        // Folders under Cloud Drive root. sens_folder carries the SENS bit so
        // its descendants must be filtered out when excludeSensitive=true.
        NodeMeta sensFolderMeta{"sens_folder", FOLDERNODE};
        sensFolderMeta.sensitive = true;
        auto normal = addNode(FOLDERNODE,
                              root,
                              NodeMeta{"normal_folder", FOLDERNODE},
                              /*isFetching=*/false);
        auto sens = addNode(FOLDERNODE, root, sensFolderMeta, /*isFetching=*/false);
        hNormalFolder = normal->nodeHandle();
        hSensFolder = sens->nodeHandle();

        const int64_t baseMtime = 1'800'000'000LL;

        // isFetching=false keeps HEAD + version children in RAM so
        // FLAGS_IS_VERSION can still be computed from the parent pointer.
        hClean = addNode(FILENODE,
                         normal,
                         NodeMeta{"clean.jpg", FILENODE, 100, baseMtime + 1},
                         /*isFetching=*/false)
                     ->nodeHandle();

        NodeMeta selfSensMeta{"self_sensitive.jpg", FILENODE, 200, baseMtime + 2};
        selfSensMeta.sensitive = true;
        hSelfSensitive =
            addNode(FILENODE, normal, selfSensMeta, /*isFetching=*/false)->nodeHandle();

        auto head = addNode(FILENODE,
                            normal,
                            NodeMeta{"head.jpg", FILENODE, 300, baseMtime + 3},
                            /*isFetching=*/false);
        hHead = head->nodeHandle();
        hVersionV1 = addNode(FILENODE,
                             head,
                             NodeMeta{"head.jpg", FILENODE, 290, baseMtime + 2},
                             /*isFetching=*/false)
                         ->nodeHandle();
        hVersionV2 = addNode(FILENODE,
                             head,
                             NodeMeta{"head.jpg", FILENODE, 280, baseMtime + 1},
                             /*isFetching=*/false)
                         ->nodeHandle();

        hUnderSens = addNode(FILENODE,
                             sens,
                             NodeMeta{"under_sens.jpg", FILENODE, 400, baseMtime + 4},
                             /*isFetching=*/false)
                         ->nodeHandle();

        hVaultFile = addNode(FILENODE,
                             vault,
                             NodeMeta{"vault_file.jpg", FILENODE, 500, baseMtime + 5},
                             /*isFetching=*/false)
                         ->nodeHandle();
        hRubbishFile = addNode(FILENODE,
                               rubbish,
                               NodeMeta{"rubbish_file.jpg", FILENODE, 600, baseMtime + 6},
                               /*isFetching=*/false)
                           ->nodeHandle();
    }

    // Single call, no limit, no cursor — returns every distinct handle matching
    // @p mime (after Cloud+Vault/version/optional-sensitive filtering).
    std::set<NodeHandle> allMatchesAsSet(MimeType_t mime, int order, bool excludeSensitive) const
    {
        auto nodes = mClient->mNodeManager.listAllNodesByPage(
            makeParams(mime, order, /*maxElements=*/0, excludeSensitive),
            CancelToken{});
        std::set<NodeHandle> out;
        for (const auto& n: nodes)
            out.insert(n->nodeHandle());
        return out;
    }

    // ── Multi-page accumulation helper ─────────────────────────────────────────
    // Accumulates all pages from `startCursor` (default: first page). Iteration
    // is bounded so a stuck cursor fails the test instead of hanging.
    std::vector<NodeHandle>
        collectAllByPage(int order,
                         size_t pageSize,
                         MimeType_t mimeType,
                         std::optional<NodeSearchCursorOffset> startCursor = std::nullopt,
                         bool excludeSensitive = false) const
    {
        std::vector<NodeHandle> result;
        std::optional<NodeSearchCursorOffset> cursor = startCursor;

        const size_t maxPages = mMeta.size() + 2;
        size_t pageCount = 0;

        while (pageCount < maxPages)
        {
            ++pageCount;
            auto page = mClient->mNodeManager.listAllNodesByPage(
                makeParams(mimeType, order, pageSize, excludeSensitive, cursor),
                CancelToken{});
            if (page.empty())
                break;
            for (const auto& n: page)
                result.push_back(n->nodeHandle());
            cursor = cursorFor(page.back()->nodeHandle(), order);
        }

        if (pageCount >= maxPages)
        {
            ADD_FAILURE() << "collectAllByPage: exceeded " << maxPages
                          << " pages for order=" << order
                          << " – cursor likely not advancing (possible infinite loop)";
        }

        return result;
    }

    // ── Build a cursor anchored at node h for the given sort order ────────────
    NodeSearchCursorOffset cursorFor(NodeHandle h, int order) const
    {
        const NodeMeta& m = mMeta.at(h.as8byte());

        NodeSearchCursorOffset c;
        c.mLastName = m.name;
        c.mLastHandle = h.as8byte();

        switch (order)
        {
            case OrderByClause::SIZE_ASC:
            case OrderByClause::SIZE_DESC:
                c.mLastSize = m.size;
                break;
            case OrderByClause::MTIME_ASC:
            case OrderByClause::MTIME_DESC:
                c.mLastMtime = m.mtime;
                break;
            case OrderByClause::LABEL_ASC:
            case OrderByClause::LABEL_DESC:
                c.mLastLabel = m.label;
                break;
            case OrderByClause::FAV_ASC:
            case OrderByClause::FAV_DESC:
                c.mLastFav = m.fav;
                break;
            case OrderByClause::MEDIATS_ASC:
            case OrderByClause::MEDIATS_DESC:
                // mediats is a derived value (filename/mtime/ctime) computed by
                // putNodeInDb(). Read the live value from the Node rather than
                // duplicating the derivation in NodeMeta.
                if (auto node = mClient->mNodeManager.getNodeByHandle(h))
                    c.mLastMediaTs = static_cast<int64_t>(node->getMediaTs());
                break;
            default:
                break;
        }
        return c;
    }
};

} // namespace pagetest
} // namespace mega

#endif // USE_SQLITE
