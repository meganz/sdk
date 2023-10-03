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

const vector<string> Node::attributesToCopyIntoPreviousVersions{ "fav", "lbl", "sen" };

Node::Node(MegaClient& cclient, NodeHandle h, NodeHandle ph,
           nodetype_t t, m_off_t s, handle u, const char* fa, m_time_t ts)
    : client(&cclient)
{
    appdata = NULL;

    nodehandle = h.as8byte();
    parenthandle = ph.as8byte();

    parent = NULL;

#ifdef ENABLE_SYNC
    syncget = NULL;

    syncdeleted = SYNCDEL_NONE;
    todebris_it = client->toDebris.end();
    tounlink_it = client->toUnlink.end();
#endif

    type = t;

    size = s;
    owner = u;

    JSON::copystring(&fileattrstring, fa);

    ctime = ts;

    foreignkey = false;

    memset(&changed, 0, sizeof changed);

    mFingerPrintPosition = client->mNodeManager.invalidFingerprintPos();

    if (type == FILENODE)
    {
        mCounter.files = 1;
        mCounter.storage = size;
    }
    else if (type == FOLDERNODE)
    {
        mCounter.folders = 1;
    }
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

#ifdef ENABLE_SYNC
    // remove from todebris node_set
    if (todebris_it != client->toDebris.end())
    {
        client->toDebris.erase(todebris_it);
    }

    // remove from tounlink node_set
    if (tounlink_it != client->toUnlink.end())
    {
        client->toUnlink.erase(tounlink_it);
    }
#endif

#ifdef ENABLE_SYNC
    // sync: remove reference from local filesystem node
    if (localnode)
    {
        localnode->deleted = true;
        localnode.reset();
    }

    // in case this node is currently being transferred for syncing: abort transfer
    delete syncget;
#endif
}

int Node::getShareType() const
{
    int shareType = ShareType_t::NO_SHARES;

    if (inshare)
    {
        shareType |= ShareType_t::IN_SHARES;
    }
    else
    {
        if (outshares)
        {
            for (share_map::iterator it = outshares->begin(); it != outshares->end(); it++)
            {
                Share *share = it->second.get();
                if (share->user)    // folder links are shares without user
                {
                    shareType |= ShareType_t::OUT_SHARES;
                    break;
                }
            }
        }
        if (pendingshares && pendingshares->size())
        {
            shareType |= ShareType_t::PENDING_OUTSHARES;
        }
        if (plink)
        {
            shareType |= ShareType_t::LINK;
        }
    }

    return shareType;
}

bool Node::isAncestor(NodeHandle ancestorHandle) const
{
    Node* ancestor = parent;
    while (ancestor)
    {
        if (ancestor->nodeHandle() == ancestorHandle)
        {
            return true;
        }

        ancestor = ancestor->parent;
    }

    return false;
}

#ifdef ENABLE_SYNC

void Node::detach(const bool recreate)
{
    if (localnode)
    {
        localnode->detach(recreate);
    }
}

#endif // ENABLE_SYNC

bool Node::hasChildWithName(const string& name) const
{
    return client->childnodebyname(this, name.c_str()) ? true : false;
}

Node::Flags Node::getDBFlagsBitset() const
{
    Flags flags;
    flags.set(FLAGS_IS_VERSION, parent && parent->type == FILENODE);
    flags.set(FLAGS_IS_IN_RUBBISH, isAncestor(client->mNodeManager.getRootNodeRubbish()));
    flags.set(FLAGS_IS_MARKED_SENSTIVE, isMarkedSensitive());
    return flags;
}

uint64_t Node::getDBFlags() const
{
    return getDBFlagsBitset().to_ulong();
}

//static
uint64_t Node::getDBFlags(uint64_t oldFlags, bool isInRubbish, bool isVersion, bool isSensitive)
{
    Flags flags = oldFlags;
    flags.set(FLAGS_IS_VERSION, isVersion);
    flags.set(FLAGS_IS_IN_RUBBISH, isInRubbish);
    flags.set(FLAGS_IS_MARKED_SENSTIVE, isSensitive);
    return flags.to_ulong();
}

bool Node::getExtension(std::string& ext, const string& nodeName)
{
    ext.clear();
    const char* name = nodeName.c_str();
    const size_t size = strlen(name);

    const char* ptr = name + size;
    char c;

    for (unsigned i = 0; i < size; ++i)
    {
        if (*--ptr == '.')
        {
            ptr++; // Avoid add dot
            ext.reserve(i);

            unsigned j = 0;
            for (; j <= i - 1; j++)
            {
                if (*ptr < '.' || *ptr > 'z') return false;

                c = *(ptr++);

                // tolower()
                if (c >= 'A' && c <= 'Z') c |= ' ';

                ext.push_back(c);
            }

            return true;
        }
    }

    return false;
}

// these lists of file extensions (and the logic to use them) all come from the webclient - if updating here, please make sure the webclient is updated too, preferably webclient first.

const std::set<nameid>& documentExtensions()
{
    static const std::set<nameid> docs {MAKENAMEID3('a','b','w'), MAKENAMEID3('d','o','c'), MAKENAMEID4('d','o','c','m'),
                                        MAKENAMEID4('d','o','c','x'), MAKENAMEID3('d','o','t'), MAKENAMEID4('d','o','t','m'),
                                        MAKENAMEID4('d','o','t','x'), MAKENAMEID3('o','d','s'), MAKENAMEID3('o','d','t'),
                                        MAKENAMEID3('s','x','c'), MAKENAMEID3('s','x','d'), MAKENAMEID3('s','x','i'),
                                        MAKENAMEID4('t','e','x','t'), MAKENAMEID3('t','s','v'), MAKENAMEID3('t','t','l'),
                                        MAKENAMEID3('t','x','t'), MAKENAMEID3('x','l','s'), MAKENAMEID4('x','l','s','x')};
    return docs;
}

const std::set<nameid>& pdfExtensions()
{
    static const std::set<nameid> pdfs {MAKENAMEID3('p','d','f')};
    return pdfs;
}

const std::set<nameid>& presentationExtensions()
{
    static const std::set<nameid> pres {MAKENAMEID3('o','d','c'), MAKENAMEID3('o','d','p'), MAKENAMEID3('o','t','c'),
                                        MAKENAMEID3('o','t','p'), MAKENAMEID3('p','o','t'), MAKENAMEID4('p','o','t','x'),
                                        MAKENAMEID3('p','p','s'), MAKENAMEID4('p','p','s','x'), MAKENAMEID3('p','p','t'),
                                        MAKENAMEID4('p','p','t','x'), MAKENAMEID4('s','l','d','x')};
    return pres;
}

