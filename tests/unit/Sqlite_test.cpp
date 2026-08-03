/**
 * @file Sqlite_test.cpp
 * @brief Unit tests for Sqlite-backed node storage, including
 *        listAllNodesByPage() cursor-based (keyset) pagination.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mega/db/sqlite.h>
#include <mega/localpath.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <mega.h>
#include <set>
#include <sqlite3.h>
#include <stdfs.h>
#include <string>
#include <vector>

using namespace mega;

/**
 * @brief Validate renameDBFiles method
 *
 * Steps:
 *  - Create new data base
 *  - Call to renameDBFiles
 *  - Check if all files has been renamed
 */
#ifdef WIN32
// As DB is opened when files are renamed, windows doesn't allow rename DB files
TEST(Sqlite, DISABLED_renameDB)
{
#else
TEST(Sqlite, renameDB)
{
#endif
    auto pathString{std::filesystem::current_path() / "folder"};

    const MrProper cleanUp(
        [pathString]()
        {
            std::filesystem::remove_all(pathString);
        });

    std::filesystem::create_directory(pathString);
    LocalPath folderPath = LocalPath::fromAbsolutePath(path_u8string(pathString));
    SqliteDbAccess dbAccess{folderPath};

    // Create and open DB
    std::unique_ptr<FileSystemAccess> fsaccess{new FSACCESS_CLASS};
    const std::string dbName{"dbName"};
    LocalPath currentDataBasePath{dbAccess.databasePath(*fsaccess, dbName, DbAccess::DB_VERSION)};
    PrnGen rng;
    constexpr int flags = 0;
    std::unique_ptr<SqliteDbTable> db{dbAccess.open(rng, *fsaccess, dbName, flags, nullptr)};
    if (!db)
    {
        ASSERT_TRUE(false) << "Failure opening DB";
    }

    // Insert elements
    for (int i = 1; i < 10; ++i)
    {
        std::string content = "content " + std::to_string(i);
        db->put(static_cast<uint32_t>((i += DbTable::IDSPACING) | MegaClient::CACHEDUSER),
                static_cast<char*>(content.data()),
                static_cast<unsigned>(content.length()));
    }

    // check if auxiliar files exist
    LocalPath shmPath = currentDataBasePath;
    shmPath.append(LocalPath::fromRelativePath("-shm"));
    bool shmExists = std::filesystem::exists(shmPath.toPath(false));
    EXPECT_TRUE(shmExists) << "Unexpected behavior, -shm file doesn't exist";

    LocalPath walPath = currentDataBasePath;
    walPath.append(LocalPath::fromRelativePath("-wal"));
    bool walExists = std::filesystem::exists(walPath.toPath(false));
    EXPECT_TRUE(walExists) << "Unexpected behavior, -wal file doesn't exist";

    // Determine new path
    const std::string dbNewName{"dbNewName"};
    LocalPath newDataBasePath{dbAccess.databasePath(*fsaccess, dbNewName, DbAccess::DB_VERSION)};

    // Rename DB
    EXPECT_TRUE(dbAccess.renameDBFiles(*fsaccess, currentDataBasePath, newDataBasePath))
        << "Failure to rename files (maybe they are in use)";

    // Verify if auxiliar files exist
    if (shmExists)
    {
        shmPath = newDataBasePath;
        shmPath.append(LocalPath::fromRelativePath("-shm"));
        std::string aux = shmPath.toPath(false);
        EXPECT_TRUE(std::filesystem::exists(aux))
            << "File " << aux << "doesn't exit when it should";
    }

    if (walExists)
    {
        walPath = newDataBasePath;
        walPath.append(LocalPath::fromRelativePath("-wal"));
        std::string aux = walPath.toPath(false);
        EXPECT_TRUE(std::filesystem::exists(aux))
            << "File " << aux << "doesn't exit when it should";
    }
}

namespace
{

/**
 * @brief Computed paths for the legacy/current DB triplets seeded by
 *        createLegacyDbTestFiles().
 */
struct LegacyDbTestFiles
{
    LocalPath legacyPath;
    LocalPath legacyShm;
    LocalPath legacyWal;
    LocalPath currentPath;
    LocalPath currentShm;
    LocalPath currentWal;
};

/**
 * @brief Creates the test folder and seeds DB placeholder files for
 *        checkDbFileAndAdjustLegacy_* tests.
 *
 * Seeds both the legacy and the current-version triplet (main, -shm, -wal)
 * with distinct contents ("legacy-*" and "current-*") so callers can verify
 * reuse, rename, and delete semantics.
 *
 * @param folderFsPath  filesystem directory to create
 * @param dbAccess      used to compute the legacy/current paths
 * @param fsaccess      used by databasePath()
 * @param dbName        DB base name
 * @return Computed legacy and current paths (with -shm/-wal sidecars).
 */
LegacyDbTestFiles createLegacyDbTestFiles(const std::filesystem::path& folderFsPath,
                                          SqliteDbAccess& dbAccess,
                                          FileSystemAccess& fsaccess,
                                          const std::string& dbName)
{
    std::filesystem::create_directory(folderFsPath);

    LegacyDbTestFiles p;
    p.legacyPath = dbAccess.databasePath(fsaccess, dbName, DbAccess::LEGACY_DB_VERSION);
    p.legacyShm = p.legacyPath;
    p.legacyShm.append(LocalPath::fromRelativePath("-shm"));
    p.legacyWal = p.legacyPath;
    p.legacyWal.append(LocalPath::fromRelativePath("-wal"));

    p.currentPath = dbAccess.databasePath(fsaccess, dbName, DbAccess::DB_VERSION);
    p.currentShm = p.currentPath;
    p.currentShm.append(LocalPath::fromRelativePath("-shm"));
    p.currentWal = p.currentPath;
    p.currentWal.append(LocalPath::fromRelativePath("-wal"));

    const std::vector<std::pair<LocalPath, std::string>> seed = {
        {p.legacyPath, "legacy-main"},
        {p.legacyShm, "legacy-shm"},
        {p.legacyWal, "legacy-wal"},
        {p.currentPath, "current-main"},
        {p.currentShm, "current-shm"},
        {p.currentWal, "current-wal"},
    };

    for (const auto& [path, contents]: seed)
    {
        std::ofstream{path.toPath(false)} << contents;
        EXPECT_TRUE(std::filesystem::exists(path.toPath(false)))
            << "Failed to create placeholder file " << path.toPath(false);
    }

    return p;
}

} // namespace

/**
 * @brief Validate checkDbFileAndAdjustLegacy method reuses legacy DB files
 *
 *
 * Steps:
 *  - Init currentDbVersion of DbAccess to LEGACY_DB_VERSION.
 *  - Drop placeholder files at both the legacy and the current-version paths
 *    (main, -shm, -wal sidecars) with distinct content, named with SQLite's
 *    WAL convention.
 *  - Call checkDbFileAndAdjustLegacy with flags 0.
 *  - Assert dbPath now points at the legacy path and the function reports the
 *    DB as existing.
 *  - Verify both the legacy and current triplets are left untouched (content
 *    unchanged); the reuse path must not modify the current-version files.
 */
TEST(Sqlite, checkDbFileAndAdjustLegacy_useLegacyDB)
{
    if (DbAccess::LEGACY_DB_VERSION == DbAccess::LAST_DB_VERSION_WITHOUT_NOD ||
        DbAccess::LEGACY_DB_VERSION == DbAccess::LAST_DB_VERSION_WITHOUT_SRW ||
        DbAccess::LEGACY_DB_VERSION == DbAccess::LAST_DB_VERSION_WITHOUT_VFINGERPRINT)
    {
        GTEST_SKIP()
            << "use-legacy-DB branch is unreachable: LEGACY_DB_VERSION sits at a migration cutoff";
    }

    auto pathString{std::filesystem::current_path() / "folder_use_legacy"};

    const MrProper cleanUp(
        [pathString]()
        {
            std::filesystem::remove_all(pathString);
        });

    LocalPath folderPath = LocalPath::fromAbsolutePath(path_u8string(pathString));
    SqliteDbAccess dbAccess{folderPath};

    std::unique_ptr<FileSystemAccess> fsaccess{new FSACCESS_CLASS};
    const std::string dbName{"dbToTest"};

    const LegacyDbTestFiles paths =
        createLegacyDbTestFiles(pathString, dbAccess, *fsaccess, dbName);

    LocalPath dbPath;
    constexpr int flags = 0;
    dbAccess.currentDbVersion = DbAccess::LEGACY_DB_VERSION;
    const bool exists = dbAccess.checkDbFileAndAdjustLegacy(*fsaccess, dbName, flags, dbPath);

    EXPECT_TRUE(exists) << "checkDbFileAndAdjustLegacy should report the legacy DB as existing";
    EXPECT_EQ(dbPath.toPath(false), paths.legacyPath.toPath(false))
        << "dbPath should be the legacy path when reusing the legacy DB as-is";
    EXPECT_EQ(dbAccess.currentDbVersion, DbAccess::LEGACY_DB_VERSION)
        << "currentDbVersion should remain at LEGACY_DB_VERSION when reusing the legacy DB";

    auto readFile = [](const LocalPath& p)
    {
        std::ifstream in{p.toPath(false)};
        return std::string{std::istreambuf_iterator<char>(in), {}};
    };

    LocalPath dbShm = dbPath;
    dbShm.append(LocalPath::fromRelativePath("-shm"));
    LocalPath dbWal = dbPath;
    dbWal.append(LocalPath::fromRelativePath("-wal"));

    EXPECT_EQ(readFile(dbPath), "legacy-main") << "Legacy DB file should be untouched";
    EXPECT_EQ(readFile(dbShm), "legacy-shm") << "Legacy -shm sidecar should be untouched";
    EXPECT_EQ(readFile(dbWal), "legacy-wal") << "Legacy -wal sidecar should be untouched";
    EXPECT_EQ(readFile(paths.currentPath), "current-main")
        << "Current DB file should be untouched when reusing the legacy DB";
    EXPECT_EQ(readFile(paths.currentShm), "current-shm")
        << "Current -shm sidecar should be untouched when reusing the legacy DB";
    EXPECT_EQ(readFile(paths.currentWal), "current-wal")
        << "Current -wal sidecar should be untouched when reusing the legacy DB";
}

/**
 * @brief Validate checkDbFileAndAdjustLegacy method recycles legacy DB files
 *
 *
 * Steps:
 *  - Init currentDbVersion of DbAccess to DB_VERSION.
 *  - Drop placeholder files at the legacy paths AND the current-version paths
 *    (main, -shm, -wal sidecars), each with distinct content so the rename is
 *    verifiable. Files are named with SQLite's WAL convention (suffix appended
 *    to the full filename).
 *  - Call checkDbFileAndAdjustLegacy with flags DB_OPEN_FLAG_RECYCLE. This wipes
 *    the stale current-version files via removeDBFiles, then renames the legacy
 *    triplet onto the current-version paths.
 *  - Assert all three legacy files have been removed.
 *  - Verify the rename by comparing content: each current-version file must now
 *    hold what was originally written to its legacy counterpart.
 */
TEST(Sqlite, checkDbFileAndAdjustLegacy_recycleLegacyDB)
{
    auto pathString{std::filesystem::current_path() / "folder_recycle_legacy"};

    const MrProper cleanUp(
        [pathString]()
        {
            std::filesystem::remove_all(pathString);
        });

    LocalPath folderPath = LocalPath::fromAbsolutePath(path_u8string(pathString));
    SqliteDbAccess dbAccess{folderPath};

    std::unique_ptr<FileSystemAccess> fsaccess{new FSACCESS_CLASS};
    const std::string dbName{"dbToTest"};

    const LegacyDbTestFiles paths =
        createLegacyDbTestFiles(pathString, dbAccess, *fsaccess, dbName);

    LocalPath dbPath;
    constexpr int flags = DB_OPEN_FLAG_RECYCLE;
    dbAccess.currentDbVersion = DbAccess::DB_VERSION;
    const bool exists = dbAccess.checkDbFileAndAdjustLegacy(*fsaccess, dbName, flags, dbPath);

    EXPECT_TRUE(exists) << "checkDbFileAndAdjustLegacy should report the legacy DB as existing";
    EXPECT_EQ(dbPath.toPath(false), paths.currentPath.toPath(false))
        << "dbPath should be the current path when recycling the legacy DB as-is";
    EXPECT_TRUE(dbAccess.currentDbVersion == DbAccess::DB_VERSION)
        << "currentDbVersion was not updated to DB_VERSION";

    EXPECT_FALSE(std::filesystem::exists(paths.legacyPath.toPath(false)))
        << "Legacy DB file " << paths.legacyPath.toPath(false) << " still exists";
    EXPECT_FALSE(std::filesystem::exists(paths.legacyShm.toPath(false)))
        << "Legacy -shm sidecar " << paths.legacyShm.toPath(false) << " still exists";
    EXPECT_FALSE(std::filesystem::exists(paths.legacyWal.toPath(false)))
        << "Legacy -wal sidecar " << paths.legacyWal.toPath(false) << " still exists";

    auto readFile = [](const LocalPath& p)
    {
        std::ifstream in{p.toPath(false)};
        return std::string{std::istreambuf_iterator<char>(in), {}};
    };

    LocalPath dbShm = dbPath;
    dbShm.append(LocalPath::fromRelativePath("-shm"));
    LocalPath dbWal = dbPath;
    dbWal.append(LocalPath::fromRelativePath("-wal"));

    EXPECT_EQ(readFile(dbPath), "legacy-main")
        << "Current DB file content doesn't match the legacy main content";
    EXPECT_EQ(readFile(dbShm), "legacy-shm")
        << "Current -shm sidecar content doesn't match the legacy -shm content";
    EXPECT_EQ(readFile(dbWal), "legacy-wal")
        << "Current -wal sidecar content doesn't match the legacy -wal content";
}

/**
 * @brief Validate checkDbFileAndAdjustLegacy method deletes legacy DB files
 *
 *
 * Steps:
 *  - Init currentDbVersion of DbAccess to DB_VERSION.
 *  - Drop placeholder files at both the legacy and the current-version paths
 *    (main, -shm, -wal sidecars), named with SQLite's WAL convention (suffix
 *    appended to the full filename).
 *  - Call checkDbFileAndAdjustLegacy with flags 0.
 *  - Assert all three legacy files have been removed.
 *  - Verify the current-version triplet is left untouched (content unchanged);
 *    the delete path must not modify the current-version files.
 */
TEST(Sqlite, checkDbFileAndAdjustLegacy_deleteLegacyDB)
{
    auto pathString{std::filesystem::current_path() / "folder_remove_legacy"};

    const MrProper cleanUp(
        [pathString]()
        {
            std::filesystem::remove_all(pathString);
        });

    LocalPath folderPath = LocalPath::fromAbsolutePath(path_u8string(pathString));
    SqliteDbAccess dbAccess{folderPath};

    std::unique_ptr<FileSystemAccess> fsaccess{new FSACCESS_CLASS};
    const std::string dbName{"dbToTest"};

    const LegacyDbTestFiles paths =
        createLegacyDbTestFiles(pathString, dbAccess, *fsaccess, dbName);

    LocalPath dbPath;
    constexpr int flags = 0;
    dbAccess.currentDbVersion = DbAccess::DB_VERSION;
    const bool exists = dbAccess.checkDbFileAndAdjustLegacy(*fsaccess, dbName, flags, dbPath);

    EXPECT_TRUE(dbAccess.currentDbVersion == DbAccess::DB_VERSION)
        << "currentDbVersion was not updated to DB_VERSION";

    EXPECT_TRUE(exists) << "checkDbFileAndAdjustLegacy should report the current DB as existing";
    EXPECT_EQ(dbPath.toPath(false), paths.currentPath.toPath(false))
        << "dbPath should be the current path when deleting the legacy DB as-is";

    EXPECT_FALSE(std::filesystem::exists(paths.legacyPath.toPath(false)))
        << "Legacy DB file " << paths.legacyPath.toPath(false) << " still exists";
    EXPECT_FALSE(std::filesystem::exists(paths.legacyShm.toPath(false)))
        << "Legacy -shm sidecar " << paths.legacyShm.toPath(false) << " still exists";
    EXPECT_FALSE(std::filesystem::exists(paths.legacyWal.toPath(false)))
        << "Legacy -wal sidecar " << paths.legacyWal.toPath(false) << " still exists";

    auto readFile = [](const LocalPath& p)
    {
        std::ifstream in{p.toPath(false)};
        return std::string{std::istreambuf_iterator<char>(in), {}};
    };

    EXPECT_EQ(readFile(paths.currentPath), "current-main")
        << "Current DB file should be untouched when deleting the legacy DB";
    EXPECT_EQ(readFile(paths.currentShm), "current-shm")
        << "Current -shm sidecar should be untouched when deleting the legacy DB";
    EXPECT_EQ(readFile(paths.currentWal), "current-wal")
        << "Current -wal sidecar should be untouched when deleting the legacy DB";
}

#ifdef USE_SQLITE

#include "utils.h"

#include <mega/megaapp.h>
#include <mega/megaclient.h>

namespace
{

// sqlite3_close_v2 — defers cleanup so ASSERT_* mid-loop doesn't leak via SQLITE_BUSY.
using SqliteHandle = std::unique_ptr<sqlite3, decltype(&sqlite3_close_v2)>;

std::filesystem::path makeFreshTestDir(const char* name)
{
    auto dirPath = std::filesystem::current_path() / name;
    std::filesystem::remove_all(dirPath);
    std::filesystem::create_directory(dirPath);
    return dirPath;
}

std::pair<SqliteHandle, int> openSqliteRaw(const std::string& path)
{
    sqlite3* raw = nullptr;
    const int rc = sqlite3_open(path.c_str(), &raw);
    return {SqliteHandle{raw, &sqlite3_close_v2}, rc};
}

std::set<std::string> readNodesColumnSet(const std::string& dbPath)
{
    std::set<std::string> cols;
    auto [dbGuard, openRc] = openSqliteRaw(dbPath);
    if (openRc != SQLITE_OK)
        return cols;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(dbGuard.get(),
                           "SELECT name FROM pragma_table_xinfo('nodes')",
                           -1,
                           &stmt,
                           nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            // sqlite3_column_text can return nullptr on OOM; std::string(nullptr) is UB.
            if (const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)))
            {
                cols.emplace(name);
            }
        }
    }
    sqlite3_finalize(stmt);
    return cols;
}

// Index counterpart of readNodesColumnSet — the explicit (non-internal) index names on
// the `nodes` table. 'sqlite_%' autoindexes (e.g. for the PRIMARY KEY) are excluded so only
// indexes created by createIndexes() are compared.
std::set<std::string> readNodesIndexSet(const std::string& dbPath)
{
    std::set<std::string> idx;
    auto [dbGuard, openRc] = openSqliteRaw(dbPath);
    if (openRc != SQLITE_OK)
        return idx;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(dbGuard.get(),
                           "SELECT name FROM sqlite_master WHERE type = 'index' "
                           "AND tbl_name = 'nodes' AND name NOT LIKE 'sqlite_%'",
                           -1,
                           &stmt,
                           nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            if (const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)))
            {
                idx.emplace(name);
            }
        }
    }
    sqlite3_finalize(stmt);
    return idx;
}

// Path of the node statecache DB under `dir`. A MegaClient creates several ".db" files (nodes
// statecache, prefs, …), so we can't pick by extension alone — return the one that actually has a
// `nodes` table. Empty if none.
std::string findNodesDbFile(const std::filesystem::path& dir)
{
    for (const auto& entry: std::filesystem::directory_iterator(dir))
    {
        const auto name = entry.path().filename().string();
        if (!entry.is_regular_file() || name.size() < 3 ||
            name.compare(name.size() - 3, 3, ".db") != 0)
        {
            continue;
        }
        const std::string path = entry.path().string();
        auto [dbGuard, rc] = openSqliteRaw(path);
        if (rc != SQLITE_OK)
            continue;
        sqlite3_stmt* stmt = nullptr;
        bool hasNodesTable = false;
        if (sqlite3_prepare_v2(
                dbGuard.get(),
                "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'nodes'",
                -1,
                &stmt,
                nullptr) == SQLITE_OK)
        {
            hasNodesTable = (sqlite3_step(stmt) == SQLITE_ROW);
        }
        sqlite3_finalize(stmt);
        if (hasNodesTable)
            return path;
    }
    return {};
}

// Per-row test fixture for migration seed/verify.
struct SeedRow
{
    handle nh;
    m_time_t mtime;
    int label; // 0 == LBL_UNKNOWN (unlabelled)
    std::string description;
    std::string tags;
};

// Builds an in-memory Node with the row's attrs set so Node::serialize()
// produces a BLOB that NodeData (mtime/label/description/tags accessors) can
// round-trip. Returns the serialized BLOB.
std::string buildSeedNodeBlob(MegaClient& client, const SeedRow& r)
{
    const NodeHandle h = NodeHandle().set6byte(r.nh);
    auto node = mt::makeNode(client, FILENODE, h);

    // mtime is read from the 'c' attr fingerprint, not a direct field.
    node->size = 100;
    node->mtime = r.mtime;
    node->ctime = r.mtime;
    node->crc[0] = static_cast<int32_t>(r.nh);
    node->isvalid = true;
    node->serializefingerprint(&node->attrs.map['c']);
    node->setfingerprint();

    if (r.label > 0)
    {
        node->attrs.map[AttrMap::string2nameid("lbl")] = std::to_string(r.label);
    }
    node->attrs.map[AttrMap::string2nameid(MegaClient::NODE_ATTRIBUTE_DESCRIPTION)] = r.description;
    if (!r.tags.empty()) // empty tags ⇒ attr absent, matches production
    {
        node->attrs.map[AttrMap::string2nameid(MegaClient::NODE_ATTRIBUTE_TAGS)] = r.tags;
    }

    std::string blob;
    [[maybe_unused]] const bool ok = node->serialize(&blob);
    assert(ok);
    return blob;
}

