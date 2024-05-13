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

SqliteDbAccess::SqliteDbAccess(const LocalPath& rootPath)
  : mRootPath(rootPath)
{
    assert(mRootPath.isAbsolute());
}

SqliteDbAccess::~SqliteDbAccess()
{
}

LocalPath SqliteDbAccess::databasePath(const FileSystemAccess& fsAccess,
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

DbTable *SqliteDbAccess::openTableWithNodes(PrnGen &rng, FileSystemAccess &fsAccess, const string &name, const int flags, DBErrorCallback dBErrorCallBack)
{
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

    // Create specific table for handle nodes
    std::string sql = "CREATE TABLE IF NOT EXISTS nodes (nodehandle int64 PRIMARY KEY NOT NULL, "
                      "parenthandle int64, name text, fingerprint BLOB, origFingerprint BLOB, "
                      "type tinyint, mimetype tinyint AS (getmimetype(name)) VIRTUAL, size int64, share tinyint, fav tinyint, "
                      "ctime int64, mtime int64 DEFAULT 0, flags int64, counter BLOB NOT NULL, node BLOB NOT NULL, "
                      "label tinyint DEFAULT 0, description text, tags text)";
    int result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    if (result)
    {
        LOG_err << "Data base error: " << sqlite3_errmsg(db);
        sqlite3_close(db);
        return nullptr;
    }

    // Add following columns to existing 'nodes' table that might not have them, and populate them if needed:
    vector<NewColumn> newCols{
        {"mtime",
         "int64 DEFAULT 0",
         NodeData::COMPONENT_MTIME,
         NewColumn::extractDataFromNodeData<MTimeType>      },
        {"label",
         "tinyint DEFAULT 0",
         NodeData::COMPONENT_LABEL,
         NewColumn::extractDataFromNodeData<LabelType>      },
        {"mimetype",
         "tinyint AS (getmimetype(name)) VIRTUAL",
         NodeData::COMPONENT_NONE,
         nullptr                                            },
        {"description",
         "text",
         NodeData::COMPONENT_DESCRIPTION,
         NewColumn::extractDataFromNodeData<DescriptionType>},
        {"tags",
         "text",
         NodeData::COMPONENT_TAGS,
         NewColumn::extractDataFromNodeData<TagsType>       },
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

    result = sqlite3_create_function(db, "ismimetype", 2, SQLITE_ANY,0, &SqliteAccountState::userIsMimetype, 0, 0);
    if (result)
    {
        LOG_err << "Data base error(sqlite3_create_function userIsMimetype): " << sqlite3_errmsg(db);
        sqlite3_close(db);
        return nullptr;
    }

    result = sqlite3_create_function(db, "isContained", 2, SQLITE_ANY,0, &SqliteAccountState::userIsContained, 0, 0);
    if (result)
    {
        LOG_err << "Data base error(sqlite3_create_function userIsContained): " << sqlite3_errmsg(db);
        sqlite3_close(db);
        return nullptr;
    }

    result = sqlite3_create_function(db, "matchTag", 2, SQLITE_ANY,0, &SqliteAccountState::userMatchTag, 0, 0);
    if (result)
    {
        LOG_err << "Data base error(sqlite3_create_function userMatchTag): " << sqlite3_errmsg(db);
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
    std::map<int, int> dataToMigrate;
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
        handle nh = sqlite3_column_int64(stmt, 0);
        NodeData nd(blob, blobSize, NodeData::COMPONENT_ATTRS);

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
        if (sqlite3_bind_int64(stmt, static_cast<int>(cols.size()) + 1, update.first) != SQLITE_OK || // nodehandle
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
        abort();
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

    *index = sqlite3_column_int(pStmt, 0);

    data->assign((char*)sqlite3_column_blob(pStmt, 1), sqlite3_column_bytes(pStmt, 1));

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
        rc = sqlite3_bind_int(stmt, 1, index);
        if (rc == SQLITE_OK)
        {
            rc = sqlite3_step(stmt);
            if (rc == SQLITE_ROW)
            {
                data->assign((char*)sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0));
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
        sqlResult = sqlite3_bind_int(mPutStmt, 1, index);
        if (sqlResult == SQLITE_OK)
        {
            sqlResult = sqlite3_bind_blob(mPutStmt, 2, data, len, SQLITE_STATIC);
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
        sqlResult = sqlite3_bind_int(mDelStmt, 1, index);
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

bool SqliteAccountState::processSqlQueryNodes(sqlite3_stmt *stmt, std::vector<std::pair<mega::NodeHandle, mega::NodeSerialized>>& nodes)
{
    assert(stmt);
    int sqlResult = SQLITE_ERROR;
    while ((sqlResult = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        NodeHandle nodeHandle;
        nodeHandle.set6byte(sqlite3_column_int64(stmt, 0));

        NodeSerialized node;

        // Blob node counter
        const void* data = sqlite3_column_blob(stmt, 1);
        int size = sqlite3_column_bytes(stmt, 1);
        if (data && size)
        {
            node.mNodeCounter = std::string(static_cast<const char*>(data), size);
        }

        // blob node
        data = sqlite3_column_blob(stmt, 2);
        size = sqlite3_column_bytes(stmt, 2);
        if (data && size)
        {
            node.mNode = std::string(static_cast<const char*>(data), size);
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
            if ((sqlResult = sqlite3_bind_int64(mStmtUpdateNode, 2, nodeHandle.as8byte())) == SQLITE_OK)
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
            if ((sqlResult = sqlite3_bind_int64(mStmtUpdateNodeAndFlags, 2, flags)) == SQLITE_OK)
            {
                if ((sqlResult = sqlite3_bind_int64(mStmtUpdateNodeAndFlags, 3, nodeHandle.as8byte())) == SQLITE_OK)
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

    sqlite3_finalize(mStmtChildren);
    mStmtChildren = nullptr;

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

    sqlite3_finalize(mStmtNodeByName);
    mStmtNodeByName = nullptr;

    sqlite3_finalize(mStmtNodeByNameNoRecursive);
    mStmtNodeByNameNoRecursive = nullptr;

    sqlite3_finalize(mStmtInShareOutShareByName);
    mStmtInShareOutShareByName = nullptr;

    sqlite3_finalize(mStmtNodeByMimeType);
    mStmtNodeByMimeType = nullptr;

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
        sqlResult = sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO nodes (nodehandle, parenthandle, "
                                           "name, fingerprint, origFingerprint, type, size, share, fav, ctime, mtime, flags, counter, node, label, description, tags) "
                                           "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", -1, &mStmtPutNode, NULL);
    }

    if (sqlResult == SQLITE_OK)
    {
        string nodeSerialized;
        node->serialize(&nodeSerialized);
        assert(nodeSerialized.size());

        sqlite3_bind_int64(mStmtPutNode, 1, node->nodehandle);
        sqlite3_bind_int64(mStmtPutNode, 2, node->parenthandle);

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
        sqlite3_bind_int64(mStmtPutNode, 7, node->size);

        int shareType = node->getShareType();
        sqlite3_bind_int(mStmtPutNode, 8, shareType);

        // node->attrstring has value => node is encrypted
        nameid favId = AttrMap::string2nameid("fav");
        auto favIt = node->attrs.map.find(favId);
        bool fav = (favIt != node->attrs.map.end() && favIt->second == "1"); // test 'fav' attr value (only "1" is valid)
        sqlite3_bind_int(mStmtPutNode, 9, fav);
        sqlite3_bind_int64(mStmtPutNode, 10, node->ctime);
        sqlite3_bind_int64(mStmtPutNode, 11, node->mtime);
        sqlite3_bind_int64(mStmtPutNode, 12, node->getDBFlags());
        std::string nodeCountersBlob = node->getCounter().serialize();
        sqlite3_bind_blob(mStmtPutNode, 13, nodeCountersBlob.data(), static_cast<int>(nodeCountersBlob.size()), SQLITE_STATIC);
        sqlite3_bind_blob(mStmtPutNode, 14, nodeSerialized.data(), static_cast<int>(nodeSerialized.size()), SQLITE_STATIC);

        static nameid labelId = AttrMap::string2nameid("lbl");
        auto labelIt = node->attrs.map.find(labelId);
        int label = (labelIt == node->attrs.map.end()) ? LBL_UNKNOWN : std::atoi(labelIt->second.c_str());
        sqlite3_bind_int(mStmtPutNode, 15, label);

        nameid descriptionId = AttrMap::string2nameid(MegaClient::NODE_ATTRIBUTE_DESCRIPTION);
        if (auto descriptionIt = node->attrs.map.find(descriptionId);
            descriptionIt != node->attrs.map.end())
        {
            const std::string& description = descriptionIt->second;
            sqlite3_bind_text(mStmtPutNode,
                              16,
                              description.c_str(),
                              static_cast<int>(description.length()),
                              SQLITE_STATIC);
        }
        else
        {
            sqlite3_bind_null(mStmtPutNode, 16);
        }

        nameid tagId = AttrMap::string2nameid(MegaClient::NODE_ATTRIBUTE_TAGS);
        if (auto tagIt = node->attrs.map.find(tagId); tagIt != node->attrs.map.end())
        {
            const std::string& tag = tagIt->second;
            sqlite3_bind_text(mStmtPutNode,
                              17,
                              tag.c_str(),
                              static_cast<int>(tag.length()),
                              SQLITE_STATIC);
        }
        else
        {
            sqlite3_bind_null(mStmtPutNode, 17);
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
        if ((sqlResult = sqlite3_bind_int64(mStmtGetNode, 1, nodehandle.as8byte())) == SQLITE_OK)
        {
            if((sqlResult = sqlite3_step(mStmtGetNode)) == SQLITE_ROW)
            {
                const void* dataNodeCounter = sqlite3_column_blob(mStmtGetNode, 0);
                int sizeNodeCounter = sqlite3_column_bytes(mStmtGetNode, 0);

                const void* dataNodeSerialized = sqlite3_column_blob(mStmtGetNode, 1);
                int sizeNodeSerialized = sqlite3_column_bytes(mStmtGetNode, 1);

                if (dataNodeCounter && sizeNodeCounter && dataNodeSerialized && sizeNodeSerialized)
                {
                    nodeSerialized.mNodeCounter.assign(static_cast<const char*>(dataNodeCounter), sizeNodeCounter);
                    nodeSerialized.mNode.assign(static_cast<const char*>(dataNodeSerialized), sizeNodeSerialized);
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

/** @deprecated */
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

/** @deprecated */
bool SqliteAccountState::getChildren(NodeHandle parentHandle, std::vector<std::pair<NodeHandle, NodeSerialized>>& children, CancelToken cancelFlag)
{
    if (!db)
    {
        return false;
    }

    if (cancelFlag.exists())
    {
        sqlite3_progress_handler(db, NUM_VIRTUAL_MACHINE_INSTRUCTIONS, SqliteAccountState::progressHandler, static_cast<void*>(&cancelFlag));
    }

    int sqlResult = SQLITE_OK;
    if (!mStmtChildren)
    {
        sqlResult = sqlite3_prepare_v2(db, "SELECT nodehandle, counter, node FROM nodes WHERE parenthandle = ?", -1, &mStmtChildren, NULL);
    }

    bool result = false;
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int64(mStmtChildren, 1, parentHandle.as8byte())) == SQLITE_OK)
        {
            result = processSqlQueryNodes(mStmtChildren, children);
        }
    }

    // unregister the handler (no-op if not registered)
    sqlite3_progress_handler(db, -1, nullptr, nullptr);

    errorHandler(sqlResult, "Get children", true);

    sqlite3_reset(mStmtChildren);

    return result;
}

bool SqliteAccountState::getChildrenFromType(NodeHandle parentHandle, nodetype_t nodeType, std::vector<std::pair<NodeHandle, NodeSerialized> >& children, CancelToken cancelFlag)
{
    if (!db)
    {
        return false;
    }

    if (cancelFlag.exists())
    {
        sqlite3_progress_handler(db, NUM_VIRTUAL_MACHINE_INSTRUCTIONS, SqliteAccountState::progressHandler, static_cast<void*>(&cancelFlag));
    }

    int sqlResult = SQLITE_OK;

    if (!mStmtChildrenFromType)
    {
        sqlResult = sqlite3_prepare_v2(db, "SELECT nodehandle, counter, node FROM nodes WHERE parenthandle = ? AND type = ?", -1, &mStmtChildrenFromType, NULL);
    }

    bool result = false;
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int64(mStmtChildrenFromType, 1, parentHandle.as8byte())) == SQLITE_OK)
        {
            if ((sqlResult = sqlite3_bind_int(mStmtChildrenFromType, 2, nodeType)) == SQLITE_OK)
            {
                result = processSqlQueryNodes(mStmtChildrenFromType, children);
            }
        }
    }

    // unregister the handler (no-op if not registered)
    sqlite3_progress_handler(db, -1, nullptr, nullptr);

    errorHandler(sqlResult, "Get children from type", true);

    sqlite3_reset(mStmtChildrenFromType);

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
        if ((sqlResult = sqlite3_bind_int64(mStmtNumChildren, 1, parentHandle.as8byte())) == SQLITE_OK)
        {
            if ((sqlResult = sqlite3_step(mStmtNumChildren)) == SQLITE_ROW)
            {
               numChildren = sqlite3_column_int64(mStmtNumChildren, 0);
            }
        }
    }

    errorHandler(sqlResult, "Get number of children", false);

    sqlite3_reset(mStmtNumChildren);

    return numChildren;
}

bool SqliteAccountState::getChildren(const mega::NodeSearchFilter& filter, int order, vector<pair<NodeHandle, NodeSerialized>>& children, CancelToken cancelFlag, const NodeSearchPage& page)
{
    if (!db)
    {
        return false;
    }

    if (cancelFlag.exists())
    {
        sqlite3_progress_handler(db, NUM_VIRTUAL_MACHINE_INSTRUCTIONS, SqliteAccountState::progressHandler, static_cast<void*>(&cancelFlag));
    }

    // There are 2 criteria used (so far) in ORDER BY clause.
    // For every combination of order-by directions, a separate query will be necessary.
    size_t cacheId = OrderByClause::getId(order);
    sqlite3_stmt*& stmt = mStmtGetChildren[cacheId];

    int sqlResult = SQLITE_OK;
    if (!stmt)
    {
        // Inherited sensitivity is not a concern here. When filtering out sensitive nodes, the parent of all children
        // would be checked before getting here. There's no point in making this query recursive just because of that.
        std::string sqlQuery = "SELECT nodehandle, counter, node "
                               "FROM nodes "
                               "WHERE (flags & ? = 0) "
                                 "AND (parenthandle = ?) "
                                 "AND (?3 = " + std::to_string(TYPE_UNKNOWN) + " OR type = ?3) "
                                 "AND (?4 = 0 OR ?4 < ctime) AND (?5 = 0 OR ctime < ?5) "
                                 "AND (?6 = 0 OR ?6 < mtime) AND (?7 = 0 OR (0 < mtime AND mtime < ?7)) " // mtime is not used (0) for some nodes
                                 "AND (?8 = " + std::to_string(MIME_TYPE_UNKNOWN) +
                                     " OR (type = " + std::to_string(FILENODE) +
                                         " AND ((?8 = " + std::to_string(MIME_TYPE_ALL_DOCS) +
                                               " AND mimetype IN (" + std::to_string(MIME_TYPE_DOCUMENT) +
                                                                ',' + std::to_string(MIME_TYPE_PDF) +
                                                                ',' + std::to_string(MIME_TYPE_PRESENTATION) +
                                                                ',' + std::to_string(MIME_TYPE_SPREADSHEET) + "))"
                                              " OR mimetype = ?8))) "
                                 "AND (?11 = 0 OR (name REGEXP ?9)) "
                                 "AND (?14 = 0 OR isContained(?15, description)) "
                                 "AND (?16 = 0 OR matchTag(?17, tags))"
                                 // Leading and trailing '*' will be added to argument '?' so we are looking for substrings containing name
                                 // Our REGEXP implementation is case insensitive

                               "ORDER BY \n" +
                                  OrderByClause::get(order, 10) + " \n" + // use ?10 for bound value

                               "LIMIT ?12 OFFSET ?13";

        sqlResult = sqlite3_prepare_v2(db, sqlQuery.c_str(), -1, &stmt, NULL);
    }

    bool result = false;
    uint64_t flags = (1 << Node::FLAGS_IS_VERSION) | // exclude file versions
                     (filter.bySensitivity() ? (1 << Node::FLAGS_IS_MARKED_SENSTIVE) : 0); // filter by sensitivity

    if (sqlResult == SQLITE_OK &&
        (sqlResult = sqlite3_bind_int64(stmt, 1, flags)) == SQLITE_OK &&
        (sqlResult = sqlite3_bind_int64(stmt, 2, filter.byParentHandle())) == SQLITE_OK &&
        (sqlResult = sqlite3_bind_int(stmt, 3, filter.byNodeType())) == SQLITE_OK &&
        (sqlResult = sqlite3_bind_int64(stmt, 4, filter.byCreationTimeLowerLimit())) == SQLITE_OK &&
        (sqlResult = sqlite3_bind_int64(stmt, 5, filter.byCreationTimeUpperLimit())) == SQLITE_OK &&
        (sqlResult = sqlite3_bind_int64(stmt, 6, filter.byModificationTimeLowerLimit())) == SQLITE_OK &&
        (sqlResult = sqlite3_bind_int64(stmt, 7, filter.byModificationTimeUpperLimit())) == SQLITE_OK &&
        (sqlResult = sqlite3_bind_int(stmt, 8, filter.byCategory())) == SQLITE_OK)
    {
        const string& nameFilter = filter.byName();
        bool matchWildcard = std::any_of(nameFilter.begin(), nameFilter.end(), [](const char& c) { return c != '*'; });
        const string& wildCardName = matchWildcard ? '*' + filter.byName() + '*' : nameFilter;
        if ((sqlResult = sqlite3_bind_text(stmt, 9, wildCardName.c_str(), static_cast<int>(wildCardName.length()), SQLITE_STATIC)) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_int(stmt, 10, order)) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_int(stmt, 11, matchWildcard)) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_int64(stmt, 12, page.size() ? static_cast<sqlite3_int64>(page.size()) : -1)) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_int64(stmt, 13, page.startingOffset())) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_int(stmt, 14, static_cast<int>(filter.byDescription().size()))) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_text(stmt, 15, filter.byDescription().c_str(), static_cast<int>(filter.byDescription().size()), SQLITE_STATIC)) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_int(stmt, 16, static_cast<int>(filter.byTag().size()))) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_text(stmt, 17, filter.byTag().c_str(), static_cast<int>(filter.byTag().size()), SQLITE_STATIC)) == SQLITE_OK)
        {
            result = processSqlQueryNodes(stmt, children);
        }
    }

    // unregister the handler (no-op if not registered)
    sqlite3_progress_handler(db, -1, nullptr, nullptr);

    string errMsg("Get children with filter");
    errorHandler(sqlResult, errMsg, true);

    sqlite3_reset(stmt);

    return result;
}

bool SqliteAccountState::searchNodes(const NodeSearchFilter& filter, int order, vector<pair<NodeHandle, NodeSerialized>>& nodes, CancelToken cancelFlag, const NodeSearchPage& page)
{
    if (!db)
    {
        return false;
    }

    if (cancelFlag.exists())
    {
        sqlite3_progress_handler(db, NUM_VIRTUAL_MACHINE_INSTRUCTIONS, SqliteAccountState::progressHandler, static_cast<void*>(&cancelFlag));
    }

    // There are multiple criteria used in ORDER BY clause.
    // For every combination of order-by directions, a separate query will be necessary.
    size_t cacheId = OrderByClause::getId(order);
    sqlite3_stmt*& stmt = mStmtSearchNodes[cacheId];

    int sqlResult = SQLITE_OK;
    if (!stmt)
    {
        string undefStr{ std::to_string(static_cast<sqlite3_int64>(UNDEF)) };

        string ancestors =
            "ancestors(nodehandle) \n"
            "AS (SELECT nodehandle FROM nodes \n"
                "WHERE (?11 != " + undefStr + " AND nodehandle = ?11) "
                   "OR (?12 != " + undefStr + " AND nodehandle = ?12) "
                   "OR (?16 != " + undefStr + " AND nodehandle = ?16) "
                   "OR (?7 != " + std::to_string(NO_SHARES) +
                      " AND nodehandle IN (SELECT nodehandle FROM nodes WHERE share = ?7)))";

        string columnsForNodeAndFilters =
            "nodehandle, parenthandle, flags, name, type, counter, node, size, ctime, mtime, share, mimetype, fav, label, description, tags";

        string nodesOfShares =
            "nodesOfShares(" + columnsForNodeAndFilters + ") \n"
            "AS (SELECT " + columnsForNodeAndFilters + " \n"
                "FROM nodes \n"
                "WHERE ?7 != " + std::to_string(NO_SHARES) + " AND share = ?7)";

        string nodesCTE =
            "nodesCTE(" + columnsForNodeAndFilters + ") \n"
            "AS (SELECT " + columnsForNodeAndFilters + " \n"
                "FROM nodes \n"
                "WHERE parenthandle IN (SELECT nodehandle FROM ancestors) \n"
                "UNION ALL \n"
                "SELECT N.nodehandle, N.parenthandle, N.flags, N.name, N.type, N.counter, N.node, "
                "N.size, N.ctime, N.mtime, N.share, N.mimetype, N.fav, N.label, N.description, N.tags \n"
                "FROM nodes AS N \n"
                "INNER JOIN nodesCTE AS P \n"
                        "ON (N.parenthandle = P.nodehandle \n"
                       "AND (P.flags & ?1 = 0) \n"
                       "AND P.type != " + std::to_string(FILENODE) + "))";

        string columnsForNodeAndOrderBy =
            "nodehandle, counter, node, " // for nodes
            "type, size, ctime, mtime, name, label, fav"; // for ORDER BY only

        string whereClause =
            "(flags & ?1 = 0) \n"
            "AND (?2 = " + std::to_string(TYPE_UNKNOWN) + " OR type = ?2) \n"
            "AND (?3 = 0 OR ?3 < ctime) AND (?4 = 0 OR ctime < ?4) \n"
            "AND (?5 = 0 OR ?5 < mtime) AND (?6 = 0 OR (0 < mtime AND mtime < ?6)) \n" // mtime is not used (0) for some nodes
            "AND (?8 = " + std::to_string(MIME_TYPE_UNKNOWN) +
                " OR (type = " + std::to_string(FILENODE) +
                    " AND ((?8 = " + std::to_string(MIME_TYPE_ALL_DOCS) +
                          " AND mimetype IN (" + std::to_string(MIME_TYPE_DOCUMENT) +
                                           ',' + std::to_string(MIME_TYPE_PDF) +
                                           ',' + std::to_string(MIME_TYPE_PRESENTATION) +
                                           ',' + std::to_string(MIME_TYPE_SPREADSHEET) + "))"
                         " OR mimetype = ?8))) \n"
            "AND (?13 = 0 OR (name REGEXP ?9)) \n"
            "AND (?17 = 0 OR isContained(?18, description)) \n"
            "AND (?19 = 0 OR matchTag(?20, tags))";
            // Leading and trailing '*' will be added to argument '?' so we are looking for substrings containing name
            // Our REGEXP implementation is case insensitive

        string nodesAfterFilters =
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
        std::string query =
            "WITH \n\n" +
             ancestors + ", \n\n" +
             nodesOfShares + ", \n\n" +
             nodesCTE + ", \n\n" +
             nodesAfterFilters + "\n\n" +

            "SELECT " + columnsForNodeAndOrderBy + " \n"
            "FROM nodesAfterFilters \n"
            "ORDER BY \n" + OrderByClause::get(order, 10) + " \n" + // use ?10 for bound value
            "LIMIT ?14 OFFSET ?15";

        sqlResult = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, NULL);
    }

    bool result = false;
    uint64_t excludeFlags = (1 << Node::FLAGS_IS_VERSION) | // exclude file versions
                            (filter.bySensitivity() ? (1 << Node::FLAGS_IS_MARKED_SENSTIVE) : 0); // filter by sensitivity

    if (sqlResult == SQLITE_OK &&
        (sqlResult = sqlite3_bind_int64(stmt, 1, excludeFlags)) == SQLITE_OK &&
        (sqlResult = sqlite3_bind_int(stmt, 2, filter.byNodeType())) == SQLITE_OK &&
        (sqlResult = sqlite3_bind_int64(stmt, 3, filter.byCreationTimeLowerLimit())) == SQLITE_OK &&
        (sqlResult = sqlite3_bind_int64(stmt, 4, filter.byCreationTimeUpperLimit())) == SQLITE_OK &&
        (sqlResult = sqlite3_bind_int64(stmt, 5, filter.byModificationTimeLowerLimit())) == SQLITE_OK &&
        (sqlResult = sqlite3_bind_int64(stmt, 6, filter.byModificationTimeUpperLimit())) == SQLITE_OK &&
        (sqlResult = sqlite3_bind_int(stmt, 7, filter.includedShares())) == SQLITE_OK &&
        (sqlResult = sqlite3_bind_int(stmt, 8, filter.byCategory())) == SQLITE_OK)
    {
        assert(filter.byAncestorHandles().size() >= 3); // support at least 3 ancestors
        const string& byName = filter.byName();
        bool matchWildcard = std::any_of(byName.begin(), byName.end(), [](const char& c) { return c != '*'; });
        const string& nameFilter = matchWildcard ? '*' + byName + '*' : byName;
        if ((sqlResult = sqlite3_bind_text(stmt, 9, nameFilter.c_str(), static_cast<int>(nameFilter.size()), SQLITE_STATIC)) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_int(stmt, 10, order)) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_int64(stmt, 11, filter.byAncestorHandles()[0])) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_int64(stmt, 12, filter.byAncestorHandles()[1])) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_int(stmt, 13, matchWildcard)) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_int64(stmt, 14, page.size() ? static_cast<sqlite3_int64>(page.size()) : -1)) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_int64(stmt, 15, page.startingOffset())) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_int64(stmt, 16, filter.byAncestorHandles()[2])) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_int(stmt, 17, static_cast<int>(filter.byDescription().size()))) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_text(stmt, 18, filter.byDescription().c_str(), static_cast<int>(filter.byDescription().size()), SQLITE_STATIC)) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_int(stmt, 19, static_cast<int>(filter.byTag().size()))) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_text(stmt, 20, filter.byTag().c_str(), static_cast<int>(filter.byTag().size()), SQLITE_STATIC)) == SQLITE_OK)
        {
            result = processSqlQueryNodes(stmt, nodes);
        }
    }

    // unregister the handler (no-op if not registered)
    sqlite3_progress_handler(db, -1, nullptr, nullptr);

    errorHandler(sqlResult, "Search nodes with filter", true);

    sqlite3_reset(stmt);

    return result;
}