const std::set<nameid>& archiveExtensions()
{
    static const std::set<nameid> acvs {MAKENAMEID2('7','z'), MAKENAMEID3('a','c','e'), MAKENAMEID3('b','z','2'),
                                        MAKENAMEID2('g','z'), MAKENAMEID3('r','a','r'), MAKENAMEID3('t','a','r'),
                                        MAKENAMEID3('z','i','p')};
    return acvs;
}

const std::set<nameid>& programExtensions()
{
    static const std::set<nameid> pgms {MAKENAMEID3('a','p','k'), MAKENAMEID3('b','a','t'), MAKENAMEID3('c','o','m'),
                                        MAKENAMEID3('d','e','b'), MAKENAMEID3('e','x','e'), MAKENAMEID3('m','s','i'),
                                        MAKENAMEID2('s','h')};
    return pgms;
}

const std::set<nameid>& miscExtensions()
{
    static const std::set<nameid> misc {MAKENAMEID3('c','s','v'), MAKENAMEID4('j','s','o','n'), MAKENAMEID3('l','o','g'),
                                        MAKENAMEID3('o','t','f'), MAKENAMEID3('t','t','f')};
    return misc;
}

const std::set<nameid>& audioExtensions()
{
    static const std::set<nameid> auds {MAKENAMEID3('a','a','c'), MAKENAMEID3('a','d','p'), MAKENAMEID3('a','i','f'),
                                        MAKENAMEID4('a','i','f','c'), MAKENAMEID4('a','i','f','f'), MAKENAMEID2('a','u'),
                                        MAKENAMEID3('c','a','f'), MAKENAMEID3('d','r','a'), MAKENAMEID3('d','t','s'),
                                        MAKENAMEID5('d','t','s','h','d'), MAKENAMEID3('e','o','l'), MAKENAMEID4('f','l','a','c'),
                                        MAKENAMEID3('k','a','r'), MAKENAMEID3('l','v','p'), MAKENAMEID3('m','2','a'),
                                        MAKENAMEID3('m','3','a'), MAKENAMEID3('m','3','u'), MAKENAMEID3('m','4','a'),
                                        MAKENAMEID3('m','i','d'), MAKENAMEID4('m','i','d','i'), MAKENAMEID3('m','k','a'),
                                        MAKENAMEID3('m','p','2'), MAKENAMEID4('m','p','2','a'), MAKENAMEID3('m','p','3'),
                                        MAKENAMEID4('m','p','4','a'), MAKENAMEID4('m','p','g','a'), MAKENAMEID3('o','g','a'),
                                        MAKENAMEID3('o','g','g'), MAKENAMEID3('p','y','a'), MAKENAMEID2('r','a'),
                                        MAKENAMEID3('r','a','m'), MAKENAMEID3('r','i','p'), MAKENAMEID3('r','m','i'),
                                        MAKENAMEID3('r','m','p'), MAKENAMEID3('s','3','m'), MAKENAMEID3('s','i','l'),
                                        MAKENAMEID3('s','n','d'), MAKENAMEID3('s','p','x'), MAKENAMEID3('u','v','a'),
                                        MAKENAMEID4('u','v','v','a'), MAKENAMEID3('w','a','v'), MAKENAMEID3('w','a','x'),
                                        MAKENAMEID4('w','e','b','a'), MAKENAMEID3('w','m','a'), MAKENAMEID2('x','m')};
    return auds;
}

// Store extension than can't be stored in nameid due they have more than 8 characters
const std::set<std::string>& longAudioExtension()
{
    static const std::set<std::string> laud {"ecelp4800", "ecelp7470", "ecelp9600"};
    return laud;
}

const std::set<nameid>& videoExtensions()
{
    static const std::set<nameid> vids {MAKENAMEID3('3','g','2'), MAKENAMEID3('3','g','p'), MAKENAMEID3('a','s','f'),
                                        MAKENAMEID3('a','s','x'), MAKENAMEID3('a','v','i'), MAKENAMEID3('d','v','b'),
                                        MAKENAMEID3('f','4','v'), MAKENAMEID3('f','l','i'), MAKENAMEID3('f','l','v'),
                                        MAKENAMEID3('f','v','t'), MAKENAMEID4('h','2','6','1'), MAKENAMEID4('h','2','6','3'),
                                        MAKENAMEID4('h','2','6','4'), MAKENAMEID4('j','p','g','m'), MAKENAMEID4('j','p','g','v'),
                                        MAKENAMEID3('j','p','m'), MAKENAMEID3('m','1','v'), MAKENAMEID3('m','2','v'),
                                        MAKENAMEID3('m','4','u'), MAKENAMEID3('m','4','v'), MAKENAMEID3('m','j','2'),
                                        MAKENAMEID4('m','j','p','2'), MAKENAMEID4('m','k','3','d'), MAKENAMEID3('m','k','s'),
                                        MAKENAMEID3('m','k','v'), MAKENAMEID3('m','n','g'), MAKENAMEID3('m','o','v'),
                                        MAKENAMEID5('m','o','v','i','e'), MAKENAMEID3('m','p','4'), MAKENAMEID4('m','p','4','v'),
                                        MAKENAMEID3('m','p','e'), MAKENAMEID4('m','p','e','g'), MAKENAMEID3('m','p','g'),
                                        MAKENAMEID4('m','p','g','4'), MAKENAMEID3('m','x','u'), MAKENAMEID3('o','g','v'),
                                        MAKENAMEID3('p','y','v'), MAKENAMEID2('q','t'), MAKENAMEID3('s','m','v'),
                                        MAKENAMEID3('u','v','h'), MAKENAMEID3('u','v','m'), MAKENAMEID3('u','v','p'),
                                        MAKENAMEID3('u','v','s'), MAKENAMEID3('u','v','u'), MAKENAMEID3('u','v','v'),
                                        MAKENAMEID4('u','v','v','h'), MAKENAMEID4('u','v','v','m'), MAKENAMEID4('u','v','v','p'),
                                        MAKENAMEID4('u','v','v','s'), MAKENAMEID4('u','v','v','u'), MAKENAMEID4('u','v','v','v'),
                                        MAKENAMEID3('v','i','v'), MAKENAMEID3('v','o','b'), MAKENAMEID4('w','e','b','m'),
                                        MAKENAMEID2('w','m'), MAKENAMEID3('w','m','v'), MAKENAMEID3('w','m','x'),
                                        MAKENAMEID3('w','v','x')};
    return vids;
}

