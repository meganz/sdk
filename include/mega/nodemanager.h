/**
 * @file mega/nodemanager.h
 * @brief Client access engine core logic
 *
 * (c) 2013-2023 by Mega Limited, Auckland, New Zealand
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

#include <thread>
#ifndef NODEMANAGER_H
#define NODEMANAGER_H 1

#include <map>
#include <limits>
#include <set>
#include <vector>
#include "node.h"
#include "types.h"

namespace mega {

class DBTableNodes;
struct FileFingerprint;
class FingerprintContainer;
class MegaClient;
class NodeSerialized;

class NodeSearchFilter
{
public:
    static constexpr char TAG_DELIMITER = ',';

    enum class BoolFilter
    {
        disabled = 0,
        onlyTrue,
        onlyFalse,
    };

    enum class TextQueryJoiner
    {
        logicalAnd,
        logicalOr,
    };

    void byAncestors(std::vector<handle>&& ancs) { assert(ancs.size() == 3); mLocationHandles.swap(ancs); }
    void setIncludedShares(ShareType_t s) { mIncludedShares = s; }

    void byName(const std::string& name)
    {
        mNameFilter = name;
    }

    void byNodeType(nodetype_t nodeType)
    {
        assert(nodeType >= nodetype_t::TYPE_UNKNOWN && nodeType <= nodetype_t::FOLDERNODE);
        mNodeType = nodeType;
    }

    void byCategory(const MimeType_t category)
    {
        mMimeCategory = category;
    }

    void bySensitivity(BoolFilter boolFilter)
    {
        mExcludeSensitive = boolFilter;
    }

    void byFavourite(const BoolFilter byFav)
    {
        mFavouriteFilterOption = byFav;
    }

    void byLocationHandle(const handle location)
    {
        mLocationHandles = {location, UNDEF, UNDEF};
    }

    void byCreationTimeLowerLimitInSecs(int64_t creationLowerLimit)
    {
        mCreationLowerLimit = creationLowerLimit;
    }

    void byCreationTimeUpperLimitInSecs(int64_t creationUpperLimit)
    {
        mCreationUpperLimit = creationUpperLimit;
    }

    void byModificationTimeLowerLimitInSecs(int64_t modificationLowerLimit)
    {
        mModificationLowerLimit = modificationLowerLimit;
    }

    void byModificationTimeUpperLimitInSecs(int64_t modificationUpperLimit)
    {
        mModificationUpperLimit = modificationUpperLimit;
    }

    void byDescription(const std::string& description)
    {
        mDescriptionFilter = escapeWildCards(description);
    }

    void byTag(const std::string& tag)
    {
        mTagFilter = escapeWildCards(tag);
        mTagFilterContainsSeparator = mTagFilter.getText().find(TAG_DELIMITER) != std::string::npos;
    }

    void useAndForTextQuery(const bool useAnd)
    {
        mUseAndForTextQuery = useAnd;
    }

    const std::string& byName() const
    {
        return mNameFilter.getText();
    }
    nodetype_t byNodeType() const { return mNodeType; }
    MimeType_t byCategory() const { return mMimeCategory; }
    BoolFilter byFavourite() const { return mFavouriteFilterOption; }
    BoolFilter bySensitivity() const { return mExcludeSensitive; }

    // recursive look-ups (searchNodes)
    const std::vector<handle>& byAncestorHandles() const { return mLocationHandles; }
    // non-recursive look-ups (getChildren)
    handle byParentHandle() const { assert(!mLocationHandles.empty()); return mLocationHandles[0]; }

    // recursive look-ups (searchNodes): type of shares to be included when searching;
    // non-recursive look-ups (getChildren): ignored.
    ShareType_t includedShares() const { return mIncludedShares; }

    int64_t byCreationTimeLowerLimit() const { return mCreationLowerLimit; }
    int64_t byCreationTimeUpperLimit() const { return mCreationUpperLimit; }

    int64_t byModificationTimeLowerLimit() const { return mModificationLowerLimit; }

    int64_t byModificationTimeUpperLimit() const
    {
        return mModificationUpperLimit;
    }

    const std::string& byDescription() const
    {
        return mDescriptionFilter.getText();
    }

    const std::string& byTag() const
    {
        return mTagFilter.getText();
    }

    bool useAndForTextQuery() const
    {
        return mUseAndForTextQuery;
    }

    bool hasNodeType() const
    {
        return mNodeType != TYPE_UNKNOWN;
    }

    bool hasCreationTimeLimits() const
    {
        return mCreationLowerLimit || mCreationUpperLimit;
    }

    bool hasModificationTimeLimits() const
    {
        return mModificationLowerLimit || mModificationUpperLimit;
    }

    bool hasCategory() const
    {
        return mMimeCategory != MIME_TYPE_UNKNOWN;
    }

    bool hasName() const
    {
        return !mNameFilter.getText().empty();
    }

    bool hasDescription() const
    {
        return !mDescriptionFilter.getText().empty();
    }

    bool hasTag() const
    {
        return !mTagFilter.getText().empty();
    }

    bool hasFav() const
    {
        return mFavouriteFilterOption != BoolFilter::disabled;
    }

    bool hasSensitive() const
    {
        return mExcludeSensitive != BoolFilter::disabled;
    }

    bool isValidNodeType(const nodetype_t nodeType) const;
    bool isValidCreationTime(const int64_t time) const;
    bool isValidModificationTime(const int64_t time) const;
    bool isValidCategory(const MimeType_t category, const nodetype_t nodeType) const;
    bool isValidName(const uint8_t* testName) const;
    bool isValidDescription(const uint8_t* testDescription) const;
    bool isValidTagSequence(const uint8_t* tagSequence) const;
    bool isValidFav(const bool isNodeFav) const;
    bool isValidSensitivity(const bool isNodeSensitive) const;

private:
    TextPattern mNameFilter;
    nodetype_t mNodeType = TYPE_UNKNOWN;
    MimeType_t mMimeCategory = MIME_TYPE_UNKNOWN;
    BoolFilter mFavouriteFilterOption = BoolFilter::disabled;
    BoolFilter mExcludeSensitive = BoolFilter::disabled;
    std::vector<handle> mLocationHandles {UNDEF, UNDEF, UNDEF}; // always contain 3 items
    ShareType_t mIncludedShares = NO_SHARES;
    int64_t mCreationLowerLimit = 0;
    int64_t mCreationUpperLimit = 0;
    int64_t mModificationLowerLimit = 0;
    int64_t mModificationUpperLimit = 0;
    TextPattern mDescriptionFilter;
    TextPattern mTagFilter;
    bool mTagFilterContainsSeparator{false};
    bool mUseAndForTextQuery{true};

    static bool isDocType(const MimeType_t t);
};

class NodeSearchPage
{
public:
    NodeSearchPage(size_t startingOffset, size_t size) : mOffset(startingOffset), mSize(size) {}
    const size_t& startingOffset() const { return mOffset; }
    const size_t& size() const { return mSize; }

private:
    size_t mOffset;
    size_t mSize;
};

/**
 * @brief The NodeManager class
 *
 * This class encapsulates the access to nodes. It hides the details to
 * access to the Node object: in case it's not loaded in RAM, it will
 * load it from the "nodes" DB table.
 *
 * The same DB file is used for the "statecache" and the "nodes" table, and
 * both tables need to follow the same domain for transactions: a commit is
 * triggered by the reception of a sequence-number in the actionpacket (scsn).
 */
