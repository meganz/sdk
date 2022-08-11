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

            if (LEGACY_DB_VERSION == LAST_DB_VERSION_WITHOUT_NOD)
            {
                LOG_debug << "Rename database file to update version to NOD";
                if (!fsAccess.renamelocal(legacyPath, dbPath))
                {
                    fsAccess.unlinklocal(legacyPath);
                }
            }
            else if (currentDbVersion == LEGACY_DB_VERSION)
            {
                LOG_debug << "Using a legacy database.";
                dbPath = std::move(legacyPath);
                upgraded = false;
            }
            else if ((flags & DB_OPEN_FLAG_RECYCLE))
            {
                LOG_debug << "Trying to recycle a legacy database.";

                if (fsAccess.renamelocal(legacyPath, dbPath, false))
                {
                    auto suffix = LocalPath::fromRelativePath("-shm");
                    auto from = legacyPath + suffix;
                    auto to = dbPath + suffix;

                    fsAccess.renamelocal(from, to);

                    suffix = LocalPath::fromRelativePath("-wal");
                    from = legacyPath + suffix;
                    to = dbPath + suffix;

                    fsAccess.renamelocal(from, to);

                    LOG_debug << "Legacy database recycled.";
                }
                else
                {
                    LOG_debug << "Unable to recycle database, deleting...";
                    fsAccess.unlinklocal(legacyPath);
                }
            }
            else
            {
                LOG_debug << "Deleting outdated legacy database.";
                fsAccess.unlinklocal(legacyPath);
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
                      "type tinyint, size int64, share tinyint, fav tinyint, "
                      "ctime int64, counter BLOB NOT NULL, node BLOB NOT NULL)";
    int result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    if (result)
    {
        LOG_debug << "Data base error: " << sqlite3_errmsg(db);
        sqlite3_close(db);
        return nullptr;
    }

    // Create index for column that is not primary key (which already has an index by default)
    sql = "CREATE INDEX IF NOT EXISTS parenthandleindex on nodes (parenthandle)";
    result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    if (result)
    {
        LOG_debug << "Data base error while creating index (parenthandleindex): " << sqlite3_errmsg(db);
        sqlite3_close(db);
        return nullptr;
    }

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
    int result = sqlite3_open_v2(dbPath.toPath().c_str(), db,
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
                                           "name, fingerprint, origFingerprint, type, size, share, fav, ctime, counter, node) "
                                           "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", -1, &mStmtPutNode, NULL);
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
        std::string nodeCountersBlob = node->getCounter().serialize();
        sqlite3_bind_blob(mStmtPutNode, 11, nodeCountersBlob.data(), static_cast<int>(nodeCountersBlob.size()), SQLITE_STATIC);
        sqlite3_bind_blob(mStmtPutNode, 12, nodeSerialized.data(), static_cast<int>(nodeSerialized.size()), SQLITE_STATIC);

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

    sqlite3_stmt *stmt = nullptr;
    bool result = false;
    int sqlResult = sqlite3_prepare_v2(db, "SELECT nodehandle, counter, node FROM nodes WHERE origfingerprint = ?", -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_blob(stmt, 1, fingerprint.data(), (int)fingerprint.size(), SQLITE_STATIC)) == SQLITE_OK)
        {
            result = processSqlQueryNodes(stmt, nodes);
        }
    }

    if (sqlResult != SQLITE_OK)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get nodes by origfingerprint from database: " << dbfile << err;
        assert(!"Unable to get nodes by origfingerprint from database.");
    }

    sqlite3_finalize(stmt);

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