const std::set<nameid>& photoExtensions()
{
    static const std::set<nameid> phts {MAKENAMEID3('3','d','s'), MAKENAMEID4('a','v','i','f'), MAKENAMEID3('b','m','p'),
                                        MAKENAMEID4('b','t','i','f'), MAKENAMEID3('c','g','m'), MAKENAMEID3('c','m','x'),
                                        MAKENAMEID3('d','j','v'), MAKENAMEID4('d','j','v','u'), MAKENAMEID3('d','w','g'),
                                        MAKENAMEID3('d','x','f'), MAKENAMEID3('f','b','s'), MAKENAMEID2('f','h'),
                                        MAKENAMEID3('f','h','4'), MAKENAMEID3('f','h','5'), MAKENAMEID3('f','h','7'),
                                        MAKENAMEID3('f','h','c'), MAKENAMEID3('f','p','x'), MAKENAMEID3('f','s','t'),
                                        MAKENAMEID2('g','3'), MAKENAMEID3('g','i','f'), MAKENAMEID4('h','e','i','c'),
                                        MAKENAMEID4('h','e','i','f'), MAKENAMEID3('i','c','o'), MAKENAMEID3('i','e','f'),
                                        MAKENAMEID3('j','p','e'), MAKENAMEID4('j','p','e','g'), MAKENAMEID3('j','p','g'),
                                        MAKENAMEID3('k','t','x'), MAKENAMEID3('m','d','i'), MAKENAMEID3('m','m','r'),
                                        MAKENAMEID3('n','p','x'), MAKENAMEID3('p','b','m'), MAKENAMEID3('p','c','t'),
                                        MAKENAMEID3('p','c','x'), MAKENAMEID3('p','g','m'), MAKENAMEID3('p','i','c'),
                                        MAKENAMEID3('p','n','g'), MAKENAMEID3('p','n','m'), MAKENAMEID3('p','p','m'),
                                        MAKENAMEID3('p','s','d'), MAKENAMEID3('r','a','s'), MAKENAMEID3('r','g','b'),
                                        MAKENAMEID3('r','l','c'), MAKENAMEID3('s','g','i'), MAKENAMEID3('s','i','d'),
                                        MAKENAMEID3('s','v','g'), MAKENAMEID4('s','v','g','z'), MAKENAMEID3('t','g','a'),
                                        MAKENAMEID3('t','i','f'), MAKENAMEID4('t','i','f','f'), MAKENAMEID3('u','v','g'),
                                        MAKENAMEID3('u','v','i'), MAKENAMEID4('u','v','v','g'), MAKENAMEID4('u','v','v','i'),
                                        MAKENAMEID4('w','b','m','p'), MAKENAMEID3('w','d','p'), MAKENAMEID4('w','e','b','p'),
                                        MAKENAMEID3('x','b','m'), MAKENAMEID3('x','i','f'), MAKENAMEID3('x','p','m'),
                                        MAKENAMEID3('x','w','d')};
    return phts;
}

const std::set<nameid>& photoRawExtensions()
{
    static const std::set<nameid> phrs {MAKENAMEID3('3','f','r'), MAKENAMEID3('a','r','i'), MAKENAMEID3('a','r','q'),
                                        MAKENAMEID3('a','r','w'), MAKENAMEID3('b','a','y'), MAKENAMEID3('b','m','q'),
                                        MAKENAMEID3('c','a','p'), MAKENAMEID4('c','i','f','f'), MAKENAMEID4('c','i','n','e'),
                                        MAKENAMEID3('c','r','2'), MAKENAMEID3('c','r','3'), MAKENAMEID3('c','r','w'),
                                        MAKENAMEID3('c','s','1'), MAKENAMEID3('d','c','2'), MAKENAMEID3('d','c','r'),
                                        MAKENAMEID3('d','n','g'), MAKENAMEID3('d','r','f'), MAKENAMEID3('d','s','c'),
                                        MAKENAMEID3('e','i','p'), MAKENAMEID3('e','r','f'), MAKENAMEID3('f','f','f'),
                                        MAKENAMEID2('i','a'), MAKENAMEID3('i','i','q'), MAKENAMEID3('k','2','5'),
                                        MAKENAMEID3('k','c','2'), MAKENAMEID3('k','d','c'), MAKENAMEID3('m','d','c'),
                                        MAKENAMEID3('m','e','f'), MAKENAMEID3('m','o','s'), MAKENAMEID3('m','r','w'),
                                        MAKENAMEID3('n','e','f'), MAKENAMEID3('n','r','w'), MAKENAMEID3('o','b','m'),
                                        MAKENAMEID3('o','r','f'), MAKENAMEID3('o','r','i'), MAKENAMEID3('p','e','f'),
                                        MAKENAMEID3('p','t','x'), MAKENAMEID3('p','x','n'), MAKENAMEID3('q','t','k'),
                                        MAKENAMEID3('r','a','f'), MAKENAMEID3('r','a','w'), MAKENAMEID3('r','d','c'),
                                        MAKENAMEID3('r','w','2'), MAKENAMEID3('r','w','l'), MAKENAMEID3('r','w','z'),
                                        MAKENAMEID3('s','r','2'), MAKENAMEID3('s','r','f'), MAKENAMEID3('s','r','w'),
                                        MAKENAMEID3('s','t','i'), MAKENAMEID3('x','3','f')};
    return phrs;
}

const std::set<nameid>& photoImageDefExtension()
{
    static const std::set<nameid> phis {MAKENAMEID3('b','m','p'), MAKENAMEID3('g','i','f'), MAKENAMEID3('j','p','g'),
                                        MAKENAMEID4('j','p','e','g'), MAKENAMEID3('p','n','g')};
    return phis;
}

bool Node::isPhoto(const std::string& ext)
{
    nameid extNameid = getExtensionNameId(ext);
    return photoImageDefExtension().find(extNameid) != photoImageDefExtension().end() ||
        photoRawExtensions().find(extNameid) != photoRawExtensions().end() ||
        photoExtensions().find(extNameid) != photoExtensions().end();
}

bool Node::isPhotoWithFileAttributes(bool checkPreview) const
{
    std::string ext;
    if (!Node::getExtension(ext, displayname()))
    {
        return false;
    }

    // evaluate according to the webclient rules, so that we get exactly the same bucketing.
    return (isPhoto(ext)
            && (!checkPreview || Node::hasfileattribute(&fileattrstring, GfxProc::PREVIEW)));
}

bool Node::isVideo(const std::string& ext)
{
    return videoExtensions().find(getExtensionNameId(ext)) != videoExtensions().end();
}

bool Node::isVideoWithFileAttributes() const
{
    std::string ext;
    if (!Node::getExtension(ext, displayname()))
    {
        return false;
    }

    if (Node::hasfileattribute(&fileattrstring, fa_media) && nodekey().size() == FILENODEKEYLENGTH)
    {
#ifdef USE_MEDIAINFO
        if (client->mediaFileInfo.mediaCodecsReceived)
        {
            MediaProperties mp = MediaProperties::decodeMediaPropertiesAttributes(fileattrstring, (uint32_t*)(nodekey().data() + FILENODEKEYLENGTH / 2));
            unsigned videocodec = mp.videocodecid;
            if (!videocodec && mp.shortformat)
            {
                auto& v = client->mediaFileInfo.mediaCodecs.shortformats;
                if (mp.shortformat < v.size())
                {
                    videocodec = v[mp.shortformat].videocodecid;
                }
            }
            // approximation: the webclient has a lot of logic to determine if a particular codec is playable in that browser.  We'll just base our decision on the presence of a video codec.
            if (!videocodec)
            {
                return false; // otherwise double-check by extension
            }
        }
#endif
    }

    return isVideo(ext);
}

