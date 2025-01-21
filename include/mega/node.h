
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

#include "attrmap.h"
#include "backofftimer.h"
#include "file.h"
#include "filefingerprint.h"
#include "syncfilter.h"
#include "syncinternals/syncuploadthrottlingfile.h"

#include <bitset>

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

    // inline convenience function to get a typed version that ensures we use the 6 bytes of a node handle, and not 8
    NodeHandle parentHandle() const { return NodeHandle().set6byte(parenthandle); }

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

// new node for putnodes()
struct MEGA_API NewNode : public NodeCore
{
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

    bool hasZeroKey() const;
};

struct MEGA_API PublicLink
{
    handle ph;
    m_time_t cts;
    m_time_t ets;
    bool takendown;
    string mAuthKey;

    PublicLink(handle ph, m_time_t cts, m_time_t ets, bool takendown, const char *authKey = nullptr);
    PublicLink(const PublicLink& plink) = default;

    bool isExpired();
};

struct NodeCounter
{
    m_off_t storage = 0;
    m_off_t versionStorage = 0;
    size_t files = 0;
    size_t folders = 0;
    size_t versions = 0;
    void operator += (const NodeCounter&);
    void operator -= (const NodeCounter&);
    std::string serialize() const;
    NodeCounter(const std::string& blob);
    NodeCounter() = default;
};

typedef std::multiset<FileFingerprint*, FileFingerprintCmp> fingerprint_set;
typedef fingerprint_set::iterator FingerprintPosition;


class NodeManagerNode
{
public:
    NodeManagerNode(NodeManager& nodeManager, NodeHandle nodeHandle);
    // Instances of this class cannot be copied
    std::unique_ptr<std::map<NodeHandle, NodeManagerNode*>> mChildren;
    bool mAllChildrenHandleLoaded = false;
    void setNode(shared_ptr<Node> node);
    shared_ptr<Node> getNodeInRam(bool updatePositionAtLRU = true);
    NodeHandle getNodeHandle() const;

    std::list<std::shared_ptr<Node> >::const_iterator mLRUPosition;

private:
    NodeHandle mNodeHandle;
    NodeManager& mNodeManager;
    weak_ptr<Node> mNode;
};
typedef std::map<NodeHandle, NodeManagerNode>::iterator NodePosition;

struct CommandChain
{
    // convenience functions, hides the unique_ptr aspect, removes it when empty
    bool empty() const
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

    void forEachCommand(const std::function<void(Command*)>& cmdFunction) const
    {
        if (chain)
        {
            for (auto& cmd : *chain)
            {
                cmdFunction(cmd);
            }
        }
    }

private:
    // most nodes don't have commands in progress so keep representation super small
    std::unique_ptr<std::list<Command*>> chain;
};

// filesystem node
struct MEGA_API Node : public NodeCore, FileFingerprint
{
    // Define what shouldn't be logged
    enum LogCondition : uint32_t
    {
        LOG_CONDITION_NONE = 0, // NONE: all is logged
        LOG_CONDITION_DISABLE_NO_KEY = 1 // NO KEY is not logged
    };

    static const std::string BLANK;
    static const std::string CRYPTO_ERROR;
    static const std::string NO_KEY;

    MegaClient* client = nullptr;

    // supplies the nodekey (which is private to ensure we track changes to it)
    const string& nodekey() const;

    // Also returns the key but does not assert that the key has been applied.  Only use it where we don't need the node to be readable.
    const string& nodekeyUnchecked() const;

    // check if the key is present and is the correct size for this node
    bool keyApplied() const;

    // Check whether the node key is a zero key or was generated by a zero key (so it is a bad key and it will be rejected by the API).
    bool hasZeroKey() const;

    // Static version of the above for related node classes.
    static bool hasZeroKey(const string& nodekeydata);

    // change parent node association. updateNodeCounters is false when called from NodeManager::unserializeNode
    bool setparent(std::shared_ptr<Node> , bool updateNodeCounters = true);

    // follow the parent links all the way to the top
    const Node* firstancestor() const;

    // If this is a file, and has a file for a parent, it's not the latest version
    std::shared_ptr<Node> latestFileVersion() const;