class MEGA_API NodeManager
{
public:
    NodeManager(MegaClient& client);

    // set interface to access to "nodes" table
    void setTable(DBTableNodes *table);

    // set interface to access to "nodes" table to nullptr, it's called just after sctable.reset()
    void reset();

    // Take node ownership
    typedef map<NodeHandle,  set<std::shared_ptr<Node>>> MissingParentNodes;
    bool addNode(std::shared_ptr<Node> node, bool notify, bool isFetching, MissingParentNodes& missingParentNodes);
    bool updateNode(Node* node);
    // removeNode() --> it's done through notifypurge()

    // if node is not available in memory, it's loaded from DB
    std::shared_ptr<Node> getNodeByHandle(NodeHandle handle);

    // read children from DB and load them in memory
    sharedNode_list getChildren(const Node *parent, CancelToken cancelToken = CancelToken());

    sharedNode_vector getChildren(const NodeSearchFilter& filter, int order, CancelToken cancelFlag, const NodeSearchPage& page);

    // get up to "maxcount" nodes, not older than "since", ordered by creation time
    // Note: nodes are read from DB and loaded in memory
    sharedNode_vector getRecentNodes(unsigned maxcount,
                                     m_time_t since,
                                     bool excludeSensitives = false);

    sharedNode_vector searchNodes(const NodeSearchFilter& filter, int order, CancelToken cancelFlag, const NodeSearchPage& page);