bool Node::isAudio(const std::string& ext)
{
    nameid extNameid = getExtensionNameId(ext);
    if (extNameid != 0)
    {
        return audioExtensions().find(extNameid) != audioExtensions().end();
    }

    // Check longer extension
    return longAudioExtension().find(ext) != longAudioExtension().end();
}

bool Node::isDocument(const std::string& ext)
{
    return documentExtensions().find(getExtensionNameId(ext)) != documentExtensions().end() || isPdf(ext) || isPresentation(ext);
}

bool Node::isPdf(const std::string& ext)
{
    return pdfExtensions().find(getExtensionNameId(ext)) != pdfExtensions().end();
}

bool Node::isPresentation(const std::string& ext)
{
    return presentationExtensions().find(getExtensionNameId(ext)) != presentationExtensions().end();
}

bool Node::isArchive(const std::string& ext)
{
    return archiveExtensions().find(getExtensionNameId(ext)) != archiveExtensions().end();
}

bool Node::isProgram(const std::string& ext)
{
    return programExtensions().find(getExtensionNameId(ext)) != programExtensions().end();
}

bool Node::isMiscellaneous(const std::string& ext)
{
    return miscExtensions().find(getExtensionNameId(ext)) != miscExtensions().end();
}

bool Node::isOfMimetype(MimeType_t mimetype, const string& ext)
{
    switch (mimetype) {
    case MimeType_t::MIME_TYPE_PHOTO:
        return Node::isPhoto(ext);
    case MimeType_t::MIME_TYPE_AUDIO:
        return Node::isAudio(ext);
    case MimeType_t::MIME_TYPE_VIDEO:
        return Node::isVideo(ext);
    case MimeType_t::MIME_TYPE_DOCUMENT:
        return Node::isDocument(ext);
    case MimeType_t::MIME_TYPE_PDF:
        return Node::isPdf(ext);
    case MimeType_t::MIME_TYPE_PRESENTATION:
        return Node::isPresentation(ext);
    case MimeType_t::MIME_TYPE_ARCHIVE:
        return Node::isArchive(ext);
    case MimeType_t::MIME_TYPE_PROGRAM:
        return Node::isProgram(ext);
    case MimeType_t::MIME_TYPE_MISC:
        return Node::isMiscellaneous(ext);
    default:
        return false;
    }
}

nameid Node::getExtensionNameId(const std::string& ext)
{
    if (ext.length() > 8)
    {
        return 0;
    }

    JSON json;
    return json.getnameid(ext.c_str());
}

// update node key data from JSON
void Node::setkeyfromjson(const char* k)
{
    string tmp;
    JSON::copystring(&tmp, k);
    setKey(tmp);
}

// update node key (already decrypted) and attempt to decrypt attributes
void Node::setkey(const byte* newkey)
{
    if (newkey)
    {
        string tmp(reinterpret_cast<const char*>(newkey), (type == FILENODE) ? FILENODEKEYLENGTH : FOLDERNODEKEYLENGTH);
        setKey(tmp);
    }

    setattr();
}

// set the node key (encrypted or decrypted)
void Node::setKey(const string& key)
{
    if (keyApplied()) --client->mAppliedKeyNodeCount;
    nodekeydata = key;
    if (keyApplied()) ++client->mAppliedKeyNodeCount;
    assert(client->mAppliedKeyNodeCount >= 0);
}

