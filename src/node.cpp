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

    parent = NULL;

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

    plink = NULL;

    memset(&changed, 0, sizeof changed);

    Node* p;

    client->nodes[h] = this;

    // folder link access: first returned record defines root node and
    // identity
    if (ISUNDEF(*client->rootnodes))
    {
        *client->rootnodes = h;
    }

    if (t >= ROOTNODE && t <= RUBBISHNODE)
    {
        client->rootnodes[t - ROOTNODE] = h;
    }

    // set parent linkage or queue for delayed parent linkage in case of
    // out-of-order delivery
    if ((p = client->nodebyhandle(ph)))
    {
        setparent(p);
    }
    else
    {
        dp->push_back(this);
    }

    client->mFingerprints.newnode(this);
}

Node::~Node()
{
    if (keyApplied())
    {
        client->mAppliedKeyNodeCount--;
        assert(client->mAppliedKeyNodeCount >= 0);
    }

    // abort pending direct reads
    client->preadabort(this);

    // remove node's fingerprint from hash
    if (!client->mOptimizePurgeNodes)
    {
        client->mFingerprints.remove(this);
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


    if (!client->mOptimizePurgeNodes)
    {
        // remove from parent's children
        if (parent)
        {
            parent->children.erase(child_it);
        }

        Node* fa = firstancestor();
        handle ancestor = fa->nodehandle;
        if (ancestor == client->rootnodes[0] || ancestor == client->rootnodes[1] || ancestor == client->rootnodes[2] || fa->inshare)
        {
            client->mNodeCounters[firstancestor()->nodehandle] -= subnodeCounts();
        }

        if (inshare)
        {
            client->mNodeCounters.erase(nodehandle);
        }

        // delete child-parent associations (normally not used, as nodes are
        // deleted bottom-up)
        for (node_list::iterator it = children.begin(); it != children.end(); it++)
        {
            (*it)->parent = NULL;
        }
    }

    if (plink)
    {
        client->mPublicLinks.erase(nodehandle);
    }

    delete plink;
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

#ifdef ENABLE_SYNC

bool Node::isIgnoreFile() const
{
    attr_map::const_iterator name_it;

    return client->ignoreFilesEnabled
           && type == FILENODE
           && ((name_it = attrs.map.find('n')) != attrs.map.end())
           && name_it->second == Sync::ignoreFileName;
}

void Node::updateFilterState()
{
    // We only care about synced nodes that aren't the root.
    if (!(localnode && localnode->parent))
    {
        return;
    }

    // We only care about excluded or pending nodes.
    if (!(localnode->excluded() || localnode->anyLoadPending()))
    {
        return;
    }

    bool mustDetach = false;

    // Has the node been moved?
    mustDetach |= changed.parent;

    // Has the node been removed?
    mustDetach |= changed.removed;

    // Has the node's name changed?
    mustDetach |=
      changed.attrs && localnode->name != displayname();

    // Detach the node if necessary.
    if (mustDetach)
    {
        localnode->detach(true);
    }
}

#endif /* ENABLE_SYNC */

void Node::setkeyfromjson(const char* k)
{
    if (keyApplied()) --client->mAppliedKeyNodeCount;
    Node::copystring(&nodekeydata, k);
    if (keyApplied()) ++client->mAppliedKeyNodeCount;
    assert(client->mAppliedKeyNodeCount >= 0);
}

// update node key and decrypt attributes
void Node::setkey(const byte* newkey)
{
    if (newkey)
    {
        if (keyApplied()) --client->mAppliedKeyNodeCount;
        nodekeydata.assign(reinterpret_cast<const char*>(newkey), (type == FILENODE) ? FILENODEKEYLENGTH : FOLDERNODEKEYLENGTH);
        if (keyApplied()) ++client->mAppliedKeyNodeCount;
        assert(client->mAppliedKeyNodeCount >= 0);
    }

    setattr();
}

// parse serialized node and return Node object - updates nodes hash and parent
// mismatch vector
Node* Node::unserialize(MegaClient* client, const string* d, node_vector* dp)
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
    Node* n;
    int i;
    char isExported = '\0';
    char hasLinkCreationTs = '\0';

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

    u = 0;
    memcpy((char*)&u, ptr, MegaClient::USERHANDLE);
    ptr += MegaClient::USERHANDLE;

    // FIME: use m_time_t / Serialize64 instead
    ptr += sizeof(time_t);

    ts = (uint32_t)MemAccess::get<time_t>(ptr);
    ptr += sizeof(time_t);

    if ((t == FILENODE) || (t == FOLDERNODE))
    {
        int keylen = ((t == FILENODE) ? FILENODEKEYLENGTH : FOLDERNODEKEYLENGTH);

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

        if (ptr + ll > end)
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

    if (ptr + sizeof isExported + sizeof hasLinkCreationTs > end)
    {
        return NULL;
    }

    isExported = MemAccess::get<char>(ptr);
    ptr += sizeof(isExported);

    hasLinkCreationTs = MemAccess::get<char>(ptr);
    ptr += sizeof(hasLinkCreationTs);

    for (i = 6; i--;)
    {
        if (ptr + (unsigned char)*ptr < end)
        {
            ptr += (unsigned char)*ptr + 1;
        }
    }

    if (ptr + sizeof(short) > end)
    {
        return NULL;
    }

    short numshares = MemAccess::get<short>(ptr);
    ptr += sizeof(numshares);

    if (numshares)
    {
        if (ptr + SymmCipher::KEYLENGTH > end)
        {
            return NULL;
        }

        skey = (const byte*)ptr;
        ptr += SymmCipher::KEYLENGTH;
    }
    else
    {
        skey = NULL;
    }

    n = new Node(client, dp, h, ph, t, s, u, fa, ts);

    if (k)
    {
        n->setkey(k);
    }

    // read inshare, outshares, or pending shares
    while (numshares)   // inshares: -1, outshare/s: num_shares
    {
        int direction = (numshares > 0) ? -1 : 0;
        NewShare *newShare = Share::unserialize(direction, h, skey, &ptr, end);
        if (!newShare)
        {
            LOG_err << "Failed to unserialize Share";
            break;
        }

        client->newshares.push_back(newShare);
        if (numshares > 0)  // outshare/s
        {
            numshares--;
        }
        else    // inshare
        {
            break;
        }
    }

    ptr = n->attrs.unserialize(ptr, end);
    if (!ptr)
    {
        delete n;
        return NULL;
    }

    // It's needed to re-normalize node names because
    // the updated version of utf8proc doesn't provide
    // exactly the same output as the previous one that
    // we were using
    attr_map::iterator it = n->attrs.map.find('n');
    if (it != n->attrs.map.end())
    {
        client->fsaccess->normalize(&(it->second));
    }

    PublicLink *plink = NULL;
    if (isExported)
    {
        if (ptr + MegaClient::NODEHANDLE + sizeof(m_time_t) + sizeof(bool) > end)
        {
            delete n;
            return NULL;
        }

        handle ph = 0;
        memcpy((char*)&ph, ptr, MegaClient::NODEHANDLE);
        ptr += MegaClient::NODEHANDLE;
        m_time_t ets = MemAccess::get<m_time_t>(ptr);
        ptr += sizeof(ets);
        bool takendown = MemAccess::get<bool>(ptr);
        ptr += sizeof(takendown);

        m_time_t cts = 0;
        if (hasLinkCreationTs)
        {
            cts = MemAccess::get<m_time_t>(ptr);
            ptr += sizeof(cts);
        }

        plink = new PublicLink(ph, cts, ets, takendown);
        client->mPublicLinks[n->nodehandle] = plink->ph;
    }
    n->plink = plink;

    n->setfingerprint();

    if (ptr == end)
    {
        return n;
    }
    else
    {
        delete n;
        return NULL;
    }
}

// serialize node - nodes with pending or RSA keys are unsupported
bool Node::serialize(string* d)
{
    // do not serialize encrypted nodes
    if (attrstring)
    {
        LOG_warn << "Trying to serialize an encrypted node";

        //Last attempt to decrypt the node
        applykey();
        setattr();

        if (attrstring)
        {
            LOG_warn << "Skipping undecryptable node";
            return false;
        }
    }

    switch (type)
    {
        case FILENODE:
            if ((int)nodekeydata.size() != FILENODEKEYLENGTH)
            {
                return false;
            }
            break;

        case FOLDERNODE:
            if ((int)nodekeydata.size() != FOLDERNODEKEYLENGTH)
            {
                return false;
            }
            break;

        default:
            if (nodekeydata.size())
            {
                return false;
            }
    }

    unsigned short ll;
    short numshares;
    m_off_t s;

    s = type ? -type : size;

    d->append((char*)&s, sizeof s);

    d->append((char*)&nodehandle, MegaClient::NODEHANDLE);

    if (parent)
    {
        d->append((char*)&parent->nodehandle, MegaClient::NODEHANDLE);
    }
    else
    {
        d->append("\0\0\0\0\0", MegaClient::NODEHANDLE);
    }

    d->append((char*)&owner, MegaClient::USERHANDLE);

    // FIXME: use Serialize64
    time_t ts = 0;  // we don't want to break backward compatibiltiy by changing the size (where m_time_t differs)
    d->append((char*)&ts, sizeof(ts));

    ts = (time_t)ctime; 
    d->append((char*)&ts, sizeof(ts));

    d->append(nodekeydata);

    if (type == FILENODE)
    {
        ll = static_cast<unsigned short>(fileattrstring.size() + 1);
        d->append((char*)&ll, sizeof ll);
        d->append(fileattrstring.c_str(), ll);
    }

    char isExported = plink ? 1 : 0;
    d->append((char*)&isExported, 1);

    char hasLinkCreationTs = plink ? 1 : 0;
    d->append((char*)&hasLinkCreationTs, 1);

    d->append("\0\0\0\0\0", 6); // Use these bytes for extensions

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
            if (outshares)
            {
                for (share_map::iterator it = outshares->begin(); it != outshares->end(); it++)
                {
                    it->second->serialize(d);
                }
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

    if (isExported)
    {
        d->append((char*) &plink->ph, MegaClient::NODEHANDLE);
        d->append((char*) &plink->ets, sizeof(plink->ets));
        d->append((char*) &plink->takendown, sizeof(plink->takendown));
        if (hasLinkCreationTs)
        {
            d->append((char*) &plink->cts, sizeof(plink->cts));
        }
    }

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
byte* Node::decryptattr(SymmCipher* key, const char* attrstring, size_t attrstrlen)
{
    if (attrstrlen)
    {
        int l = int(attrstrlen * 3 / 4 + 3);
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

void Node::parseattr(byte *bufattr, AttrMap &attrs, m_off_t size, m_time_t &mtime , string &fileName, string &fingerprint, FileFingerprint &ffp)
{
    JSON json;
    nameid name;
    string *t;

    json.begin((char*)bufattr + 5);
    while ((name = json.getnameid()) != EOO && json.storeobject((t = &attrs.map[name])))
    {
        JSON::unescape(t);
    }

    attr_map::iterator it = attrs.map.find('n');   // filename
    if (it == attrs.map.end())
    {
        fileName = "CRYPTO_ERROR";
    }
    else if (it->second.empty())
    {
        fileName = "BLANK";
    }

    it = attrs.map.find('c');   // checksum
    if (it != attrs.map.end())
    {
        if (ffp.unserializefingerprint(&it->second))
        {
            ffp.size = size;
            mtime = ffp.mtime;

            char bsize[sizeof(size) + 1];
            int l = Serialize64::serialize((byte *)bsize, size);
            char *buf = new char[l * 4 / 3 + 4];
            char ssize = static_cast<char>('A' + Base64::btoa((const byte *)bsize, l, buf));

            string result(1, ssize);
            result.append(buf);
            result.append(it->second);
            delete [] buf;

            fingerprint = result;
        }
    }
}

// return temporary SymmCipher for this nodekey
SymmCipher* Node::nodecipher()
{
    if (client->tmpnodecipher.setkey(&nodekeydata))
    {
        return &client->tmpnodecipher;
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

        attrs.map.clear();
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

        attrstring.reset();
    }
}

// if present, configure FileFingerprint from attributes
// otherwise, the file's fingerprint is derived from the file's mtime/size/key
void Node::setfingerprint()
{
    if (type == FILENODE && nodekeydata.size() >= sizeof crc)
    {
        client->mFingerprints.remove(this);

        attr_map::iterator it = attrs.map.find('c');

        if (it != attrs.map.end())
        {
            if (!unserializefingerprint(&it->second))
            {
                LOG_warn << "Invalid fingerprint";
            }
        }

        // if we lack a valid FileFingerprint for this file, use file's key,
        // size and client timestamp instead
        if (!isvalid)
        {
            memcpy(crc.data(), nodekeydata.data(), sizeof crc);
            mtime = ctime;
        }

        client->mFingerprints.add(this);
    }
}

// return file/folder name or special status strings
const char* Node::displayname() const
{
    // not yet decrypted
    if (attrstring)
    {
        LOG_debug << "NO_KEY " << type << " " << size << " " << Base64Str<MegaClient::NODEHANDLE>(nodehandle);
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

string Node::displaypath() const
{
    // factored from nearly identical functions in megapi_impl and megacli
    string path;
    const Node* n = this;
    for (; n; n = n->parent)
    {
        switch (n->type)
        {
        case FOLDERNODE:
            path.insert(0, n->displayname());

            if (n->inshare)
            {
                path.insert(0, ":");
                if (n->inshare->user)
                {
                    path.insert(0, n->inshare->user->email);
                }
                else
                {
                    path.insert(0, "UNKNOWN");
                }
                return path;
            }
            break;

        case INCOMINGNODE:
            path.insert(0, "//in");
            return path;

        case ROOTNODE:
            return path.empty() ? "/" : path;

        case RUBBISHNODE:
            path.insert(0, "//bin");
            return path;

        case TYPE_UNKNOWN:
        case FILENODE:
            path.insert(0, n->displayname());
        }
        path.insert(0, "/");
    }
    return path;
}

// returns position of file attribute or 0 if not present
int Node::hasfileattribute(fatype t) const
{
    return Node::hasfileattribute(&fileattrstring, t);
}

int Node::hasfileattribute(const string *fileattrstring, fatype t)
{
    char buf[24];

    sprintf(buf, ":%u*", t);
    return static_cast<int>(fileattrstring->find(buf) + 1);
}

// attempt to apply node key - sets nodekey to a raw key if successful
bool Node::applykey()
{
    if (type > FOLDERNODE)
    {
        //Root nodes contain an empty attrstring
        attrstring.reset();
    }

    if (keyApplied() || !nodekeydata.size())
    {
        return false;
    }

    int l = -1;
    size_t t = 0;
    handle h;
    const char* k = NULL;
    SymmCipher* sc = &client->key;
    handle me = client->loggedin() ? client->me : *client->rootnodes;

    while ((t = nodekeydata.find_first_of(':', t)) != string::npos)
    {
        // compound key: locate suitable subkey (always symmetric)
        h = 0;

        l = Base64::atob(nodekeydata.c_str() + (nodekeydata.find_last_of('/', t) + 1), (byte*)&h, sizeof h);
        t++;

        if (l == MegaClient::USERHANDLE)
        {
            // this is a user handle - reject if it's not me
            if (h != me)
            {
                continue;
            }
        }
        else
        {
            // look for share key if not folder access with folder master key
            if (h != me)
            {
                Node* n;

                // this is a share node handle - check if we have node and the
                // share key
                if (!(n = client->nodebyhandle(h)) || !n->sharekey)
                {
                    continue;
                }

                sc = n->sharekey;

                // this key will be rewritten when the node leaves the outbound share
                foreignkey = true;
            }
        }

        k = nodekeydata.c_str() + t;
        break;
    }

    // no: found => personal key, use directly
    // otherwise, no suitable key available yet - bail (it might arrive soon)
    if (!k)
    {
        if (l < 0)
        {
            k = nodekeydata.c_str();
        }
        else
        {
            return false;
        }
    }

    byte key[FILENODEKEYLENGTH];
    unsigned keylength = (type == FILENODE) ? FILENODEKEYLENGTH : FOLDERNODEKEYLENGTH;

    if (client->decryptkey(k, key, keylength, sc, 0, nodehandle))
    {
        client->mAppliedKeyNodeCount++;
        nodekeydata.assign((const char*)key, keylength);
        setattr();
    }

    assert(keyApplied());
    return true;
}

NodeCounter Node::subnodeCounts() const
{
    NodeCounter nc;
    for (Node *child : children)
    {
        nc += child->subnodeCounts();
    }
    if (type == FILENODE)
    {
        nc.files += 1;
        nc.storage += size;
        if (parent && parent->type == FILENODE)
        {
            nc.versions += 1;
            nc.versionStorage += size;
        }
    }
    else if (type == FOLDERNODE)
    {
        nc.folders += 1;
    }
    return nc;
}

// returns whether node was moved
bool Node::setparent(Node* p)
{
    if (p == parent)
    {
        return false;
    }

    NodeCounter nc;
    bool gotnc = false;

    Node *originalancestor = firstancestor();
    handle oah = originalancestor->nodehandle;
    if (oah == client->rootnodes[0] || oah == client->rootnodes[1] || oah == client->rootnodes[2] || originalancestor->inshare)
    {
        nc = subnodeCounts();
        gotnc = true;

        // nodes moving from cloud drive to rubbish for example, or between inshares from the same user.
        client->mNodeCounters[oah] -= nc;
    }

    if (parent)
    {
        parent->children.erase(child_it);
    }

#ifdef ENABLE_SYNC
    Node *oldparent = parent;
#endif

    parent = p;

    if (parent)
    {
        child_it = parent->children.insert(parent->children.end(), this);
    }

    Node* newancestor = firstancestor();
    handle nah = newancestor->nodehandle;
    if (nah == client->rootnodes[0] || nah == client->rootnodes[1] || nah == client->rootnodes[2] || newancestor->inshare)
    {
        if (!gotnc)
        {
            nc = subnodeCounts();
        }

        client->mNodeCounters[nah] += nc;
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

            p = p->parent;
        }

        if (!p || p->type == FILENODE)
        {
            TreeProcDelSyncGet tdsg;
            client->proctree(this, &tdsg);
        }
    }

    if (oldparent && oldparent->localnode)
    {
        oldparent->localnode->treestate(oldparent->localnode->checkstate());
    }
#endif

    return true;
}

Node* Node::firstancestor()
{
    Node* n = this;
    while (n->parent != NULL)
    {
        n = n->parent;
    }
    return n;
}

// returns 1 if n is under p, 0 otherwise
bool Node::isbelow(Node* p) const
{
    const Node* n = this;

    for (;;)
    {
        if (!n)
        {
            return false;
        }

        if (n == p)
        {
            return true;
        }

        n = n->parent;
    }
}

void Node::setpubliclink(handle ph, m_time_t cts, m_time_t ets, bool takendown)
{
    if (!plink) // creation
    {
        assert(client->mPublicLinks.find(nodehandle) == client->mPublicLinks.end());
        plink = new PublicLink(ph, cts, ets, takendown);
    }
    else            // update
    {
        assert(client->mPublicLinks.find(nodehandle) != client->mPublicLinks.end());
        plink->ph = ph;
        plink->cts = cts;
        plink->ets = ets;
        plink->takendown = takendown;
    }
    client->mPublicLinks[nodehandle] = ph;
}

PublicLink::PublicLink(handle ph, m_time_t cts, m_time_t ets, bool takendown)
{
    this->ph = ph;
    this->cts = cts;
    this->ets = ets;
    this->takendown = takendown;
}

PublicLink::PublicLink(PublicLink *plink)
{
    this->ph = plink->ph;
    this->cts = plink->cts;
    this->ets = plink->ets;
    this->takendown = plink->takendown;
}

bool PublicLink::isExpired()
{
    if (!ets)       // permanent link: ets=0
        return false;

    m_time_t t = m_time();
    return ets < t;
}

#ifdef ENABLE_SYNC
// set, change or remove LocalNode's parent and name/localname/slocalname.
// newlocalpath must be a full path and must not point to an empty string.
// no shortname allowed as the last path component.
void LocalNode::setnameparent(LocalNode* newparent, LocalPath* newlocalpath, std::unique_ptr<LocalPath> newshortname)
{
    if (!sync)
    {
        LOG_err << "LocalNode::init() was never called";
        assert(false);
        return;
    }

    bool newnode = localname.empty();
    Node* todelete = NULL;
    int nc = 0;
    Sync* oldsync = NULL;

    // update our filtering state based on what we will become.
    updateFilterState(newparent, newlocalpath);

    if (parent)
    {
        // remove existing child linkage
        parent->children.erase(&localname);

        if (slocalname)
        {
            parent->schildren.erase(slocalname.get());
            slocalname.reset();
        }
    }

    if (newlocalpath)
    {
        // extract name component from localpath, check for rename unless newnode
        size_t p = newlocalpath->getLeafnameByteIndex(*sync->client->fsaccess);

        // has the name changed?
        if (!newlocalpath->backEqual(p, localname))
        {
            // set new name
            localname = newlocalpath->subpathFrom(p);
            name = localname.toName(*sync->client->fsaccess);

            if (node)
            {
                if (name != node->attrs.map['n'])
                {
                    if (node->type == FILENODE)
                    {
                        treestate(TREESTATE_SYNCING);
                    }
                    else
                    {
                        sync->client->app->syncupdate_treestate(this);
                    }

                    string prevname = node->attrs.map['n'];
                    int creqtag = sync->client->reqtag;

                    // set new name
                    node->attrs.map['n'] = name;
                    sync->client->reqtag = sync->tag;
                    sync->client->setattr(node, prevname.c_str());
                    sync->client->reqtag = creqtag;
                }
            }
        }
    }

    if (parent && parent != newparent && !sync->client->destructorRunning)
    {
        treestate(TREESTATE_NONE);
    }

    if (newparent)
    {
        if (newparent != parent)
        {
            parent = newparent;

            if (!newnode && node)
            {
                assert(parent->node);
                
                int creqtag = sync->client->reqtag;
                sync->client->reqtag = sync->tag;
                LOG_debug << "Moving node: " << node->displayname() << " to " << parent->node->displayname();
                if (sync->client->rename(node, parent->node, SYNCDEL_NONE, node->parent ? node->parent->nodehandle : UNDEF) == API_EACCESS
                        && sync != parent->sync)
                {
                    LOG_debug << "Rename not permitted. Using node copy/delete";

                    // save for deletion
                    todelete = node;
                }
                sync->client->reqtag = creqtag;

                if (type == FILENODE)
                {
                    ts = TREESTATE_SYNCING;
                }
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

        if (newshortname && *newshortname != localname)
        {
            slocalname = std::move(newshortname);
            parent->schildren[slocalname.get()] = this;
        }
        else
        {
            slocalname.reset();
        }

        treestate(TREESTATE_NONE);

        if (todelete)
        {
            // complete the copy/delete operation
            dstime nds = NEVER;
            sync->client->syncup(parent, &nds);

            // check if nodes can be immediately created
            bool immediatecreation = (int) sync->client->synccreate.size() == nc;

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

    if (newlocalpath)
    {
        LocalTreeProcUpdateTransfers tput;
        sync->client->proclocaltree(this, &tput);
    }
}

// delay uploads by 1.1 s to prevent server flooding while a file is still being written
void LocalNode::bumpnagleds()
{
    if (!sync)
    {
        LOG_err << "LocalNode::init() was never called";
        assert(false);
        return;
    }

    nagleds = sync->client->waiter->ds + 11;
}

LocalNode::LocalNode()
: deleted{false}
, created{false}
, reported{false}
, checked{false}
{}

// initialize fresh LocalNode object - must be called exactly once
void LocalNode::init(Sync* csync, nodetype_t ctype, LocalNode* cparent, LocalPath& cfullpath, std::unique_ptr<LocalPath> shortname)
{
    sync = csync;
    parent = NULL;
    node = NULL;
    notseen = 0;
    deleted = false;
    created = false;
    reported = false;
    syncxfer = true;
    newnode.reset();
    parent_dbid = 0;
    slocalname = NULL;

    ts = TREESTATE_NONE;
    dts = TREESTATE_NONE;

    type = ctype;
    syncid = sync->client->nextsyncid();

    bumpnagleds();

    mDownloading = false;
    mExcluded = false;
    mInhibitFilterUpdate = false;
    mIsIgnoreFile = false;
    mLoadFailed = false;
    mLoadPending = false;
    mParentLoadPending = false;
    mParentLoadFailed = false;

    if (cparent)
    {
        setnameparent(cparent, &cfullpath, std::move(shortname));
    }
    else
    {
        localname = cfullpath;
        slocalname.reset(shortname && *shortname != localname ? shortname.release() : nullptr);
        name = localname.toPath(*sync->client->fsaccess);

        mIgnoreFilePath = localname;
        mIgnoreFilePath.appendWithSeparator(
          sync->ignoreFileLocalName,
          true,
          sync->client->fsaccess->localseparator);
    }

    scanseqno = sync->scanseqno;

    // mark fsid as not valid
    fsid_it = sync->client->fsidnode.end();

    // enable folder notification
    if (type == FOLDERNODE)
    {
        sync->dirnotify->addnotify(this, cfullpath.editStringDirect());
    }

    sync->client->syncactivity = true;

    sync->client->totalLocalNodes++;
    sync->localnodes[type]++;
}

// update treestates back to the root LocalNode, inform app about changes
void LocalNode::treestate(treestate_t newts)
{
    if (!sync)
    {
        LOG_err << "LocalNode::init() was never called";
        assert(false);
        return;
    }

    if (newts != TREESTATE_NONE)
    {
        ts = newts;
    }

    if (ts != dts)
    {
        sync->client->app->syncupdate_treestate(this);
    }

    if (parent && ((newts == TREESTATE_NONE && ts != TREESTATE_NONE)
                   || (ts != dts && (!(ts == TREESTATE_SYNCED && parent->ts == TREESTATE_SYNCED))
                                 && (!(ts == TREESTATE_SYNCING && parent->ts == TREESTATE_SYNCING))
                                 && (!(ts == TREESTATE_PENDING && (parent->ts == TREESTATE_PENDING
                                                                   || parent->ts == TREESTATE_SYNCING))))))
    {
        treestate_t state = TREESTATE_NONE;
        if (newts != TREESTATE_NONE && ts == TREESTATE_SYNCING)
        {
            state = TREESTATE_SYNCING;
        }
        else
        {
            state = parent->checkstate();
        }

        parent->treestate(state);
    }

    dts = ts;
}

treestate_t LocalNode::checkstate()
{
    if (type == FILENODE)
        return ts;

    treestate_t state = TREESTATE_SYNCED;
    for (localnode_map::iterator it = children.begin(); it != children.end(); it++)
    {
        if (it->second->ts == TREESTATE_SYNCING)
        {
            state = TREESTATE_SYNCING;
            break;
        }

        if (it->second->ts == TREESTATE_PENDING && state == TREESTATE_SYNCED)
        {
            state = TREESTATE_PENDING;
        }
    }
    return state;
}

void LocalNode::setnode(Node* cnode)
{
    if (node && (node != cnode) && node->localnode)
    {
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
    if (!sync)
    {
        LOG_err << "LocalNode::init() was never called";
        assert(false);
        return;
    }

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
void LocalNode::setfsid(handle newfsid, handlelocalnode_map& fsidnodes)
{
    if (!sync)
    {
        LOG_err << "LocalNode::init() was never called";
        assert(false);
        return;
    }

    if (fsid_it != fsidnodes.end())
    {
        if (newfsid == fsid)
        {
            return;
        }

        fsidnodes.erase(fsid_it);
    }

    fsid = newfsid;

    pair<handlelocalnode_map::iterator, bool> r = fsidnodes.insert(std::make_pair(fsid, this));

    fsid_it = r.first;

    if (!r.second)
    {
        // remove previous fsid assignment (the node is likely about to be deleted)
        fsid_it->second->fsid_it = fsidnodes.end();
        fsid_it->second = this;
    }
}

LocalNode::~LocalNode()
{
    if (!sync)
    {
        LOG_err << "LocalNode::init() was never called";
        assert(false);
        return;
    }

    if (sync->state == SYNC_ACTIVE || sync->state == SYNC_INITIALSCAN)
    {
        sync->statecachedel(this);

        if (!(mExcluded || mParentLoadPending))
        {
            if (type == FOLDERNODE)
            {
                sync->client->app->syncupdate_local_folder_deletion(sync, this);
            }
            else
            {
                sync->client->app->syncupdate_local_file_deletion(sync, this);
            }
        }
    }

    setnotseen(0);

    newnode.reset();

    if (sync->dirnotify.get())
    {
        // deactivate corresponding notifyq records
        for (int q = DirNotify::RETRY; q >= DirNotify::EXTRA; q--)
        {
            sync->dirnotify->notifyq[q].replaceLocalNodePointers(this, (LocalNode*)~0);
        }
    }
    
    // remove from fsidnode map, if present
    if (fsid_it != sync->client->fsidnode.end())
    {
        sync->client->fsidnode.erase(fsid_it);
    }

    sync->client->totalLocalNodes--;
    sync->localnodes[type]--;

    if (type == FILENODE && size > 0)
    {
        sync->localbytes -= size;
    }

    if (type == FOLDERNODE)
    {
        if (sync->dirnotify.get())
        {
            sync->dirnotify->delnotify(this);
        }
    }

    // remove parent association
    if (parent)
    {
        setnameparent(NULL, NULL, NULL);
    }

    for (localnode_map::iterator it = children.begin(); it != children.end(); )
    {
        it->second->mInhibitFilterUpdate = true;
        delete it++->second;
    }

    if (node)
    {
        // move associated node to SyncDebris unless the sync is currently
        // shutting down
        if (sync->state < SYNC_INITIALSCAN
            || mExcluded
            || mParentLoadPending)
        {
            node->localnode = NULL;
        }
        else
        {
            sync->client->movetosyncdebris(node, sync->inshare);
        }
    }

    slocalname.reset();
}

LocalPath LocalNode::getLocalPath(bool sdisable) const
{
    LocalPath lp;
    getlocalpath(lp, sdisable);
    return lp;
}

void LocalNode::getlocalpath(LocalPath& path, bool sdisable, const std::string* localseparator) const
{
    if (!sync)
    {
        LOG_err << "LocalNode::init() was never called";
        assert(false);
        return;
    }

    path.erase();

    for (const LocalNode* l = this; l != nullptr; l = l->parent)
    {
        assert(!l->parent || l->parent->sync == sync);

        // use short name, if available (less likely to overflow MAXPATH,
        // perhaps faster?) and sdisable not set.  Use localname from the sync root though, as it has the absolute path.
        if (!sdisable && l->slocalname && l->parent)
        {
            path.prependWithSeparator(*l->slocalname, localseparator ? *localseparator : sync->client->fsaccess->localseparator);
        }
        else
        {
            path.prependWithSeparator(l->localname, localseparator ? *localseparator : sync->client->fsaccess->localseparator);
        }
    }
}

string LocalNode::localnodedisplaypath(FileSystemAccess& fsa) const
{
    LocalPath local;
    getlocalpath(local, true);
    return local.toPath(fsa);
}

// locate child by localname or slocalname
LocalNode* LocalNode::childbyname(LocalPath* localname)
{
    localnode_map::iterator it;

    if (!localname || ((it = children.find(localname)) == children.end() && (it = schildren.find(localname)) == schildren.end()))
    {
        return NULL;
    }

    return it->second;
}

void LocalNode::prepare()
{
    getlocalpath(transfer->localfilename, true);

    // is this transfer in progress? update file's filename.
    if (transfer->slot && transfer->slot->fa && !transfer->slot->fa->nonblocking_localname.empty())
    {
        transfer->slot->fa->updatelocalname(transfer->localfilename);
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
        h = parent->node->nodehandle;
    }

    File::completed(t, this);
}

// serialize/unserialize the following LocalNode properties:
// - type/size
// - fsid
// - parent LocalNode's dbid
// - corresponding Node handle
// - local name
// - fingerprint crc/mtime (filenodes only)
bool LocalNode::serialize(string* d)
{
    CacheableWriter w(*d);
    w.serializei64(type ? -type : size);
    w.serializehandle(fsid);
    w.serializeu32(parent ? parent->dbid : 0);
    w.serializenodehandle(node ? node->nodehandle : UNDEF);
    w.serializestring(*localname.editStringDirect());
    if (type == FILENODE)
    {
        w.serializebinary((byte*)crc.data(), sizeof(crc));
        w.serializecompressed64(mtime);
    }
    w.serializebyte(mSyncable);
    w.serializeexpansionflags(1);  // first flag indicates we are storing slocalname.  Storing it is much, much faster than looking it up on startup.
    w.serializepstr(slocalname ? slocalname->editStringDirect() : nullptr);
    return true;
}

LocalNode* LocalNode::unserialize(Sync* sync, const string* d)
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

    CacheableReader r(*d);

    nodetype_t type;
    m_off_t size;
    
    if (!r.unserializei64(size)) return nullptr;

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

    handle fsid;
    uint32_t parent_dbid;
    handle h = 0;
    string localname, shortname;
    uint64_t mtime = 0;
    int32_t crc[4];
    memset(crc, 0, sizeof crc);
    byte syncable = 1;
    unsigned char expansionflags[8] = { 0 };

    if (!r.unserializehandle(fsid) ||
        !r.unserializeu32(parent_dbid) || 
        !r.unserializenodehandle(h) ||
        !r.unserializestring(localname) ||
        (type == FILENODE && !r.unserializebinary((byte*)crc, sizeof(crc))) ||
        (type == FILENODE && !r.unserializecompressed64(mtime)) ||
        (r.hasdataleft() && !r.unserializebyte(syncable)) ||
        (r.hasdataleft() && !r.unserializeexpansionflags(expansionflags, 1)) ||
        (expansionflags[0] && !r.unserializecstr(shortname, false)))
    {
        LOG_err << "LocalNode unserialization failed at field " << r.fieldnum;
        return nullptr;
    }
    assert(!r.hasdataleft());

    LocalNode* l = new LocalNode();

    l->type = type;
    l->size = size;

    l->parent_dbid = parent_dbid;

    l->fsid = fsid;
    l->fsid_it = sync->client->fsidnode.end();

    l->localname = LocalPath(std::move(localname));
    l->slocalname.reset(shortname.empty() ? nullptr : new LocalPath(std::move(shortname)));
    l->slocalname_in_db = 0 != expansionflags[0];
    l->name = l->localname.toName(*sync->client->fsaccess);

    memcpy(l->crc.data(), crc, sizeof crc);
    l->mtime = mtime;
    l->isvalid = true;

    l->node = sync->client->nodebyhandle(h);
    l->parent = nullptr;
    l->sync = sync;
    l->mSyncable = syncable == 1;

    // FIXME: serialize/unserialize
    l->created = false;
    l->reported = false;
    l->checked = h != UNDEF; // TODO: Is this a bug? h will never be UNDEF

    return l;
}

bool LocalNode::anyLoadFailed() const
{
    return mLoadFailed | mParentLoadFailed;
}

bool LocalNode::anyLoadPending() const
{
    return mLoadPending | mParentLoadPending;
}

void LocalNode::detach(const bool recreate)
{
    // Never detach the root.
    if (parent && node)
    {
        node->localnode = nullptr;
        node->tag = sync->tag;
        node = nullptr;

        created &= !recreate;

        sync->statecacheadd(this);
    }
}

bool LocalNode::excluded(const string& name, const nodetype_t type) const
{
    assert(this->type == FOLDERNODE);

    auto& fsAccess = *sync->client->fsaccess;
    LocalPath path = getLocalPath();

    if (mExcluded)
    {
        LOG_verbose << name
                    << " excluded by excluded parent "
                    << path.toPath(fsAccess);

        return true;
    }

    string_pair namePath(name, name);
    LocalPath localPath;
    auto* node = this;
    auto& separator = fsAccess.localseparator;

    // check for an exclusion.
    for ( ; node; node = node->parent)
    {
        if (node->mFilters.excluded(namePath, type, node != this))
        {
            break;
        }

        // local path.
        localPath.prependWithSeparator(node->localname, separator);

        // normalized path.
        // separator is always '/'.
        namePath.second.insert(0, namePath.second.size() > 0, '/');
        namePath.second.insert(0, node->name);
    }
    
    if (node)
    {
        // excluded by some parent.
        if (node != this)
        {
            // recompute path if necessary for logging purposes.
            path = node->getLocalPath();
        }
    }
    else
    {
        // check static rules if name wasn't excluded by any parent.
        auto& app = *sync->client->app;
        auto fsType = sync->mFilesystemType;

        // compute local name.
        const LocalPath localName =
          LocalPath::fromName(name, fsAccess, fsType);

        // finalize local path.
        localPath.appendWithSeparator(localName, true, separator);

        // don't bother testing for an inclusion if we weren't excluded.
        if (app.sync_syncable(sync, name.c_str(), localPath))
        {
            return false;
        }
        else
        {
            path = LocalPath::fromName("static rule", fsAccess, fsType);
        }
    }

    namePath.second = name;

    // check for an explicit inclusion.
    for (auto *inode = this; inode; inode = inode->parent)
    {
        if (inode->mFilters.included(namePath, type, inode != this))
        {
            if (inode != node)
            {
                // recompute path only if necessary.
                path = inode->getLocalPath();
            }

            LOG_verbose << name
                        << " explicitly included by "
                        << path.toPath(fsAccess);

            return false;
        }

        namePath.second.insert(0, namePath.second.size() > 0, '/');
        namePath.second.insert(0, inode->name);
    }

    LOG_verbose << name
                << " excluded by "
                << path.toPath(fsAccess);

    return true;
}

bool LocalNode::excluded() const
{
    return mExcluded;
}

void LocalNode::ignoreFileDownloading()
{
    assert(type == FOLDERNODE);

    if (mDownloading)
    {
        return;
    }

    mDownloading = true;

    const bool changed =
      loadFailed(false) | loadPending(true);

    if (!changed)
    {
        return;
    }

    for (auto& child_it : children)
    {
        child_it.second->updateFilterState();
    }
}

const LocalPath& LocalNode::ignoreFilePath() const
{
    assert(type == FOLDERNODE);

    return mIgnoreFilePath;
}

bool LocalNode::included(const string& name, const nodetype_t type) const
{
    assert(type == FOLDERNODE);

    return !excluded(name, type);
}

bool LocalNode::included() const
{
    return !mExcluded;
}

void LocalNode::inhibitFilterUpdate()
{
    assert(type == FILENODE);

    mInhibitFilterUpdate = true;
}

bool LocalNode::isIgnoreFile() const
{
    return sync->client->ignoreFilesEnabled
           && mIsIgnoreFile;
}

bool LocalNode::loadFilters(const bool updating)
{
    assert(type == FOLDERNODE);

    if (!canPerformLoad())
    {
        return loadPending(true);
    }

    auto fileAccess =
      sync->client->fsaccess->newfileaccess(false);
    bool failed = true;

    if (fileAccess->fopen(mIgnoreFilePath, true, false))
    {
        FilterChain filters;

        if (filters.load(*fileAccess))
        {
            mFilters = std::move(filters);
            failed = false;
        }
    }
    else
    {
        mFilters.clear();
        failed = false;
    }

    mDownloading = false;

    const bool changed =
      loadFailed(failed) | loadPending(failed);

    if (updating)
    {
        return changed;
    }

    for (auto& child_it : children)
    {
        child_it.second->updateFilterState(true);
    }

    return changed;
}

bool LocalNode::loadPending() const
{
    assert(type == FOLDERNODE);

    return mLoadPending;
}

bool LocalNode::parentLoadPending() const
{
    return mParentLoadPending;
}

int LocalNode::performPendingLoad()
{
    assert(type == FOLDERNODE);
    assert(mLoadPending);

    loadFilters();

    return 1 - mLoadPending - mLoadFailed;
}

void LocalNode::purgeFilterState()
{
    assert(type == FOLDERNODE);
    assert(parent == nullptr);

    localnode_list pending;

    // Purge our filter state.
    purgeGeneralFilterState();
    purgeDirectoryFilterState();

    // Root is never excluded.
    mExcluded = false;

    // Process our immediate children.
    for (auto& child_it : children)
    {
        LocalNode& child = *child_it.second;

        // Purge common filter state.
        child.purgeGeneralFilterState();

        // Child might be excluded by global filters.
        child.mExcluded = excluded(child.name, child.type);

        // Process files directly.
        if (child.type == FILENODE)
        {
            child.purgeFileFilterState();
            continue;
        }

        // Queue directories for traversal.
        pending.emplace_back(&child);
    }

    // Process subdirectories.
    while (pending.size())
    {
        LocalNode& node = *pending.front();

        // Purge our filter state.
        node.purgeDirectoryFilterState();

        // Process our immediate children.
        for(auto& child_it : node.children)
        {
            LocalNode& child = *child_it.second;

            // Purge common filter state.
            child.purgeGeneralFilterState();

            // Child might be excluded by global filters.
            child.mExcluded =
              child.parent->excluded(child.name, child.type);

            // Process children immediately.
            if (child.type == FILENODE)
            {
                child.purgeFileFilterState();
                continue;
            }

            // Queue subdirectories for traversal.
            pending.emplace_back(&child);
        }

        // We're done with this node.
        pending.pop_front();
    }
}

void LocalNode::restoreFilterState()
{
    assert(type == FOLDERNODE);
    assert(parent == nullptr);

    localnode_list pending;

    // Restore our filter state.
    restoreGeneralFilterState();
    restoreDirectoryFilterState();

    // Process our immediate children.
    for (auto& child_it : children)
    {
        LocalNode& child = *child_it.second;

        // Restore common filter state.
        child.restoreGeneralFilterState();

        // Are we excluded?
        child.mExcluded = excluded(child.name, child.type);

        // Process files directly.
        if (child.type == FILENODE)
        {
            child.restoreFileFilterState();
            continue;
        }

        // Queue subdirectories.
        pending.emplace_back(&child);
    }

    // Process subdirectories.
    while (pending.size())
    {
        LocalNode& node = *pending.front();

        // Restore our filter state.
        node.restoreDirectoryFilterState();

        // Process direct children.
        for (auto& child_it : node.children)
        {
            LocalNode& child = *child_it.second;

            // Restore common filter state.
            child.restoreGeneralFilterState();

            // Is the child excluded?
            child.mExcluded =
              child.parent->excluded(child.name, child.type);

            // Process files directly.
            if (child.type == FILENODE)
            {
                child.restoreFileFilterState();
                continue;
            }

            // Queue subdirectories.
            pending.emplace_back(&child);
        }

        // We're done with this directory.
        pending.pop_front();
    }
}

void LocalNode::updateFilterState(const bool force)
{
    localnode_list pending(1, this);

    while (pending.size())
    {
        LocalNode& node = *pending.front();

        pending.pop_front();

        bool changed = node.updateGeneralFilterState();

        if (node.type != FOLDERNODE)
        {
            continue;
        }

        if (node.mLoadPending)
        {
            changed |= node.loadFilters(true);
        }

        if (!(changed || force))
        {
            continue;
        }

        for (auto& child_it : node.children)
        {
            pending.emplace_back(child_it.second);
        }
    }
}

bool LocalNode::canPerformLoad() const
{
    assert(type == FOLDERNODE);

    if (mExcluded || mParentLoadPending)
    {
        return false;
    }

    if (mDownloading)
    {
        return !isIgnoreFileDownloading();
    }

    return true;
}

bool LocalNode::clearFilters()
{
    assert(type == FOLDERNODE);

    bool changed = !mFilters.empty();

    mFilters.clear();

    mDownloading = false;

    changed |= loadFailed(false);
    changed |= loadPending(false);

    return changed;
}

bool LocalNode::excluded(const bool excluded)
{
    const bool changed = mExcluded ^ excluded;

    mExcluded = excluded;

    return changed;
}

bool LocalNode::hasIgnoreFile() const
{
    return children.count(&sync->ignoreFileLocalName);
}

bool LocalNode::isAbove(const LocalNode& other) const
{
    return other.isBelow(*this);
}

bool LocalNode::isBelow(const LocalNode& other) const
{
    for (auto* node = parent; node; node = node->parent)
    {
        if (node == &other)
        {
            return true;
        }
    }

    return false;
}

bool LocalNode::isIgnoreFile(const bool ignoreFile)
{
    assert(type == FILENODE);

    const bool changed = mIsIgnoreFile ^ ignoreFile;

    mIsIgnoreFile = ignoreFile;

    return changed;
}

bool LocalNode::isIgnoreFileDownloading() const
{
    assert(type == FOLDERNODE);

    if (!node)
    {
        return false;
    }

    for (auto& child_it : node->children)
    {
        if (child_it->syncget && child_it->isIgnoreFile())
        {
            return true;
        }
    }

    return false;
}

bool LocalNode::loadFailed(const bool failed)
{
    assert(type == FOLDERNODE);

    if (mLoadFailed == failed)
    {
        return false;
    }

    mLoadFailed = failed;


    // For convenience.
    auto& it = mIgnoreFileFailuresIt;
    auto& list = sync->client->ignoreFileFailures;

    if (failed)
    {
        // Notify the application if it hasn't already been notified.
        if (list.empty())
        {
            sync->client->app->syncupdate_filter_error(sync, this);
        }

        it = list.emplace(list.end(), this);
    }
    else
    {
        list.erase(it);
    }

    return true;
}

bool LocalNode::loadPending(const bool pending)
{
    assert(type == FOLDERNODE);

    const bool changed = mLoadPending ^ pending;

    mLoadPending = pending;

    return changed;
}

bool LocalNode::parentLoadFailed(const bool failed)
{
    const bool changed = mParentLoadFailed ^ failed;

    mParentLoadFailed = failed;

    return changed;
}

bool LocalNode::parentLoadFailed() const
{
    return mParentLoadFailed;
}

bool LocalNode::parentLoadPending(const bool pending)
{
    const bool changed = mParentLoadPending ^ pending;

    mParentLoadPending = pending;

    return changed;
}

void LocalNode::purgeDirectoryFilterState()
{
    assert(type == FOLDERNODE);

    mDownloading = false;

    if (mLoadFailed)
    {
        auto& list = sync->client->ignoreFileFailures;

        list.erase(mIgnoreFileFailuresIt);

        mLoadFailed = false;
    }

    mFilters.clear();
    mLoadPending = false;
}

void LocalNode::purgeFileFilterState()
{
    assert(type == FILENODE);

    mIsIgnoreFile = false;
}

void LocalNode::purgeGeneralFilterState()
{
    mParentLoadFailed = false;
    mParentLoadPending = false;
}

void LocalNode::restoreDirectoryFilterState()
{
    assert(type == FOLDERNODE);

    if (parent)
    {
        mParentLoadPending |= parent->anyLoadPending();
        mParentLoadFailed |= parent->anyLoadFailed();
    }

    if (isIgnoreFileDownloading())
    {
        mDownloading = true;
        mLoadPending = true;
    }
    else if (hasIgnoreFile())
    {
        loadFilters(true);
    }
}

void LocalNode::restoreFileFilterState()
{
    assert(type == FILENODE);

    mIsIgnoreFile = name == Sync::ignoreFileName;
}

void LocalNode::restoreGeneralFilterState()
{
    if (parent)
    {
        mParentLoadPending |= parent->anyLoadPending();
        mParentLoadFailed |= parent->anyLoadFailed();
    }
}

void LocalNode::updateFilterState(LocalNode* newParent, LocalPath* newPath)
{
    if (!newParent)
    {
        if (!isIgnoreFile())
        {
            return;
        }

        parent->clearFilters();

        if (!mInhibitFilterUpdate)
        {
            parent->updateFilterState(true);
        }

        return;
    }

    auto& client = *sync->client;
    auto& fsAccess = *client.fsaccess;

    string oldName;
    LocalPath newName = newPath->lastPart(fsAccess);
    LocalNode* oldParent = parent;
    bool mustDetachRemote = false;
    bool mustRubbishRemote = false;
    bool mustUpdateCache = false;
    bool pathChanged = oldParent != newParent;
    bool wasExcluded = mExcluded;
    bool wasIgnoreFile = isIgnoreFile();

    if (localname != newName)
    {
        oldName = std::move(name);
        name = newName.toName(fsAccess, sync->mFilesystemType);

        if (type == FILENODE)
        {
            mIsIgnoreFile = name == Sync::ignoreFileName;
        }

        pathChanged |= true;
    }

    if (pathChanged)
    {
        mustDetachRemote |= mExcluded || anyLoadPending();

        if (type == FOLDERNODE)
        {
            mIgnoreFilePath = *newPath;
            mIgnoreFilePath.appendWithSeparator(
              sync->ignoreFileLocalName,
              true,
              fsAccess.localseparator);
        }
    }

    parent = newParent;

    if (oldParent && oldParent != newParent)
    {
        oldParent->children.erase(&localname);
        newParent->children.emplace(&localname, this);
    }

    updateFilterState();

    if (isIgnoreFile())
    {
        newParent->loadFilters(true);

        if (wasIgnoreFile)
        {
            oldParent->clearFilters();

            if (oldParent == newParent || newParent->isAbove(*oldParent))
            {
                newParent->updateFilterState(true);
            }
            else if (parent->isBelow(*oldParent))
            {
                oldParent->updateFilterState(true);
            }
            else
            {
                oldParent->updateFilterState(true);
                newParent->updateFilterState(true);
            }
        }
        else
        {
            newParent->updateFilterState(true);
        }
    }
    else if (wasIgnoreFile)
    {
        oldParent->clearFilters();
        oldParent->updateFilterState(true);
    }

    mustRubbishRemote = wasExcluded ^ mExcluded;

    if (oldName.size())
    {
        name = std::move(oldName);
    }

    parent = oldParent;

    if (oldParent && oldParent != newParent)
    {
        newParent->children.erase(&localname);
        oldParent->children.emplace(&localname, this);
    }

    if (node && mustDetachRemote)
    {
        detach();
        mustUpdateCache = true;
    }

    if (node && mustRubbishRemote)
    {
        client.movetosyncdebris(node, sync->inshare);
        mustUpdateCache = true;
    }

    if (mustUpdateCache)
    {
        created = false;
        sync->statecacheadd(this);
    }
}

bool LocalNode::updateGeneralFilterState()
{
    bool changed = false;

    if (parent)
    {
        changed |= excluded(parent->excluded(name, type));
        changed |= parentLoadFailed(parent->anyLoadFailed());
        changed |= parentLoadPending(parent->anyLoadPending());
    }
    else
    {
        changed |= excluded(false);
        changed |= parentLoadFailed(false);
        changed |= parentLoadPending(false);
    }

    return changed;
}

list<pair<const LocalPath*, LocalNode*>> inSyncOrder(const localnode_map& children)
{
    list<pair<const LocalPath*, LocalNode*>> directories;
    list<pair<const LocalPath*, LocalNode*>> files;

    for (auto &child_it : children)
    {
        if (child_it.second->excluded())
        {
            continue;
        }

        if (child_it.second->type == FILENODE)
        {
            if (child_it.second->isIgnoreFile())
            {
                directories.emplace_front(child_it);
            }
            else
            {
                files.emplace_back(child_it);
            }
        }
        else
        {
            directories.emplace_back(child_it);
        }
    }

    directories.splice(directories.end(), files);

    return directories;
}

list<pair<const string*, Node*>> inSyncOrder(const remotenode_map& children)
{
    list<pair<const string*, Node*>> directories;
    list<pair<const string*, Node*>> files;

    for (auto &child_it : children)
    {
        if (child_it.second->type == FILENODE)
        {
            if (child_it.second->isIgnoreFile())
            {
                directories.emplace_front(child_it);
            }
            else
            {
                files.emplace_back(child_it);
            }
        }
        else
        {
            directories.emplace_back(child_it);
        }
    }

    directories.splice(directories.end(), files);

    return directories;
}

#endif

void Fingerprints::newnode(Node* n)
{
    if (n->type == FILENODE)
    {
        n->fingerprint_it = mFingerprints.end();
    }
}

void Fingerprints::add(Node* n)
{
    if (n->type == FILENODE)
    {
        n->fingerprint_it = mFingerprints.insert(n);
        mSumSizes += n->size;
    }
}

void Fingerprints::remove(Node* n)
{
    if (n->type == FILENODE && n->fingerprint_it != mFingerprints.end())
    {
        mSumSizes -= n->size;
        mFingerprints.erase(n->fingerprint_it);
        n->fingerprint_it = mFingerprints.end();
    }
}

void Fingerprints::clear()
{
    mFingerprints.clear();
    mSumSizes = 0;
}

m_off_t Fingerprints::getSumSizes()
{
    return mSumSizes;
}

Node* Fingerprints::nodebyfingerprint(FileFingerprint* fingerprint)
{
    fingerprint_set::iterator it = mFingerprints.find(fingerprint);
    return it == mFingerprints.end() ? nullptr : static_cast<Node*>(*it);
}

node_vector *Fingerprints::nodesbyfingerprint(FileFingerprint* fingerprint)
{
    node_vector *nodes = new node_vector();
    auto p = mFingerprints.equal_range(fingerprint);
    for (iterator it = p.first; it != p.second; ++it)
    {
        nodes->push_back(static_cast<Node*>(*it));
    }
    return nodes;
}

} // namespace
