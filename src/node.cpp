/**
 * @file node.cpp
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

#include "mega/node.h"
#include "mega/megaclient.h"
#include "mega/megaapp.h"
#include "mega/share.h"
#include "mega/serialize64.h"
#include "mega/base64.h"
#include "mega/sync.h"
#include "mega/transfer.h"
#include "mega/transferslot.h"
#include "mega/logging.h"

namespace mega {
Node::Node(MegaClient* cclient, node_vector* dp, handle h, handle ph,
           nodetype_t t, m_off_t s, handle u, const char* fa, m_time_t ts)
{
    client = cclient;
    outshares = NULL;
    pendingshares = NULL;
    tag = 0;
    appdata = NULL;

    nodehandle = h;
    parenthandle = ph;

#ifdef ENABLE_SYNC
    localnode = NULL;
    syncget = NULL;

    syncdeleted = SYNCDEL_NONE;
    todebris_it = client->todebris.end();
    tounlink_it = client->tounlink.end();
#endif

    type = t;

    size = s;
    owner = u;

    copystring(&fileattrstring, fa);

    ctime = ts;

    inshare = NULL;
    sharekey = NULL;
    foreignkey = false;

    memset(&changed,-1,sizeof changed);
    changed.removed = false;

    if (client)
    {
        // set parent linkage or queue for delayed parent linkage in case of
        // out-of-order delivery
        setparent(client->nodebyhandle(ph));
    }
}

Node::~Node()
{
    if(!changed.removed)
    {
        delete inshare;
        delete sharekey;
        return;
    }

    // abort pending direct reads
    // TODO: Fix this
    //client->preadabort(pnode_t(this));

    // remove node's fingerprint from hash
    if (type == FILENODE)
    {
        string fpstring;
        serializefingerprint(&fpstring);
        std::pair<multimap<string, int32_t>::iterator, multimap<string, int32_t>::iterator> range = client->fingerprinttodbid.equal_range(fpstring);
        multimap<string, int32_t>::iterator it = range.first;
        for (; it != range.second; ++it) {
            if (it->second == dbid) {
                client->fingerprinttodbid.erase(it);
                break;
            }
        }
    }

#ifdef ENABLE_SYNC
    // remove from todebris node_set
    if (todebris_it != client->todebris.end())
    {
        client->todebris.erase(todebris_it);
    }

    // remove from tounlink node_set
    if (tounlink_it != client->tounlink.end())
    {
        client->tounlink.erase(tounlink_it);
    }
#endif

    if (outshares)
    {
        // delete outshares, including pointers from users for this node
        for (share_map::iterator it = outshares->begin(); it != outshares->end(); it++)
        {
            delete it->second;
        }
        delete outshares;
    }

    if (pendingshares)
    {
        // delete pending shares
        for (share_map::iterator it = pendingshares->begin(); it != pendingshares->end(); it++)
        {
            delete it->second;
        }
        delete pendingshares;
    }

    delete inshare;
    delete sharekey;

#ifdef ENABLE_SYNC
    // sync: remove reference from local filesystem node
    if (localnode)
    {
        localnode->deleted = true;
        localnode->node = NULL;
    }

    // in case this node is currently being transferred for syncing: abort transfer
    delete syncget;
#endif
}

// update node key and decrypt attributes
void Node::setkey(const byte* newkey)
{
    if (newkey)
    {
        nodekey.assign((char*)newkey, (type == FILENODE) ? FILENODEKEYLENGTH + 0 : FOLDERNODEKEYLENGTH + 0);
    }

    setattr();
}

// parse serialized node and return pnode_t object - updates nodes hash and parent
// mismatch vector
pnode_t Node::unserialize(MegaClient* client, string* d, node_vector* dp)
{
    handle h, ph;
    nodetype_t t;
    m_off_t s;
    handle u;
    const byte* k = NULL;
    const char* fa;
    m_time_t ts;
    const byte* skey;
    const char* ptr = d->data();
    const char* end = ptr + d->size();
    unsigned short ll;
    pnode_t n;
    int i;

    if (ptr + sizeof s + 2 * MegaClient::NODEHANDLE + MegaClient::USERHANDLE + 2 * sizeof ts + sizeof ll > end)
    {
        return NULL;
    }

    s = MemAccess::get<m_off_t>(ptr);
    ptr += sizeof s;

    if (s < 0 && s >= -RUBBISHNODE)
    {
        t = (nodetype_t)-s;
    }
    else
    {
        t = FILENODE;
    }

    h = 0;
    memcpy((char*)&h, ptr, MegaClient::NODEHANDLE);
    ptr += MegaClient::NODEHANDLE;

    ph = 0;
    memcpy((char*)&ph, ptr, MegaClient::NODEHANDLE);
    ptr += MegaClient::NODEHANDLE;

    if (!ph)
    {
        ph = UNDEF;
    }

    memcpy((char*)&u, ptr, MegaClient::USERHANDLE);
    ptr += MegaClient::USERHANDLE;

    // FIME: use m_time_t / Serialize64 instead
    ptr += sizeof(time_t);

    ts = (uint32_t)MemAccess::get<time_t>(ptr);
    ptr += sizeof(time_t);

    if ((t == FILENODE) || (t == FOLDERNODE))
    {
        int keylen = ((t == FILENODE) ? FILENODEKEYLENGTH + 0 : FOLDERNODEKEYLENGTH + 0);

        if (ptr + keylen + 8 + sizeof(short) > end)
        {
            return NULL;
        }

        k = (const byte*)ptr;
        ptr += keylen;
    }

    if (t == FILENODE)
    {
        ll = MemAccess::get<unsigned short>(ptr);
        ptr += sizeof ll;

        if ((ptr + ll > end) || ptr[ll])
        {
            return NULL;
        }

        fa = ptr;
        ptr += ll;
    }
    else
    {
        fa = NULL;
    }

    for (i = 8; i--;)
    {
        if (ptr + (unsigned char)*ptr < end)
        {
            ptr += (unsigned char)*ptr + 1;
        }
    }

    short numshares = MemAccess::get<short>(ptr);
    ptr += sizeof(numshares);

    if (numshares)
    {
        if (ptr + SymmCipher::KEYLENGTH > end)
        {
            return 0;
        }

        skey = (const byte*)ptr;
        ptr += SymmCipher::KEYLENGTH;
    }
    else
    {
        skey = NULL;
    }

    //n = pnode_t(new Node(client, dp, h, ph, t, s, u, fa, ts));
    n = make_shared<Node>(client, dp, h, ph, t, s, u, fa, ts);  // allocate object manager and object at once

    if (k)
    {
        n->setkey(k);
    }

    if (numshares)
    {
        // read inshare (numshares = -1) or outshares (numshares = x)
        do
        {
            int direction;
            if(numshares > 0)
                direction = -1;
            else
                direction = 0;

            if(!Share::unserialize(client, direction, h, skey, &ptr, end, n))
                break;

            --numshares;

        } while (numshares > 0);

//        while (Share::unserialize(client,
//                                  (numshares > 0) ? -1 : 0,
//                                  h, skey, &ptr, end, n)
//               && numshares > 0
//               && --numshares);
    }

    ptr = n->attrs.unserialize(ptr);

    n->setfingerprint();

    if (ptr == end)
    {
        return n;
    }
    else
    {
        return NULL;
    }
}

// serialize node - nodes with pending or RSA keys are unsupported
bool Node::serialize(string* d)
{
//    bool encnode = false;
    // do not serialize encrypted nodes
    if (attrstring)
    {
        LOG_warn << "Trying to serialize an encrypted node";

        //Last attempt to decrypt the node
        applykey();
        setattr();

        if (attrstring)
        {
            LOG_err << "Skipping undecryptable node";
            return false;
//            encnode = true;
        }
    }

//    if(!encnode)    // check key lenght only for nodes with valid key
    {
        switch (type)
        {
        case FILENODE:
            if ((int)nodekey.size() != FILENODEKEYLENGTH)
            {
                return false;
            }
            break;

        case FOLDERNODE:
            if ((int)nodekey.size() != FOLDERNODEKEYLENGTH)
            {
                return false;
            }
            break;

        default:
            if (nodekey.size())
            {
                return false;
            }
        }
    }

    unsigned short ll;
    short numshares;
    string t;
    m_off_t s;

    s = type ? -type : size;

    d->append((char*)&s, sizeof s);

    d->append((char*)&nodehandle, MegaClient::NODEHANDLE);

    if (parenthandle != UNDEF)
    {
        d->append((char*)&parenthandle, MegaClient::NODEHANDLE);
    }
    else
    {
        d->append("\0\0\0\0\0", MegaClient::NODEHANDLE);
    }

    d->append((char*)&owner, MegaClient::USERHANDLE);

    // FIXME: use Serialize64
    time_t ts = 0;
    d->append((char*)&ts, sizeof(ts));

    ts = ctime;
    d->append((char*)&ts, sizeof(ts));

    d->append(nodekey);

    if (type == FILENODE)
    {
        ll = (short)fileattrstring.size() + 1;
        d->append((char*)&ll, sizeof ll);
        d->append(fileattrstring.c_str(), ll);
    }

    d->append("\0\0\0\0\0\0\0", 8);

    if (inshare)
    {
        numshares = -1;
    }
    else
    {
        numshares = 0;
        if (outshares)
        {
            numshares += (short)outshares->size();
        }
        if (pendingshares)
        {
            numshares += (short)pendingshares->size();
        }
    }

    d->append((char*)&numshares, sizeof numshares);

    if (numshares)
    {
        d->append((char*)sharekey->key, SymmCipher::KEYLENGTH);

        if (inshare)
        {
            inshare->serialize(d);
        }
        else
        {
            for (share_map::iterator it = outshares->begin(); it != outshares->end(); it++)
            {
                it->second->serialize(d);
            }
            if (pendingshares)
            {
                for (share_map::iterator it = pendingshares->begin(); it != pendingshares->end(); it++)
                {
                    it->second->serialize(d);
                }
            }
        }
    }

    attrs.serialize(d);

    return true;
}

// copy remainder of quoted string (no unescaping, use for base64 data only)
void Node::copystring(string* s, const char* p)
{
    if (p)
    {
        const char* pp;

        if ((pp = strchr(p, '"')))
        {
            s->assign(p, pp - p);
        }
        else
        {
            *s = p;
        }
    }
    else
    {
        s->clear();
    }
}

// decrypt attrstring and check magic number prefix
byte* Node::decryptattr(SymmCipher* key, const char* attrstring, int attrstrlen)
{
    if (attrstrlen)
    {
        int l = attrstrlen * 3 / 4 + 3;
        byte* buf = new byte[l];

        l = Base64::atob(attrstring, buf, l);

        if (!(l & (SymmCipher::BLOCKSIZE - 1)))
        {
            key->cbc_decrypt(buf, l);

            if (!memcmp(buf, "MEGA{\"", 6))
            {
                return buf;
            }
        }

        delete[] buf;
    }

    return NULL;
}

// return temporary SymmCipher for this nodekey
SymmCipher* Node::nodecipher()
{
    if (client->tmpcipher.setkey(&nodekey))
    {
        return &client->tmpcipher;
    }

    return NULL;
}

// decrypt attributes and build attribute hash
void Node::setattr()
{
    byte* buf;
    SymmCipher* cipher;

    if (attrstring && (cipher = nodecipher()) && (buf = decryptattr(cipher, attrstring->c_str(), attrstring->size())))
    {
        JSON json;
        nameid name;
        string* t;

        json.begin((char*)buf + 5);

        while ((name = json.getnameid()) != EOO && json.storeobject((t = &attrs.map[name])))
        {
            JSON::unescape(t);

            if (name == 'n')
            {
                client->fsaccess->normalize(t);
            }
        }

        setfingerprint();

        delete[] buf;

        delete attrstring;
        attrstring = NULL;
    }
}

// if present, configure FileFingerprint from attributes
// otherwise, the file's fingerprint is derived from the file's mtime/size/key
void Node::setfingerprint()
{
    if (type == FILENODE && nodekey.size() >= sizeof crc)
    {
        string fpstring;
        serializefingerprint(&fpstring);
        std::pair<multimap<string, int32_t>::iterator, multimap<string, int32_t>::iterator> range = client->fingerprinttodbid.equal_range(fpstring);
        multimap<string, int32_t>::iterator itfp = range.first;
        for (; itfp != range.second; ++itfp) {
            if (itfp->second == dbid) {
                client->fingerprinttodbid.erase(itfp);
                break;
            }
        }

        attr_map::iterator it = attrs.map.find('c');

        if (it != attrs.map.end())
        {
            if(!unserializefingerprint(&it->second))
            {
                LOG_warn << "Invalid fingerprint";
            }
        }

        // if we lack a valid FileFingerprint for this file, use file's key,
        // size and client timestamp instead
        if (!isvalid)
        {
            memcpy(crc, nodekey.data(), sizeof crc);
            mtime = ctime;
        }

        string newfpstring;
        serializefingerprint(&newfpstring);
        client->fingerprinttodbid.insert(std::pair<string, int32_t>(newfpstring, dbid));
    }
}

// return file/folder name or special status strings
const char* Node::displayname() const
{
    // not yet decrypted
    if (attrstring)
    {
        LOG_debug << "NO_KEY " << type << " " << size << " " << nodehandle;
#ifdef ENABLE_SYNC
        if (localnode)
        {
            LOG_debug << "Local name: " << localnode->name;
        }
#endif
        return "NO_KEY";
    }

    attr_map::const_iterator it;

    it = attrs.map.find('n');

    if (it == attrs.map.end())
    {
        if (type < ROOTNODE || type > RUBBISHNODE)
        {
            LOG_debug << "CRYPTO_ERROR " << type << " " << size << " " << nodehandle;
#ifdef ENABLE_SYNC
            if (localnode)
            {
                LOG_debug << "Local name: " << localnode->name;
            }
#endif
        }
        return "CRYPTO_ERROR";
    }

    if (!it->second.size())
    {
        LOG_debug << "BLANK " << type << " " << size << " " << nodehandle;
#ifdef ENABLE_SYNC
        if (localnode)
        {
            LOG_debug << "Local name: " << localnode->name;
        }
#endif
        return "BLANK";
    }

    return it->second.c_str();
}

// returns position of file attribute or 0 if not present
int Node::hasfileattribute(fatype t) const
{
    char buf[24];

    sprintf(buf, ":%u*", t);
    return fileattrstring.find(buf) + 1;
}

// attempt to apply node key - sets nodekey to a raw key if successful
bool Node::applykey()
{
    int keylength = (type == FILENODE)
                   ? FILENODEKEYLENGTH + 0
                   : FOLDERNODEKEYLENGTH + 0;

    if (type > FOLDERNODE)
    {
        //Root nodes contain an empty attrstring
        delete attrstring;
        attrstring = NULL;
    }

    if (nodekey.size() == keylength || !nodekey.size())
    {
        return false;
    }

    int l = -1, t = 0;
    handle h;
    const char* k = NULL;
    SymmCipher* sc = &client->key;
    handle me = client->loggedin() ? client->me : *client->rootnodes;
    pnode_t n = NULL; // declare 'n' here, so the reference is valid until the end of this method

    while ((t = nodekey.find_first_of(':', t)) != (int)string::npos)
    {
        // compound key: locate suitable subkey (always symmetric)
        h = 0;

        l = Base64::atob(nodekey.c_str() + (nodekey.find_last_of('/', t) + 1), (byte*)&h, sizeof h);
        t++;

        if (l == MegaClient::USERHANDLE)
        {
            // this is a user handle - reject if it's not me
            if (h != me)
            {
                continue;
            }
            // else: node is the outshare root folder and nodekey is encrypted to master key
        }
        else    // l == NODEHANDLE
        {
            if (h == nodehandle)
            {
                if(!sharekey)
                {
                    continue;
                }

                sc = sharekey;
            }
            else
            {
                // look for the share root and check if we have node and the share key
                if (!(n = client->nodebyhandle(h)) || !n->sharekey)
                {
                    continue;   // if not, look for other node with sharekey
                }

                sc = n->sharekey;
            }

            // this key will be rewritten when the node leaves the outbound share
            foreignkey = true;
        }

        k = nodekey.c_str() + t;
        break;
    }

    // no: found => personal key, use directly
    // otherwise, no suitable key available yet - bail (it might arrive soon)
    if (!k)
    {
        if (l < 0)
        {
            k = nodekey.c_str();
        }
        else
        {
            return false;
        }
    }

    byte key[FILENODEKEYLENGTH];
    if (client->decryptkey(k, key, keylength, sc, 0, nodehandle))
    {
        nodekey.assign((const char*)key, keylength);
        setattr();
    }

    return true;
}

// returns whether node was moved
bool Node::setparent(pnode_t p)
{
    if (p && p->nodehandle == parenthandle) // 'p' can be NULL (call from readnodes())
    {
        return false;
    }

    if(p)
    {
        parenthandle = p->nodehandle;
    }
    else
    {
        parenthandle = UNDEF;
    }

#ifdef ENABLE_SYNC
    // if we are moving an entire sync, don't cancel GET transfers
    if (!localnode || localnode->parent)
    {
        // if the new location is not synced, cancel all GET transfers
        while (p)
        {
            if (p->localnode)
            {
                break;
            }

            p = client->nodebyhandle(p->parenthandle);
        }

        if (!p)
        {
            TreeProcDelSyncGet tdsg;
            client->proctree(pnode_t(this), &tdsg);
        }
    }
#endif

    return true;
}

// returns 1 if n is under p, 0 otherwise
bool Node::isbelow(pnode_t p) const
{
    pnode_t n;
    if (nodehandle == p->nodehandle)
    {
        return true;
    }
    n = client->nodebyhandle(parenthandle);

    for (;;)
    {
        if (!n)
        {
            return false;
        }

        if (n->nodehandle == p->nodehandle)
        {
            return true;
        }

        n = client->nodebyhandle(n->parenthandle);
    }
}

NodeCore::NodeCore()
{
    attrstring = NULL;
    nodehandle = UNDEF;
    parenthandle = UNDEF;
    type = TYPE_UNKNOWN;
}

NodeCore::~NodeCore()
{
    delete attrstring;
}

#ifdef ENABLE_SYNC
// set, change or remove LocalNode's parent and name/localname/slocalname.
// newlocalpath must be a full path and must not point to an empty string.
// no shortname allowed as the last path component.
void LocalNode::setnameparent(LocalNode* newparent, string* newlocalpath)
{
    bool newnode = !localname.size();
    pnode_t todelete = NULL;
    int nc = 0;
    Sync* oldsync = NULL;

    if (parent)
    {
        // remove existing child linkage
        parent->children.erase(&localname);

        if (slocalname.size())
        {
            parent->schildren.erase(&slocalname);
        }
    }

    if (newlocalpath)
    {
        // extract name component from localpath, check for rename unless newnode
        int p;

        for (p = newlocalpath->size(); p -= sync->client->fsaccess->localseparator.size(); )
        {
            if (!memcmp(newlocalpath->data() + p,
                        sync->client->fsaccess->localseparator.data(),
                        sync->client->fsaccess->localseparator.size()))
            {
                p += sync->client->fsaccess->localseparator.size();
                break;
            }
        }

        // has the name changed?
        if (localname.size() != newlocalpath->size() - p
         || memcmp(localname.data(), newlocalpath->data() + p, localname.size()))
        {
            // set new name
            localname.assign(newlocalpath->data() + p, newlocalpath->size() - p);

            name = localname;
            sync->client->fsaccess->local2name(&name);

            if (node)
            {
                if (name != node->attrs.map['n'])
                {
                    string prevname = node->attrs.map['n'];
                    int creqtag = sync->client->reqtag;

                    // set new name
                    node->attrs.map['n'] = name;
                    sync->client->reqtag = sync->tag;
                    sync->client->setattr(node, NULL, prevname.c_str());
                    sync->client->reqtag = creqtag;
                    treestate(TREESTATE_SYNCING);
                }
            }
        }
    }

    if (newparent)
    {
        if (newparent != parent)
        {
            if (parent)
            {
                parent->treestate();
            }

            parent = newparent;

            if (!newnode && node)
            {
                assert(parent->node);
                
                int creqtag = sync->client->reqtag;
                sync->client->reqtag = sync->tag;
                if (sync->client->rename(node, parent->node, SYNCDEL_NONE, node->parenthandle) != API_OK)
//              I assume that 'node->parenthandle' is correctly set to its value or UNDEF
//                if (sync->client->rename(node, parent->node, SYNCDEL_NONE, node->parent ? node->parent->nodehandle : UNDEF) != API_OK)
                {
                    LOG_debug << "Rename not permitted. Using node copy/delete";

                    // save for deletion
                    todelete = node;
                }
                sync->client->reqtag = creqtag;
                treestate(TREESTATE_SYNCING);
            }

            if (sync != parent->sync)
            {
                LOG_debug << "Moving files between different syncs";
                oldsync = sync;
            }

            if (todelete || oldsync)
            {
                // prepare localnodes for a sync change or/and a copy operation
                LocalTreeProcMove tp(parent->sync, todelete != NULL);
                sync->client->proclocaltree(this, &tp);
                nc = tp.nc;
            }
        }

        // (we don't construct a UTF-8 or sname for the root path)
        parent->children[&localname] = this;

        if (sync->client->fsaccess->getsname(newlocalpath, &slocalname))
        {
            parent->schildren[&slocalname] = this;
        }

        parent->treestate();

        if (todelete)
        {
            // complete the copy/delete operation
            dstime nds = NEVER;
            sync->client->syncup(parent, &nds);

            // check if nodes can be immediately created
            bool immediatecreation = sync->client->synccreate.size() == nc;

            sync->client->syncupdate();

            // try to keep nodes in syncdebris if they can't be immediately created
            // to avoid uploads
            sync->client->movetosyncdebris(todelete, immediatecreation || oldsync->inshare);
        }

        if (oldsync)
        {
            // update local cache if there is a sync change
            oldsync->cachenodes();
            sync->cachenodes();
        }
    }
}

// delay uploads by 1.1 s to prevent server flooding while a file is still being written
void LocalNode::bumpnagleds()
{
    nagleds = sync->client->waiter->ds + 11;
}

// initialize fresh LocalNode object - must be called exactly once
void LocalNode::init(Sync* csync, nodetype_t ctype, LocalNode* cparent, string* cfullpath)
{
    sync = csync;
    parent = NULL;
    node = NULL;
    notseen = 0;
    deleted = false;
    created = false;
    reported = false;
    checked = false;
    syncxfer = true;
    newnode = NULL;
    parent_dbid = 0;

    ts = TREESTATE_NONE;
    dts = TREESTATE_NONE;

    type = ctype;
    syncid = sync->client->nextsyncid();

    bumpnagleds();

    if (cparent)
    {
        setnameparent(cparent, cfullpath);
    }
    else
    {
        localname = *cfullpath;
    }

    scanseqno = sync->scanseqno;

    // mark fsid as not valid
    fsid_it = sync->client->fsidnode.end();

    // enable folder notification
    if (type == FOLDERNODE)
    {
        sync->dirnotify->addnotify(this, cfullpath);
    }

    sync->client->syncactivity = true;

    sync->localnodes[type]++;
}

// update treestates back to the root LocalNode, inform app about changes
void LocalNode::treestate(treestate_t newts)
{
    if (newts != TREESTATE_NONE)
    {
        ts = newts;
    }

    if (ts != dts)
    {
        sync->client->app->syncupdate_treestate(this);
    }

    if (parent)
    {
        if (ts == TREESTATE_SYNCING)
        {
            parent->ts = TREESTATE_SYNCING;
        }
        else if (newts != dts && (ts != TREESTATE_SYNCED || parent->ts != TREESTATE_SYNCED))
        {
            parent->ts = TREESTATE_SYNCED;

            for (localnode_map::iterator it = parent->children.begin(); it != parent->children.end(); it++)
            {
                if (it->second->ts == TREESTATE_SYNCING)
                {
                    parent->ts = TREESTATE_SYNCING;
                    break;
                }

                if (it->second->ts == TREESTATE_PENDING && parent->ts == TREESTATE_SYNCED)
                {
                    parent->ts = TREESTATE_PENDING;
                }
            }
        }

        parent->treestate();
    }

    dts = ts;
}

void LocalNode::setnode(pnode_t cnode)
{
    if (node && (node != cnode) && node->localnode)
    {
        node->localnode->treestate();
        node->localnode = NULL;
    }

    deleted = false;

    node = cnode;

    if (node)
    {
        node->localnode = this;
    }
}

void LocalNode::setnotseen(int newnotseen)
{
    if (!newnotseen)
    {
        if (notseen)
        {
            sync->client->localsyncnotseen.erase(notseen_it);
        }

        notseen = 0;
        scanseqno = sync->scanseqno;
    }
    else
    {
        if (!notseen)
        {
            notseen_it = sync->client->localsyncnotseen.insert(this).first;
        }

        notseen = newnotseen;
    }
}

// set fsid - assume that an existing assignment of the same fsid is no longer current and revoke
void LocalNode::setfsid(handle newfsid)
{
    if (fsid_it != sync->client->fsidnode.end())
    {
        if (newfsid == fsid)
        {
            return;
        }

        sync->client->fsidnode.erase(fsid_it);
    }

    fsid = newfsid;

    pair<handlelocalnode_map::iterator, bool> r = sync->client->fsidnode.insert(pair<handle, LocalNode*>(fsid, this));

    fsid_it = r.first;

    if (!r.second)
    {
        // remove previous fsid assignment (the node is likely about to be deleted)
        fsid_it->second->fsid_it = sync->client->fsidnode.end();
        fsid_it->second = this;
    }
}

LocalNode::~LocalNode()
{
    if (sync->state == SYNC_ACTIVE || sync->state == SYNC_INITIALSCAN)
    {
        sync->statecachedel(this);

        if (type == FOLDERNODE)
        {
            sync->client->app->syncupdate_local_folder_deletion(sync, this);
        }
        else
        {
            sync->client->app->syncupdate_local_file_deletion(sync, this);
        }
    }

    setnotseen(0);

    if (newnode)
    {
        newnode->localnode = NULL;
    }

#ifdef USE_INOTIFY
    if (sync->dirnotify)
    {
        // deactivate corresponding notifyq records
        for (int q = DirNotify::RETRY; q >= DirNotify::DIREVENTS; q--)
        {
            for (notify_deque::iterator it = sync->dirnotify->notifyq[q].begin(); it != sync->dirnotify->notifyq[q].end(); it++)
            {
                if ((*it).localnode == this)
                {
                    (*it).localnode = (LocalNode*)~0;
                }
            }
        }
    }
#endif
    
    // remove from fsidnode map, if present
    if (fsid_it != sync->client->fsidnode.end())
    {
        sync->client->fsidnode.erase(fsid_it);
    }

    sync->localnodes[type]--;

    if (type == FILENODE && size > 0)
    {
        sync->localbytes -= size;
    }

    if (type == FOLDERNODE)
    {
        if (sync->dirnotify)
        {
            sync->dirnotify->delnotify(this);
        }
    }

    // remove parent association
    if (parent)
    {
        setnameparent(NULL, NULL);
    }

    for (localnode_map::iterator it = children.begin(); it != children.end(); )
    {
        delete it++->second;
    }

    if (node)
    {
        // move associated node to SyncDebris unless the sync is currently
        // shutting down
        if (sync->state < SYNC_INITIALSCAN)
        {
            node->localnode = NULL;
        }
        else
        {
            sync->client->movetosyncdebris(node, sync->inshare);
        }
    }
}

void LocalNode::getlocalpath(string* path, bool sdisable) const
{
    const LocalNode* l = this;

    path->erase();

    while (l)
    {
        // use short name, if available (less likely to overflow MAXPATH,
        // perhaps faster?) and sdisable not set
        if (!sdisable && l->slocalname.size())
        {
            path->insert(0, l->slocalname);
        }
        else
        {
            path->insert(0, l->localname);
        }

        if ((l = l->parent))
        {
            path->insert(0, sync->client->fsaccess->localseparator);
        }

        if (sdisable)
        {
            sdisable = false;
        }
    }
}

void LocalNode::getlocalsubpath(string* path) const
{
    const LocalNode* l = this;

    path->erase();

    for (;;)
    {
        path->insert(0, l->localname);

        if (!(l = l->parent) || !l->parent)
        {
            break;
        }

        path->insert(0, sync->client->fsaccess->localseparator);
    }
}

// locate child by localname or slocalname
LocalNode* LocalNode::childbyname(string* localname)
{
    localnode_map::iterator it;

    if ((it = children.find(localname)) == children.end() && (it = schildren.find(localname)) == schildren.end())
    {
        return NULL;
    }

    return it->second;
}

void LocalNode::prepare()
{
    getlocalpath(&transfer->localfilename, true);

    // is this transfer in progress? update file's filename.
    if (transfer->slot && transfer->slot->fa->localname.size())
    {
        transfer->slot->fa->updatelocalname(&transfer->localfilename);
    }

    treestate(TREESTATE_SYNCING);
}

// complete a sync upload: complete to //bin if a newer node exists (which
// would have been caused by a race condition)
void LocalNode::completed(Transfer* t, LocalNode*)
{
    // complete to rubbish for later retrieval if the parent node does not
    // exist or is newer
    if (!parent || !parent->node || (node && mtime < node->mtime))
    {
        h = t->client->rootnodes[RUBBISHNODE - ROOTNODE];
    }
    else
    {
        // otherwise, overwrite node if it already exists and complete in its
        // place
        if (node)
        {
            sync->client->movetosyncdebris(node, sync->inshare);
            sync->client->execsyncdeletions();
        }

        h = parent->node->nodehandle;
    }

    File::completed(t, this);
}

// serialize/unserialize the following LocalNode properties:
// - type/size
// - fsid
// - parent LocalNode's dbid
// - corresponding pnode_t handle
// - local name
// - fingerprint crc/mtime (filenodes only)
bool LocalNode::serialize(string* d)
{
    m_off_t s = type ? -type : size;

    d->append((const char*)&s, sizeof s);

    d->append((const char*)&fsid, sizeof fsid);

    uint32_t id = parent ? parent->dbid : 0;

    d->append((const char*)&id, sizeof id);

    handle h = node ? node->nodehandle : UNDEF;

    d->append((const char*)&h, MegaClient::NODEHANDLE);

    unsigned short ll = localname.size();

    d->append((char*)&ll, sizeof ll);
    d->append(localname.data(), ll);

    if (type == FILENODE)
    {
        d->append((const char*)crc, sizeof crc);

        byte buf[sizeof mtime+1];

        d->append((const char*)buf, Serialize64::serialize(buf, mtime));
    }

    return true;
}

LocalNode* LocalNode::unserialize(Sync* sync, string* d)
{
    if (d->size() < sizeof(m_off_t)         // type/size combo
                  + sizeof(handle)          // fsid
                  + sizeof(uint32_t)        // parent dbid
                  + MegaClient::NODEHANDLE  // handle
                  + sizeof(short))          // localname length
    {
        LOG_err << "LocalNode unserialization failed - short data";
        return NULL;
    }

    const char* ptr = d->data();
    const char* end = ptr + d->size();

    nodetype_t type;
    m_off_t size = MemAccess::get<m_off_t>(ptr);
    ptr += sizeof(m_off_t);

    if (size < 0 && size >= -FOLDERNODE)
    {
        // will any compiler optimize this to a const assignment?
        type = (nodetype_t)-size;
        size = 0;
    }
    else
    {
        type = FILENODE;
    }

    handle fsid = MemAccess::get<handle>(ptr);
    ptr += sizeof fsid;

    uint32_t parent_dbid = MemAccess::get<uint32_t>(ptr);
    ptr += sizeof parent_dbid;

    handle h = 0;
    memcpy((char*)&h, ptr, MegaClient::NODEHANDLE);
    ptr += MegaClient::NODEHANDLE;

    unsigned short localnamelen = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof localnamelen;

    if (ptr + localnamelen > end)
    {
        LOG_err << "LocalNode unserialization failed - name too long";
        return NULL;
    }

    const char* localname = ptr;
    ptr += localnamelen;
    uint64_t mtime = 0;

    if (type == FILENODE)
    {
        if (ptr + 4 * sizeof(int32_t) > end + 1)
        {
            LOG_err << "LocalNode unserialization failed - short fingerprint";
            return NULL;
        }

        if (!Serialize64::unserialize((byte*)ptr + 4 * sizeof(int32_t), end - ptr - 4 * sizeof(int32_t), &mtime))
        {
            LOG_err << "LocalNode unserialization failed - malformed fingerprint mtime";
            return NULL;
        }
    }

    LocalNode* l = new LocalNode();

    l->type = type;
    l->size = size;

    l->parent_dbid = parent_dbid;

    l->fsid = fsid;

    l->localname.assign(localname, localnamelen);
    l->name.assign(localname, localnamelen);
    sync->client->fsaccess->local2name(&l->name);

    memcpy(l->crc, ptr, sizeof l->crc);
    l->mtime = mtime;
    l->isvalid = 1;

    l->node = sync->client->nodebyhandle(h);
    l->parent = NULL;
    l->sync = sync;

    // FIXME: serialize/unserialize
    l->created = false;
    l->reported = false;

    return l;
}

#endif
} // namespace
