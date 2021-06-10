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
    syncdeleted = SYNCDEL_NONE;
    todebris_it = client->todebris.end();
    tounlink_it = client->tounlink.end();
#endif

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

    if (t >= ROOTNODE && t <= RUBBISHNODE)
    {
        client->rootnodes[t - ROOTNODE] = h;
    }

    // set parent linkage or queue for delayed parent linkage in case of
    // out-of-order delivery
    if ((p = client->nodebyhandle(ph, true)))
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

        const Node* fa = firstancestor();
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
}

/// it's a nice idea but too simple - what about cases where the key is missing or just not available yet (and hence attrs not decrypted yet), etc
//string Node::name() const
//{
//    auto it = attrs.map.find('n');
//
//    if (it != attrs.map.end())
//    {
//        return it->second;
//    }
//
//    return "";
//}

//bool Node::syncable(const LocalNode& parent) const
//{
//    // Not syncable if we're deleted.
//    if (syncdeleted != SYNCDEL_NONE)
//    {
//        return false;
//    }
//
//    // Not syncable if we aren't decrypted.
//    if (attrstring)
//    {
//        return false;
//    }
//
//    auto it = attrs.map.find('n');
//
//    // Not syncable if we don't have a valid name.
//    if (it == attrs.map.end() || it->second.empty())
//    {
//        return false;
//    }
//
//    // We're syncable if we're not the sync debris.
//    return parent.parent || parent.sync->debris != it->second;
//}


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

    const Node *originalancestor = firstancestor();
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

    parent = p;

    if (parent)
    {
        //LOG_info << "moving " << Base64Str<MegaClient::NODEHANDLE>(nodehandle) << " " << attrs.map['n'] << " into " << Base64Str<MegaClient::NODEHANDLE>(parent->nodehandle) << " " << parent->attrs.map['n'];
        child_it = parent->children.insert(parent->children.end(), this);
    }

    const Node* newancestor = firstancestor();
    handle nah = newancestor->nodehandle;
    if (nah == client->rootnodes[0] || nah == client->rootnodes[1] || nah == client->rootnodes[2] || newancestor->inshare)
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
// set, change or remove LocalNode's parent and name/localname/slocalname.
// newlocalpath must be a full path and must not point to an empty string.
// no shortname allowed as the last path component.
void LocalNode::setnameparent(LocalNode* newparent, const LocalPath* newlocalpath, std::unique_ptr<LocalPath> newshortname, bool applyToCloud)
{

    assert(!applyToCloud);

    if (!sync)
    {
        LOG_err << "LocalNode::init() was never called";
        assert(false);
        return;
    }

    //bool newnode = localname.empty();
    Node* todelete = NULL;
    int nc = 0;
    Sync* oldsync = NULL;

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

            //if (node && applyToCloud)
            //{
            //    if (!node->hasName(name))
            //    {
            //        if (node->type == FILENODE)
            //        {
            //            treestate(TREESTATE_SYNCING);
            //        }
            //        else
            //        {
            //            sync->client->app->syncupdate_treestate(this);
            //        }

            //        // queue command to set new name
            //        sync->client->setattr(node, attr_map('n', name), sync->tag);
            //    }
            //}
        }
    }

    if (parent && parent != newparent && !sync->mDestructorRunning)
    {
        treestate(TREESTATE_NONE);
    }

    if (newparent)
    {
        if (newparent != parent)
        {
            parent = newparent;

            //if (!newnode && node && applyToCloud)
            //{
            //    assert(parent->node);

/*                if(!parent->node)
                {
                    LOG_err << node->displayname() << " parent Localnode is missing its associated Node. Cross referenced must have been removed";

                    sync->client->syncs.disableSelectedSyncs([&](SyncConfig& c, Sync* s) {
                        return s == sync;
                    }, MISSING_PARENT_NODE, false);

                    sync->client->sendevent(99455,"Disabling sync after null parent->node cross referent", 0);
                    return;
                }*/

            //    int creqtag = sync->client->reqtag;
            //    sync->client->reqtag = sync->tag;
            //    LOG_debug << "Moving node: " << node->displayname() << " to " << parent->node->displayname();
            //    if (sync->client->rename(node, parent->node, SYNCDEL_NONE, node->parent ? node->parent->nodehandle : UNDEF) == API_EACCESS
            //            && sync != parent->sync)
            //    {
            //        LOG_debug << "Rename not permitted. Using node copy/delete";

            //        // save for deletion
            //        todelete = node;
            //    }
            //    sync->client->reqtag = creqtag;

            //    if (type == FILENODE)
            //    {
            //        ts = TREESTATE_SYNCING;
            //    }
            //}

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

        //if (todelete)
        //{
        //    // complete the copy/delete operation
        //    dstime nds = NEVER;
        //    size_t numPending = 0;
        //    sync->client->syncup(parent, &nds, numPending, true);

        //    // check if nodes can be immediately created
        //    bool immediatecreation = (int) sync->client->synccreate.size() == nc;

        //    sync->client->syncupdate();

        //    // try to keep nodes in syncdebris if they can't be immediately created
        //    // to avoid uploads
        //    sync->client->movetosyncdebris(todelete, immediatecreation || oldsync->inshare);
        //}

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

void LocalNode::moveContentTo(LocalNode* ln, LocalPath& fullPath, bool setScanAgain)
{
    vector<LocalNode*> workingList;
    workingList.reserve(children.size());
    for (auto& c : children) workingList.push_back(c.second);
    for (auto& c : workingList)
    {
        ScopedLengthRestore restoreLen(fullPath);
        fullPath.appendWithSeparator(c->localname, true);
        c->setnameparent(ln, &fullPath, sync->client->fsaccess->fsShortname(fullPath), false);
        if (setScanAgain)
        {
            c->setScanAgain(false, true, true, 0);
        }
    }

    ln->upload = move(upload);
    ln->download = move(download);

    LocalTreeProcUpdateTransfers tput;
    tput.proc(sync->client, ln);
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
: unstableFsidAssigned(false)
, deletingCloud{false}
, deletedFS{false}
, moveSourceAppliedToCloud(false)
, moveSourceApplyingToCloud(false)
, moveTargetApplyingToCloud(false)
, moveAppliedToLocal(false)
, moveApplyingToLocal(false)
, conflicts(TREE_RESOLVED)
, scanAgain(TREE_RESOLVED)
, checkMovesAgain(TREE_RESOLVED)
, syncAgain(TREE_RESOLVED)
, parentSetCheckMovesAgain(false)
, parentSetSyncAgain(false)
, parentSetScanAgain(false)
, parentSetContainsConflicts(false)
, fsidReused(false)
, scanInProgress(false)
, scanObsolete(false)
, useBlocked(TREE_RESOLVED)
, scanBlocked(TREE_RESOLVED)
{}

// initialize fresh LocalNode object - must be called exactly once
void LocalNode::init(Sync* csync, nodetype_t ctype, LocalNode* cparent, const LocalPath& cfullpath, std::unique_ptr<LocalPath> shortname)
{
    sync = csync;
    parent = NULL;
//    notseen = 0;
    unstableFsidAssigned = false;
    deletingCloud = false;
    deletedFS = false;
    moveSourceAppliedToCloud = false;
    moveSourceApplyingToCloud = false;
    moveTargetApplyingToCloud = false;
    moveAppliedToLocal = false;
    moveApplyingToLocal = false;
    conflicts = TREE_RESOLVED;
    scanAgain = TREE_RESOLVED;
    checkMovesAgain = TREE_RESOLVED;
    syncAgain = TREE_RESOLVED;
    parentSetCheckMovesAgain = false;
    parentSetSyncAgain = false;
    parentSetScanAgain = false;
    parentSetContainsConflicts = false;
    fsidReused = false;
    scanInProgress = false;
    scanObsolete = false;
    useBlocked = TREE_RESOLVED;
    scanBlocked = TREE_RESOLVED;
    newnode.reset();
    parent_dbid = 0;
    slocalname = NULL;

    ts = TREESTATE_NONE;
    dts = TREESTATE_NONE;

    type = ctype;
    syncid = sync->client->nextsyncid();

    bumpnagleds();

    if (cparent)
    {
        setnameparent(cparent, &cfullpath, std::move(shortname), false);
    }
    else
    {
        localname = cfullpath;
        slocalname.reset(shortname && *shortname != localname ? shortname.release() : nullptr);
        name = localname.toName(*sync->client->fsaccess);
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

//    scanseqno = sync->scanseqno;

    // mark fsid as not valid
    fsid_it = sync->client->localnodeByFsid.end();
    syncedCloudNodeHandle_it = sync->client->localnodeByNodeHandle.end();

    sync->client->syncactivity = true;

    sync->client->totalLocalNodes++;

    if (type != TYPE_UNKNOWN)
    {
        sync->localnodes[type]++;
    }
}

void LocalNode::init(const FSNode& fsNode)
{
    // Must have been previously initialized.
    assert(sync);

    // Node we're initializing from must not be unknown.
    assert(fsNode.type != TYPE_UNKNOWN);

    // Must have been created to represent a blocked node.
    assert(type == TYPE_UNKNOWN);

    // Have we gained a shortname?
    if (fsNode.shortname && fsNode.localname != *fsNode.shortname)
    {
        // Unknown nodes shouldn't have a prior shortname.
        assert(!slocalname);

        slocalname.reset(new LocalPath(*fsNode.shortname));

        // Unknown nodes should never be the root.
        assert(parent);

        // Link us to our parent.
        parent->schildren[slocalname.get()] = this;
    }

    // Update our fingerprint.
    syncedFingerprint = fsNode.fingerprint;

    // Update our FSID.
    setfsid(fsNode.fsid, sync->client->localnodeByFsid, fsNode.localname);

    // Update our type.
    type = fsNode.type;

    // Update node counts.
    ++sync->localnodes[type];
}

auto LocalNode::rare() -> RareFields&
{
    if (!rareFields)
    {
        rareFields.reset(new RareFields);
    }
    return *rareFields;
}

void LocalNode::trimRareFields()
{
    if (rareFields)
    {
        if (useBlocked < TREE_ACTION_HERE) rareFields->useBlockedTimer.reset();
        if (scanBlocked < TREE_ACTION_HERE) rareFields->scanBlockedTimer.reset();
        if (!scanInProgress) rareFields->scanRequest.reset();

        if (!rareFields->useBlockedTimer &&
            !rareFields->scanBlockedTimer &&
            !rareFields->scanRequest)
        {
            rareFields.reset();
        }
    }
}

void LocalNode::setScanAgain(bool doParent, bool doHere, bool doBelow, dstime delayds)
{
    if (doHere)
    {
        scanObsolete = true;
    }

    unsigned state = (doHere?1u:0u) << 1 | (doBelow?1u:0u);
    if (state >= TREE_ACTION_HERE && delayds > 0)
    {
        scanDelayUntil = std::max<dstime>(scanDelayUntil,  Waiter::ds + delayds);
    }

    scanAgain = std::max<unsigned>(scanAgain, state);
    for (auto p = parent; p != NULL; p = p->parent)
    {
        p->scanAgain = std::max<unsigned>(p->scanAgain, TREE_DESCENDANT_FLAGGED);
    }

    // for scanning, we only need to set the parent once
    if (parent && doParent)
    {
        parent->scanAgain = std::max<unsigned>(parent->scanAgain, TREE_ACTION_HERE);
        doParent = false;
        parentSetScanAgain = false;
    }
    parentSetScanAgain = parentSetScanAgain || doParent;
}

void LocalNode::setCheckMovesAgain(bool doParent, bool doHere, bool doBelow)
{
    unsigned state = (doHere?1u:0u) << 1 | (doBelow?1u:0u);

    checkMovesAgain = std::max<unsigned>(checkMovesAgain, state);
    for (auto p = parent; p != NULL; p = p->parent)
    {
        p->checkMovesAgain = std::max<unsigned>(p->checkMovesAgain, TREE_DESCENDANT_FLAGGED);
    }

    parentSetCheckMovesAgain = parentSetCheckMovesAgain || doParent;
}

void LocalNode::setSyncAgain(bool doParent, bool doHere, bool doBelow)
{
    unsigned state = (doHere?1u:0u) << 1 | (doBelow?1u:0u);

    syncAgain = std::max<unsigned>(syncAgain, state);
    for (auto p = parent; p != NULL; p = p->parent)
    {
        p->syncAgain = std::max<unsigned>(p->syncAgain, TREE_DESCENDANT_FLAGGED);
    }

    parentSetSyncAgain = parentSetSyncAgain || doParent;
}

void LocalNode::setContainsConflicts(bool doParent, bool doHere, bool doBelow)
{
    // using the 3 flags for consistency & understandabilty but doBelow is not relevant
    assert(!doBelow);

    unsigned state = (doHere?1u:0u) << 1 | (doBelow?1u:0u);

    conflicts = std::max<unsigned>(conflicts, state);
    for (auto p = parent; p != NULL; p = p->parent)
    {
        p->conflicts = std::max<unsigned>(p->conflicts, TREE_DESCENDANT_FLAGGED);
    }

    parentSetContainsConflicts = parentSetContainsConflicts || doParent;
}

void LocalNode::setUseBlocked()
{
    useBlocked = std::max<unsigned>(useBlocked, TREE_ACTION_HERE);

    if (!rare().useBlockedTimer)
    {
        rare().useBlockedTimer.reset(new BackoffTimer(sync->client->rng));
    }
    if (rare().useBlockedTimer->armed())
    {
        rare().useBlockedTimer->backoff(Sync::SCANNING_DELAY_DS);
    }

    for (auto p = parent; p != NULL; p = p->parent)
    {
        p->useBlocked = std::max<unsigned>(p->useBlocked, TREE_DESCENDANT_FLAGGED);
    }
}

void LocalNode::setScanBlocked()
{
    scanBlocked = std::max<unsigned>(scanBlocked, TREE_ACTION_HERE);

    for (auto p = parent; p != NULL; p = p->parent)
    {
        p->scanBlocked = std::max<unsigned>(p->scanBlocked, TREE_DESCENDANT_FLAGGED);
    }
}

bool LocalNode::checkForScanBlocked(FSNode* fsNode)
{
    if (scanBlocked >= TREE_ACTION_HERE)
    {
        // Have we recovered?
        if (type == TYPE_UNKNOWN && fsNode && fsNode->type != TYPE_UNKNOWN)
        {
            LOG_verbose << "Recovered from being scan blocked: " << localnodedisplaypath(*sync->client->fsaccess);
            init(*fsNode);
            scanBlocked = TREE_RESOLVED;
            rare().scanBlockedTimer->reset();
            return false;
        }

        // rescan if the timer is up
        if (rare().scanBlockedTimer->armed())
        {
            LOG_verbose << "Scan blocked timer elapsed, trigger parent rescan: "  << localnodedisplaypath(*sync->client->fsaccess);;
            if (parent) parent->setScanAgain(false, true, false, 0);
            rare().scanBlockedTimer->backoff(); // wait increases exponentially
            return true;
        }
        else
        {
            LOG_verbose << "Waiting on scan blocked timer, retry in ds: "
                << rare().scanBlockedTimer->retryin();
            return true;
        }
    }

    if (fsNode && (fsNode->type == TYPE_UNKNOWN || fsNode->isBlocked))
    {
        // We were not able to get details of the filesystem item when scanning the directory.
        // Consider it a blocked file, and we'll rescan the folder from time to time.
        LOG_verbose << "File/folder was blocked when reading directory, retry later: " << localnodedisplaypath(*sync->client->fsaccess);
        setScanBlocked();
        rare().scanBlockedTimer.reset(new BackoffTimer(sync->client->rng));
        rare().scanBlockedTimer->backoff(Sync::SCANNING_DELAY_DS);
        return true;
    }

    return false;
}


bool LocalNode::scanRequired() const
{
    return scanAgain != TREE_RESOLVED;
}

bool LocalNode::mightHaveMoves() const
{
    return checkMovesAgain != TREE_RESOLVED;
}

bool LocalNode::syncRequired() const
{
    return syncAgain != TREE_RESOLVED;
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


// set fsid - assume that an existing assignment of the same fsid is no longer current and revoke
void LocalNode::setfsid(handle newfsid, fsid_localnode_map& fsidnodes, const LocalPath& fsName)
{
    assert(sync && sync->client);

    if (fsid_it != fsidnodes.end())
    {
        if (newfsid == fsid && localname == fsName)
        {
            return;
        }

        fsidnodes.erase(fsid_it);
    }

    fsid = newfsid;
    fsidReused = false;

    // if synced to fs, localname should match exactly (no differences in case/escaping etc)
    localname = fsName;

    if (fsid == UNDEF)
    {
        fsid_it = fsidnodes.end();
    }
    else
    {
        fsid_it = fsidnodes.insert(std::make_pair(fsid, this));
    }

    assert(localname.empty() || name.empty() || (!parent && parent_dbid == UNDEF) || parent_dbid == 0 ||
        0 == compareUtf(localname, true, name, false, true));
}

void LocalNode::setSyncedNodeHandle(NodeHandle h, const string& cloudName)
{
    assert(sync && sync->client);

    if (syncedCloudNodeHandle_it != sync->client->localnodeByNodeHandle.end())
    {
        if (h == syncedCloudNodeHandle && name == cloudName)
        {
            return;
        }

        assert(syncedCloudNodeHandle_it->first == syncedCloudNodeHandle);

        LOG_verbose << sync->client->clientname << "removing syned handle " << syncedCloudNodeHandle << " for " << localnodedisplaypath(*sync->client->fsaccess);

        sync->client->localnodeByNodeHandle.erase(syncedCloudNodeHandle_it);
    }

    syncedCloudNodeHandle = h;

    // if synced to cloud, name should match exactly (no differences in case/escaping etc)
    name = cloudName;

    if (syncedCloudNodeHandle == UNDEF)
    {
        syncedCloudNodeHandle_it = sync->client->localnodeByNodeHandle.end();
    }
    else
    {
        LOG_verbose << sync->client->clientname << "adding syned handle " << syncedCloudNodeHandle << " for " << localnodedisplaypath(*sync->client->fsaccess);

        syncedCloudNodeHandle_it = sync->client->localnodeByNodeHandle.insert(std::make_pair(syncedCloudNodeHandle, this));
    }

    assert(localname.empty() || name.empty() || (!parent && parent_dbid == UNDEF) || parent_dbid == 0 ||
        0 == compareUtf(localname, true, name, false, true));
}

LocalNode::~LocalNode()
{
    if (!sync)
    {
        LOG_err << "LocalNode::init() was never called";
        assert(false);
        return;
    }

    if (!sync->mDestructorRunning && (
        sync->state == SYNC_ACTIVE || sync->state == SYNC_INITIALSCAN))
    {
        sync->statecachedel(this);
    }

//    setnotseen(0);

    newnode.reset();

    if (sync->dirnotify.get())
    {
        // deactivate corresponding notifyq records
        sync->dirnotify->fsEventq.replaceLocalNodePointers(this, (LocalNode*)~0);
        sync->dirnotify->fsDelayedNetworkEventq.replaceLocalNodePointers(this, (LocalNode*)~0);
    }

    // remove from fsidnode map, if present
    if (fsid_it != sync->client->localnodeByFsid.end())
    {
        sync->client->localnodeByFsid.erase(fsid_it);
    }
    if (syncedCloudNodeHandle_it != sync->client->localnodeByNodeHandle.end())
    {
        sync->client->localnodeByNodeHandle.erase(syncedCloudNodeHandle_it);
    }

    sync->client->totalLocalNodes--;

    if (type != TYPE_UNKNOWN)
    {
        sync->localnodes[type]--;    // todo: make sure we are not using the larger types and overflowing the buffer
    }

    // remove parent association
    if (parent)
    {
        setnameparent(NULL, NULL, NULL, false);
    }

    for (localnode_map::iterator it = children.begin(); it != children.end(); )
    {
        delete it++->second;
    }

    //if (node)
    //{
    //    // move associated node to SyncDebris unless the sync is currently
    //    // shutting down
    //    if (node->localnode == this)
    //    {
    //        node->localnode = NULL;
    //    }
    //}
}

//void LocalNode::conflictDetected(const TREESTATE conflicts)
//{
//    this->conflicts = conflicts;
//    conflictRefresh();
//}
//
//void LocalNode::conflictDetected()
//{
//    conflicts |= TREE_ACTION_HERE;
//    conflictRefresh();
//}
//
//void LocalNode::conflictRefresh()
//{
//    if (conflicts == TREE_RESOLVED)
//    {
//        return;
//    }
//
//    for (auto* node = parent; node; node = node->parent)
//    {
//        if (node->conflictsDetectedBelow())
//        {
//            return;
//        }
//
//        node->conflicts |= TREE_DESCENDANT_FLAGGED;
//    }
//}
//
bool LocalNode::conflictsDetected() const
{
    return conflicts != TREE_RESOLVED;
}
//
//bool LocalNode::conflictsDetectedBelow() const
//{
//    return conflicts & TREE_DESCENDANT_FLAGGED;
//}
//
//bool LocalNode::conflictsDetectedHere() const
//{
//    return conflicts & TREE_ACTION_HERE;
//}
//
//void LocalNode::conflictsResolved()
//{
//    conflicts = TREE_RESOLVED;
//}

//void LocalNode::detach(const bool recreate)
//{
//    // Never detach the root node.
//    if (parent && node)
//    {
//        node->localnode = nullptr;
//        node->tag = sync->tag;
//        node = nullptr;
//
//        created &= !recreate;
//    }
//}

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
    path.erase();

    for (const LocalNode* l = this; l != nullptr; l = l->parent)
    {
        assert(!l->parent || l->parent->sync == sync);

        // sync root has absolute path, the rest are just their leafname
        path.prependWithSeparator(l->localname);
    }
}

string LocalNode::localnodedisplaypath(FileSystemAccess& fsa) const
{
    LocalPath local;
    getlocalpath(local);
    return local.toPath(fsa);
}

string LocalNode::getCloudPath() const
{
    string path;

    for (const LocalNode* l = this; l != nullptr; l = l->parent)
    {
        assert(!l->parent || l->parent->sync == sync);

        // sync root has absolute path, the rest are just their leafname
        path = l->name + "/" + path;
    }
    if (auto cr = sync->cloudRoot())
    {
        path = cr->displaypath() + "/" + path;
    }
    return path;
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

FSNode LocalNode::getKnownFSDetails()
{
    FSNode n;
    n.localname = localname;
    n.name = name;
    n.shortname = slocalname ? make_unique<LocalPath>(*slocalname): nullptr;
    n.type = type;
    n.fsid = fsid;
    n.isSymlink = false;  // todo: store localndoes for symlinks but don't use them?
    n.fingerprint = syncedFingerprint;
    return n;
}


LocalNode::Upload::Upload(LocalNode& ln, FSNode& details, NodeHandle targetFolder, const LocalPath& fullPath)
    : localNode(ln)
{
    *static_cast<FileFingerprint*>(this) = details.fingerprint;

    // normalized name (UTF-8 with unescaped special chars)
    // todo: we did unescape them though?
    name = details.name;

    // setting the full path means it works like a normal non-sync transfer
    localname = fullPath;

    h = targetFolder;

    hprivate = false;
    hforeign = false;
    syncxfer = true;
    temporaryfile = false;
    chatauth = nullptr;
    transfer = nullptr;
    tag = 0;

    localNode.sync->mUnifiedSync.mNextHeartbeat->adjustTransferCounts(1, 0, size, 0);
}

void LocalNode::Upload::prepare()
{
    localNode.getlocalpath(transfer->localfilename);

    // is this transfer in progress? update file's filename.
    if (transfer->slot && transfer->slot->fa && !transfer->slot->fa->nonblocking_localname.empty())
    {
        transfer->slot->fa->updatelocalname(transfer->localfilename, false);
    }

    localNode.treestate(TREESTATE_SYNCING);
}

void LocalNode::Upload::terminated()
{
    localNode.sync->mUnifiedSync.mNextHeartbeat->adjustTransferCounts(-1, 0, size, 0);

    File::terminated();
}

// complete a sync upload: complete to //bin if a newer node exists (which
// would have been caused by a race condition)
void LocalNode::Upload::completed(Transfer* t, LocalNode*)
{
    localNode.sync->mUnifiedSync.mNextHeartbeat->adjustTransferCounts(-1, 0, 0, size);

    // in case this LocalNode was moved around (todo: since we don't actually move, make sure to copy internals such as upload transfers)

    h = NodeHandle();

    if (localNode.parent && !localNode.parent->syncedCloudNodeHandle.isUndef())
    {
        if (Node* p = localNode.sync->client->nodeByHandle(localNode.parent->syncedCloudNodeHandle))
        {
            h = p->nodeHandle();
        }
    }

    if (h.isUndef())
    {
        h.set6byte(t->client->rootnodes[RUBBISHNODE - ROOTNODE]);
    }

    File::completed(t, &localNode);
    localNode.upload.reset(); // deletes this object!
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
#ifdef DEBUG
    if (fsid != UNDEF)
    {
        LocalPath localpath = getLocalPath();
        auto fa = sync->client->fsaccess->newfileaccess(false);
        if (fa->fopen(localpath))  // exists, is file
        {
            auto sn = sync->client->fsaccess->fsShortname(localpath);
            assert(!localname.empty() &&
                ((!slocalname && (!sn || localname == *sn)) ||
                    (slocalname && sn && !slocalname->empty() && *slocalname != localname && *slocalname == *sn)));
        }
    }
#endif

    assert(type != TYPE_UNKNOWN);

    CacheableWriter w(*d);
    w.serializei64(type ? -type : syncedFingerprint.size);
    w.serializehandle(fsid);
    w.serializeu32(parent ? parent->dbid : 0);
    w.serializenodehandle(syncedCloudNodeHandle.as8byte());
    w.serializestring(localname.platformEncoded());
    if (type == FILENODE)
    {
        w.serializebinary((byte*)syncedFingerprint.crc.data(), sizeof(syncedFingerprint.crc));
        w.serializecompressed64(syncedFingerprint.mtime);
    }
    w.serializebyte(mSyncable);
    w.serializeexpansionflags(1);  // first flag indicates we are storing slocalname.  Storing it is much, much faster than looking it up on startup.
    auto tmpstr = slocalname ? slocalname->platformEncoded() : string();
    w.serializepstr(slocalname ? &tmpstr : nullptr);

#ifdef DEBUG
    // just check deserializing, real quick, only in debug
    string testread = w.dest;
    LocalNode* test = LocalNode::unserialize(sync, &testread);
    assert(test->localname == localname);
    assert((test->slocalname && slocalname) || (!test->slocalname && !slocalname));
    assert(!test->slocalname || *test->slocalname == *slocalname);
#endif

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
    l->syncedFingerprint.size = size;

    l->parent_dbid = parent_dbid;

    l->fsid = fsid;
    l->fsid_it = sync->client->localnodeByFsid.end();

    l->localname = LocalPath::fromPlatformEncoded(localname);
    l->slocalname.reset(shortname.empty() ? nullptr : new LocalPath(LocalPath::fromPlatformEncoded(shortname)));
    l->slocalname_in_db = 0 != expansionflags[0];
    l->name = l->localname.toName(*sync->client->fsaccess);

    memcpy(l->syncedFingerprint.crc.data(), crc, sizeof crc);
    l->syncedFingerprint.mtime = mtime;
    l->syncedFingerprint.isvalid = true;

    l->syncedCloudNodeHandle.set6byte(h);
    l->syncedCloudNodeHandle_it = sync->client->localnodeByNodeHandle.end();

//    l->node = sync->client->nodebyhandle(h);
    l->parent = nullptr;
    l->sync = sync;
    l->mSyncable = syncable == 1;

    return l;
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
    auto& notifier = static_cast<PosixDirNotify&>(*sync.dirnotify);

    notifier.removeWatch(mEntry);
    mEntry = mSentinel.end();
}

bool LocalNode::WatchHandle::operator==(handle fsid) const
{
    if (mEntry == mSentinel.end()) return false;

    return fsid == mEntry->second.second;
}

bool LocalNode::watch(const LocalPath& path, handle fsid)
{
    // Do we need to (re)create a watch?
    if (mWatchHandle == fsid) return true;

    // Get our hands on the notifier.
    auto& notifier = static_cast<PosixDirNotify&>(*sync->dirnotify);

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

bool LocalNode::watch(const LocalPath&, handle)
{
    // Only inotify requires us to create watches for each node.
    return true;
}

#endif // ! USE_INOTIFY

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

} // namespace
