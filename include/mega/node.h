
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

namespace mega {

struct LocalPathPtrCmp
{
    bool operator()(const LocalPath* a, const LocalPath* b) const
    {
        return *a < *b;
    }
};

typedef map<const LocalPath*, LocalNode*, LocalPathPtrCmp> localnode_map;
typedef map<const string*, Node*, StringCmp> remotenode_map;

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

// new node for putnodes()
struct MEGA_API NewNode : public NodeCore
{
    static const int OLDUPLOADTOKENLEN = 27;

    string nodekey;

    newnodesource_t source = NEW_NODE;

    NodeHandle ovhandle;
    UploadHandle uploadhandle;
    UploadToken uploadtoken;

    handle syncid = UNDEF;
#ifdef ENABLE_SYNC
    crossref_ptr<LocalNode, NewNode> localnode; // non-owning
#endif
    std::unique_ptr<string> fileattributes;

    // versioning used for this new node, forced at server's side regardless the account's value
    VersioningOption mVersioningOption = NoVersioning;
    bool added = false;           // set true when the actionpacket arrives
    handle mAddedHandle = UNDEF;  // updated as actionpacket arrives
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

struct NodesFingerprintMap
{
    bool mAllNodesLoaded = false;
    nodePtr_map mNodes;
};

typedef std::map<FileFingerprint, NodesFingerprintMap, FileFingerprintCmp> FingerprintMap;
typedef FingerprintMap::iterator FingerprintMapPosition;


class NodeManagerNode
{
public:
    std::unique_ptr<Node> mNode;
    std::map<NodeHandle, Node*> mChildren;
    bool mAllChildrenHandleLoaded = false;
};
typedef std::map<NodeHandle, NodeManagerNode>::iterator NodePosition;

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

    // change parent node association. updateNodeCounters is false when called from NodeManager::unserializeNode
    bool setparent(Node*, bool updateNodeCounters = true);

    // follow the parent links all the way to the top
    const Node* firstancestor() const;

    // If this is a file, and has a file for a parent, it's not the latest version
    const Node* latestFileVersion() const;

    // try to resolve node key string
    bool applykey();

    // set up nodekey in a static SymmCipher
    SymmCipher* nodecipher();

    // decrypt attribute string, set fileattrs and save fingerprint
    void setattr();

    // display name (UTF-8)
    const char* displayname() const;

    // check if the name matches (UTF-8)
    bool hasName(const string&) const;

    // display path from its root in the cloud (UTF-8)
    string displaypath() const;

    // node attributes
    AttrMap attrs;

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
        bool counter : 1;

    } changed;

    void setkey(const byte* = NULL);

    void setkeyfromjson(const char*);

    void setUndecryptedKey(const std::string &undecryptedKey);

    void setfingerprint();

    void faspec(string*);

    NodeCounter getCounter() const;
    void setCounter(const NodeCounter &counter, bool notify = false);

    // parent
    Node* parent = nullptr;

    // own position in NodeManager::mFingerPrints (only valid for file nodes)
    // It's used for speeding up node removing at NodeManager::removeFingerprint
    FingerprintMapPosition mFingerPrintPosition;
    // own position in NodeManager::mNodes. The map can have an element of type NodeManagerNode
    // previously Node exists
    // It's used for speeding up get children when Node parent is known
    NodePosition mNodePosition;

#ifdef ENABLE_SYNC
    // related synced item or NULL
    crossref_ptr<LocalNode, Node> localnode;

    // active sync get
    struct SyncFileGet* syncget = nullptr;

    // state of removal to //bin / SyncDebris
    syncdel_t syncdeleted = SYNCDEL_NONE;

    // location in the todebris node_set
    node_set::iterator todebris_it;

    // location in the tounlink node_set
    // FIXME: merge todebris / tounlink
    node_set::iterator tounlink_it;
#endif

    // source tag.  The tag of the request or transfer that last modified this node (available in MegaApi)
    int tag = 0;

    // check if node is below this node
    bool isbelow(Node*) const;
    bool isbelow(NodeHandle) const;

    // handle of public link for the node
    PublicLink* plink = nullptr;

    void setpubliclink(handle, m_time_t, m_time_t, bool, const string &authKey = {});

    bool serialize(string*) override;

    Node(MegaClient&, NodeHandle, NodeHandle, nodetype_t, m_off_t, handle, const char*, m_time_t);
    ~Node();

    int getShareType() const;

