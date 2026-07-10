/**
 * @file sqlite.cpp
 * @brief SQLite DB access layer
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
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

#include "mega.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <sstream>

#ifdef USE_SQLITE
namespace mega {

static const char* NodeSearchFilterPtrStr = "NodeSearchFilterPtrStr";

SqliteDbAccess::SqliteDbAccess(const LocalPath& rootPath)
  : mRootPath(rootPath)
{
    LOG_debug << "sqlite version: " << sqlite3_libversion();
    assert(mRootPath.isAbsolute());
}

SqliteDbAccess::~SqliteDbAccess()
{
}

LocalPath SqliteDbAccess::databasePath(const FileSystemAccess&,
                                       const string& name,
                                       const int version) const
{
    ostringstream osstream;

    osstream << "megaclient_statecache"
             << version
             << "_"
             << name
             << ".db";

    LocalPath path = mRootPath;

    path.appendWithSeparator(
      LocalPath::fromRelativePath(osstream.str()),
      false);

    return path;
}


bool SqliteDbAccess::checkDbFileAndAdjustLegacy(FileSystemAccess& fsAccess, const string& name, const int flags, LocalPath& dbPath)
{
    dbPath = databasePath(fsAccess, name, DB_VERSION);
    auto upgraded = true;

    {
        auto legacyPath = databasePath(fsAccess, name, LEGACY_DB_VERSION);
        auto fileAccess = fsAccess.newfileaccess();

        if (fileAccess->fopen(legacyPath, FSLogging::logExceptFileNotFound))
        {
            LOG_debug << "Found legacy database at: " << legacyPath;

            // if current version is legacy, use that one... unless migration to NoD, DB with
            // virtual fingerprint column in Nodes table, or renaming to adapt the version to SRW
            // are required
            if (currentDbVersion == LEGACY_DB_VERSION &&
                LEGACY_DB_VERSION != LAST_DB_VERSION_WITHOUT_NOD &&
                LEGACY_DB_VERSION != LAST_DB_VERSION_WITHOUT_SRW &&
                LEGACY_DB_VERSION != LAST_DB_VERSION_WITHOUT_VFINGERPRINT)
            {
                LOG_debug << "Using a legacy database.";
                dbPath = std::move(legacyPath);
                upgraded = false;
            }
            else if ((flags & DB_OPEN_FLAG_RECYCLE))
            {
                LOG_debug << "Trying to recycle a legacy database.";
                // if DB_VERSION already exist, let's get rid of it first
                // (it could happen if downgrade is executed and come back to newer version)
                removeDBFiles(fsAccess, dbPath);

                if (renameDBFiles(fsAccess, legacyPath, dbPath))
                {
                    LOG_debug << "Legacy database recycled.";
                }
                else
                {
                    LOG_err << "Unable to recycle database, deleting...";
                    assert(false);
                    removeDBFiles(fsAccess, legacyPath);
                }
            }
            else
            {
                LOG_debug << "Deleting outdated legacy database.";
                removeDBFiles(fsAccess, legacyPath);
            }
        }
    }

    if (upgraded)
    {
        LOG_debug << "Using an upgraded DB: " << dbPath;
        currentDbVersion = DB_VERSION;
    }

    return fsAccess.fileExistsAt(dbPath);
}

SqliteDbTable *SqliteDbAccess::open(PrnGen &rng, FileSystemAccess &fsAccess, const string &name, const int flags, DBErrorCallback dBErrorCallBack)
{
    sqlite3 *db = nullptr;
    auto dbPath = databasePath(fsAccess, name, DB_VERSION);
    if (!openDBAndCreateStatecache(&db, fsAccess, name, dbPath, flags))
    {
        return nullptr;
    }

    return new SqliteDbTable(rng,
                             db,
                             fsAccess,
                             dbPath,
                             (flags & DB_OPEN_FLAG_TRANSACTED) > 0, std::move(dBErrorCallBack));

}

// An adapter around naturalsorting_compare
static int
    sqlite_naturalsorting_compare(void*, int size1, const void* data1, int size2, const void* data2)
{
    return naturalsorting_compare(static_cast<const char*>(data1),
                                  size1,
                                  static_cast<const char*>(data2),
                                  size2);
}

DbTable *SqliteDbAccess::openTableWithNodes(PrnGen &rng, FileSystemAccess &fsAccess, const string &name, const int flags, DBErrorCallback dBErrorCallBack)
{
    /**
     * Deprecated columns (WARNING: do not use these names anymore for new columns):
     * - size: file/folder size in Bytes (replaced by sizeVirtual, calculated from nodeCounter)
     * - mimetype: node mimetype (replaced by mimetypeVirtual, calculated from node name)
     */
    sqlite3 *db = nullptr;
    auto dbPath = databasePath(fsAccess, name, DB_VERSION);
    if (!openDBAndCreateStatecache(&db, fsAccess, name, dbPath, flags))
    {
        return nullptr;
    }

    if (sqlite3_create_function(db, u8"getmimetype", 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC, 0, &SqliteAccountState::userGetMimetype, 0, 0) != SQLITE_OK)
    {
        LOG_err << "Data base error(sqlite3_create_function userGetMimetype): " << sqlite3_errmsg(db);
        sqlite3_close(db);
        return nullptr;
    }

    if (sqlite3_create_function(db,
                                u8"getfilesubtype",
                                1,
                                SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                0,
                                &SqliteAccountState::userGetFileSubType,
                                0,
                                0) != SQLITE_OK)
    {
        LOG_err << "Data base error(sqlite3_create_function userGetFileSubType): "
                << sqlite3_errmsg(db);
        sqlite3_close(db);
        return nullptr;
    }

    if (sqlite3_create_function(db,
                                u8"getFingerprintExcludingMtime",
                                1,
                                SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                0,
                                &SqliteAccountState::getFingerprintExcludingMtime,
                                0,
                                0) != SQLITE_OK)
    {
        LOG_err << "Data base error(sqlite3_create_function getFingerprintExcludingMtime): "
                << sqlite3_errmsg(db);
        sqlite3_close(db);
        return nullptr;
    }

    if (sqlite3_create_function(db,
                                u8"getSizeFromNodeCounter",
                                1,
                                SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                0,
                                &SqliteAccountState::getSizeFromNodeCounter,
                                0,
                                0) != SQLITE_OK)
    {
        LOG_err << "Data base error(sqlite3_create_function getSizeFromNodeCounter): "
                << sqlite3_errmsg(db);
        sqlite3_close(db);
        return nullptr;
    }

    if (sqlite3_create_collation(db,
                                 "NATURALNOCASE",
                                 SQLITE_UTF8,
                                 nullptr,
                                 sqlite_naturalsorting_compare))
    {
        LOG_err << "Data base error(sqlite3_create_collation NATURALNOCASE): "
                << sqlite3_errmsg(db);
        sqlite3_close(db);
        return nullptr;
    }

    // Create specific table for handle nodes
    std::string sql =
        "CREATE TABLE IF NOT EXISTS nodes (nodehandle int64 PRIMARY KEY NOT NULL, "
        "parenthandle int64, name text, fingerprint BLOB, origFingerprint BLOB, "
        "type tinyint, mimetypeVirtual tinyint AS (getmimetype(name)) VIRTUAL, "
        "fingerprintVirtual BLOB AS (getFingerprintExcludingMtime(fingerprint)) VIRTUAL, "
        "sizeVirtual int64 AS (getSizeFromNodeCounter(counter)) VIRTUAL,"
        "s3keyVirtual text AS (name || (CASE WHEN type = 1 THEN '/' ELSE '' END)) VIRTUAL, "
        "share tinyint, fav tinyint, ctime int64, mtime int64 DEFAULT 0, "
        "flags int64, counter BLOB NOT NULL, "
        "node BLOB NOT NULL, label tinyint DEFAULT 0, description text, tags text)";

    int result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    if (result)
    {
        LOG_err << "Data base error: " << sqlite3_errmsg(db);
        sqlite3_close(db);
        return nullptr;
    }

    // Add following columns to existing 'nodes' table that might not have them, and populate them
    // if needed:
    vector<NewColumn> newCols{
        {"mtime",
         "int64 DEFAULT 0",
         NodeData::COMPONENT_MTIME,
         NewColumn::extractDataFromNodeData<MTimeType>},
        {"label",
         "tinyint DEFAULT 0",
         NodeData::COMPONENT_LABEL,
         NewColumn::extractDataFromNodeData<LabelType>},
        {"mimetypeVirtual",
         "tinyint AS (getmimetype(name)) VIRTUAL",
         NodeData::COMPONENT_NONE,
         nullptr},
        {"fingerprintVirtual",
         "BLOB AS (getFingerprintExcludingMtime(fingerprint)) VIRTUAL",
         NodeData::COMPONENT_NONE,
         nullptr},
        {"description",
         "text",
         NodeData::COMPONENT_DESCRIPTION,
         NewColumn::extractDataFromNodeData<DescriptionType>},
        {"tags", "text", NodeData::COMPONENT_TAGS, NewColumn::extractDataFromNodeData<TagsType>},
        {"sizeVirtual",
         "int64 AS (getSizeFromNodeCounter(counter)) VIRTUAL",
         NodeData::COMPONENT_NONE,
         nullptr},
        {"s3keyVirtual",
         "text AS (name || (CASE WHEN type = 1 THEN '/' ELSE '' END)) VIRTUAL",
         NodeData::COMPONENT_NONE,
         nullptr},
    };

    if (!addAndPopulateColumns(db, std::move(newCols)))
    {
        sqlite3_close(db);
        return nullptr;
    }

#if __ANDROID__
    // Android doesn't provide a temporal directory -> change default policy for temp
    // store (FILE=1) to avoid failures on large queries, so it relies on MEMORY=2
    result = sqlite3_exec(db, "PRAGMA temp_store=2;", nullptr, nullptr, nullptr);
    if (result)
    {
        LOG_err << "PRAGMA temp_store error " << sqlite3_errmsg(db);
        sqlite3_close(db);
        return nullptr;
    }
#endif

    result = sqlite3_create_function(db, "regexp", 2, SQLITE_ANY,0, &SqliteAccountState::userRegexp, 0, 0);
    if (result)
    {
        LOG_err << "Data base error(sqlite3_create_function userRegexp): " << sqlite3_errmsg(db);
        sqlite3_close(db);
        return nullptr;
    }

    result = sqlite3_create_function(db,
                                     "matchFilter",
                                     10,
                                     SQLITE_ANY,
                                     0,
                                     &SqliteAccountState::userMatchFilter,
                                     0,
                                     0);
    if (result)
    {
        LOG_err << "Data base error(sqlite3_create_function userMatchFilter): "
                << sqlite3_errmsg(db);
        sqlite3_close(db);
        return nullptr;
    }
    return new SqliteAccountState(rng,
                                db,
                                fsAccess,
                                dbPath,
                                (flags & DB_OPEN_FLAG_TRANSACTED) > 0,
                                std::move(dBErrorCallBack));
}

bool SqliteDbAccess::probe(FileSystemAccess& fsAccess, const string& name) const
{
    auto fileAccess = fsAccess.newfileaccess();

    LocalPath dbPath = databasePath(fsAccess, name, DB_VERSION);

    if (fileAccess->isfile(dbPath))
    {
        return true;
    }

    dbPath = databasePath(fsAccess, name, LEGACY_DB_VERSION);

    return fileAccess->isfile(dbPath);
}

std::optional<std::filesystem::path>
    SqliteDbAccess::getExistingDbPath(const FileSystemAccess& fsAccess,
                                      const std::string& fname) const
{
    auto expectedPath =
        databasePath(fsAccess, fname, DbAccess::DB_VERSION).asPlatformEncoded(false);
    if (std::filesystem::exists(expectedPath))
        return expectedPath;

    expectedPath =
        databasePath(fsAccess, fname, DbAccess::LEGACY_DB_VERSION).asPlatformEncoded(false);
    if (std::filesystem::exists(expectedPath))
        return expectedPath;
    return {};
}

const LocalPath& SqliteDbAccess::rootPath() const
{
    return mRootPath;
}

bool SqliteDbAccess::openDBAndCreateStatecache(sqlite3 **db, FileSystemAccess &fsAccess, const string &name, LocalPath &dbPath, const int flags)
{
    checkDbFileAndAdjustLegacy(fsAccess, name, flags, dbPath);
    int result = sqlite3_open_v2(dbPath.toPath(false).c_str(), db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE // The database is opened for reading and writing, and is created if it does not already exist. This is the behavior that is always used for sqlite3_open() and sqlite3_open16().
        | SQLITE_OPEN_FULLMUTEX // The new database connection will use the "Serialized" threading mode. This means that multiple threads can be used withou restriction.
        , nullptr);

    if (result)
    {
        if (db)
        {
            sqlite3_close(*db);
        }

        return false;
    }

#if !(TARGET_OS_IPHONE)
    result = sqlite3_exec(*db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    if (result)
    {
        sqlite3_close(*db);
        return false;
    }
#endif /* ! TARGET_OS_IPHONE */

    string sql = "CREATE TABLE IF NOT EXISTS statecache (id INTEGER PRIMARY KEY ASC NOT NULL, content BLOB NOT NULL)";

    result = sqlite3_exec(*db, sql.c_str(), nullptr, nullptr, nullptr);
    if (result)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(*db) ? sqlite3_errmsg(*db) : std::to_string(result));
        LOG_debug << "Failed to create table 'statecache'" << err;
        sqlite3_close(*db);
        return false;
    }

    return true;
}

bool SqliteDbAccess::renameDBFiles(FileSystemAccess& fsAccess,
                                   const LocalPath& legacyPath,
                                   const LocalPath& dbPath)
{
    // Main DB file should exits
    if (!fsAccess.renamelocal(legacyPath, dbPath))
    {
        return false;
    }

    std::unique_ptr<FileAccess> fileAccess = fsAccess.newfileaccess();

#if !(TARGET_OS_IPHONE)
    auto suffix = LocalPath::fromRelativePath("-shm");
    auto from = legacyPath;
    from.append(suffix);
    auto to = dbPath;
    to.append(suffix);

    // -shm could or couldn't be present
    if (fileAccess->fopen(from, FSLogging::logExceptFileNotFound) && !fsAccess.renamelocal(from, to))
    {
         // Exists origin and failure to rename
        LOG_debug << "Failure to rename -shm file";
        return false;
    }

    suffix = LocalPath::fromRelativePath("-wal");
    from = legacyPath;
    from.append(suffix);
    to = dbPath;
    to.append(suffix);

    // -wal could or couldn't be present
    if (fileAccess->fopen(from, FSLogging::logExceptFileNotFound) && !fsAccess.renamelocal(from, to))
    {
         // Exists origin and failure to rename
        LOG_debug << "Failure to rename -wall file";
        return false;
    }
#else
    // iOS doesn't use WAL mode, but Journal
    auto suffix = LocalPath::fromRelativePath("-journal");
    auto from = legacyPath;
    from.append(suffix);
    auto to = dbPath;
    to.append(suffix);

    // -journal could or couldn't be present
    if (fileAccess->fopen(from, FSLogging::logExceptFileNotFound) && !fsAccess.renamelocal(from, to))
    {
         // Exists origin and failure to rename
        LOG_debug << "Failure to rename -journal file";
        return false;
    }

#endif

    return true;
}

void SqliteDbAccess::removeDBFiles(FileSystemAccess& fsAccess, mega::LocalPath& dbPath)
{
    fsAccess.unlinklocal(dbPath);

#if !(TARGET_OS_IPHONE)
    auto suffix = LocalPath::fromRelativePath("-shm");
    auto fileToRemove = dbPath;
    fileToRemove.append(suffix);
    fsAccess.unlinklocal(fileToRemove);

    suffix = LocalPath::fromRelativePath("-wal");
    fileToRemove = dbPath;
    fileToRemove.append(suffix);
    fsAccess.unlinklocal(fileToRemove);
#else
    // iOS doesn't use WAL mode, but Journal
    auto suffix = LocalPath::fromRelativePath("-journal");
    auto fileToRemove = dbPath;
    fileToRemove.append(suffix);
    fsAccess.unlinklocal(fileToRemove);

#endif

}

bool SqliteDbAccess::addAndPopulateColumns(sqlite3* db, vector<NewColumn>&& newCols)
{
    // skip existing columns
    if (!stripExistingColumns(db, newCols))
    {
        return false;
    }

    // Fast path: nothing to migrate, no transaction needed.
    if (newCols.empty())
    {
        return true;
    }

    // Single outer txn makes ALTER + populate atomic against crash. SDK-6155.
    // IMMEDIATE acquires the write lock up front so SQLITE_BUSY fails fast.
    if (sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        LOG_err << "Db error during migration BEGIN IMMEDIATE: " << sqlite3_errmsg(db);
        return false;
    }

    // errmsg is only fresh for the ROLLBACK itself; callers log their own cause inline.
    auto rollback = [db](const char* where) -> bool
    {
        LOG_err << "Db error during migration: rolling back at " << where
                << " (see preceding error for cause)";
        if (sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            LOG_err << "Db error during migration ROLLBACK at " << where << ": "
                    << sqlite3_errmsg(db);
        }
        return false;
    };

    // add missing columns
    for (const auto& c : newCols)
    {
        if (!addColumn(db, c.name, c.type))
        {
            return rollback("ALTER TABLE");
        }
    }

    if (!migrateDataToColumns(db, std::move(newCols)))
    {
        return rollback("migrateDataToColumns");
    }

    if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        // COMMIT failures aren't logged anywhere else, so capture errmsg here.
        LOG_err << "Db error during migration COMMIT: " << sqlite3_errmsg(db);
        return rollback("COMMIT");
    }

    return true;
}

bool SqliteDbAccess::stripExistingColumns(sqlite3* db, vector<NewColumn>& cols)
{
    string query = "SELECT name, COUNT(name) FROM pragma_table_xinfo('nodes') WHERE name IN ( ";
    std::for_each(cols.begin(), cols.end(), [&query](const NewColumn& c) { query += '\'' + c.name + "',"; });
    query.pop_back(); // drop trailing ','
    query += " ) GROUP BY name";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_err << "Db error while preparing to search for existing cols: " << sqlite3_errmsg(db);
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        int existing = sqlite3_column_int(stmt, 1);
        const char* n = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (existing)
        {
            assert(n); // "Strings returned by sqlite3_column_text(), even empty strings, are always zero-terminated."
            cols.erase(std::remove_if(cols.begin(), cols.end(), [n](const NewColumn& c) { return c.name == n; }), cols.end());
        }
    }

    sqlite3_finalize(stmt);

    return true;
}

// Precondition: caller holds an open transaction (see addAndPopulateColumns, SDK-6155).
bool SqliteDbAccess::addColumn(sqlite3* db, const string& name, const string& type)
{
    assert(sqlite3_get_autocommit(db) == 0 && "addColumn() requires an open transaction");

    string query("ALTER TABLE nodes ADD COLUMN '" + name + "' " + type);
    if (sqlite3_exec(db, query.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        LOG_err << "Db error while adding 'nodes." << name << ' ' << type << "' column: " << sqlite3_errmsg(db);
        return false;
    }

    return true;
}

// Precondition: caller holds an open transaction (see addAndPopulateColumns, SDK-6155).
bool SqliteDbAccess::migrateDataToColumns(sqlite3* db, vector<NewColumn>&& cols)
{
    assert(sqlite3_get_autocommit(db) == 0 &&
           "migrateDataToColumns() requires an open transaction");

    if (cols.empty()) return true;

    // identify data pieces to copy to new columns
    cols.erase(std::remove_if(cols.begin(), cols.end(),
        [](const NewColumn& c) { return c.migrationId == NodeData::COMPONENT_NONE; }), cols.end());

    if (cols.empty()) return true;

    LOG_info << "Migrating Data base - populating new columns";

    // get existing data
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT nodehandle, node FROM nodes", -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_err << "Db error while preparing to extract data to migrate: " << sqlite3_errmsg(db);
        return false;
    }

    // extract values to be copied
    map<handle, std::vector<std::unique_ptr<MigrateType>>> newValues;
    uint64_t numRows = 0;
    uint64_t affectedRows = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char* blob = static_cast<const char*>(sqlite3_column_blob(stmt, 1));
        int blobSize = sqlite3_column_bytes(stmt, 1);
        handle nh = static_cast<handle>(sqlite3_column_int64(stmt, 0));
        NodeData nd(blob, static_cast<size_t>(blobSize), NodeData::COMPONENT_ATTRS);

        std::vector<std::unique_ptr<MigrateType>> migrateElement;
        migrateElement.reserve(cols.size());
        bool hasValues = std::transform_reduce(
            cols.begin(),
            cols.end(),
            false,
            std::logical_or{},
            [&migrateElement, &nd](const NewColumn& c) -> bool
            {
                assert(c.migrateOperation);
                return c.migrateOperation(nd, migrateElement);
            });


        ++numRows;

        // Only update row in DB if some column has valid data
        if (hasValues)
        {
            assert(migrateElement.size() == cols.size());
            newValues[nh] = std::move(migrateElement);
            ++affectedRows;
        }
    }

    LOG_info << "Migrating Data base - affected rows: " << affectedRows
             << "   from total rows: " << numRows;

    sqlite3_finalize(stmt);

    if (newValues.empty())
    {
        return true;
    }

    // Calculate index for query parameters
    std::map<int, int> dataToMigrate;
    int bindIdx = 0;
    for (const auto& c: cols)
    {
        assert(c.migrationId > NodeData::COMPONENT_NONE);
        dataToMigrate[c.migrationId] = ++bindIdx;
    }

    // build update query
    string query{"UPDATE nodes SET "};
    for (const NewColumn& c : cols)
    {
        query += c.name + "= ?" + std::to_string(dataToMigrate[c.migrationId]) + ',';
    }
    query.pop_back(); // drop trailing ','
    query += " WHERE nodehandle = ?" + std::to_string(cols.size() + 1); // identifier for 'nodehandle'

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
        LOG_err << "Db error while preparing to populate new columns: " << sqlite3_errmsg(db);
        return false;
    }

    // run update query for each data entry
    for (const auto& update : newValues)
    {
        assert(update.second.size() == cols.size());
        for (const auto& values : update.second)
        {
            assert(values);
            if (!values->bindToDb(stmt, dataToMigrate))
                return false;
        }

        int stepResult;
        if (sqlite3_bind_int64(stmt,
                               static_cast<int>(cols.size()) + 1,
                               static_cast<sqlite3_int64>(update.first)) !=
                SQLITE_OK || // nodehandle
            ((stepResult = sqlite3_step(stmt)) != SQLITE_DONE && stepResult != SQLITE_ROW) ||
            sqlite3_reset(stmt) != SQLITE_OK)
        {
            LOG_err << "Db error during migration while updating columns: " << sqlite3_errmsg(db);
            sqlite3_finalize(stmt);
            return false;
        }
    }

    sqlite3_finalize(stmt);

    return true;
}