    // Node's depth, counting from the cloud root.
    unsigned depth() const;

    // try to resolve node key string
    bool applykey();

    // Returns false if the share key can't correctly decrypt the key and the
    // attributes of the node. Otherwise, it returns true. There are cases in
    // which it's not possible to check if the key is valid (for example when
    // the node is already decrypted). In those cases, this function returns
    // true, because it is intended to discard outdated share keys that could
    // make nodes undecryptable until the next full reload. That way, nodes
    // can be decrypted when the updated share key is received.
    bool testShareKey(const byte* shareKey);

    // set up nodekey in a static SymmCipher
    SymmCipher* nodecipher();

    // decrypt attribute string, set fileattrs and save fingerprint
    void setattr();

    // display name (UTF-8)
    const char* displayname(LogCondition log = LOG_CONDITION_NONE) const;

    // check if the name matches (UTF-8)
    bool hasName(const string&) const;

    // check if this node has a name.
    bool hasName() const;

    // display path from its root in the cloud (UTF-8)
    string displaypath() const;

    // match mimetype type
    // checkPreview flag is only compatible with MimeType_t::MIME_TYPE_PHOTO
    bool isIncludedForMimetype(MimeType_t mimetype, bool checkPreview = false) const;

    // node attributes
    AttrMap attrs;

    static const vector<string> attributesToCopyIntoPreviousVersions;

    // 'sen' attribute
    bool isMarkedSensitive() const;
    bool isSensitiveInherited() const;

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
    // fingerprint output param is a raw fingerprint (i.e. without App prefixes)
    static void parseattr(byte* bufattr, AttrMap& attrs, m_off_t size, m_time_t& mtime, string& fileName,
                          string& fingerprint, FileFingerprint& ffp);

    // inbound share
    unique_ptr<Share> inshare;

    // outbound shares by user
    unique_ptr<share_map> outshares;

    // outbound pending shares
    unique_ptr<share_map> pendingshares;

    // incoming/outgoing share key
    unique_ptr<SymmCipher> sharekey;

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
        bool counter : 1;
        bool sensitive : 1;

        // this field also only used internally, for reporting new NO_KEY occurrences
        bool modifiedByThisClient : 1;

