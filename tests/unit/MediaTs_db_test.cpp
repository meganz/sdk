/**
 * @file MediaTs_db_test.cpp
 * @brief Integration tests for the mediats SQLite column.
 *
 * These tests run against a real SQLite database created in a temporary directory
 * using the same mt::makeClient / mt::makeNode helpers used by Sqlite_test.cpp.
 * No server connection is required.
 *
 * In the local-only design, mediats is computed inside NodeManager::putNodeInDb()
 * from the node's filename, mtime, and ctime — NOT stored as a node attribute.
 * SqliteAccountState::put() is pure persistence: it reads node->getMediaTs().
 *
 * (c) 2013-2024 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#include <mega.h>

#ifdef USE_SQLITE

#include "utils.h"

#include <gtest/gtest.h>
#include <mega/db/sqlite.h>
#include <mega/localpath.h>
#include <mega/megaapp.h>
#include <mega/megaclient.h>
#include <mega/nodemanager.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
#include <map>
#include <stdfs.h>

namespace fs = std::filesystem;

using namespace mega;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

const nameid kNameId = AttrMap::string2nameid("n");

// Expected mediats values for the five photos seeded by seedOrderDataset(),
// in insertion (non-sorted) order. Declared at namespace scope so the
// section-B helper functions can reach them.
constexpr std::array<uint64_t, 5> kOrderDatasetMediats{
    1685613600000ULL,
    1622541600000ULL,
    1748772000000ULL,
    1654077600000ULL,
    1717236000000ULL,
};

class MediaTsDbFixture: public ::testing::Test
{
protected:
    mega::MegaApp mApp;
    NodeManager::MissingParentNodes mMissingParentNodes;
    std::shared_ptr<MegaClient> mClient;
    fs::path mTestDir;
    fs::path mSctablePath;
    uint64_t mNextHandle = 1;
    NodeHandle mRootHandle;

    void SetUp() override
    {
        mTestDir = fs::current_path() / "mediats_db_test";
        fs::remove_all(mTestDir);
        fs::create_directories(mTestDir);

        auto* dbAccess = new SqliteDbAccess(LocalPath::fromAbsolutePath(path_u8string(mTestDir)));

        mClient = mt::makeClient(mApp, dbAccess);
        mClient->sid =
            "AWA5YAbtb4JO-y2zWxmKZpSe5-6XM7CTEkA-3Nv7J4byQUpOazdfSC1ZUFlS-kah76gPKUEkTF9g7MeE";
        mClient->opensctable();

        // Mirror MegaClient::opensctable()'s dbname derivation. Lets the
        // migration test address the statecache directly instead of scanning
        // mTestDir, whose order is platform-dependent.
        constexpr unsigned kPayload = MegaClient::SIDLEN - SymmCipher::KEYLENGTH;
        std::string dbname(kPayload * 4 / 3 + 3, '\0');
        dbname.resize(
            Base64::btoa(reinterpret_cast<const byte*>(mClient->sid.data()) + SymmCipher::KEYLENGTH,
                         static_cast<int>(kPayload),
                         dbname.data()));
        mSctablePath =
            dbAccess->databasePath(*mClient->fsaccess, dbname, DbAccess::DB_VERSION).toPath(false);

        NodeHandle rootH = NodeHandle().set6byte(mNextHandle++);
        auto rootNode = mt::makeNode(*mClient, ROOTNODE, rootH, nullptr);
        rootNode->attrs.map[kNameId] = "ROOT";
        mClient->mNodeManager.addNode(rootNode, false, true, mMissingParentNodes);
        mClient->mNodeManager.saveNodeInDb(rootNode.get());
        mRootHandle = rootH;
    }

    void TearDown() override
    {
        mClient.reset();
        fs::remove_all(mTestDir);
    }

    // Add and persist a file node. mediats is computed by putNodeInDb() from filename/mtime/ctime.
    NodeHandle addFileNode(const std::string& name,
                           m_time_t mtime = 1700000000LL,
                           m_time_t ctime = 1700000000LL)
    {
        auto root = mClient->mNodeManager.getNodeByHandle(mRootHandle);
        EXPECT_NE(root, nullptr);
        if (!root)
            return {};

        NodeHandle h = NodeHandle().set6byte(mNextHandle++);
        auto node = mt::makeNode(*mClient, FILENODE, h, root.get());

        node->attrs.map[kNameId] = name;
        node->mtime = mtime;
        node->ctime = ctime;
        node->size = 1024;
        node->crc[0] = static_cast<int32_t>(mNextHandle);
        node->isvalid = true;
        node->serializefingerprint(&node->attrs.map['c']);
        node->setfingerprint();

        NodeCounter nc;
        nc.files = 1;
        nc.storage = 1024;
        node->setCounter(nc);

        mClient->mNodeManager.addNode(node, false, true, mMissingParentNodes);
        mClient->mNodeManager.saveNodeInDb(node.get());

        return h;
    }

    NodeHandle addFolderNode(const std::string& name)
    {
        auto root = mClient->mNodeManager.getNodeByHandle(mRootHandle);
        EXPECT_NE(root, nullptr);
        if (!root)
            return {};

        NodeHandle h = NodeHandle().set6byte(mNextHandle++);
        auto folder = mt::makeNode(*mClient, FOLDERNODE, h, root.get());
        folder->attrs.map[kNameId] = name;

        mClient->mNodeManager.addNode(folder, false, true, mMissingParentNodes);
        mClient->mNodeManager.saveNodeInDb(folder.get());

        return h;
    }

    // Mark a node as a public link and re-persist it so the DB's `share`
    // column carries the LINK bit. Sufficient for
    // getNodesWithSharesOrLink(LINK); the other three ShareType variants use
    // identical SELECT text so one variant proves the column-layout invariant.
    void addPublicLinkMarker(NodeHandle h)
    {
        auto node = mClient->mNodeManager.getNodeByHandle(h);
        ASSERT_NE(node, nullptr);
        node->plink.reset(new PublicLink(h.as8byte(), 0, 0, false));
        mClient->mNodeManager.saveNodeInDb(node.get());
    }

    // Stamp origfingerprint (attr 'c0') and re-persist. getNodesByOrigFingerprint
    // queries the `origFingerprint` column.
    void setOrigFingerprint(NodeHandle h, const std::string& ofp)
    {
        auto node = mClient->mNodeManager.getNodeByHandle(h);
        ASSERT_NE(node, nullptr);
        node->attrs.map[AttrMap::string2nameid("c0")] = ofp;
        mClient->mNodeManager.saveNodeInDb(node.get());
    }

    // Create an additional root-type node (ROOTNODE / VAULTNODE / RUBBISHNODE)
    // alongside mRootHandle. getRootNodes selects rows with type in that range.
    NodeHandle addRootNodeOfType(nodetype_t t, const std::string& name)
    {
        NodeHandle h = NodeHandle().set6byte(mNextHandle++);
        auto n = mt::makeNode(*mClient, t, h, nullptr);
        n->attrs.map[kNameId] = name;
        mClient->mNodeManager.addNode(n, false, true, mMissingParentNodes);
        mClient->mNodeManager.saveNodeInDb(n.get());
        return h;
    }

    // Seed the five ordering photos (plus a pdf + folder) used by section-B tests.
    // Photos are intentionally inserted in non-sorted order so that an ASC/DESC
    // assertion can only pass if the DB is doing the sorting. Expected mediats
    // live at namespace scope (kOrderDatasetMediats).
    void seedOrderDataset()
    {
        static constexpr const char* kPhotoNames[] = {
            "IMG_20230601_100000.jpg",
            "IMG_20210601_100000.jpg",
            "IMG_20250601_100000.jpg",
            "IMG_20220601_100000.jpg",
            "IMG_20240601_100000.jpg",
        };
        for (const char* name: kPhotoNames)
            addFileNode(name);
        addFileNode("document.pdf");
        addFolderNode("subfolder");
    }

    // Create a file node with an encrypted attrstring ready for Node::setattr() to
    // decrypt. Mirrors the shape of a node received from the server: attrs.map is
    // empty, attrstring holds a base64-encoded CBC-encrypted "MEGA{json}" blob.
    //
    // Does NOT call setattr() — the caller drives that to exercise the real path.
    // Caller owns the returned shared_ptr; the node is already registered in
    // NodeManager (so nodecipher() / key lookups work).
    std::shared_ptr<Node> addFileNodeWithEncryptedAttrs(const std::string& name,
                                                        m_time_t mtime = 1700000000LL,
                                                        m_time_t ctime = 1600000000LL)
    {
        auto root = mClient->mNodeManager.getNodeByHandle(mRootHandle);
        EXPECT_NE(root, nullptr);
        if (!root)
            return nullptr;

        // mt::makeNode sets a 32-byte nodekey to 'X' bytes, so nodecipher() is valid.
        NodeHandle h = NodeHandle().set6byte(mNextHandle++);
        auto node = mt::makeNode(*mClient, FILENODE, h, root.get());

        node->mtime = mtime;
        node->ctime = ctime;
        node->size = 1024;

        // Populate an AttrMap with name + fingerprint, then serialise to JSON.
        // Fingerprint 'c' entry carries mtime/size/crc; setattr() reads it via
        // setfingerprint() to populate node->mtime consistently.
        AttrMap attrs;
        attrs.map[kNameId] = name;
        {
            FileFingerprint ffp;
            ffp.mtime = mtime;
            ffp.size = node->size;
            ffp.crc[0] = 1;
            ffp.isvalid = true;
            ffp.serializefingerprint(&attrs.map['c']);
        }

        std::string json;
        attrs.getjson(&json);

        // makeattr prepends "MEGA{", appends '}', and CBC-encrypts with the
        // node's cipher. Production stores the result base64-encoded (the 'a'
        // field from readnode's JSON payload); decryptattr() reverses that.
        std::string rawCipher;
        MegaClient::makeattr(node->nodecipher(), &rawCipher, json.c_str());
        node->attrstring.reset(new std::string(Base64::btoa(rawCipher)));

        mClient->mNodeManager.addNode(node, false, true, mMissingParentNodes);

        return node;
    }

    // Read the mediats column via the production read path
    // (SqliteAccountState::getNode), which now returns it in NodeSerialized.
    uint64_t getMediatsFromDb(NodeHandle h)
    {
        auto* table = dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
        EXPECT_NE(table, nullptr);
        NodeSerialized ns;
        if (!table || !table->getNode(h, ns))
            return UINT64_MAX;
        return ns.mMediaTs;
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Test: mediats is computed from filename pattern in putNodeInDb()
// ---------------------------------------------------------------------------
TEST_F(MediaTsDbFixture, PutComputesMediatsFromFilename)
{
    // IMG_20240115_103045.jpg → 2024-01-15T10:30:45Z = 1705314645000 ms
    NodeHandle h = addFileNode("IMG_20240115_103045.jpg");
    EXPECT_EQ(getMediatsFromDb(h), 1705314645000LL);
}

// ---------------------------------------------------------------------------
// Test: mediats falls back to mtime when filename has no pattern
// ---------------------------------------------------------------------------
TEST_F(MediaTsDbFixture, PutFallsBackToMtime)
{
    const m_time_t mtime = 1700000000LL;
    NodeHandle h = addFileNode("photo.jpg", mtime, 1600000000LL);
    EXPECT_EQ(getMediatsFromDb(h), static_cast<uint64_t>(mtime) * 1000);
}

// ---------------------------------------------------------------------------
// Test: mediats falls back to ctime when mtime is 0
// ---------------------------------------------------------------------------
TEST_F(MediaTsDbFixture, PutFallsBackToCtime)
{
    const m_time_t ctime = 1600000000LL;
    NodeHandle h = addFileNode("photo.jpg", 0, ctime);
    EXPECT_EQ(getMediatsFromDb(h), static_cast<uint64_t>(ctime) * 1000);
}

// ---------------------------------------------------------------------------
// Test: mediats is 0 for non-media files
// ---------------------------------------------------------------------------
TEST_F(MediaTsDbFixture, NonMediaFileHasZeroMediats)
{
    NodeHandle h = addFileNode("document.pdf");
    EXPECT_EQ(getMediatsFromDb(h), 0);
}

// ---------------------------------------------------------------------------
// Test: mediats is 0 for folder nodes
// ---------------------------------------------------------------------------
TEST_F(MediaTsDbFixture, FolderNodeHasZeroMediats)
{
    NodeHandle h = addFolderNode("MyFolder");
    EXPECT_EQ(getMediatsFromDb(h), 0);
}

// ---------------------------------------------------------------------------
// Test: filename pattern takes priority over mtime
// ---------------------------------------------------------------------------
TEST_F(MediaTsDbFixture, FilenamePatternWinsOverMtime)
{
    // Filename says 2024-01-15T10:30:45Z, but mtime is different
    NodeHandle h = addFileNode("IMG_20240115_103045.jpg", 1000000LL, 999000LL);
    EXPECT_EQ(getMediatsFromDb(h), 1705314645000LL);
}

// ---------------------------------------------------------------------------
// Test: multiple nodes each get correct mediats
// ---------------------------------------------------------------------------
TEST_F(MediaTsDbFixture, MultipleNodesCorrectMediats)
{
    NodeHandle h1 = addFileNode("IMG_20240115_103045.jpg"); // filename pattern
    NodeHandle h2 = addFileNode("photo.jpg", 1700000000LL, 0); // mtime fallback
    NodeHandle h3 = addFileNode("document.pdf"); // non-media → 0

    EXPECT_EQ(getMediatsFromDb(h1), 1705314645000LL);
    EXPECT_EQ(getMediatsFromDb(h2), 1700000000000LL);
    EXPECT_EQ(getMediatsFromDb(h3), 0);
}

// ---------------------------------------------------------------------------
// Test: migration backfills mediats from blob when column is first added
//
// Simulates upgrading from an old DB that doesn't have the mediats column.
// 1. Create DB with nodes (mediats column exists, values computed)
// 2. Close SDK, drop mediats column via raw SQL (simulates old schema)
// 3. Re-open via SDK → addAndPopulateColumns detects missing column →
//    migrateDataToColumns backfills from blob + ctime column
// 4. Verify correct mediats values
// ---------------------------------------------------------------------------
TEST_F(MediaTsDbFixture, MigrationBackfillsMediatsFromBlob)
{
    // Phase 1: Create nodes in a DB that has the mediats column.
    const m_time_t mediaMtime = 1700000000LL;
    const m_time_t mediaCtime = 1600000000LL;

    // Media file with filename pattern → expect filename timestamp
    NodeHandle hPattern = addFileNode("IMG_20240115_103045.jpg", mediaMtime, mediaCtime);
    // Media file without pattern → expect mtime * 1000
    NodeHandle hMtime = addFileNode("photo.jpg", mediaMtime, mediaCtime);
    // Media file, no pattern, mtime=0 → expect ctime * 1000
    NodeHandle hCtime = addFileNode("image.png", 0, mediaCtime);
    // Non-media file → expect 0
    NodeHandle hNonMedia = addFileNode("document.pdf", mediaMtime, mediaCtime);
    // Folder node → expect 0
    NodeHandle hFolder = addFolderNode("MyFolder");

    // Sanity: verify mediats was computed correctly before migration test.
    ASSERT_EQ(getMediatsFromDb(hPattern), 1705314645000LL);
    ASSERT_GT(getMediatsFromDb(hMtime), uint64_t{0});

    // Phase 2: Close SDK and drop the mediats column to simulate old schema.
    mClient->sctable->commit();
    mClient.reset();

    const fs::path dbPath = mSctablePath;
    ASSERT_TRUE(fs::exists(dbPath)) << "Statecache DB not found: " << dbPath;

    // Drop the mediats column to simulate an old schema (requires SQLite >= 3.35,
    // guaranteed on all CI/dev machines targeted by unit tests).
    {
        sqlite3* rawDb = nullptr;
        int rc = sqlite3_open_v2(dbPath.string().c_str(),
                                 &rawDb,
                                 SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX,
                                 nullptr);
        ASSERT_EQ(rc, SQLITE_OK) << "Failed to open DB: " << sqlite3_errmsg(rawDb);
        char* errMsg = nullptr;
        rc =
            sqlite3_exec(rawDb, "ALTER TABLE nodes DROP COLUMN mediats", nullptr, nullptr, &errMsg);
        std::string sqlErr = errMsg ? errMsg : "";
        sqlite3_free(errMsg);
        sqlite3_close(rawDb);
        ASSERT_EQ(rc, SQLITE_OK) << "SQL error dropping mediats column: " << sqlErr;
    }

    // Phase 3: Re-open via SDK — this triggers migration.
    {
        auto* dbAccess = new SqliteDbAccess(LocalPath::fromAbsolutePath(path_u8string(mTestDir)));
        mClient = mt::makeClient(mApp, dbAccess);
        mClient->sid =
            "AWA5YAbtb4JO-y2zWxmKZpSe5-6XM7CTEkA-3Nv7J4byQUpOazdfSC1ZUFlS-kah76gPKUEkTF9g7MeE";
        mClient->opensctable();
    }

    // Phase 4: Verify migration computed correct mediats values.

    // Media file with filename pattern → 2024-01-15T10:30:45Z = 1705314645000
    EXPECT_EQ(getMediatsFromDb(hPattern), 1705314645000LL)
        << "Migration should compute mediats from filename pattern in blob";

    // Media file without pattern → mtime * 1000
    EXPECT_EQ(getMediatsFromDb(hMtime), static_cast<uint64_t>(mediaMtime) * 1000)
        << "Migration should fall back to mtime from blob fingerprint";

    // Media file, mtime=0 → ctime * 1000
    EXPECT_EQ(getMediatsFromDb(hCtime), static_cast<uint64_t>(mediaCtime) * 1000)
        << "Migration should fall back to ctime from DB column";

    // Non-media file → 0
    EXPECT_EQ(getMediatsFromDb(hNonMedia), 0) << "Migration should set 0 for non-media files";

    // Folder → 0
    EXPECT_EQ(getMediatsFromDb(hFolder), 0) << "Migration should set 0 for folder nodes";
}

// ---------------------------------------------------------------------------
// Test: NodeSerialized carries mediats from DB via getNode()
//
// Directly call the SqliteAccountState::getNode() method and verify that
// the NodeSerialized struct has the correct mMediaTs value.
// ---------------------------------------------------------------------------
TEST_F(MediaTsDbFixture, GetNodeReturnsCorrectMediats)
{
    NodeHandle h = addFileNode("IMG_20240115_103045.jpg");

    auto* table = dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    ASSERT_NE(table, nullptr);

    NodeSerialized ns;
    bool ok = table->getNode(h, ns);
    ASSERT_TRUE(ok);
    EXPECT_EQ(ns.mMediaTs, 1705314645000ULL)
        << "NodeSerialized loaded from DB should carry the correct mediats value";
}

// ---------------------------------------------------------------------------
// Test: Node::setattr() computes mediats in RAM from an encrypted attrs blob
//
// Drives the real setattr() code path end-to-end: the helper encrypts a JSON
// attrstring with makeattr() and base64-encodes it exactly as production does;
// setattr() then decrypts, parses, calls setfingerprint(), and updateMediaTs().
// This is the same flow used during readnode() / sc_updatenode() / applyKeys().
// ---------------------------------------------------------------------------
TEST_F(MediaTsDbFixture, SetAttrComputesMediatsInRAM)
{
    auto node = addFileNodeWithEncryptedAttrs("IMG_20240115_103045.jpg");
    ASSERT_NE(node, nullptr);

    // Precondition: mediats is zero before setattr() runs.
    ASSERT_EQ(static_cast<uint64_t>(node->getMediaTs()), 0u);
    ASSERT_TRUE(node->attrstring != nullptr);

    node->setattr();

    // setattr() clears attrstring on successful decryption.
    EXPECT_EQ(node->attrstring, nullptr)
        << "setattr() should have cleared attrstring after successful decryption";

    // Filename encodes 2024-01-15 10:30:45 UTC = 1705314645000 ms.
    // Priority 1 in the chain (filename pattern > mtime > ctime).
    EXPECT_EQ(static_cast<uint64_t>(node->getMediaTs()), 1705314645000LL)
        << "After setattr() with pattern filename, mediats should reflect the filename timestamp";

    // Verify the decrypted attrs map carries the name we encoded.
    auto it = node->attrs.map.find(kNameId);
    ASSERT_NE(it, node->attrs.map.end());
    EXPECT_EQ(it->second, "IMG_20240115_103045.jpg");
}

// ---------------------------------------------------------------------------
// Test: Node::setattr() falls back to mtime when the filename has no pattern
//
// Complements SetAttrComputesMediatsInRAM: with a non-pattern filename, the
// priority chain falls through to mtime * 1000.
// ---------------------------------------------------------------------------
TEST_F(MediaTsDbFixture, SetAttrFallsBackToMtimeForNonPatternFilename)
{
    auto node = addFileNodeWithEncryptedAttrs("vacation_photo.jpg", 1700000000LL, 1600000000LL);
    ASSERT_NE(node, nullptr);

    node->setattr();

    // Filename has no pattern → Priority 2: mtime * 1000.
    EXPECT_EQ(static_cast<uint64_t>(node->getMediaTs()), 1700000000000LL)
        << "Non-pattern filename should fall back to mtime * 1000 in setattr()";
}

// ---------------------------------------------------------------------------
// Test: OrderByClause::get() returns correct SQL for MEDIATS orders
// ---------------------------------------------------------------------------
TEST_F(MediaTsDbFixture, OrderByClauseGetMediatsAsc)
{
    std::string sql = OrderByClause::get(OrderByClause::MEDIATS_ASC);
    EXPECT_NE(sql.find("mediats"), std::string::npos)
        << "MEDIATS_ASC should contain 'mediats' in the ORDER BY clause";
    // "type DESC" is always present; verify mediats itself is not DESC
    EXPECT_EQ(sql.find("mediats DESC"), std::string::npos)
        << "MEDIATS_ASC should not contain 'mediats DESC'";
}

TEST_F(MediaTsDbFixture, OrderByClauseGetMediatsDesc)
{
    std::string sql = OrderByClause::get(OrderByClause::MEDIATS_DESC);
    EXPECT_NE(sql.find("mediats DESC"), std::string::npos)
        << "MEDIATS_DESC should contain 'mediats DESC' in the ORDER BY clause";
}

// ---------------------------------------------------------------------------
// Test: NodeSerialized carries mediats for multiple nodes with different values
//
// Insert nodes with different mediats values via putNodeInDb(), then read
// them back via getNode() and verify the mMediaTs field on NodeSerialized.
// (Full ORDER BY mediats testing is in the integration tests.)
// ---------------------------------------------------------------------------
TEST_F(MediaTsDbFixture, NodeSerializedMediatsRoundTrip)
{
    // Three media files with different timestamps.
    NodeHandle h1 = addFileNode("IMG_20240115_103045.jpg"); // filename: 1705314645000
    NodeHandle h2 = addFileNode("photo_b.jpg", 1700000000LL); // mtime: 1700000000000
    NodeHandle h3 = addFileNode("photo_a.jpg", 1600000000LL); // mtime: 1600000000000

    auto* table = dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    ASSERT_NE(table, nullptr);

    NodeSerialized ns1, ns2, ns3;
    ASSERT_TRUE(table->getNode(h1, ns1));
    ASSERT_TRUE(table->getNode(h2, ns2));
    ASSERT_TRUE(table->getNode(h3, ns3));

    EXPECT_EQ(ns1.mMediaTs, 1705314645000ULL);
    EXPECT_EQ(ns2.mMediaTs, 1700000000000ULL);
    EXPECT_EQ(ns3.mMediaTs, 1600000000000ULL);
}

// ---------------------------------------------------------------------------
// Section A: mediats round-trip through each SqliteAccountState read method.
//
// Every section-A test drives the table directly (`dynamic_cast<SqliteAccountState*>`
// on `mClient->sctable`) rather than going through NodeManager. NodeManager's
// read paths (search / getChildren / ...) short-circuit to the RAM cache via
// getNodeInRAM(); in the unbounded-LRU unit fixture every node stays in RAM,
// so those paths return `node->getMediaTs()` (RAM value) regardless of what
// the DB column holds. Direct table calls are the only way to prove that
// `processSqlQueryNodes` reads `mediats` from column index 3 correctly.
//
// Every section-A dataset mixes photos (non-zero mediats) with non-media
// files / folders (mediats=0). A wrong column index could happen to align
// with another non-zero column and spuriously pass a non-zero assertion;
// the zero-value rows catch that.
// ---------------------------------------------------------------------------

namespace
{

// Ordered expected-mediats table for multi-row assertions.
struct Expectation
{
    NodeHandle handle;
    uint64_t mediats;
};

void expectAllHandlesFoundWithMediats(
    const std::vector<std::pair<NodeHandle, NodeSerialized>>& rows,
    const std::vector<Expectation>& expected)
{
    ASSERT_EQ(rows.size(), expected.size()) << "row count mismatch";
    std::map<uint64_t, uint64_t> got; // handle8 -> mediats
    for (const auto& r: rows)
        got[r.first.as8byte()] = r.second.mMediaTs;
    for (const auto& e: expected)
    {
        auto it = got.find(e.handle.as8byte());
        ASSERT_NE(it, got.end()) << "handle " << e.handle.as8byte() << " missing from result";
        EXPECT_EQ(it->second, e.mediats) << "wrong mediats for handle " << e.handle.as8byte();
    }
}

} // namespace

// A.1 — searchNodes
TEST_F(MediaTsDbFixture, SearchNodesReadsMediatsColumn)
{
    const NodeHandle hPhoto1 = addFileNode("IMG_20240115_103045.jpg");
    const NodeHandle hPhoto2 = addFileNode("photo_b.jpg", 1700000000LL);
    const NodeHandle hPhoto3 = addFileNode("photo_a.jpg", 1600000000LL);
    const NodeHandle hPdf = addFileNode("document.pdf");
    const NodeHandle hFolder = addFolderNode("subfolder");

    auto* table = dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    ASSERT_NE(table, nullptr);

    NodeSearchFilter filter;
    filter.byAncestors({mRootHandle.as8byte(), UNDEF, UNDEF});

    std::vector<std::pair<NodeHandle, NodeSerialized>> out;
    CancelToken ct;
    ASSERT_TRUE(
        table->searchNodes(filter, OrderByClause::DEFAULT_ASC, out, ct, NodeSearchPage{0, 0}));

    expectAllHandlesFoundWithMediats(out,
                                     {
                                         {hPhoto1, 1705314645000ULL},
                                         {hPhoto2, 1700000000000ULL},
                                         {hPhoto3, 1600000000000ULL},
                                         {hPdf, 0ULL},
                                         {hFolder, 0ULL},
                                     });
}

// A.2 — getChildren
TEST_F(MediaTsDbFixture, GetChildrenReadsMediatsColumn)
{
    const NodeHandle hPhoto1 = addFileNode("IMG_20240115_103045.jpg");
    const NodeHandle hPhoto2 = addFileNode("photo_b.jpg", 1700000000LL);
    const NodeHandle hPdf = addFileNode("document.pdf");
    const NodeHandle hFolder = addFolderNode("subfolder");

    auto* table = dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    ASSERT_NE(table, nullptr);

    NodeSearchFilter filter;
    filter.byLocationHandle(mRootHandle.as8byte());

    std::vector<std::pair<NodeHandle, NodeSerialized>> out;
    CancelToken ct;
    ASSERT_TRUE(
        table->getChildren(filter, OrderByClause::DEFAULT_ASC, out, ct, NodeSearchPage{0, 0}));

    expectAllHandlesFoundWithMediats(out,
                                     {
                                         {hPhoto1, 1705314645000ULL},
                                         {hPhoto2, 1700000000000ULL},
                                         {hPdf, 0ULL},
                                         {hFolder, 0ULL},
                                     });
}

// A.3 — getRecentNodes. Method filters to FILENODE only, so folders are
// excluded by the query itself — not a column-read question.
TEST_F(MediaTsDbFixture, GetRecentNodesReadsMediatsColumn)
{
    const NodeHandle hPhoto1 = addFileNode("IMG_20240115_103045.jpg");
    const NodeHandle hPhoto2 = addFileNode("photo_b.jpg", 1700000000LL);
    const NodeHandle hPdf = addFileNode("document.pdf");
    addFolderNode("subfolder"); // must be excluded by the method's type filter

    auto* table = dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    ASSERT_NE(table, nullptr);

    std::vector<std::pair<NodeHandle, NodeSerialized>> out;
    ASSERT_TRUE(table->getRecentNodes(NodeSearchPage{0, 10}, /*since=*/0, out));

    expectAllHandlesFoundWithMediats(out,
                                     {
                                         {hPhoto1, 1705314645000ULL},
                                         {hPhoto2, 1700000000000ULL},
                                         {hPdf, 0ULL},
                                     });
}

