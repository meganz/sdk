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
    bool mDecrypted = true;
    std::string mNode;
};

class MEGA_API DbTable
{
    static const int IDSPACING = 16;
    PrnGen &rng;

protected:
    bool mCheckAlwaysTransacted = false;
    DBTableTransactionCommitter* mTransactionCommitter = nullptr;
    friend class DBTableTransactionCommitter;
    void checkTransaction();
    // should be called by the subclass' destructor
    void resetCommitter();

public:
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

    // Get/Set values in data base with string as key
    virtual std::string getVar(const std::string& name) = 0;
    virtual bool setVar(const std::string& name, const std::string& value) = 0;

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
    typedef enum { NO_SHARES = 0x00, IN_SHARES = 0x01, OUT_SHARES = 0x02, PENDING_OUTSHARES = 0x04, LINK = 0x08} ShareType_t;

    // get nodes and queries about nodes
    virtual bool getNode(NodeHandle nodehandle, NodeSerialized& nodeSerialized) = 0;
    virtual bool getNodes(std::vector<NodeSerialized>& nodes) = 0;
    virtual bool getNodesByFingerprint(const FileFingerprint& fingerprint, std::map<mega::NodeHandle, NodeSerialized>& nodes) = 0;
    virtual bool getNodesByOrigFingerprint(const std::string& fingerprint, std::map<mega::NodeHandle, NodeSerialized>& nodes) = 0;
    virtual bool getNodeByFingerprint(const FileFingerprint& fingerprint, NodeSerialized& node) = 0;
    virtual bool getRootNodes(std::map<mega::NodeHandle, NodeSerialized>& nodes) = 0;
    virtual bool getNodesWithSharesOrLink(std::map<mega::NodeHandle, NodeSerialized>, ShareType_t shareType) = 0;
    virtual bool getChildrenFromNode(NodeHandle parentHandle, std::map<NodeHandle, NodeSerialized>& children) = 0;
    virtual bool getChildrenHandlesFromNode(NodeHandle node, std::vector<NodeHandle>& nodes) = 0;
    virtual bool getNodesByName(const std::string& name, std::map<mega::NodeHandle, NodeSerialized>& nodes) = 0;
    virtual NodeCounter getNodeCounter(NodeHandle node, bool parentIsFile) = 0;
    virtual bool getFavouritesNodeHandles(NodeHandle node, uint32_t count, std::vector<mega::NodeHandle>& nodes) = 0;
    virtual int getNumberOfChildrenFromNode(NodeHandle parentHandle) = 0;
    virtual bool isNodesOnDemandDb() = 0;
    virtual NodeHandle getFirstAncestor(NodeHandle node) = 0;
    virtual bool isNodeInDB(NodeHandle node) = 0;
    virtual bool isAncestor(NodeHandle node, NodeHandle ancestror) = 0;
    virtual bool isFileNode(NodeHandle node) = 0;
    virtual uint64_t getNumberOfNodes() = 0;

    // update or add a node
    virtual bool put(Node* node) = 0;

    // Remove nodes
    virtual bool del(NodeHandle nodehandle) = 0;
    virtual bool removeNodes() = 0;

    static int getShareType(Node *);
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

    DbAccess();

    virtual ~DbAccess() { }

    virtual DbTable* open(PrnGen &rng, FileSystemAccess& fsAccess, const string& name, const int flags = 0x0) = 0;

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