        bool pwd : 1;
        bool description : 1;
        bool tags : 1;
    } changed;


    void setKey(const string& key);
    void setkey(const byte*);
    void setkeyfromjson(const char*);

    void setfingerprint();

    void faspec(string*);

    NodeCounter getCounter() const;
    void setCounter(const NodeCounter &counter);  // to only be called by mNodeManger::setNodeCounter

    // parent
    shared_ptr<Node> parent;

    // own position in NodeManager::mFingerPrints (only valid for file nodes)
    // It's used for speeding up node removing at NodeManager::removeFingerprint
    FingerprintPosition mFingerPrintPosition;
    // own position in NodeManager::mNodes. The map can have an element of type NodeManagerNode
    // previously Node exists
    // It's used for speeding up get children when Node parent is known
    NodePosition mNodePosition;

    // check if node is below this node
    bool isbelow(const Node*) const;
    bool isbelow(NodeHandle) const;

    // handle of public link for the node
    unique_ptr<PublicLink> plink;

    void setpubliclink(handle, m_time_t, m_time_t, bool, const string &authKey = {});

    bool serialize(string*) const override;
    static std::shared_ptr<Node> unserialize(MegaClient& client, const string*, bool fromOldCache, std::list<std::unique_ptr<NewShare>>& ownNewshares);

    Node(MegaClient&, NodeHandle, NodeHandle, nodetype_t, m_off_t, handle, const char*, m_time_t);
    ~Node();

    int getShareType() const;

    using nodeCondition_t = std::function<bool(const Node& node)>;

    /**
     * @brief Check if any of the ancestors of this node matches the give condition
     *
     * @param condition The condition to check on every ancestor.
     * @return Returns true if any of the ancestors of this node evaluates to true the given
     * condition, false otherwise.
     */
    bool hasAncestorMatching(const nodeCondition_t& condition) const;

    /**
     * @brief Same as hasAncestorMatching but evaluates also the condition on this node.
     */
    bool matchesOrHasAncestorMatching(const nodeCondition_t& condition) const
    {
        return condition(*this) || hasAncestorMatching(condition);
    }

    /**
     * @brief Check if any of the ancestors of this node has the given handle
     *
     * @param ancestorHandle The handle that is expected to match.
     * @return Returns true if any of the ancestors of this node has the given ancestorHandle
     */
    bool isAncestor(const NodeHandle ancestorHandle) const
    {
        return hasAncestorMatching(
            [ancestorHandle](const Node& node)
            {
                return node.nodeHandle() == ancestorHandle;
            });
    }

    /**
     * @brief Returns true if this node has the given nodeHandle or any of its ancestors have it.
     */
    bool hasNHOrHasAncestorWithNH(const NodeHandle nh) const
    {
        return nodeHandle() == nh || isAncestor(nh);
    }

    // true for outshares, pending outshares and folder links (which are shared folders internally)
    bool isShared() const { return  (outshares && !outshares->empty()) || (pendingshares && !pendingshares->empty()); }

    // Returns true if this node has a child with the given name.
    bool hasChildWithName(const string& name) const;


    // values that are used to populate the flags column in the database
    // for efficent searching
    enum
    {
        FLAGS_IS_VERSION = 0,        // This bit is active if node is a version
        // i.e. the parent is a file not a folder
        FLAGS_IS_IN_RUBBISH = 1,     // This bit is active if node is in rubbish bin
        // i.e. the root ansestor is the rubbish bin
        FLAGS_IS_MARKED_SENSTIVE = 2,// This bit is active if node is marked as sensitive
        // that is it and every descendent is to be considered
        // sensitive
        // i.e. the 'sen' attribute is set
        FLAGS_SIZE = 3
    };

    typedef std::bitset<FLAGS_SIZE> Flags;

    // check if any of the flags are set in any of the anesestors
    bool anyExcludeRecursiveFlag(Flags excludeRecursiveFlags) const;

    // should we keep the node
    // requiredFlags are flags that must be set
    // excludeFlags are flags that must not be set
    // excludeRecursiveFlags are flags that must not be set or set in a ansestor
    bool areFlagsValid(Flags requiredFlags, Flags excludeFlags, Flags excludeRecursiveFlags = Flags()) const;

    Flags getDBFlagsBitset() const;
    uint64_t getDBFlags() const;

    static uint64_t getDBFlags(uint64_t oldFlags, bool isInRubbish, bool isVersion, bool isSensitive);

    static bool getExtension(std::string& ext, const std::string& nodeName);
    static bool isPhoto(const std::string& ext);
    static bool isVideo(const std::string& ext);
    static bool isAudio(const std::string& ext);
    static bool isDocument(const std::string& ext);
    static bool isSpreadsheet(const std::string& ext);
    static bool isPdf(const std::string& ext);
    static bool isPresentation(const std::string& ext);
    static bool isArchive(const std::string& ext);
    static bool isProgram(const std::string& ext);
    static bool isMiscellaneous(const std::string& ext);
    static bool isOfMimetype(MimeType_t mimetype, const std::string& ext);
    static MimeType_t getMimetype(const std::string& ext);

    bool isPhotoWithFileAttributes(bool checkPreview) const;

    bool isPasswordNode() const;
    bool isPasswordNodeFolder() const;

private:
    // full folder/file key, symmetrically or asymmetrically encrypted
    // node crypto keys (raw or cooked -
    // cooked if size() == FOLDERNODEKEYLENGTH or FILEFOLDERNODEKEYLENGTH)
    string nodekeydata;

    // keeps track of counts of files, folder, versions, storage and version's storage
    NodeCounter mCounter;

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

inline bool Node::hasZeroKey() const
{
    return keyApplied() && SymmCipher::isZeroKey(reinterpret_cast<const byte*>(nodekeydata.data()), nodekeydata.size());
}

inline bool Node::hasZeroKey(const string& nodekeydata)
{
    return ((nodekeydata.size() == FILENODEKEYLENGTH) || (nodekeydata.size() == FOLDERNODEKEYLENGTH)) &&
            SymmCipher::isZeroKey(reinterpret_cast<const byte*>(nodekeydata.data()), nodekeydata.size());
}

