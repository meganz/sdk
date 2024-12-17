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

#include <numeric>

#ifdef USE_SQLITE
namespace mega {

static const char* NodeSearchFilterPtrStr = "NodeSearchFilterPtrStr";

SqliteDbAccess::SqliteDbAccess(const LocalPath& rootPath)
  : mRootPath(rootPath)
{
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

            // if current version, use that one... unless migration to NoD or renaming to adapt the version to SRW are required
            if (currentDbVersion == LEGACY_DB_VERSION && LEGACY_DB_VERSION != LAST_DB_VERSION_WITHOUT_NOD && LEGACY_DB_VERSION != LAST_DB_VERSION_WITHOUT_SRW)
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
    // We need to ensure that the strings to compare are null terminated
    std::string s1{static_cast<const char*>(data1), static_cast<size_t>(size1)};
    std::string s2{static_cast<const char*>(data2), static_cast<size_t>(size2)};
    return naturalsorting_compare(s1.c_str(), s2.c_str());
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
    std::string sql = "CREATE TABLE IF NOT EXISTS nodes (nodehandle int64 PRIMARY KEY NOT NULL, "
                      "parenthandle int64, name text, fingerprint BLOB, origFingerprint BLOB, "
                      "type tinyint, mimetypeVirtual tinyint AS (getmimetype(name)) VIRTUAL, "
                      "sizeVirtual int64 AS (getSizeFromNodeCounter(counter)) VIRTUAL,"
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
         "int64 DEFAULT 0",                                    NodeData::COMPONENT_MTIME,
         NewColumn::extractDataFromNodeData<MTimeType>                                                                                      },
        {"label",
         "tinyint DEFAULT 0",                                  NodeData::COMPONENT_LABEL,
         NewColumn::extractDataFromNodeData<LabelType>                                                                                      },
        {"mimetypeVirtual",
         "tinyint AS (getmimetype(name)) VIRTUAL",             NodeData::COMPONENT_NONE,
         nullptr                                                                                                                            },
        {"description",
         "text",                                               NodeData::COMPONENT_DESCRIPTION,
         NewColumn::extractDataFromNodeData<DescriptionType>                                                                                },
        {"tags",            "text",                            NodeData::COMPONENT_TAGS,        NewColumn::extractDataFromNodeData<TagsType>},
        {"sizeVirtual",
         "int64 AS (getSizeFromNodeCounter(counter)) VIRTUAL", NodeData::COMPONENT_NONE,
         nullptr                                                                                                                            },
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
    auto expectedPath = databasePath(fsAccess, fname, DbAccess::DB_VERSION).rawValue();
    if (std::filesystem::exists(expectedPath))
        return expectedPath;

    expectedPath = databasePath(fsAccess, fname, DbAccess::LEGACY_DB_VERSION).rawValue();
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

bool SqliteDbAccess::renameDBFiles(mega::FileSystemAccess& fsAccess, mega::LocalPath& legacyPath, mega::LocalPath& dbPath)
{
    // Main DB file should exits
    if (!fsAccess.renamelocal(legacyPath, dbPath))
    {
        return false;
    }

    std::unique_ptr<FileAccess> fileAccess = fsAccess.newfileaccess();

#if !(TARGET_OS_IPHONE)
    auto suffix = LocalPath::fromRelativePath("-shm");
    auto from = legacyPath + suffix;
    auto to = dbPath + suffix;

    // -shm could or couldn't be present
    if (fileAccess->fopen(from, FSLogging::logExceptFileNotFound) && !fsAccess.renamelocal(from, to))
    {
         // Exists origin and failure to rename
        LOG_debug << "Failure to rename -shm file";
        return false;
    }

    suffix = LocalPath::fromRelativePath("-wal");
    from = legacyPath + suffix;
    to = dbPath + suffix;

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
    auto from = legacyPath + suffix;
    auto to = dbPath + suffix;

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
    auto fileToRemove = dbPath + suffix;
    fsAccess.unlinklocal(fileToRemove);

    suffix = LocalPath::fromRelativePath("-wal");
    fileToRemove = dbPath + suffix;
    fsAccess.unlinklocal(fileToRemove);
#else
    // iOS doesn't use WAL mode, but Journal
    auto suffix = LocalPath::fromRelativePath("-journal");
    auto fileToRemove = dbPath + suffix;
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

    // add missing columns
    for (const auto& c : newCols)
    {
        if (!addColumn(db, c.name, c.type))
        {
            return false;
        }
    }

    return migrateDataToColumns(db, std::move(newCols));
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

bool SqliteDbAccess::addColumn(sqlite3* db, const string& name, const string& type)
{
    string query("ALTER TABLE nodes ADD COLUMN '" + name + "' " + type);
    if (sqlite3_exec(db, query.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        LOG_err << "Db error while adding 'nodes." << name << ' ' << type << "' column: " << sqlite3_errmsg(db);
        return false;
    }

    return true;
}

bool SqliteDbAccess::migrateDataToColumns(sqlite3* db, vector<NewColumn>&& cols)
{
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

    if (sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        LOG_debug << "Db error during migration for " << "BEGIN: " << sqlite3_errmsg(db);
        return false;
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

    if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        LOG_debug << "Db error during migration for " << "COMMIT: " << sqlite3_errmsg(db);
        return false;
    }

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

    case SQLITE_INTERRUPT:
        if (interrupt)
        {
            // SQLITE_INTERRUPT isn't handle as an error if caller can be interrupted
            LOG_debug <<  operation << ": interrupted";
            return;
        }
        break;

    case SQLITE_FULL:
        dbError = DBError::DB_ERROR_FULL;
        break;

    case SQLITE_IOERR:
        dbError = DBError::DB_ERROR_IO;
        break;

    default:
        dbError = DBError::DB_ERROR_UNKNOWN;
        break;
    }

    string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqliteError));
    LOG_err << operation << ": " << dbfile << err;
    assert(!operation.c_str());