    /*
     * @brief
     * Get all node tags below a specified node.
     *
     * @param cancelToken
     * A token that can be used to terminate the query's execution prematurely.
     *
     * @param handles
     * A set of handles specifying which nodes we want to list tags below.
     *
     * If undefined, the query will list tags below all root nodes.
     *
     * @param pattern
     * An optional pattern that can be used to filter which tags we list.
     *
     * @returns
     * std::nullopt on failure.
     * std::set<std::string> on success.
     */
    auto getNodeTagsBelow(CancelToken cancelToken,
                          const std::set<NodeHandle>& handles,
                          const std::string& pattern = {}) -> std::optional<std::set<std::string>>;

    sharedNode_vector getNodesByFingerprint(const FileFingerprint& fingerprint);
    sharedNode_vector getNodesByOrigFingerprint(const std::string& fingerprint, Node *parent);
    std::shared_ptr<Node> getNodeByFingerprint(FileFingerprint &fingerprint);

    // Return a first level child node whose name matches with 'name'
    // Valid values for nodeType: FILENODE, FOLDERNODE
    // Note: if not found among children loaded in RAM (and not all children are loaded), it will search in DB
    // Hint: ensure all children are loaded if this method is called for all children of a folder
    std::shared_ptr<Node> childNodeByNameType(const Node *parent, const std::string& name, nodetype_t nodeType);

    // Returns ROOTNODE, INCOMINGNODE, RUBBISHNODE (In case of logged into folder link returns only ROOTNODE)
    // Load from DB if it's necessary
    sharedNode_vector getRootNodes();

    sharedNode_vector getNodesWithInShares(); // both, top-level and nested ones
    sharedNode_vector getNodesWithOutShares();
    sharedNode_vector getNodesWithPendingOutShares();
    sharedNode_vector getNodesWithLinks();

    std::vector<NodeHandle> getFavouritesNodeHandles(NodeHandle node, uint32_t count);
    size_t getNumberOfChildrenFromNode(NodeHandle parentHandle);

    // Returns the number of children nodes of specific node type with a query to DB
    // Valid types are FILENODE and FOLDERNODE
    size_t getNumberOfChildrenByType(NodeHandle parentHandle, nodetype_t nodeType);

    // true if 'node' is a child node of 'ancestor', false otherwise.
    bool isAncestor(NodeHandle nodehandle, NodeHandle ancestor, CancelToken cancelFlag);

    // Clean 'changed' flag from all nodes
    void removeChanges();

    // Remove all nodes from all caches
    void cleanNodes();

    // Use blob received as parameter to generate a node
    // Used to generate nodes from old cache
    std::shared_ptr<Node> getNodeFromBlob(const string* nodeSerialized);

    // attempt to apply received keys to decrypt node's keys
    void applyKeys(uint32_t appliedKeys);

    // add node to the notification queue
    void notifyNode(std::shared_ptr<Node> node, sharedNode_vector* nodesToReport = nullptr);

    // for consistently notifying when updating node counters
    void setNodeCounter(std::shared_ptr<Node> n, const NodeCounter &counter, bool notify, sharedNode_vector* nodesToReport);

    // process notified/changed nodes from 'mNodeNotify': dump changes to DB
    void notifyPurge();

    size_t nodeNotifySize() const;

