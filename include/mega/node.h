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
struct MEGA_API NodeCore
{
    NodeCore();
    ~NodeCore();

    // node's own handle
    handle nodehandle = mega::UNDEF;

    // parent node handle (in a Node context, temporary placeholder until parent is set)
    handle parenthandle = mega::UNDEF;

    // node type
    nodetype_t type = TYPE_UNKNOWN;

    // full folder/file key, symmetrically or asymmetrically encrypted
    // node crypto keys (raw or cooked -
    // cooked if size() == FOLDERNODEKEYLENGTH or FILEFOLDERNODEKEYLENGTH)
    string nodekey;

    // node attributes
    string* attrstring = nullptr;
};

// new node for putnodes()
struct MEGA_API NewNode : public NodeCore
{
    static const int OLDUPLOADTOKENLEN = 27;
    static const int UPLOADTOKENLEN = 36;

    newnodesource_t source = NEW_NODE;

    handle ovhandle = mega::UNDEF;
    handle uploadhandle = mega::UNDEF;
    byte uploadtoken[UPLOADTOKENLEN]{};

    handle syncid = mega::UNDEF;
    LocalNode* localnode = nullptr;
    string* fileattributes = nullptr;  // owned here, usually NULL

    bool added = false;

    NewNode();
    ~NewNode();
};

struct MEGA_API PublicLink
{
    handle ph = mega::UNDEF;
    m_time_t cts = 0;
    m_time_t ets = 0;
    bool takendown = false;

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

    void setfingerprint();

    void faspec(string*);

    NodeCounter subnodeCounts() const;

    // parent
    Node* parent = nullptr;

    // children
    node_list children;

    // own position in parent's children
    node_list::iterator child_it{};

    // own position in fingerprint set (only valid for file nodes)
    Fingerprints::iterator fingerprint_it{};

#ifdef ENABLE_SYNC
    // related synced item or NULL
    LocalNode* localnode = nullptr;

    // active sync get
    struct SyncFileGet* syncget = nullptr;

    // state of removal to //bin / SyncDebris
    syncdel_t syncdeleted = SYNCDEL_NONE;

    // location in the todebris node_set
    node_set::iterator todebris_it{};

    // location in the tounlink node_set
    // FIXME: merge todebris / tounlink
    node_set::iterator tounlink_it{};
#endif

    // source tag
    int tag = 0;

    // check if node is below this node
    bool isbelow(Node*) const;

    // handle of public link for the node
    PublicLink* plink = nullptr;

    void setpubliclink(handle, m_time_t, m_time_t, bool);

    bool serialize(string*);
    static Node* unserialize(MegaClient*, string*, node_vector*);

    Node(MegaClient*, vector<Node*>*, handle, handle, nodetype_t, m_off_t, handle, const char*, m_time_t);
    ~Node();
};

#ifdef ENABLE_SYNC
struct MEGA_API LocalNode : public File
{
    class Sync* sync = nullptr;

    // parent linkage
    LocalNode* parent = nullptr;

    // stored to rebuild tree after serialization => this must not be a pointer to parent->dbid
    int32_t parent_dbid = 0;

    // children by name
    localnode_map children;

    // for botched filesystems with legacy secondary ("short") names
    string* slocalname = nullptr;
    localnode_map schildren;

    // local filesystem node ID (inode...) for rename/move detection
    handle fsid = mega::UNDEF;
    handlelocalnode_map::iterator fsid_it{};

    // related cloud node, if any
    Node* node = nullptr;

    // related pending node creation or NULL
    NewNode* newnode = nullptr;

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
    void getlocalpath(string*, bool sdisable = false, const std::string* localseparator = nullptr) const;
    void getlocalsubpath(string*) const;
    string localnodedisplaypath(FileSystemAccess& fsa) const;

    // return child node by name
    LocalNode* childbyname(string*);

#ifdef USE_INOTIFY
    // node-specific DirNotify tag
    handle dirnotifytag = mega::UNDEF;
#endif

    void prepare();
    void completed(Transfer*, LocalNode*);

    void setnode(Node*);

    void setnotseen(int);

    void setfsid(handle newfsid, handlelocalnode_map& fsidnodes);

    void setnameparent(LocalNode*, string*);

    LocalNode();
    void init(Sync*, nodetype_t, LocalNode*, string*);

    virtual bool serialize(string*);
    static LocalNode* unserialize( Sync* sync, string* sData );

    ~LocalNode();
};
#endif
} // namespace

#endif