    if (mDBErrorCallBack && dbError != DBError::DB_ERROR_UNKNOWN)
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

bool SqliteAccountState::processSqlQueryAllNodeTags(
    sqlite3_stmt* stmt,
    std::set<std::string>& tags,
    std::function<bool(const std::string&)> isValidTagF)
{
    assert(stmt);
    int sqlResult = SQLITE_ERROR;
    while ((sqlResult = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        const void* data = sqlite3_column_blob(stmt, 0);
        int size = sqlite3_column_bytes(stmt, 0);
        if (!data || !size)
        {
            continue;
        }
        std::string allTags(static_cast<const char*>(data), static_cast<size_t>(size));
        const std::set<std::string> separatedTags = splitString(allTags, MegaClient::TAG_DELIMITER);
        std::for_each(std::begin(separatedTags),
                      std::end(separatedTags),
                      [&tags, &isValidTagF](const std::string& t)
                      {
                          if (isValidTagF(t))
                              tags.insert(t);
                      });
    }

    errorHandler(sqlResult, "Process sql query for all node tags", true);
    return sqlResult == SQLITE_DONE;
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

void SqliteAccountState::createIndexes()
{
    if (!db)
    {
        return;
    }
    // Create index for column that is not primary key (which already has an index by default)
    std::string sql = "CREATE INDEX IF NOT EXISTS parenthandleindex on nodes (parenthandle)";
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

#if defined( __ANDROID__) || defined(USE_IOS)
    sql = "CREATE INDEX IF NOT EXISTS origFingerprintindex on nodes (origFingerprint)";
    result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    if (result)
    {
        LOG_err << "Data base error while creating index (origFingerprintindex): " << sqlite3_errmsg(db);
    }
#endif

    sql = "CREATE INDEX IF NOT EXISTS shareindex on nodes (share)";
    result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    if (result)
    {
        LOG_err << "Data base error while creating index (shareindex): " << sqlite3_errmsg(db);
    }

    sql = "CREATE INDEX IF NOT EXISTS favindex on nodes (fav)";
    result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    if (result)
    {
        LOG_err << "Data base error while creating index (favindex): " << sqlite3_errmsg(db);
    }

    sql = "CREATE INDEX IF NOT EXISTS ctimeindex on nodes (ctime)";
    result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    if (result)
    {
        LOG_err << "Data base error while creating index (ctimeindex): " << sqlite3_errmsg(db);
    }
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

    for (auto& s : mStmtSearchNodes)
    {
        sqlite3_finalize(s.second);
    }
    mStmtSearchNodes.clear();

    sqlite3_finalize(mStmtAllNodeTags);
    mStmtAllNodeTags = nullptr;

    sqlite3_finalize(mStmtNodesByFp);
    mStmtNodesByFp = nullptr;

    sqlite3_finalize(mStmtNodeByFp);
    mStmtNodeByFp = nullptr;

    sqlite3_finalize(mStmtNodeByOrigFp);
    mStmtNodeByOrigFp = nullptr;

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

        std::string name = node->displayname();
        sqlite3_bind_text(mStmtPutNode, 3, name.c_str(), static_cast<int>(name.length()), SQLITE_STATIC);

        string fp;
        node->FileFingerprint::serialize(&fp);
        sqlite3_bind_blob(mStmtPutNode, 4, fp.data(), static_cast<int>(fp.size()), SQLITE_STATIC);

        std::string origFingerprint;
        attr_map::const_iterator attrIt = node->attrs.map.find(MAKENAMEID2('c', '0'));
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

    sqlite3_stmt *stmt = nullptr;
    bool result = false;
    int sqlResult = sqlite3_prepare_v2(db, "SELECT nodehandle, counter, node FROM nodes WHERE share & ? != 0", -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int(stmt, 1, static_cast<int>(shareType))) == SQLITE_OK)
        {
            result = processSqlQueryNodes(stmt, nodes);
        }
    }

    errorHandler(sqlResult, "Get nodes with shares or link", false);

    sqlite3_finalize(stmt);

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
    const size_t cacheId = OrderByClause::getId(order);
    sqlite3_stmt*& stmt = mStmtGetChildren[cacheId];

    int sqlResult = SQLITE_OK;
    static const QueryTagId idParentHand{1};
    static const QueryTagId idPageSize{2};
    static const QueryTagId idPageOff{3};
    static const QueryTagId idFilter{4};
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
            "WHERE (parenthandle = " + idParentHand + ") " // Versions aren't taken in consideration
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

    bindPointer(sqlResult, stmt, idFilter, &filterCopy, NodeSearchFilterPtrStr);
    bindValue(sqlResult, stmt, idParentHand, filter.byParentHandle(), sqlite3_bind_int64);
    bindValue(sqlResult, stmt, idPageSize, pageSize, sqlite3_bind_int64);
    bindValue(sqlResult, stmt, idPageOff, page.startingOffset(), sqlite3_bind_int64);

    if (sqlResult == SQLITE_OK)
        result = processSqlQueryNodes(stmt, children);

    // unregister the handler (no-op if not registered)
    sqlite3_progress_handler(db, -1, nullptr, nullptr);

    errorHandler(sqlResult, "Get children with filter", true);

    sqlite3_reset(stmt);

    return result;
}

bool SqliteAccountState::getAllNodeTags(const std::string& searchString,
                                        std::set<std::string>& tags,
                                        CancelToken cancelFlag)
{
    if (!db)
    {
        LOG_err << "SqliteAccountState::getAllNodeTags: Invalid db";
        return false;
    }

    // When early returning, this gets executed
    int sqlResult = SQLITE_OK;
    const MrProper cleanUp(
        [this, &sqlResult]()
        {
            // unregister the handler (no-op if not registered)
            sqlite3_progress_handler(db, -1, nullptr, nullptr);
            errorHandler(sqlResult, "Get all node tags", true);
            sqlite3_reset(mStmtAllNodeTags);
        });

    if (cancelFlag.exists())
    {
        sqlite3_progress_handler(db,
                                 NUM_VIRTUAL_MACHINE_INSTRUCTIONS,
                                 SqliteAccountState::progressHandler,
                                 static_cast<void*>(&cancelFlag));
    }

    static const std::string selectStmBase{R"(
        SELECT DISTINCT tags
            FROM nodes
            WHERE
                tags IS NOT NULL AND
                tags != '' AND
                (?1 = 0 OR (tags REGEXP ?2))
    )"};

    // Ensure stmt is valid
    if (!mStmtAllNodeTags &&
        (SQLITE_OK !=
         (sqlResult =
              sqlite3_prepare_v2(db, selectStmBase.c_str(), -1, &mStmtAllNodeTags, nullptr))))
    {
        return false;
    }
    // The search string has something different from *?
    bool therIsSomethingToSearch = std::any_of(searchString.begin(),
                                               searchString.end(),
                                               [](const char& c)
                                               {
                                                   return c != '*';
                                               });
    const std::string asteriskSurroundSearch = ensureAsteriskSurround(searchString);

    if (SQLITE_OK != (sqlResult = sqlite3_bind_int(mStmtAllNodeTags, 1, therIsSomethingToSearch)) ||
        SQLITE_OK != (sqlResult = sqlite3_bind_text(mStmtAllNodeTags,
                                                    2,
                                                    asteriskSurroundSearch.c_str(),
                                                    static_cast<int>(asteriskSurroundSearch.size()),
                                                    SQLITE_STATIC)))
    {
        return false;
    }

    if (therIsSomethingToSearch)
    {
        const auto tagValidator = [&pattern = asteriskSurroundSearch](const std::string& tag)
        {
            return likeCompare(pattern.c_str(), tag.c_str(), 0);
        };
        return processSqlQueryAllNodeTags(mStmtAllNodeTags, tags, tagValidator);
    }
    static const auto alwaysTrueF = [](const std::string&)
    {
        return true;
    };
    return processSqlQueryAllNodeTags(mStmtAllNodeTags, tags, alwaysTrueF);
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
                "OR (" + idIncShares + " != " + noShareStr + " AND nodehandle IN "
                    "(SELECT nodehandle FROM nodes WHERE share = " + idIncShares + ")))";

        static const std::string nodesOfShares =
            "nodesOfShares(" + columnsForNodeAndFilters + ") \n"
            "AS (SELECT " + columnsForNodeAndFilters + " \n"
                "FROM nodes \n"
                "WHERE " + idIncShares + " != " + noShareStr + " AND share = " + idIncShares + ")";

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
            "FROM nodesAfterFilters \n"
            "ORDER BY \n" +
            OrderByClause::get(order) + " \n" +
            "LIMIT " + idPageSize + " OFFSET " + idPageOff;
        // clang-format on

        sqlResult = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, NULL);
    }

    constexpr uint64_t versionFlag = (1 << Node::FLAGS_IS_VERSION); // exclude file versions
    constexpr uint64_t senstivityFlag = 1 << Node::FLAGS_IS_MARKED_SENSTIVE; // by sensitivity

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
    bindValue(sqlResult, stmt, idSensFlag, senstivityFlag, sqlite3_bind_int64);

    const bool result = (sqlResult == SQLITE_OK) && processSqlQueryNodes(stmt, nodes);

    // unregister the handler (no-op if not registered)
    sqlite3_progress_handler(db, -1, nullptr, nullptr);

    errorHandler(sqlResult, "Search nodes with filter", true);

    sqlite3_reset(stmt);

    return result;
}

