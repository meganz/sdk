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

        if (fileAccess->fopen(legacyPath))
        {
            LOG_debug << "Found legacy database at: " << legacyPath;

            // if current version, use that one... unless migration to NoD is required
            if (currentDbVersion == LEGACY_DB_VERSION && LEGACY_DB_VERSION != LAST_DB_VERSION_WITHOUT_NOD)
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

SqliteDbTable *SqliteDbAccess::open(PrnGen &rng, FileSystemAccess &fsAccess, const string &name, const int flags)
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
                             (flags & DB_OPEN_FLAG_TRANSACTED) > 0);

}

DbTable *SqliteDbAccess::openTableWithNodes(PrnGen &rng, FileSystemAccess &fsAccess, const string &name, const int flags)
{
    sqlite3 *db = nullptr;
    auto dbPath = databasePath(fsAccess, name, DB_VERSION);
    if (!openDBAndCreateStatecache(&db, fsAccess, name, dbPath, flags))
    {
        return nullptr;
    }

    // Create specific table for handle nodes
    std::string sql = "CREATE TABLE IF NOT EXISTS nodes (nodehandle int64 PRIMARY KEY NOT NULL, "
                      "parenthandle int64, name text, fingerprint BLOB, origFingerprint BLOB, "
                      "type tinyint, size int64, share tinyint, fav tinyint, mimetype tinyint, "
                      "ctime int64, counter BLOB NOT NULL, node BLOB NOT NULL)";
    int result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    if (result)
    {
        LOG_debug << "Data base error: " << sqlite3_errmsg(db);
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

    return new SqliteAccountState(rng,
                                db,
                                fsAccess,
                                dbPath,
                                (flags & DB_OPEN_FLAG_TRANSACTED) > 0);
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
        | SQLITE_OPEN_FULLMUTEX // The new database connection will use the "Serialized" threading mode. This means that multiple threads can be used withou restriction. (Required to avoid failure at SyncTest)
        | SQLITE_OPEN_SHAREDCACHE // Allow shared uncommited data between connections
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

    auto fileAccess = fsAccess.newfileaccess();

#if !(TARGET_OS_IPHONE)
    auto suffix = LocalPath::fromRelativePath("-shm");
    auto from = legacyPath + suffix;
    auto to = dbPath + suffix;

    // -shm could or couldn't be present
    if (fileAccess->fopen(from) && !fsAccess.renamelocal(from, to))
    {
         // Exists origin and failure to rename
        LOG_debug << "Failure to rename -shm file";
        return false;
    }

    suffix = LocalPath::fromRelativePath("-wal");
    from = legacyPath + suffix;
    to = dbPath + suffix;

    // -wal could or couldn't be present
    if (fileAccess->fopen(from) && !fsAccess.renamelocal(from, to))
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
    if (fileAccess->fopen(from) && !fsAccess.renamelocal(from, to))
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

SqliteDbTable::SqliteDbTable(PrnGen &rng, sqlite3* db, FileSystemAccess &fsAccess, const LocalPath &path, const bool checkAlwaysTransacted)
  : DbTable(rng, checkAlwaysTransacted)
  , db(db)
  , pStmt(nullptr)
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

    int result;

    if (pStmt)
    {
        result = sqlite3_reset(pStmt);
    }
    else
    {
        result = sqlite3_prepare_v2(db, "SELECT id, content FROM statecache", -1, &pStmt, NULL);
    }

    if (result != SQLITE_OK)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(result));
        LOG_err << "Unable to rewind database: " << dbfile << err;
        assert(!"Unable to rewind database.");
    }
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

        if (rc != SQLITE_DONE)
        {
            string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(rc));
            LOG_err << "Unable to get next record from database: " << dbfile << err;
            assert(!"Unable to get next record from database.");
        }

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

    if (rc != SQLITE_DONE && rc != SQLITE_ROW)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(rc));
        LOG_err << "Unable to get record from database: " << dbfile << err;
        assert(!"Unable to get record from database.");
    }

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

    bool ok = sqlResult == SQLITE_DONE;

    if (!ok)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to put record into database: " << dbfile << err;
        assert(!"Unable to put record into database.");
    }

    sqlite3_reset(mPutStmt);

    return ok;
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

    bool ok = sqlResult == SQLITE_DONE || sqlResult == SQLITE_ROW;

    if (!ok)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to delete record from database: " << dbfile << err;
        assert(!"Unable to delete record from database.");
    }

    sqlite3_reset(mDelStmt);

    return ok;
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
    if (rc != API_OK)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(rc));
        LOG_err << "Unable to truncate database: " << dbfile << err;
        assert(!"Unable to truncate database.");
    }
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
    if (rc != SQLITE_OK)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(rc));
        LOG_err << "Unable to begin transaction on database: " << dbfile << err;
        assert(!"Unable to begin transaction on database.");
    }
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
    if (rc != SQLITE_OK)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(rc));
        LOG_err << "Unable to commit transaction on database: " << dbfile << err;
        assert(!"Unable to commit transaction on database.");
    }
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
    if (rc != SQLITE_OK)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(rc));
        LOG_err << "Unable to rollback transaction on database: " << dbfile << err;
        assert(!"Unable to rollback transaction on database.");
    }
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

