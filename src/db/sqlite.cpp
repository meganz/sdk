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

SqliteDbTable* SqliteDbAccess::open(PrnGen &rng, FileSystemAccess& fsAccess, const string& name, const int flags)
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
                      "type tinyint, size int64, share tinyint, decrypted tinyint, fav tinyint, "
                      "ctime int64, node BLOB NOT NULL)";
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
    auto upgraded = true;

    {
        auto legacyPath = databasePath(fsAccess, name, LEGACY_DB_VERSION);
        auto fileAccess = fsAccess.newfileaccess();

        if (fileAccess->fopen(legacyPath))
        {
            LOG_debug << "Found legacy database at: " << legacyPath;

            if (currentDbVersion == LEGACY_DB_VERSION)
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

    int result = sqlite3_open_v2(dbPath.toPath().c_str(), db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE // The database is opened for reading and writing, and is created if it does not already exist. This is the behavior that is always used for sqlite3_open() and sqlite3_open16().
        | SQLITE_OPEN_FULLMUTEX // The new database connection will use the "Serialized" threading mode. This means that multiple threads can be used withou restriction. (Required to avoid failure at SyncTest)
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
        result = sqlite3_prepare(db, "SELECT id, content FROM statecache", -1, &pStmt, NULL);
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

    sqlite3_stmt *stmt;
    int rc;

    rc = sqlite3_prepare(db, "SELECT content FROM statecache WHERE id = ?", -1, &stmt, NULL);
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

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE && rc != SQLITE_ROW)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(rc));
        LOG_err << "Unable to get record from database: " << dbfile << err;
        assert(!"Unable to get record from database.");
    }

    return rc == SQLITE_ROW;
}

// add/update record by index
bool SqliteDbTable::put(uint32_t index, char* data, unsigned len)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    sqlite3_stmt *stmt;
    bool result = false;

    int rc = sqlite3_prepare(db, "INSERT OR REPLACE INTO statecache (id, content) VALUES (?, ?)", -1, &stmt, NULL);
    if (rc == SQLITE_OK)
    {
        rc = sqlite3_bind_int(stmt, 1, index);
        if (rc == SQLITE_OK)
        {
            rc = sqlite3_bind_blob(stmt, 2, data, len, SQLITE_STATIC);
            if (rc == SQLITE_OK)
            {

                rc = sqlite3_step(stmt);
                if (rc == SQLITE_DONE)
                {
                    result = true;
                }
            }
        }
    }

    sqlite3_finalize(stmt);

    if (!result)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(rc));
        LOG_err << "Unable to put record into database: " << dbfile << err;
        assert(!"Unable to put record into database.");
    }

    return result;
}


// delete record by index
bool SqliteDbTable::del(uint32_t index)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    char buf[64];

    sprintf(buf, "DELETE FROM statecache WHERE id = %" PRIu32, index);

    int rc = sqlite3_exec(db, buf, 0, 0, nullptr);
    if (rc != SQLITE_OK)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(rc));
        LOG_err << "Unable to delete record from database: " << dbfile << err;
        assert(!"Unable to delete record from database.");

        return false;
    }

    return true;
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

