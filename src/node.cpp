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
#include "mega/heartbeats.h"
#include "megafs.h"

namespace mega {

Node::Node(MegaClient* cclient, node_vector* dp, NodeHandle h, NodeHandle ph,
           nodetype_t t, m_off_t s, handle u, const char* fa, m_time_t ts)
{
    client = cclient;
    outshares = NULL;
    pendingshares = NULL;
    tag = 0;
    appdata = NULL;

    nodehandle = h.as8byte();
    parenthandle = ph.as8byte();

    parent = NULL;

    type = t;

    size = s;
    owner = u;

    JSON::copystring(&fileattrstring, fa);

    ctime = ts;

    inshare = NULL;
    sharekey = NULL;
    foreignkey = false;

    plink = NULL;

    memset(&changed, 0, sizeof changed);

    Node* p;

    client->nodes[h] = this;

    if (t == ROOTNODE) client->rootnodes.files = h;
    if (t == INCOMINGNODE) client->rootnodes.inbox = h;
    if (t == RUBBISHNODE) client->rootnodes.rubbish = h;

    // set parent linkage or queue for delayed parent linkage in case of
    // out-of-order delivery
    if ((p = client->nodeByHandle(ph, true)))
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

        const Node* fa = firstancestor();
        NodeHandle ancestor = fa->nodeHandle();
        if (ancestor == client->rootnodes.files || ancestor == client->rootnodes.inbox || ancestor == client->rootnodes.rubbish || fa->inshare)
        {
            client->mNodeCounters[firstancestor()->nodeHandle()] -= subnodeCounts();
        }

        if (inshare)
        {
            client->mNodeCounters.erase(nodeHandle());
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
}


Node* Node::childbyname(const string& name)
{
    for (auto* child : children)
    {
        if (child->hasName(name))
            return child;
    }

    return nullptr;
}

bool Node::hasChildWithName(const string& name) const
{
    for (auto* child : children)
    {
        if (child->hasName(name))
            return true;
    }

    return false;
}

void Node::setkeyfromjson(const char* k)
{
    if (keyApplied()) --client->mAppliedKeyNodeCount;
    JSON::copystring(&nodekeydata, k);
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

    auto authKeySize = MemAccess::get<char>(ptr);

    ptr += sizeof authKeySize;
    const char *authKey = nullptr;
    if (authKeySize)
    {
        authKey = ptr;
        ptr += authKeySize;
    }

    for (i = 5; i--;)
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

    n = new Node(client, dp, NodeHandle().set6byte(h), NodeHandle().set6byte(ph), t, s, u, fa, ts);

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
        LocalPath::utf8_normalize(&(it->second));
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

        plink = new PublicLink(ph, cts, ets, takendown, authKey ? authKey : "");
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

    if (isExported && plink && plink->mAuthKey.size())
    {
        auto authKeySize = (char)plink->mAuthKey.size();
        d->append((char*)&authKeySize, sizeof(authKeySize));
        d->append(plink->mAuthKey.data(), authKeySize);
    }
    else
    {
        d->append("", 1);
    }

    d->append("\0\0\0\0", 5); // Use these bytes for extensions

    if (inshare)
    {
        numshares = -1;
    }
    else
    {
        numshares = 0;
        if (outshares)
        {
            numshares = static_cast<short int>(numshares + outshares->size());
        }
        if (pendingshares)
        {
            numshares = static_cast<short int>(numshares + pendingshares->size());
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
    return client->getRecycledTemporaryNodeCipher(&nodekeydata);
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

        AttrMap oldAttrs(attrs);
        attrs.map.clear();
        json.begin((char*)buf + 5);

        while ((name = json.getnameid()) != EOO && json.storeobject((t = &attrs.map[name])))
        {
            JSON::unescape(t);

            if (name == 'n')
            {
                LocalPath::utf8_normalize(t);
            }
        }

        changed.name = attrs.hasDifferentValue('n', oldAttrs.map);
        changed.favourite = attrs.hasDifferentValue(AttrMap::string2nameid("fav"), oldAttrs.map);

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

bool Node::hasName(const string& name) const
{
    auto it = attrs.map.find('n');
    return it != attrs.map.end() && it->second == name;
}

// return file/folder name or special status strings
const char* Node::displayname() const
{
    // not yet decrypted
    if (attrstring)
    {
        LOG_debug << "NO_KEY " << type << " " << size << " " << Base64Str<MegaClient::NODEHANDLE>(nodehandle);
        return "NO_KEY";
    }

    attr_map::const_iterator it;

    it = attrs.map.find('n');

    if (it == attrs.map.end())
    {
        if (type < ROOTNODE || type > RUBBISHNODE)
        {
            LOG_debug << "CRYPTO_ERROR " << type << " " << size << " " << nodehandle;
        }
        return "CRYPTO_ERROR";
    }

    if (!it->second.size())
    {
        LOG_debug << "BLANK " << type << " " << size << " " << nodehandle;
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
    handle me = client->loggedin() ? client->me : client->rootnodes.files.as8byte();

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

    const Node *originalancestor = firstancestor();
    NodeHandle oah = originalancestor->nodeHandle();
    if (oah == client->rootnodes.files || oah == client->rootnodes.inbox || oah == client->rootnodes.rubbish || originalancestor->inshare)
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

    parent = p;

    if (parent)
    {
        //LOG_info << "moving " << Base64Str<MegaClient::NODEHANDLE>(nodehandle) << " " << attrs.map['n'] << " into " << Base64Str<MegaClient::NODEHANDLE>(parent->nodehandle) << " " << parent->attrs.map['n'];
        child_it = parent->children.insert(parent->children.end(), this);
    }

    const Node* newancestor = firstancestor();
    NodeHandle nah = newancestor->nodeHandle();
    if (nah == client->rootnodes.files || nah == client->rootnodes.inbox || nah == client->rootnodes.rubbish || newancestor->inshare)
    {
        if (!gotnc)
        {
            nc = subnodeCounts();
        }

        client->mNodeCounters[nah] += nc;
    }

//#ifdef ENABLE_SYNC
//    client->cancelSyncgetsOutsideSync(this);
//#endif

    return true;
}

const Node* Node::firstancestor() const
{
    const Node* n = this;
    while (n->parent != NULL)
    {
        n = n->parent;
    }
    return n;
}

const Node* Node::latestFileVersion() const
{
    const Node* n = this;
    if (type == FILENODE)
    {
        while (n->parent && n->parent->type == FILENODE)
        {
            n = n->parent;
        }
    }
    return n;
}

unsigned Node::depth() const
{
    auto* node = latestFileVersion();
    unsigned depth = 0u;

    for ( ; node->parent; node = node->parent)
        ++depth;

    return depth;
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

bool Node::isbelow(NodeHandle p) const
{
    const Node* n = this;

    for (;;)
    {
        if (!n)
        {
            return false;
        }

        if (n->nodeHandle() == p)
        {
            return true;
        }

        n = n->parent;
    }
}

void Node::setpubliclink(handle ph, m_time_t cts, m_time_t ets, bool takendown, const string &authKey)
{
    if (!plink) // creation
    {
        assert(client->mPublicLinks.find(nodehandle) == client->mPublicLinks.end());
        plink = new PublicLink(ph, cts, ets, takendown, authKey.empty() ? nullptr : authKey.c_str());
    }
    else            // update
    {
        assert(client->mPublicLinks.find(nodehandle) != client->mPublicLinks.end());
        plink->ph = ph;
        plink->cts = cts;
        plink->ets = ets;
        plink->takendown = takendown;
        plink->mAuthKey = authKey;
    }
    client->mPublicLinks[nodehandle] = ph;
}

PublicLink::PublicLink(handle ph, m_time_t cts, m_time_t ets, bool takendown, const char *authKey)
{
    this->ph = ph;
    this->cts = cts;
    this->ets = ets;
    this->takendown = takendown;
    if (authKey)
    {
        this->mAuthKey = authKey;
    }
}

PublicLink::PublicLink(PublicLink *plink)
{
    this->ph = plink->ph;
    this->cts = plink->cts;
    this->ets = plink->ets;
    this->takendown = plink->takendown;
    this->mAuthKey = plink->mAuthKey;
}

bool PublicLink::isExpired()
{
    if (!ets)       // permanent link: ets=0
        return false;

    m_time_t t = m_time();
    return ets < t;
}

#ifdef ENABLE_SYNC
// set, change or remove LocalNode's parent and localname/slocalname.
// newlocalpath must be a leaf name and must not point to an empty string (unless newparent == NULL).
// no shortname allowed as the last path component.
void LocalNode::setnameparent(LocalNode* newparent, const LocalPath& newlocalpath, std::unique_ptr<LocalPath> newshortname)
{
    Sync* oldsync = NULL;

    if (newshortname && *newshortname == newlocalpath)
    {
        // if the short name is the same, don't bother storing it.
        newshortname.reset();
    }

    bool parentChange = newparent != parent;
    bool localnameChange = newlocalpath != localname;
    bool shortnameChange = (newshortname && !slocalname) ||
                           (slocalname && !newshortname) ||
                           (newshortname && slocalname && *newshortname != *slocalname);

    if (parent)
    {
        if (parentChange || localnameChange)
        {
            // remove existing child linkage for localname
            parent->children.erase(&localname);
        }

        if (slocalname && (
            parentChange || shortnameChange))
        {
            // remove existing child linkage for slocalname
            parent->schildren.erase(slocalname.get());
        }
    }

    // reset treestate for old subtree (before we update the names for this node, in case we generate paths while recursing)
    // in case of just not syncing that subtree anymore - updates icon overlays
    if (parent && !newparent && !sync->mDestructorRunning)
    {
        // since we can't do it after the parent is updated
        // send out notifications with the current (soon to be old) paths, saying these are not consdiered by the sync anymore
        recursiveSetAndReportTreestate(TREESTATE_NONE, true, true);
    }

    if (localnameChange)
    {
        // set new name
        localname = newlocalpath;
    }

    if (shortnameChange)
    {
        // set new shortname
        slocalname = move(newshortname);
    }


    if (parentChange)
    {
        parent = newparent;

        if (parent && sync != parent->sync)
        {
            oldsync = sync;
            LOG_debug << "Moving files between different syncs";
        }
    }

    // add to parent map by localname
    if (parent && (parentChange || localnameChange))
    {
        #ifdef DEBUG
            auto it = parent->children.find(&localname);
            assert(it == parent->children.end());   // check we are not about to orphan the old one at this location... if we do then how did we get a clash in the first place?
        #endif

        parent->children[&localname] = this;
    }

    // add to parent map by shortname
    if (parent && slocalname && (parentChange || shortnameChange))
    {
#ifdef DEBUG
// TODO: enable these checks after we're sure about the localname check above
//        auto it = parent->schildren.find(&*slocalname);
//        assert(it == parent->schildren.end());   // check we are not about to orphan the old one at this location... if we do then how did we get a clash in the first place?
#endif
        parent->schildren[&*slocalname] = this;
    }

    // reset treestate
    if (parent && parentChange && !sync->mDestructorRunning)
    {
        // As we recurse through the update tree, we will see
        // that it's different from this, and send out the true state
        recursiveSetAndReportTreestate(TREESTATE_NONE, true, false);
    }

    if (oldsync)
    {
        DBTableTransactionCommitter committer(oldsync->statecachetable);

        // prepare localnodes for a sync change or/and a copy operation
        LocalTreeProcMove tp(parent->sync);
        sync->syncs.proclocaltree(this, &tp);

        // add to new parent map by localname// update local cache if there is a sync change
        oldsync->cachenodes();
        sync->cachenodes();
    }

    if (parent && parentChange)
    {
        LocalTreeProcUpdateTransfers tput;
        sync->syncs.proclocaltree(this, &tput);
    }
}

void LocalNode::moveContentTo(LocalNode* ln, LocalPath& fullPath, bool setScanAgain)
{
    vector<LocalNode*> workingList;
    workingList.reserve(children.size());
    for (auto& c : children) workingList.push_back(c.second);
    for (auto& c : workingList)
    {
        ScopedLengthRestore restoreLen(fullPath);
        fullPath.appendWithSeparator(c->localname, true);
        c->setnameparent(ln, fullPath.leafName(), sync->syncs.fsaccess->fsShortname(fullPath));

        // if moving between syncs, removal from old sync db is already done
        ln->sync->statecacheadd(c);

        if (setScanAgain)
        {
            c->setScanAgain(false, true, true, 0);
        }
    }

    ln->resetTransfer(move(transferSP));

    LocalTreeProcUpdateTransfers tput;
    tput.proc(*sync->syncs.fsaccess, ln);

    ln->mWaitingForIgnoreFileLoad = mWaitingForIgnoreFileLoad;

    // Make sure our exclusion state is recomputed.
    ln->setRecomputeExclusionState(true);
}

// delay uploads by 1.1 s to prevent server flooding while a file is still being written
void LocalNode::bumpnagleds()
{
    nagleds = Waiter::ds + 11;
}

LocalNode::LocalNode(Sync* csync)
: sync(csync)
, scanAgain(TREE_RESOLVED)
, checkMovesAgain(TREE_RESOLVED)
, syncAgain(TREE_RESOLVED)
, conflicts(TREE_RESOLVED)
, unstableFsidAssigned(false)
, deletedFS(false)
, moveApplyingToLocal(false)
, moveAppliedToLocal(false)
, scanInProgress(false)
, scanObsolete(false)
, parentSetScanAgain(false)
, parentSetCheckMovesAgain(false)
, parentSetSyncAgain(false)
, parentSetContainsConflicts(false)
, fsidSyncedReused(false)
, fsidScannedReused(false)
, confirmDeleteCount(0)
, certainlyOrphaned(0)
, neverScanned(0)
{
    fsid_lastSynced_it = sync->syncs.localnodeBySyncedFsid.end();
    fsid_asScanned_it = sync->syncs.localnodeByScannedFsid.end();
    syncedCloudNodeHandle_it = sync->syncs.localnodeByNodeHandle.end();
}

// initialize fresh LocalNode object - must be called exactly once
void LocalNode::init(nodetype_t ctype, LocalNode* cparent, const LocalPath& cfullpath, std::unique_ptr<LocalPath> shortname)
{
    parent = NULL;
//    notseen = 0;
    unstableFsidAssigned = false;
    deletedFS = false;
    moveAppliedToLocal = false;
    moveApplyingToLocal = false;
    oneTimeUseSyncedFingerprintInScan = false;
    recomputeFingerprint = false;
    scanAgain = TREE_RESOLVED;
    checkMovesAgain = TREE_RESOLVED;
    syncAgain = TREE_RESOLVED;
    conflicts = TREE_RESOLVED;
    parentSetCheckMovesAgain = false;
    parentSetSyncAgain = false;
    parentSetScanAgain = false;
    parentSetContainsConflicts = false;
    fsidSyncedReused = false;
    fsidScannedReused = false;
    confirmDeleteCount = 0;
    certainlyOrphaned = 0;
    neverScanned = 0;
    scanInProgress = false;
    scanObsolete = false;
    slocalname = NULL;

    if (type != FILENODE)
    {
        neverScanned = 1;
        ++sync->threadSafeState->neverScannedFolderCount;
    }

    mReportedSyncState = TREESTATE_NONE;

    type = ctype;

    bumpnagleds();

    mWaitingForIgnoreFileLoad = false;

    if (cparent)
    {
        setnameparent(cparent, cfullpath.leafName(), std::move(shortname));

        mIsIgnoreFile = type == FILENODE && localname == IGNORE_FILE_NAME;

        mExclusionState = parent->exclusionState(localname, type);
    }
    else
    {
        localname = cfullpath;
        slocalname.reset(shortname && *shortname != localname ? shortname.release() : nullptr);

        mExclusionState = ES_INCLUDED;
    }


//#ifdef DEBUG
//    // double check we were given the right shortname (if the file exists yet)
//    auto fa = sync->client->fsaccess->newfileaccess(false);
//    if (fa->fopen(cfullpath))  // exists, is file
//    {
//        auto sn = sync->client->fsaccess->fsShortname(cfullpath);
//        assert(!localname.empty() &&
//            ((!slocalname && (!sn || localname == *sn)) ||
//                (slocalname && sn && !slocalname->empty() && *slocalname != localname && *slocalname == *sn)));
//    }
//#endif

    sync->syncs.totalLocalNodes++;

    if (type != TYPE_UNKNOWN)
    {
        sync->localnodes[type]++;
    }
}

LocalNode::RareFields::ScanBlocked::ScanBlocked(PrnGen &rng, const LocalPath& lp, LocalNode* ln)
    : scanBlockedTimer(rng)
    , scanBlockedLocalPath(lp)
    , localNode(ln)
{
    scanBlockedTimer.backoff(Sync::SCANNING_DELAY_DS);
}

auto LocalNode::rare() -> RareFields&
{
    // Rare fields are those that are hardly ever used, and we don't want every LocalNode using more RAM for them all the time.
    // Those rare fields are put in this RareFields struct instead, and LocalNode holds an optional unique_ptr to them
    // Only a tiny subset of the LocalNodes should have populated RareFields at any one time.
    // If any of the rare fields are in use, the struct is present.  trimRareFields() removes the struct when none are in use.
    // This function should be used when one of those field is needed, as it creates the struct if it doesn't exist yet
    // and then returns it.

    if (!rareFields)
    {
        rareFields.reset(new RareFields);
    }
    return *rareFields;
}

auto LocalNode::rareRO() const -> const RareFields&
{
    // RO = read only
    // Use this function when you're not sure if rare fields have been populated, but need to check
    if (!rareFields)
    {
        static RareFields blankFields;
        return blankFields;
    }
    return *rareFields;
}

void LocalNode::trimRareFields()
{
    if (rareFields)
    {
        if (!scanInProgress) rareFields->scanRequest.reset();

        if (!rareFields->scanBlocked &&
            !rareFields->scanRequest &&
            !rareFields->moveFromHere &&
            !rareFields->moveToHere &&
            !rareFields->filterChain &&
            !rareFields->badlyFormedIgnoreFilePath &&
            rareFields->createFolderHere.expired() &&
            rareFields->removeNodeHere.expired() &&
            rareFields->unlinkHere.expired())
        {
            rareFields.reset();
        }
    }
}

unique_ptr<LocalPath> LocalNode::cloneShortname() const
{
    return unique_ptr<LocalPath>(
        slocalname
        ? new LocalPath(*slocalname)
        : nullptr);
}


void LocalNode::setScanAgain(bool doParent, bool doHere, bool doBelow, dstime delayds)
{
    if (doHere && scanInProgress)
    {
        scanObsolete = true;
    }

    auto state = TreeState((doHere?1u:0u) << 1 | (doBelow?1u:0u));

    if (state >= TREE_ACTION_HERE && delayds > 0)
        scanDelayUntil = std::max<dstime>(scanDelayUntil,  Waiter::ds + delayds);

    scanAgain = std::max<TreeState>(scanAgain, state);
    for (auto p = parent; p != NULL; p = p->parent)
    {
        p->scanAgain = std::max<TreeState>(p->scanAgain, TREE_DESCENDANT_FLAGGED);
    }

    // for scanning, we only need to set the parent once
    if (parent && doParent)
    {
        parent->scanAgain = std::max<TreeState>(parent->scanAgain, TREE_ACTION_HERE);
        doParent = false;
        parentSetScanAgain = false;
    }
    parentSetScanAgain = parentSetScanAgain || doParent;
}

void LocalNode::setCheckMovesAgain(bool doParent, bool doHere, bool doBelow)
{
    auto state = TreeState((doHere?1u:0u) << 1 | (doBelow?1u:0u));

    checkMovesAgain = std::max<TreeState>(checkMovesAgain, state);
    for (auto p = parent; p != NULL; p = p->parent)
    {
        p->checkMovesAgain = std::max<TreeState>(p->checkMovesAgain, TREE_DESCENDANT_FLAGGED);
    }

    parentSetCheckMovesAgain = parentSetCheckMovesAgain || doParent;
}

void LocalNode::setSyncAgain(bool doParent, bool doHere, bool doBelow)
{
    auto state = TreeState((doHere?1u:0u) << 1 | (doBelow?1u:0u));

    syncAgain = std::max<TreeState>(syncAgain, state);
    for (auto p = parent; p != NULL; p = p->parent)
    {
        p->syncAgain = std::max<TreeState>(p->syncAgain, TREE_DESCENDANT_FLAGGED);
    }

    parentSetSyncAgain = parentSetSyncAgain || doParent;
}

void LocalNode::setContainsConflicts(bool doParent, bool doHere, bool doBelow)
{
    // using the 3 flags for consistency & understandabilty but doBelow is not relevant
    assert(!doBelow);

    auto state = TreeState((doHere?1u:0u) << 1 | (doBelow?1u:0u));

    conflicts = std::max<TreeState>(conflicts, state);
    for (auto p = parent; p != NULL; p = p->parent)
    {
        p->conflicts = std::max<TreeState>(p->conflicts, TREE_DESCENDANT_FLAGGED);
    }

    parentSetContainsConflicts = parentSetContainsConflicts || doParent;
}

void LocalNode::initiateScanBlocked(bool folderBlocked, bool containsFingerprintBlocked)
{

    // Setting node as scan-blocked. The main loop will check it regularly by weak_ptr
    if (!rare().scanBlocked)
    {
        rare().scanBlocked.reset(new RareFields::ScanBlocked(sync->syncs.rng, getLocalPath(), this));
        sync->syncs.scanBlockedPaths.push_back(rare().scanBlocked);
    }

    if (folderBlocked && !rare().scanBlocked->folderUnreadable)
    {
        rare().scanBlocked->folderUnreadable = true;

        LOG_verbose << sync->syncname << "Directory scan has become inaccesible for path: " << getLocalPath();

        // Mark all immediate children as requiring refingerprinting.
        for (auto& childIt : children)
        {
            if (childIt.second->type == FILENODE)
                childIt.second->recomputeFingerprint = true;
        }
    }

    if (containsFingerprintBlocked && !rare().scanBlocked->filesUnreadable)
    {
        LOG_verbose << sync->syncname << "Directory scan contains fingerprint-blocked files: " << getLocalPath();

        rare().scanBlocked->filesUnreadable = true;
    }
}

bool LocalNode::checkForScanBlocked(FSNode* fsNode)
{
    if (rareRO().scanBlocked && rare().scanBlocked->folderUnreadable)
    {
        // Have we recovered?
        if (fsNode && fsNode->type != TYPE_UNKNOWN && !fsNode->isBlocked)
        {
            LOG_verbose << sync->syncname << "Recovered from being scan blocked: " << getLocalPath();

            type = fsNode->type; // original scan may not have been able to discern type, fix it now
            setScannedFsid(UNDEF, sync->syncs.localnodeByScannedFsid, fsNode->localname, FileFingerprint());
            sync->statecacheadd(this);

            if (!rare().scanBlocked->filesUnreadable)
            {
                rare().scanBlocked.reset();
                trimRareFields();
                return false;
            }
        }

        LOG_verbose << sync->syncname << "Waiting on scan blocked timer, retry in ds: "
            << rare().scanBlocked->scanBlockedTimer.retryin() << " for " << getLocalPath().toPath();

        // make sure path stays accurate in case this node moves
        rare().scanBlocked->scanBlockedLocalPath = getLocalPath();

        return true;
    }

    if (fsNode && (fsNode->type == TYPE_UNKNOWN || fsNode->isBlocked))
    {
        // We were not able to get details of the filesystem item when scanning the directory.
        // Consider it a blocked file, and we'll rescan the folder from time to time.
        LOG_verbose << sync->syncname << "File/folder was blocked when reading directory, retry later: " << getLocalPath();

        // Setting node as scan-blocked. The main loop will check it regularly by weak_ptr
        initiateScanBlocked(true, false);
        return true;
    }

    return false;
}


bool LocalNode::scanRequired() const
{
    return scanAgain != TREE_RESOLVED;
}

void LocalNode::clearRegeneratableFolderScan(SyncPath& fullPath, vector<syncRow>& childRows)
{
    if (lastFolderScan &&
        lastFolderScan->size() == children.size())
    {
        // check for scan-blocked entries, those are not regeneratable
        for (auto& c : *lastFolderScan)
        {
            if (c.type == TYPE_UNKNOWN) return;
            if (c.isBlocked) return;
        }

        // check that generating the fsNodes results in the same set
        unsigned nChecked = 0;
        for (auto& row : childRows)
        {
            if (!!row.syncNode != !!row.fsNode) return;
            if (row.syncNode && row.fsNode)
            {
                if (row.syncNode->type == FILENODE &&
                    !scannedFingerprint.isvalid)
                {
                    return;
                }

                ++nChecked;
                auto generated = row.syncNode->getScannedFSDetails();
                if (!generated.equivalentTo(*row.fsNode)) return;
            }
        }

        if (nChecked == children.size())
        {
            // LocalNodes are now consistent with the last scan.
            LOG_debug << sync->syncname << "Clearing regeneratable folder scan records (" << lastFolderScan->size() << ") at " << fullPath.localPath_utf8();
            lastFolderScan.reset();
        }
    }
}

bool LocalNode::mightHaveMoves() const
{
    return checkMovesAgain != TREE_RESOLVED;
}

bool LocalNode::syncRequired() const
{
    return syncAgain != TREE_RESOLVED;
}


void LocalNode::propagateAnySubtreeFlags()
{
    for (auto& child : children)
    {
        if (child.second->type != FILENODE)
        {
            if (scanAgain == TREE_ACTION_SUBTREE)
            {
                child.second->scanDelayUntil = std::max<dstime>(child.second->scanDelayUntil,  scanDelayUntil);
            }

            child.second->scanAgain = propagateSubtreeFlag(scanAgain, child.second->scanAgain);
            child.second->checkMovesAgain = propagateSubtreeFlag(checkMovesAgain, child.second->checkMovesAgain);
            child.second->syncAgain = propagateSubtreeFlag(syncAgain, child.second->syncAgain);
        }
    }
    if (scanAgain == TREE_ACTION_SUBTREE) scanAgain = TREE_ACTION_HERE;
    if (checkMovesAgain == TREE_ACTION_SUBTREE) checkMovesAgain = TREE_ACTION_HERE;
    if (syncAgain == TREE_ACTION_SUBTREE) syncAgain = TREE_ACTION_HERE;
}


bool LocalNode::processBackgroundFolderScan(syncRow& row, SyncPath& fullPath)
{
    bool syncHere = false;

    assert(row.syncNode == this);
    assert(row.fsNode);
    assert(!sync->localdebris.isContainingPathOf(fullPath.localPath));

    std::shared_ptr<ScanService::ScanRequest> ourScanRequest = scanInProgress ? rare().scanRequest  : nullptr;

    std::shared_ptr<ScanService::ScanRequest>* availableScanSlot = nullptr;
    if (!sync->mActiveScanRequestGeneral || sync->mActiveScanRequestGeneral->completed())
    {
        availableScanSlot = &sync->mActiveScanRequestGeneral;
    }
    else if (neverScanned &&
            (!sync->mActiveScanRequestUnscanned || sync->mActiveScanRequestUnscanned->completed()))
    {
        availableScanSlot = &sync->mActiveScanRequestUnscanned;
    }

    if (!ourScanRequest && availableScanSlot)
    {
        // we can start a single new request if we are still recursing and the last request from this sync completed already
        if (scanDelayUntil != 0 && Waiter::ds < scanDelayUntil)
        {
            LOG_verbose << sync->syncname << "Too soon to scan this folder, needs more ds: " << scanDelayUntil - Waiter::ds;
        }
        else
        {
            // queueScan() already logs: LOG_verbose << "Requesting scan for: " << fullPath.toPath(*client->fsaccess);
            scanObsolete = false;
            scanInProgress = true;

            // If enough details of the scan are the same, we can reuse fingerprints instead of recalculating
            map<LocalPath, FSNode> priorScanChildren;

            for (auto& childIt : children)
            {
                auto& child = *childIt.second;

                bool useSyncedFP = child.oneTimeUseSyncedFingerprintInScan;
                child.oneTimeUseSyncedFingerprintInScan = false;

                bool forceRecompute = child.recomputeFingerprint;
                child.recomputeFingerprint = false;

                // Can't fingerprint directories.
                if (child.type != FILENODE || forceRecompute)
                    continue;

                if (child.scannedFingerprint.isvalid)
                {
                    // as-scanned by this instance is more accurate if available
                    priorScanChildren.emplace(*childIt.first, child.getScannedFSDetails());
                }
                else if (useSyncedFP && child.fsid_lastSynced != UNDEF && child.syncedFingerprint.isvalid)
                {
                    // But otherwise, already-synced syncs on startup should not re-fingerprint
                    // files that match the synced fingerprint by fsid/size/mtime (for quick startup)
                    priorScanChildren.emplace(*childIt.first, child.getLastSyncedFSDetails());
                }
            }

            ourScanRequest = sync->syncs.mScanService->queueScan(fullPath.localPath,
                row.fsNode->fsid, sync->syncs.mClient.followsymlinks, move(priorScanChildren));

            rare().scanRequest = ourScanRequest;
            *availableScanSlot = ourScanRequest;

            LOG_verbose << sync->syncname << "Issuing Directory scan request for : " << fullPath.localPath_utf8() << (availableScanSlot == &sync->mActiveScanRequestUnscanned ? " (in unscanned slot)" : "");

            if (neverScanned)
            {
                neverScanned = 0;
                --sync->threadSafeState->neverScannedFolderCount;
                LOG_verbose << sync->syncname << "Remaining known unscanned folders: " << sync->threadSafeState->neverScannedFolderCount.load();
            }
        }
    }
    else if (ourScanRequest &&
             ourScanRequest->completed())
    {
        if (ourScanRequest == sync->mActiveScanRequestGeneral) sync->mActiveScanRequestGeneral.reset();
        if (ourScanRequest == sync->mActiveScanRequestUnscanned) sync->mActiveScanRequestUnscanned.reset();

        scanInProgress = false;

        if (ScanService::SCAN_FSID_MISMATCH == ourScanRequest->completionResult())
        {
            LOG_verbose << sync->syncname << "Directory scan detected outdated fsid : " << fullPath.localPath_utf8();
            scanObsolete = true;
        }

        if (ScanService::SCAN_SUCCESS == ourScanRequest->completionResult()
            && ourScanRequest->fsidScanned() != row.fsNode->fsid)
        {
            LOG_verbose << sync->syncname << "Directory scan returned was for now outdated fsid : " << fullPath.localPath_utf8();
            scanObsolete = true;
        }

        if (scanObsolete)
        {
            LOG_verbose << sync->syncname << "Directory scan outdated for : " << fullPath.localPath_utf8();
            scanObsolete = false;

            // Scan results are out of date but may still be useful.
            lastFolderScan.reset(new vector<FSNode>(ourScanRequest->resultNodes()));

            // Mark this directory as requiring another scan.
            setScanAgain(false, true, false, 10);
        }
        else if (ScanService::SCAN_SUCCESS == ourScanRequest->completionResult())
        {
            lastFolderScan.reset(new vector<FSNode>(ourScanRequest->resultNodes()));

            LOG_verbose << sync->syncname << "Received " << lastFolderScan->size() << " directory scan results for: " << fullPath.localPath_utf8();

            scanDelayUntil = Waiter::ds + 20; // don't scan too frequently
            scanAgain = TREE_RESOLVED;
            setSyncAgain(false, true, false);
            syncHere = true;

            size_t numFingerprintBlocked = 0;
            for (auto& n : *lastFolderScan)
            {
                if (n.type == FILENODE && !n.fingerprint.isvalid) ++numFingerprintBlocked;
            }

            if (numFingerprintBlocked)
            {
                initiateScanBlocked(false, true);
            }
            else if (rareRO().scanBlocked &&
                     rareRO().scanBlocked->filesUnreadable)
            {
                LOG_verbose << sync->syncname << "Directory scan fingerprint-blocked files all resolved at: " << getLocalPath();
                rare().scanBlocked.reset();
                trimRareFields();
            }
        }
        else // SCAN_INACCESSIBLE
        {
            // we were previously able to scan this node, but now we can't.
            row.fsNode->isBlocked = true;
            if (!checkForScanBlocked(row.fsNode))
            {
                initiateScanBlocked(true, false);
            }
        }
    }

    trimRareFields();
    return syncHere;
}

void LocalNode::reassignUnstableFsidsOnceOnly(const FSNode* fsnode)
{
    if (!sync->fsstableids && !unstableFsidAssigned)
    {
        // for FAT and other filesystems where we can't rely on fsid
        // being the same after remount, so update our previously synced nodes
        // with the actual fsids now attached to them (usually generated by FUSE driver)

        if (fsnode && sync->syncEqual(*fsnode, *this))
        {
            setSyncedFsid(fsnode->fsid, sync->syncs.localnodeBySyncedFsid, localname, fsnode->cloneShortname());
            sync->statecacheadd(this);
        }
        else if (fsid_lastSynced != UNDEF)
        {
            // this node was synced with something, but not the thing that's there now (or not there)
            setSyncedFsid(UNDEF-1, sync->syncs.localnodeBySyncedFsid, localname, fsnode->cloneShortname());
            sync->statecacheadd(this);
        }
        unstableFsidAssigned = true;
    }
}

void LocalNode::recursiveSetAndReportTreestate(treestate_t ts, bool recurse, bool reportToApp)
{
    if (reportToApp && ts != mReportedSyncState)
    {
        assert(sync->syncs.onSyncThread());
        sync->syncs.mClient.app->syncupdate_treestate(sync->getConfig(), getLocalPath(), ts, type);
    }

    mReportedSyncState = ts;

    if (recurse)
    {
        for (auto& i : children)
        {
            i.second->recursiveSetAndReportTreestate(ts, recurse, reportToApp);
        }
    }
}

treestate_t LocalNode::checkTreestate(bool notifyChangeToApp)
{
    // notify file explorer if the sync state overlay icon should change

    treestate_t ts = TREESTATE_NONE;

    if (scanAgain == TREE_RESOLVED &&
        checkMovesAgain == TREE_RESOLVED &&
        syncAgain == TREE_RESOLVED)
    {
        ts = TREESTATE_SYNCED;
    }
    else if (type == FILENODE)
    {
        ts = TREESTATE_PENDING;
    }
    else if (scanAgain <= TREE_DESCENDANT_FLAGGED &&
        checkMovesAgain <= TREE_DESCENDANT_FLAGGED &&
        syncAgain <= TREE_DESCENDANT_FLAGGED)
    {
        ts = TREESTATE_SYNCING;
    }
    else
    {
        ts = TREESTATE_PENDING;
    }

    recursiveSetAndReportTreestate(ts, false, notifyChangeToApp);

    return ts;
}


// set fsid - assume that an existing assignment of the same fsid is no longer current and revoke
void LocalNode::setSyncedFsid(handle newfsid, fsid_localnode_map& fsidnodes, const LocalPath& fsName, std::unique_ptr<LocalPath> newshortname)
{
    if (fsid_lastSynced_it != fsidnodes.end())
    {
        if (newfsid == fsid_lastSynced && localname == fsName)
        {
            return;
        }

        fsidnodes.erase(fsid_lastSynced_it);
    }

    fsid_lastSynced = newfsid;
    fsidSyncedReused = false;

    // if synced to fs, localname should match exactly (no differences in case/escaping etc)
    if (localname != fsName ||
            !!newshortname != !!slocalname ||
            (newshortname && slocalname && *newshortname != *slocalname))
    {
        // localname must always be set by this function, to maintain parent's child maps
        setnameparent(parent, fsName, move(newshortname));
    }

    // LOG_verbose << "localnode " << this << " fsid " << toHandle(fsid_lastSynced) << " localname " << fsName.toPath() << " parent " << parent;

    if (fsid_lastSynced == UNDEF)
    {
        fsid_lastSynced_it = fsidnodes.end();
    }
    else
    {
        fsid_lastSynced_it = fsidnodes.insert(std::make_pair(fsid_lastSynced, this));
    }

//    assert(localname.empty() || name.empty() || (!parent && parent_dbid == UNDEF) || parent_dbid == 0 ||
//        0 == compareUtf(localname, true, name, false, true));
}

void LocalNode::setScannedFsid(handle newfsid, fsid_localnode_map& fsidnodes, const LocalPath& fsName, const FileFingerprint& scanfp)
{
    if (fsid_asScanned_it != fsidnodes.end())
    {
        fsidnodes.erase(fsid_asScanned_it);
    }

    fsid_asScanned = newfsid;
    fsidScannedReused = false;

    scannedFingerprint = scanfp;

    if (fsid_asScanned == UNDEF)
    {
        fsid_asScanned_it = fsidnodes.end();
    }
    else
    {
        fsid_asScanned_it = fsidnodes.insert(std::make_pair(fsid_asScanned, this));
    }

    assert(fsid_asScanned == UNDEF || 0 == compareUtf(localname, true, fsName, true, true));
}

void LocalNode::setSyncedNodeHandle(NodeHandle h)
{
    if (syncedCloudNodeHandle_it != sync->syncs.localnodeByNodeHandle.end())
    {
        if (h == syncedCloudNodeHandle)
        {
            return;
        }

        assert(syncedCloudNodeHandle_it->first == syncedCloudNodeHandle);

        // too verbose for million-node syncs
        //LOG_verbose << sync->syncname << "removing synced handle " << syncedCloudNodeHandle << " for " << localnodedisplaypath(*sync->syncs.fsaccess);

        sync->syncs.localnodeByNodeHandle.erase(syncedCloudNodeHandle_it);
    }

    syncedCloudNodeHandle = h;

    if (syncedCloudNodeHandle == UNDEF)
    {
        syncedCloudNodeHandle_it = sync->syncs.localnodeByNodeHandle.end();
    }
    else
    {
        // too verbose for million-node syncs
        //LOG_verbose << sync->syncname << "adding synced handle " << syncedCloudNodeHandle << " for " << localnodedisplaypath(*sync->syncs.fsaccess);

        syncedCloudNodeHandle_it = sync->syncs.localnodeByNodeHandle.insert(std::make_pair(syncedCloudNodeHandle, this));
    }

//    assert(localname.empty() || name.empty() || (!parent && parent_dbid == UNDEF) || parent_dbid == 0 ||
//        0 == compareUtf(localname, true, name, false, true));
}

LocalNode::~LocalNode()
{
    if (!sync->mDestructorRunning && dbid)
    {
        sync->statecachedel(this);
    }

    if (sync->dirnotify && !sync->mDestructorRunning)
    {
        // deactivate corresponding notifyq records
        sync->dirnotify->fsEventq.replaceLocalNodePointers(this, (LocalNode*)~0);
        sync->dirnotify->fsDelayedNetworkEventq.replaceLocalNodePointers(this, (LocalNode*)~0);
    }

    if (!sync->syncs.mExecutingLocallogout)
    {
        // for Locallogout, we will resume syncs and their transfers on re-login.
        // for other cases - single sync cancel, disable etc - transfers are cancelled.
        resetTransfer(nullptr);

        // remove from fsidnode map, if present
        if (fsid_lastSynced_it != sync->syncs.localnodeBySyncedFsid.end())
        {
            sync->syncs.localnodeBySyncedFsid.erase(fsid_lastSynced_it);
        }
        if (fsid_asScanned_it != sync->syncs.localnodeByScannedFsid.end())
        {
            sync->syncs.localnodeByScannedFsid.erase(fsid_asScanned_it);
        }
        if (syncedCloudNodeHandle_it != sync->syncs.localnodeByNodeHandle.end())
        {
            sync->syncs.localnodeByNodeHandle.erase(syncedCloudNodeHandle_it);
        }
    }

    sync->syncs.totalLocalNodes--;

    if (type != TYPE_UNKNOWN)
    {
        sync->localnodes[type]--;    // todo: make sure we are not using the larger types and overflowing the buffer
    }

    // remove parent association
    if (parent)
    {
        setnameparent(nullptr, LocalPath(), nullptr);
    }

    deleteChildren();
}

void LocalNode::deleteChildren()
{
    for (localnode_map::iterator it = children.begin(); it != children.end(); )
    {
        // the destructor removes the child from our `children` map
        delete it++->second;
    }
    assert(children.empty());
}


bool LocalNode::conflictsDetected() const
{
    return conflicts != TREE_RESOLVED;
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

LocalPath LocalNode::getLocalPath() const
{
    LocalPath lp;
    getlocalpath(lp);
    return lp;
}

void LocalNode::getlocalpath(LocalPath& path) const
{
    path.clear();

    for (const LocalNode* l = this; l != nullptr; l = l->parent)
    {
        assert(!l->parent || l->parent->sync == sync);

        // sync root has absolute path, the rest are just their leafname
        path.prependWithSeparator(l->localname);
    }
}

string LocalNode::getCloudPath() const
{
    string path;

    for (const LocalNode* l = this; l != nullptr; l = l->parent)
    {
        string name;

        CloudNode cn;
        string fullpath;
        if (sync->syncs.lookupCloudNode(l->syncedCloudNodeHandle, cn, l->parent ? nullptr : &fullpath,
            nullptr, nullptr, nullptr, nullptr, Syncs::LATEST_VERSION))
        {
            name = cn.name;
        }
        else
        {
            name = localname.toName(*sync->syncs.fsaccess);
        }

        assert(!l->parent || l->parent->sync == sync);

        if (!path.empty())
            path.insert(0, 1, '/');

        path.insert(0, l->parent ? name : fullpath);
    }
    return path;
}

string LocalNode::getCloudName() const
{
    return localname.toName(*sync->syncs.fsaccess);
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

LocalNode* LocalNode::findChildWithSyncedNodeHandle(NodeHandle h)
{
    for (auto& c : children)
    {
        if (c.second->syncedCloudNodeHandle == h)
        {
            return c.second;
        }
    }
    return nullptr;
}

FSNode LocalNode::getLastSyncedFSDetails() const
{
    assert(fsid_lastSynced != UNDEF);

    FSNode n;
    n.localname = localname;
    n.shortname = slocalname ? make_unique<LocalPath>(*slocalname): nullptr;
    n.type = type;
    n.fsid = fsid_lastSynced;
    n.isSymlink = false;  // todo: store localndoes for symlinks but don't use them?
    n.fingerprint = syncedFingerprint;
    assert(syncedFingerprint.isvalid || type != FILENODE);
    return n;
}


FSNode LocalNode::getScannedFSDetails() const
{
    FSNode n;
    n.localname = localname;
    n.shortname = slocalname ? make_unique<LocalPath>(*slocalname): nullptr;
    n.type = type;
    n.fsid = fsid_asScanned;
    n.isSymlink = false;  // todo: store localndoes for symlinks but don't use them?
    n.fingerprint = scannedFingerprint;
    assert(scannedFingerprint.isvalid || type != FILENODE);
    return n;
}

void LocalNode::queueClientUpload(shared_ptr<SyncUpload_inClient> upload, VersioningOption vo)
{
    resetTransfer(upload);

    sync->syncs.queueClient([upload, vo](MegaClient& mc, DBTableTransactionCommitter& committer)
        {
            mc.nextreqtag();
            mc.startxfer(PUT, upload.get(), committer, false, false, false, vo);
        });

}

void LocalNode::queueClientDownload(shared_ptr<SyncDownload_inClient> download)
{
    resetTransfer(download);

    sync->syncs.queueClient([download](MegaClient& mc, DBTableTransactionCommitter& committer)
        {
            mc.nextreqtag();
            mc.startxfer(GET, download.get(), committer, false, false, false, NoVersioning);
        });

}

void LocalNode::resetTransfer(shared_ptr<SyncTransfer_inClient> p)
{
    if (transferSP)
    {
        if (!transferSP->wasTerminated &&
            !transferSP->wasCompleted)
        {
            LOG_debug << "Abandoning old transfer, and queueing its cancel on client thread";

            // this flag allows in-progress transfers to self-cancel
            transferSP->wasRequesterAbandoned = true;

            // also queue an operation on the client thread to cancel it if it's queued
            auto tsp = transferSP;
            sync->syncs.queueClient([tsp](MegaClient& mc, DBTableTransactionCommitter& committer)
                {
                    mc.nextreqtag();
                    mc.stopxfer(tsp.get(), &committer);
                });
        }
    }

    if (p) p->selfKeepAlive = p;
    transferSP = move(p);
}


void LocalNode::updateTransferLocalname()
{
    if (transferSP)
    {
        transferSP->setLocalname(getLocalPath());
    }
}

void LocalNode::transferResetUnlessMatched(direction_t dir, const FileFingerprint& fingerprint)
{
    auto uploadPtr = dynamic_cast<SyncUpload_inClient*>(transferSP.get());

    // todo: should we be more accurate than just fingerprint?
    if (transferSP && (
        transferSP->wasTerminated ||
        dir != (uploadPtr ? PUT : GET) ||
        !(transferSP->fingerprint() == fingerprint)))
    {

        if (uploadPtr && uploadPtr->putnodesStarted)
        {
            // checking for a race where we already sent putnodes and it hasn't completed,
            // then we discover something that means we should abandon the transfer
            LOG_debug << sync->syncname << "Cancelling superceded transfer even though we have an outstanding putnodes request! " << transferSP->getLocalname().toPath();
            assert(false);
        }

        LOG_debug << sync->syncname << "Cancelling superceded transfer of " << transferSP->getLocalname().toPath();
        resetTransfer(nullptr);
    }
}

void SyncTransfer_inClient::terminated(error e)
{
    File::terminated(e);

    if (e == API_EOVERQUOTA)
    {
        syncThreadSafeState->client()->syncs.disableSyncByBackupId(syncThreadSafeState->backupId(), FOREIGN_TARGET_OVERSTORAGE, false, true, nullptr);
    }

    wasTerminated = true;
    selfKeepAlive.reset();  // deletes this object! (if abandoned by sync)
}

void SyncTransfer_inClient::completed(Transfer* t, putsource_t source)
{
    assert(source == PUTNODES_SYNC);

    // do not allow the base class to submit putnodes immediately
    //File::completed(t, source);

    wasCompleted = true;
    selfKeepAlive.reset();  // deletes this object! (if abandoned by sync)
}

void SyncUpload_inClient::completed(Transfer* t, putsource_t source)
{
    // Keep the info required for putnodes and wait for
    // the sync thread to validate and activate the putnodes

    uploadHandle = t->uploadhandle;
    uploadToken = *t->ultoken;
    fileNodeKey = t->filekey;

    SyncTransfer_inClient::completed(t, source);
}

void SyncUpload_inClient::sendPutnodes(MegaClient* client, NodeHandle ovHandle)
{
    weak_ptr<SyncThreadsafeState> stts = syncThreadSafeState;

    File::sendPutnodes(client,
        uploadHandle,
        uploadToken,
        fileNodeKey,
        PUTNODES_SYNC,
        ovHandle,
        [stts](const Error& e, targettype_t t, vector<NewNode>& nn, bool targetOverride){

            if (auto s = stts.lock())
            {
                auto client = s->client();
                if (e == API_EACCESS)
                {
                    client->sendevent(99402, "API_EACCESS putting node in sync transfer", 0);
                }
                else if (e == API_EOVERQUOTA)
                {
                    client->syncs.disableSyncByBackupId(s->backupId(),  FOREIGN_TARGET_OVERSTORAGE, false, true, nullptr);
                }

                // since we used a completion function, putnodes_result is not called.
                // but the intermediate layer still needs that in order to call the client app back:
                client->app->putnodes_result(e, t, nn, targetOverride);
            }
        });
}

SyncUpload_inClient::SyncUpload_inClient(NodeHandle targetFolder, const LocalPath& fullPath,
        const string& nodeName, const FileFingerprint& ff, shared_ptr<SyncThreadsafeState> stss,
        handle fsid, const LocalPath& localname, bool fromInshare)
{
    *static_cast<FileFingerprint*>(this) = ff;

    // normalized name (UTF-8 with unescaped special chars)
    // todo: we did unescape them though?
    name = nodeName;

    // setting the full path means it works like a normal non-sync transfer
    setLocalname(fullPath);

    h = targetFolder;

    hprivate = false;
    hforeign = false;
    syncxfer = true;
    fromInsycShare = fromInshare;
    temporaryfile = false;
    chatauth = nullptr;
    transfer = nullptr;
    tag = 0;

    syncThreadSafeState = move(stss);
    syncThreadSafeState->transferBegin(PUT, size);

    sourceFsid = fsid;
    sourceLocalname = localname;
}

SyncUpload_inClient::~SyncUpload_inClient()
{
    if (!wasTerminated && !wasCompleted)
    {
        assert(wasRequesterAbandoned);
        transfer = nullptr;  // don't try to remove File from Transfer from the wrong thread
    }

    if (wasCompleted && wasPutnodesCompleted)
    {
        syncThreadSafeState->transferComplete(PUT, size);
    }
    else
    {
        syncThreadSafeState->transferFailed(PUT, size);
    }

    if (putnodesStarted)
    {
        syncThreadSafeState->removeExpectedUpload(h, name);
    }
}

void SyncUpload_inClient::prepare(FileSystemAccess&)
{
    transfer->localfilename = getLocalname();

    // is this transfer in progress? update file's filename.
    if (transfer->slot && transfer->slot->fa && !transfer->slot->fa->nonblocking_localname.empty())
    {
        transfer->slot->fa->updatelocalname(transfer->localfilename, false);
    }

    //todo: localNode.treestate(TREESTATE_SYNCING);
}


// serialize/unserialize the following LocalNode properties:
// - type/size
// - fsid
// - parent LocalNode's dbid
// - corresponding Node handle
// - local name
// - fingerprint crc/mtime (filenodes only)
bool LocalNodeCore::write(string& destination, uint32_t parentID)
{
    // We need size even if we're not synced.
    auto size = syncedFingerprint.isvalid ? syncedFingerprint.size : 0;

    CacheableWriter w(destination);
    w.serializei64(type ? -type : size);
    w.serializehandle(fsid_lastSynced);
    w.serializeu32(parentID);
    w.serializenodehandle(syncedCloudNodeHandle.as8byte());
    w.serializestring(localname.platformEncoded());
    if (type == FILENODE)
    {
        if (syncedFingerprint.isvalid)
        {
            w.serializebinary((byte*)syncedFingerprint.crc.data(), sizeof(syncedFingerprint.crc));
            w.serializecompressed64(syncedFingerprint.mtime);
        }
        else
        {
            static FileFingerprint zeroFingerprint;
            w.serializebinary((byte*)zeroFingerprint.crc.data(), sizeof(zeroFingerprint.crc));
            w.serializecompressed64(zeroFingerprint.mtime);
        }
    }

    // Formerly mSyncable.
    //
    // No longer meaningful but serialized to maintain compatibility.
    w.serializebyte(1u);

    // first flag indicates we are storing slocalname.
    // Storing it is much, much faster than looking it up on startup.
    w.serializeexpansionflags(1, 1);
    auto tmpstr = slocalname ? slocalname->platformEncoded() : string();
    w.serializepstr(slocalname ? &tmpstr : nullptr);

    w.serializebool(namesSynchronized);

    return true;
}

bool LocalNode::serialize(string* d)
{
    assert(type != TYPE_UNKNOWN);

    // In fact this can occur, eg we invalidated scannedFingerprint when it was below a removed node, when an ancestor folder moved
    // Or (probably) from a node created from the cloud only
    //assert(type != FILENODE || syncedFingerprint.isvalid || scannedFingerprint.isvalid);

    // Every node we serialize should have a parent.
    assert(parent);

    // The only node with a zero DBID should be the root.
    assert(parent->dbid || !parent->parent);

#ifdef DEBUG
    if (fsid_lastSynced != UNDEF)
    {
        LocalPath localpath = getLocalPath();
        auto fa = sync->syncs.fsaccess->newfileaccess(false);
        if (fa->fopen(localpath))  // exists, is file
        {
            auto sn = sync->syncs.fsaccess->fsShortname(localpath);
            if (!(!localname.empty() &&
                ((!slocalname && (!sn || localname == *sn)) ||
                    (slocalname && sn && !slocalname->empty() && *slocalname != localname && *slocalname == *sn))))
            {
                // we can't assert here or it can cause test failures, when the LocalNode just hasn't been updated from the disk state yet.
                // but we can log ERR to try to detect any issues during development.  Occasionally there will be false positives,
                // but also please do investigate when it's not a test that got shut down while busy.
                LOG_err << "Shortname mismatch on LocalNode serialize! " <<
                           "localname: " << localname << " slocalname " << (slocalname?slocalname->toPath():"<null>") << " actual shorname " << (sn?sn->toPath():"<null") << " for path " << localpath;

            }
        }
    }
#endif

    auto parentID = parent ? parent->dbid : 0;
    auto result = LocalNodeCore::write(*d, parentID);

#ifdef DEBUG
    // Quick (de)serizliation check.
    {
        string source = *d;
        uint32_t id = 0u;

        auto node = unserialize(*sync, source, id);

        assert(node);
        assert(node->localname == localname);
        assert(!node->slocalname == !slocalname);
        assert(!node->slocalname || *node->slocalname == *slocalname);
    }
#endif

    return result;
}

bool LocalNodeCore::read(const string& source, uint32_t& parentID)
{
    if (source.size() < sizeof(m_off_t)         // type/size combo
                      + sizeof(handle)          // fsid
                      + sizeof(uint32_t)        // parent dbid
                      + MegaClient::NODEHANDLE  // handle
                      + sizeof(short))          // localname length
    {
        LOG_err << "LocalNode unserialization failed - short data";
        return false;
    }

    CacheableReader r(source);

    nodetype_t type;
    m_off_t size;

    if (!r.unserializei64(size)) return false;

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
    handle h = 0;
    string localname, shortname;
    uint64_t mtime = 0;
    int32_t crc[4];
    memset(crc, 0, sizeof crc);
    byte syncable = 1;
    unsigned char expansionflags[8] = { 0 };
    bool ns = false;

    if (!r.unserializehandle(fsid) ||
        !r.unserializeu32(parentID) ||
        !r.unserializenodehandle(h) ||
        !r.unserializestring(localname) ||
        (type == FILENODE && !r.unserializebinary((byte*)crc, sizeof(crc))) ||
        (type == FILENODE && !r.unserializecompressed64(mtime)) ||
        (r.hasdataleft() && !r.unserializebyte(syncable)) ||
        (r.hasdataleft() && !r.unserializeexpansionflags(expansionflags, 2)) ||
        (expansionflags[0] && !r.unserializecstr(shortname, false)) ||
        (expansionflags[1] && !r.unserializebool(ns)))
    {
        LOG_err << "LocalNode unserialization failed at field " << r.fieldnum;
        assert(false);
        return false;
    }
    assert(!r.hasdataleft());

    this->type = type;
    this->syncedFingerprint.size = size;
    this->fsid_lastSynced = fsid;
    this->localname = LocalPath::fromPlatformEncodedRelative(localname);
    this->slocalname.reset(shortname.empty() ? nullptr : new LocalPath(LocalPath::fromPlatformEncodedRelative(shortname)));
    this->slocalname_in_db = 0 != expansionflags[0];
    this->namesSynchronized = ns;

    memcpy(this->syncedFingerprint.crc.data(), crc, sizeof crc);

    this->syncedFingerprint.mtime = mtime;
    this->syncedFingerprint.isvalid = mtime != 0;

    // previously we scanned and created the LocalNode, but we had not set syncedFingerprint
    this->syncedCloudNodeHandle.set6byte(h);

    return true;
}

unique_ptr<LocalNode> LocalNode::unserialize(Sync& sync, const string& source, uint32_t& parentID)
{
    auto node = ::mega::make_unique<LocalNode>(&sync);

    if (!node->read(source, parentID))
        return nullptr;

    return node;
}

#ifdef USE_INOTIFY

LocalNode::WatchHandle::WatchHandle()
  : mEntry(mSentinel.end())
{
}

LocalNode::WatchHandle::~WatchHandle()
{
    operator=(nullptr);
}

auto LocalNode::WatchHandle::operator=(WatchMapIterator entry) -> WatchHandle&
{
    if (mEntry == entry) return *this;

    operator=(nullptr);
    mEntry = entry;

    return *this;
}

auto LocalNode::WatchHandle::operator=(std::nullptr_t) -> WatchHandle&
{
    if (mEntry == mSentinel.end()) return *this;

    auto& node = *mEntry->second.first;
    auto& sync = *node.sync;
    auto& notifier = static_cast<LinuxDirNotify&>(*sync.dirnotify);

    notifier.removeWatch(mEntry);
    invalidate();
    return *this;
}

void LocalNode::WatchHandle::invalidate()
{
    mEntry = mSentinel.end();
}

bool LocalNode::WatchHandle::operator==(handle fsid) const
{
    if (mEntry == mSentinel.end()) return false;

    return fsid == mEntry->second.second;
}

WatchResult LocalNode::watch(const LocalPath& path, handle fsid)
{
    // Can't add a watch if we don't have a notifier.
    if (!sync->dirnotify)
        return WR_SUCCESS;

    // Do we need to (re)create a watch?
    if (mWatchHandle == fsid)
    {
        LOG_verbose << "[" << std::this_thread::get_id() << "]"
                    << " watch for path: " << path.toPath()
                    << " with mWatchHandle == fsid == " << fsid
                    << " Already in place";
        return WR_SUCCESS;
    }

    // Get our hands on the notifier.
    auto& notifier = static_cast<LinuxDirNotify&>(*sync->dirnotify);

    // Add the watch.
    auto result = notifier.addWatch(*this, path, fsid);

    // Were we able to add the watch?
    if (result.second)
    {
        // Yup so assign the handle.
        mWatchHandle = result.first;
    }
    else
    {
        // Make sure any existing watch is invalidated.
        mWatchHandle = nullptr;
    }

    return result.second;
}

WatchMap LocalNode::WatchHandle::mSentinel;

#else // USE_INOTIFY

WatchResult LocalNode::watch(const LocalPath&, handle)
{
    // Only inotify requires us to create watches for each node.
    return WR_SUCCESS;
}

#endif // ! USE_INOTIFY

void LocalNode::clearFilters()
{
    // Only for directories.
    assert(type == FOLDERNODE);

    // Clear filter state.
    if (rareRO().filterChain)
    {
        rare().filterChain.reset();
        rare().badlyFormedIgnoreFilePath.reset();
        trimRareFields();
    }

    // Reset ignore file state.
    setRecomputeExclusionState(false);

    // Re-examine this subtree.
    setScanAgain(false, true, true, 0);
    setSyncAgain(false, true, true);
}

const FilterChain& LocalNode::filterChainRO() const
{
    static const FilterChain dummy;

    auto& filterChainPtr = rareRO().filterChain;

    if (filterChainPtr)
        return *filterChainPtr;

    return dummy;
}

bool LocalNode::loadFiltersIfChanged(const FileFingerprint& fingerprint, const LocalPath& path)
{
    // Only meaningful for directories.
    assert(type == FOLDERNODE);

    // Convenience.
    auto& filterChain = this->filterChain();

    if (filterChain.isValid() && !filterChain.changed(fingerprint))
    {
        return true;
    }

    if (filterChain.isValid())
    {
        filterChain.invalidate();
        setRecomputeExclusionState(false);
    }

    // Try and load the ignore file.
    if (FLR_SUCCESS != filterChain.load(*sync->syncs.fsaccess, path))
    {
        filterChain.invalidate();
    }

    return filterChain.isValid();
}

FilterChain& LocalNode::filterChain()
{
    auto& filterChainPtr = rare().filterChain;

    if (!filterChainPtr)
        filterChainPtr.reset(new FilterChain());

    return *filterChainPtr;
}

bool LocalNode::isExcluded(RemotePathPair namePath, nodetype_t type, bool inherited) const
{
    // This specialization only makes sense for directories.
    assert(this->type == FOLDERNODE);

    // Check whether the file is excluded by any filters.
    for (auto* node = this; node; node = node->parent)
    {
        assert(node->mExclusionState == ES_INCLUDED);

        if (node->rareRO().filterChain)
        {
            // Should we only consider inheritable filter rules?
            inherited = inherited || node != this;

            // Check for a filter match.
            auto result = node->filterChainRO().match(namePath, type, inherited);

            // Was the file matched by any filters?
            if (result.matched)
                return !result.included;
        }

        // Compute the node's cloud name.
        auto name = node->localname.toName(*sync->syncs.fsaccess);

        // Update path so that it's applicable to the next node's path filters.
        namePath.second.prependWithSeparator(name);
    }

    // File's included.
    return false;
}

bool LocalNode::isExcluded(const RemotePathPair&, m_off_t size) const
{
    // Specialization only meaningful for directories.
    assert(type == FOLDERNODE);

    // Consider files of unknown size included.
    if (size < 0)
        return false;

    // Check whether this file is excluded by any size filters.
    for (auto* node = this; node; node = node->parent)
    {
        // Sanity: We should never be called if either of these is true.
        assert(node->mExclusionState == ES_INCLUDED);

        if (node->rareRO().filterChain)
        {
            // Check for a filter match.
            auto result = node->filterChainRO().match(size);

            // Was the file matched by any filters?
            if (result.matched)
                return !result.included;
        }
    }

    // File's included.
    return false;
}

//void LocalNode::setWaitingForIgnoreFileLoad(bool pending)
//{
//    // Only meaningful for directories.
//    assert(type == FOLDERNODE);
//
//    // Do we really need to update our children?
//    if (!mWaitingForIgnoreFileLoad)
//    {
//        // Tell our children they need to recompute their state.
//        for (auto& childIt : children)
//            childIt.second->setRecomputeExclusionState();
//    }
//
//    // Apply new pending state.
//    mWaitingForIgnoreFileLoad = pending;
//}

void LocalNode::setRecomputeExclusionState(bool includingThisOne)
{
    if (includingThisOne)
    {
        mExclusionState = ES_UNKNOWN;
    }

    if (type == FILENODE)
        return;

    list<LocalNode*> pending(1, this);

    while (!pending.empty())
    {
        auto& node = *pending.front();

        for (auto& childIt : node.children)
        {
            auto& child = *childIt.second;

            if (child.mExclusionState == ES_UNKNOWN)
                continue;

            child.mExclusionState = ES_UNKNOWN;

            if (child.type == FOLDERNODE)
                pending.emplace_back(&child);
        }

        pending.pop_front();
    }
}

bool LocalNode::waitingForIgnoreFileLoad() const
{
    for (auto* node = this; node; node = node->parent)
    {
        if (node->mWaitingForIgnoreFileLoad)
            return true;
    }

    return false;
}

// Query whether a file is excluded by this node or one of its parents.
template<typename PathType>
typename std::enable_if<IsPath<PathType>::value, ExclusionState>::type
LocalNode::exclusionState(const PathType& path, nodetype_t type, m_off_t size) const
{
    // This specialization is only meaningful for directories.
    assert(this->type == FOLDERNODE);

    // We can't determine our child's exclusion state if we don't know our own.
    // Our children are excluded if we are.
    if (mExclusionState != ES_INCLUDED)
        return mExclusionState;

    // Children of unknown type still have to be handled.
    // Scan-blocked appear as TYPE_UNKNOWN and the user must be
	// able to exclude them when they are notified of them

    // Ignore files are only excluded if one of their parents is.
    if (type == FILENODE && path == IGNORE_FILE_NAME)
        return ES_INCLUDED;

    // We can't know the child's state unless our filters are current.
    if (mWaitingForIgnoreFileLoad)
        return ES_UNKNOWN;

    // Computed cloud name and relative cloud path.
    RemotePathPair namePath;

    // Current path component.
    PathType component;

    // Check if any intermediary path components are excluded.
    for (size_t index = 0; path.nextPathComponent(index, component); )
    {
        // Compute cloud name.
        namePath.first = component.toName(*sync->syncs.fsaccess);

        // Compute relative cloud path.
        namePath.second.appendWithSeparator(namePath.first, false);

        // Have we hit the final path component?
        if (!path.hasNextPathComponent(index))
            break;

        // Is this path component excluded?
        if (isExcluded(namePath, FOLDERNODE, false))
            return ES_EXCLUDED;
    }

    // Which node we should start our search from.
    auto* node = this;

    // Does the final path component represent a file?
    if (type == FILENODE)
    {
        // Ignore files are only exluded if one of their parents is.
        if (namePath.first == IGNORE_FILE_NAME)
            return ES_INCLUDED;

        // Is the file excluded by any size filters?
        if (node->isExcluded(namePath, size))
            return ES_EXCLUDED;
    }

    // Is the file excluded by any name filters?
    if (node->isExcluded(namePath, type, node != this))
        return ES_EXCLUDED;

    // File's included.
    return ES_INCLUDED;
}

// Make sure we instantiate the two types.  Jenkins gcc can't handle this in the header.
template ExclusionState LocalNode::exclusionState(const LocalPath& path, nodetype_t type, m_off_t size) const;
template ExclusionState LocalNode::exclusionState(const RemotePath& path, nodetype_t type, m_off_t size) const;

ExclusionState LocalNode::exclusionState(const string& name, nodetype_t type, m_off_t size) const
{
    assert(this->type == FOLDERNODE);

    // Consider providing a specialized implementation to avoid conversion.
    auto fsAccess = sync->syncs.fsaccess.get();
    auto fsType = sync->mFilesystemType;
    auto localname = LocalPath::fromRelativeName(name, *fsAccess, fsType);

    return exclusionState(localname, type, size);
}

ExclusionState LocalNode::exclusionState() const
{
    return mExclusionState;
}

bool LocalNode::isIgnoreFile() const
{
    return mIsIgnoreFile;
}

bool LocalNode::recomputeExclusionState()
{
    // We should never be asked to recompute the root's exclusion state.
    assert(parent);

    // Only recompute the state if it's necessary.
    if (mExclusionState != ES_UNKNOWN)
        return false;

    mExclusionState = parent->exclusionState(localname, type);

    return mExclusionState != ES_UNKNOWN;
}


void LocalNode::ignoreFilterPresenceChanged(bool present, FSNode* fsNode)
{
    // ignore file appeared or disappeared
    if (present)
    {
        // if the file is actually present locally, it'll be loaded after its syncItem()
        filterChain().invalidate();
    }
    else
    {
        rare().filterChain.reset();
    }
    setRecomputeExclusionState(false);
}

#endif // ENABLE_SYNC

void Fingerprints::newnode(Node* n)
{
    if (n->type == FILENODE)
    {
        n->fingerprint_it = mFingerprints.end();
    }
}

void Fingerprints::add(Node* n)
{
    assert(n->fingerprint_it == mFingerprints.end());
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

unique_ptr<FSNode> FSNode::fromFOpened(FileAccess& fa, const LocalPath& fullPath, FileSystemAccess& fsa)
{
    unique_ptr<FSNode> result(new FSNode);
    result->type = fa.type;
    result->fsid = fa.fsidvalid ? fa.fsid : UNDEF;
    result->isSymlink = fa.mIsSymLink;
    result->fingerprint.mtime = fa.mtime;
    result->fingerprint.size = fa.size;

    result->localname = fullPath.leafName();

    if (auto sn = fsa.fsShortname(fullPath))
    {
        if (*sn != result->localname)
        {
            result->shortname = std::move(sn);
        }
    }
    return result;
}

unique_ptr<FSNode> FSNode::fromPath(FileSystemAccess& fsAccess, const LocalPath& path)
{
    auto fileAccess = fsAccess.newfileaccess(false);

    if (!fileAccess->fopen(path, true, false))
        return nullptr;

    auto fsNode = fromFOpened(*fileAccess, path, fsAccess);

    if (fsNode->type != FILENODE)
        return fsNode;

    if (!fsNode->fingerprint.genfingerprint(fileAccess.get()))
        return nullptr;

    return fsNode;
}

CloudNode::CloudNode(const Node& n)
    : name(n.displayname())
    , type(n.type)
    , handle(n.nodeHandle())
    , parentHandle(n.parent ? n.parent->nodeHandle() : NodeHandle())
    , parentType(n.parent ? n.parent->type : TYPE_UNKNOWN)
    , fingerprint(n.fingerprint())
{
    assert(fingerprint.isvalid || type != FILENODE);
}

bool CloudNode::isIgnoreFile() const
{
    return type == FILENODE && name == IGNORE_FILE_NAME;
}

} // namespace