SqliteAccountState::SqliteAccountState(PrnGen &rng, sqlite3 *pdb, FileSystemAccess &fsAccess, const LocalPath &path, const bool checkAlwaysTransacted)
    : SqliteDbTable(rng, pdb, fsAccess, path, checkAlwaysTransacted)
{
}

SqliteAccountState::~SqliteAccountState()
{
    sqlite3_finalize(mStmtPutNode);
    sqlite3_finalize(mStmtUpdateNode);
    sqlite3_finalize(mStmtTypeAndSizeNode);
    sqlite3_finalize(mStmtGetNode);
    sqlite3_finalize(mStmtChildren);
    sqlite3_finalize(mStmtChildrenFromType);
    sqlite3_finalize(mStmtNumChildren);
    sqlite3_finalize(mStmtNodeByName);
    sqlite3_finalize(mStmtNodeByMimeType);
    sqlite3_finalize(mStmtNodesByFp);
    sqlite3_finalize(mStmtNodeByFp);
    sqlite3_finalize(mStmtNodeByOrigFp);
    sqlite3_finalize(mStmtChildNode);
    sqlite3_finalize(mStmtIsAncestor);
    sqlite3_finalize(mStmtNumChild);
    sqlite3_finalize(mStmtRecents);
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

    if (sqlResult != SQLITE_DONE)
    {
        if (sqlResult == SQLITE_INTERRUPT)
        {
            LOG_debug << "Unable to processSqlQueryNodes, running the query has been interrupted";
        }
        else
        {
            string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
            LOG_err << "Unable to processSqlQueryNodes for database: " << dbfile << err;
        }
    }

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

    sprintf(buf, "DELETE FROM nodes WHERE nodehandle = %" PRId64, nodehandle.as8byte());

    int sqlResult = sqlite3_exec(db, buf, 0, 0, NULL);
    if (sqlResult == SQLITE_ERROR)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to remove a node from database: " << dbfile << err;
        assert(!"Unable to remove a node from database.");
    }

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
    if (sqlResult == SQLITE_ERROR)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to remove all nodes from database: " << dbfile << err;
        assert(!"Unable to remove all nodes from database.");
    }

    return sqlResult == SQLITE_OK;
}

