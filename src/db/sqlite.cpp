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
      LocalPath::fromPath(osstream.str(), fsAccess),
      false);

    return path;
}

SqliteDbTable* SqliteDbAccess::open(PrnGen &rng, FileSystemAccess& fsAccess, const string& name, const int flags)
{
    auto dbPath = databasePath(fsAccess, name, DB_VERSION);
    auto upgraded = true;

    {
        auto legacyPath = databasePath(fsAccess, name, LEGACY_DB_VERSION);
        auto fileAccess = fsAccess.newfileaccess();

        if (fileAccess->fopen(legacyPath))
        {
            LOG_debug << "Found legacy database at: " << legacyPath.toPath(fsAccess);

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
                    auto suffix = LocalPath::fromPath("-shm", fsAccess);
                    auto from = legacyPath + suffix;
                    auto to = dbPath + suffix;

                    fsAccess.renamelocal(from, to);

                    suffix = LocalPath::fromPath("-wal", fsAccess);
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
        LOG_debug << "Using an upgraded DB: " << dbPath.toPath(fsAccess);
        currentDbVersion = DB_VERSION;
    }

    const string dbPathStr = dbPath.toPath(fsAccess);
    sqlite3* db;
    int result = sqlite3_open(dbPathStr.c_str(), &db);

    if (result)
    {
        if (db)
        {
            sqlite3_close(db);
        }

        return nullptr;
    }

#if !(TARGET_OS_IPHONE)
    result = sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    if (result)
    {
        sqlite3_close(db);
        return nullptr;
    }
#endif /* ! TARGET_OS_IPHONE */

    string sql = "CREATE TABLE IF NOT EXISTS statecache (id INTEGER PRIMARY KEY ASC NOT NULL, content BLOB NOT NULL)";

    result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    if (result)
    {
        sqlite3_close(db);
        return nullptr;
    }

    sql = "CREATE TABLE IF NOT EXISTS nodes (nodehandle int64 PRIMARY KEY NOT NULL, parenthandle int64, name text, fingerprint BLOB, origFingerprint BLOB, type int, size int64, share int, decrypted int, node BLOB NOT NULL)";
    result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    if (result)
    {
        sqlite3_close(db);
        return nullptr;
    }

    sql = "CREATE TABLE IF NOT EXISTS vars(name text PRIMARY KEY NOT NULL, value BLOB)";
    result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    if (result)
    {
        sqlite3_close(db);
        return nullptr;
    }

    return new SqliteDbTable(rng,
                             db,
                             fsAccess,
                             dbPathStr,
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

SqliteDbTable::SqliteDbTable(PrnGen &rng, sqlite3* db, FileSystemAccess &fsAccess, const string &path, const bool checkAlwaysTransacted)
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

LocalPath SqliteDbTable::dbFile() const
{
    return LocalPath::fromPath(dbfile, *fsaccess);
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

    checkTransaction();

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

bool SqliteDbTable::getNode(NodeHandle nodehandle, NodeSerialized &nodeSerialized)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    nodeSerialized.mNode.clear();
    nodeSerialized.mDecrypted = true;

    sqlite3_stmt *stmt;
    if (sqlite3_prepare(db, "SELECT decrypted, node FROM nodes  WHERE nodehandle = ?", -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_bind_int64(stmt, 1, nodehandle.as8byte()) == SQLITE_OK)
        {
            if((sqlite3_step(stmt) == SQLITE_ROW))
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
    return nodeSerialized.mNode.size() ? true : false;
}

bool SqliteDbTable::getNodes(std::vector<NodeSerialized> &nodes)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    sqlite3_stmt *stmt;
    int result = SQLITE_ERROR;
    if (sqlite3_prepare(db, "SELECT decrypted, node FROM nodes", -1, &stmt, NULL) == SQLITE_OK)
    {
        while ((result = sqlite3_step(stmt) == SQLITE_ROW))
        {
            NodeSerialized node;
            node.mDecrypted = sqlite3_column_int(stmt, 0);

            const void* data = sqlite3_column_blob(stmt, 1);
            int size = sqlite3_column_bytes(stmt, 1);
            if (data && size)
            {
                node.mNode = std::string(static_cast<const char*>(data), size);
                nodes.push_back(node);
            }
        }
    }

    sqlite3_finalize(stmt);
    return result == SQLITE_DONE ? true : false;
}

bool SqliteDbTable::getNodesByFingerprint(const FileFingerprint &fingerprint, std::map<mega::NodeHandle, NodeSerialized> &nodes)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    sqlite3_stmt *stmt;
    int result = SQLITE_ERROR;
    if (sqlite3_prepare(db, "SELECT nodehandle, decrypted, node FROM nodes WHERE fingerprint = ?", -1, &stmt, NULL) == SQLITE_OK)
    {
        string fp;
        fingerprint.serializefingerprint(&fp);
        if (sqlite3_bind_blob(stmt, 1, fp.data(), fp.size(), SQLITE_STATIC) == SQLITE_OK)
        {
            while ((result = sqlite3_step(stmt) == SQLITE_ROW))
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
                    nodes[nodeHandle] = node;
                }
            }
        }
    }

    sqlite3_finalize(stmt);
    return result == SQLITE_DONE ? true : false;
}

bool SqliteDbTable::getNodesByOrigFingerprint(const std::string &fingerprint, std::map<mega::NodeHandle, NodeSerialized> &nodes)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    sqlite3_stmt *stmt;
    int result = SQLITE_ERROR;
    if (sqlite3_prepare(db, "SELECT nodehandle, decrypted, node FROM nodes WHERE origFingerprint = ?", -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_bind_blob(stmt, 1, fingerprint.data(), fingerprint.size(), SQLITE_STATIC) == SQLITE_OK)
        {
            while ((result = sqlite3_step(stmt) == SQLITE_ROW))
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
                    nodes[nodeHandle] = node;
                }
            }
        }
    }

    sqlite3_finalize(stmt);
    return result == SQLITE_DONE ? true : false;
}

