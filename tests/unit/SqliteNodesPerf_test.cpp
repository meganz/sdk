/**
 * @file SqliteNodesPerf_test.cpp
 * @brief Performance tests for the SqliteAccountState `nodes` table query methods.
 *
 * Each TEST_F runs a query repeatedly and reports per-iteration latency via
 * GTEST_LOG_(INFO).  No wall-clock assertions are made so the suite never
 * fails on slow machines, but developers can spot regressions by comparing
 * reported numbers.
 *
 * Dataset (created in SetUp):
 *   - 3 root nodes  (ROOT / VAULT / RUBBISH)
 *   - NUM_TOP_FOLDERS folder nodes  (children of ROOT)
 *   - NUM_TOP_FOLDERS * NUM_SUB_PER_TOP sub-folders
 *   - NUM_FILES_PER_SUB file nodes per sub-folder
 *   Total ≈ 3 + 10 + 100 + 9 800 = 9 913 nodes
 */

#include "utils.h"

#include <gtest/gtest.h>
#include <mega/db/sqlite.h>
#include <mega/megaapp.h>
#include <mega/megaclient.h>
#include <mega/nodemanager.h>

#include <chrono>
#include <filesystem>
#include <mega.h>
#include <optional>
#include <stdfs.h>
#include <string>

#ifdef USE_SQLITE

namespace fs = std::filesystem;

using namespace mega;

namespace
{

// ─── Dataset dimensions ─────────────────────────────────────────────────────
constexpr int NUM_TOP_FOLDERS = 10;
constexpr int NUM_SUB_PER_TOP = 10; // → 100 sub-folders total
constexpr int NUM_FILES_PER_SUB = 98; // → 9,800 file nodes total

// ─── Iteration counts ───────────────────────────────────────────────────────
// Simple point-queries (index seek, returns ≤ 1 row)
constexpr int SIMPLE_ITERS = 1000;
// Scan-based or recursive queries
constexpr int COMPLEX_ITERS = 100;

// ─── Timing helper ──────────────────────────────────────────────────────────
// Returns total elapsed time in microseconds after running fn() `iters` times.
template<typename Fn>
long long measureUs(int iters, Fn&& fn)
{
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i)
        fn();
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
}

// ─── Test fixture ───────────────────────────────────────────────────────────
class SqliteNodesPerfTest: public ::testing::Test
{
protected:
    mega::MegaApp mApp;
    NodeManager::MissingParentNodes mMissingParentNodes;
    std::shared_ptr<MegaClient> mClient;
    fs::path mTestDir;

    uint64_t mNextHandle = 1;

    // Handles collected during dataset construction
    NodeHandle mRootHandle;
    std::vector<NodeHandle> mTopFolderHandles;
    std::vector<NodeHandle> mSubFolderHandles;
    std::vector<NodeHandle> mFileHandles;

    // A representative leaf file used in single-node tests
    NodeHandle mLeafFileHandle;
    std::string mLeafFileName;
    std::string mLeafFileFingerprint; // serialised FileFingerprint blob
    NodeHandle mLeafParentHandle; // direct parent (sub-folder)
    m_off_t mLeafFileSize{};
    m_time_t mLeafFileMtime{};

    // Representative video leaf file for mime-filter cursor tests (i=0, j=0, k=25)
    NodeHandle mVideoLeafHandle; // k=25 → k%10==5 → .mp4
    std::string mVideoLeafFileName;
    m_off_t mVideoLeafSize{};
    m_time_t mVideoLeafMtime{};

    void SetUp() override
    {
        mTestDir = fs::current_path() / "sqlite_nodes_perf_test";
        // Remove any leftover directory from a previous crashed run to avoid
        // SQLite "database is locked" errors caused by stale WAL files.
        fs::remove_all(mTestDir);
        fs::create_directories(mTestDir);

        auto* dbAccess = new SqliteDbAccess(LocalPath::fromAbsolutePath(path_u8string(mTestDir)));

        mClient = mt::makeClient(mApp, dbAccess);
        // sid must be set so that opensctable() can derive a DB filename.
        mClient->sid =
            "AWA5YAbtb4JO-y2zWxmKZpSe5-6XM7CTEkA-3Nv7J4byQUpOazdfSC1ZUFlS-kah76gPKUEkTF9g7MeE";
        mClient->opensctable();

        populateDB();

        // Build indexes after bulk-insert for realistic query benchmarking
        // (both the search-index set and the lexicographic-sort index set).
        if (auto* sa = dynamic_cast<SqliteAccountState*>(mClient->sctable.get()))
            sa->createIndexes(/*enableSearch=*/true, /*enableLexi=*/true);
    }

    void TearDown() override
    {
        mClient.reset();
        fs::remove_all(mTestDir);
    }

    // ── Low-level node factory ───────────────────────────────────────────────
    std::shared_ptr<Node> addNode(nodetype_t nodeType,
                                  std::shared_ptr<Node> parent,
                                  const std::string& name,
                                  bool fav = false,
                                  int label = 0)
    {
        NodeHandle handle = NodeHandle().set6byte(mNextHandle++);
        auto node = mt::makeNode(*mClient, nodeType, handle, parent.get());

        // Name attribute
        static const nameid nameId = AttrMap::string2nameid("n");
        node->attrs.map[nameId] = name;

        // File-specific fields
        if (nodeType == FILENODE)
        {
            node->size = static_cast<m_off_t>(mNextHandle * 512);
            node->ctime = static_cast<m_time_t>(1700000000LL + static_cast<int64_t>(mNextHandle));
            node->mtime = node->ctime;
            node->crc[0] = static_cast<int32_t>(mNextHandle);
            node->crc[1] = static_cast<int32_t>(mNextHandle >> 8u);
            node->crc[2] = static_cast<int32_t>(mNextHandle >> 16u);
            node->crc[3] = static_cast<int32_t>(mNextHandle >> 24u);
            node->isvalid = true;
            node->serializefingerprint(&node->attrs.map['c']);
            node->setfingerprint();

            // sizeVirtual in the DB is derived from NodeCounter.storage.
            // Without this, ORDER BY sizeVirtual returns 0 for all files and
            // cursor-based pagination for SIZE sorts would not advance.
            NodeCounter nc;
            nc.files = 1;
            nc.storage = node->size;
            node->setCounter(nc);
        }

        if (fav)
        {
            static const nameid favId = AttrMap::string2nameid("fav");
            node->attrs.map[favId] = "1";
        }

        if (label > 0)
        {
            static const nameid labelId = AttrMap::string2nameid("lbl");
            node->attrs.map[labelId] = std::to_string(label);
        }

        mClient->mNodeManager.addNode(node,
                                      /*notify=*/false,
                                      /*isFetching=*/true,
                                      mMissingParentNodes);
        mClient->mNodeManager.saveNodeInDb(node.get());
        return node;
    }