// A.4 — childNodeByNameType
TEST_F(MediaTsDbFixture, ChildNodeByNameTypeReadsMediatsColumn)
{
    addFileNode("IMG_20240115_103045.jpg"); // 1705314645000
    addFileNode("document.pdf"); // 0

    auto* table = dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    ASSERT_NE(table, nullptr);

    std::pair<NodeHandle, NodeSerialized> hit;
    ASSERT_TRUE(table->childNodeByNameType(mRootHandle, "IMG_20240115_103045.jpg", FILENODE, hit));
    EXPECT_EQ(hit.second.mMediaTs, 1705314645000ULL);

    ASSERT_TRUE(table->childNodeByNameType(mRootHandle, "document.pdf", FILENODE, hit));
    EXPECT_EQ(hit.second.mMediaTs, 0ULL);
}

// A.5 — getNodeByFingerprint
TEST_F(MediaTsDbFixture, GetNodeByFingerprintReadsMediatsColumn)
{
    const NodeHandle hPhoto = addFileNode("IMG_20240115_103045.jpg");
    const NodeHandle hPdf = addFileNode("document.pdf");

    auto* table = dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    ASSERT_NE(table, nullptr);

    auto photo = mClient->mNodeManager.getNodeByHandle(hPhoto);
    auto pdf = mClient->mNodeManager.getNodeByHandle(hPdf);
    ASSERT_NE(photo, nullptr);
    ASSERT_NE(pdf, nullptr);

    std::string fpPhoto, fpPdf;
    photo->FileFingerprint::serialize(&fpPhoto);
    pdf->FileFingerprint::serialize(&fpPdf);

    NodeSerialized ns;
    NodeHandle outH;
    ASSERT_TRUE(table->getNodeByFingerprint(fpPhoto, ns, outH));
    EXPECT_EQ(outH, hPhoto);
    EXPECT_EQ(ns.mMediaTs, 1705314645000ULL);

    ASSERT_TRUE(table->getNodeByFingerprint(fpPdf, ns, outH));
    EXPECT_EQ(outH, hPdf);
    EXPECT_EQ(ns.mMediaTs, 0ULL);
}

