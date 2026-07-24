/**
 * @file ChildNodeLookup_test.cpp
 * @brief Unit tests for NodeManager::childNodeByNameType() covering the
 *        lookup-performance optimisation introduced in SDK-6027.
 *
 * Production behaviour under test (childNodeByNameType_internal):
 *   1. skipRamScan: when mChildren->size() > mScanDbThreshold, skip the
 *      in-RAM traversal entirely and go straight to the DB.
 *   2. No-LRU-update during traversal: iterate children with
 *      getNodeInRam(false) so that non-matching nodes are NOT moved to the
 *      front of the LRU cache; only the matched node is touched.
 *   3. skipRamScan + node already in RAM: rely on the LRU update inside
 *      getNodeInRAM() itself (no second insertNodeCacheLRU_internal call).
 *
 * What the tests assert are OBSERVABLE invariants: matched-node LRU
 * membership, unchanged LRU position of non-matched nodes, correct
 * return value and type, behaviour at the threshold boundary, and
 * correct re-insertion of evicted-but-still-reachable nodes. The tests
 * intentionally do NOT try to prove "exactly one LRU insert call"
 * because single and double inserts yield an identical LRU state and
 * cannot be distinguished by a functional test.
 *
 * Dataset: each test creates its own nodes; the fixture sets up a private
 * temp directory per test case so runs are isolated from each other and
 * from concurrent test binaries. mScanDbThreshold is tuned per-test via
 * setChildScanDbThreshold().
 */

#include "mega.h"
#include "utils.h"

#include <gtest/gtest.h>
#include <mega/db/sqlite.h>
#include <mega/megaapp.h>
#include <mega/megaclient.h>
#include <mega/nodemanager.h>

#include <filesystem>
#include <stdfs.h>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace mega;

namespace
{

class ChildNodeLookupTest: public ::testing::Test
{
protected:
    mega::MegaApp mApp;
    NodeManager::MissingParentNodes mMissingParentNodes;
    std::shared_ptr<MegaClient> mClient;
    fs::path mTestDir;
    uint64_t mNextHandle = 1;

    void SetUp() override
    {
        // Unique per-test dir under the system temp area to avoid collisions
        // with parallel ctest runs, prior crashed runs, or sibling fixtures.
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const std::string testName = info ? info->name() : "unnamed";
        mTestDir = fs::temp_directory_path() / "mega_ChildNodeLookupTest" / testName;
        std::error_code ec;
        fs::remove_all(mTestDir, ec);
        if (ec)
        {
            LOG_err << "Error removing directory during SetUp, ec: " << ec.value()
                    << " msg: " << ec.message();
        }
        fs::create_directories(mTestDir);

        auto* dbAccess = new SqliteDbAccess(LocalPath::fromAbsolutePath(path_u8string(mTestDir)));
        mClient = mt::makeClient(mApp, dbAccess);
        mClient->sid =
            "AWA5YAbtb4JO-y2zWxmKZpSe5-6XM7CTEkA-3Nv7J4byQUpOazdfSC1ZUFlS-kah76gPKUEkTF9g7MeE";
        mClient->opensctable();

        // Add mandatory root nodes so rootnodes.files is set
        addNode(ROOTNODE, nullptr, /*notify=*/false, /*isFetching=*/true);
        addNode(VAULTNODE, nullptr, false, true);
        addNode(RUBBISHNODE, nullptr, false, true);
    }

    void TearDown() override
    {
        mClient.reset();
        std::error_code ec;
        fs::remove_all(mTestDir, ec);
        if (ec)
        {
            LOG_err << "Error removing directory during TearDown, ec: " << ec.value()
                    << " msg: " << ec.message();
        }
    }