/** @deprecated */
bool SqliteAccountState::searchForNodesByName(const std::string &name, std::vector<std::pair<NodeHandle, NodeSerialized>> &nodes, CancelToken cancelFlag)
{
    if (!db)
    {
        return false;
    }

    assert(name != "");

    if (cancelFlag.exists())
    {
        sqlite3_progress_handler(db, NUM_VIRTUAL_MACHINE_INSTRUCTIONS, SqliteAccountState::progressHandler, static_cast<void*>(&cancelFlag));
    }

    int sqlResult = SQLITE_OK;
    if (!mStmtNodeByName)
    {
        uint64_t excludeFlags = (1 << Node::FLAGS_IS_VERSION);
        std::string sqlQuery = "SELECT n1.nodehandle, n1.counter, n1.node "
                               "FROM nodes n1 "
                               "WHERE n1.flags & " + std::to_string(excludeFlags) + " = 0 AND n1.name REGEXP ?";
        // Leading and trailing '*' will be added to argument '?' so we are looking for a substring of name
        // Our REGEXP implementation is case insensitive

        sqlResult = sqlite3_prepare_v2(db, sqlQuery.c_str(), -1, &mStmtNodeByName, NULL);
    }

    bool result = false;
    if (sqlResult == SQLITE_OK)
    {
        string wildCardName = "*" + name + "*";
        if ((sqlResult = sqlite3_bind_text(mStmtNodeByName, 1, wildCardName.c_str(), static_cast<int>(wildCardName.length()), SQLITE_STATIC)) == SQLITE_OK)
        {
            result = processSqlQueryNodes(mStmtNodeByName, nodes);
        }
    }

    // unregister the handler (no-op if not registered)
    sqlite3_progress_handler(db, -1, nullptr, nullptr);

    errorHandler(sqlResult, "Search nodes by name", true);

    sqlite3_reset(mStmtNodeByName);

    return result;
}