SqliteDbTable::SqliteDbTable(PrnGen &rng, sqlite3* db, FileSystemAccess &fsAccess, const LocalPath &path, const bool checkAlwaysTransacted, DBErrorCallback dBErrorCallBack)
  : DbTable(rng, checkAlwaysTransacted, dBErrorCallBack)
  , db(db)
  , dbfile(path)
  , fsaccess(&fsAccess)
{
}

SqliteDbTable::~SqliteDbTable()
{
    resetCommitter();

    if (!db)
    {
        return;
    }

    sqlite3_finalize(pStmt);
    sqlite3_finalize(mDelStmt);
    sqlite3_finalize(mPutStmt);

    if (inTransaction())
    {
        SqliteDbTable::abort(); // fully qualify virtual function
    }

    sqlite3_close(db);
    LOG_debug << "Database closed " << dbfile;
}

bool SqliteDbTable::inTransaction() const
{
    return sqlite3_get_autocommit(db) == 0;
}

// set cursor to first record
void SqliteDbTable::rewind()
{
    if (!db)
    {
        return;
    }

    int result = SQLITE_OK;

    if (pStmt)
    {
        result = sqlite3_reset(pStmt);
    }
    else
    {
        result = sqlite3_prepare_v2(db, "SELECT id, content FROM statecache", -1, &pStmt, NULL);
    }

    errorHandler(result, "Rewind", false);
}

// retrieve next record through cursor
bool SqliteDbTable::next(uint32_t* index, string* data)
{
    if (!db)
    {
        return false;
    }

    if (!pStmt)
    {
        return false;
    }

    int rc = sqlite3_step(pStmt);

    if (rc != SQLITE_ROW)
    {
        sqlite3_finalize(pStmt);
        pStmt = NULL;

        errorHandler(rc, "Get next record", false);

        return false;
    }

    *index = static_cast<uint32_t>(sqlite3_column_int(pStmt, 0));

    data->assign(static_cast<const char*>(sqlite3_column_blob(pStmt, 1)),
                 static_cast<size_t>(sqlite3_column_bytes(pStmt, 1)));

    return true;
}

// retrieve record by index
bool SqliteDbTable::get(uint32_t index, string* data)
{
    if (!db)
    {
        return false;
    }

    sqlite3_stmt *stmt = nullptr;
    int rc;

    rc = sqlite3_prepare_v2(db, "SELECT content FROM statecache WHERE id = ?", -1, &stmt, NULL);
    if (rc == SQLITE_OK)
    {
        rc = sqlite3_bind_int(stmt, 1, static_cast<int>(index));
        if (rc == SQLITE_OK)
        {
            rc = sqlite3_step(stmt);
            if (rc == SQLITE_ROW)
            {
                data->assign(static_cast<const char*>(sqlite3_column_blob(stmt, 0)),
                             static_cast<size_t>(sqlite3_column_bytes(stmt, 0)));
            }
        }
    }

    errorHandler(rc, "Get record statecache", false);

    sqlite3_finalize(stmt);

    return rc == SQLITE_ROW;
}

// add/update record by index
bool SqliteDbTable::put(uint32_t index, char* data, unsigned len)
{
    if (!db)
    {
        return false;
    }

    // First bits at index are reserved for the type
    assert((index & (DbTable::IDSPACING - 1)) != MegaClient::CACHEDNODE); // nodes must be stored in DbTableNodes ('nodes' table, not 'statecache' table)

    checkTransaction();

    int sqlResult = SQLITE_OK;
    if (!mPutStmt)
    {
        sqlResult = sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO statecache (id, content) VALUES (?, ?)", -1, &mPutStmt, nullptr);
    }

    if (sqlResult == SQLITE_OK)
    {
        sqlResult = sqlite3_bind_int(mPutStmt, 1, static_cast<int>(index));
        if (sqlResult == SQLITE_OK)
        {
            sqlResult = sqlite3_bind_blob(mPutStmt, 2, data, static_cast<int>(len), SQLITE_STATIC);
            if (sqlResult == SQLITE_OK)
            {
                sqlResult = sqlite3_step(mPutStmt);
            }
        }
    }

    errorHandler(sqlResult, "Put record", false);

    sqlite3_reset(mPutStmt);

    return sqlResult == SQLITE_DONE;
}


// delete record by index
bool SqliteDbTable::del(uint32_t index)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    int sqlResult = SQLITE_OK;
    if (!mDelStmt)
    {
        sqlResult = sqlite3_prepare_v2(db, "DELETE FROM statecache WHERE id = ?", -1, &mDelStmt, nullptr);
    }

    if (sqlResult == SQLITE_OK)
    {
        sqlResult = sqlite3_bind_int(mDelStmt, 1, static_cast<int>(index));
        if (sqlResult == SQLITE_OK)
        {
            sqlResult = sqlite3_step(mDelStmt); // tipically SQLITE_DONE, but could be SQLITE_ROW if implementation returned removed row count
        }
    }

    errorHandler(sqlResult, "Delete record", false);

    sqlite3_reset(mDelStmt);

    return sqlResult == SQLITE_DONE || sqlResult == SQLITE_ROW;
}

// truncate table
void SqliteDbTable::truncate()
{
    if (!db)
    {
        return;
    }

    checkTransaction();
    assert(inTransaction());

    int rc = sqlite3_exec(db, "DELETE FROM statecache", 0, 0, NULL);
    errorHandler(rc, "Truncate ", false);
}

// begin transaction
void SqliteDbTable::begin()
{
    if (!db)
    {
        return;
    }

    assert(!inTransaction());
    LOG_debug << "DB transaction BEGIN " << dbfile;
    int rc = sqlite3_exec(db, "BEGIN", 0, 0, NULL);
    errorHandler(rc, "Begin transaction", false);
}

// commit transaction
void SqliteDbTable::commit()
{
    if (!db)
    {
        return;
    }

    LOG_debug << "DB transaction COMMIT " << dbfile;

    int rc = sqlite3_exec(db, "COMMIT", 0, 0, NULL);
    errorHandler(rc, "Commit transaction", false);
}

// abort transaction
void SqliteDbTable::abort()
{
    if (!db)
    {
        return;
    }

    LOG_debug << "DB transaction ROLLBACK " << dbfile;

    int rc = sqlite3_exec(db, "ROLLBACK", 0, 0, NULL);
    errorHandler(rc, "Rollback", false);
}

void SqliteDbTable::remove()
{
    if (!db)
    {
        return;
    }

    sqlite3_finalize(pStmt);
    pStmt = nullptr;
    sqlite3_finalize(mDelStmt);
    mDelStmt = nullptr;
    sqlite3_finalize(mPutStmt);
    mPutStmt = nullptr;

    if (inTransaction())
    {
        abort();
    }

    sqlite3_close(db);

    db = NULL;

    fsaccess->unlinklocal(dbfile);
}

void SqliteDbTable::errorHandler(int sqliteError, const string& operation, bool interrupt)
{
    DBError dbError = DBError::DB_ERROR_UNKNOWN;
    switch (sqliteError)
    {
        case SQLITE_OK:
        case SQLITE_ROW:
        case SQLITE_DONE:
            return;
        case SQLITE_ERROR:
            dbError = DBError::DB_ERROR;
            break;
        case SQLITE_INTERNAL:
            dbError = DBError::DB_ERROR_INTERNAL;
            break;
        case SQLITE_PERM:
            dbError = DBError::DB_ERROR_PERM;
            break;
        case SQLITE_ABORT:
            dbError = DBError::DB_ERROR_ABORT;
            break;
        case SQLITE_BUSY:
            dbError = DBError::DB_ERROR_BUSY;
            break;
        case SQLITE_LOCKED:
            dbError = DBError::DB_ERROR_LOCKED;
            break;
        case SQLITE_NOMEM:
            dbError = DBError::DB_ERROR_NOMEM;
            break;
        case SQLITE_READONLY:
            dbError = DBError::DB_ERROR_READONLY;
            break;
        case SQLITE_INTERRUPT:
            if (interrupt)
            {
                // SQLITE_INTERRUPT isn't handle as an error if caller can be interrupted
                LOG_debug << operation << ": interrupted";
                return;
            }
            dbError = DBError::DB_ERROR_INTERRUPT;
            break;
        case SQLITE_IOERR:
            dbError = DBError::DB_ERROR_IO;
            break;
        case SQLITE_CORRUPT:
            dbError = DBError::DB_ERROR_CORRUPT;
            break;
        case SQLITE_NOTFOUND:
            dbError = DBError::DB_ERROR_NOTFOUND;
            break;
        case SQLITE_FULL:
            dbError = DBError::DB_ERROR_FULL;
            break;
        case SQLITE_CANTOPEN:
            dbError = DBError::DB_ERROR_CANTOPEN;
            break;
        case SQLITE_PROTOCOL:
            dbError = DBError::DB_ERROR_PROTOCOL;
            break;
        case SQLITE_EMPTY:
            dbError = DBError::DB_ERROR_EMPTY;
            break;
        case SQLITE_SCHEMA:
            dbError = DBError::DB_ERROR_SCHEMA;
            break;
        case SQLITE_TOOBIG:
            dbError = DBError::DB_ERROR_TOOBIG;
            break;
        case SQLITE_CONSTRAINT:
            dbError = DBError::DB_ERROR_CONSTRAINT;
            break;
        case SQLITE_MISMATCH:
            dbError = DBError::DB_ERROR_MISMATCH;
            break;
        case SQLITE_MISUSE:
            dbError = DBError::DB_ERROR_MISUSE;
            break;
        case SQLITE_NOLFS:
            dbError = DBError::DB_ERROR_NOLFS;
            break;
        case SQLITE_AUTH:
            dbError = DBError::DB_ERROR_AUTH;
            break;
        case SQLITE_FORMAT:
            dbError = DBError::DB_ERROR_FORMAT;
            break;
        case SQLITE_RANGE:
            dbError = DBError::DB_ERROR_RANGE;
            break;
        case SQLITE_NOTADB:
            dbError = DBError::DB_ERROR_NOTADB;
            break;
        default:
            dbError = DBError::DB_ERROR_UNKNOWN;
            break;
    }

    string err = string(" Error: ") +
                 (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqliteError));
    LOG_err << operation << ": " << dbfile << err;
    assert(!operation.c_str());

    if (mDBErrorCallBack)
    {
        // Only notify DB errors related to disk-is-full and input/output failures
        mDBErrorCallBack(dbError);
    }
}

SqliteAccountState::SqliteAccountState(PrnGen &rng, sqlite3 *pdb, FileSystemAccess &fsAccess, const LocalPath &path, const bool checkAlwaysTransacted, DBErrorCallback dBErrorCallBack)
    : SqliteDbTable(rng, pdb, fsAccess, path, checkAlwaysTransacted, dBErrorCallBack)
{
}

SqliteAccountState::~SqliteAccountState()
{
    finalise();
}

int SqliteAccountState::progressHandler(void *param)
{
    CancelToken* cancelFlag = static_cast<CancelToken*>(param);
    return cancelFlag->isCancelled();
}

bool SqliteAccountState::processSqlQueryNodes(sqlite3_stmt *stmt, std::vector<std::pair<mega::NodeHandle, mega::NodeSerialized>>& nodes)
{
    assert(stmt);
    int sqlResult = SQLITE_ERROR;
    while ((sqlResult = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        NodeHandle nodeHandle;
        nodeHandle.set6byte(static_cast<uint64_t>(sqlite3_column_int64(stmt, 0)));

        NodeSerialized node;

        // Blob node counter
        const void* data = sqlite3_column_blob(stmt, 1);
        int size = sqlite3_column_bytes(stmt, 1);
        if (data && size)
        {
            node.mNodeCounter =
                std::string(static_cast<const char*>(data), static_cast<size_t>(size));
        }

        // blob node
        data = sqlite3_column_blob(stmt, 2);
        size = sqlite3_column_bytes(stmt, 2);
        if (data && size)
        {
            node.mNode = std::string(static_cast<const char*>(data), static_cast<size_t>(size));
            nodes.insert(nodes.end(), std::make_pair(nodeHandle, std::move(node)));
        }
    }

    errorHandler(sqlResult, "Process sql query", true);

    return sqlResult == SQLITE_DONE;
}

bool SqliteAccountState::remove(NodeHandle nodehandle)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    char buf[64];

    snprintf(buf, sizeof(buf), "DELETE FROM nodes WHERE nodehandle = %" PRId64, nodehandle.as8byte());

    int sqlResult = sqlite3_exec(db, buf, 0, 0, NULL);
    errorHandler(sqlResult, "Delete node", false);

    return sqlResult == SQLITE_OK;
}

bool SqliteAccountState::removeNodes()
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    int sqlResult = sqlite3_exec(db, "DELETE FROM nodes", 0, 0, NULL);
    errorHandler(sqlResult, "Delete nodes", false);

    return sqlResult == SQLITE_OK;
}

void SqliteAccountState::updateCounter(NodeHandle nodeHandle, const std::string& nodeCounterBlob)
{
    if (!db)
    {
        return;
    }

    checkTransaction();

    int sqlResult = SQLITE_OK;
    if (!mStmtUpdateNode)
    {
        sqlResult = sqlite3_prepare_v2(db, "UPDATE nodes SET counter = ?  WHERE nodehandle = ?", -1, &mStmtUpdateNode, NULL);
    }

    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_blob(mStmtUpdateNode, 1, nodeCounterBlob.data(), static_cast<int>(nodeCounterBlob.size()), SQLITE_STATIC)) == SQLITE_OK)
        {
            if ((sqlResult = sqlite3_bind_int64(
                     mStmtUpdateNode,
                     2,
                     static_cast<sqlite3_int64>(nodeHandle.as8byte()))) == SQLITE_OK)
            {
                sqlResult = sqlite3_step(mStmtUpdateNode);
            }
        }

    }

    errorHandler(sqlResult, "Update counter", false);

    sqlite3_reset(mStmtUpdateNode);
}

void SqliteAccountState::updateCounterAndFlags(NodeHandle nodeHandle, uint64_t flags, const std::string& nodeCounterBlob)
{
    if (!db)
    {
        return;
    }

    checkTransaction();

    int sqlResult = SQLITE_OK;
    if (!mStmtUpdateNodeAndFlags)
    {
        sqlResult = sqlite3_prepare_v2(db, "UPDATE nodes SET counter = ?, flags = ? WHERE nodehandle = ?", -1, &mStmtUpdateNodeAndFlags, NULL);
    }

    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_blob(mStmtUpdateNodeAndFlags, 1, nodeCounterBlob.data(), static_cast<int>(nodeCounterBlob.size()), SQLITE_STATIC)) == SQLITE_OK)
        {
            if ((sqlResult = sqlite3_bind_int64(mStmtUpdateNodeAndFlags,
                                                2,
                                                static_cast<sqlite3_int64>(flags))) == SQLITE_OK)
            {
                if ((sqlResult = sqlite3_bind_int64(
                         mStmtUpdateNodeAndFlags,
                         3,
                         static_cast<sqlite3_int64>(nodeHandle.as8byte()))) == SQLITE_OK)
                {
                    sqlResult = sqlite3_step(mStmtUpdateNodeAndFlags);
                }
            }
        }
    }

    errorHandler(sqlResult, "Update counter and flags", false);

    sqlite3_reset(mStmtUpdateNodeAndFlags);
}

// Single source of truth for the `nodes` table indexes. Invoked from every node-load path
// (initCompleted = server fetch, dumpNodes = legacy upgrade, loadNodes = cache resume), so any
// index added here is created on existing DBs too. Add new node indexes here, not elsewhere.
// All statements are CREATE INDEX IF NOT EXISTS (idempotent).
void SqliteAccountState::createIndexes(bool enableIndexesForSearching,
                                       bool enableIndexesForLexicographicalList)
{
    if (!db)
    {
        return;
    }

    // Create index for column that is not primary key (which already has an index by default)
    std::string sql =
        "CREATE INDEX IF NOT EXISTS parenthandleindex on nodes (parenthandle, type, name)";
    int result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    if (result)
    {
        LOG_err << "Data base error while creating index (parenthandleindex): " << sqlite3_errmsg(db);
    }

    sql = "CREATE INDEX IF NOT EXISTS fingerprintindex on nodes (fingerprint)";
    result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    if (result)
    {
        LOG_err << "Data base error while creating index (fingerprintindex): " << sqlite3_errmsg(db);
    }

    sql = "CREATE INDEX IF NOT EXISTS fingerprintvirtualindex on nodes (fingerprintVirtual)";
    result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    if (result)
    {
        LOG_err << "Data base error while creating index (fingerprintvirtualindex): "
                << sqlite3_errmsg(db);
    }

#if defined( __ANDROID__) || defined(USE_IOS)
    sql = "CREATE INDEX IF NOT EXISTS origFingerprintindex on nodes (origFingerprint)";
    result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    if (result)
    {
        LOG_err << "Data base error while creating index (origFingerprintindex): " << sqlite3_errmsg(db);
    }
#endif

    if (enableIndexesForSearching)
    {
        sql = "CREATE INDEX IF NOT EXISTS shareindex on nodes (share)";
        result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
        if (result)
        {
            LOG_err << "Data base error while creating index (shareindex): " << sqlite3_errmsg(db);
        }

        sql = "CREATE INDEX IF NOT EXISTS ctimeindex on nodes (type, ctime DESC)";
        result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
        if (result)
        {
            LOG_err << "Data base error while creating index (ctimeindex): " << sqlite3_errmsg(db);
        }

        // Column layout mirrors buildOrderByForListAll:
        //   mimetypeVirtual — equality seek for the mandatory MIME filter
        //   <sort key(s)>   — covers ORDER BY without a filesort
        //   nodehandle      — unique tiebreaker, avoids extra lookup

        // Index for ORDER_DEFAULT_ASC / ORDER_DEFAULT_DESC.
        // name COLLATE NATURALNOCASE must carry the collation to match
        // "name COLLATE NATURALNOCASE" in the ORDER BY.
        sql = "CREATE INDEX IF NOT EXISTS listallnodesdefaultidx on nodes "
              "(mimetypeVirtual, name COLLATE NATURALNOCASE, nodehandle)";
        result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
        if (result)
        {
            LOG_err << "Data base error while creating index (listallnodesdefaultidx): "
                    << sqlite3_errmsg(db);
        }

        // Index for ORDER_MODIFICATION_ASC / ORDER_MODIFICATION_DESC.
        sql = "CREATE INDEX IF NOT EXISTS listallnodesmtimeidx on nodes "
              "(mimetypeVirtual, mtime, name COLLATE NATURALNOCASE, nodehandle)";
        result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
        if (result)
        {
            LOG_err << "Data base error while creating index (listallnodesmtimeidx): "
                    << sqlite3_errmsg(db);
        }

        // Index for ORDER_SIZE_ASC / ORDER_SIZE_DESC.
        sql = "CREATE INDEX IF NOT EXISTS listallnodessizeidx on nodes "
              "(mimetypeVirtual, sizeVirtual, name COLLATE NATURALNOCASE, nodehandle)";
        result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
        if (result)
        {
            LOG_err << "Data base error while creating index (listallnodessizeidx): "
                    << sqlite3_errmsg(db);
        }

        // Index for ORDER_FAV_ASC (ORDER BY fav DESC, name ASC, nodehandle ASC).
        sql = "CREATE INDEX IF NOT EXISTS listallnodesfavidx on nodes "
              "(mimetypeVirtual, fav DESC, name COLLATE NATURALNOCASE, nodehandle)";
        result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
        if (result)
        {
            LOG_err << "Data base error while creating index (listallnodesfavidx): "
                    << sqlite3_errmsg(db);
        }

        // Index for ORDER_FAV_DESC (ORDER BY fav ASC, name ASC, nodehandle ASC).
        sql = "CREATE INDEX IF NOT EXISTS listallnodesfavdescidx on nodes "
              "(mimetypeVirtual, fav ASC, name COLLATE NATURALNOCASE, nodehandle)";
        result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
        if (result)
        {
            LOG_err << "Data base error while creating index (listallnodesfavdescidx): "
                    << sqlite3_errmsg(db);
        }

        // Index for ORDER_LABEL_ASC
        // (ORDER BY CASE WHEN label=0 THEN 1 ELSE 0 END ASC, label ASC, name ASC).
        // The expression column lets SQLite cover the ORDER BY expression without a filesort.
        sql = "CREATE INDEX IF NOT EXISTS listallnodeslabelidx on nodes "
              "(mimetypeVirtual, (CASE WHEN label = 0 THEN 1 ELSE 0 END), label, "
              "name COLLATE NATURALNOCASE, nodehandle)";
        result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
        if (result)
        {
            LOG_err << "Data base error while creating index (listallnodeslabelidx): "
                    << sqlite3_errmsg(db);
        }

        // Index for ORDER_LABEL_DESC (ORDER BY label DESC, name ASC, nodehandle ASC).
        sql = "CREATE INDEX IF NOT EXISTS listallnodeslabeldescidx on nodes "
              "(mimetypeVirtual, label DESC, name COLLATE NATURALNOCASE, nodehandle)";
        result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
        if (result)
        {
            LOG_err << "Data base error while creating index (listallnodeslabeldescidx): "
                    << sqlite3_errmsg(db);
        }
    }
    if (enableIndexesForLexicographicalList)
    {
        // Drop the pre-S3-key index (keyed on raw name, type): listings now order by s3keyVirtual,
        // so it's dead. createIndexes runs on every open, so this also clears it from an existing
        // DB — migrated in place, no DB version bump.
        sql = "DROP INDEX IF EXISTS lexicopraphicindex";
        result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
        if (result)
        {
            LOG_err << "Data base error while dropping stale index (lexicopraphicindex): "
                    << sqlite3_errmsg(db);
        }

        sql = "CREATE INDEX IF NOT EXISTS lexicographics3keyindex on nodes (parenthandle, "
              "s3keyVirtual, nodehandle)";
        result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
        if (result)
        {
            LOG_err << "Data base error while creating index (lexicographics3keyindex): "
                    << sqlite3_errmsg(db);
        }
    }
}