// A.6 — getNodesByOrigFingerprint. 'c0' attr stamped on both nodes with the
// same value; query by that value returns both rows.
TEST_F(MediaTsDbFixture, GetNodesByOrigFingerprintReadsMediatsColumn)
{
    const NodeHandle hPhoto = addFileNode("photo.jpg", 1700000000LL); // 1700000000000
    const NodeHandle hPdf = addFileNode("document.pdf"); // 0

    const std::string sharedOrigFp = "shared_origfp_blob";
    setOrigFingerprint(hPhoto, sharedOrigFp);
    setOrigFingerprint(hPdf, sharedOrigFp);

    auto* table = dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    ASSERT_NE(table, nullptr);

    std::vector<std::pair<NodeHandle, NodeSerialized>> out;
    ASSERT_TRUE(table->getNodesByOrigFingerprint(sharedOrigFp, out));

    expectAllHandlesFoundWithMediats(out,
                                     {
                                         {hPhoto, 1700000000000ULL},
                                         {hPdf, 0ULL},
                                     });
}

// A.7 — getNodesByFingerprintNoMtime. Two nodes are forced to share the same
// size/crc/isvalid so their `fingerprintVirtual` (the 25-byte mtime-excluded
// blob) matches. Query by that blob returns both rows.
TEST_F(MediaTsDbFixture, GetNodesByFingerprintNoMtimeReadsMediatsColumn)
{
    const NodeHandle hPhoto = addFileNode("photo.jpg", 1700000000LL); // mediats = 1700000000000
    const NodeHandle hPdf = addFileNode("document.pdf"); // mediats = 0

    auto photo = mClient->mNodeManager.getNodeByHandle(hPhoto);
    auto pdf = mClient->mNodeManager.getNodeByHandle(hPdf);
    ASSERT_NE(photo, nullptr);
    ASSERT_NE(pdf, nullptr);

    // Force identical fingerprintVirtual (size + crc + isvalid) while keeping
    // different mtime. put() serializes FileFingerprint directly from the
    // node's in-RAM size/mtime/crc/isvalid, so updating those fields and
    // calling saveNodeInDb is sufficient. NOTE: do NOT call setfingerprint()
    // here — it re-parses attrs.map['c'] (set by addFileNode with the original
    // unique crc) and would overwrite our mutation.
    pdf->size = photo->size;
    pdf->crc[0] = photo->crc[0];
    pdf->crc[1] = photo->crc[1];
    pdf->crc[2] = photo->crc[2];
    pdf->crc[3] = photo->crc[3];
    pdf->isvalid = photo->isvalid;
    mClient->mNodeManager.saveNodeInDb(pdf.get());

    std::string noMtimeFp;
    photo->FileFingerprint::serializeExcludingMtime(&noMtimeFp);

    auto* table = dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    ASSERT_NE(table, nullptr);

    std::vector<std::pair<NodeHandle, NodeSerialized>> out;
    ASSERT_TRUE(table->getNodesByFingerprintNoMtime(noMtimeFp, out));

    expectAllHandlesFoundWithMediats(out,
                                     {
                                         {hPhoto, 1700000000000ULL},
                                         {hPdf, 0ULL},
                                     });
}

