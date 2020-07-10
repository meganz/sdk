
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
#include "filter.h"
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

    // parent node handle (in a Node context, temporary placeholder until parent is set)
    handle parenthandle = UNDEF;

    // node type
    nodetype_t type = TYPE_UNKNOWN;

    // node attributes
    std::unique_ptr<string> attrstring;
};

// new node for putnodes()
struct MEGA_API NewNode : public NodeCore
{
    static const int OLDUPLOADTOKENLEN = 27;
    static const int UPLOADTOKENLEN = 36;

    string nodekey;

    newnodesource_t source = NEW_NODE;

    handle ovhandle = UNDEF;
    handle uploadhandle = UNDEF;
    byte uploadtoken[UPLOADTOKENLEN]{};

    handle syncid = UNDEF;
#ifdef ENABLE_SYNC
    crossref_ptr<LocalNode, NewNode> localnode; // non-owning
#endif
    std::unique_ptr<string> fileattributes;

    bool added = false;
};

struct MEGA_API PublicLink
{
    handle ph;
    m_time_t cts;
    m_time_t ets;
    bool takendown;

    PublicLink(handle ph, m_time_t cts, m_time_t ets, bool takendown);
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
    Node* firstancestor();

    // copy JSON-delimited string
    static void copystring(string*, const char*);

    // try to resolve node key string
    bool applykey();

    // set up nodekey in a static SymmCipher
    SymmCipher* nodecipher();

    // decrypt attribute string and set fileattrs
    void setattr();

    // display name (UTF-8)
    const char* displayname() const;

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

#ifdef ENABLE_SYNC
    // related synced item or NULL
    LocalNode* localnode = nullptr;

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

    // source tag
    int tag = 0;

    // check if node is below this node
    bool isbelow(Node*) const;

    // handle of public link for the node
    PublicLink* plink = nullptr;

    void setpubliclink(handle, m_time_t, m_time_t, bool);

    bool serialize(string*) override;
    static Node* unserialize(MegaClient*, const string*, node_vector*);

    Node(MegaClient*, vector<Node*>*, handle, handle, nodetype_t, m_off_t, handle, const char*, m_time_t);
    ~Node();

#ifdef ENABLE_SYNC
    // Is this node an ignore file?
    bool isIgnoreFile() const;

    // Update our local node's filter state based on our state.
    void updateFilterState();
#endif /* ENABLE_SYNC */

private:
    // full folder/file key, symmetrically or asymmetrically encrypted
    // node crypto keys (raw or cooked -
    // cooked if size() == FOLDERNODEKEYLENGTH or FILEFOLDERNODEKEYLENGTH)
    string nodekeydata;
};