/** @deprecated */
bool SqliteAccountState::searchForNodesByNameNoRecursive(const std::string& name, std::vector<std::pair<NodeHandle, NodeSerialized> >& nodes, NodeHandle parentHandle, CancelToken cancelFlag)
{
    if (!db)
    {
        return false;
    }

    assert(!name.empty());

    if (cancelFlag.exists())
    {
        sqlite3_progress_handler(db, NUM_VIRTUAL_MACHINE_INSTRUCTIONS, SqliteAccountState::progressHandler, static_cast<void*>(&cancelFlag));
    }

    int sqlResult = SQLITE_OK;
    if (!mStmtNodeByNameNoRecursive)
    {
        std::string sqlQuery = "SELECT n1.nodehandle, n1.counter, n1.node "
                               "FROM nodes n1 "
                               "WHERE n1.parenthandle = ? AND n1.name REGEXP ?";
        // Leading and trailing '*' will be added to argument '?' so we are looking for a substring of name
        // Our REGEXP implementation is case insensitive

        sqlResult = sqlite3_prepare_v2(db, sqlQuery.c_str(), -1, &mStmtNodeByNameNoRecursive, NULL);
    }

    bool result = false;
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int64(mStmtNodeByNameNoRecursive, 1, parentHandle.as8byte())) == SQLITE_OK)
        {
            string wildCardName = "*" + name + "*";
            if ((sqlResult = sqlite3_bind_text(mStmtNodeByNameNoRecursive, 2, wildCardName.c_str(), static_cast<int>(wildCardName.length()), SQLITE_STATIC)) == SQLITE_OK)
            {
                result = processSqlQueryNodes(mStmtNodeByNameNoRecursive, nodes);
            }
        }
    }

    // unregister the handler (no-op if not registered)
    sqlite3_progress_handler(db, -1, nullptr, nullptr);

    errorHandler(sqlResult, "Search nodes by name without recursion", true);

    sqlite3_reset(mStmtNodeByNameNoRecursive);

    return result;
}

