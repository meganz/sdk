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
SqliteDbAccess::SqliteDbAccess(string* path)
{
    if (path)
    {
        dbpath = *path;
    }
}

SqliteDbAccess::~SqliteDbAccess()
{
}

DbTable* SqliteDbAccess::open(PrnGen &rng, FileSystemAccess* fsaccess, string* name, bool recycleLegacyDB, bool checkAlwaysTransacted)
{
    //Each table will use its own database object and its own file
    sqlite3* db;
    string dbfile;
    ostringstream legacyoss;
    legacyoss << dbpath;
    legacyoss << "megaclient_statecache";
    legacyoss << LEGACY_DB_VERSION;
    legacyoss << "_" << *name << ".db";
    string legacydbpath = legacyoss.str();

    ostringstream newoss;
    newoss << dbpath;
    newoss << "megaclient_statecache";
    newoss << DB_VERSION;
    newoss << "_" << *name << ".db";
    string currentdbpath = newoss.str();

    
    auto fa = fsaccess->newfileaccess();
    auto locallegacydbpath = LocalPath::fromPath(legacydbpath, *fsaccess);
    bool legacydbavailable = fa->fopen(locallegacydbpath);
    fa.reset();

    if (legacydbavailable)
    {
        if (currentDbVersion == LEGACY_DB_VERSION)
        {
            LOG_debug << "Using a legacy DB";
            dbfile = legacydbpath;
        }
        else
        {
            if (!recycleLegacyDB)
            {
                LOG_debug << "Legacy DB is outdated. Deleting.";
                fsaccess->unlinklocal(locallegacydbpath);
            }
            else
            {
                LOG_debug << "Trying to recycle a legacy DB";
                auto localcurrentdbpath = LocalPath::fromPath(currentdbpath, *fsaccess);
                if (fsaccess->renamelocal(locallegacydbpath, localcurrentdbpath, false))
                {
                    auto localsuffix = LocalPath::fromPath("-shm", *fsaccess);

                    auto oldfile = locallegacydbpath + localsuffix;
                    auto newfile = localcurrentdbpath + localsuffix;
                    fsaccess->renamelocal(oldfile, newfile, true);

                    localsuffix = LocalPath::fromPath("-wal", *fsaccess);
                    oldfile = locallegacydbpath + localsuffix;
                    newfile = localcurrentdbpath + localsuffix;
                    fsaccess->renamelocal(oldfile, newfile, true);
                    LOG_debug << "Legacy DB recycled";
                }
                else
                {
                    LOG_debug << "Unable to recycle legacy DB. Deleting.";
                    fsaccess->unlinklocal(locallegacydbpath);
                }
            }
        }
    }

    if (!dbfile.size())
    {
        LOG_debug << "Using an upgraded DB";
        dbfile = currentdbpath;
        currentDbVersion = DB_VERSION;
    }

    int rc;

    rc = sqlite3_open(dbfile.c_str(), &db);

    if (rc)
    {
        return NULL;
    }

#if !(TARGET_OS_IPHONE)
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
#endif

    string sql = "CREATE TABLE IF NOT EXISTS statecache (id INTEGER PRIMARY KEY ASC NOT NULL, content BLOB NOT NULL)";

    rc = sqlite3_exec(db, sql.c_str(), NULL, NULL, NULL);
    if (rc)
    {
        return NULL;
    }

    sql = "CREATE TABLE IF NOT EXISTS nodes (nodehandle int64 PRIMARY KEY NOT NULL, parenthandle int64, name text, fingerprint BLOB, origFingerprint BLOB, type int, size int64, share int, decrypted int, node BLOB NOT NULL)";
    rc = sqlite3_exec(db, sql.c_str(), NULL, NULL, NULL);
    if (rc)
    {
        return NULL;
    }

    sql = "CREATE TABLE IF NOT EXISTS vars(name text PRIMARY KEY NOT NULL, value BLOB)";
    rc = sqlite3_exec(db, sql.c_str(), NULL, NULL, NULL);
    if (rc)
    {
        return NULL;
    }

    return new SqliteDbTable(rng, db, fsaccess, &dbfile, checkAlwaysTransacted);
}

SqliteDbTable::SqliteDbTable(PrnGen &rng, sqlite3* cdb, FileSystemAccess *fs, string *filepath, bool checkAlwaysTransacted)
    : DbTable(rng, checkAlwaysTransacted)
{
    db = cdb;
    pStmt = NULL;
    fsaccess = fs;
    dbfile = *filepath;
}

SqliteDbTable::~SqliteDbTable()
{
    resetCommitter();

    if (!db)
    {
        return;
    }

    if (pStmt)
    {
        sqlite3_finalize(pStmt);
    }
    abort();
    sqlite3_close(db);
    LOG_debug << "Database closed " << dbfile;
}

