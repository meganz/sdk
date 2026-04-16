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

#include <filesystem>
#include <stdfs.h>

namespace fs = std::filesystem;

using namespace mega;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

const nameid kNameId = AttrMap::string2nameid("n");

class MediaTsDbFixture: public ::testing::Test
{
protected:
    mega::MegaApp mApp;
    NodeManager::MissingParentNodes mMissingParentNodes;
    std::shared_ptr<MegaClient> mClient;
    fs::path mTestDir;
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

    // Read the mediats SQL column directly (not from the BLOB).
    // Commits the SDK transaction, reads via a separate connection, then
    // re-opens the transaction. This is a temporary function to query, will be removed in task2.
    uint64_t rawSqlMediatsColumn(NodeHandle h)
    {
        mClient->sctable->commit();
        MrProper guard(
            [this]()
            {
                mClient->sctable->begin();
            });

        fs::path dbPath;
        for (auto& entry: fs::directory_iterator(mTestDir))
        {
            if (entry.path().extension() == ".db")
            {
                dbPath = entry.path();
                break;
            }
        }

        uint64_t val = UINT64_MAX;
        if (!dbPath.empty())
        {
            sqlite3* rawDb = nullptr;
            if (sqlite3_open_v2(dbPath.string().c_str(),
                                &rawDb,
                                SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX,
                                nullptr) == SQLITE_OK)
            {
                sqlite3_stmt* stmt = nullptr;
                const char* sql = "SELECT mediats FROM nodes WHERE nodehandle=?";
                if (sqlite3_prepare_v2(rawDb, sql, -1, &stmt, nullptr) == SQLITE_OK)
                {
                    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(h.as8byte()));
                    if (sqlite3_step(stmt) == SQLITE_ROW)
                        val = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
                    sqlite3_finalize(stmt);
                }
                sqlite3_close(rawDb);
            }
        }

        return val;
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
    EXPECT_EQ(rawSqlMediatsColumn(h), 1705314645000LL);
}

// ---------------------------------------------------------------------------
// Test: mediats falls back to mtime when filename has no pattern
// ---------------------------------------------------------------------------
TEST_F(MediaTsDbFixture, PutFallsBackToMtime)
{
    const m_time_t mtime = 1700000000LL;
    NodeHandle h = addFileNode("photo.jpg", mtime, 1600000000LL);
    EXPECT_EQ(rawSqlMediatsColumn(h), static_cast<uint64_t>(mtime) * 1000);
}

// ---------------------------------------------------------------------------
// Test: mediats falls back to ctime when mtime is 0
// ---------------------------------------------------------------------------
TEST_F(MediaTsDbFixture, PutFallsBackToCtime)
{
    const m_time_t ctime = 1600000000LL;
    NodeHandle h = addFileNode("photo.jpg", 0, ctime);
    EXPECT_EQ(rawSqlMediatsColumn(h), static_cast<uint64_t>(ctime) * 1000);
}

// ---------------------------------------------------------------------------
// Test: mediats is 0 for non-media files
// ---------------------------------------------------------------------------
TEST_F(MediaTsDbFixture, NonMediaFileHasZeroMediats)
{
    NodeHandle h = addFileNode("document.pdf");
    EXPECT_EQ(rawSqlMediatsColumn(h), 0);
}

// ---------------------------------------------------------------------------
// Test: mediats is 0 for folder nodes
// ---------------------------------------------------------------------------
TEST_F(MediaTsDbFixture, FolderNodeHasZeroMediats)
{
    NodeHandle h = addFolderNode("MyFolder");
    EXPECT_EQ(rawSqlMediatsColumn(h), 0);
}

// ---------------------------------------------------------------------------
// Test: filename pattern takes priority over mtime
// ---------------------------------------------------------------------------
TEST_F(MediaTsDbFixture, FilenamePatternWinsOverMtime)
{
    // Filename says 2024-01-15T10:30:45Z, but mtime is different
    NodeHandle h = addFileNode("IMG_20240115_103045.jpg", 1000000LL, 999000LL);
    EXPECT_EQ(rawSqlMediatsColumn(h), 1705314645000LL);
}

// ---------------------------------------------------------------------------
// Test: multiple nodes each get correct mediats
// ---------------------------------------------------------------------------
TEST_F(MediaTsDbFixture, MultipleNodesCorrectMediats)
{
    NodeHandle h1 = addFileNode("IMG_20240115_103045.jpg"); // filename pattern
    NodeHandle h2 = addFileNode("photo.jpg", 1700000000LL, 0); // mtime fallback
    NodeHandle h3 = addFileNode("document.pdf"); // non-media → 0

    EXPECT_EQ(rawSqlMediatsColumn(h1), 1705314645000LL);
    EXPECT_EQ(rawSqlMediatsColumn(h2), 1700000000000LL);
    EXPECT_EQ(rawSqlMediatsColumn(h3), 0);
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
    ASSERT_EQ(rawSqlMediatsColumn(hPattern), 1705314645000LL);
    ASSERT_GT(rawSqlMediatsColumn(hMtime), uint64_t{0});

    // Phase 2: Close SDK and drop the mediats column to simulate old schema.
    mClient->sctable->commit();
    mClient.reset();

    // Find the .db file
    fs::path dbPath;
    for (auto& entry: fs::directory_iterator(mTestDir))
    {
        if (entry.path().extension() == ".db")
        {
            dbPath = entry.path();
            break;
        }
    }
    ASSERT_FALSE(dbPath.empty()) << "DB file not found in test directory";

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
    EXPECT_EQ(rawSqlMediatsColumn(hPattern), 1705314645000LL)
        << "Migration should compute mediats from filename pattern in blob";

    // Media file without pattern → mtime * 1000
    EXPECT_EQ(rawSqlMediatsColumn(hMtime), static_cast<uint64_t>(mediaMtime) * 1000)
        << "Migration should fall back to mtime from blob fingerprint";

    // Media file, mtime=0 → ctime * 1000
    EXPECT_EQ(rawSqlMediatsColumn(hCtime), static_cast<uint64_t>(mediaCtime) * 1000)
        << "Migration should fall back to ctime from DB column";

    // Non-media file → 0
    EXPECT_EQ(rawSqlMediatsColumn(hNonMedia), 0) << "Migration should set 0 for non-media files";

    // Folder → 0
    EXPECT_EQ(rawSqlMediatsColumn(hFolder), 0) << "Migration should set 0 for folder nodes";
}

#endif // USE_SQLITE