/** @deprecated */
bool SqliteAccountState::searchInShareOrOutShareByName(const std::string& name, std::vector<std::pair<NodeHandle, NodeSerialized> >& nodes, ShareType_t shareType, CancelToken cancelFlag)
{
    if (!db)
    {
        return false;
    }

    assert(!name.empty());

    if (cancelFlag.exists())
    {
        sqlite3_progress_handler(db, NUM_VIRTUAL_MACHINE_INSTRUCTIONS, SqliteAccountState::progressHandler, static_cast<void*>(&cancelFlag));
    }

    int sqlResult = SQLITE_OK;
    if (!mStmtInShareOutShareByName)
    {
        std::string sqlQuery = "SELECT n1.nodehandle, n1.counter, n1.node "
                               "FROM nodes n1 "
                               "WHERE n1.share = ? AND n1.name REGEXP ?";
        // Leading and trailing '*' will be added to argument '?' so we are looking for a substring of name
        // Our REGEXP implementation is case insensitive

        sqlResult = sqlite3_prepare_v2(db, sqlQuery.c_str(), -1, &mStmtInShareOutShareByName, NULL);
    }

    bool result = false;
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int(mStmtInShareOutShareByName, 1, static_cast<int>(shareType))) == SQLITE_OK)
        {
            string wildCardName = "*" + name + "*";
            if ((sqlResult = sqlite3_bind_text(mStmtInShareOutShareByName, 2, wildCardName.c_str(), static_cast<int>(wildCardName.length()), SQLITE_STATIC)) == SQLITE_OK)
            {
                result = processSqlQueryNodes(mStmtInShareOutShareByName, nodes);
            }
        }
    }

    // unregister the handler (no-op if not registered)
    sqlite3_progress_handler(db, -1, nullptr, nullptr);

    if (sqlResult != SQLITE_OK)
    {
        errorHandler(sqlResult, "Search shares or link by name", true);
    }

    sqlite3_reset(mStmtInShareOutShareByName);

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