    // ── Dataset construction ─────────────────────────────────────────────────
    void populateDB()
    {
        // Root nodes (parenthandle == UNDEF ↔ nullptr parent)
        auto rootNode = addNode(ROOTNODE, nullptr, "ROOT");
        mRootHandle = rootNode->nodeHandle();
        addNode(VAULTNODE, nullptr, "VAULT");
        auto rubbishNode = addNode(RUBBISHNODE, nullptr, "RUBBISH");

        bool leafCaptured = false;

        for (int i = 0; i < NUM_TOP_FOLDERS; ++i)
        {
            auto topFolder = addNode(FOLDERNODE, rootNode, "Folder_" + std::to_string(i));
            mTopFolderHandles.push_back(topFolder->nodeHandle());

            for (int j = 0; j < NUM_SUB_PER_TOP; ++j)
            {
                auto subFolder =
                    addNode(FOLDERNODE,
                            topFolder,
                            "SubFolder_" + std::to_string(i) + "_" + std::to_string(j));
                mSubFolderHandles.push_back(subFolder->nodeHandle());

                for (int k = 0; k < NUM_FILES_PER_SUB; ++k)
                {
                    bool isFav = (i == 0 && j == 0 && k == 0);

                    // Cycle through label values 0-6 (0 = unlabelled, 1-6 = colour labels).
                    // Using k%7 ensures a realistic mix across all label groups.
                    int fileLabel = k % 7;

                    // Mix file types: k%10 < 5 → .jpg (PHOTO),
                    //                 k%10 == 5 → .mp4 (VIDEO),
                    //                 k%10 >= 6 → .txt (DOCUMENT)
                    const char* ext = (k % 10 < 5) ? ".jpg" : (k % 10 == 5) ? ".mp4" : ".txt";
                    std::string fname = "file_" + std::to_string(i) + "_" + std::to_string(j) +
                                        "_" + std::to_string(k) + ext;

                    auto file = addNode(FILENODE, subFolder, fname, isFav, fileLabel);
                    mFileHandles.push_back(file->nodeHandle());

                    // Capture the middle file of the first sub-folder as our
                    // representative leaf used in single-node benchmarks.
                    // k=49 → 49%10==9 ≥ 6 → .txt
                    if (!leafCaptured && i == 0 && j == 0 && k == NUM_FILES_PER_SUB / 2)
                    {
                        mLeafFileHandle = file->nodeHandle();
                        mLeafFileName = fname;
                        mLeafParentHandle = subFolder->nodeHandle();
                        mLeafFileSize = file->size;
                        mLeafFileMtime = file->mtime;

                        // Persist the fingerprint blob for later use.
                        auto it = file->attrs.map.find('c');
                        if (it != file->attrs.map.end())
                            mLeafFileFingerprint = it->second;

                        leafCaptured = true;
                    }

                    // Capture video leaf: i=0, j=0, k=25 → k%10==5 → .mp4
                    if (i == 0 && j == 0 && k == 25)
                    {
                        mVideoLeafHandle = file->nodeHandle();
                        mVideoLeafFileName = fname;
                        mVideoLeafSize = file->size;
                        mVideoLeafMtime = file->mtime;
                    }
                }
            }
        }

        // Additional files in rubbish — 1/10 of Cloud Drive file count. These
        // match the same MIME distribution so they appear in the mimetypeVirtual
        // index but MUST be excluded by the filesRoot filter. Exercises the
        // "index match rate but filter rejects" path.
        const int rubbishCount = NUM_TOP_FOLDERS * NUM_SUB_PER_TOP * NUM_FILES_PER_SUB / 10;
        for (int k = 0; k < rubbishCount; ++k)
        {
            const char* ext = (k % 10 < 5) ? ".jpg" : (k % 10 == 5) ? ".mp4" : ".txt";
            std::string fname = "rubbish_" + std::to_string(k) + ext;
            addNode(FILENODE, rubbishNode, fname);
        }
    }

    // ── Convenience accessor ─────────────────────────────────────────────────
    DBTableNodes* nodesTable()
    {
        return dynamic_cast<DBTableNodes*>(mClient->sctable.get());
    }

    // ── Build a cursor anchored at the leaf file for listAllNodesByPage ───────
    // Only the sort-key optional relevant to `order` is populated so that the
    // WHERE clause matches what production uses.
    NodeSearchCursorOffset leafCursor(int order) const
    {
        NodeSearchCursorOffset c;
        c.mLastName = mLeafFileName;
        c.mLastHandle = mLeafFileHandle.as8byte();

        switch (order)
        {
            case OrderByClause::SIZE_ASC:
            case OrderByClause::SIZE_DESC:
                c.mLastSize = mLeafFileSize;
                break;
            case OrderByClause::MTIME_ASC:
            case OrderByClause::MTIME_DESC:
                c.mLastMtime = mLeafFileMtime;
                break;
            case OrderByClause::LABEL_ASC:
            case OrderByClause::LABEL_DESC:
                c.mLastLabel = 0; // leaf file carries no label
                break;
            case OrderByClause::FAV_ASC:
            case OrderByClause::FAV_DESC:
                c.mLastFav = 0; // leaf file is not a favourite
                break;
            default:
                break; // DEFAULT_ASC / DEFAULT_DESC: name + handle suffice
        }
        return c;
    }

    // ── Cursor anchored at the representative .mp4 leaf (i=0, j=0, k=25) ─────
    NodeSearchCursorOffset videoLeafCursor(int order) const
    {
        NodeSearchCursorOffset c;
        c.mLastName = mVideoLeafFileName;
        c.mLastHandle = mVideoLeafHandle.as8byte();

        switch (order)
        {
            case OrderByClause::SIZE_ASC:
            case OrderByClause::SIZE_DESC:
                c.mLastSize = mVideoLeafSize;
                break;
            case OrderByClause::MTIME_ASC:
            case OrderByClause::MTIME_DESC:
                c.mLastMtime = mVideoLeafMtime;
                break;
            case OrderByClause::LABEL_ASC:
            case OrderByClause::LABEL_DESC:
                c.mLastLabel = 25 % 7; // k=25 → label = 4
                break;
            case OrderByClause::FAV_ASC:
            case OrderByClause::FAV_DESC:
                c.mLastFav = 0;
                break;
            default:
                break;
        }
        return c;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
//  Individual benchmarks
// ═══════════════════════════════════════════════════════════════════════════

// Type alias so GTest registers every TEST_F under a DISABLED_ suite name.
// Run with --gtest_also_run_disabled_tests to include them.
using DISABLED_SqliteNodesPerfTest = SqliteNodesPerfTest;

// ─── 1. getNumberOfNodes ────────────────────────────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfGetNumberOfNodes)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);

    uint64_t count = 0;
    const long long us = measureUs(SIMPLE_ITERS,
                                   [&]
                                   {
                                       count = table->getNumberOfNodes();
                                   });

    EXPECT_GT(count, 0u);
    GTEST_LOG_(INFO) << "getNumberOfNodes [" << count << " nodes]: " << SIMPLE_ITERS
                     << " iters, total " << us << " us, avg " << us / SIMPLE_ITERS << " us/iter";
}

// ─── 2. getNode (single handle lookup) ──────────────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfGetNode)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);
    ASSERT_FALSE(mLeafFileHandle.isUndef());

    NodeSerialized ns;
    // Warm-up: ensure the statement is prepared.
    ASSERT_TRUE(table->getNode(mLeafFileHandle, ns));

    const long long us = measureUs(SIMPLE_ITERS,
                                   [&]
                                   {
                                       NodeSerialized tmp;
                                       table->getNode(mLeafFileHandle, tmp);
                                   });

    GTEST_LOG_(INFO) << "getNode (by handle): " << SIMPLE_ITERS << " iters, total " << us
                     << " us, avg " << us / SIMPLE_ITERS << " us/iter";
}