Node *Node::unserialize(MegaClient& client, const std::string *d, bool fromOldCache, std::list<std::unique_ptr<NewShare>>& ownNewshares)
{
    handle h, ph;
    nodetype_t t;
    m_off_t s;
    handle u;
    string nodekey;
    const char* fa;
    m_time_t ts;
    const byte* skey;
    const char* ptr = d->data();
    const char* end = ptr + d->size();
    unsigned short ll;
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

        nodekey.assign(ptr, keylen);
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

    if (ptr + (unsigned)*ptr > end)
    {
        return nullptr;
    }

    auto encrypted = *ptr && ptr[1];

    ptr += (unsigned)*ptr + 1;

    for (i = 4; i--;)
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

    unique_ptr<Node> n(new Node(client, NodeHandle().set6byte(h), NodeHandle().set6byte(ph), t, s, u, fa, ts));

    // read inshare, outshares, or pending shares
    while (numshares)   // inshares: -1, outshare/s: num_shares
    {
        int direction = (numshares > 0) ? -1 : 0;
        std::unique_ptr<NewShare> newShare(Share::unserialize(direction, h, skey, &ptr, end));

        if (!newShare)
        {
            LOG_err << "Failed to unserialize Share";
            break;
        }

        if (fromOldCache)
        {
            // mergenewshare should be called when users and pcr are loaded
            // It's used only when we are migrating the cache
            // mergenewshares is called when all nodes, user and pcr are loaded (fetchsc)
            client.newshares.push_back(newShare.release());
        }
        else
        {
            ownNewshares.push_back(std::move(newShare));
        }

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
        LOG_err << "Failed to unserialize attrs";
        assert(false);
        return NULL;
    }

    if (fromOldCache)
    {
        // It's needed to re-normalize node names because
        // the updated version of utf8proc doesn't provide
        // exactly the same output as the previous one that
        // we were using
        attr_map::iterator it = n->attrs.map.find('n');
        if (it != n->attrs.map.end())
        {
            LocalPath::utf8_normalize(&(it->second));
        }
    }
    // else from new cache, names has been normalized before to store in DB

    if (isExported)
    {
        if (ptr + MegaClient::NODEHANDLE + sizeof(m_time_t) + sizeof(bool) > end)
        {
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

        n->plink.reset(new PublicLink(ph, cts, ets, takendown, authKey ? authKey : ""));
    }

    if (encrypted)
    {
        // Have we encoded the node key data's length?
        if (ptr + sizeof(uint32_t) > end)
        {
            return nullptr;
        }

        auto length = MemAccess::get<uint32_t>(ptr);
        ptr += sizeof(length);

        // Have we encoded the node key data?
        if (ptr + length > end)
        {
            return nullptr;
        }

        nodekey.assign(ptr, length);
        ptr += length;

        // Have we encoded the length of the attribute string?
        if (ptr + sizeof(uint32_t) > end)
        {
            return nullptr;
        }

        length = MemAccess::get<uint32_t>(ptr);
        ptr += sizeof(length);

        // Have we encoded the attribute string?
        if (ptr + length > end)
        {
            return nullptr;
        }

        n->attrstring.reset(new string(ptr, length));
        ptr += length;
    }

    n->setKey(nodekey); // it can be decrypted or encrypted

    if (!encrypted)
    {
        // only if the node is not encrypted, we can generate a valid
        // fingerprint, based on the node's attribute 'c'
        n->setfingerprint();
    }

    if (ptr == end)
    {
        return n.release();
    }
    else
    {
        return NULL;
    }
}

// serialize node - nodes with pending or RSA keys are unsupported
bool Node::serialize(string* d) const
{
    switch (type)
    {
        case FILENODE:
            if (!attrstring && (int)nodekeydata.size() != FILENODEKEYLENGTH)
            {
                return false;
            }
            break;

        case FOLDERNODE:
            if (!attrstring && (int)nodekeydata.size() != FOLDERNODEKEYLENGTH)
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
    time_t ts = 0;  // we don't want to break backward compatibility by changing the size (where m_time_t differs)
    d->append((char*)&ts, sizeof(ts));

    ts = (time_t)ctime;
    d->append((char*)&ts, sizeof(ts));

    if (attrstring)
    {
        auto length = 0u;

        if (type == FOLDERNODE)
        {
            length = FOLDERNODEKEYLENGTH;
        }
        else if (type == FILENODE)
        {
            length = FILENODEKEYLENGTH;
        }

        d->append(length, '\0');
    }
    else
    {
        d->append(nodekeydata);
    }

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

    d->append(1, static_cast<char>(!!attrstring));

    if (attrstring)
    {
        d->append(1, '\1');
    }

    // Use these bytes for extensions.
    d->append(4, '\0');

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
        if (sharekey)
        {
            d->append((char*)sharekey->key, SymmCipher::KEYLENGTH);
        }
        else // with ^!keys, shares may not receive the sharekey along with the share's data
        {
            d->append(SymmCipher::KEYLENGTH, '\0');
        }

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

    // Write data necessary to thaw encrypted nodes.
    if (attrstring)
    {
        // Write node key data.
        uint32_t length = static_cast<uint32_t>(nodekeydata.size());
        d->append((char*)&length, sizeof(length));
        d->append(nodekeydata, 0, length);

        // Write attribute string data.
        length = static_cast<uint32_t>(attrstring->size());
        d->append((char*)&length, sizeof(length));
        d->append(*attrstring, 0, length);
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

            fingerprint = it->second;
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
        changed.sensitive = attrs.hasDifferentValue(AttrMap::string2nameid("sen"), oldAttrs.map);

        setfingerprint();

        delete[] buf;

        attrstring.reset();
    }
}

nameid Node::sdsId()
{
    constexpr nameid nid = MAKENAMEID3('s', 'd', 's');
    return nid;
}

bool Node::isMarkedSensitive() const
{
    return attrs.getBool("sen");
}

bool Node::isSensitiveInherited() const
{
    if (isMarkedSensitive())
        return true;
    Node* p = parent;
    while (p)
    {
        if (p->isMarkedSensitive())
        {
            return true;
        }
        p = p->parent;
    }

    return false;
}

bool Node::areFlagsValid(Node::Flags requiredFlags, Node::Flags excludeFlags, Node::Flags excludeRecursiveFlags) const
{
    if (excludeRecursiveFlags.any() && anyExcludeRecursiveFlag(excludeRecursiveFlags))
        return false;
    if (requiredFlags.any() || excludeFlags.any())
    {
        Node::Flags flags = getDBFlagsBitset();
        if ((flags & excludeFlags).any())
            return false;
        if ((flags & requiredFlags) != requiredFlags)
            return false;
    }
    return true;
}

bool Node::anyExcludeRecursiveFlag(Node::Flags excludeRecursiveFlags) const
{
    if ((getDBFlagsBitset() & excludeRecursiveFlags).any())
        return true;

    const Node* p = parent;
    while (p)
    {
        Node::Flags flags = p->getDBFlagsBitset();
        if ((excludeRecursiveFlags & flags).any())
        {
            return true;
        }
        p = p->parent;
    }

    return false;
}

vector<pair<handle, int>> Node::getSdsBackups() const
{
    vector<pair<handle, int>> bkps;

    auto it = attrs.map.find(sdsId());
    if (it != attrs.map.end())
    {
        std::istringstream is(it->second);  // "b64aa:8,b64bb:8"
        while (!is.eof())
        {
            string b64BkpIdStr;
            std::getline(is, b64BkpIdStr, ':');
            if (!is.good())
            {
                LOG_err << "Invalid format in 'sds' attr value for backup id";
                break;
            }
            handle bkpId = UNDEF;
            Base64::atob(b64BkpIdStr.c_str(), (byte*)&bkpId, MegaClient::BACKUPHANDLE);
            assert(bkpId != UNDEF);

            string stateStr;
            std::getline(is, stateStr, ',');
            try
            {
                int state = std::stoi(stateStr);
                bkps.push_back(std::make_pair(bkpId, state));
            }
            catch (...)
            {
                LOG_err << "Invalid backup state in 'sds' attr value";
                break;
            }
        }
    }

    return bkps;
}

string Node::toSdsString(const vector<pair<handle, int>>& ids)
{
    string value;

    for (const auto& i : ids)
    {
        std::string idStr(Base64Str<MegaClient::BACKUPHANDLE>(i.first));
        value += idStr + ':' + std::to_string(i.second) + ','; // `b64aa:8,b64bb:8,`
    }

    if (!value.empty())
    {
        value.pop_back(); // remove trailing ','
    }

    return value;
}

// if present, configure FileFingerprint from attributes
// otherwise, the file's fingerprint is derived from the file's mtime/size/key
void Node::setfingerprint()
{
    if (type == FILENODE && nodekeydata.size() >= sizeof crc)
    {
        client->mNodeManager.removeFingerprint(this);

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

        mFingerPrintPosition = client->mNodeManager.insertFingerprint(this);
    }
}

bool Node::hasName(const string& name) const
{
    auto it = attrs.map.find('n');
    return it != attrs.map.end() && it->second == name;
}

bool Node::hasName() const
{
    auto i = attrs.map.find('n');

    return i != attrs.map.end() && !i->second.empty();
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

        case VAULTNODE:
            path.insert(0, "//in");
            return path;

        case ROOTNODE:
            return path.empty() ? "/" : path;

        case RUBBISHNODE:
            path.insert(0, "//bin");
            return path;

        case TYPE_DONOTSYNC:
        case TYPE_SPECIAL:
        case TYPE_UNKNOWN:
        case FILENODE:
            path.insert(0, n->displayname());
        }
        path.insert(0, "/");
    }
    return path;
}

