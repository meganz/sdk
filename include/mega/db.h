/**
 * @file mega/db.h
 * @brief Database access interface
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

#ifndef MEGA_DB_H
#define MEGA_DB_H 1

#include "filesystem.h"

namespace mega {
// generic host transactional database access interface
class DBTableTransactionCommitter;

// Class to load serialized node from data base
class NodeSerialized
{
public:
    std::string mNode;
    std::string mNodeCounter;
};

class MEGA_API DbTable
{
    PrnGen &rng;

protected:
    bool mCheckAlwaysTransacted = false;
    DBTableTransactionCommitter* mTransactionCommitter = nullptr;
    friend class DBTableTransactionCommitter;
    void checkTransaction();
    // should be called by the subclass' destructor
    void resetCommitter();

public:
    static const int IDSPACING = 16;
    // for a full sequential get: rewind to first record
    virtual void rewind() = 0;

    // get next record in sequence
    virtual bool next(uint32_t*, string*) = 0;
    bool next(uint32_t*, string*, SymmCipher*);

    // get specific record by key
    virtual bool get(uint32_t, string*) = 0;

    // update or add specific record
    virtual bool put(uint32_t, char*, unsigned) = 0;
    bool put(uint32_t, string*);
    bool put(uint32_t, Cacheable *, SymmCipher*);

    // delete specific record
    virtual bool del(uint32_t) = 0;

    // delete all records
    virtual void truncate() = 0;

    // begin transaction
    virtual void begin() = 0;

    // commit transaction
    virtual void commit() = 0;

    // abort transaction
    virtual void abort() = 0;

    // permanantly remove all database info
    virtual void remove() = 0;

    // whether an unmatched begin() has been issued
    virtual bool inTransaction() const = 0;

    void checkCommitter(DBTableTransactionCommitter*);

    // autoincrement
    uint32_t nextid;

    DbTable(PrnGen &rng, bool alwaysTransacted);
    virtual ~DbTable() { }
    DBTableTransactionCommitter *getTransactionCommitter() const;
};

class MEGA_API DBTableNodes
{
public:

    // add or update a node
    virtual bool put(Node* node) = 0;

    // remove one node from 'nodes' table
    virtual bool remove(NodeHandle nodehandle) = 0;

    // remove all nodes from 'nodes' table (truncate)
    virtual bool removeNodes() = 0;

    // get nodes and queries about nodes
    virtual bool getNode(NodeHandle nodehandle, NodeSerialized& nodeSerialized) = 0;
    virtual bool getNodesByOrigFingerprint(const std::string& fingerprint, std::vector<std::pair<NodeHandle, NodeSerialized>>& nodes) = 0;
    virtual bool getNodesByName(const std::string& name, std::vector<std::pair<NodeHandle, NodeSerialized>>& nodes) = 0;
    virtual bool getRecentNodes(unsigned maxcount, m_time_t since, std::vector<std::pair<NodeHandle, NodeSerialized>>& nodes) = 0;

    virtual bool getRootNodes(std::vector<std::pair<NodeHandle, NodeSerialized>>& nodes) = 0;
    virtual bool getNodesWithSharesOrLink(std::vector<std::pair<NodeHandle, NodeSerialized>>&, ShareType_t shareType) = 0;
    virtual bool getFavouritesHandles(NodeHandle node, uint32_t count, std::vector<mega::NodeHandle>& nodes) = 0;
    virtual bool getNodeByNameAtFirstLevel(NodeHandle parentHandle, const std::string& name, nodetype_t nodeType, std::pair<NodeHandle, NodeSerialized>& node) = 0;

    virtual bool isNodeInDB(NodeHandle node) = 0;

    virtual bool isAncestor(NodeHandle node, NodeHandle ancestror) = 0;

    // Get all fingerprints with their asociated NodeHandle
    virtual bool loadFingerprintsAndChildren(std::map<FileFingerprint, std::map<NodeHandle, Node*>, FileFingerprintCmp>& fingerprints, std::map<NodeHandle, std::set<NodeHandle>>& children) = 0;

    // count of items in 'nodes' table. Returns 0 if error
    virtual uint64_t getNumberOfNodes() = 0;


    // -- get node properties --

    virtual bool getNodeSizeAndType(NodeHandle node, m_off_t& size, nodetype_t& nodeType) = 0;

    virtual void cancelQuery() = 0;

    virtual void updateCounter(NodeHandle nodeHandle, const std::string& nodeCounterBlob) = 0;

};

class MEGA_API DBTableTransactionCommitter
{
    DbTable* mTable;
    bool mStarted = false;
    std::thread::id threadId;

public:
    void beginOnce()
    {
        if (mTable && !mStarted)
        {
            mTable->begin();
            mStarted = true;
        }
    }

    void commitNow()
    {
        if (mTable)
        {
            if (mStarted)
            {
                mTable->commit();
                mStarted = false;
            }
        }
    }

    void reset()
    {
        mTable = nullptr;
    }

    ~DBTableTransactionCommitter()
    {
        if (mTable)
        {
            commitNow();
            mTable->mTransactionCommitter = nullptr;
        }
    }

    explicit DBTableTransactionCommitter(unique_ptr<DbTable>& t)
        : mTable(t.get()), threadId(std::this_thread::get_id())
    {
        if (mTable)
        {
            if (mTable->mTransactionCommitter)
            {
                assert(mTable->mTransactionCommitter->threadId == threadId);
                mTable = nullptr;  // we are nested; this one does nothing.  This can occur during eg. putnodes response when the core sdk and the intermediate layer both do db work.
            }
            else
            {
                mTable->mTransactionCommitter = this;
            }
        }
    }


    MEGA_DISABLE_COPY_MOVE(DBTableTransactionCommitter)
};

enum DbOpenFlag
{
    // Recycle legacy database, if present.
    DB_OPEN_FLAG_RECYCLE = 0x1,
    // Operations should always be transacted.
    DB_OPEN_FLAG_TRANSACTED = 0x2
}; // DbOpenFlag

struct MEGA_API DbAccess
{
    static const int LEGACY_DB_VERSION;
    static const int DB_VERSION;
    static const int LAST_DB_VERSION_WITHOUT_NOD;

    DbAccess();

    virtual ~DbAccess() { }

    virtual bool checkDbFileAndAdjustLegacy(FileSystemAccess& fsAccess, const string& name, const int flags, LocalPath& dbPath) = 0;

    virtual DbTable* open(PrnGen &rng, FileSystemAccess& fsAccess, const string& name, const int flags = 0x0) = 0;

    // use this method to get a `DbTable` that also implements `DbTableNodes` interface
    virtual DbTable* openTableWithNodes(PrnGen &rng, FileSystemAccess& fsAccess, const string& name, const int flags = 0x0) = 0;

    virtual bool probe(FileSystemAccess& fsAccess, const string& name) const = 0;

    virtual const LocalPath& rootPath() const = 0;

    int currentDbVersion;
};

// Convenience.
using DbAccessPtr = unique_ptr<DbAccess>;
using DbTablePtr = unique_ptr<DbTable>;

} // namespace

#endif