    // Returns if cache has been loaded
    bool hasCacheLoaded();

    // Load rootnodes (ROOTNODE, INCOMING, RUBBISH), its first-level children
    // and root of incoming shares. Return true if success, false if error
    bool loadNodes();

    // Returns total of nodes in the account (cloud+inbox+rubbish AND inshares), including versions
    uint64_t getNodeCount();

    // return the counter for all root nodes (cloud+inbox+rubbish)
    NodeCounter getCounterOfRootNodes();

    // update the counter of 'n' when its parent is updated (from 'oldParent' to 'n.parent')
    void updateCounter(std::shared_ptr<Node> n, std::shared_ptr<Node> oldParent);

    // true if 'h' is a rootnode: cloud, inbox or rubbish bin
    bool isRootNode(NodeHandle h) const;

    // Set values to mClient.rootnodes for ROOTNODE, INBOX and RUBBISH
    bool setrootnode(std::shared_ptr<Node> node);

    // Add fingerprint to mFingerprint. If node isn't going to keep in RAM
    // node isn't added
    FingerprintPosition insertFingerprint(Node* node);
    // Remove fingerprint from mFingerprint
    void removeFingerprint(Node* node, bool unloadNode = false);
    FingerprintPosition invalidFingerprintPos();
    std::list<std::shared_ptr<Node>>::const_iterator invalidCacheLRUPos() const;

    // Node has received last updates and it's ready to store in DB
    void saveNodeInDb(Node *node);

    // write all nodes into DB (used for migration from legacy to NOD DB schema)
    void dumpNodes();

    // This method only can be used in Megacli for testing purposes
    uint64_t getNumberNodesInRam() const;

    // Add new relationship between parent and child
    void addChild(NodeHandle parent, NodeHandle child, Node *node);
    // remove relationship between parent and child
    void removeChild(Node *parent, NodeHandle child);

    // Returns the number of versions for a node (including the current version)
    int getNumVersions(NodeHandle nodeHandle);

    NodeHandle getRootNodeFiles() const;
    NodeHandle getRootNodeVault() const;
    NodeHandle getRootNodeRubbish() const;
    void setRootNodeFiles(NodeHandle h);
    void setRootNodeVault(NodeHandle h);
    void setRootNodeRubbish(NodeHandle h);

    // In case of orphans send an event
    void checkOrphanNodes(MissingParentNodes& nodesWithMissingParent);

    // This method is called when initial fetch nodes is finished
    // Initialize node counters and create indexes at DB
    void initCompleted();

    std::shared_ptr<Node> getNodeFromNodeManagerNode(NodeManagerNode& nodeManagerNode);

    void insertNodeCacheLRU(std::shared_ptr<Node> node);

    void increaseNumNodesInRam();
    void decreaseNumNodesInRam();

    uint64_t getCacheLRUMaxSize() const;
    void setCacheLRUMaxSize(uint64_t cacheLRUMaxSize);

    uint64_t getNumNodesAtCacheLRU() const;

    // true when the filesystem has been initialized
    // i.e., when nodes have been fully loaded from a fetchnodes or from cache
    bool ready();

private:
    class NoKeyLogger
    {
    public:
        void log(const Node& node) const;

    private:
        // How many no key nodes has been counted for logging
        mutable std::atomic_int mCount{1};
    };

    MegaClient& mClient;

#if defined(DEBUG)
    using MutexType = CheckableMutex<std::recursive_mutex>;
#else // DEBUG
    using MutexType = std::recursive_mutex;
#endif // ! DEBUG

    using LockGuard = std::lock_guard<MutexType>;

    mutable MutexType mMutex;

    // interface to handle accesses to "nodes" table
    DBTableNodes* mTable = nullptr;

    // logger with rate limitting for no key
    static NoKeyLogger mNoKeyLogger;

    // root nodes (files, vault, rubbish)
    struct Rootnodes
    {
        NodeHandle files;
        NodeHandle vault;
        NodeHandle rubbish;
        std::map<nodetype_t, std::shared_ptr<Node> > mRootNodes;