// Binds a SeedRow into a 12-column INSERT prepared statement against the
// pre-migration `nodes` schema. counterBlob and blob must outlive sqlite3_step.
void bindSeedRow(sqlite3_stmt* ins,
                 const SeedRow& r,
                 const std::string& blob,
                 const std::string& counterBlob)
{
    sqlite3_reset(ins);
    sqlite3_clear_bindings(ins);
    sqlite3_bind_int64(ins, 1, static_cast<sqlite3_int64>(r.nh));
    sqlite3_bind_int64(ins, 2, 0);
    sqlite3_bind_text(ins, 3, "file.txt", -1, SQLITE_STATIC);
    sqlite3_bind_blob(ins, 4, "", 0, SQLITE_STATIC);
    sqlite3_bind_blob(ins, 5, "", 0, SQLITE_STATIC);
    sqlite3_bind_int(ins, 6, FILENODE);
    sqlite3_bind_int(ins, 7, 0);
    sqlite3_bind_int(ins, 8, 0);
    sqlite3_bind_int64(ins, 9, r.mtime);
    sqlite3_bind_int64(ins, 10, 0);
    sqlite3_bind_blob(ins,
                      11,
                      counterBlob.data(),
                      static_cast<int>(counterBlob.size()),
                      SQLITE_TRANSIENT);
    sqlite3_bind_blob(ins, 12, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);
}

// Reads the four migrated columns for `nh` from an open DB. Returns defaults
// (zero / empty strings) if the row is missing.
struct MigratedRowValues
{
    m_time_t mtime = 0;
    int label = 0;
    std::string description;
    std::string tags;
    bool found = false;
};

MigratedRowValues readMigratedRow(sqlite3* db, handle nh)
{
    MigratedRowValues v;
    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(db,
                           "SELECT mtime, label, description, tags FROM nodes "
                           "WHERE nodehandle = ?",
                           -1,
                           &s,
                           nullptr) != SQLITE_OK)
    {
        return v;
    }
    sqlite3_bind_int64(s, 1, static_cast<sqlite3_int64>(nh));
    if (sqlite3_step(s) == SQLITE_ROW)
    {
        v.found = true;
        v.mtime = sqlite3_column_int64(s, 0);
        v.label = sqlite3_column_int(s, 1);
        if (const auto* d = sqlite3_column_text(s, 2))
            v.description = reinterpret_cast<const char*>(d);
        if (const auto* t = sqlite3_column_text(s, 3))
            v.tags = reinterpret_cast<const char*>(t);
    }
    sqlite3_finalize(s);
    return v;
}

// ASSERT_* in a helper doesn't abort the caller — wrap calls in ASSERT_NO_FATAL_FAILURE.
void readAllMigratedRows(const std::string& dbPath,
                         const std::vector<SeedRow>& rows,
                         std::vector<MigratedRowValues>& out)
{
    auto [dbGuard, openRc] = openSqliteRaw(dbPath);
    ASSERT_EQ(SQLITE_OK, openRc);

    out.clear();
    out.reserve(rows.size());
    for (const auto& r: rows)
    {
        const auto got = readMigratedRow(dbGuard.get(), r.nh);
        ASSERT_TRUE(got.found) << "row missing, nh=" << r.nh;
        out.push_back(got);
    }
}

// Per-test scaffold: makes a fresh test dir, wires SqliteDbAccess + filesystem
// access against it, derives the db file path, and cleans the dir up on exit.
// Move-disabled because MrProper captures dirPath by value (safe) but moving
// the env would still split the SqliteDbAccess from its filesystem.
struct MigrationTestEnv
{
    std::filesystem::path dirPath;
    MrProper cleanUp;
    LocalPath folderPath;
    SqliteDbAccess dbAccess;
    std::unique_ptr<FileSystemAccess> fsaccess;
    std::string dbName;
    LocalPath dbLocalPath;
    std::string dbPathStr;

    MigrationTestEnv(const char* dirName, std::string dbN):
        dirPath(makeFreshTestDir(dirName)),
        cleanUp(
            [p = dirPath]()
            {
                std::filesystem::remove_all(p);
            }),
        folderPath(LocalPath::fromAbsolutePath(path_u8string(dirPath))),
        dbAccess(folderPath),
        fsaccess(new FSACCESS_CLASS),
        dbName(std::move(dbN)),
        dbLocalPath(dbAccess.databasePath(*fsaccess, dbName, DbAccess::DB_VERSION)),
        dbPathStr(dbLocalPath.toPath(false))
    {}

    MigrationTestEnv(const MigrationTestEnv&) = delete;
    MigrationTestEnv& operator=(const MigrationTestEnv&) = delete;
};

// Pre-migration `nodes` schema. Keep this schema frozen — do not add or
// remove columns here. It represents a pre-migration `nodes` table on disk,
// so the whole point of the tests that use it is to upgrade *this exact
// shape* to the current schema. Expanding it silently weakens the regression
// guard.
//
// Mirrors the CREATE TABLE `nodes` DDL in src/db/sqlite.cpp with all
// migration-added columns (the entries in the `newCols` vector inside
// SqliteDbAccess::openTableWithNodes) removed.
constexpr const char* kOldNodesSchema = "CREATE TABLE nodes ("
                                        " nodehandle int64 PRIMARY KEY NOT NULL,"
                                        " parenthandle int64,"
                                        " name text,"
                                        " fingerprint BLOB,"
                                        " origFingerprint BLOB,"
                                        " type tinyint,"
                                        " share tinyint,"
                                        " fav tinyint,"
                                        " ctime int64,"
                                        " flags int64,"
                                        " counter BLOB NOT NULL,"
                                        " node BLOB NOT NULL)";

// INSERT statement against the pre-migration schema. Binds 12 columns,
// matches bindSeedRow().
constexpr const char* kOldNodesInsertSql =
    "INSERT INTO nodes(nodehandle, parenthandle, name, fingerprint, "
    "origFingerprint, type, share, fav, ctime, flags, counter, node) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

// ASSERT_* in a helper doesn't abort the caller — wrap calls in ASSERT_NO_FATAL_FAILURE.
void seedOldSchemaWithRows(const std::string& dbPath, const std::vector<SeedRow>& rows)
{
    auto [dbGuard, openRc] = openSqliteRaw(dbPath);
    ASSERT_EQ(SQLITE_OK, openRc);

    char* err = nullptr;
    const int rc = sqlite3_exec(dbGuard.get(), kOldNodesSchema, nullptr, nullptr, &err);
    const std::string errStr = err ? err : "";
    sqlite3_free(err);
    ASSERT_EQ(SQLITE_OK, rc) << "Failed to seed old schema: " << errStr;

    if (rows.empty())
        return;

    // Transient client, no DbAccess — only used for Node::serialize().
    mega::MegaApp app;
    auto client = mt::makeClient(app);

    NodeCounter nc;
    nc.files = 1;
    nc.storage = 100;
    const std::string counterBlob = nc.serialize();

    sqlite3_stmt* ins = nullptr;
    ASSERT_EQ(SQLITE_OK, sqlite3_prepare_v2(dbGuard.get(), kOldNodesInsertSql, -1, &ins, nullptr));

    for (const auto& r: rows)
    {
        const std::string blob = buildSeedNodeBlob(*client, r);
        bindSeedRow(ins, r, blob, counterBlob); // resets + clears bindings internally
        ASSERT_EQ(SQLITE_DONE, sqlite3_step(ins))
            << "INSERT failed for nh=" << r.nh << ": " << sqlite3_errmsg(dbGuard.get());
    }
    sqlite3_finalize(ins);
}

} // anonymous namespace

/**
 * @brief Validate that opening a DB shaped like a previous schema version
 *        runs the column-migration path to completion AND populates the new
 *        columns with the correct per-row data (SDK-6155).
 *
 * Regression guard for DB-migration bugs where a new VIRTUAL column
 * references a base column that does not exist in older on-disk schemas,
 * e.g. "ADD COLUMN fingerprintVirtual32 BLOB AS (getFingerprintExcludingMtime(fp)) VIRTUAL"
 * failing with "no such column: fp" on upgraded databases.
 *
 * Steps:
 *  - Seed an on-disk DB with an older `nodes` schema (base columns only) and
 *    INSERT rows with serialized Node BLOBs encoding known attribute values.
 *  - Open via SqliteDbAccess::openTableWithNodes, which runs addAndPopulateColumns.
 *  - Assert the call succeeds, every expected column is present, the row count
 *    is unchanged, and each migrated column holds the value encoded in the BLOB.
 */
TEST(Sqlite, MigratesOldNodesSchema)
{
    MigrationTestEnv env("nodes_schema_migration_test", "nodes_schema_migration");

    const std::vector<SeedRow> rows = {
        {1, 1700000001, 1, "first node description", "alpha,beta"},
        {2, 1700000002, 2, "second", ""}, // empty tags
        {3, 1700000003, 0, "third with multi-byte chars", "x"}, // unlabelled (LBL_UNKNOWN)
    };

    ASSERT_NO_FATAL_FAILURE(seedOldSchemaWithRows(env.dbPathStr, rows));

    // Run the migration through the SDK's open path.
    PrnGen rng;
    std::unique_ptr<DbTable> dbTable{
        env.dbAccess.openTableWithNodes(rng, *env.fsaccess, env.dbName, 0, nullptr)};
    // If this fails, openTableWithNodes could not migrate the old schema
    // to the current one — such as a new VIRTUAL column in `newCols`
    // (SqliteDbAccess::openTableWithNodes in src/db/sqlite.cpp) referencing
    // a base column that older DBs don't have. Do NOT "fix" by adding the
    // base column to oldSchema above — oldSchema is a frozen historical
    // snapshot, the guard only works if it stays one.
    ASSERT_TRUE(dbTable) << "Migration failed — openTableWithNodes() returned null";
    EXPECT_EQ(env.dbAccess.currentDbVersion, DbAccess::DB_VERSION);
    dbTable.reset(); // release the DB handle before re-opening for assertions

    // Build a reference DB from scratch and compare column sets. This catches
    // the case where someone adds a new entry to newCols in sqlite.cpp but
    // forgets to keep this test in sync — the two sets will diverge.
    MigrationTestEnv refEnv("nodes_schema_migration_ref", env.dbName);
    std::unique_ptr<DbTable> refDbTable{
        refEnv.dbAccess.openTableWithNodes(rng, *refEnv.fsaccess, refEnv.dbName, 0, nullptr)};
    ASSERT_TRUE(refDbTable);
    refDbTable.reset();

    const auto migratedCols = readNodesColumnSet(env.dbPathStr);
    const auto freshCols = readNodesColumnSet(refEnv.dbPathStr);

    // Guard against false-green: readNodesColumnSet silently returns an empty
    // set on open / prepare failure or if the `nodes` table is absent.
    // Without this, two empty sets would trivially compare equal.
    ASSERT_FALSE(migratedCols.empty())
        << "Failed to read columns from migrated DB: " << env.dbPathStr;
    ASSERT_FALSE(freshCols.empty()) << "Failed to read columns from fresh DB: " << refEnv.dbPathStr;

    // If this fails, the CREATE TABLE DDL and the `newCols` list in
    // SqliteDbAccess::openTableWithNodes (src/db/sqlite.cpp) are out of
    // sync — such as a new column added to one but not the other. Any
    // new column must appear in both so that fresh-create and migrate
    // paths end up with identical schemas.
    EXPECT_THAT(migratedCols, ::testing::UnorderedElementsAreArray(freshCols));

    // Data verification — guards against migrate-leaves-DEFAULTs regressions.
    std::vector<MigratedRowValues> migrated;
    ASSERT_NO_FATAL_FAILURE(readAllMigratedRows(env.dbPathStr, rows, migrated));
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto& r = rows[i];
        const auto& got = migrated[i];
        // EXPECT (not ASSERT) so all column mismatches surface in one run.
        EXPECT_EQ(r.mtime, got.mtime) << "mtime mismatch, nh=" << r.nh;
        EXPECT_EQ(r.label, got.label) << "label mismatch, nh=" << r.nh;
        EXPECT_EQ(r.description, got.description) << "description mismatch, nh=" << r.nh;
        EXPECT_EQ(r.tags, got.tags) << "tags mismatch, nh=" << r.nh;
    }

    // Row count must be unchanged.
    {
        auto [dbGuard, openRc] = openSqliteRaw(env.dbPathStr);
        ASSERT_EQ(SQLITE_OK, openRc);
        sqlite3_stmt* countStmt = nullptr;
        ASSERT_EQ(SQLITE_OK,
                  sqlite3_prepare_v2(dbGuard.get(),
                                     "SELECT COUNT(*) FROM nodes",
                                     -1,
                                     &countStmt,
                                     nullptr));
        ASSERT_EQ(SQLITE_ROW, sqlite3_step(countStmt));
        EXPECT_EQ(rows.size(), static_cast<size_t>(sqlite3_column_int64(countStmt, 0)));
        sqlite3_finalize(countStmt);
    }
}

// SDK-6155: a populate-phase failure must roll back the ALTERs too.
// Forced via a BEFORE-UPDATE trigger that RAISE(ABORT)s the populate UPDATE.
TEST(Sqlite, MigrationIsAtomicOnPopulateFailure)
{
    MigrationTestEnv env("nodes_migration_atomic_test", "nodes_migration_atomic");

    const SeedRow seedRow{42, 1700000000, 0, "forced-failure row", ""};

    // Need at least one row with extractable data, else populate is a no-op
    // and the trigger never fires.
    ASSERT_NO_FATAL_FAILURE(seedOldSchemaWithRows(env.dbPathStr, {seedRow}));

    {
        // Install the trigger AFTER seeding — it's BEFORE-UPDATE only, so it
        // doesn't fire on the seed INSERT.
        auto [dbGuard, openRc] = openSqliteRaw(env.dbPathStr);
        ASSERT_EQ(SQLITE_OK, openRc);
        ASSERT_EQ(SQLITE_OK,
                  sqlite3_exec(dbGuard.get(),
                               "CREATE TRIGGER fail_update BEFORE UPDATE ON nodes "
                               "BEGIN SELECT RAISE(ABORT, 'forced abort for SDK-6155 test'); END",
                               nullptr,
                               nullptr,
                               nullptr));
    }

    // Snapshot pre-migration columns from the live DB rather than a hardcoded
    // list — that way any future migration column added in src/db/sqlite.cpp is
    // automatically covered without touching this test.
    const auto preMigrationCols = readNodesColumnSet(env.dbPathStr);
    ASSERT_FALSE(preMigrationCols.empty()) << "Failed to read pre-migration columns";

    PrnGen rng;
    std::unique_ptr<DbTable> dbTable{
        env.dbAccess.openTableWithNodes(rng, *env.fsaccess, env.dbName, 0, nullptr)};
    ASSERT_FALSE(dbTable) << "openTableWithNodes() should fail when populate aborts";

    // Atomicity: any column added by the failed migration must have been
    // rolled back. survivors = postFailureCols - preMigrationCols.
    const auto postFailureCols = readNodesColumnSet(env.dbPathStr);
    ASSERT_FALSE(postFailureCols.empty()) << "Failed to read columns from DB after rollback";

    std::vector<std::string> survivors;
    std::set_difference(postFailureCols.begin(),
                        postFailureCols.end(),
                        preMigrationCols.begin(),
                        preMigrationCols.end(),
                        std::back_inserter(survivors));

    EXPECT_TRUE(survivors.empty())
        << "columns survived rollback: " << ::testing::PrintToString(survivors)
        << " — addAndPopulateColumns() is not atomic. See SDK-6155.";

    // Pre-existing row survives — ROLLBACK only undoes the migration's own work.
    auto [dbGuard, openRc] = openSqliteRaw(env.dbPathStr);
    ASSERT_EQ(SQLITE_OK, openRc);
    {
        sqlite3_stmt* stmt = nullptr;
        ASSERT_EQ(SQLITE_OK,
                  sqlite3_prepare_v2(dbGuard.get(),
                                     "SELECT COUNT(*) FROM nodes WHERE nodehandle = ?",
                                     -1,
                                     &stmt,
                                     nullptr));
        sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(seedRow.nh));
        ASSERT_EQ(SQLITE_ROW, sqlite3_step(stmt));
        EXPECT_EQ(1, sqlite3_column_int(stmt, 0));
        sqlite3_finalize(stmt);
    }
}

// Idempotence: re-opening an already-migrated DB must leave both the schema
// and the per-row migrated data untouched. Guards against a regression in
// stripExistingColumns() where columns are wrongly treated as missing and the
// populate UPDATE re-runs over already-populated rows.
TEST(Sqlite, MigrationOpenOnAlreadyMigratedDbIsIdempotent)
{
    MigrationTestEnv env("nodes_migration_idempotent_test", "nodes_migration_idempotent");

    const std::vector<SeedRow> rows = {
        {1, 1700000001, 1, "first idempotent row", "alpha"},
        {2, 1700000002, 2, "second idempotent row", ""},
    };

    ASSERT_NO_FATAL_FAILURE(seedOldSchemaWithRows(env.dbPathStr, rows));

    // First open runs the real migration path on the old schema.
    {
        PrnGen rng;
        std::unique_ptr<DbTable> dbTable{
            env.dbAccess.openTableWithNodes(rng, *env.fsaccess, env.dbName, 0, nullptr)};
        ASSERT_TRUE(dbTable);
        EXPECT_EQ(env.dbAccess.currentDbVersion, DbAccess::DB_VERSION);
    }

    const auto colsAfterFirstOpen = readNodesColumnSet(env.dbPathStr);
    ASSERT_FALSE(colsAfterFirstOpen.empty());

    // Snapshot the migrated row values so we can diff them after the second open.
    std::vector<MigratedRowValues> snapshotAfterFirstOpen;
    ASSERT_NO_FATAL_FAILURE(readAllMigratedRows(env.dbPathStr, rows, snapshotAfterFirstOpen));

    // Second open — the asserted idempotent case.
    {
        PrnGen rng;
        std::unique_ptr<DbTable> dbTable{
            env.dbAccess.openTableWithNodes(rng, *env.fsaccess, env.dbName, 0, nullptr)};
        ASSERT_TRUE(dbTable) << "second open of an already-migrated DB must succeed";
        EXPECT_EQ(env.dbAccess.currentDbVersion, DbAccess::DB_VERSION);
    }

    const auto colsAfterSecondOpen = readNodesColumnSet(env.dbPathStr);
    EXPECT_THAT(colsAfterSecondOpen, ::testing::UnorderedElementsAreArray(colsAfterFirstOpen));

    // Per-row data must be byte-for-byte unchanged.
    std::vector<MigratedRowValues> snapshotAfterSecondOpen;
    ASSERT_NO_FATAL_FAILURE(readAllMigratedRows(env.dbPathStr, rows, snapshotAfterSecondOpen));
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto& before = snapshotAfterFirstOpen[i];
        const auto& got = snapshotAfterSecondOpen[i];
        EXPECT_EQ(before.mtime, got.mtime) << "mtime changed, nh=" << rows[i].nh;
        EXPECT_EQ(before.label, got.label) << "label changed, nh=" << rows[i].nh;
        EXPECT_EQ(before.description, got.description) << "description changed, nh=" << rows[i].nh;
        EXPECT_EQ(before.tags, got.tags) << "tags changed, nh=" << rows[i].nh;
    }
}

#endif // USE_SQLITE

// ─────────────────────────────────────────────────────────────────────────────
//  SQLite-backed node storage tests (listAllNodesByPage cursor-based pagination)
// ─────────────────────────────────────────────────────────────────────────────

#ifdef USE_SQLITE

#include "CacheKeyCombinations.h"
#include "SearchByPageTestBase.h"
#include "utils.h"

#include <mega/megaapp.h>
#include <mega/megaclient.h>
#include <mega/nodemanager.h>

namespace fs = std::filesystem;

namespace
{

using namespace mega::pagetest;

// ─────────────────────────────────────────────────────────────────────────────
//  listAllNodesByPage – cursor-based global pagination tests
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Fixture for listAllNodesByPage tests.
 *
 * Inherits the SearchByPageTest dataset (5 folders + 20 .txt files plus the
 * shared jpg / Vault / Rubbish subtrees used by the filter tests) and the
 * cursorFor() helper so cursor fields are always correctly populated.
 * Adds two helpers:
 *   referenceAll()    – single-call with no limit (ground truth for order / count)
 *   collectAllByPage() – multi-page accumulation (verifies no skips / duplicates)
 */
class ListAllNodesByPageTest: public SearchByPageTest
{
protected:
    // Pagination assertions target the 20 .txt files (MIME_TYPE_DOCUMENT);
    // jpg-subtree tests pass MIME_TYPE_PHOTO explicitly.
    static constexpr MimeType_t TEST_MIME = MIME_TYPE_DOCUMENT;