bool SqliteAccountState::getNodesByName(const std::string &name, std::vector<std::pair<NodeHandle, NodeSerialized>> &nodes, CancelToken cancelFlag)
{
    if (!db)
    {
        return false;
    }

    // select nodes whose 'name', in lowercase, matches the 'name' received by parameter, in lowercase,
    // (with or without any additional char at the beginning and/or end of the name). The '%' is the wildcard in SQL
    std::string sqlQuery = "SELECT nodehandle, counter, node FROM nodes WHERE LOWER(name) LIKE LOWER(";
    sqlQuery.append("'%")
            .append(name)
            .append("%')");
    // TODO: lower() works only with ASCII chars. If we want to add support to names in UTF-8, a new
    // test should be added, in example to search for 'ñam' when there is a node called 'Ñam'

    if (cancelFlag.exists())
    {
        sqlite3_progress_handler(db, NUM_VIRTUAL_MACHINE_INSTRUCTIONS, SqliteAccountState::progressHandler, static_cast<void*>(&cancelFlag));
    }

    sqlite3_stmt *stmt = nullptr;
    bool result = false;
    int sqlResult = sqlite3_prepare_v2(db, sqlQuery.c_str(), -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        result = processSqlQueryNodes(stmt, nodes);
    }

    // unregister the handler (no-op if not registered)
    sqlite3_progress_handler(db, -1, nullptr, nullptr);

    if (sqlResult != SQLITE_OK)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get nodes by name from database: " << dbfile << err;
        assert(!"Unable to get nodes by name from database.");
    }

    sqlite3_finalize(stmt);

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
        " ORDER BY n1.ctime DESC";

    if (maxcount)
    {
        sqlQuery += " LIMIT " + std::to_string(maxcount);
    }

    sqlite3_stmt *stmt = nullptr;

    bool stepResult = false;
    int sqlResult = sqlite3_prepare_v2(db, sqlQuery.c_str(), -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        sqlResult = sqlite3_bind_int64(stmt, 1, since);
        if (sqlResult == SQLITE_OK)
        {
            stepResult = processSqlQueryNodes(stmt, nodes);
        }
    }

    if (sqlResult != SQLITE_OK)
    {
        std::string err = std::string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get recent nodes from database: " << dbfile << err;
    }

    sqlite3_finalize(stmt);

    return stepResult;
}