    bool isAncestor(NodeHandle ancestorHandle) const;

#ifdef ENABLE_SYNC
    void detach(const bool recreate = false);
#endif // ENABLE_SYNC

private:
    // full folder/file key, symmetrically or asymmetrically encrypted
    // node crypto keys (raw or cooked -
    // cooked if size() == FOLDERNODEKEYLENGTH or FILEFOLDERNODEKEYLENGTH)
    string nodekeydata;

    // keeps track of counts of files, folder, versions, storage and version's storage
    NodeCounter mCounter;
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
struct MEGA_API LocalNode : public File
{
    class Sync* sync = nullptr;

    // parent linkage
    LocalNode* parent = nullptr;

    // stored to rebuild tree after serialization => this must not be a pointer to parent->dbid
    int32_t parent_dbid = 0;

    // whether this node can be synced to the remote tree
    bool mSyncable = true;

    // whether this node knew its shortname (otherwise it was loaded from an old db)
    bool slocalname_in_db = false;

    // children by name
    localnode_map children;

    // for botched filesystems with legacy secondary ("short") names
    // Filesystem notifications could arrive with long or short names, and we need to recognise which LocalNode corresponds.
    std::unique_ptr<LocalPath> slocalname;   // null means either the entry has no shortname or it's the same as the (normal) longname
    localnode_map schildren;

    // local filesystem node ID (inode...) for rename/move detection
    handle fsid = mega::UNDEF;
    handlelocalnode_map::iterator fsid_it{};

    // related cloud node, if any
    crossref_ptr<Node, LocalNode> node;

    // related pending node creation or NULL
    crossref_ptr<NewNode, LocalNode> newnode;

    // FILENODE or FOLDERNODE
    nodetype_t type = TYPE_UNKNOWN;

    // detection of deleted filesystem records
    int scanseqno = 0;

    // number of iterations since last seen
    int notseen = 0;

    // global sync reference
    handle syncid = mega::UNDEF;

    struct
    {
        // was actively deleted
        bool deleted : 1;

        // has been created remotely
        bool created : 1;

        // an issue has been reported
        bool reported : 1;

        // checked for missing attributes
        bool checked : 1;

        // set after the cloud node is created
        bool needsRescan : 1;
    };

    // current subtree sync state: current and displayed
    treestate_t ts = TREESTATE_NONE;
    treestate_t dts = TREESTATE_NONE;

    // update sync state all the way to the root node
    void treestate(treestate_t = TREESTATE_NONE);

    // check the current state (only useful for folders)
    treestate_t checkstate();

    // timer to delay upload start
    dstime nagleds = 0;
    void bumpnagleds();

    // if delage > 0, own iterator inside MegaClient::localsyncnotseen
    localnode_set::iterator notseen_it{};

    // build full local path to this node
    void getlocalpath(LocalPath&) const;
    LocalPath getLocalPath() const;

    // return child node by name
    LocalNode* childbyname(LocalPath*);

#ifdef USE_INOTIFY
    // node-specific DirNotify tag
    handle dirnotifytag = mega::UNDEF;
#endif

    void prepare(FileSystemAccess&) override;
    void completed(Transfer*, putsource_t source) override;
    void terminated(error e) override;

    void setnode(Node*);

    void setnotseen(int);

    // set fsid - assume that an existing assignment of the same fsid is no longer current and revoke.
    // fsidnodes is a map from fsid to LocalNode, keeping track of all fs ids.
    void setfsid(handle newfsid, handlelocalnode_map& fsidnodes);

    void setnameparent(LocalNode*, const LocalPath* newlocalpath, std::unique_ptr<LocalPath>);

    LocalNode(Sync*);
    void init(nodetype_t, LocalNode*, const LocalPath&, std::unique_ptr<LocalPath>);

    bool serialize(string*) override;
    static LocalNode* unserialize( Sync* sync, const string* sData );

    ~LocalNode();

    void detach(const bool recreate = false);

    void setSubtreeNeedsRescan(bool includeFiles);
};

template <> inline NewNode*& crossref_other_ptr_ref<LocalNode, NewNode>(LocalNode* p) { return p->newnode.ptr; }
template <> inline LocalNode*& crossref_other_ptr_ref<NewNode, LocalNode>(NewNode* p) { return p->localnode.ptr; }
template <> inline Node*& crossref_other_ptr_ref<LocalNode, Node>(LocalNode* p) { return p->node.ptr; }
template <> inline LocalNode*& crossref_other_ptr_ref<Node, LocalNode>(Node* p) { return p->localnode.ptr; }

#endif

} // namespace



#endif