// A.8 — getRootNodes. All root-type nodes have mediats=0 (folders), so the
// assertion is that zero values round-trip cleanly; a wrong column index that
// happens to land on a non-zero column would fail.
TEST_F(MediaTsDbFixture, GetRootNodesReadsMediatsColumn)
{
    // SetUp() already created ROOTNODE at mRootHandle; add the other two root types.
    const NodeHandle hVault = addRootNodeOfType(VAULTNODE, "VAULT");
    const NodeHandle hRubbish = addRootNodeOfType(RUBBISHNODE, "RUBBISH");

    auto* table = dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    ASSERT_NE(table, nullptr);

    std::vector<std::pair<NodeHandle, NodeSerialized>> out;
    ASSERT_TRUE(table->getRootNodes(out));

    expectAllHandlesFoundWithMediats(out,
                                     {
                                         {mRootHandle, 0ULL},
                                         {hVault, 0ULL},
                                         {hRubbish, 0ULL},
                                     });
}

// A.9 — getNodesWithSharesOrLink (LINK variant). All four variants share
// identical SELECT text differing only by the IN-list bitmask, so one
// variant proves the column layout for all of them.
TEST_F(MediaTsDbFixture, GetNodesWithSharesOrLinkReadsMediatsColumn)
{
    const NodeHandle hPhoto = addFileNode("IMG_20240115_103045.jpg");
    const NodeHandle hPdf = addFileNode("document.pdf");
    addFileNode("unshared.jpg", 1700000000LL); // must NOT appear in result

    addPublicLinkMarker(hPhoto);
    addPublicLinkMarker(hPdf);

    auto* table = dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    ASSERT_NE(table, nullptr);

    std::vector<std::pair<NodeHandle, NodeSerialized>> out;
    ASSERT_TRUE(table->getNodesWithSharesOrLink(out, LINK));

    expectAllHandlesFoundWithMediats(out,
                                     {
                                         {hPhoto, 1705314645000ULL},
                                         {hPdf, 0ULL},
                                     });
}