// set cursor to first record
void SqliteDbTable::rewind()
{
    if (!db)
    {
        return;
    }

    if (pStmt)
    {
        sqlite3_reset(pStmt);
    }
    else
    {
        sqlite3_prepare(db, "SELECT id, content FROM statecache", -1, &pStmt, NULL);
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
    bool result = false;

    if (sqlite3_prepare(db, "SELECT content FROM statecache WHERE id = ?", -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_bind_int(stmt, 1, index) == SQLITE_OK)
        {
            if (sqlite3_step(stmt) == SQLITE_ROW)
            {
                data->assign((char*)sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0));

                result = true;
            }
        }
    }

    sqlite3_finalize(stmt);
    return result;
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

bool SqliteDbTable::getNodesWithShares(std::vector<NodeSerialized> &nodes)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    sqlite3_stmt *stmt;
    int result = SQLITE_ERROR;
    if (sqlite3_prepare(db, "SELECT decrypted, node FROM nodes WHERE share = 1", -1, &stmt, NULL) == SQLITE_OK)
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

bool SqliteDbTable::getChildrenFromNode(handle node, std::map<handle, NodeSerialized> &nodes)
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
        if (sqlite3_bind_int64(stmt, 1, node) == SQLITE_OK)
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
    }

    sqlite3_finalize(stmt);
    return result == SQLITE_DONE ? true : false;
}

bool SqliteDbTable::getChildrenHandlesFromNode(mega::handle node, std::vector<handle> & nodes)
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
        if (sqlite3_bind_int64(stmt, 1, node) == SQLITE_OK)
        {
            while ((result = sqlite3_step(stmt) == SQLITE_ROW))
            {
                int64_t handle = sqlite3_column_int64(stmt, 0);
                nodes.push_back(handle);
            }
        }
    }

    sqlite3_finalize(stmt);
    return result == SQLITE_DONE ? true : false;
}

uint32_t SqliteDbTable::getNumberOfChildrenFromNode(handle node)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    sqlite3_stmt *stmt;
    uint32_t numChilds = 0;
    if (sqlite3_prepare(db, "SELECT count(*) FROM nodes WHERE parenthandle = ?", -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_bind_int64(stmt, 1, node) == SQLITE_OK)
        {
            int result;
            if ((result = sqlite3_step(stmt) == SQLITE_ROW))
            {
               numChilds = static_cast<uint32_t>(sqlite3_column_int(stmt, 0));
            }
        }
    }

    sqlite3_finalize(stmt);
    return numChilds;
}

NodeCounter SqliteDbTable::getNodeCounter(handle node)
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
    }
    else if (type == FOLDERNODE)
    {
        nodeCounter.folders = 1;
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
    std::string sqlQuery = "WITH nodesCTE(nodehandle, parenthandle) AS (SELECT nodehandle, parenthandle FROM nodes WHERE nodehandle = ? UNION ALL SELECT A.nodehandle, A.parenthandle FROM nodes AS A INNER JOIN nodesCTE AS E ON (A.nodehandle = E.parenthandle)) SELECT * FROM nodesCTE WHERE parenthandle = -1";
    if (sqlite3_prepare(db, sqlQuery.c_str(), -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_bind_int64(stmt, 1, node) == SQLITE_OK)
        {
            if (sqlite3_step(stmt) == SQLITE_ROW)
            {
                ancestor = sqlite3_column_int64(stmt, 0);
            }
        }
    }


    sqlite3_finalize(stmt);
    return ancestor;
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
        int inshare = 0;
        if (node->inshare)
        {
            inshare = 1;
        }

        sqlite3_bind_int(stmt, 8, inshare);
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

    return !sqlite3_exec(db, buf, 0, 0, NULL);
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

    sqlite3_exec(db, "DELETE FROM statecache", 0, 0, NULL);
}

// begin transaction
void SqliteDbTable::begin()
{
    if (!db)
    {
        return;
    }

    LOG_debug << "DB transaction BEGIN " << dbfile;
    sqlite3_exec(db, "BEGIN", 0, 0, NULL);
}

// commit transaction
void SqliteDbTable::commit()
{
    if (!db)
    {
        return;
    }

    LOG_debug << "DB transaction COMMIT " << dbfile;
    sqlite3_exec(db, "COMMIT", 0, 0, NULL);
}

// abort transaction
void SqliteDbTable::abort()
{
    if (!db)
    {
        return;
    }

    LOG_debug << "DB transaction ROLLBACK " << dbfile;
    sqlite3_exec(db, "ROLLBACK", 0, 0, NULL);
}

void SqliteDbTable::remove()
{
    if (!db)
    {
        return;
    }

    if (pStmt)
    {
        sqlite3_finalize(pStmt);
    }
    abort();
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
