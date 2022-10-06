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
    LocalPath dbfile;
    FileSystemAccess *fsaccess;
    sqlite3_stmt* mDelStmt = nullptr;
    sqlite3_stmt* mPutStmt = nullptr;

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

    SqliteDbTable(PrnGen &rng, sqlite3*, FileSystemAccess &fsAccess, const LocalPath &path, const bool checkAlwaysTransacted);
    virtual ~SqliteDbTable();

    bool inTransaction() const override;
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
    bool getNodesByOrigFingerprint(const std::string& fingerprint, std::vector<std::pair<NodeHandle, NodeSerialized>> &nodes) override;
    bool getRootNodes(std::vector<std::pair<NodeHandle, NodeSerialized>>& nodes) override;
    bool getNodesWithSharesOrLink(std::vector<std::pair<NodeHandle, NodeSerialized>>& nodes, ShareType_t shareType) override;
    bool getChildren(NodeHandle parentHandle, std::vector<std::pair<NodeHandle, NodeSerialized>>& children) override;
    uint64_t getNumberOfChildren(NodeHandle parentHandle) override;
    // If a cancelFlag is passed, it must be kept alive until this method returns.
    bool getNodesByName(const std::string& name, std::vector<std::pair<NodeHandle, NodeSerialized>> &nodes, CancelToken cancelFlag) override;
    bool getNodesByFingerprint(const std::string& fingerprint, std::vector<std::pair<NodeHandle, NodeSerialized>>& nodes) override;
    bool getNodeByFingerprint(const std::string& fingerprint, mega::NodeSerialized& node) override;
    bool getRecentNodes(unsigned maxcount, m_time_t since, std::vector<std::pair<NodeHandle, NodeSerialized>>& nodes) override;
    bool getFavouritesHandles(NodeHandle node, uint32_t count, std::vector<mega::NodeHandle>& nodes) override;
    bool childNodeByNameType(NodeHandle parentHanlde, const std::string& name, nodetype_t nodeType, std::pair<NodeHandle, NodeSerialized>& node) override;
    bool getNodeSizeAndType(NodeHandle node, m_off_t& size, nodetype_t& nodeType) override;
    bool isAncestor(mega::NodeHandle node, mega::NodeHandle ancestor, CancelToken cancelFlag) override;
    uint64_t getNumberOfNodes() override;
    uint64_t getNumberOfChildrenByType(NodeHandle parentHandle, nodetype_t nodeType) override;
    bool getNodesByMimetype(MimeType_t mimeType, std::vector<std::pair<mega::NodeHandle, mega::NodeSerialized> >& nodes, CancelToken cancelFlag) override;
    bool put(Node* node) override;
    bool remove(mega::NodeHandle nodehandle) override;
    bool removeNodes() override;

    void updateCounter(NodeHandle nodeHandle, const std::string& nodeCounterBlob) override;
    void createIndexes() override;

    void remove() override;
    SqliteAccountState(PrnGen &rng, sqlite3*, FileSystemAccess &fsAccess, const mega::LocalPath &path, const bool checkAlwaysTransacted);
    virtual ~SqliteAccountState();

    // Callback registered by some long-time running queries, so they can be canceled
    // If the progress callback returns non-zero, the operation is interrupted
    static int progressHandler(void *);

private:
    // Iterate over a SQL query row by row and fill the map
    // Allow at least the following containers:
    bool processSqlQueryNodes(sqlite3_stmt *stmt, std::vector<std::pair<mega::NodeHandle, mega::NodeSerialized>>& nodes);

    sqlite3_stmt* mStmtPutNode = nullptr;
    sqlite3_stmt* mStmtUpdateNode = nullptr;
    sqlite3_stmt* mStmtTypeAndSizeNode = nullptr;
    sqlite3_stmt* mStmtGetNode = nullptr;
    sqlite3_stmt* mStmtChildren = nullptr;
    sqlite3_stmt* mStmtNumChildren = nullptr;
    sqlite3_stmt* mStmtNodeByName = nullptr;
    sqlite3_stmt* mStmtNodeByMimeType = nullptr;
    sqlite3_stmt* mStmtNodesByFp = nullptr;
    sqlite3_stmt* mStmtNodeByFp = nullptr;
    sqlite3_stmt* mStmtNodeByOrigFp = nullptr;
    sqlite3_stmt* mStmtChildNode = nullptr;
    sqlite3_stmt* mStmtIsAncestor = nullptr;
    sqlite3_stmt* mStmtNumChild = nullptr;
    sqlite3_stmt* mStmtRecents = nullptr;

    // how many SQLite instructions will be executed between callbacks to the progress handler
    // (tests with a value of 1000 results on a callback every 1.2ms on a desktop PC)
    static const int NUM_VIRTUAL_MACHINE_INSTRUCTIONS = 1000;
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

    // Note: for proper adjustment of legacy versions, 'sctable' should be the first DB to be opened
    // In this way, when it's called with other DB (statusTable, tctable, ...), DbAccess::currentDbVersion has been
    // updated to new value
    bool checkDbFileAndAdjustLegacy(FileSystemAccess& fsAccess, const string& name, const int flags, LocalPath& dbPath) override;

    SqliteDbTable* open(PrnGen &rng, FileSystemAccess& fsAccess, const string& name, const int flags = 0x0) override;

    DbTable* openTableWithNodes(PrnGen &rng, FileSystemAccess& fsAccess, const string& name, const int flags = 0x0) override;

    bool probe(FileSystemAccess& fsAccess, const string& name) const override;

    const LocalPath& rootPath() const override;

private:
    bool openDBAndCreateStatecache(sqlite3 **db, FileSystemAccess& fsAccess, const string& name, mega::LocalPath &dbPath, const int flags);
    void renameDBTemporaryFiles(mega::FileSystemAccess& fsAccess, mega::LocalPath& legacyPath, mega::LocalPath& to);
    void removeDBFiles(mega::FileSystemAccess& fsAccess, mega::LocalPath& dbPath);
};

} // namespace

#endif
#endif