    // Returns every node matching TEST_MIME in `order` in one call (no limit, no cursor).
    std::vector<NodeHandle> referenceAll(int order) const
    {
        auto nodes = mClient->mNodeManager.listAllNodesByPage(
            makeParams(TEST_MIME, order, /*maxElements=*/0),
            CancelToken{});

        std::vector<NodeHandle> result;
        result.reserve(nodes.size());
        for (const auto& n: nodes)
            result.push_back(n->nodeHandle());
        return result;
    }

    // Convenience overload using the fixture's TEST_MIME.
    // `startCursor` defaults to nullopt (first page); pass a cursor to resume mid-sequence.
    std::vector<NodeHandle>
        collectAllByPage(int order,
                         size_t pageSize,
                         std::optional<NodeSearchCursorOffset> startCursor = std::nullopt,
                         bool excludeSensitive = false) const
    {
        return SearchByPageTest::collectAllByPage(order,
                                                  pageSize,
                                                  TEST_MIME,
                                                  startCursor,
                                                  excludeSensitive);
    }

    // ── Shared body for the deletion-between-pages tests (E1, E2). ────────────
    // Fetches page 1 of size `pageSize` under `order`, then physically removes
    // the (pageSize+1)-th node from the DB, then walks the rest using the
    // cursor anchored at the last item of page 1. Asserts the deleted node is
    // absent, every reference[pageSize+1 ..] still appears, and no page-1
    // handle reappears.
    void runDeletionBetweenPages(int order, size_t pageSize) const
    {
        SCOPED_TRACE("order=" + std::to_string(order) + " pageSize=" + std::to_string(pageSize));

        auto page1 =
            mClient->mNodeManager.listAllNodesByPage(makeParams(TEST_MIME, order, pageSize),
                                                     CancelToken{});
        ASSERT_EQ(page1.size(), pageSize);

        const auto fullReference = referenceAll(order);
        ASSERT_EQ(fullReference.size(), static_cast<size_t>(NUM_FILES));

        // Delete the (pageSize+1)-th node — the first of page 2 in offset terms.
        const NodeHandle toDelete = fullReference[pageSize];
        auto* sa = dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
        ASSERT_NE(sa, nullptr);
        ASSERT_TRUE(sa->remove(toDelete)) << "failed to delete node from DB";

        const auto page2Plus =
            collectAllByPage(order, pageSize, cursorFor(page1.back()->nodeHandle(), order));

        const std::set<NodeHandle> page2Set(page2Plus.begin(), page2Plus.end());

        EXPECT_EQ(page2Set.count(toDelete), 0u) << "deleted node appeared in page 2+";

        // Every node after the deleted one in the reference ordering must
        // appear — this is the no-skip guarantee that cursor pagination
        // provides regardless of the cursor predicate variant.
        for (size_t i = pageSize + 1; i < fullReference.size(); ++i)
        {
            EXPECT_EQ(page2Set.count(fullReference[i]), 1u)
                << "node at reference index " << i << " was skipped after deletion";
        }

        for (const auto& n: page1)
        {
            EXPECT_EQ(page2Set.count(n->nodeHandle()), 0u)
                << "page-1 handle " << n->nodeHandle() << " repeated in page 2+";
        }
    }