template <class T>
bool SqliteAccountState::processSqlQueryNodes(sqlite3_stmt *stmt, T &nodes)
{
    assert(stmt);
    int sqlResult = SQLITE_ERROR;
    while ((sqlResult = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        NodeHandle nodeHandle;
        nodeHandle.set6byte(sqlite3_column_int64(stmt, 0));

        NodeSerialized node;
        node.mDecrypted = sqlite3_column_int(stmt, 1);

        const void* data = sqlite3_column_blob(stmt, 2);
        int size = sqlite3_column_bytes(stmt, 2);
        if (data && size)
        {
            node.mNode = std::string(static_cast<const char*>(data), size);
            nodes.insert(nodes.end(), std::make_pair(nodeHandle, std::move(node)));
        }
    }

    if (sqlResult == SQLITE_ERROR)
    {
        // In case of interrupt db query, it will finish with (expected) error
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_debug << "Unable to processSqlQueryNodes from database (maybe query has been interrupted): " << dbfile << err;
    }

    return true;
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

void SqliteAccountState::cancelQuery()
{
    if (!db)
    {
        return;
    }

    sqlite3_interrupt(db);
}

bool SqliteAccountState::put(Node *node)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    sqlite3_stmt *stmt;
    int sqlResult = sqlite3_prepare(db, "INSERT OR REPLACE INTO nodes (nodehandle, parenthandle, "
                                        "name, fingerprint, origFingerprint, type, size, share, decrypted, fav, ctime, node) "
                                        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        string nodeSerialized;
        node->serialize(&nodeSerialized);
        assert(nodeSerialized.size());

        sqlite3_bind_int64(stmt, 1, node->nodehandle);
        sqlite3_bind_int64(stmt, 2, node->parenthandle);

        std::string name = node->displayname();
        sqlite3_bind_text(stmt, 3, name.c_str(), static_cast<int>(name.length()), SQLITE_STATIC);

        string fp;
        node->FileFingerprint::serialize(&fp);
        sqlite3_bind_blob(stmt, 4, fp.data(), static_cast<int>(fp.size()), SQLITE_STATIC);

        std::string origFingerprint;
        attr_map::const_iterator attrIt = node->attrs.map.find(MAKENAMEID2('c', '0'));
        if (attrIt != node->attrs.map.end())
        {
           origFingerprint = attrIt->second;
        }
        sqlite3_bind_blob(stmt, 5, origFingerprint.data(), static_cast<int>(origFingerprint.size()), SQLITE_STATIC);

        sqlite3_bind_int(stmt, 6, node->type);
        sqlite3_bind_int64(stmt, 7, node->size);

        int shareType = node->getShareType();
        sqlite3_bind_int(stmt, 8, shareType);

        // node->attrstring has value => node is encrypted
        sqlite3_bind_int(stmt, 9, !node->attrstring);
        nameid favId = AttrMap::string2nameid("fav");
        bool fav = (node->attrs.map.find(favId) != node->attrs.map.end());
        sqlite3_bind_int(stmt, 10, fav);
        sqlite3_bind_int64(stmt, 11, node->ctime);
        sqlite3_bind_blob(stmt, 12, nodeSerialized.data(), static_cast<int>(nodeSerialized.size()), SQLITE_STATIC);

        sqlResult = sqlite3_step(stmt);
    }

    sqlite3_finalize(stmt);

    if (sqlResult == SQLITE_ERROR)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to put a node from database: " << dbfile << err;
        assert(!"Unable to put a node from database.");
        return false;
    }

    return true;
}

bool SqliteAccountState::getNode(NodeHandle nodehandle, NodeSerialized &nodeSerialized)
{
    if (!db)
    {
        return false;
    }

    nodeSerialized.mNode.clear();
    nodeSerialized.mDecrypted = true;

    int sqlResult = SQLITE_ERROR;
    sqlite3_stmt *stmt;
    if ((sqlResult = sqlite3_prepare(db, "SELECT decrypted, node FROM nodes  WHERE nodehandle = ?", -1, &stmt, NULL)) == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int64(stmt, 1, nodehandle.as8byte())) == SQLITE_OK)
        {
            if((sqlResult = sqlite3_step(stmt)) == SQLITE_ROW)
            {
                nodeSerialized.mDecrypted = sqlite3_column_int(stmt, 0);
                const void* data = sqlite3_column_blob(stmt, 1);
                int size = sqlite3_column_bytes(stmt, 1);
                if (data && size)
                {
                    nodeSerialized.mNode.assign(static_cast<const char*>(data), size);
                }
            }
        }
    }

    sqlite3_finalize(stmt);

    if (sqlResult == SQLITE_ERROR)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get a node from database: " << dbfile << err;
        assert(!"Unable to get a node from database.");
    }

    return nodeSerialized.mNode.size() ? true : false;
}


bool SqliteAccountState::getNodesByOrigFingerprint(const std::string &fingerprint, std::vector<std::pair<NodeHandle, NodeSerialized>> &nodes)
{
    if (!db)
    {
        return false;
    }

    sqlite3_stmt *stmt;
    bool result = false;
    int sqlResult = sqlite3_prepare(db, "SELECT nodehandle, decrypted, node FROM nodes WHERE origfingerprint = ?", -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_blob(stmt, 1, fingerprint.data(), (int)fingerprint.size(), SQLITE_STATIC)) == SQLITE_OK)
        {
            result = processSqlQueryNodes(stmt, nodes);
        }
    }

    sqlite3_finalize(stmt);

    if (sqlResult == SQLITE_ERROR)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get nodes by origfingerprint from database: " << dbfile << err;
        assert(!"Unable to get nodes by origfingerprint from database.");
    }

    return result;
}