bool Node::isIncludedForMimetype(MimeType_t mimetype, bool checkPreview) const
{
    if (type != FILENODE)
    {
        return false;
    }

    if (mimetype == MimeType_t::MIME_TYPE_PHOTO)
    {
        return isPhotoWithFileAttributes(checkPreview);
    }

    std::string extension;
    if (!getExtension(extension, displayname()))
    {
        return false;
    }

    return Node::isOfMimetype(mimetype, extension);
}

bool isPhotoVideoAudioByName(const string& filenameExtensionLowercaseNoDot)
{
    nameid id = filenameExtensionLowercaseNoDot.length() > 8
              ? 0 : AttrMap::string2nameid(filenameExtensionLowercaseNoDot.c_str());

    if (id)
    {
        if (photoImageDefExtension().find(id) != photoImageDefExtension().end()) return true;
        if (photoRawExtensions().find(id) != photoRawExtensions().end()) return true;
        if (photoExtensions().find(id) != photoExtensions().end()) return true;
        if (videoExtensions().find(id) != videoExtensions().end()) return true;
        if (audioExtensions().find(id) != audioExtensions().end()) return true;
    }

    if (longAudioExtension().find(filenameExtensionLowercaseNoDot) != longAudioExtension().end()) return true;
    return false;
}

// returns position of file attribute or 0 if not present
int Node::hasfileattribute(fatype t) const
{
    return Node::hasfileattribute(&fileattrstring, t);
}

int Node::hasfileattribute(const string *fileattrstring, fatype t)
{
    char buf[24];

    snprintf(buf, sizeof(buf), ":%u*", t);
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
    handle me = client->loggedIntoFolder() ? client->mNodeManager.getRootNodeFiles().as8byte() : client->me;

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
            // look for share key if not folder link access with folder master key
            if (h != me)
            {
                // this is a share node handle - check if share key is available
                if (client->mKeyManager.isSecure() && client->mKeyManager.generation())
                {
                    std::string key = client->mKeyManager.getShareKey(h);
                    if (key.size())
                    {
                        sc = client->getRecycledTemporaryNodeCipher(&key);
                    }
                    else
                    {
                        continue;
                    }
                }
                else // check at new keys repository and, if not found, at the root node of share
                {
                    auto it = client->mNewKeyRepository.find(NodeHandle().set6byte(h));
                    if (it == client->mNewKeyRepository.end())
                    {
                        Node* n;
                        if (!(n = client->nodebyhandle(h)) || !n->sharekey)
                        {
                            continue;
                        }

                        sc = n->sharekey.get();
                    }
                    else
                    {
                        sc = client->getRecycledTemporaryNodeCipher(it->second.data());
                    }
                }

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
        std::string undecryptedKey = nodekeydata;
        client->mAppliedKeyNodeCount++;
        nodekeydata.assign((const char*)key, keylength);
        setattr();
        if (attrstring)
        {
            if (foreignkey)
            {
                // Decryption with a foreign share key failed.
                // Restoring the undecrypted node key because an updated
                // share key can be received later.
                client->mAppliedKeyNodeCount--;
                nodekeydata = undecryptedKey;
            }
            LOG_warn << "Failed to decrypt attributes for node: " << toNodeHandle(nodehandle);
        }
    }

    bool applied = keyApplied();
    if (!applied)
    {
        LOG_warn << "Failed to apply key for node: " << Base64Str<MegaClient::NODEHANDLE>(nodehandle);
        // keys could be missing due to nested inshares with multiple users: user A shares a folder 1
        // with user B and folder 1 has a subfolder folder 1_1. User A shares folder 1_1 with user C
        // and user C adds some files, which will be undecryptable for user B.
        // The ticket SDK-1959 aims to mitigate the problem. Uncomment next line when done:
        // assert(applied);
        // This can also happen due to a race condition between the creation / destruction of shares
        // the retrieval keys using "pk" and the update of the "^!keys" attribute.
        // If a folder is shared / unshared / shared, it's possible to reach this code with the old share
        // key, that could be in "mNewKeyRepository" (not in n->sharekey because Node::testShareKey is
        // used to prevent it).
    }

    return applied;
}

bool Node::testShareKey(const byte *shareKey)
{
    if (keyApplied() || !attrstring)
    {
        return true;
    }

    std::string mark = toNodeHandle(nodehandle) + ":";
    size_t p = nodekeydata.find(mark);
    if (p == string::npos)
    {
        return true;
    }

    byte key[FILENODEKEYLENGTH];
    unsigned keylength = (type == FILENODE) ? FILENODEKEYLENGTH : FOLDERNODEKEYLENGTH;
    const char* k = nodekeydata.c_str() + p + mark.size();
    SymmCipher *sc = client->getRecycledTemporaryNodeCipher(shareKey);
    if (!client->decryptkey(k, key, keylength, sc, 0, UNDEF))
    {
        // This should never happen (malformed key)
        LOG_err << "Malformed node key detected";
        assert(false);
        return true; // The share key could be OK
    }

    sc = client->getRecycledTemporaryNodeCipher(key);
    byte* buf = Node::decryptattr(sc, attrstring->c_str(), attrstring->size());
    if (!buf)
    {
        LOG_warn << "Outdated / incorrect share key detected for " << toNodeHandle(nodehandle);
        return false;
    }

    delete [] buf;
    return true;
}

NodeCounter Node::getCounter() const
{
    return mCounter;
}

void Node::setCounter(const NodeCounter &counter)
{
    mCounter = counter;
}

// returns whether node was moved
bool Node::setparent(Node* p, bool updateNodeCounters)
{
    if (p == parent)
    {
        return false;
    }

    Node *oldparent = parent;
    if (oldparent)
    {
        client->mNodeManager.removeChild(oldparent, nodeHandle());
    }

    parenthandle = p ? p->nodehandle : UNDEF;
    parent = p;
    if (parent)
    {
        client->mNodeManager.addChild(parent->nodeHandle(), nodeHandle(), this);
    }

    if (updateNodeCounters)
    {
        client->mNodeManager.updateCounter(*this, oldparent);
    }

#ifdef ENABLE_SYNC
    // 'updateNodeCounters' is false when node is loaded from DB. In that case, we want to skip the
    // processing by TreeProcDelSyncGet, since the node won't have a valid SyncFileGet yet.
    // (this is important, since otherwise the whole tree beneath this node will be loaded in
    // result of the proctree())
    if (updateNodeCounters)
    {
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
                TransferDbCommitter committer(client->tctable); // potentially stopping many transfers here
                TreeProcDelSyncGet tdsg;
                client->proctree(this, &tdsg);
            }
        }
    }

    if (oldparent && oldparent->localnode)
    {
        oldparent->localnode->treestate(oldparent->localnode->checkstate());
    }
