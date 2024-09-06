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

#include "mega/types.h"

#ifdef USE_SQLITE
#ifndef DBACCESS_CLASS
#define DBACCESS_CLASS SqliteDbAccess

#include "mega/db.h"

#include <sqlite3.h>

namespace mega {

class MEGA_API SqliteDbTable : public DbTable
{
protected:
    sqlite3* db = nullptr;
    LocalPath dbfile;
    FileSystemAccess *fsaccess;

    sqlite3_stmt* pStmt = nullptr;
    sqlite3_stmt* mDelStmt = nullptr;
    sqlite3_stmt* mPutStmt = nullptr;

    // handler for DB errors ('interrupt' is true if caller can be interrupted by CancelToken)
    void errorHandler(int sqliteError, const std::string& operation, bool interrupt);

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

    SqliteDbTable(PrnGen &rng, sqlite3*, FileSystemAccess &fsAccess, const LocalPath &path, const bool checkAlwaysTransacted, DBErrorCallback dBErrorCallBack);
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

    uint64_t getNumberOfChildren(NodeHandle parentHandle) override;
    // If a cancelFlag is passed, it must be kept alive until this method returns.
    bool getChildren(const mega::NodeSearchFilter& filter, int order, std::vector<std::pair<NodeHandle, NodeSerialized>>& children, CancelToken cancelFlag, const NodeSearchPage& page) override;
    bool searchNodes(const mega::NodeSearchFilter& filter, int order, std::vector<std::pair<NodeHandle, NodeSerialized>>& nodes, CancelToken cancelFlag, const NodeSearchPage& page) override;

    bool getAllNodeTags(const std::string& searchString,
                        std::set<std::string>& tags,
                        CancelToken cancelFlag) override;

    bool getNodesByFingerprint(const std::string& fingerprint, std::vector<std::pair<NodeHandle, NodeSerialized>>& nodes) override;
    bool getNodeByFingerprint(const std::string& fingerprint,
                              mega::NodeSerialized& node,
                              NodeHandle& handle) override;
    // getRecentNodes() is for the deprecated method MegaClient::getRecentActions without
    // excludeSensitives flag
    bool getRecentNodes(const NodeSearchPage& page,
                        m_time_t since,
                        std::vector<std::pair<NodeHandle, NodeSerialized>>& nodes) override;
    bool getFavouritesHandles(NodeHandle node, uint32_t count, std::vector<mega::NodeHandle>& nodes) override;
    bool childNodeByNameType(NodeHandle parentHanlde, const std::string& name, nodetype_t nodeType, std::pair<NodeHandle, NodeSerialized>& node) override;
    bool getNodeSizeTypeAndFlags(NodeHandle node, m_off_t& size, nodetype_t& nodeType, uint64_t &oldFlags) override;
    bool isAncestor(mega::NodeHandle node, mega::NodeHandle ancestor, CancelToken cancelFlag) override;
    uint64_t getNumberOfNodes() override;
    uint64_t getNumberOfChildrenByType(NodeHandle parentHandle, nodetype_t nodeType) override;

    bool put(Node* node) override;
    bool remove(mega::NodeHandle nodehandle) override;
    bool removeNodes() override;

    void updateCounter(NodeHandle nodeHandle, const std::string& nodeCounterBlob) override;
    void updateCounterAndFlags(NodeHandle nodeHandle, uint64_t flags, const std::string& nodeCounterBlob) override;
    void createIndexes() override;

    void remove() override;
    SqliteAccountState(PrnGen &rng, sqlite3*, FileSystemAccess &fsAccess, const mega::LocalPath &path, const bool checkAlwaysTransacted, DBErrorCallback dBErrorCallBack);
    void finalise();
    virtual ~SqliteAccountState();

    // Callback registered by some long-time running queries, so they can be canceled
    // If the progress callback returns non-zero, the operation is interrupted
    static int progressHandler(void *);
    static void userRegexp(sqlite3_context* context, int argc, sqlite3_value** argv);

    // Method called when query uses 'getmimetype'
    // Gets the mimetype corresponding to the file extension
    static void userGetMimetype(sqlite3_context* context, int argc, sqlite3_value** argv);

    // Method called when query uses 'getSizeFromNodeCounter'
    // Gets the node size from node counter (blob)
    static void getSizeFromNodeCounter(sqlite3_context* context, int argc, sqlite3_value** argv);

    // Check if string (pattern - argv[0]) is contained at data base column from type text (argv[1])
    static void userIsContained(sqlite3_context* context, int argc, sqlite3_value** argv);

    // Check if a tag (string - argv[0]) is contained in the stored list of tags
    //(string with the tags delimited by TAG_DELIMITER - argv[1]).
    static void userMatchTag(sqlite3_context* context, int argc, sqlite3_value** argv);

private:
    // Iterate over a SQL query row by row and fill the map
    // Allow at least the following containers:
    bool processSqlQueryNodes(sqlite3_stmt *stmt, std::vector<std::pair<mega::NodeHandle, mega::NodeSerialized>>& nodes);

    bool processSqlQueryAllNodeTags(sqlite3_stmt* stmt,
                                    std::set<std::string>& tags,
                                    std::function<bool(const std::string&)> isValidTagF);

    // if add a new sqlite3_stmt update finalise()
    sqlite3_stmt* mStmtPutNode = nullptr;
    sqlite3_stmt* mStmtUpdateNode = nullptr;
    sqlite3_stmt* mStmtUpdateNodeAndFlags = nullptr;
    sqlite3_stmt* mStmtTypeAndSizeNode = nullptr;
    sqlite3_stmt* mStmtGetNode = nullptr;

    /** @deprecated */
    sqlite3_stmt* mStmtChildrenFromType = nullptr;