bool SqliteAccountState::getRootNodes(std::vector<std::pair<NodeHandle, NodeSerialized>> &nodes)
{
    if (!db)
    {
        return false;
    }

    sqlite3_stmt *stmt;
    bool result = false;
    int sqlResult = sqlite3_prepare(db, "SELECT nodehandle, decrypted, node FROM nodes WHERE type >= ? AND type <= ?", -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        // nodeHandleUndef; // By default is set as undef
        if ((sqlResult = sqlite3_bind_int64(stmt, 1, nodetype_t::ROOTNODE)) == SQLITE_OK)
        {
            if ((sqlResult = sqlite3_bind_int64(stmt, 2, nodetype_t::RUBBISHNODE)) == SQLITE_OK)
            {
                result = processSqlQueryNodes(stmt, nodes);
            }
        }
    }

    if (sqlResult == SQLITE_ERROR)
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

    sqlite3_stmt *stmt;
    bool result = false;
    int sqlResult = sqlite3_prepare(db, "SELECT nodehandle, decrypted, node FROM nodes WHERE share & ? > 0", -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int(stmt, 1, static_cast<int>(shareType))) == SQLITE_OK)
        {
            result = processSqlQueryNodes(stmt, nodes);
        }
    }

    sqlite3_finalize(stmt);

    if (sqlResult == SQLITE_ERROR)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get root nodes from database: " << dbfile << err;
        assert(!"Unable to get root nodes from database.");
    }

    return result;
}

bool SqliteAccountState::getChildren(NodeHandle parentHandle, std::map<NodeHandle, NodeSerialized> &children)
{
    if (!db)
    {
        return false;
    }

    sqlite3_stmt *stmt;
    bool result = false;
    int sqlResult = sqlite3_prepare(db, "SELECT nodehandle, decrypted, node FROM nodes WHERE parenthandle = ?", -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int64(stmt, 1, parentHandle.as8byte())) == SQLITE_OK)
        {
            result = processSqlQueryNodes(stmt, children);
        }
    }

    sqlite3_finalize(stmt);

    if (sqlResult == SQLITE_ERROR)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get children from database: " << dbfile << err;
        assert(!"Unable to get children from database.");
    }

    return result;
}

bool SqliteAccountState::getChildrenHandles(mega::NodeHandle parentHandle, std::set<NodeHandle> & children)
{
    if (!db)
    {
        return false;
    }

    sqlite3_stmt *stmt;
    int sqlResult = sqlite3_prepare(db, "SELECT nodehandle FROM nodes WHERE parenthandle = ?", -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int64(stmt, 1, parentHandle.as8byte())) == SQLITE_OK)
        {
            while ((sqlResult = sqlite3_step(stmt)) == SQLITE_ROW)
            {
                int64_t h = sqlite3_column_int64(stmt, 0);
                children.insert(NodeHandle().set6byte(h));
            }
        }
    }

    sqlite3_finalize(stmt);

    if (sqlResult == SQLITE_ERROR)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get children handle from database: " << dbfile << err;
        assert(!"Unable to get children handle from database.");
        return false;
    }

    return true;
}

bool SqliteAccountState::getNodesByName(const std::string &name, std::map<mega::NodeHandle, NodeSerialized> &nodes)
{
    if (!db)
    {
        return false;
    }

    // select nodes whose 'name', in lowercase, matches the 'name' received by parameter, in lowercase,
    // (with or without any additional char at the beginning and/or end of the name). The '%' is the wildcard in SQL
    std::string sqlQuery = "SELECT nodehandle, decrypted, node FROM nodes WHERE LOWER(name) LIKE LOWER(";
    sqlQuery.append("'%")
            .append(name)
            .append("%')");
    // TODO: lower() works only with ASCII chars. If we want to add support to names in UTF-8, a new
    // test should be added, in example to search for 'ñam' when there is a node called 'Ñam'

    sqlite3_stmt *stmt;
    bool result = false;
    int sqlResult = sqlite3_prepare(db, sqlQuery.c_str(), -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        result = processSqlQueryNodes(stmt, nodes);
    }

    sqlite3_finalize(stmt);

    if (sqlResult == SQLITE_ERROR)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get nodes by name from database: " << dbfile << err;
        assert(!"Unable to get nodes by name from database.");
        return false;
    }

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

    std::string sqlQuery = "SELECT n1.nodehandle, n1.decrypted, n1.node, (" + isInRubbish + ") isinrubbish FROM nodes n1 "
        "LEFT JOIN nodes n2 on n2.nodehandle = n1.parenthandle"
        " where n1.type = " + filenode + " AND n1.ctime >= ? AND n2.type != " + filenode + " AND isinrubbish = 0"
        " ORDER BY n1.ctime DESC";

    if (maxcount)
    {
        sqlQuery += " LIMIT " + std::to_string(maxcount);
    }

    sqlite3_stmt* stmt;

    bool stepResult = false;
    int sqlResult = sqlite3_prepare(db, sqlQuery.c_str(), -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        sqlResult = sqlite3_bind_int64(stmt, 1, since);
        if (sqlResult == SQLITE_OK)
        {
            stepResult = processSqlQueryNodes(stmt, nodes);
        }
    }

    sqlite3_finalize(stmt);

    if (sqlResult == SQLITE_ERROR)
    {
        std::string err = std::string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get recent nodes from database: " << dbfile << err;
        return false;
    }

    return stepResult;
}