void SqliteAccountState::dropSearchDBIndexes()
{
    dropDBIndexes({"shareindex",
                   "favindex",
                   "ctimeindex",
                   "listallnodesdefaultidx",
                   "listallnodesmtimeidx",
                   "listallnodessizeidx",
                   "listallnodesfavidx",
                   "listallnodesfavdescidx",
                   "listallnodeslabelidx",
                   "listallnodeslabeldescidx"});
}

void SqliteAccountState::dropLexicographicDBIndexes()
{
    dropDBIndexes({"lexicographics3keyindex", "lexicopraphicindex"});
}

void SqliteAccountState::dropDBIndexes(const std::vector<std::string>& indicesToDelete)
{
    if (!db)
    {
        return;
    }

    assert(!inTransaction());
    // Finalise all statements
    finalise();
    begin();

    for (const auto& indexName: indicesToDelete)
    {
        sqlite3_stmt* stmt = nullptr;
        const std::string query = "SELECT 1 FROM sqlite_master WHERE type = 'index' AND name = ?";

        if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, indexName.c_str(), -1, SQLITE_TRANSIENT);

            if (sqlite3_step(stmt) == SQLITE_ROW)
            {
                sqlite3_finalize(stmt);
                const std::string dropStmt = "DROP INDEX " + indexName + ";";
                if (int sqlResult = sqlite3_exec(db, dropStmt.c_str(), nullptr, nullptr, nullptr);
                    sqlResult != SQLITE_OK)
                {
                    errorHandler(sqlResult,
                                 "Error while dropping index (" + indexName + ")",
                                 false);
                }
            }
            else
            {
                sqlite3_finalize(stmt);
            }
        }
    }

    commit();
}

void SqliteAccountState::remove()
{
    finalise();

    SqliteDbTable::remove();
}

void SqliteAccountState::finalise()
{
    sqlite3_finalize(mStmtPutNode);
    mStmtPutNode = nullptr;

    sqlite3_finalize(mStmtUpdateNode);
    mStmtUpdateNode = nullptr;

    sqlite3_finalize(mStmtUpdateNodeAndFlags);
    mStmtUpdateNodeAndFlags = nullptr;

    sqlite3_finalize(mStmtTypeAndSizeNode);
    mStmtTypeAndSizeNode = nullptr;

    sqlite3_finalize(mStmtGetNode);
    mStmtGetNode = nullptr;

    sqlite3_finalize(mStmtChildrenFromType);
    mStmtChildrenFromType = nullptr;

    sqlite3_finalize(mStmtNumChildren);
    mStmtNumChildren = nullptr;

    for (auto& s : mStmtGetChildren)
    {
        sqlite3_finalize(s.second);
    }
    mStmtGetChildren.clear();

    sqlite3_finalize(mStmtGetChildrenLexi);
    mStmtGetChildrenLexi = nullptr;

    sqlite3_finalize(mStmtGetChildrenLexiNoOffset);
    mStmtGetChildrenLexiNoOffset = nullptr;

    for (auto& s : mStmtSearchNodes)
    {
        sqlite3_finalize(s.second);
    }
    mStmtSearchNodes.clear();

    for (auto& s: mStmtListAllNodesByPage)
    {
        sqlite3_finalize(s.second);
    }
    mStmtListAllNodesByPage.clear();

    for (auto& s: mStmtDateSections)
    {
        sqlite3_finalize(s.second);
    }
    mStmtDateSections.clear();

    sqlite3_finalize(mStmtNodeTagsBelow);
    mStmtNodeTagsBelow = nullptr;

    sqlite3_finalize(mStmtNodesByFpNoMtime);
    mStmtNodesByFpNoMtime = nullptr;

    sqlite3_finalize(mStmtNodeByFp);
    mStmtNodeByFp = nullptr;

    sqlite3_finalize(mStmtNodeByOrigFp);
    mStmtNodeByOrigFp = nullptr;

    sqlite3_finalize(mStmtNodesWithInshares);
    mStmtNodesWithInshares = nullptr;

    sqlite3_finalize(mStmtNodesWithOutshares);
    mStmtNodesWithOutshares = nullptr;

    sqlite3_finalize(mStmtNodesWithPendingOutshares);
    mStmtNodesWithPendingOutshares = nullptr;

    sqlite3_finalize(mStmtNodesWithPubLink);
    mStmtNodesWithPubLink = nullptr;

    sqlite3_finalize(mStmtChildNode);
    mStmtChildNode = nullptr;

    sqlite3_finalize(mStmtIsAncestor);
    mStmtIsAncestor = nullptr;

    sqlite3_finalize(mStmtNumChild);
    mStmtNumChild = nullptr;

    sqlite3_finalize(mStmtRecents);
    mStmtRecents = nullptr;

    sqlite3_finalize(mStmtFavourites);
    mStmtFavourites = nullptr;
}

bool SqliteAccountState::put(Node *node)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    int sqlResult = SQLITE_OK;
    if (!mStmtPutNode)
    {
        sqlResult =
            sqlite3_prepare_v2(db,
                               "INSERT OR REPLACE INTO nodes (nodehandle, parenthandle, "
                               "name, fingerprint, origFingerprint, type, share, fav, ctime, "
                               "mtime, flags, counter, node, label, description, tags) "
                               "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                               -1,
                               &mStmtPutNode,
                               NULL);
    }

    if (sqlResult == SQLITE_OK)
    {
        string nodeSerialized;
        node->serialize(&nodeSerialized);
        assert(nodeSerialized.size());

        sqlite3_bind_int64(mStmtPutNode, 1, static_cast<sqlite3_int64>(node->nodehandle));
        sqlite3_bind_int64(mStmtPutNode, 2, static_cast<sqlite3_int64>(node->parenthandle));

        std::string name = node->displayname(Node::LOG_CONDITION_DISABLE_NO_KEY);
        sqlite3_bind_text(mStmtPutNode, 3, name.c_str(), static_cast<int>(name.length()), SQLITE_STATIC);

        string fp;
        node->FileFingerprint::serialize(&fp);
        sqlite3_bind_blob(mStmtPutNode, 4, fp.data(), static_cast<int>(fp.size()), SQLITE_STATIC);

        std::string origFingerprint;
        attr_map::const_iterator attrIt = node->attrs.map.find(makeNameid("c0"));
        if (attrIt != node->attrs.map.end())
        {
           origFingerprint = attrIt->second;
        }
        sqlite3_bind_blob(mStmtPutNode, 5, origFingerprint.data(), static_cast<int>(origFingerprint.size()), SQLITE_STATIC);

        sqlite3_bind_int(mStmtPutNode, 6, node->type);

        int shareType = node->getShareType();
        sqlite3_bind_int(mStmtPutNode, 7, shareType);

        // node->attrstring has value => node is encrypted
        nameid favId = AttrMap::string2nameid("fav");
        auto favIt = node->attrs.map.find(favId);
        bool fav = (favIt != node->attrs.map.end() && favIt->second == "1"); // test 'fav' attr value (only "1" is valid)
        sqlite3_bind_int(mStmtPutNode, 8, fav);
        sqlite3_bind_int64(mStmtPutNode, 9, node->ctime);
        sqlite3_bind_int64(mStmtPutNode, 10, node->mtime);
        sqlite3_bind_int64(mStmtPutNode, 11, static_cast<sqlite3_int64>(node->getDBFlags()));
        std::string nodeCountersBlob = node->getCounter().serialize();
        sqlite3_bind_blob(mStmtPutNode,
                          12,
                          nodeCountersBlob.data(),
                          static_cast<int>(nodeCountersBlob.size()),
                          SQLITE_STATIC);
        sqlite3_bind_blob(mStmtPutNode,
                          13,
                          nodeSerialized.data(),
                          static_cast<int>(nodeSerialized.size()),
                          SQLITE_STATIC);

        static nameid labelId = AttrMap::string2nameid("lbl");
        auto labelIt = node->attrs.map.find(labelId);
        int label = (labelIt == node->attrs.map.end()) ? LBL_UNKNOWN : std::atoi(labelIt->second.c_str());
        sqlite3_bind_int(mStmtPutNode, 14, label);

        nameid descriptionId = AttrMap::string2nameid(MegaClient::NODE_ATTRIBUTE_DESCRIPTION);
        if (auto descriptionIt = node->attrs.map.find(descriptionId);
            descriptionIt != node->attrs.map.end())
        {
            const std::string& description = descriptionIt->second;
            sqlite3_bind_text(mStmtPutNode,
                              15,
                              description.c_str(),
                              static_cast<int>(description.length()),
                              SQLITE_STATIC);
        }
        else
        {
            sqlite3_bind_null(mStmtPutNode, 15);
        }

        nameid tagId = AttrMap::string2nameid(MegaClient::NODE_ATTRIBUTE_TAGS);
        if (auto tagIt = node->attrs.map.find(tagId); tagIt != node->attrs.map.end())
        {
            const std::string& tag = tagIt->second;
            sqlite3_bind_text(mStmtPutNode,
                              16,
                              tag.c_str(),
                              static_cast<int>(tag.length()),
                              SQLITE_STATIC);
        }
        else
        {
            sqlite3_bind_null(mStmtPutNode, 16);
        }

        sqlResult = sqlite3_step(mStmtPutNode);
    }

    errorHandler(sqlResult, "Put node", false);

    sqlite3_reset(mStmtPutNode);

    return sqlResult == SQLITE_DONE;
}

bool SqliteAccountState::getNode(NodeHandle nodehandle, NodeSerialized &nodeSerialized)
{
    bool success = false;
    if (!db)
    {
        return success;
    }

    nodeSerialized.mNode.clear();

    int sqlResult = SQLITE_OK;
    if (!mStmtGetNode)
    {
        sqlResult = sqlite3_prepare_v2(db, "SELECT counter, node FROM nodes  WHERE nodehandle = ?", -1, &mStmtGetNode, NULL);
    }

    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int64(mStmtGetNode,
                                            1,
                                            static_cast<sqlite3_int64>(nodehandle.as8byte()))) ==
            SQLITE_OK)
        {
            if((sqlResult = sqlite3_step(mStmtGetNode)) == SQLITE_ROW)
            {
                const void* dataNodeCounter = sqlite3_column_blob(mStmtGetNode, 0);
                int sizeNodeCounter = sqlite3_column_bytes(mStmtGetNode, 0);

                const void* dataNodeSerialized = sqlite3_column_blob(mStmtGetNode, 1);
                int sizeNodeSerialized = sqlite3_column_bytes(mStmtGetNode, 1);

                if (dataNodeCounter && sizeNodeCounter && dataNodeSerialized && sizeNodeSerialized)
                {
                    nodeSerialized.mNodeCounter.assign(static_cast<const char*>(dataNodeCounter),
                                                       static_cast<size_t>(sizeNodeCounter));
                    nodeSerialized.mNode.assign(static_cast<const char*>(dataNodeSerialized),
                                                static_cast<size_t>(sizeNodeSerialized));
                    success = true;
                }
            }
        }
    }

    if (sqlResult != SQLITE_ROW && sqlResult != SQLITE_DONE)
    {
        errorHandler(sqlResult, "Get node", false);
    }

    sqlite3_reset(mStmtGetNode);

    return success;
}


bool SqliteAccountState::getNodesByOrigFingerprint(const std::string &fingerprint, std::vector<std::pair<NodeHandle, NodeSerialized>> &nodes)
{
    if (!db)
    {
        return false;
    }

    int sqlResult = SQLITE_OK;
    if (!mStmtNodeByOrigFp)
    {
        sqlResult = sqlite3_prepare_v2(db, "SELECT nodehandle, counter, node FROM nodes WHERE origfingerprint = ?", -1, &mStmtNodeByOrigFp, NULL);
    }

    bool result = false;
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_blob(mStmtNodeByOrigFp, 1, fingerprint.data(), (int)fingerprint.size(), SQLITE_STATIC)) == SQLITE_OK)
        {
            result = processSqlQueryNodes(mStmtNodeByOrigFp, nodes);
        }
    }

    errorHandler(sqlResult, "Get node by orig fingerprint", false);

    sqlite3_reset(mStmtNodeByOrigFp);

    return result;
}

bool SqliteAccountState::getRootNodes(std::vector<std::pair<NodeHandle, NodeSerialized>> &nodes)
{
    if (!db)
    {
        return false;
    }

    sqlite3_stmt *stmt = nullptr;
    bool result = false;
    int sqlResult = sqlite3_prepare_v2(db, "SELECT nodehandle, counter, node FROM nodes WHERE type >= ? AND type <= ?", -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int(stmt, 1, nodetype_t::ROOTNODE)) == SQLITE_OK)
        {
            if ((sqlResult = sqlite3_bind_int(stmt, 2, nodetype_t::RUBBISHNODE)) == SQLITE_OK)
            {
                result = processSqlQueryNodes(stmt, nodes);
            }
        }
    }

    errorHandler(sqlResult, "Get root nodes", false);

    sqlite3_finalize(stmt);

    return result;
}

bool SqliteAccountState::getNodesWithSharesOrLink(std::vector<std::pair<NodeHandle, NodeSerialized>> &nodes, ShareType_t shareType)
{
    if (!db)
    {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    int sqlResult = SQLITE_OK;
    // The integers for the expresion "share IN (x, y, z,...)" are the decimal representation of
    // the possible combinations of ShareType_t binary map values.
    // For example 10 coresponds to IN_SHARES = 0x01 + LINK = 0x08 and 12 corresponds to
    // PENDING_OUTSHARES = 0x04 + LINK = 0x08.
    static constexpr auto sqlQueryInshares =
        "SELECT nodehandle, counter, node FROM nodes WHERE share IN (1,3,5,7,9,11,13,15)";
    static constexpr auto sqlQueryOutshares =
        "SELECT nodehandle, counter, node FROM nodes WHERE share IN (2,3,6,7,10,11,14,15)";
    static constexpr auto sqlQueryPendingOutshares =
        "SELECT nodehandle, counter, node FROM nodes WHERE share IN (4,5,6,7,12,13,14,15)";
    static constexpr auto sqlQueryPubLink =
        "SELECT nodehandle, counter, node FROM nodes WHERE share IN (8,9,10,11,12,13,14,15)";

    switch (shareType)
    {
        case ShareType_t::IN_SHARES:
            if (!mStmtNodesWithInshares)
            {
                sqlResult =
                    sqlite3_prepare_v2(db, sqlQueryInshares, -1, &mStmtNodesWithInshares, nullptr);
            }
            stmt = mStmtNodesWithInshares;
            break;
        case ShareType_t::OUT_SHARES:
            if (!mStmtNodesWithOutshares)
            {
                sqlResult = sqlite3_prepare_v2(db,
                                               sqlQueryOutshares,
                                               -1,
                                               &mStmtNodesWithOutshares,
                                               nullptr);
            }
            stmt = mStmtNodesWithOutshares;
            break;
        case ShareType_t::PENDING_OUTSHARES:
            if (!mStmtNodesWithPendingOutshares)
            {
                sqlResult = sqlite3_prepare_v2(db,
                                               sqlQueryPendingOutshares,
                                               -1,
                                               &mStmtNodesWithPendingOutshares,
                                               nullptr);
            }
            stmt = mStmtNodesWithPendingOutshares;
            break;
        case ShareType_t::LINK:
            if (!mStmtNodesWithPubLink)
            {
                sqlResult =
                    sqlite3_prepare_v2(db, sqlQueryPubLink, -1, &mStmtNodesWithPubLink, nullptr);
            }
            stmt = mStmtNodesWithPubLink;
            break;
        default:
            return false;
    }

    bool result = false;
    if (sqlResult == SQLITE_OK && stmt)
    {
        result = processSqlQueryNodes(stmt, nodes);
    }

    errorHandler(sqlResult, "Get nodes with shares or link", false);

    if (stmt)
    {
        sqlite3_reset(stmt);
    }

    return result;
}

uint64_t SqliteAccountState::getNumberOfChildren(NodeHandle parentHandle)
{
    if (!db)
    {
        return false;
    }

    uint64_t numChildren = 0;
    int sqlResult = SQLITE_OK;
    if (!mStmtNumChildren)
    {
        sqlResult = sqlite3_prepare_v2(db, "SELECT count(*) FROM nodes WHERE parenthandle = ?", -1, &mStmtNumChildren, NULL);
    }

    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int64(mStmtNumChildren,
                                            1,
                                            static_cast<sqlite3_int64>(parentHandle.as8byte()))) ==
            SQLITE_OK)
        {
            if ((sqlResult = sqlite3_step(mStmtNumChildren)) == SQLITE_ROW)
            {
                numChildren = static_cast<uint64_t>(sqlite3_column_int64(mStmtNumChildren, 0));
            }
        }
    }

    errorHandler(sqlResult, "Get number of children", false);

    sqlite3_reset(mStmtNumChildren);

    return numChildren;
}

namespace
{
/**
 * @class QueryTagId
 * @brief Helper struct to deal with sqlite statement place holders (e.g. ?10)
 *
 */
struct QueryTagId
{
    explicit QueryTagId(int id):
        mId{id}
    {}

    operator int() const
    {
        return mId;
    }

    operator std::string() const
    {
        return "?" + std::to_string(mId);
    }

    std::string operator+(const std::string& other)
    {
        return std::string(*this) + other;
    }

    friend std::string operator+(const std::string& lhs, const QueryTagId& rhs)
    {
        return lhs + std::string(rhs);
    }

private:
    int mId;
};

// Helper function for binding values like int, int64
template<typename T, typename V>
int bindValue(int& sqlResult,
              sqlite3_stmt* stmt,
              int index,
              V value,
              int (*bindFunc)(sqlite3_stmt*, int, T))
{
    if constexpr (std::is_integral_v<T> && std::is_integral_v<V>)
    {
        // Allow casting between integral types
        static_assert(sizeof(T) >= sizeof(V),
                      "Target type T must be able to hold value of type V without truncation");
    }
    if (sqlResult == SQLITE_OK)
    {
        sqlResult = bindFunc(stmt, index, static_cast<T>(value));
    }
    return sqlResult;
}

// Helper function for binding text
int bindText(int& sqlResult, sqlite3_stmt* stmt, int index, const std::string& text)
{
    if (sqlResult == SQLITE_OK)
    {
        sqlResult = sqlite3_bind_text(stmt,
                                      index,
                                      text.c_str(),
                                      static_cast<int>(text.size()),
                                      SQLITE_STATIC);
    }
    return sqlResult;
}

// Helper function for binding pointers
int bindPointer(int& sqlResult, sqlite3_stmt* stmt, int index, void* ptr, const char* type)
{
    if (sqlResult == SQLITE_OK)
    {
        sqlResult = sqlite3_bind_pointer(stmt, index, ptr, type, nullptr);
    }
    return sqlResult;
}
}

bool SqliteAccountState::getChildren(const mega::NodeSearchFilter& filter,
                                     int order,
                                     vector<pair<NodeHandle, NodeSerialized>>& children,
                                     CancelToken cancelFlag,
                                     const NodeSearchPage& page,
                                     const bool skipVersions)
{
    if (!db)
        return false;

    if (cancelFlag.exists())
        sqlite3_progress_handler(db,
                                 NUM_VIRTUAL_MACHINE_INSTRUCTIONS,
                                 SqliteAccountState::progressHandler,
                                 static_cast<void*>(&cancelFlag));

    // There are multiple criteria used in ORDER BY clause.
    // For every order type a new statement is created
    const size_t cacheId = OrderByClause::getId(order);
    sqlite3_stmt*& stmt = mStmtGetChildren[cacheId];

    int sqlResult = SQLITE_OK;
    static const QueryTagId idParentHand{1};
    static const QueryTagId idPageSize{2};
    static const QueryTagId idPageOff{3};
    static const QueryTagId idFilter{4};
    static const QueryTagId idVerFlag{5};
    if (!stmt)
    {
        // Inherited sensitivity is not a concern here. When filtering out sensitive nodes, the
        // parent of all children would be checked before getting here. There's no point in making
        // this query recursive just because of that.

        using namespace std::string_literals;
        // Disabling format for query readability
        // clang-format off
        const std::string sqlQuery =
            "SELECT nodehandle, counter, node "s +
            "FROM nodes "
            "WHERE (parenthandle = " + idParentHand + ") "
            "AND (flags & " + idVerFlag + ") = 0 " // bound to versionFlag to skip, or 0 to include
            "AND matchFilter(" + idFilter + ", flags, type, ctime, mtime, mimetypeVirtual, name, description, tags, fav)"
            "ORDER BY \n" +
            OrderByClause::get(order) + " \n" +
            "LIMIT " + idPageSize + " OFFSET " + idPageOff;
        // clang-format on

        sqlResult = sqlite3_prepare_v2(db, sqlQuery.c_str(), -1, &stmt, NULL);
    }

    bool result = false;

    const sqlite3_int64 pageSize = page.size() ? static_cast<sqlite3_int64>(page.size()) : -1;
    NodeSearchFilter filterCopy = filter;
    constexpr int64_t versionFlag = 1 << Node::FLAGS_IS_VERSION;
    const int64_t verMask = skipVersions ? versionFlag : 0;

    bindPointer(sqlResult, stmt, idFilter, &filterCopy, NodeSearchFilterPtrStr);
    bindValue(sqlResult, stmt, idParentHand, filter.byParentHandle(), sqlite3_bind_int64);
    bindValue(sqlResult, stmt, idPageSize, pageSize, sqlite3_bind_int64);
    bindValue(sqlResult, stmt, idPageOff, page.startingOffset(), sqlite3_bind_int64);
    bindValue(sqlResult, stmt, idVerFlag, verMask, sqlite3_bind_int64);

    if (sqlResult == SQLITE_OK)
        result = processSqlQueryNodes(stmt, children);

    // unregister the handler (no-op if not registered)
    sqlite3_progress_handler(db, -1, nullptr, nullptr);

    errorHandler(sqlResult, "Get children with filter", true);

    sqlite3_reset(stmt);

    return result;
}