        // minimum expected number of root nodes (min num of root nodes may vary depending on
        // client type i.e password manager)
        static constexpr uint8_t MIN_NUM_ROOT_NODES{3};
        // returns true if the 'h' provided matches any of the rootnodes.
        // (when logged into folder links, the handle of the folder is set to 'files')
        bool isRootNode(NodeHandle h) const { return (h == files || h == vault || h == rubbish); }
        void clear();
    } rootnodes;

    class FingerprintContainer : public fingerprint_set
    {
    public:
        bool allFingerprintsAreLoaded(const FileFingerprint *fingerprint) const;
        void setAllFingerprintLoaded(const FileFingerprint *fingerprint);
        void removeAllFingerprintLoaded(const FileFingerprint *fingerprint);
        void clear();

    private:
        // it stores all FileFingerprint that have been looked up in DB, so it
        // avoid the DB query for future lookups (includes non-existing (yet) fingerprints)
        std::set<FileFingerprint, FileFingerprintCmp> mAllFingerprintsLoaded;
    };

    // Stores nodes that have been loaded in RAM from DB (not necessarily all of them)
    std::map<NodeHandle, NodeManagerNode> mNodes;

    uint64_t mCacheLRUMaxSize = std::numeric_limits<uint64_t>::max();
    std::list<std::shared_ptr<Node> > mCacheLRU;

    std::atomic<uint64_t> mNodesInRam;

    // nodes that have changed and are pending to notify to app and dump to DB
    sharedNode_vector mNodeNotify;

    shared_ptr<Node> getNodeInRAM(NodeHandle handle);
    void saveNodeInRAM(std::shared_ptr<Node> node, bool isRootnode, MissingParentNodes& missingParentNodes);    // takes ownership

    sharedNode_vector getNodesWithSharesOrLink_internal(ShareType_t shareType);

    enum OperationType
    {
        INCREASE = 0,
        DECREASE,
    };

    // Update a node counter for 'origin' and its subtree (recursively)
    // If operationType is INCREASE, nc is added, in other case is decreased (ie. upon deletion)
    void updateTreeCounter(std::shared_ptr<Node> origin, NodeCounter nc, OperationType operation, sharedNode_vector* nodesToReport);

    // returns nullptr if there are unserialization errors. Also triggers a full reload (fetchnodes)
    shared_ptr<Node> getNodeFromNodeSerialized(const NodeSerialized& nodeSerialized);

    // reads from DB and loads the node in memory
    shared_ptr<Node> unserializeNode(const string*, bool fromOldCache);

    // returns the counter for the specified node, calculating it recursively and accessing to DB if it's neccesary
    NodeCounter calculateNodeCounter(const NodeHandle &nodehandle, nodetype_t parentType, std::shared_ptr<Node> node, bool isInRubbish);

    // Container storing FileFingerprint* (Node* in practice) ordered by fingerprint
    FingerprintContainer mFingerPrints;

    // Return a node from Data base, node shouldn't be in RAM previously
    shared_ptr<Node> getNodeFromDataBase(NodeHandle handle);

    // Returns root nodes without nested in-shares
    sharedNode_vector getRootNodesAndInshares();

    // Process unserialized nodes read from DB
    // Avoid loading nodes whose ancestor is not ancestorHandle. If ancestorHandle is undef load all nodes
    // If a valid cancelFlag is passed and takes true value, this method returns without complete operation
    // If a valid object is passed, it must be kept alive until this method returns.
    sharedNode_vector processUnserializedNodes(const std::vector<std::pair<NodeHandle, NodeSerialized>>& nodesFromTable, NodeHandle ancestorHandle = NodeHandle(), CancelToken cancelFlag = CancelToken());

    sharedNode_vector searchNodes_internal(const NodeSearchFilter& filter, int order, CancelToken cancelFlag, const NodeSearchPage& page);
    sharedNode_vector processUnserializedNodes(const std::vector<std::pair<NodeHandle, NodeSerialized>>& nodesFromTable, CancelToken cancelFlag);
    sharedNode_vector getChildren_internal(const NodeSearchFilter& filter, int order, CancelToken cancelFlag, const NodeSearchPage& page);
    sharedNode_vector getRecentNodes_internal(const NodeSearchPage& page, m_time_t since);