bool SqliteDbTable::getNodeByFingerprint(const FileFingerprint &fingerprint, NodeSerialized &node)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    node.mNode.clear();
    node.mDecrypted = true;

    sqlite3_stmt *stmt;
    int result = SQLITE_ERROR;
    if (sqlite3_prepare(db, "SELECT decrypted, node FROM nodes WHERE fingerprint = ?", -1, &stmt, NULL) == SQLITE_OK)
    {
        string fp;
        fingerprint.serializefingerprint(&fp);
        if (sqlite3_bind_blob(stmt, 1, fp.data(), fp.size(), SQLITE_STATIC) == SQLITE_OK)
        {
            if ((result = sqlite3_step(stmt) == SQLITE_ROW))
            {
                NodeSerialized node;
                node.mDecrypted = sqlite3_column_int(stmt, 0);
                const void* data = sqlite3_column_blob(stmt, 1);
                int size = sqlite3_column_bytes(stmt, 1);
                if (data && size)
                {
                    node.mNode = std::string(static_cast<const char*>(data), size);
                    result = SQLITE_DONE;
                }
            }
        }
    }

    sqlite3_finalize(stmt);
    return result == SQLITE_DONE ? true : false;
}

bool SqliteDbTable::getNodesWithoutParent(std::vector<NodeSerialized> &nodes)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    sqlite3_stmt *stmt;
    int result = SQLITE_ERROR;
    if (sqlite3_prepare(db, "SELECT decrypted, node FROM nodes WHERE parenthandle = -1", -1, &stmt, NULL) == SQLITE_OK)
    {
        while ((result = sqlite3_step(stmt) == SQLITE_ROW))
        {
            NodeSerialized node;
            node.mDecrypted = sqlite3_column_int(stmt, 0);
            const void* data = sqlite3_column_blob(stmt, 1);
            int size = sqlite3_column_bytes(stmt, 1);
            if (data && size)
            {
                node.mNode = std::string(static_cast<const char*>(data), size);
                nodes.push_back(node);
            }
        }
    }

    sqlite3_finalize(stmt);
    return result == SQLITE_DONE ? true : false;
}