// END MEGA_API Node

class NodeData
{
public:
    NodeData(const char* ptr, size_t size, int component) : mStart(ptr), mEnd(ptr + size), mComp(component) {}

    m_time_t getMtime();
    int getLabel();
    std::string getDescription();
    std::string getTags();
    handle getHandle();

    std::unique_ptr<Node> createNode(MegaClient& client, bool fromOldCache, std::list<std::unique_ptr<NewShare>>& ownNewshares);

    enum
    {
        COMPONENT_ALL = -1,
        COMPONENT_NONE = COMPONENT_ALL, // dummy symbol useful where "all" makes no sense (i.e. when no migration is required)
        COMPONENT_ATTRS,
        COMPONENT_MTIME,
        COMPONENT_LABEL,
        COMPONENT_DESCRIPTION,
        COMPONENT_TAGS,
    };

private:
    bool readComponents();
    bool readFailed() { return (mReadAttempted && !mReadSucceeded) || (!mReadAttempted && !readComponents()); }

    const char* mStart;
    const char* mEnd;
    int mComp;

    m_off_t mSize = 0;
    nodetype_t mType = TYPE_UNKNOWN;
    handle mHandle = 0;
    handle mParentHandle = 0;
    handle mUserHandle = 0;
    m_time_t mCtime = 0;
    string mNodeKey;
    char mIsExported = '\0';
    char mIsEncrypted = '\0';
    string mFileAttributes;
    string mAuthKey;
    std::unique_ptr<byte[]> mShareKey;
    int mShareDirection = INT_MAX; // valid values are -1 (outshares) and 0 (inshare)
    std::list<std::vector<char>> mShares;
    AttrMap mAttrs;
    string mAttrString; // encrypted attrs
    handle mPubLinkHandle = 0;
    m_time_t mPubLinkEts = 0;
    m_time_t mPubLinkCts = 0;
    bool mPubLinkTakenDown = false;

    bool mReadAttempted = false;
    bool mReadSucceeded = false;
};

#ifdef ENABLE_SYNC

enum TreeState
{
    TREE_RESOLVED = 0,
    TREE_DESCENDANT_FLAGGED = 1,
    TREE_ACTION_HERE = 2,           // And also check if any children have flags set (ie, implicitly TREE_DESCENDANT_FLAGGED)
    TREE_ACTION_SUBTREE = 3         // overrides any children so the whole subtree is processed
};

inline TreeState updateTreestateFromChild(TreeState oldFlag, TreeState childFlag)
{
    return oldFlag == TREE_RESOLVED && childFlag != TREE_RESOLVED ? TREE_DESCENDANT_FLAGGED : oldFlag;
}

inline TreeState propagateSubtreeFlag(TreeState nodeFlag, TreeState childFlag)
{
    return nodeFlag == TREE_ACTION_SUBTREE ? TREE_ACTION_SUBTREE : TreeState(childFlag);
}

struct SyncRow;
struct SyncPath;

struct MEGA_API LocalNode;

struct MEGA_API LocalNodeCore
  : public Cacheable
{
    // deserialize attributes from binary storage.
    bool read(const string& source, uint32_t& parentID);

    // serialize attributes to binary for storage.
    bool write(string& destination, uint32_t parentID) const;

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
    void clearRegeneratableFolderScan(SyncPath& fullPath, vector<SyncRow>& childRows);

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

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4201) // nameless struct
#endif
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

        // if we write a file with this name, and then checking the filename given back, it's different
        // that makes it impossible to sync properly.  The user must be informed.
        // eg. Synology SMB network drive from windows, and filenames with trailing spaces
        unsigned localFSCannotStoreThisName : 1;
    };