    // ── Shared body for the DbTable-level invalid-roots rejection tests. ──────
    // G1e (empty), G1e' (UNDEF in list), G1g (size > kListAllMaxRoots) all
    // pin the same contract: false return + empty out vector.
    void assertDbTableRejectsRoots(const std::vector<NodeHandle>& roots,
                                   const std::string& label) const
    {
        SCOPED_TRACE(label);
        auto* table = dynamic_cast<DBTableNodes*>(mClient->sctable.get());
        ASSERT_NE(table, nullptr);

        std::vector<std::pair<NodeHandle, NodeSerialized>> out;
        const bool ok = table->listAllNodesByPage(
            makeParams(MIME_TYPE_PHOTO, OrderByClause::DEFAULT_ASC, /*maxElements=*/0),
            roots,
            out,
            CancelToken{});
        EXPECT_TRUE(out.empty()) << label << " must yield no rows";
        EXPECT_FALSE(ok) << label << " must be rejected (layer contract)";
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Helper: assert handles collected by listAllNodesByPage == reference
// ─────────────────────────────────────────────────────────────────────────────

void assertListAllMatchesReference(const std::vector<NodeHandle>& paged,
                                   const std::vector<NodeHandle>& reference,
                                   const std::string& label)
{
    ASSERT_EQ(paged.size(), reference.size()) << label << ": total count mismatch";
    for (size_t i = 0; i < reference.size(); ++i)
    {
        EXPECT_EQ(paged[i], reference[i]) << label << ": handle mismatch at index " << i;
    }
    const std::set<NodeHandle> unique(paged.begin(), paged.end());
    EXPECT_EQ(unique.size(), paged.size()) << label << ": duplicate handles found";
}

// ═══════════════════════════════════════════════════════════════════════════
//  Group A – Input validation (error / guard paths)
// ═══════════════════════════════════════════════════════════════════════════

// A1. MIME_TYPE_UNKNOWN is rejected – returns empty result without crash.
//     The implementation guards against MIME_TYPE_UNKNOWN at the SQLite layer
//     and returns false; the NodeManager must surface an empty vector.
TEST_F(ListAllNodesByPageTest, UnknownMimeType_ReturnsEmpty)
{
    auto nodes = mClient->mNodeManager.listAllNodesByPage(
        makeParams(MIME_TYPE_UNKNOWN, OrderByClause::DEFAULT_ASC, 10),
        CancelToken{});

    EXPECT_TRUE(nodes.empty()) << "MIME_TYPE_UNKNOWN must return an empty result";
}

// A2. Valid MIME type with no matching nodes → empty result (no crash).
//     Exercises the zero-row result path for a valid, non-UNKNOWN type.
TEST_F(ListAllNodesByPageTest, ValidMimeTypeNoMatches_ReturnsEmpty)
{
    // Dataset contains only .txt files; MIME_TYPE_AUDIO has no matches.
    auto nodes = mClient->mNodeManager.listAllNodesByPage(
        makeParams(MIME_TYPE_AUDIO, OrderByClause::DEFAULT_ASC, 10),
        CancelToken{});

    EXPECT_TRUE(nodes.empty()) << "MIME_TYPE_AUDIO with no matching nodes must return empty";
}

// A3. Cursor/order mismatch → returns empty, no crash.
//
//     The implementation validates that the cursor contains the optional field
//     required by the chosen sort order before binding any SQL parameters.
//     When the required field is absent (cursor was built for a different order),
//     bindCursorParamsForListAll() returns false and listAllNodesByPage() returns
//     an empty vector without executing the query.
//
//     Mismatch matrix (cursor built for row, query order is column):
//       DEFAULT cursor → SIZE / MTIME / FAV / LABEL orders  (4 cases)
//       SIZE    cursor → MTIME / FAV / LABEL orders          (3 cases)
//       MTIME   cursor → SIZE / FAV / LABEL orders           (3 cases)
//       FAV     cursor → SIZE / MTIME / LABEL orders         (3 cases)
//       LABEL   cursor → SIZE / MTIME / FAV orders           (3 cases)
//     Total: 16 mismatch cases.
TEST_F(ListAllNodesByPageTest, CursorOrderMismatch_ReturnsEmpty)
{
    // Pick a stable mid-dataset handle (valid for all mMeta lookups).
    const auto reference = referenceAll(OrderByClause::DEFAULT_ASC);
    ASSERT_GE(reference.size(), 2u);
    const NodeHandle midHandle = reference[reference.size() / 2];

    struct MismatchCase
    {
        int cursorOrder; ///< order used to build the cursor (sets its optional fields)
        int queryOrder; ///< order passed to listAllNodesByPage (needs a different field)
        const char* label;
    };

    // clang-format off
    const std::vector<MismatchCase> cases = {
        // DEFAULT cursor has no optional fields → fails for any order that needs one
        {OrderByClause::DEFAULT_ASC,  OrderByClause::SIZE_ASC,   "DEFAULT→SIZE"},
        {OrderByClause::DEFAULT_ASC,  OrderByClause::MTIME_ASC,  "DEFAULT→MTIME"},
        {OrderByClause::DEFAULT_ASC,  OrderByClause::FAV_ASC,    "DEFAULT→FAV"},
        {OrderByClause::DEFAULT_ASC,  OrderByClause::LABEL_ASC,  "DEFAULT→LABEL"},
        // SIZE cursor has mLastSize but not mLastMtime / mLastFav / mLastLabel
        {OrderByClause::SIZE_ASC,     OrderByClause::MTIME_ASC,  "SIZE→MTIME"},
        {OrderByClause::SIZE_ASC,     OrderByClause::FAV_ASC,    "SIZE→FAV"},
        {OrderByClause::SIZE_ASC,     OrderByClause::LABEL_ASC,  "SIZE→LABEL"},
        // MTIME cursor has mLastMtime but not mLastSize / mLastFav / mLastLabel
        {OrderByClause::MTIME_ASC,    OrderByClause::SIZE_ASC,   "MTIME→SIZE"},
        {OrderByClause::MTIME_ASC,    OrderByClause::FAV_ASC,    "MTIME→FAV"},
        {OrderByClause::MTIME_ASC,    OrderByClause::LABEL_ASC,  "MTIME→LABEL"},
        // FAV cursor has mLastFav but not mLastSize / mLastMtime / mLastLabel
        {OrderByClause::FAV_ASC,      OrderByClause::SIZE_ASC,   "FAV→SIZE"},
        {OrderByClause::FAV_ASC,      OrderByClause::MTIME_ASC,  "FAV→MTIME"},
        {OrderByClause::FAV_ASC,      OrderByClause::LABEL_ASC,  "FAV→LABEL"},
        // LABEL cursor has mLastLabel but not mLastSize / mLastMtime / mLastFav
        {OrderByClause::LABEL_ASC,    OrderByClause::SIZE_ASC,   "LABEL→SIZE"},
        {OrderByClause::LABEL_ASC,    OrderByClause::MTIME_ASC,  "LABEL→MTIME"},
        {OrderByClause::LABEL_ASC,    OrderByClause::FAV_ASC,    "LABEL→FAV"},
    };
    // clang-format on

    for (const auto& c: cases)
    {
        auto cursor = cursorFor(midHandle, c.cursorOrder);
        auto result = mClient->mNodeManager.listAllNodesByPage(
            makeParams(TEST_MIME, c.queryOrder, 10, /*excludeSensitive=*/false, cursor),
            CancelToken{});
        EXPECT_TRUE(result.empty())
            << c.label << ": expected empty result for cursor/order mismatch";
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Group B – First-page basics (no cursor)
// ═══════════════════════════════════════════════════════════════════════════

// B1. MIME filter excludes non-file nodes – only .txt files are returned.
TEST_F(ListAllNodesByPageTest, NoCursor_DocumentMimeFilter_ReturnsAllFiles)
{
    auto nodes = mClient->mNodeManager.listAllNodesByPage(
        makeParams(TEST_MIME, OrderByClause::DEFAULT_ASC, /*maxElements=*/0),
        CancelToken{});

    // Dataset has NUM_FILES .txt files; folders/root have no MIME type so are excluded.
    EXPECT_EQ(nodes.size(), static_cast<size_t>(NUM_FILES));

    const auto unique = handlesOf(nodes);
    EXPECT_EQ(unique.size(), nodes.size()) << "duplicate handles in result";
}

// B2. pageSize larger than total – all results fit in one page.
TEST_F(ListAllNodesByPageTest, PageSizeLargerThanTotal_AllInOnePage)
{
    auto page = mClient->mNodeManager.listAllNodesByPage(
        makeParams(TEST_MIME, OrderByClause::DEFAULT_ASC, 1000),
        CancelToken{});

    EXPECT_EQ(page.size(), static_cast<size_t>(NUM_FILES));
}

// B3. maxElements == 0 is the documented "no pagination" sentinel
//     (sqlite.cpp maps it to LIMIT -1). Returns every matching row in a single
//     call. Pins the contract so it cannot silently regress to "empty page".
TEST_F(ListAllNodesByPageTest, MaxElementsZero_ReturnsAllInOnePage)
{
    auto page = mClient->mNodeManager.listAllNodesByPage(
        makeParams(TEST_MIME, OrderByClause::DEFAULT_ASC, /*maxElements=*/0),
        CancelToken{});

    EXPECT_EQ(page.size(), static_cast<size_t>(NUM_FILES));
}

// B4. Cursor past the last element with the unlimited (maxElements == 0)
//     sentinel still terminates with an empty page.
TEST_F(ListAllNodesByPageTest, CursorPastLastElement_MaxElementsZero_ReturnsEmpty)
{
    const auto reference = referenceAll(OrderByClause::DEFAULT_ASC);
    ASSERT_FALSE(reference.empty());

    auto cursor = cursorFor(reference.back(), OrderByClause::DEFAULT_ASC);
    auto page = mClient->mNodeManager.listAllNodesByPage(makeParams(TEST_MIME,
                                                                    OrderByClause::DEFAULT_ASC,
                                                                    /*maxElements=*/0,
                                                                    /*excludeSensitive=*/false,
                                                                    cursor),
                                                         CancelToken{});

    EXPECT_TRUE(page.empty()) << "Cursor past last element with maxElements=0 must still be empty";
}

// ═══════════════════════════════════════════════════════════════════════════
//  Group C – Sort order correctness
// ═══════════════════════════════════════════════════════════════════════════

// C1. FAV_DESC – non-fav nodes precede fav nodes in the result.
TEST_F(ListAllNodesByPageTest, FavDesc_AllTypes_NonFavPrecedesFav)
{
    // Dataset: fav=1 for files where i%5==0 (file_05, file_10, file_15, file_20).
    // FAV_DESC (fav ASC) → fav=0 nodes first, then fav=1 nodes.

    const auto reference = referenceAll(OrderByClause::FAV_DESC);
    ASSERT_EQ(reference.size(), static_cast<size_t>(NUM_FILES));

    bool seenFav = false;
    for (const auto& h: reference)
    {
        auto node = mClient->mNodeManager.getNodeByHandle(h);
        ASSERT_NE(node, nullptr);
        const bool isFav = node->attrs.map.count(kFavId) && node->attrs.map.at(kFavId) == "1";
        if (isFav)
            seenFav = true;
        if (seenFav)
        {
            EXPECT_TRUE(isFav) << "non-fav node after fav node in FAV_DESC result: "
                               << node->displayname();
        }
    }
}

// C2. LABEL_DESC – label values are non-increasing across the result.
TEST_F(ListAllNodesByPageTest, LabelDesc_AllTypes_LabelsAreNonIncreasing)
{
    // Dataset: label = i%4 cycling (0,1,2,3).
    // LABEL_DESC: ORDER BY label DESC → label 3 group first, then 2, 1, 0.

    const auto reference = referenceAll(OrderByClause::LABEL_DESC);
    ASSERT_EQ(reference.size(), static_cast<size_t>(NUM_FILES));

    int prevLabel = INT_MAX;
    for (const auto& h: reference)
    {
        auto node = mClient->mNodeManager.getNodeByHandle(h);
        ASSERT_NE(node, nullptr);
        const auto it = node->attrs.map.find(kLabelId);
        const int lbl = (it != node->attrs.map.end()) ? std::stoi(it->second) : 0;
        // Tie-breaking within a label group is name ASC then nodehandle ASC.
        // This check only verifies that the label never increases across the result.
        EXPECT_LE(lbl, prevLabel) << "label increased at node " << node->displayname();
        prevLabel = lbl;
    }
}

// C3. DEFAULT_DESC – name is non-increasing across the paged result.
//     Validates buildOrderByForListAll/buildCursorWhereForListAll for DEFAULT_DESC
//     without relying on referenceAll() as the oracle.
//     Dataset names are zero-padded so natural-case and lexicographic order agree.
TEST_F(ListAllNodesByPageTest, DefaultDesc_AllTypes_DefaultOrderIsNonIncreasing)
{
    const auto paged = collectAllByPage(OrderByClause::DEFAULT_DESC, 7);
    ASSERT_EQ(paged.size(), static_cast<size_t>(NUM_FILES));

    // DEFAULT_DESC: ORDER BY name DESC, nodehandle DESC.
    // All names in our dataset are unique → the sequence must be strictly decreasing.
    std::string prevName;
    for (const auto& h: paged)
    {
        const std::string& name = mMeta.at(h.as8byte()).name;
        if (!prevName.empty())
        {
            EXPECT_GE(prevName, name)
                << "name increased in DEFAULT_DESC result: prev=" << prevName << " curr=" << name;
        }
        prevName = name;
    }
}

// C4. SIZE_ASC – sizes are non-decreasing across the paged result.
//     Constrains buildOrderByForListAll for SIZE_ASC independently of referenceAll().
TEST_F(ListAllNodesByPageTest, SizeAsc_AllTypes_SizesAreNonDecreasing)
{
    const auto paged = collectAllByPage(OrderByClause::SIZE_ASC, 5);
    ASSERT_EQ(paged.size(), static_cast<size_t>(NUM_FILES));

    // Dataset: size = i*100 (i=1..20), all unique → strictly increasing.
    int64_t prevSize = INT64_MIN;
    for (const auto& h: paged)
    {
        const int64_t size = mMeta.at(h.as8byte()).size;
        EXPECT_LE(prevSize, size) << "size not non-decreasing in SIZE_ASC result";
        prevSize = size;
    }
}

// C5. MTIME_DESC – mtimes are non-increasing across the paged result.
//     Constrains buildOrderByForListAll for MTIME_DESC independently of referenceAll().
TEST_F(ListAllNodesByPageTest, MtimeDesc_AllTypes_MtimeIsNonIncreasing)
{
    const auto paged = collectAllByPage(OrderByClause::MTIME_DESC, 8);
    ASSERT_EQ(paged.size(), static_cast<size_t>(NUM_FILES));

    // Dataset: mtime = 1_700_000_000 + i (i=1..20), all unique → strictly decreasing.
    int64_t prevMtime = INT64_MAX;
    for (const auto& h: paged)
    {
        const int64_t mtime = mMeta.at(h.as8byte()).mtime;
        EXPECT_GE(prevMtime, mtime) << "mtime not non-increasing in MTIME_DESC result";
        prevMtime = mtime;
    }
}

// C6. LABEL_ASC – labelled nodes (label > 0) appear before unlabelled (label = 0),
//     and within the labelled section the label value is non-decreasing.
//     Tests the isZero ASC, label ASC primary keys without using referenceAll().
TEST_F(ListAllNodesByPageTest, LabelAsc_AllTypes_LabeledBeforeUnlabeledAndNonDecreasing)
{
    // LABEL_ASC: ORDER BY (CASE WHEN label=0 THEN 1 ELSE 0 END) ASC, label ASC, name ASC, …
    // labelled (isZero=0) block comes first in non-decreasing label order,
    // then unlabelled (isZero=1, label=0) block.
    const auto paged = collectAllByPage(OrderByClause::LABEL_ASC, 6);
    ASSERT_EQ(paged.size(), static_cast<size_t>(NUM_FILES));

    bool inUnlabeledSection = false;
    int prevNonZeroLabel = 0;

    for (const auto& h: paged)
    {
        auto node = mClient->mNodeManager.getNodeByHandle(h);
        ASSERT_NE(node, nullptr);
        const auto it = node->attrs.map.find(kLabelId);
        const int lbl = (it != node->attrs.map.end()) ? std::stoi(it->second) : 0;

        if (lbl == 0)
        {
            inUnlabeledSection = true;
        }
        else
        {
            EXPECT_FALSE(inUnlabeledSection)
                << "labeled node appeared after unlabeled section: " << node->displayname();
            EXPECT_LE(prevNonZeroLabel, lbl)
                << "label decreased within labeled section: prev=" << prevNonZeroLabel
                << " curr=" << lbl << " at " << node->displayname();
            prevNonZeroLabel = lbl;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Group D – Pagination boundary conditions
// ═══════════════════════════════════════════════════════════════════════════

// D1. pageSize equals total count → first page is full, second page is empty.
TEST_F(ListAllNodesByPageTest, PageSizeEqualsTotal_SinglePage)
{
    auto page1 = mClient->mNodeManager.listAllNodesByPage(
        makeParams(TEST_MIME, OrderByClause::DEFAULT_ASC, /*maxElements=*/NUM_FILES),
        CancelToken{});
    ASSERT_EQ(page1.size(), static_cast<size_t>(NUM_FILES));

    auto cursor = cursorFor(page1.back()->nodeHandle(), OrderByClause::DEFAULT_ASC);
    auto page2 = mClient->mNodeManager.listAllNodesByPage(makeParams(TEST_MIME,
                                                                     OrderByClause::DEFAULT_ASC,
                                                                     NUM_FILES,
                                                                     /*excludeSensitive=*/false,
                                                                     cursor),
                                                          CancelToken{});

    EXPECT_TRUE(page2.empty()) << "Page after last element must be empty";
}

// D2. pageSize=1 – advancing one node at a time produces no skips and no
//     duplicates; concatenated pages equal the single-call reference.
TEST_F(ListAllNodesByPageTest, PageSizeOne_AllTypes_NoSkipsNoDuplicates)
{
    const auto reference = referenceAll(OrderByClause::DEFAULT_ASC);
    const auto paged = collectAllByPage(OrderByClause::DEFAULT_ASC, 1);

    assertListAllMatchesReference(paged, reference, "PageSizeOne ALL DEFAULT_ASC");
}

// D3. Cursor past the last element → next page is empty.
TEST_F(ListAllNodesByPageTest, CursorPastLastElement_ReturnsEmpty)
{
    const auto reference = referenceAll(OrderByClause::DEFAULT_ASC);
    ASSERT_FALSE(reference.empty());

    auto cursor = cursorFor(reference.back(), OrderByClause::DEFAULT_ASC);
    auto page = mClient->mNodeManager.listAllNodesByPage(
        makeParams(TEST_MIME, OrderByClause::DEFAULT_ASC, 10, /*excludeSensitive=*/false, cursor),
        CancelToken{});

    EXPECT_TRUE(page.empty()) << "Page after last element must be empty";
}

// D4. Midpoint cursor – tail pages cover exactly reference[splitAt..end].
TEST_F(ListAllNodesByPageTest, MidpointCursor_TailMatchesReference)
{
    const auto reference = referenceAll(OrderByClause::DEFAULT_ASC);
    ASSERT_GE(reference.size(), 4u);

    const size_t splitAt = reference.size() / 2;
    const auto tail =
        collectAllByPage(OrderByClause::DEFAULT_ASC,
                         5,
                         cursorFor(reference[splitAt - 1], OrderByClause::DEFAULT_ASC));

    const std::vector<NodeHandle> expected(reference.begin() + static_cast<int>(splitAt),
                                           reference.end());
    ASSERT_EQ(tail.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
        EXPECT_EQ(tail[i], expected[i]) << "tail index " << i;
}

// D5. DEFAULT_ASC with MIME filter – full paged result equals single-call
//     reference, and every returned node is a FILENODE.
TEST_F(ListAllNodesByPageTest, DefaultAsc_DocumentMime_PaginationMatchesReference)
{
    const auto reference = referenceAll(OrderByClause::DEFAULT_ASC);
    const auto paged = collectAllByPage(OrderByClause::DEFAULT_ASC, 8);

    assertListAllMatchesReference(paged, reference, "DEFAULT_ASC DOCUMENT");

    // With MIME_TYPE_DOCUMENT, only file nodes are returned (folders have no MIME type).
    for (const auto& h: paged)
    {
        auto node = mClient->mNodeManager.getNodeByHandle(h);
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->type, FILENODE) << "non-file node returned with DOCUMENT mime filter";
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Group E – Stability under concurrent modification
// ═══════════════════════════════════════════════════════════════════════════

// E1. Deletion between pages – cursor-based pagination must not skip any
//     remaining nodes when a node adjacent to the page boundary is deleted
//     before the next page is fetched.
//
//     With offset-based pagination, deleting the first item of page 2 would
//     shift all subsequent items up by one, causing the old first item of page 3
//     to be skipped entirely.  Cursor-based pagination is immune to this because
//     the cursor encodes a sort-key position, not an offset.
TEST_F(ListAllNodesByPageTest, Deletion_BetweenPages_NoSkip)
{
    runDeletionBetweenPages(OrderByClause::DEFAULT_ASC, /*pageSize=*/5);
}

// E2. SizeAsc_DeletionBetweenPages_NoSkip – same guarantee as E1 but using
//     SIZE_ASC, which exercises a different cursor predicate path
//     (sizeVirtual/name/nodehandle triple) vs the DEFAULT name/handle pair.
//
//     DEFAULT_ASC passing does NOT imply SIZE_ASC is also correct because the
//     cursor WHERE clauses are generated by separate branches in
//     buildCursorWhereForListAll and bound by separate branches in
//     bindCursorParamsForListAll.
TEST_F(ListAllNodesByPageTest, SizeAsc_DeletionBetweenPages_NoSkip)
{
    runDeletionBetweenPages(OrderByClause::SIZE_ASC, /*pageSize=*/5);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Group F – Exhaustive sort-order coverage (simple MIME path)
// ═══════════════════════════════════════════════════════════════════════════

// F1. All 10 sort orders × distinct page sizes – paged result equals reference.
//     Each page size is chosen to exercise a different boundary condition
//     (non-divisible remainder, exact fit, mid-run, etc.).
TEST_F(ListAllNodesByPageTest, AllOrders_AllTypes_PaginationMatchesReference)
{
    const std::vector<OrderAndPageSize> cases = {
        {OrderByClause::DEFAULT_ASC, 7}, // 20 nodes → pages [7,7,6]
        {OrderByClause::DEFAULT_DESC, 6}, // pages [6,6,6,2]
        {OrderByClause::SIZE_ASC, 5}, // pages [5,5,5,5]
        {OrderByClause::SIZE_DESC, 9}, // pages [9,9,2]
        {OrderByClause::MTIME_ASC, 4}, // pages [4,4,4,4,4]
        {OrderByClause::MTIME_DESC, 8}, // pages [8,8,4]
        {OrderByClause::LABEL_ASC, 6}, // pages [6,6,6,2]
        {OrderByClause::LABEL_DESC, 3}, // pages [3,3,3,3,3,3,2]
        {OrderByClause::FAV_ASC, 5}, // pages [5,5,5,5]
        {OrderByClause::FAV_DESC, 7}, // pages [7,7,6]
    };

    for (const auto& [order, pageSize]: cases)
    {
        const auto reference = referenceAll(order);
        const auto paged = collectAllByPage(order, pageSize);
        assertListAllMatchesReference(paged,
                                      reference,
                                      "ALL order=" + std::to_string(order) +
                                          " pageSize=" + std::to_string(pageSize));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Group G – Filter semantics (default Cloud+Vault scope / explicit ancestor /
//            versions / sensitive)
//
//  Exercises the filters built on top of cursor-based pagination:
//    1. Default scope (explicitAncestor unset): Cloud + Vault subtrees;
//       Rubbish and inshare subtrees are always excluded.
//    2. File versions (FILENODE whose parent is a FILENODE) are always
//       excluded regardless of MIME filter.
//    3. When excludeSensitive = true, nodes whose own flags or any ancestor
//       (strictly below the matched root) carry FLAGS_IS_MARKED_SENSITIVE are
//       filtered out. The matched root's own SENS flag is intentionally ignored.
//
//  MIME_TYPE_PHOTO result matrix for the shared Cloud jpg subtree:
//    excludeSensitive=false: {clean, self_sensitive, head, under_sens}
//    excludeSensitive=true:  {clean, head}
//  Rubbish files and all versions never appear regardless of the flag.
// ═══════════════════════════════════════════════════════════════════════════

// G1a. Default scope (explicitAncestor unset): Cloud + Vault included; Rubbish
//      always excluded.
TEST_F(ListAllNodesByPageTest, DefaultScope_IncludesVault_ExcludesRubbish)
{
    for (bool exSens: {false, true})
    {
        SCOPED_TRACE(std::string("excludeSensitive=") + (exSens ? "true" : "false"));
        auto got = allMatchesAsSet(MIME_TYPE_PHOTO, OrderByClause::DEFAULT_ASC, exSens);
        EXPECT_EQ(got.count(hVaultFile), 1u)
            << "vault_file.jpg must be included under default scope";
        EXPECT_EQ(got.count(hRubbishFile), 0u)
            << "rubbish_file.jpg must not be included — Rubbish is excluded";
    }
}

// G1a'. Default scope when Vault is not provisioned (rootnodes.vault is UNDEF):
//       resolveListAllRoots must skip the UNDEF slot and fall back to Cloud-only.
//       Models real production accounts that never materialise a Vault rootnode
//       (folder-link sessions, password-manager-only accounts).
TEST_F(ListAllNodesByPageTest, DefaultScope_VaultNotProvisioned_FallsBackToCloudOnly)
{
    // Simulate "no Vault" by clearing the cached rootnode handle. The Vault
    // node still exists in the DB so the IN(?) bind can't accidentally match
    // it as ancestor — but resolveListAllRoots must not push it.
    mClient->mNodeManager.setRootNodeVault(NodeHandle());

    auto got = allMatchesAsSet(MIME_TYPE_PHOTO,
                               OrderByClause::DEFAULT_ASC,
                               /*excludeSensitive=*/false);
    EXPECT_GT(got.count(hClean), 0u) << "Cloud content must still be returned";
    EXPECT_EQ(got.count(hVaultFile), 0u)
        << "Vault content must not appear when rootnodes.vault is UNDEF";
    EXPECT_EQ(got.count(hRubbishFile), 0u)
        << "Rubbish remains excluded regardless of Vault provisioning";
}

// G1b. byLocationHandles (explicit ancestor) — Vault-only subset. Also covers
//      narrowing semantics: default scope is Cloud+Vault, so seeing hClean
//      excluded here proves explicitAncestors narrows to Vault only.
TEST_F(ListAllNodesByPageTest, ExplicitAncestor_VaultHandle_OnlyVaultNodesReturned)
{
    const auto got = handlesOf(
        mClient->mNodeManager.listAllNodesByPage(makeParams(MIME_TYPE_PHOTO,
                                                            OrderByClause::DEFAULT_ASC,
                                                            /*maxElements=*/0,
                                                            /*excludeSensitive=*/false,
                                                            /*cursor=*/std::nullopt,
                                                            /*explicitAncestors=*/{hVault}),
                                                 CancelToken{}));
    EXPECT_EQ(got.count(hVaultFile), 1u) << "vault_file.jpg must be included under hVault";
    EXPECT_EQ(got.count(hClean), 0u)
        << "cloud-drive node must not be included — explicitAncestors must narrow "
           "default Cloud+Vault scope to Vault only";
    EXPECT_EQ(got.count(hRubbishFile), 0u) << "rubbish nodes must not leak";
}

// G1c. byLocationHandles (explicit ancestor) — Cloud-Drive root subset.
TEST_F(ListAllNodesByPageTest, ExplicitAncestor_CloudHandle_OnlyCloudNodesReturned)
{
    const auto got = handlesOf(
        mClient->mNodeManager.listAllNodesByPage(makeParams(MIME_TYPE_PHOTO,
                                                            OrderByClause::DEFAULT_ASC,
                                                            /*maxElements=*/0,
                                                            /*excludeSensitive=*/false,
                                                            /*cursor=*/std::nullopt,
                                                            /*explicitAncestors=*/{hFilesRoot}),
                                                 CancelToken{}));
    EXPECT_EQ(got.count(hClean), 1u) << "clean.jpg under Cloud Drive must be included";
    EXPECT_EQ(got.count(hVaultFile), 0u) << "vault_file.jpg must not leak";
    EXPECT_EQ(got.count(hRubbishFile), 0u) << "rubbish_file.jpg must not leak";
}

// G1d. Sensitive filtering under default scope: a sensitive ancestor in the
//      Cloud subtree hides its descendants; Vault content is unaffected unless
//      separately flagged.
TEST_F(ListAllNodesByPageTest, Sensitive_DefaultScope_CloudSensAncestor_StillFiltered)
{
    const auto got =
        handlesOf(mClient->mNodeManager.listAllNodesByPage(makeParams(MIME_TYPE_PHOTO,
                                                                      OrderByClause::DEFAULT_ASC,
                                                                      /*maxElements=*/0,
                                                                      /*excludeSensitive=*/true),
                                                           CancelToken{}));
    // Under a sensitive Cloud ancestor (hSensFolder): hUnderSens filtered out.
    EXPECT_EQ(got.count(hUnderSens), 0u);
    // Cloud node without sens ancestor: included.
    EXPECT_EQ(got.count(hClean), 1u);
    // Vault file: not under any sens ancestor, must still appear.
    EXPECT_EQ(got.count(hVaultFile), 1u);
}

// G1e-h. DbTable-level tests for the dynamic IN(?,...) machinery. NodeManager
//        exposes at most 2 real slots (Cloud + Vault under default scope); the
//        tests below hit the DbTable virtual directly for full multi-root
//        coverage.
//
// Contract pinned by G1e / G1e' / G1g:
//   DbTable::listAllNodesByPage() rejects invalid root sets (empty, contains
//   UNDEF, or size > kListAllMaxRoots) with `false` and leaves `out` empty. Empty
//   `out` is the observable contract; `false` additionally pins the current
//   layer behaviour so a silent switch to true+empty (also valid externally)
//   is caught.
//
// G1f additionally pins the positive case: a valid root set (size in [1..
// kListAllMaxRoots], no UNDEF) returns ok=true with the union of all reachable
// subtrees — confirming the IN-list machinery emits every row any root reaches
// (with file versions still excluded).
//
// G1h additionally pins that a duplicate handle in the IN-list does not produce
// duplicate rows — a positive (ok=true) case validating structural dedup via
// the EXISTS up-walk.

// G1e. Empty filesRoots → rejected (guard path).
TEST_F(ListAllNodesByPageTest, DbTable_EmptyRoots_ReturnsEmpty)
{
    assertDbTableRejectsRoots(/*roots=*/{}, "empty filesRoots");
}

// G1e'. Any UNDEF in filesRoots → rejected (caller contract).
TEST_F(ListAllNodesByPageTest, DbTable_UndefInRoots_ReturnsEmpty)
{
    assertDbTableRejectsRoots({hFilesRoot, NodeHandle()}, "UNDEF in filesRoots");
}

// G1f. Three roots (Cloud + Vault + Rubbish) — result is the union of the
//      three subtrees (file versions still excluded).
TEST_F(ListAllNodesByPageTest, DbTable_ThreeRoots_UnionReturned)
{
    auto* table = dynamic_cast<DBTableNodes*>(mClient->sctable.get());
    ASSERT_NE(table, nullptr);

    std::vector<std::pair<NodeHandle, NodeSerialized>> out;
    const std::vector<NodeHandle> roots{hFilesRoot, hVault, hRubbish};
    const bool ok = table->listAllNodesByPage(
        makeParams(MIME_TYPE_PHOTO, OrderByClause::DEFAULT_ASC, /*maxElements=*/0),
        roots,
        out,
        CancelToken{});
    ASSERT_TRUE(ok);

    const auto got = handlesOf(out);
    EXPECT_EQ(got.count(hVaultFile), 1u) << "vault subtree must be included";
    EXPECT_EQ(got.count(hRubbishFile), 1u) << "rubbish subtree must be included";
    EXPECT_GT(got.count(hClean), 0u) << "cloud subtree must be included";
    // Versions still excluded regardless of root count.
    EXPECT_EQ(got.count(hVersionV1), 0u);
    EXPECT_EQ(got.count(hVersionV2), 0u);
}

// G1g. filesRoots.size() > kListAllMaxRoots(=3) → rejected. MEGA has 3 rootnodes
//      structurally; sizes beyond that indicate a caller bug. Direct DbTable
//      call because NodeManager::resolveListAllRoots tops out at 2.
TEST_F(ListAllNodesByPageTest, DbTable_TooManyRoots_ReturnsEmpty)
{
    // 4 valid roots — exceeds kListAllMaxRoots=3.
    assertDbTableRejectsRoots({hFilesRoot, hVault, hRubbish, hFilesRoot},
                              "size > kListAllMaxRoots");
}

// G1h. Same handle passed twice — result deduped (not doubled).
//      Dedup is structural: each row of `nodes` is matched at most once by the
//      EXISTS up-walk regardless of how many times a root appears in the
//      IN-list, so duplicates cannot arise — no GROUP BY required.
TEST_F(ListAllNodesByPageTest, DbTable_DuplicateRootSlot_NoDuplicates)
{
    auto* table = dynamic_cast<DBTableNodes*>(mClient->sctable.get());
    ASSERT_NE(table, nullptr);

    std::vector<std::pair<NodeHandle, NodeSerialized>> out;
    // Duplicate the Cloud root in two slots. Expected result count = same as
    // single-Cloud.
    const std::vector<NodeHandle> roots{hFilesRoot, hFilesRoot};
    const bool ok = table->listAllNodesByPage(
        makeParams(MIME_TYPE_PHOTO, OrderByClause::DEFAULT_ASC, /*maxElements=*/0),
        roots,
        out,
        CancelToken{});
    ASSERT_TRUE(ok);

    const auto got = handlesOf(out);
    EXPECT_EQ(got.size(), out.size()) << "duplicate root must not produce duplicate rows";
    EXPECT_GT(out.size(), 0u) << "fixture invariant: cloud subtree must contain ≥ 1 photo";
}

// G2. Versions (FILENODE with FILENODE parent) are always excluded.
TEST_F(ListAllNodesByPageTest, ExcludesFileVersions)
{
    // Sanity: confirm the version bit is set on the in-memory Node. If this
    // fails, the problem is in Node construction / parent linkage, not the SQL.
    auto v1 = mClient->mNodeManager.getNodeByHandle(hVersionV1);
    ASSERT_NE(v1, nullptr);
    ASSERT_NE(v1->parent, nullptr) << "version node must have its parent pointer set";
    EXPECT_EQ(v1->parent->type, FILENODE)
        << "version's parent must be a FILENODE so FLAGS_IS_VERSION is computed true";
    EXPECT_TRUE(v1->getDBFlagsBitset().test(Node::FLAGS_IS_VERSION))
        << "FLAGS_IS_VERSION should be set for a FILENODE child of a FILENODE";

    for (bool exSens: {false, true})
    {
        SCOPED_TRACE(std::string("excludeSensitive=") + (exSens ? "true" : "false"));
        auto got = allMatchesAsSet(MIME_TYPE_PHOTO, OrderByClause::DEFAULT_ASC, exSens);
        EXPECT_EQ(got.count(hVersionV1), 0u) << "version_v1.jpg returned";
        EXPECT_EQ(got.count(hVersionV2), 0u) << "version_v2.jpg returned";
        EXPECT_EQ(got.count(hHead), 1u) << "HEAD must always be included";
    }
}

// G3. excludeSensitive: node whose own SENS bit is set is excluded.
TEST_F(ListAllNodesByPageTest, Sensitive_NodeDirectlyMarked)
{
    // self_sensitive.jpg: own SENS bit set.
    EXPECT_EQ(allMatchesAsSet(MIME_TYPE_PHOTO,
                              OrderByClause::DEFAULT_ASC,
                              /*excludeSensitive=*/false)
                  .count(hSelfSensitive),
              1u);
    EXPECT_EQ(allMatchesAsSet(MIME_TYPE_PHOTO,
                              OrderByClause::DEFAULT_ASC,
                              /*excludeSensitive=*/true)
                  .count(hSelfSensitive),
              0u);
}

// G4. excludeSensitive: node whose ancestor has SENS bit is excluded.
TEST_F(ListAllNodesByPageTest, Sensitive_AncestorMarked)
{
    // under_sens.jpg: own flag is clean but sens_folder (parent) has SENS bit.
    EXPECT_EQ(allMatchesAsSet(MIME_TYPE_PHOTO,
                              OrderByClause::DEFAULT_ASC,
                              /*excludeSensitive=*/false)
                  .count(hUnderSens),
              1u);
    EXPECT_EQ(allMatchesAsSet(MIME_TYPE_PHOTO,
                              OrderByClause::DEFAULT_ASC,
                              /*excludeSensitive=*/true)
                  .count(hUnderSens),
              0u)
        << "sensitive ancestor must propagate to descendant when filtering";
}

// G5. excludeSensitive: Cloud Drive root's own SENS bit is intentionally ignored.
TEST_F(ListAllNodesByPageTest, Sensitive_FilesRootMarked_DescendantsStillReturned)
{
    // Mark the Cloud Drive root itself as sensitive. The up-walk CTE deliberately
    // stops BEFORE inspecting filesRoot, so descendants must remain visible.
    auto root = mClient->mNodeManager.getNodeByHandle(hFilesRoot);
    ASSERT_NE(root, nullptr);
    root->attrs.map[AttrMap::string2nameid("sen")] = "1";
    mClient->mNodeManager.saveNodeInDb(root.get());

    // Sanity: the attr-map poke + saveNodeInDb must land the SENS bit in the
    // `flags` column the EXISTS walk reads. If this fails, the test below
    // would pass for the wrong reason (no SENS anywhere, not root-ignored).
    ASSERT_TRUE(root->getDBFlagsBitset().test(Node::FLAGS_IS_MARKED_SENSITIVE))
        << "SENS bit did not propagate from attrs.map to flags after saveNodeInDb";

    auto got = allMatchesAsSet(MIME_TYPE_PHOTO,
                               OrderByClause::DEFAULT_ASC,
                               /*excludeSensitive=*/true);
    // clean.jpg and head.jpg are both strictly-non-sensitive and should still
    // appear even though the root above them now carries the SENS bit.
    EXPECT_EQ(got.count(hClean), 1u) << "clean.jpg should remain despite root SENS bit";
    EXPECT_EQ(got.count(hHead), 1u) << "head.jpg should remain despite root SENS bit";
}

// G6. excludeSensitive: filtered set is a strict subset of the unfiltered set.
TEST_F(ListAllNodesByPageTest, Sensitive_ResultIsSubsetWhenEnabled)
{
    auto all = allMatchesAsSet(MIME_TYPE_PHOTO,
                               OrderByClause::DEFAULT_ASC,
                               /*excludeSensitive=*/false);
    auto filtered = allMatchesAsSet(MIME_TYPE_PHOTO,
                                    OrderByClause::DEFAULT_ASC,
                                    /*excludeSensitive=*/true);

    // Strict subset: everything `filtered` returned must be in `all`, and the two
    // sets must not be equal (our dataset contains at least one sensitive hit).
    for (const auto& h: filtered)
        EXPECT_EQ(all.count(h), 1u) << "filtered leaked a handle not in unfiltered set";
    EXPECT_LT(filtered.size(), all.size())
        << "dataset must contain at least one sensitive-excluded node";
}

// ─────────────────────────────────────────────────────────────────────────────
//  TieBreakTest
//  Minimal fixture whose entire dataset shares the same (size, mtime, label, name).
//  Only the nodehandle differs between nodes, so every sort order that reaches the
//  nodehandle tiebreaker is exercised in its pure form.
//
//  Dataset: ROOT + 5 file nodes, all "tied.txt" (MIME_TYPE_DOCUMENT)
//    size  = 500        (constant → SIZE sorts tie after primary key)
//    mtime = 1'900'000'000  (constant → MTIME sorts tie)
//    label = 2          (nonzero → LABEL_ASC isZero=0; all in same label bucket)
//    fav   = 0
//  Handles are assigned sequentially during insertion:
//    mTiedHandles[0] has the smallest handle, mTiedHandles[4] the largest.
// ─────────────────────────────────────────────────────────────────────────────

class TieBreakTest: public SearchByPageTest
{
protected:
    static constexpr MimeType_t TEST_MIME = MIME_TYPE_DOCUMENT;
    static constexpr int64_t kTiedSize = 500;
    static constexpr int64_t kTiedMtime = 1'900'000'000LL;
    static constexpr int kTiedLabel = 2;
    static constexpr int kNumTied = 5;

    std::vector<NodeHandle> mTiedHandles; ///< insertion order = ascending handle order

    void populateDB() override
    {
        auto root = addNode(ROOTNODE, nullptr, NodeMeta{"ROOT", ROOTNODE, 0, 0, 0, 0});
        mRootHandle = root->nodeHandle();

        for (int i = 0; i < kNumTied; ++i)
        {
            NodeMeta m;
            m.name = "tied.txt";
            m.size = kTiedSize;
            m.mtime = kTiedMtime;
            m.label = kTiedLabel;
            m.fav = 0;
            auto node = addNode(FILENODE, root, m);
            mTiedHandles.push_back(node->nodeHandle());
        }
        // mTiedHandles[0].as8byte() < … < mTiedHandles[4].as8byte()
    }

    std::vector<NodeHandle> collectAllByPage(int order, size_t pageSize) const
    {
        return SearchByPageTest::collectAllByPage(order, pageSize, TEST_MIME);
    }

    // Shared assertions for the H-group tiebreaker tests. All tests:
    //   - paginate with pageSize=2 to split inside the tied group,
    //   - check no duplicate handles,
    //   - check the nodehandle tiebreaker advances strictly in the requested
    //     direction, and
    //   - check the full result equals the expected handle sequence
    //     (mTiedHandles for ascending, reversed for descending).
    void assertTiedHandleOrder(const std::vector<NodeHandle>& paged,
                               bool ascending,
                               const std::string& label) const
    {
        SCOPED_TRACE(label);
        ASSERT_EQ(paged.size(), static_cast<size_t>(kNumTied));

        const std::set<NodeHandle> unique(paged.begin(), paged.end());
        ASSERT_EQ(unique.size(), paged.size())
            << "duplicate handles in " << label << " tied result";

        for (size_t i = 1; i < paged.size(); ++i)
        {
            if (ascending)
            {
                EXPECT_GT(paged[i].as8byte(), paged[i - 1].as8byte())
                    << "nodehandle did not advance (ASC) in " << label << " tied sequence at index "
                    << i;
            }
            else
            {
                EXPECT_LT(paged[i].as8byte(), paged[i - 1].as8byte())
                    << "nodehandle did not decrease (DESC) in " << label
                    << " tied sequence at index " << i;
            }
        }

        const std::vector<NodeHandle> expected =
            ascending ? mTiedHandles :
                        std::vector<NodeHandle>(mTiedHandles.rbegin(), mTiedHandles.rend());
        EXPECT_EQ(paged, expected) << label << " tied result does not match expected order";
    }
};

// ═══════════════════════════════════════════════════════════════════════════
//  Group H – Nodehandle tiebreaker: split at page boundary inside a tied group
//
//  Assertions for each test:
//    1. No duplicate handles across the full result.
//    2. The relative handle order across every consecutive pair is consistent
//       with the sort direction (ASC → strictly increasing, DESC → strictly
//       decreasing). This proves the cursor correctly resumed inside the tied
//       group rather than repeating or skipping entries.
//    3. The full result equals the expected handle sequence.
// ═══════════════════════════════════════════════════════════════════════════

// H1. LABEL_ASC with tied (isZero, label, name) – nodehandle ASC breaks the tie.
//     Exercises buildCursorWhereForListAll (LABEL_ASC, sqlite.cpp:2686-2698):
//       isZero = p1 AND label = p2 AND name = p3 AND nodehandle > p4
//     LABEL_ASC reduces to nodehandle ASC → expected: mTiedHandles[0..4].
TEST_F(TieBreakTest, LabelAsc_TiedLabelAndName_UsesNodehandleTieBreak)
{
    assertTiedHandleOrder(collectAllByPage(OrderByClause::LABEL_ASC, 2),
                          /*ascending=*/true,
                          "LABEL_ASC");
}

// H2. SIZE_DESC with tied (size, name) – nodehandle DESC breaks the tie.
//     Exercises buildCursorWhereForListAll (SIZE_DESC, sqlite.cpp:2646-2652):
//       sizeVirtual = p1 AND name = p2 AND nodehandle < p3
//     SIZE_DESC: (size DESC, name DESC, nodehandle DESC) → reversed handles.
TEST_F(TieBreakTest, SizeDesc_TiedSizeAndName_UsesNodehandleTieBreak)
{
    assertTiedHandleOrder(collectAllByPage(OrderByClause::SIZE_DESC, 2),
                          /*ascending=*/false,
                          "SIZE_DESC");
}

// H3. MTIME_ASC with tied (mtime, name) – nodehandle ASC breaks the tie.
//     Exercises buildCursorWhereForListAll (MTIME_ASC, sqlite.cpp:2654-2660):
//       mtime = p1 AND name = p2 AND nodehandle > p3
//     MTIME_ASC: (mtime ASC, name ASC, nodehandle ASC) → mTiedHandles[0..4].
TEST_F(TieBreakTest, MtimeAsc_TiedMtimeAndName_UsesNodehandleTieBreak)
{
    assertTiedHandleOrder(collectAllByPage(OrderByClause::MTIME_ASC, 2),
                          /*ascending=*/true,
                          "MTIME_ASC");
}

// H4. FAV_DESC with tied (fav, name) – nodehandle ASC breaks the tie.
//     Exercises buildCursorWhereForListAll (FAV_DESC, sqlite.cpp:2678-2684):
//       fav = p1 AND name = p2 AND nodehandle > p3
//
//     This is the "reverse" counterpart to H2 (SIZE_DESC): although both sort
//     orders carry "DESC" in their name, their tiebreaker directions differ.
//     SIZE_DESC uses  nodehandle < p3  (descending),
//     FAV_DESC  uses  nodehandle > p3  (ascending, because fav/name are both ASC
//     in the underlying ORDER BY clause). A bug that swapped > / < for one of
//     them would not be caught by the other test — that's why H4 stays separate
//     from H2 even though both are "DESC" orderings.
TEST_F(TieBreakTest, FavDesc_TiedFavAndName_UsesNodehandleAscTieBreak)
{
    assertTiedHandleOrder(collectAllByPage(OrderByClause::FAV_DESC, 2),
                          /*ascending=*/true,
                          "FAV_DESC");
}

// H5. DEFAULT_ASC with tied name – nodehandle ASC breaks the tie.
//     Exercises buildCursorWhereForListAll (DEFAULT_ASC):
//       (name > p1) OR (name = p1 AND nodehandle > p2)
//     With every name == "tied.txt", reduces to pure nodehandle ASC.
//     Distinct from H1/H3 because the DEFAULT cursor branch carries no
//     leading sort-key field (no isZero/label/mtime/fav predicate term),
//     so a regression in that branch is invisible to the other H tests.
TEST_F(TieBreakTest, DefaultAsc_TiedName_UsesNodehandleTieBreak)
{
    assertTiedHandleOrder(collectAllByPage(OrderByClause::DEFAULT_ASC, 2),
                          /*ascending=*/true,
                          "DEFAULT_ASC");
}

// H6. DEFAULT_DESC with tied name – nodehandle DESC breaks the tie.
//     Exercises buildCursorWhereForListAll (DEFAULT_DESC):
//       (name < p1) OR (name = p1 AND nodehandle < p2)
//     A sign-flip in the DEFAULT_DESC branch (e.g. '>' instead of '<')
//     would not be caught by H2 (SIZE_DESC) because the SIZE cursor uses
//     a different SQL fragment with the sizeVirtual predicate term.
TEST_F(TieBreakTest, DefaultDesc_TiedName_UsesNodehandleDescTieBreak)
{
    assertTiedHandleOrder(collectAllByPage(OrderByClause::DEFAULT_DESC, 2),
                          /*ascending=*/false,
                          "DEFAULT_DESC");
}

// ─────────────────────────────────────────────────────────────────────────────
//  GroupedListAllNodesByPageTest
//  Tests the grouped (UNION ALL CTE) code path used for MIME_TYPE_ALL_DOCS and
//  MIME_TYPE_ALL_VISUAL_MEDIA, plus the simple path for their constituent types
//  (MIME_TYPE_VIDEO, MIME_TYPE_PHOTO) individually.
//
//  Additional dataset (appended on top of the base 20 .txt files):
//    ALL_DOCS members:
//      report_alpha.pdf, notes_delta.pdf  → MIME_TYPE_PDF
//      slides_beta.pptx                  → MIME_TYPE_PRESENTATION
//      sheet_gamma.xlsx                  → MIME_TYPE_SPREADSHEET
//    ALL_VISUAL_MEDIA members:
//      photo_alpha.jpg, photo_beta.jpg, photo_gamma.jpg  → MIME_TYPE_PHOTO
//      video_alpha.mp4, video_beta.mp4, video_gamma.mp4  → MIME_TYPE_VIDEO
// ─────────────────────────────────────────────────────────────────────────────

class GroupedListAllNodesByPageTest: public SearchByPageTest
{
protected:
    // Sensitive nodes used by grouped-path excludeSensitive TEST_Fs. A .mp4
    // covers the VIDEO route of MIME_TYPE_ALL_VISUAL_MEDIA; a .pdf covers the
    // PDF route of MIME_TYPE_ALL_DOCS. The base fixture already contributes a
    // sensitive .jpg (self_sensitive.jpg) for the PHOTO route.
    NodeHandle hVideoSensitive;
    NodeHandle hReportSensitive;

    // Dataset cardinality under the default Cloud+Vault scope (versions excluded,
    // Rubbish excluded). Used by the ASSERT_EQ guard in C1/C2/D1 so a dataset
    // tweak updates one place.
    //
    // PHOTO: Cloud subtree contributes {clean, self_sens, head, under_sens} = 4;
    //        Vault subtree contributes {vault_file} = 1; total filter-subtree = 5.
    //        VIDEO has no entries in the filter subtree.
    static constexpr size_t kBaseFilterSubtreePhotos = 5;
    static constexpr size_t kGroupedFixturePhotos = 3; // photo_alpha/beta/gamma
    static constexpr size_t kGroupedFixtureVideos = 3; // video_alpha/beta/gamma
    static constexpr size_t kGroupedSensitiveVideos = 1; // video_sensitive
    static constexpr size_t kPhotoTotal = kGroupedFixturePhotos + kBaseFilterSubtreePhotos;
    static constexpr size_t kVideoTotal = kGroupedFixtureVideos + kGroupedSensitiveVideos;
    static constexpr size_t kVisualMediaTotal = kPhotoTotal + kVideoTotal;

    void SetUp() override
    {
        SearchByPageTest::SetUp();

        auto root = mClient->mNodeManager.getNodeByHandle(mRootHandle);
        ASSERT_NE(root, nullptr);

        const int64_t baseMtime = 1'800'000'000LL;

        addNode(FILENODE, root, NodeMeta{"report_alpha.pdf", FILENODE, 410, baseMtime + 1, 1, 0});
        addNode(FILENODE, root, NodeMeta{"slides_beta.pptx", FILENODE, 240, baseMtime + 2, 2, 1});
        addNode(FILENODE, root, NodeMeta{"sheet_gamma.xlsx", FILENODE, 510, baseMtime + 3, 3, 0});
        addNode(FILENODE, root, NodeMeta{"notes_delta.pdf", FILENODE, 180, baseMtime + 4, 0, 1});

        addNode(FILENODE, root, NodeMeta{"photo_alpha.jpg", FILENODE, 90, baseMtime + 11, 1, 0});
        addNode(FILENODE, root, NodeMeta{"photo_beta.jpg", FILENODE, 120, baseMtime + 12, 0, 1});
        addNode(FILENODE, root, NodeMeta{"photo_gamma.jpg", FILENODE, 80, baseMtime + 13, 2, 0});
        addNode(FILENODE, root, NodeMeta{"video_alpha.mp4", FILENODE, 900, baseMtime + 21, 0, 1});
        addNode(FILENODE, root, NodeMeta{"video_beta.mp4", FILENODE, 760, baseMtime + 22, 3, 0});
        addNode(FILENODE, root, NodeMeta{"video_gamma.mp4", FILENODE, 830, baseMtime + 23, 1, 1});

        // Sensitive-flagged FILENODEs placed directly under the Cloud Drive root
        // (not inside sens_folder) so that only the node's own SENS bit matters.
        NodeMeta sensPdf{"report_sensitive.pdf", FILENODE, 300, baseMtime + 5, 0, 0};
        sensPdf.sensitive = true;
        hReportSensitive = addNode(FILENODE, root, sensPdf, /*isFetching=*/false)->nodeHandle();

        NodeMeta sensVideo{"video_sensitive.mp4", FILENODE, 800, baseMtime + 24, 0, 0};
        sensVideo.sensitive = true;
        hVideoSensitive = addNode(FILENODE, root, sensVideo, /*isFetching=*/false)->nodeHandle();
    }

    // Ground-truth via searchNodes() (single call, no cursor). When
    // @p excludeSensitive is true, the filter switches NodeSearchFilter's
    // bySensitivity to BoolFilter::onlyTrue, which the searchNodes SQL
    // interprets as "exclude sensitive nodes" (see recent_actions.cpp:400,
    // isValidSensitivity in nodemanager.cpp:102, and nodesCTE up-walk check
    // at sqlite.cpp:2405 — the BoolFilter enum names are counter-intuitive:
    // onlyTrue = drop sensitive rows, onlyFalse = keep only sensitive rows).
    std::vector<NodeHandle> referenceBySearch(int order,
                                              MimeType_t mimeType,
                                              bool excludeSensitive = false) const
    {
        NodeSearchFilter filter;
        // Match listAllNodesByPage's default scope: Cloud + Vault.
        filter.byAncestors({mRootHandle.as8byte(), hVault.as8byte(), UNDEF});
        filter.byNodeType(FILENODE);
        filter.byCategory(mimeType);
        if (excludeSensitive)
            filter.bySensitivity(NodeSearchFilter::BoolFilter::onlyTrue);

        auto nodes =
            mClient->mNodeManager.searchNodes(filter, order, CancelToken{}, NodeSearchPage{0, 0});

        std::vector<NodeHandle> result;
        result.reserve(nodes.size());
        for (const auto& n: nodes)
            result.push_back(n->nodeHandle());
        return result;
    }

    // Sweep all 10 sort orders for @p mime, asserting the paginated walk equals
    // the searchNodes reference and that the reference contains @p expectedCount
    // entries (a fixture-cardinality guard — picks up dataset drift before the
    // ordered comparison fires). Used by the C1/C2 / B1 sweep tests.
    void assertAllOrdersPaginationMatchesSearch(MimeType_t mime,
                                                size_t expectedCount,
                                                const std::vector<OrderAndPageSize>& cases,
                                                const std::string& mimeLabel)
    {
        for (const auto& [order, pageSize]: cases)
        {
            const auto reference = referenceBySearch(order, mime);
            const auto paged = collectAllByPage(order, pageSize, mime);
            ASSERT_EQ(reference.size(), expectedCount)
                << "unexpected " << mimeLabel << " count for order=" << order;
            assertListAllMatchesReference(paged,
                                          reference,
                                          mimeLabel + " order=" + std::to_string(order) +
                                              " pageSize=" + std::to_string(pageSize));
        }
    }
};

// Default per-order page-size grid for the simple-path sweep tests
// (C1: VIDEO, C2: PHOTO). Picked so DEFAULT/MTIME/FAV/LABEL each split the
// 4-/8-node datasets at a non-divisible boundary.
inline const std::vector<OrderAndPageSize> kSimplePathOrderCases = {
    {OrderByClause::DEFAULT_ASC, 2},
    {OrderByClause::DEFAULT_DESC, 3},
    {OrderByClause::SIZE_ASC, 2},
    {OrderByClause::SIZE_DESC, 3},
    {OrderByClause::MTIME_ASC, 2},
    {OrderByClause::MTIME_DESC, 3},
    {OrderByClause::LABEL_ASC, 2},
    {OrderByClause::LABEL_DESC, 3},
    {OrderByClause::FAV_ASC, 2},
    {OrderByClause::FAV_DESC, 3},
};

// ═══════════════════════════════════════════════════════════════════════════
//  Group A – MIME_TYPE_ALL_DOCS (grouped path: UNION ALL CTE over
//            DOCUMENT + PDF + PRESENTATION + SPREADSHEET routes)
//  Dataset: 20 .txt + 3 .pdf (1 sensitive) + 1 .pptx + 1 .xlsx = 25 nodes
//  (A1 uses excludeSensitive=false, so reference and paged both include
//   report_sensitive.pdf. See Group E for the excludeSensitive=true path.)
// ═══════════════════════════════════════════════════════════════════════════

// A1. All 10 sort orders – paged result equals searchNodes reference for each.
TEST_F(GroupedListAllNodesByPageTest, AllOrders_AllDocs_PaginationMatchesSearchReference)
{
    const std::vector<OrderAndPageSize> cases = {
        {OrderByClause::DEFAULT_ASC, 7},
        {OrderByClause::DEFAULT_DESC, 6},
        {OrderByClause::SIZE_ASC, 5},
        {OrderByClause::SIZE_DESC, 9},
        {OrderByClause::MTIME_ASC, 4},
        {OrderByClause::MTIME_DESC, 8},
        {OrderByClause::LABEL_ASC, 6},
        {OrderByClause::LABEL_DESC, 3},
        {OrderByClause::FAV_ASC, 5},
        {OrderByClause::FAV_DESC, 7},
    };

    for (const auto& [order, pageSize]: cases)
    {
        const auto reference = referenceBySearch(order, MIME_TYPE_ALL_DOCS);
        const auto paged = collectAllByPage(order, pageSize, MIME_TYPE_ALL_DOCS);
        assertListAllMatchesReference(paged,
                                      reference,
                                      "ALL_DOCS order=" + std::to_string(order) +
                                          " pageSize=" + std::to_string(pageSize));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Group B – MIME_TYPE_ALL_VISUAL_MEDIA (grouped path: UNION ALL CTE over
//            PHOTO + VIDEO routes)
//  Dataset: 8 .jpg + 4 .mp4 = kVisualMediaTotal (12) Cloud+Vault-scope nodes
//  (4 jpg from SearchByPageTest's Cloud filter subtree + 1 vault_file jpg +
//   3 photo_* jpg + 3 video_* mp4 + 1 sensitive mp4. Versions of head.jpg and
//   rubbish_file.jpg are excluded by the listAllNodesByPage filter chain.)
// ═══════════════════════════════════════════════════════════════════════════

// B1. All 10 sort orders – paged result equals searchNodes reference for each.
TEST_F(GroupedListAllNodesByPageTest, AllOrders_AllVisualMedia_PaginationMatchesSearchReference)
{
    const std::vector<OrderAndPageSize> cases = {
        {OrderByClause::DEFAULT_ASC, 3},
        {OrderByClause::DEFAULT_DESC, 2},
        {OrderByClause::SIZE_ASC, 3},
        {OrderByClause::SIZE_DESC, 2},
        {OrderByClause::MTIME_ASC, 3},
        {OrderByClause::MTIME_DESC, 2},
        {OrderByClause::LABEL_ASC, 3},
        {OrderByClause::LABEL_DESC, 2},
        {OrderByClause::FAV_ASC, 3},
        {OrderByClause::FAV_DESC, 2},
    };

    for (const auto& [order, pageSize]: cases)
    {
        const auto reference = referenceBySearch(order, MIME_TYPE_ALL_VISUAL_MEDIA);
        const auto paged = collectAllByPage(order, pageSize, MIME_TYPE_ALL_VISUAL_MEDIA);
        assertListAllMatchesReference(paged,
                                      reference,
                                      "ALL_VISUAL_MEDIA order=" + std::to_string(order) +
                                          " pageSize=" + std::to_string(pageSize));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Group C – Simple path for individual MIME types that belong to groups
//  Verifies that the non-grouped SQL path (mimetypeVirtual = ?) is exercised
//  correctly for MIME_TYPE_VIDEO and MIME_TYPE_PHOTO, which also appear as
//  constituent members inside the ALL_VISUAL_MEDIA grouped path.
// ═══════════════════════════════════════════════════════════════════════════

// C1. MIME_TYPE_VIDEO individually – all 10 sort orders.
//     Video count = 3 non-sensitive .mp4 (video_alpha/beta/gamma) + 1 sensitive
//     .mp4 (video_sensitive, used by grouped excludeSensitive tests) = 4 nodes.
TEST_F(GroupedListAllNodesByPageTest, AllOrders_Video_SimplePathPaginationMatchesSearchReference)
{
    assertAllOrdersPaginationMatchesSearch(MIME_TYPE_VIDEO,
                                           kVideoTotal,
                                           kSimplePathOrderCases,
                                           "VIDEO");
}

// C2. MIME_TYPE_PHOTO individually – all 10 sort orders.
//     Photo count under the default Cloud+Vault scope =
//         3 photo_*.jpg (this fixture) + 4 jpg from SearchByPageTest's Cloud
//         subtree (clean, self_sensitive, head, under_sens) + 1 jpg in Vault
//         (vault_file) = 8 nodes.
//     Versions of head.jpg are always excluded; rubbish_file is filtered out
//     because Rubbish is not part of the default scope.
TEST_F(GroupedListAllNodesByPageTest, AllOrders_Photo_SimplePathPaginationMatchesSearchReference)
{
    assertAllOrdersPaginationMatchesSearch(MIME_TYPE_PHOTO,
                                           kPhotoTotal,
                                           kSimplePathOrderCases,
                                           "PHOTO");
}

// ═══════════════════════════════════════════════════════════════════════════
//  Group D – Stability under concurrent modification (grouped MIME path)
// ═══════════════════════════════════════════════════════════════════════════

// D1. AllVisualMedia_DefaultDesc_DeletionBetweenPages_NoSkip
//     Mirrors ListAllNodesByPageTest::Deletion_BetweenPages_NoSkip (E1) but uses
//     the grouped MIME path (MIME_TYPE_ALL_VISUAL_MEDIA → UNION ALL CTE over
//     PHOTO + VIDEO, sqlite.cpp:2878).  A delete between pages must not cause
//     cursor-based pagination to skip any remaining visual-media nodes.
//
//     Dataset under default Cloud+Vault scope: 8 .jpg (3 photo_* + 4 from the
//     Cloud filter subtree + 1 vault_file) + 3 non-sensitive .mp4 + 1 sensitive
//     .mp4 = 12 visual-media nodes. excludeSensitive=false keeps them all.
//     pageSize=3, delete the node at reference[3] (first of page 2), then
//     verify no remaining node is skipped and page-1 entries aren't repeated.
TEST_F(GroupedListAllNodesByPageTest, AllVisualMedia_DefaultDesc_DeletionBetweenPages_NoSkip)
{
    constexpr MimeType_t mime = MIME_TYPE_ALL_VISUAL_MEDIA;
    constexpr int order = OrderByClause::DEFAULT_DESC;
    constexpr size_t pageSize = 3;

    const auto fullReference = referenceBySearch(order, mime);
    ASSERT_EQ(fullReference.size(), kVisualMediaTotal);

    auto page1 =
        mClient->mNodeManager.listAllNodesByPage(makeParams(mime, order, pageSize), CancelToken{});
    ASSERT_EQ(page1.size(), pageSize);

    // Delete the first node of what would be page 2 in offset terms.
    const NodeHandle toDelete = fullReference[pageSize];
    auto* sa = dynamic_cast<SqliteAccountState*>(mClient->sctable.get());
    ASSERT_NE(sa, nullptr);
    ASSERT_TRUE(sa->remove(toDelete)) << "failed to delete node from DB";

    const auto page2Plus =
        collectAllByPage(order, pageSize, mime, cursorFor(page1.back()->nodeHandle(), order));

    const std::set<NodeHandle> page2Set(page2Plus.begin(), page2Plus.end());

    EXPECT_EQ(page2Set.count(toDelete), 0u) << "deleted node appeared in page 2+";

    // Every node after the deleted one in the reference ordering must appear.
    for (size_t i = pageSize + 1; i < fullReference.size(); ++i)
    {
        EXPECT_EQ(page2Set.count(fullReference[i]), 1u)
            << "visual-media node at reference index " << i << " was skipped after deletion";
    }

    for (const auto& n: page1)
    {
        EXPECT_EQ(page2Set.count(n->nodeHandle()), 0u) << "page-1 handle repeated in page 2+";
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Group E – excludeSensitive=true on grouped routes (UNION ALL CTE)
//
//  The simple-path Sensitive_* TEST_Fs (fixture ListAllNodesByPageTest) only
//  exercise one route (PHOTO). buildGroupedListAllQuery stitches the
//  buildUpWalkExists predicate into every constituent SELECT, so a bug in a
//  single route's parameter binding would pass the PHOTO-only coverage. The
//  tests below push the sensitive predicate through both PHOTO+VIDEO routes
//  of ALL_VISUAL_MEDIA and through the PDF route of ALL_DOCS.
// ═══════════════════════════════════════════════════════════════════════════

// E1. ALL_VISUAL_MEDIA with excludeSensitive=true must drop the sensitive photo
//     AND the sensitive video — one per UNION route — while keeping every
//     non-sensitive entry.
TEST_F(GroupedListAllNodesByPageTest, AllVisualMedia_ExcludeSensitive_FiltersBothPhotoAndVideo)
{
    const auto all = allMatchesAsSet(MIME_TYPE_ALL_VISUAL_MEDIA,
                                     OrderByClause::DEFAULT_ASC,
                                     /*excludeSensitive=*/false);
    const auto filtered = allMatchesAsSet(MIME_TYPE_ALL_VISUAL_MEDIA,
                                          OrderByClause::DEFAULT_ASC,
                                          /*excludeSensitive=*/true);

    // Photo route: self_sensitive.jpg + under_sens.jpg (ancestor) removed.
    EXPECT_EQ(filtered.count(hSelfSensitive), 0u) << "self_sensitive.jpg leaked";
    EXPECT_EQ(filtered.count(hUnderSens), 0u) << "under_sens.jpg leaked";
    // Video route: video_sensitive.mp4 removed by the grouped EXISTS predicate.
    EXPECT_EQ(filtered.count(hVideoSensitive), 0u)
        << "video_sensitive.mp4 leaked — grouped path may be missing the "
           "sensitivity EXISTS on the VIDEO route";

    // Strict subset with a strictly smaller size: dataset carries ≥ 3 sensitive-
    // excluded entries, so filtered.size() must be less than all.size().
    for (const auto& h: filtered)
        EXPECT_EQ(all.count(h), 1u) << "filtered leaked a handle not in unfiltered set";
    EXPECT_LT(filtered.size(), all.size())
        << "ALL_VISUAL_MEDIA excludeSensitive=true did not remove any node";
}

// E2. Paginated ALL_VISUAL_MEDIA + excludeSensitive=true must equal the
//     searchNodes reference that applies bySensitivity(onlyTrue) — onlyTrue
//     drops sensitive rows in the search filter (counter-intuitive name; see
//     referenceBySearch comment). Cross-checks that the grouped UNION-ALL path
//     produces the same ordered sequence as the canonical searchNodes walker
//     when sensitivity filtering is on.
TEST_F(GroupedListAllNodesByPageTest, AllVisualMedia_ExcludeSensitive_MatchesSearchReference)
{
    constexpr MimeType_t mime = MIME_TYPE_ALL_VISUAL_MEDIA;
    const std::vector<OrderAndPageSize> cases = {
        {OrderByClause::DEFAULT_ASC, 3},
        {OrderByClause::SIZE_DESC, 4},
        {OrderByClause::MTIME_ASC, 2},
    };

    for (const auto& [order, pageSize]: cases)
    {
        const auto reference = referenceBySearch(order, mime, /*excludeSensitive=*/true);
        const auto paged = collectAllByPage(order,
                                            pageSize,
                                            mime,
                                            /*startCursor=*/std::nullopt,
                                            /*excludeSensitive=*/true);
        assertListAllMatchesReference(paged,
                                      reference,
                                      "ALL_VISUAL_MEDIA excludeSensitive order=" +
                                          std::to_string(order));
    }
}

// E3. ALL_DOCS with excludeSensitive=true must drop a sensitive document
//     (report_sensitive.pdf) while keeping every non-sensitive .txt/.pdf/.pptx/
//     .xlsx. Guards against a PDF-route regression where the EXISTS bind
//     slot drifted relative to the DOCUMENT / PRESENTATION / SPREADSHEET routes.
TEST_F(GroupedListAllNodesByPageTest, AllDocs_ExcludeSensitive_FiltersCorrectly)
{
    const auto all = allMatchesAsSet(MIME_TYPE_ALL_DOCS,
                                     OrderByClause::DEFAULT_ASC,
                                     /*excludeSensitive=*/false);
    const auto filtered = allMatchesAsSet(MIME_TYPE_ALL_DOCS,
                                          OrderByClause::DEFAULT_ASC,
                                          /*excludeSensitive=*/true);

    EXPECT_EQ(all.count(hReportSensitive), 1u)
        << "precondition: ALL_DOCS with excludeSensitive=false must surface "
           "report_sensitive.pdf";
    EXPECT_EQ(filtered.count(hReportSensitive), 0u)
        << "report_sensitive.pdf leaked through the ALL_DOCS PDF route";

    for (const auto& h: filtered)
        EXPECT_EQ(all.count(h), 1u) << "filtered leaked a handle not in unfiltered set";
    EXPECT_EQ(all.size(), filtered.size() + 1u)
        << "ALL_DOCS excludeSensitive=true must remove exactly the sensitive .pdf";
}

// ═══════════════════════════════════════════════════════════════════════════
//  Group H — byExcludeLocationHandles (excSeen accumulator, semantic A)
//
//  Reuses the SearchByPageTest fixture (Cloud + Vault + Rubbish photo subtree
//  built by populateDB). The exclude-list is implemented as a recursive
//  excSeen bit walked alongside sensSeen + a final-row `up.h NOT IN excludes`
//  check; the cases below pin both pieces against a single concrete tree.
// ═══════════════════════════════════════════════════════════════════════════

// H1. Exclude an interior folder: every descendant (direct and grand-) must be
//     dropped. Sibling files outside the excluded folder must remain.
TEST_F(ListAllNodesByPageTest, ByExclude_InteriorFolder_DropsWholeSubtree)
{
    auto p = makeParams(MIME_TYPE_PHOTO,
                        OrderByClause::DEFAULT_ASC,
                        /*maxElements=*/0,
                        /*excludeSensitive=*/false,
                        /*cursor=*/std::nullopt,
                        /*explicitAncestors=*/{},
                        /*excludeHandles=*/{hNormalFolder});

    const auto got = handlesOf(mClient->mNodeManager.listAllNodesByPage(p, CancelToken{}));

    EXPECT_EQ(got.count(hClean), 0u) << "clean.jpg is a direct child of the excluded folder";
    EXPECT_EQ(got.count(hHead), 0u) << "head.jpg dropped via excluded ancestor";
    EXPECT_EQ(got.count(hSelfSensitive), 0u) << "self_sensitive.jpg dropped via excluded ancestor";
    // Vault content survives — exclude list is independent of include scope.
    EXPECT_EQ(got.count(hVaultFile), 1u);
}

// H2. Exclude the node itself (not a folder): the initial-row excSeen check
//     `(n.nodehandle IN excludes)` must drop the node directly.
TEST_F(ListAllNodesByPageTest, ByExclude_NodeItself_DroppedByInitialCheck)
{
    auto p = makeParams(MIME_TYPE_PHOTO,
                        OrderByClause::DEFAULT_ASC,
                        /*maxElements=*/0,
                        /*excludeSensitive=*/false,
                        /*cursor=*/std::nullopt,
                        /*explicitAncestors=*/{},
                        /*excludeHandles=*/{hClean});

    const auto got = handlesOf(mClient->mNodeManager.listAllNodesByPage(p, CancelToken{}));

    EXPECT_EQ(got.count(hClean), 0u) << "n itself in excludes — must be dropped";
    // Other photos under the same folder must remain.
    EXPECT_EQ(got.count(hHead), 1u);
    EXPECT_EQ(got.count(hVaultFile), 1u);
}

// H3. Semantic A: excluding a root drops its entire subtree even when that
//     root is also the only included ancestor. Combination of byLocationHandles
//     and byExcludeLocationHandles with a deliberate handle overlap.
TEST_F(ListAllNodesByPageTest, ByExclude_MatchedRoot_SemanticA_DropsSubtree)
{
    auto p = makeParams(MIME_TYPE_PHOTO,
                        OrderByClause::DEFAULT_ASC,
                        /*maxElements=*/0,
                        /*excludeSensitive=*/false,
                        /*cursor=*/std::nullopt,
                        /*explicitAncestors=*/{hVault},
                        /*excludeHandles=*/{hVault});

    const auto got = handlesOf(mClient->mNodeManager.listAllNodesByPage(p, CancelToken{}));

    EXPECT_TRUE(got.empty())
        << "Excluding the only included root must drop the whole subtree (semantic A); "
           "got "
        << got.size() << " nodes";
}

// H4. Default scope (Cloud + Vault) with exclude={hVault} drops Vault content
//     but keeps Cloud — the user-visible answer to "exclude My Backups
//     folder". Independence from include path is the point.
TEST_F(ListAllNodesByPageTest, ByExclude_DefaultScope_VaultExcluded_KeepsCloud)
{
    auto p = makeParams(MIME_TYPE_PHOTO,
                        OrderByClause::DEFAULT_ASC,
                        /*maxElements=*/0,
                        /*excludeSensitive=*/false,
                        /*cursor=*/std::nullopt,
                        /*explicitAncestors=*/{},
                        /*excludeHandles=*/{hVault});

    const auto got = handlesOf(mClient->mNodeManager.listAllNodesByPage(p, CancelToken{}));

    EXPECT_EQ(got.count(hVaultFile), 0u) << "Vault subtree must be dropped — root in exclude list";
    EXPECT_EQ(got.count(hClean), 1u) << "Cloud content unaffected by Vault exclusion";
    EXPECT_EQ(got.count(hRubbishFile), 0u)
        << "Rubbish still excluded by default scope (independent of exclude list)";
}

// H5. Empty exclude list is equivalent to the pre-Phase-2 baseline. Pins the
//     "no-exclude template branch" parity — buildUpWalkExists must not emit
//     the excSeen column / IN-list when numExcludes == 0.
TEST_F(ListAllNodesByPageTest, ByExclude_EmptyList_EquivalentToBaseline)
{
    const auto baseline = allMatchesAsSet(MIME_TYPE_PHOTO,
                                          OrderByClause::DEFAULT_ASC,
                                          /*excludeSensitive=*/false);

    auto p = makeParams(MIME_TYPE_PHOTO,
                        OrderByClause::DEFAULT_ASC,
                        /*maxElements=*/0,
                        /*excludeSensitive=*/false,
                        /*cursor=*/std::nullopt,
                        /*explicitAncestors=*/{},
                        /*excludeHandles=*/{});
    const auto got = handlesOf(mClient->mNodeManager.listAllNodesByPage(p, CancelToken{}));

    EXPECT_EQ(got, baseline)
        << "Empty exclude list must produce a result identical to the no-filter baseline";
}

// H6. Multiple exclude handles (up to kListAllMaxExcludes=3): chain accumulates
//     OR over all entries. Excluding both Cloud and Vault folders simultaneously
//     leaves Rubbish-only — pinned via locationScope=
//     LOCATION_CLOUD_DRIVE_VAULT_AND_RUBBISH.
TEST_F(ListAllNodesByPageTest, ByExclude_MultipleHandles_OrAccumulated)
{
    auto p = makeParams(MIME_TYPE_PHOTO,
                        OrderByClause::DEFAULT_ASC,
                        /*maxElements=*/0,
                        /*excludeSensitive=*/false,
                        /*cursor=*/std::nullopt,
                        /*explicitAncestors=*/{},
                        /*excludeHandles=*/{hFilesRoot, hVault},
                        /*locationScope=*/2 /* CLOUD+VAULT+RUBBISH */);

    const auto got = handlesOf(mClient->mNodeManager.listAllNodesByPage(p, CancelToken{}));

    EXPECT_EQ(got.count(hClean), 0u);
    EXPECT_EQ(got.count(hVaultFile), 0u);
    EXPECT_EQ(got.count(hRubbishFile), 1u)
        << "Rubbish must remain — neither Cloud nor Vault root excludes it";
}

// H7. DbTable-level rejection: oversize exclude list (> kListAllMaxExcludes=3)
//     short-circuits with ok=false and empty out — same contract as filesRoots.
TEST_F(ListAllNodesByPageTest, DbTable_TooManyExcludes_ReturnsEmpty)
{
    auto* table = dynamic_cast<DBTableNodes*>(mClient->sctable.get());
    ASSERT_NE(table, nullptr);

    auto p = makeParams(MIME_TYPE_PHOTO,
                        OrderByClause::DEFAULT_ASC,
                        /*maxElements=*/0,
                        /*excludeSensitive=*/false,
                        /*cursor=*/std::nullopt,
                        /*explicitAncestors=*/{},
                        /*excludeHandles=*/{hClean, hHead, hVault, hRubbish}); // 4 entries

    std::vector<std::pair<NodeHandle, NodeSerialized>> out;
    const bool ok = table->listAllNodesByPage(p,
                                              /*roots=*/{hFilesRoot},
                                              out,
                                              CancelToken{});
    EXPECT_FALSE(ok);
    EXPECT_TRUE(out.empty());
}

// H8. DbTable-level rejection: UNDEF in exclude list short-circuits.
TEST_F(ListAllNodesByPageTest, DbTable_UndefInExcludes_ReturnsEmpty)
{
    auto* table = dynamic_cast<DBTableNodes*>(mClient->sctable.get());
    ASSERT_NE(table, nullptr);

    auto p = makeParams(MIME_TYPE_PHOTO,
                        OrderByClause::DEFAULT_ASC,
                        /*maxElements=*/0,
                        /*excludeSensitive=*/false,
                        /*cursor=*/std::nullopt,
                        /*explicitAncestors=*/{},
                        /*excludeHandles=*/{hClean, NodeHandle()});

    std::vector<std::pair<NodeHandle, NodeSerialized>> out;
    const bool ok = table->listAllNodesByPage(p,
                                              /*roots=*/{hFilesRoot},
                                              out,
                                              CancelToken{});
    EXPECT_FALSE(ok);
    EXPECT_TRUE(out.empty());
}

// H9. Pagination across an exclude boundary. Drop hSelfSensitive via exclude;
//     two pages of size 2 must produce no skips/duplicates.
TEST_F(ListAllNodesByPageTest, ByExclude_CursorAcrossExcludeBoundary_NoSkipsNoDuplicates)
{
    // Reference: single full-set call without exclude, then strip hSelfSensitive
    // — same expected output as the paged exclude run.
    std::vector<NodeHandle> reference;
    {
        auto refParams = makeParams(MIME_TYPE_PHOTO,
                                    OrderByClause::DEFAULT_ASC,
                                    /*maxElements=*/0,
                                    /*excludeSensitive=*/false);
        auto refNodes = mClient->mNodeManager.listAllNodesByPage(refParams, CancelToken{});
        for (const auto& n: refNodes)
            if (n->nodeHandle() != hSelfSensitive)
                reference.push_back(n->nodeHandle());
    }

    // Now collect the same set page-by-page with pageSize=2 + exclude.
    std::vector<NodeHandle> paged;
    std::optional<NodeSearchCursorOffset> cursor;
    const size_t maxIters = mMeta.size() + 2;
    size_t i = 0;
    for (; i < maxIters; ++i)
    {
        auto p = makeParams(MIME_TYPE_PHOTO,
                            OrderByClause::DEFAULT_ASC,
                            /*maxElements=*/2,
                            /*excludeSensitive=*/false,
                            cursor,
                            /*explicitAncestors=*/{},
                            /*excludeHandles=*/{hSelfSensitive});
        auto nodes = mClient->mNodeManager.listAllNodesByPage(p, CancelToken{});
        if (nodes.empty())
            break;
        for (const auto& n: nodes)
            paged.push_back(n->nodeHandle());
        const auto* lastNode = nodes.back().get();
        NodeSearchCursorOffset c;
        c.mLastName = lastNode->displayname() ? lastNode->displayname() : "";
        c.mLastHandle = lastNode->nodeHandle().as8byte();
        cursor = c;
    }

    ASSERT_LT(i, maxIters) << "Pagination did not terminate within " << maxIters
                           << " iterations — likely an infinite-pagination regression";

    // Order-preserving equivalence to reference.
    EXPECT_EQ(paged, reference)
        << "Pagination across an excluded row must skip it without losing or duplicating others";
}

// H10. Cache-key separation: same (mimeType, order, hasCursor, excludeSensitive,
//      numRoots) but different numExcludes must produce different prepared
//      statements. Verifies computeListAllCacheId(numExcludes) is wired in.
//      We assert it indirectly: a back-to-back call sequence with then without
//      excludes must succeed (not crash from a stale-stmt parameter mismatch).
TEST_F(ListAllNodesByPageTest, ByExclude_CacheKey_DistinctAcross_NumExcludes)
{
    auto p_with = makeParams(MIME_TYPE_PHOTO,
                             OrderByClause::DEFAULT_ASC,
                             /*maxElements=*/0,
                             /*excludeSensitive=*/false,
                             /*cursor=*/std::nullopt,
                             /*explicitAncestors=*/{},
                             /*excludeHandles=*/{hVault});
    auto p_without = makeParams(MIME_TYPE_PHOTO,
                                OrderByClause::DEFAULT_ASC,
                                /*maxElements=*/0,
                                /*excludeSensitive=*/false,
                                /*cursor=*/std::nullopt,
                                /*explicitAncestors=*/{},
                                /*excludeHandles=*/{});

    const auto a = handlesOf(mClient->mNodeManager.listAllNodesByPage(p_with, CancelToken{}));
    const auto b = handlesOf(mClient->mNodeManager.listAllNodesByPage(p_without, CancelToken{}));
    const auto a2 = handlesOf(mClient->mNodeManager.listAllNodesByPage(p_with, CancelToken{}));
    const auto b2 = handlesOf(mClient->mNodeManager.listAllNodesByPage(p_without, CancelToken{}));
    EXPECT_EQ(a, a2) << "Same params with excludes must be deterministic across repeated calls";
    EXPECT_EQ(b, b2) << "Same params without excludes must be deterministic across repeated calls";
    EXPECT_NE(a, b) << "Differently-shaped statements must not share a prepared stmt cache slot";
    EXPECT_LT(a.size(), b.size())
        << "exclude={vault} must drop at least the vault subtree compared to no exclude";
}

// H11. excludeHandles and excludeSensitive walk independent accumulator bits
//      (excSeen vs sensSeen) in buildUpWalkExists. Pin orthogonality so a
//      future refactor that merges or reorders them gets caught.
//
//      With the fixture's normal_folder subtree:
//        - hClean / hSelfSensitive / hHead under hNormalFolder.
//        - hSelfSensitive carries its own SENS bit; hUnderSens inherits SENS
//          via hSensFolder; hVaultFile is unaffected by either filter.
//      Expected drops with both filters enabled simultaneously:
//        - hClean       → dropped by exclude=normal_folder (clean's ancestor)
//        - hSelfSensitive → dropped by both
//        - hHead        → dropped by exclude (also under normal_folder)
//        - hUnderSens   → dropped by sensSeen (sens_folder ancestor)
//      Surviving: hVaultFile.
TEST_F(ListAllNodesByPageTest, ByExclude_AndExcludeSensitive_AreOrthogonal)
{
    auto p = makeParams(MIME_TYPE_PHOTO,
                        OrderByClause::DEFAULT_ASC,
                        /*maxElements=*/0,
                        /*excludeSensitive=*/true,
                        /*cursor=*/std::nullopt,
                        /*explicitAncestors=*/{},
                        /*excludeHandles=*/{hNormalFolder});

    const auto got = handlesOf(mClient->mNodeManager.listAllNodesByPage(p, CancelToken{}));

    EXPECT_EQ(got.count(hClean), 0u) << "dropped by excludeHandles";
    EXPECT_EQ(got.count(hHead), 0u) << "dropped by excludeHandles";
    EXPECT_EQ(got.count(hSelfSensitive), 0u) << "dropped by both filters";
    EXPECT_EQ(got.count(hUnderSens), 0u) << "dropped by excludeSensitive";
    EXPECT_EQ(got.count(hVaultFile), 1u) << "unaffected by either filter";
}

// ═══════════════════════════════════════════════════════════════════════════
//  Group I — locationScope (LOCATION_*)
//
//  Verifies that resolveListAllRoots maps each enum value to the right
//  rootnode set. SQL plumbing reuses the existing numRoots IN-list machinery
//  (already covered by G1*); these tests pin the NodeManager layer.
// ═══════════════════════════════════════════════════════════════════════════

// I1. LOCATION_CLOUD_DRIVE: only Cloud rootnode is walked; Vault and Rubbish
//     subtree photos must be absent.
TEST_F(ListAllNodesByPageTest, LocationScope_CloudDriveOnly_ExcludesVaultAndRubbish)
{
    auto p = makeParams(MIME_TYPE_PHOTO,
                        OrderByClause::DEFAULT_ASC,
                        /*maxElements=*/0,
                        /*excludeSensitive=*/false,
                        /*cursor=*/std::nullopt,
                        /*explicitAncestors=*/{},
                        /*excludeHandles=*/{},
                        /*locationScope=*/0 /* LOCATION_CLOUD_DRIVE */);
    const auto got = handlesOf(mClient->mNodeManager.listAllNodesByPage(p, CancelToken{}));

    EXPECT_EQ(got.count(hClean), 1u);
    EXPECT_EQ(got.count(hVaultFile), 0u);
    EXPECT_EQ(got.count(hRubbishFile), 0u);
}

// I2. LOCATION_CLOUD_DRIVE_VAULT_AND_RUBBISH: all three rootnodes walked.
TEST_F(ListAllNodesByPageTest, LocationScope_AllRootnodes_IncludesRubbish)
{
    auto p = makeParams(MIME_TYPE_PHOTO,
                        OrderByClause::DEFAULT_ASC,
                        /*maxElements=*/0,
                        /*excludeSensitive=*/false,
                        /*cursor=*/std::nullopt,
                        /*explicitAncestors=*/{},
                        /*excludeHandles=*/{},
                        /*locationScope=*/2 /* CLOUD+VAULT+RUBBISH */);
    const auto got = handlesOf(mClient->mNodeManager.listAllNodesByPage(p, CancelToken{}));

    EXPECT_EQ(got.count(hClean), 1u);
    EXPECT_EQ(got.count(hVaultFile), 1u);
    EXPECT_EQ(got.count(hRubbishFile), 1u)
        << "Rubbish subtree photos must surface when locationScope == ALL";
}

// I3. explicitAncestors wins over locationScope: when both are set, the list
//     takes precedence and locationScope is silently ignored. Pinned by
//     setting explicitAncestors={Vault} with locationScope=CLOUD_DRIVE_ONLY:
//     the Cloud-only scope must NOT narrow further.
TEST_F(ListAllNodesByPageTest, LocationScope_IgnoredWhenExplicitAncestorsPresent)
{
    auto p = makeParams(MIME_TYPE_PHOTO,
                        OrderByClause::DEFAULT_ASC,
                        /*maxElements=*/0,
                        /*excludeSensitive=*/false,
                        /*cursor=*/std::nullopt,
                        /*explicitAncestors=*/{hVault},
                        /*excludeHandles=*/{},
                        /*locationScope=*/0 /* LOCATION_CLOUD_DRIVE */);
    const auto got = handlesOf(mClient->mNodeManager.listAllNodesByPage(p, CancelToken{}));

    // Vault content must appear despite locationScope saying "Cloud only".
    EXPECT_EQ(got.count(hVaultFile), 1u);
    // Cloud content must NOT appear because explicitAncestors didn't include it.
    EXPECT_EQ(got.count(hClean), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Indexes must be (re)created on the resume/load path, not only on a
// fresh fetchnodes. These tests drive NodeManager::loadNodes() — the resume entry
// point — against a DB whose indexes were dropped to simulate an older SDK.
// ─────────────────────────────────────────────────────────────────────────────

// Minimal harness: a MegaClient over a SqliteDbAccess rooted at a fresh temp dir (named after
// `dirName`), with the nodes table opened. Index-enable flags are set before opensctable() so the
// open-time drop behaviour matches production. A sid is set so opensctable() can derive the
// statecache filename. Owns the temp dir: destruction releases the DB (and its WAL) and removes
// the dir.
struct IndexTestClient
{
    mega::MegaApp app;
    std::filesystem::path dir;
    std::shared_ptr<mega::MegaClient> client;

    IndexTestClient(const char* dirName, bool enableSearch, bool enableLexi):
        dir(makeFreshTestDir(dirName))
    {
        auto* dbAccess = new SqliteDbAccess(LocalPath::fromAbsolutePath(path_u8string(dir)));
        client = mt::makeClient(app, dbAccess);
        client->sid =
            "AWA5YAbtb4JO-y2zWxmKZpSe5-6XM7CTEkA-3Nv7J4byQUpOazdfSC1ZUFlS-kah76gPKUEkTF9g7MeE";
        client->enableSearchDBIndexes(enableSearch);
        client->enableLexicographicDBIndexes(enableLexi);
        client->opensctable();
    }

    ~IndexTestClient()
    {
        client.reset(); // release the DB (and its WAL) before removing the dir
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    // Commits the open sctable transaction so a separate read-only connection sees the writes
    // (~SqliteDbTable would otherwise roll them back on close). Call only while a transaction is
    // open — committing with none active is a SQLite error.
    void commit()
    {
        client->sctable->commit();
    }

    // Path of the node statecache DB this client created. Empty until opensctable() has run.
    std::string dbFile() const
    {
        return findNodesDbFile(dir);
    }
};

TEST(Sqlite, ResumeCreatesMissingIndexesOnExistingDb)
{
    // One live client throughout; reads use a separate raw connection while it (and its WAL) stays
    // open, avoiding a teardown/checkpoint race. That connection only sees committed state: the
    // first createIndexes() is persisted by commit() (also required so dropSearchDBIndexes() finds
    // no open transaction); the drop and the resume loadNodes() then run in auto-commit.
    IndexTestClient c("resume_creates_indexes_test", /*search=*/true, /*lexi=*/true);
    auto* sa = dynamic_cast<SqliteAccountState*>(c.client->sctable.get());
    ASSERT_TRUE(sa) << "sctable is not a SqliteAccountState";

    // A DB written by the current SDK has every index.
    sa->createIndexes(/*enableSearch=*/true, /*enableLexi=*/true);
    c.commit();
    const std::string dbFile = c.dbFile();
    ASSERT_FALSE(dbFile.empty()) << "node DB file not found under " << c.dir;

    // Simulate a DB written by an older SDK that lacked the search indexes.
    c.client->mNodeManager.dropSearchDBIndexes();
    const auto afterDrop = readNodesIndexSet(dbFile);
    ASSERT_FALSE(afterDrop.empty()) << "could not read indexes / db missing";
    ASSERT_EQ(0u, afterDrop.count("listallnodesdefaultidx")) << "drop did not take effect";
    ASSERT_EQ(1u, afterDrop.count("parenthandleindex")) << "unconditional index unexpectedly gone";

    // Resume from the existing cache: loadNodes() must recreate the missing indexes.
    ASSERT_TRUE(c.client->mNodeManager.loadNodes());
    const auto afterResume = readNodesIndexSet(dbFile);
    EXPECT_EQ(1u, afterResume.count("listallnodesdefaultidx"))
        << "resume (loadNodes) did not recreate the missing index";
    EXPECT_EQ(1u, afterResume.count("ctimeindex"));
    EXPECT_EQ(1u, afterResume.count("parenthandleindex"));

    // Idempotence: a second resume neither errors nor changes the index set.
    ASSERT_TRUE(c.client->mNodeManager.loadNodes());
    EXPECT_EQ(afterResume, readNodesIndexSet(dbFile)) << "second resume changed the index set";
}

TEST(Sqlite, ResumedDbHasSameIndexesAsFreshDb)
{
    IndexTestClient c("parity_index_set_test", /*search=*/true, /*lexi=*/true);

    // Fresh build through the load path -> the full index set the current createIndexes defines.
    ASSERT_TRUE(c.client->mNodeManager.loadNodes());
    c.commit();
    const std::string dbFile = c.dbFile();
    ASSERT_FALSE(dbFile.empty());
    const auto freshSet = readNodesIndexSet(dbFile);
    ASSERT_FALSE(freshSet.empty());

    // Mimic a DB written by an older SDK missing several indexes.
    c.client->mNodeManager.dropSearchDBIndexes();
    c.client->mNodeManager.dropLexicographicDBIndexes();
    ASSERT_NE(freshSet, readNodesIndexSet(dbFile)) << "drops did not change the index set";

    // Resume must restore the set to be identical to fresh.
    ASSERT_TRUE(c.client->mNodeManager.loadNodes());
    const auto resumedSet = readNodesIndexSet(dbFile);

    // Self-maintaining: a future index added to createIndexes appears in both sets; they diverge
    // only if the resume path stops reaching createIndexes (the regression this guards).
    EXPECT_THAT(resumedSet, ::testing::UnorderedElementsAreArray(freshSet));
}

TEST(Sqlite, ResumeRespectsIndexEnableFlags)
{
    // search OFF, lexi OFF: resume must create only the unconditional indexes.
    {
        IndexTestClient c("resume_flags_off_test", /*search=*/false, /*lexi=*/false);
        ASSERT_TRUE(c.client->mNodeManager.loadNodes());
        c.commit();
        const std::string dbFile = c.dbFile();
        ASSERT_FALSE(dbFile.empty()) << "node DB file not found under " << c.dir;
        const auto set = readNodesIndexSet(dbFile);
        ASSERT_FALSE(set.empty());
        EXPECT_EQ(1u, set.count("parenthandleindex")) << "unconditional index must always exist";
        EXPECT_EQ(0u, set.count("listallnodesdefaultidx"))
            << "search index built despite search=off";
        EXPECT_EQ(0u, set.count("ctimeindex")) << "search index built despite search=off";
        EXPECT_EQ(0u, set.count("lexicographics3keyindex")) << "lexi index built despite lexi=off";
    }

    // search ON, lexi ON: resume must create the full set.
    {
        IndexTestClient c("resume_flags_on_test", /*search=*/true, /*lexi=*/true);
        ASSERT_TRUE(c.client->mNodeManager.loadNodes());
        c.commit();
        const std::string dbFile = c.dbFile();
        ASSERT_FALSE(dbFile.empty()) << "node DB file not found under " << c.dir;
        const auto set = readNodesIndexSet(dbFile);
        EXPECT_EQ(1u, set.count("parenthandleindex"));
        EXPECT_EQ(1u, set.count("listallnodesdefaultidx"))
            << "search index missing despite search=on";
        EXPECT_EQ(1u, set.count("lexicographics3keyindex")) << "lexi index missing despite lexi=on";
    }
}

// ─── Offset windowing ────────────────────────────────────────────────────────
class ListAllNodesByPageOffsetTest: public mega::pagetest::SearchByPageTest
{
protected:
    std::vector<NodeHandle> reference(int order, MimeType_t mime = MIME_TYPE_DOCUMENT) const
    {
        return collectAllByPage(order, /*pageSize=*/4, mime);
    }

    std::vector<NodeHandle>
        offsetWindow(int order, size_t limit, int64_t offset, MimeType_t mime = MIME_TYPE_DOCUMENT)
    {
        auto nodes = mClient->mNodeManager.listAllNodesByPage(
            mega::pagetest::makeParams(mime, order, limit, false, std::nullopt, {}, {}, 1, offset),
            CancelToken{});
        std::vector<NodeHandle> out;
        for (const auto& n: nodes)
            out.push_back(n->nodeHandle());
        return out;
    }

    // Returns handles in the order returned by listAllNodesByPage for the given params.
    std::vector<NodeHandle> handlesOfParams(const ListAllNodesParams& p)
    {
        auto nodes = mClient->mNodeManager.listAllNodesByPage(p, CancelToken{});
        std::vector<NodeHandle> out;
        for (const auto& n: nodes)
            out.push_back(n->nodeHandle());
        return out;
    }
};

TEST_F(ListAllNodesByPageOffsetTest, Offset_Mid_SkipsExactly)
{
    const auto ref = reference(OrderByClause::DEFAULT_ASC);
    ASSERT_GE(ref.size(), 7u);
    const std::vector<NodeHandle> expected(ref.begin() + 3, ref.begin() + 7);

    EXPECT_EQ(offsetWindow(OrderByClause::DEFAULT_ASC, /*limit=*/4, /*offset=*/3), expected);
}

// Grouped-mime offset, single dominant route. Uses MIME_TYPE_ALL_VISUAL_MEDIA
// (grouped path: UNION ALL CTE over MIME_TYPE_PHOTO + MIME_TYPE_VIDEO routes),
// but the base fixture has 5 MIME_TYPE_PHOTO and 0 MIME_TYPE_VIDEO nodes, so only
// the PHOTO route is non-empty here. With offset >= limit, the outer OFFSET skips
// past what a wrongly-bound inner CTE would supply, so this catches the two
// inner-LIMIT binding bugs: under-fetch (LIMIT pageSize instead of offset+pageSize
// → outer OFFSET falls short → empty/partial slice) and double-OFFSET (inner also
// applying OFFSET → outer over-skips). It does NOT exercise cross-route merge
// interleave (one non-empty route) — that is covered by
// Offset_GroupedMime_DominantRoute_CrossesBoundary.
TEST_F(ListAllNodesByPageOffsetTest, Offset_GroupedMime_MatchesReferenceSlice)
{
    const int order = OrderByClause::MTIME_DESC;
    const auto ref = reference(order, MIME_TYPE_ALL_VISUAL_MEDIA);
    ASSERT_GE(ref.size(), 4u) << "need >=4 visual-media nodes in the fixture";

    // offset >= limit: outer OFFSET exposes any inner-LIMIT under-fetch / double-OFFSET.
    EXPECT_EQ(offsetWindow(order, /*limit=*/2, /*offset=*/2, MIME_TYPE_ALL_VISUAL_MEDIA),
              std::vector<NodeHandle>(ref.begin() + 2, ref.begin() + 4));
    EXPECT_EQ(offsetWindow(order, /*limit=*/3, /*offset=*/1, MIME_TYPE_ALL_VISUAL_MEDIA),
              std::vector<NodeHandle>(ref.begin() + 1, ref.begin() + 4));
}

TEST_F(ListAllNodesByPageOffsetTest, AllOrders_OffsetMatchesReferenceSlice)
{
    const std::vector<int> orders = {
        OrderByClause::DEFAULT_ASC,
        OrderByClause::DEFAULT_DESC,
        OrderByClause::SIZE_ASC,
        OrderByClause::SIZE_DESC,
        OrderByClause::MTIME_ASC,
        OrderByClause::MTIME_DESC,
        OrderByClause::LABEL_ASC,
        OrderByClause::LABEL_DESC,
        OrderByClause::FAV_ASC,
        OrderByClause::FAV_DESC,
    };

    for (int order: orders)
    {
        const auto ref = reference(order);
        const size_t len = ref.size();
        ASSERT_GT(len, 0u) << "order=" << order;

        const std::vector<int64_t> offsets = {0,
                                              1,
                                              static_cast<int64_t>(len / 2),
                                              static_cast<int64_t>(len) - 1,
                                              static_cast<int64_t>(len),
                                              static_cast<int64_t>(len) + 5};
        const std::vector<size_t> limits = {1, 3, len, 0 /* no limit */};

        for (int64_t k: offsets)
        {
            if (k < 0)
                continue;
            for (size_t l: limits)
            {
                const size_t from = std::min(static_cast<size_t>(k), len);
                const size_t count = (l == 0) ? (len - from) : std::min(l, len - from);
                const std::vector<NodeHandle> expected(
                    ref.begin() + static_cast<std::ptrdiff_t>(from),
                    ref.begin() + static_cast<std::ptrdiff_t>(from + count));
                EXPECT_EQ(offsetWindow(order, l, k), expected)
                    << "order=" << order << " offset=" << k << " limit=" << l;
            }
        }
    }
}

TEST_F(ListAllNodesByPageOffsetTest, Offset_Zero_EqualsNoOffset)
{
    const auto noOffset = offsetWindow(OrderByClause::MTIME_DESC, /*limit=*/5, /*offset=*/0);
    const auto ref = reference(OrderByClause::MTIME_DESC);
    ASSERT_GE(ref.size(), 5u);
    const std::vector<NodeHandle> first5(ref.begin(), ref.begin() + 5);
    EXPECT_EQ(noOffset, first5);
}

TEST_F(ListAllNodesByPageOffsetTest, Offset_BeyondEnd_ReturnsEmpty)
{
    EXPECT_TRUE(offsetWindow(OrderByClause::DEFAULT_ASC, /*limit=*/5, /*offset=*/100).empty());
}

// A negative offset is rejected at the engine entry (mirrors the cursor-exclusivity
// guard): rather than let SQLite clamp a negative OFFSET to 0 and emit a full
// unwindowed page, SqliteAccountState::listAllNodesByPage returns no rows. Exercised
// here directly through NodeManager, bypassing the public wrapper's own >=0 check.
TEST_F(ListAllNodesByPageOffsetTest, Offset_Negative_ReturnsEmpty)
{
    EXPECT_TRUE(offsetWindow(OrderByClause::DEFAULT_ASC, /*limit=*/5, /*offset=*/-1).empty());
}

// A cursor combined with a non-zero offset is rejected at the engine entry: the two
// pagination modes are mutually exclusive. (offset==0 is a no-op and stays allowed.)
TEST_F(ListAllNodesByPageOffsetTest, Offset_WithCursor_Rejected)
{
    const int order = OrderByClause::DEFAULT_ASC;
    auto p = mega::pagetest::makeParams(MIME_TYPE_PHOTO,
                                        order,
                                        /*maxElements=*/5,
                                        false,
                                        cursorFor(hClean, order),
                                        {},
                                        {},
                                        1,
                                        /*offset=*/1);
    EXPECT_TRUE(handlesOfParams(p).empty());
}

TEST_F(ListAllNodesByPageOffsetTest, Offset_MaxElementsZero_SkipsThenAll)
{
    const auto ref = reference(OrderByClause::DEFAULT_ASC);
    ASSERT_GE(ref.size(), 5u);
    const std::vector<NodeHandle> tail(ref.begin() + 4, ref.end());
    EXPECT_EQ(offsetWindow(OrderByClause::DEFAULT_ASC, /*limit=*/0, /*offset=*/4), tail);
}

TEST_F(ListAllNodesByPageOffsetTest, Offset_LastPartialWindow)
{
    const auto ref = reference(OrderByClause::DEFAULT_ASC);
    const size_t len = ref.size();
    ASSERT_GE(len, 2u);
    const std::vector<NodeHandle> last2(ref.end() - 2, ref.end());
    EXPECT_EQ(offsetWindow(OrderByClause::DEFAULT_ASC,
                           /*limit=*/5,
                           /*offset=*/static_cast<int64_t>(len) - 2),
              last2);
}

// Offset + excludeSensitive filter: window at offset=1 equals reference[1..2).
// Reference uses the same filter (excludeSensitive=true, MTIME_DESC, photos).
// Base fixture has 3 photos visible under default scope with sens filtered out:
//   clean.jpg (baseMtime+1), head.jpg (baseMtime+3), vault_file.jpg (baseMtime+5).
TEST_F(ListAllNodesByPageOffsetTest, Offset_ExcludeSensitive_Slice)
{
    const int order = OrderByClause::MTIME_DESC;
    const auto ref = collectAllByPage(order,
                                      /*pageSize=*/4,
                                      MIME_TYPE_PHOTO,
                                      /*startCursor=*/std::nullopt,
                                      /*excludeSensitive=*/true);
    ASSERT_GE(ref.size(), 2u);
    auto got = handlesOfParams(mega::pagetest::makeParams(MIME_TYPE_PHOTO,
                                                          order,
                                                          /*maxElements=*/1,
                                                          /*excludeSensitive=*/true,
                                                          std::nullopt,
                                                          {},
                                                          {},
                                                          1,
                                                          /*offset=*/1));
    EXPECT_EQ(got, std::vector<NodeHandle>(ref.begin() + 1, ref.begin() + 2));
}

// Offset + multi-root union: window at offset=1, limit=2 equals reference[1..3).
// Explicit roots {hFilesRoot, hVault, hRubbish} pull in all 5 photos.
TEST_F(ListAllNodesByPageOffsetTest, Offset_MultiRootUnion_Slice)
{
    const int order = OrderByClause::MTIME_ASC;
    const std::vector<NodeHandle> roots{hFilesRoot, hVault, hRubbish};
    const auto ref = handlesOfParams(mega::pagetest::makeParams(MIME_TYPE_PHOTO,
                                                                order,
                                                                /*maxElements=*/0,
                                                                false,
                                                                std::nullopt,
                                                                roots,
                                                                {},
                                                                1,
                                                                /*offset=*/0));
    ASSERT_GE(ref.size(), 3u);
    auto got = handlesOfParams(mega::pagetest::makeParams(MIME_TYPE_PHOTO,
                                                          order,
                                                          /*maxElements=*/2,
                                                          false,
                                                          std::nullopt,
                                                          roots,
                                                          {},
                                                          1,
                                                          /*offset=*/1));
    EXPECT_EQ(got, std::vector<NodeHandle>(ref.begin() + 1, ref.begin() + 3));
}

// Offset + excludeHandles subtree: window at offset=1, limit=1 equals reference[1..2).
// Dropping hNormalFolder leaves vault_file.jpg and under_sens.jpg in default scope.
TEST_F(ListAllNodesByPageOffsetTest, Offset_ExcludeSubtree_Slice)
{
    const int order = OrderByClause::MTIME_ASC;
    const std::vector<NodeHandle> excludes{hNormalFolder};
    const auto ref = handlesOfParams(mega::pagetest::makeParams(MIME_TYPE_PHOTO,
                                                                order,
                                                                /*maxElements=*/0,
                                                                false,
                                                                std::nullopt,
                                                                {},
                                                                excludes,
                                                                1,
                                                                /*offset=*/0));
    ASSERT_GE(ref.size(), 2u);
    // offset=1 (not 0) so OFFSET skipping actually composes with the exclude-subtree
    // filter; an impl that ignored params.offset would now fail this slice.
    auto got = handlesOfParams(mega::pagetest::makeParams(MIME_TYPE_PHOTO,
                                                          order,
                                                          /*maxElements=*/1,
                                                          false,
                                                          std::nullopt,
                                                          {},
                                                          excludes,
                                                          1,
                                                          /*offset=*/1));
    EXPECT_EQ(got, std::vector<NodeHandle>(ref.begin() + 1, ref.begin() + 2));
}

// Offset + timestampAnchor (DESC): window at offset=1 equals reference[1..2).
// mEndSeconds=1'900'000'000 admits all photos (baseMtime+1..+5 < 1'900'000'000).
TEST_F(ListAllNodesByPageOffsetTest, Offset_Anchor_DESC_Slice)
{
    const int order = OrderByClause::MTIME_DESC;
    auto p = mega::pagetest::makeParams(MIME_TYPE_PHOTO,
                                        order,
                                        /*maxElements=*/0,
                                        false,
                                        std::nullopt,
                                        {},
                                        {},
                                        1,
                                        /*offset=*/0);
    TimestampAnchorFilter ta;
    ta.mOrder = OrderByClause::MTIME_DESC; // DESC ⇒ upper bound: mtime < mEndSeconds
    ta.mStartSeconds = 0;
    ta.mEndSeconds = 1'900'000'000LL; // > all photo mtimes (baseMtime=1'800'000'000)
    p.timestampAnchor = ta;
    const auto ref = handlesOfParams(p);
    ASSERT_GE(ref.size(), 2u);

    p.maxElements = 1;
    p.offset = 1;
    EXPECT_EQ(handlesOfParams(p), std::vector<NodeHandle>(ref.begin() + 1, ref.begin() + 2));
}

// Offset + timestampAnchor (ASC): half-bound is mtime >= mStartSeconds (the DESC
// upper-bound path is covered above). mStartSeconds sits between the photo mtimes
// (baseMtime+1..+5) so the two earliest photos are cut off, proving the anchor
// actually excludes rows; offset=1 then slices within the survivors.
TEST_F(ListAllNodesByPageOffsetTest, Offset_Anchor_ASC_Slice)
{
    const int order = OrderByClause::MTIME_ASC;
    auto p = mega::pagetest::makeParams(MIME_TYPE_PHOTO,
                                        order,
                                        /*maxElements=*/0,
                                        false,
                                        std::nullopt,
                                        {},
                                        {},
                                        1,
                                        /*offset=*/0);
    TimestampAnchorFilter ta;
    ta.mOrder = OrderByClause::MTIME_ASC; // ASC ⇒ lower bound: mtime >= mStartSeconds
    ta.mStartSeconds = 1'800'000'003LL; // cuts off photos at baseMtime+1, +2
    ta.mEndSeconds = 1'900'000'000LL; // unused for ASC; kept > start for validity
    p.timestampAnchor = ta;
    const auto ref = handlesOfParams(p);
    ASSERT_GE(ref.size(), 2u);

    // The anchor really excluded rows (full unanchored set is larger).
    const auto full = handlesOfParams(mega::pagetest::makeParams(MIME_TYPE_PHOTO,
                                                                 order,
                                                                 /*maxElements=*/0,
                                                                 false,
                                                                 std::nullopt,
                                                                 {},
                                                                 {},
                                                                 1,
                                                                 /*offset=*/0));
    EXPECT_LT(ref.size(), full.size());

    p.maxElements = 1;
    p.offset = 1;
    EXPECT_EQ(handlesOfParams(p), std::vector<NodeHandle>(ref.begin() + 1, ref.begin() + 2));
}

// Dominant-route: photos all EARLIER than videos so the merged ASC order is
// all photos then all videos. An offset crossing the photo/video boundary must
// take items from the merged stream (proves inner top-(offset+limit), not per-CTE).
TEST_F(ListAllNodesByPageOffsetTest, Offset_GroupedMime_DominantRoute_CrossesBoundary)
{
    auto normal = mClient->mNodeManager.getNodeByHandle(hNormalFolder);
    ASSERT_NE(normal, nullptr);
    // Videos with mtimes AFTER every photo (photos use baseMtime≈1'800'000'00x).
    addNode(FILENODE, normal, NodeMeta{"vid_1.mp4", FILENODE, 700, 1'900'000'001LL}, false);
    addNode(FILENODE, normal, NodeMeta{"vid_2.mp4", FILENODE, 800, 1'900'000'002LL}, false);
    if (auto* sa = dynamic_cast<SqliteAccountState*>(mClient->sctable.get()))
        sa->createIndexes(/*enableSearch=*/true, /*enableLexi=*/true);

    const int order = OrderByClause::MTIME_ASC;
    const auto ref = reference(order, MIME_TYPE_ALL_VISUAL_MEDIA);
    ASSERT_GE(ref.size(), 4u);
    // A window whose offset lands near the end of the photos and whose limit
    // spills into the videos.
    const int64_t off = static_cast<int64_t>(ref.size()) - 3;
    EXPECT_EQ(offsetWindow(order, /*limit=*/3, off, MIME_TYPE_ALL_VISUAL_MEDIA),
              std::vector<NodeHandle>(ref.end() - 3, ref.end()));
}

// ---------------------------------------------------------------------------
// CacheKeyBuilder regression coverage. Pins computeListAllCacheId /
// computeDateSectionsCacheId (src/db/sqlite.cpp) to the pre-refactor
// arithmetic, so any change to digit order/stride is caught here rather than
// causing silent prepared-statement aliasing in production.
// ---------------------------------------------------------------------------

// Oracle = the v1 inline arithmetic, copied verbatim. The production
// function in sqlite.cpp is the CacheKeyBuilder-based rewrite. They MUST
// produce identical size_t for every legal input.
size_t oracleComputeListAllCacheId(MimeType_t mimeType,
                                   FileSubType_t fileSubType,
                                   int order,
                                   bool hasCursor,
                                   AnchorDirectionDigit anchorDir,
                                   bool excludeSensitive,
                                   size_t numRoots,
                                   size_t numExcludes,
                                   FavouriteFilter_t favourite)
{
    constexpr size_t orderStride = static_cast<size_t>(OrderByClause::FAV_DESC) + 1;
    constexpr size_t maxRoots = 3; // mirrors kListAllMaxLocationHandles
    constexpr size_t maxExcludes = 3;
    constexpr size_t anchorStride = 3;
    constexpr size_t subtypeStride = static_cast<size_t>(FILE_SUBTYPE_MAX) + 1;

    size_t key = static_cast<size_t>(mimeType);
    key = key * subtypeStride + static_cast<size_t>(fileSubType);
    key = key * orderStride + static_cast<size_t>(order);
    key = key * 2 + (hasCursor ? 1u : 0u);
    key = key * anchorStride + static_cast<size_t>(anchorDir);
    key = key * 2 + (excludeSensitive ? 1u : 0u);
    key = key * maxRoots + (numRoots - 1);
    key = key * (maxExcludes + 1) + numExcludes;
    constexpr size_t favStride = static_cast<size_t>(FAVOURITE_FILTER_MAX) + 1;
    key = key * favStride + static_cast<size_t>(favourite);
    return key;
}

size_t oracleComputeDateSectionsCacheId(MimeType_t mimeType,
                                        FileSubType_t fileSubType,
                                        int order,
                                        DateSectionGranularity granularity,
                                        bool excludeSensitive,
                                        size_t numRoots,
                                        size_t numExcludes,
                                        FavouriteFilter_t favourite)
{
    constexpr size_t orderStride = static_cast<size_t>(OrderByClause::FAV_DESC) + 1;
    constexpr size_t maxRoots = 3;
    constexpr size_t maxExcludes = 3;
    constexpr size_t subtypeStride = static_cast<size_t>(FILE_SUBTYPE_MAX) + 1;

    size_t key = static_cast<size_t>(mimeType);
    key = key * subtypeStride + static_cast<size_t>(fileSubType);
    key = key * orderStride + static_cast<size_t>(order);
    key = key * 3 + static_cast<size_t>(granularity);
    key = key * 2 + (excludeSensitive ? 1u : 0u);
    key = key * maxRoots + (numRoots - 1);
    key = key * (maxExcludes + 1) + numExcludes;
    constexpr size_t favStride = static_cast<size_t>(FAVOURITE_FILTER_MAX) + 1;
    key = key * favStride + static_cast<size_t>(favourite);
    return key;
}

constexpr FileSubType_t kAllFileSubTypes[] = {FILE_SUBTYPE_NONE,
                                              FILE_SUBTYPE_GIF,
                                              FILE_SUBTYPE_RAW};

// Dimension arrays (kAllMimeTypes / kAllValidOrders / kAllAnchorDirs /
// kAllGranularities) live in CacheKeyCombinations.h.

TEST(CacheKeyBuilder, ListAll_MatchesOracleArithmetic)
{
    // Nested for-loops, NOT TEST_P — ~24k combinations would create 24k
    // gtest entries and dwarf the rest of test_unit's listing.
    size_t checked = 0;
    for (auto mime: kAllMimeTypes)
        for (auto sub: kAllFileSubTypes)
            for (int order: kAllValidOrders)
                for (bool hasCursor: {false, true})
                    for (auto anchorDir: kAllAnchorDirs)
                        for (bool sens: {false, true})
                            for (size_t roots = 1; roots <= 3; ++roots)
                                for (size_t excludes = 0; excludes <= 3; ++excludes)
                                    for (auto fav: kAllFavouriteFilters)
                                    {
                                        const size_t actual = computeListAllCacheId(mime,
                                                                                    sub,
                                                                                    order,
                                                                                    hasCursor,
                                                                                    anchorDir,
                                                                                    sens,
                                                                                    roots,
                                                                                    excludes,
                                                                                    fav);
                                        const size_t expected =
                                            oracleComputeListAllCacheId(mime,
                                                                        sub,
                                                                        order,
                                                                        hasCursor,
                                                                        anchorDir,
                                                                        sens,
                                                                        roots,
                                                                        excludes,
                                                                        fav);
                                        ASSERT_EQ(actual, expected)
                                            << "mime=" << mime << " sub=" << static_cast<int>(sub)
                                            << " order=" << order << " hasCursor=" << hasCursor
                                            << " anchorDir=" << static_cast<int>(anchorDir)
                                            << " sens=" << sens << " roots=" << roots
                                            << " excludes=" << excludes
                                            << " fav=" << static_cast<int>(fav);
                                        ++checked;
                                    }
    EXPECT_EQ(checked, 14u * 3u * 12u * 2u * 3u * 2u * 3u * 4u * 3u); // 217728
}

TEST(CacheKeyBuilder, DateSections_MatchesOracleArithmetic)
{
    size_t checked = 0;
    for (auto mime: kAllMimeTypes)
        for (auto sub: kAllFileSubTypes)
            for (int order: kAllValidOrders)
                for (auto gran: kAllGranularities)
                    for (bool sens: {false, true})
                        for (size_t roots = 1; roots <= 3; ++roots)
                            for (size_t excludes = 0; excludes <= 3; ++excludes)
                                for (auto fav: kAllFavouriteFilters)
                                {
                                    const size_t actual = computeDateSectionsCacheId(mime,
                                                                                     sub,
                                                                                     order,
                                                                                     gran,
                                                                                     sens,
                                                                                     roots,
                                                                                     excludes,
                                                                                     fav);
                                    const size_t expected =
                                        oracleComputeDateSectionsCacheId(mime,
                                                                         sub,
                                                                         order,
                                                                         gran,
                                                                         sens,
                                                                         roots,
                                                                         excludes,
                                                                         fav);
                                    ASSERT_EQ(actual, expected)
                                        << "mime=" << mime << " sub=" << static_cast<int>(sub)
                                        << " order=" << order << " gran=" << static_cast<int>(gran)
                                        << " sens=" << sens << " roots=" << roots
                                        << " excludes=" << excludes
                                        << " fav=" << static_cast<int>(fav);
                                    ++checked;
                                }
    EXPECT_EQ(checked, 14u * 3u * 12u * 3u * 2u * 3u * 4u * 3u); // 108864
}

TEST(CacheKeyBuilder, ListAll_DistinctInputsProduceDistinctKeys)
{
    // Spot-check the no-collision property on a handful of dimensions:
    // any single-digit flip must change the key.
    const auto baseKey = computeListAllCacheId(MIME_TYPE_PHOTO,
                                               FILE_SUBTYPE_NONE,
                                               OrderByClause::MTIME_DESC,
                                               /*hasCursor=*/false,
                                               AnchorDirectionDigit::None,
                                               /*sens=*/false,
                                               /*roots=*/1,
                                               /*excludes=*/0,
                                               FAVOURITE_FILTER_DISABLED);

    EXPECT_NE(baseKey,
              computeListAllCacheId(MIME_TYPE_VIDEO,
                                    FILE_SUBTYPE_NONE,
                                    OrderByClause::MTIME_DESC,
                                    false,
                                    AnchorDirectionDigit::None,
                                    false,
                                    1,
                                    0,
                                    FAVOURITE_FILTER_DISABLED));
    EXPECT_NE(baseKey,
              computeListAllCacheId(MIME_TYPE_PHOTO,
                                    FILE_SUBTYPE_GIF,
                                    OrderByClause::MTIME_DESC,
                                    false,
                                    AnchorDirectionDigit::None,
                                    false,
                                    1,
                                    0,
                                    FAVOURITE_FILTER_DISABLED));
    EXPECT_NE(computeListAllCacheId(MIME_TYPE_PHOTO,
                                    FILE_SUBTYPE_GIF,
                                    OrderByClause::MTIME_DESC,
                                    false,
                                    AnchorDirectionDigit::None,
                                    false,
                                    1,
                                    0,
                                    FAVOURITE_FILTER_DISABLED),
              computeListAllCacheId(MIME_TYPE_PHOTO,
                                    FILE_SUBTYPE_RAW,
                                    OrderByClause::MTIME_DESC,
                                    false,
                                    AnchorDirectionDigit::None,
                                    false,
                                    1,
                                    0,
                                    FAVOURITE_FILTER_DISABLED));
    EXPECT_NE(baseKey,
              computeListAllCacheId(MIME_TYPE_PHOTO,
                                    FILE_SUBTYPE_NONE,
                                    OrderByClause::MTIME_ASC,
                                    false,
                                    AnchorDirectionDigit::None,
                                    false,
                                    1,
                                    0,
                                    FAVOURITE_FILTER_DISABLED));
    EXPECT_NE(baseKey,
              computeListAllCacheId(MIME_TYPE_PHOTO,
                                    FILE_SUBTYPE_NONE,
                                    OrderByClause::MTIME_DESC,
                                    true,
                                    AnchorDirectionDigit::None,
                                    false,
                                    1,
                                    0,
                                    FAVOURITE_FILTER_DISABLED));
    EXPECT_NE(baseKey,
              computeListAllCacheId(MIME_TYPE_PHOTO,
                                    FILE_SUBTYPE_NONE,
                                    OrderByClause::MTIME_DESC,
                                    false,
                                    AnchorDirectionDigit::Asc,
                                    false,
                                    1,
                                    0,
                                    FAVOURITE_FILTER_DISABLED));
    EXPECT_NE(baseKey,
              computeListAllCacheId(MIME_TYPE_PHOTO,
                                    FILE_SUBTYPE_NONE,
                                    OrderByClause::MTIME_DESC,
                                    false,
                                    AnchorDirectionDigit::Desc,
                                    false,
                                    1,
                                    0,
                                    FAVOURITE_FILTER_DISABLED));

    const auto favBase = computeListAllCacheId(MIME_TYPE_PHOTO,
                                               FILE_SUBTYPE_NONE,
                                               OrderByClause::MTIME_DESC,
                                               false,
                                               AnchorDirectionDigit::None,
                                               false,
                                               1,
                                               0,
                                               FAVOURITE_FILTER_DISABLED);
    EXPECT_NE(favBase,
              computeListAllCacheId(MIME_TYPE_PHOTO,
                                    FILE_SUBTYPE_NONE,
                                    OrderByClause::MTIME_DESC,
                                    false,
                                    AnchorDirectionDigit::None,
                                    false,
                                    1,
                                    0,
                                    FAVOURITE_FILTER_ONLY_TRUE));
    EXPECT_NE(computeListAllCacheId(MIME_TYPE_PHOTO,
                                    FILE_SUBTYPE_NONE,
                                    OrderByClause::MTIME_DESC,
                                    false,
                                    AnchorDirectionDigit::None,
                                    false,
                                    1,
                                    0,
                                    FAVOURITE_FILTER_ONLY_TRUE),
              computeListAllCacheId(MIME_TYPE_PHOTO,
                                    FILE_SUBTYPE_NONE,
                                    OrderByClause::MTIME_DESC,
                                    false,
                                    AnchorDirectionDigit::None,
                                    false,
                                    1,
                                    0,
                                    FAVOURITE_FILTER_ONLY_FALSE));
}

} // anonymous namespace

#endif // USE_SQLITE