// A.10 — listChildNodesLexicographically
TEST_F(MediaTsDbFixture, ListChildNodesLexicographicallyReadsMediatsColumn)
{
    const NodeHandle hPhoto1 = addFileNode("IMG_20240115_103045.jpg");
    const NodeHandle hPhoto2 = addFileNode("photo_b.jpg", 1700000000LL);
    const NodeHandle hPdf = addFileNode("document.pdf");
    const NodeHandle hFolder = addFolderNode("subfolder");

    auto* table = dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    ASSERT_NE(table, nullptr);

    std::vector<std::pair<NodeHandle, NodeSerialized>> out;
    CancelToken ct;
    ASSERT_TRUE(table->listChildNodesLexicographically(mRootHandle.as8byte(),
                                                       out,
                                                       ct,
                                                       /*maxElements=*/100,
                                                       std::nullopt));

    expectAllHandlesFoundWithMediats(out,
                                     {
                                         {hPhoto1, 1705314645000ULL},
                                         {hPhoto2, 1700000000000ULL},
                                         {hPdf, 0ULL},
                                         {hFolder, 0ULL},
                                     });
}

// ---------------------------------------------------------------------------
// Section B: ORDER_MEDIATS_ASC/DESC for searchNodes and getChildren.
//
// listAllNodesByPage's MEDIATS order is covered by TieBreakTest in
// Sqlite_test.cpp; that goes through a different code path (buildOrderByForListAll).
// These tests cover the OrderByClause::get() MEDIATS branches — the only
// callers are searchNodes and getChildren.
// ---------------------------------------------------------------------------