bool SqliteAccountState::listChildNodesLexicographically(
    const handle parenthandle,
    std::vector<std::pair<NodeHandle, NodeSerialized>>& children,
    CancelToken cancelFlag,
    const size_t maxElements,
    const std::optional<NodeSearchLexicographicalOffset>& offset)
{
    if (!db)
        return false;

    if (cancelFlag.exists())
        sqlite3_progress_handler(db,
                                 NUM_VIRTUAL_MACHINE_INSTRUCTIONS,
                                 SqliteAccountState::progressHandler,
                                 static_cast<void*>(&cancelFlag));

    // There are multiple criteria used in ORDER BY clause.
    // For every order type a new statement is created

    int sqlResult = SQLITE_OK;
    static const QueryTagId idParentHand{1};
    static const QueryTagId idPageOffName{2};
    static const QueryTagId idPageSize{3};
    static const QueryTagId idPageOffHandle{4};

    sqlite3_stmt*& stmt = offset ? mStmtGetChildrenLexi : mStmtGetChildrenLexiNoOffset;
    if (!stmt)
    {
        // clang-format off
        // Order by the effective S3 key (folder = name + '/') to match S3 key order, not raw name.
        // The trailing '/' folds type into the key, so the seek tiebreak is just (s3key, nodehandle).
        const std::string offsetWhere = offset ?
             "AND ((s3keyVirtual > "s + idPageOffName + ") OR " +
                  "(s3keyVirtual = "  + idPageOffName + " AND nodehandle > " + idPageOffHandle + "))"
             : "";
        const std::string sqlQuery =
            "SELECT nodehandle, counter, node "s +
            "FROM nodes "
            "WHERE (parenthandle = " + idParentHand + ") " // Versions aren't taken in consideration
            + offsetWhere +
            "ORDER BY s3keyVirtual, nodehandle\n"
            "LIMIT " + idPageSize;
        // clang-format on
        sqlResult = sqlite3_prepare_v2(db, sqlQuery.c_str(), -1, &stmt, NULL);
    }

    const sqlite3_int64 pageSize = maxElements == 0 ? -1 : static_cast<sqlite3_int64>(maxElements);
    bindValue(sqlResult, stmt, idPageSize, pageSize, sqlite3_bind_int64);
    bindValue(sqlResult, stmt, idParentHand, parenthandle, sqlite3_bind_int64);
    if (offset)
    {
        // Tests engaged-ness, not value: an inclusive seek sets mLastHandle = 0 -> binds 0 ->
        // "nodehandle > 0", including the node whose key == mLastName; unset binds MAX (strictly
        // after the key).
        const auto lastHandle = offset->mLastHandle ?
                                    static_cast<sqlite3_int64>(*offset->mLastHandle) :
                                    std::numeric_limits<sqlite3_int64>::max();
        bindValue(sqlResult, stmt, idPageOffHandle, lastHandle, sqlite3_bind_int64);
        bindText(sqlResult, stmt, idPageOffName, offset->mLastName);
    }

    bool result = false;
    if (sqlResult == SQLITE_OK)
        result = processSqlQueryNodes(stmt, children);

    // unregister the handler (no-op if not registered)
    sqlite3_progress_handler(db, -1, nullptr, nullptr);

    errorHandler(sqlResult, "List child nodes with Lexicographical order", true);

    sqlite3_reset(stmt);

    return result;
}

auto SqliteAccountState::getNodeTagsBelow(CancelToken cancelToken,
                                          NodeHandle handle,
                                          const std::string& pattern)
    -> std::optional<std::set<std::string>>
{
    // Convenience.
    static auto failed = [](const std::string& message)
    {
        LOG_err << "SqliteAccountState::getNodeTagsBelow: " << message;
        return std::nullopt;
    }; // failed

    static auto couldntBindParameter = [](auto index)
    {
        static const std::string message = "Couldn't bind parameter ?";
        return failed(message + std::to_string(index));
    }; // couldntBindParameter

    // Our database isn't in a usable state.
    if (!db)
        return failed("Invalid database");

    // Transmit to global error handler on return.
    auto result = SQLITE_OK;

    // Transmits our result to the global error handler on return.
    auto cleanup = makeScopedDestructor(
        [&]()
        {
            // Remove any active progress handler.
            sqlite3_progress_handler(db, -1, nullptr, nullptr);
            // Transmit result to global error handler.
            errorHandler(result, "Get node tags below", true);
            // Make sure our statement's in a reusable state.
            sqlite3_reset(mStmtNodeTagsBelow);
        }); // cleanup

    // Caller wants to be able to abort the query.
    if (cancelToken.exists())
    {
        sqlite3_progress_handler(db,
                                 NUM_VIRTUAL_MACHINE_INSTRUCTIONS,
                                 SqliteAccountState::progressHandler,
                                 static_cast<void*>(&cancelToken));
    }

    // Statement needs to be instantiated.
    if (!mStmtNodeTagsBelow)
    {
        // This query retrieves all the tags below some particular node or
        // below all root nodes in the user's account by performing a
        // breadth-first traversal from those nodes.
        //
        // The way the query works is basically by populating a virtual
        // table repeatedly. For instance, in the first step, we ask which
        // nodes are below a particular node. That'll produce a result set
        // containing that node's direct children.
        //
        // The engine then performs the same query on that exact result set
        // which will produce a result set containing the descendants of
        // those children.
        //
        // The process repeats until there are no more directories to
        // traverse into.
        //
        // Note that this query does not descend down file version chains.
        auto query = "with recursive tags (nodehandle, tags, type) as ( "
                     "select n.nodehandle "
                     "     , n.tags "
                     "     , n.type "
                     "  from nodes as n "
                     " where ((?1 = 0 and n.parenthandle = -1) "
                     "        or (?1 = 1 and n.nodehandle = ?2)) "
                     "    and n.type != 0 "
                     " union "
                     "select n.nodehandle "
                     "     , n.tags "
                     "     , n.type "
                     "  from tags as t "
                     " inner join nodes as n "
                     "    on n.parenthandle = t.nodehandle "
                     "   and t.type != 0 "
                     " where n.type != 0 "
                     "    or n.tags is not null "
                     "   and n.tags != '' "
                     ") "
                     "select distinct "
                     "       tags "
                     "  from tags "
                     " where tags is not null "
                     "   and tags != '' "
                     "   and (?3 = 0 or tags regexp ?4)";

        // Try and instantiate our statement.
        result = sqlite3_prepare_v2(db, query, -1, &mStmtNodeTagsBelow, nullptr);

        // Couldn't instantiate statement.
        if (result != SQLITE_OK)
            return failed("Couldn't prepare query");
    }

    // Clarity.
    enum ParameterIndex
    {
        PARAM_HAS_NODE_HANDLE = 1,
        PARAM_NODE_HANDLE,
        PARAM_HAS_PATTERN,
        PARAM_PATTERN
    }; // ParameterIndex

    // Let the query know if we have a search root.
    result = sqlite3_bind_int64(mStmtNodeTagsBelow, PARAM_HAS_NODE_HANDLE, !handle.isUndef());

    // Couldn't bind parameter.
    if (result != API_OK)
        return couldntBindParameter(PARAM_HAS_NODE_HANDLE);

    // Let the query know which node we're searching below.
    result = sqlite3_bind_int64(mStmtNodeTagsBelow,
                                PARAM_NODE_HANDLE,
                                static_cast<std::int64_t>(handle.as8byte()));

    // Couldn't bind parameter.
    if (result != API_OK)
        return couldntBindParameter(PARAM_NODE_HANDLE);

    // Generate effective pattern.
    auto effectivePattern = ensureAsteriskSurround(pattern);

    // Let the query know if the caller's provided a pattern.
    result = sqlite3_bind_int(mStmtNodeTagsBelow, PARAM_HAS_PATTERN, !pattern.empty());

    // Couldn't bind parameter.
    if (result != API_OK)
        return couldntBindParameter(PARAM_HAS_PATTERN);

    // Let the query know what the pattern is.
    result = sqlite3_bind_text(mStmtNodeTagsBelow,
                               PARAM_PATTERN,
                               effectivePattern.c_str(),
                               static_cast<int>(effectivePattern.size()),
                               SQLITE_STATIC);

    // Couldn't bind parameter.
    if (result != API_OK)
        return couldntBindParameter(PARAM_PATTERN);

    std::set<std::string> tags;

    // Process each result row.
    while (result != SQLITE_DONE)
    {
        // Try and retrieve a row from the database.
        result = sqlite3_step(mStmtNodeTagsBelow);

        // Couldn't get a row from the database.
        if (result != SQLITE_DONE && result != SQLITE_ROW)
            return failed("Couldn't retrieve row from database");

        // Get our hands on this node's delimited list of tags.
        auto* data = reinterpret_cast<const char*>(sqlite3_column_blob(mStmtNodeTagsBelow, 0));

        // How large is the node's delimited list of tags?
        auto size = static_cast<std::size_t>(sqlite3_column_bytes(mStmtNodeTagsBelow, 0));

        // Delimited list of tags is null or empty.
        if (!data || !size)
            continue;

        // Separate individual tags.
        auto individualTags =
            splitString<decltype(tags)>(std::string(data, size), MegaClient::TAG_DELIMITER);

        // Collect the tags that satisfy our pattern.
        for (auto i = individualTags.begin(); i != individualTags.end();)
        {
            // Convenience.
            auto j = i++;

            // Not interested in this node.
            if (!pattern.empty() && !likeCompare(effectivePattern.c_str(), j->c_str(), false))
                continue;

            // Move tag into tags set.
            tags.insert(individualTags.extract(j));
        }
    }

    // Return tags to caller.
    return std::optional<decltype(tags)>(std::in_place, std::move(tags));
}

bool SqliteAccountState::searchNodes(const NodeSearchFilter& filter,
                                     int order,
                                     vector<pair<NodeHandle, NodeSerialized>>& nodes,
                                     CancelToken cancelFlag,
                                     const NodeSearchPage& page)
{
    if (!db)
        return false;

    if (cancelFlag.exists())
        sqlite3_progress_handler(db,
                                 NUM_VIRTUAL_MACHINE_INSTRUCTIONS,
                                 SqliteAccountState::progressHandler,
                                 static_cast<void*>(&cancelFlag));

    // There are multiple criteria used in ORDER BY clause.
    // For every order type a new statement is created
    size_t cacheId = OrderByClause::getId(order);
    sqlite3_stmt*& stmt = mStmtSearchNodes[cacheId];

    static const QueryTagId idVerFlag{1};
    static const QueryTagId idName{2};
    static const QueryTagId idAncestor1{3};
    static const QueryTagId idAncestor2{4};
    static const QueryTagId idAncestor3{5};
    static const QueryTagId idPageSize{6};
    static const QueryTagId idPageOff{7};
    static const QueryTagId idSens{8};
    static const QueryTagId idSensFlag{9};
    static const QueryTagId idIncShares{10};
    static const QueryTagId idFilter{11};

    int sqlResult = SQLITE_OK;
    if (!stmt)
    {
        // Handful string conversions
        static const std::string undefStr{std::to_string(static_cast<sqlite3_int64>(UNDEF))};
        static const std::string noShareStr{std::to_string(NO_SHARES)};
        static const std::string onlyTrueStr =
            std::to_string(static_cast<int>(NodeSearchFilter::BoolFilter::onlyTrue));
        static const std::string filenodeStr = std::to_string(FILENODE);

        // Columns for the SELECT
        static const std::vector<std::string> columnsForNodeAndFiltersVec = {"nodehandle",
                                                                             "parenthandle",
                                                                             "flags",
                                                                             "name",
                                                                             "type",
                                                                             "counter",
                                                                             "node",
                                                                             "sizeVirtual",
                                                                             "ctime",
                                                                             "mtime",
                                                                             "share",
                                                                             "mimetypeVirtual",
                                                                             "fav",
                                                                             "label",
                                                                             "description",
                                                                             "tags"};
        // Output: "nodehandle, parenthandle, flags, ..."
        static const std::string columnsForNodeAndFilters =
            joinStrings(std::cbegin(columnsForNodeAndFiltersVec),
                        std::cend(columnsForNodeAndFiltersVec),
                        ", ");

        // Output: "N.nodehandle, N.parenthandle, N.flags, ..."
        static const std::string columnsForNodeAndFiltersPrefixN =
            joinStrings(std::cbegin(columnsForNodeAndFiltersVec),
                        std::cend(columnsForNodeAndFiltersVec),
                        ", ",
                        [](const std::string& n) -> std::string
                        {
                            return "N." + n;
                        });

        static const std::string columnsForNodeAndOrderBy =
            "nodehandle, counter, node, " // for nodes
            "type, sizeVirtual, ctime, mtime, name, label, fav"; // for ORDER BY only

        using namespace std::string_literals;

        // Disabling format for query readability
        // clang-format off
        static const std::string ancestors =
            "ancestors(nodehandle) \n"s
            "AS (SELECT nodehandle FROM nodes \n"
                "WHERE (" + idAncestor1 + " != " + undefStr + " AND nodehandle = " + idAncestor1 + ") "
                "OR (" + idAncestor2 + " != " + undefStr + " AND nodehandle = " + idAncestor2 + ") "
                "OR (" + idAncestor3 + " != " + undefStr + " AND nodehandle = " + idAncestor3 + ") "
                "OR (" + idIncShares + " != " + noShareStr + " AND type != " + filenodeStr + " AND share & " + idIncShares + " != 0))";

        static const std::string nodesOfShares =
            "nodesOfShares(" + columnsForNodeAndFilters + ") \n"
            "AS (SELECT " + columnsForNodeAndFilters + " \n"
                "FROM nodes \n"
                "WHERE parenthandle NOT IN (SELECT nodehandle FROM ancestors) AND "
                + idIncShares + " != " + noShareStr + " AND share & " + idIncShares + " != 0 "
                "AND (flags & " + idVerFlag + " = 0))"; // Versions aren't taken in consideration

        static const std::string nodesCTE =
            "nodesCTE(" + columnsForNodeAndFilters + ") \n"
            "AS (SELECT " + columnsForNodeAndFilters + " \n"
                "FROM nodes \n"
                "WHERE parenthandle IN (SELECT nodehandle FROM ancestors) \n"
                "UNION ALL \n"
                "SELECT " + columnsForNodeAndFiltersPrefixN + " \n"
                "FROM nodes AS N \n"
                "INNER JOIN nodesCTE AS P \n"
                "ON (N.parenthandle = P.nodehandle \n"
                "AND (P.flags & " + idVerFlag + " = 0) \n" // Versions aren't taken in consideration
                "AND (" + idSens + " != " + onlyTrueStr + // Sensitive nodes
                " OR " + idSens + " = " + onlyTrueStr +
                " AND (P.flags & " + idSensFlag + ") = 0) "
                "AND P.type != " + filenodeStr + "))";

        static const std::string whereClause =
            "matchFilter("s + idFilter +
            ", flags, type, ctime, mtime, mimetypeVirtual, name, description, tags, fav)";

        static const std::string nodesAfterFilters =
            "nodesAfterFilters (" + columnsForNodeAndOrderBy + ") \n"
            "AS (SELECT " + columnsForNodeAndOrderBy + " \n"
                "FROM nodesOfShares \n"
                "WHERE " + whereClause + " \n"
                "UNION ALL \n"
                "SELECT " + columnsForNodeAndOrderBy + " \n"
                "FROM nodesCTE \n"
                "WHERE " + whereClause +
                // avoid duplicates (should be faster than SELECT DISTINCT, but possibly require more memory)
                "GROUP BY nodehandle)";

        /// recursive query considering ancestors
        const std::string query =
            "WITH \n\n" +
            ancestors + ", \n\n" +
            nodesOfShares + ", \n\n" +
            nodesCTE + ", \n\n" +
            nodesAfterFilters + "\n\n" +
            "SELECT " + columnsForNodeAndOrderBy + " \n"
            "FROM nodesAfterFilters GROUP BY nodehandle\n" // Avoid duplicates after union of nodesOfShares and nodesCTE
            "ORDER BY \n" +
            OrderByClause::get(order) + " \n" +
            "LIMIT " + idPageSize + " OFFSET " + idPageOff;
        // clang-format on

        sqlResult = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, NULL);
    }

    constexpr uint64_t versionFlag = (1 << Node::FLAGS_IS_VERSION); // exclude file versions
    constexpr uint64_t sensitivityFlag = 1 << Node::FLAGS_IS_MARKED_SENSITIVE; // by sensitivity

    const auto& ancestors = filter.byAncestorHandles();
    const sqlite3_int64 pageSize = page.size() ? static_cast<sqlite3_int64>(page.size()) : -1;
    assert(ancestors.size() >= 3); // support at least 3 ancestors
    NodeSearchFilter filterCopy = filter;

    bindValue(sqlResult, stmt, idVerFlag, versionFlag, sqlite3_bind_int64);
    bindValue(sqlResult, stmt, idIncShares, filter.includedShares(), sqlite3_bind_int);
    bindText(sqlResult, stmt, idName, filter.byName());
    bindValue(sqlResult, stmt, idAncestor1, ancestors[0], sqlite3_bind_int64);
    bindValue(sqlResult, stmt, idAncestor2, ancestors[1], sqlite3_bind_int64);
    bindValue(sqlResult, stmt, idAncestor3, ancestors[2], sqlite3_bind_int64);
    bindValue(sqlResult, stmt, idPageSize, pageSize, sqlite3_bind_int64);
    bindValue(sqlResult, stmt, idPageOff, page.startingOffset(), sqlite3_bind_int64);
    bindPointer(sqlResult, stmt, idFilter, &filterCopy, NodeSearchFilterPtrStr);
    bindValue(sqlResult, stmt, idSens, filter.bySensitivity(), sqlite3_bind_int);
    bindValue(sqlResult, stmt, idSensFlag, sensitivityFlag, sqlite3_bind_int64);

    const bool result = (sqlResult == SQLITE_OK) && processSqlQueryNodes(stmt, nodes);

    // unregister the handler (no-op if not registered)
    sqlite3_progress_handler(db, -1, nullptr, nullptr);

    errorHandler(sqlResult, "Search nodes with filter", true);

    sqlite3_reset(stmt);

    return result;
}

// ---------------------------------------------------------------------------
// SQL + cache-key helpers for listAllNodesByPage and groupAllNodesByDate.
// Shared infrastructure (cache-key digit bases, CacheKeyBuilder, subtree-scope
// SQL) sits up top; the date-section-specific helpers follow.
// ---------------------------------------------------------------------------

namespace
{

// ORDER BY clause for listAllNodesByPage.
std::string buildOrderByForListAll(int order)
{
    static const std::string nA = "name COLLATE NATURALNOCASE ASC";
    static const std::string nD = "name COLLATE NATURALNOCASE DESC";
    static const std::string hA = "nodehandle ASC";
    static const std::string hD = "nodehandle DESC";

    switch (order)
    {
        case OrderByClause::DEFAULT_ASC:
            return nA + ", " + hA;
        case OrderByClause::DEFAULT_DESC:
            return nD + ", " + hD;
        case OrderByClause::SIZE_ASC:
            return "sizeVirtual ASC, " + nA + ", " + hA;
        case OrderByClause::SIZE_DESC:
            return "sizeVirtual DESC, " + nD + ", " + hD;
        case OrderByClause::MTIME_ASC:
            return "mtime ASC, " + nA + ", " + hA;
        case OrderByClause::MTIME_DESC:
            return "mtime DESC, " + nD + ", " + hD;
        case OrderByClause::FAV_ASC:
            return "fav DESC, " + nA + ", " + hA;
        case OrderByClause::FAV_DESC:
            return "fav ASC, " + nA + ", " + hA;
        case OrderByClause::LABEL_ASC:
            return "CASE WHEN label = 0 THEN 1 ELSE 0 END ASC, label ASC, " + nA + ", " + hA;
        case OrderByClause::LABEL_DESC:
            return "label DESC, " + nA + ", " + hA;
        default:
            return nA + ", " + hA;
    }
}

// Parameter slot layout for listAllNodesByPage:
//   ?1                              = LIMIT (always)
//   ?2                              = OFFSET (always)
//   ?3                              = innerLimit (grouped CTEs only; always bound, unused for
//                                     single-mime queries which do not reference ?3 in SQL)
//   ?4                              = mimeFilter      (omitted for grouped mime types)
//   ?filesRootParam     .. +N-1     = filesRoots      (N = numRoots ≥ 1)
//   ?excludeHandleParam .. +M-1     = excludeHandles  (M = numExcludes, may be 0)
//   ?timestampAnchorParam           = mStartSeconds (ASC) or mEndSeconds (DESC),
//                                     scaled by unitsPerSecond (only if hasTimestampAnchor)
//   ?cursorStartParam onward        = cursor fields   (only if hasCursor)
//
// Grouped mime types (ALL_DOCS, ALL_VISUAL_MEDIA) bake the mimetype as a literal in
// per-route CTEs, so they do not consume the mimeFilter slot (?4). The per-route CTEs
// use ?3 (innerLimit = offset+pageSize) so that the outer UNION ALL merge receives
// enough rows from each route; OFFSET ?2 is applied only at the outermost query.

static const QueryTagId kLaIdPageSize{1};
static const QueryTagId kLaIdOffset{2}; // always-present OFFSET slot
static const QueryTagId kLaIdInnerLimit{
    3}; // per-route CTE LIMIT for grouped mimes (= offset+pageSize)

inline bool isAscOrder(int order)
{
    switch (order)
    {
        case OrderByClause::DEFAULT_ASC:
        case OrderByClause::SIZE_ASC:
        case OrderByClause::CTIME_ASC:
        case OrderByClause::MTIME_ASC:
        case OrderByClause::LABEL_ASC:
        case OrderByClause::FAV_ASC:
            return true;
        default:
            return false;
    }
}

// Upper bounds on the filesRoots and excludeHandles IN-lists embedded in the SQL;
// also cap cache-key packing in computeListAllCacheId / computeDateSectionsCacheId.
constexpr size_t kListAllMaxRoots = kListAllMaxLocationHandles;
constexpr size_t kListAllMaxExcludes = kListAllMaxLocationHandles;

constexpr size_t kListAllOrderStride = static_cast<size_t>(OrderByClause::LAST) + 1;
constexpr size_t kMimeTypeCount = static_cast<size_t>(MIME_TYPE_MAX) + 1;
constexpr size_t kFileSubTypeStride = static_cast<size_t>(FILE_SUBTYPE_MAX) + 1;
constexpr size_t kAnchorDirectionStride = static_cast<size_t>(AnchorDirectionDigit::Max) + 1;
constexpr size_t kDateSectionGranularityStride =
    static_cast<size_t>(DateSectionGranularity::Max) + 1;

// Cache-key space upper bound; assert it stays within 32 bits (size_t is 32-bit on
// armv7) if a base grows.
constexpr size_t kListAllMaxCacheKey = kMimeTypeCount * kFileSubTypeStride * kListAllOrderStride *
                                       2 /* hasCursor */ * kAnchorDirectionStride *
                                       2 /* excludeSensitive */ * kListAllMaxRoots *
                                       (kListAllMaxExcludes + 1);
static_assert(kListAllMaxCacheKey < (uint64_t{1} << 32),
              "cache-key product no longer fits in 32 bits; revisit bounds");

// Same bound for the date-section key (granularity digit replaces hasCursor + anchorDir).
constexpr size_t kDateSectionMaxCacheKey =
    kMimeTypeCount * kFileSubTypeStride * kListAllOrderStride * kDateSectionGranularityStride *
    2 /* excludeSensitive */ * kListAllMaxRoots * (kListAllMaxExcludes + 1);
static_assert(kDateSectionMaxCacheKey < (uint64_t{1} << 32),
              "date-section cache-key product no longer fits in 32 bits; revisit bounds");

// CacheKeyBuilder: positional-number digit packing. Each `append(value, base)`
// does `mKey = mKey * base + value` and asserts `value < base` — the single
// enforcement point for the no-aliasing invariant (a digit >= its base carries
// into the next and collides with another shape's cache slot). Pass each digit's
// true cardinality as `base`. Under NDEBUG the assert is stripped; upper layers
// (MegaApiImpl::buildListAllParams / buildDateSectionParams) reject out-of-range
// inputs first.
class CacheKeyBuilder
{
public:
    constexpr CacheKeyBuilder() noexcept = default;