bool SqliteAccountState::getNodesByFingerprint(const std::string &fingerprint, std::vector<std::pair<NodeHandle, NodeSerialized> > &nodes)
{
    if (!db)
    {
        return false;
    }

    int sqlResult = SQLITE_OK;
    if (!mStmtNodesByFp)
    {
        sqlResult = sqlite3_prepare_v2(db, "SELECT nodehandle, counter, node FROM nodes WHERE fingerprint = ?", -1, &mStmtNodesByFp, NULL);
    }

    bool result = false;
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_blob(mStmtNodesByFp, 1, fingerprint.data(), (int)fingerprint.size(), SQLITE_STATIC)) == SQLITE_OK)
        {
            result = processSqlQueryNodes(mStmtNodesByFp, nodes);
        }
    }

    if (sqlResult != SQLITE_OK)
    {
        errorHandler(sqlResult, "get nodes by fingerprint", false);
    }

    sqlite3_reset(mStmtNodesByFp);

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

void SqliteAccountState::userGetMimetype(sqlite3_context* context, int argc, sqlite3_value** argv)
{
    if (argc != 1)
    {
        LOG_err << "Invalid parameters for userGetMimetype";
        assert(argc == 1);
        sqlite3_result_int(context, MimeType_t::MIME_TYPE_UNKNOWN);
        return;
    }

    const char* fileName = reinterpret_cast<const char*>(sqlite3_value_text(argv[0]));
    string ext;
    int result = (fileName && *fileName && Node::getExtension(ext, fileName) && !ext.empty()) ?
                 Node::getMimetype(ext) : MimeType_t::MIME_TYPE_OTHERS;
    sqlite3_result_int(context, result);
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
    // Versioning
    constexpr int64_t versionFlag = 1 << Node::FLAGS_IS_VERSION;
    const int64_t flags = sqlite3_value_int64(argv[1]);
    if ((flags & versionFlag) != 0)
        return;

    auto filter = static_cast<const NodeSearchFilter*>(
        sqlite3_value_pointer(argv[0], NodeSearchFilterPtrStr));

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
    constexpr int64_t sensitivityFlag = 1 << Node::FLAGS_IS_MARKED_SENSTIVE;
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