#endif

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
        plink.reset(new PublicLink(ph, cts, ets, takendown, authKey.empty() ? nullptr : authKey.c_str()));
    }
    else            // update
    {
        plink->ph = ph;
        plink->cts = cts;
        plink->ets = ets;
        plink->takendown = takendown;
        plink->mAuthKey = authKey;
    }
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
void LocalNode::setnameparent(LocalNode* newparent, const LocalPath* newlocalpath, std::unique_ptr<LocalPath> newshortname)
{
    if (!sync)
    {
        LOG_err << "LocalNode::init() was never called";
        assert(false);
        return;
    }

    bool newnode = getLocalname().empty();
    Node* todelete = NULL;
    int nc = 0;
    Sync* oldsync = NULL;
    bool canChangeVault = sync->isBackup() || (newparent && newparent->sync && newparent->sync->isBackup());

    assert(!newparent || newparent->node || newnode);

    if (parent)
    {
        // remove existing child linkage
        parent->children.erase(getLocalname());

        if (slocalname)
        {
            parent->schildren.erase(*slocalname);
            slocalname.reset();
        }
    }

    if (newlocalpath)
    {
        // extract name component from localpath, check for rename unless newnode
        size_t p = newlocalpath->getLeafnameByteIndex();

        // has the name changed?
        if (!newlocalpath->backEqual(p, getLocalname()))
        {
            // set new name
            setLocalname(newlocalpath->subpathFrom(p));
            name = getLocalname().toName(*sync->syncs.fsaccess);

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
                        sync->client->app->syncupdate_treestate(sync->getConfig(), getLocalPath(), ts, type);
                    }

                    string prevname = node->attrs.map['n'];

                    // set new name
                    auto client = sync->client;
                    sync->client->setattr(node, attr_map('n', name),
                        [prevname, client](NodeHandle h, Error e){
                            if (!e)
                            {
                                if (Node* node = client->nodeByHandle(h))
                                {
                                    // After speculative instant completion removal, this is not needed (always sent via actionpacket code)
                                    LOG_debug << "Sync - remote rename from " << prevname << " to " << node->displayname();
                                }
                            }
                        },
                        canChangeVault);
                }
            }
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

            if (!newnode && node)
            {
                sync->client->nextreqtag(); //make reqtag advance to use the next one
                LOG_debug << "Moving node: " << node->displaypath() << " to " << parent->node->displaypath();
                if (sync->client->rename(node, parent->node, SYNCDEL_NONE, node->parent ? node->parent->nodeHandle() : NodeHandle(), nullptr, canChangeVault, nullptr) == API_EACCESS
                        && sync != parent->sync)
                {
                    LOG_debug << "Rename not permitted. Using node copy/delete";

                    // save for deletion
                    todelete = node;
                }

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
        parent->children[getLocalname()] = this;

        if (newshortname && *newshortname != getLocalname())
        {
            slocalname = std::move(newshortname);
            parent->schildren[*slocalname] = this;
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
            bool immediatecreation = nc == (int) (sync->client->synccreateForVault.size()
                                                + sync->client->synccreateGeneral.size());

            sync->client->syncupdate();

            // try to keep nodes in syncdebris if they can't be immediately created
            // to avoid uploads
            sync->client->movetosyncdebris(todelete, immediatecreation || oldsync->inshare, sync->isBackup());
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

LocalNode::LocalNode(Sync* csync)
: sync(csync)
, deleted{false}
, created{false}
, reported{false}
, checked{false}
, needsRescan(false)
{}

// initialize fresh LocalNode object - must be called exactly once
void LocalNode::init(nodetype_t ctype, LocalNode* cparent, const LocalPath& cfullpath, std::unique_ptr<LocalPath> shortname)
{
    parent = NULL;
    node.reset();
    notseen = 0;
    deleted = false;
    created = false;
    reported = false;
    needsRescan = false;
    syncxfer = true;
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
        setnameparent(cparent, &cfullpath, std::move(shortname));
    }
    else
    {
        setLocalname(cfullpath);
        slocalname.reset(shortname && *shortname != cfullpath ? shortname.release() : nullptr);
        name = cfullpath.toPath(true);
    }

    scanseqno = sync->scanseqno;

    // mark fsid as not valid
    fsid_it = sync->client->fsidnode.end();

    // enable folder notification
    if (type == FOLDERNODE && sync->dirnotify)
    {
        sync->dirnotify->addnotify(this, cfullpath);
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
        sync->client->app->syncupdate_treestate(sync->getConfig(), getLocalPath(), ts, type);
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
    deleted = false;

    node.reset();
    if (cnode)
    {
        cnode->localnode.reset();
        node.crossref(cnode, this);
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

    if (!sync->mDestructorRunning && (
        sync->state() == SYNC_ACTIVE || sync->state() == SYNC_INITIALSCAN))
    {
        sync->statecachedel(this);

        if (type == FOLDERNODE)
        {
            LOG_debug << "Sync - local folder deletion detected: " << getLocalPath();
        }
        else
        {
            LOG_debug << "Sync - local file deletion detected: " << getLocalPath();
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
        delete it++->second;
    }

    if (node && !sync->mDestructorRunning)
    {
        // move associated node to SyncDebris unless the sync is currently
        // shutting down
        if (sync->state() >= SYNC_INITIALSCAN)
        {
            sync->client->movetosyncdebris(node, sync->inshare, sync->isBackup());
        }
    }
}

void LocalNode::detach(const bool recreate)
{
    // Never detach the sync root.
    if (parent && node)
    {
        node.reset();
        created &= !recreate;
    }
}

void LocalNode::setSubtreeNeedsRescan(bool includeFiles)
{
    assert(type != FILENODE);

    needsRescan = true;

    for (auto& child : children)
    {
        if (child.second->type != FILENODE)
        {
            child.second->setSubtreeNeedsRescan(includeFiles);
        }
        else
        {
            child.second->needsRescan |= includeFiles;
        }
    }
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
        path.prependWithSeparator(l->getLocalname());
    }
}

string LocalNode::debugGetParentList()
{
    string s;

    for (const LocalNode* l = this; l != nullptr; l = l->parent)
    {
        s += l->getLocalname().toPath(false) + "(" + std::to_string((long long)(void*)l) + ") ";
    }
    return s;
}

// locate child by localname or slocalname
LocalNode* LocalNode::childbyname(LocalPath* localname)
{
    localnode_map::iterator it;

    if (!localname || ((it = children.find(*localname)) == children.end() && (it = schildren.find(*localname)) == schildren.end()))
    {
        return NULL;
    }

    return it->second;
}

void LocalNode::prepare(FileSystemAccess&)
{
    getlocalpath(transfer->localfilename);
    assert(transfer->localfilename.isAbsolute());


    // is this transfer in progress? update file's filename.
    if (transfer->slot && transfer->slot->fa && !transfer->slot->fa->nonblocking_localname.empty())
    {
        transfer->slot->fa->updatelocalname(transfer->localfilename, false);
    }

    treestate(TREESTATE_SYNCING);
}

void LocalNode::terminated(error e)
{
    sync->threadSafeState->transferFailed(PUT, size);

    File::terminated(e);
}

// complete a sync upload: complete to //bin if a newer node exists (which
// would have been caused by a race condition)
void LocalNode::completed(Transfer* t, putsource_t source)
{
    sync->threadSafeState->transferComplete(PUT, size);

    // complete to rubbish for later retrieval if the parent node does not
    // exist or is newer
    if (!parent || !parent->node || (node && mtime < node->mtime))
    {
        h = t->client->mNodeManager.getRootNodeRubbish();
    }
    else
    {
        // otherwise, overwrite node if it already exists and complete in its
        // place
        h = parent->node->nodeHandle();
    }

    bool canChangeVault = sync->isBackup();

    // we are overriding completed() for sync upload, we don't use the File::completed version at all.
    assert(t->type == PUT);
    sendPutnodesOfUpload(t->client, t->uploadhandle, *t->ultoken, t->filekey, source, NodeHandle(), nullptr, this, nullptr, canChangeVault);
}

// serialize/unserialize the following LocalNode properties:
// - type/size
// - fsid
// - parent LocalNode's dbid
// - corresponding Node handle
// - local name
// - fingerprint crc/mtime (filenodes only)
bool LocalNode::serialize(string* d) const
{
    CacheableWriter w(*d);
    w.serializei64(type ? -type : size);
    w.serializehandle(fsid);
    w.serializeu32(parent ? parent->dbid : 0);
    w.serializenodehandle(node ? node->nodehandle : UNDEF);
    w.serializestring(getLocalname().platformEncoded());
    if (type == FILENODE)
    {
        w.serializebinary((byte*)crc.data(), sizeof(crc));
        w.serializecompressedi64(mtime);
    }
    w.serializebyte(mSyncable);
    w.serializeexpansionflags(1);  // first flag indicates we are storing slocalname.  Storing it is much, much faster than looking it up on startup.
    auto tmpstr = slocalname ? slocalname->platformEncoded() : string();
    w.serializepstr(slocalname ? &tmpstr : nullptr);

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
    m_time_t mtime = 0;
    int32_t crc[4];
    memset(crc, 0, sizeof crc);
    byte syncable = 1;
    unsigned char expansionflags[8] = { 0 };

    if (!r.unserializehandle(fsid) ||
        !r.unserializeu32(parent_dbid) ||
        !r.unserializenodehandle(h) ||
        !r.unserializestring(localname) ||
        (type == FILENODE && !r.unserializebinary((byte*)crc, sizeof(crc))) ||
        (type == FILENODE && !r.unserializecompressedi64(mtime)) ||
        (r.hasdataleft() && !r.unserializebyte(syncable)) ||
        (r.hasdataleft() && !r.unserializeexpansionflags(expansionflags, 1)) ||
        (expansionflags[0] && !r.unserializecstr(shortname, false)))
    {
        LOG_err << "LocalNode unserialization failed at field " << r.fieldnum;
        return nullptr;
    }
    assert(!r.hasdataleft());

    LocalNode* l = new LocalNode(sync);

    l->type = type;
    l->size = size;

    l->parent_dbid = parent_dbid;

    l->fsid = fsid;
    l->fsid_it = sync->client->fsidnode.end();

    l->setLocalname(LocalPath::fromPlatformEncodedRelative(localname));
    l->slocalname.reset(shortname.empty() ? nullptr : new LocalPath(LocalPath::fromPlatformEncodedRelative(shortname)));
    l->slocalname_in_db = 0 != expansionflags[0];
    l->name = l->getLocalname().toName(*sync->syncs.fsaccess);

    memcpy(l->crc.data(), crc, sizeof crc);
    l->mtime = mtime;
    l->isvalid = true;

    l->node.store_unchecked(sync->client->nodebyhandle(h));
    l->parent = nullptr;
    l->sync = sync;
    l->mSyncable = syncable == 1;

    // FIXME: serialize/unserialize
    l->created = false;
    l->reported = false;
    l->checked = h != UNDEF; // TODO: Is this a bug? h will never be UNDEF
    l->needsRescan = false;

    return l;
}

#endif // ENABLE_SYNC

void NodeCounter::operator += (const NodeCounter& o)
{
    storage += o.storage;
    files += o.files;
    folders += o.folders;
    versions += o.versions;
    versionStorage += o.versionStorage;
}

void NodeCounter::operator -= (const NodeCounter& o)
{
    storage -= o.storage;
    files -= o.files;
    folders -= o.folders;
    versions -= o.versions;
    versionStorage -= o.versionStorage;
}

std::string NodeCounter::serialize() const
{
    std::string nodeCountersBlob;
    CacheableWriter w(nodeCountersBlob);
    w.serializeu32(static_cast<uint32_t>(files));
    w.serializeu32(static_cast<uint32_t>(folders));
    w.serializei64(storage);
    w.serializeu32(static_cast<uint32_t>(versions));
    w.serializei64(versionStorage);

    return nodeCountersBlob;
}

NodeCounter::NodeCounter(const std::string &blob)
{
    CacheableReader r(blob);
    if (blob.size() == 28) // 4 + 4 + 8 + 4 + 8
    {
        uint32_t auxFiles;
        uint32_t auxFolders;
        uint32_t auxVersions;
        if (!r.unserializeu32(auxFiles) || !r.unserializeu32(auxFolders)
                || !r.unserializei64(storage) || !r.unserializeu32(auxVersions)
                || !r.unserializei64(versionStorage))
        {
            LOG_err << "Failure to unserialize node counter";
            assert(false);
            return;
        }

        files = auxFiles;
        folders = auxFolders;
        versions = auxVersions;

    }
    // During internal testing, 'files', 'folders' and 'versions' were stored as 'size_t', whose size is platform-dependent
    // -> in some machines it is 8 bytes, in others is 4 bytes. With the only goal of providing backwards compatibility for
    // internal testers, if the blob doesn't have expected size (using 4 bytes), check if size matches the expected using 8 bytes
    else if (blob.size() == 40)  // 8 + 8 + 8 + 8 + 8
    {
        uint64_t auxFiles;
        uint64_t auxFolders;
        uint64_t auxVersions;
        if (!r.unserializeu64(auxFiles) || !r.unserializeu64(auxFolders)
                || !r.unserializei64(storage) || !r.unserializeu64(auxVersions)
                || !r.unserializei64(versionStorage))
        {
            LOG_err << "Failure to unserialize node counter (files, folders and versions uint64_t)";
            assert(false);
            return;
        }

        files = static_cast<size_t>(auxFiles);
        folders = static_cast<size_t>(auxFolders);
        versions = static_cast<size_t>(auxVersions);
    }
    else
    {
        LOG_err << "Invalid size at node counter unserialization";
        assert(false);
    }
}

} // namespace