    constexpr CacheKeyBuilder& append(size_t value, size_t base) noexcept
    {
        assert(value < base && "CacheKeyBuilder digit out of range — would alias cache slots");
        mKey = mKey * base + value;
        return *this;
    }

    constexpr size_t build() const noexcept
    {
        return mKey;
    }

private:
    size_t mKey = 0;
};

// SQL-shape helpers below (timestamp-column routing, GID/bound/route expressions) stay
// internal to this TU. The cache-key functions that consume the strides and CacheKeyBuilder
// above need external linkage for the Sqlite_test regression test, so they can't sit
// inside the anonymous namespace.

// Metadata for a timeline timestamp column, used by the SQL builders + bind site
// below; extend via timestampColumnForOrder(). Internal to this TU.
//
// SQL-injection invariant: rawColumnExpr / secondsColumnExpr are concatenated into
// prepared-statement text, so they MUST be hard-coded SQL fragments ("mtime" or
// "(capture_ts / 1000)") — never user-derived data.
struct TimestampColumnDescriptor
{
    std::string rawColumnExpr; ///< column ref for range / index use (e.g. "mtime")
    std::string secondsColumnExpr; ///< wrapped to epoch seconds; "(<col>/1000)" for ms columns
    int64_t invalidSentinel; ///< rows where rawColumnExpr <= this are excluded (today: 0)
    int64_t unitsPerSecond; ///< 1 for seconds, 1000 for ms; applied at bind time
    bool descending; ///< drives ORDER BY direction in the section query
};

// Single source of truth for which OrderByClause values are supported timeline
// orders and the column / unit / direction each maps to; std::nullopt otherwise.
std::optional<TimestampColumnDescriptor> timestampColumnForOrder(int order)
{
    // ⚠️ Adding a case for a NEW column also requires extending computeListAllCacheId —
    // its anchor digit only encodes direction, which identifies the column only while
    // mtime is the sole one here.
    switch (order)
    {
        case OrderByClause::MTIME_ASC:
            return TimestampColumnDescriptor{"mtime", "mtime", 0, 1, false};
        case OrderByClause::MTIME_DESC:
            return TimestampColumnDescriptor{"mtime", "mtime", 0, 1, true};
        default:
            return std::nullopt;
    }
}

// SQL fragment for the computed isZero column used by LABEL sort orders.
static const std::string kLabelIsZeroExpr = "(CASE WHEN label = 0 THEN 1 ELSE 0 END)";

// Range pre-filter on the primary sort key(s) so SQLite can seek to the cursor row
// via an index rather than scanning everything before it. The secondary key (name)
// is included for multi-key orders to absorb ties; exact tie-breaking on nodehandle
// is left to buildCursorWhereForListAll.
// Slots used by this function:
//   DEFAULT_ASC/DESC  : p1=name
//   SIZE_ASC/DESC     : p1=size,   p2=name
//   MTIME_ASC/DESC    : p1=mtime,  p2=name
//   FAV_ASC/DESC      : p1=fav,    p2=name
//   LABEL_ASC         : p1=isZero, p2=label, p3=name
//   LABEL_DESC        : p1=label,  p2=name
std::string buildBoundingWhereForListAll(int order, int startParam)
{
    const std::string p1 = "?" + std::to_string(startParam);
    const std::string p2 = "?" + std::to_string(startParam + 1);

    switch (order)
    {
        case OrderByClause::DEFAULT_ASC:
            // p1=name; rows after cursor have name >= cursor.name
            return "name >= " + p1 + " COLLATE NATURALNOCASE";

        case OrderByClause::DEFAULT_DESC:
            // p1=name; rows after cursor have name <= cursor.name
            return "name <= " + p1 + " COLLATE NATURALNOCASE";

        case OrderByClause::MTIME_ASC:
            // p1=mtime, p2=name; ORDER BY mtime ASC, name ASC, nodehandle ASC
            return "(mtime > " + p1 + " OR (mtime = " + p1 + " AND name >= " + p2 +
                   " COLLATE NATURALNOCASE))";

        case OrderByClause::MTIME_DESC:
            // p1=mtime, p2=name; ORDER BY mtime DESC, name DESC, nodehandle DESC
            return "(mtime < " + p1 + " OR (mtime = " + p1 + " AND name <= " + p2 +
                   " COLLATE NATURALNOCASE))";

        case OrderByClause::SIZE_ASC:
            // p1=size, p2=name; ORDER BY sizeVirtual ASC, name ASC, nodehandle ASC
            return "(sizeVirtual > " + p1 + " OR (sizeVirtual = " + p1 + " AND name >= " + p2 +
                   " COLLATE NATURALNOCASE))";

        case OrderByClause::SIZE_DESC:
            // p1=size, p2=name; ORDER BY sizeVirtual DESC, name DESC, nodehandle DESC
            return "(sizeVirtual < " + p1 + " OR (sizeVirtual = " + p1 + " AND name <= " + p2 +
                   " COLLATE NATURALNOCASE))";

        case OrderByClause::FAV_ASC:
            // p1=fav, p2=name; ORDER BY fav DESC → lower fav or same fav with name >=
            return "(fav < " + p1 + " OR (fav = " + p1 + " AND name >= " + p2 +
                   " COLLATE NATURALNOCASE))";

        case OrderByClause::FAV_DESC:
            // p1=fav, p2=name; ORDER BY fav ASC → higher fav or same fav with name >=
            return "(fav > " + p1 + " OR (fav = " + p1 + " AND name >= " + p2 +
                   " COLLATE NATURALNOCASE))";

        case OrderByClause::LABEL_ASC:
        {
            // p1=isZero, p2=label, p3=name; ORDER BY isZero ASC, label ASC, name ASC
            // (isZero=0 → labelled rows first; isZero=1 → unlabelled rows last)
            const std::string p3 = "?" + std::to_string(startParam + 2);
            return "(" + kLabelIsZeroExpr + " > " + p1 + " OR (" + kLabelIsZeroExpr + " = " + p1 +
                   " AND label > " + p2 +
                   ")"
                   " OR (" +
                   kLabelIsZeroExpr + " = " + p1 + " AND label = " + p2 + " AND name >= " + p3 +
                   " COLLATE NATURALNOCASE))";
        }

        case OrderByClause::LABEL_DESC:
            // p1=label, p2=name; ORDER BY label DESC → smaller label or same label with name >=
            return "(label < " + p1 + " OR (label = " + p1 + " AND name >= " + p2 +
                   " COLLATE NATURALNOCASE))";

        default:
            return buildBoundingWhereForListAll(OrderByClause::DEFAULT_ASC, startParam);
    }
}

// Cursor WHERE clause for resuming pagination at the row after the last seen item.
// `type` is omitted since all results are FILENODEs. startParam is the first ?N slot.
// Slot layout matches bindCursorParamsForListAll:
//   DEFAULT_ASC/DESC  : p1=name,   p2=handle
//   SIZE_ASC/DESC     : p1=size,   p2=name,  p3=handle
//   MTIME_ASC/DESC    : p1=mtime,  p2=name,  p3=handle
//   FAV_ASC/DESC      : p1=fav,    p2=name,  p3=handle
//   LABEL_ASC         : p1=isZero, p2=label, p3=name, p4=handle
//   LABEL_DESC        : p1=label,  p2=name,  p3=handle
std::string buildCursorWhereForListAll(int order, int startParam)
{
    const std::string p1 = "?" + std::to_string(startParam);
    const std::string p2 = "?" + std::to_string(startParam + 1);
    const std::string p3 = "?" + std::to_string(startParam + 2);
    const std::string p4 = "?" + std::to_string(startParam + 3);

    switch (order)
    {
        case OrderByClause::DEFAULT_ASC:
            // ORDER BY name ASC, nodehandle ASC
            return "(name > " + p1 +
                   " COLLATE NATURALNOCASE"
                   " OR (name = " +
                   p1 + " COLLATE NATURALNOCASE AND nodehandle > " + p2 + "))";

        case OrderByClause::DEFAULT_DESC:
            // ORDER BY name DESC, nodehandle DESC
            return "(name < " + p1 +
                   " COLLATE NATURALNOCASE"
                   " OR (name = " +
                   p1 + " COLLATE NATURALNOCASE AND nodehandle < " + p2 + "))";

        case OrderByClause::SIZE_ASC:
            // ORDER BY sizeVirtual ASC, name ASC, nodehandle ASC
            return "(sizeVirtual > " + p1 + " OR (sizeVirtual = " + p1 + " AND name > " + p2 +
                   " COLLATE NATURALNOCASE)"
                   " OR (sizeVirtual = " +
                   p1 + " AND name = " + p2 + " COLLATE NATURALNOCASE AND nodehandle > " + p3 +
                   "))";

        case OrderByClause::SIZE_DESC:
            // ORDER BY sizeVirtual DESC, name DESC, nodehandle DESC
            return "(sizeVirtual < " + p1 + " OR (sizeVirtual = " + p1 + " AND name < " + p2 +
                   " COLLATE NATURALNOCASE)"
                   " OR (sizeVirtual = " +
                   p1 + " AND name = " + p2 + " COLLATE NATURALNOCASE AND nodehandle < " + p3 +
                   "))";

        case OrderByClause::MTIME_ASC:
            // ORDER BY mtime ASC, name ASC, nodehandle ASC
            return "(mtime > " + p1 + " OR (mtime = " + p1 + " AND name > " + p2 +
                   " COLLATE NATURALNOCASE)"
                   " OR (mtime = " +
                   p1 + " AND name = " + p2 + " COLLATE NATURALNOCASE AND nodehandle > " + p3 +
                   "))";

        case OrderByClause::MTIME_DESC:
            // ORDER BY mtime DESC, name DESC, nodehandle DESC
            return "(mtime < " + p1 + " OR (mtime = " + p1 + " AND name < " + p2 +
                   " COLLATE NATURALNOCASE)"
                   " OR (mtime = " +
                   p1 + " AND name = " + p2 + " COLLATE NATURALNOCASE AND nodehandle < " + p3 +
                   "))";

        case OrderByClause::FAV_ASC:
            // ORDER BY fav DESC, name ASC, nodehandle ASC
            return "(fav < " + p1 + " OR (fav = " + p1 + " AND name > " + p2 +
                   " COLLATE NATURALNOCASE)"
                   " OR (fav = " +
                   p1 + " AND name = " + p2 + " COLLATE NATURALNOCASE AND nodehandle > " + p3 +
                   "))";

        case OrderByClause::FAV_DESC:
            // ORDER BY fav ASC, name ASC, nodehandle ASC
            return "(fav > " + p1 + " OR (fav = " + p1 + " AND name > " + p2 +
                   " COLLATE NATURALNOCASE)"
                   " OR (fav = " +
                   p1 + " AND name = " + p2 + " COLLATE NATURALNOCASE AND nodehandle > " + p3 +
                   "))";

        case OrderByClause::LABEL_ASC:
        {
            // ORDER BY (CASE WHEN label=0 THEN 1 ELSE 0 END) ASC, label ASC, name ASC, nodehandle
            // ASC
            return "(" + kLabelIsZeroExpr + " > " + p1 + " OR (" + kLabelIsZeroExpr + " = " + p1 +
                   " AND label > " + p2 +
                   ")"
                   " OR (" +
                   kLabelIsZeroExpr + " = " + p1 + " AND label = " + p2 + " AND name > " + p3 +
                   " COLLATE NATURALNOCASE)"
                   " OR (" +
                   kLabelIsZeroExpr + " = " + p1 + " AND label = " + p2 + " AND name = " + p3 +
                   " COLLATE NATURALNOCASE AND nodehandle > " + p4 + "))";
        }

        case OrderByClause::LABEL_DESC:
            // ORDER BY label DESC, name ASC, nodehandle ASC
            return "(label < " + p1 + " OR (label = " + p1 + " AND name > " + p2 +
                   " COLLATE NATURALNOCASE)"
                   " OR (label = " +
                   p1 + " AND name = " + p2 + " COLLATE NATURALNOCASE AND nodehandle > " + p3 +
                   "))";

        default:
            return buildCursorWhereForListAll(OrderByClause::DEFAULT_ASC, startParam);
    }
}

// Binds cursor parameters for the given sort order into stmt starting at startParam.
// Returns false if the cursor is missing the required field for that order.
bool bindCursorParamsForListAll(int& sqlResult,
                                sqlite3_stmt* stmt,
                                int order,
                                int startParam,
                                const NodeSearchCursorOffset& cursor)
{
    const int kP1 = startParam;
    const int kP2 = startParam + 1;
    const int kP3 = startParam + 2;
    const int kP4 = startParam + 3;

    const auto bindI64 = sqlite3_bind_int64;
    const auto bindI = sqlite3_bind_int;
    const sqlite3_int64 h = static_cast<sqlite3_int64>(cursor.mLastHandle);

    switch (order)
    {
        case OrderByClause::DEFAULT_ASC:
        case OrderByClause::DEFAULT_DESC:
            bindText(sqlResult, stmt, kP1, cursor.mLastName);
            bindValue(sqlResult, stmt, kP2, h, bindI64);
            break;

        case OrderByClause::SIZE_ASC:
        case OrderByClause::SIZE_DESC:
            if (!cursor.mLastSize.has_value())
                return false;
            bindValue(sqlResult, stmt, kP1, *cursor.mLastSize, bindI64);
            bindText(sqlResult, stmt, kP2, cursor.mLastName);
            bindValue(sqlResult, stmt, kP3, h, bindI64);
            break;

        case OrderByClause::MTIME_ASC:
        case OrderByClause::MTIME_DESC:
            if (!cursor.mLastMtime.has_value())
                return false;
            bindValue(sqlResult, stmt, kP1, *cursor.mLastMtime, bindI64);
            bindText(sqlResult, stmt, kP2, cursor.mLastName);
            bindValue(sqlResult, stmt, kP3, h, bindI64);
            break;

        case OrderByClause::FAV_ASC:
        case OrderByClause::FAV_DESC:
            if (!cursor.mLastFav.has_value())
                return false;
            bindValue(sqlResult, stmt, kP1, *cursor.mLastFav, bindI64);
            bindText(sqlResult, stmt, kP2, cursor.mLastName);
            bindValue(sqlResult, stmt, kP3, h, bindI64);
            break;

        case OrderByClause::LABEL_ASC:
        {
            if (!cursor.mLastLabel.has_value())
                return false;
            const int isZero = (*cursor.mLastLabel == 0) ? 1 : 0;
            bindValue(sqlResult, stmt, kP1, isZero, bindI);
            bindValue(sqlResult, stmt, kP2, *cursor.mLastLabel, bindI);
            bindText(sqlResult, stmt, kP3, cursor.mLastName);
            bindValue(sqlResult, stmt, kP4, h, bindI64);
            break;
        }

        case OrderByClause::LABEL_DESC:
            if (!cursor.mLastLabel.has_value())
                return false;
            bindValue(sqlResult, stmt, kP1, *cursor.mLastLabel, bindI);
            bindText(sqlResult, stmt, kP2, cursor.mLastName);
            bindValue(sqlResult, stmt, kP3, h, bindI64);
            break;

        default:
            return bindCursorParamsForListAll(sqlResult,
                                              stmt,
                                              OrderByClause::DEFAULT_ASC,
                                              startParam,
                                              cursor);
    }
    return true;
}

// Binds the timestamp-anchor half-bound into stmt at anchorParam: ASC → mStartSeconds,
// DESC → mEndSeconds (the open side stays unbounded so pages walk into adjacent sections).
// Returns false only if the anchor order has no timestamp column. The API layer rejects
// such orders, so the caller treats false as an internal error and aborts the query.
bool bindTimestampAnchorParamForListAll(int& sqlResult,
                                        sqlite3_stmt* stmt,
                                        const TimestampAnchorFilter& anchor,
                                        int anchorParam)
{
    const auto colOpt = timestampColumnForOrder(anchor.mOrder);
    assert(colOpt.has_value());
    if (!colOpt)
        return false;
    const auto col = *colOpt;

    // Scale seconds → column units. unitsPerSecond is always 1 today, so the overflow
    // clamps are dead; they guard a future ms-resolution column against signed-overflow UB.
    auto scaleSat = [&](int64_t v) -> sqlite3_int64
    {
        if (col.unitsPerSecond > 1)
        {
            if (v > std::numeric_limits<int64_t>::max() / col.unitsPerSecond)
                return std::numeric_limits<sqlite3_int64>::max();
            if (v < std::numeric_limits<int64_t>::min() / col.unitsPerSecond)
                return std::numeric_limits<sqlite3_int64>::min();
        }
        return static_cast<sqlite3_int64>(v * col.unitsPerSecond);
    };

    const sqlite3_int64 bound =
        isAscOrder(anchor.mOrder) ? scaleSat(anchor.mStartSeconds) : scaleSat(anchor.mEndSeconds);
    bindValue(sqlResult, stmt, anchorParam, bound, sqlite3_bind_int64);
    return true;
}

// Result columns for listAllNodesByPage (omits ctime, which no sort order uses).
const std::string& listAllNodesResultCols()
{
    static const std::string s{"nodehandle, counter, node, "
                               "type, sizeVirtual, mtime, name, label, fav"};
    return s;
}

bool isGroupMimeTypeForListAll(const MimeType_t mimeType)
{
    return mimeType == MIME_TYPE_ALL_DOCS || mimeType == MIME_TYPE_ALL_VISUAL_MEDIA;
}

const std::vector<MimeType_t>& groupedMimeTypesForListAll(const MimeType_t mimeType)
{
    static const std::vector<MimeType_t> docs = {MIME_TYPE_DOCUMENT,
                                                 MIME_TYPE_PDF,
                                                 MIME_TYPE_PRESENTATION,
                                                 MIME_TYPE_SPREADSHEET};
    static const std::vector<MimeType_t> visualMedia = {MIME_TYPE_PHOTO, MIME_TYPE_VIDEO};
    static const std::vector<MimeType_t> empty;

    switch (mimeType)
    {
        case MIME_TYPE_ALL_DOCS:
            return docs;
        case MIME_TYPE_ALL_VISUAL_MEDIA:
            return visualMedia;
        default:
            assert(false && "Unexpected grouped MIME type");
            return empty;
    }
}

// A sub-category matches only a category that contains its parent mime, so an incompatible
// pairing (e.g. VIDEO + gif) can never match. Extensible: parent from Node::fileSubTypeParent,
// group membership from groupedMimeTypesForListAll (the single source of truth).
bool fileSubTypeCompatibleWithCategory(FileSubType_t fileSubType, MimeType_t category)
{
    if (fileSubType == FILE_SUBTYPE_NONE)
        return true;
    const MimeType_t parent = Node::fileSubTypeParent(fileSubType);
    if (category == parent)
        return true;
    if (!isGroupMimeTypeForListAll(category))
        return false;
    const std::vector<MimeType_t>& members = groupedMimeTypesForListAll(category);
    return std::find(members.begin(), members.end(), parent) != members.end();
}

std::string buildWhereClauseForListAll(const std::vector<std::string>& conditions)
{
    if (conditions.empty())
    {
        return {};
    }

    std::string whereClause = "WHERE ";
    for (size_t i = 0; i < conditions.size(); ++i)
    {
        if (i > 0)
        {
            whereClause += " AND ";
        }
        whereClause += conditions[i];
    }
    whereClause += " \n";

    return whereClause;
}

// Guard the constants baked into the SQL text below. If these ever change, bump the
// literals in buildListAllRouteSelect / buildUpWalkExists to match.
static_assert(Node::FLAGS_IS_VERSION == 0,
              "FLAGS_IS_VERSION changed; update listAllNodesByPage SQL literals");
static_assert(Node::FLAGS_IS_MARKED_SENSITIVE == 2,
              "FLAGS_IS_MARKED_SENSITIVE changed; update listAllNodesByPage SQL literals");

// EXISTS clause: walks the parent chain of the outer `nodes AS n` row and is true
// iff it reaches any handle in slots ?<filesRootParam>..?<filesRootParam+numRoots-1>.
// Outer FROM must alias nodes as `n`. Assumes no cycles. Caller guarantees no UNDEF
// in the bound roots, so the post-rootnode `up.h = UNDEF` row fails IN(...) naturally.
//
// excludeSensitive: walks `sensSeen` (OR of the sensitive bit) over n inclusive up to
// the matched root EXCLUSIVE — the root's own flag is ignored, matching
// searchNodesByMimetype. Final SELECT requires sensSeen = 0.
//
// excludeHandles: when numExcludes > 0, walks `excSeen` and rejects rows whose chain
// (including the matched root) hits the exclude list — dropping a root drops its
// subtree. When numExcludes == 0, no exc fragments are emitted.
struct ExcludeSqlFragments
{
    std::string columnDecl; // ", excSeen" or empty
    std::string initial; // initial value for excSeen on first CTE row
    std::string step; // recursive step value for excSeen
    std::string recursiveCut; // optional pruning in recursive WHERE
    std::string outerWhere; // outer SELECT filter rejecting matched roots
};

ExcludeSqlFragments buildExcludeFragment(int excludeHandleParam, size_t numExcludes)
{
    if (numExcludes == 0)
        return {};

    std::string inList;
    for (size_t i = 0; i < numExcludes; ++i)
    {
        if (i > 0)
            inList += ", ";
        inList += "?";
        inList += std::to_string(excludeHandleParam + static_cast<int>(i));
    }

    return {
        ", excSeen",
        ", (n.nodehandle IN (" + inList + "))",
        ", up.excSeen OR (p.nodehandle IN (" + inList + "))",
        " AND up.excSeen = 0",
        " AND up.excSeen = 0 AND up.h NOT IN (" + inList + ")",
    };
}

// Subtree-scope SQL slots shared by every list-all / date-section builder: the
// param slots and counts that drive buildUpWalkExists (root walk, sensitivity,
// exclude handles). Computed once per public call and threaded down unchanged.
struct SubtreeScopeSql
{
    int filesRootParam; ///< first ?slot of the contiguous filesRoots run
    size_t numRoots;
    bool excludeSensitive; ///< walk + reject sensitive ancestors
    int excludeHandleParam; ///< first ?slot of the contiguous exclude run
    size_t numExcludes;
};

// Keyset-pagination cursor slot (list-all only). When `has` is false this is the
// first page and `startParam` is unused.
struct CursorSql
{
    bool has;
    int startParam; ///< first ?slot of the cursor's keyset values
};

// Timestamp-anchor slot (list-all only). When `has` is false there is no anchor;
// `order` selects the column + direction (its own ASC/DESC), `param` is the
// single half-bound's ?slot.
struct AnchorSql
{
    bool has;
    int param;
    int order;
};

std::string buildUpWalkExists(const SubtreeScopeSql& scope)
{
    const int filesRootParam = scope.filesRootParam;
    const size_t numRoots = scope.numRoots;
    const bool excludeSensitive = scope.excludeSensitive;
    const int excludeHandleParam = scope.excludeHandleParam;
    const size_t numExcludes = scope.numExcludes;

    assert(numRoots >= 1);
    static const std::string undefStr{std::to_string(static_cast<sqlite3_int64>(UNDEF))};
    static const std::string sensMask{std::to_string(1ULL << Node::FLAGS_IS_MARKED_SENSITIVE)};

    const std::string sensInitial =
        excludeSensitive ? ("((n.flags & " + sensMask + ") != 0)") : std::string("0");
    const std::string sensStep = excludeSensitive ?
                                     ("up.sensSeen OR ((p.flags & " + sensMask + ") != 0)") :
                                     std::string("0");
    const std::string sensWhere =
        excludeSensitive ? std::string(" AND up.sensSeen = 0") : std::string();
    // Prune subtrees below a sensitive ancestor: once sensSeen flips to 1 the outer
    // sensWhere will reject the row anyway, so there is nothing to learn by walking
    // further up. Correctness-neutral — final filter is unchanged.
    const std::string sensRecursiveCut =
        excludeSensitive ? std::string(" AND up.sensSeen = 0") : std::string();

    std::string rootInList;
    for (size_t i = 0; i < numRoots; ++i)
    {
        if (i > 0)
            rootInList += ", ";
        rootInList += "?";
        rootInList += std::to_string(filesRootParam + static_cast<int>(i));
    }

    const ExcludeSqlFragments exc = buildExcludeFragment(excludeHandleParam, numExcludes);

    return "EXISTS ("
           "WITH RECURSIVE up(h, sensSeen" +
           exc.columnDecl +
           ") AS ("
           "SELECT n.parenthandle, " +
           sensInitial + exc.initial +
           " "
           "UNION ALL "
           "SELECT p.parenthandle, " +
           sensStep + exc.step +
           " "
           "FROM nodes AS p JOIN up ON p.nodehandle = up.h "
           "WHERE up.h IS NOT NULL AND up.h != " +
           undefStr + sensRecursiveCut + exc.recursiveCut +
           ") "
           "SELECT 1 FROM up WHERE up.h IN (" +
           rootInList + ")" + sensWhere + exc.outerWhere +
           " LIMIT 1"
           ")";
}

// Builds a single-route SELECT for listAllNodesByPage given a mime filter condition
// (either a literal value for grouped/CTE routes or a bound parameter for simple ones).
//
// Always-on filters (in addition to the caller-provided mime filter):
//   1. (flags & FLAGS_IS_VERSION) = 0   — exclude file versions
//   2. EXISTS(walk up to any of the caller-supplied filesRoots) — restrict the row to the
//      subtree rooted at Cloud / Vault / explicit ancestor. When excludeSensitive is true,
//      it also requires every walked ancestor (n inclusive, matched root exclusive) to have
//      FLAGS_IS_MARKED_SENSITIVE clear.
// Optional filters, appended in this order before ORDER BY / LIMIT:
//   3. anchor (when set)  — `<col> > invalidSentinel` plus a half-bound: `<col> >= ?` for an
//      ASC sectionOrder, `<col> < ?` for DESC. Column/direction come from the anchor's own
//      sectionOrder, independent of the page order.
//   4. cursor (when set)  — keyset bounding + pagination predicates (buildBoundingWhereForListAll
//      / buildCursorWhereForListAll).
std::string buildListAllRouteSelect(const std::string& mimeFilterClause,
                                    int order,
                                    const SubtreeScopeSql& scope,
                                    const CursorSql& cursor,
                                    const AnchorSql& anchor,
                                    bool asGroupedCte = false)
{
    std::vector<std::string> conditions;
    conditions.push_back(mimeFilterClause);
    conditions.push_back("(n.flags & " + std::to_string(1ULL << Node::FLAGS_IS_VERSION) + ") = 0");
    conditions.push_back(buildUpWalkExists(scope));

    if (anchor.has)
    {
        // Direction comes from the anchor's own sectionOrder, not the page order.
        auto col = timestampColumnForOrder(anchor.order);
        assert(col && "timestampColumnForOrder returned nullopt at anchor build");
        if (!col)
            col = timestampColumnForOrder(OrderByClause::MTIME_DESC);

        // Exclude no-timestamp nodes (matches the section query, so anchored pages
        // match section membership). Needed for both directions: a DESC anchor has no
        // lower bound and would otherwise leak mtime<=0 nodes that belong to no section.
        conditions.push_back(col->rawColumnExpr + " > " + std::to_string(col->invalidSentinel));

        const std::string p = "?" + std::to_string(anchor.param);
        if (isAscOrder(anchor.order))
        {
            // ASC anchor: <col> >= mStartSeconds.
            conditions.push_back(col->rawColumnExpr + " >= " + p);
        }
        else
        {
            // DESC anchor: <col> < mEndSeconds.
            conditions.push_back(col->rawColumnExpr + " < " + p);
        }
    }

    if (cursor.has)
    {
        conditions.push_back(buildBoundingWhereForListAll(order, cursor.startParam));
        conditions.push_back(buildCursorWhereForListAll(order, cursor.startParam));
    }

    // Grouped-CTE routes use kLaIdInnerLimit (= offset+pageSize) so the outer
    // merge sees enough rows from each route. OFFSET is applied only at the
    // outer query (buildGroupedListAllQuery). Standalone queries apply both.
    const std::string tail = asGroupedCte ?
                                 std::string("LIMIT ") + kLaIdInnerLimit :
                                 std::string("LIMIT ") + kLaIdPageSize + " OFFSET " + kLaIdOffset;

    return "SELECT " + listAllNodesResultCols() +
           " \n"
           "FROM nodes AS n \n" +
           buildWhereClauseForListAll(std::move(conditions)) + "ORDER BY \n" +
           buildOrderByForListAll(order) + " \n" + tail;
}

// Empty (no-op) for FILE_SUBTYPE_NONE. The subtype is baked as a validated literal (no bound
// slot); the cache key carries it (computeListAllCacheId) so gif/raw/none statements don't alias.
std::string fileSubTypeResidualClause(FileSubType_t subtype)
{
    if (subtype == FILE_SUBTYPE_NONE)
        return {};
    return " AND getfilesubtype(name) = " + std::to_string(static_cast<int>(subtype));
}

std::string buildGroupedListAllQuery(MimeType_t mimeType,
                                     const std::string& fileSubTypeClause,
                                     int order,
                                     const SubtreeScopeSql& scope,
                                     const CursorSql& cursor,
                                     const AnchorSql& anchor)
{
    assert(isGroupMimeTypeForListAll(mimeType));

    const auto& routeMimeTypes = groupedMimeTypesForListAll(mimeType);
    std::string ctes;
    std::string merged;

    for (size_t i = 0; i < routeMimeTypes.size(); ++i)
    {
        const std::string routeName = "route" + std::to_string(i);
        if (!ctes.empty())
        {
            ctes += ",\n\n";
            merged += "UNION ALL\n";
        }

        ctes += routeName + " AS (\n" +
                buildListAllRouteSelect(
                    "mimetypeVirtual = " + std::to_string(static_cast<int>(routeMimeTypes[i])) +
                        fileSubTypeClause,
                    order,
                    scope,
                    cursor,
                    anchor,
                    /*asGroupedCte=*/true) +
                "\n)";
        merged += "SELECT " + listAllNodesResultCols() + " \nFROM " + routeName + "\n";
    }

    return "WITH \n\n" + ctes +
           "\n\n"
           "SELECT " +
           listAllNodesResultCols() +
           " \n"
           "FROM (\n" +
           merged +
           ") AS merged \n"
           "ORDER BY \n" +
           buildOrderByForListAll(order) +
           " \n"
           "LIMIT " +
           kLaIdPageSize + " OFFSET " + kLaIdOffset;
}

bool validateListAllHandles(const std::vector<NodeHandle>& handles,
                            size_t maxSize,
                            const char* label,
                            const char* maxLabel,
                            const char* logPrefix)
{
    if (handles.size() > maxSize)
    {
        LOG_warn << logPrefix << ": " << label << ".size()=" << handles.size() << " exceeds "
                 << maxLabel << "=" << maxSize;
        return false;
    }
    if (std::any_of(handles.begin(),
                    handles.end(),
                    [](NodeHandle h)
                    {
                        return h.isUndef();
                    }))
    {
        LOG_warn << logPrefix << ": " << label
                 << " contains UNDEF handle — caller contract requires all handles to be valid; "
                    "returning empty";
        return false;
    }
    return true;
}

// ── Date-section helpers (groupAllNodesByDate) ───────────────────────────────
//
// Inner-SELECT gid expression: gid := strftime(<fmt>, (<secondsExpr>) + ?tz, 'unixepoch').
// strftime takes the unix-time value + 'unixepoch' modifier directly (no nested
// datetime() needed). Granularity picks the strftime format; secondsExpr comes
// from the descriptor so a future column stored in milliseconds can wrap as
// "(col / 1000)" without changing this builder.
std::string buildDateSectionGidExpr(const TimestampColumnDescriptor& col,
                                    DateSectionGranularity granularity,
                                    int tzParamIndex)
{
    const char* fmt = nullptr;
    switch (granularity)
    {
        case DateSectionGranularity::Day:
            fmt = "'%Y-%m-%d'";
            break;
        case DateSectionGranularity::Month:
            fmt = "'%Y-%m'";
            break;
        case DateSectionGranularity::Year:
            fmt = "'%Y'";
            break;
        default:
            assert(false && "unknown DateSectionGranularity");
            fmt = "'%Y-%m'";
            break;
    }
    // (col + ?tz) shifts the timestamp into the caller's local wall-clock time
    // before strftime splits the calendar date. ?tz is bound to offset seconds
    // (0 == UTC), so the SQL text is offset-independent (cache-stable).
    return "strftime(" + std::string(fmt) + ", (" + col.secondsColumnExpr + ") + ?" +
           std::to_string(tzParamIndex) + ", 'unixepoch')";
}

// Outer-SELECT bound expressions: gid is concatenated with a fixed datetime
// tail to form a SQLite-recognised literal ('YYYY-MM-DD HH:MM:SS'), then
// strftime('%s', …) returns the epoch seconds as TEXT; CAST AS INTEGER so
// sqlite3_column_int64() reads it directly. modifier picks the next bucket
// boundary (DAY/MONTH/YEAR).
struct DateSectionBoundExprs
{
    std::string startExpr; ///< bucket lower bound (inclusive), as int64
    std::string endExpr; ///< bucket upper bound (exclusive), as int64
};

DateSectionBoundExprs buildDateSectionBoundExprs(DateSectionGranularity granularity,
                                                 int tzParamIndex)
{
    const char* concatTail = nullptr;
    const char* modifier = nullptr;
    switch (granularity)
    {
        case DateSectionGranularity::Day:
            concatTail = "' 00:00:00'"; // gid e.g. "2024-07-15" → "2024-07-15 00:00:00"
            modifier = "'+1 day'";
            break;
        case DateSectionGranularity::Month:
            concatTail = "'-01 00:00:00'"; // gid e.g. "2024-07" → "2024-07-01 00:00:00"
            modifier = "'+1 month'";
            break;
        case DateSectionGranularity::Year:
            concatTail = "'-01-01 00:00:00'"; // gid e.g. "2024" → "2024-01-01 00:00:00"
            modifier = "'+1 year'";
            break;
        default:
            assert(false && "unknown DateSectionGranularity");
            concatTail = "'-01 00:00:00'";
            modifier = "'+1 month'";
            break;
    }
    const std::string base = std::string("gid || ") + concatTail;
    // gid is now a LOCAL date string; strftime('%s', ...) reads it as-if-UTC, so
    // subtract ?tz to recover the true UTC epoch of the local-midnight boundary.
    const std::string tz = " - ?" + std::to_string(tzParamIndex);
    return {
        "CAST(strftime('%s', " + base + ") AS INTEGER)" + tz,
        "CAST(strftime('%s', " + base + ", " + modifier + ") AS INTEGER)" + tz,
    };
}

// Single source of truth for the tz-offset bound-parameter slot (immediately
// after the exclude-handle run): the SQL builders and the binder both derive the
// index here, so the query's `?N` and the value bind can't drift into silent
// mis-binding.
int dateSectionTzSlot(int excludeHandleParam, size_t numExcludes)
{
    return excludeHandleParam + static_cast<int>(numExcludes);
}

// Per-route inner SELECT — same WHERE shape as buildListAllRouteSelect plus
// the "<col> > invalidSentinel" guard, GROUP BY gid. Returns columns
// (gid, cnt). Used as a CTE body by the simple-mime and grouped paths.
std::string buildDateSectionInnerSelect(const std::string& mimeFilterClause,
                                        const TimestampColumnDescriptor& col,
                                        DateSectionGranularity granularity,
                                        const SubtreeScopeSql& scope)
{
    const int tzParamIndex = dateSectionTzSlot(scope.excludeHandleParam, scope.numExcludes);
    const std::string gidExpr = buildDateSectionGidExpr(col, granularity, tzParamIndex);

    std::vector<std::string> conditions;
    conditions.push_back(mimeFilterClause);
    conditions.push_back("(n.flags & " + std::to_string(1ULL << Node::FLAGS_IS_VERSION) + ") = 0");
    // `<col> > invalidSentinel` is the SDK's canonical "has timestamp" check
    // (mega_invalid_timestamp == 0); also prevents a spurious "1970-..." bucket.
    conditions.push_back(col.rawColumnExpr + " > " + std::to_string(col.invalidSentinel));
    conditions.push_back(buildUpWalkExists(scope));

    return "SELECT " + gidExpr +
           " AS gid, \n"
           "       COUNT(*) AS cnt \n"
           "FROM nodes AS n \n" +
           buildWhereClauseForListAll(std::move(conditions)) + "GROUP BY gid";
}

// Simple-mime full section query: 2-tier CTE — inner does GROUP BY, outer
// adds bucket_start / bucket_end computed from gid string. Result columns:
// (gid TEXT, bucket_start INTEGER, bucket_end INTEGER, cnt INTEGER).
//
// See SqliteAccountState::groupAllNodesByDate for a rendered SQL example.
std::string buildDateSectionRouteSelect(const std::string& mimeFilterClause,
                                        int order,
                                        DateSectionGranularity granularity,
                                        const SubtreeScopeSql& scope)
{
    auto col = timestampColumnForOrder(order);
    assert(col && "timestampColumnForOrder returned nullopt at date-section build");
    if (!col)
        col = timestampColumnForOrder(OrderByClause::MTIME_DESC);

    const std::string innerSelect =
        buildDateSectionInnerSelect(mimeFilterClause, *col, granularity, scope);
    const int tzParamIndex = dateSectionTzSlot(scope.excludeHandleParam, scope.numExcludes);
    const auto bounds = buildDateSectionBoundExprs(granularity, tzParamIndex);

    return "WITH grouped AS ( \n" + innerSelect +
           " \n"
           ") \n"
           "SELECT gid, \n"
           "       " +
           bounds.startExpr +
           " AS bucket_start, \n"
           "       " +
           bounds.endExpr +
           " AS bucket_end, \n"
           "       cnt \n"
           "FROM grouped \n"
           // gid is a zero-padded ISO fragment from strftime, so lexicographic
           // ORDER BY matches chronological order. A non-ISO gid would break this.
           "ORDER BY gid " +
           (col->descending ? "DESC" : "ASC");
}

// Date-section only: literal IN-list lets grouped mime share the simple-mime
// CTE shape. listAllNodesByPage keeps the per-route UNION ALL form for its
// per-route LIMIT.
std::string buildGroupedMimeInListClause(MimeType_t mimeType)
{
    assert(isGroupMimeTypeForListAll(mimeType));
    const auto& routeMimeTypes = groupedMimeTypesForListAll(mimeType);

    std::string inList;
    for (size_t i = 0; i < routeMimeTypes.size(); ++i)
    {
        if (i > 0)
            inList += ", ";
        inList += std::to_string(static_cast<int>(routeMimeTypes[i]));
    }
    return "mimetypeVirtual IN (" + inList + ")";
}

} // anonymous namespace (sqlite query + cache-key helpers)