bool SqliteDbTable::getNodesWithSharesOrLink(std::vector<NodeSerialized> &nodes, ShareType_t shareType)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    sqlite3_stmt *stmt;
    int result = SQLITE_ERROR;
    if (sqlite3_prepare(db, "SELECT decrypted, node FROM nodes WHERE share & ? > 0", -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_bind_int(stmt, 1, static_cast<int>(shareType)) == SQLITE_OK)
        {
            while ((result = sqlite3_step(stmt) == SQLITE_ROW))
            {
                NodeSerialized node;
                node.mDecrypted = sqlite3_column_int(stmt, 0);
                const void* data = sqlite3_column_blob(stmt, 1);
                int size = sqlite3_column_bytes(stmt, 1);
                if (data && size)
                {
                    node.mNode = std::string(static_cast<const char*>(data), size);
                    nodes.push_back(node);
                }
            }
        }
    }

    sqlite3_finalize(stmt);
    return result == SQLITE_DONE ? true : false;
}

bool SqliteDbTable::getChildrenFromNode(NodeHandle parentHandle, std::map<NodeHandle, NodeSerialized> &children)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    sqlite3_stmt *stmt;
    int result = SQLITE_ERROR;
    if (sqlite3_prepare(db, "SELECT nodehandle, decrypted, node FROM nodes WHERE parenthandle = ?", -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_bind_int64(stmt, 1, parentHandle.as8byte()) == SQLITE_OK)
        {
            while ((result = sqlite3_step(stmt) == SQLITE_ROW))
            {
                NodeHandle childHandle;
                childHandle.set6byte(sqlite3_column_int64(stmt, 0));
                NodeSerialized child;
                child.mDecrypted = sqlite3_column_int(stmt, 1);
                const void* data = sqlite3_column_blob(stmt, 2);
                int size = sqlite3_column_bytes(stmt, 2);
                if (data && size)
                {
                    child.mNode = std::string(static_cast<const char*>(data), size);
                    children[childHandle] = child;
                }
            }
        }
    }

    sqlite3_finalize(stmt);
    return result == SQLITE_DONE ? true : false;
}

bool SqliteDbTable::getChildrenHandlesFromNode(mega::NodeHandle parentHandle, std::vector<NodeHandle> & children)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    sqlite3_stmt *stmt;
    int result = SQLITE_ERROR;
    if (sqlite3_prepare(db, "SELECT nodehandle FROM nodes WHERE parenthandle = ?", -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_bind_int64(stmt, 1, parentHandle.as8byte()) == SQLITE_OK)
        {
            while ((result = sqlite3_step(stmt) == SQLITE_ROW))
            {
                int64_t h = sqlite3_column_int64(stmt, 0);
                children.push_back(NodeHandle().set6byte(h));
            }
        }
    }

    sqlite3_finalize(stmt);
    return result == SQLITE_DONE ? true : false;
}

bool SqliteDbTable::getNodesByName(const std::string &name, std::map<mega::NodeHandle, NodeSerialized> &nodes)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    std::string sqlQuery = "SELECT nodehandle, decrypted, node FROM nodes WHERE LOWER(name) LIKE LOWER(";
    sqlQuery.append("'%")
            .append(name)
            .append("%')");

    sqlite3_stmt *stmt;
    int result = SQLITE_ERROR;
    if (sqlite3_prepare(db, sqlQuery.c_str(), -1, &stmt, NULL) == SQLITE_OK)
    {
        while ((result = sqlite3_step(stmt) == SQLITE_ROW))
        {
            NodeHandle nodeHandle;
            nodeHandle.set6byte(sqlite3_column_int64(stmt, 0));
            NodeSerialized node;
            node.mDecrypted = sqlite3_column_int(stmt, 1);;
            const void* data = sqlite3_column_blob(stmt, 2);
            int size = sqlite3_column_bytes(stmt, 2);
            if (data && size)
            {
                node.mNode = std::string(static_cast<const char*>(data), size);
                nodes[nodeHandle] = node;
            }
        }
    }

    sqlite3_finalize(stmt);
    return result == SQLITE_DONE ? true : false;
}

uint32_t SqliteDbTable::getNumberOfChildrenFromNode(NodeHandle parentHandle)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    sqlite3_stmt *stmt;
    uint32_t numChildren = 0;
    if (sqlite3_prepare(db, "SELECT count(*) FROM nodes WHERE parenthandle = ?", -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_bind_int64(stmt, 1, parentHandle.as8byte()) == SQLITE_OK)
        {
            int result;
            if ((result = sqlite3_step(stmt) == SQLITE_ROW))
            {
               numChildren = static_cast<uint32_t>(sqlite3_column_int(stmt, 0));
            }
        }
    }

    sqlite3_finalize(stmt);
    return numChildren;
}