bool SqliteAccountState::getFavouritesHandles(NodeHandle node, uint32_t count, std::vector<mega::NodeHandle> &nodes)
{
    if (!db)
    {
        return false;
    }

    sqlite3_stmt *stmt = nullptr;
    std::string sqlQuery = "WITH nodesCTE(nodehandle, parenthandle, fav) AS (SELECT nodehandle, parenthandle, fav "
                           "FROM nodes WHERE parenthandle = ? UNION ALL SELECT A.nodehandle, A.parenthandle, A.fav "
                           "FROM nodes AS A INNER JOIN nodesCTE AS E ON (A.parenthandle = E.nodehandle)) SELECT * "
                           "FROM nodesCTE where fav = 1;";

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

bool SqliteAccountState::getNodeByNameAtFirstLevel(NodeHandle parentHanlde, const std::string& name, nodetype_t nodeType, std::pair<NodeHandle, NodeSerialized> &node)
{
    bool success = false;
    if (!db)
    {
        return success;
    }

    std::string sqlQuery = "SELECT nodehandle, counter, node FROM nodes WHERE parenthandle = ? AND name = ";
    sqlQuery.append("'")
            .append(name)
            .append("'");
    if (nodeType == FILENODE || nodeType == FOLDERNODE)
    {
        sqlQuery.append(" AND type = ?");
    }
    else
    {
        assert(nodeType == TYPE_UNKNOWN);
    }

    sqlQuery.append(" limit 1");

    sqlite3_stmt *stmt = nullptr;
    int sqlResult = sqlite3_prepare_v2(db, sqlQuery.c_str(), -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int64(stmt, 1, parentHanlde.as8byte())) == SQLITE_OK)
        {
            // if nodeType is unknown, no need to bind the value, but to proceed to sqlite3_step()
            if (nodeType == TYPE_UNKNOWN
                || (sqlResult = sqlite3_bind_int64(stmt, 2, nodeType)) == SQLITE_OK)
            {
                if((sqlResult = sqlite3_step(stmt)) == SQLITE_ROW)
                {
                    node.first.set6byte(sqlite3_column_int64(stmt, 0));

                    const void* dataNodeCounter = sqlite3_column_blob(stmt, 1);
                    int sizeNodeCounter = sqlite3_column_bytes(stmt, 1);

                    const void* dataNodeSerialized = sqlite3_column_blob(stmt, 2);
                    int sizeNodeSerialized = sqlite3_column_bytes(stmt, 2);

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

    if (sqlResult != SQLITE_ROW && sqlResult != SQLITE_DONE)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get nodes by name and type from database: " << dbfile << err;
        assert(!"Unable to get node by name from database (Only search at first level).");
    }

    sqlite3_finalize(stmt);

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

    sqlite3_stmt *stmt = nullptr;
    int sqlResult = sqlite3_prepare_v2(db, sqlQuery.c_str(), -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int64(stmt, 1, node.as8byte())) == SQLITE_OK)
        {
            if ((sqlResult = sqlite3_bind_int64(stmt, 2, ancestor.as8byte())) == SQLITE_OK)
            {
                if ((sqlResult = sqlite3_step(stmt)) == SQLITE_ROW)
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

    sqlite3_finalize(stmt);

    return result;
}

bool SqliteAccountState::isNodeInDB(NodeHandle node)
{
    bool inDb = false;
    if (!db)
    {
        return inDb;
    }

    sqlite3_stmt *stmt = nullptr;
    int sqlResult = sqlite3_prepare_v2(db, "SELECT count(*) FROM nodes WHERE nodehandle = ?", -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int64(stmt, 1, node.as8byte())) == SQLITE_OK)
        {
            if ((sqlResult = sqlite3_step(stmt)) == SQLITE_ROW)
            {
               inDb = sqlite3_column_int(stmt, 0);
            }
        }
    }

    if (sqlResult != SQLITE_ROW)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get `isNodeInDB` from database: " << dbfile << err;
        assert(!"Unable to get `isNodeInDB` from database.");
    }

    sqlite3_finalize(stmt);

    return inDb;
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

    sqlite3_stmt *stmt = nullptr;
    int sqlResult = sqlite3_prepare_v2(db, "SELECT count(*) FROM nodes where parenthandle = ? AND type = ?", -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int64(stmt, 1, parentHandle.as8byte())) == SQLITE_OK)
        {
            if ((sqlResult = sqlite3_bind_int(stmt, 2, nodeType)) == SQLITE_OK)
            {
                if ((sqlResult = sqlite3_step(stmt)) == SQLITE_ROW)
                {
                    count = sqlite3_column_int64(stmt, 0);
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

    sqlite3_finalize(stmt);

    return count;
}

bool SqliteAccountState::loadFingerprintsAndChildren(std::map<FileFingerprint, nodePtr_map, FileFingerprintCmp> &fingerprints, std::map<NodeHandle, nodePtr_map> &children)
{
    if (!db)
    {
        return false;
    }

    sqlite3_stmt *stmt = nullptr;
    int sqlResult = sqlite3_prepare_v2(db, "SELECT nodehandle, fingerprint, parenthandle, type FROM nodes", -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        while ((sqlResult = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            NodeHandle nodeHandle;
            nodeHandle.set6byte(sqlite3_column_int64(stmt, 0));
            std::string fingerPrintString;
            const void* data = sqlite3_column_blob(stmt, 1);
            int size = sqlite3_column_bytes(stmt, 1);
            NodeHandle parentHandle;
            parentHandle.set6byte(sqlite3_column_int64(stmt, 2));
            nodetype_t nodeType = (nodetype_t)sqlite3_column_int(stmt, 3);

            if (data && size && nodeType == FILENODE)
            {
                fingerPrintString = std::string(static_cast<const char*>(data), size);
                std::unique_ptr<FileFingerprint> fingerprint;
                fingerprint.reset(FileFingerprint::unserialize(&fingerPrintString));
                fingerprints[*fingerprint].insert(std::pair<NodeHandle, Node*>(nodeHandle, nullptr));
            }

            children[parentHandle].insert(std::pair<NodeHandle, Node*>(nodeHandle, nullptr));
        }
    }

    if (sqlResult != SQLITE_DONE)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get a map with fingerprints: " << dbfile << err;
        assert(!"Unable to get a map with fingerprints.");
    }

    sqlite3_finalize(stmt);

    return sqlResult == SQLITE_DONE;
}

} // namespace

#endif