    // ── Low-level node factory ──────────────────────────────────────────────
    // Returns the shared_ptr that keeps the node alive. The caller must hold
    // it for as long as the node should remain reachable from RAM.
    std::shared_ptr<Node> addNode(nodetype_t nodeType,
                                  std::shared_ptr<Node> parent,
                                  bool notify,
                                  bool isFetching,
                                  const std::string& name = "",
                                  std::function<void(Node&)> extraSetup = nullptr)
    {
        NodeHandle handle = NodeHandle().set6byte(mNextHandle++);
        Node& nodeRef = mt::makeNode(*mClient, nodeType, handle, parent.get());
        auto node = std::shared_ptr<Node>(&nodeRef);

        if (!name.empty())
        {
            node->attrs.map['n'] = name;
        }

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
        }

        if (extraSetup)
        {
            extraSetup(*node);
        }

        mClient->mNodeManager.addNode(node, notify, isFetching, mMissingParentNodes);
        mClient->mNodeManager.saveNodeInDb(node.get());
        return node;
    }

    // Adds a folder node under `parent`. Uses the "fetch-style" flags
    // (notify=false, isFetching=true) which is the path normally taken when
    // nodes arrive from the server. Tests pass a root node as `parent`, so
    // the new folder ends up kept in memory.
    std::shared_ptr<Node> addFolder(std::shared_ptr<Node> parent,
                                    const std::string& name = "testFolder")
    {
        return addNode(FOLDERNODE, parent, false, true, name);
    }

    // Shorthand for the NodeManager under test
    NodeManager& nodeMgr()
    {
        return mClient->mNodeManager;
    }

    // Convenience: is node currently in the LRU cache?
    bool isInLru(const std::shared_ptr<Node>& node) const
    {
        return node && node->mNodePosition->second.mLRUPosition !=
                           mClient->mNodeManager.invalidCacheLRUPos();
    }

    // Configures the two knobs that drive which path childNodeByNameType
    // takes: LRU capacity and the "skip RAM scan" threshold.
    void configureLookup(uint64_t lruMaxSize, size_t childScanDbThreshold)
    {
        mClient->mNodeManager.setCacheLRUMaxSize(lruMaxSize);
        mClient->mNodeManager.setChildScanDbThreshold(childScanDbThreshold);
    }

    // Returns the files-root node. SetUp() always populates it, so the
    // lookup is not expected to fail in tests.
    std::shared_ptr<Node> filesRoot()
    {
        auto handle = nodeMgr().getRootNodeFiles();
        assert(!handle.isUndef() && "SetUp should have populated the files root");
        auto root = nodeMgr().getNodeByHandle(handle);
        assert(root && "files root handle must resolve to a live Node");
        return root;
    }