NodeCounter SqliteDbTable::getNodeCounter(NodeHandle node, bool isParentFile)
{
    NodeCounter nodeCounter;
    if (!db)
    {
        return nodeCounter;
    }

    checkTransaction();

    sqlite3_stmt *stmt;
    int64_t size = 0;
    int type = TYPE_UNKNOWN;

    if (sqlite3_prepare(db, "SELECT size, type FROM nodes WHERE nodehandle = ?", -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_bind_int64(stmt, 1, node.as8byte()) == SQLITE_OK)
        {
            if ((sqlite3_step(stmt) == SQLITE_ROW))
            {
                size = sqlite3_column_int64(stmt, 0);
                type = sqlite3_column_int(stmt, 1);
            }
        }
    }
    sqlite3_finalize(stmt);

    if (type == FILENODE)
    {
        nodeCounter.files = 1;
        nodeCounter.storage = size;
        if (isParentFile)
        {
            nodeCounter.versions = 1;
            nodeCounter.versionStorage = size;
        }
    }
    else
    {
        nodeCounter.folders = 1;
        assert(!isParentFile);
    }

    return nodeCounter;
}

bool SqliteDbTable::isNodesOnDemandDb()
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    int numRows = -1;
    sqlite3_stmt *stmt;
    if (sqlite3_prepare(db, "SELECT count(*) FROM nodes", -1, &stmt, NULL) == SQLITE_OK)
    {
        int result;
        if ((result = sqlite3_step(stmt) == SQLITE_ROW))
        {
           numRows = sqlite3_column_int(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);

    return numRows > 0 ? true : false;
}

NodeHandle SqliteDbTable::getFirstAncestor(NodeHandle node)
{
    NodeHandle ancestor;
    if (!db)
    {
        return ancestor;
    }

    checkTransaction();

    std::string sqlQuery = "WITH nodesCTE(nodehandle, parenthandle) "
            "AS (SELECT nodehandle, parenthandle FROM nodes WHERE nodehandle = ? "
            "UNION ALL SELECT A.nodehandle, A.parenthandle FROM nodes AS A INNER JOIN nodesCTE "
            "AS E ON (A.nodehandle = E.parenthandle)) "
            "SELECT * FROM nodesCTE";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare(db, sqlQuery.c_str(), -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_bind_int64(stmt, 1, node.as8byte()) == SQLITE_OK)
        {
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                ancestor.set6byte(sqlite3_column_int64(stmt, 0));
            }
        }
    }
    sqlite3_finalize(stmt);

    return ancestor;
}

bool SqliteDbTable::isAncestor(NodeHandle node, NodeHandle ancestor)
{
    bool result = false;
    if (!db)
    {
        return result;
    }

    checkTransaction();

    std::string sqlQuery = "WITH nodesCTE(nodehandle, parenthandle) "
            "AS (SELECT nodehandle, parenthandle FROM nodes WHERE nodehandle = ? "
            "UNION ALL SELECT A.nodehandle, A.parenthandle FROM nodes AS A INNER JOIN nodesCTE "
            "AS E ON (A.nodehandle = E.parenthandle)) "
            "SELECT * FROM nodesCTE WHERE parenthandle = ?";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare(db, sqlQuery.c_str(), -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_bind_int64(stmt, 1, node.as8byte()) == SQLITE_OK)
        {
            if (sqlite3_bind_int64(stmt, 2, ancestor.as8byte()) == SQLITE_OK)
            {
                if (sqlite3_step(stmt) == SQLITE_ROW)
                {
                    result = true;
                }
            }
        }
    }
    sqlite3_finalize(stmt);

    return result;
}

bool SqliteDbTable::isFileNode(NodeHandle node)
{
    bool isFileNode = false;
    if (!db)
    {
        return isFileNode;
    }

    checkTransaction();

    sqlite3_stmt *stmt;
    if (sqlite3_prepare(db, "SELECT type FROM nodes WHERE nodehandle = ?", -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_bind_int64(stmt, 1, node.as8byte()) == SQLITE_OK)
        {
            if (sqlite3_step(stmt) == SQLITE_ROW)
            {
               isFileNode = sqlite3_column_int(stmt, 0) == FILENODE;
            }
        }
    }
    sqlite3_finalize(stmt);

    return isFileNode;
}

