/**
 * @file sqlite.h
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

#ifdef USE_SQLITE
#ifndef DBACCESS_CLASS
#define DBACCESS_CLASS SqliteDbAccess

#include <sqlite3.h>

namespace mega {

class MEGA_API SqliteDbTable : public DbTable
{
protected:
    sqlite3* db = nullptr;
    sqlite3_stmt* pStmt;
    string dbfile;
    FileSystemAccess *fsaccess;

public:
    void rewind() override;
    bool next(uint32_t*, string*) override;
    bool get(uint32_t, string*) override;
    bool put(uint32_t, char*, unsigned) override;
    bool del(uint32_t) override;
    void truncate() override;
    void begin() override;
    void commit() override;
    void abort() override;
    void remove() override;

    SqliteDbTable(PrnGen &rng, sqlite3*, FileSystemAccess &fsAccess, const string &path, const bool checkAlwaysTransacted);
    virtual ~SqliteDbTable();

    bool inTransaction() const override;

    LocalPath dbFile() const;
};

/**
 * This class implements DbTable iface (by deriving SqliteDbTable), and additionally
 * implements DbTableNodes iface too, so it allows to manage `nodes` table.
 */
class MEGA_API SqliteAccountState : public SqliteDbTable, public DBTableNodes
{
public:
    // Access to table `nodes`
    bool getNode(mega::NodeHandle nodehandle, NodeSerialized& nodeSerialized) override;
    bool getNodes(std::vector<NodeSerialized>& nodes) override;
    bool getNodesByFingerprint(const FileFingerprint& fingerprint, std::map<mega::NodeHandle, mega::NodeSerialized> &nodes) override;
    bool getNodesByOrigFingerprint(const std::string& fingerprint, std::map<mega::NodeHandle, mega::NodeSerialized> &nodes) override;
    bool getNodeByFingerprint(const FileFingerprint& fingerprint, NodeSerialized &node, NodeHandle& nodeHandle) override;
    bool getRootNodes(std::map<mega::NodeHandle, NodeSerialized>& nodes) override;
    bool getNodesWithSharesOrLink(std::map<mega::NodeHandle, mega::NodeSerialized>& nodes, ShareType_t shareType) override;
    bool getChildrenFromNode(NodeHandle parentHandle, std::map<NodeHandle, NodeSerialized>& children) override;
    bool getChildrenHandlesFromNode(mega::NodeHandle parentHandle, std::vector<mega::NodeHandle> &children) override;
    bool getNodesByName(const std::string& name, std::map<mega::NodeHandle, mega::NodeSerialized> &nodes) override;
    bool getFavouritesNodeHandles(NodeHandle node, uint32_t count, std::vector<mega::NodeHandle>& nodes) override;
    int getNumberOfChildrenFromNode(mega::NodeHandle parentHandle) override;
    NodeCounter getNodeCounter(mega::NodeHandle node, bool parentIsFile) override;
    bool isNodesOnDemandDb() override;
    bool isAncestor(mega::NodeHandle node, mega::NodeHandle ancestor) override;
    bool isFileNode(NodeHandle node) override;
    mega::NodeHandle getFirstAncestor(mega::NodeHandle node) override;
    bool isNodeInDB(mega::NodeHandle node) override;
    uint64_t getNumberOfNodes() override;
    bool put(Node* node) override;
    bool remove(mega::NodeHandle nodehandle) override;
    bool removeNodes() override;

    SqliteAccountState(PrnGen &rng, sqlite3*, FileSystemAccess &fsAccess, const string &path, const bool checkAlwaysTransacted);

private:
    // Iterate row by row from a query and fill the map
    bool processSqlQueryNodeMap(sqlite3_stmt *stmt, std::map<mega::NodeHandle, NodeSerialized> &nodes);
};

class MEGA_API SqliteDbAccess : public DbAccess
{
    LocalPath mRootPath;

public:
    explicit SqliteDbAccess(const LocalPath& rootPath);

    ~SqliteDbAccess();

    LocalPath databasePath(const FileSystemAccess& fsAccess,
                           const string& name,
                           const int version) const;

    SqliteDbTable* open(PrnGen &rng, FileSystemAccess& fsAccess, const string& name, const int flags = 0x0) override;

    DbTable* openTableWithNodes(PrnGen &rng, FileSystemAccess& fsAccess, const string& name, const int flags = 0x0) override;

    bool probe(FileSystemAccess& fsAccess, const string& name) const override;

    const LocalPath& rootPath() const override;

private:
    bool openDBAndCreateStatecache(sqlite3 **db, FileSystemAccess& fsAccess, const string& name, std::string& dbPathStr, const int flags);
};

} // namespace

#endif
#endif