inline const string& Node::nodekey() const
{
    assert(keyApplied() || type == ROOTNODE || type == INCOMINGNODE || type == RUBBISHNODE);
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
    Node* node = nullptr;

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
    void getlocalpath(LocalPath&, bool sdisable = false, const std::string* localseparator = nullptr) const;
    LocalPath getLocalPath(bool sdisable = false) const;
    string localnodedisplaypath(FileSystemAccess& fsa) const;

    // return child node by name
    LocalNode* childbyname(LocalPath*);

#ifdef USE_INOTIFY
    // node-specific DirNotify tag
    handle dirnotifytag = mega::UNDEF;
#endif

    void prepare() override;
    void completed(Transfer*, LocalNode*) override;

    void setnode(Node*);

    void setnotseen(int);

    // set fsid - assume that an existing assignment of the same fsid is no longer current and revoke.
    // fsidnodes is a map from fsid to LocalNode, keeping track of all fs ids.
    void setfsid(handle newfsid, handlelocalnode_map& fsidnodes);

    void setnameparent(LocalNode*, LocalPath* newlocalpath, std::unique_ptr<LocalPath>);

    LocalNode();
    void init(Sync*, nodetype_t, LocalNode*, LocalPath&, std::unique_ptr<LocalPath>);

    bool serialize(string*) override;
    static LocalNode* unserialize( Sync* sync, const string* sData );

    ~LocalNode();

    // Did we or any of our parents fail to load an ignore file?
    bool anyLoadFailed() const;

    // Do we or any of our parents have a load pending?
    bool anyLoadPending() const;

    // Detach the node from its remote.
    void detach(const bool recreate = false);

    // Is name excluded by our or our parent's filter rules?
    bool excluded(const string& name, const nodetype_t type) const;

    // Are we excluded?
    bool excluded() const;

    // Let the node know its ignore file is downloading.
    void ignoreFileDownloading();

    // Our ignore file's path.
    const LocalPath& ignoreFilePath() const;

    // Is name included by our or our parent's filter rules?
    bool included(const string& name, const nodetype_t type) const;

    // Are we included?
    bool included() const;

    // Don't update our parent's filters when we are destroyed.
    void inhibitFilterUpdate();

    // Are we an ignore file?
    bool isIgnoreFile() const;

    // Loads filters from mIgnoreFilePath.
    //
    // Sets:
    //   mLoadPending
    //     If we couldn't perform the load.
    //   mLoadFailed
    //     If we couldn't perform the load due to error.
    //
    // Clears:
    //   mDownloading
    //     If we could successfully load the filter.
    //
    // Calls:
    //   updateFilterState()
    //     If updating is false.
    //
    // Returns true if:
    //   mLoadFailed or mLoadPending changed state.
    bool loadFilters(const bool updating = false);

    // Do we have a ignore file load pending?
    bool loadPending() const;

    // Does any parent need to load their ignore file?
    bool parentLoadPending() const;

    // Perform pending load.
    //
    // Returns:
    //  < 0 If the load failed.
    // <= 0 If the load couldn't be performed.
    //  > 0 If the load was performed.
    int performPendingLoad();

    // Purges all filter state in this subtree.
    void purgeFilterState();

    // Restores the filter state of this subtree.
    // Expected to be called after purgeFilterState().
    void restoreFilterState();

    // Updates the filter state of this subtree.
    //
    // If force is true, we will update all nodes in the subtree regardless
    // of whether their parent's state changed or not.
    void updateFilterState(const bool force = false);

private:
    // Can we perform a pending load?
    bool canPerformLoad() const;

    // Clear our filters.
    bool clearFilters();

    // Set mExcluded.
    // Return true if mExcluded changed.
    bool excluded(const bool excluded);

    // Does this node contain an ignore file?
    bool hasIgnoreFile() const;

    // Are we above other?
    bool isAbove(const LocalNode& other) const;

    // Are we below other?
    bool isBelow(const LocalNode& other) const;

    // Set mIsIgnoreFile.
    // Return true if mIsIgnoreFile changed.
    bool isIgnoreFile(const bool ignoreFile);

    // Is our ignore file still downloading?
    bool isIgnoreFileDownloading() const;

    // Set mLoadFailed.
    // Returns true if mLoadFailed changed.
    bool loadFailed(const bool failed);

    // Sets mLoadPending.
    // Returns true if mLoadPending changed.
    bool loadPending(const bool pending);

    // Sets mParentLoadFailed.
    // Returns true if mParentLoadFailed changed.
    bool parentLoadFailed(const bool failed);
    bool parentLoadFailed() const;

    // Sets mParentLoadPending.
    // Returns true if mParentLoadPending changed.
    bool parentLoadPending(const bool pending);

    // Purges directory filter state.
    void purgeDirectoryFilterState();

    // Purges file filter state.
    void purgeFileFilterState();

    // Purges general filter state.
    void purgeGeneralFilterState();

    // Restores directory filter state.
    void restoreDirectoryFilterState();

    // Restores file filter state.
    void restoreFileFilterState();

    // Restores general filter state.
    void restoreGeneralFilterState();

    // Updates the filter state of this subtree.
    // Specialization called from setnameparent(...).
    void updateFilterState(LocalNode* newParent, LocalPath* newPath);

    // Updates the following based on our parent:
    //   mExcluded
    //   mParentLoadPending
    //   mParentLoadFailed
    // Returns true if any of the above changed state.
    bool updateGeneralFilterState();

    // Our filtering rules.
    FilterChain mFilters;

    // Our position in MegaClient::ignoreFileFailures.
    // Only meaningful if mLoadFailed is true.
    localnode_list::iterator mIgnoreFileFailuresIt;

    // Our ignore file's path.
    LocalPath mIgnoreFilePath;

    // Is our ignore file downloading?
    bool mDownloading;

    // Are we excluded?
    bool mExcluded;

    // Should we update our parent's filters when we are destroyed?
    bool mInhibitFilterUpdate;

    // Are we an ignore file?
    bool mIsIgnoreFile;

    // Were we unable to load our ignore file?
    bool mLoadFailed;

    // Do we have a ignore file load pending?
    bool mLoadPending;

    // Was any parent unable to load their ignore file?
    bool mParentLoadFailed;

    // Does any parent need to load their ignore file?
    bool mParentLoadPending;
};

template <> inline NewNode*& crossref_other_ptr_ref<LocalNode, NewNode>(LocalNode* p) { return p->newnode.ptr; }
template <> inline LocalNode*& crossref_other_ptr_ref<NewNode, LocalNode>(NewNode* p) { return p->localnode.ptr; }

// returns a list of children in "sync order."
// i.e. ignore files, subdirectories, files.
list<pair<const LocalPath*, LocalNode*>> inSyncOrder(const localnode_map& children);
list<pair<const string*, Node*>> inSyncOrder(const remotenode_map& children);
 
#endif

} // namespace



#endif