bool SqliteAccountState::getRecentNodes(unsigned maxcount, m_time_t since, std::vector<std::pair<NodeHandle, NodeSerialized>>& nodes)
{
    if (!db)
    {
        return false;
    }

    const std::string filenode = std::to_string(FILENODE);
    uint64_t excludeFlags = (1 << Node::FLAGS_IS_VERSION | 1 << Node::FLAGS_IS_IN_RUBBISH);
    std::string sqlQuery =  "SELECT n1.nodehandle, n1.counter, n1.node "
                            "FROM nodes n1 "
                            "WHERE n1.flags & " + std::to_string(excludeFlags) + " = 0 AND n1.ctime >= ? AND n1.type = " + filenode + " "
                            "ORDER BY n1.ctime DESC LIMIT ?";

    int sqlResult = SQLITE_OK;
    if (!mStmtRecents)
    {
        sqlResult = sqlite3_prepare_v2(db, sqlQuery.c_str(), -1, &mStmtRecents, NULL);
    }

    bool stepResult = false;
    if (sqlResult == SQLITE_OK)
    {
        if (sqlResult == sqlite3_bind_int64(mStmtRecents, 1, since))
        {
            // LIMIT expression evaluates to a negative value, then there is no upper bound on the number of rows returned
            int64_t nodeCount = (maxcount > 0) ? static_cast<int64_t>(maxcount) : -1;
            if (sqlResult == sqlite3_bind_int64(mStmtRecents, 2, nodeCount))
            {
                stepResult = processSqlQueryNodes(mStmtRecents, nodes);
            }
        }
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
        if ((sqlResult = sqlite3_bind_int64(mStmtFavourites, 1, node.as8byte())) == SQLITE_OK)
        {
            while ((sqlResult = sqlite3_step(mStmtFavourites)) == SQLITE_ROW && (nodes.size() < count || count == 0))
            {
                nodes.push_back(NodeHandle().set6byte(sqlite3_column_int64(mStmtFavourites, 0)));
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
        if ((sqlResult = sqlite3_bind_int64(mStmtChildNode, 1, parentHandle.as8byte())) == SQLITE_OK)
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
        sqlResult = sqlite3_prepare_v2(db, "SELECT type, size, flags FROM nodes WHERE nodehandle = ?", -1, &mStmtTypeAndSizeNode, NULL);
    }

    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int64(mStmtTypeAndSizeNode, 1, node.as8byte())) == SQLITE_OK)
        {
            if ((sqlResult = sqlite3_step(mStmtTypeAndSizeNode)) == SQLITE_ROW)
            {
               nodeType = (nodetype_t)sqlite3_column_int(mStmtTypeAndSizeNode, 0);
               size = sqlite3_column_int64(mStmtTypeAndSizeNode, 1);
               oldFlags = sqlite3_column_int64(mStmtTypeAndSizeNode, 2);
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
        if ((sqlResult = sqlite3_bind_int64(mStmtIsAncestor, 1, node.as8byte())) == SQLITE_OK)
        {
            if ((sqlResult = sqlite3_bind_int64(mStmtIsAncestor, 2, ancestor.as8byte())) == SQLITE_OK)
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
            count = sqlite3_column_int64(stmt, 0);
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
        if ((sqlResult = sqlite3_bind_int64(mStmtNumChild, 1, parentHandle.as8byte())) == SQLITE_OK)
        {
            if ((sqlResult = sqlite3_bind_int(mStmtNumChild, 2, nodeType)) == SQLITE_OK)
            {
                if ((sqlResult = sqlite3_step(mStmtNumChild)) == SQLITE_ROW)
                {
                    count = sqlite3_column_int64(mStmtNumChild, 0);
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

/** @deprecated */
bool SqliteAccountState::getNodesByMimetype(MimeType_t mimeType, std::vector<std::pair<NodeHandle, NodeSerialized>>& nodes, Node::Flags requiredFlags, Node::Flags excludeFlags, CancelToken cancelFlag)
{
    if (!db)
    {
        return false;
    }

    if (cancelFlag.exists())
    {
        sqlite3_progress_handler(db, NUM_VIRTUAL_MACHINE_INSTRUCTIONS, SqliteAccountState::progressHandler, static_cast<void*>(&cancelFlag));
    }

    bool result = false;
    int sqlResult = SQLITE_OK;
    if (!mStmtNodeByMimeType)
    {
        // exclude previous versions <- parent handle is of type != FILENODE
        std::string query = "SELECT n1.nodehandle, n1.counter, n1.node FROM nodes n1 "
            "INNER JOIN nodes n2 on n2.nodehandle = n1.parenthandle where ismimetype(n1.name, ?) = 1 AND n1.flags & ? = ? AND n1.flags & ? = 0 AND n2.type !=";
        query.append(std::to_string(FILENODE))
            .append(" AND n1.type =")
            .append(std::to_string(FILENODE));

        sqlResult = sqlite3_prepare_v2(db, query.c_str(), -1, &mStmtNodeByMimeType, nullptr);
    }
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int  (mStmtNodeByMimeType, 1, static_cast<int>(mimeType))) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_int64(mStmtNodeByMimeType, 2, static_cast<sqlite3_int64>(requiredFlags.to_ulong()))) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_int64(mStmtNodeByMimeType, 3, static_cast<sqlite3_int64>(requiredFlags.to_ulong()))) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_int64(mStmtNodeByMimeType, 4, static_cast<sqlite3_int64>(excludeFlags.to_ullong()))) == SQLITE_OK)
        {
            result = processSqlQueryNodes(mStmtNodeByMimeType, nodes);
        }
    }

    // unregister the handler (no-op if not registered)
    sqlite3_progress_handler(db, -1, nullptr, nullptr);

    if (sqlResult != SQLITE_OK)
    {
        errorHandler(sqlResult, "Get nodes by mime type", true);
    }

    sqlite3_reset(mStmtNodeByMimeType);

    return result;
}


/** @deprecated */
bool SqliteAccountState::getNodesByMimetypeExclusiveRecursive(MimeType_t mimeType, std::vector<std::pair<NodeHandle, NodeSerialized>>& nodes, Node::Flags requiredFlags, Node::Flags excludeFlags, Node::Flags excludeRecursiveFlags, NodeHandle ancestorHandle, CancelToken cancelFlag)
{
    assert(!ancestorHandle.isUndef());
    // must recurse from a specfic point

    if (!db)
    {
        return false;
    }

    if (cancelFlag.exists())
    {
        sqlite3_progress_handler(db, NUM_VIRTUAL_MACHINE_INSTRUCTIONS, SqliteAccountState::progressHandler, static_cast<void*>(&cancelFlag));
    }

    bool result = false;
    int sqlResult = SQLITE_OK;

    if (!mStmtNodeByMimeTypeExcludeRecursiveFlags)
    {
        // recursive query from ancestorHandle
        // do not recurse if parent type is FILENODE (that would be version)
        // do not recurse if sensative flag set
        // exclude previous versions <- parent handle is of type != FILENODE
        //query = "SELECT nodehandle, counter, node FROM nodes";

        std::string query = "WITH nodesCTE(nodehandle, parenthandle, flags, name, type, counter, node) AS (SELECT nodehandle, parenthandle, flags, name, type, counter, node "
            "FROM nodes WHERE parenthandle = ? UNION ALL SELECT N.nodehandle, N.parenthandle, N.flags, N.name, N.type, N.counter, N.node "
            "FROM nodes AS N INNER JOIN nodesCTE AS P ON (N.parenthandle = P.nodehandle AND N.flags & ? = 0)) "
            "SELECT node.nodehandle, node.counter, node.node "
            "FROM nodesCTE AS node INNER JOIN nodes parent on node.parenthandle = parent.nodehandle AND ismimetype(node.name, ?) = 1 AND node.flags & ? = ? AND node.flags & ? = 0 AND parent.type != "
                            + std::to_string(FILENODE) + " AND node.type = " + std::to_string(FILENODE);

        sqlResult = sqlite3_prepare_v2(db, query.c_str(), -1, &mStmtNodeByMimeTypeExcludeRecursiveFlags, nullptr);
    }

    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int64(mStmtNodeByMimeTypeExcludeRecursiveFlags, 1, ancestorHandle.as8byte())) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_int64(mStmtNodeByMimeTypeExcludeRecursiveFlags, 2, static_cast<sqlite3_int64>(excludeRecursiveFlags.to_ulong()))) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_int  (mStmtNodeByMimeTypeExcludeRecursiveFlags, 3, static_cast<int>(mimeType))) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_int64(mStmtNodeByMimeTypeExcludeRecursiveFlags, 4, static_cast<sqlite3_int64>(requiredFlags.to_ulong()))) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_int64(mStmtNodeByMimeTypeExcludeRecursiveFlags, 5, static_cast<sqlite3_int64>(requiredFlags.to_ulong()))) == SQLITE_OK &&
            (sqlResult = sqlite3_bind_int64(mStmtNodeByMimeTypeExcludeRecursiveFlags, 6, static_cast<sqlite3_int64>(excludeFlags.to_ulong()))) == SQLITE_OK)
        {
            result = processSqlQueryNodes(mStmtNodeByMimeTypeExcludeRecursiveFlags, nodes);
        }
    }

    // unregister the handler (no-op if not registered)
    sqlite3_progress_handler(db, -1, nullptr, nullptr);

    if (sqlResult != SQLITE_OK)
    {
        errorHandler(sqlResult, "Get by mime type exclusive recurisve", true);
    }

    sqlite3_reset(mStmtNodeByMimeTypeExcludeRecursiveFlags);

    return result;
}