// Cache key for mStmtListAllNodesByPage. Distinct SQL shapes never collide
// on one prepared statement — numRoots / numExcludes set IN-list arity;
// excludeSensitive gates a WHERE clause; hasCursor adds cursor predicates;
// anchorDir picks one of three half-bound shapes (none / >= / <).
// locationScope is omitted: it only picks rootnodes; SQL depends only on
// numRoots.
//
// Digit order: mimeType, fileSubType, order, hasCursor, anchorDir,
// excludeSensitive, numRoots-1, numExcludes (see the append() chain below for each base).
size_t computeListAllCacheId(MimeType_t mimeType,
                             FileSubType_t fileSubType,
                             int order,
                             bool hasCursor,
                             AnchorDirectionDigit anchorDir,
                             bool excludeSensitive,
                             size_t numRoots,
                             size_t numExcludes)
{
    assert(numRoots > 0); // numRoots - 1 below would underflow; append() bounds the rest

    // anchorDir encodes only the anchor direction, not which timestamp column.
    // Safe only while timestampColumnForOrder() has a single column (mtime); a
    // second column would need its identity folded in here too (see the warning
    // at timestampColumnForOrder).
    return CacheKeyBuilder{}
        .append(static_cast<size_t>(mimeType), kMimeTypeCount)
        .append(static_cast<size_t>(fileSubType), kFileSubTypeStride)
        .append(static_cast<size_t>(order), kListAllOrderStride)
        .append(hasCursor ? 1u : 0u, 2)
        .append(static_cast<size_t>(anchorDir), kAnchorDirectionStride)
        .append(excludeSensitive ? 1u : 0u, 2)
        .append(numRoots - 1, kListAllMaxRoots)
        .append(numExcludes, kListAllMaxExcludes + 1)
        .build();
}

// Cache key for mStmtDateSections — same shape as computeListAllCacheId but
// with a base-3 granularity digit instead of hasCursor + anchorDir (the
// section query has no cursor and no timestamp-anchor filter). The tz offset is
// a bound value, not part of the SQL text, so it needs no key digit. Declared in
// include/mega/db/sqlite.h for the same test-reach reason as above.
size_t computeDateSectionsCacheId(MimeType_t mimeType,
                                  FileSubType_t fileSubType,
                                  int order,
                                  DateSectionGranularity granularity,
                                  bool excludeSensitive,
                                  size_t numRoots,
                                  size_t numExcludes)
{
    assert(numRoots > 0);

    return CacheKeyBuilder{}
        .append(static_cast<size_t>(mimeType), kMimeTypeCount)
        .append(static_cast<size_t>(fileSubType), kFileSubTypeStride)
        .append(static_cast<size_t>(order), kListAllOrderStride)
        .append(static_cast<size_t>(granularity), kDateSectionGranularityStride)
        .append(excludeSensitive ? 1u : 0u, 2)
        .append(numRoots - 1, kListAllMaxRoots)
        .append(numExcludes, kListAllMaxExcludes + 1)
        .build();
}

bool SqliteAccountState::validateListAllEntry(MimeType_t mimeType,
                                              FileSubType_t fileSubType,
                                              const std::vector<NodeHandle>& filesRoots,
                                              const std::vector<NodeHandle>& excludeHandles,
                                              const char* logPrefix)
{
    if (!db)
        return false;

    if (mimeType <= MIME_TYPE_UNKNOWN || mimeType > MIME_TYPE_ALL_VISUAL_MEDIA)
    {
        LOG_warn << logPrefix << ": invalid mimeType value " << mimeType;
        return false;
    }

    // Incompatible category + sub-category (e.g. VIDEO + gif) can never match. Reject it so it
    // surfaces as a logged empty result rather than one indistinguishable from a real no-match.
    if (!fileSubTypeCompatibleWithCategory(fileSubType, mimeType))
    {
        LOG_warn << logPrefix << ": fileSubType " << fileSubType << " is not contained by mimeType "
                 << mimeType << "; returning empty";
        return false;
    }

    if (filesRoots.empty())
    {
        LOG_warn << logPrefix << ": filesRoots is empty; returning empty";
        return false;
    }

    if (!validateListAllHandles(filesRoots,
                                kListAllMaxRoots,
                                "filesRoots",
                                "kListAllMaxRoots",
                                logPrefix))
        return false;

    if (!validateListAllHandles(excludeHandles,
                                kListAllMaxExcludes,
                                "excludeHandles",
                                "kListAllMaxExcludes",
                                logPrefix))
        return false;

    return true;
}