    sqlite3_stmt* mStmtNumChildren = nullptr;
    std::map<size_t, sqlite3_stmt*> mStmtGetChildren;
    std::map<size_t, sqlite3_stmt*> mStmtSearchNodes;
    sqlite3_stmt* mStmtAllNodeTags = nullptr;

    sqlite3_stmt* mStmtNodesByFp = nullptr;
    sqlite3_stmt* mStmtNodeByFp = nullptr;
    sqlite3_stmt* mStmtNodeByOrigFp = nullptr;
    sqlite3_stmt* mStmtChildNode = nullptr;
    sqlite3_stmt* mStmtIsAncestor = nullptr;
    sqlite3_stmt* mStmtNumChild = nullptr;
    sqlite3_stmt* mStmtRecents = nullptr; // For getRecentNodes()
    sqlite3_stmt* mStmtFavourites = nullptr;

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
                           const int version) const override;

    // Note: for proper adjustment of legacy versions, 'sctable' should be the first DB to be opened
    // In this way, when it's called with other DB (statusTable, tctable, ...), DbAccess::currentDbVersion has been
    // updated to new value
    bool checkDbFileAndAdjustLegacy(FileSystemAccess& fsAccess, const string& name, const int flags, LocalPath& dbPath) override;

    SqliteDbTable* open(PrnGen &rng, FileSystemAccess& fsAccess, const string& name, const int flags, DBErrorCallback dBErrorCallBack) override;

    DbTable* openTableWithNodes(PrnGen &rng, FileSystemAccess& fsAccess, const string& name, const int flags, DBErrorCallback dBErrorCallBack) override;

    bool probe(FileSystemAccess& fsAccess, const string& name) const override;

    const LocalPath& rootPath() const override;

private:
    bool openDBAndCreateStatecache(sqlite3 **db, FileSystemAccess& fsAccess, const string& name, mega::LocalPath &dbPath, const int flags);
    bool renameDBFiles(mega::FileSystemAccess& fsAccess, mega::LocalPath& legacyPath, mega::LocalPath& dbPath);
    void removeDBFiles(mega::FileSystemAccess& fsAccess, mega::LocalPath& dbPath);

    // We should add new type for every new column that was added to DB
    // This new type have to inherit from `MigrateType`
    class MigrateType
    {
    public:
        virtual ~MigrateType() = default;
        virtual bool bindToDb(sqlite3_stmt* stmt, const std::map<int, int>& lookupId) const = 0;
        virtual bool hasValidValue() const = 0;
    };

    class MTimeType: public MigrateType
    {
    public:
        MTimeType(m_time_t value);
        bool bindToDb(sqlite3_stmt* stmt, const std::map<int, int>& lookupId) const override;
        static std::unique_ptr<MigrateType> fromNodeData(NodeData& nd);
        bool hasValidValue() const override;
        static constexpr auto COMPONENT = NodeData::COMPONENT_MTIME;

    private:
        m_time_t mValue;
    };

    class LabelType: public MigrateType
    {
    public:
        LabelType(int value);
        bool bindToDb(sqlite3_stmt* stmt, const std::map<int, int>& lookupId) const override;
        static std::unique_ptr<MigrateType> fromNodeData(NodeData& nd);
        bool hasValidValue() const override;
        static constexpr auto COMPONENT = NodeData::COMPONENT_LABEL;

    private:
        int mValue;
    };

    class DescriptionType: public MigrateType
    {
    public:
        DescriptionType(const std::string& value);
        bool bindToDb(sqlite3_stmt* stmt, const std::map<int, int>& lookupId) const override;
        static std::unique_ptr<MigrateType> fromNodeData(NodeData& nd);
        bool hasValidValue() const override;
        static constexpr auto COMPONENT = NodeData::COMPONENT_DESCRIPTION;

    private:
        std::string mValue;
    };

    class TagsType: public MigrateType
    {
    public:
        TagsType(const std::string& value);
        bool bindToDb(sqlite3_stmt* stmt, const std::map<int, int>& lookupId) const override;
        static std::unique_ptr<SqliteDbAccess::MigrateType> fromNodeData(NodeData& nd);
        bool hasValidValue() const override;
        static constexpr auto COMPONENT = NodeData::COMPONENT_TAGS;

    private:
        std::string mValue;
    };

    // functionality for adding columns to existing table, and copying data to them
    struct NewColumn
    {
        string name;
        string type;
        int migrationId;
        // Method to extract info from NodeData and add it to vector
        std::function<bool(NodeData&, std::vector<std::unique_ptr<MigrateType>>&)>
            migrateOperation;

        template<typename T>
        static bool extractDataFromNodeData(NodeData& nd,
                                            std::vector<std::unique_ptr<MigrateType>>& newValues)
        {
            std::unique_ptr<MigrateType> val = T::fromNodeData(nd);
            bool hasValidValue = val->hasValidValue();
            newValues.push_back(std::move(val));
            return hasValidValue;
        }
    };

    bool addAndPopulateColumns(sqlite3* db, vector<NewColumn>&& newCols);
    bool stripExistingColumns(sqlite3* db, vector<NewColumn>& cols);
    bool addColumn(sqlite3* db, const string& name, const string& type);
    bool migrateDataToColumns(sqlite3* db, vector<NewColumn>&& cols);
};

class OrderByClause
{
public:
    static std::string get(int order);
    static size_t getId(int order);

    enum {
        DEFAULT_ASC = 1, DEFAULT_DESC,
        SIZE_ASC, SIZE_DESC,
        CTIME_ASC, CTIME_DESC,
        MTIME_ASC, MTIME_DESC,
        LABEL_ASC = 17, LABEL_DESC,
        FAV_ASC, FAV_DESC
    };
};

} // namespace

#endif
#endif