// ─── 3. getNumberOfChildren ─────────────────────────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfGetNumberOfChildren)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);
    ASSERT_FALSE(mSubFolderHandles.empty());

    const NodeHandle parent = mSubFolderHandles.front();
    uint64_t childCount = 0;

    const long long us = measureUs(SIMPLE_ITERS,
                                   [&]
                                   {
                                       childCount = table->getNumberOfChildren(parent);
                                   });

    EXPECT_EQ(childCount, static_cast<uint64_t>(NUM_FILES_PER_SUB));
    GTEST_LOG_(INFO) << "getNumberOfChildren [" << childCount << " children]: " << SIMPLE_ITERS
                     << " iters, total " << us << " us, avg " << us / SIMPLE_ITERS << " us/iter";
}

// ─── 4. getNumberOfChildrenByType ───────────────────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfGetNumberOfChildrenByType)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);
    ASSERT_FALSE(mSubFolderHandles.empty());

    const NodeHandle parent = mSubFolderHandles.front();
    uint64_t fileCount = 0;

    const long long us = measureUs(SIMPLE_ITERS,
                                   [&]
                                   {
                                       fileCount =
                                           table->getNumberOfChildrenByType(parent, FILENODE);
                                   });

    EXPECT_EQ(fileCount, static_cast<uint64_t>(NUM_FILES_PER_SUB));
    GTEST_LOG_(INFO) << "getNumberOfChildrenByType(FILENODE) [" << fileCount
                     << " children]: " << SIMPLE_ITERS << " iters, total " << us << " us, avg "
                     << us / SIMPLE_ITERS << " us/iter";
}

// ─── 5. childNodeByNameType ─────────────────────────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfChildNodeByNameType)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);
    ASSERT_FALSE(mLeafFileName.empty());

    std::pair<NodeHandle, NodeSerialized> result;
    // Verify the node is actually found before benchmarking.
    ASSERT_TRUE(table->childNodeByNameType(mLeafParentHandle, mLeafFileName, FILENODE, result));
    EXPECT_EQ(result.first, mLeafFileHandle);

    const long long us =
        measureUs(SIMPLE_ITERS,
                  [&]
                  {
                      std::pair<NodeHandle, NodeSerialized> tmp;
                      table->childNodeByNameType(mLeafParentHandle, mLeafFileName, FILENODE, tmp);
                  });

    GTEST_LOG_(INFO) << "childNodeByNameType: " << SIMPLE_ITERS << " iters, total " << us
                     << " us, avg " << us / SIMPLE_ITERS << " us/iter";
}

// ─── 6. getNodeSizeTypeAndFlags ──────────────────────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfGetNodeSizeTypeAndFlags)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);
    ASSERT_FALSE(mLeafFileHandle.isUndef());

    m_off_t size = 0;
    nodetype_t ntype = TYPE_UNKNOWN;
    uint64_t flags = 0;
    ASSERT_TRUE(table->getNodeSizeTypeAndFlags(mLeafFileHandle, size, ntype, flags));

    const long long us = measureUs(SIMPLE_ITERS,
                                   [&]
                                   {
                                       m_off_t s = 0;
                                       nodetype_t t = TYPE_UNKNOWN;
                                       uint64_t f = 0;
                                       table->getNodeSizeTypeAndFlags(mLeafFileHandle, s, t, f);
                                   });

    GTEST_LOG_(INFO) << "getNodeSizeTypeAndFlags: " << SIMPLE_ITERS << " iters, total " << us
                     << " us, avg " << us / SIMPLE_ITERS << " us/iter";
}

// ─── 7. getNodeByFingerprint ────────────────────────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfGetNodeByFingerprint)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);
    ASSERT_FALSE(mLeafFileFingerprint.empty());

    NodeSerialized ns;
    NodeHandle handle;
    ASSERT_TRUE(table->getNodeByFingerprint(mLeafFileFingerprint, ns, handle));

    const long long us = measureUs(SIMPLE_ITERS,
                                   [&]
                                   {
                                       NodeSerialized tmp;
                                       NodeHandle h;
                                       table->getNodeByFingerprint(mLeafFileFingerprint, tmp, h);
                                   });

    GTEST_LOG_(INFO) << "getNodeByFingerprint: " << SIMPLE_ITERS << " iters, total " << us
                     << " us, avg " << us / SIMPLE_ITERS << " us/iter";
}

// ─── 8. getNodesByOrigFingerprint ───────────────────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfGetNodesByOrigFingerprint)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);
    ASSERT_FALSE(mLeafFileFingerprint.empty());

    const long long us =
        measureUs(SIMPLE_ITERS,
                  [&]
                  {
                      std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
                      table->getNodesByOrigFingerprint(mLeafFileFingerprint, nodes);
                  });

    GTEST_LOG_(INFO) << "getNodesByOrigFingerprint: " << SIMPLE_ITERS << " iters, total " << us
                     << " us, avg " << us / SIMPLE_ITERS << " us/iter";
}

// ─── 9. getRootNodes ────────────────────────────────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfGetRootNodes)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);

    std::vector<std::pair<NodeHandle, NodeSerialized>> roots;
    ASSERT_TRUE(table->getRootNodes(roots));
    EXPECT_EQ(roots.size(), 3u); // ROOT + VAULT + RUBBISH

    const long long us = measureUs(SIMPLE_ITERS,
                                   [&]
                                   {
                                       std::vector<std::pair<NodeHandle, NodeSerialized>> tmp;
                                       table->getRootNodes(tmp);
                                   });

    GTEST_LOG_(INFO) << "getRootNodes [" << roots.size() << " roots]: " << SIMPLE_ITERS
                     << " iters, total " << us << " us, avg " << us / SIMPLE_ITERS << " us/iter";
}

// ─── 10. getChildren (no filter, default order) ─────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfGetChildren_NoFilter)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);
    ASSERT_FALSE(mSubFolderHandles.empty());

    const NodeHandle parent = mSubFolderHandles.front();

    NodeSearchFilter filter;
    filter.byLocationHandle(parent.as8byte());

    const long long us =
        measureUs(COMPLEX_ITERS,
                  [&]
                  {
                      std::vector<std::pair<NodeHandle, NodeSerialized>> children;
                      CancelToken ct;
                      NodeSearchPage page{0, 0}; // no paging limit
                      table->getChildren(filter, OrderByClause::DEFAULT_ASC, children, ct, page);
                  });

    std::vector<std::pair<NodeHandle, NodeSerialized>> children;
    CancelToken ct;
    NodeSearchPage page{0, 0};
    table->getChildren(filter, OrderByClause::DEFAULT_ASC, children, ct, page);

    GTEST_LOG_(INFO) << "getChildren (no filter) [" << children.size()
                     << " results]: " << COMPLEX_ITERS << " iters, total " << us << " us, avg "
                     << us / COMPLEX_ITERS << " us/iter";
}