namespace
{

void expectPhotosInMediatsOrder(const std::vector<std::pair<NodeHandle, NodeSerialized>>& rows,
                                bool ascending)
{
    ASSERT_EQ(rows.size(), kOrderDatasetMediats.size())
        << "photo row count mismatch (filter did not isolate photos?)";
    std::vector<uint64_t> sorted(kOrderDatasetMediats.begin(), kOrderDatasetMediats.end());
    if (ascending)
        std::sort(sorted.begin(), sorted.end());
    else
        std::sort(sorted.begin(), sorted.end(), std::greater<uint64_t>{});
    for (size_t i = 0; i < rows.size(); ++i)
        EXPECT_EQ(rows[i].second.mMediaTs, sorted[i])
            << "position " << i << " not in " << (ascending ? "ascending" : "descending")
            << " mediats order";
}

} // namespace

// B.1 — searchNodes ORDER BY mediats ASC
TEST_F(MediaTsDbFixture, SearchNodesOrderByMediatsAsc)
{
    seedOrderDataset();

    auto* table = dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    ASSERT_NE(table, nullptr);

    NodeSearchFilter filter;
    filter.byAncestors({mRootHandle.as8byte(), UNDEF, UNDEF});
    filter.byNodeType(FILENODE);
    filter.byCategory(MIME_TYPE_PHOTO);

    std::vector<std::pair<NodeHandle, NodeSerialized>> out;
    CancelToken ct;
    ASSERT_TRUE(
        table->searchNodes(filter, OrderByClause::MEDIATS_ASC, out, ct, NodeSearchPage{0, 0}));

    expectPhotosInMediatsOrder(out, /*ascending=*/true);
}