    // Adds `count` FILENODE children under `parent`, each named
    // "<namePrefix><i>" for i in [0, count). Uses notify=true/isFetching=false
    // so each child is kept in memory and inserted into LRU on creation.
    // Returns the shared_ptrs so the caller can control their lifetime
    // (discarding the vector lets the LRU become the sole strong ref).
    std::vector<std::shared_ptr<Node>> addFileChildren(const std::shared_ptr<Node>& parent,
                                                       int count,
                                                       const std::string& namePrefix)
    {
        std::vector<std::shared_ptr<Node>> children;
        children.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            children.push_back(addNode(FILENODE,
                                       parent,
                                       /*notify=*/true,
                                       /*isFetching=*/false,
                                       namePrefix + std::to_string(i)));
        }
        return children;
    }

    // Adds a child reproducing a node received via action packet whose DB dump
    // is still pending (i.e. before notifypurge): it is kept in RAM and linked
    // into the parent's mChildren, and pushed into the notify queue via
    // notifyNode() (populating mNodeNotify), but saveNodeInDb() is intentionally
    // NOT called, so it is invisible to a DB query. This is exactly the window
    // in which the putnodes completion callback re-enters and looks the node up.
    std::shared_ptr<Node> addPendingNotifyChild(const std::shared_ptr<Node>& parent,
                                                nodetype_t nodeType,
                                                const std::string& name)
    {
        NodeHandle handle = NodeHandle().set6byte(mNextHandle++);
        auto& nodeRef = mt::makeNode(*mClient, nodeType, handle, parent.get());
        std::shared_ptr<Node> node(&nodeRef);
        node->attrs.map['n'] = name;

        // notify=true / isFetching=false → kept in RAM and added to mChildren.
        mClient->mNodeManager.addNode(node,
                                      /*notify=*/true,
                                      /*isFetching=*/false,
                                      mMissingParentNodes);
        // Queue for notify (populates mNodeNotify) WITHOUT persisting to the DB.
        mClient->mNodeManager.notifyNode(node);
        return node;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
//  TC1 – Small directory, all children in RAM, node found via RAM scan
//
//  mChildren->size() = 5 <= scanDbThreshold(100) → skipRamScan = false.
//  All children are valid in RAM + mAllChildrenHandleLoaded=true → RAM scan
//  finds the match and returns it.
//
//  To make the LRU assertion meaningful, the target is first forced out of
//  LRU (by shrinking max size while keeping an external shared_ptr alive).
//  If childNodeByNameType did not update LRU on match, the post-lookup
//  isInLru check would fail.
// ═══════════════════════════════════════════════════════════════════════════
TEST_F(ChildNodeLookupTest, SmallDir_AllInRam_NodeFound)
{
    configureLookup(/*lruMaxSize=*/50, /*childScanDbThreshold=*/100);
    auto folder = addFolder(filesRoot());
    auto children = addFileChildren(folder, /*count=*/5, "child_");

    // Evict the target from LRU (keep it alive via `children`) so that the
    // post-lookup "isInLru" check is a real signal that the match-path
    // updated the LRU, not a leftover from creation.
    mClient->mNodeManager.setCacheLRUMaxSize(0);
    ASSERT_FALSE(isInLru(children[2]))
        << "Pre-condition: target must be evicted from LRU before lookup";
    mClient->mNodeManager.setCacheLRUMaxSize(50);

    const std::string targetName = "child_2";
    auto found = nodeMgr().childNodeByNameType(folder.get(), targetName, FILENODE);

    ASSERT_NE(found, nullptr) << "childNodeByNameType should find child_2 in RAM";
    EXPECT_EQ(std::string(found->displayname()), targetName);
    EXPECT_EQ(found.get(), children[2].get()) << "Should return the same in-RAM object";
    EXPECT_TRUE(isInLru(found))
        << "The matched node must be (re-)inserted into LRU by the match path";
}

// ═══════════════════════════════════════════════════════════════════════════
//  TC2 – Small directory, all children known, node NOT found → returns null
//
//  All children are kept alive via the external `children` vector, so every
//  weak_ptr in mChildren resolves during the scan and `allChildrenLoaded`
//  stays true. After scanning without a match, the production code exits
//  early through `if (allChildrenLoaded && !skipRamScan) return nullptr;`
//  (no fall-through to the DB). The assertion here only validates the
//  nullptr result; proving "no DB query happened" would require a DB spy
//  and is out of scope for a unit test.
// ═══════════════════════════════════════════════════════════════════════════
TEST_F(ChildNodeLookupTest, SmallDir_AllChildrenKnown_NodeNotFound)
{
    configureLookup(/*lruMaxSize=*/50, /*childScanDbThreshold=*/100);
    auto folder = addFolder(filesRoot());
    auto children = addFileChildren(folder, /*count=*/5, "child_"); // keep all alive

    auto found = nodeMgr().childNodeByNameType(folder.get(), "nonexistent_node", FILENODE);
    EXPECT_EQ(found, nullptr) << "Should return nullptr when node does not exist";
}

// ═══════════════════════════════════════════════════════════════════════════
//  TC3 – Small directory, traversal does NOT update LRU for non-matched nodes
//
//  Key assertion: children that are in RAM (kept via external shared_ptr)
//  but NOT in the LRU cache must remain absent from the LRU after the
//  lookup, because the traversal loop uses getNodeInRam(false).
//
//  Robustness: we search for a name that does NOT exist, forcing the loop
//  to visit every child regardless of mChildren iteration order. Every
//  evicted-but-kept-alive child is therefore exercised as a "non-matched
//  traversal" observation. Without the updatePositionAtLRU=false,
//  any of them would be pulled back into LRU.
// ═══════════════════════════════════════════════════════════════════════════
TEST_F(ChildNodeLookupTest, SmallDir_TraversalDoesNotUpdateLruForNonMatched)
{
    // LRU small + more children than LRU → early nodes get evicted. Large
    // threshold keeps the path in the RAM-scan branch (no skipRamScan).
    constexpr uint64_t kLruSize = 5;
    configureLookup(kLruSize, /*childScanDbThreshold=*/100);

    auto folder = addFolder(filesRoot());
    auto allChildren = addFileChildren(folder, /*count=*/10, "file_tc3_");

    // Collect every child currently NOT in LRU (alive only via allChildren).
    std::vector<Node*> evictedBefore;
    for (const auto& c: allChildren)
    {
        if (!isInLru(c))
            evictedBefore.push_back(c.get());
    }
    ASSERT_FALSE(evictedBefore.empty())
        << "Pre-condition: at least one child must be evicted from LRU";

    // A NAME THAT DOES NOT EXIST forces the traversal to visit every entry
    // in mChildren, independent of iteration order.
    auto found = nodeMgr().childNodeByNameType(folder.get(), "no_such_name_tc3", FILENODE);
    EXPECT_EQ(found, nullptr);

    // Core assertion: every previously-evicted child is still outside the
    // LRU. If the traversal had refreshed LRU positions (getNodeInRam(true)),
    // at least one would have been pulled back in.
    for (Node* c: evictedBefore)
    {
        EXPECT_EQ(c->mNodePosition->second.mLRUPosition, nodeMgr().invalidCacheLRUPos())
            << "Traversal must not re-insert non-matched node '" << c->displayname()
            << "' into LRU";
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  TC4 – Large directory (> threshold), skipRamScan=true, node IS in RAM
//
//  The RAM scan is skipped. the DB is queried for the node's handle.
//  getNodeInRAM() then finds the node (which is still in RAM) and returns it –
//  with a single LRU update from inside getNodeInRAM itself.  No second
//  insertNodeCacheLRU_internal() is performed (local change removes it).
// ═══════════════════════════════════════════════════════════════════════════
TEST_F(ChildNodeLookupTest, LargeDir_SkipRamScan_NodeInRam)
{
    // Tiny threshold so that even 5 children trigger skipRamScan.
    configureLookup(/*lruMaxSize=*/50, /*childScanDbThreshold=*/2);

    auto folder = addFolder(filesRoot());
    auto children = addFileChildren(folder, /*count=*/5, "large_child_");

    // 5 > 2 → skipRamScan = true: DB is queried; the node is then found in
    // RAM via getNodeInRAM (which updates LRU once internally).
    auto found = nodeMgr().childNodeByNameType(folder.get(), "large_child_0", FILENODE);

    ASSERT_NE(found, nullptr) << "Should find large_child_0 via DB + in-RAM check";
    EXPECT_EQ(std::string(found->displayname()), "large_child_0");
    EXPECT_EQ(found.get(), children[0].get())
        << "skipRamScan path must return the existing in-RAM node, not a DB copy";
    EXPECT_TRUE(isInLru(found)) << "getNodeInRAM() should have inserted the node into LRU";
}

// ═══════════════════════════════════════════════════════════════════════════
//  TC5 – Large directory (> threshold), skipRamScan=true, node NOT in RAM
//
//  After the DB query, getNodeInRAM() returns nullptr because all children
//  have been evicted from the LRU and have no other strong references.
//  The code falls through to getNodeFromNodeSerialized(), which deserialises
//  the node from the DB row and returns a fresh shared_ptr.
// ═══════════════════════════════════════════════════════════════════════════
TEST_F(ChildNodeLookupTest, LargeDir_SkipRamScan_NodeNotInRam)
{
    // Use a large LRU initially so that addNode() succeeds in persisting to DB.
    configureLookup(/*lruMaxSize=*/50, /*childScanDbThreshold=*/2);

    auto folder = addFolder(filesRoot());
    const std::string targetName = "evicted_child_0";

    {
        // Discard the returned shared_ptrs at the end of this block; after
        // that, the only strong references are those inside the LRU cache.
        addFileChildren(folder, /*count=*/5, "evicted_child_");
    }

    // Shrink LRU to 0 to evict all cached nodes. With no remaining strong
    // references the nodes are destroyed; getNodeInRAM() returns nullptr.
    mClient->mNodeManager.setCacheLRUMaxSize(0);

    // 5 > 2 → skipRamScan = true. DB finds the serialised record;
    // getNodeInRAM returns null; getNodeFromNodeSerialized deserialises
    // and returns a new Node object.
    auto found = nodeMgr().childNodeByNameType(folder.get(), targetName, FILENODE);

    ASSERT_NE(found, nullptr) << "Should load node from DB when it is not present in RAM";
    EXPECT_EQ(std::string(found->displayname()), targetName);
}

// ═══════════════════════════════════════════════════════════════════════════
//  TC6 – Threshold boundary: exactly at threshold → RAM scan is used
//
//  mChildren->size() == scanDbThreshold is NOT > threshold, so skipRamScan
//  is false and the ordinary RAM traversal path is taken.
// ═══════════════════════════════════════════════════════════════════════════
TEST_F(ChildNodeLookupTest, ThresholdBoundaryExact_UsesRamScan)
{
    constexpr size_t kThreshold = 3;
    configureLookup(/*lruMaxSize=*/50, kThreshold);

    auto folder = addFolder(filesRoot());
    // size == threshold → 3 > 3 = false → RAM scan
    auto children = addFileChildren(folder, static_cast<int>(kThreshold), "boundary_child_");

    ASSERT_EQ(nodeMgr().getChildScanDbThreshold(), kThreshold);

    auto found = nodeMgr().childNodeByNameType(folder.get(), "boundary_child_0", FILENODE);

    ASSERT_NE(found, nullptr);
    EXPECT_EQ(std::string(found->displayname()), "boundary_child_0");
    // RAM path: same pointer as the in-RAM object
    EXPECT_EQ(found.get(), children[0].get());
}

// ═══════════════════════════════════════════════════════════════════════════
//  TC7 – Threshold boundary: one above threshold → DB scan is used
//
//  mChildren->size() == scanDbThreshold + 1 → skipRamScan = true.
//  The node is still in RAM (large LRU), so getNodeInRAM succeeds.
// ═══════════════════════════════════════════════════════════════════════════
TEST_F(ChildNodeLookupTest, ThresholdBoundaryExceeded_SkipsRamScan)
{
    constexpr size_t kThreshold = 3;
    configureLookup(/*lruMaxSize=*/50, kThreshold);

    auto folder = addFolder(filesRoot());
    // size == threshold + 1 → skipRamScan = true
    auto children = addFileChildren(folder, static_cast<int>(kThreshold) + 1, "exceeded_child_");

    auto found = nodeMgr().childNodeByNameType(folder.get(), "exceeded_child_0", FILENODE);

    ASSERT_NE(found, nullptr);
    EXPECT_EQ(std::string(found->displayname()), "exceeded_child_0");
    // skipRamScan path: getNodeInRAM() returns the same in-RAM pointer
    EXPECT_EQ(found.get(), children[0].get());
    EXPECT_TRUE(isInLru(found))
        << "getNodeInRAM() inside skipRamScan path should insert node into LRU";
}

// ═══════════════════════════════════════════════════════════════════════════
//  TC8 – skipRamScan path: node evicted from LRU but still in RAM is
//        correctly re-inserted into LRU on lookup.
//
//  This exercises the exact call chain the local change relies on:
//     childNodeByNameType_internal (skipRamScan=true)
//       → DB returns the handle
//       → getNodeInRAM(handle)                           // NodeManager
//           → NodeManagerNode::getNodeInRam(/*updatePositionAtLRU=*/true)
//               → mNodeManager.insertNodeCacheLRU(node)  // LRU update here
//
//  Setup: evict the target from LRU while keeping it alive via an
//  external shared_ptr. The lookup must go through the skipRamScan branch
//  (mChildren->size() > threshold) and end up with the target back in LRU.
// ═══════════════════════════════════════════════════════════════════════════
TEST_F(ChildNodeLookupTest, LargeDir_SkipRamScan_ReinsertsEvictedTargetIntoLru)
{
    constexpr int kNumChildren = 5;
    constexpr size_t kThreshold = 2; // kNumChildren > kThreshold → skipRamScan
    constexpr uint64_t kLruSizeLarge = 50; // big enough during setup

    configureLookup(kLruSizeLarge, kThreshold);

    auto folder = addFolder(filesRoot());
    auto children = addFileChildren(folder, kNumChildren, "lru_child_");
    auto targetChild = children.front();
    ASSERT_NE(targetChild, nullptr);

    // Shrink the LRU to 0 to force eviction of every cached entry. The
    // external `children`/`folder` shared_ptrs keep the Node objects alive
    // (NodeManagerNode::mNode is a weak_ptr, so the nodes are still
    // reachable via weak_ptr.lock() after LRU eviction).
    mClient->mNodeManager.setCacheLRUMaxSize(0);
    ASSERT_EQ(nodeMgr().getNumNodesAtCacheLRU(), 0u);
    ASSERT_FALSE(isInLru(targetChild)) << "Pre-condition: target must be evicted from LRU";

    // Grow LRU back so the lookup can re-insert without immediately evicting.
    mClient->mNodeManager.setCacheLRUMaxSize(kLruSizeLarge);

    // skipRamScan=true: DB query returns handle → getNodeInRAM() resolves
    // the weak_ptr → inner LRU update re-inserts the target.
    auto found = nodeMgr().childNodeByNameType(folder.get(), "lru_child_0", FILENODE);

    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found.get(), targetChild.get())
        << "skipRamScan path must return the existing in-RAM node, not a DB copy";

    // Core regression check: the LRU update chain ran – target is back in LRU.
    EXPECT_TRUE(isInLru(found)) << "Target must be (re-)inserted into LRU by the skipRamScan path";

    // General soundness: LRU size respects its max.
    EXPECT_LE(nodeMgr().getNumNodesAtCacheLRU(), kLruSizeLarge);
}

// ═══════════════════════════════════════════════════════════════════════════
//  TC9 – Same name, different type: only the requested type matches
//
//  Exercises the `node->type == nodeType` check in the RAM-scan loop so
//  that a folder named "shared_name" is not returned when a file with the
//  same name is being looked up (and vice versa).
// ═══════════════════════════════════════════════════════════════════════════
TEST_F(ChildNodeLookupTest, SameName_DifferentType_TypeFilterRespected)
{
    configureLookup(/*lruMaxSize=*/50, /*childScanDbThreshold=*/100);
    auto folder = addFolder(filesRoot());

    const std::string sharedName = "shared_name";
    auto fileChild = addNode(FILENODE, folder, true, false, sharedName);
    auto folderChild = addNode(FOLDERNODE, folder, true, false, sharedName);
    ASSERT_NE(fileChild, nullptr);
    ASSERT_NE(folderChild, nullptr);

    auto foundAsFile = nodeMgr().childNodeByNameType(folder.get(), sharedName, FILENODE);
    ASSERT_NE(foundAsFile, nullptr);
    EXPECT_EQ(foundAsFile.get(), fileChild.get());
    EXPECT_EQ(foundAsFile->type, FILENODE);

    auto foundAsFolder = nodeMgr().childNodeByNameType(folder.get(), sharedName, FOLDERNODE);
    ASSERT_NE(foundAsFolder, nullptr);
    EXPECT_EQ(foundAsFolder.get(), folderChild.get());
    EXPECT_EQ(foundAsFolder->type, FOLDERNODE);
}

// ═══════════════════════════════════════════════════════════════════════════
//  TC10 – Folder with no children returns nullptr
//
//  Covers the branch where parent->mNodePosition->second.mChildren is null
//  / empty: skipRamScan evaluates to false (no children to count), the
//  RAM-scan loop is skipped, and the DB query also finds nothing.
// ═══════════════════════════════════════════════════════════════════════════
TEST_F(ChildNodeLookupTest, EmptyFolder_ReturnsNull)
{
    configureLookup(/*lruMaxSize=*/50, /*childScanDbThreshold=*/100);
    auto emptyFolder = addFolder(filesRoot(), "empty_folder");
    ASSERT_NE(emptyFolder, nullptr);

    auto found = nodeMgr().childNodeByNameType(emptyFolder.get(), "anything", FILENODE);
    EXPECT_EQ(found, nullptr);
}

// ═══════════════════════════════════════════════════════════════════════════
//  TC11 – Large directory (skipRamScan): a child still pending in the notify
//         queue (in RAM, not yet dumped to the DB) is found via the fallback.
//
//  Reproduces the folder-upload crash scenario: a folder was just created by a
//  putnodes action packet, and the putnodes completion callback re-enters
//  (still inside sc processing, before notifypurge) to look the node up.
//  Because the parent has more children than the threshold, the RAM scan is
//  skipped (skipRamScan=true) and the query goes straight to the DB, where the
//  just-created node is not present yet. Without the mNodeNotify fallback,
//  childNodeByNameType would wrongly return nullptr (this would cause a
//  duplicate std::move of the NewNode).
// ═══════════════════════════════════════════════════════════════════════════
TEST_F(ChildNodeLookupTest, LargeDir_SkipRamScan_PendingNotifyNodeFound)
{
    constexpr size_t kThreshold = 2;
    configureLookup(/*lruMaxSize=*/50, kThreshold);

    auto parent = addFolder(filesRoot(), "collection");

    // Committed folder siblings: kept in RAM and persisted to the DB (but NOT
    // in the notify queue). Enough of them to push the child count over the
    // threshold so the RAM scan is skipped.
    std::vector<std::shared_ptr<Node>> committed;
    for (int i = 0; i < 3; ++i)
    {
        committed.push_back(addNode(FOLDERNODE,
                                    parent,
                                    /*notify=*/true,
                                    /*isFetching=*/false,
                                    "album_" + std::to_string(i)));
    }

    // The just-created folder: in RAM + mNodeNotify, but NOT in the DB.
    const std::string pendingName = "just_created_album";
    auto pending = addPendingNotifyChild(parent, FOLDERNODE, pendingName);

    // Guard: the parent must have more children than the threshold, otherwise
    // skipRamScan would be false and the RAM scan (not the fallback) would find
    // the pending node, making this a false-positive test.
    ASSERT_GT(nodeMgr().getNumberOfChildrenFromNode(parent->nodeHandle()), kThreshold);

    // skipRamScan=true → DB is queried → misses the pending node → fallback
    // scans mNodeNotify and finds it.
    auto found = nodeMgr().childNodeByNameType(parent.get(), pendingName, FOLDERNODE);

    ASSERT_NE(found, nullptr)
        << "Pending (not-yet-committed) child must be found via the mNodeNotify fallback";
    EXPECT_EQ(found.get(), pending.get()) << "Must return the live in-RAM node";
    EXPECT_EQ(found->type, FOLDERNODE);
    EXPECT_EQ(std::string(found->displayname()), pendingName);

    // A name present in neither the DB nor the notify queue must still miss:
    // the fallback is scoped to real pending children (no false positive).
    EXPECT_EQ(nodeMgr().childNodeByNameType(parent.get(), "no_such_album", FOLDERNODE), nullptr);
}

} // anonymous namespace