// ─── 11. getChildren (filter by name) ───────────────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfGetChildren_FilterByName)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);
    ASSERT_FALSE(mSubFolderHandles.empty());

    const NodeHandle parent = mSubFolderHandles.front();

    NodeSearchFilter filter;
    filter.byLocationHandle(parent.as8byte());
    filter.byName("file_0_0"); // prefix – should match several files

    const long long us =
        measureUs(COMPLEX_ITERS,
                  [&]
                  {
                      std::vector<std::pair<NodeHandle, NodeSerialized>> children;
                      CancelToken ct;
                      NodeSearchPage page{0, 0};
                      table->getChildren(filter, OrderByClause::DEFAULT_ASC, children, ct, page);
                  });

    std::vector<std::pair<NodeHandle, NodeSerialized>> children;
    CancelToken ct;
    NodeSearchPage page{0, 0};
    table->getChildren(filter, OrderByClause::DEFAULT_ASC, children, ct, page);

    GTEST_LOG_(INFO) << "getChildren (filter by name) [" << children.size()
                     << " results]: " << COMPLEX_ITERS << " iters, total " << us << " us, avg "
                     << us / COMPLEX_ITERS << " us/iter";
}

// ─── 12. getChildren (ordered by size) ──────────────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfGetChildren_OrderBySize)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);
    ASSERT_FALSE(mSubFolderHandles.empty());

    const NodeHandle parent = mSubFolderHandles.front();

    NodeSearchFilter filter;
    filter.byLocationHandle(parent.as8byte());

    const long long us =
        measureUs(COMPLEX_ITERS,
                  [&]
                  {
                      std::vector<std::pair<NodeHandle, NodeSerialized>> children;
                      CancelToken ct;
                      NodeSearchPage page{0, 0};
                      table->getChildren(filter, OrderByClause::SIZE_ASC, children, ct, page);
                  });

    std::vector<std::pair<NodeHandle, NodeSerialized>> children;
    CancelToken ct;
    NodeSearchPage page{0, 0};
    table->getChildren(filter, OrderByClause::SIZE_ASC, children, ct, page);

    GTEST_LOG_(INFO) << "getChildren (order by size ASC) [" << children.size()
                     << " results]: " << COMPLEX_ITERS << " iters, total " << us << " us, avg "
                     << us / COMPLEX_ITERS << " us/iter";
}

// ─── 13. getChildren (ordered by mtime) ─────────────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfGetChildren_OrderByMtime)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);
    ASSERT_FALSE(mSubFolderHandles.empty());

    const NodeHandle parent = mSubFolderHandles.front();

    NodeSearchFilter filter;
    filter.byLocationHandle(parent.as8byte());

    const long long us =
        measureUs(COMPLEX_ITERS,
                  [&]
                  {
                      std::vector<std::pair<NodeHandle, NodeSerialized>> children;
                      CancelToken ct;
                      NodeSearchPage page{0, 0};
                      table->getChildren(filter, OrderByClause::MTIME_DESC, children, ct, page);
                  });

    std::vector<std::pair<NodeHandle, NodeSerialized>> children;
    CancelToken ct;
    NodeSearchPage page{0, 0};
    table->getChildren(filter, OrderByClause::MTIME_DESC, children, ct, page);

    GTEST_LOG_(INFO) << "getChildren (order by mtime DESC) [" << children.size()
                     << " results]: " << COMPLEX_ITERS << " iters, total " << us << " us, avg "
                     << us / COMPLEX_ITERS << " us/iter";
}

// ─── 14. getChildren (paginated) ────────────────────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfGetChildren_Paginated)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);
    ASSERT_FALSE(mSubFolderHandles.empty());

    const NodeHandle parent = mSubFolderHandles.front();

    NodeSearchFilter filter;
    filter.byLocationHandle(parent.as8byte());

    constexpr size_t kPageSize = 20;
    constexpr size_t kPageOffset = 10;

    const long long us =
        measureUs(COMPLEX_ITERS,
                  [&]
                  {
                      std::vector<std::pair<NodeHandle, NodeSerialized>> children;
                      CancelToken ct;
                      NodeSearchPage page{kPageOffset, kPageSize};
                      table->getChildren(filter, OrderByClause::DEFAULT_ASC, children, ct, page);
                  });

    std::vector<std::pair<NodeHandle, NodeSerialized>> children;
    CancelToken ct;
    NodeSearchPage page{kPageOffset, kPageSize};
    table->getChildren(filter, OrderByClause::DEFAULT_ASC, children, ct, page);

    EXPECT_LE(children.size(), kPageSize);
    GTEST_LOG_(INFO) << "getChildren (paginated offset=" << kPageOffset << " size=" << kPageSize
                     << ") [" << children.size() << " results]: " << COMPLEX_ITERS
                     << " iters, total " << us << " us, avg " << us / COMPLEX_ITERS << " us/iter";
}

// ─── 15. listChildNodesLexicographically (no offset) ────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfListChildNodesLexicographically_NoOffset)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);
    ASSERT_FALSE(mSubFolderHandles.empty());

    const handle parent = mSubFolderHandles.front().as8byte();

    const long long us =
        measureUs(COMPLEX_ITERS,
                  [&]
                  {
                      std::vector<std::pair<NodeHandle, NodeSerialized>> children;
                      CancelToken ct;
                      table->listChildNodesLexicographically(parent,
                                                             children,
                                                             ct,
                                                             /*maxElements=*/50,
                                                             /*offset=*/std::nullopt);
                  });

    std::vector<std::pair<NodeHandle, NodeSerialized>> children;
    CancelToken ct;
    table->listChildNodesLexicographically(parent, children, ct, 50, std::nullopt);

    GTEST_LOG_(INFO) << "listChildNodesLexicographically (no offset) [" << children.size()
                     << " results]: " << COMPLEX_ITERS << " iters, total " << us << " us, avg "
                     << us / COMPLEX_ITERS << " us/iter";
}

// ─── 16. listChildNodesLexicographically (with offset) ──────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfListChildNodesLexicographically_WithOffset)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);
    ASSERT_FALSE(mSubFolderHandles.empty());

    const handle parent = mSubFolderHandles.front().as8byte();

    NodeSearchLexicographicalOffset offset;
    offset.mLastName = "file_0_0_010.txt"; // arbitrary cursor position

    const long long us = measureUs(COMPLEX_ITERS,
                                   [&]
                                   {
                                       std::vector<std::pair<NodeHandle, NodeSerialized>> children;
                                       CancelToken ct;
                                       table->listChildNodesLexicographically(parent,
                                                                              children,
                                                                              ct,
                                                                              /*maxElements=*/30,
                                                                              offset);
                                   });

    std::vector<std::pair<NodeHandle, NodeSerialized>> children;
    CancelToken ct;
    table->listChildNodesLexicographically(parent, children, ct, 30, offset);

    GTEST_LOG_(INFO) << "listChildNodesLexicographically (with offset) [" << children.size()
                     << " results]: " << COMPLEX_ITERS << " iters, total " << us << " us, avg "
                     << us / COMPLEX_ITERS << " us/iter";
}

// ─── 17. searchNodes (recursive, from root, no filter) ──────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfSearchNodes_FromRoot_NoFilter)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);

    NodeSearchFilter filter;
    filter.byAncestors({mRootHandle.as8byte(), UNDEF, UNDEF});

    const long long us =
        measureUs(COMPLEX_ITERS,
                  [&]
                  {
                      std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
                      CancelToken ct;
                      NodeSearchPage page{0, 0};
                      table->searchNodes(filter, OrderByClause::DEFAULT_ASC, nodes, ct, page);
                  });

    std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
    CancelToken ct;
    NodeSearchPage page{0, 0};
    table->searchNodes(filter, OrderByClause::DEFAULT_ASC, nodes, ct, page);

    GTEST_LOG_(INFO) << "searchNodes (from root, no filter) [" << nodes.size()
                     << " results]: " << COMPLEX_ITERS << " iters, total " << us << " us, avg "
                     << us / COMPLEX_ITERS << " us/iter";
}