// Rendered SQL examples for the query this method assembles and binds (regenerate from
// buildListAllRouteSelect + buildUpWalkExists if either changes):
//
// Example 1 — ORDER_DEFAULT_ASC, 1 root, no cursor, no excludes, excludeSensitive=false.
// Slot layout: ?1=LIMIT, ?2=OFFSET, ?3=innerLimit (unused here), ?4=mimeFilter,
// ?5=filesRoots[0].
//
//   SELECT nodehandle, counter, node, type, sizeVirtual, mtime, name, label, fav
//   FROM nodes AS n
//   WHERE mimetypeVirtual = ?4
//     AND (n.flags & 1) = 0
//     AND EXISTS (
//       WITH RECURSIVE up(h, sensSeen) AS (
//         SELECT n.parenthandle, 0
//         UNION ALL
//         SELECT p.parenthandle, 0
//         FROM nodes AS p JOIN up ON p.nodehandle = up.h
//         WHERE up.h IS NOT NULL AND up.h != -1   -- UNDEF
//       )
//       SELECT 1 FROM up WHERE up.h IN (?5) LIMIT 1
//     )
//   ORDER BY name COLLATE NATURALNOCASE ASC, nodehandle ASC
//   LIMIT ?1 OFFSET ?2
//
// Example 2 — ORDER_SIZE_DESC, 2 roots, hasCursor=true, 1 exclude, excludeSensitive=true,
// byTimestampAnchor with MTIME_DESC sectionOrder. The anchor column + direction come from
// its own sectionOrder, independent of the SIZE_DESC page order; its slot sits before the
// cursor slots.
// Slot layout: ?1=LIMIT, ?2=OFFSET, ?3=innerLimit (unused here), ?4=mimeFilter,
// ?5..?6=filesRoots, ?7=excludeHandles[0], ?8=anchor bound,
// ?9=cursor.lastSize, ?10=cursor.lastName, ?11=cursor.lastHandle.
//
//   SELECT nodehandle, counter, node, type, sizeVirtual, mtime, name, label, fav
//   FROM nodes AS n
//   WHERE mimetypeVirtual = ?4
//     AND (n.flags & 1) = 0
//     AND EXISTS (
//       WITH RECURSIVE up(h, sensSeen, excSeen) AS (
//         SELECT n.parenthandle,
//                ((n.flags & 4) != 0),
//                (n.nodehandle IN (?7))
//         UNION ALL
//         SELECT p.parenthandle,
//                up.sensSeen OR ((p.flags & 4) != 0),
//                up.excSeen OR (p.nodehandle IN (?7))
//         FROM nodes AS p JOIN up ON p.nodehandle = up.h
//         WHERE up.h IS NOT NULL AND up.h != -1   -- UNDEF
//           AND up.sensSeen = 0
//           AND up.excSeen = 0
//       )
//       SELECT 1 FROM up
//       WHERE up.h IN (?5, ?6)
//         AND up.sensSeen = 0
//         AND up.excSeen = 0
//         AND up.h NOT IN (?7)
//       LIMIT 1
//     )
//     AND mtime > 0    -- anchor: exclude no-timestamp nodes
//     AND mtime < ?8   -- anchor: MTIME_DESC half-bound (< endDate)
//     AND (sizeVirtual < ?9 OR (sizeVirtual = ?9 AND name <= ?10 COLLATE NATURALNOCASE))
//     AND (sizeVirtual < ?9
//          OR (sizeVirtual = ?9 AND name < ?10 COLLATE NATURALNOCASE)
//          OR (sizeVirtual = ?9 AND name = ?10 COLLATE NATURALNOCASE AND nodehandle < ?11))
//   ORDER BY sizeVirtual DESC, name COLLATE NATURALNOCASE DESC, nodehandle DESC
//   LIMIT ?1 OFFSET ?2
//
// The optional pieces layer onto that skeleton: excludeSensitive/excludeHandles
// add sensSeen/excSeen columns to the up-walk CTE (buildUpWalkExists); a cursor
// appends keyset predicates (buildCursorWhereForListAll); an anchor appends a
// half-bound on the timestamp column (see buildListAllRouteSelect).
bool SqliteAccountState::listAllNodesByPage(
    const ListAllNodesParams& params,
    const std::vector<NodeHandle>& filesRoots,
    std::vector<std::pair<NodeHandle, NodeSerialized>>& nodes,
    CancelToken cancelFlag)
{
    if (!validateListAllEntry(params.mimeType,
                              params.fileSubType,
                              filesRoots,
                              params.excludeHandles,
                              "listAllNodesByPage"))
        return false;

    // OFFSET 0 is a no-op, so cursor + offset==0 keeps pure keyset semantics; only a
    // non-zero offset combined with a cursor is the ambiguous (rejected) case.
    if (params.cursor.has_value() && params.offset != 0)
    {
        LOG_warn << "listAllNodesByPage: offset and cursor are mutually exclusive";
        return false;
    }

    // Authoritative offset>=0 gate for callers that bypass the public wrapper (unit
    // tests, future internal callers). SQLite silently clamps a negative OFFSET to 0,
    // which would emit a full page instead of the documented empty result.
    if (params.offset < 0)
    {
        LOG_warn << "listAllNodesByPage: negative offset (" << params.offset << ")";
        return false;
    }

    if (cancelFlag.exists())
        sqlite3_progress_handler(db,
                                 NUM_VIRTUAL_MACHINE_INSTRUCTIONS,
                                 SqliteAccountState::progressHandler,
                                 static_cast<void*>(&cancelFlag));

    const bool hasCursor = params.cursor.has_value();
    const bool hasTimestampAnchor = params.timestampAnchor.has_value();
    const size_t numRoots = filesRoots.size();
    const size_t numExcludes = params.excludeHandles.size();

    // Group types are expanded into one top-N CTE per concrete mime, merged with UNION ALL.
    const bool isGroupMimeType = isGroupMimeTypeForListAll(params.mimeType);
    // Group mime types use literal per-route WHERE clauses in CTEs — no parameter slot needed.
    const bool mimeFilterNeedsParam = !isGroupMimeType;

    // Anchor presence + direction (none / ASC / DESC) is a base-3 cache-key
    // digit. Direction comes from the anchor's own sectionOrder, so the ASC and
    // DESC SQL shapes get distinct cache slots.
    const AnchorDirectionDigit anchorDir = !hasTimestampAnchor ? AnchorDirectionDigit::None :
                                           isAscOrder(params.timestampAnchor->mOrder) ?
                                                                 AnchorDirectionDigit::Asc :
                                                                 AnchorDirectionDigit::Desc;
    const size_t cacheId = computeListAllCacheId(params.mimeType,
                                                 params.fileSubType,
                                                 params.order,
                                                 hasCursor,
                                                 anchorDir,
                                                 params.excludeSensitive,
                                                 numRoots,
                                                 numExcludes);
    // Look up without creating a slot; cache only after a successful prepare
    // (below), so a prepare failure leaves no dead nullptr entry behind.
    auto stmtIt = mStmtListAllNodesByPage.find(cacheId);
    sqlite3_stmt* stmt = (stmtIt != mStmtListAllNodesByPage.end()) ? stmtIt->second : nullptr;

    // Slot layout: ?1=pageSize, ?2=OFFSET (always), ?3=innerLimit (grouped CTEs only;
    // unused for single mime), optional mimeFilter at ?4, numRoots contiguous filesRoot
    // slots, numExcludes contiguous exclude-handle slots, then the optional
    // timestamp-anchor slot (1 when set), then the optional cursor slots.
    const int mimeFilterParam = 4;
    const int filesRootParam = mimeFilterParam + (mimeFilterNeedsParam ? 1 : 0);
    const int excludeHandleParam = filesRootParam + static_cast<int>(numRoots);
    const int timestampAnchorParam = excludeHandleParam + static_cast<int>(numExcludes);
    const int cursorStartParam = timestampAnchorParam + (hasTimestampAnchor ? 1 : 0);
    const int timestampAnchorOrder = hasTimestampAnchor ? params.timestampAnchor->mOrder : 0;

    const SubtreeScopeSql scope{filesRootParam,
                                numRoots,
                                params.excludeSensitive,
                                excludeHandleParam,
                                numExcludes};
    const CursorSql cursor{hasCursor, cursorStartParam};
    const AnchorSql anchor{hasTimestampAnchor, timestampAnchorParam, timestampAnchorOrder};

    int sqlResult = SQLITE_OK;
    if (!stmt)
    {
        const std::string fileSubTypeClause = fileSubTypeResidualClause(params.fileSubType);
        std::string query;
        if (isGroupMimeType)
        {
            query = buildGroupedListAllQuery(params.mimeType,
                                             fileSubTypeClause,
                                             params.order,
                                             scope,
                                             cursor,
                                             anchor);
        }
        else
        {
            query = buildListAllRouteSelect("mimetypeVirtual = ?" +
                                                std::to_string(mimeFilterParam) + fileSubTypeClause,
                                            params.order,
                                            scope,
                                            cursor,
                                            anchor);
        }
        sqlResult = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
        if (sqlResult != SQLITE_OK)
        {
            sqlite3_progress_handler(db, -1, nullptr, nullptr);
            errorHandler(sqlResult, "List all nodes by page (cursor-based)", true);
            return false;
        }
        mStmtListAllNodesByPage[cacheId] = stmt; // cache only after a successful prepare
    }

    const sqlite3_int64 pageSize =
        params.maxElements == 0 ? -1 : static_cast<sqlite3_int64>(params.maxElements);
    bindValue(sqlResult, stmt, kLaIdPageSize, pageSize, sqlite3_bind_int64);
    bindValue(sqlResult,
              stmt,
              kLaIdOffset,
              static_cast<sqlite3_int64>(params.offset),
              sqlite3_bind_int64);
    // Saturate the offset+pageSize sum: an adversarial maxElements/offset pair could
    // otherwise overflow sqlite3_int64 (signed-overflow UB) at this last bind point.
    sqlite3_int64 innerLimit = -1;
    if (pageSize >= 0)
    {
        const sqlite3_int64 off = static_cast<sqlite3_int64>(params.offset);
        innerLimit = (off > std::numeric_limits<sqlite3_int64>::max() - pageSize) ?
                         std::numeric_limits<sqlite3_int64>::max() :
                         (pageSize + off);
    }
    bindValue(sqlResult, stmt, kLaIdInnerLimit, innerLimit, sqlite3_bind_int64);

    if (mimeFilterNeedsParam)
        bindValue(sqlResult,
                  stmt,
                  mimeFilterParam,
                  static_cast<int>(params.mimeType),
                  sqlite3_bind_int);

    for (size_t i = 0; i < numRoots; ++i)
    {
        bindValue(sqlResult,
                  stmt,
                  filesRootParam + static_cast<int>(i),
                  static_cast<sqlite3_int64>(filesRoots[i].as8byte()),
                  sqlite3_bind_int64);
    }

    for (size_t i = 0; i < numExcludes; ++i)
    {
        bindValue(sqlResult,
                  stmt,
                  excludeHandleParam + static_cast<int>(i),
                  static_cast<sqlite3_int64>(params.excludeHandles[i].as8byte()),
                  sqlite3_bind_int64);
    }

    if (hasTimestampAnchor && !bindTimestampAnchorParamForListAll(sqlResult,
                                                                  stmt,
                                                                  *params.timestampAnchor,
                                                                  timestampAnchorParam))
    {
        LOG_warn << "listAllNodesByPage: unknown timestamp-anchor order "
                 << params.timestampAnchor->mOrder;
        sqlite3_progress_handler(db, -1, nullptr, nullptr);
        sqlite3_reset(stmt);
        return false;
    }

    if (hasCursor && !bindCursorParamsForListAll(sqlResult,
                                                 stmt,
                                                 params.order,
                                                 cursorStartParam,
                                                 *params.cursor))
    {
        LOG_warn << "listAllNodesByPage: cursor is missing the required field for order "
                 << params.order << "; cursor was likely built for a different sort order";
        sqlite3_progress_handler(db, -1, nullptr, nullptr);
        sqlite3_reset(stmt);
        return false;
    }

    const bool result = (sqlResult == SQLITE_OK) && processSqlQueryNodes(stmt, nodes);

    sqlite3_progress_handler(db, -1, nullptr, nullptr);
    errorHandler(sqlResult, "List all nodes by page (cursor-based)", true);
    sqlite3_reset(stmt);

    return result;
}

// Rendered SQL example for the query this method assembles and binds (regenerate from
// buildDateSectionRouteSelect if it changes):
//
// ORDER_MODIFICATION_DESC, Month granularity, simple mime, 1 root, no excludes,
// excludeSensitive=false. Slot layout: ?1=mimeFilter, ?2=filesRoots[0], ?3=tzOffsetSeconds.
// No LIMIT, no cursor, no anchor — the section query always spans the whole scope.
//
//   WITH grouped AS (
//     SELECT strftime('%Y-%m', (mtime) + ?3, 'unixepoch') AS gid,
//            COUNT(*) AS cnt
//     FROM nodes AS n
//     WHERE mimetypeVirtual = ?1
//       AND (n.flags & 1) = 0
//       AND mtime > 0
//       AND EXISTS (
//         WITH RECURSIVE up(h, sensSeen) AS (
//           SELECT n.parenthandle, 0
//           UNION ALL
//           SELECT p.parenthandle, 0
//           FROM nodes AS p JOIN up ON p.nodehandle = up.h
//           WHERE up.h IS NOT NULL AND up.h != -1   -- UNDEF
//         )
//         SELECT 1 FROM up WHERE up.h IN (?2) LIMIT 1
//       )
//     GROUP BY gid
//   )
//   SELECT gid,
//          CAST(strftime('%s', gid || '-01 00:00:00') AS INTEGER) - ?3 AS bucket_start,
//          CAST(strftime('%s', gid || '-01 00:00:00', '+1 month') AS INTEGER) - ?3 AS bucket_end,
//          cnt
//   FROM grouped
//   ORDER BY gid DESC
//
// Day/Year swap the strftime format and the bucket concat-tail/modifier (see
// buildDateSectionGidExpr / buildDateSectionBoundExprs); a grouped mime replaces
// `mimetypeVirtual = ?1` with a literal IN-list and drops the ?1 slot (tz then
// shifts to ?2); excludeSensitive / excludes add sensSeen / excSeen columns to
// the up-walk CTE and shift the tz slot further right.
bool SqliteAccountState::groupAllNodesByDate(const DateSectionParams& params,
                                             const std::vector<NodeHandle>& filesRoots,
                                             std::vector<DateSection>& out,
                                             CancelToken cancelFlag)
{
    if (!validateListAllEntry(params.mimeType,
                              params.fileSubType,
                              filesRoots,
                              params.excludeHandles,
                              "groupAllNodesByDate"))
        return false;

    if (!timestampColumnForOrder(params.order))
    {
        LOG_warn << "groupAllNodesByDate: unsupported order " << params.order;
        return false;
    }

    const int gran = static_cast<int>(params.granularity);
    if (gran < static_cast<int>(DateSectionGranularity::Day) ||
        gran > static_cast<int>(DateSectionGranularity::Year))
    {
        LOG_warn << "groupAllNodesByDate: invalid granularity " << gran;
        return false;
    }

    if (cancelFlag.exists())
        sqlite3_progress_handler(db,
                                 NUM_VIRTUAL_MACHINE_INSTRUCTIONS,
                                 SqliteAccountState::progressHandler,
                                 static_cast<void*>(&cancelFlag));

    const size_t numRoots = filesRoots.size();
    const size_t numExcludes = params.excludeHandles.size();

    const bool isGroupMimeType = isGroupMimeTypeForListAll(params.mimeType);
    const bool mimeFilterNeedsParam = !isGroupMimeType;

    const size_t cacheId = computeDateSectionsCacheId(params.mimeType,
                                                      params.fileSubType,
                                                      params.order,
                                                      params.granularity,
                                                      params.excludeSensitive,
                                                      numRoots,
                                                      numExcludes);

    auto stmtIt = mStmtDateSections.find(cacheId);
    sqlite3_stmt* stmt = (stmtIt != mStmtDateSections.end()) ? stmtIt->second : nullptr;

    // Slot layout: optional mimeFilter (?1 when set), numRoots contiguous filesRoot
    // slots, numExcludes contiguous exclude-handle slots, then one tz-offset slot.
    // No pageSize, no cursor.
    const int mimeFilterParam = 1;
    const int filesRootParam = mimeFilterParam + (mimeFilterNeedsParam ? 1 : 0);
    const int excludeHandleParam = filesRootParam + static_cast<int>(numRoots);

    int sqlResult = SQLITE_OK;
    if (!stmt)
    {
        // Grouped: literal `IN (X, Y, ...)`; simple: bound `= ?`.
        const std::string mimeFilterClause =
            (isGroupMimeType ? buildGroupedMimeInListClause(params.mimeType) :
                               ("mimetypeVirtual = ?" + std::to_string(mimeFilterParam))) +
            fileSubTypeResidualClause(params.fileSubType);

        const SubtreeScopeSql scope{filesRootParam,
                                    numRoots,
                                    params.excludeSensitive,
                                    excludeHandleParam,
                                    numExcludes};
        const std::string query =
            buildDateSectionRouteSelect(mimeFilterClause, params.order, params.granularity, scope);

        sqlResult = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
        if (sqlResult != SQLITE_OK)
        {
            sqlite3_progress_handler(db, -1, nullptr, nullptr);
            errorHandler(sqlResult, "Group all nodes by date", true);
            // No slot was inserted, so there's nothing to erase and no dangling ref.
            return false;
        }
        mStmtDateSections[cacheId] = stmt; // cache only after a successful prepare
    }

    if (mimeFilterNeedsParam)
        bindValue(sqlResult,
                  stmt,
                  mimeFilterParam,
                  static_cast<int>(params.mimeType),
                  sqlite3_bind_int);

    for (size_t i = 0; i < numRoots; ++i)
    {
        bindValue(sqlResult,
                  stmt,
                  filesRootParam + static_cast<int>(i),
                  static_cast<sqlite3_int64>(filesRoots[i].as8byte()),
                  sqlite3_bind_int64);
    }

    for (size_t i = 0; i < numExcludes; ++i)
    {
        bindValue(sqlResult,
                  stmt,
                  excludeHandleParam + static_cast<int>(i),
                  static_cast<sqlite3_int64>(params.excludeHandles[i].as8byte()),
                  sqlite3_bind_int64);
    }

    // tz offset occupies the slot immediately after the exclude-handle run
    // (same source of truth as the SQL builders). Bound even when 0 (UTC) so
    // the SQL text — and thus the prepared-statement cache key — is offset-independent.
    const int tzOffsetParam = dateSectionTzSlot(excludeHandleParam, numExcludes);
    bindValue(sqlResult,
              stmt,
              tzOffsetParam,
              static_cast<sqlite3_int64>(params.tzOffsetSeconds),
              sqlite3_bind_int64);

    if (sqlResult == SQLITE_OK)
    {
        int rc;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            const unsigned char* gidText = sqlite3_column_text(stmt, 0);
            if (!gidText)
                continue; // Defensive: WHERE <col> > sentinel prevents NULL gid
                          // unless DB/driver is corrupted.

            // Column order matches the outer SELECT in buildDateSectionRouteSelect
            // (both simple-mime and grouped-mime paths): (gid, bucket_start,
            // bucket_end, cnt).
            DateSection s;
            s.mGroupId = reinterpret_cast<const char*>(gidText);
            s.mStartDate = sqlite3_column_int64(stmt, 1);
            s.mEndDate = sqlite3_column_int64(stmt, 2);
            s.mCount = sqlite3_column_int64(stmt, 3);
            out.push_back(std::move(s));
        }
        if (rc != SQLITE_DONE)
            sqlResult = rc;
    }

    sqlite3_progress_handler(db, -1, nullptr, nullptr);
    errorHandler(sqlResult, "Group all nodes by date", true);
    sqlite3_reset(stmt);

    return sqlResult == SQLITE_OK;
}

bool SqliteAccountState::getNodesByFingerprintNoMtime(
    const std::string& fingerprint,
    std::vector<std::pair<NodeHandle, NodeSerialized>>& nodes)
{
    if (!db)
    {
        return false;
    }

    int sqlResult = SQLITE_OK;
    if (!mStmtNodesByFpNoMtime)
    {
        sqlResult = sqlite3_prepare_v2(
            db,
            "SELECT nodehandle, counter, node FROM nodes WHERE fingerprintVirtual = ?",
            -1,
            &mStmtNodesByFpNoMtime,
            NULL);
    }

    bool result = false;
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_blob(mStmtNodesByFpNoMtime,
                                           1,
                                           fingerprint.data(),
                                           (int)fingerprint.size(),
                                           SQLITE_STATIC)) == SQLITE_OK)
        {
            result = processSqlQueryNodes(mStmtNodesByFpNoMtime, nodes);
        }
    }

    if (sqlResult != SQLITE_OK)
    {
        errorHandler(sqlResult, "get nodes by getNodesByFingerprintNoMtime", false);
    }

    sqlite3_reset(mStmtNodesByFpNoMtime);

    return result;
}

bool SqliteAccountState::getNodeByFingerprint(const std::string &fingerprint, mega::NodeSerialized &node, NodeHandle& handle)
{
    if (!db)
    {
        return false;
    }

    int sqlResult = SQLITE_OK;
    if (!mStmtNodeByFp)
    {
        sqlResult = sqlite3_prepare_v2(db, "SELECT nodehandle, counter, node FROM nodes WHERE fingerprint = ? LIMIT 1", -1, &mStmtNodeByFp, NULL);
    }

    bool result = false;
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_blob(mStmtNodeByFp, 1, fingerprint.data(), (int)fingerprint.size(), SQLITE_STATIC)) == SQLITE_OK)
        {
            std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
            result = processSqlQueryNodes(mStmtNodeByFp, nodes);
            if (nodes.size())
            {
                node = nodes.begin()->second;
                handle = nodes.begin()->first;
            }
        }
    }

    if (sqlResult != SQLITE_OK)
    {
        errorHandler(sqlResult, "Get node by fingerprint", false);
    }

    sqlite3_reset(mStmtNodeByFp);

    return result;
}