bool SqliteAccountState::getFavouritesHandles(NodeHandle node, uint32_t count, std::vector<mega::NodeHandle> &nodes)
{
    if (!db)
    {
        return false;
    }

    sqlite3_stmt *stmt;
    std::string sqlQuery = "WITH nodesCTE(nodehandle, parenthandle, fav) AS (SELECT nodehandle, parenthandle, fav "
                           "FROM nodes WHERE parenthandle = ? UNION ALL SELECT A.nodehandle, A.parenthandle, A.fav "
                           "FROM nodes AS A INNER JOIN nodesCTE AS E ON (A.parenthandle = E.nodehandle)) SELECT * "
                           "FROM nodesCTE where fav = 1;";

    int sqlResult = sqlite3_prepare(db, sqlQuery.c_str(), -1, &stmt, NULL);
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

    sqlite3_finalize(stmt);

    if (sqlResult == SQLITE_ERROR)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get favourites from database: " << dbfile << err;
        assert(!"Unable to get favourites from database.");
        return false;
    }

    return true;
}

m_off_t SqliteAccountState::getNodeSize(NodeHandle node)
{
    m_off_t size = 0;
    if (!db)
    {
        return size;
    }

    sqlite3_stmt *stmt;

    int sqlResult = sqlite3_prepare(db, "SELECT size FROM nodes WHERE nodehandle = ?", -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int64(stmt, 1, node.as8byte())) == SQLITE_OK)
        {
            if ((sqlResult = sqlite3_step(stmt)) == SQLITE_ROW)
            {
                size = sqlite3_column_int64(stmt, 0);
            }
        }
    }

    sqlite3_finalize(stmt);

    if (sqlResult == SQLITE_ERROR)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get node counter from database: " << dbfile << err;
        assert(!"Unable to get node counter from database.");
    }

    return size;
}

bool SqliteAccountState::isNodesOnDemandDb()
{
    if (!db)
    {
        return false;
    }

    int numRows = -1;
    sqlite3_stmt *stmt;
    int sqlResult = sqlite3_prepare(db, "SELECT count(*) FROM nodes", -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_step(stmt)) == SQLITE_ROW)
        {
           numRows = sqlite3_column_int(stmt, 0);
        }
    }

    sqlite3_finalize(stmt);

    if (sqlResult == SQLITE_ERROR)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to know if data base is nodes on demand: " << dbfile << err;
        assert(!"Unable to know if data base is nodes on demand.");
    }

    return numRows > 0 ? true : false;
}

NodeHandle SqliteAccountState::getFirstAncestor(NodeHandle node)
{
    NodeHandle ancestor;
    if (!db)
    {
        return ancestor;
    }

    std::string sqlQuery = "WITH nodesCTE(nodehandle, parenthandle) "
            "AS (SELECT nodehandle, parenthandle FROM nodes WHERE nodehandle = ? "
            "UNION ALL SELECT A.nodehandle, A.parenthandle FROM nodes AS A INNER JOIN nodesCTE "
            "AS E ON (A.nodehandle = E.parenthandle)) "
            "SELECT * FROM nodesCTE";

    sqlite3_stmt *stmt;
    int sqlResult = sqlite3_prepare(db, sqlQuery.c_str(), -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int64(stmt, 1, node.as8byte())) == SQLITE_OK)
        {
            while ((sqlResult = sqlite3_step(stmt)) == SQLITE_ROW)
            {
                ancestor.set6byte(sqlite3_column_int64(stmt, 0));
            }
        }
    }

    sqlite3_finalize(stmt);

    if (sqlResult == SQLITE_ERROR)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get first ancestor from database: " << dbfile << err;
        assert(!"Unable to get first ancestor from database.");
    }

    return ancestor;
}