// ─── 18. searchNodes (recursive, from root, filter by name) ─────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfSearchNodes_FromRoot_FilterByName)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);

    NodeSearchFilter filter;
    filter.byAncestors({mRootHandle.as8byte(), UNDEF, UNDEF});
    filter.byName("file_0_0"); // substring – matches many files

    const long long us =
        measureUs(COMPLEX_ITERS,
                  [&]
                  {
                      std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
                      CancelToken ct;
                      NodeSearchPage page{0, 0};
                      table->searchNodes(filter, OrderByClause::DEFAULT_ASC, nodes, ct, page);
                  });

    std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
    CancelToken ct;
    NodeSearchPage page{0, 0};
    table->searchNodes(filter, OrderByClause::DEFAULT_ASC, nodes, ct, page);

    GTEST_LOG_(INFO) << "searchNodes (from root, filter name='file_0_0') [" << nodes.size()
                     << " results]: " << COMPLEX_ITERS << " iters, total " << us << " us, avg "
                     << us / COMPLEX_ITERS << " us/iter";
}

// ─── 19. searchNodes (recursive, from root, filter FILENODE type) ────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfSearchNodes_FromRoot_FilterByType)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);

    NodeSearchFilter filter;
    filter.byAncestors({mRootHandle.as8byte(), UNDEF, UNDEF});
    filter.byNodeType(FILENODE);

    const long long us =
        measureUs(COMPLEX_ITERS,
                  [&]
                  {
                      std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
                      CancelToken ct;
                      NodeSearchPage page{0, 0};
                      table->searchNodes(filter, OrderByClause::DEFAULT_ASC, nodes, ct, page);
                  });

    std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
    CancelToken ct;
    NodeSearchPage page{0, 0};
    table->searchNodes(filter, OrderByClause::DEFAULT_ASC, nodes, ct, page);

    GTEST_LOG_(INFO) << "searchNodes (from root, type=FILENODE) [" << nodes.size()
                     << " results]: " << COMPLEX_ITERS << " iters, total " << us << " us, avg "
                     << us / COMPLEX_ITERS << " us/iter";
}

// ─── 20. searchNodes (recursive, from root, order by ctime DESC) ─────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfSearchNodes_FromRoot_OrderByCtime)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);

    NodeSearchFilter filter;
    filter.byAncestors({mRootHandle.as8byte(), UNDEF, UNDEF});
    filter.byNodeType(FILENODE);

    const long long us =
        measureUs(COMPLEX_ITERS,
                  [&]
                  {
                      std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
                      CancelToken ct;
                      NodeSearchPage page{0, 100}; // top-100 by ctime
                      table->searchNodes(filter, OrderByClause::CTIME_DESC, nodes, ct, page);
                  });

    std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
    CancelToken ct;
    NodeSearchPage page{0, 100};
    table->searchNodes(filter, OrderByClause::CTIME_DESC, nodes, ct, page);

    GTEST_LOG_(INFO) << "searchNodes (from root, FILENODE, order by ctime DESC, limit 100) ["
                     << nodes.size() << " results]: " << COMPLEX_ITERS << " iters, total " << us
                     << " us, avg " << us / COMPLEX_ITERS << " us/iter";
}

// ─── 21. searchNodes (recursive, ALL_VISUAL_MEDIA, page 51-100) ─────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfSearchNodes_FromRoot_AllVisualMedia_Page51To100)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);

    NodeSearchFilter filter;
    filter.byAncestors({mRootHandle.as8byte(), UNDEF, UNDEF});
    filter.byNodeType(FILENODE);
    filter.byCategory(MIME_TYPE_ALL_VISUAL_MEDIA);

    constexpr size_t pageOffset = 50;
    constexpr size_t pageSize = 50;

    const long long us =
        measureUs(COMPLEX_ITERS,
                  [&]
                  {
                      std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
                      CancelToken ct;
                      NodeSearchPage page{pageOffset, pageSize};
                      table->searchNodes(filter, OrderByClause::DEFAULT_ASC, nodes, ct, page);
                  });

    std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
    CancelToken ct;
    NodeSearchPage page{pageOffset, pageSize};
    table->searchNodes(filter, OrderByClause::DEFAULT_ASC, nodes, ct, page);

    std::vector<std::pair<NodeHandle, NodeSerialized>> reference;
    CancelToken referenceCt;
    NodeSearchPage allResults{0, 0};
    table->searchNodes(filter, OrderByClause::DEFAULT_ASC, reference, referenceCt, allResults);

    ASSERT_GT(reference.size(), pageOffset);
    ASSERT_EQ(nodes.size(), std::min(pageSize, reference.size() - pageOffset));
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        EXPECT_EQ(nodes[i].first, reference[pageOffset + i].first);
    }

    GTEST_LOG_(INFO) << "searchNodes (from root, ALL_VISUAL_MEDIA, page 51-100) [" << nodes.size()
                     << " results]: " << COMPLEX_ITERS << " iters, total " << us << " us, avg "
                     << us / COMPLEX_ITERS << " us/iter";
}

// ─── 22. searchNodes (recursive, sub-tree only) ──────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfSearchNodes_SubTree)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);
    ASSERT_FALSE(mTopFolderHandles.empty());

    NodeSearchFilter filter;
    filter.byAncestors({mTopFolderHandles.front().as8byte(), UNDEF, UNDEF});

    const long long us =
        measureUs(COMPLEX_ITERS,
                  [&]
                  {
                      std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
                      CancelToken ct;
                      NodeSearchPage page{0, 0};
                      table->searchNodes(filter, OrderByClause::DEFAULT_ASC, nodes, ct, page);
                  });

    std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
    CancelToken ct;
    NodeSearchPage page{0, 0};
    table->searchNodes(filter, OrderByClause::DEFAULT_ASC, nodes, ct, page);

    GTEST_LOG_(INFO) << "searchNodes (sub-tree from top-folder) [" << nodes.size()
                     << " results]: " << COMPLEX_ITERS << " iters, total " << us << " us, avg "
                     << us / COMPLEX_ITERS << " us/iter";
}

// ─── 23. getRecentNodes ──────────────────────────────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfGetRecentNodes)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);

    // "since" = 0 → return everything
    const m_time_t since = 0;

    const long long us = measureUs(COMPLEX_ITERS,
                                   [&]
                                   {
                                       std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
                                       NodeSearchPage page{0, 200}; // top-200 recent
                                       table->getRecentNodes(page, since, nodes);
                                   });

    std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
    NodeSearchPage page{0, 200};
    table->getRecentNodes(page, since, nodes);

    GTEST_LOG_(INFO) << "getRecentNodes (top 200, since=0) [" << nodes.size()
                     << " results]: " << COMPLEX_ITERS << " iters, total " << us << " us, avg "
                     << us / COMPLEX_ITERS << " us/iter";
}