void SqliteAccountState::updateCounter(NodeHandle nodeHandle, const std::string& nodeCounterBlob)
{
    if (!db)
    {
        return;
    }

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

    if (sqlResult != SQLITE_DONE && sqlResult != SQLITE_ROW)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to update counter in database: " << dbfile << err;
        assert(!"Unable to update counter in database: ");
    }

    sqlite3_reset(mStmtUpdateNode);
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
    sqlite3_finalize(mStmtPutNode);
    mStmtPutNode = nullptr;
    
    sqlite3_finalize(mStmtUpdateNode);
    mStmtUpdateNode = nullptr;
 
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

    sqlite3_finalize(mStmtNodeByName);
    mStmtNodeByName = nullptr;

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

    SqliteDbTable::remove();
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
                                           "name, fingerprint, origFingerprint, type, size, share, fav, mimetype, ctime, counter, node) "
                                           "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", -1, &mStmtPutNode, NULL);
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
        sqlite3_bind_int(mStmtPutNode, 10, node->getMimeType());
        sqlite3_bind_int64(mStmtPutNode, 11, node->ctime);
        std::string nodeCountersBlob = node->getCounter().serialize();
        sqlite3_bind_blob(mStmtPutNode, 12, nodeCountersBlob.data(), static_cast<int>(nodeCountersBlob.size()), SQLITE_STATIC);
        sqlite3_bind_blob(mStmtPutNode, 13, nodeSerialized.data(), static_cast<int>(nodeSerialized.size()), SQLITE_STATIC);

        sqlResult = sqlite3_step(mStmtPutNode);
    }

    if (sqlResult != SQLITE_DONE)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to put a node from database: " << dbfile << err;
        assert(!"Unable to put a node from database.");
    }

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
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get a node from database: " << dbfile << err;
        assert(!"Unable to get a node from database.");
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

    if (sqlResult != SQLITE_OK)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get nodes by origfingerprint from database: " << dbfile << err;
        assert(!"Unable to get nodes by origfingerprint from database.");
    }

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

    if (sqlResult != SQLITE_OK)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get root nodes from database: " << dbfile << err;
        assert(!"Unable to get root nodes from database.");
    }

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
    int sqlResult = sqlite3_prepare_v2(db, "SELECT nodehandle, counter, node FROM nodes WHERE share & ? > 0", -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int(stmt, 1, static_cast<int>(shareType))) == SQLITE_OK)
        {
            result = processSqlQueryNodes(stmt, nodes);
        }
    }

    if (sqlResult != SQLITE_OK)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get root nodes from database: " << dbfile << err;
        assert(!"Unable to get root nodes from database.");
    }

    sqlite3_finalize(stmt);

    return result;
}

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

    sqlite3_reset(mStmtChildren);

    if (sqlResult != SQLITE_OK)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get children from database: " << dbfile << err;
        assert(!"Unable to get children from database.");
    }

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

    sqlite3_reset(mStmtChildrenFromType);

    if (sqlResult != SQLITE_OK)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get children from database from type: " << dbfile << err;
        assert(!"Unable to get children from database from type.");
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
        if ((sqlResult = sqlite3_bind_int64(mStmtNumChildren, 1, parentHandle.as8byte())) == SQLITE_OK)
        {
            if ((sqlResult = sqlite3_step(mStmtNumChildren)) == SQLITE_ROW)
            {
               numChildren = sqlite3_column_int64(mStmtNumChildren, 0);
            }
        }
    }

    sqlite3_reset(mStmtNumChildren);

    if (sqlResult != SQLITE_DONE && sqlResult != SQLITE_ROW)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get number of children from database: " << dbfile << err;
        assert(!"Unable to get number of children from database.");
    }

    return numChildren;
}