#ifdef _MSC_VER
#pragma warning(pop)
#endif

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
            Sync* sync = nullptr;

            ScanBlocked(PrnGen &rng, const LocalPath& lp, LocalNode* ln, Sync* s);
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

            fsfp_ptr_t sourceFsfp;
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
            NodeHandle succeededHandle;
            handle originalFsid = UNDEF;
            bool failed = false;


            CreateFolderInProgress(handle fsid) : originalFsid(fsid) {}
        };

        struct DeleteToDebrisInProgress
        {
            // (actually if it's an inshare, we unlink() as there's no debris
            string pathDeleting;
            bool failed = false;
            bool succeeded = false;
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
        shared_ptr<CreateFolderInProgress> createFolderHere;
        shared_ptr<DeleteToDebrisInProgress> removeNodeHere;
        weak_ptr<UnlinkInProgress> unlinkHere;

        // Filter rules applicable below this node.
        unique_ptr<FilterChain> filterChain;

        // If we can tell what the filesystem renamed a downloaded file to
        LocalPath localFSRenamedToThisName;
    };

    bool hasRare() { return !!rareFields; }
    RareFields& rare();
    void trimRareFields();

    // use this one to skip the hasRare check, if it doesn't exist a reference to a blank one is returned
    const RareFields& rareRO() const;

    /**
     * @brief Marks the node and optionally its relatives for scanning again.
     *
     * This method sets the scanning flag on the current node, its parent, and/or its descendants,
     * indicating that they need to be scanned for filesystem changes during the next
     * synchronization cycle. Optionally, you can specify a delay before the scan occurs.
     *
     * @param doParent If `true`, marks the parent node for scanning.
     * @param doHere If `true`, marks the current node for scanning.
     * @param doBelow If `true`, marks all descendant nodes for scanning.
     * @param delayds The delay in deciseconds before the scan should occur. If zero, the scan is
     * addressed in the next syncLoop iteration.
     */
    void setScanAgain(bool doParent, bool doHere, bool doBelow, dstime delayds);

    /**
     * @brief Marks the node and optionally its relatives to recheck for moved or renamed items.
     *
     * This method sets the move checking flag on the current node, its parent, and/or its
     * descendants, indicating that they need to be rechecked for any moves or renames within the
     * synchronization scope.
     *
     * @param doParent If `true`, marks the parent node for move checking.
     * @param doHere If `true`, marks the current node for move checking.
     * @param doBelow If `true`, marks all descendant nodes for move checking.
     */
    void setCheckMovesAgain(bool doParent, bool doHere, bool doBelow);

    /**
     * @brief Marks the node and optionally its relatives to be resynchronized.
     *
     * This method sets the synchronization flag on the current node, its parent, and/or its
     * descendants, indicating that they need to be synchronized with the remote server during the
     * next syncLoop iteration.
     *
     * @param doParent If `true`, marks the parent node for synchronization.
     * @param doHere If `true`, marks the current node for synchronization.
     * @param doBelow If `true`, marks all descendant nodes for synchronization.
     */
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
    bool processBackgroundFolderScan(SyncRow& row, SyncPath& fullPath);

    void reassignUnstableFsidsOnceOnly(const FSNode* fsnode);

    // current subtree sync state as last notified to OS
    treestate_t mReportedSyncState = TREESTATE_NONE;

    // check the current state
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

    /**
     * @brief Queues an upload task for processing, with throttling support.
     *
     * This method resets the transferSP shared pointer to the new SyncUpload_inClient, checks
     * throttling conditions, and queues the upload for processing. If throttling is required, the
     * upload is added to a global delayed queue owned by Syncs. Otherwise, the upload is sent to
     * the client to be processed immediately.
     *
     * @param upload Const reference to the shared pointer to the upload task being processed.
     * @param vo Versioning option for the upload.
     * @param queueFirst Flag indicating if this upload should be prioritized. This is meant for the
     * queue client, not for the delayed queue. In case the upload is added to the delayed queue,
     * this param will be taken into account when sending it to the client.
     * @param ovHandleIfShortcut Node handle representing a shortcut for the upload.
     * @return True if the upload was queued for immediate processing, false if it was added to the
     * throttling delayed queue.
     */
    bool queueClientUpload(shared_ptr<SyncUpload_inClient> upload,
                           const VersioningOption vo,
                           const bool queueFirst,
                           const NodeHandle ovHandleIfShortcut);
    void queueClientDownload(shared_ptr<SyncDownload_inClient> upload, bool queueFirst);
    void resetTransfer(shared_ptr<SyncTransfer_inClient> p);
    void checkTransferCompleted(SyncRow& row, SyncRow& parentRow, SyncPath& fullPath);
    void updateTransferLocalname();
    bool transferResetUnlessMatched(direction_t, const FileFingerprint& fingerprint);

    shared_ptr<SyncTransfer_inClient> transferSP;

    /**
     * @brief Check if this node or any successors have any pending transfer (transferSP != nullptr)
     *
     * @return true if there are pending transfers, false otherwise
     */
    bool hasPendingTransfers() const;

    void updateMoveInvolvement();

    void setSyncedFsid(handle newfsid, fsid_localnode_map& fsidnodes, const LocalPath& fsName, std::unique_ptr<LocalPath> newshortname);
    void setScannedFsid(handle newfsid, fsid_localnode_map& fsidnodes, const LocalPath& fsName, const FileFingerprint& scanfp);

    void setSyncedNodeHandle(NodeHandle h);

    void setnameparent(LocalNode*, const LocalPath& newlocalpath, std::unique_ptr<LocalPath>);
    void moveContentTo(LocalNode*, LocalPath&, bool setScanAgain);

    LocalNode(Sync*);
    void init(nodetype_t, LocalNode*, const LocalPath&, std::unique_ptr<LocalPath>);

    bool serialize(string*) const override;
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

    void setSubtreeNeedsRefingerprint();