// ─── 24. getRecentNodes (with since filter) ──────────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfGetRecentNodes_WithSince)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);

    // Use a timestamp that is in the middle of the inserted nodes' ctime range.
    const m_time_t since = 1700000000LL + static_cast<int64_t>(mNextHandle / 2);

    const long long us = measureUs(COMPLEX_ITERS,
                                   [&]
                                   {
                                       std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
                                       NodeSearchPage page{0, 0};
                                       table->getRecentNodes(page, since, nodes);
                                   });

    std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
    NodeSearchPage page{0, 0};
    table->getRecentNodes(page, since, nodes);

    GTEST_LOG_(INFO) << "getRecentNodes (since mid-point) [" << nodes.size()
                     << " results]: " << COMPLEX_ITERS << " iters, total " << us << " us, avg "
                     << us / COMPLEX_ITERS << " us/iter";
}

// ─── 25. getFavouritesHandles ────────────────────────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfGetFavouritesHandles)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);

    const long long us =
        measureUs(COMPLEX_ITERS,
                  [&]
                  {
                      std::vector<NodeHandle> handles;
                      table->getFavouritesHandles(mRootHandle, /*count=*/0, handles);
                  });

    std::vector<NodeHandle> handles;
    table->getFavouritesHandles(mRootHandle, 0, handles);

    GTEST_LOG_(INFO) << "getFavouritesHandles (from root) [" << handles.size()
                     << " favourites]: " << COMPLEX_ITERS << " iters, total " << us << " us, avg "
                     << us / COMPLEX_ITERS << " us/iter";
}

// ─── 26. isAncestor ──────────────────────────────────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfIsAncestor)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);
    ASSERT_FALSE(mLeafFileHandle.isUndef());

    // Verify correctness first
    CancelToken ct;
    EXPECT_TRUE(table->isAncestor(mLeafFileHandle, mRootHandle, ct));

    const long long us = measureUs(SIMPLE_ITERS,
                                   [&]
                                   {
                                       CancelToken tmp;
                                       table->isAncestor(mLeafFileHandle, mRootHandle, tmp);
                                   });

    GTEST_LOG_(INFO) << "isAncestor (leaf → root): " << SIMPLE_ITERS << " iters, total " << us
                     << " us, avg " << us / SIMPLE_ITERS << " us/iter";
}

// ─── 26. getNodesWithSharesOrLink (in-shares) ────────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfGetNodesWithSharesOrLink_InShares)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);

    const long long us =
        measureUs(COMPLEX_ITERS,
                  [&]
                  {
                      std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
                      table->getNodesWithSharesOrLink(nodes, ShareType_t::IN_SHARES);
                  });

    std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
    table->getNodesWithSharesOrLink(nodes, ShareType_t::IN_SHARES);

    GTEST_LOG_(INFO) << "getNodesWithSharesOrLink (IN_SHARES) [" << nodes.size()
                     << " results]: " << COMPLEX_ITERS << " iters, total " << us << " us, avg "
                     << us / COMPLEX_ITERS << " us/iter";
}

// ─── 27. getNodesWithSharesOrLink (public links) ─────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfGetNodesWithSharesOrLink_Links)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);

    const long long us = measureUs(COMPLEX_ITERS,
                                   [&]
                                   {
                                       std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
                                       table->getNodesWithSharesOrLink(nodes, ShareType_t::LINK);
                                   });

    std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
    table->getNodesWithSharesOrLink(nodes, ShareType_t::LINK);

    GTEST_LOG_(INFO) << "getNodesWithSharesOrLink (LINK) [" << nodes.size()
                     << " results]: " << COMPLEX_ITERS << " iters, total " << us << " us, avg "
                     << us / COMPLEX_ITERS << " us/iter";
}

// ─── 28. getNodeTagsBelow ────────────────────────────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfGetNodeTagsBelow)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);

    const long long us = measureUs(COMPLEX_ITERS,
                                   [&]
                                   {
                                       CancelToken ct;
                                       table->getNodeTagsBelow(ct, mRootHandle, /*pattern=*/"");
                                   });

    CancelToken ct;
    auto tags = table->getNodeTagsBelow(ct, mRootHandle, "");

    GTEST_LOG_(INFO) << "getNodeTagsBelow (from root, no pattern) [" << (tags ? tags->size() : 0u)
                     << " distinct tags]: " << COMPLEX_ITERS << " iters, total " << us
                     << " us, avg " << us / COMPLEX_ITERS << " us/iter";
}

// ─── 29. put (INSERT OR REPLACE) ─────────────────────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfPutNode)
{
    auto* table = nodesTable();
    auto* sa = dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    ASSERT_NE(table, nullptr);
    ASSERT_NE(sa, nullptr);

    // Create a fresh node outside the main dataset; re-insert it every iteration.
    NodeHandle freshHandle = NodeHandle().set6byte(mNextHandle++);
    auto freshNode = mt::makeNode(*mClient,
                                  FILENODE,
                                  freshHandle,
                                  /*parent=*/nullptr);

    static const nameid nameId = AttrMap::string2nameid("n");
    freshNode->attrs.map[nameId] = "perf_test_node.dat";
    freshNode->size = 1024;
    freshNode->ctime = 1700000000LL;
    freshNode->mtime = 1700000000LL;
    freshNode->isvalid = false; // no CRC → fingerprint BLOB will be empty

    mClient->mNodeManager.addNode(freshNode, false, true, mMissingParentNodes);

    const long long us = measureUs(SIMPLE_ITERS,
                                   [&]
                                   {
                                       sa->put(freshNode.get());
                                   });

    GTEST_LOG_(INFO) << "put(Node*) (INSERT OR REPLACE) [NO listAll index]: " << SIMPLE_ITERS
                     << " iters, total " << us << " us, avg " << us / SIMPLE_ITERS << " us/iter";
}

// ─── 30. updateCounter ───────────────────────────────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfUpdateCounter)
{
    auto* sa = dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    ASSERT_NE(sa, nullptr);
    ASSERT_FALSE(mLeafFileHandle.isUndef());

    const NodeCounter counter;
    const std::string blob = counter.serialize();

    const long long us = measureUs(SIMPLE_ITERS,
                                   [&]
                                   {
                                       sa->updateCounter(mLeafFileHandle, blob);
                                   });

    GTEST_LOG_(INFO) << "updateCounter: " << SIMPLE_ITERS << " iters, total " << us << " us, avg "
                     << us / SIMPLE_ITERS << " us/iter";
}

// ─── 31. updateCounterAndFlags ───────────────────────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfUpdateCounterAndFlags)
{
    auto* sa = dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    ASSERT_NE(sa, nullptr);
    ASSERT_FALSE(mLeafFileHandle.isUndef());

    const NodeCounter counter;
    const std::string blob = counter.serialize();
    const uint64_t flags = 0;

    const long long us = measureUs(SIMPLE_ITERS,
                                   [&]
                                   {
                                       sa->updateCounterAndFlags(mLeafFileHandle, flags, blob);
                                   });

    GTEST_LOG_(INFO) << "updateCounterAndFlags: " << SIMPLE_ITERS << " iters, total " << us
                     << " us, avg " << us / SIMPLE_ITERS << " us/iter";
}

