
/**
 * @file mega/node.h
 * @brief Classes for accessing local and remote nodes
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

#ifndef MEGA_NODE_H
#define MEGA_NODE_H 1

#include "filefingerprint.h"
#include "file.h"
#include "attrmap.h"
#include "syncfilter.h"
#include "backofftimer.h"

namespace mega {

typedef map<LocalPath, LocalNode*> localnode_map;

struct MEGA_API NodeCore
{
    // node's own handle
    handle nodehandle = UNDEF;

    // inline convenience function to get a typed version that ensures we use the 6 bytes of a node handle, and not 8
    NodeHandle nodeHandle() const { return NodeHandle().set6byte(nodehandle); }

    // parent node handle (in a Node context, temporary placeholder until parent is set)
    handle parenthandle = UNDEF;

    // node type
    nodetype_t type = TYPE_UNKNOWN;

    // node attributes
    std::unique_ptr<string> attrstring;
};

struct CloudNode
{
    // We can't use Node* directly on the sync thread,as such pointers
    // may be rendered dangling (and changes in Nodes thread-unsafe)
    // by actionpackets on the MegaClient thread.
    // So, we take temporary copies of the minimally needed aspects.
    // These are only used while recursing the LocalNode tree.

    string name;
    nodetype_t type = TYPE_UNKNOWN;
    NodeHandle handle;
    NodeHandle parentHandle;
    nodetype_t parentType = TYPE_UNKNOWN;
    FileFingerprint fingerprint;

    CloudNode() {}
    CloudNode(const Node& n);

    // Query whether this cloud node represents an ignore file.
    bool isIgnoreFile() const;
};

class SyncThreadsafeState;

struct SyncTransfer_inClient: public File
{
    // self-destruct after completion
    void completed(Transfer*, putsource_t) override;
    void terminated(error) override;

    // We will be passing a raw pointer to this object
    // into the tranfer system on the client thread.
    // this member prevents that becoming a dangling pointer
    // should the sync no longer require it.  So we set this
    // member just before startxfer, and reset it on completed()/terminated()
    shared_ptr<SyncTransfer_inClient> selfKeepAlive;

    shared_ptr<SyncThreadsafeState> syncThreadSafeState;

    // Why was the transfer failed/terminated?
    error mError = API_OK;

    bool wasTerminated = false;
    bool wasCompleted = false;
    bool wasRequesterAbandoned = false;

    int transferTag = -1;
};

struct SyncDownload_inClient: public SyncTransfer_inClient
{
    shared_ptr<FileDistributor> downloadDistributor;

    // set sync-specific temp filename, update treestate
    void prepare(FileSystemAccess&) override;
    bool failed(error, MegaClient*) override;

    SyncDownload_inClient(CloudNode& n, const LocalPath&, bool fromInshare,
            shared_ptr<SyncThreadsafeState> stss, const FileFingerprint& overwriteFF);

    ~SyncDownload_inClient();

    // True if we could copy (or move) the download into place.
    bool wasDistributed = false;

    FileFingerprint okToOverwriteFF;
};

struct SyncUpload_inClient : SyncTransfer_inClient, std::enable_shared_from_this<SyncUpload_inClient>
{
    // This class is part of the client's Transfer system (ie, works in the client's thread)
    // The sync system keeps a shared_ptr to it.  Whichever system finishes with it last actually deletes it
    SyncUpload_inClient(NodeHandle targetFolder, const LocalPath& fullPath,
            const string& nodeName, const FileFingerprint& ff, shared_ptr<SyncThreadsafeState> stss,
            handle fsid, const LocalPath& localname, bool fromInshare);
    ~SyncUpload_inClient();

    void prepare(FileSystemAccess&) override;
    void completed(Transfer*, putsource_t) override;

    bool putnodesStarted = false;

    // Valid when wasPutnodesCompleted is true.
    handle putnodesResultHandle;
    bool putnodesFailed = false;

    std::atomic<bool> wasPutnodesCompleted{false};
    //std::atomic<bool> renameInProgress{false};

    handle sourceFsid;
    LocalPath sourceLocalname;

    // once the upload completes these are set.  todo: should we dynamically allocate space for these, save RAM for mass transfer cases?
    UploadHandle uploadHandle;
    UploadToken uploadToken;
    FileNodeKey fileNodeKey;

    void sendPutnodes(MegaClient* client, NodeHandle ovHandle);
};

// new node for putnodes()
struct MEGA_API NewNode : public NodeCore
{
    static const int OLDUPLOADTOKENLEN = 27;

    string nodekey;

    newnodesource_t source = NEW_NODE;

    NodeHandle ovhandle;
    UploadHandle uploadhandle;
    UploadToken uploadtoken;

    std::unique_ptr<string> fileattributes;

    // versioning used for this new node, forced at server's side regardless the account's value
    VersioningOption mVersioningOption = NoVersioning;
    bool added = false;           // set true when the actionpacket arrives
    bool canChangeVault = false;
    handle mAddedHandle = UNDEF;  // updated as actionpacket arrives
    error mError = API_OK;        // per-node error (updated in cs response)
};

struct MEGA_API PublicLink
{
    handle ph;
    m_time_t cts;
    m_time_t ets;
    bool takendown;
    string mAuthKey;

    PublicLink(handle ph, m_time_t cts, m_time_t ets, bool takendown, const char *authKey = nullptr);
    PublicLink(PublicLink *plink);

    bool isExpired();
};

// Container storing FileFingerprint* (Node* in practice) ordered by fingerprint.
struct Fingerprints
{
    // maps FileFingerprints to node
    using fingerprint_set = std::multiset<FileFingerprint*, FileFingerprintCmp>;
    using iterator = fingerprint_set::iterator;

    void newnode(Node* n);
    void add(Node* n);
    void remove(Node* n);
    void clear();
    m_off_t getSumSizes();

    Node* nodebyfingerprint(FileFingerprint* fingerprint);
    node_vector *nodesbyfingerprint(FileFingerprint* fingerprint);

private:
    fingerprint_set mFingerprints;
    m_off_t mSumSizes = 0;
};

struct CommandChain
{
    // convenience functions, hides the unique_ptr aspect, removes it when empty
    bool empty()
    {
        return !chain || chain->empty();
    }

    void push_back(Command* c)
    {
        if (!chain)
        {
            chain.reset(new std::list<Command*>);
        }
        chain->push_back(c);
    }

    void erase(Command* c)
    {
        if (chain)
        {
            for (auto i = chain->begin(); i != chain->end(); ++i)
            {
                if (*i == c)
                {
                    chain->erase(i);
                    if (chain->empty())
                    {
                        chain.reset();
                    }
                    return;
                }
            }
        }
    }

private:
    friend class CommandSetAttr;

    // most nodes don't have commands in progress so keep representation super small
    std::unique_ptr<std::list<Command*>> chain;
};


// filesystem node
struct MEGA_API Node : public NodeCore, FileFingerprint
{
    MegaClient* client = nullptr;

    // supplies the nodekey (which is private to ensure we track changes to it)
    const string& nodekey() const;

    // Also returns the key but does not assert that the key has been applied.  Only use it where we don't need the node to be readable.
    const string& nodekeyUnchecked() const;

    // check if the key is present and is the correct size for this node
    bool keyApplied() const;

    // change parent node association
    bool setparent(Node*);

    // follow the parent links all the way to the top
    const Node* firstancestor() const;

    // If this is a file, and has a file for a parent, it's not the latest version
    const Node* latestFileVersion() const;

    // Node's depth, counting from the cloud root.
    unsigned depth() const;

    // try to resolve node key string
    bool applykey(bool notAppliedOk = false);

    // set up nodekey in a static SymmCipher
    SymmCipher* nodecipher();

    // decrypt attribute string and set fileattrs
    void setattr();

    // display name (UTF-8)
    const char* displayname() const;

    // check if the name matches (UTF-8)
    bool hasName(const string&) const;

    // check if this node has a name.
    bool hasName() const;

    // display path from its root in the cloud (UTF-8)
    string displaypath() const;

    // return mimetype type
    MimeType_t getMimeType(bool checkPreview = false) const;

    // node attributes
    AttrMap attrs;

    // {backup-id, state} pairs received in "sds" node attribute
    vector<pair<handle, int>> getSdsBackups() const;
    static nameid sdsId();
    static string toSdsString(const vector<pair<handle, int>>&);

    // track upcoming attribute changes for this node, so we can reason about current vs future state
    CommandChain mPendingChanges;

    // owner
    handle owner = mega::UNDEF;

    // actual time this node was created (cannot be set by user)
    m_time_t ctime = 0;

    // file attributes
    string fileattrstring;

    // check presence of file attribute
    int hasfileattribute(fatype) const;
    static int hasfileattribute(const string *fileattrstring, fatype);

    // decrypt node attribute string
    static byte* decryptattr(SymmCipher*, const char*, size_t);

    // parse node attributes from an incoming buffer, this function must be called after call decryptattr
    static void parseattr(byte*, AttrMap&, m_off_t, m_time_t&, string&, string&, FileFingerprint&);

    // inbound share
    Share* inshare = nullptr;

    // outbound shares by user
    share_map* outshares = nullptr;

    // outbound pending shares
    share_map* pendingshares = nullptr;

    // incoming/outgoing share key
    SymmCipher* sharekey = nullptr;

    // app-private pointer
    void* appdata = nullptr;

    bool foreignkey = false;

    struct
    {
        bool removed : 1;
        bool attrs : 1;
        bool owner : 1;
        bool ctime : 1;
        bool fileattrstring : 1;
        bool inshare : 1;
        bool outshares : 1;
        bool pendingshares : 1;
        bool parent : 1;
        bool publiclink : 1;
        bool newnode : 1;
        bool name : 1;
        bool favourite : 1;

#ifdef ENABLE_SYNC
        // this field is only used internally in syncdown()
        bool syncdown_node_matched_here : 1;
#endif

    } changed;

    void setkey(const byte* = NULL);

    void setkeyfromjson(const char*);

    void setfingerprint();

    void faspec(string*);

    NodeCounter subnodeCounts() const;

    // parent
    Node* parent = nullptr;

    // children
    node_list children;

    // own position in parent's children
    node_list::iterator child_it;

    // own position in fingerprint set (only valid for file nodes)
    Fingerprints::iterator fingerprint_it;

    // source tag.  The tag of the request or transfer that last modified this node (available in MegaApi)
    int tag = 0;

    // check if node is below this node
    bool isbelow(Node*) const;
    bool isbelow(NodeHandle) const;

    // handle of public link for the node
    PublicLink* plink = nullptr;

    void setpubliclink(handle, m_time_t, m_time_t, bool, const string &authKey = {});

    bool serialize(string*) override;
    static Node* unserialize(MegaClient*, const string*, node_vector*);

    Node(MegaClient*, vector<Node*>*, NodeHandle, NodeHandle, nodetype_t, m_off_t, handle, const char*, m_time_t);
    ~Node();

    Node* childbyname(const string& name);

    // Returns true if this node has a child with the given name.
    bool hasChildWithName(const string& name) const;

private:
    // full folder/file key, symmetrically or asymmetrically encrypted
    // node crypto keys (raw or cooked -
    // cooked if size() == FOLDERNODEKEYLENGTH or FILEFOLDERNODEKEYLENGTH)
    string nodekeydata;

    bool getExtension(std::string& ext) const;
    bool isPhoto(const std::string& ext, bool checkPreview) const;
    bool isVideo(const std::string& ext) const;
    bool isAudio(const std::string& ext) const;
    bool isDocument(const std::string& ext) const;

    static nameid getExtensionNameId(const std::string& ext);
};

inline const string& Node::nodekey() const
{
    assert(keyApplied() || type == ROOTNODE || type == VAULTNODE || type == RUBBISHNODE);
    return nodekeydata;
}

inline const string& Node::nodekeyUnchecked() const
{
    return nodekeydata;
}

inline bool Node::keyApplied() const
{
    return nodekeydata.size() == size_t((type == FILENODE) ? FILENODEKEYLENGTH : FOLDERNODEKEYLENGTH);
}

#ifdef ENABLE_SYNC

enum TreeState
{
    TREE_RESOLVED = 0,
    TREE_DESCENDANT_FLAGGED = 1,
    TREE_ACTION_HERE = 2,           // And also check if any children have flags set (ie, implicitly TREE_DESCENDANT_FLAGGED)
    TREE_ACTION_SUBTREE = 3         // overrides any children so the whole subtree is processed
};

enum ExclusionState : unsigned char
{
    // Node's definitely excluded.
    ES_EXCLUDED,
    // Node's definitely included.
    ES_INCLUDED,
    // Node has an indeterminate exclusion state.
    ES_UNKNOWN
}; // ExclusionState

inline TreeState updateTreestateFromChild(TreeState oldFlag, TreeState childFlag)
{
    return oldFlag == TREE_RESOLVED && childFlag != TREE_RESOLVED ? TREE_DESCENDANT_FLAGGED : oldFlag;
}

inline TreeState propagateSubtreeFlag(TreeState nodeFlag, TreeState childFlag)
{
    return nodeFlag == TREE_ACTION_SUBTREE ? TREE_ACTION_SUBTREE : TreeState(childFlag);
}

struct syncRow;
struct SyncPath;

struct MEGA_API LocalNode;

struct MEGA_API LocalNodeCore
  : public Cacheable
{
    // deserialize attributes from binary storage.
    bool read(const string& source, uint32_t& parentID);

    // serialize attributes to binary for storage.
    bool write(string& destination, uint32_t parentID);

    // local filesystem node ID (inode...) for rename/move detection
    handle fsid_lastSynced = ::mega::UNDEF;

    // The exact name of the file we are synced with, if synced
    // If not synced then it's the to-local (escaped) version of the CloudNode's name
    // This is also the key in the parent LocalNode's children map
    // (if this is the sync root node, it is an absolute path - otherwise just a leaf name)
    LocalPath localname;

    // for botched filesystems with legacy secondary ("short") names
    // Filesystem notifications could arrive with long or short names, and we need to recognise which LocalNode corresponds.
    // null means either the entry has no shortname or it's the same as the (normal) longname
    std::unique_ptr<LocalPath> slocalname = nullptr;

    // whether this node knew its shortname (otherwise it was loaded from an old db)
    bool slocalname_in_db = false;

    // related cloud node, if any
    NodeHandle syncedCloudNodeHandle;

    // The fingerprint of the node and/or file we are synced with
    FileFingerprint syncedFingerprint;

    // FILENODE or FOLDERNODE
    nodetype_t type = TYPE_UNKNOWN;

    // Once the local and remote names match exactly (taking into account escaping), we will keep them matching
    // This is so users can, for example, change uppercase/lowercase and have that synchronized.
    bool namesSynchronized = false;

}; // LocalNodeCore

struct MEGA_API LocalNode
  : public LocalNodeCore
{
    class Sync* sync = nullptr;

    // UTF8 NFC version of LocalNodeCore::localname.
    // Not serialized.
    // Should be updated whenever localname is.
    // Does not match the corresponding Node's name,
    // as escapes/case may be involved.
    string toName_of_localname;

    // parent linkage
    LocalNode* parent = nullptr;

    // children by name
    localnode_map children;

    unique_ptr<LocalPath> cloneShortname() const;
    localnode_map schildren;

    // The last scan of the folder (for folders).
    // Removed again when the folder is fully synced.
    std::unique_ptr<vector<FSNode>> lastFolderScan;

    // If we can regenerate the filsystem data at this node, no need to store it, save some RAM
    void clearRegeneratableFolderScan(SyncPath& fullPath, vector<syncRow>& childRows);

    fsid_localnode_map::iterator fsid_lastSynced_it;

    // we also need to track what fsid corresponded to our FSNode last time, even if not synced (not serialized)
    // if it changes, we should rescan, in case of LocalNode pre-existing with no FSNode, then one appears.  Or, now it's different
    handle fsid_asScanned = ::mega::UNDEF;
    fsid_localnode_map::iterator fsid_asScanned_it;

    // Fingerprint of the file as of the last scan.  TODO: does this make LocalNode too large?
    FileFingerprint scannedFingerprint;

    // related cloud node, if any
    nodehandle_localnode_map::iterator syncedCloudNodeHandle_it;

    // using a per-Localnode scan delay prevents self-notifications delaying the whole sync
    dstime scanDelayUntil = 0;
    unsigned expectedSelfNotificationCount = 0;
    //dstime lastScanTime = 0;

    struct
    {
        // Already-synced syncs on startup should not re-fingerprint files that match the synced fingerprint by fsid/size/mtime
        bool oneTimeUseSyncedFingerprintInScan : 1;

        // Determines whether we refingerprint a file when it is scanned.
        bool recomputeFingerprint : 1;

        // needs another recursiveSync for scanning at this level after pending changes
        TreeState scanAgain : 3;

        // needs another recursiveSync() to check moves at this level after pending changes
        // (can only be cleared if all scanAgain flags are clear)
        TreeState checkMovesAgain : 3;

        // needs another recursiveSync() for deletes/uploads/downloads at this level after pending changes
        // (can only be cleared if all checkMoveAgain flags are clear)
        TreeState syncAgain : 3;

        // whether any name conflicts have been detected.
        TreeState conflicts : 3;

        // fsids have been assigned in this node.
        bool unstableFsidAssigned : 1;

        // disappeared from local FS; we are moving the cloud node to the trash.
        bool deletedFS : 1;

        // we saw this node moved/renamed in the cloud, local move expected (or active)
        bool moveApplyingToLocal : 1;    // todo: do we need these anymore?
        bool moveAppliedToLocal : 1;

        unsigned scanInProgress : 1;
        unsigned scanObsolete : 1;

        // When recursing the tree, sometimes we need a node to set a flag in its parent
        // but, on other runs we skip over some nodes (eg. syncHere flag false)
        // however, we still need to compute the required flags for the parent node.
        // these flags record what the node still needs its parent to do in case it's not visited
        unsigned parentSetScanAgain : 1;
        unsigned parentSetCheckMovesAgain : 1;
        unsigned parentSetSyncAgain : 1;
        unsigned parentSetContainsConflicts : 1;

        // Set when we've created a new directory (say, as part of downsync)
        // that has reused this node's FSID.
        unsigned fsidSyncedReused : 1;
        unsigned fsidScannedReused : 1;

        // we can't delete a node immediately in case it's involved in a move
        // that we haven't detected yet.  So we increment this counter
        // Once it's big enough then we are sure and can delete the LocalNode.
        unsigned confirmDeleteCount : 2;

        // If we detected+actioned a move, and this is the old node
        // we can't delete it directly as there may be references on the stack
        unsigned certainlyOrphaned : 1;

        // track whether we have ever scanned this folder
        // folders never scanned can issue a second scan request for this sync
        unsigned neverScanned : 1;
    };

    // Fields which are hardly ever used.
    // We keep the average memory use by only alloating these when used.
    struct RareFields
    {
        shared_ptr<ScanService::ScanRequest> scanRequest;

        struct ScanBlocked
        {
            BackoffTimer scanBlockedTimer;
            LocalPath scanBlockedLocalPath;

            bool folderUnreadable = false;
            bool filesUnreadable = false;

            // There is only one shared_ptr so if the node is gone,
            // we can't look this up by weak_ptr.  So this ptr is not dangling
            LocalNode* localNode = nullptr;

            ScanBlocked(PrnGen &rng, const LocalPath& lp, LocalNode* ln);
        };

        shared_ptr<ScanBlocked> scanBlocked;

        struct BadlyFormedIgnore
        {
            LocalPath localPath;

            // There is only one shared_ptr so if the node is gone,
            // we can't look this up by weak_ptr.  So this ptr is not dangling
            Sync* sync = nullptr;

            BadlyFormedIgnore(const LocalPath& lp, Sync* s) : localPath(lp), sync(s) {}
        };

        shared_ptr<BadlyFormedIgnore> badlyFormedIgnoreFilePath;

        struct MoveInProgress
        {
            bool succeeded = false;
            bool failed = false;
            bool syncCodeProcessedResult = false;

            bool inProgress() { return !succeeded && !failed; }

            handle sourceFsid = UNDEF;
            nodetype_t sourceType = FILENODE;
            FileFingerprint sourceFingerprint;
            NodeHandle movedHandle;
            const void* sourcePtr = nullptr; // for ptr comparison only - could be dangling (actually LocalNode*)
            map<LocalPath, LocalNode*> priorChildrenToRemove;
        };

        struct MovePending
        {
            MovePending(LocalPath&& sourcePath)
              : sourcePath(std::move(sourcePath))
            {
            }

            LocalPath sourcePath;
        };

        struct CreateFolderInProgress
        {
        };

        struct DeleteToDebrisInProgress
        {
            // (actually if it's an inshare, we unlink() as there's no debris
            string pathDeleting;
        };

        struct UnlinkInProgress
        {
            bool failed = false;
            bool succeeded = false;

            handle sourceFsid = UNDEF;
            nodetype_t sourceType = FILENODE;
            FileFingerprint sourceFingerprint;
            LocalNode* sourcePtr = nullptr;
        };

        weak_ptr<MovePending> movePendingFrom;
        shared_ptr<MovePending> movePendingTo;

        shared_ptr<MoveInProgress> moveFromHere;
        shared_ptr<MoveInProgress> moveToHere;
        weak_ptr<CreateFolderInProgress> createFolderHere;
        weak_ptr<DeleteToDebrisInProgress> removeNodeHere;
        weak_ptr<UnlinkInProgress> unlinkHere;

        // Filter rules applicable below this node.
        unique_ptr<FilterChain> filterChain;
    };

    bool hasRare() { return !!rareFields; }
    RareFields& rare();
    void trimRareFields();

    // use this one to skip the hasRare check, if it doesn't exist a reference to a blank one is returned
    const RareFields& rareRO() const;

    // set the syncupTargetedAction for this, and parents
    void setScanAgain(bool doParent, bool doHere, bool doBelow, dstime delayds);
    void setCheckMovesAgain(bool doParent, bool doHere, bool doBelow);
    void setSyncAgain(bool doParent, bool doHere, bool doBelow);

    void setContainsConflicts(bool doParent, bool doHere, bool doBelow);

    void initiateScanBlocked(bool folderBlocked, bool containsFingerprintBlocked);
    bool checkForScanBlocked(FSNode* fsnode);

    // True if this subtree requires scanning.
    bool scanRequired() const;

    // True if this subtree could contain move sources or targets
    bool mightHaveMoves() const;

    // True if this subtree requires syncing.
    bool syncRequired() const;

    // Pass any TREE_ACTION_SUBTREE flags on to child nodes, so we can clear the flag at this level
    void propagateAnySubtreeFlags();

    // Queue a scan request for this node if needed, and if a slot is available (just one per sync)
    // Also receive the results if they are ready
    bool processBackgroundFolderScan(syncRow& row, SyncPath& fullPath);

    void reassignUnstableFsidsOnceOnly(const FSNode* fsnode);

    // current subtree sync state as last notified to OS
    treestate_t mReportedSyncState = TREESTATE_NONE;

    // check the current state (only useful for folders)
    treestate_t checkTreestate(bool notifyChangeToApp);
    void recursiveSetAndReportTreestate(treestate_t ts, bool recurse, bool reportToApp);

    // timer to delay upload start
    dstime nagleds = 0;
    void bumpnagleds();

    // build full local path to this node
    void getlocalpath(LocalPath&) const;
    LocalPath getLocalPath() const;

    // build full remote path to this node (might not exist anymore, of course)
    string getCloudPath(bool guessLeafName) const;

    // For debugging duplicate LocalNodes from older SDK versions
    string debugGetParentList();

    // return child node by name   (TODO: could this be ambiguous, especially with case insensitive filesystems)
    LocalNode* childbyname(LocalPath*);

    LocalNode* findChildWithSyncedNodeHandle(NodeHandle h);

    FSNode getLastSyncedFSDetails() const;
    FSNode getScannedFSDetails() const;

    // Each LocalNode can be either uploading or downloading a file.
    // These functions manage that
    void queueClientUpload(shared_ptr<SyncUpload_inClient> upload, VersioningOption vo, bool queueFirst);
    void queueClientDownload(shared_ptr<SyncDownload_inClient> upload, bool queueFirst);
    void resetTransfer(shared_ptr<SyncTransfer_inClient> p);
    void checkTransferCompleted(syncRow& row, syncRow& parentRow, SyncPath& fullPath);
    void updateTransferLocalname();
    void transferResetUnlessMatched(direction_t, const FileFingerprint& fingerprint);
    shared_ptr<SyncTransfer_inClient> transferSP;

    void updateMoveInvolvement();

    void setSyncedFsid(handle newfsid, fsid_localnode_map& fsidnodes, const LocalPath& fsName, std::unique_ptr<LocalPath> newshortname);
    void setScannedFsid(handle newfsid, fsid_localnode_map& fsidnodes, const LocalPath& fsName, const FileFingerprint& scanfp);

    void setSyncedNodeHandle(NodeHandle h);

    void setnameparent(LocalNode*, const LocalPath& newlocalpath, std::unique_ptr<LocalPath>);
    void moveContentTo(LocalNode*, LocalPath&, bool setScanAgain);

    LocalNode(Sync*);
    void init(nodetype_t, LocalNode*, const LocalPath&, std::unique_ptr<LocalPath>);

    bool serialize(string* d) override;
    static unique_ptr<LocalNode> unserialize(Sync& sync, const string& source, uint32_t& parentID);

    void deleteChildren();

    ~LocalNode();

    //// True if any name conflicts have been detected in this subtree.
    bool conflictsDetected() const;

    // Are we above other?
    bool isAbove(const LocalNode& other) const;

    // Are we below other?
    bool isBelow(const LocalNode& other) const;

    // Create a watch for this node if necessary.
    WatchResult watch(const LocalPath& path, handle fsid);

    void ignoreFilterPresenceChanged(bool present, FSNode* fsNode);

private:
    struct
    {
        // The node's exclusion state.
        ExclusionState mExclusionState;

        // Whether we're an ignore file.
        bool mIsIgnoreFile : 1;

        // Whether we need to reload this node's ignore file.
        bool mWaitingForIgnoreFileLoad : 1;
    };

    // Returns a reference to this node's filter chain.
    FilterChain& filterChain();

    // Query whether a file is excluded by a name filter.
    bool isExcluded(RemotePathPair namePath, nodetype_t type, bool inherited) const;

    // Query whether a file is excluded by a size filter.
    bool isExcluded(const RemotePathPair& namePath, m_off_t size) const;

    // Signal that LocalNodes in this subtree must recompute their exclusion state.
    void setRecomputeExclusionState(bool includingThisOne);

public:
    // Clears the filters defined by this node.
    void clearFilters();

    // Returns a reference to this node's filter chain.
    const FilterChain& filterChainRO() const;

    // Load filters from the ignore file identified by path.
    bool loadFiltersIfChanged(const FileFingerprint& fingerprint, const LocalPath& path);

    // Signal whether this node needs to load its ignore file.
    //void setWaitingForIgnoreFileLoad(bool waiting);

    // Query whether this node needs to load its ignore file.
    bool waitingForIgnoreFileLoad() const;

    // Query whether a file is excluded by this node or one of its parents.
    template<typename PathType>
    typename std::enable_if<IsPath<PathType>::value, ExclusionState>::type
    exclusionState(const PathType& path, nodetype_t type, m_off_t size = -1) const;

    // Specialization of above intended for cloud name queries.
    ExclusionState exclusionState(const string& name, nodetype_t type, m_off_t size = -1) const;

    // Query this node's exclusion state.
    ExclusionState exclusionState() const;

    // Query whether this node represents an ignore file.
    bool isIgnoreFile() const;

    // Recompute this node's exclusion state.
    bool recomputeExclusionState();

private:
    unique_ptr<RareFields> rareFields;

#ifdef USE_INOTIFY
    class WatchHandle
    {
    public:
        WatchHandle();

        ~WatchHandle();

        MEGA_DISABLE_COPY_MOVE(WatchHandle);

        auto operator=(WatchMap::iterator entry) -> WatchHandle&;
        auto operator=(std::nullptr_t) -> WatchHandle&;

        bool operator==(handle fsid) const;

        void invalidate();

    private:
        WatchMap::iterator mEntry;

        static WatchMap mSentinel;
    }; // WatchHandle

public:
    WatchHandle mWatchHandle;
#endif // USE_INOTIFY
};

#endif

} // namespace



#endif