bool SqliteAccountState::getRecentNodes(const NodeSearchPage& page,
                                        m_time_t since,
                                        std::vector<std::pair<NodeHandle, NodeSerialized>>& nodes)
{
    if (!db)
    {
        return false;
    }

    constexpr uint64_t excludeFlags =
        (1 << Node::FLAGS_IS_VERSION | 1 << Node::FLAGS_IS_IN_RUBBISH);
    static const std::string filenode = std::to_string(FILENODE);
    static const std::string sqlQuery = "SELECT n1.nodehandle, n1.counter, n1.node "
                                        "FROM nodes n1 "
                                        "WHERE n1.flags & " +
                                        std::to_string(excludeFlags) +
                                        " = 0 AND n1.ctime >= ?1 AND n1.type = " + filenode +
                                        " "
                                        "ORDER BY n1.ctime DESC LIMIT ?2 OFFSET ?3";

    int sqlResult = SQLITE_OK;
    if (!mStmtRecents)
    {
        sqlResult = sqlite3_prepare_v2(db, sqlQuery.c_str(), -1, &mStmtRecents, NULL);
    }

    bool stepResult = false;
    const int64_t nodeCount = page.size() ? static_cast<int64_t>(page.size()) : -1;
    const int64_t offset = static_cast<int64_t>(page.startingOffset());
    if (sqlResult == SQLITE_OK && sqlResult == sqlite3_bind_int64(mStmtRecents, 1, since) &&
        sqlResult == sqlite3_bind_int64(mStmtRecents, 2, nodeCount) &&
        sqlResult == sqlite3_bind_int64(mStmtRecents, 3, offset))
    {
        stepResult = processSqlQueryNodes(mStmtRecents, nodes);
    }

    if (sqlResult != SQLITE_OK)
    {
        errorHandler(sqlResult, "Get recent nodes", false);
    }

    sqlite3_reset(mStmtRecents);

    return stepResult;
}

bool SqliteAccountState::getFavouritesHandles(NodeHandle node, uint32_t count, std::vector<mega::NodeHandle> &nodes)
{
    if (!db)
    {
        return false;
    }

    int sqlResult = SQLITE_OK;
    if (!mStmtFavourites)
    {
        // exclude previous versions <- P.type != FILENODE
        //   this is 1.6x faster than using the flags
        std::string sqlQuery =  "WITH nodesCTE(nodehandle, parenthandle, fav, type) AS (SELECT nodehandle, parenthandle, fav, type "
                                "FROM nodes WHERE parenthandle = ? UNION ALL SELECT N.nodehandle, N.parenthandle, N.fav, N.type "
                                "FROM nodes AS N INNER JOIN nodesCTE AS P ON (N.parenthandle = P.nodehandle AND P.type != " + std::to_string(FILENODE) + ")) SELECT node.nodehandle "
                                "FROM nodesCTE AS node WHERE node.fav = 1";

        sqlResult = sqlite3_prepare_v2(db, sqlQuery.c_str(), -1, &mStmtFavourites, NULL);
    }

    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int64(mStmtFavourites,
                                            1,
                                            static_cast<sqlite3_int64>(node.as8byte()))) ==
            SQLITE_OK)
        {
            while ((sqlResult = sqlite3_step(mStmtFavourites)) == SQLITE_ROW && (nodes.size() < count || count == 0))
            {
                nodes.push_back(NodeHandle().set6byte(
                    static_cast<uint64_t>(sqlite3_column_int64(mStmtFavourites, 0))));
            }
        }
    }

    if (sqlResult != SQLITE_DONE && sqlResult != SQLITE_ROW)
    {
        errorHandler(sqlResult, "Get favourites handles", false);
    }

    sqlite3_reset(mStmtFavourites);

    return sqlResult == SQLITE_DONE || sqlResult == SQLITE_ROW;
}

bool SqliteAccountState::childNodeByNameType(NodeHandle parentHandle, const std::string& name, nodetype_t nodeType, std::pair<NodeHandle, NodeSerialized> &node)
{
    bool success = false;
    if (!db)
    {
        return success;
    }

    std::string sqlQuery = "SELECT nodehandle, counter, node FROM nodes WHERE parenthandle = ? AND name = ? AND type = ? limit 1";

    int sqlResult = SQLITE_OK;
    if (!mStmtChildNode)
    {
        sqlResult = sqlite3_prepare_v2(db, sqlQuery.c_str(), -1, &mStmtChildNode, NULL);
    }

    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int64(mStmtChildNode,
                                            1,
                                            static_cast<sqlite3_int64>(parentHandle.as8byte()))) ==
            SQLITE_OK)
        {
            if ((sqlResult = sqlite3_bind_text(mStmtChildNode, 2, name.c_str(), static_cast<int>(name.length()), SQLITE_STATIC)) == SQLITE_OK)
            {
                if ((sqlResult = sqlite3_bind_int64(mStmtChildNode, 3, nodeType)) == SQLITE_OK)
                {
                    std::vector<std::pair<NodeHandle, NodeSerialized>> nodes;
                    processSqlQueryNodes(mStmtChildNode, nodes);
                    if (nodes.size())
                    {
                        node.first = nodes.begin()->first;
                        node.second = nodes.begin()->second;
                        success = true;
                    }
                }
            }
        }
    }

    if (sqlResult != SQLITE_OK)
    {
        errorHandler(sqlResult, "Get nodes by name and type", false);
    }

    sqlite3_reset(mStmtChildNode);

    return success;
}

bool SqliteAccountState::getNodeSizeTypeAndFlags(NodeHandle node, m_off_t& size, nodetype_t& nodeType, uint64_t& oldFlags)
{
    if (!db)
    {
        return false;
    }

    int sqlResult = SQLITE_OK;
    if (!mStmtTypeAndSizeNode)
    {
        sqlResult =
            sqlite3_prepare_v2(db,
                               "SELECT type, sizeVirtual, flags FROM nodes WHERE nodehandle = ?",
                               -1,
                               &mStmtTypeAndSizeNode,
                               NULL);
    }

    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int64(mStmtTypeAndSizeNode,
                                            1,
                                            static_cast<sqlite3_int64>(node.as8byte()))) ==
            SQLITE_OK)
        {
            if ((sqlResult = sqlite3_step(mStmtTypeAndSizeNode)) == SQLITE_ROW)
            {
               nodeType = (nodetype_t)sqlite3_column_int(mStmtTypeAndSizeNode, 0);
               size = sqlite3_column_int64(mStmtTypeAndSizeNode, 1);
               oldFlags = static_cast<uint64_t>(sqlite3_column_int64(mStmtTypeAndSizeNode, 2));
            }
        }
    }

    if (sqlResult != SQLITE_ROW && sqlResult != SQLITE_DONE)
    {
        errorHandler(sqlResult, "Get nodes by name, type and flags", false);
    }

    sqlite3_reset(mStmtTypeAndSizeNode);

    return sqlResult == SQLITE_ROW;
}

bool SqliteAccountState::isAncestor(NodeHandle node, NodeHandle ancestor, CancelToken cancelFlag)
{
    bool result = false;
    if (!db)
    {
        return result;
    }

    std::string sqlQuery = "WITH nodesCTE(nodehandle, parenthandle) "
            "AS (SELECT nodehandle, parenthandle FROM nodes WHERE nodehandle = ? "
            "UNION ALL SELECT A.nodehandle, A.parenthandle FROM nodes AS A INNER JOIN nodesCTE "
            "AS E ON (A.nodehandle = E.parenthandle)) "
            "SELECT * FROM nodesCTE WHERE parenthandle = ?";

    if (cancelFlag.exists())
    {
        sqlite3_progress_handler(db, NUM_VIRTUAL_MACHINE_INSTRUCTIONS, SqliteAccountState::progressHandler, static_cast<void*>(&cancelFlag));
    }

    int sqlResult = SQLITE_OK;
    if (!mStmtIsAncestor)
    {
        sqlResult = sqlite3_prepare_v2(db, sqlQuery.c_str(), -1, &mStmtIsAncestor, NULL);
    }

    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int64(mStmtIsAncestor,
                                            1,
                                            static_cast<sqlite3_int64>(node.as8byte()))) ==
            SQLITE_OK)
        {
            if ((sqlResult = sqlite3_bind_int64(mStmtIsAncestor,
                                                2,
                                                static_cast<sqlite3_int64>(ancestor.as8byte()))) ==
                SQLITE_OK)
            {
                if ((sqlResult = sqlite3_step(mStmtIsAncestor)) == SQLITE_ROW)
                {
                    result = true;
                }
            }
        }
    }

    // unregister the handler (no-op if not registered)
    sqlite3_progress_handler(db, -1, nullptr, nullptr);

    if (sqlResult != SQLITE_ROW && sqlResult != SQLITE_DONE)
    {
        errorHandler(sqlResult, "Is ancestor", true);
    }

    sqlite3_reset(mStmtIsAncestor);

    return result;
}

uint64_t SqliteAccountState::getNumberOfNodes()
{
    uint64_t count = 0;
    if (!db)
    {
        return count;
    }

    sqlite3_stmt *stmt = nullptr;
    int sqlResult = sqlite3_prepare_v2(db, "SELECT count(*) FROM nodes", -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            count = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
        }
    }

    if (sqlResult != SQLITE_ROW)
    {
        errorHandler(sqlResult, "Get number of nodes", false);
    }

    sqlite3_finalize(stmt);

    return count;
}

uint64_t SqliteAccountState::getNumberOfChildrenByType(NodeHandle parentHandle, nodetype_t nodeType)
{
    uint64_t count = 0;
    if (!db)
    {
        return count;
    }

    int sqlResult = SQLITE_OK;
    if (!mStmtNumChild)
    {
        sqlResult = sqlite3_prepare_v2(db, "SELECT count(*) FROM nodes where parenthandle = ? AND type = ?", -1, &mStmtNumChild, NULL);
    }

    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int64(mStmtNumChild,
                                            1,
                                            static_cast<sqlite3_int64>(parentHandle.as8byte()))) ==
            SQLITE_OK)
        {
            if ((sqlResult = sqlite3_bind_int(mStmtNumChild, 2, nodeType)) == SQLITE_OK)
            {
                if ((sqlResult = sqlite3_step(mStmtNumChild)) == SQLITE_ROW)
                {
                    count = static_cast<uint64_t>(sqlite3_column_int64(mStmtNumChild, 0));
                }
            }
        }
    }

    if (sqlResult != SQLITE_ROW)
    {
        errorHandler(sqlResult, "Get number of children by type", false);
    }

    sqlite3_reset(mStmtNumChild);

    return count;
}

void SqliteAccountState::userRegexp(sqlite3_context* context, int argc, sqlite3_value** argv)
{
    if (argc != 2)
    {
        LOG_err << "Invalid parameters for user Regexp";
        assert(false);
        return;
    }

    auto pattern = reinterpret_cast<const char*>(sqlite3_value_text(argv[0]));
    auto nameFromDataBase = reinterpret_cast<const char*>(sqlite3_value_text(argv[1]));
    if (nameFromDataBase && pattern)
    {
        // C++ standard, true to 1, false to 0
        int result = static_cast<int>(likeCompare(pattern, nameFromDataBase, 0));
        sqlite3_result_int(context, result);
    }
}

void SqliteAccountState::getSizeFromNodeCounter(sqlite3_context* context,
                                                int argc,
                                                sqlite3_value** argv)
{
    if (argc != 1)
    {
        LOG_err << "getSizeFromNodeCounter: Invalid parameters for getSizeFromNodeCounter";
        assert(argc == 1);
        sqlite3_result_int64(context, -1);
        return;
    }

    const auto blob = sqlite3_value_blob(argv[0]);
    if (!blob)
    {
        LOG_err << "getSizeFromNodeCounter: invalid FromNodeCounter blob";
        sqlite3_result_int64(context, -1);
        return;
    }
    const auto blobSize = sqlite3_value_bytes(argv[0]);
    const std::string nodeCounter(static_cast<const char*>(blob), static_cast<size_t>(blobSize));
    const NodeCounter nc(nodeCounter);
    sqlite3_result_int64(context, nc.storage);
}

// Returns false (ext left empty) when the name is null/empty or has no extension. Shared by the
// getmimetype / getfilesubtype UDFs so their null/empty/extension contract can't drift.
static bool extractUdfExtension(sqlite3_value* arg, string& ext)
{
    const char* fileName = reinterpret_cast<const char*>(sqlite3_value_text(arg));
    return fileName && *fileName && Node::getExtension(ext, fileName) && !ext.empty();
}

void SqliteAccountState::userGetMimetype(sqlite3_context* context, int argc, sqlite3_value** argv)
{
    if (argc != 1)
    {
        LOG_err << "Invalid parameters for userGetMimetype";
        assert(argc == 1);
        sqlite3_result_int(context, MimeType_t::MIME_TYPE_UNKNOWN);
        return;
    }

    string ext;
    sqlite3_result_int(context,
                       extractUdfExtension(argv[0], ext) ? Node::getMimetype(ext) :
                                                           MimeType_t::MIME_TYPE_OTHERS);
}

void SqliteAccountState::userGetFileSubType(sqlite3_context* context,
                                            int argc,
                                            sqlite3_value** argv)
{
    if (argc != 1)
    {
        LOG_err << "Invalid parameters for userGetFileSubType";
        assert(argc == 1);
        sqlite3_result_int(context, FileSubType_t::FILE_SUBTYPE_NONE);
        return;
    }

    string ext;
    sqlite3_result_int(context,
                       extractUdfExtension(argv[0], ext) ? Node::getFileSubType(ext) :
                                                           FileSubType_t::FILE_SUBTYPE_NONE);
}

void SqliteAccountState::getFingerprintExcludingMtime(sqlite3_context* context,
                                                      int argc,
                                                      sqlite3_value** argv)
{
    if (argc != 1)
    {
        LOG_err << "Invalid parameters for getFingerprintExcludingMtime (argc=" << argc << ")";
        sqlite3_result_null(context);
        return;
    }

    const unsigned char* input = static_cast<const unsigned char*>(sqlite3_value_blob(argv[0]));
    const int len = sqlite3_value_bytes(argv[0]);
    if (!input)
    {
        sqlite3_result_null(context);
        return;
    }

    if (len < 33)
    {
        LOG_err << "getFingerprintExcludingMtime: invalid fingerprint blob size (len=" << len
                << ")";
        assert(false && "getFingerprintExcludingMtime(): invalid fingerprint blob size");
        sqlite3_result_null(context);
        return;
    }

    std::array<uint8_t, 25> result;
    std::copy_n(input, 8, result.begin()); // Copy size
    std::copy_n(input + 16, 16, result.begin() + 8); // Ignore mtime and copy CRC
    std::copy_n(input + 32, 1, result.begin() + 24); // Copy isValid
    sqlite3_result_blob(context, result.data(), static_cast<int>(result.size()), SQLITE_TRANSIENT);
}

void SqliteAccountState::userMatchFilter(sqlite3_context* context, int argc, sqlite3_value** argv)
{
    bool result = false;
    const MrProper cleanUp{[&context, &result]()
                           {
                               sqlite3_result_int(context, result);
                           }};

    if (argc != 10)
    {
        LOG_err << "Invalid parameters for userMatchFilter. Expected (in this order): filter*, "
                   "flags, type, ctime, mtime, mimetypeVirtual, name, description, tags, fav";
        assert(false);
        return;
    }

    auto filter = static_cast<const NodeSearchFilter*>(
        sqlite3_value_pointer(argv[0], NodeSearchFilterPtrStr));

    // Version filtering is handled structurally by the SQL query itself (via the
    // skipVersions parameter on SqliteAccountState::getChildren and the idVerFlag
    // binding in SqliteAccountState::searchNodes). userMatchFilter no longer needs
    // to enforce it here.
    const int64_t flags = sqlite3_value_int64(argv[1]);

    // type
    const nodetype_t type = static_cast<nodetype_t>(sqlite3_value_int(argv[2]));
    if (filter->hasNodeType() && !filter->isValidNodeType(type))
        return;

    // ctime
    if (filter->hasCreationTimeLimits() &&
        !filter->isValidCreationTime(sqlite3_value_int64(argv[3])))
        return;

    // mtime
    if (filter->hasModificationTimeLimits() &&
        !filter->isValidModificationTime(sqlite3_value_int64(argv[4])))
        return;

    // mimetype
    if (filter->hasCategory() &&
        !filter->isValidCategory(static_cast<MimeType_t>(sqlite3_value_int(argv[5])), type))
        return;

    // Fav
    if (filter->hasFav() && !filter->isValidFav(static_cast<bool>(sqlite3_value_int(argv[9]))))
        return;

    // sensitive
    constexpr int64_t sensitivityFlag = 1 << Node::FLAGS_IS_MARKED_SENSITIVE;
    if (!filter->isValidSensitivity((flags & sensitivityFlag) == sensitivityFlag))
        return;

    //// This block defines conditions to be combined by OR or AND operations if present in filter
    // Define a vector with all the conditions to combine
    std::vector<std::function<bool()>> conditionEvals;
    if (filter->hasName())
        conditionEvals.emplace_back(
            [&filter, &argv]()
            {
                return filter->isValidName(sqlite3_value_text(argv[6]));
            });
    if (filter->hasDescription())
        conditionEvals.emplace_back(
            [&filter, &argv]()
            {
                return filter->isValidDescription(sqlite3_value_text(argv[7]));
            });
    if (filter->hasTag())
        conditionEvals.emplace_back(
            [&filter, &argv]()
            {
                return filter->isValidTagSequence(sqlite3_value_text(argv[8]));
            });

    // Condition combination
    if (conditionEvals.empty())
    {
        result = true;
    }
    else if (filter->useAndForTextQuery())
    {
        result = std::all_of(std::begin(conditionEvals),
                             std::end(conditionEvals),
                             [](auto&& f) -> bool
                             {
                                 return f();
                             });
    }
    else
    {
        result = std::any_of(std::begin(conditionEvals),
                             std::end(conditionEvals),
                             [](auto&& f) -> bool
                             {
                                 return f();
                             });
    }
}

std::string OrderByClause::get(int order)
{
    static const std::string nameSort = "name COLLATE NATURALNOCASE";
    static const std::string typeSort = " type DESC";
    switch (order)
    {
        case DEFAULT_ASC:
            return typeSort + ", " + nameSort;
        case DEFAULT_DESC:
            return typeSort + ", " + nameSort + " DESC";
        case SIZE_ASC:
            return typeSort + ", " + "sizeVirtual, " + nameSort;
        case SIZE_DESC:
            return typeSort + ", " + "sizeVirtual DESC, " + nameSort + " DESC";
        case CTIME_ASC:
            return typeSort + ", " + "ctime, " + nameSort;
        case CTIME_DESC:
            return typeSort + ", " + "ctime DESC, " + nameSort + " DESC";
        case MTIME_ASC:
            return typeSort + ", " + "mtime, " + nameSort;
        case MTIME_DESC:
            return typeSort + ", " + "mtime DESC, " + nameSort + " DESC";
        case LABEL_ASC:
            return "CASE WHEN label = 0 THEN 1 ELSE 0 END ASC, label ASC, " + typeSort + ",  " +
                   nameSort;
        case LABEL_DESC:
            return "label DESC, " + typeSort + ", " + nameSort;
        // fav have inverse order
        case FAV_ASC:
            return "fav DESC," + typeSort + ", " + nameSort;
        case FAV_DESC:
            return "fav, " + typeSort + ", " + nameSort;
        default:
            return typeSort + ", " + nameSort;
    }
}

size_t OrderByClause::getId(int order)
{
    return static_cast<size_t>(order);
}

SqliteDbAccess::MTimeType::MTimeType(mega::m_time_t value):
    mValue(value)
{}

bool SqliteDbAccess::MTimeType::bindToDb(sqlite3_stmt* stmt,
                                         const std::map<int, int>& lookupId) const
{
    if (sqlite3_bind_int64(stmt, lookupId.at(COMPONENT), mValue) != SQLITE_OK)
    {
        LOG_err << "Db error during migration while binding mTime value to column: ";
        sqlite3_finalize(stmt);
        return false;
    }

    return true;
}

std::unique_ptr<SqliteDbAccess::MigrateType> SqliteDbAccess::MTimeType::fromNodeData(NodeData& nd)
{
    return std::make_unique<MTimeType>(nd.getMtime());
}

bool SqliteDbAccess::MTimeType::hasValidValue() const
{
    return mValue != 0;
}

SqliteDbAccess::LabelType::LabelType(int value):
    mValue(value)
{}

bool SqliteDbAccess::LabelType::bindToDb(sqlite3_stmt* stmt,
                                         const std::map<int, int>& lookupId) const
{
    if (sqlite3_bind_int64(stmt, lookupId.at(COMPONENT), mValue) != SQLITE_OK)
    {
        LOG_err << "Db error during migration while binding label value to column: ";
        sqlite3_finalize(stmt);
        return false;
    }

    return true;
}

std::unique_ptr<SqliteDbAccess::MigrateType> SqliteDbAccess::LabelType::fromNodeData(NodeData& nd)
{
    return std::make_unique<LabelType>(nd.getLabel());
}

bool SqliteDbAccess::LabelType::hasValidValue() const
{
    return mValue != LBL_UNKNOWN;
}

SqliteDbAccess::DescriptionType::DescriptionType(const string& value):
    mValue(value)
{}

bool SqliteDbAccess::DescriptionType::bindToDb(sqlite3_stmt* stmt,
                                               const std::map<int, int>& lookupId) const
{
    if (mValue.size())
    {
        if (sqlite3_bind_text(stmt,
                              lookupId.at(COMPONENT),
                              mValue.c_str(),
                              static_cast<int>(mValue.length()),
                              SQLITE_STATIC) != SQLITE_OK)
        {
            LOG_err << "Db error during migration while binding description value to column: ";
            sqlite3_finalize(stmt);
            return false;
        }
    }
    else
    {
        sqlite3_bind_null(stmt, lookupId.at(COMPONENT));
    }

    return true;
}

std::unique_ptr<SqliteDbAccess::MigrateType>
    SqliteDbAccess::DescriptionType::fromNodeData(NodeData& nd)
{
    return std::make_unique<DescriptionType>(nd.getDescription());
}

bool SqliteDbAccess::DescriptionType::hasValidValue() const
{
    return mValue.size() > 0;
}

SqliteDbAccess::TagsType::TagsType(const string& value):
    mValue(value)
{}

bool SqliteDbAccess::TagsType::bindToDb(sqlite3_stmt* stmt,
                                        const std::map<int, int>& lookupId) const
{
    if (mValue.size())
    {
        if (sqlite3_bind_text(stmt,
                              lookupId.at(COMPONENT),
                              mValue.c_str(),
                              static_cast<int>(mValue.length()),
                              SQLITE_STATIC) != SQLITE_OK)
        {
            LOG_err << "Db error during migration while binding tags value to column: ";
            sqlite3_finalize(stmt);
            return false;
        }
    }
    else
    {
        sqlite3_bind_null(stmt, lookupId.at(COMPONENT));
    }

    return true;
}

std::unique_ptr<SqliteDbAccess::MigrateType> SqliteDbAccess::TagsType::fromNodeData(NodeData& nd)
{
    return std::make_unique<TagsType>(nd.getTags());;
}

bool SqliteDbAccess::TagsType::hasValidValue() const
{
    return mValue.size() > 0;
}

} // namespace

#endif