// ─── 32. remove (single node) ────────────────────────────────────────────────
TEST_F(DISABLED_SqliteNodesPerfTest, PerfRemoveNode)
{
    auto* sa = dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    ASSERT_NE(sa, nullptr);
    ASSERT_FALSE(mFileHandles.empty());
    ASSERT_GE(mFileHandles.size(), static_cast<size_t>(SIMPLE_ITERS));

    const long long us = measureUs(SIMPLE_ITERS,
                                   [&, idx = 0]() mutable
                                   {
                                       sa->remove(mFileHandles[static_cast<size_t>(idx)]);
                                       ++idx;
                                   });

    GTEST_LOG_(INFO) << "remove(NodeHandle) [NO listAll index]: " << SIMPLE_ITERS
                     << " iters, total " << us << " us, avg " << us / SIMPLE_ITERS << " us/iter";
}

// ═══════════════════════════════════════════════════════════════════════════
//  listAllNodesByPage – parameterised suite
//
//  Covers MIME_TYPE_ALL_VISUAL_MEDIA and MIME_TYPE_VIDEO across all 10 sort
//  orders with two cursor states each (first page / mid cursor) = 40 cases.
//  No wall-clock assertions are made; the suite never fails on slow machines.
// ═══════════════════════════════════════════════════════════════════════════

struct ListAllByPageParam
{
    MimeType_t mimeType;
    int order;
    bool firstPage;
};

static const char* listAllOrderName(int order)
{
    switch (order)
    {
        case OrderByClause::DEFAULT_ASC:
            return "DEFAULT_ASC";
        case OrderByClause::DEFAULT_DESC:
            return "DEFAULT_DESC";
        case OrderByClause::MTIME_ASC:
            return "MTIME_ASC";
        case OrderByClause::MTIME_DESC:
            return "MTIME_DESC";
        case OrderByClause::SIZE_ASC:
            return "SIZE_ASC";
        case OrderByClause::SIZE_DESC:
            return "SIZE_DESC";
        case OrderByClause::FAV_ASC:
            return "FAV_ASC";
        case OrderByClause::FAV_DESC:
            return "FAV_DESC";
        case OrderByClause::LABEL_ASC:
            return "LABEL_ASC";
        case OrderByClause::LABEL_DESC:
            return "LABEL_DESC";
        default:
            return "UNKNOWN";
    }
}

static const char* listAllMimeName(MimeType_t mimeType)
{
    return mimeType == MIME_TYPE_VIDEO ? "VIDEO" : "ALL_VISUAL_MEDIA";
}

class SqliteNodesPerfListAllByPageTest:
    public SqliteNodesPerfTest,
    public ::testing::WithParamInterface<ListAllByPageParam>
{};

TEST_P(SqliteNodesPerfListAllByPageTest, DISABLED_Perf)
{
    auto* table = nodesTable();
    ASSERT_NE(table, nullptr);

    const auto& param = GetParam();
    const size_t pageSize = 50;

    std::optional<NodeSearchCursorOffset> cursorOpt;
    if (!param.firstPage)
        cursorOpt = (param.mimeType == MIME_TYPE_VIDEO) ? videoLeafCursor(param.order) :
                                                          leafCursor(param.order);

    ListAllNodesParams lparams;
    lparams.mimeType = param.mimeType;
    lparams.order = param.order;
    lparams.maxElements = pageSize;
    lparams.excludeSensitive = false;
    lparams.cursor = cursorOpt;
    const std::vector<NodeHandle> filesRoots{mRootHandle};

    const long long us = measureUs(COMPLEX_ITERS,
                                   [&]
                                   {
                                       std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
                                       CancelToken ct;
                                       table->listAllNodesByPage(lparams, filesRoots, nodes, ct);
                                   });

    std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
    CancelToken ct;
    table->listAllNodesByPage(lparams, filesRoots, nodes, ct);

    if (param.firstPage)
    {
        EXPECT_EQ(nodes.size(), pageSize);
    }

    GTEST_LOG_(INFO) << "listAllNodesByPage (" << listAllOrderName(param.order) << ", "
                     << listAllMimeName(param.mimeType) << ", "
                     << (param.firstPage ? "first page" : "mid cursor") << ", limit " << pageSize
                     << "): " << COMPLEX_ITERS << " iters, total " << us << " us, avg "
                     << us / COMPLEX_ITERS << " us/iter, " << nodes.size() << " results";
}

// clang-format off
static const ListAllByPageParam kListAllByPageParams[] = {
    // mimeType                    order                         firstPage
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::DEFAULT_ASC,   true },
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::DEFAULT_ASC,   false},
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::DEFAULT_DESC,  true },
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::DEFAULT_DESC,  false},
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::MTIME_ASC,     true },
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::MTIME_ASC,     false},
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::MTIME_DESC,    true },
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::MTIME_DESC,    false},
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::SIZE_ASC,      true },
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::SIZE_ASC,      false},
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::SIZE_DESC,     true },
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::SIZE_DESC,     false},
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::FAV_ASC,       true },
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::FAV_ASC,       false},
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::FAV_DESC,      true },
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::FAV_DESC,      false},
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::LABEL_ASC,     true },
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::LABEL_ASC,     false},
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::LABEL_DESC,    true },
    {MIME_TYPE_ALL_VISUAL_MEDIA,  OrderByClause::LABEL_DESC,    false},
    {MIME_TYPE_VIDEO,             OrderByClause::DEFAULT_ASC,   true },
    {MIME_TYPE_VIDEO,             OrderByClause::DEFAULT_ASC,   false},
    {MIME_TYPE_VIDEO,             OrderByClause::DEFAULT_DESC,  true },
    {MIME_TYPE_VIDEO,             OrderByClause::DEFAULT_DESC,  false},
    {MIME_TYPE_VIDEO,             OrderByClause::MTIME_ASC,     true },
    {MIME_TYPE_VIDEO,             OrderByClause::MTIME_ASC,     false},
    {MIME_TYPE_VIDEO,             OrderByClause::MTIME_DESC,    true },
    {MIME_TYPE_VIDEO,             OrderByClause::MTIME_DESC,    false},
    {MIME_TYPE_VIDEO,             OrderByClause::SIZE_ASC,      true },
    {MIME_TYPE_VIDEO,             OrderByClause::SIZE_ASC,      false},
    {MIME_TYPE_VIDEO,             OrderByClause::SIZE_DESC,     true },
    {MIME_TYPE_VIDEO,             OrderByClause::SIZE_DESC,     false},
    {MIME_TYPE_VIDEO,             OrderByClause::FAV_ASC,       true },
    {MIME_TYPE_VIDEO,             OrderByClause::FAV_ASC,       false},
    {MIME_TYPE_VIDEO,             OrderByClause::FAV_DESC,      true },
    {MIME_TYPE_VIDEO,             OrderByClause::FAV_DESC,      false},
    {MIME_TYPE_VIDEO,             OrderByClause::LABEL_ASC,     true },
    {MIME_TYPE_VIDEO,             OrderByClause::LABEL_ASC,     false},
    {MIME_TYPE_VIDEO,             OrderByClause::LABEL_DESC,    true },
    {MIME_TYPE_VIDEO,             OrderByClause::LABEL_DESC,    false},
};
// clang-format on

INSTANTIATE_TEST_SUITE_P(All,
                         SqliteNodesPerfListAllByPageTest,
                         ::testing::ValuesIn(kListAllByPageParams),
                         [](const ::testing::TestParamInfo<ListAllByPageParam>& info)
                         {
                             return std::string(listAllOrderName(info.param.order)) + "_" +
                                    listAllMimeName(info.param.mimeType) +
                                    (info.param.firstPage ? "_FirstPage" : "_MidCursor");
                         });