    // node temporary in memory, which will be removed upon write to DB
    std::shared_ptr<Node> mNodeToWriteInDb;

    // Stores (or updates) the node in the DB. It also tries to decrypt it for the last time before storing it.
    void putNodeInDb(Node* node) const;

    // true when the NodeManager has been inicialized and contains a valid filesystem
    bool mInitialized = false;

    // flag that determines if null root nodes error has already been reported
    bool mNullRootNodesReported{false};

    // These are all the "internal" versions of the public interfaces.
    // This is to avoid confusion where public functions used to call other public functions
    // but that introudces confusion about where the mutex gets locked.
    // Now the public interfaces lock the mutex once, and call these internal functions
    // which have all the original code in them.
    // Internal functions only call other internal fucntions, and that keeps things simple
    // We would use a non-recursive mutex for more precise control, and to make sure we can unlock
    // it when we need to make callbacks to the app.
    // It's quite a verbose approach, but at least simple, easy to understand, and easy to get right.
    void setTable_internal(DBTableNodes *table);
    void reset_internal();
    bool addNode_internal(std::shared_ptr<Node> node, bool notify, bool isFetching, MissingParentNodes& missingParentNodes);
    bool updateNode_internal(Node* node);

    /**
     * @brief Manages null root nodes error server event (just once in NodeManager lifetime)
     * This method sends an event to stats server and prints a log error to inform about this
     * scenario.
     */
    void reportNullRootNodes(const size_t rootNodesSize);

    std::shared_ptr<Node> getNodeByHandle_internal(NodeHandle handle);
    sharedNode_list getChildren_internal(const Node* parent,
                                         CancelToken cancelToken = CancelToken());

    sharedNode_vector getNodesByFingerprint_internal(const FileFingerprint& fingerprint);
    sharedNode_vector getNodesByOrigFingerprint_internal(const std::string& fingerprint, Node *parent);
    std::shared_ptr<Node> getNodeByFingerprint_internal(FileFingerprint &fingerprint);
    std::shared_ptr<Node> childNodeByNameType_internal(const Node *parent, const std::string& name, nodetype_t nodeType);
    sharedNode_vector getRootNodes_internal();

    std::vector<NodeHandle> getFavouritesNodeHandles_internal(NodeHandle node, uint32_t count);
    size_t getNumberOfChildrenFromNode_internal(NodeHandle parentHandle);
    size_t getNumberOfChildrenByType_internal(NodeHandle parentHandle, nodetype_t nodeType);
    bool isAncestor_internal(NodeHandle nodehandle, NodeHandle ancestor, CancelToken cancelFlag);
    void removeChanges_internal();
    void cleanNodes_internal();
    std::shared_ptr<Node> getNodeFromBlob_internal(const string* nodeSerialized);
    void applyKeys_internal(uint32_t appliedKeys);
    void notifyNode_internal(std::shared_ptr<Node> node, sharedNode_vector* nodesToReport);
    bool loadNodes_internal();
    uint64_t getNodeCount_internal();
    NodeCounter getCounterOfRootNodes_internal();
    void updateCounter_internal(std::shared_ptr<Node> n, std::shared_ptr<Node> oldParent);
    bool setrootnode_internal(std::shared_ptr<Node> node);
    FingerprintPosition insertFingerprint_internal(Node* node);
    void removeFingerprint_internal(Node* node, bool unloadNode);
    void saveNodeInDb_internal(Node *node);
    void dumpNodes_internal();
    void addChild_internal(NodeHandle parent, NodeHandle child, Node *node);
    void removeChild_internal(Node *parent, NodeHandle child);
    void setRootNodeFiles_internal(NodeHandle h);
    void setRootNodeVault_internal(NodeHandle h);
    void setRootNodeRubbish_internal(NodeHandle h);
    void initCompleted_internal();
    void insertNodeCacheLRU_internal(std::shared_ptr<Node> node);
    void unLoadNodeFromCacheLRU();
};

} // namespace

#endif