// B.2 — searchNodes ORDER BY mediats DESC
TEST_F(MediaTsDbFixture, SearchNodesOrderByMediatsDesc)
{
    seedOrderDataset();

    auto* table = dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    ASSERT_NE(table, nullptr);

    NodeSearchFilter filter;
    filter.byAncestors({mRootHandle.as8byte(), UNDEF, UNDEF});
    filter.byNodeType(FILENODE);
    filter.byCategory(MIME_TYPE_PHOTO);

    std::vector<std::pair<NodeHandle, NodeSerialized>> out;
    CancelToken ct;
    ASSERT_TRUE(
        table->searchNodes(filter, OrderByClause::MEDIATS_DESC, out, ct, NodeSearchPage{0, 0}));

    expectPhotosInMediatsOrder(out, /*ascending=*/false);
}

// B.3 — getChildren ORDER BY mediats ASC
TEST_F(MediaTsDbFixture, GetChildrenOrderByMediatsAsc)
{
    seedOrderDataset();

    auto* table = dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    ASSERT_NE(table, nullptr);

    NodeSearchFilter filter;
    filter.byLocationHandle(mRootHandle.as8byte());
    filter.byNodeType(FILENODE);
    filter.byCategory(MIME_TYPE_PHOTO);

    std::vector<std::pair<NodeHandle, NodeSerialized>> out;
    CancelToken ct;
    ASSERT_TRUE(
        table->getChildren(filter, OrderByClause::MEDIATS_ASC, out, ct, NodeSearchPage{0, 0}));

    expectPhotosInMediatsOrder(out, /*ascending=*/true);
}

// B.4 — getChildren ORDER BY mediats DESC
TEST_F(MediaTsDbFixture, GetChildrenOrderByMediatsDesc)
{
    seedOrderDataset();

    auto* table = dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    ASSERT_NE(table, nullptr);

    NodeSearchFilter filter;
    filter.byLocationHandle(mRootHandle.as8byte());
    filter.byNodeType(FILENODE);
    filter.byCategory(MIME_TYPE_PHOTO);

    std::vector<std::pair<NodeHandle, NodeSerialized>> out;
    CancelToken ct;
    ASSERT_TRUE(
        table->getChildren(filter, OrderByClause::MEDIATS_DESC, out, ct, NodeSearchPage{0, 0}));

    expectPhotosInMediatsOrder(out, /*ascending=*/false);
}

#endif // USE_SQLITE