bool SqliteDbTable::isNodeInDB(NodeHandle node)
{
    bool inDb = false;
    if (!db)
    {
        return inDb;
    }

    checkTransaction();

    sqlite3_stmt *stmt;
    if (sqlite3_prepare(db, "SELECT count(*) FROM nodes WHERE nodehandle = ?", -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_bind_int64(stmt, 1, node.as8byte()) == SQLITE_OK)
        {
            if (sqlite3_step(stmt) == SQLITE_ROW)
            {
               inDb = sqlite3_column_int(stmt, 0);
            }
        }
    }
    sqlite3_finalize(stmt);

    return inDb;
}

uint64_t SqliteDbTable::getNumberOfNodes()
{
    uint64_t nodeNumber = 0;
    if (!db)
    {
        return nodeNumber;
    }

    checkTransaction();

    sqlite3_stmt *stmt;
    if (sqlite3_prepare(db, "SELECT count(*) FROM nodes", -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            nodeNumber = sqlite3_column_int64(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);

    return nodeNumber;
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

bool SqliteDbTable::put(Node *node)
{
    bool result = false;
    if (!db)
    {
        return result;
    }

    checkTransaction();

    sqlite3_stmt *stmt;
    int sqlResult = sqlite3_prepare(db, "INSERT OR REPLACE INTO nodes (nodehandle, parenthandle, name, fingerprint, origFingerprint, type, size, share, decrypted, node) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        string nodeSerialized;
        assert(node->serialize(&nodeSerialized));

        sqlite3_bind_int64(stmt, 1, node->nodehandle);
        sqlite3_bind_int64(stmt, 2, node->parenthandle);

        std::string name = node->displayname();
        sqlite3_bind_text(stmt, 3, name.c_str(), static_cast<int>(name.length()), SQLITE_STATIC);

        string fp;
        node->serializefingerprint(&fp);
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

        int shareType = getShareType(node);
        sqlite3_bind_int(stmt, 8, shareType);

        sqlite3_bind_int(stmt, 9, !node->attrstring);
        sqlite3_bind_blob(stmt, 10, nodeSerialized.data(), static_cast<int>(nodeSerialized.size()), SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_DONE)
        {
            result = true;
        }
    }

    sqlite3_finalize(stmt);
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

bool SqliteDbTable::del(handle nodehandle)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    char buf[64];

    sprintf(buf, "DELETE FROM nodes WHERE nodehandle = %" PRId64, nodehandle);

    return !sqlite3_exec(db, buf, 0, 0, NULL);
}

bool SqliteDbTable::removeNodes()
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    return !sqlite3_exec(db, "TRUNCATE TABLE nodes", 0, 0, NULL);
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

    auto localpath = LocalPath::fromPath(dbfile, *fsaccess);
    fsaccess->unlinklocal(localpath);
}
std::string SqliteDbTable::getVar(const std::string& name)
{
    if (!db)
    {
        return "";
    }

    std::string value;
    checkTransaction();

    sqlite3_stmt *stmt;
    if (sqlite3_prepare(db, "SELECT value FROM vars WHERE name = ?", -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_bind_text(stmt, 1, name.c_str(), name.length(), SQLITE_STATIC) == SQLITE_OK)
        {
            if((sqlite3_step(stmt) == SQLITE_ROW))
            {
                const void* data = sqlite3_column_blob(stmt, 0);
                int size = sqlite3_column_bytes(stmt, 0);
                if (data && size)
                {
                    value.assign(static_cast<const char*>(data), size);
                }
            }
        }
    }

    sqlite3_finalize(stmt);
    return value;
}

bool SqliteDbTable::setVar(const std::string& name, const std::string& value)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    sqlite3_stmt *stmt;
    bool result = false;

    if (sqlite3_prepare(db, "INSERT OR REPLACE INTO vars (name, value) VALUES (?, ?)", -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_bind_text(stmt, 1, name.c_str(), name.length(), SQLITE_STATIC) == SQLITE_OK)
        {
            if (sqlite3_bind_blob(stmt, 2, value.data(), value.size(), SQLITE_STATIC) == SQLITE_OK)
            {
                if (sqlite3_step(stmt) == SQLITE_DONE)
                {
                    result = true;
                }
            }
        }
    }

    sqlite3_finalize(stmt);
    return result;
}
} // namespace

#endif