void SqliteAccountState::userRegexp(sqlite3_context* context, int argc, sqlite3_value** argv)
{
    if (argc != 2)
    {
        LOG_err << "Invalid parameters for user Regexp";
        assert(false);
        return;
    }

    const uint8_t* pattern = static_cast<const uint8_t*>(sqlite3_value_text(argv[0]));
    const uint8_t* nameFromDataBase = static_cast<const uint8_t*>(sqlite3_value_text(argv[1]));
    if (nameFromDataBase && pattern)
    {
        int result = icuLikeCompare(pattern, nameFromDataBase, 0);
        sqlite3_result_int(context, result);
    }
}

void SqliteAccountState::userIsMimetype(sqlite3_context* context, int argc, sqlite3_value** argv)
{
    if (argc != 2)
    {
        LOG_err << "Invalid parameters for user isMimetype";
        assert(false);
        sqlite3_result_int(context, 0);
        return;
    }

    int result = 0;
    std::string name = argv[0] ? reinterpret_cast<const char*>(sqlite3_value_text(argv[0])) : "";
    int mimetype = argv[1] ? sqlite3_value_int(argv[1]) : MimeType_t::MIME_TYPE_UNKNOWN;
    if (name.size() && mimetype)
    {
        std::string ext;
        result = ((!Node::getExtension(ext, name) || ext.empty()) &&
                  mimetype == MimeType_t::MIME_TYPE_OTHERS) ||
                 Node::isOfMimetype(static_cast<MimeType_t>(mimetype), ext);

    }

    sqlite3_result_int(context, result);
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

void SqliteAccountState::userIsContained(sqlite3_context* context, int argc, sqlite3_value** argv)
{
    if (argc != 2)
    {
        LOG_err << "Invalid parameters for userIsContained";
        assert(false);
        sqlite3_result_int(context, 0);
        return;
    }

    const uint8_t* descriptionToCheck = static_cast<const uint8_t*>(sqlite3_value_text(argv[0]));
    const uint8_t* descriptionFromDataBase = static_cast<const uint8_t*>(sqlite3_value_text(argv[1]));
    if (!descriptionFromDataBase || !descriptionToCheck)
    {
        sqlite3_result_int(context, 0);
        return;
    }

    std::string stringToMatch(reinterpret_cast<const char*>(descriptionToCheck));
    std::string stringAddingWildcards =
        WILDCARD_MATCH_ALL + escapeWildCards(stringToMatch) + WILDCARD_MATCH_ALL;

    const uint8_t* pattern = reinterpret_cast<const uint8_t*>(stringAddingWildcards.c_str());
    int result = icuLikeCompare(pattern, descriptionFromDataBase, ESCAPE_CHARACTER);
    sqlite3_result_int(context, result);
}

 void SqliteAccountState::userMatchTag(sqlite3_context* context, int argc, sqlite3_value** argv)
{
    if (argc != 2)
    {
        LOG_err << "Invalid parameters for userMatchTag";
        assert(false);
        sqlite3_result_int(context, 0);
        return;
    }

    const uint8_t* tagToCheck = static_cast<const uint8_t*>(sqlite3_value_text(argv[0]));
    const uint8_t* tagsFromDataBase = static_cast<const uint8_t*>(sqlite3_value_text(argv[1]));
    if (!tagsFromDataBase || !tagToCheck)
    {
        sqlite3_result_int(context, 0);
        return;
    }

    std::string tags{reinterpret_cast<const char*>(tagsFromDataBase)};
    std::set<std::string> tokens = splitString(tags, MegaClient::TAG_DELIMITER);
    std::string tag{reinterpret_cast<const char*>(tagToCheck)};
    sqlite3_result_int(context, getTagPosition(tokens, tag) != tokens.end());
}

std::string OrderByClause::get(int order, int sqlParamIndex)
{
    std::string criteria1 =
        "WHEN " + std::to_string(DEFAULT_ASC)  + " THEN type \n"  // folders first
        "WHEN " + std::to_string(DEFAULT_DESC) + " THEN type \n"  // files first
        "WHEN " + std::to_string(SIZE_ASC)  + " THEN size \n"
        "WHEN " + std::to_string(SIZE_DESC) + " THEN size \n"
        "WHEN " + std::to_string(CTIME_ASC)  + " THEN ctime \n"
        "WHEN " + std::to_string(CTIME_DESC) + " THEN ctime \n"
        "WHEN " + std::to_string(MTIME_ASC)  + " THEN mtime \n"
        "WHEN " + std::to_string(MTIME_DESC) + " THEN mtime \n"
        "WHEN " + std::to_string(LABEL_ASC)  + " THEN type \n"    // folders first
        "WHEN " + std::to_string(LABEL_DESC) + " THEN type \n"    // folders first
        "WHEN " + std::to_string(FAV_ASC)  + " THEN type \n"      // folders first
        "WHEN " + std::to_string(FAV_DESC) + " THEN type \n";     // folders first
    std::string criteria2 =
        "WHEN " + std::to_string(DEFAULT_ASC)  + " THEN name \n"
        "WHEN " + std::to_string(DEFAULT_DESC) + " THEN name \n"
        "WHEN " + std::to_string(LABEL_ASC)  + " THEN label \n"
        "WHEN " + std::to_string(LABEL_DESC) + " THEN label \n"
        "WHEN " + std::to_string(FAV_ASC)  + " THEN fav \n"
        "WHEN " + std::to_string(FAV_DESC) + " THEN fav \n";

    std::bitset<3> dirs = getDescendingDirs(order);
    std::string direction1 = dirs[0] ? "DESC" : "";
    std::string direction2 = dirs[1] ? "DESC" : "";
    std::string direction3 = dirs[2] ? "DESC" : "";

    std::string x = '?' + std::to_string(sqlParamIndex) + ' ';

    std::string clause =
        "CASE " + x +
        criteria1 +
        "END " + direction1 + ", \n"
        "CASE " + x +
        criteria2 +
        "END " + direction2 + ", \n"
        // always order by PK last, to get the same order for identical queries
        "nodehandle " + direction3;

    return clause;
}

size_t OrderByClause::getId(int order)
{
    std::bitset<3> dirs = getDescendingDirs(order);
    size_t id = dirs.to_ulong();
    return id;
}

std::bitset<3> OrderByClause::getDescendingDirs(int order)
{
    std::bitset<3> dirs;

    switch (order)
    {
    case SIZE_DESC:
    case CTIME_DESC:
    case MTIME_DESC:
        dirs[2] = true; // DESC, by PK
        [[fallthrough]];
    case DEFAULT_ASC:
    case LABEL_ASC:
    case FAV_ASC:
        dirs[0] = true; break;
    case DEFAULT_DESC:
        dirs[1] = true;
        dirs[2] = true; // DESC, by PK
        break;
    case LABEL_DESC:
    case FAV_DESC:
        dirs[0] = true;
        dirs[1] = true;
        dirs[2] = true; // DESC, by PK
        break;
    case SIZE_ASC:
    case CTIME_ASC:
    case MTIME_ASC:
        break;
    }

    return dirs;
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
    std::unique_ptr<SqliteDbAccess::MigrateType> migrateType(new MTimeType(nd.getMtime()));
    return migrateType;
}

bool SqliteDbAccess::MTimeType::hasValidValue() const
{
    return mValue;
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
    std::unique_ptr<SqliteDbAccess::MigrateType> migrateType(new LabelType(nd.getLabel()));
    return migrateType;
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
    std::unique_ptr<SqliteDbAccess::MigrateType> migrateType(
        new DescriptionType(nd.getDescription()));
    return migrateType;
}

bool SqliteDbAccess::DescriptionType::hasValidValue() const
{
    return mValue.size();
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
    std::unique_ptr<SqliteDbAccess::MigrateType> migrateType(new TagsType(nd.getTags()));
    return migrateType;
}

bool SqliteDbAccess::TagsType::hasValidValue() const
{
    return mValue.size();
}

} // namespace

#endif