private:
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4201) // nameless struct
#endif
    struct
    {
        // The node's exclusion state.
        ExclusionState mExclusionState;

        // Whether we're an ignore file.
        bool mIsIgnoreFile : 1;

        // Whether we need to reload this node's ignore file.
        bool mWaitingForIgnoreFileLoad : 1;
    };
#ifdef _MSC_VER
#pragma warning(pop)
#endif

    // Query whether a file is excluded by a name filter.
    ExclusionState calcExcluded(RemotePathPair namePath,
                                nodetype_t applicableType,
                                bool inherited) const;

    // Query whether a file is excluded by a size filter.
    ExclusionState calcExcluded(const RemotePathPair& namePath, m_off_t size) const;

public:
    // Signal that LocalNodes in this subtree must recompute their exclusion state.
    void setRecomputeExclusionState(bool includingThisOne, bool scan);

    // Clears the filters defined by this node.
    void clearFilters();

    // Returns a reference to this node's filter chain.
    const FilterChain& filterChainRO() const;

    // Load filters from the ignore file identified by path.
    bool loadFilters(const LocalPath& path);

    // Signal whether this node needs to load its ignore file.
    //void setWaitingForIgnoreFileLoad(bool waiting);

    // Query whether this node needs to load its ignore file.
    bool waitingForIgnoreFileLoad() const;

    // Query whether a file is excluded by this node or one of its parents.
    template<typename PathType>
    typename std::enable_if<IsPath<PathType>::value, ExclusionState>::type
    exclusionState(const PathType& path, nodetype_t type, m_off_t size) const;

    // Specialization of above intended for cloud name queries.
    ExclusionState exclusionState(const string& name,
                                  nodetype_t applicableType,
                                  m_off_t size) const;

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

private:
    /**
     * @brief Member containing the state and operations for UploadThrottlingFile.
     */
    UploadThrottlingFile mUploadThrottling;

public:
    /**
     * @brief Sets the mUploadThrottling flag to bypass throttling.
     *
     * @param maxUploadsBeforeThrottle Maximum number of allowed uploads before the next upload must
     * be throttled.
     *
     * @see UploadThrottlingFile
     */
    void bypassThrottlingNextTime(const unsigned maxUploadsBeforeThrottle)
    {
        mUploadThrottling.bypassThrottlingNextTime(maxUploadsBeforeThrottle);
    }

    /**
     * @brief Increases the upload counter by 1 and returns the updated counter.
     *
     * @see UploadThrottlingFile
     */
    unsigned increaseUploadCounter()
    {
        return mUploadThrottling.increaseUploadCounter();
    }
};

bool isDoNotSyncFileName(const string& name);
#endif  // ENABLE_SYNC

bool isPhotoVideoAudioByName(const string& filenameExtensionLowercaseNoDot);

} // namespace



#endif