bool SqliteAccountState::getNodesByName(const std::string &name, std::vector<std::pair<NodeHandle, NodeSerialized>> &nodes, CancelToken cancelFlag)
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
        // select nodes whose 'name', in lowercase, matches the 'name' received by parameter, in lowercase,
        // (with or without any additional char at the beginning and/or end of the name). The '%' is the wildcard in SQL
        // exclude previous versions <- n2.type != FILENODE
        std::string sqlQuery = "SELECT n1.nodehandle, n1.counter, n1.node FROM nodes n1  INNER JOIN nodes n2 on "
                               "n2.nodehandle = n1.parenthandle where LOWER(n1.name) LIKE LOWER(?) AND n2.type !=";
        sqlQuery.append(std::to_string(FILENODE));
        // TODO: lower() works only with ASCII chars. If we want to add support to names in UTF-8, a new
        // test should be added, in example to search for 'ñam' when there is a node called 'Ñam'
        sqlResult = sqlite3_prepare_v2(db, sqlQuery.c_str(), -1, &mStmtNodeByName, NULL);
    }

    bool result = false;
    if (sqlResult == SQLITE_OK)
    {
        string wildCardName = "%" + name + "%";
        if ((sqlResult = sqlite3_bind_text(mStmtNodeByName, 1, wildCardName.c_str(), static_cast<int>(wildCardName.length()), SQLITE_STATIC)) == SQLITE_OK)
        {
            result = processSqlQueryNodes(mStmtNodeByName, nodes);
        }
    }

    // unregister the handler (no-op if not registered)
    sqlite3_progress_handler(db, -1, nullptr, nullptr);

    if (sqlResult != SQLITE_OK)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get nodes by name from database: " << dbfile << err;
        assert(!"Unable to get nodes by name from database.");
    }

    sqlite3_reset(mStmtNodeByName);

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
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get nodes by fingerprint from database: " << dbfile << err;
        assert(!"Unable to get nodes by fingerprint from database.");
    }

    sqlite3_reset(mStmtNodesByFp);

    return result;

}

bool SqliteAccountState::getNodeByFingerprint(const std::string &fingerprint, mega::NodeSerialized &node)
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
            }
        }
    }

    if (sqlResult != SQLITE_OK)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get nodes by fingerprint from database: " << dbfile << err;
        assert(!"Unable to get nodes by fingerprint from database.");
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

    // exclude recent nodes that are in Rubbish Bin
    const std::string isInRubbish = "WITH nodesCTE(nodehandle, parenthandle, type) "
        "AS (SELECT nodehandle, parenthandle, type FROM nodes WHERE nodehandle = n1.nodehandle "
        "UNION ALL SELECT A.nodehandle, A.parenthandle, A.type FROM nodes AS A INNER JOIN nodesCTE "
        "AS E ON (A.nodehandle = E.parenthandle)) "
        "SELECT COUNT(nodehandle) FROM nodesCTE where type = " + std::to_string(RUBBISHNODE);
    const std::string filenode = std::to_string(FILENODE);

    std::string sqlQuery = "SELECT n1.nodehandle, n1.counter, n1.node, (" + isInRubbish + ") isinrubbish FROM nodes n1 "
        "LEFT JOIN nodes n2 on n2.nodehandle = n1.parenthandle"
        " where n1.type = " + filenode + " AND n1.ctime >= ? AND n2.type != " + filenode + " AND isinrubbish = 0"
        " ORDER BY n1.ctime DESC LIMIT ?";

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
        std::string err = std::string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get recent nodes from database: " << dbfile << err;
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

    // exclude previous versions <- n2.type != FILENODE
    sqlite3_stmt *stmt = nullptr;
    std::string sqlQuery = "WITH nodesCTE(nodehandle, parenthandle, fav) AS (SELECT nodehandle, parenthandle, fav "
                           "FROM nodes WHERE parenthandle = ? UNION ALL SELECT A.nodehandle, A.parenthandle, A.fav "
                           "FROM nodes AS A INNER JOIN nodesCTE AS E ON (A.parenthandle = E.nodehandle)) SELECT n1.nodehandle "
                           "FROM nodesCTE AS n1 INNER JOIN nodes n2 on n1.parenthandle = n2.nodehandle AND n1.fav=1 AND n2.type!=" + std::to_string(FILENODE);

    int sqlResult = sqlite3_prepare_v2(db, sqlQuery.c_str(), -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int64(stmt, 1, node.as8byte())) == SQLITE_OK)
        {
            while ((sqlResult = sqlite3_step(stmt)) == SQLITE_ROW && (nodes.size() < count || count == 0))
            {
                nodes.push_back(NodeHandle().set6byte(sqlite3_column_int64(stmt, 0)));
            }
        }
    }

    if (sqlResult != SQLITE_DONE && sqlResult != SQLITE_ROW)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get favourites from database: " << dbfile << err;
        assert(!"Unable to get favourites from database.");
    }

    sqlite3_finalize(stmt);

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
                    if((sqlResult = sqlite3_step(mStmtChildNode)) == SQLITE_ROW)
                    {
                        node.first.set6byte(sqlite3_column_int64(mStmtChildNode, 0));

                        const void* dataNodeCounter = sqlite3_column_blob(mStmtChildNode, 1);
                        int sizeNodeCounter = sqlite3_column_bytes(mStmtChildNode, 1);

                        const void* dataNodeSerialized = sqlite3_column_blob(mStmtChildNode, 2);
                        int sizeNodeSerialized = sqlite3_column_bytes(mStmtChildNode, 2);

                        if (dataNodeCounter && sizeNodeCounter && dataNodeSerialized && sizeNodeSerialized)
                        {
                            node.second.mNodeCounter.assign(static_cast<const char*>(dataNodeCounter), sizeNodeCounter);
                            node.second.mNode.assign(static_cast<const char*>(dataNodeSerialized), sizeNodeSerialized);
                            success = true;
                        }
                    }
                }
            }
        }
    }

    if (sqlResult != SQLITE_ROW && sqlResult != SQLITE_DONE)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get nodes by name and type from database: " << dbfile << err;
        assert(!"Unable to get node by name from database (Only search at first level).");
    }

    sqlite3_reset(mStmtChildNode);

    return success;
}

