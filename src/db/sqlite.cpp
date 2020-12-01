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

static LocalPath databasePath(const FileSystemAccess& fsAccess,
                              const LocalPath& rootPath,
                              const string& name,
                              const int version)
{
    ostringstream osstream;

    osstream << "megaclient_statecache"
             << version
             << "_"
             << name
             << ".db";

    LocalPath path = rootPath;

    path.appendWithSeparator(
      LocalPath::fromPath(osstream.str(), fsAccess),
      false);

    return path;
}

SqliteDbAccess::SqliteDbAccess(const LocalPath& rootPath)
  : mRootPath(rootPath)
{
}

SqliteDbAccess::~SqliteDbAccess()
{
}

DbTable* SqliteDbAccess::open(PrnGen &rng, FileSystemAccess& fsAccess, const string& name, const int flags)
{
    auto dbPath = databasePath(fsAccess, mRootPath, name, DB_VERSION);

    {
        auto legacyPath = databasePath(fsAccess, mRootPath, name, LEGACY_DB_VERSION);
        auto fileAccess = fsAccess.newfileaccess();

        if (fileAccess->fopen(legacyPath))
        {
            LOG_debug << "Found legacy database at: " << legacyPath.toPath(fsAccess);

            if (currentDbVersion == LEGACY_DB_VERSION)
            {
                LOG_debug << "Using a legacy database.";
                dbPath = std::move(legacyPath);
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

                    suffix = LocalPath::fromPath("-shm", fsAccess);
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

    LocalPath dbPath = databasePath(fsAccess, mRootPath, name, DB_VERSION);

    if (fileAccess->isfile(dbPath))
    {
        return true;
    }

    dbPath = databasePath(fsAccess, mRootPath, name, LEGACY_DB_VERSION);

    return fileAccess->isfile(dbPath);
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
        LOG_err << "Unable to rewind database: " << dbfile;
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
            LOG_err << "Unable to get next record from database: " << dbfile;
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
        LOG_err << "Unable to get record from database: " << dbfile;
        assert(!"Unable to get record from database.");
    }

    return rc == SQLITE_ROW;
}

bool SqliteDbTable::getNode(handle nodehandle, NodeSerialized &nodeSerialized)
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
        if (sqlite3_bind_int64(stmt, 1, nodehandle) == SQLITE_OK)
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

bool SqliteDbTable::getNodesByFingerprint(const FileFingerprint &fingerprint, std::map<mega::handle, NodeSerialized> &nodes)
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
                handle nodeHandle = sqlite3_column_int64(stmt, 0);
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

bool SqliteDbTable::getNodesByOrigFingerprint(const std::string &fingerprint, std::map<mega::handle, NodeSerialized> &nodes)
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
                handle nodeHandle = sqlite3_column_int64(stmt, 0);
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

bool SqliteDbTable::getChildrenFromNode(handle parentHandle, std::map<handle, NodeSerialized> &children)
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
        if (sqlite3_bind_int64(stmt, 1, parentHandle) == SQLITE_OK)
        {
            while ((result = sqlite3_step(stmt) == SQLITE_ROW))
            {
                handle childHandle = sqlite3_column_int64(stmt, 0);
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

bool SqliteDbTable::getChildrenHandlesFromNode(mega::handle parentHandle, std::vector<handle> & children)
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
        if (sqlite3_bind_int64(stmt, 1, parentHandle) == SQLITE_OK)
        {
            while ((result = sqlite3_step(stmt) == SQLITE_ROW))
            {
                int64_t h = sqlite3_column_int64(stmt, 0);
                children.push_back(h);
            }
        }
    }

    sqlite3_finalize(stmt);
    return result == SQLITE_DONE ? true : false;
}

bool SqliteDbTable::getNodesByName(const std::string &name, std::map<mega::handle, NodeSerialized> &nodes)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();
    std::string regExp;
    regExp.append("'%")
            .append(name)
            .append("%'");

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
            handle nodeHandle = sqlite3_column_int64(stmt, 0);
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

uint32_t SqliteDbTable::getNumberOfChildrenFromNode(handle parentHandle)
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
        if (sqlite3_bind_int64(stmt, 1, parentHandle) == SQLITE_OK)
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

NodeCounter SqliteDbTable::getNodeCounter(handle node, bool isParentFile)
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
    handle parentHandle = UNDEF;

    if (sqlite3_prepare(db, "SELECT size, type, parentHandle FROM nodes WHERE nodehandle = ?", -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_bind_int64(stmt, 1, node) == SQLITE_OK)
        {
            if ((sqlite3_step(stmt) == SQLITE_ROW))
            {
                size = sqlite3_column_int64(stmt, 0);
                type = sqlite3_column_int(stmt, 1);
                parentHandle = sqlite3_column_int64(stmt, 2);
            }
        }
    }

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

handle SqliteDbTable::getFirstAncestor(handle node)
{
    if (!db)
    {
        return UNDEF;
    }

    checkTransaction();
    handle ancestor = UNDEF;
    sqlite3_stmt *stmt;
    std::string sqlQuery = "WITH nodesCTE(nodehandle, parenthandle) AS (SELECT nodehandle, parenthandle FROM nodes WHERE nodehandle = ? UNION ALL SELECT A.nodehandle, A.parenthandle FROM nodes AS A INNER JOIN nodesCTE AS E ON (A.nodehandle = E.parenthandle)) SELECT * FROM nodesCTE";
    if (sqlite3_prepare(db, sqlQuery.c_str(), -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_bind_int64(stmt, 1, node) == SQLITE_OK)
        {
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                ancestor = sqlite3_column_int64(stmt, 0);
            }
        }
    }

    sqlite3_finalize(stmt);
    return ancestor;
}

bool SqliteDbTable::isAncestor(handle node, handle ancestor)
{
    if (!db)
    {
        return UNDEF;
    }

    checkTransaction();
    bool result = false;
    sqlite3_stmt *stmt;
    std::string sqlQuery = "WITH nodesCTE(nodehandle, parenthandle) AS (SELECT nodehandle, parenthandle FROM nodes WHERE nodehandle = ? UNION ALL SELECT A.nodehandle, A.parenthandle FROM nodes AS A INNER JOIN nodesCTE AS E ON (A.nodehandle = E.parenthandle)) SELECT * FROM nodesCTE WHERE parenthandle = ?";
    if (sqlite3_prepare(db, sqlQuery.c_str(), -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_bind_int64(stmt, 1, node) == SQLITE_OK)
        {
            if (sqlite3_bind_int64(stmt, 2, ancestor) == SQLITE_OK)
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

bool SqliteDbTable::isFileNode(handle node)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();
    bool isFileNode = false;
    sqlite3_stmt *stmt;

    if (sqlite3_prepare(db, "SELECT type FROM nodes WHERE nodehandle = ?", -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_bind_int64(stmt, 1, node) == SQLITE_OK)
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

bool SqliteDbTable::isNodeInDB(handle node)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();
    bool inDb = false;
    sqlite3_stmt *stmt;

    if (sqlite3_prepare(db, "SELECT count(*) FROM nodes WHERE nodehandle = ?", -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_bind_int64(stmt, 1, node) == SQLITE_OK)
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
    if (!db)
    {
        return false;
    }

    checkTransaction();
    sqlite3_stmt *stmt;
    uint64_t nodeNumber = 0;
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

    if (sqlite3_prepare(db, "INSERT OR REPLACE INTO statecache (id, content) VALUES (?, ?)", -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_bind_int(stmt, 1, index) == SQLITE_OK)
        {
            if (sqlite3_bind_blob(stmt, 2, data, len, SQLITE_STATIC) == SQLITE_OK)
            {
                if (sqlite3_step(stmt) == SQLITE_DONE)
                {
                    result = true;
                }
            }
        }
    }

    sqlite3_finalize(stmt);

    if (!result)
    {
        LOG_err << "Unable to put record into database: " << dbfile;
        assert(!"Unable to put record into database.");
    }

    return result;
}

bool SqliteDbTable::put(Node *node)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    sqlite3_stmt *stmt;
    bool result = false;

    int sqlResult = sqlite3_prepare(db, "INSERT OR REPLACE INTO nodes (nodehandle, parenthandle, name, fingerprint, origFingerprint, type, size, share, decrypted, node) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", -1, &stmt, NULL);
    if (sqlResult == SQLITE_OK)
    {
        string nodeSerialized;
        assert(node->serialize(&nodeSerialized));
        sqlite3_bind_int64(stmt, 1, node->nodehandle);
        sqlite3_bind_int64(stmt, 2, node->parenthandle);
        std::string name = node->displayname();
        sqlite3_bind_text(stmt, 3, name.c_str(), name.length(), SQLITE_STATIC);
        string fp;
        node->serializefingerprint(&fp);
        sqlite3_bind_blob(stmt, 4, fp.data(), fp.size(), SQLITE_STATIC);

        attr_map::const_iterator a = node->attrs.map.find(MAKENAMEID2('c', '0'));
        std::string origFingerprint;
        if (a != node->attrs.map.end())
        {
           origFingerprint = a->second;
        }

        sqlite3_bind_blob(stmt, 5, origFingerprint.data(), origFingerprint.size(), SQLITE_STATIC);


        sqlite3_bind_int(stmt, 6, node->type);
        sqlite3_bind_int64(stmt, 7, node->size);

        int shareType = getShareType(node);
        sqlite3_bind_int(stmt, 8, shareType);

        sqlite3_bind_int(stmt, 9, !node->attrstring);
        sqlite3_bind_blob(stmt, 10, nodeSerialized.data(), nodeSerialized.size(), SQLITE_STATIC);

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

    if (sqlite3_exec(db, buf, 0, 0, nullptr) != SQLITE_OK)
    {
        LOG_err << "Unable to delete record from database: " << dbfile;
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

    if (sqlite3_exec(db, "DELETE FROM statecache", 0, 0, NULL) != API_OK)
    {
        LOG_err << "Unable to truncate database: " << dbfile;
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
    if (sqlite3_exec(db, "BEGIN", 0, 0, NULL) != SQLITE_OK)
    {
        LOG_err << "Unable to begin transaction on database: " << dbfile;
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

    if (sqlite3_exec(db, "COMMIT", 0, 0, NULL) != SQLITE_OK)
    {
        LOG_err << "Unable to commit transaction on database: " << dbfile;
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

    if (sqlite3_exec(db, "ROLLBACK", 0, 0, NULL) != SQLITE_OK)
    {
        LOG_err << "Unable to rollback transaction on database: " << dbfile;
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