// ─── GIF / RAW residual-filter perf ─────────────────────────────────────────
// Times listAllNodesByPage over a large photo-heavy tree with sparse gif/raw (the album
// use case) for PHOTO vs PHOTO+gif vs PHOTO+raw. The residual filters per row, so cost scales
// with the photo-partition size. No assertions (DISABLED); read the logged us.
class SqliteGifRawPerfTest: public SqliteNodesPerfTest
{
protected:
    static constexpr int EXTRA_PHOTOS = 50000;
    static constexpr int GIF_EVERY = 200; // ~0.5% gif
    static constexpr int RAW_EVERY = 200; // ~0.5% raw

    // Seeds `photos` files under a fresh folder: one .gif every gifEvery, one .cr2 (raw)
    // every rawEvery (offset by 1 so they don't overlap), the rest .jpg. Returns the
    // exact gif/raw counts so callers can assert membership.
    NodeHandle
        seedPhotoTree(int photos, int gifEvery, int rawEvery, size_t& gifCount, size_t& rawCount)
    {
        auto root = mClient->mNodeManager.getNodeByHandle(mRootHandle);
        EXPECT_NE(root, nullptr);
        auto folder = addNode(FOLDERNODE, root, "GifRawFolder");
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
            addNode(FILENODE, folder, "p_" + std::to_string(k) + ext);
        }
        if (auto* sa = dynamic_cast<SqliteAccountState*>(mClient->sctable.get()))
            sa->createIndexes(/*enableSearch=*/true, /*enableLexi=*/true);
        return folder->nodeHandle();
    }

    long long timePage(FileSubType_t sub, int64_t offset, size_t& resultCount)
    {
        ListAllNodesParams p;
        p.mimeType = MIME_TYPE_PHOTO;
        p.fileSubType = sub;
        p.order = OrderByClause::MTIME_DESC;
        p.maxElements = 100;
        p.offset = offset;
        const std::vector<NodeHandle> filesRoots{mRootHandle};
        resultCount = 0;
        const long long us = measureUs(COMPLEX_ITERS,
                                       [&]
                                       {
                                           std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
                                           CancelToken ct;
                                           table()->listAllNodesByPage(p, filesRoots, nodes, ct);
                                           resultCount = nodes.size();
                                       });
        return us / COMPLEX_ITERS;
    }

    SqliteAccountState* table()
    {
        return dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    }
};

TEST_F(SqliteGifRawPerfTest, DISABLED_ResidualFilterPerf)
{
    size_t gifCount = 0, rawCount = 0;
    seedPhotoTree(EXTRA_PHOTOS, GIF_EVERY, RAW_EVERY, gifCount, rawCount);
    for (int64_t offset: {int64_t{0}, int64_t{2000}})
    {
        size_t nNone = 0, nGif = 0, nRaw = 0;
        const long long none = timePage(FILE_SUBTYPE_NONE, offset, nNone);
        const long long gif = timePage(FILE_SUBTYPE_GIF, offset, nGif);
        const long long raw = timePage(FILE_SUBTYPE_RAW, offset, nRaw);
        GTEST_LOG_(INFO) << "offset=" << offset << " | PHOTO(all)=" << none << "us (" << nNone
                         << ") | PHOTO+gif=" << gif << "us (" << nGif << ") | PHOTO+raw=" << raw
                         << "us (" << nRaw << ")";
    }
}

// ─── byFavourite residual-filter perf ───────────────────────────────────────
// Times listAllNodesByPage with FAVOURITE_FILTER_ONLY_TRUE over a large tree with sparse
// favourites (~0.5%), across three orders: FAV_DESC (index seek — favourites sort first),
// DEFAULT_ASC (does the planner still pick the fav index?), and MTIME_DESC (true residual —
// the mtime index has no fav prefix, so every row must be re-checked). No assertions
// (DISABLED); read the logged us. Backs the "no new index" decision for byFavourite.
class SqliteFavouritePerfTest: public SqliteNodesPerfTest
{
protected:
    static constexpr int EXTRA_PHOTOS = 50000;
    static constexpr int FAV_EVERY = 200; // ~0.5% favourited

    // Seeds `photos` files under a fresh folder: one favourite every favEvery, the rest not.
    NodeHandle seedPhotoTree(int photos, int favEvery, size_t& favCount)
    {
        auto root = mClient->mNodeManager.getNodeByHandle(mRootHandle);
        EXPECT_NE(root, nullptr);
        auto folder = addNode(FOLDERNODE, root, "FavouriteFolder");
        favCount = 0;
        for (int k = 0; k < photos; ++k)
        {
            const bool isFav = (k % favEvery == 0);
            if (isFav)
                ++favCount;
            addNode(FILENODE, folder, "p_" + std::to_string(k) + ".jpg", isFav);
        }
        if (auto* sa = dynamic_cast<SqliteAccountState*>(mClient->sctable.get()))
            sa->createIndexes(/*enableSearch=*/true, /*enableLexi=*/true);
        return folder->nodeHandle();
    }

    long long timePage(int order, int64_t offset, size_t& resultCount)
    {
        ListAllNodesParams p;
        p.mimeType = MIME_TYPE_PHOTO; // matches the seeded .jpg files
        p.favouriteFilter = FAVOURITE_FILTER_ONLY_TRUE;
        p.order = order;
        p.maxElements = 100;
        p.offset = offset;
        const std::vector<NodeHandle> filesRoots{mRootHandle};
        resultCount = 0;
        const long long us = measureUs(COMPLEX_ITERS,
                                       [&]
                                       {
                                           std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
                                           CancelToken ct;
                                           table()->listAllNodesByPage(p, filesRoots, nodes, ct);
                                           resultCount = nodes.size();
                                       });
        return us / COMPLEX_ITERS;
    }

    SqliteAccountState* table()
    {
        return dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    }
};

TEST_F(SqliteFavouritePerfTest, DISABLED_FavouriteResidualPerf)
{
    size_t favCount = 0;
    seedPhotoTree(EXTRA_PHOTOS, FAV_EVERY, favCount);
    // offset=100 (mid, full page) vs offset=2000 (deep, favourites exhausted -> residual
    // worst case: ~250 favourites total means an offset-2000 favourite-ordered page is empty,
    // while DEFAULT_ASC/MTIME_DESC must residual-scan the whole 50k-row tree to confirm it).
    for (int64_t offset: {int64_t{100}, int64_t{2000}})
    {
        size_t nFavDesc = 0, nDefaultAsc = 0, nMtimeDesc = 0;
        const long long favDesc = timePage(OrderByClause::FAV_DESC, offset, nFavDesc);
        const long long defaultAsc = timePage(OrderByClause::DEFAULT_ASC, offset, nDefaultAsc);
        const long long mtimeDesc = timePage(OrderByClause::MTIME_DESC, offset, nMtimeDesc);
        GTEST_LOG_(INFO) << "offset=" << offset << " | FAV_DESC=" << favDesc << "us (" << nFavDesc
                         << ") | DEFAULT_ASC=" << defaultAsc << "us (" << nDefaultAsc
                         << ") | MTIME_DESC=" << mtimeDesc << "us (" << nMtimeDesc << ")";
    }
}

} // anonymous namespace

#endif // USE_SQLITE