bool SqliteAccountState::getNodeSizeAndType(NodeHandle node, m_off_t& size, nodetype_t& nodeType)
{
    if (!db)
    {
        return false;
    }

    int sqlResult = SQLITE_OK;
    if (!mStmtTypeAndSizeNode)
    {
        sqlResult = sqlite3_prepare_v2(db, "SELECT type, size FROM nodes WHERE nodehandle = ?", -1, &mStmtTypeAndSizeNode, NULL);
    }

    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int64(mStmtTypeAndSizeNode, 1, node.as8byte())) == SQLITE_OK)
        {
            if ((sqlResult = sqlite3_step(mStmtTypeAndSizeNode)) == SQLITE_ROW)
            {
               nodeType = (nodetype_t)sqlite3_column_int(mStmtTypeAndSizeNode, 0);
               size = sqlite3_column_int64(mStmtTypeAndSizeNode, 1);
            }
        }
    }

    if (sqlResult != SQLITE_ROW && sqlResult != SQLITE_DONE)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get node type and size from database: " << dbfile << err;
        assert(!"Unable to get node type and size from database.");
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
        if (sqlResult == SQLITE_INTERRUPT)
        {
            LOG_debug << "Unable to get `isAncestor`, running the query has been interrupted";
        }
        else
        {
            string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
            LOG_err << "Unable to get `isAncestor` from database: " << dbfile << err;
        }
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
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get number of nodes from database: " << dbfile << err;
        assert(!"Unable to get number of nodes from database.");
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
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get number of children of type from database: " << dbfile << err;
        assert(!"Unable to get number of children of type from database.");
    }

    sqlite3_reset(mStmtNumChild);

    return count;
}

bool SqliteAccountState::getNodesByMimetype(MimeType_t mimeType, std::vector<std::pair<NodeHandle, NodeSerialized>>& nodes, CancelToken cancelFlag)
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
                "INNER JOIN nodes n2 on n2.nodehandle = n1.parenthandle where n1.mimetype = ? AND n2.type !=";
        query.append(std::to_string(FILENODE));
        sqlResult = sqlite3_prepare_v2(db, query.c_str(), -1, &mStmtNodeByMimeType, nullptr);

    }
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int(mStmtNodeByMimeType, 1, static_cast<int>(mimeType))) == SQLITE_OK)
        {
            result = processSqlQueryNodes(mStmtNodeByMimeType, nodes);
        }
    }

    // unregister the handler (no-op if not registered)
    sqlite3_progress_handler(db, -1, nullptr, nullptr);

    if (sqlResult != SQLITE_OK)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get node by Mime type from database: " << dbfile << err;
        assert(!"Unable to get node by Mime type from database.");
    }

    sqlite3_reset(mStmtNodeByMimeType);

    return result;
}

} // namespace

#endif