bool SqliteAccountState::isAncestor(NodeHandle node, NodeHandle ancestor)
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

    sqlite3_stmt *stmt;
    int sqlResult = sqlite3_prepare(db, sqlQuery.c_str(), -1, &stmt, NULL);
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

    sqlite3_finalize(stmt);

    if (sqlResult == SQLITE_ERROR)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get `isAncestor` from database: " << dbfile << err;
        assert(!"Unable to get `isAncestor` from database.");
    }

    return result;
}

nodetype_t SqliteAccountState::getNodeType(NodeHandle node)
{
    nodetype_t nodeType = TYPE_UNKNOWN;
    if (!db)
    {
        return nodeType;
    }

    sqlite3_stmt *stmt;
    int sqlResult = sqlite3_prepare(db, "SELECT type FROM nodes WHERE nodehandle = ?", -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int64(stmt, 1, node.as8byte())) == SQLITE_OK)
        {
            if ((sqlResult = sqlite3_step(stmt)) == SQLITE_ROW)
            {
               nodeType = (nodetype_t)sqlite3_column_int(stmt, 0);
            }
        }
    }

    sqlite3_finalize(stmt);

    if (sqlResult == SQLITE_ERROR)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get `isFileNode` from database: " << dbfile << err;
        assert(!"Unable to get `isFileNode` from database.");
    }

    return nodeType;
}

bool SqliteAccountState::isNodeInDB(NodeHandle node)
{
    bool inDb = false;
    if (!db)
    {
        return inDb;
    }

    sqlite3_stmt *stmt;
    int sqlResult = sqlite3_prepare(db, "SELECT count(*) FROM nodes WHERE nodehandle = ?", -1, &stmt, NULL);
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

    sqlite3_finalize(stmt);

    if (sqlResult == SQLITE_ERROR)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get `isNodeInDB` from database: " << dbfile << err;
        assert(!"Unable to get `isNodeInDB` from database.");
    }

    return inDb;
}

uint64_t SqliteAccountState::getNumberOfNodes()
{
    uint64_t nodeNumber = 0;
    if (!db)
    {
        return nodeNumber;
    }

    sqlite3_stmt *stmt;
    int sqlResult = sqlite3_prepare(db, "SELECT count(*) FROM nodes WHERE type = ? OR type = ?", -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        if ((sqlResult = sqlite3_bind_int(stmt, 1, FILENODE)) == SQLITE_OK)
        {
            if ((sqlResult = sqlite3_bind_int(stmt, 2, FOLDERNODE)) == SQLITE_OK)
            {
                if ((sqlResult = sqlite3_step(stmt)) == SQLITE_ROW)
                {
                    nodeNumber = sqlite3_column_int64(stmt, 0);
                }
            }
        }
    }

    sqlite3_finalize(stmt);
    if (sqlResult == SQLITE_ERROR)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get number of nodes from database: " << dbfile << err;
        assert(!"Unable to get number of nodes from database.");
    }

    return nodeNumber;
}

bool SqliteAccountState::loadFingerprintsAndChildren(std::map<FileFingerprint, std::map<NodeHandle, Node *>, FileFingerprintCmp> &fingerprints, std::vector<std::pair<NodeHandle, NodeHandle>>& nodeAndParent)
{
    if (!db)
    {
        return false;
    }

    sqlite3_stmt *stmt;
    int sqlResult = sqlite3_prepare(db, "SELECT nodehandle, fingerprint, parenthandle, type FROM nodes", -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        while ((sqlResult = sqlite3_step(stmt) == SQLITE_ROW))
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

            nodeAndParent.push_back(std::make_pair(parentHandle, nodeHandle));
        }
    }

    sqlite3_finalize(stmt);

    if (sqlResult == SQLITE_ERROR)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(sqlResult));
        LOG_err << "Unable to get a map with fingerprints: " << dbfile << err;
        assert(!"Unable to get a map with fingerprints.");
        return false;
    }

    return true;

}

} // namespace

#endif
