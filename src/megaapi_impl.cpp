/**
 * @file megaapi_impl.cpp
 * @brief Private implementation of the intermediate layer for the MEGA C++ SDK.
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

#define _LARGE_FILES

#define _GNU_SOURCE 1
#define _FILE_OFFSET_BITS 64

#define USE_VARARGS
#define PREFER_STDARG
#include "megaapi_impl.h"
#include "megaapi.h"
#include "mega/mediafileattribute.h"

#include <iomanip>
#include <algorithm>
#include <functional>
#include <cctype>
#include <locale>
#include <thread>

#ifndef _WIN32
#ifndef _LARGEFILE64_SOURCE
    #define _LARGEFILE64_SOURCE
#endif
#include <signal.h>
#endif


#ifdef __APPLE__
    #include <xlocale.h>
    #include <strings.h>

    #if TARGET_OS_IPHONE
    #include <netdb.h>
    #include <resolv.h>
    #include <arpa/inet.h>
    #endif
#endif

#ifdef _WIN32
#ifndef WINDOWS_PHONE
#include <shlwapi.h>
#endif

#endif

#ifdef USE_OPENSSL
#include <openssl/rand.h>
#endif

#include "mega/mega_zxcvbn.h"

namespace {

#ifdef ENABLE_SYNC
std::vector<std::string> regExpToVector(mega::MegaRegExp* regExp)
{
    std::vector<std::string> regExps;
    if (regExp)
    {
        for (int i = 0; i < regExp->getNumRegExp(); ++i)
        {
            regExps.push_back(regExp->getRegExp(i));
        }
    }
    return regExps;
}
#endif

}

namespace mega {

MegaNodePrivate::MegaNodePrivate(const char *name, int type, int64_t size, int64_t ctime, int64_t mtime, uint64_t nodehandle,
                                 string *nodekey, string *attrstring, string *fileattrstring, const char *fingerprint, const char *originalFingerprint, MegaHandle owner, MegaHandle parentHandle,
                                 const char *privateauth, const char *publicauth, bool ispublic, bool isForeign, const char *chatauth)
: MegaNode()
{
    this->name = MegaApi::strdup(name);
    this->fingerprint = MegaApi::strdup(fingerprint);
    this->originalfingerprint = MegaApi::strdup(originalFingerprint);
    this->customAttrs = NULL;
    this->duration = -1;
    this->width = -1;
    this->height = -1;
    this->shortformat = -1;
    this->videocodecid = -1;
    this->latitude = INVALID_COORDINATE;
    this->longitude = INVALID_COORDINATE;
    this->type = type;
    this->size = size;
    this->ctime = ctime;
    this->mtime = mtime;
    this->nodehandle = nodehandle;
    this->parenthandle = parentHandle;
    this->attrstring.assign(attrstring->data(), attrstring->size());
    this->fileattrstring.assign(fileattrstring->data(), fileattrstring->size());
    this->nodekey.assign(nodekey->data(), nodekey->size());
    this->changed = 0;
    this->thumbnailAvailable = (Node::hasfileattribute(fileattrstring, GfxProc::THUMBNAIL) != 0);
    this->previewAvailable = (Node::hasfileattribute(fileattrstring, GfxProc::PREVIEW) != 0);
    this->tag = 0;
    this->isPublicNode = ispublic;
    this->outShares = false;
    this->inShare = false;
    this->plink = NULL;
    this->mNewLinkFormat = false;
    this->sharekey = NULL;
    this->foreign = isForeign;
    this->children = NULL;
    this->owner = owner;

    if (privateauth)
    {
        this->privateAuth = privateauth;
    }

    if (publicauth)
    {
        this->publicAuth = publicauth;
    }

    this->chatAuth = chatauth ? MegaApi::strdup(chatauth) : NULL;

#ifdef ENABLE_SYNC
    this->syncdeleted = false;
#endif
}

MegaNodePrivate::MegaNodePrivate(MegaNode *node)
: MegaNode()
{
    this->name = MegaApi::strdup(node->getName());
    this->fingerprint = MegaApi::strdup(node->getFingerprint());
    this->originalfingerprint = MegaApi::strdup(node->getOriginalFingerprint());
    this->customAttrs = NULL;

    MegaNodePrivate *np = dynamic_cast<MegaNodePrivate *>(node);
    if (np)
    {
        this->duration = np->duration;
        this->width = np->width;
        this->height = np->height;
        this->shortformat = np->shortformat;
        this->videocodecid = np->videocodecid;
    }
    else
    {
        this->duration = node->getDuration();
        this->width = node->getWidth();
        this->height = node->getHeight();
        this->shortformat = node->getShortformat();
        this->videocodecid = node->getVideocodecid();
    }

    this->latitude = node->getLatitude();
    this->longitude = node->getLongitude();
    this->restorehandle = node->getRestoreHandle();
    this->type = node->getType();
    this->size = node->getSize();
    this->ctime = node->getCreationTime();
    this->mtime = node->getModificationTime();
    this->nodehandle = node->getHandle();
    this->parenthandle = node->getParentHandle();
    string * attrstring = node->getAttrString();
    this->attrstring.assign(attrstring->data(), attrstring->size());
    char* fileAttributeString = node->getFileAttrString();
    if (fileAttributeString)
    {
        this->fileattrstring = std::string(fileAttributeString);
        delete [] fileAttributeString;
    }
    string *nodekey = node->getNodeKey();
    this->nodekey.assign(nodekey->data(),nodekey->size());
    this->changed = node->getChanges();
    this->thumbnailAvailable = node->hasThumbnail();
    this->previewAvailable = node->hasPreview();
    this->tag = node->getTag();
    this->isPublicNode = node->isPublic();
    this->privateAuth = *node->getPrivateAuth();
    this->publicAuth = *node->getPublicAuth();
    this->chatAuth = node->getChatAuth() ? MegaApi::strdup(node->getChatAuth()) : NULL;
    this->outShares = node->isOutShare();
    this->inShare = node->isInShare();
    this->foreign = node->isForeign();
    this->sharekey = NULL;
    this->children = NULL;
    this->owner = node->getOwner();

    if (node->isExported())
    {
        this->plink = new PublicLink(node->getPublicHandle(), node->getPublicLinkCreationTime(), node->getExpirationTime(), node->isTakenDown());

        if (type == FOLDERNODE)
        {
            MegaNodePrivate *n = dynamic_cast<MegaNodePrivate *>(node);
            if (n)
            {
                string *sk = n->getSharekey();
                if (sk)
                {
                    this->sharekey = new string(*sk);
                }
            }
        }
    }
    else
    {
        this->plink = NULL;
    }
    this->mNewLinkFormat = np->isNewLinkFormat();

    if (node->hasCustomAttrs())
    {
        this->customAttrs = new attr_map();
        MegaStringList *names = node->getCustomAttrNames();
        for (int i = 0; i < names->size(); i++)
        {
            (*customAttrs)[AttrMap::string2nameid(names->get(i))] = node->getCustomAttr(names->get(i));
        }
        delete names;
    }

#ifdef ENABLE_SYNC
    this->syncdeleted = node->isSyncDeleted();
    this->localPath = node->getLocalPath();
#endif
}

MegaNodePrivate::MegaNodePrivate(Node *node)
: MegaNode()
{
    this->name = MegaApi::strdup(node->displayname());
    this->fingerprint = NULL;
    this->originalfingerprint = NULL;
    this->children = NULL;
    this->chatAuth = NULL;

    if (node->isvalid)
    {
        string fingerprint;
        node->serializefingerprint(&fingerprint);
        m_off_t size = node->size;
        char bsize[sizeof(size)+1];
        int l = Serialize64::serialize((byte *)bsize, size);
        char *buf = new char[l * 4 / 3 + 4];
        char ssize = static_cast<char>('A' + Base64::btoa((const byte *)bsize, l, buf));
        string result(1, ssize);
        result.append(buf);
        result.append(fingerprint);
        delete [] buf;

        this->fingerprint = MegaApi::strdup(result.c_str());
    }

    this->duration = -1;
    this->width = -1;
    this->height = -1;
    this->shortformat = -1;
    this->videocodecid = -1;
    this->latitude = INVALID_COORDINATE;
    this->longitude = INVALID_COORDINATE;
    this->customAttrs = NULL;
    this->restorehandle = UNDEF;

    char buf[10];
    for (attr_map::iterator it = node->attrs.map.begin(); it != node->attrs.map.end(); it++)
    {
        int attrlen = node->attrs.nameid2string(it->first, buf);
        buf[attrlen] = '\0';
        if (buf[0] == '_')
        {
           if (!customAttrs)
           {
               customAttrs = new attr_map();
           }

           nameid id = AttrMap::string2nameid(&buf[1]);
           (*customAttrs)[id] = it->second;
        }
        else
        {
            if (it->first == AttrMap::string2nameid("d"))
            {
               if (node->type == FILENODE)
               {
                   duration = int(Base64::atoi(&it->second));
               }
            }
            else if (it->first == AttrMap::string2nameid("l") || it->first == AttrMap::string2nameid("gp"))
            {
                if (node->type == FILENODE)
                {
                    string coords = it->second;
                    if ((it->first == AttrMap::string2nameid("l") && coords.size() != 8) ||
                        (it->first == AttrMap::string2nameid("gp") && coords.size() != Base64Str<16>::STRLEN))
                    {
                       LOG_warn << "Malformed GPS coordinates attribute";
                    }
                    else
                    {
                        bool ok = true;
                        if (it->first == AttrMap::string2nameid("gp"))
                        {
                            if (node->client && node->client->unshareablekey.size() == Base64Str<SymmCipher::KEYLENGTH>::STRLEN && coords.size() == Base64Str<16>::STRLEN)
                            {
                                SymmCipher c;
                                byte data[SymmCipher::BLOCKSIZE] = { 0 };
                                Base64::atob(coords.data(), data, Base64Str<SymmCipher::BLOCKSIZE>::STRLEN);

                                node->client->setkey(&c, node->client->unshareablekey.data());
                                c.ctr_crypt(data, SymmCipher::BLOCKSIZE, 0, 0, NULL, false);
                                ok = !memcmp(data, "unshare/", 8);
                                if (ok)
                                {
                                    coords = string((char*)data + 8, 8);
                                }
                            }
                            else
                            {
                                ok = false;
                            }
                        }

                        if (ok)
                        {
                            byte buf[3];
                            int number = 0;
                            if (Base64::atob((const char *)coords.substr(0, 4).data(), buf, sizeof(buf)) == sizeof(buf))
                            {
                                number = (buf[2] << 16) | (buf[1] << 8) | (buf[0]);
                                latitude = -90 + 180 * (double)number / 0xFFFFFF;
                            }

                            if (Base64::atob((const char *)coords.substr(4, 4).data(), buf, sizeof(buf)) == sizeof(buf))
                            {
                                number = (buf[2] << 16) | (buf[1] << 8) | (buf[0]);
                                longitude = -180 + 360 * (double)number / 0x01000000;
                            }
                        }
                    }

                    if (longitude < -180 || longitude > 180)
                    {
                        longitude = INVALID_COORDINATE;
                    }
                    if (latitude < -90 || latitude > 90)
                    {
                        latitude = INVALID_COORDINATE;
                    }
                    if (longitude == INVALID_COORDINATE || latitude == INVALID_COORDINATE)
                    {
                        longitude = INVALID_COORDINATE;
                        latitude = INVALID_COORDINATE;
                    }
               }
            }
            else if (it->first == AttrMap::string2nameid("rr"))
            {
                handle rr = 0;
                if (Base64::atob(it->second.c_str(), (byte *)&rr, sizeof(rr)) == MegaClient::NODEHANDLE)
                {
                    restorehandle = rr;
                }
            }
            else if (it->first == AttrMap::string2nameid("c") && !fingerprint)
            {
                fingerprint = MegaApi::strdup(it->second.c_str());
            }
            else if (it->first == AttrMap::string2nameid("c0"))
            {
                originalfingerprint = MegaApi::strdup(it->second.c_str());
            }
        }
    }

    this->type = node->type;
    this->size = node->size;
    this->ctime = node->ctime;
    this->mtime = node->mtime;
    this->nodehandle = node->nodehandle;
    this->parenthandle = node->parent ? node->parent->nodehandle : INVALID_HANDLE;
    this->owner = node->owner;

    if(node->attrstring)
    {
        this->attrstring.assign(node->attrstring->data(), node->attrstring->size());
    }
    this->fileattrstring = node->fileattrstring;
    this->nodekey = node->nodekeyUnchecked();

    this->changed = 0;
    if(node->changed.attrs)
    {
        this->changed |= MegaNode::CHANGE_TYPE_ATTRIBUTES;
    }
    if(node->changed.ctime)
    {
        this->changed |= MegaNode::CHANGE_TYPE_TIMESTAMP;
    }
    if(node->changed.fileattrstring)
    {
        this->changed |= MegaNode::CHANGE_TYPE_FILE_ATTRIBUTES;
    }
    if(node->changed.inshare)
    {
        this->changed |= MegaNode::CHANGE_TYPE_INSHARE;
    }
    if(node->changed.outshares)
    {
        this->changed |= MegaNode::CHANGE_TYPE_OUTSHARE;
    }
    if(node->changed.pendingshares)
    {
        this->changed |= MegaNode::CHANGE_TYPE_PENDINGSHARE;
    }
    if(node->changed.owner)
    {
        this->changed |= MegaNode::CHANGE_TYPE_OWNER;
    }
    if(node->changed.parent)
    {
        this->changed |= MegaNode::CHANGE_TYPE_PARENT;
    }
    if(node->changed.removed)
    {
        this->changed |= MegaNode::CHANGE_TYPE_REMOVED;
    }
    if(node->changed.publiclink)
    {
        this->changed |= MegaNode::CHANGE_TYPE_PUBLIC_LINK;
    }
    if(node->changed.newnode)
    {
        this->changed |= MegaNode::CHANGE_TYPE_NEW;
    }


#ifdef ENABLE_SYNC
    this->syncdeleted = (node->syncdeleted != SYNCDEL_NONE);
    if(node->localnode)
    {
        LocalPath lp;
        node->localnode->getlocalpath(lp, true);
        localPath.swap(*lp.editStringDirect());
        localPath.append("", 1);
    }
#endif

    this->thumbnailAvailable = (node->hasfileattribute(0) != 0);
    this->previewAvailable = (node->hasfileattribute(1) != 0);
    this->tag = node->tag;
    this->isPublicNode = false;
    this->foreign = false;

    // if there's only one share and it has no user --> public link
    this->outShares = (node->outshares) ? (node->outshares->size() > 1 || node->outshares->begin()->second->user) : false;
    this->inShare = (node->inshare != NULL) && !node->parent;
    this->plink = node->plink ? new PublicLink(node->plink) : NULL;
    this->mNewLinkFormat = node->client->mNewLinkFormat;
    if (plink && type == FOLDERNODE && node->sharekey)
    {
        char key[FOLDERNODEKEYLENGTH*4/3+3];
        Base64::btoa(node->sharekey->key, FOLDERNODEKEYLENGTH, key);
        this->sharekey = new string(key);
    }
    else
    {
        this->sharekey = NULL;
    }
}

string* MegaNodePrivate::getSharekey()
{
    return sharekey;
}

MegaNode *MegaNodePrivate::copy()
{
    return new MegaNodePrivate(this);
}

char *MegaNodePrivate::serialize()
{
    string d;
    if (!serialize(&d))
    {
        return NULL;
    }

    char *ret = new char[d.size()*4/3+3];
    Base64::btoa((byte*) d.data(), int(d.size()), ret);

    return ret;
}

bool MegaNodePrivate::serialize(string *d)
{
    CacheableWriter w(*d);
    w.serializecstr(name, true);
    w.serializecstr(fingerprint, true);
    w.serializei64(size);
    w.serializei64(ctime);
    w.serializei64(mtime);
    w.serializehandle(nodehandle);
    w.serializehandle(parenthandle);
    w.serializestring(attrstring);
    w.serializestring(nodekey);
    w.serializestring(privateAuth);
    w.serializestring(publicAuth);
    w.serializebool(isPublicNode);
    w.serializebool(foreign);

    bool hasChatAuth = chatAuth && chatAuth[0];
    bool hasOwner = true;

    bool hasOriginalFingerprint = originalfingerprint && originalfingerprint[0];

    w.serializeexpansionflags(hasChatAuth, hasOwner, hasOriginalFingerprint);

    if (hasChatAuth)
    {
        w.serializecstr(chatAuth, false);
    }
    if (hasOwner)
    {
        w.serializehandle(owner);
    }
    if (hasOriginalFingerprint)
    {
        w.serializecstr(originalfingerprint, false);
    }

    return true;
}

MegaNodePrivate *MegaNodePrivate::unserialize(string *d)
{
    CacheableReader r(*d);
    string name, fingerprint, originalfingerprint, attrstring, nodekey, privauth, pubauth, chatauth;
    int64_t size, ctime, mtime;
    MegaHandle nodehandle, parenthandle, owner = INVALID_HANDLE;
    bool isPublicNode, foreign;
    unsigned char expansions[8];
    string fileattrstring; // fileattrstring is not serialized
    if (!r.unserializecstr(name, true) ||
        !r.unserializecstr(fingerprint, true) ||
        !r.unserializei64(size) ||
        !r.unserializei64(ctime) ||
        !r.unserializei64(mtime) ||
        !r.unserializehandle(nodehandle) ||
        !r.unserializehandle(parenthandle) ||
        !r.unserializestring(attrstring) ||
        !r.unserializestring(nodekey) ||
        !r.unserializestring(privauth) ||
        !r.unserializestring(pubauth) ||
        !r.unserializebool(isPublicNode) ||
        !r.unserializebool(foreign) ||
        !r.unserializeexpansionflags(expansions, 3) ||
        (expansions[0] && !r.unserializecstr(chatauth, false)) ||
        (expansions[1] && !r.unserializehandle(owner)) ||
        (expansions[2] && !r.unserializecstr(originalfingerprint, false)))
    {
        LOG_err << "MegaNode unserialization failed at field " << r.fieldnum;
        return NULL;
    }

    r.eraseused(*d);

    return new MegaNodePrivate(name.c_str(), FILENODE, size, ctime,
                               mtime, nodehandle, &nodekey, &attrstring, &fileattrstring,
                               fingerprint.empty() ? NULL : fingerprint.c_str(), originalfingerprint.empty() ? NULL : originalfingerprint.c_str(),
                               owner, parenthandle, privauth.c_str(), pubauth.c_str(),
                               isPublicNode, foreign, chatauth.empty() ? NULL : chatauth.c_str());
}

char *MegaNodePrivate::getBase64Handle()
{
    char *base64Handle = new char[12];
    Base64::btoa((byte*)&(nodehandle),MegaClient::NODEHANDLE,base64Handle);
    return base64Handle;
}

int MegaNodePrivate::getType()
{
    return type;
}

const char* MegaNodePrivate::getName()
{
    if(type <= FOLDERNODE)
    {
        return name;
    }

    switch(type)
    {
        case ROOTNODE:
            return "Cloud Drive";
        case INCOMINGNODE:
            return "Inbox";
        case RUBBISHNODE:
            return "Rubbish Bin";
        default:
            return name;
    }
}

const char *MegaNodePrivate::getFingerprint()
{
    return fingerprint;
}

const char *MegaNodePrivate::getOriginalFingerprint()
{
    return originalfingerprint;
}

bool MegaNodePrivate::hasCustomAttrs()
{
    return customAttrs != NULL;
}

MegaStringList *MegaNodePrivate::getCustomAttrNames()
{
    if (!customAttrs)
    {
        return new MegaStringList();
    }

    vector<char*> names;
    char *buf;
    for (attr_map::iterator it = customAttrs->begin(); it != customAttrs->end(); it++)
    {
        buf = new char[10];
        int attrlen = AttrMap::nameid2string(it->first, buf);
        buf[attrlen] = '\0';
        names.push_back(buf);
    }
    return new MegaStringListPrivate(names.data(), int(names.size()));
}

const char *MegaNodePrivate::getCustomAttr(const char *attrName)
{
    if (!customAttrs)
    {
        return NULL;
    }

    nameid n = AttrMap::string2nameid(attrName);
    if (!n)
    {
        return NULL;
    }

    attr_map::iterator it = customAttrs->find(n);
    if (it == customAttrs->end())
    {
        return NULL;
    }

    return it->second.c_str();
}

int MegaNodePrivate::getDuration()
{
    if (type == MegaNode::TYPE_FILE && nodekey.size() == FILENODEKEYLENGTH && fileattrstring.size())
    {
        uint32_t* attrKey = (uint32_t*)(nodekey.data() + FILENODEKEYLENGTH / 2);
        MediaProperties mediaProperties = MediaProperties::decodeMediaPropertiesAttributes(fileattrstring, attrKey);
        if (mediaProperties.shortformat != 255 // 255 = MediaInfo failed processing the file
                && mediaProperties.shortformat != 254 // 254 = No information available
                && mediaProperties.playtime > 0)
        {
            return mediaProperties.playtime;
        }
    }

    return duration;
}

int MegaNodePrivate::getWidth()
{
    if (width == -1)    // not initialized yet, or not available
    {
        if (type == MegaNode::TYPE_FILE && nodekey.size() == FILENODEKEYLENGTH && fileattrstring.size())
        {
            uint32_t* attrKey = (uint32_t*)(nodekey.data() + FILENODEKEYLENGTH / 2);
            MediaProperties mediaProperties = MediaProperties::decodeMediaPropertiesAttributes(fileattrstring, attrKey);
            if (mediaProperties.shortformat != 255 // 255 = MediaInfo failed processing the file
                    && mediaProperties.shortformat != 254 // 254 = No information available
                    && mediaProperties.width > 0)
            {
                width = mediaProperties.width;
            }
        }
    }
    
    return width;
}

int MegaNodePrivate::getHeight()
{
    if (height == -1)    // not initialized yet, or not available
    {
        if (type == MegaNode::TYPE_FILE && nodekey.size() == FILENODEKEYLENGTH && fileattrstring.size())
        {
            uint32_t* attrKey = (uint32_t*)(nodekey.data() + FILENODEKEYLENGTH / 2);
            MediaProperties mediaProperties = MediaProperties::decodeMediaPropertiesAttributes(fileattrstring, attrKey);
            if (mediaProperties.shortformat != 255 // 255 = MediaInfo failed processing the file
                    && mediaProperties.shortformat != 254 // 254 = No information available
                    && mediaProperties.height > 0)
            {
                height = mediaProperties.height;
            }
        }
    }
    
    return height;
}
    
int MegaNodePrivate::getShortformat()
{
    if (shortformat == -1)    // not initialized yet, or not available
    {
        if (type == MegaNode::TYPE_FILE && nodekey.size() == FILENODEKEYLENGTH && fileattrstring.size())
        {
            uint32_t* attrKey = (uint32_t*)(nodekey.data() + FILENODEKEYLENGTH / 2);
            MediaProperties mediaProperties = MediaProperties::decodeMediaPropertiesAttributes(fileattrstring, attrKey);
            if (mediaProperties.shortformat != 255 // 255 = MediaInfo failed processing the file
                && mediaProperties.shortformat != 254 // 254 = No information available
                && mediaProperties.shortformat > 0)
            {
                shortformat = mediaProperties.shortformat;
            }
        }
    }
    
    return shortformat;
}
    
int MegaNodePrivate::getVideocodecid()
{
    if (videocodecid == -1)    // not initialized yet, or not available
    {
        if (type == MegaNode::TYPE_FILE && nodekey.size() == FILENODEKEYLENGTH && fileattrstring.size())
        {
            uint32_t* attrKey = (uint32_t*)(nodekey.data() + FILENODEKEYLENGTH / 2);
            MediaProperties mediaProperties = MediaProperties::decodeMediaPropertiesAttributes(fileattrstring, attrKey);
            if (mediaProperties.shortformat != 255 // 255 = MediaInfo failed processing the file
                && mediaProperties.shortformat != 254 // 254 = No information available
                && mediaProperties.videocodecid > 0)
            {
                videocodecid = mediaProperties.videocodecid;
            }
        }
    }
    
    return videocodecid;
}

double MegaNodePrivate::getLatitude()
{
    return latitude;
}

double MegaNodePrivate::getLongitude()
{
    return longitude;
}

int64_t MegaNodePrivate::getSize()
{
    return size;
}

int64_t MegaNodePrivate::getCreationTime()
{
    return ctime;
}

int64_t MegaNodePrivate::getModificationTime()
{
    return mtime;
}

MegaHandle MegaNodePrivate::getRestoreHandle()
{
    return restorehandle;
}

MegaHandle MegaNodePrivate::getParentHandle()
{
    return parenthandle;
}

uint64_t MegaNodePrivate::getHandle()
{
    return nodehandle;
}

string *MegaNodePrivate::getNodeKey()
{
    return &nodekey;
}

char *MegaNodePrivate::getBase64Key()
{
    char *key = NULL;

    // the key
    if (type == FILENODE && nodekey.size() >= FILENODEKEYLENGTH)
    {
        key = new char[FILENODEKEYLENGTH * 4 / 3 + 3];
        Base64::btoa((const byte*)nodekey.data(), FILENODEKEYLENGTH, key);
    }
    else if (type == FOLDERNODE && sharekey)
    {
        key = MegaApi::strdup(sharekey->c_str());
    }
    else
    {
        key = new char[1];
        key[0] = 0;
    }

    return key;
}

string *MegaNodePrivate::getAttrString()
{
    return &attrstring;
}

char *MegaNodePrivate::getFileAttrString()
{
    char* fileAttributes = NULL;

    if (fileattrstring.size() > 0)
    {
        fileAttributes = MegaApi::strdup(fileattrstring.c_str());
    }

    return fileAttributes;
}

int MegaNodePrivate::getTag()
{
    return tag;
}

int64_t MegaNodePrivate::getExpirationTime()
{
    return plink ? plink->ets : -1;
}

MegaHandle MegaNodePrivate::getPublicHandle()
{
    return plink ? (MegaHandle) plink->ph : INVALID_HANDLE;
}

MegaNode* MegaNodePrivate::getPublicNode()
{
    if (!plink || plink->isExpired())
    {
        return NULL;
    }

    char *skey = getBase64Key();
    string key(skey);

    MegaNode *node = new MegaNodePrivate(
                name, type, size, ctime, mtime,
                plink->ph, &key, &attrstring, &fileattrstring, fingerprint, originalfingerprint, 
                INVALID_HANDLE);

    delete [] skey;

    return node;
}

char *MegaNodePrivate::getPublicLink(bool includeKey)
{
    if (!plink)
    {
        return NULL;
    }

    char *base64k = getBase64Key();
    string strlink = MegaClient::getPublicLink(mNewLinkFormat, static_cast<nodetype_t>(type), plink->ph, (includeKey ? base64k : nullptr));
    delete [] base64k;

    return MegaApi::strdup(strlink.c_str());
}

int64_t MegaNodePrivate::getPublicLinkCreationTime()
{
    return plink ? plink->cts : -1;
}

bool MegaNodePrivate::isNewLinkFormat()
{
    return mNewLinkFormat;
}

bool MegaNodePrivate::isFile()
{
    return type == TYPE_FILE;
}

bool MegaNodePrivate::isFolder()
{
    return (type != TYPE_FILE) && (type != TYPE_UNKNOWN);
}

bool MegaNodePrivate::isRemoved()
{
    return hasChanged(MegaNode::CHANGE_TYPE_REMOVED);
}

bool MegaNodePrivate::hasChanged(int changeType)
{
    return (changed & changeType);
}

int MegaNodePrivate::getChanges()
{
    return changed;
}

MegaHandle MegaNodePrivate::getOwner() const
{
    return owner;
}

const unsigned int MegaApiImpl::MAX_SESSION_LENGTH = 64;


MegaBackgroundMediaUploadPrivate::MegaBackgroundMediaUploadPrivate(MegaApi* capi)
    : api(MegaApiImpl::ImplOf(capi))
{
    // generate fresh random encryption key/CTR IV for this file
    api->client->rng.genblock(filekey, sizeof filekey);
}

MegaBackgroundMediaUploadPrivate::MegaBackgroundMediaUploadPrivate(const string& serialised, MegaApi* capi)
    : api(MegaApiImpl::ImplOf(capi))
{
    CacheableReader r(serialised);
    string mediapropertiesstr;
    unsigned char expansions[8];
    string fileattrstring; // fileattrstring is not serialized
    if (!r.unserializebinary(filekey, sizeof(filekey)) ||
        !r.unserializechunkmacs(chunkmacs) ||
        !r.unserializestring(mediapropertiesstr) ||
        !r.unserializestring(url) ||
        !r.unserializedouble(latitude) ||
        !r.unserializedouble(longitude) ||
        !r.unserializebool(unshareableGPS) ||
        !r.unserializehandle(thumbnailFA) ||
        !r.unserializehandle(previewFA) ||
        !r.unserializeexpansionflags(expansions, 0))
    {
        LOG_err << "MegaBackgroundMediaUploadPrivate unserialization failed at field " << r.fieldnum;
    }
    else
    {
        mediaproperties = MediaProperties(mediapropertiesstr);
    }
}

bool MegaBackgroundMediaUploadPrivate::serialize(string* s)
{
    CacheableWriter w(*s);
    w.serializebinary(filekey, sizeof(filekey));
    w.serializechunkmacs(chunkmacs);
    w.serializestring(mediaproperties.serialize());
    w.serializestring(url);
    w.serializedouble(latitude);
    w.serializedouble(longitude);
    w.serializebool(unshareableGPS);
    w.serializehandle(thumbnailFA);
    w.serializehandle(previewFA);
    w.serializeexpansionflags();  // if/when we add more in future, set the first one true to signal the new set are present.
    return s;
}

char *MegaBackgroundMediaUploadPrivate::serialize()
{
    string d;
    return serialize(&d) ? MegaApi::binaryToBase64(d.data(), d.size()) : NULL;
}

void MegaBackgroundMediaUploadPrivate::setThumbnail(MegaHandle h)
{
    thumbnailFA = h;
}

void MegaBackgroundMediaUploadPrivate::setPreview(MegaHandle h)
{
    previewFA = h;
}

void MegaBackgroundMediaUploadPrivate::setCoordinates(double lat, double lon, bool unsh)
{
    latitude = lat;
    longitude = lon;
    unshareableGPS = unsh;
}

SymmCipher* MegaBackgroundMediaUploadPrivate::nodecipher(MegaClient* client)
{
    client->tmpnodecipher.setkey(filekey);
    return &client->tmpnodecipher;
}

MegaBackgroundMediaUploadPrivate::~MegaBackgroundMediaUploadPrivate()
{
}

bool MegaBackgroundMediaUploadPrivate::analyseMediaInfo(const char* inputFilepath)
{
#ifdef USE_MEDIAINFO
    if (!api->client->mediaFileInfo.mediaCodecsReceived)
    {
        // the client app should already have requested these but just in case:
        api->client->mediaFileInfo.requestCodecMappingsOneTime(api->client, NULL);
        return false;
    }

    auto localfilename = LocalPath::fromPath(inputFilepath, *api->fsAccess);

    char ext[8];
    if (api->fsAccess->getextension(localfilename, ext, sizeof(ext)) && MediaProperties::isMediaFilenameExt(ext))
    {
        mediaproperties.extractMediaPropertyFileAttributes(localfilename, api->fsAccess);

        // cause the codec IDs to be looked up before serialization. Codec names are not serialized, just the codec IDs
        uint32_t dummykey[4];
        mediaproperties.convertMediaPropertyFileAttributes(dummykey, api->client->mediaFileInfo);
    }
#endif
    return true;
}

char *MegaBackgroundMediaUploadPrivate::encryptFile(const char* inputFilepath, int64_t startPos, int64_t* length, const char* outputFilepath, bool adjustsizeonly)
{
    if (startPos != ChunkedHash::chunkfloor(startPos))
    {
        LOG_err << "non-chunk start postion supplied";
        return nullptr;
    }

    std::unique_ptr<FileAccess> fain(api->fsAccess->newfileaccess());
    auto localfilename = LocalPath::fromPath(inputFilepath, *api->fsAccess);

    if (fain->fopen(localfilename, true, false) || fain->type != FILENODE)
    {
        if (*length == -1)
        {
            *length = fain->size - startPos;
        }
        if (startPos < 0 || startPos > fain->size)
        {
            LOG_err << "invalid startPos supplied";
            return nullptr;
        } 
        else if (*length < 0 || startPos + *length > fain->size)
        {
            LOG_err << "invalid enryption length supplied";
            return nullptr;
        }
        else
        {
            // make sure we load to a chunk boundary
            m_off_t endPos = ChunkedHash::chunkceil(startPos + *length, fain->size);
            *length = endPos - startPos;
            if (adjustsizeonly)
            {
                // return non-null to indicate success.  As it's a string return in the standard case, caller must deallocate as usual.
                return MegaApi::strdup("1");
            }
            else
            {
                auto localencryptedfilename = LocalPath::fromPath(outputFilepath, *api->fsAccess);

                std::unique_ptr<FileAccess> faout(api->fsAccess->newfileaccess());
                if (faout->fopen(localencryptedfilename, false, true))
                {
                    SymmCipher cipher;
                    cipher.setkey(filekey);
                    uint64_t ctriv = MemAccess::get<uint64_t>((const char*)filekey + SymmCipher::KEYLENGTH);

                    EncryptFilePieceByChunks ef(fain.get(), startPos, faout.get(), 0, &cipher, &chunkmacs, ctriv);
                    string urlSuffix;
                    if (ef.encrypt(startPos, endPos, urlSuffix))
                    {
                        ((int64_t*)filekey)[3] = chunkmacs.macsmac(&cipher);
                        return MegaApi::strdup(urlSuffix.c_str());
                    }
                }
            }
        }
    }
    return nullptr; 
}

char *MegaBackgroundMediaUploadPrivate::getUploadURL()
{
    return url.empty() ? nullptr : MegaApi::strdup(url.c_str());
}

EncryptFilePieceByChunks::EncryptFilePieceByChunks(FileAccess *cFain, m_off_t cInPos, FileAccess *cFaout, m_off_t cOutPos,
                                                   SymmCipher *cipher, chunkmac_map *chunkmacs, uint64_t ctriv)
    : EncryptByChunks(cipher, chunkmacs, ctriv)
    , fain(cFain), faout(cFaout)
    , inpos(cInPos), outpos(cOutPos)
    , lastsize(0)
{
}

byte *EncryptFilePieceByChunks::nextbuffer(unsigned bufsize)
{
    if (lastsize)
    {
        // write the last encrypted chunk
        if (!faout->fwrite((byte*)buffer.data(), lastsize, outpos))
        {
            return NULL;
        }
        outpos += lastsize;
    }

    buffer.resize(bufsize + SymmCipher::BLOCKSIZE);
    memset((void*)(buffer.data() + bufsize), 0, SymmCipher::BLOCKSIZE);
    if (!fain->frawread((byte*)buffer.data(), bufsize, inpos))
    {
        return NULL;
    }
    lastsize = bufsize;
    inpos += bufsize;
    return (byte*)buffer.data();
}

#ifdef ENABLE_SYNC
bool MegaNodePrivate::isSyncDeleted()
{
    return syncdeleted;
}

string MegaNodePrivate::getLocalPath()
{
    return localPath;
}


bool WildcardMatch(const char *pszString, const char *pszMatch)
//  cf. http://www.planet-source-code.com/vb/scripts/ShowCode.asp?txtCodeId=1680&lngWId=3
{
    const char *cp = nullptr;
    const char *mp = nullptr;

    while ((*pszString) && (*pszMatch != '*'))
    {
        if ((*pszMatch != *pszString) && (*pszMatch != '?'))
        {
            return false;
        }
        pszMatch++;
        pszString++;
    }

    while (*pszString)
    {
        if (*pszMatch == '*')
        {
            if (!*++pszMatch)
            {
                return true;
            }
            mp = pszMatch;
            cp = pszString + 1;
        }
        else if ((*pszMatch == *pszString) || (*pszMatch == '?'))
        {
            pszMatch++;
            pszString++;
        }
        else
        {
            pszMatch = mp;
            pszString = cp++;
        }
    }
    while (*pszMatch == '*')
    {
        pszMatch++;
    }
    return !*pszMatch;
}

bool MegaApiImpl::is_syncable(Sync *sync, const char *name, const LocalPath& localpath)
{
    // Don't sync these system files from OS X
    if (!strcmp(name, "Icon\x0d"))
    {
        return false;
    }

    for (unsigned int i = 0; i < excludedNames.size(); i++)
    {
        if (WildcardMatch(name, excludedNames[i].c_str()))
        {
            return false;
        }
    }

    MegaRegExp *regExp = NULL;
#ifdef USE_PCRE
    MegaSyncPrivate* megaSync = (MegaSyncPrivate *)sync->appData;
    if (megaSync)
    {
        regExp = megaSync->getRegExp();
    }
#endif

    if (regExp || excludedPaths.size())
    {             
        string utf8path = localpath.toPath(*fsAccess);

        for (unsigned int i = 0; i < excludedPaths.size(); i++)
        {
            if (WildcardMatch(utf8path.c_str(), excludedPaths[i].c_str()))
            {
                return false;
            }
        }

#ifdef USE_PCRE
        if (regExp && regExp->match(utf8path.c_str()))
        {
            return false;
        }
#endif
    }

    return true;
}

bool MegaApiImpl::is_syncable(long long size)
{
    if (!syncLowerSizeLimit)
    {
        // No lower limit. Check upper limit only
        if (syncUpperSizeLimit && size > syncUpperSizeLimit)
        {
            return false;
        }
    }
    else if (!syncUpperSizeLimit)
    {
        // No upper limit. Check lower limit only
        if (syncLowerSizeLimit && size < syncLowerSizeLimit)
        {
            return false;
        }
    }
    else
    {
        //Upper and lower limit
        if(syncLowerSizeLimit < syncUpperSizeLimit)
        {
            // Normal use case:
            // Exclude files with a size lower than the lower limit
            // or greater than the upper limit
            if(size < syncLowerSizeLimit || size > syncUpperSizeLimit)
            {
                return false;
            }
        }
        else
        {
            // Special use case:
            // Exclude files with a size lower than the lower limit
            // AND greater than the upper limit
            if(size < syncLowerSizeLimit && size > syncUpperSizeLimit)
            {
                return false;
            }
        }
    }

    return true;
}

int MegaApiImpl::isNodeSyncable(MegaNode *megaNode)
{
    if (!megaNode)
    {
        return MegaError::API_EARGS;
    }

    sdkMutex.lock();
    Node *node = client->nodebyhandle(megaNode->getHandle());
    if (!node)
    {
        sdkMutex.unlock();
        return MegaError::API_ENOENT;
    }

    error e = client->isnodesyncable(node);
    sdkMutex.unlock();
    return e;
}

bool MegaApiImpl::isIndexing()
{
    if(!client || client->syncs.size() == 0)
    {
        return false;
    }

    if(client->syncscanstate)
    {
        return true;
    }

    bool indexing = false;
    sdkMutex.lock();
    sync_list::iterator it = client->syncs.begin();
    while(it != client->syncs.end())
    {
        Sync *sync = (*it);
        if(sync->state == SYNC_INITIALSCAN)
        {
            indexing = true;
            break;
        }
        it++;
    }
    sdkMutex.unlock();
    return indexing;
}

bool MegaApiImpl::isSyncing()
{
    if (!client->syncs.size())
    {
        return false;
    }
    SdkMutexGuard g(sdkMutex);

    for (auto & sync : client->syncs)
    {
        if (sync->localroot->ts == TREESTATE_SYNCING || sync->localroot->ts == TREESTATE_PENDING)
        {
            return true;
        }
    }
    return false;
}

MegaSync *MegaApiImpl::getSyncByTag(int tag)
{
    sdkMutex.lock();
    if (syncMap.find(tag) == syncMap.end())
    {
        sdkMutex.unlock();
        return NULL;
    }
    MegaSync *result = syncMap.at(tag)->copy();
    sdkMutex.unlock();
    return result;
}

MegaSync *MegaApiImpl::getSyncByNode(MegaNode *node)
{
    if (!node)
    {
        return NULL;
    }

    MegaSync *result = NULL;
    MegaHandle nodeHandle = node->getHandle();
    sdkMutex.lock();
    std::map<int, MegaSyncPrivate*>::iterator it = syncMap.begin();
    while(it != syncMap.end())
    {
        MegaSyncPrivate* sync = it->second;
        if (sync->getMegaHandle() == nodeHandle)
        {
            result = sync->copy();
            break;
        }
        it++;
    }

    sdkMutex.unlock();
    return result;
}

MegaSync *MegaApiImpl::getSyncByPath(const char *localPath)
{
    if (!localPath)
    {
        return NULL;
    }

    MegaSync *result = NULL;
    sdkMutex.lock();
    std::map<int, MegaSyncPrivate*>::iterator it = syncMap.begin();
    while(it != syncMap.end())
    {
        MegaSyncPrivate* sync = it->second;
        if (!strcmp(localPath, sync->getLocalFolder()))
        {
            result = sync->copy();
            break;
        }
        it++;
    }

    sdkMutex.unlock();
    return result;
}

char *MegaApiImpl::getBlockedPath()
{
    char *path = NULL;
    sdkMutex.lock();
    if (!client->blockedfile.empty())
    {
        path = MegaApi::strdup(client->blockedfile.toPath(*fsAccess).c_str());
    }
    sdkMutex.unlock();
    return path;
}
#endif

MegaBackup *MegaApiImpl::getBackupByTag(int tag)
{
    sdkMutex.lock();
    if (backupsMap.find(tag) == backupsMap.end())
    {
        sdkMutex.unlock();
        return NULL;
    }
    MegaBackup *result = backupsMap.at(tag)->copy();
    sdkMutex.unlock();
    return result;
}

MegaBackup *MegaApiImpl::getBackupByNode(MegaNode *node)
{
    if (!node)
    {
        return NULL;
    }

    MegaBackup *result = NULL;
    MegaHandle nodeHandle = node->getHandle();
    sdkMutex.lock();
    std::map<int, MegaBackupController*>::iterator it = backupsMap.begin();
    while(it != backupsMap.end())
    {
        MegaBackupController* backup = it->second;
        if (backup->getMegaHandle() == nodeHandle)
        {
            result = backup->copy();
            break;
        }
        it++;
    }

    sdkMutex.unlock();
    return result;
}

MegaBackup *MegaApiImpl::getBackupByPath(const char *localPath)
{
    if (!localPath)
    {
        return NULL;
    }

    MegaBackup *result = NULL;
    sdkMutex.lock();
    std::map<int, MegaBackupController*>::iterator it = backupsMap.begin();
    while(it != backupsMap.end())
    {
        MegaBackupController* backup = it->second;
        if (!strcmp(localPath, backup->getLocalFolder()))
        {
            result = backup->copy();
            break;
        }
        it++;
    }

    sdkMutex.unlock();
    return result;
}
bool MegaNodePrivate::hasThumbnail()
{
    return thumbnailAvailable;
}

bool MegaNodePrivate::hasPreview()
{
    return previewAvailable;
}

bool MegaNodePrivate::isPublic()
{
    return isPublicNode;
}

bool MegaNodePrivate::isShared()
{
    return outShares || inShare;
}

bool MegaNodePrivate::isOutShare()
{
    return outShares;
}

bool MegaNodePrivate::isInShare()
{
    return inShare;
}

bool MegaNodePrivate::isExported()
{
    return plink;
}

bool MegaNodePrivate::isExpired()
{
    return plink ? (plink->isExpired()) : false;
}

bool MegaNodePrivate::isTakenDown()
{
    return plink ? plink->takendown : false;
}

bool MegaNodePrivate::isForeign()
{
    return foreign;
}

string *MegaNodePrivate::getPrivateAuth()
{
    return &privateAuth;
}

MegaNodeList *MegaNodePrivate::getChildren()
{
    return children;
}

void MegaNodePrivate::setPrivateAuth(const char *privateAuth)
{
    if (!privateAuth || !privateAuth[0])
    {
        this->privateAuth.clear();
    }
    else
    {
        this->privateAuth = privateAuth;
    }
}

void MegaNodePrivate::setPublicAuth(const char *publicAuth)
{
    if (!publicAuth || !publicAuth[0])
    {
        this->publicAuth.clear();
    }
    else
    {
        this->publicAuth = publicAuth;
    }
}

void MegaNodePrivate::setChatAuth(const char *chatAuth)
{
    delete [] this->chatAuth;
    if (!chatAuth || !chatAuth[0])
    {
        this->chatAuth = NULL;
        this->foreign = false;
    }
    else
    {
        this->chatAuth = MegaApi::strdup(chatAuth);
        this->foreign = true;
    }
}

void MegaNodePrivate::setForeign(bool foreign)
{
    this->foreign = foreign;
}

void MegaNodePrivate::setChildren(MegaNodeList *children)
{
    this->children = children;
}

void MegaNodePrivate::setName(const char *newName)
{
    if (name)
        delete [] name;

    name = MegaApi::strdup(newName);
}

string *MegaNodePrivate::getPublicAuth()
{
    return &publicAuth;
}

const char *MegaNodePrivate::getChatAuth()
{
    return chatAuth;
}

MegaNodePrivate::~MegaNodePrivate()
{
    delete[] name;
    delete[] fingerprint;
    delete[] originalfingerprint;
    delete [] chatAuth;
    delete customAttrs;
    delete plink;
    delete sharekey;
    delete children;
}

MegaUserPrivate::MegaUserPrivate(User *user) : MegaUser()
{
    email = MegaApi::strdup(user->email.c_str());
    handle = user->userhandle;
    visibility = user->show;
    ctime = user->ctime;
    tag = user->getTag();
    changed = 0;
    if (user->changed.authring)
    {
        changed |= MegaUser::CHANGE_TYPE_AUTHRING;
    }
    if(user->changed.avatar)
    {
        changed |= MegaUser::CHANGE_TYPE_AVATAR;
    }
    if(user->changed.lstint)
    {
        changed |= MegaUser::CHANGE_TYPE_LSTINT;
    }
    if(user->changed.firstname)
    {
        changed |= MegaUser::CHANGE_TYPE_FIRSTNAME;
    }
    if(user->changed.lastname)
    {
        changed |= MegaUser::CHANGE_TYPE_LASTNAME;
    }
    if(user->changed.email)
    {
        changed |= MegaUser::CHANGE_TYPE_EMAIL;
    }
    if(user->changed.keyring)
    {
        changed |= MegaUser::CHANGE_TYPE_KEYRING;
    }
    if(user->changed.country)
    {
        changed |= MegaUser::CHANGE_TYPE_COUNTRY;
    }
    if(user->changed.birthday)
    {
        changed |= MegaUser::CHANGE_TYPE_BIRTHDAY;
    }
    if(user->changed.puCu255)
    {
        changed |= MegaUser::CHANGE_TYPE_PUBKEY_CU255;
    }
    if(user->changed.puEd255)
    {
        changed |= MegaUser::CHANGE_TYPE_PUBKEY_ED255;
    }
    if(user->changed.sigPubk)
    {
        changed |= MegaUser::CHANGE_TYPE_SIG_PUBKEY_RSA;
    }
    if(user->changed.sigCu255)
    {
        changed |= MegaUser::CHANGE_TYPE_SIG_PUBKEY_CU255;
    }
    if(user->changed.language)
    {
        changed |= MegaUser::CHANGE_TYPE_LANGUAGE;
    }
    if(user->changed.pwdReminder)
    {
        changed |= MegaUser::CHANGE_TYPE_PWD_REMINDER;
    }
    if(user->changed.disableVersions)
    {
        changed |= MegaUser::CHANGE_TYPE_DISABLE_VERSIONS;
    }
    if(user->changed.contactLinkVerification)
    {
        changed |= MegaUser::CHANGE_TYPE_CONTACT_LINK_VERIFICATION;
    }
    if(user->changed.richPreviews)
    {
        changed |= MegaUser::CHANGE_TYPE_RICH_PREVIEWS;
    }
    if(user->changed.rubbishTime)
    {
        changed |= MegaUser::CHANGE_TYPE_RUBBISH_TIME;
    }
    if(user->changed.storageState)
    {
        changed |= MegaUser::CHANGE_TYPE_STORAGE_STATE;
    }
    if(user->changed.geolocation)
    {
        changed |= MegaUser::CHANGE_TYPE_GEOLOCATION;
    }
    if(user->changed.cameraUploadsFolder)
    {
        changed |= MegaUser::CHANGE_TYPE_CAMERA_UPLOADS_FOLDER;
    }
    if(user->changed.myChatFilesFolder)
    {
        changed |= MegaUser::CHANGE_TYPE_MY_CHAT_FILES_FOLDER;
    }
    if (user->changed.pushSettings)
    {
        changed |= MegaUser::CHANGE_TYPE_PUSH_SETTINGS;
    }
    if (user->changed.alias)
    {
        changed |= MegaUser::CHANGE_TYPE_ALIAS;
    }
    if (user->changed.unshareablekey)
    {
        changed |= MegaUser::CHANGE_TYPE_UNSHAREABLE_KEY;
    }
    if (user->changed.devicenames)
    {
        changed |= MegaUser::CHANGE_TYPE_DEVICE_NAMES;
    }
}

MegaUserPrivate::MegaUserPrivate(MegaUser *user) : MegaUser()
{
    email = MegaApi::strdup(user->getEmail());
    handle = user->getHandle();
    visibility = user->getVisibility();
    ctime = user->getTimestamp();
    changed = user->getChanges();
    tag = user->isOwnChange();
}

MegaUser *MegaUserPrivate::fromUser(User *user)
{
    if(!user)
    {
        return NULL;
    }
    return new MegaUserPrivate(user);
}

MegaUser *MegaUserPrivate::copy()
{
    return new MegaUserPrivate(this);
}

MegaUserPrivate::~MegaUserPrivate()
{
    delete[] email;
}

const char* MegaUserPrivate::getEmail()
{
    return email;
}

MegaHandle MegaUserPrivate::getHandle()
{
    return handle;
}

int MegaUserPrivate::getVisibility()
{
    return visibility;
}

int64_t MegaUserPrivate::getTimestamp()
{
    return ctime;
}

bool MegaUserPrivate::hasChanged(int changeType)
{
    return (changed & changeType);
}

int MegaUserPrivate::getChanges()
{
    return changed;
}

int MegaUserPrivate::isOwnChange()
{
    return tag;
}

MegaUserAlertPrivate::MegaUserAlertPrivate(UserAlert::Base *b, MegaClient* mc)
    : id(b->id)
    , seen(b->seen)
    , relevant(b->relevant)
    , type(-1)
    , tag(b->tag)
    , userHandle(UNDEF)
    , nodeHandle(UNDEF)
{
    b->text(heading, title, mc);
    timestamps.push_back(b->timestamp);

    switch (b->type)
    {
    case UserAlert::type_ipc:
    {
        UserAlert::IncomingPendingContact* p = static_cast<UserAlert::IncomingPendingContact*>(b);
        if (p->requestWasDeleted)
        {
            type = TYPE_INCOMINGPENDINGCONTACT_CANCELLED;
        }
        else if (p->requestWasReminded)
        {
            type = TYPE_INCOMINGPENDINGCONTACT_REMINDER;
        }
        else
        {
            type = TYPE_INCOMINGPENDINGCONTACT_REQUEST;
        }
        userHandle = p->userHandle;
        email = p->userEmail;
    }
    break;
    case UserAlert::type_c:
    {
        UserAlert::ContactChange* p = static_cast<UserAlert::ContactChange*>(b);
        switch (p->action)
        {
        case 0: type = TYPE_CONTACTCHANGE_DELETEDYOU; break;
        case 1: type = TYPE_CONTACTCHANGE_CONTACTESTABLISHED; break;
        case 2: type = TYPE_CONTACTCHANGE_ACCOUNTDELETED; break;
        case 3: type = TYPE_CONTACTCHANGE_BLOCKEDYOU; break;
        }
        userHandle = p->userHandle;
        email = p->userEmail;
    }
    break;
    case UserAlert::type_upci:
    {
        UserAlert::UpdatedPendingContactIncoming* p = static_cast<UserAlert::UpdatedPendingContactIncoming*>(b);
        switch (p->action)
        {
        case 1: type = TYPE_UPDATEDPENDINGCONTACTINCOMING_IGNORED; break;
        case 2: type = TYPE_UPDATEDPENDINGCONTACTINCOMING_ACCEPTED; break;
        case 3: type = TYPE_UPDATEDPENDINGCONTACTINCOMING_DENIED; break;
        }
        userHandle = p->userHandle;
        email = p->userEmail;
    }
    break;
    case UserAlert::type_upco:
    {
        UserAlert::UpdatedPendingContactOutgoing* p = static_cast<UserAlert::UpdatedPendingContactOutgoing*>(b);
        switch (p->action)
        {
        case 1: type = TYPE_UPDATEDPENDINGCONTACTINCOMING_IGNORED; break;
        case 2: type = TYPE_UPDATEDPENDINGCONTACTOUTGOING_ACCEPTED; break;
        case 3: type = TYPE_UPDATEDPENDINGCONTACTOUTGOING_DENIED; break;
        }
        userHandle = p->userHandle;
        email = p->userEmail;
    }
    break;
    case UserAlert::type_share:
    {
        UserAlert::NewShare* p = static_cast<UserAlert::NewShare*>(b);
        type = TYPE_NEWSHARE;
        userHandle = p->userHandle;
        email = p->userEmail;
        nodeHandle = p->folderhandle;
        if (Node* node = mc->nodebyhandle(p->folderhandle))
        {
            nodePath = node->displaypath();
            nodeName = node->displayname();
        }
    }
    break;
    case UserAlert::type_dshare:
    {
        UserAlert::DeletedShare* p = static_cast<UserAlert::DeletedShare*>(b);
        type = TYPE_DELETEDSHARE;
        userHandle = p->userHandle;
        email = p->userEmail;
        nodePath = p->folderPath;
        nodeName = p->folderName;
        nodeHandle = p->folderHandle;
        bool accessRevoked = p->userHandle == p->ownerHandle;
        numbers.push_back(accessRevoked ? 1 : 0);
    }
    break;
    case UserAlert::type_put:
    {
        UserAlert::NewSharedNodes* p = static_cast<UserAlert::NewSharedNodes*>(b);
        type = TYPE_NEWSHAREDNODES;
        userHandle = p->userHandle;
        email = p->userEmail;
        nodeHandle = p->parentHandle;
        numbers.push_back(p->folderCount);
        numbers.push_back(p->fileCount);
    }
    break;
    case UserAlert::type_d:
    {
        UserAlert::RemovedSharedNode* p = static_cast<UserAlert::RemovedSharedNode*>(b);
        type = TYPE_REMOVEDSHAREDNODES;
        userHandle = p->userHandle;
        email = p->userEmail;
        numbers.push_back(p->itemsNumber);
    }
    break;
    case UserAlert::type_psts:
    {
        UserAlert::Payment* p = static_cast<UserAlert::Payment*>(b);
        type = p->success ? TYPE_PAYMENT_SUCCEEDED : TYPE_PAYMENT_FAILED;
        extraStrings.push_back(p->getProPlanName());
    }
    break;
    case UserAlert::type_pses:
    {
        UserAlert::PaymentReminder* p = static_cast<UserAlert::PaymentReminder*>(b);
        type = TYPE_PAYMENTREMINDER;
        timestamps.push_back(p->expiryTime);
    }
    break;
    case UserAlert::type_ph:
    {
        UserAlert::Takedown* p = static_cast<UserAlert::Takedown*>(b);
        if (p->isTakedown)
        {
            type = TYPE_TAKEDOWN;
        } 
        else if (p->isReinstate)
        {
            type = TYPE_TAKEDOWN_REINSTATED;
        }
        nodeHandle = p->nodeHandle;
        Node* node = mc->nodebyhandle(nodeHandle);
        if (node)
        {
            nodePath = node->displaypath();
            nodeName = node->displayname();
        }
    }
    break;
    }
}

MegaUserAlert *MegaUserAlertPrivate::copy() const
{
    return new MegaUserAlertPrivate(*this);
}

unsigned MegaUserAlertPrivate::getId() const
{
    return id;
}

bool MegaUserAlertPrivate::getSeen() const
{
    return seen;
}

bool MegaUserAlertPrivate::getRelevant() const
{
    return relevant;
}

int MegaUserAlertPrivate::getType() const
{
    return type;
}

const char *MegaUserAlertPrivate::getTypeString() const
{
    switch (type)
    {
    case TYPE_INCOMINGPENDINGCONTACT_REQUEST:           return "NEW_CONTACT_REQUEST";
    case TYPE_INCOMINGPENDINGCONTACT_CANCELLED:         return "CONTACT_REQUEST_CANCELLED";
    case TYPE_INCOMINGPENDINGCONTACT_REMINDER:          return "CONTACT_REQUEST_REMINDED";
    case TYPE_CONTACTCHANGE_DELETEDYOU:                 return "CONTACT_DISCONNECTED";
    case TYPE_CONTACTCHANGE_CONTACTESTABLISHED:         return "CONTACT_ESTABLISHED";
    case TYPE_CONTACTCHANGE_ACCOUNTDELETED:             return "CONTACT_ACCOUNTDELETED";
    case TYPE_CONTACTCHANGE_BLOCKEDYOU:                 return "CONTACT_BLOCKED";
    case TYPE_UPDATEDPENDINGCONTACTINCOMING_IGNORED:    return "YOU_IGNORED_CONTACT";
    case TYPE_UPDATEDPENDINGCONTACTINCOMING_ACCEPTED:   return "YOU_ACCEPTED_CONTACT";
    case TYPE_UPDATEDPENDINGCONTACTINCOMING_DENIED:     return "YOU_DENIED_CONTACT";
    case TYPE_UPDATEDPENDINGCONTACTOUTGOING_ACCEPTED:   return "CONTACT_ACCEPTED_YOU";
    case TYPE_UPDATEDPENDINGCONTACTOUTGOING_DENIED:     return "CONTACT_DENIED_YOU";
    case TYPE_NEWSHARE:                                 return "NEW_SHARE";
    case TYPE_DELETEDSHARE:                             return "SHARE_UNSHARED";
    case TYPE_NEWSHAREDNODES:                           return "NEW_NODES_IN_SHARE";
    case TYPE_REMOVEDSHAREDNODES:                       return "NODES_IN_SHARE_REMOVED";
    case TYPE_PAYMENT_SUCCEEDED:                        return "PAYMENT_SUCCEEDED";
    case TYPE_PAYMENT_FAILED:                           return "PAYMENT_FAILED";
    case TYPE_PAYMENTREMINDER:                          return "PAYMENT_REMINDER";
    case TYPE_TAKEDOWN:                                 return "TAKEDOWN";
    case TYPE_TAKEDOWN_REINSTATED:                      return "TAKEDOWN_REINSTATED";
    }
    return "<new type>";
}

MegaHandle MegaUserAlertPrivate::getUserHandle() const
{
    return userHandle;
}

MegaHandle MegaUserAlertPrivate::getNodeHandle() const
{
    return nodeHandle;
}

const char* MegaUserAlertPrivate::getEmail() const
{
    return email.empty() ? NULL : email.c_str();
}

const char*MegaUserAlertPrivate::getPath() const
{
    return  nodePath.empty() ? NULL : nodePath.c_str();
}

const char *MegaUserAlertPrivate::getName() const
{
    return  nodeName.empty() ? NULL : nodeName.c_str();
}

const char *MegaUserAlertPrivate::getHeading() const
{
    return heading.c_str();
}

const char *MegaUserAlertPrivate::getTitle() const
{
    return title.c_str();
}

int64_t MegaUserAlertPrivate::getNumber(unsigned index) const
{
    return index < numbers.size() ? numbers[index] : -1;
}

int64_t MegaUserAlertPrivate::getTimestamp(unsigned index) const
{
    return index < timestamps.size() ? timestamps[index] : -1;
}

const char* MegaUserAlertPrivate::getString(unsigned index) const
{
    return index < extraStrings.size() ? extraStrings[index].c_str() : NULL;
}

bool MegaUserAlertPrivate::isOwnChange() const
{
    return tag != 0;
}

MegaNode *MegaNodePrivate::fromNode(Node *node)
{
    if(!node) return NULL;
    return new MegaNodePrivate(node);
}

MegaSharePrivate::MegaSharePrivate(MegaShare *share) : MegaShare()
{
    this->nodehandle = share->getNodeHandle();
    this->user = MegaApi::strdup(share->getUser());
    this->access = share->getAccess();
    this->ts = share->getTimestamp();
    this->pending = share->isPending();
}

MegaShare *MegaSharePrivate::copy()
{
    return new MegaSharePrivate(this);
}

MegaSharePrivate::MegaSharePrivate(uint64_t handle, Share *share)
{
    this->nodehandle = handle;
    this->user = share->user ? MegaApi::strdup(share->user->email.c_str()) : NULL;
    if ((!user || !*user) && share->pcr)
    {
        delete [] user;
        user = MegaApi::strdup(share->pcr->isoutgoing ? share->pcr->targetemail.c_str() : share->pcr->originatoremail.c_str());
    }
    this->access = share->access;
    this->ts = share->ts;
    this->pending = share->pcr;
}

MegaShare *MegaSharePrivate::fromShare(uint64_t nodeuint64_t, Share *share)
{
    return new MegaSharePrivate(nodeuint64_t, share);
}

MegaSharePrivate::~MegaSharePrivate()
{
    delete[] user;
}

const char *MegaSharePrivate::getUser()
{
    return user;
}

uint64_t MegaSharePrivate::getNodeHandle()
{
    return nodehandle;
}

int MegaSharePrivate::getAccess()
{
    return access;
}

int64_t MegaSharePrivate::getTimestamp()
{
    return ts;
}

bool MegaSharePrivate::isPending()
{
    return pending;
}


MegaTransferPrivate::MegaTransferPrivate(int type, MegaTransferListener *listener)
{
    this->type = type;
    this->tag = -1;
    this->path = NULL;
    this->nodeHandle = UNDEF;
    this->parentHandle = UNDEF;
    this->startPos = -1;
    this->endPos = -1;
    this->parentPath = NULL;
    this->listener = listener;
    this->retry = 0;
    this->maxRetries = 7;
    this->time = -1;
    this->startTime = 0;
    this->transferredBytes = 0;
    this->totalBytes = 0;
    this->fileName = NULL;
    this->transfer = NULL;
    this->speed = 0;
    this->deltaSize = 0;
    this->updateTime = 0;
    this->publicNode = NULL;
    this->lastBytes = NULL;
    this->syncTransfer = false;
    this->streamingTransfer = false;
    this->temporarySourceFile = false;
    this->startFirst = false;
    this->backupTransfer = false;
    this->foreignOverquota = false;
    this->folderTransferTag = 0;
    this->appData = NULL;
    this->state = STATE_NONE;
    this->priority = 0;
    this->meanSpeed = 0;
    this->notificationNumber = 0;
}

MegaTransferPrivate::MegaTransferPrivate(const MegaTransferPrivate *transfer)
{
    path = NULL;
    parentPath = NULL;
    fileName = NULL;
    publicNode = NULL;
    lastBytes = NULL;
    appData = NULL;

    this->listener = transfer->getListener();
    this->transfer = transfer->getTransfer();
    this->type = transfer->getType();
    this->setState(transfer->getState());
    this->setPriority(transfer->getPriority());
    this->setTag(transfer->getTag());
    this->setPath(transfer->getPath());
    this->setNodeHandle(transfer->getNodeHandle());
    this->setParentHandle(transfer->getParentHandle());
    this->setStartPos(transfer->getStartPos());
    this->setEndPos(transfer->getEndPos());
    this->setParentPath(transfer->getParentPath());
    this->setNumRetry(transfer->getNumRetry());
    this->setMaxRetries(transfer->getMaxRetries());
    this->setTime(transfer->getTime());
    this->startTime = transfer->getStartTime();
    this->setTransferredBytes(transfer->getTransferredBytes());
    this->setTotalBytes(transfer->getTotalBytes());
    this->setFileName(transfer->getFileName());
    this->setSpeed(transfer->getSpeed());
    this->setMeanSpeed(transfer->getMeanSpeed());
    this->setDeltaSize(transfer->getDeltaSize());
    this->setUpdateTime(transfer->getUpdateTime());
    this->setPublicNode(transfer->getPublicNode());
    this->setTransfer(transfer->getTransfer());
    this->setSyncTransfer(transfer->isSyncTransfer());
    this->setStreamingTransfer(transfer->isStreamingTransfer());
    this->setSourceFileTemporary(transfer->isSourceFileTemporary());
    this->setStartFirst(transfer->shouldStartFirst());
    this->setBackupTransfer(transfer->isBackupTransfer());
    this->setForeignOverquota(transfer->isForeignOverquota());
    this->setLastError(transfer->lastError.get());
    this->setFolderTransferTag(transfer->getFolderTransferTag());
    this->setAppData(transfer->getAppData());
    this->setNotificationNumber(transfer->getNotificationNumber());
}

MegaTransfer* MegaTransferPrivate::copy()
{
    return new MegaTransferPrivate(this);
}

void MegaTransferPrivate::setTransfer(Transfer *transfer)
{
    this->transfer = transfer;
}

Transfer* MegaTransferPrivate::getTransfer() const
{
    return transfer;
}

int MegaTransferPrivate::getTag() const
{
    return tag;
}

long long MegaTransferPrivate::getSpeed() const
{
    return speed;
}

long long MegaTransferPrivate::getMeanSpeed() const
{
    return meanSpeed;
}

long long MegaTransferPrivate::getDeltaSize() const
{
    return deltaSize;
}

int64_t MegaTransferPrivate::getUpdateTime() const
{
    return updateTime;
}

MegaNode *MegaTransferPrivate::getPublicNode() const
{
    return publicNode;
}

MegaNode *MegaTransferPrivate::getPublicMegaNode() const
{
    if(publicNode)
    {
        return publicNode->copy();
    }

    return NULL;
}

bool MegaTransferPrivate::isSyncTransfer() const
{
    return syncTransfer;
}

bool MegaTransferPrivate::isStreamingTransfer() const
{
    return streamingTransfer;
}

bool MegaTransferPrivate::isFinished() const
{
    return state == STATE_COMPLETED || state == STATE_CANCELLED || state == STATE_FAILED;
}

bool MegaTransferPrivate::isBackupTransfer() const
{
    return backupTransfer;
}

bool MegaTransferPrivate::isForeignOverquota() const
{
    return foreignOverquota;
}

bool MegaTransferPrivate::isSourceFileTemporary() const
{
    return temporarySourceFile;
}

bool MegaTransferPrivate::shouldStartFirst() const
{
    return startFirst;
}

int MegaTransferPrivate::getType() const
{
    return type;
}

int64_t MegaTransferPrivate::getStartTime() const
{
    return startTime;
}

long long MegaTransferPrivate::getTransferredBytes() const
{
    return transferredBytes;
}

long long MegaTransferPrivate::getTotalBytes() const
{
    return totalBytes;
}

const char* MegaTransferPrivate::getPath() const
{
    return path;
}

const char* MegaTransferPrivate::getParentPath() const
{
    return parentPath;
}

uint64_t MegaTransferPrivate::getNodeHandle() const
{
    return nodeHandle;
}

uint64_t MegaTransferPrivate::getParentHandle() const
{
    return parentHandle;
}

long long MegaTransferPrivate::getStartPos() const
{
    return startPos;
}

long long MegaTransferPrivate::getEndPos() const
{
    return endPos;
}

int MegaTransferPrivate::getNumRetry() const
{
    return retry;
}

int MegaTransferPrivate::getMaxRetries() const
{
    return maxRetries;
}

int64_t MegaTransferPrivate::getTime() const
{
    return time;
}

const char* MegaTransferPrivate::getFileName() const
{
    return fileName;
}

char * MegaTransferPrivate::getLastBytes() const
{
    return lastBytes;
}

MegaError MegaTransferPrivate::getLastError() const
{
    return lastError ? *lastError.get() : MegaTransfer::getLastError();
}

const MegaError *MegaTransferPrivate::getLastErrorExtended() const
{
    return lastError.get();
}

bool MegaTransferPrivate::isFolderTransfer() const
{
    return folderTransferTag < 0;
}

int MegaTransferPrivate::getFolderTransferTag() const
{
    return this->folderTransferTag;
}

void MegaTransferPrivate::setAppData(const char *data)
{
    if (this->appData)
    {
        delete [] this->appData;
    }
    this->appData = MegaApi::strdup(data);
}

const char *MegaTransferPrivate::getAppData() const
{
    return this->appData;
}

void MegaTransferPrivate::setState(int state)
{
    this->state = state;
}

int MegaTransferPrivate::getState() const
{
    return state;
}

void MegaTransferPrivate::setPriority(unsigned long long p)
{
    this->priority = p;
}

unsigned long long MegaTransferPrivate::getPriority() const
{
    return priority;
}

long long MegaTransferPrivate::getNotificationNumber() const
{
    return notificationNumber;
}

bool MegaTransferPrivate::serialize(string *d)
{
    d->append((const char*)&type, sizeof(type));
    d->append((const char*)&nodeHandle, sizeof(nodeHandle));
    d->append((const char*)&parentHandle, sizeof(parentHandle));

    unsigned short ll;
    ll = (unsigned short)(path ? strlen(path) + 1 : 0);
    d->append((char*)&ll, sizeof(ll));
    d->append(path, ll);

    ll = (unsigned short)(parentPath ? strlen(parentPath) + 1 : 0);
    d->append((char*)&ll, sizeof(ll));
    d->append(parentPath, ll);

    ll = (unsigned short)(fileName ? strlen(fileName) + 1 : 0);
    d->append((char*)&ll, sizeof(ll));
    d->append(fileName, ll);

    d->append((const char*)&folderTransferTag, sizeof(folderTransferTag));
    d->append("\0\0\0\0\0\0", 7);

    ll = (unsigned short)(appData ? strlen(appData) + 1 : 0);
    if (ll)
    {
        char hasAppData = 1;
        d->append(&hasAppData, 1);
        d->append((char*)&ll, sizeof(ll));
        d->append(appData, ll);
    }
    else
    {
        d->append("", 1);
    }

    MegaNodePrivate *node = dynamic_cast<MegaNodePrivate *>(publicNode);
    bool isPublic = (node != NULL);
    d->append((const char*)&isPublic, sizeof(bool));
    if (isPublic)
    {
        node->serialize(d);
    }
    return true;
}

MegaTransferPrivate *MegaTransferPrivate::unserialize(string *d)
{
    const char* ptr = d->data();
    const char* end = ptr + d->size();

    if (ptr + sizeof(int) + sizeof(MegaHandle)
            + sizeof(MegaHandle) + sizeof(unsigned short) > end)
    {
        LOG_err << "MegaTransfer unserialization failed - data too short";
        return NULL;
    }

    int type = MemAccess::get<int>(ptr);
    ptr += sizeof(int);

    MegaTransferPrivate *transfer = new MegaTransferPrivate(type);
    transfer->nodeHandle = MemAccess::get<MegaHandle>(ptr);
    ptr += sizeof(MegaHandle);

    transfer->parentHandle = MemAccess::get<MegaHandle>(ptr);
    ptr += sizeof(MegaHandle);

    unsigned short pathlen = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(unsigned short);

    if (ptr + pathlen + sizeof(unsigned short) > end)
    {
        LOG_err << "MegaTransfer unserialization failed - path too long";
        delete transfer;
        return NULL;
    }

    if (pathlen)
    {
        string path;
        path.assign(ptr, pathlen - 1);
        transfer->setPath(path.c_str());
    }
    ptr += pathlen;

    unsigned short parentPathLen = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(unsigned short);

    if (ptr + parentPathLen + sizeof(unsigned short) > end)
    {
        LOG_err << "MegaTransfer unserialization failed - parentpath too long";
        delete transfer;
        return NULL;
    }

    if (parentPathLen)
    {
        string path;
        path.assign(ptr, parentPathLen - 1);
        transfer->setParentPath(path.c_str());
    }
    ptr += parentPathLen;

    unsigned short fileNameLen = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(unsigned short);

    if (ptr + fileNameLen + sizeof(int) + 7 + sizeof(char) > end)
    {
        LOG_err << "MegaTransfer unserialization failed - filename too long";
        delete transfer;
        return NULL;
    }

    if (fileNameLen)
    {
        string path;
        path.assign(ptr, fileNameLen - 1);
        transfer->setFileName(path.c_str());
    }
    ptr += fileNameLen;

    transfer->folderTransferTag = MemAccess::get<int>(ptr);
    ptr += sizeof(int);

    if (memcmp(ptr, "\0\0\0\0\0\0", 7))
    {
        LOG_err << "MegaTransfer unserialization failed - invalid version";
        delete transfer;
        return NULL;
    }
    ptr += 7;

    char hasAppData = MemAccess::get<char>(ptr);
    ptr += sizeof(char);
    if (hasAppData > 1)
    {
        LOG_err << "MegaTransfer unserialization failed - invalid app data";
        delete transfer;
        return NULL;
    }

    if (hasAppData)
    {
        if (ptr + sizeof(unsigned short) > end)
        {
            LOG_err << "MegaTransfer unserialization failed - no app data header";
            delete transfer;
            return NULL;
        }

        unsigned short appDataLen = MemAccess::get<unsigned short>(ptr);
        ptr += sizeof(unsigned short);
        if (!appDataLen || (ptr + appDataLen > end))
        {
            LOG_err << "MegaTransfer unserialization failed - invalid appData";
            delete transfer;
            return NULL;
        }

        string data;
        data.assign(ptr, appDataLen - 1);
        transfer->setAppData(data.c_str());
        ptr += appDataLen;
    }

    if (ptr + sizeof(bool) > end)
    {
        LOG_err << "MegaTransfer unserialization failed - reading public node";
        delete transfer;
        return NULL;
    }

    bool isPublic = MemAccess::get<bool>(ptr);
    ptr += sizeof(bool);

    d->erase(0, ptr - d->data());

    if (isPublic)
    {
        MegaNodePrivate *publicNode = MegaNodePrivate::unserialize(d);
        if (!publicNode)
        {
            LOG_err << "MegaTransfer unserialization failed - unable to unserialize MegaNode";
            delete transfer;
            return NULL;
        }

        transfer->setPublicNode(publicNode);
        delete publicNode;
    }

    return transfer;
}

void MegaTransferPrivate::setTag(int tag)
{
    this->tag = tag;
}

void MegaTransferPrivate::setSpeed(long long speed)
{
    this->speed = speed;
}

void MegaTransferPrivate::setMeanSpeed(long long meanSpeed)
{
    this->meanSpeed = meanSpeed;
}

void MegaTransferPrivate::setDeltaSize(long long deltaSize)
{
    this->deltaSize = deltaSize;
}

void MegaTransferPrivate::setUpdateTime(int64_t updateTime)
{
    this->updateTime = updateTime;
}
void MegaTransferPrivate::setPublicNode(MegaNode *publicNode, bool copyChildren)
{
    if (this->publicNode)
    {
        delete this->publicNode;
    }

    if (!publicNode)
    {
        this->publicNode = NULL;
    }
    else
    {
        MegaNodePrivate *nodePrivate = new MegaNodePrivate(publicNode);
        MegaNodeListPrivate *children = dynamic_cast<MegaNodeListPrivate *>(publicNode->getChildren());
        if (children && copyChildren)
        {
            nodePrivate->setChildren(new MegaNodeListPrivate(children, true));
        }
        this->publicNode = nodePrivate;
    }
}

void MegaTransferPrivate::setSyncTransfer(bool syncTransfer)
{
    this->syncTransfer = syncTransfer;
}

void MegaTransferPrivate::setSourceFileTemporary(bool temporary)
{
    this->temporarySourceFile = temporary;
}

void MegaTransferPrivate::setStartFirst(bool startFirst)
{
    this->startFirst = startFirst;
}

void MegaTransferPrivate::setBackupTransfer(bool backupTransfer)
{
    this->backupTransfer = backupTransfer;
}

void MegaTransferPrivate::setForeignOverquota(bool foreignOverquota)
{
    this->foreignOverquota = foreignOverquota;
}

void MegaTransferPrivate::setStreamingTransfer(bool streamingTransfer)
{
    this->streamingTransfer = streamingTransfer;
}

void MegaTransferPrivate::setStartTime(int64_t startTime)
{
    if (!this->startTime)
    {
        this->startTime = startTime;
    }
}

void MegaTransferPrivate::setTransferredBytes(long long transferredBytes)
{
    this->transferredBytes = transferredBytes;
}

void MegaTransferPrivate::setTotalBytes(long long totalBytes)
{
    this->totalBytes = totalBytes;
}

void MegaTransferPrivate::setLastBytes(char *lastBytes)
{
    this->lastBytes = lastBytes;
}

void MegaTransferPrivate::setLastError(const MegaError *e)
{
   lastError.reset(e ? e->copy() : nullptr);
}

void MegaTransferPrivate::setFolderTransferTag(int tag)
{
    this->folderTransferTag = tag;
}

void MegaTransferPrivate::setNotificationNumber(long long notificationNumber)
{
    this->notificationNumber = notificationNumber;
}

void MegaTransferPrivate::setListener(MegaTransferListener *listener)
{
    this->listener = listener;
}

void MegaTransferPrivate::startRecursiveOperation(unique_ptr<MegaRecursiveOperation> op, MegaNode* node)
{
    assert(op && !recursiveOperation);
    recursiveOperation = move(op);
    recursiveOperation->start(node);
}

long long MegaTransferPrivate::getPlaceInQueue() const
{
    return placeInQueue;
}

void MegaTransferPrivate::setPlaceInQueue(long long value)
{
    placeInQueue = value;
}

void MegaTransferPrivate::setPath(const char* path)
{
    if(this->path) delete [] this->path;
    this->path = MegaApi::strdup(path);
    if(!this->path) return;

    for (int i = int(strlen(path) - 1); i >= 0; i--)
    {
        if (strchr(FileSystemAccess::getPathSeparator(), path[i]))
        {
            setFileName(&(path[i+1]));
            char *parentPath = MegaApi::strdup(path);
            parentPath[i+1] = '\0';
            setParentPath(parentPath);
            delete [] parentPath;
            return;
        }
    }
    setFileName(path);
}

void MegaTransferPrivate::setParentPath(const char* path)
{
    if(this->parentPath) delete [] this->parentPath;
    this->parentPath =  MegaApi::strdup(path);
}

void MegaTransferPrivate::setFileName(const char* fileName)
{
    if(this->fileName) delete [] this->fileName;
    this->fileName =  MegaApi::strdup(fileName);
}

void MegaTransferPrivate::setNodeHandle(uint64_t nodeHandle)
{
    this->nodeHandle = nodeHandle;
}

void MegaTransferPrivate::setParentHandle(uint64_t parentHandle)
{
    this->parentHandle = parentHandle;
}

void MegaTransferPrivate::setStartPos(long long startPos)
{
    this->startPos = startPos;
}

void MegaTransferPrivate::setEndPos(long long endPos)
{
    this->endPos = endPos;
}

void MegaTransferPrivate::setNumRetry(int retry)
{
    this->retry = retry;
}

void MegaTransferPrivate::setMaxRetries(int maxRetries)
{
    this->maxRetries = maxRetries;
}

void MegaTransferPrivate::setTime(int64_t time)
{
    this->time = time;
}

const char * MegaTransferPrivate::getTransferString() const
{
    switch(type)
    {
    case TYPE_UPLOAD:
        return "UPLOAD";
    case TYPE_DOWNLOAD:
        return "DOWNLOAD";
    case TYPE_LOCAL_TCP_DOWNLOAD:
        return "LOCAL_HTTP_DOWNLOAD";
    }

    return "UNKNOWN";
}

MegaTransferListener* MegaTransferPrivate::getListener() const
{
    return listener;
}

MegaTransferPrivate::~MegaTransferPrivate()
{
    if (recursiveOperation)
    {
        recursiveOperation->cancel();
    }
    delete[] path;
    delete[] parentPath;
    delete [] fileName;
    delete [] appData;
    delete publicNode;
}

const char * MegaTransferPrivate::toString() const
{
    return getTransferString();
}

const char * MegaTransferPrivate::__str__() const
{
    return getTransferString();
}

const char *MegaTransferPrivate::__toString() const
{
    return getTransferString();
}

MegaContactRequestPrivate::MegaContactRequestPrivate(PendingContactRequest *request)
{
    handle = request->id;
    sourceEmail = request->originatoremail.size() ? MegaApi::strdup(request->originatoremail.c_str()) : NULL;
    sourceMessage = request->msg.size() ? MegaApi::strdup(request->msg.c_str()) : NULL;
    targetEmail = request->targetemail.size() ? MegaApi::strdup(request->targetemail.c_str()) : NULL;
    creationTime = request->ts;
    modificationTime = request->uts;
    autoaccepted = request->autoaccepted;

    if(request->changed.accepted)
    {
        status = MegaContactRequest::STATUS_ACCEPTED;
    }
    else if(request->changed.deleted)
    {
        status = MegaContactRequest::STATUS_DELETED;
    }
    else if(request->changed.denied)
    {
        status = MegaContactRequest::STATUS_DENIED;
    }
    else if(request->changed.ignored)
    {
        status = MegaContactRequest::STATUS_IGNORED;
    }
    else if(request->changed.reminded)
    {
        status = MegaContactRequest::STATUS_REMINDED;
    }
    else
    {
        status = MegaContactRequest::STATUS_UNRESOLVED;
    }

    outgoing = request->isoutgoing;
}

MegaContactRequestPrivate::MegaContactRequestPrivate(const MegaContactRequest *request)
{
    handle = request->getHandle();
    sourceEmail = MegaApi::strdup(request->getSourceEmail());
    sourceMessage = MegaApi::strdup(request->getSourceMessage());
    targetEmail = MegaApi::strdup(request->getTargetEmail());
    creationTime = request->getCreationTime();
    modificationTime = request->getModificationTime();
    status = request->getStatus();
    outgoing = request->isOutgoing();
    autoaccepted = request->isAutoAccepted();
}

MegaContactRequestPrivate::~MegaContactRequestPrivate()
{
    delete [] sourceEmail;
    delete [] sourceMessage;
    delete [] targetEmail;
}

MegaContactRequest *MegaContactRequestPrivate::fromContactRequest(PendingContactRequest *request)
{
    return new MegaContactRequestPrivate(request);
}

MegaContactRequest *MegaContactRequestPrivate::copy() const
{
    return new MegaContactRequestPrivate(this);
}

MegaHandle MegaContactRequestPrivate::getHandle() const
{
    return handle;
}

char *MegaContactRequestPrivate::getSourceEmail() const
{
    return sourceEmail;
}

char *MegaContactRequestPrivate::getSourceMessage() const
{
    return sourceMessage;
}

char *MegaContactRequestPrivate::getTargetEmail() const
{
    return targetEmail;
}

int64_t MegaContactRequestPrivate::getCreationTime() const
{
    return creationTime;
}

int64_t MegaContactRequestPrivate::getModificationTime() const
{
    return modificationTime;
}

int MegaContactRequestPrivate::getStatus() const
{
    return status;
}

bool MegaContactRequestPrivate::isOutgoing() const
{
    return outgoing;
}

bool MegaContactRequestPrivate::isAutoAccepted() const
{
    return autoaccepted;
}

MegaAccountDetails *MegaAccountDetailsPrivate::fromAccountDetails(AccountDetails *details)
{
    return new MegaAccountDetailsPrivate(details);
}

MegaAccountDetailsPrivate::MegaAccountDetailsPrivate(AccountDetails *details)
{
    this->details = (*details);
}

MegaAccountDetailsPrivate::~MegaAccountDetailsPrivate()
{ }

MegaRequest *MegaRequestPrivate::copy()
{
    return new MegaRequestPrivate(this);
}

MegaRequestPrivate::MegaRequestPrivate(int type, MegaRequestListener *listener)
{
    this->type = type;
    this->tag = 0;
    this->transfer = 0;
    this->listener = listener;
#ifdef ENABLE_SYNC
    this->syncListener = NULL;
    this->regExp = NULL;
#endif
    this->backupListener = NULL;
    this->nodeHandle = UNDEF;
    this->link = NULL;
    this->parentHandle = UNDEF;
    this->sessionKey = NULL;
    this->name = NULL;
    this->email = NULL;
    this->text = NULL;
    this->password = NULL;
    this->newPassword = NULL;
    this->privateKey = NULL;
    this->access = MegaShare::ACCESS_UNKNOWN;
    this->numRetry = 0;
    this->publicNode = NULL;
    this->numDetails = 0;
    this->file = NULL;
    this->attrType = 0;
    this->flag = false;
    this->totalBytes = -1;
    this->transferredBytes = 0;
    this->number = 0;
    this->timeZoneDetails = NULL;

    if (type == MegaRequest::TYPE_ACCOUNT_DETAILS)
    {
        this->accountDetails = new AccountDetails();
    }
    else
    {
        this->accountDetails = NULL;
    }

    if (type == MegaRequest::TYPE_GET_ACHIEVEMENTS)
    {
        this->achievementsDetails = new AchievementsDetails();
    }
    else
    {
        this->achievementsDetails = NULL;
    }

    if ((type == MegaRequest::TYPE_GET_PRICING) || (type == MegaRequest::TYPE_GET_PAYMENT_ID) || type == MegaRequest::TYPE_UPGRADE_ACCOUNT)
    {
        this->megaPricing = new MegaPricingPrivate();
    }
    else
    {
        megaPricing = NULL;
    }

#ifdef ENABLE_CHAT
    if (type == MegaRequest::TYPE_CHAT_CREATE)
    {
        this->chatPeerList = new MegaTextChatPeerListPrivate();
    }
    else
    {
        this->chatPeerList = NULL;
    }

    if (type == MegaRequest::TYPE_CHAT_FETCH)
    {
        this->chatList = new MegaTextChatListPrivate();
    }
    else
    {
        this->chatList = NULL;
    }
#endif

    stringMap = NULL;
    mStringListMap = NULL;
    mStringTable = NULL;
    folderInfo = NULL;
    settings = NULL;
    backgroundMediaUpload = NULL;
}

MegaRequestPrivate::MegaRequestPrivate(MegaRequestPrivate *request)
{
    this->link = NULL;
    this->sessionKey = NULL;
    this->name = NULL;
    this->email = NULL;
    this->text = NULL;
    this->password = NULL;
    this->newPassword = NULL;
    this->privateKey = NULL;
    this->access = MegaShare::ACCESS_UNKNOWN;
    this->file = NULL;
    this->publicNode = NULL;
    this->type = request->getType();
    this->setTag(request->getTag());
    this->setNodeHandle(request->getNodeHandle());
    this->setLink(request->getLink());
    this->setParentHandle(request->getParentHandle());
    this->setSessionKey(request->getSessionKey());
    this->setName(request->getName());
    this->setEmail(request->getEmail());
    this->setPassword(request->getPassword());
    this->setNewPassword(request->getNewPassword());
    this->setPrivateKey(request->getPrivateKey());
    this->setAccess(request->getAccess());
    this->setNumRetry(request->getNumRetry());
    this->setNumDetails(request->getNumDetails());
    this->setFile(request->getFile());
    this->setParamType(request->getParamType());
    this->setText(request->getText());
    this->setNumber(request->getNumber());
    this->setPublicNode(request->getPublicNode());
    this->setFlag(request->getFlag());
    this->setTransferTag(request->getTransferTag());
    this->setTotalBytes(request->getTotalBytes());
    this->setTransferredBytes(request->getTransferredBytes());
    this->listener = request->getListener();

#ifdef ENABLE_SYNC
    this->regExp = NULL;
    this->setRegExp(request->getRegExp());
    this->syncListener = request->getSyncListener();
#endif
    this->backupListener = request->getBackupListener();
    this->megaPricing = (MegaPricingPrivate *)request->getPricing();

    this->accountDetails = NULL;
    if(request->getAccountDetails())
    {
        this->accountDetails = new AccountDetails();
        *(this->accountDetails) = *(request->getAccountDetails());
    }

    this->achievementsDetails = NULL;
    if(request->getAchievementsDetails())
    {
        this->achievementsDetails = new AchievementsDetails();
        *(this->achievementsDetails) = *(request->getAchievementsDetails());
    }

    this->timeZoneDetails = request->getMegaTimeZoneDetails() ? request->timeZoneDetails->copy() : NULL;

#ifdef ENABLE_CHAT   
    this->chatPeerList = request->getMegaTextChatPeerList() ? request->chatPeerList->copy() : NULL;
    this->chatList = request->getMegaTextChatList() ? request->chatList->copy() : NULL;
#endif

    this->stringMap = request->getMegaStringMap() ? request->stringMap->copy() : NULL;
    this->mStringListMap = request->getMegaStringListMap() ? request->mStringListMap->copy() : NULL;
    this->mStringTable = request->getMegaStringTable() ? request->mStringTable->copy() : NULL;
    this->folderInfo = request->getMegaFolderInfo() ? request->folderInfo->copy() : NULL;
    this->settings = request->getMegaPushNotificationSettings() ? request->settings->copy() : NULL;
    this->backgroundMediaUpload = NULL;
}

AccountDetails *MegaRequestPrivate::getAccountDetails() const
{
    return accountDetails;
}

MegaAchievementsDetails *MegaRequestPrivate::getMegaAchievementsDetails() const
{
    if (achievementsDetails)
    {
        return MegaAchievementsDetailsPrivate::fromAchievementsDetails(achievementsDetails);
    }
    return NULL;
}

AchievementsDetails *MegaRequestPrivate::getAchievementsDetails() const
{
    return achievementsDetails;
}

MegaTimeZoneDetails *MegaRequestPrivate::getMegaTimeZoneDetails() const
{
    return timeZoneDetails;
}

#ifdef ENABLE_CHAT
MegaTextChatPeerList *MegaRequestPrivate::getMegaTextChatPeerList() const
{
    return chatPeerList;
}

void MegaRequestPrivate::setMegaTextChatPeerList(MegaTextChatPeerList *chatPeers)
{
    if (this->chatPeerList)
        delete this->chatPeerList;

    this->chatPeerList = chatPeers->copy();
}

MegaTextChatList *MegaRequestPrivate::getMegaTextChatList() const
{
    return chatList;
}

void MegaRequestPrivate::setMegaTextChatList(MegaTextChatList *chatList)
{
    if (this->chatList)
        delete this->chatList;

    this->chatList = chatList->copy();
}
#endif

MegaStringMap *MegaRequestPrivate::getMegaStringMap() const
{
    return stringMap;
}

void MegaRequestPrivate::setMegaStringMap(const MegaStringMap *stringMap)
{
    if (this->stringMap)
    {
        delete this->stringMap;
    }

    this->stringMap = stringMap ? stringMap->copy() : NULL;
}

MegaStringListMap *MegaRequestPrivate::getMegaStringListMap() const
{
    return mStringListMap;
}

void MegaRequestPrivate::setMegaStringListMap(const MegaStringListMap* stringListMap)
{
    if (mStringListMap)
    {
        delete mStringListMap;
    }
    mStringListMap = stringListMap ? stringListMap->copy() : nullptr;
}

MegaStringTable *MegaRequestPrivate::getMegaStringTable() const
{
    return mStringTable;
}

void MegaRequestPrivate::setMegaStringTable(const MegaStringTable* stringTable)
{
    if (mStringTable)
    {
        delete mStringTable;
    }
    mStringTable = stringTable ? stringTable->copy() : nullptr;
}

MegaFolderInfo *MegaRequestPrivate::getMegaFolderInfo() const
{
    return folderInfo;
}

void MegaRequestPrivate::setMegaFolderInfo(const MegaFolderInfo *folderInfo)
{
    if (this->folderInfo)
    {
        delete this->folderInfo;
    }

    this->folderInfo = folderInfo ? folderInfo->copy() : NULL;
}

const MegaPushNotificationSettings *MegaRequestPrivate::getMegaPushNotificationSettings() const
{
    return settings;
}

void MegaRequestPrivate::setMegaPushNotificationSettings(const MegaPushNotificationSettings *settings)
{
    if (this->settings)
    {
        delete this->settings;
    }

    this->settings = settings ? settings->copy() : NULL;
}

MegaBackgroundMediaUpload *MegaRequestPrivate::getMegaBackgroundMediaUploadPtr() const
{
    // non-owned pointer
    return backgroundMediaUpload;
}

void MegaRequestPrivate::setMegaBackgroundMediaUploadPtr(MegaBackgroundMediaUpload *p)
{
    // non-owned pointer
    backgroundMediaUpload = p;
}


#ifdef ENABLE_SYNC
void MegaRequestPrivate::setSyncListener(MegaSyncListener *syncListener)
{
    this->syncListener = syncListener;
}

MegaSyncListener *MegaRequestPrivate::getSyncListener() const
{
    return syncListener;
}
MegaRegExp *MegaRequestPrivate::getRegExp() const
{
    return regExp;
}

void MegaRequestPrivate::setRegExp(MegaRegExp *regExp)
{
    if (this->regExp)
    {
        delete this->regExp;
    }

    if (!regExp)
    {
        this->regExp = NULL;
    }
    else
    {
        this->regExp = regExp->copy();
    }
}
#endif

MegaBackupListener *MegaRequestPrivate::getBackupListener() const
{
    return backupListener;
}

void MegaRequestPrivate::setBackupListener(MegaBackupListener *value)
{
    backupListener = value;
}

MegaAccountDetails *MegaRequestPrivate::getMegaAccountDetails() const
{
    if(accountDetails)
    {
        return MegaAccountDetailsPrivate::fromAccountDetails(accountDetails);
    }
    return NULL;
}

MegaRequestPrivate::~MegaRequestPrivate()
{
    delete [] link;
    delete [] name;
    delete [] email;
    delete [] password;
    delete [] newPassword;
    delete [] privateKey;
    delete [] sessionKey;
    delete publicNode;
    delete [] file;
    delete accountDetails;
    delete megaPricing;
    delete achievementsDetails;
    delete [] text;
    delete stringMap;
    delete mStringListMap;
    delete mStringTable;
    delete folderInfo;
    delete timeZoneDetails;
    delete settings;

#ifdef ENABLE_SYNC
    delete regExp;
#endif
#ifdef ENABLE_CHAT
    delete chatPeerList;
    delete chatList;
#endif
}

int MegaRequestPrivate::getType() const
{
    return type;
}

uint64_t MegaRequestPrivate::getNodeHandle() const
{
    return nodeHandle;
}

const char* MegaRequestPrivate::getLink() const
{
    return link;
}

uint64_t MegaRequestPrivate::getParentHandle() const
{
    return parentHandle;
}

const char* MegaRequestPrivate::getSessionKey() const
{
    return sessionKey;
}

const char* MegaRequestPrivate::getName() const
{
    return name;
}

const char* MegaRequestPrivate::getEmail() const
{
    return email;
}

const char* MegaRequestPrivate::getPassword() const
{
    return password;
}

const char* MegaRequestPrivate::getNewPassword() const
{
    return newPassword;
}

const char* MegaRequestPrivate::getPrivateKey() const
{
    return privateKey;
}

int MegaRequestPrivate::getAccess() const
{
    return access;
}

const char* MegaRequestPrivate::getFile() const
{
    return file;
}

int MegaRequestPrivate::getParamType() const
{
    return attrType;
}

const char *MegaRequestPrivate::getText() const
{
    return text;
}

long long MegaRequestPrivate::getNumber() const
{
    return number;
}

bool MegaRequestPrivate::getFlag() const
{
    return flag;
}

long long MegaRequestPrivate::getTransferredBytes() const
{
    return transferredBytes;
}

long long MegaRequestPrivate::getTotalBytes() const
{
    return totalBytes;
}

int MegaRequestPrivate::getNumRetry() const
{
    return numRetry;
}

int MegaRequestPrivate::getNumDetails() const
{
    return numDetails;
}

int MegaRequestPrivate::getTag() const
{
    return tag;
}

MegaPricing *MegaRequestPrivate::getPricing() const
{
    return megaPricing ? megaPricing->copy() : NULL;
}

void MegaRequestPrivate::setNumDetails(int numDetails)
{
    this->numDetails = numDetails;
}

MegaNode *MegaRequestPrivate::getPublicNode() const
{
    return publicNode;
}

MegaNode *MegaRequestPrivate::getPublicMegaNode() const
{
    if(publicNode)
    {
        return publicNode->copy();
    }

    return NULL;
}

void MegaRequestPrivate::setNodeHandle(uint64_t nodeHandle)
{
    this->nodeHandle = nodeHandle;
}

void MegaRequestPrivate::setParentHandle(uint64_t parentHandle)
{
    this->parentHandle = parentHandle;
}

void MegaRequestPrivate::setSessionKey(const char* sessionKey)
{
    if(this->sessionKey) delete [] this->sessionKey;
    this->sessionKey = MegaApi::strdup(sessionKey);
}

void MegaRequestPrivate::setNumRetry(int numRetry)
{
    this->numRetry = numRetry;
}

void MegaRequestPrivate::setLink(const char* link)
{
    if(this->link)
        delete [] this->link;

    this->link = MegaApi::strdup(link);
}
void MegaRequestPrivate::setName(const char* name)
{
    if(this->name)
        delete [] this->name;

    this->name = MegaApi::strdup(name);
}
void MegaRequestPrivate::setEmail(const char* email)
{
    if(this->email)
        delete [] this->email;

    this->email = MegaApi::strdup(email);
}
void MegaRequestPrivate::setPassword(const char* password)
{
    if(this->password)
        delete [] this->password;

    this->password = MegaApi::strdup(password);
}
void MegaRequestPrivate::setNewPassword(const char* newPassword)
{
    if(this->newPassword)
        delete [] this->newPassword;

    this->newPassword = MegaApi::strdup(newPassword);
}
void MegaRequestPrivate::setPrivateKey(const char* privateKey)
{
    if(this->privateKey)
        delete [] this->privateKey;

    this->privateKey = MegaApi::strdup(privateKey);
}
void MegaRequestPrivate::setAccess(int access)
{
    this->access = access;
}

void MegaRequestPrivate::setFile(const char* file)
{
    if(this->file)
        delete [] this->file;

    this->file = MegaApi::strdup(file);
}

void MegaRequestPrivate::setParamType(int type)
{
    this->attrType = type;
}

void MegaRequestPrivate::setText(const char *text)
{
    if(this->text) delete [] this->text;
    this->text = MegaApi::strdup(text);
}

void MegaRequestPrivate::setNumber(long long number)
{
    this->number = number;
}

void MegaRequestPrivate::setFlag(bool flag)
{
    this->flag = flag;
}

void MegaRequestPrivate::setTransferTag(int transfer)
{
    this->transfer = transfer;
}

void MegaRequestPrivate::setListener(MegaRequestListener *listener)
{
    this->listener = listener;
}

void MegaRequestPrivate::setTotalBytes(long long totalBytes)
{
    this->totalBytes = totalBytes;
}

void MegaRequestPrivate::setTransferredBytes(long long transferredBytes)
{
    this->transferredBytes = transferredBytes;
}

void MegaRequestPrivate::setTag(int tag)
{
    this->tag = tag;
}

void MegaRequestPrivate::addProduct(unsigned int type, handle product, int proLevel, int gbStorage, int gbTransfer, int months, int amount, int amountMonth, const char *currency, const char* description, const char* iosid, const char* androidid)
{
    if (megaPricing)
    {
        megaPricing->addProduct(type, product, proLevel, gbStorage, gbTransfer, months, amount, amountMonth, currency, description, iosid, androidid);
    }
}

void MegaRequestPrivate::setProxy(Proxy *proxy)
{
    this->proxy = proxy;
}

Proxy *MegaRequestPrivate::getProxy()
{
    return proxy;
}

void MegaRequestPrivate::setTimeZoneDetails(MegaTimeZoneDetails *timeZoneDetails)
{
    if (this->timeZoneDetails)
    {
        delete this->timeZoneDetails;
    }
    this->timeZoneDetails = timeZoneDetails ? timeZoneDetails->copy() : NULL;
}

void MegaRequestPrivate::setPublicNode(MegaNode *publicNode, bool copyChildren)
{
    if (this->publicNode)
    {
        delete this->publicNode;
    }

    if (!publicNode)
    {
        this->publicNode = NULL;
    }
    else
    {
        MegaNodePrivate *nodePrivate = new MegaNodePrivate(publicNode);
        MegaNodeListPrivate *children = dynamic_cast<MegaNodeListPrivate *>(publicNode->getChildren());
        if (children && copyChildren)
        {
            nodePrivate->setChildren(new MegaNodeListPrivate(children, true));
        }
        this->publicNode = nodePrivate;
    }
}

const char *MegaRequestPrivate::getRequestString() const
{
    switch(type)
    {
        case TYPE_LOGIN: return "LOGIN";
        case TYPE_CREATE_FOLDER: return "CREATE_FOLDER";
        case TYPE_MOVE: return "MOVE";
        case TYPE_COPY: return "COPY";
        case TYPE_RENAME: return "RENAME";
        case TYPE_REMOVE: return "REMOVE";
        case TYPE_SHARE: return "SHARE";
        case TYPE_IMPORT_LINK: return "IMPORT_LINK";
        case TYPE_EXPORT: return "EXPORT";
        case TYPE_FETCH_NODES: return "FETCH_NODES";
        case TYPE_ACCOUNT_DETAILS: return "ACCOUNT_DETAILS";
        case TYPE_CHANGE_PW: return "CHANGE_PW";
        case TYPE_UPLOAD: return "UPLOAD";
        case TYPE_LOGOUT: return "LOGOUT";
        case TYPE_GET_PUBLIC_NODE: return "GET_PUBLIC_NODE";
        case TYPE_GET_ATTR_FILE: return "GET_ATTR_FILE";
        case TYPE_SET_ATTR_FILE: return "SET_ATTR_FILE";
        case TYPE_GET_ATTR_USER: return "GET_ATTR_USER";
        case TYPE_SET_ATTR_USER: return "SET_ATTR_USER";
        case TYPE_RETRY_PENDING_CONNECTIONS: return "RETRY_PENDING_CONNECTIONS";
        case TYPE_REMOVE_CONTACT: return "REMOVE_CONTACT";
        case TYPE_CREATE_ACCOUNT: return "CREATE_ACCOUNT";
        case TYPE_CONFIRM_ACCOUNT: return "CONFIRM_ACCOUNT";
        case TYPE_QUERY_SIGNUP_LINK: return "QUERY_SIGNUP_LINK";
        case TYPE_ADD_SYNC: return "ADD_SYNC";
        case TYPE_REMOVE_SYNC: return "REMOVE_SYNC";
        case TYPE_REMOVE_SYNCS: return "REMOVE_SYNCS";
        case TYPE_PAUSE_TRANSFERS: return "PAUSE_TRANSFERS";
        case TYPE_CANCEL_TRANSFER: return "CANCEL_TRANSFER";
        case TYPE_CANCEL_TRANSFERS: return "CANCEL_TRANSFERS";
        case TYPE_DELETE: return "DELETE";
        case TYPE_REPORT_EVENT: return "REPORT_EVENT";
        case TYPE_CANCEL_ATTR_FILE: return "CANCEL_ATTR_FILE";
        case TYPE_GET_PRICING: return "GET_PRICING";
        case TYPE_GET_PAYMENT_ID: return "GET_PAYMENT_ID";
        case TYPE_UPGRADE_ACCOUNT: return "UPGRADE_ACCOUNT";
        case TYPE_GET_USER_DATA: return "GET_USER_DATA";
        case TYPE_LOAD_BALANCING: return "LOAD_BALANCING";
        case TYPE_KILL_SESSION: return "KILL_SESSION";
        case TYPE_SUBMIT_PURCHASE_RECEIPT: return "SUBMIT_PURCHASE_RECEIPT";
        case TYPE_CREDIT_CARD_STORE: return "CREDIT_CARD_STORE";
        case TYPE_CREDIT_CARD_QUERY_SUBSCRIPTIONS: return "CREDIT_CARD_QUERY_SUBSCRIPTIONS";
        case TYPE_CREDIT_CARD_CANCEL_SUBSCRIPTIONS: return "CREDIT_CARD_CANCEL_SUBSCRIPTIONS";
        case TYPE_GET_SESSION_TRANSFER_URL: return "GET_SESSION_TRANSFER_URL";
        case TYPE_GET_PAYMENT_METHODS: return "GET_PAYMENT_METHODS";
        case TYPE_INVITE_CONTACT: return "INVITE_CONTACT";
        case TYPE_REPLY_CONTACT_REQUEST: return "REPLY_CONTACT_REQUEST";
        case TYPE_SUBMIT_FEEDBACK: return "SUBMIT_FEEDBACK";
        case TYPE_SEND_EVENT: return "SEND_EVENT";
        case TYPE_CLEAN_RUBBISH_BIN: return "CLEAN_RUBBISH_BIN";
        case TYPE_SET_ATTR_NODE: return "SET_ATTR_NODE";
        case TYPE_CHAT_CREATE: return "CHAT_CREATE";
        case TYPE_CHAT_FETCH: return "CHAT_FETCH";
        case TYPE_CHAT_INVITE: return "CHAT_INVITE";
        case TYPE_CHAT_REMOVE: return "CHAT_REMOVE";
        case TYPE_CHAT_URL: return "CHAT_URL";
        case TYPE_CHAT_GRANT_ACCESS: return "CHAT_GRANT_ACCESS";
        case TYPE_CHAT_REMOVE_ACCESS: return "CHAT_REMOVE_ACCESS";
        case TYPE_USE_HTTPS_ONLY: return "USE_HTTPS_ONLY";
        case TYPE_SET_PROXY: return "SET_PROXY";
        case TYPE_GET_RECOVERY_LINK: return "GET_RECOVERY_LINK";
        case TYPE_QUERY_RECOVERY_LINK: return "QUERY_RECOVERY_LINK";
        case TYPE_CONFIRM_RECOVERY_LINK: return "CONFIRM_RECOVERY_LINK";
        case TYPE_GET_CANCEL_LINK: return "GET_CANCEL_LINK";
        case TYPE_CONFIRM_CANCEL_LINK: return "CONFIRM_CANCEL_LINK";
        case TYPE_GET_CHANGE_EMAIL_LINK: return "GET_CHANGE_EMAIL_LINK";
        case TYPE_CONFIRM_CHANGE_EMAIL_LINK: return "CONFIRM_CHANGE_EMAIL_LINK";
        case TYPE_PAUSE_TRANSFER: return "PAUSE_TRANSFER";
        case TYPE_MOVE_TRANSFER: return "MOVE_TRANSFER";
        case TYPE_CHAT_SET_TITLE: return "CHAT_SET_TITLE";
        case TYPE_CHAT_UPDATE_PERMISSIONS: return "CHAT_UPDATE_PERMISSIONS";
        case TYPE_CHAT_TRUNCATE: return "CHAT_TRUNCATE";
        case TYPE_SET_MAX_CONNECTIONS: return "SET_MAX_CONNECTIONS";
        case TYPE_CHAT_PRESENCE_URL: return "CHAT_PRESENCE_URL";
        case TYPE_REGISTER_PUSH_NOTIFICATION: return "REGISTER_PUSH_NOTIFICATION";
        case TYPE_GET_USER_EMAIL: return "GET_USER_EMAIL";
        case TYPE_APP_VERSION: return "APP_VERSION";
        case TYPE_GET_LOCAL_SSL_CERT: return "GET_LOCAL_SSL_CERT";
        case TYPE_SEND_SIGNUP_LINK: return "SEND_SIGNUP_LINK";
        case TYPE_QUERY_DNS: return "QUERY_DNS";
        case TYPE_QUERY_GELB: return "QUERY_GELB";
        case TYPE_CHAT_STATS: return "CHAT_STATS";
        case TYPE_DOWNLOAD_FILE: return "DOWNLOAD_FILE";
        case TYPE_QUERY_TRANSFER_QUOTA: return "QUERY_TRANSFER_QUOTA";
        case TYPE_PASSWORD_LINK: return "PASSWORD_LINK";
        case TYPE_RESTORE: return "RESTORE";
        case TYPE_GET_ACHIEVEMENTS: return "GET_ACHIEVEMENTS";
        case TYPE_REMOVE_VERSIONS: return "REMOVE_VERSIONS";
        case TYPE_CHAT_ARCHIVE: return "CHAT_ARCHIVE";
        case TYPE_WHY_AM_I_BLOCKED: return "WHY_AM_I_BLOCKED";
        case TYPE_CONTACT_LINK_CREATE: return "CONTACT_LINK_CREATE";
        case TYPE_CONTACT_LINK_QUERY: return "CONTACT_LINK_QUERY";
        case TYPE_CONTACT_LINK_DELETE: return "CONTACT_LINK_DELETE";
        case TYPE_FOLDER_INFO: return "FOLDER_INFO";
        case TYPE_RICH_LINK: return "RICH_LINK";
        case TYPE_CHAT_LINK_HANDLE: return "CHAT_LINK_HANDLE";
        case TYPE_CHAT_LINK_URL: return "CHAT_LINK_URL";
        case TYPE_SET_PRIVATE_MODE: return "SET_PRIVATE_MODE";
        case TYPE_AUTOJOIN_PUBLIC_CHAT: return "AUTOJOIN_PUBLIC_CHAT";
        case TYPE_KEEP_ME_ALIVE: return "KEEP_ME_ALIVE";
        case TYPE_MULTI_FACTOR_AUTH_CHECK: return "MULTI_FACTOR_AUTH_CHECK";
        case TYPE_MULTI_FACTOR_AUTH_GET: return "MULTI_FACTOR_AUTH_GET";
        case TYPE_MULTI_FACTOR_AUTH_SET: return "MULTI_FACTOR_AUTH_SET";
        case TYPE_ADD_BACKUP: return "ADD_BACKUP";
        case TYPE_REMOVE_BACKUP: return "REMOVE_BACKUP";
        case TYPE_TIMER: return "SET_TIMER";
        case TYPE_ABORT_CURRENT_BACKUP: return "ABORT_BACKUP";
        case TYPE_GET_PSA: return "GET_PSA";
        case TYPE_FETCH_TIMEZONE: return "FETCH_TIMEZONE";
        case TYPE_USERALERT_ACKNOWLEDGE: return "USERALERT_ACKNOWLEDGE";
        case TYPE_CATCHUP: return "CATCHUP";
        case TYPE_PUBLIC_LINK_INFORMATION: return "PUBLIC_LINK_INFORMATION";
        case TYPE_GET_BACKGROUND_UPLOAD_URL: return "GET_BACKGROUND_UPLOAD_URL";
        case TYPE_COMPLETE_BACKGROUND_UPLOAD: return "COMPLETE_BACKGROUND_UPLOAD";
        case TYPE_GET_CLOUD_STORAGE_USED: return "GET_CLOUD_STORAGE_USED";
        case TYPE_SEND_SMS_VERIFICATIONCODE: return "SEND_SMS_VERIFICATIONCODE";
        case TYPE_CHECK_SMS_VERIFICATIONCODE: return "CHECK_SMS_VERIFICATIONCODE";
        case TYPE_GET_REGISTERED_CONTACTS: return "GET_REGISTERED_CONTACTS";
        case TYPE_GET_COUNTRY_CALLING_CODES: return "GET_COUNTRY_CALLING_CODES";
        case TYPE_VERIFY_CREDENTIALS: return "VERIFY_CREDENTIALS";
        case TYPE_GET_MISC_FLAGS: return "GET_MISC_FLAGS";
        case TYPE_RESEND_VERIFICATION_EMAIL: return "RESEND_VERIFICATION_EMAIL";
        case TYPE_SUPPORT_TICKET: return "SUPPORT_TICKET";
        case TYPE_SET_RETENTION_TIME: return "SET_RETENTION_TIME";
        case TYPE_RESET_SMS_VERIFIED_NUMBER: return "RESET_SMS_VERIFIED_NUMBER";
        case TYPE_SEND_DEV_COMMAND: return "SEND_DEV_COMMAND";
    }
    return "UNKNOWN";
}

MegaRequestListener *MegaRequestPrivate::getListener() const
{
    return listener;
}

int MegaRequestPrivate::getTransferTag() const
{
    return transfer;
}

const char *MegaRequestPrivate::toString() const
{
    return getRequestString();
}

const char *MegaRequestPrivate::__str__() const
{
    return getRequestString();
}

const char *MegaRequestPrivate::__toString() const
{
    return getRequestString();
}

MegaStringMapPrivate::MegaStringMapPrivate()
{

}

MegaStringMapPrivate::MegaStringMapPrivate(const string_map *map, bool toBase64)
{
    strMap.insert(map->begin(),map->end());

    if (toBase64)
    {
        char* buf;
        string_map::iterator it;
        for (it = strMap.begin(); it != strMap.end(); it++)
        {
            buf = new char[it->second.length() * 4 / 3 + 4];
            Base64::btoa((const byte *) it->second.data(), int(it->second.length()), buf);

            it->second.assign(buf);

            delete [] buf;
        }
    }
}

MegaStringMapPrivate::~MegaStringMapPrivate()
{

}

MegaStringMap *MegaStringMapPrivate::copy() const
{
    return new MegaStringMapPrivate(this);
}

const char *MegaStringMapPrivate::get(const char *key) const
{
    string_map::const_iterator it = strMap.find(key);

    if (it == strMap.end())
    {
        return NULL;
    }

    return it->second.data();
}

MegaStringList *MegaStringMapPrivate::getKeys() const
{
    vector<char*> keys;
    char *buf;
    for (string_map::const_iterator it = strMap.begin(); it != strMap.end(); it++)
    {
        buf = new char[it->first.length()+1];
        memcpy(buf, it->first.data(), it->first.length());
        buf[it->first.length()] = 0;

        keys.push_back(buf);
    }

    return new MegaStringListPrivate(keys.data(), int(keys.size()));
}

void MegaStringMapPrivate::set(const char *key, const char *value)
{
    strMap[key] = value;
}

int MegaStringMapPrivate::size() const
{
    return int(strMap.size());
}

const string_map *MegaStringMapPrivate::getMap() const
{
    return &strMap;
}

MegaStringMapPrivate::MegaStringMapPrivate(const MegaStringMapPrivate *megaStringMap)
{
    MegaStringList *keys = megaStringMap->getKeys();
    const char *key = NULL;
    const char *value = NULL;
    for (int i=0; i < keys->size(); i++)
    {
        key = keys->get(i);
        value = megaStringMap->get(key);

        strMap[key] = value;
    }

    delete keys;
}

MegaStringListPrivate::MegaStringListPrivate()
{
    list = NULL;
    s = 0;
}

MegaStringListPrivate::MegaStringListPrivate(const MegaStringListPrivate *stringList)
{
    s = stringList->size();
    if (!s)
    {
        list = NULL;
        return;
    }

    list = new const char*[s];
    for (int i = 0; i < s; i++)
        list[i] = MegaApi::strdup(stringList->get(i));
}

MegaStringListPrivate::MegaStringListPrivate(char **newlist, int size)
{
    list = NULL;
    s = size;
    if (!size)
    {
        return;
    }

    list = new const char*[size];
    for (int i = 0; i < size; i++)
        list[i] = newlist[i];
}

MegaStringListPrivate::~MegaStringListPrivate()
{
    if(!list)
        return;

    for(int i=0; i<s; i++)
        delete [] list[i];
    delete [] list;
}

MegaStringList *MegaStringListPrivate::copy() const
{
    return new MegaStringListPrivate(this);
}

const char *MegaStringListPrivate::get(int i) const
{
    if(!list || (i < 0) || (i >= s))
        return NULL;

    return list[i];
}

int MegaStringListPrivate::size() const
{
    return s;
}

bool operator==(const MegaStringList& lhs, const MegaStringList& rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }
    for (int i = 0; i < lhs.size(); ++i)
    {
        if (strcmp(lhs.get(i), rhs.get(i)) != 0)
        {
            return false;
        }
    }
    return true;
}


MegaStringListMap* MegaStringListMapPrivate::copy() const
{
    auto map = new MegaStringListMapPrivate;
    for (const auto& pair : mMap)
    {
        map->set(pair.first.get(), pair.second->copy());
    }
    return map;
}

const MegaStringList* MegaStringListMapPrivate::get(const char* key) const
{
    auto key_ptr = std::unique_ptr<const char[]>{key};
    auto iter = mMap.find(key_ptr);
    key_ptr.release();
    if (iter != mMap.end())
    {
        return iter->second.get();
    }
    return nullptr;
}

MegaStringList *MegaStringListMapPrivate::getKeys() const
{
    vector<char*> list;
    for (const auto& pair : mMap)
    {
        list.emplace_back(MegaApi::strdup(pair.first.get()));
    }
    return new MegaStringListPrivate{list.data(), static_cast<int>(list.size())};
}

void MegaStringListMapPrivate::set(const char* key, const MegaStringList* value)
{
    std::unique_ptr<const char[]> key_ptr{MegaApi::strdup(key)};
    mMap[std::move(key_ptr)] = std::unique_ptr<const MegaStringList>{value};
}

int MegaStringListMapPrivate::size() const
{
    return static_cast<int>(mMap.size());
}

bool MegaStringListMapPrivate::Compare::operator()(const std::unique_ptr<const char[]>& rhs,
                                                   const std::unique_ptr<const char[]>& lhs) const
{
    return strcmp(rhs.get(), lhs.get()) < 0;
}


MegaStringTable* MegaStringTablePrivate::copy() const
{
    auto table = new MegaStringTablePrivate;
    for (const auto& value : mTable)
    {
        table->append(value->copy());
    }
    return table;
}

void MegaStringTablePrivate::append(const MegaStringList* value)
{
    mTable.emplace_back(value);
}

const MegaStringList* MegaStringTablePrivate::get(int i) const
{
    if (i >= 0 && i < size())
    {
        return mTable[i].get();
    }
    return nullptr;
}

int MegaStringTablePrivate::size() const
{
    return static_cast<int>(mTable.size());
}


MegaNodeListPrivate::MegaNodeListPrivate()
{
    list = NULL;
    s = 0;
}

MegaNodeListPrivate::MegaNodeListPrivate(node_vector& v)
{
    list = NULL; s = static_cast<int>(v.size());
    if (!s) return;

    list = new MegaNode*[s];
    for (int i = 0; i < s; i++)
        list[i] = MegaNodePrivate::fromNode(v[i]);
}

MegaNodeListPrivate::MegaNodeListPrivate(Node** newlist, int size)
{
    list = NULL; s = size;
    if(!size) return;

    list = new MegaNode*[size];
    for(int i=0; i<size; i++)
        list[i] = MegaNodePrivate::fromNode(newlist[i]);
}

MegaNodeListPrivate::MegaNodeListPrivate(const MegaNodeListPrivate *nodeList, bool copyChildren)
{
    s = nodeList->size();
    if (!s)
    {
        list = NULL;
        return;
    }

    list = new MegaNode*[s];
    for (int i = 0; i<s; i++)
    {
        MegaNode *node = nodeList->get(i);
        MegaNodePrivate *nodePrivate = new MegaNodePrivate(node);
        MegaNodeListPrivate *children = dynamic_cast<MegaNodeListPrivate *>(node->getChildren());
        if (children && copyChildren)
        {
            nodePrivate->setChildren(new MegaNodeListPrivate(children, true));
        }
        list[i] = nodePrivate;
    }
}

MegaNodeListPrivate::~MegaNodeListPrivate()
{
    if(!list)
        return;

    for(int i=0; i<s; i++)
        delete list[i];
    delete [] list;
}

MegaNodeList *MegaNodeListPrivate::copy() const
{
    return new MegaNodeListPrivate(this);
}

MegaNode *MegaNodeListPrivate::get(int i) const
{
    if(!list || (i < 0) || (i >= s))
        return NULL;

    return list[i];
}

int MegaNodeListPrivate::size() const
{
    return s;
}

void MegaNodeListPrivate::addNode(MegaNode *node)
{
    MegaNode** copyList = list;
    s = s + 1;
    list = new MegaNode*[s];
    for (int i = 0; i < s - 1; ++i)
    {
        list[i] = copyList[i];
    }

    list[s - 1] = node->copy();

    if (copyList != NULL)
    {
        delete [] copyList;
    }
}

MegaUserListPrivate::MegaUserListPrivate()
{
    list = NULL;
    s = 0;
}

MegaUserListPrivate::MegaUserListPrivate(User** newlist, int size)
{
    list = NULL;
    s = size;

    if(!size)
        return;

    list = new MegaUser*[size];
    for(int i=0; i<size; i++)
        list[i] = MegaUserPrivate::fromUser(newlist[i]);
}

MegaUserListPrivate::MegaUserListPrivate(MegaUserListPrivate *userList)
{
    s = userList->size();
    if (!s)
    {
        list = NULL;
        return;
    }
    list = new MegaUser*[s];
    for (int i = 0; i<s; i++)
        list[i] = new MegaUserPrivate(userList->get(i));
}

MegaUserListPrivate::~MegaUserListPrivate()
{
    if(!list)
        return;

    for(int i=0; i<s; i++)
        delete list[i];

    delete [] list;
}

MegaUserList *MegaUserListPrivate::copy()
{
    return new MegaUserListPrivate(this);
}

MegaUser *MegaUserListPrivate::get(int i)
{
    if(!list || (i < 0) || (i >= s))
        return NULL;

    return list[i];
}

int MegaUserListPrivate::size()
{
    return s;
}

MegaUserAlertListPrivate::MegaUserAlertListPrivate()
{
    list = NULL;
    s = 0;
}

MegaUserAlertListPrivate::MegaUserAlertListPrivate(UserAlert::Base** newlist, int size, MegaClient* mc)
{
    list = NULL;
    s = size;

    if (!size)
        return;

    list = new MegaUserAlert*[size];
    for (int i = 0; i < size; i++)
    {
        list[i] = new MegaUserAlertPrivate(newlist[i], mc);
    }
}

MegaUserAlertListPrivate::MegaUserAlertListPrivate(const MegaUserAlertListPrivate &userList)
{
    s = userList.size();
    list = s ? new MegaUserAlert*[s] : NULL;
    for (int i = 0; i < s; ++i)
    {
        list[i] = userList.get(i)->copy();
    }
}

MegaUserAlertListPrivate::~MegaUserAlertListPrivate()
{
    for (int i = 0; i < s; i++)
    {
        delete list[i];
    }
    delete[] list;
}

MegaUserAlertList *MegaUserAlertListPrivate::copy() const
{
    return new MegaUserAlertListPrivate(*this);
}

MegaUserAlert *MegaUserAlertListPrivate::get(int i) const
{
    if (!list || (i < 0) || (i >= s))
        return NULL;

    return list[i];
}

int MegaUserAlertListPrivate::size() const
{
    return s;
}

void MegaUserAlertListPrivate::clear()
{
    delete[] list;
    s = 0;
    list = nullptr;
}

MegaRecentActionBucketPrivate::MegaRecentActionBucketPrivate(recentaction& ra, MegaClient* mc)
{
    User* u = mc->finduser(ra.user);

    timestamp = ra.time;
    user = u ? u->email : "";
    parent = ra.parent;
    update = ra.updated;
    media = ra.media;
    nodes = new MegaNodeListPrivate(ra.nodes);
}

MegaRecentActionBucketPrivate::MegaRecentActionBucketPrivate(int64_t ts, const string& u, handle p, bool up, bool m, MegaNodeList* l)
{
    timestamp = ts;
    user = u;
    parent = p;
    update = up;
    media = m;
    nodes = l;
}

MegaRecentActionBucketPrivate::~MegaRecentActionBucketPrivate()
{
    delete nodes;
}

MegaRecentActionBucket *MegaRecentActionBucketPrivate::copy() const
{
    return new MegaRecentActionBucketPrivate(timestamp, user, parent, update, media, nodes->copy());
}

int64_t MegaRecentActionBucketPrivate::getTimestamp() const
{
    return timestamp;
}

const char* MegaRecentActionBucketPrivate::getUserEmail() const
{
    return user.c_str();
}

MegaHandle MegaRecentActionBucketPrivate::getParentHandle() const
{
    return parent;
}

bool MegaRecentActionBucketPrivate::isUpdate() const
{
    return update;
}

bool MegaRecentActionBucketPrivate::isMedia() const
{
    return media;
}

const MegaNodeList* MegaRecentActionBucketPrivate::getNodes() const
{
    return nodes;
}

MegaRecentActionBucketListPrivate::MegaRecentActionBucketListPrivate()
{
    list = NULL;
    s = 0;
}

MegaRecentActionBucketListPrivate::MegaRecentActionBucketListPrivate(recentactions_vector& v, MegaClient* mc)
{
    list = NULL;
    s = static_cast<int>(v.size());

    if (!s)
        return;

    list = new MegaRecentActionBucketPrivate*[s];
    for (int i = 0; i < s; i++)
    {
        list[i] = new MegaRecentActionBucketPrivate(v[i], mc);
    }
}

MegaRecentActionBucketListPrivate::MegaRecentActionBucketListPrivate(const MegaRecentActionBucketListPrivate &o)
{
    s = o.size();
    list = s ? new MegaRecentActionBucketPrivate*[s] : NULL;
    for (int i = 0; i < s; ++i)
    {
        list[i] = (MegaRecentActionBucketPrivate*)o.get(i)->copy();
    }
}

MegaRecentActionBucketListPrivate::~MegaRecentActionBucketListPrivate()
{
    for (int i = 0; i < s; i++)
    {
        delete list[i];
    }
    delete[] list;
}

MegaRecentActionBucketList *MegaRecentActionBucketListPrivate::copy() const
{
    return new MegaRecentActionBucketListPrivate(*this);
}

MegaRecentActionBucket *MegaRecentActionBucketListPrivate::get(int i) const
{
    if (!list || (i < 0) || (i >= s))
    {
        return NULL;
    }
    return list[i];
}

int MegaRecentActionBucketListPrivate::size() const
{
    return s;
}

MegaShareListPrivate::MegaShareListPrivate()
{
    list = NULL;
    s = 0;
}

MegaShareListPrivate::MegaShareListPrivate(Share** newlist, uint64_t *uint64_tlist, int size)
{
    list = NULL; s = size;
    if(!size) return;

    list = new MegaShare*[size];
    for(int i=0; i<size; i++)
    {
        list[i] = MegaSharePrivate::fromShare(uint64_tlist[i], newlist[i]);
    }
}

MegaShareListPrivate::~MegaShareListPrivate()
{
    if(!list)
        return;

    for(int i=0; i<s; i++)
        delete list[i];

    delete [] list;
}

MegaShare *MegaShareListPrivate::get(int i)
{
    if(!list || (i < 0) || (i >= s))
        return NULL;

    return list[i];
}

int MegaShareListPrivate::size()
{
    return s;
}

MegaTransferListPrivate::MegaTransferListPrivate()
{
    list = NULL;
    s = 0;
}

MegaTransferListPrivate::MegaTransferListPrivate(MegaTransfer** newlist, int size)
{
    list = NULL;
    s = size;

    if(!size)
        return;

    list = new MegaTransfer*[size];
    for(int i=0; i<size; i++)
        list[i] = newlist[i]->copy();
}

MegaTransferListPrivate::~MegaTransferListPrivate()
{
    if(!list)
        return;

    for(int i=0; i < s; i++)
        delete list[i];

    delete [] list;
}

MegaTransfer *MegaTransferListPrivate::get(int i)
{
    if(!list || (i < 0) || (i >= s))
        return NULL;

    return list[i];
}

int MegaTransferListPrivate::size()
{
    return s;
}

MegaContactRequestListPrivate::MegaContactRequestListPrivate()
{
    list = NULL;
    s = 0;
}

MegaContactRequestListPrivate::MegaContactRequestListPrivate(PendingContactRequest **newlist, int size)
{
    list = NULL;
    s = size;

    if(!size)
        return;

    list = new MegaContactRequest*[size];
    for(int i=0; i<size; i++)
        list[i] = new MegaContactRequestPrivate(newlist[i]);
}

MegaContactRequestListPrivate::~MegaContactRequestListPrivate()
{
    if(!list)
        return;

    for(int i=0; i < s; i++)
        delete list[i];

    delete [] list;
}

MegaContactRequestList *MegaContactRequestListPrivate::copy()
{
    return new MegaContactRequestListPrivate(this);
}

MegaContactRequest *MegaContactRequestListPrivate::get(int i)
{
    if(!list || (i < 0) || (i >= s))
        return NULL;

    return list[i];
}

int MegaContactRequestListPrivate::size()
{
    return s;
}

MegaContactRequestListPrivate::MegaContactRequestListPrivate(MegaContactRequestListPrivate *requestList)
{
    s = requestList->size();
    if (!s)
    {
        list = NULL;
        return;
    }
    list = new MegaContactRequest*[s];
    for (int i = 0; i < s; i++)
        list[i] = new MegaContactRequestPrivate(requestList->get(i));
}

MegaFile::MegaFile() : File()
{
    megaTransfer = NULL;
}

void MegaFile::setTransfer(MegaTransferPrivate *transfer)
{
    this->megaTransfer = transfer;
}

MegaTransferPrivate *MegaFile::getTransfer()
{
    return megaTransfer;
}

bool MegaFile::serialize(string *d)
{
    if (!megaTransfer)
    {
        return false;
    }

    if (!File::serialize(d))
    {
        return false;
    }

    if (!megaTransfer->serialize(d))
    {
        return false;
    }

    d->append("\0\0\0\0\0\0\0", 8);

    return true;
}

MegaFile *MegaFile::unserialize(string *d)
{
    File *file = File::unserialize(d);
    if (!file)
    {
        LOG_err << "Error unserializing MegaFile: Unable to unserialize File";
        return NULL;
    }

    MegaFile *megaFile = new MegaFile();
    *(File *)megaFile = *(File *)file;
    file->chatauth = NULL;
    delete file;

    MegaTransferPrivate *transfer = MegaTransferPrivate::unserialize(d);
    if (!transfer)
    {
        delete megaFile;
        return NULL;
    }

    const char* ptr = d->data();
    const char* end = ptr + d->size();
    if (ptr + 8 > end)
    {
        LOG_err << "MegaFile unserialization failed - data too short";
        delete megaFile;
        delete transfer;
        return NULL;
    }

    if (memcmp(ptr, "\0\0\0\0\0\0\0", 8))
    {
        LOG_err << "MegaFile unserialization failed - invalid version";
        delete megaFile;
        delete transfer;
        return NULL;
    }
    ptr += 8;

    d->erase(0, ptr - d->data());

    transfer->setSourceFileTemporary(megaFile->temporaryfile);

    megaFile->setTransfer(transfer);
    return megaFile;
}

MegaFileGet::MegaFileGet(MegaClient *client, Node *n, const LocalPath& dstPath, FileSystemType fsType) : MegaFile()
{
    h = n->nodehandle;
    *(FileFingerprint*)this = *n;

    LocalPath lpName = LocalPath::fromName(n->displayname(), *client->fsaccess, fsType);
    name = lpName.toPath(*client->fsaccess);

    LocalPath finalPath;
    if(!dstPath.empty())
    {
        if (dstPath.endsInSeparator(*client->fsaccess))
        {
            finalPath = dstPath;
            finalPath.appendWithSeparator(lpName, true, client->fsaccess->localseparator);
        }
        else finalPath = dstPath;
    }
    else
        finalPath = lpName;

    size = n->size;
    mtime = n->mtime;

    if(n->nodekey().size()>=sizeof(filekey))
        memcpy(filekey,n->nodekey().data(),sizeof filekey);

    localname = finalPath;
    hprivate = true;
    hforeign = false;
}

MegaFileGet::MegaFileGet(MegaClient *client, MegaNode *n, const LocalPath& dstPath) : MegaFile()
{
    h = n->getHandle();

    FileSystemType fsType = client->fsaccess->getFilesystemType(dstPath);

    LocalPath lpName = LocalPath::fromName(n->getName(), *client->fsaccess, fsType);
    name = lpName.toPath(*client->fsaccess);

    LocalPath finalPath;
    if(!dstPath.empty())
    {
        if (dstPath.endsInSeparator(*client->fsaccess))
        {
            finalPath = dstPath;
            finalPath.appendWithSeparator(lpName, true, client->fsaccess->localseparator);
        }
        else finalPath = dstPath;
    }
    else finalPath = lpName;

    const char *fingerprint = n->getFingerprint();
    if (fingerprint)
    {
        FileFingerprint *fp = MegaApiImpl::getFileFingerprintInternal(fingerprint);
        if (fp)
        {
            *(FileFingerprint *)this = *(FileFingerprint *)fp;
            delete fp;
        }
    }

    size = n->getSize();
    mtime = n->getModificationTime();

    if(n->getNodeKey()->size()>=sizeof(filekey))
        memcpy(filekey,n->getNodeKey()->data(),sizeof filekey);

    localname = finalPath;
    hprivate = !n->isPublic();
    hforeign = n->isForeign();

    if(n->getPrivateAuth()->size())
    {
        privauth = *n->getPrivateAuth();
    }

    if(n->getPublicAuth()->size())
    {
        pubauth = *n->getPublicAuth();
    }

    chatauth = n->getChatAuth() ? MegaApi::strdup(n->getChatAuth()) : NULL;
}

bool MegaFileGet::serialize(string *d)
{
    if (!MegaFile::serialize(d))
    {
        return false;
    }

    d->append("\0\0\0\0\0\0\0", 8);

    return true;
}

MegaFileGet *MegaFileGet::unserialize(string *d)
{
    MegaFile *file = MegaFile::unserialize(d);
    if (!file)
    {
        LOG_err << "Error unserializing MegaFileGet: Unable to unserialize MegaFile";
        return NULL;
    }

    const char* ptr = d->data();
    const char* end = ptr + d->size();
    if (ptr + 8 > end)
    {
        LOG_err << "MegaFileGet unserialization failed - data too short";
        delete file;
        return NULL;
    }

    if (memcmp(ptr, "\0\0\0\0\0\0\0", 8))
    {
        LOG_err << "MegaFileGet unserialization failed - invalid version";
        delete file;
        return NULL;
    }

    ptr += 8;
    if (ptr != end)
    {
        LOG_err << "MegaFileGet unserialization failed - wrong size";
        delete file;
        return NULL;
    }

    MegaFileGet *megaFile = new MegaFileGet();
    *(MegaFile *)megaFile = *(MegaFile *)file;
    file->chatauth = NULL;
    delete file;

    return megaFile;
}

void MegaFileGet::prepare()
{
    if (transfer->localfilename.empty())
    {
        transfer->localfilename = localname;

        size_t leafIndex = transfer->localfilename.getLeafnameByteIndex(*transfer->client->fsaccess);
        transfer->localfilename.truncate(leafIndex);

        LocalPath suffix;
        transfer->client->fsaccess->tmpnamelocal(suffix);
        transfer->localfilename.append(suffix);
    }
}

void MegaFileGet::updatelocalname()
{
#ifdef _WIN32
    transfer->localfilename.editStringDirect()->append("", 1);
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW((LPCWSTR)transfer->localfilename.editStringDirect()->data(), GetFileExInfoStandard, &fad))
        SetFileAttributesW((LPCWSTR)transfer->localfilename.editStringDirect()->data(), fad.dwFileAttributes & ~FILE_ATTRIBUTE_HIDDEN);
    transfer->localfilename.editStringDirect()->resize(transfer->localfilename.editStringDirect()->size()-1);
#endif
}

void MegaFileGet::progress()
{
#ifdef _WIN32
    if(transfer->slot && !transfer->slot->progressreported)
    {
        transfer->localfilename.editStringDirect()->append("", 1);
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW((LPCWSTR)transfer->localfilename.editStringDirect()->data(), GetFileExInfoStandard, &fad))
            SetFileAttributesW((LPCWSTR)transfer->localfilename.editStringDirect()->data(), fad.dwFileAttributes | FILE_ATTRIBUTE_HIDDEN);
        transfer->localfilename.editStringDirect()->resize(transfer->localfilename.editStringDirect()->size()-1);
    }
#endif
}

void MegaFileGet::completed(Transfer*, LocalNode*)
{
    delete this;
}

void MegaFileGet::terminated()
{
    delete this;
}

MegaFilePut::MegaFilePut(MegaClient *, LocalPath clocalname, string *filename, handle ch, const char* ctargetuser, int64_t mtime, bool isSourceTemporary) 
    : MegaFile()
{
    // full local path
    localname = std::move(clocalname);

    // target parent node
    h = ch;

    // target user
    targetuser = ctargetuser;

    // new node name
    name = *filename;

    customMtime = mtime;

    temporaryfile = isSourceTemporary;
}

bool MegaFilePut::serialize(string *d)
{
    if (!MegaFile::serialize(d))
    {
        return false;
    }

    d->append((char*)&customMtime, sizeof(customMtime));
    d->append("\0\0\0\0\0\0\0", 8);

    return true;
}

MegaFilePut *MegaFilePut::unserialize(string *d)
{
    MegaFile *file = MegaFile::unserialize(d);
    if (!file)
    {
        LOG_err << "Error unserializing MegaFilePut: Unable to unserialize MegaFile";
        return NULL;
    }

    const char* ptr = d->data();
    const char* end = ptr + d->size();
    if (ptr + sizeof(int64_t) + 8 > end)
    {
        LOG_err << "MegaFilePut unserialization failed - data too short";
        delete file;
        return NULL;
    }

    int64_t customMtime = MemAccess::get<int64_t>(ptr);
    ptr += sizeof(customMtime);

    if (memcmp(ptr, "\0\0\0\0\0\0\0", 8))
    {
        LOG_err << "MegaFilePut unserialization failed - invalid version";
        delete file;
        return NULL;
    }

    ptr += 8;
    if (ptr != end)
    {
        LOG_err << "MegaFilePut unserialization failed - wrong size";
        delete file;
        return NULL;
    }

    MegaFilePut *megaFile = new MegaFilePut();
    *(MegaFile *)megaFile = *(MegaFile *)file;
    file->chatauth = NULL;
    delete file;

    megaFile->customMtime = customMtime;
    return megaFile;
}

void MegaFilePut::completed(Transfer* t, LocalNode*)
{
    if(customMtime >= 0)
        t->mtime = customMtime;

    File::completed(t,NULL);
    delete this;
}

void MegaFilePut::terminated()
{
    delete this;
}

bool TreeProcessor::processNode(Node*)
{
    return false; /* Stops the processing */
}

TreeProcessor::~TreeProcessor()
{ }


//Entry point for the blocking thread
void *MegaApiImpl::threadEntryPoint(void *param)
{
#ifndef _WIN32
    struct sigaction noaction;
    memset(&noaction, 0, sizeof(noaction));
    noaction.sa_handler = SIG_IGN;
    ::sigaction(SIGPIPE, &noaction, 0);
#endif

    MegaApiImpl *megaApiImpl = (MegaApiImpl *)param;
    megaApiImpl->loop();
    return 0;
}

MegaTransferPrivate *MegaApiImpl::getMegaTransferPrivate(int tag)
{
    map<int, MegaTransferPrivate *>::iterator it = transferMap.find(tag);
    if (it == transferMap.end())
    {
        return NULL;
    }
    return it->second;
}

ExternalLogger MegaApiImpl::externalLogger;

MegaApiImpl::MegaApiImpl(MegaApi *api, const char *appKey, MegaGfxProcessor* processor, const char *basePath, const char *userAgent, unsigned workerThreadCount)
{
    init(api, appKey, processor, basePath, userAgent, -1, workerThreadCount);
}

MegaApiImpl::MegaApiImpl(MegaApi *api, const char *appKey, const char *basePath, const char *userAgent, unsigned workerThreadCount)
{
    init(api, appKey, NULL, basePath, userAgent, -1, workerThreadCount);
}

MegaApiImpl::MegaApiImpl(MegaApi *api, const char *appKey, const char *basePath, const char *userAgent, int fseventsfd, unsigned workerThreadCount)
{
    init(api, appKey, NULL, basePath, userAgent, fseventsfd, workerThreadCount);
}

void MegaApiImpl::init(MegaApi *api, const char *appKey, MegaGfxProcessor* processor, const char *basePath, const char *userAgent, int fseventsfd, unsigned clientWorkerThreadCount)
{
    this->api = api;

    maxRetries = 7;
    currentTransfer = NULL;
    pendingUploads = 0;
    pendingDownloads = 0;
    totalUploads = 0;
    totalDownloads = 0;
    client = NULL;
    waitingRequest = RETRY_NONE;
    totalDownloadedBytes = 0;
    totalUploadedBytes = 0;
    totalDownloadBytes = 0;
    totalUploadBytes = 0;
    notificationNumber = 0;
    activeRequest = NULL;
    activeTransfer = NULL;
    activeError = NULL;
    activeNodes = NULL;
    activeUsers = NULL;
    syncLowerSizeLimit = 0;
    syncUpperSizeLimit = 0;

#ifdef HAVE_LIBUV
    httpServer = NULL;
    httpServerMaxBufferSize = 0;
    httpServerMaxOutputSize = 0;
    httpServerEnableFiles = true;
    httpServerEnableFolders = false;
    httpServerOfflineAttributeEnabled = false;
    httpServerRestrictedMode = MegaApi::TCP_SERVER_ALLOW_CREATED_LOCAL_LINKS;
    httpServerSubtitlesSupportEnabled = false;

    ftpServer = NULL;
    ftpServerMaxBufferSize = 0;
    ftpServerMaxOutputSize = 0;
    ftpServerRestrictedMode = MegaApi::TCP_SERVER_ALLOW_CREATED_LOCAL_LINKS;
#endif

    mPushSettings = NULL;
    mTimezones = NULL;

    httpio = new MegaHttpIO();
    waiter = new MegaWaiter();

#ifndef __APPLE__
    (void)fseventsfd;
    fsAccess = new MegaFileSystemAccess();
#else
    fsAccess = new MegaFileSystemAccess(fseventsfd);
#endif

    if (basePath)
    {
        string sBasePath = basePath;
        int lastIndex = int(sBasePath.size() - 1);
        if (sBasePath[lastIndex] != '/' && sBasePath[lastIndex] != '\\')
        {
            string utf8Separator;
            fsAccess->local2path(&fsAccess->localseparator, &utf8Separator);
            sBasePath.append(utf8Separator);
        }
        dbAccess = new MegaDbAccess(&sBasePath);

        this->basePath = basePath;
    }
    else dbAccess = NULL;

    gfxAccess = NULL;
    if(processor)
    {
        GfxProcExternal *externalGfx = new GfxProcExternal();
        externalGfx->startProcessingThread();
        externalGfx->setProcessor(processor);
        gfxAccess = externalGfx;
    }
    else
    {
        gfxAccess = new MegaGfxProc();
        gfxAccess->startProcessingThread();
    }

    if(!userAgent)
    {
        userAgent = "";
    }

    nocache = false;
    if (appKey)
    {
        this->appKey = appKey;
    }
    client = new MegaClient(this, waiter, httpio, fsAccess, dbAccess, gfxAccess, appKey, userAgent, clientWorkerThreadCount);

#if defined(_WIN32) && !defined(WINDOWS_PHONE)
    httpio->unlock();
#endif

    //Start blocking thread
    threadExit = 0;
    thread.start(threadEntryPoint, this);
}

MegaApiImpl::~MegaApiImpl()
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_DELETE);
    requestQueue.push(request);
    waiter->notify();
    thread.join();

    delete mPushSettings;
    delete mTimezones;    

    assert(requestMap.empty());
    assert(backupsMap.empty());
    assert(transferMap.empty());

    delete gfxAccess;
    delete fsAccess;
    delete waiter;

#ifndef DONT_RELEASE_HTTPIO
    delete httpio;
#endif

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
}

MegaApiImpl* MegaApiImpl::ImplOf(MegaApi* api)
{
    // Sometimes we need to be able to reference the MegaApiImpl from objects other than MegaApi (without giving clients access to the pImpl pointer)
    return api->pImpl;
}

int MegaApiImpl::isLoggedIn()
{
    sdkMutex.lock();
    int result = client->loggedin();
    sdkMutex.unlock();
    return result;
}

void MegaApiImpl::whyAmIBlocked(bool logout, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_WHY_AM_I_BLOCKED, listener);
    request->setFlag(logout);
    requestQueue.push(request);
    waiter->notify();
}

char* MegaApiImpl::getMyEmail()
{
    User* u;
    sdkMutex.lock();
    if (!client->loggedin() || !(u = client->finduser(client->me)))
    {
        sdkMutex.unlock();
        return NULL;
    }

    char *result = MegaApi::strdup(u->email.c_str());
    sdkMutex.unlock();
    return result;
}

char *MegaApiImpl::getMyUserHandle()
{
    sdkMutex.lock();
    if (ISUNDEF(client->me))
    {
        sdkMutex.unlock();
        return NULL;
    }

    char buf[12];
    Base64::btoa((const byte*)&client->me, MegaClient::USERHANDLE, buf);
    char *result = MegaApi::strdup(buf);
    sdkMutex.unlock();
    return result;
}

MegaHandle MegaApiImpl::getMyUserHandleBinary()
{
    MegaHandle me;
    sdkMutex.lock();
    me = client->me;
    sdkMutex.unlock();
    return me;
}

MegaUser *MegaApiImpl::getMyUser()
{
    sdkMutex.lock();
    MegaUser *user = MegaUserPrivate::fromUser(client->finduser(client->me));
    sdkMutex.unlock();
    return user;
}

bool MegaApiImpl::isAchievementsEnabled()
{
    assert(!isBusinessAccount() || !client->achievements_enabled);
    return client->achievements_enabled;
}

bool MegaApiImpl::isBusinessAccount()
{
    return client->mBizStatus != BIZ_STATUS_INACTIVE
             && client->mBizStatus != BIZ_STATUS_UNKNOWN;
}

bool MegaApiImpl::isMasterBusinessAccount()
{
    return client->mBizMode == BIZ_MODE_MASTER;
}

bool MegaApiImpl::isBusinessAccountActive()
{
    return getBusinessStatus() >= BIZ_STATUS_ACTIVE;
}

int MegaApiImpl::getBusinessStatus()
{
    m_time_t now = m_time(nullptr);

    // Check if current status has expired (based on ts of transition) and update status
    BizStatus oldStatus = client->mBizStatus;
    if (client->mBizExpirationTs && client->mBizExpirationTs < now)
    {
        client->mBizStatus = BIZ_STATUS_EXPIRED;

    }
    else if (client->mBizGracePeriodTs && client->mBizGracePeriodTs < now)
    {
        client->mBizStatus = BIZ_STATUS_GRACE_PERIOD;
    }

    if (client->mBizStatus != oldStatus)
    {
        client->app->notify_business_status(client->mBizStatus);
    }

    // Prevent return apps unknown status
    return (client->mBizStatus == BIZ_STATUS_UNKNOWN)
            ? BIZ_STATUS_INACTIVE
            : client->mBizStatus;
}

int64_t MegaApiImpl::getOverquotaDeadlineTs()
{
    return client->mOverquotaDeadlineTs;
}

MegaIntegerList *MegaApiImpl::getOverquotaWarningsTs()
{
    return new MegaIntegerListPrivate(client->mOverquotaWarningTs);
}

bool MegaApiImpl::checkPassword(const char *password)
{
    sdkMutex.lock();
    if (!password || !password[0] || client->k.size() != SymmCipher::KEYLENGTH)
    {
        sdkMutex.unlock();
        return false;
    }

    string k = client->k;
    if (client->accountversion == 1)
    {
        byte pwkey[SymmCipher::KEYLENGTH];
        if (client->pw_key(password, pwkey))
        {
            sdkMutex.unlock();
            return false;
        }

        SymmCipher cipher(pwkey);
        cipher.ecb_decrypt((byte *)k.data());
    }
    else if (client->accountversion == 2)
    {
        if (client->accountsalt.size() != 32) // SHA256
        {
            sdkMutex.unlock();
            return false;
        }

        byte derivedKey[2 * SymmCipher::KEYLENGTH];
        CryptoPP::PKCS5_PBKDF2_HMAC<CryptoPP::SHA512> pbkdf2;
        pbkdf2.DeriveKey(derivedKey, sizeof(derivedKey), 0, (byte *)password, strlen(password),
                         (const byte *)client->accountsalt.data(), client->accountsalt.size(), 100000);

        SymmCipher cipher(derivedKey);
        cipher.ecb_decrypt((byte *)k.data());
    }
    else
    {
        LOG_warn << "Version of account not supported";
        sdkMutex.unlock();
        return false;
    }

    bool result = !memcmp(k.data(), client->key.key, SymmCipher::KEYLENGTH);
    sdkMutex.unlock();
    return result;
}

char *MegaApiImpl::getMyCredentials()
{
    SdkMutexGuard g(sdkMutex);
    if (ISUNDEF(client->me))
    {
        return NULL;
    }

    string result;
    if (client->signkey)
    {
        result = AuthRing::fingerprint(string((const char*)client->signkey->pubKey, EdDSA::PUBLIC_KEY_LENGTH), true);
    }

    return result.size() ? MegaApi::strdup(result.c_str()) : nullptr;
}

void MegaApiImpl::getUserCredentials(MegaUser *user, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_ATTR_USER, listener);

    request->setParamType(ATTR_ED25519_PUBK);
    request->setFlag(true);
    if(user)
    {
        request->setEmail(user->getEmail());
    }

    requestQueue.push(request);
    waiter->notify();
}

bool MegaApiImpl::areCredentialsVerified(MegaUser *user)
{
    SdkMutexGuard g(sdkMutex);
    return user ? client->areCredentialsVerified(user->getHandle()) : false;
}

void MegaApiImpl::verifyCredentials(MegaUser *user, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_VERIFY_CREDENTIALS, listener);

    if(user)
    {
        request->setNodeHandle(user->getHandle());
    }

    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::resetCredentials(MegaUser *user, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_VERIFY_CREDENTIALS, listener);

    if(user)
    {
        request->setNodeHandle(user->getHandle());
    }
    request->setFlag(true);

    requestQueue.push(request);
    waiter->notify();
}

char *MegaApiImpl::getMyRSAPrivateKey()
{
    SdkMutexGuard g(sdkMutex);
    if (ISUNDEF(client->me) || client->mPrivKey.empty())
    {
        return nullptr;
    }

    return MegaApi::strdup(client->mPrivKey.c_str());
}

void MegaApiImpl::setLogLevel(int logLevel)
{
    externalLogger.setLogLevel(logLevel);
}

void MegaApiImpl::setMaxPayloadLogSize(long long maxSize)
{
    SimpleLogger::setMaxPayloadLogSize(maxSize);
}

void MegaApiImpl::addLoggerClass(MegaLogger *megaLogger)
{
    externalLogger.addMegaLogger(megaLogger);
}

void MegaApiImpl::removeLoggerClass(MegaLogger *megaLogger)
{
    externalLogger.removeMegaLogger(megaLogger);
}

void MegaApiImpl::setLogToConsole(bool enable)
{
    externalLogger.setLogToConsole(enable);
}

void MegaApiImpl::log(int logLevel, const char *message, const char *filename, int line)
{
    externalLogger.postLog(logLevel, message, filename, line);
}

void MegaApiImpl::setLoggingName(const char* loggingName)
{
    sdkMutex.lock();
    if (loggingName)
    {
        client->clientname = string(loggingName) + " ";
    }
    else
    {
        client->clientname.clear();
    }
    sdkMutex.unlock();
}

long long MegaApiImpl::getSDKtime()
{
    return Waiter::ds;
}

void MegaApiImpl::getSessionTransferURL(const char *path, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_SESSION_TRANSFER_URL);
    request->setText(path);
    request->setListener(listener);
    requestQueue.push(request);
    waiter->notify();
}

char* MegaApiImpl::getStringHash(const char* base64pwkey, const char* inBuf)
{
    if (!base64pwkey || !inBuf)
    {
        return NULL;
    }

    char pwkey[2 * SymmCipher::KEYLENGTH];
    if (Base64::atob(base64pwkey, (byte *)pwkey, sizeof pwkey) != SymmCipher::KEYLENGTH)
    {
        return MegaApi::strdup("");
    }

    SymmCipher key;
    key.setkey((byte*)pwkey);

    uint64_t strhash;
    string neBuf = inBuf;

    strhash = client->stringhash64(&neBuf, &key);

    char* buf = new char[8*4/3+4];
    Base64::btoa((byte*)&strhash, 8, buf);
    return buf;
}

MegaHandle MegaApiImpl::base32ToHandle(const char *base32Handle)
{
    if(!base32Handle) return INVALID_HANDLE;

    handle h = 0;
    Base32::atob(base32Handle,(byte*)&h, MegaClient::USERHANDLE);
    return h;
}

const char* MegaApiImpl::ebcEncryptKey(const char* encryptionKey, const char* plainKey)
{
    if(!encryptionKey || !plainKey) return NULL;

    char pwkey[SymmCipher::KEYLENGTH];
    Base64::atob(encryptionKey, (byte *)pwkey, sizeof pwkey);

    SymmCipher key;
    key.setkey((byte*)pwkey);

    char plkey[SymmCipher::KEYLENGTH];
    Base64::atob(plainKey, (byte*)plkey, sizeof plkey);
    key.ecb_encrypt((byte*)plkey);

    char* buf = new char[SymmCipher::KEYLENGTH*4/3+4];
    Base64::btoa((byte*)plkey, SymmCipher::KEYLENGTH, buf);
    return buf;
}

handle MegaApiImpl::base64ToHandle(const char* base64Handle)
{
    if(!base64Handle) return UNDEF;

    handle h = 0;
    Base64::atob(base64Handle,(byte*)&h,MegaClient::NODEHANDLE);
    return h;
}

handle MegaApiImpl::base64ToUserHandle(const char *base64Handle)
{
    if(!base64Handle) return UNDEF;

    handle h = 0;
    Base64::atob(base64Handle,(byte*)&h,MegaClient::USERHANDLE);
    return h;
}

char *MegaApiImpl::handleToBase64(MegaHandle handle)
{
    char *base64Handle = new char[12];
    Base64::btoa((byte*)&(handle),MegaClient::NODEHANDLE,base64Handle);
    return base64Handle;
}

char *MegaApiImpl::userHandleToBase64(MegaHandle handle)
{
    char *base64Handle = new char[14];
    Base64::btoa((byte*)&(handle),MegaClient::USERHANDLE,base64Handle);
    return base64Handle;
}

char *MegaApiImpl::binaryToBase64(const char *binaryData, size_t length)
{
    char *ret = new char[length * 4 / 3 + 3];
    Base64::btoa((byte*)binaryData, int(length), ret);
    return ret;
}

void MegaApiImpl::base64ToBinary(const char *base64string, unsigned char **binary, size_t* binarysize)
{
    string data;
    data.resize(strlen(base64string) * 3 / 4 + 3);
    data.resize(Base64::atob(base64string, (byte*)data.data(), int(data.size())));
    *binarysize = data.size();
    *binary = new unsigned char[*binarysize];
    memcpy(*binary, data.data(), *binarysize);
}

void MegaApiImpl::retryPendingConnections(bool disconnect, bool includexfers, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_RETRY_PENDING_CONNECTIONS);
    request->setFlag(disconnect);
    request->setNumber(includexfers);
    request->setListener(listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setDnsServers(const char *dnsServers, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_RETRY_PENDING_CONNECTIONS);
    request->setFlag(true);
    request->setNumber(true);
    request->setText(dnsServers);
    request->setListener(listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::addEntropy(char *data, unsigned int size)
{
    if(client && client->rng.CanIncorporateEntropy())
    {
        client->rng.IncorporateEntropy((const byte*)data, size);
    }

#ifdef USE_OPENSSL
    RAND_seed(data, size);
#endif
}

string MegaApiImpl::userAttributeToString(int type)
{
    return User::attr2string((::mega::attr_t) type);
}

string MegaApiImpl::userAttributeToLongName(int type)
{
    return User::attr2longname((::mega::attr_t) type);
}

int MegaApiImpl::userAttributeFromString(const char *name)
{
    if (!name)
    {
        return MegaApi::USER_ATTR_UNKNOWN;
    }
    return User::string2attr(name);
}

char MegaApiImpl::userAttributeToScope(int type)
{
    char scope;

    switch(type)
    {
        case MegaApi::USER_ATTR_AVATAR:
        case MegaApi::USER_ATTR_ED25519_PUBLIC_KEY:
        case MegaApi::USER_ATTR_CU25519_PUBLIC_KEY:
        case MegaApi::USER_ATTR_SIG_RSA_PUBLIC_KEY:
        case MegaApi::USER_ATTR_SIG_CU255_PUBLIC_KEY:
            scope = '+';
            break;

        case MegaApi::USER_ATTR_FIRSTNAME:
        case MegaApi::USER_ATTR_LASTNAME:
            scope = '0';
            break;

        case MegaApi::USER_ATTR_AUTHRING:
        case MegaApi::USER_ATTR_LAST_INTERACTION:
        case MegaApi::USER_ATTR_KEYRING:
        case MegaApi::USER_ATTR_RICH_PREVIEWS:
        case MegaApi::USER_ATTR_GEOLOCATION:
        case MegaApi::USER_ATTR_CAMERA_UPLOADS_FOLDER:
        case MegaApi::USER_ATTR_MY_CHAT_FILES_FOLDER:
        case MegaApi::USER_ATTR_ALIAS:
        case MegaApi::USER_ATTR_DEVICE_NAMES:
            scope = '*';
            break;

        case MegaApi::USER_ATTR_LANGUAGE:
        case MegaApi::USER_ATTR_PWD_REMINDER:
        case MegaApi::USER_ATTR_DISABLE_VERSIONS:
        case MegaApi::USER_ATTR_CONTACT_LINK_VERIFICATION:
        case MegaApi::USER_ATTR_LAST_PSA:
        case MegaApi::USER_ATTR_RUBBISH_TIME:
        case MegaApi::USER_ATTR_STORAGE_STATE:
        case MegaApi::USER_ATTR_PUSH_SETTINGS:
            scope = '^';
            break;

        default:
            LOG_err << "Getting invalid scope";
            scope = 0;
            break;
    }

    return scope;
}

void MegaApiImpl::setStatsID(const char *id)
{
    if (!id || !*id || MegaClient::statsid.size())
    {
        return;
    }

    MegaClient::statsid = id;
}

bool MegaApiImpl::serverSideRubbishBinAutopurgeEnabled()
{
    return client->ssrs_enabled;
}

bool MegaApiImpl::appleVoipPushEnabled()
{
    return client->aplvp_enabled;
}

bool MegaApiImpl::newLinkFormatEnabled()
{
    return client->mNewLinkFormat;
}

int MegaApiImpl::smsAllowedState()
{
    return (client->mSmsVerificationState != SMS_STATE_UNKNOWN) ? client->mSmsVerificationState : 0;
}

char* MegaApiImpl::smsVerifiedPhoneNumber()
{
    SdkMutexGuard g(sdkMutex);
    return client->mSmsVerifiedPhone.empty() ? NULL : MegaApi::strdup(client->mSmsVerifiedPhone.c_str());
}

void MegaApiImpl::resetSmsVerifiedPhoneNumber(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_RESET_SMS_VERIFIED_NUMBER, listener);
    request->setListener(listener);
    requestQueue.push(request);
    waiter->notify();
}

bool MegaApiImpl::multiFactorAuthAvailable()
{
    return client->gmfa_enabled;
}

void MegaApiImpl::multiFactorAuthCheck(const char *email, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_MULTI_FACTOR_AUTH_CHECK, listener);
    request->setEmail(email);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::multiFactorAuthGetCode(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_MULTI_FACTOR_AUTH_GET, listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::multiFactorAuthEnable(const char *pin, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_MULTI_FACTOR_AUTH_SET, listener);
    request->setFlag(true);
    request->setPassword(pin);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::multiFactorAuthDisable(const char *pin, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_MULTI_FACTOR_AUTH_SET, listener);
    request->setFlag(false);
    request->setPassword(pin);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::multiFactorAuthLogin(const char *email, const char *password, const char *pin, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_LOGIN, listener);
    request->setEmail(email);
    request->setPassword(password);
    request->setText(pin);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::multiFactorAuthChangePassword(const char *oldPassword, const char *newPassword, const char *pin, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHANGE_PW, listener);
    request->setPassword(oldPassword);
    request->setNewPassword(newPassword);
    request->setText(pin);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::multiFactorAuthChangeEmail(const char *email, const char *pin, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_CHANGE_EMAIL_LINK, listener);
    request->setEmail(email);
    request->setText(pin);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::multiFactorAuthCancelAccount(const char *pin, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_CANCEL_LINK, listener);
    request->setText(pin);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::fetchTimeZone(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_FETCH_TIMEZONE, listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::fastLogin(const char* email, const char *stringHash, const char *base64pwkey, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_LOGIN, listener);
    request->setEmail(email);
    request->setPassword(stringHash);
    request->setPrivateKey(base64pwkey);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::fastLogin(const char *session, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_LOGIN, listener);
    request->setSessionKey(session);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::killSession(MegaHandle sessionHandle, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_KILL_SESSION, listener);
    request->setNodeHandle(sessionHandle);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getUserData(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_USER_DATA, listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getUserData(MegaUser *user, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_USER_DATA, listener);
    request->setFlag(true);
    if(user)
    {
        request->setEmail(user->getEmail());
    }

    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getUserData(const char *user, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_USER_DATA, listener);
    request->setFlag(true);
    request->setEmail(user);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getMiscFlags(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_MISC_FLAGS, listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::sendDevCommand(const char *command, const char *email, long long quota, int businessStatus, int userStatus, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SEND_DEV_COMMAND, listener);
    request->setName(command);
    request->setEmail(email);
    request->setTotalBytes(quota);
    request->setAccess(businessStatus);
    request->setNumDetails(userStatus);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::login(const char *login, const char *password, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_LOGIN, listener);
    request->setEmail(login);
    request->setPassword(password);
    requestQueue.push(request);
    waiter->notify();
}

char *MegaApiImpl::dumpSession()
{
    sdkMutex.lock();
    byte session[MAX_SESSION_LENGTH];
    char* buf = NULL;
    int size;
    size = client->dumpsession(session, sizeof session);
    if (size > 0)
    {
        buf = new char[sizeof(session) * 4 / 3 + 4];
        Base64::btoa(session, size, buf);
    }

    sdkMutex.unlock();
    return buf;
}

char *MegaApiImpl::getSequenceNumber()
{
    sdkMutex.lock();

    char *scsn = MegaApi::strdup(client->scsn.text());

    sdkMutex.unlock();

    return scsn;
}

char *MegaApiImpl::getAccountAuth()
{
    sdkMutex.lock();
    char* buf = NULL;

    if (client->loggedin())
    {
        buf = new char[MAX_SESSION_LENGTH * 4 / 3 + 4];
        Base64::btoa((const byte *)client->sid.data(), int(client->sid.size()), buf);
    }

    sdkMutex.unlock();
    return buf;
}

void MegaApiImpl::setAccountAuth(const char *auth)
{
    sdkMutex.lock();
    if (!auth)
    {
        client->accountauth.clear();
    }
    else
    {
        client->accountauth = auth;
    }

    handle h = client->getrootpublicfolder();
    if (h != UNDEF)
    {
        client->setrootnode(h);
    }
    sdkMutex.unlock();
}

void MegaApiImpl::createAccount(const char* email, const char* password, const char* firstname, const char* lastname, MegaHandle lastPublicHandle, int lastPublicHandleType, int64_t lastAccessTimestamp, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CREATE_ACCOUNT, listener);
    request->setEmail(email);
    request->setPassword(password);
    request->setName(firstname);
    request->setText(lastname);
    request->setNodeHandle(lastPublicHandle);
    request->setAccess(lastPublicHandleType);
    request->setTransferredBytes(lastAccessTimestamp);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::resumeCreateAccount(const char *sid, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CREATE_ACCOUNT, listener);
    request->setSessionKey(sid);
    request->setParamType(1);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::cancelCreateAccount(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CREATE_ACCOUNT, listener);
    request->setParamType(2);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::sendSignupLink(const char *email, const char *name, const char *password, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SEND_SIGNUP_LINK, listener);
    request->setEmail(email);
    request->setPassword(password);
    request->setName(name);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::fastSendSignupLink(const char *email, const char *base64pwkey, const char *name, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SEND_SIGNUP_LINK, listener);
    request->setEmail(email);
    request->setPrivateKey(base64pwkey);
    request->setName(name);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::querySignupLink(const char* link, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_QUERY_SIGNUP_LINK, listener);
    request->setLink(link);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::confirmAccount(const char* link, const char *password, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CONFIRM_ACCOUNT, listener);
    request->setLink(link);
    request->setPassword(password);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::fastConfirmAccount(const char* link, const char *base64pwkey, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CONFIRM_ACCOUNT, listener);
    request->setLink(link);
    request->setPrivateKey(base64pwkey);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::resetPassword(const char *email, bool hasMasterKey, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_RECOVERY_LINK, listener);
    request->setEmail(email);
    request->setFlag(hasMasterKey);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::queryRecoveryLink(const char *link, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_QUERY_RECOVERY_LINK, listener);
    request->setLink(link);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::confirmResetPasswordLink(const char *link, const char *newPwd, const char *masterKey, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CONFIRM_RECOVERY_LINK, listener);
    request->setLink(link);
    request->setPassword(newPwd);
    request->setPrivateKey(masterKey);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::cancelAccount(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_CANCEL_LINK, listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::confirmCancelAccount(const char *link, const char *pwd, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CONFIRM_CANCEL_LINK, listener);
    request->setLink(link);
    request->setPassword(pwd);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::resendVerificationEmail(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_RESEND_VERIFICATION_EMAIL);
    request->setListener(listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::changeEmail(const char *email, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_CHANGE_EMAIL_LINK, listener);
    request->setEmail(email);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::confirmChangeEmail(const char *link, const char *pwd, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CONFIRM_CHANGE_EMAIL_LINK, listener);
    request->setLink(link);
    request->setPassword(pwd);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setProxySettings(MegaProxy *proxySettings, MegaRequestListener *listener)
{
    Proxy *localProxySettings = new Proxy();
    localProxySettings->setProxyType(proxySettings->getProxyType());

    string url;
    if(proxySettings->getProxyURL())
        url = proxySettings->getProxyURL();

    string localurl;

#if defined(WINDOWS_PHONE) || (defined(_WIN32) && defined(USE_CURL))
    localurl = url;
#else
    fsAccess->path2local(&url, &localurl);
#endif

    localProxySettings->setProxyURL(&localurl);

    if(proxySettings->credentialsNeeded())
    {
        string username;
        if(proxySettings->getUsername())
            username = proxySettings->getUsername();

        string localusername;

#if defined(WINDOWS_PHONE) || (defined(_WIN32) && defined(USE_CURL))
        localusername = username;
#else
        fsAccess->path2local(&username, &localusername);
#endif

        string password;
        if(proxySettings->getPassword())
            password = proxySettings->getPassword();

        string localpassword;

#if defined(WINDOWS_PHONE) || (defined(_WIN32) && defined(USE_CURL))
        localpassword = password;
#else
        fsAccess->path2local(&password, &localpassword);
#endif

        localProxySettings->setCredentials(&localusername, &localpassword);
    }

    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_PROXY, listener);
    request->setProxy(localProxySettings);
    requestQueue.push(request);
    waiter->notify();
}

MegaProxy *MegaApiImpl::getAutoProxySettings()
{
    MegaProxy *proxySettings = new MegaProxy;
    sdkMutex.lock();
    Proxy *localProxySettings = httpio->getautoproxy();
    sdkMutex.unlock();
    proxySettings->setProxyType(localProxySettings->getProxyType());
    if(localProxySettings->getProxyType() == Proxy::CUSTOM)
    {
        string localProxyURL = localProxySettings->getProxyURL();
        string proxyURL;
        fsAccess->local2path(&localProxyURL, &proxyURL);
        LOG_debug << "Autodetected proxy: " << proxyURL;
        proxySettings->setProxyURL(proxyURL.c_str());
    }

    delete localProxySettings;
    return proxySettings;
}

void MegaApiImpl::loop()
{
#if defined(WINDOWS_PHONE) || TARGET_OS_IPHONE
    // Workaround to get the IP of valid DNS servers on Windows Phone/iOS
    string servers;

    while (true)
    {
    #ifdef WINDOWS_PHONE
        client->httpio->getMEGADNSservers(&servers, false);
    #else
        __res_state res;
        bool valid;
        if (res_ninit(&res) == 0)
        {
            union res_sockaddr_union u[MAXNS];
            int nscount = res_getservers(&res, u, MAXNS);

            for(int i = 0; i < nscount; i++)
            {
                char straddr[INET6_ADDRSTRLEN];
                straddr[0] = 0;
                valid = false;

                if (u[i].sin.sin_family == PF_INET)
                {
                    valid = mega_inet_ntop(PF_INET, &u[i].sin.sin_addr, straddr, sizeof(straddr)) == straddr;
                }

                if (u[i].sin6.sin6_family == PF_INET6)
                {
                    valid = mega_inet_ntop(PF_INET6, &u[i].sin6.sin6_addr, straddr, sizeof(straddr)) == straddr;
                }

                if (valid && straddr[0])
                {
                    if (servers.size())
                    {
                        servers.append(",");
                    }
                    servers.append(straddr);
                }
            }

            res_ndestroy(&res);
        }
    #endif

        if (servers.size())
            break;

    #ifdef WINDOWS_PHONE
        std::this_thread::sleep_for(std::chrono::seconds(1));
    #else
        sleep(1);
    #endif
    }

    LOG_debug << "Using DNS servers " << servers;
    sdkMutex.lock();
    httpio->setdnsservers(servers.c_str());
    sdkMutex.unlock();
#elif _WIN32
    httpio->lock();
#endif

    while(true)
    {
        sdkMutex.lock();
        int r = client->preparewait();
        sdkMutex.unlock();
        if (!r)
        {
            r = client->dowait();
            sdkMutex.lock();
            r |= client->checkevents();
            sdkMutex.unlock();
        }

        if (r & Waiter::NEEDEXEC)
        {
            WAIT_CLASS::bumpds();
            updateBackups();
            if (sendPendingTransfers())
            {
                yield();
            }
            sendPendingRequests();
            sendPendingScRequest();
            if (threadExit)
            {
                break;
            }

            sdkMutex.lock();
            client->exec();
            sdkMutex.unlock();
        }
    }

    sdkMutex.lock();
    delete client;
    sdkMutex.unlock();
}


void MegaApiImpl::createFolder(const char *name, MegaNode *parent, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CREATE_FOLDER, listener);
    if(parent) request->setParentHandle(parent->getHandle());
    request->setName(name);
    requestQueue.push(request);
    waiter->notify();
}

bool MegaApiImpl::createLocalFolder(const char *path)
{
    if (!path)
    {
        return false;
    }

    string sPath(path);
    
    auto localpath = LocalPath::fromPath(sPath, *client->fsaccess);

    localpath.ensureWinExtendedPathLenPrefix();

    sdkMutex.lock();
    bool success = client->fsaccess->mkdirlocal(localpath);
    sdkMutex.unlock();

    return success;
}

void MegaApiImpl::moveNode(MegaNode *node, MegaNode *newParent, const char *newName, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_MOVE, listener);
    if(node) request->setNodeHandle(node->getHandle());
    if(newParent) request->setParentHandle(newParent->getHandle());
    request->setName(newName);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::moveNode(MegaNode *node, MegaNode *newParent, MegaRequestListener *listener)
{
    moveNode(node, newParent, nullptr, listener);
}

void MegaApiImpl::copyNode(MegaNode *node, MegaNode* target, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_COPY, listener);
    if (node)
    {
        request->setPublicNode(node, true);
        request->setNodeHandle(node->getHandle());
    }
    if(target) request->setParentHandle(target->getHandle());
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::copyNode(MegaNode *node, MegaNode *target, const char *newName, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_COPY, listener);
    if (node)
    {
        request->setPublicNode(node, true);
        request->setNodeHandle(node->getHandle());
    }
    if(target) request->setParentHandle(target->getHandle());
    request->setName(newName);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::renameNode(MegaNode *node, const char *newName, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_RENAME, listener);
    if(node) request->setNodeHandle(node->getHandle());
    request->setName(newName);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::remove(MegaNode *node, bool keepversions, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_REMOVE, listener);
    if(node) request->setNodeHandle(node->getHandle());
    request->setFlag(keepversions);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::removeVersions(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_REMOVE_VERSIONS, listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::restoreVersion(MegaNode *version, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_RESTORE, listener);
    if (version)
    {
        request->setNodeHandle(version->getHandle());
    }
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::cleanRubbishBin(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CLEAN_RUBBISH_BIN, listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::sendFileToUser(MegaNode *node, MegaUser *user, MegaRequestListener *listener)
{
    return sendFileToUser(node, user ? user->getEmail() : NULL, listener);
}

void MegaApiImpl::sendFileToUser(MegaNode *node, const char* email, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_COPY, listener);
    if (node)
    {
        request->setPublicNode(node, true);
        request->setNodeHandle(node->getHandle());
    }
    request->setEmail(email);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::share(MegaNode* node, MegaUser *user, int access, MegaRequestListener *listener)
{
    return share(node, user ? user->getEmail() : NULL, access, listener);
}

void MegaApiImpl::share(MegaNode *node, const char* email, int access, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SHARE, listener);
    if(node) request->setNodeHandle(node->getHandle());
    request->setEmail(email);
    request->setAccess(access);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::loginToFolder(const char* megaFolderLink, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_LOGIN, listener);
    request->setLink(megaFolderLink);
    request->setEmail("FOLDER");
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::importFileLink(const char* megaFileLink, MegaNode *parent, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_IMPORT_LINK, listener);
    if(parent) request->setParentHandle(parent->getHandle());
    request->setLink(megaFileLink);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::decryptPasswordProtectedLink(const char *link, const char *password, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_PASSWORD_LINK, listener);
    request->setLink(link);
    request->setPassword(password);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::encryptLinkWithPassword(const char *link, const char *password, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_PASSWORD_LINK, listener);
    request->setLink(link);
    request->setPassword(password);
    request->setFlag(true); // encrypt
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getPublicNode(const char* megaFileLink, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_PUBLIC_NODE, listener);
    request->setLink(megaFileLink);
    requestQueue.push(request);
    waiter->notify();
}

const char *MegaApiImpl::buildPublicLink(const char *publicHandle, const char *key, bool isFolder)
{
    handle ph = MegaApi::base64ToHandle(publicHandle);
    string link = client->getPublicLink(client->mNewLinkFormat, isFolder ? FOLDERNODE : FILENODE, ph, key);
    return MegaApi::strdup(link.c_str());
}

void MegaApiImpl::getThumbnail(MegaNode* node, const char *dstFilePath, MegaRequestListener *listener)
{
    getNodeAttribute(node, GfxProc::THUMBNAIL, dstFilePath, listener);
}

void MegaApiImpl::cancelGetThumbnail(MegaNode* node, MegaRequestListener *listener)
{
    cancelGetNodeAttribute(node, GfxProc::THUMBNAIL, listener);
}

void MegaApiImpl::setThumbnail(MegaNode* node, const char *srcFilePath, MegaRequestListener *listener)
{
    setNodeAttribute(node, GfxProc::THUMBNAIL, srcFilePath, INVALID_HANDLE, listener);
}

void MegaApiImpl::putThumbnail(MegaBackgroundMediaUpload* bu, const char *srcFilePath, MegaRequestListener *listener)
{
    putNodeAttribute(bu, GfxProc::THUMBNAIL, srcFilePath, listener);
}

void MegaApiImpl::setThumbnailByHandle(MegaNode* node, MegaHandle attributehandle, MegaRequestListener *listener)
{
    setNodeAttribute(node, GfxProc::THUMBNAIL, nullptr, attributehandle, listener);
}

void MegaApiImpl::getPreview(MegaNode* node, const char *dstFilePath, MegaRequestListener *listener)
{
    getNodeAttribute(node, GfxProc::PREVIEW, dstFilePath, listener);
}

void MegaApiImpl::cancelGetPreview(MegaNode* node, MegaRequestListener *listener)
{
    cancelGetNodeAttribute(node, GfxProc::PREVIEW, listener);
}

void MegaApiImpl::setPreview(MegaNode* node, const char *srcFilePath, MegaRequestListener *listener)
{
    setNodeAttribute(node, GfxProc::PREVIEW, srcFilePath, INVALID_HANDLE, listener);
}

void MegaApiImpl::putPreview(MegaBackgroundMediaUpload* bu, const char *srcFilePath, MegaRequestListener *listener)
{
    putNodeAttribute(bu, GfxProc::PREVIEW, srcFilePath, listener);
}

void MegaApiImpl::setPreviewByHandle(MegaNode* node, MegaHandle attributehandle, MegaRequestListener *listener)
{
    setNodeAttribute(node, GfxProc::PREVIEW, nullptr, attributehandle, listener);
}

void MegaApiImpl::getUserAvatar(MegaUser* user, const char *dstFilePath, MegaRequestListener *listener)
{
    const char *email = NULL;
    if (user)
    {
        email = user->getEmail();
    }
    getUserAttr(email, MegaApi::USER_ATTR_AVATAR, dstFilePath, 0, listener);
}

void MegaApiImpl::getUserAvatar(const char* email_or_handle, const char *dstFilePath, MegaRequestListener *listener)
{
    getUserAttr(email_or_handle, MegaApi::USER_ATTR_AVATAR, dstFilePath, 0, listener);
}

char *MegaApiImpl::getUserAvatarColor(MegaUser *user)
{
    return user ? MegaApiImpl::getAvatarColor((handle) user->getHandle()) : NULL;
}

char *MegaApiImpl::getUserAvatarColor(const char *userhandle)
{
    return userhandle ? MegaApiImpl::getAvatarColor(MegaApiImpl::base64ToUserHandle(userhandle)) : NULL;
}

char *MegaApiImpl::getUserAvatarSecondaryColor(MegaUser *user)
{
    return user ? MegaApiImpl::getAvatarSecondaryColor((handle) user->getHandle()) : NULL;
}

char *MegaApiImpl::getUserAvatarSecondaryColor(const char *userhandle)
{
    return userhandle ? MegaApiImpl::getAvatarSecondaryColor(MegaApiImpl::base64ToUserHandle(userhandle)) : NULL;
}

void MegaApiImpl::setAvatar(const char *dstFilePath, MegaRequestListener *listener)
{
    setUserAttr(MegaApi::USER_ATTR_AVATAR, dstFilePath, listener);
}

void MegaApiImpl::getUserAttribute(MegaUser* user, int type, MegaRequestListener *listener)
{
    const char *email = NULL;
    if (user)
    {
        email = user->getEmail();
    }
    getUserAttr(email, type ? type : -1, NULL, 0, listener);
}

bool MegaApiImpl::testAllocation(unsigned allocCount, size_t allocSize)
{
    bool success = true;
    std::vector<char*> v;
    try
    {
        for (unsigned i = allocCount; i--; )
        {
            v.push_back(new char[allocSize]);
        }
    }
    catch (std::bad_alloc&)
    {
        LOG_warn << "MegaApi::testAllocation detected low memory: " << allocCount << " " << allocSize;
        success = false;
    }
    for (auto it : v)
    {
        delete[] it;
    }
    return success;
}

void MegaApiImpl::getUserAttribute(const char* email_or_handle, int type, MegaRequestListener *listener)
{
    getUserAttr(email_or_handle, type ? type : -1, NULL, 0, listener);
}

void MegaApiImpl::getChatUserAttribute(const char *email_or_handle, int type, const char *ph, MegaRequestListener *listener)
{
    getChatUserAttr(email_or_handle, type ? type : -1, NULL, ph, 0, listener);
}

void MegaApiImpl::setUserAttribute(int type, const char *value, MegaRequestListener *listener)
{
    setUserAttr(type ? type : -1, value, listener);
}

void MegaApiImpl::setUserAttribute(int type, const MegaStringMap *value, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_ATTR_USER, listener);

    request->setMegaStringMap(value);
    request->setParamType(type);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getRubbishBinAutopurgePeriod(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_ATTR_USER, listener);
    request->setParamType(MegaApi::USER_ATTR_RUBBISH_TIME);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setRubbishBinAutopurgePeriod(int days, MegaRequestListener *listener)
{
    ostringstream oss;
    oss << days;
    string value = oss.str();
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_ATTR_USER, listener);
    request->setText(value.data());
    request->setParamType(MegaApi::USER_ATTR_RUBBISH_TIME);
    request->setNumber(days);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getDeviceName(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_ATTR_USER, listener);
    request->setParamType(MegaApi::USER_ATTR_DEVICE_NAMES);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setDeviceName(const char *deviceName, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_ATTR_USER, listener);
    MegaStringMapPrivate stringMap;
    string buf = deviceName ? deviceName : "";
    stringMap.set(client->getDeviceid().c_str(), Base64::btoa(buf).c_str());
    request->setMegaStringMap(&stringMap);
    request->setName(deviceName);
    request->setParamType(MegaApi::USER_ATTR_DEVICE_NAMES);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getUserEmail(MegaHandle handle, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_USER_EMAIL, listener);
    request->setNodeHandle(handle);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setCustomNodeAttribute(MegaNode *node, const char *attrName, const char *value, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_ATTR_NODE, listener);
    if(node) request->setNodeHandle(node->getHandle());
    request->setName(attrName);
    request->setText(value);
    request->setFlag(false);     // is official attribute?
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setNodeDuration(MegaNode *node, int secs, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_ATTR_NODE, listener);
    if(node) request->setNodeHandle(node->getHandle());
    request->setParamType(MegaApi::NODE_ATTR_DURATION);
    request->setNumber(secs);
    request->setFlag(true);     // is official attribute?
    requestQueue.push(request);
    waiter->notify();
}

static void encodeCoordinates(double latitude, double longitude, int& lat, int& lon)
{
    lat = int(latitude);
    if (latitude != MegaNode::INVALID_COORDINATE)
    {
        lat = int(((latitude + 90) / 180) * 0xFFFFFF);
    }

    lon = int(longitude);
    if (longitude != MegaNode::INVALID_COORDINATE)
    {
        lon = int((longitude == 180) ? 0 : ((longitude + 180) / 360) * 0x01000000);
    }
}

void MegaApiImpl::setNodeCoordinates(MegaNode *node, bool unshareable, double latitude, double longitude, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_ATTR_NODE, listener);

    if(node)
    {
        request->setNodeHandle(node->getHandle());
    }

    int lat, lon;
    encodeCoordinates(latitude, longitude, lat, lon);

    request->setParamType(MegaApi::NODE_ATTR_COORDINATES);
    request->setTransferTag(lat);
    request->setNumDetails(lon);
    request->setAccess(unshareable);
    request->setFlag(true);     // official attribute (otherwise it would go in the custom section)
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::exportNode(MegaNode *node, int64_t expireTime, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_EXPORT, listener);
    if(node) request->setNodeHandle(node->getHandle());
    request->setNumber(expireTime);
    request->setAccess(1);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::disableExport(MegaNode *node, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_EXPORT, listener);
    if(node) request->setNodeHandle(node->getHandle());
    request->setAccess(0);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::fetchNodes(bool resumeSyncs, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_FETCH_NODES, listener);
    request->setNumber(resumeSyncs);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getPricing(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_PRICING, listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getPaymentId(handle productHandle, handle lastPublicHandle, int lastPublicHandleType, int64_t lastAccessTimestamp, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_PAYMENT_ID, listener);
    request->setNodeHandle(productHandle);
    request->setParentHandle(lastPublicHandle);
    request->setParamType(lastPublicHandleType);
    request->setTransferredBytes(lastAccessTimestamp);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::upgradeAccount(MegaHandle productHandle, int paymentMethod, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_UPGRADE_ACCOUNT, listener);
    request->setNodeHandle(productHandle);
    request->setNumber(paymentMethod);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::submitPurchaseReceipt(int gateway, const char *receipt, MegaHandle lastPublicHandle, int lastPublicHandleType, int64_t lastAccessTimestamp, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SUBMIT_PURCHASE_RECEIPT, listener);
    request->setNumber(gateway);
    request->setText(receipt);
    request->setNodeHandle(lastPublicHandle);
    request->setParamType(lastPublicHandleType);
    request->setTransferredBytes(lastAccessTimestamp);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::creditCardStore(const char* address1, const char* address2, const char* city,
                                  const char* province, const char* country, const char *postalcode,
                                  const char* firstname, const char* lastname, const char* creditcard,
                                  const char* expire_month, const char* expire_year, const char* cv2,
                                  MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CREDIT_CARD_STORE, listener);
    string email;

    sdkMutex.lock();
    User *u = client->finduser(client->me);
    if (u)
    {
        email = u->email;
    }
    sdkMutex.unlock();

    if (email.size())
    {
        string saddress1, saddress2, scity, sprovince, scountry, spostalcode;
        string sfirstname, slastname, screditcard, sexpire_month, sexpire_year, scv2;

        if (address1)
        {
           saddress1 = address1;
        }

        if (address2)
        {
            saddress2 = address2;
        }

        if (city)
        {
            scity = city;
        }

        if (province)
        {
            sprovince = province;
        }

        if (country)
        {
            scountry = country;
        }

        if (postalcode)
        {
            spostalcode = postalcode;
        }

        if (firstname)
        {
            sfirstname = firstname;
        }

        if (lastname)
        {
            slastname = lastname;
        }

        if (creditcard)
        {
            screditcard = creditcard;
            screditcard.erase(std::remove_if(screditcard.begin(), screditcard.end(), char_is_not_digit), screditcard.end());
        }

        if (expire_month)
        {
            sexpire_month = expire_month;
        }

        if (expire_year)
        {
            sexpire_year = expire_year;
        }

        if (cv2)
        {
            scv2 = cv2;
        }

        int tam = int(256 + sfirstname.size() + slastname.size() + screditcard.size()
                + sexpire_month.size() + sexpire_year.size() + scv2.size() + saddress1.size()
                + saddress2.size() + scity.size() + sprovince.size() + spostalcode.size()
                + scountry.size() + email.size());

        char *ccplain = new char[tam];
        snprintf(ccplain, tam, "{\"first_name\":\"%s\",\"last_name\":\"%s\","
                "\"card_number\":\"%s\","
                "\"expiry_date_month\":\"%s\",\"expiry_date_year\":\"%s\","
                "\"cv2\":\"%s\",\"address1\":\"%s\","
                "\"address2\":\"%s\",\"city\":\"%s\","
                "\"province\":\"%s\",\"postal_code\":\"%s\","
                "\"country_code\":\"%s\",\"email_address\":\"%s\"}", sfirstname.c_str(), slastname.c_str(),
                 screditcard.c_str(), sexpire_month.c_str(), sexpire_year.c_str(), scv2.c_str(), saddress1.c_str(),
                 saddress2.c_str(), scity.c_str(), sprovince.c_str(), spostalcode.c_str(), scountry.c_str(), email.c_str());

        request->setText((const char* )ccplain);
        delete [] ccplain;
    }

    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::creditCardQuerySubscriptions(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CREDIT_CARD_QUERY_SUBSCRIPTIONS, listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::creditCardCancelSubscriptions(const char* reason, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CREDIT_CARD_CANCEL_SUBSCRIPTIONS, listener);
    request->setText(reason);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getPaymentMethods(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_PAYMENT_METHODS, listener);
    requestQueue.push(request);
    waiter->notify();
}

char *MegaApiImpl::exportMasterKey()
{
    sdkMutex.lock();
    char* buf = NULL;

    if(client->loggedin())
    {
        buf = new char[SymmCipher::KEYLENGTH * 4 / 3 + 4];
        Base64::btoa(client->key.key, SymmCipher::KEYLENGTH, buf);
    }

    sdkMutex.unlock();
    return buf;
}

void MegaApiImpl::updatePwdReminderData(bool lastSuccess, bool lastSkipped, bool mkExported, bool dontShowAgain, bool lastLogin, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_ATTR_USER, listener);
    request->setParamType(MegaApi::USER_ATTR_PWD_REMINDER);
    int numDetails = 0;
    if (lastSuccess) numDetails |= 0x01;
    if (lastSkipped) numDetails |= 0x02;
    if (mkExported) numDetails |= 0x04;
    if (dontShowAgain) numDetails |= 0x08;
    if (lastLogin) numDetails |= 0x10;
    request->setNumDetails(numDetails);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getCloudStorageUsed(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_CLOUD_STORAGE_USED, listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getAccountDetails(bool storage, bool transfer, bool pro, bool sessions, bool purchases, bool transactions, int source, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_ACCOUNT_DETAILS, listener);
    int numDetails = 0;
    if(storage) numDetails |= 0x01;
    if(transfer) numDetails |= 0x02;
    if(pro) numDetails |= 0x04;
    if(transactions) numDetails |= 0x08;
    if(purchases) numDetails |= 0x10;
    if(sessions) numDetails |= 0x20;
    request->setNumDetails(numDetails);
    request->setAccess(source);

    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::queryTransferQuota(long long size, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_QUERY_TRANSFER_QUOTA, listener);
    request->setNumber(size);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::changePassword(const char *oldPassword, const char *newPassword, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHANGE_PW, listener);
    request->setPassword(oldPassword);
    request->setNewPassword(newPassword);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::logout(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_LOGOUT, listener);
    request->setFlag(true);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::localLogout(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_LOGOUT, listener);
    request->setFlag(false);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::invalidateCache()
{
    sdkMutex.lock();
    nocache = true;
    sdkMutex.unlock();
}

int MegaApiImpl::getPasswordStrength(const char *password)
{
    if (!password || strlen(password) < 8)
    {
        return MegaApi::PASSWORD_STRENGTH_VERYWEAK;
    }

    double entrophy = ZxcvbnMatch(password, NULL, NULL);
    if (entrophy > 75)
    {
        return MegaApi::PASSWORD_STRENGTH_STRONG;
    }
    if (entrophy > 50)
    {
        return MegaApi::PASSWORD_STRENGTH_GOOD;
    }
    if (entrophy > 40)
    {
        return MegaApi::PASSWORD_STRENGTH_MEDIUM;
    }
    if (entrophy > 15)
    {
        return MegaApi::PASSWORD_STRENGTH_WEAK;
    }
    return MegaApi::PASSWORD_STRENGTH_VERYWEAK;
}

void MegaApiImpl::submitFeedback(int rating, const char *comment, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SUBMIT_FEEDBACK, listener);
    request->setText(comment);
    request->setNumber(rating);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::reportEvent(const char *details, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_REPORT_EVENT, listener);
    request->setText(details);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::sendEvent(int eventType, const char *message, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SEND_EVENT, listener);
    request->setNumber(eventType);
    request->setText(message);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::createSupportTicket(const char *message, int type, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SUPPORT_TICKET, listener);
    request->setParamType(type);
    request->setText(message);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::useHttpsOnly(bool usehttps, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_USE_HTTPS_ONLY, listener);
    request->setFlag(usehttps);
    requestQueue.push(request);
    waiter->notify();
}

bool MegaApiImpl::usingHttpsOnly()
{
    return client->usehttps;
}

void MegaApiImpl::getNodeAttribute(MegaNode *node, int type, const char *dstFilePath, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_ATTR_FILE, listener);
    if(dstFilePath)
    {
        string path(dstFilePath);
#if defined(_WIN32) && !defined(WINDOWS_PHONE)
        if(!PathIsRelativeA(path.c_str()) && ((path.size()<2) || path.compare(0, 2, "\\\\")))
            path.insert(0, "\\\\?\\");
#endif

        int c = path[path.size()-1];
        if((c=='/') || (c == '\\'))
        {
            const char *base64Handle = node->getBase64Handle();
            path.append(base64Handle);
            path.push_back(static_cast<char>('0' + type));
            path.append(".jpg");
            delete [] base64Handle;
        }

        request->setFile(path.c_str());
    }

    request->setParamType(type);
    if(node)
    {
        request->setNodeHandle(node->getHandle());
        const char *fileAttributes = node->getFileAttrString();
        if (fileAttributes)
        {
            request->setText(fileAttributes);
            const char *nodekey = node->getBase64Key();
            request->setPrivateKey(nodekey);
            delete [] nodekey;
            delete [] fileAttributes;
        }
    }
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::cancelGetNodeAttribute(MegaNode *node, int type, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CANCEL_ATTR_FILE, listener);
    request->setParamType(type);
    if (node)
    {
        request->setNodeHandle(node->getHandle());
        const char *fileAttributes = node->getFileAttrString();
        if (fileAttributes)
        {
            request->setText(fileAttributes);
            delete [] fileAttributes;
        }
    }
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setNodeAttribute(MegaNode *node, int type, const char *srcFilePath, MegaHandle attributehandle, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_ATTR_FILE, listener);
    if (srcFilePath) request->setFile(srcFilePath);
    request->setNumber(srcFilePath ? INVALID_HANDLE : attributehandle);
    request->setParamType(type);
    request->setNodeHandle(node ? node->getHandle() : INVALID_HANDLE);
    request->setMegaBackgroundMediaUploadPtr(nullptr);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::putNodeAttribute(MegaBackgroundMediaUpload* bu, int type, const char *srcFilePath, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_ATTR_FILE, listener);
    request->setFile(srcFilePath);
    request->setParamType(type);
    request->setMegaBackgroundMediaUploadPtr(bu);
    request->setNumber(INVALID_HANDLE);
    request->setParentHandle(INVALID_HANDLE);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getUserAttr(const char *email_or_handle, int type, const char *dstFilePath, int number, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_ATTR_USER, listener);

    if (type == MegaApi::USER_ATTR_AVATAR && dstFilePath)
    {
        string path(dstFilePath);
#if defined(_WIN32) && !defined(WINDOWS_PHONE)
        if(!PathIsRelativeA(path.c_str()) && ((path.size()<2) || path.compare(0, 2, "\\\\")))
            path.insert(0, "\\\\?\\");
#endif

        int c = path[path.size()-1];
        if((c=='/') || (c == '\\'))
        {
            path.append(email_or_handle);
            path.push_back(static_cast<char>('0' + type));
            path.append(".jpg");
        }

        request->setFile(path.c_str());
    }

    request->setParamType(type);
    request->setNumber(number);
    if(email_or_handle)
    {
        request->setEmail(email_or_handle);
    }

    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getChatUserAttr(const char *email_or_handle, int type, const char *dstFilePath, const char *ph, int number, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_ATTR_USER, listener);

    if (type == MegaApi::USER_ATTR_AVATAR && dstFilePath)
    {
        string path(dstFilePath);
#if defined(_WIN32) && !defined(WINDOWS_PHONE)
        if(!PathIsRelativeA(path.c_str()) && ((path.size()<2) || path.compare(0, 2, "\\\\")))
            path.insert(0, "\\\\?\\");
#endif

        int c = path[path.size()-1];
        if((c=='/') || (c == '\\'))
        {
            path.append(email_or_handle);
            path.push_back(static_cast<char>('0' + type));
            path.append(".jpg");
        }

        request->setFile(path.c_str());
    }

    request->setSessionKey(ph);
    request->setParamType(type);
    request->setNumber(number);
    if(email_or_handle)
    {
        request->setEmail(email_or_handle);
    }

    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setUserAttr(int type, const char *value, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_ATTR_USER, listener);
    if(type == MegaApi::USER_ATTR_AVATAR)
    {
        request->setFile(value);
    }
    else
    {
        request->setText(value);
    }

    request->setParamType(type);
    requestQueue.push(request);
    waiter->notify();
}

char *MegaApiImpl::getAvatarColor(handle userhandle)
{
    string colors[] = {        
        "#4ADC95",
        "#10CC37",
        "#31B500",
        "#00897B",
        "#00ACC1",
        "#61D2FF",
        "#2BA6DE",
        "#FFD300",
        "#FFA500",
        "#FF6F00",
        "#D84D02",
        "#FF5252",
        "#FF333A",
        "#C51162",
        "#880E4F"
    };

    int index = userhandle % (handle)(sizeof(colors)/sizeof(colors[0]));

    return MegaApi::strdup(colors[index].c_str());
}

char *MegaApiImpl::getAvatarSecondaryColor(handle userhandle)
{
    string colors[] = {
        "#64FFB3",
        "#13F241",
        "#5FDB00",
        "#00BDB2",
        "#00D5E2",
        "#9AEAFF",
        "#55D2F0",
        "#FFEB00",
        "#FFD200",
        "#FFA700",
        "#FF8700",
        "#FF8989",
        "#FF626C",
        "#E4269B",
        "#BC2086"
    };

    int index = userhandle % (handle)(sizeof(colors)/sizeof(colors[0]));

    return MegaApi::strdup(colors[index].c_str());
}

bool MegaApiImpl::isGlobalNotifiable()
{
    return !mPushSettings || (!mPushSettings->isGlobalDndEnabled() && isScheduleNotifiable());
}

bool MegaApiImpl::isScheduleNotifiable()
{
    if (!mTimezones)
    {
        LOG_warn << "Timezones are not available yet";
        return true;
    }

    if (!mPushSettings || !mPushSettings->isGlobalScheduleEnabled())
    {
        return true;
    }

    // find the configured timezone for notification's schedule and get the corresponding offset based on UTC
    int offsetTz = 0;
    bool tzFound = false;
    for (int i = 0; i < mTimezones->getNumTimeZones(); i++)
    {
        if (strcmp(mPushSettings->getGlobalScheduleTimezone(), mTimezones->getTimeZone(i)) == 0)
        {
            offsetTz = mTimezones->getTimeOffset(i);
            tzFound = true;
            break;
        }
    }

    if (!tzFound)
    {
        LOG_err << "Timezone not found: " << mPushSettings->getGlobalScheduleTimezone();
        assert(false);
        return true;    // better to generate the notification, in this case
    }

    // calculate the timestamp for time 00:00:00 of the current day in the configured timezone
    m_time_t now = m_time(NULL) + offsetTz;
    struct tm tmp;
    m_gmtime(now, &tmp);
    tmp.tm_hour = tmp.tm_min = tmp.tm_sec = 0;  // set the time to 00:00:00
    m_time_t dayStart = m_mktime_UTC(&tmp);

    // calculate the timestamps for the scheduled period
    int offsetStart = mPushSettings->getGlobalScheduleStart() * 60; // convert minutes into seconds
    int offsetEnd = mPushSettings->getGlobalScheduleEnd() * 60;
    m_time_t scheduleStart = dayStart + offsetStart;
    m_time_t scheduleEnd = dayStart + offsetEnd;

    if (offsetStart <= offsetEnd)
    {
        return now >= scheduleStart && now <= scheduleEnd;
    }
    else    // the scheduled period covers 2 days
    {
        assert(now >= dayStart && now <= dayStart + 24 * 60 * 60);
        return now <= scheduleEnd || now >= scheduleStart;
    }
}

// clears backups/requests/transfers notifying failure with EACCESS (and resets total up/down bytes)
void MegaApiImpl::abortPendingActions(error preverror)
{
    if (!preverror)
    {
        preverror = API_EACCESS;
    }

    // -- Backups --
    for (auto it : backupsMap)
    {
        delete it.second;
    }
    backupsMap.clear();

    // -- CS Requests in progress --
    deque<MegaRequestPrivate*> requests;
    for (auto requestPair : requestMap)
    {
        if (requestPair.second)
        {
            requests.push_back(requestPair.second);
        }
    }
    for (auto request : requests)
    {
        if (request->getType() == MegaRequest::TYPE_DELETE)
        {
            continue; // this request is finished at MegaApiImpl dtor
        }
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(preverror));
    }
    requestMap.clear();

    // -- Transfers in progress --
    {
        DBTableTransactionCommitter committer(client->tctable);

        // -- Transfers in the queue --
        // clear queued transfers, not yet started (and not added to cache)
        while (MegaTransferPrivate *transfer = transferQueue.pop())
        {
            fireOnTransferStart(transfer);
            transfer->setState(MegaTransfer::STATE_FAILED);
            fireOnTransferFinish(transfer, make_unique<MegaErrorPrivate>(preverror), committer);
        }

        // clear existing transfers
        while (!transferMap.empty())
        {
            auto transfer = transferMap.begin()->second;
            transfer->setState(MegaTransfer::STATE_FAILED);
            // this call can be recursive and remove multiple, eg with MegaFolderUploadController
            fireOnTransferFinish(transfer, make_unique<MegaErrorPrivate>(preverror), committer);
        }
        assert(transferMap.empty());
        transferMap.clear();

    }

    resetTotalDownloads();
    resetTotalUploads();
}

bool MegaApiImpl::hasToForceUpload(const Node &node, const MegaTransferPrivate &transfer) const
{
    bool hasPreview = (Node::hasfileattribute(&node.fileattrstring, GfxProc::PREVIEW) != 0);
    bool hasThumbnail = (Node::hasfileattribute(&node.fileattrstring, GfxProc::THUMBNAIL) != 0);
    string name = node.displayname();
    bool isMedia = gfxAccess->isgfx(&name) || gfxAccess->isvideo(&name);
    bool canForceUpload = transfer.isStreamingTransfer();
    bool isPdf = name.find(".pdf") != string::npos;

    return canForceUpload && (isMedia || isPdf) && !(hasPreview && hasThumbnail);
}

void MegaApiImpl::inviteContact(const char *email, const char *message, int action, MegaHandle contactLink, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_INVITE_CONTACT, listener);
    request->setNumber(action);
    request->setEmail(email);
    request->setText(message);
    request->setNodeHandle(contactLink);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::replyContactRequest(MegaContactRequest *r, int action, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_REPLY_CONTACT_REQUEST, listener);
    if(r)
    {
        request->setNodeHandle(r->getHandle());
    }

    request->setNumber(action);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::removeContact(MegaUser *user, MegaRequestListener* listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_REMOVE_CONTACT, listener);
    if(user)
    {
        request->setEmail(user->getEmail());
    }

    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::pauseTransfers(bool pause, int direction, MegaRequestListener* listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_PAUSE_TRANSFERS, listener);
    request->setFlag(pause);
    request->setNumber(direction);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::pauseTransfer(int transferTag, bool pause, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_PAUSE_TRANSFER, listener);
    request->setTransferTag(transferTag);
    request->setFlag(pause);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::moveTransferUp(int transferTag, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_MOVE_TRANSFER, listener);
    request->setTransferTag(transferTag);
    request->setFlag(true);
    request->setNumber(MegaTransfer::MOVE_TYPE_UP);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::moveTransferDown(int transferTag, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_MOVE_TRANSFER, listener);
    request->setTransferTag(transferTag);
    request->setFlag(true);
    request->setNumber(MegaTransfer::MOVE_TYPE_DOWN);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::moveTransferToFirst(int transferTag, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_MOVE_TRANSFER, listener);
    request->setTransferTag(transferTag);
    request->setFlag(true);
    request->setNumber(MegaTransfer::MOVE_TYPE_TOP);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::moveTransferToLast(int transferTag, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_MOVE_TRANSFER, listener);
    request->setTransferTag(transferTag);
    request->setFlag(true);
    request->setNumber(MegaTransfer::MOVE_TYPE_BOTTOM);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::moveTransferBefore(int transferTag, int prevTransferTag, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_MOVE_TRANSFER, listener);
    request->setTransferTag(transferTag);
    request->setFlag(false);
    request->setNumber(prevTransferTag);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::enableTransferResumption(const char *loggedOutId)
{
    sdkMutex.lock();
    client->enabletransferresumption(loggedOutId);
    sdkMutex.unlock();
}

void MegaApiImpl::disableTransferResumption(const char *loggedOutId)
{
    sdkMutex.lock();
    client->disabletransferresumption(loggedOutId);
    sdkMutex.unlock();
}

bool MegaApiImpl::areTransfersPaused(int direction)
{
    if(direction != MegaTransfer::TYPE_DOWNLOAD && direction != MegaTransfer::TYPE_UPLOAD)
    {
        return false;
    }

    bool result;
    sdkMutex.lock();
    if(direction == MegaTransfer::TYPE_DOWNLOAD)
    {
        result = client->xferpaused[GET];
    }
    else
    {
        result = client->xferpaused[PUT];
    }
    sdkMutex.unlock();
    return result;
}

//-1 -> AUTO, 0 -> NONE, >0 -> b/s
void MegaApiImpl::setUploadLimit(int bpslimit)
{
    client->putmbpscap = bpslimit;
}

void MegaApiImpl::setMaxConnections(int direction, int connections, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_MAX_CONNECTIONS, listener);
    request->setParamType(direction);
    request->setNumber(connections);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setDownloadMethod(int method)
{
    switch(method)
    {
        case MegaApi::TRANSFER_METHOD_NORMAL:
            client->usealtdownport = false;
            client->autodownport = false;
            break;
        case MegaApi::TRANSFER_METHOD_ALTERNATIVE_PORT:
            client->usealtdownport = true;
            client->autodownport = false;
            break;
        case MegaApi::TRANSFER_METHOD_AUTO:
            client->autodownport = true;
            break;
        case MegaApi::TRANSFER_METHOD_AUTO_NORMAL:
            client->usealtdownport = false;
            client->autodownport = true;
            break;
        case MegaApi::TRANSFER_METHOD_AUTO_ALTERNATIVE:
            client->usealtdownport = true;
            client->autodownport = true;
            break;
        default:
            break;
    }
}

void MegaApiImpl::setUploadMethod(int method)
{
    switch(method)
    {
        case MegaApi::TRANSFER_METHOD_NORMAL:
            client->usealtupport = false;
            client->autoupport = false;
            break;
        case MegaApi::TRANSFER_METHOD_ALTERNATIVE_PORT:
            client->usealtupport = true;
            client->autoupport = false;
            break;
        case MegaApi::TRANSFER_METHOD_AUTO:
            client->autoupport = true;
            break;
        case MegaApi::TRANSFER_METHOD_AUTO_NORMAL:
            client->usealtupport = false;
            client->autoupport = true;
            break;
        case MegaApi::TRANSFER_METHOD_AUTO_ALTERNATIVE:
            client->usealtupport = true;
            client->autoupport = true;
            break;
        default:
            break;
    }
}

bool MegaApiImpl::setMaxDownloadSpeed(m_off_t bpslimit)
{
    sdkMutex.lock();
    bool result = client->setmaxdownloadspeed(bpslimit);
    sdkMutex.unlock();
    return result;
}

bool MegaApiImpl::setMaxUploadSpeed(m_off_t bpslimit)
{
    sdkMutex.lock();
    bool result = client->setmaxuploadspeed(bpslimit);
    sdkMutex.unlock();
    return result;
}

int MegaApiImpl::getMaxDownloadSpeed()
{
    return int(client->getmaxdownloadspeed());
}

int MegaApiImpl::getMaxUploadSpeed()
{
    return int(client->getmaxuploadspeed());
}

int MegaApiImpl::getCurrentDownloadSpeed()
{
    return int(httpio->downloadSpeed);
}

int MegaApiImpl::getCurrentUploadSpeed()
{
    return int(httpio->uploadSpeed);
}

int MegaApiImpl::getCurrentSpeed(int type)
{
    switch (type)
    {
    case MegaTransfer::TYPE_DOWNLOAD:
        return int(httpio->downloadSpeed);
    case MegaTransfer::TYPE_UPLOAD:
        return int(httpio->uploadSpeed);
    default:
        return 0;
    }
}

int MegaApiImpl::getDownloadMethod()
{
    if (client->autodownport)
    {
        if(client->usealtdownport)
        {
            return MegaApi::TRANSFER_METHOD_AUTO_ALTERNATIVE;
        }
        else
        {
            return MegaApi::TRANSFER_METHOD_AUTO_NORMAL;
        }
    }

    if (client->usealtdownport)
    {
        return MegaApi::TRANSFER_METHOD_ALTERNATIVE_PORT;
    }

    return MegaApi::TRANSFER_METHOD_NORMAL;
}

int MegaApiImpl::getUploadMethod()
{
    if (client->autoupport)
    {
        if(client->usealtupport)
        {
            return MegaApi::TRANSFER_METHOD_AUTO_ALTERNATIVE;
        }
        else
        {
            return MegaApi::TRANSFER_METHOD_AUTO_NORMAL;
        }
    }

    if (client->usealtupport)
    {
        return MegaApi::TRANSFER_METHOD_ALTERNATIVE_PORT;
    }

    return MegaApi::TRANSFER_METHOD_NORMAL;
}

MegaTransferData *MegaApiImpl::getTransferData(MegaTransferListener *listener)
{
    MegaTransferData *data;
    sdkMutex.lock();
    data = new MegaTransferDataPrivate(&client->transferlist, notificationNumber);
    if (listener)
    {
        transferListeners.insert(listener);
    }
    sdkMutex.unlock();
    return data;
}

MegaTransfer *MegaApiImpl::getFirstTransfer(int type)
{
    if (type != MegaTransfer::TYPE_DOWNLOAD && type != MegaTransfer::TYPE_UPLOAD)
    {
        return NULL;
    }

    MegaTransfer* transfer = NULL;
    sdkMutex.lock();
    auto it = client->transferlist.begin((direction_t)type);
    if (it != client->transferlist.end((direction_t)type))
    {
         Transfer *t = (*it);
         if (t->files.size())
         {
             MegaTransferPrivate *megaTransfer = getMegaTransferPrivate(t->files.front()->tag);
             if (megaTransfer)
             {
                 transfer = megaTransfer->copy();
             }
         }
    }
    sdkMutex.unlock();
    return transfer;
}

void MegaApiImpl::notifyTransfer(int transferTag, MegaTransferListener *listener)
{
    sdkMutex.lock();
    MegaTransferPrivate *t = getMegaTransferPrivate(transferTag);
    if (!t)
    {
        sdkMutex.unlock();
        return;
    }

    fireOnTransferUpdate(t);
    if (listener)
    {
        activeTransfer = t;
        listener->onTransferUpdate(api, t);
        activeTransfer = NULL;
    }
    sdkMutex.unlock();
}

MegaTransferList *MegaApiImpl::getTransfers()
{
    sdkMutex.lock();
    vector<MegaTransfer *> transfers;
    for (int d = GET; d == GET || d == PUT; d += PUT - GET)
    {
        auto end = client->transferlist.end((direction_t)d);
        for (auto it = client->transferlist.begin((direction_t)d); it != end; it++)
        {
            Transfer *t = (*it);
            for (file_list::iterator it2 = t->files.begin(); it2 != t->files.end(); it2++)
            {
                MegaTransferPrivate* transfer = getMegaTransferPrivate((*it2)->tag);
                if (transfer)
                {
                    transfers.push_back(transfer);
                }
            }
        }
    }
    MegaTransferList *result = new MegaTransferListPrivate(transfers.data(), int(transfers.size()));
    sdkMutex.unlock();
    return result;
}

MegaTransferList *MegaApiImpl::getStreamingTransfers()
{
    sdkMutex.lock();

    vector<MegaTransfer *> transfers;
    for (std::map<int, MegaTransferPrivate *>::iterator it = transferMap.begin(); it != transferMap.end(); it++)
    {
        MegaTransferPrivate *transfer = it->second;
        if (transfer->isStreamingTransfer())
        {
            transfers.push_back(transfer);
        }
    }
    MegaTransferList *result = new MegaTransferListPrivate(transfers.data(), int(transfers.size()));

    sdkMutex.unlock();
    return result;
}

MegaTransfer *MegaApiImpl::getTransferByTag(int transferTag)
{
    MegaTransfer* value;
    sdkMutex.lock();
    value = getMegaTransferPrivate(transferTag);
    if (value)
    {
        value = value->copy();
    }
    sdkMutex.unlock();
    return value;
}

MegaTransferList *MegaApiImpl::getTransfers(int type)
{
    if(type != MegaTransfer::TYPE_DOWNLOAD && type != MegaTransfer::TYPE_UPLOAD)
    {
        return new MegaTransferListPrivate();
    }

    sdkMutex.lock();
    vector<MegaTransfer *> transfers;
    auto end = client->transferlist.end((direction_t)type);
    for (auto it = client->transferlist.begin((direction_t)type); it != end; it++)
    {
        Transfer *t = (*it);
        for (file_list::iterator it2 = t->files.begin(); it2 != t->files.end(); it2++)
        {
            MegaTransferPrivate* transfer = getMegaTransferPrivate((*it2)->tag);
            if (transfer)
            {
                transfers.push_back(transfer);
            }
        }
    }
    MegaTransferList *result = new MegaTransferListPrivate(transfers.data(), int(transfers.size()));
    sdkMutex.unlock();
    return result;
}

MegaTransferList *MegaApiImpl::getChildTransfers(int transferTag)
{
    sdkMutex.lock();

    MegaTransfer *transfer = getMegaTransferPrivate(transferTag);
    if (!transfer)
    {
        sdkMutex.unlock();
        return new MegaTransferListPrivate();
    }

    if (!transfer->isFolderTransfer())
    {
        sdkMutex.unlock();
        return new MegaTransferListPrivate();
    }

    vector<MegaTransfer *> transfers;
    for (std::map<int, MegaTransferPrivate *>::iterator it = transferMap.begin(); it != transferMap.end(); it++)
    {
        MegaTransferPrivate *t = it->second;
        if (t->getFolderTransferTag() == transferTag)
        {
            transfers.push_back(transfer);
        }
    }

    MegaTransferList *result = new MegaTransferListPrivate(transfers.data(), int(transfers.size()));
    sdkMutex.unlock();
    return result;
}

MegaTransferList *MegaApiImpl::getTansfersByFolderTag(int folderTransferTag)
{
    sdkMutex.lock();
    vector<MegaTransfer *> transfers;
    for (std::map<int, MegaTransferPrivate *>::iterator it = transferMap.begin(); it != transferMap.end(); it++)
    {
        MegaTransferPrivate *t = it->second;
        if (t->getFolderTransferTag() == folderTransferTag)
        {
            transfers.push_back(t);
        }
    }

    MegaTransferList *result = new MegaTransferListPrivate(transfers.data(), int(transfers.size()));
    sdkMutex.unlock();
    return result;
}

MegaStringList *MegaApiImpl::getBackupFolders(int backuptag)
{
    MegaStringListPrivate * backupFolders;

    map<int64_t, string> backupTimesPaths;
    sdkMutex.lock();

    map<int, MegaBackupController *>::iterator itr = backupsMap.find(backuptag) ;
    if (itr == backupsMap.end())
    {
        LOG_err << "Failed to find backup with tag " << backuptag;
        sdkMutex.unlock();
        return NULL;
    }

    MegaBackupController *mbc = itr->second;

    MegaNode * parentNode = getNodeByHandle(mbc->getMegaHandle());
    if (parentNode)
    {
        MegaNodeList* children = getChildren(parentNode, MegaApi::ORDER_NONE);
        if (children)
        {
            for (int i = 0; i < children->size(); i++)
            {
                MegaNode *childNode = children->get(i);
                string childname = childNode->getName();
                if (mbc->isBackup(childname, mbc->getBackupName()) )
                {
                    int64_t timeofbackup = mbc->getTimeOfBackup(childname);
                    if (timeofbackup)
                    {
                        backupTimesPaths[timeofbackup]=getNodePath(childNode);
                    }
                    else
                    {
                        LOG_err << "Failed to get backup time for folder: " << childname << ". Discarded.";
                    }
                }
            }

            delete children;
        }
        delete parentNode;
    }
    sdkMutex.unlock();

    vector<char*> listofpaths;

    for(map<int64_t, string>::iterator itr = backupTimesPaths.begin(); itr != backupTimesPaths.end(); itr++)
    {
        listofpaths.push_back(MegaApi::strdup(itr->second.c_str()));
    }
    backupFolders = new MegaStringListPrivate(listofpaths.data(), int(listofpaths.size()));

    return backupFolders;
}

void MegaApiImpl::setBackup(const char* localFolder, MegaNode* parent, bool attendPastBackups, int64_t period, string periodstring, int numBackups, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_ADD_BACKUP);
    if(parent) request->setNodeHandle(parent->getHandle());
    if(localFolder)
    {
        string path(localFolder);
#if defined(_WIN32) && !defined(WINDOWS_PHONE)
        if(!PathIsRelativeA(path.c_str()) && ((path.size()<2) || path.compare(0, 2, "\\\\")))
            path.insert(0, "\\\\?\\");
#endif
        request->setFile(path.data());
    }

    request->setNumRetry(numBackups);
    request->setNumber(period);
    request->setText(periodstring.c_str());
    request->setFlag(attendPastBackups);

    request->setListener(listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::removeBackup(int tag, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_REMOVE_BACKUP);
    request->setNumber(tag);
    request->setListener(listener);
    requestQueue.push(request);
    waiter->notify();
}


void MegaApiImpl::abortCurrentBackup(int tag, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_ABORT_CURRENT_BACKUP);
    request->setNumber(tag);
    request->setListener(listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::startTimer( int64_t period, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_TIMER);
    request->setNumber(period);

    request->setListener(listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::startUpload(bool startFirst, const char *localPath, MegaNode *parent, const char *fileName, const char *targetUser, int64_t mtime, int folderTransferTag, bool isBackup, const char *appData, bool isSourceFileTemporary, bool forceNewUpload, FileSystemType fsType, MegaTransferListener *listener)
{
    if (fsType == FS_UNKNOWN && localPath)
    {
        fsType = fsAccess->getFilesystemType(LocalPath::fromPath(localPath, *fsAccess));
    }

    MegaTransferPrivate* transfer = new MegaTransferPrivate(MegaTransfer::TYPE_UPLOAD, listener);
    if(localPath)
    {
        string path(localPath);
#if defined(_WIN32) && !defined(WINDOWS_PHONE)
        if(!PathIsRelativeA(path.c_str()) && ((path.size()<2) || path.compare(0, 2, "\\\\")))
            path.insert(0, "\\\\?\\");
#endif
        transfer->setPath(path.data());
    }

    if(parent)
    {
        transfer->setParentHandle(parent->getHandle());
    }

    if (targetUser)
    {
        transfer->setParentPath(targetUser);
    }

    transfer->setMaxRetries(maxRetries);
    transfer->setAppData(appData);
    transfer->setSourceFileTemporary(isSourceFileTemporary);
    transfer->setStartFirst(startFirst);

    transfer->setBackupTransfer(isBackup);

    if (fileName || transfer->getFileName())
    {
       std::string auxName = fileName
               ? fileName
               : transfer->getFileName();

       std::string path = localPath
               ? localPath
               : "";

       client->fsaccess->unescapefsincompatible(&auxName, fsType);
       transfer->setFileName(auxName.c_str());
    }

    transfer->setTime(mtime);

    if(folderTransferTag)
    {
        transfer->setFolderTransferTag(folderTransferTag);
    }

    transfer->setStreamingTransfer(forceNewUpload);

    transferQueue.push(transfer);
    waiter->notify();
}

void MegaApiImpl::startUpload(bool startFirst, const char *localPath, MegaNode *parent, const char *fileName, int64_t mtime, int folderTransferTag, bool isBackup, const char *appData, bool isSourceFileTemporary, bool forceNewUpload, FileSystemType fsType, MegaTransferListener *listener)
{ return startUpload(startFirst, localPath, parent, fileName, nullptr, mtime, folderTransferTag, isBackup, appData, isSourceFileTemporary, forceNewUpload, fsType, listener); }

void MegaApiImpl::startUpload(const char* localPath, MegaNode* parent, FileSystemType fsType, MegaTransferListener *listener)
{ return startUpload(false, localPath, parent, (const char *)NULL, -1, 0, false, NULL, false, false, fsType, listener); }

void MegaApiImpl::startUpload(const char *localPath, MegaNode *parent, int64_t mtime,FileSystemType fsType, MegaTransferListener *listener)
{ return startUpload(false, localPath, parent, (const char *)NULL, mtime, 0, false, NULL, false, false, fsType, listener); }

void MegaApiImpl::startUpload(const char* localPath, MegaNode* parent, const char* fileName, FileSystemType fsType, MegaTransferListener *listener)
{ return startUpload(false, localPath, parent, fileName, -1, 0, false, NULL, false, false, fsType, listener); }

void MegaApiImpl::startUploadForSupport(const char *localPath, bool isSourceTemporary, FileSystemType fsType, MegaTransferListener *listener)
{
    return startUpload(true, localPath, nullptr, nullptr, "pGTOqu7_Fek", -1, 0, false, nullptr, isSourceTemporary, false, fsType, listener);
}

void MegaApiImpl::startDownload(bool startFirst, MegaNode *node, const char* localPath, int folderTransferTag, const char *appData, MegaTransferListener *listener)
{
    MegaTransferPrivate* transfer = new MegaTransferPrivate(MegaTransfer::TYPE_DOWNLOAD, listener);

    if(localPath)
    {
#if defined(_WIN32) && !defined(WINDOWS_PHONE)
        string path(localPath);
        if(!PathIsRelativeA(path.c_str()) && ((path.size()<2) || path.compare(0, 2, "\\\\")))
            path.insert(0, "\\\\?\\");
        localPath = path.data();
#endif

        int c = localPath[strlen(localPath)-1];
        if (strchr(FileSystemAccess::getPathSeparator(), c))
        {
            transfer->setParentPath(localPath);
        }
        else
        {
            transfer->setPath(localPath);
        }
    }

    if (node)
    {
        transfer->setNodeHandle(node->getHandle());
        if (node->isPublic() || node->isForeign())
        {
            transfer->setPublicNode(node, true);
        }
    }

    transfer->setMaxRetries(maxRetries);
    transfer->setAppData(appData);
    transfer->setStartFirst(startFirst);

    if (folderTransferTag)
    {
        transfer->setFolderTransferTag(folderTransferTag);
    }

    transferQueue.push(transfer);
    waiter->notify();
}

void MegaApiImpl::startDownload(MegaNode *node, const char* localFolder, MegaTransferListener *listener)
{ startDownload(false, node, localFolder, 0, NULL, listener); }

void MegaApiImpl::cancelTransfer(MegaTransfer *t, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CANCEL_TRANSFER, listener);
    if(t)
    {
        request->setTransferTag(t->getTag());
    }
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::cancelTransferByTag(int transferTag, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CANCEL_TRANSFER, listener);
    request->setTransferTag(transferTag);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::cancelTransfers(int direction, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CANCEL_TRANSFERS, listener);
    request->setParamType(direction);
    request->setTransferTag(transferQueue.getLastPushedTag());
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::startStreaming(MegaNode* node, m_off_t startPos, m_off_t size, MegaTransferListener *listener)
{
    MegaTransferPrivate* transfer = new MegaTransferPrivate(MegaTransfer::TYPE_DOWNLOAD, listener);
    
    if (node)
    {
        transfer->setNodeHandle(node->getHandle());
        if (node->isPublic() || node->isForeign())
        {
            transfer->setPublicNode(node, true);
        }
    }

    transfer->setStreamingTransfer(true);
    transfer->setStartPos(startPos);
    transfer->setEndPos(startPos + size - 1);
    transfer->setMaxRetries(maxRetries);
    transferQueue.push(transfer);
    waiter->notify();
}

void MegaApiImpl::setStreamingMinimumRate(int bytesPerSecond)
{
    SdkMutexGuard g(sdkMutex);
    client->minstreamingrate = bytesPerSecond;
}

void MegaApiImpl::retryTransfer(MegaTransfer *transfer, MegaTransferListener *listener)
{
    MegaTransferPrivate *t = dynamic_cast<MegaTransferPrivate*>(transfer);
    if (!t || (t->getType() != MegaTransfer::TYPE_DOWNLOAD && t->getType() != MegaTransfer::TYPE_UPLOAD))
    {
        return;
    }

    int type = t->getType();
    if (type == MegaTransfer::TYPE_DOWNLOAD)
    {
        MegaNode *node = t->getPublicMegaNode();
        if (!node)
        {
            node = getNodeByHandle(t->getNodeHandle());
        }
        this->startDownload(t->shouldStartFirst(), node, t->getPath(), 0, t->getAppData(), listener);
        delete node;
    }
    else
    {
        MegaNode *parent = getNodeByHandle(t->getParentHandle());
        startUpload(t->shouldStartFirst(), t->getPath(), parent, t->getFileName(), t->getTime(), 0,
                          t->isBackupTransfer(), t->getAppData(), t->isSourceFileTemporary(), t->isStreamingTransfer(), client->fsaccess->getFilesystemType(LocalPath::fromLocalname(t->getPath())), listener);
        delete parent;
    }
}

#ifdef ENABLE_SYNC

//Move local files inside synced folders to the "Rubbish" folder.
bool MegaApiImpl::moveToLocalDebris(const char *path)
{
    if (!path)
    {
        return false;
    }

    sdkMutex.lock();

    string utf8path = path;
#if defined(_WIN32) && !defined(WINDOWS_PHONE)
        if(!PathIsRelativeA(utf8path.c_str()) && ((utf8path.size()<2) || utf8path.compare(0, 2, "\\\\")))
            utf8path.insert(0, "\\\\?\\");
#endif

    auto localpath = LocalPath::fromPath(utf8path, *fsAccess);
    
    Sync *sync = NULL;
    for (sync_list::iterator it = client->syncs.begin(); it != client->syncs.end(); it++)
    {
        if ((*it)->localroot->localname.isContainingPathOf(localpath, *fsAccess))
        {
            sync = (*it);
            break;
        }
    }

    if(!sync)
    {
        sdkMutex.unlock();
        return false;
    }

    bool result = sync->movetolocaldebris(localpath);
    sdkMutex.unlock();

    return result;
}

int MegaApiImpl::syncPathState(string* path)
{
#if defined(_WIN32) && !defined(WINDOWS_PHONE)
    string prefix("\\\\?\\");
    string localPrefix;
    fsAccess->path2local(&prefix, &localPrefix);
    path->append("", 1);
    if(!PathIsRelativeW((LPCWSTR)path->data()) && (path->size()<4 || memcmp(path->data(), localPrefix.data(), 4)))
    {
        path->insert(0, localPrefix);
    }
    path->resize(path->size() - 1);
    if (path->size() > (2 * sizeof(wchar_t)) && !memcmp(path->data() + path->size() -  (2 * sizeof(wchar_t)), L":\\", 2 * sizeof(wchar_t)))
    {
        path->resize(path->size() - sizeof(wchar_t));
    }
#endif

    // Avoid blocking on the mutex for a long time, as we may be blocking windows explorer (or another platform's equivalent) from opening or displaying a window, unrelated to sync folders
    // We try to lock the SDK mutex.  If we can't get it in 10ms then we return a simple default, and subsequent requests try to lock the mutex but don't wait.
    SdkMutexGuard g(sdkMutex, std::defer_lock);
    if (!syncPathStateLockTimeout && !g.try_lock_for(std::chrono::milliseconds(10)) ||
        syncPathStateLockTimeout && !g.try_lock())
    {
        syncPathStateLockTimeout = true;
        return MegaApi::STATE_IGNORED;
    }

    // once we do manage to lock, return to normal operation.
    syncPathStateLockTimeout = false;

    int state = MegaApi::STATE_NONE;
    if (client->syncs.empty())
    {
        return state;
    }

    LocalPath localpath = LocalPath::fromLocalname(*path);

    for (sync_list::iterator it = client->syncs.begin(); it != client->syncs.end(); it++)
    {
        Sync *sync = (*it);
        if (!sync->localroot->localname.isContainingPathOf(localpath, *client->fsaccess))
        {
            continue;
        }

        if (sync->localdebris.isContainingPathOf(localpath, *client->fsaccess))
        {
            state = MegaApi::STATE_IGNORED;
            break;
        }

        if (localpath == sync->localroot->localname)
        {
            state = sync->localroot->ts;
            break;
        }
        else
        {
            LocalNode* l = sync->localnodebypath(NULL, localpath);
            if (l)
            {
                state = l->ts;
            }
            else
            {
                size_t index = fsAccess->lastpartlocal(path);
                string name = path->substr(index);
                fsAccess->local2name(&name, sync->mFilesystemType);
                if (is_syncable(sync, name.c_str(), localpath))
                {
                    auto fa = fsAccess->newfileaccess();
                    if (fa->fopen(localpath, false, false) && (fa->type == FOLDERNODE || is_syncable(fa->size)))
                    {
                        state = MegaApi::STATE_PENDING;
                    }
                    else
                    {
                        state = MegaApi::STATE_IGNORED;
                    }
                }
                else
                {
                    state = MegaApi::STATE_IGNORED;
                }
            }
            break;
        }
    }
    return state;
}


MegaNode *MegaApiImpl::getSyncedNode(const LocalPath& path)
{
    sdkMutex.lock();
    MegaNode *node = NULL;
    for (sync_list::iterator it = client->syncs.begin(); (it != client->syncs.end()) && (node == NULL); it++)
    {
        Sync *sync = (*it);
        if (path == sync->localroot->localname)
        {
            node = MegaNodePrivate::fromNode(sync->localroot->node);
            break;
        }

        LocalNode * localNode = sync->localnodebypath(NULL, path);
        if(localNode) node = MegaNodePrivate::fromNode(localNode->node);
    }
    sdkMutex.unlock();
    return node;
}

void MegaApiImpl::syncFolder(const char *localFolder, MegaNode *megaFolder, MegaRegExp *regExp, long long localfp, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_ADD_SYNC);
    if(megaFolder) request->setNodeHandle(megaFolder->getHandle());
    if(localFolder)
    {
        string path(localFolder);
#if defined(_WIN32) && !defined(WINDOWS_PHONE)
        if(!PathIsRelativeA(path.c_str()) && ((path.size()<2) || path.compare(0, 2, "\\\\")))
            path.insert(0, "\\\\?\\");
#endif
        request->setFile(path.data());
    }

    request->setListener(listener);
    request->setRegExp(regExp);
    request->setNumber(localfp);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::removeSync(handle nodehandle, MegaRequestListener* listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_REMOVE_SYNC, listener);
    request->setNodeHandle(nodehandle);
    request->setFlag(true);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::disableSync(handle nodehandle, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_REMOVE_SYNC, listener);
    request->setNodeHandle(nodehandle);
    request->setFlag(false);
    requestQueue.push(request);
    waiter->notify();
}

int MegaApiImpl::getNumActiveSyncs()
{
    sdkMutex.lock();
    int num = int(client->syncs.size());
    sdkMutex.unlock();
    return num;
}

void MegaApiImpl::stopSyncs(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_REMOVE_SYNCS, listener);
    requestQueue.push(request);
    waiter->notify();
}

bool MegaApiImpl::isSynced(MegaNode *n)
{
    if(!n) return false;
    sdkMutex.lock();
    Node *node = client->nodebyhandle(n->getHandle());
    if(!node)
    {
        sdkMutex.unlock();
        return false;
    }

    bool result = (node->localnode!=NULL);
    sdkMutex.unlock();
    return result;
}

void MegaApiImpl::setExcludedNames(vector<string> *excludedNames)
{
    sdkMutex.lock();
    if (!excludedNames)
    {
        this->excludedNames.clear();
        sdkMutex.unlock();
        return;
    }

    this->excludedNames.clear();
    for (unsigned int i = 0; i < excludedNames->size(); i++)
    {
        string name = excludedNames->at(i);
        fsAccess->normalize(&name);
        if (name.size())
        {
            this->excludedNames.push_back(name);
            LOG_debug << "Excluded name: " << name;
        }
        else
        {
            LOG_warn << "Invalid excluded name: " << excludedNames->at(i);
        }
    }
    sdkMutex.unlock();
}

void MegaApiImpl::setExcludedPaths(vector<string> *excludedPaths)
{
    sdkMutex.lock();
    if (!excludedPaths)
    {
        this->excludedPaths.clear();
        sdkMutex.unlock();
        return;
    }

    this->excludedPaths.clear();
    for (unsigned int i = 0; i < excludedPaths->size(); i++)
    {
        string path = excludedPaths->at(i);
        fsAccess->normalize(&path);
        if (path.size())
        {
    #if defined(_WIN32) && !defined(WINDOWS_PHONE)
            if(!PathIsRelativeA(path.c_str()) && ((path.size()<2) || path.compare(0, 2, "\\\\")))
            {
                path.insert(0, "\\\\?\\");
            }
    #endif
            this->excludedPaths.push_back(path);
            LOG_debug << "Excluded path: " << path;
        }
        else
        {
            LOG_warn << "Invalid excluded path: " << excludedPaths->at(i);
        }
    }
    sdkMutex.unlock();
}

void MegaApiImpl::setExclusionLowerSizeLimit(long long limit)
{
    syncLowerSizeLimit = limit;
}

void MegaApiImpl::setExclusionUpperSizeLimit(long long limit)
{
    syncUpperSizeLimit = limit;
}

void MegaApiImpl::setExcludedRegularExpressions(MegaSync *sync, MegaRegExp *regExp)
{
    if (!sync)
    {
        return;
    }

    int tag = sync->getTag();
    sdkMutex.lock();
    if (syncMap.find(tag) == syncMap.end())
    {
        sdkMutex.unlock();
        return;
    }
    MegaSyncPrivate* megaSync = syncMap.at(tag);
    megaSync->setRegExp(regExp);
    sdkMutex.unlock();
}

string MegaApiImpl::getLocalPath(MegaNode *n)
{
    if(!n) return string();
    sdkMutex.lock();
    Node *node = client->nodebyhandle(n->getHandle());
    if(!node || !node->localnode)
    {
        sdkMutex.unlock();
        return string();
    }

    string result;
    LocalPath lp;
    node->localnode->getlocalpath(lp, true);
    result.swap(*lp.editStringDirect());
    result.append("", 1);
    sdkMutex.unlock();
    return result;
}

long long MegaApiImpl::getNumLocalNodes()
{
    return client->totalLocalNodes;
}

bool MegaApiImpl::isSyncable(const char *path, long long size)
{
    if (!path)
    {
        return false;
    }

    string utf8path = path;
#if defined(_WIN32) && !defined(WINDOWS_PHONE)
    if (!PathIsRelativeA(utf8path.c_str()) && ((utf8path.size()<2) || utf8path.compare(0, 2, "\\\\")))
    {
        utf8path.insert(0, "\\\\?\\");
    }
#endif

    string name;
    LocalNode *parent = NULL;
    auto localpath = LocalPath::fromPath(utf8path, *fsAccess);

    bool result = false;
    sdkMutex.lock();
    if (size >= 0)
    {
        if (!is_syncable(size))
        {
            sdkMutex.unlock();
            return false;
        }
    }

    for (sync_list::iterator it = client->syncs.begin(); it != client->syncs.end(); it++)
    {
        Sync *sync = (*it);
        if (sync->localnodebypath(NULL, localpath, &parent) || parent)
        {
            if (sync->localdebris.isContainingPathOf(localpath, *fsAccess))
            {
                break;
            }

            size_t lastpart = localpath.lastpartlocal(*fsAccess);
            name = localpath.subpathFrom(lastpart).toName(*fsAccess);
            fsAccess->local2name(&name, sync->mFilesystemType);
            result = is_syncable(sync, name.c_str(), localpath);
            break;
        }
    }
    sdkMutex.unlock();
    return result;
}

bool MegaApiImpl::isInsideSync(MegaNode *node)
{
    if (!node)
    {
        return false;
    }

    sdkMutex.lock();
    Node *n = client->nodebyhandle(node->getHandle());
    while (n)
    {
        if (n->localnode)
        {
            sdkMutex.unlock();
            return true;
        }
        n = n->parent;
    }
    sdkMutex.unlock();
    return false;
}

#endif

int MegaApiImpl::getNumPendingUploads()
{
    return pendingUploads;
}

int MegaApiImpl::getNumPendingDownloads()
{
    return pendingDownloads;
}

int MegaApiImpl::getTotalUploads()
{
    return totalUploads;
}

int MegaApiImpl::getTotalDownloads()
{
    return totalDownloads;
}

void MegaApiImpl::resetTotalDownloads()
{
    totalDownloads = 0;
    totalDownloadBytes = 0;
    totalDownloadedBytes = 0;
}

void MegaApiImpl::resetTotalUploads()
{
    totalUploads = 0;
    totalUploadBytes = 0;
    totalUploadedBytes = 0;
}

MegaNode *MegaApiImpl::getRootNode()
{
    sdkMutex.lock();
    MegaNode *result = MegaNodePrivate::fromNode(client->nodebyhandle(client->rootnodes[0]));
    sdkMutex.unlock();
    return result;
}

MegaNode* MegaApiImpl::getInboxNode()
{
    sdkMutex.lock();
    MegaNode *result = MegaNodePrivate::fromNode(client->nodebyhandle(client->rootnodes[1]));
    sdkMutex.unlock();
    return result;
}

MegaNode* MegaApiImpl::getRubbishNode()
{
    sdkMutex.lock();
    MegaNode *result = MegaNodePrivate::fromNode(client->nodebyhandle(client->rootnodes[2]));
    sdkMutex.unlock();
    return result;
}

MegaNode *MegaApiImpl::getRootNode(MegaNode *node)
{
    MegaNode *rootnode = NULL;

    sdkMutex.lock();

    Node *n;
    if (node && (n = client->nodebyhandle(node->getHandle())))
    {
        while (n->parent)
        {
            n = n->parent;
        }

        rootnode = MegaNodePrivate::fromNode(n);
    }

    sdkMutex.unlock();

    return rootnode;
}

bool MegaApiImpl::isInRootnode(MegaNode *node, int index)
{
    bool ret = false;

    sdkMutex.lock();

    MegaNode *rootnode = getRootNode(node);
    ret = (rootnode && (rootnode->getHandle() == client->rootnodes[index]));
    delete rootnode;

    sdkMutex.unlock();

    return ret;
}

void MegaApiImpl::setDefaultFilePermissions(int permissions)
{
    fsAccess->setdefaultfilepermissions(permissions);
}

int MegaApiImpl::getDefaultFilePermissions()
{
    return fsAccess->getdefaultfilepermissions();
}

void MegaApiImpl::setDefaultFolderPermissions(int permissions)
{
    fsAccess->setdefaultfolderpermissions(permissions);
}

int MegaApiImpl::getDefaultFolderPermissions()
{
    return fsAccess->getdefaultfolderpermissions();
}

long long MegaApiImpl::getBandwidthOverquotaDelay()
{
    long long result = client->overquotauntil;
    return result > Waiter::ds ? (result - Waiter::ds) / 10 : 0;
}

bool MegaApiImpl::userComparatorDefaultASC (User *i, User *j)
{
    if(strcasecmp(i->email.c_str(), j->email.c_str())<=0) return 1;
    return 0;
}

char *MegaApiImpl::escapeFsIncompatible(const char *filename, const char *dstPath)
{
    if(!filename)
    {
        return NULL;
    }
    string name = filename;
    string path = dstPath ? dstPath : "";
    client->fsaccess->escapefsincompatible(&name, client->fsaccess->getFilesystemType(LocalPath::fromPath(path, *client->fsaccess)));
    return MegaApi::strdup(name.c_str());
}

char *MegaApiImpl::unescapeFsIncompatible(const char *name, const char *path)
{
    if(!name)
    {
        return NULL;
    }
    string filename = name;
    string localpath = path ? path : "";
    client->fsaccess->unescapefsincompatible(&filename, client->fsaccess->getFilesystemType(LocalPath::fromPath(localpath, *client->fsaccess)));
    return MegaApi::strdup(filename.c_str());
}

bool MegaApiImpl::createThumbnail(const char *imagePath, const char *dstPath)
{
    if (!gfxAccess || !imagePath || !dstPath)
    {
        return false;
    }

    string utf8ImagePath = imagePath;
    string localImagePath;
    fsAccess->path2local(&utf8ImagePath, &localImagePath);

    string utf8DstPath = dstPath;
    string localDstPath;
    fsAccess->path2local(&utf8DstPath, &localDstPath);

    sdkMutex.lock();
    bool result = gfxAccess->savefa(&localImagePath, GfxProc::dimensions[GfxProc::THUMBNAIL][0],
            GfxProc::dimensions[GfxProc::THUMBNAIL][1], &localDstPath);
    sdkMutex.unlock();

    return result;
}

bool MegaApiImpl::createPreview(const char *imagePath, const char *dstPath)
{
    if (!gfxAccess || !imagePath || !dstPath)
    {
        return false;
    }

    string utf8ImagePath = imagePath;
    string localImagePath;
    fsAccess->path2local(&utf8ImagePath, &localImagePath);

    string utf8DstPath = dstPath;
    string localDstPath;
    fsAccess->path2local(&utf8DstPath, &localDstPath);

    sdkMutex.lock();
    bool result = gfxAccess->savefa(&localImagePath, GfxProc::dimensions[GfxProc::PREVIEW][0],
            GfxProc::dimensions[GfxProc::PREVIEW][1], &localDstPath);
    sdkMutex.unlock();

    return result;
}

bool MegaApiImpl::createAvatar(const char *imagePath, const char *dstPath)
{
    if (!gfxAccess || !imagePath || !dstPath)
    {
        return false;
    }
    
    string utf8ImagePath = imagePath;
    string localImagePath;
    fsAccess->path2local(&utf8ImagePath, &localImagePath);
    
    string utf8DstPath = dstPath;
    string localDstPath;
    fsAccess->path2local(&utf8DstPath, &localDstPath);
    
    sdkMutex.lock();
    bool result = gfxAccess->savefa(&localImagePath, GfxProc::dimensionsavatar[GfxProc::AVATAR250X250][0],
            GfxProc::dimensionsavatar[GfxProc::AVATAR250X250][1], &localDstPath);
    sdkMutex.unlock();
    
    return result;
}

void MegaApiImpl::backgroundMediaUploadRequestUploadURL(int64_t fullFileSize, MegaBackgroundMediaUpload* state, MegaRequestListener *listener)
{
    MegaRequestPrivate* req = new MegaRequestPrivate(MegaRequest::TYPE_GET_BACKGROUND_UPLOAD_URL, listener);
    req->setNumber(fullFileSize);
    req->setMegaBackgroundMediaUploadPtr(state);
    requestQueue.push(req);
    waiter->notify();
}

void MegaApiImpl::backgroundMediaUploadComplete(MegaBackgroundMediaUpload* state, const char* utf8Name, MegaNode *parent, const char* fingerprint, const char* fingerprintoriginal,
    const char *string64UploadToken, MegaRequestListener *listener)
{
    MegaRequestPrivate* req = new MegaRequestPrivate(MegaRequest::TYPE_COMPLETE_BACKGROUND_UPLOAD, listener);
    req->setMegaBackgroundMediaUploadPtr(static_cast<MegaBackgroundMediaUploadPrivate*>(state));
    req->setPassword(fingerprintoriginal);
    req->setNewPassword(fingerprint);
    req->setName(utf8Name);
    if (parent)
    {
        req->setParentHandle(parent->getHandle());
    }
    if (string64UploadToken)
    {
        req->setSessionKey(MegaApi::strdup(string64UploadToken));
    }
    requestQueue.push(req);
    waiter->notify();
}

bool MegaApiImpl::ensureMediaInfo()
{
#ifdef USE_MEDIAINFO
    if (client->mediaFileInfo.mediaCodecsReceived)
    {
        return true;
    }
    else
    {
        sdkMutex.lock();
        client->mediaFileInfo.requestCodecMappingsOneTime(client, NULL);
        sdkMutex.unlock();
        return false;
    }
#else
    return false;
#endif
}

void MegaApiImpl::setOriginalFingerprint(MegaNode* node, const char* originalFingerprint, MegaRequestListener *listener)
{
    MegaRequestPrivate* req = new MegaRequestPrivate(MegaRequest::TYPE_SET_ATTR_NODE, listener);
    req->setParamType(MegaApi::NODE_ATTR_ORIGINALFINGERPRINT);
    req->setText(originalFingerprint);
    req->setNodeHandle(node->getHandle());
    req->setFlag(true);
    requestQueue.push(req);
    waiter->notify();
}

bool MegaApiImpl::isOnline()
{
    return !client->httpio->noinetds;
}

#ifdef HAVE_LIBUV
bool MegaApiImpl::httpServerStart(bool localOnly, int port, bool useTLS, const char *certificatepath, const char *keypath, bool useIPv6)
{
    #ifndef ENABLE_EVT_TLS
    if (useTLS)
    {
        LOG_err << "Could not start HTTP server: TLS is not supported in current compilation";
        return false;
    }
    #endif

    if (useTLS && (!certificatepath || !keypath || !strlen(certificatepath) || !strlen(keypath)))
    {
        LOG_err << "Could not start HTTP server: No certificate/key provided";
        return false;
    }

    sdkMutex.lock();
    if (httpServer && httpServer->getPort() == port && httpServer->isLocalOnly() == localOnly)
    {
        httpServer->clearAllowedHandles();
        sdkMutex.unlock();
        return true;
    }

    httpServerStop();
    httpServer = new MegaHTTPServer(this, basePath, useTLS, certificatepath ? certificatepath : string(), keypath ? keypath : string(), useIPv6);
    httpServer->setMaxBufferSize(httpServerMaxBufferSize);
    httpServer->setMaxOutputSize(httpServerMaxOutputSize);
    httpServer->enableFileServer(httpServerEnableFiles);
    httpServer->enableOfflineAttribute(httpServerOfflineAttributeEnabled);
    httpServer->enableFolderServer(httpServerEnableFolders);
    httpServer->setRestrictedMode(httpServerRestrictedMode);
    httpServer->enableSubtitlesSupport(httpServerRestrictedMode);

    bool result = httpServer->start(port, localOnly);
    if (!result)
    {
        MegaHTTPServer *server = httpServer;
        httpServer = NULL;
        sdkMutex.unlock();
        delete server;
    }
    else
    {
        sdkMutex.unlock();
    }
    return result;
}

void MegaApiImpl::httpServerStop()
{
    sdkMutex.lock();
    if (httpServer)
    {
        MegaHTTPServer *server = httpServer;
        httpServer = NULL;
        sdkMutex.unlock();
        server->stop();
        delete server;
    }
    else
    {
        sdkMutex.unlock();
    }
}

int MegaApiImpl::httpServerIsRunning()
{
    bool result = false;
    sdkMutex.lock();
    if (httpServer)
    {
        result = httpServer->getPort();
    }
    sdkMutex.unlock();
    return result;
}

char *MegaApiImpl::httpServerGetLocalLink(MegaNode *node)
{
    if (!node)
    {
        return NULL;
    }

    sdkMutex.lock();
    if (!httpServer)
    {
        sdkMutex.unlock();
        return NULL;
    }

    char *result = httpServer->getLink(node, "http");
    sdkMutex.unlock();
    return result;
}

char *MegaApiImpl::httpServerGetLocalWebDavLink(MegaNode *node)
{
    if (!node)
    {
        return NULL;
    }

    sdkMutex.lock();
    if (!httpServer)
    {
        sdkMutex.unlock();
        return NULL;
    }

    char *result = httpServer->getWebDavLink(node);
    sdkMutex.unlock();
    return result;
}

MegaStringList *MegaApiImpl::httpServerGetWebDavLinks()
{

    MegaStringListPrivate * links;

    sdkMutex.lock();
    if (!httpServer)
    {
        sdkMutex.unlock();
        return NULL;
    }

    set<handle> handles = httpServer->getAllowedWebDavHandles();

    vector<char *> listoflinks;

    for (std::set<handle>::iterator it = handles.begin(); it != handles.end(); ++it)
    {
        handle h = *it;
        MegaNode *n = getNodeByHandle(h);
        if (n)
        {
            listoflinks.push_back(httpServer->getWebDavLink(n));
        }
    }
    sdkMutex.unlock();

    links = new MegaStringListPrivate(listoflinks.data(), int(listoflinks.size()));

    return links;
}

MegaNodeList *MegaApiImpl::httpServerGetWebDavAllowedNodes()
{
    MegaNodeListPrivate * nodes;

    sdkMutex.lock();
    if (!httpServer)
    {
        sdkMutex.unlock();
        return NULL;
    }

    set<handle> handles = httpServer->getAllowedWebDavHandles();

    vector<Node *> listofnodes;

    for (std::set<handle>::iterator it = handles.begin(); it != handles.end(); ++it)
    {
        handle h = *it;
        Node *n = client->nodebyhandle(h);
        if (n)
        {
            listofnodes.push_back(n);
        }
    }
    sdkMutex.unlock();

    nodes = new MegaNodeListPrivate(listofnodes.data(), int(listofnodes.size()));

    return nodes;
}

void MegaApiImpl::httpServerRemoveWebDavAllowedNode(MegaHandle handle)
{
    sdkMutex.lock();
    if (httpServer)
    {
        httpServer->removeAllowedWebDavHandle(handle);
    }
    sdkMutex.unlock();
}

void MegaApiImpl::httpServerRemoveWebDavAllowedNodes()
{
    sdkMutex.lock();
    if (httpServer)
    {
        httpServer->clearAllowedHandles();
    }
    sdkMutex.unlock();
}

void MegaApiImpl::httpServerSetMaxBufferSize(int bufferSize)
{
    sdkMutex.lock();
    httpServerMaxBufferSize = bufferSize <= 0 ? 0 : bufferSize;
    if (httpServer)
    {
        httpServer->setMaxBufferSize(httpServerMaxBufferSize);
    }
    sdkMutex.unlock();
}

int MegaApiImpl::httpServerGetMaxBufferSize()
{
    int value;
    sdkMutex.lock();
    if (httpServerMaxBufferSize)
    {
        value = httpServerMaxBufferSize;
    }
    else
    {
        value = StreamingBuffer::MAX_BUFFER_SIZE;
    }
    sdkMutex.unlock();
    return value;
}

void MegaApiImpl::httpServerSetMaxOutputSize(int outputSize)
{
    sdkMutex.lock();
    httpServerMaxOutputSize = outputSize <= 0 ? 0 : outputSize;
    if (httpServer)
    {
        httpServer->setMaxOutputSize(httpServerMaxOutputSize);
    }
    sdkMutex.unlock();
}

int MegaApiImpl::httpServerGetMaxOutputSize()
{
    int value;
    sdkMutex.lock();
    if (httpServerMaxOutputSize)
    {
        value = httpServerMaxOutputSize;
    }
    else
    {
        value = StreamingBuffer::MAX_OUTPUT_SIZE;
    }
    sdkMutex.unlock();
    return value;
}

void MegaApiImpl::httpServerEnableFileServer(bool enable)
{
    sdkMutex.lock();
    this->httpServerEnableFiles = enable;
    if (httpServer)
    {
        httpServer->enableFileServer(enable);
    }
    sdkMutex.unlock();
}

bool MegaApiImpl::httpServerIsFileServerEnabled()
{
    return httpServerEnableFiles;
}

void MegaApiImpl::httpServerEnableFolderServer(bool enable)
{
    sdkMutex.lock();
    this->httpServerEnableFolders = enable;
    if (httpServer)
    {
        httpServer->enableFolderServer(enable);
    }
    sdkMutex.unlock();
}

void MegaApiImpl::httpServerEnableOfflineAttribute(bool enable)
{
    sdkMutex.lock();
    this->httpServerOfflineAttributeEnabled = enable;
    if (httpServer)
    {
        httpServer->enableOfflineAttribute(enable);
    }
    sdkMutex.unlock();
}

bool MegaApiImpl::httpServerIsFolderServerEnabled()
{
    return httpServerEnableFolders;
}

bool MegaApiImpl::httpServerIsOfflineAttributeEnabled()
{
    return httpServerOfflineAttributeEnabled;
}

void MegaApiImpl::httpServerSetRestrictedMode(int mode)
{
    if (mode != MegaApi::TCP_SERVER_DENY_ALL
            && mode != MegaApi::TCP_SERVER_ALLOW_ALL
            && mode != MegaApi::TCP_SERVER_ALLOW_CREATED_LOCAL_LINKS
            && mode != MegaApi::TCP_SERVER_ALLOW_LAST_LOCAL_LINK)
    {
        return;
    }

    sdkMutex.lock();
    httpServerRestrictedMode = mode;
    if (httpServer)
    {
        httpServer->setRestrictedMode(httpServerRestrictedMode);
    }
    sdkMutex.unlock();
}

int MegaApiImpl::httpServerGetRestrictedMode()
{
    return httpServerRestrictedMode;
}

void MegaApiImpl::httpServerEnableSubtitlesSupport(bool enable)
{
    sdkMutex.lock();
    httpServerSubtitlesSupportEnabled = enable;
    if (httpServer)
    {
        httpServer->enableSubtitlesSupport(httpServerSubtitlesSupportEnabled);
    }
    sdkMutex.unlock();
}

bool MegaApiImpl::httpServerIsSubtitlesSupportEnabled()
{
    return httpServerSubtitlesSupportEnabled;
}

bool MegaApiImpl::httpServerIsLocalOnly()
{
    bool localOnly = true;
    sdkMutex.lock();
    if (httpServer)
    {
        localOnly = httpServer->isLocalOnly();
    }
    sdkMutex.unlock();
    return localOnly;
}

void MegaApiImpl::httpServerAddListener(MegaTransferListener *listener)
{
    if (!listener)
    {
        return;
    }

    sdkMutex.lock();
    httpServerListeners.insert(listener);
    sdkMutex.unlock();
}

void MegaApiImpl::httpServerRemoveListener(MegaTransferListener *listener)
{
    if (!listener)
    {
        return;
    }

    sdkMutex.lock();
    httpServerListeners.erase(listener);
    sdkMutex.unlock();
}

void MegaApiImpl::fireOnStreamingStart(MegaTransferPrivate *transfer)
{
    for(set<MegaTransferListener *>::iterator it = httpServerListeners.begin(); it != httpServerListeners.end() ; it++)
        (*it)->onTransferStart(api, transfer);
}

void MegaApiImpl::fireOnStreamingTemporaryError(MegaTransferPrivate *transfer, unique_ptr<MegaErrorPrivate> e)
{
    for(set<MegaTransferListener *>::iterator it = httpServerListeners.begin(); it != httpServerListeners.end() ; it++)
        (*it)->onTransferTemporaryError(api, transfer, e.get());
}

void MegaApiImpl::fireOnStreamingFinish(MegaTransferPrivate *transfer, unique_ptr<MegaErrorPrivate> e)
{
    if(e->getErrorCode())
    {
        LOG_warn << "Streaming request finished with error: " << e->getErrorString();
    }
    else
    {
        LOG_info << "Streaming request finished";
    }

    for(set<MegaTransferListener *>::iterator it = httpServerListeners.begin(); it != httpServerListeners.end() ; it++)
        (*it)->onTransferFinish(api, transfer, e.get());

    delete transfer;
}

bool MegaApiImpl::ftpServerStart(bool localOnly, int port, int dataportBegin, int dataPortEnd, bool useTLS, const char *certificatepath, const char *keypath)
{
    #ifndef ENABLE_EVT_TLS
    if (useTLS)
    {
        LOG_err << "Could not start FTP server: TLS is not supported in current compilation";
        return false;
    }
    #endif

    sdkMutex.lock();
    if (ftpServer && ftpServer->getPort() == port && ftpServer->isLocalOnly() == localOnly)
    {
        ftpServer->clearAllowedHandles();
        sdkMutex.unlock();
        return true;
    }

    ftpServerStop();
    ftpServer = new MegaFTPServer(this, basePath, dataportBegin, dataPortEnd, useTLS, certificatepath ? certificatepath : string(), keypath ? keypath : string());
    ftpServer->setRestrictedMode(MegaApi::TCP_SERVER_ALLOW_CREATED_LOCAL_LINKS);
    ftpServer->setRestrictedMode(ftpServerRestrictedMode);
    ftpServer->setMaxBufferSize(ftpServerMaxBufferSize);
    ftpServer->setMaxOutputSize(ftpServerMaxOutputSize);

    bool result = ftpServer->start(port, localOnly);
    if (!result)
    {
        MegaFTPServer *server = ftpServer;
        ftpServer = NULL;
        sdkMutex.unlock();
        delete server;
    }
    else
    {
        sdkMutex.unlock();
    }
    return result;
}

void MegaApiImpl::ftpServerStop()
{
    sdkMutex.lock();
    if (ftpServer)
    {
        MegaFTPServer *server = ftpServer;
        ftpServer = NULL;
        sdkMutex.unlock();
        server->stop();
        delete server;
    }
    else
    {
        sdkMutex.unlock();
    }
}

int MegaApiImpl::ftpServerIsRunning()
{
    bool result = false;
    sdkMutex.lock();
    if (ftpServer)
    {
        result = ftpServer->getPort();
    }
    sdkMutex.unlock();
    return result;
}

char *MegaApiImpl::ftpServerGetLocalLink(MegaNode *node)
{
    if (!node)
    {
        return NULL;
    }

    sdkMutex.lock();
    if (!ftpServer)
    {
        sdkMutex.unlock();
        return NULL;
    }

    char *result = ftpServer->getLink(node, "ftp");
    sdkMutex.unlock();
    return result;
}

MegaStringList *MegaApiImpl::ftpServerGetLinks()
{

    MegaStringListPrivate * links;

    sdkMutex.lock();
    if (!ftpServer)
    {
        sdkMutex.unlock();
        return NULL;
    }

    set<handle> handles = ftpServer->getAllowedHandles();

    vector<char *> listoflinks;

    for (std::set<handle>::iterator it = handles.begin(); it != handles.end(); ++it)
    {
        handle h = *it;
        MegaNode *n = getNodeByHandle(h);
        if (n)
        {
            listoflinks.push_back(ftpServer->getLink(n));

        }
    }
    sdkMutex.unlock();

    links = new MegaStringListPrivate(listoflinks.data(), int(listoflinks.size()));

    return links;
}

MegaNodeList *MegaApiImpl::ftpServerGetAllowedNodes()
{
    MegaNodeListPrivate * nodes;

    sdkMutex.lock();
    if (!ftpServer)
    {
        sdkMutex.unlock();
        return NULL;
    }

    set<handle> handles = ftpServer->getAllowedHandles();

    vector<Node *> listofnodes;

    for (std::set<handle>::iterator it = handles.begin(); it != handles.end(); ++it)
    {
        handle h = *it;
        Node *n = client->nodebyhandle(h);
        if (n)
        {
            listofnodes.push_back(n);
        }
    }
    sdkMutex.unlock();

    nodes = new MegaNodeListPrivate(listofnodes.data(), int(listofnodes.size()));

    return nodes;
}

void MegaApiImpl::ftpServerRemoveAllowedNode(MegaHandle handle)
{
    sdkMutex.lock();
    if (ftpServer)
    {
        ftpServer->removeAllowedHandle(handle);
    }
    sdkMutex.unlock();
}

void MegaApiImpl::ftpServerRemoveAllowedNodes()
{
    sdkMutex.lock();
    if (ftpServer)
    {
        ftpServer->clearAllowedHandles();
    }
    sdkMutex.unlock();
}

void MegaApiImpl::ftpServerSetMaxBufferSize(int bufferSize)
{
    sdkMutex.lock();
    ftpServerMaxBufferSize = bufferSize <= 0 ? 0 : bufferSize;
    if (ftpServer)
    {
        ftpServer->setMaxBufferSize(ftpServerMaxBufferSize);
    }
    sdkMutex.unlock();
}

int MegaApiImpl::ftpServerGetMaxBufferSize()
{
    int value;
    sdkMutex.lock();
    if (ftpServerMaxBufferSize)
    {
        value = ftpServerMaxBufferSize;
    }
    else
    {
        value = StreamingBuffer::MAX_BUFFER_SIZE;
    }
    sdkMutex.unlock();
    return value;
}

void MegaApiImpl::ftpServerSetMaxOutputSize(int outputSize)
{
    sdkMutex.lock();
    ftpServerMaxOutputSize = outputSize <= 0 ? 0 : outputSize;
    if (ftpServer)
    {
        ftpServer->setMaxOutputSize(ftpServerMaxOutputSize);
    }
    sdkMutex.unlock();
}

int MegaApiImpl::ftpServerGetMaxOutputSize()
{
    int value;
    sdkMutex.lock();
    if (ftpServerMaxOutputSize)
    {
        value = ftpServerMaxOutputSize;
    }
    else
    {
        value = StreamingBuffer::MAX_OUTPUT_SIZE;
    }
    sdkMutex.unlock();
    return value;
}

void MegaApiImpl::ftpServerSetRestrictedMode(int mode)
{
    if (mode != MegaApi::TCP_SERVER_DENY_ALL
            && mode != MegaApi::TCP_SERVER_ALLOW_ALL
            && mode != MegaApi::TCP_SERVER_ALLOW_CREATED_LOCAL_LINKS
            && mode != MegaApi::TCP_SERVER_ALLOW_LAST_LOCAL_LINK)
    {
        return;
    }

    sdkMutex.lock();
    ftpServerRestrictedMode = mode;
    if (ftpServer)
    {
        ftpServer->setRestrictedMode(ftpServerRestrictedMode);
    }
    sdkMutex.unlock();
}

int MegaApiImpl::ftpServerGetRestrictedMode()
{
    return ftpServerRestrictedMode;
}

bool MegaApiImpl::ftpServerIsLocalOnly()
{
    bool localOnly = true;
    sdkMutex.lock();
    if (ftpServer)
    {
        localOnly = ftpServer->isLocalOnly();
    }
    sdkMutex.unlock();
    return localOnly;
}

void MegaApiImpl::ftpServerAddListener(MegaTransferListener *listener)
{
    if (!listener)
    {
        return;
    }

    sdkMutex.lock();
    ftpServerListeners.insert(listener);
    sdkMutex.unlock();
}

void MegaApiImpl::ftpServerRemoveListener(MegaTransferListener *listener)
{
    if (!listener)
    {
        return;
    }

    sdkMutex.lock();
    ftpServerListeners.erase(listener);
    sdkMutex.unlock();
}

void MegaApiImpl::fireOnFtpStreamingStart(MegaTransferPrivate *transfer)
{
    for(set<MegaTransferListener *>::iterator it = ftpServerListeners.begin(); it != ftpServerListeners.end() ; it++)
        (*it)->onTransferStart(api, transfer);
}

void MegaApiImpl::fireOnFtpStreamingTemporaryError(MegaTransferPrivate *transfer, unique_ptr<MegaErrorPrivate> e)
{
    for(set<MegaTransferListener *>::iterator it = ftpServerListeners.begin(); it != ftpServerListeners.end() ; it++)
        (*it)->onTransferTemporaryError(api, transfer, e.get());
}

void MegaApiImpl::fireOnFtpStreamingFinish(MegaTransferPrivate *transfer, unique_ptr<MegaErrorPrivate> e)
{
    if(e->getErrorCode())
    {
        LOG_warn << "Streaming request finished with error: " << e->getErrorString();
    }
    else
    {
        LOG_info << "Streaming request finished";
    }

    for(set<MegaTransferListener *>::iterator it = ftpServerListeners.begin(); it != ftpServerListeners.end() ; it++)
        (*it)->onTransferFinish(api, transfer, e.get());

    delete transfer;
}

#endif

#ifdef ENABLE_CHAT

void MegaApiImpl::createChat(bool group, bool publicchat, MegaTextChatPeerList *peers, const MegaStringMap *userKeyMap, const char *title, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHAT_CREATE, listener);
    request->setFlag(group);
    request->setAccess(publicchat ? 1 : 0);
    request->setMegaTextChatPeerList(peers);
    request->setText(title);
    request->setMegaStringMap(userKeyMap);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::inviteToChat(MegaHandle chatid, MegaHandle uh, int privilege, bool openMode, const char *unifiedKey, const char *title, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHAT_INVITE, listener);
    request->setNodeHandle(chatid);
    request->setParentHandle(uh);
    request->setAccess(privilege);
    request->setText(title);
    request->setFlag(openMode);
    request->setSessionKey(unifiedKey);

    requestQueue.push(request);
    waiter->notify();
}
void MegaApiImpl::removeFromChat(MegaHandle chatid, MegaHandle uh, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHAT_REMOVE, listener);
    request->setNodeHandle(chatid);
    if (uh != INVALID_HANDLE)   // if not provided, it removes oneself from the chat
    {
        request->setParentHandle(uh);
    }
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getUrlChat(MegaHandle chatid, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHAT_URL, listener);
    request->setNodeHandle(chatid);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::grantAccessInChat(MegaHandle chatid, MegaNode *n, MegaHandle uh, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHAT_GRANT_ACCESS, listener);
    request->setParentHandle(chatid);
    request->setNodeHandle(n->getHandle());

    char uid[12];
    Base64::btoa((byte*)&uh, MegaClient::USERHANDLE, uid);
    uid[11] = 0;

    request->setEmail(uid);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::removeAccessInChat(MegaHandle chatid, MegaNode *n, MegaHandle uh, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHAT_REMOVE_ACCESS, listener);
    request->setParentHandle(chatid);
    request->setNodeHandle(n->getHandle());

    char uid[12];
    Base64::btoa((byte*)&uh, MegaClient::USERHANDLE, uid);
    uid[11] = 0;

    request->setEmail(uid);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::updateChatPermissions(MegaHandle chatid, MegaHandle uh, int privilege, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHAT_UPDATE_PERMISSIONS, listener);
    request->setNodeHandle(chatid);
    request->setParentHandle(uh);
    request->setAccess(privilege);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::truncateChat(MegaHandle chatid, MegaHandle messageid, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHAT_TRUNCATE, listener);
    request->setNodeHandle(chatid);
    request->setParentHandle(messageid);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setChatTitle(MegaHandle chatid, const char *title, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHAT_SET_TITLE, listener);
    request->setNodeHandle(chatid);
    request->setText(title);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getChatPresenceURL(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHAT_PRESENCE_URL, listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::registerPushNotification(int deviceType, const char *token, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_REGISTER_PUSH_NOTIFICATION, listener);
    request->setNumber(deviceType);
    request->setText(token);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::sendChatStats(const char *data, int port, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHAT_STATS, listener);
    request->setName(data);
    request->setNumber(port);
    request->setParamType(1);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::sendChatLogs(const char *data, const char* aid, int port, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHAT_STATS, listener);
    request->setName(data);
    request->setSessionKey(aid);
    request->setParamType(2);
    request->setNumber(port);
    requestQueue.push(request);
    waiter->notify();
}

MegaTextChatList *MegaApiImpl::getChatList()
{
    sdkMutex.lock();

    MegaTextChatListPrivate *list = new MegaTextChatListPrivate(&client->chats);

    sdkMutex.unlock();

    return list;
}

MegaHandleList *MegaApiImpl::getAttachmentAccess(MegaHandle chatid, MegaHandle h)
{
    MegaHandleList *uhList = new MegaHandleListPrivate();

    if (chatid == INVALID_HANDLE || h == INVALID_HANDLE)
    {
        return uhList;
    }

    sdkMutex.lock();

    textchat_map::iterator itc = client->chats.find(chatid);
    if (itc != client->chats.end())
    {
        attachments_map::iterator ita = itc->second->attachedNodes.find(h);
        if (ita != itc->second->attachedNodes.end())
        {
            set<handle> userList = ita->second;
            set<handle>::iterator ituh;
            for (ituh = userList.begin(); ituh != userList.end(); ituh++)
            {
                uhList->addMegaHandle(*ituh);
            }
        }
    }

    sdkMutex.unlock();

    return uhList;
}

bool MegaApiImpl::hasAccessToAttachment(MegaHandle chatid, MegaHandle h, MegaHandle uh)
{
    bool ret = false;

    if (chatid == INVALID_HANDLE || h == INVALID_HANDLE || uh == INVALID_HANDLE)
    {
        return ret;
    }

    sdkMutex.lock();

    textchat_map::iterator itc = client->chats.find(chatid);
    if (itc != client->chats.end())
    {
        attachments_map::iterator ita = itc->second->attachedNodes.find(h);
        if (ita != itc->second->attachedNodes.end())
        {
            set<handle> userList = ita->second;
            ret = (userList.find(uh) != userList.end());
        }
    }

    sdkMutex.unlock();

    return ret;
}

const char* MegaApiImpl::getFileAttribute(MegaHandle h)
{
    char* fileAttributes = NULL;

    sdkMutex.lock();
    Node *node = client->nodebyhandle(h);
    if (node)
    {
        fileAttributes = MegaApi::strdup(node->fileattrstring.c_str());
    }

    sdkMutex.unlock();

    return fileAttributes;
}

void MegaApiImpl::archiveChat(MegaHandle chatid, int archive, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHAT_ARCHIVE, listener);
    request->setNodeHandle(chatid);
    request->setFlag(archive);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setChatRetentionTime(MegaHandle chatid, int period, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_RETENTION_TIME, listener);
    request->setNodeHandle(chatid);
    request->setNumDetails(period);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::requestRichPreview(const char *url, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_RICH_LINK, listener);
    request->setLink(url);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::chatLinkHandle(MegaHandle chatid, bool del, bool createifmissing, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHAT_LINK_HANDLE, listener);
    request->setNodeHandle(chatid);
    request->setFlag(del);
    request->setAccess(createifmissing ? 1 : 0);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getChatLinkURL(MegaHandle publichandle, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHAT_LINK_URL, listener);
    request->setNodeHandle(publichandle);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::chatLinkClose(MegaHandle chatid, const char *title, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_PRIVATE_MODE, listener);
    request->setNodeHandle(chatid);
    request->setText(title);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::chatLinkJoin(MegaHandle publichandle, const char *unifiedkey, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_AUTOJOIN_PUBLIC_CHAT, listener);
    request->setNodeHandle(publichandle);
    request->setSessionKey(unifiedkey);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::enableRichPreviews(bool enable, MegaRequestListener *listener)
{
    MegaStringMap *stringMap = new MegaStringMapPrivate();
    string rawvalue = enable ? "1" : "0";
    string base64value;
    Base64::btoa(rawvalue, base64value);
    stringMap->set("num", base64value.c_str());
    setUserAttribute(MegaApi::USER_ATTR_RICH_PREVIEWS, stringMap, listener);
    delete stringMap;
}

void MegaApiImpl::isRichPreviewsEnabled(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_ATTR_USER, listener);
    request->setParamType(MegaApi::USER_ATTR_RICH_PREVIEWS);
    request->setNumDetails(0);  // 0 --> flag should indicate whether rich-links are enabled or not
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::shouldShowRichLinkWarning(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_ATTR_USER, listener);
    request->setParamType(MegaApi::USER_ATTR_RICH_PREVIEWS);
    request->setNumDetails(1);  // 1 --> flag should indicate whether to show the warning or not
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setRichLinkWarningCounterValue(int value, MegaRequestListener *listener)
{
    MegaStringMap *stringMap = new MegaStringMapPrivate();
    std::ostringstream oss;
    oss << value;
    string base64value;
    Base64::btoa(oss.str(), base64value);
    stringMap->set("c", base64value.c_str());
    setUserAttribute(MegaApi::USER_ATTR_RICH_PREVIEWS, stringMap, listener);
    delete stringMap;
}

void MegaApiImpl::enableGeolocation(MegaRequestListener *listener)
{
    MegaStringMap *stringMap = new MegaStringMapPrivate();
    string base64value;
    Base64::btoa("1", base64value);
    stringMap->set("v", base64value.c_str());
    setUserAttribute(MegaApi::USER_ATTR_GEOLOCATION, stringMap, listener);
    delete stringMap;
}

void MegaApiImpl::isGeolocationEnabled(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_ATTR_USER, listener);
    request->setParamType(MegaApi::USER_ATTR_GEOLOCATION);
    requestQueue.push(request);
    waiter->notify();
}

bool MegaApiImpl::isChatNotifiable(MegaHandle chatid)
{
    if (mPushSettings)
    {
        if (mPushSettings->isChatAlwaysNotifyEnabled(chatid))
        {
            return true;
        }

        return (!mPushSettings->isChatDndEnabled(chatid) && isGlobalNotifiable() && !mPushSettings->isGlobalChatsDndEnabled());
    }

    return true;
}

#endif

void MegaApiImpl::getCameraUploadsFolder(bool secondary, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_ATTR_USER, listener);
    request->setParamType(MegaApi::USER_ATTR_CAMERA_UPLOADS_FOLDER);
    request->setFlag(secondary);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setCameraUploadsFolder(MegaHandle nodehandle, bool secondary, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_ATTR_USER, listener);

    MegaStringMapPrivate stringMap;
    const char *key = secondary ? "sh" : "h";
    stringMap.set(key, Base64Str<MegaClient::NODEHANDLE>(nodehandle));
    request->setMegaStringMap(&stringMap);
    request->setParamType(MegaApi::USER_ATTR_CAMERA_UPLOADS_FOLDER);
    request->setFlag(secondary);
    request->setNodeHandle(nodehandle);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setCameraUploadsFolders(MegaHandle primaryFolder, MegaHandle secondaryFolder, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_ATTR_USER, listener);

    MegaStringMapPrivate stringMap;
    if (!ISUNDEF(primaryFolder))
    {
        stringMap.set("h", Base64Str<MegaClient::NODEHANDLE>(primaryFolder));
    }
    if (!ISUNDEF(secondaryFolder))
    {
        stringMap.set("sh", Base64Str<MegaClient::NODEHANDLE>(secondaryFolder));
    }
    request->setMegaStringMap(&stringMap);
    request->setParamType(MegaApi::USER_ATTR_CAMERA_UPLOADS_FOLDER);
    request->setNodeHandle(primaryFolder);
    request->setParentHandle(secondaryFolder);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getMyChatFilesFolder(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_ATTR_USER, listener);
    request->setParamType(MegaApi::USER_ATTR_MY_CHAT_FILES_FOLDER);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setMyChatFilesFolder(MegaHandle nodehandle, MegaRequestListener *listener)
{
    MegaStringMapPrivate stringMap;
    stringMap.set("h", Base64Str<MegaClient::NODEHANDLE>(nodehandle));
    setUserAttribute(MegaApi::USER_ATTR_MY_CHAT_FILES_FOLDER, &stringMap, listener);
}

void MegaApiImpl::getUserAlias(MegaHandle uh, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_ATTR_USER, listener);
    request->setParamType(MegaApi::USER_ATTR_ALIAS);
    request->setNodeHandle(uh);
    request->setText(Base64Str<MegaClient::USERHANDLE>(uh));
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setUserAlias(MegaHandle uh, const char *alias, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_ATTR_USER, listener);
    MegaStringMapPrivate stringMap;
    string buf = alias ? alias : "";    // alias is null to remove it
    stringMap.set(Base64Str<MegaClient::USERHANDLE>(uh), Base64::btoa(buf).c_str());
    request->setMegaStringMap(&stringMap);
    request->setParamType(MegaApi::USER_ATTR_ALIAS);
    request->setNodeHandle(uh);
    request->setText(alias);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getPushNotificationSettings(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_ATTR_USER, listener);
    request->setParamType(MegaApi::USER_ATTR_PUSH_SETTINGS);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setPushNotificationSettings(MegaPushNotificationSettings *settings, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_ATTR_USER, listener);
    request->setParamType(MegaApi::USER_ATTR_PUSH_SETTINGS);
    request->setMegaPushNotificationSettings(settings);
    requestQueue.push(request);
    waiter->notify();
}

bool MegaApiImpl::isSharesNotifiable()
{
    return !mPushSettings || (mPushSettings->isSharesEnabled() && isScheduleNotifiable());
}

bool MegaApiImpl::isContactsNotifiable()
{
    return !mPushSettings || (mPushSettings->isContactsEnabled() && isScheduleNotifiable());
}

void MegaApiImpl::getAccountAchievements(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_ACHIEVEMENTS, listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getMegaAchievements(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_ACHIEVEMENTS, listener);
    request->setFlag(true);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::catchup(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CATCHUP, listener);
    scRequestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::sendSMSVerificationCode(const char* phoneNumber, MegaRequestListener *listener, bool reverifying_whitelisted)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SEND_SMS_VERIFICATIONCODE, listener);
    request->setText(phoneNumber);
    request->setFlag(reverifying_whitelisted);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::checkSMSVerificationCode(const char* verificationCode, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHECK_SMS_VERIFICATIONCODE, listener);
    request->setText(verificationCode);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getRegisteredContacts(const MegaStringMap* contacts, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_REGISTERED_CONTACTS, listener);
    request->setMegaStringMap(contacts);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getCountryCallingCodes(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_COUNTRY_CALLING_CODES, listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getPublicLinkInformation(const char *megaFolderLink, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_PUBLIC_LINK_INFORMATION, listener);
    request->setLink(megaFolderLink);
    requestQueue.push(request);
    waiter->notify();
}

MegaUserList* MegaApiImpl::getContacts()
{
    sdkMutex.lock();

    vector<User*> vUsers;
    for (user_map::iterator it = client->users.begin() ; it != client->users.end() ; it++ )
    {
        User *u = &(it->second);
        if (u->userhandle == client->me)
        {
            continue;
        }
        vector<User *>::iterator i = std::lower_bound(vUsers.begin(), vUsers.end(), u, MegaApiImpl::userComparatorDefaultASC);
        vUsers.insert(i, u);
    }
    MegaUserList *userList = new MegaUserListPrivate(vUsers.data(), int(vUsers.size()));

    sdkMutex.unlock();

    return userList;
}


MegaUser* MegaApiImpl::getContact(const char *uid)
{
    sdkMutex.lock();
    MegaUser *user = MegaUserPrivate::fromUser(client->finduser(uid, 0));

    if (user && user->getHandle() == client->me)
    {
        delete user;
        user = NULL;    // it's not a contact
    }

    sdkMutex.unlock();
    return user;
}

MegaUserAlertList* MegaApiImpl::getUserAlerts()
{
    sdkMutex.lock();

    vector<UserAlert::Base*> v;
    v.reserve(client->useralerts.alerts.size());
    for (UserAlerts::Alerts::iterator it = client->useralerts.alerts.begin(); it != client->useralerts.alerts.end(); ++it)
    {
        v.push_back(*it);
    }
    MegaUserAlertList *alertList = new MegaUserAlertListPrivate(v.data(), int(v.size()), client);

    sdkMutex.unlock();

    return alertList;
}

int MegaApiImpl::getNumUnreadUserAlerts()
{
    int result = 0;
    sdkMutex.lock();
    for (UserAlerts::Alerts::iterator it = client->useralerts.alerts.begin(); it != client->useralerts.alerts.end(); ++it)
    {
        if (!(*it)->seen)
        {
            result++;
        }
    }
    sdkMutex.unlock();
    return result;
}

MegaNodeList* MegaApiImpl::getInShares(MegaUser *megaUser, int order)
{
    if (!megaUser)
    {
        return new MegaNodeListPrivate();
    }

    sdkMutex.lock();
    node_vector vNodes;
    User *user = client->finduser(megaUser->getEmail(), 0);
    if (!user)
    {
        sdkMutex.unlock();
        return new MegaNodeListPrivate();
    }

    for (handle_set::iterator sit = user->sharing.begin(); sit != user->sharing.end(); sit++)
    {
        Node *n;
        if ((n = client->nodebyhandle(*sit)) && !n->parent)
        {
            vNodes.push_back(n);
        }
    }

    MegaNodeList *nodeList;
    if (vNodes.size())
    {
        sortByComparatorFunction(vNodes, order, *client);
        nodeList = new MegaNodeListPrivate(vNodes.data(), int(vNodes.size()));
    }
    else
    {
        nodeList = new MegaNodeListPrivate();
    }

    sdkMutex.unlock();
    return nodeList;
}

MegaNodeList* MegaApiImpl::getInShares(int order)
{
    sdkMutex.lock();

    node_vector nodes;
    for (user_map::iterator it = client->users.begin(); it != client->users.end(); it++)
    {
        Node *n;
        User *user = &(it->second);
        for (handle_set::iterator sit = user->sharing.begin(); sit != user->sharing.end(); sit++)
        {
            if ((n = client->nodebyhandle(*sit)) && !n->parent)
            {
                nodes.push_back(n);
            }
        }
    }

    sortByComparatorFunction(nodes, order, *client);

    MegaNodeList *nodeList = new MegaNodeListPrivate(nodes.data(), int(nodes.size()));
    sdkMutex.unlock();
    return nodeList;
}

MegaShareList* MegaApiImpl::getInSharesList(int order)
{
    sdkMutex.lock();

    node_vector nodes;
    for(user_map::iterator it = client->users.begin(); it != client->users.end(); it++)
    {
        Node *n;
        User *user = &(it->second);
        for (handle_set::iterator sit = user->sharing.begin(); sit != user->sharing.end(); sit++)
        {
            if ((n = client->nodebyhandle(*sit)) && !n->parent)
            {
                nodes.push_back(n);
            }
        }
    }

    sortByComparatorFunction(nodes, order, *client);

    vector<Share*> shares;
    handle_vector handles;
    for (Node *node : nodes)
    {
        shares.push_back(node->inshare);
        handles.push_back(node->nodehandle);
    }

    MegaShareList *shareList = new MegaShareListPrivate(shares.data(), handles.data(), int(shares.size()));
    sdkMutex.unlock();
    return shareList;
}

MegaUser *MegaApiImpl::getUserFromInShare(MegaNode *megaNode, bool recurse)
{
    if (!megaNode)
    {
        return NULL;
    }

    MegaUser *user = NULL;

    sdkMutex.lock();

    Node *node = client->nodebyhandle(megaNode->getHandle());
    if (recurse && node)
    {
        node = client->getrootnode(node);
    }

    if (node && node->inshare && node->inshare->user)
    {
        user = MegaUserPrivate::fromUser(node->inshare->user);
    }

    sdkMutex.unlock();

    return user;
}

bool MegaApiImpl::isPendingShare(MegaNode *megaNode)
{
    if(!megaNode) return false;

    sdkMutex.lock();
    Node *node = client->nodebyhandle(megaNode->getHandle());
    if(!node)
    {
        sdkMutex.unlock();
        return false;
    }

    bool result = (node->pendingshares != NULL);
    sdkMutex.unlock();

    return result;
}

MegaShareList *MegaApiImpl::getOutShares(int order)
{
    sdkMutex.lock();

    OutShareProcessor shareProcessor(*client);
    processTree(client->nodebyhandle(client->rootnodes[0]), &shareProcessor, true);
    shareProcessor.sortShares(order);
    MegaShareList *shareList = new MegaShareListPrivate(shareProcessor.getShares().data(), shareProcessor.getHandles().data(), int(shareProcessor.getShares().size()));

    sdkMutex.unlock();
    return shareList;
}

MegaShareList* MegaApiImpl::getOutShares(MegaNode *megaNode)
{
    if(!megaNode) return new MegaShareListPrivate();

    sdkMutex.lock();
    Node *node = client->nodebyhandle(megaNode->getHandle());
    if(!node)
    {
        sdkMutex.unlock();
        return new MegaShareListPrivate();
    }

    if(!node->outshares && !node->pendingshares)
    {
        sdkMutex.unlock();
        return new MegaShareListPrivate();
    }

    vector<Share*> vShares;
    vector<handle> vHandles;

    if (node->outshares)
    {
        for (share_map::iterator it = node->outshares->begin(); it != node->outshares->end(); it++)
        {
            Share *share = it->second;
            if (share->user)
            {
                vShares.push_back(share);
                vHandles.push_back(node->nodehandle);
            }
        }
    }

    if (node->pendingshares)
    {
        for (share_map::iterator it = node->pendingshares->begin(); it != node->pendingshares->end(); it++)
        {
            vShares.push_back(it->second);
            vHandles.push_back(node->nodehandle);
        }
    }

    MegaShareList *shareList = new MegaShareListPrivate(vShares.data(), vHandles.data(), int(vShares.size()));
    sdkMutex.unlock();
    return shareList;
}

MegaShareList *MegaApiImpl::getPendingOutShares()
{
    sdkMutex.lock();

    PendingOutShareProcessor shareProcessor;
    processTree(client->nodebyhandle(client->rootnodes[0]), &shareProcessor, true);
    MegaShareList *shareList = new MegaShareListPrivate(shareProcessor.getShares().data(), shareProcessor.getHandles().data(), int(shareProcessor.getShares().size()));

    sdkMutex.unlock();
    return shareList;
}

MegaShareList *MegaApiImpl::getPendingOutShares(MegaNode *megaNode)
{
    if(!megaNode)
    {
        return new MegaShareListPrivate();
    }

    sdkMutex.lock();
    Node *node = client->nodebyhandle(megaNode->getHandle());
    if(!node || !node->pendingshares)
    {
        sdkMutex.unlock();
        return new MegaShareListPrivate();
    }

    vector<Share*> vShares;
    vector<handle> vHandles;
    for (share_map::iterator it = node->pendingshares->begin(); it != node->pendingshares->end(); it++)
    {
        vShares.push_back(it->second);
        vHandles.push_back(node->nodehandle);
    }

    MegaShareList *shareList = new MegaShareListPrivate(vShares.data(), vHandles.data(), int(vShares.size()));
    sdkMutex.unlock();
    return shareList;
}

MegaNodeList *MegaApiImpl::getPublicLinks(int order)
{
    sdkMutex.lock();

    Node *n;
    node_vector nodes;
    for (const auto& item : client->mPublicLinks)
    {
        n = client->nodebyhandle(item.first);
        assert(n);
        nodes.emplace_back(n);
    }
    sortByComparatorFunction(nodes, order, *client);
    MegaNodeList *nodeList = new MegaNodeListPrivate(nodes.data(), int(nodes.size()));

    sdkMutex.unlock();
    return nodeList;
}

MegaContactRequestList *MegaApiImpl::getIncomingContactRequests()
{
    sdkMutex.lock();
    vector<PendingContactRequest*> vContactRequests;
    for (handlepcr_map::iterator it = client->pcrindex.begin(); it != client->pcrindex.end(); it++)
    {
        if(!it->second->isoutgoing && !it->second->removed())
        {
            vContactRequests.push_back(it->second);
        }
    }

    MegaContactRequestList *requestList = new MegaContactRequestListPrivate(vContactRequests.data(), int(vContactRequests.size()));
    sdkMutex.unlock();

    return requestList;
}

MegaContactRequestList *MegaApiImpl::getOutgoingContactRequests()
{
    sdkMutex.lock();
    vector<PendingContactRequest*> vContactRequests;
    for (handlepcr_map::iterator it = client->pcrindex.begin(); it != client->pcrindex.end(); it++)
    {
        if(it->second->isoutgoing && !it->second->removed())
        {
            vContactRequests.push_back(it->second);
        }
    }

    MegaContactRequestList *requestList = new MegaContactRequestListPrivate(vContactRequests.data(), int(vContactRequests.size()));
    sdkMutex.unlock();

    return requestList;
}

int MegaApiImpl::getAccess(MegaNode* megaNode)
{
    if(!megaNode) return MegaShare::ACCESS_UNKNOWN;

    sdkMutex.lock();
    Node *node = client->nodebyhandle(megaNode->getHandle());
    if(!node)
    {
        sdkMutex.unlock();
        return MegaShare::ACCESS_UNKNOWN;
    }

    if (!client->loggedin())
    {
        sdkMutex.unlock();
        return MegaShare::ACCESS_READ;
    }

    if(node->type > FOLDERNODE)
    {
        sdkMutex.unlock();
        return MegaShare::ACCESS_OWNER;
    }

    Node *n = node;
    accesslevel_t a = OWNER;
    while (n)
    {
        if (n->inshare) { a = n->inshare->access; break; }
        n = n->parent;
    }

    sdkMutex.unlock();

    switch(a)
    {
        case RDONLY: return MegaShare::ACCESS_READ;
        case RDWR: return MegaShare::ACCESS_READWRITE;
        case FULL: return MegaShare::ACCESS_FULL;
        default: return MegaShare::ACCESS_OWNER;
    }
}

MegaRecentActionBucketList* MegaApiImpl::getRecentActions(unsigned days, unsigned maxnodes)
{
    SdkMutexGuard g(sdkMutex);
    m_time_t since = m_time() - days * 86400;
    recentactions_vector v = client->getRecentActions(maxnodes, since);
    return new MegaRecentActionBucketListPrivate(v, client);
}

bool MegaApiImpl::processMegaTree(MegaNode* n, MegaTreeProcessor* processor, bool recursive)
{
    if (!n)
    {
        return true;
    }

    if (!processor)
    {
        return false;
    }

    sdkMutex.lock();
    Node *node = NULL;

    if (!n->isForeign() && !n->isPublic())
    {
        node = client->nodebyhandle(n->getHandle());
    }

    if (!node)
    {
        if (n->getType() != FILENODE)
        {
            MegaNodeList *nList = n->getChildren();
            if (nList)
            {
                for (int i = 0; i < nList->size(); i++)
                {
                    MegaNode *child = nList->get(i);
                    if (recursive)
                    {
                        if (!processMegaTree(child, processor))
                        {
                            sdkMutex.unlock();
                            return 0;
                        }
                    }
                    else
                    {
                        if (!processor->processMegaNode(child))
                        {
                            sdkMutex.unlock();
                            return 0;
                        }
                    }
                }
            }
        }
        bool result = processor->processMegaNode(n);
        sdkMutex.unlock();
        return result;
    }

    if (node->type != FILENODE)
    {
        for (node_list::iterator it = node->children.begin(); it != node->children.end(); )
        {
            MegaNode *megaNode = MegaNodePrivate::fromNode(*it++);
            if (recursive)
            {
                if (!processMegaTree(megaNode,processor))
                {
                    delete megaNode;
                    sdkMutex.unlock();
                    return 0;
                }
            }
            else
            {
                if (!processor->processMegaNode(megaNode))
                {
                    delete megaNode;
                    sdkMutex.unlock();
                    return 0;
                }
            }
            delete megaNode;
        }
    }
    bool result = processor->processMegaNode(n);
    sdkMutex.unlock();
    return result;
}

MegaNodeList* MegaApiImpl::searchInAllShares(const char *searchString, MegaCancelToken *cancelToken, int order, int target)
{
    if (!searchString)
    {
        return new MegaNodeListPrivate();
    }

    if (cancelToken && cancelToken->isCancelled())
    {
        return new MegaNodeListPrivate();
    }

    if (target < TARGET_INSHARE || target > TARGET_PUBLICLINK)
    {
        return new MegaNodeListPrivate();
    }

    SdkMutexGuard g(sdkMutex);

    if (cancelToken && cancelToken->isCancelled())
    {
        return new MegaNodeListPrivate();
    }

    node_vector result;
    Node *node;
    if (target == TARGET_INSHARE || target == TARGET_OUTSHARE)
    {
        // Search in inShares or outShares
        ::mega::unique_ptr<MegaShareList> shares (target == TARGET_INSHARE
                                                  ? getInSharesList(MegaApi::ORDER_NONE)
                                                  : getOutShares(MegaApi::ORDER_NONE));

        for (int i = 0; i < shares->size() && !(cancelToken && cancelToken->isCancelled()); i++)
        {
           node = client->nodebyhandle(shares->get(i)->getNodeHandle());
           SearchTreeProcessor searchProcessor(searchString);
           processTree(node, &searchProcessor, true, cancelToken);
           vector<Node *>& vNodes  = searchProcessor.getResults();
           result.insert(result.end(), vNodes.begin(), vNodes.end());
        }
    }
    else
    {
        // Search in public links
        for (auto it = client->mPublicLinks.begin(); it != client->mPublicLinks.end()
             && !(cancelToken && cancelToken->isCancelled()); it++)
        {
            node = client->nodebyhandle(it->first);
            SearchTreeProcessor searchProcessor(searchString);
            processTree(node, &searchProcessor, true, cancelToken);
            vector<Node *>& vNodes  = searchProcessor.getResults();
            result.insert(result.end(), vNodes.begin(), vNodes.end());
        }
    }

    sortByComparatorFunction(result, order, *client);
    MegaNodeList *nodeList = new MegaNodeListPrivate(result.data(), int(result.size()));
    return nodeList;
}

MegaNodeList *MegaApiImpl::search(const char *searchString, MegaCancelToken *cancelToken, int order)
{
    if(!searchString)
    {
        return new MegaNodeListPrivate();
    }

    if (cancelToken && cancelToken->isCancelled())
    {
        return new MegaNodeListPrivate();
    }

    SdkMutexGuard g(sdkMutex);

    if (cancelToken && cancelToken->isCancelled())
    {
        return new MegaNodeListPrivate();
    }

    node_vector result;
    Node *node;

    // rootnodes
    for (unsigned int i = 0; i < (sizeof client->rootnodes / sizeof *client->rootnodes)
          && !(cancelToken && cancelToken->isCancelled()); i++)
    {
        node = client->nodebyhandle(client->rootnodes[i]);

        SearchTreeProcessor searchProcessor(searchString);
        processTree(node, &searchProcessor, true, cancelToken);
        node_vector& vNodes = searchProcessor.getResults();

        result.insert(result.end(), vNodes.begin(), vNodes.end());
    }

    // inshares
    MegaShareList *shares = getInSharesList(MegaApi::ORDER_NONE);
    for (int i = 0; i < shares->size() && !(cancelToken && cancelToken->isCancelled()); i++)
    {
        node = client->nodebyhandle(shares->get(i)->getNodeHandle());

        SearchTreeProcessor searchProcessor(searchString);
        processTree(node, &searchProcessor, true, cancelToken);
        vector<Node *>& vNodes  = searchProcessor.getResults();

        result.insert(result.end(), vNodes.begin(), vNodes.end());
    }
    delete shares;

    sortByComparatorFunction(result, order, *client);
    MegaNodeList *nodeList = new MegaNodeListPrivate(result.data(), int(result.size()));
    
    return nodeList;
}

MegaNode *MegaApiImpl::createForeignFileNode(MegaHandle handle, const char *key, const char *name, m_off_t size, m_off_t mtime,
                                            MegaHandle parentHandle, const char* privateauth, const char *publicauth, const char *chatauth)
{
    string nodekey;
    string attrstring;
    string fileattrsting;
    nodekey.resize(strlen(key) * 3 / 4 + 3);
    nodekey.resize(Base64::atob(key, (byte *)nodekey.data(), int(nodekey.size())));
    return new MegaNodePrivate(name, FILENODE, size, mtime, mtime, handle, &nodekey, &attrstring, &fileattrsting, NULL, NULL, INVALID_HANDLE,
                               parentHandle, privateauth, publicauth, false, true, chatauth);
}

MegaNode *MegaApiImpl::createForeignFolderNode(MegaHandle handle, const char *name, MegaHandle parentHandle, const char *privateauth, const char *publicauth)
{
    string nodekey;
    string attrstring;
    string fileattrsting;
    return new MegaNodePrivate(name, FOLDERNODE, 0, 0, 0, handle, &nodekey, &attrstring, &fileattrsting, NULL, NULL, INVALID_HANDLE, parentHandle,
                               privateauth, publicauth, false, true);
}

MegaNode *MegaApiImpl::authorizeNode(MegaNode *node)
{
    if (!node)
    {
        return NULL;
    }

    if (node->isPublic() || node->isForeign())
    {
        return node->copy();
    }

    MegaNodePrivate *result = NULL;
    sdkMutex.lock();
    Node *n = client->nodebyhandle(node->getHandle());
    if (n)
    {
        result = new MegaNodePrivate(node);
        authorizeMegaNodePrivate(result);
    }

    sdkMutex.unlock();
    return result;
}

void MegaApiImpl::authorizeMegaNodePrivate(MegaNodePrivate *node)
{
    // Versions are not added to authorized MegaNodes.
    // If it's decided to do so, it's needed to modify the processing
    // of MegaNode copies accordingly

    node->setForeign(true);
    if (node->getType() == MegaNode::TYPE_FILE)
    {
        char *h = NULL;
        if (client->sid.size())
        {
            h = getAccountAuth();
            node->setPrivateAuth(h);
        }
        else
        {
            h = MegaApiImpl::handleToBase64(client->getrootpublicfolder());
            node->setPublicAuth(h);
        }
        delete [] h;
    }
    else
    {
        MegaNodeList *children = getChildren(node, MegaApi::ORDER_NONE);
        node->setChildren(children);
        for (int i = 0; i < children->size(); i++)
        {
            MegaNodePrivate *privNode = (MegaNodePrivate *)children->get(i);
            authorizeMegaNodePrivate(privNode);
        }
    }
}

MegaNode *MegaApiImpl::authorizeChatNode(MegaNode *node, const char *cauth)
{
    if (!node)
    {
        return NULL;
    }

    MegaNodePrivate *result = new MegaNodePrivate(node);
    result->setChatAuth(cauth);

    return result;
}

const char *MegaApiImpl::getVersion()
{
    return client->version();
}

char *MegaApiImpl::getOperatingSystemVersion()
{
    string version;
    fsAccess->osversion(&version, false);
    return MegaApi::strdup(version.c_str());
}

void MegaApiImpl::getLastAvailableVersion(const char *appKey, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_APP_VERSION, listener);
    request->setText(appKey);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getLocalSSLCertificate(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_LOCAL_SSL_CERT, listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::queryDNS(const char *hostname, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_QUERY_DNS, listener);
    request->setName(hostname);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::queryGeLB(const char *service, int timeoutds, int maxretries, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_QUERY_GELB, listener);
    request->setName(service);
    request->setNumber(timeoutds);
    request->setNumRetry(maxretries);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::downloadFile(const char *url, const char *dstpath, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_DOWNLOAD_FILE, listener);
    request->setLink(url);
    request->setFile(dstpath);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::contactLinkCreate(bool renew, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CONTACT_LINK_CREATE, listener);
    request->setFlag(renew);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::contactLinkQuery(MegaHandle handle, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CONTACT_LINK_QUERY, listener);
    request->setNodeHandle(handle);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::contactLinkDelete(MegaHandle handle, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CONTACT_LINK_DELETE, listener);
    request->setNodeHandle(handle);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::keepMeAlive(int type, bool enable, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_KEEP_ME_ALIVE, listener);
    request->setParamType(type);
    request->setFlag(enable);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getPSA(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_PSA, listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setPSA(int id, MegaRequestListener *listener)
{
    std::ostringstream oss;
    oss << id;
    string value = oss.str();
    setUserAttr(MegaApi::USER_ATTR_LAST_PSA, value.c_str(), listener);
}

void MegaApiImpl::acknowledgeUserAlerts(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_USERALERT_ACKNOWLEDGE, listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::disableGfxFeatures(bool disable)
{
    client->gfxdisabled = disable;
}

bool MegaApiImpl::areGfxFeaturesDisabled()
{
    return !client->gfx || client->gfxdisabled;
}

const char *MegaApiImpl::getUserAgent()
{
    return client->useragent.c_str();
}

const char *MegaApiImpl::getBasePath()
{
    return basePath.c_str();
}

void MegaApiImpl::changeApiUrl(const char *apiURL, bool disablepkp)
{
    sdkMutex.lock();
    MegaClient::APIURL = apiURL;
    if(disablepkp)
    {
        MegaClient::disablepkp = true;
    }

    client->abortbackoff();
    client->disconnect();
    sdkMutex.unlock();
}

bool MegaApiImpl::setLanguage(const char *languageCode)
{
    string code;
    if (!getLanguageCode(languageCode, &code))
    {
        return false;
    }

    bool val;
    sdkMutex.lock();
    val = client->setlang(&code);
    sdkMutex.unlock();
    return val;
}

void MegaApiImpl::setLanguagePreference(const char *languageCode, MegaRequestListener *listener)
{
    setUserAttr(MegaApi::USER_ATTR_LANGUAGE, languageCode, listener);
}

void MegaApiImpl::getLanguagePreference(MegaRequestListener *listener)
{
    getUserAttr(NULL, MegaApi::USER_ATTR_LANGUAGE, NULL, 0, listener);
}

bool MegaApiImpl::getLanguageCode(const char *languageCode, string *code)
{
    if (!languageCode || !code)
    {
        return false;
    }

    size_t len = strlen(languageCode);
    if (len < 2 || len > 7)
    {
        return false;
    }

    code->clear();
    string s = languageCode;
    tolower_string(s);

    while (s.size() >= 2)
    {
        JSON json;
        nameid id = json.getnameid(s.c_str());
        switch (id)
        {
            // Regular language codes
            case MAKENAMEID2('a', 'r'):
            case MAKENAMEID2('b', 'g'):
            case MAKENAMEID2('d', 'e'):
            case MAKENAMEID2('e', 'n'):
            case MAKENAMEID2('e', 's'):
            case MAKENAMEID2('f', 'a'):
            case MAKENAMEID2('f', 'i'):
            case MAKENAMEID2('f', 'r'):
            case MAKENAMEID2('h', 'e'):
            case MAKENAMEID2('h', 'u'):
            case MAKENAMEID2('i', 'd'):
            case MAKENAMEID2('i', 't'):
            case MAKENAMEID2('n', 'l'):
            case MAKENAMEID2('p', 'l'):
            case MAKENAMEID2('r', 'o'):
            case MAKENAMEID2('r', 'u'):
            case MAKENAMEID2('s', 'k'):
            case MAKENAMEID2('s', 'l'):
            case MAKENAMEID2('s', 'r'):
            case MAKENAMEID2('t', 'h'):
            case MAKENAMEID2('t', 'l'):
            case MAKENAMEID2('t', 'r'):
            case MAKENAMEID2('u', 'k'):
            case MAKENAMEID2('v', 'i'):

            // Not used on apps
            case MAKENAMEID2('c', 'z'):
            case MAKENAMEID2('j', 'p'):
            case MAKENAMEID2('k', 'r'):
            case MAKENAMEID2('b', 'r'):
            case MAKENAMEID2('s', 'e'):
            case MAKENAMEID2('c', 'n'):
            case MAKENAMEID2('c', 't'):
                *code = s;
                break;

            // Conversions
            case MAKENAMEID2('c', 's'):
                *code = "cz";
                break;

            case MAKENAMEID2('j', 'a'):
                *code = "jp";
                break;

            case MAKENAMEID2('k', 'o'):
                *code = "kr";
                break;

            case MAKENAMEID2('p', 't'):
            case MAKENAMEID5('p', 't', '_', 'b', 'r'):
            case MAKENAMEID5('p', 't', '-', 'b', 'r'):
            case MAKENAMEID5('p', 't', '_', 'p', 't'):
            case MAKENAMEID5('p', 't', '-', 'p', 't'):
                *code = "br";
                break;

            case MAKENAMEID2('s', 'v'):
                *code = "se";
                break;

            case MAKENAMEID5('z', 'h', '_', 'c', 'n'):
            case MAKENAMEID5('z', 'h', '-', 'c', 'n'):
            case MAKENAMEID7('z', 'h', '_', 'h', 'a', 'n', 's'):
            case MAKENAMEID7('z', 'h', '-', 'h', 'a', 'n', 's'):
                *code = "cn";
                break;

            case MAKENAMEID5('z', 'h', '_', 't', 'w'):
            case MAKENAMEID5('z', 'h', '-', 't', 'w'):
            case MAKENAMEID7('z', 'h', '_', 'h', 'a', 'n', 't'):
            case MAKENAMEID7('z', 'h', '-', 'h', 'a', 'n', 't'):
                *code = "ct";
                break;

            case MAKENAMEID2('i', 'n'):
                *code = "id";
                break;

            case MAKENAMEID2('i', 'w'):
                *code = "he";
                break;

            // Not supported in the web
            case MAKENAMEID2('e', 'e'):
            case MAKENAMEID2('h', 'r'):
            case MAKENAMEID2('k', 'a'):
                break;

            default:
                LOG_debug << "Unknown language code: " << s.c_str();
                break;
        }

        if (code->size())
        {
            return true;
        }

        s.resize(s.size() - 1);
    }

    LOG_debug << "Unsupported language code: " << languageCode;
    return false;
}

void MegaApiImpl::setFileVersionsOption(bool disable, MegaRequestListener *listener)
{
    string av = disable ? "1" : "0";
    setUserAttr(MegaApi::USER_ATTR_DISABLE_VERSIONS, av.data(), listener);
}

void MegaApiImpl::getFileVersionsOption(MegaRequestListener *listener)
{
    getUserAttr(NULL, MegaApi::USER_ATTR_DISABLE_VERSIONS, NULL, 0, listener);
}

void MegaApiImpl::setContactLinksOption(bool disable, MegaRequestListener *listener)
{
    string av = disable ? "0" : "1";
    setUserAttr(MegaApi::USER_ATTR_CONTACT_LINK_VERIFICATION, av.data(), listener);
}

void MegaApiImpl::getContactLinksOption(MegaRequestListener *listener)
{
    getUserAttr(NULL, MegaApi::USER_ATTR_CONTACT_LINK_VERIFICATION, NULL, 0, listener);
}

void MegaApiImpl::retrySSLerrors(bool enable)
{
    sdkMutex.lock();
    client->retryessl = enable;
    sdkMutex.unlock();
}

void MegaApiImpl::setPublicKeyPinning(bool enable)
{
    sdkMutex.lock();
    client->disablepkp = !enable;
    sdkMutex.unlock();
}

void MegaApiImpl::pauseActionPackets()
{
    sdkMutex.lock();
    LOG_debug << "Pausing action packets";
    client->scpaused = true;
    sdkMutex.unlock();
}

void MegaApiImpl::resumeActionPackets()
{
    sdkMutex.lock();
    LOG_debug << "Resuming action packets";
    client->scpaused = false;
    sdkMutex.unlock();
}

bool MegaApiImpl::processTree(Node* node, TreeProcessor* processor, bool recursive, MegaCancelToken *cancelToken)
{
    if (!node)
    {
        return 1;
    }

    if (!processor)
    {
        return 0;
    }

    if (cancelToken && cancelToken->isCancelled())
    {
        return 0;
    }

    SdkMutexGuard g(sdkMutex);

    if (cancelToken && cancelToken->isCancelled()) // check before lock and after, in case it was cancelled while being locked
    {
        return 0;
    }

    node = client->nodebyhandle(node->nodehandle);
    if (!node)
    {
        return 1;
    }

    if (recursive && node->type != FILENODE)
    {
        for (node_list::iterator it = node->children.begin(); it != node->children.end(); )
        {
            if (!processTree(*it++, processor, recursive, cancelToken))
            {
                return 0;
            }
        }
    }

    bool result = processor->processNode(node);
    return result;
}

MegaNodeList* MegaApiImpl::search(MegaNode* n, const char* searchString, MegaCancelToken *cancelToken, bool recursive, int order)
{
    if (!n || !searchString)
    {
        return new MegaNodeListPrivate();
    }

    if (cancelToken && cancelToken->isCancelled())
    {
        return new MegaNodeListPrivate();
    }
    
    SdkMutexGuard g(sdkMutex);

    if (cancelToken && cancelToken->isCancelled())
    {
        return new MegaNodeListPrivate();
    }
    
    Node *node = client->nodebyhandle(n->getHandle());
    if (!node)
    {
        return new MegaNodeListPrivate();
    }

    SearchTreeProcessor searchProcessor(searchString);
    for (node_list::iterator it = node->children.begin(); it != node->children.end()
         && !(cancelToken && cancelToken->isCancelled()); )
    {
        processTree(*it++, &searchProcessor, recursive, cancelToken);
    }

    vector<Node *>& vNodes = searchProcessor.getResults();
    sortByComparatorFunction(vNodes, order, *client);

    MegaNodeList *nodeList = new MegaNodeListPrivate(vNodes.data(), int(vNodes.size()));
    return nodeList;
}

long long MegaApiImpl::getSize(MegaNode *n)
{
    if(!n) return 0;

    if (n->getType() == MegaNode::TYPE_FILE)
    {
       return n->getSize();
    }

    if (n->isForeign())
    {
        MegaSizeProcessor megaSizeProcessor;
        processMegaTree(n, &megaSizeProcessor);
        return megaSizeProcessor.getTotalBytes();
    }

    sdkMutex.lock();
    Node *node = client->nodebyhandle(n->getHandle());
    if(!node)
    {
        sdkMutex.unlock();
        return 0;
    }
    SizeProcessor sizeProcessor;
    processTree(node, &sizeProcessor);
    long long result = sizeProcessor.getTotalBytes();
    sdkMutex.unlock();

    return result;
}

char *MegaApiImpl::getFingerprint(const char *filePath)
{
    if(!filePath) return NULL;

    auto localpath = LocalPath::fromPath(filePath, *fsAccess);

    auto fa = fsAccess->newfileaccess();
    if(!fa->fopen(localpath, true, false))
        return NULL;

    FileFingerprint fp;
    fp.genfingerprint(fa.get());
    m_off_t size = fa->size;
    if(fp.size < 0)
        return NULL;

    string fingerprint;
    fp.serializefingerprint(&fingerprint);

    char bsize[sizeof(size)+1];
    int l = Serialize64::serialize((byte *)bsize, size);
    char *buf = new char[l * 4 / 3 + 4];
    char ssize = static_cast<char>('A' + Base64::btoa((const byte *)bsize, l, buf));

    string result(1, ssize);
    result.append(buf);
    result.append(fingerprint);
    delete [] buf;

    return MegaApi::strdup(result.c_str());
}

char *MegaApiImpl::getFingerprint(MegaNode *n)
{
    if(!n) return NULL;

    return MegaApi::strdup(n->getFingerprint());
}

void MegaApiImpl::transfer_failed(Transfer* t, const Error& e, dstime timeleft)
{
    for (file_list::iterator it = t->files.begin(); it != t->files.end(); it++)
    {
        MegaTransferPrivate* transfer = getMegaTransferPrivate((*it)->tag);
        if (!transfer)
        {
            continue;
        }
        processTransferFailed(t, transfer, e, timeleft);
    }
}

char *MegaApiImpl::getFingerprint(MegaInputStream *inputStream, int64_t mtime)
{
    if(!inputStream) return NULL;

    ExternalInputStream is(inputStream);
    m_off_t size = is.size();
    if(size < 0)
        return NULL;

    FileFingerprint fp;
    fp.genfingerprint(&is, mtime);

    if(fp.size < 0)
        return NULL;

    string fingerprint;
    fp.serializefingerprint(&fingerprint);

    char bsize[sizeof(size)+1];
    int l = Serialize64::serialize((byte *)bsize, size);
    char *buf = new char[l * 4 / 3 + 4];
    char ssize = static_cast<char>('A' + Base64::btoa((const byte *)bsize, l, buf));

    string result(1, ssize);
    result.append(buf);
    result.append(fingerprint);
    delete [] buf;

    return MegaApi::strdup(result.c_str());
}

MegaNode *MegaApiImpl::getNodeByFingerprint(const char *fingerprint)
{
    if(!fingerprint) return NULL;

    MegaNode *result;
    sdkMutex.lock();
    result = MegaNodePrivate::fromNode(getNodeByFingerprintInternal(fingerprint));
    sdkMutex.unlock();
    return result;
}

MegaNodeList *MegaApiImpl::getNodesByFingerprint(const char *fingerprint)
{
    FileFingerprint *fp = MegaApiImpl::getFileFingerprintInternal(fingerprint);
    if (!fp)
    {
        return new MegaNodeListPrivate();
    }

    sdkMutex.lock();
    node_vector *nodes = client->nodesbyfingerprint(fp);
    MegaNodeList *result = new MegaNodeListPrivate(nodes->data(), int(nodes->size()));
    delete fp;
    delete nodes;
    sdkMutex.unlock();
    return result;
}

MegaNodeList *MegaApiImpl::getNodesByOriginalFingerprint(const char *originalfingerprint, MegaNode* megaparent)
{
    SdkMutexGuard g(sdkMutex);
    Node* parent = megaparent ? client->nodebyhandle(megaparent->getHandle()) : NULL;

    if (!originalfingerprint || (megaparent && (!parent || parent->type == FILENODE)))
    {
        return new MegaNodeListPrivate();
    }

    node_vector *nodes = new node_vector;
    client->nodesbyoriginalfingerprint(originalfingerprint, parent, nodes);
    MegaNodeList *result = new MegaNodeListPrivate(nodes->data(), int(nodes->size()));
    delete nodes;
    return result;
}

MegaNode *MegaApiImpl::getExportableNodeByFingerprint(const char *fingerprint, const char *name)
{
    MegaNode *result = NULL;

    FileFingerprint *fp = MegaApiImpl::getFileFingerprintInternal(fingerprint);
    if (!fp)
    {
        return NULL;
    }

    sdkMutex.lock();
    node_vector *nodes = client->nodesbyfingerprint(fp);
    for (unsigned int i = 0; i < nodes->size(); i++)
    {
        Node *node = nodes->at(i);
        if ((!name || !strcmp(name, node->displayname())) &&
                client->checkaccess(node, OWNER))
        {
            Node *n = node;
            while (n)
            {
                if (n->type == RUBBISHNODE)
                {
                    node = NULL;
                    break;
                }
                n = n->parent;
            }

            if (!node)
            {
                continue;
            }

            result = MegaNodePrivate::fromNode(node);
            break;
        }
    }

    delete fp;
    delete nodes;
    sdkMutex.unlock();
    return result;
}

MegaNode *MegaApiImpl::getNodeByFingerprint(const char *fingerprint, MegaNode* parent)
{
    if(!fingerprint) return NULL;

    MegaNode *result;
    sdkMutex.lock();
    Node *p = NULL;
    if(parent)
    {
        p = client->nodebyhandle(parent->getHandle());
    }

    result = MegaNodePrivate::fromNode(getNodeByFingerprintInternal(fingerprint, p));
    sdkMutex.unlock();
    return result;
}

bool MegaApiImpl::hasFingerprint(const char *fingerprint)
{
    return (getNodeByFingerprintInternal(fingerprint) != NULL);
}

char *MegaApiImpl::getCRC(const char *filePath)
{
    if(!filePath) return NULL;

    auto localpath = LocalPath::fromPath(filePath, *fsAccess);

    auto fa = fsAccess->newfileaccess();
    if(!fa->fopen(localpath, true, false))
        return NULL;

    FileFingerprint fp;
    fp.genfingerprint(fa.get());
    if(fp.size < 0)
        return NULL;

    string result;
    result.resize((sizeof fp.crc) * 4 / 3 + 4);
    result.resize(Base64::btoa((const byte *)fp.crc.data(), sizeof fp.crc, (char*)result.c_str()));
    return MegaApi::strdup(result.c_str());
}

char *MegaApiImpl::getCRCFromFingerprint(const char *fingerprint)
{    
    FileFingerprint *fp = MegaApiImpl::getFileFingerprintInternal(fingerprint);
    if (!fp)
    {
        return NULL;
    }
    
    string result;
    result.resize((sizeof fp->crc) * 4 / 3 + 4);
    result.resize(Base64::btoa((const byte *)fp->crc.data(), sizeof fp->crc,(char*)result.c_str()));
    delete fp;

    return MegaApi::strdup(result.c_str());
}

char *MegaApiImpl::getCRC(MegaNode *n)
{
    if(!n) return NULL;

    sdkMutex.lock();
    Node *node = client->nodebyhandle(n->getHandle());
    if(!node || node->type != FILENODE || node->size < 0 || !node->isvalid)
    {
        sdkMutex.unlock();
        return NULL;
    }

    string result;
    result.resize((sizeof node->crc) * 4 / 3 + 4);
    result.resize(Base64::btoa((const byte *)node->crc.data(), sizeof node->crc.data(), (char*)result.c_str()));

    sdkMutex.unlock();
    return MegaApi::strdup(result.c_str());
}

MegaNode *MegaApiImpl::getNodeByCRC(const char *crc, MegaNode *parent)
{
    if(!parent) return NULL;

    sdkMutex.lock();
    Node *node = client->nodebyhandle(parent->getHandle());
    if(!node || node->type == FILENODE)
    {
        sdkMutex.unlock();
        return NULL;
    }

    byte binarycrc[sizeof(node->crc)];
    Base64::atob(crc, binarycrc, sizeof(binarycrc));

    for (node_list::iterator it = node->children.begin(); it != node->children.end(); it++)
    {
        Node *child = (*it);
        if(!memcmp(child->crc.data(), binarycrc, sizeof(node->crc)))
        {
            MegaNode *result = MegaNodePrivate::fromNode(child);
            sdkMutex.unlock();
            return result;
        }
    }

    sdkMutex.unlock();
    return NULL;
}

SearchTreeProcessor::SearchTreeProcessor(const char *search) { this->search = search; }

#if defined(_WIN32) || defined(__APPLE__)

char *strcasestr(const char *string, const char *substring)
{
    int i, j;
    for (i = 0; string[i]; i++)
    {
        for (j = 0; substring[j]; j++)
        {
            unsigned char c1 = string[i + j];
            if (!c1)
                return NULL;

            unsigned char c2 = substring[j];
            if (toupper(c1) != toupper(c2))
                break;
        }

        if (!substring[j])
            return (char *)string + i;
    }
    return NULL;
}

#endif

bool SearchTreeProcessor::processNode(Node* node)
{
    if (!node)
    {
        return true;
    }

    if (!search)
    {
        return false;
    }

    if (node->type <= FOLDERNODE && strcasestr(node->displayname(), search) != NULL)
    {
        results.push_back(node);
    }

    return true;
}

vector<Node *> &SearchTreeProcessor::getResults()
{
    return results;
}

SizeProcessor::SizeProcessor()
{
    totalBytes=0;
}

bool SizeProcessor::processNode(Node *node)
{
    if(node->type == FILENODE)
        totalBytes += node->size;
    return true;
}

long long SizeProcessor::getTotalBytes()
{
    return totalBytes;
}

void MegaApiImpl::file_added(File *f)
{
    Transfer *t = f->transfer;
    MegaTransferPrivate *transfer = currentTransfer;
    if (!transfer)
    {
        transfer = new MegaTransferPrivate(t->type);
        transfer->setSyncTransfer(true);

        if (t->type == GET)
        {
            transfer->setNodeHandle(f->h);
        }
        else
        {
#ifdef ENABLE_SYNC
            LocalNode *ll = dynamic_cast<LocalNode *>(f);
            if (ll && ll->parent && ll->parent->node)
            {
                transfer->setParentHandle(ll->parent->node->nodehandle);
            }
            else
#endif
            {
                transfer->setParentHandle(f->h);
            }
        }


        string path;
#ifdef ENABLE_SYNC
        LocalNode *l = dynamic_cast<LocalNode *>(f);
        if (l)
        {
            path = l->getLocalPath(true).toPath(*fsAccess);
        }
        else
#endif
        {
            path = f->localname.toPath(*fsAccess);
        }
        transfer->setPath(path.c_str());
    }

    currentTransfer = NULL;
    transfer->setTransfer(t);
    transfer->setState(t->state);
    transfer->setPriority(t->priority);
    transfer->setTotalBytes(t->size);
    transfer->setTransferredBytes(t->progresscompleted);
    transfer->setTag(f->tag);
    transferMap[f->tag] = transfer;

    if (t->type == GET)
    {
        totalDownloads++;
        pendingDownloads++;
        totalDownloadBytes += t->size;
        totalDownloadedBytes += t->progresscompleted;
    }
    else
    {
        totalUploads++;
        pendingUploads++;
        totalUploadBytes += t->size;
        totalUploadedBytes += t->progresscompleted;
    }

    fireOnTransferStart(transfer);
}

void MegaApiImpl::file_removed(File *f, const Error &e)
{
    MegaTransferPrivate* transfer = getMegaTransferPrivate(f->tag);
    if (transfer)
    {
        processTransferRemoved(f->transfer, transfer, e);
    }
}

void MegaApiImpl::file_complete(File *f)
{   
    MegaTransferPrivate* transfer = getMegaTransferPrivate(f->tag);
    if (transfer)
    {
        if (f->transfer->type == GET)
        {
            // The final name can change when downloads are complete
            // if there is another file in the same path

            string path = f->localname.toPath(*fsAccess);
            transfer->setPath(path.c_str());
        }

        processTransferComplete(f->transfer, transfer);
    }
}

void MegaApiImpl::transfer_complete(Transfer *t)
{
    MegaTransferPrivate* transfer = getMegaTransferPrivate(t->tag);
    if (transfer)
    {
        transfer->setTransfer(nullptr);
    }
}

void MegaApiImpl::transfer_removed(Transfer *t)
{
    MegaTransferPrivate* transfer = getMegaTransferPrivate(t->tag);
    if (transfer)
    {
        transfer->setTransfer(nullptr);
    }
}
void MegaApiImpl::transfer_prepare(Transfer *t)
{
    for (file_list::iterator it = t->files.begin(); it != t->files.end(); it++)
    {
        MegaTransferPrivate* transfer = getMegaTransferPrivate((*it)->tag);
        if (!transfer)
        {
            continue;
        }
        processTransferPrepare(t, transfer);
    }
}

void MegaApiImpl::transfer_update(Transfer *t)
{
    for (file_list::iterator it = t->files.begin(); it != t->files.end(); it++)
    {
        MegaTransferPrivate* transfer = getMegaTransferPrivate((*it)->tag);
        if (!transfer)
        {
            continue;
        }

        if (it == t->files.begin()
                && transfer->getUpdateTime() == Waiter::ds
                && transfer->getState() == t->state
                && transfer->getPriority() == t->priority
                && (!t->slot
                    || (t->slot->progressreported
                        && t->slot->progressreported != t->size)))
        {
            // don't send more than one callback per decisecond
            // if the state doesn't change, the priority doesn't change
            // and there isn't anything new or it's not the first
            // nor the last callback
            return;
        }

        processTransferUpdate(t, transfer);
    }
}

File *MegaApiImpl::file_resume(string *d, direction_t *type)
{
    if (!d || d->size() < sizeof(char))
    {
        return NULL;
    }

    MegaFile *file = NULL;
    *type = (direction_t)MemAccess::get<char>(d->data());
    switch (*type)
    {
    case GET:
    {
        file = MegaFileGet::unserialize(d);
        break;
    }
    case PUT:
    {
        file = MegaFilePut::unserialize(d);
        if (!file)
        {
            break;
        }
        MegaTransferPrivate* transfer = file->getTransfer();
        Node *parent = client->nodebyhandle(transfer->getParentHandle());
        node_vector *nodes = client->nodesbyfingerprint(file);
        const char *name = transfer->getFileName();
        if (parent && nodes && name)
        {
            for (unsigned int i = 0; i < nodes->size(); i++)
            {
                Node* node = nodes->at(i);
                if (node->parent == parent && !strcmp(node->displayname(), name))
                {
                    // don't resume the upload if the node already exist in the target folder
                    DBTableTransactionCommitter committer(client->tctable);
                    delete file;
                    delete transfer;   // committer needed here
                    file = NULL;
                    break;
                }
            }
        }
        delete nodes;
        break;
    }
    default:
        break;
    }

    if (file)
    {
        currentTransfer = file->getTransfer();
        waiter->notify();
    }
    return file;
}

dstime MegaApiImpl::pread_failure(const Error &e, int retry, void* param, dstime timeLeft)
{
    MegaTransferPrivate *transfer = (MegaTransferPrivate *)param;
    transfer->setUpdateTime(Waiter::ds);
    transfer->setDeltaSize(0);
    transfer->setSpeed(0);
    transfer->setMeanSpeed(0);
    transfer->setLastBytes(NULL);
    if (retry <= transfer->getMaxRetries() && e != API_EINCOMPLETE && !(e == API_ETOOMANY && e.hasExtraInfo()))
    {	
        auto megaError = make_unique<MegaErrorPrivate>(e, timeLeft / 10);
        transfer->setLastError(megaError.get());
        transfer->setState(MegaTransfer::STATE_RETRYING);
        fireOnTransferTemporaryError(transfer, std::move(megaError));
        LOG_debug << "Streaming temporarily failed " << retry;
        if (retry <= 1)
        {
            return 0;
        }

        return (dstime)(1 << (retry - 1));
    }
    else
    {
        if (e && (e != API_EINCOMPLETE || (e == API_ETOOMANY && e.hasExtraInfo())))
        {
            transfer->setState(MegaTransfer::STATE_FAILED);
        }
        else
        {
            transfer->setState(MegaTransfer::STATE_COMPLETED);
        }
        DBTableTransactionCommitter committer(client->tctable);
        fireOnTransferFinish(transfer, make_unique<MegaErrorPrivate>(e), committer);
        return NEVER;
    }
}

bool MegaApiImpl::pread_data(byte *buffer, m_off_t len, m_off_t, m_off_t speed, m_off_t meanSpeed, void* param)
{
    MegaTransferPrivate *transfer = (MegaTransferPrivate *)param;
    dstime currentTime = Waiter::ds;
    transfer->setStartTime(currentTime);
    transfer->setState(MegaTransfer::STATE_ACTIVE);
    transfer->setUpdateTime(currentTime);
    transfer->setDeltaSize(len);
    transfer->setLastBytes((char *)buffer);
    transfer->setTransferredBytes(transfer->getTransferredBytes() + len);
    transfer->setSpeed(speed);
    transfer->setMeanSpeed(meanSpeed);

    bool end = (transfer->getTransferredBytes() == transfer->getTotalBytes());
    fireOnTransferUpdate(transfer);
    if (!fireOnTransferData(transfer) || end)
    {
        transfer->setState(end ? MegaTransfer::STATE_COMPLETED : MegaTransfer::STATE_CANCELLED);
        DBTableTransactionCommitter committer(client->tctable);
        fireOnTransferFinish(transfer, make_unique<MegaErrorPrivate>(end ? API_OK : API_EINCOMPLETE), committer);
        return end;
    }
    return true;
}

void MegaApiImpl::reportevent_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_REPORT_EVENT)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::sessions_killed(handle, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_KILL_SESSION)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::cleanrubbishbin_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CLEAN_RUBBISH_BIN)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::getrecoverylink_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_GET_RECOVERY_LINK) &&
                    (request->getType() != MegaRequest::TYPE_GET_CANCEL_LINK))) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::queryrecoverylink_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_QUERY_RECOVERY_LINK) &&
                    (request->getType() != MegaRequest::TYPE_CONFIRM_RECOVERY_LINK) &&
                    (request->getType() != MegaRequest::TYPE_CONFIRM_CHANGE_EMAIL_LINK))) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::queryrecoverylink_result(int type, const char *email, const char *ip, time_t, handle uh, const vector<string> *)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    int reqType = request->getType();
    if(!request || ((reqType != MegaRequest::TYPE_QUERY_RECOVERY_LINK) &&
                    (reqType != MegaRequest::TYPE_CONFIRM_RECOVERY_LINK) &&
                    (reqType != MegaRequest::TYPE_CONFIRM_CHANGE_EMAIL_LINK))) return;

    request->setEmail(email);
    request->setFlag(type == RECOVER_WITH_MASTERKEY);
    request->setNumber(type);   // not specified in MegaApi documentation
    request->setText(ip);       // not specified in MegaApi documentation
    request->setNodeHandle(uh); // not specified in MegaApi documentation

    if (reqType == MegaRequest::TYPE_QUERY_RECOVERY_LINK)
    {
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>());
        return;
    }
    else if (reqType == MegaRequest::TYPE_CONFIRM_RECOVERY_LINK)
    {
        int creqtag = client->reqtag;
        client->reqtag = client->restag;
        client->prelogin(email);
        client->reqtag = creqtag;
        return;
    }
    else if (reqType == MegaRequest::TYPE_CONFIRM_CHANGE_EMAIL_LINK)
    {
        if (type != CHANGE_EMAIL)
        {
            LOG_debug << "Unknown type of change email link";

            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_EARGS));
            return;
        }
                
        const char* code;
        if ((code = strstr(request->getLink(), "#verify")))
        {
            code += strlen("#verify");

            if (!checkPassword(request->getPassword()))
            {
                fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_ENOENT));
                return;
            }

            int creqtag = client->reqtag;
            client->reqtag = client->restag;
            if (client->accountversion == 1)
            {
                byte pwkey[SymmCipher::KEYLENGTH];
                client->pw_key(request->getPassword(), pwkey);
                client->confirmemaillink(code, request->getEmail(), pwkey);
            }
            else if (client->accountversion == 2)
            {
                client->confirmemaillink(code, request->getEmail(), NULL);
            }
            else
            {
                LOG_warn << "Version of account not supported";
                fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_EINTERNAL));
            }
            client->reqtag = creqtag;
        }
        else
        {
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_EARGS));
        }
    }
}

void MegaApiImpl::getprivatekey_result(error e, const byte *privk, const size_t len_privk)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CONFIRM_RECOVERY_LINK)) return;

    if (e)
    {
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
        return;
    }

    const char *link = request->getLink();
    const char* code;
    if ((code = strstr(link, "#recover")))
    {
        code += strlen("#recover");
    }
    else
    {
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_EARGS));
        return;
    }

    byte mk[SymmCipher::KEYLENGTH];
    Base64::atob(request->getPrivateKey(), mk, sizeof mk);

    // check the private RSA is valid after decryption with master key
    SymmCipher key;
    key.setkey(mk);

    byte privkbuf[AsymmCipher::MAXKEYLENGTH * 2];
    memcpy(privkbuf, privk, len_privk);
    key.ecb_decrypt(privkbuf, len_privk);

    AsymmCipher uk;
    if (!uk.setkey(AsymmCipher::PRIVKEY, privkbuf, int(len_privk)))
    {
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_EKEY));
        return;
    }

    int creqtag = client->reqtag;
    client->reqtag = client->restag;
    client->confirmrecoverylink(code, request->getEmail(), request->getPassword(), mk, request->getParamType());
    client->reqtag = creqtag;
}

void MegaApiImpl::confirmrecoverylink_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CONFIRM_RECOVERY_LINK)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::confirmcancellink_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CONFIRM_CANCEL_LINK)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::getemaillink_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_GET_CHANGE_EMAIL_LINK)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::resendverificationemail_result(error e)
{
    auto it = requestMap.find(client->restag);
    if (it == requestMap.end()) return;
    MegaRequestPrivate *request = it->second;
    if (!request || ((request->getType() != MegaRequest::TYPE_RESEND_VERIFICATION_EMAIL))) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::resetSmsVerifiedPhoneNumber_result(error e)
{
    if (requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate *request = requestMap.at(client->restag);
    if (!request || (request->getType() != MegaRequest::TYPE_RESET_SMS_VERIFIED_NUMBER)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::confirmemaillink_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CONFIRM_CHANGE_EMAIL_LINK)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::getversion_result(int versionCode, const char *versionString, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_APP_VERSION)) return;

    if (!e)
    {
        request->setNumber(versionCode);
        request->setName(versionString);
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::getlocalsslcertificate_result(m_time_t ts, string *certdata, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_GET_LOCAL_SSL_CERT)) return;

    if (!e)
    {
        string result;
        const char *data = certdata->data();
        const char *enddata = certdata->data() + certdata->size();
        MegaStringMapPrivate *datamap = new MegaStringMapPrivate();
        for (int i = 0; data < enddata; i++)
        {
            result = i ? "-----BEGIN CERTIFICATE-----\n"
                       : "-----BEGIN RSA PRIVATE KEY-----\n";

            const char *end = strstr(data, ";");
            if (!end)
            {
                if (!i)
                {
                    delete datamap;
                    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_EINTERNAL));
                    return;
                }
                end = enddata;
            }

            while (data < end)
            {
                int remaining = int(end - data);
                int dataSize = (remaining > 64) ? 64 : remaining;
                result.append(data, dataSize);
                result.append("\n");
                data += dataSize;
            }

            switch (i)
            {
                case 0:
                {
                    result.append("-----END RSA PRIVATE KEY-----\n");
                    datamap->set("key", result.c_str());
                    break;
                }
                case 1:
                {
                    result.append("-----END CERTIFICATE-----\n");
                    datamap->set("cert", result.c_str());
                    break;
                }
                default:
                {
                    result.append("-----END CERTIFICATE-----\n");
                    std::ostringstream oss;
                    oss << "intermediate_" << (i - 1);
                    datamap->set(oss.str().c_str(), result.c_str());
                    break;
                }
            }
            data++;
        }

        request->setNumber(ts);
        request->setMegaStringMap(datamap);
        delete datamap;
    }
    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::getmegaachievements_result(AchievementsDetails *details, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_GET_ACHIEVEMENTS)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::getwelcomepdf_result(handle ph, string *key, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CREATE_ACCOUNT)) return;

    if (e == API_OK)
    {
        int creqtag = client->reqtag;
        client->reqtag = client->restag;
        client->reqs.add(new CommandGetPH(client, ph, (const byte*) key->data(), 1));
        client->reqtag = creqtag;
    }
    else
    {
        return fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));    // if import fails, notify account was successfuly created anyway
    }
}

void MegaApiImpl::backgrounduploadurl_result(error e, string* url)
{
    if (requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if (!request || (request->getType() != MegaRequest::TYPE_GET_BACKGROUND_UPLOAD_URL)) return;

    if (e  == API_OK && url)
    {
        MegaBackgroundMediaUploadPrivate* mu = static_cast<MegaBackgroundMediaUploadPrivate*>(request->getMegaBackgroundMediaUploadPtr());
        mu->url = *url;
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::mediadetection_ready()
{
    MegaEventPrivate *event = new MegaEventPrivate(MegaEvent::EVENT_MEDIA_INFO_READY);
    fireOnEvent(event);
}

void MegaApiImpl::storagesum_changed(int64_t newsum)
{
    MegaEventPrivate *event = new MegaEventPrivate(MegaEvent::EVENT_STORAGE_SUM_CHANGED);
    event->setNumber(newsum);
    fireOnEvent(event);
}

void MegaApiImpl::getmiscflags_result(error e)
{
    if (e == API_OK)
    {
        MegaEventPrivate *event = new MegaEventPrivate(MegaEvent::EVENT_MISC_FLAGS_READY);
        fireOnEvent(event);
    }

    if (requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if (!request || (request->getType() != MegaRequest::TYPE_GET_MISC_FLAGS)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

#ifdef ENABLE_CHAT

void MegaApiImpl::chatcreate_result(TextChat *chat, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CHAT_CREATE)) return;

    if (!e)
    {
        // encapsulate the chat in a list for the request
        textchat_map chatList;
        chatList[chat->id] = chat;

        auto megaChatList = mega::make_unique<MegaTextChatListPrivate>(&chatList);
        request->setMegaTextChatList(megaChatList.get());
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::chatinvite_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CHAT_INVITE)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::chatremove_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CHAT_REMOVE)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::chaturl_result(string *url, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CHAT_URL)) return;

    if (!e)
    {
        request->setLink(url->c_str());
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::chatgrantaccess_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CHAT_GRANT_ACCESS)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::chatremoveaccess_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CHAT_REMOVE_ACCESS)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::chatupdatepermissions_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CHAT_UPDATE_PERMISSIONS)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::chattruncate_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CHAT_TRUNCATE)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::chatsettitle_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CHAT_SET_TITLE)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::chatpresenceurl_result(string *url, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CHAT_PRESENCE_URL)) return;

    if (!e)
    {
        request->setLink(url->c_str());
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::registerpushnotification_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_REGISTER_PUSH_NOTIFICATION)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::archivechat_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CHAT_ARCHIVE)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::setchatretentiontime_result(error e)
{
    if (requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate *request = requestMap.at(client->restag);
    if (!request || (request->getType() != MegaRequest::TYPE_SET_RETENTION_TIME)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::chats_updated(textchat_map *chats, int count)
{
    if (chats)
    {
        MegaTextChatList *chatList = new MegaTextChatListPrivate(chats);
        fireOnChatsUpdate(chatList);
        delete chatList;
    }
    else
    {
        fireOnChatsUpdate(NULL);
    }
}

void MegaApiImpl::richlinkrequest_result(string *richLink, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_RICH_LINK)) return;

    if (!e)
    {
        request->setText(richLink->c_str());
    }
    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::chatlink_result(handle h, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CHAT_LINK_HANDLE)) return;
    
    if (!e && !request->getFlag())
    {
        request->setParentHandle(h);
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::chatlinkurl_result(handle chatid, int shard, string *link, string *ct, int numPeers, m_time_t ts, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CHAT_LINK_URL)) return;

    if (!e)
    {
        request->setLink(link->c_str());
        request->setAccess(shard);
        request->setParentHandle(chatid);
        request->setText(ct->c_str());
        request->setNumDetails(numPeers);
        request->setNumber(ts);
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::chatlinkclose_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_SET_PRIVATE_MODE)) return;
    
    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::chatlinkjoin_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_AUTOJOIN_PUBLIC_CHAT)) return;
    
    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

#endif

void MegaApiImpl::folderlinkinfo_result(error e, handle owner, handle /*ph*/, string *attr, string* k, m_off_t currentSize, uint32_t numFiles, uint32_t numFolders, m_off_t versionsSize, uint32_t numVersions)
{
    MegaRequestPrivate* request = NULL;
    auto it = requestMap.find(client->restag);
    if (it == requestMap.end() || !(request = it->second)
            || request->getType() != MegaRequest::TYPE_PUBLIC_LINK_INFORMATION) return;

    if (e == API_OK)
    {
        // Decrypt nodekey with the key of the folder link
        SymmCipher cipher;
        byte folderkey[SymmCipher::KEYLENGTH];
        Base64::atob(request->getPrivateKey(), folderkey, sizeof(folderkey));
        cipher.setkey(folderkey);
        const char *nodekeystr = k->data() + 9;    // skip the userhandle(8) and the `:`
        byte nodekey[FOLDERNODEKEYLENGTH];
        if (client->decryptkey(nodekeystr, nodekey, sizeof(nodekey), &cipher, 0, UNDEF))
        {
            // Decrypt node attributes with the nodekey
            cipher.setkey(nodekey);
            byte* buf = Node::decryptattr(&cipher, attr->c_str(), attr->size());
            if (buf)
            {
                AttrMap attrs;
                string fileName;
                string fingerprint;
                FileFingerprint ffp;
                m_time_t mtime = 0;
                Node::parseattr(buf, attrs, currentSize, mtime, fileName, fingerprint, ffp);

                // Normalize node name to UTF-8 string
                attr_map::iterator it = attrs.map.find('n');
                if (it != attrs.map.end() && !it->second.empty())
                {
                    client->fsaccess->normalize(&(it->second));
                    fileName = it->second.c_str();
                }

                MegaFolderInfoPrivate *folderInfo = new MegaFolderInfoPrivate(numFiles, numFolders - 1, numVersions, currentSize, versionsSize);
                request->setMegaFolderInfo(folderInfo);
                request->setParentHandle(owner);
                request->setText(fileName.c_str());

                delete folderInfo;
                delete [] buf;
            }
            else
            {
                LOG_err << "Error decrypting node attributes with decrypted nodekey";
                e = API_EKEY;
            }
        }
        else
        {
            LOG_err << "Error decrypting nodekey with folder link key";
            e = API_EKEY;
        }
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

#ifdef ENABLE_SYNC
void MegaApiImpl::syncupdate_state(Sync *sync, syncstate_t newstate)
{
    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);
    megaSync->setState(newstate);
    LOG_debug << "Sync state change: " << newstate << " Path: " << sync->localroot->name;
    client->abortbackoff(false);

    if (newstate == SYNC_FAILED)
    {
        MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_ADD_SYNC);

        if(sync->localroot->node)
        {
            request->setNodeHandle(sync->localroot->node->nodehandle);
        }

        int nextTag = client->nextreqtag();
        request->setTag(nextTag);
        requestMap[nextTag]=request;
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(sync->errorcode));
    }

    fireOnSyncStateChanged(megaSync);
}

void MegaApiImpl::syncupdate_scanning(bool scanning)
{
    if(client)
    {
        client->abortbackoff(false);
        client->syncscanstate = scanning;
    }
    fireOnGlobalSyncStateChanged();
}

void MegaApiImpl::syncupdate_local_folder_addition(Sync *sync, LocalNode *, const char* path)
{
    LOG_debug << "Sync - local folder addition detected: " << path;
    client->abortbackoff(false);

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_LOCAL_FOLDER_ADITION);
    event->setPath(path);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_local_folder_deletion(Sync *sync, LocalNode *localNode)
{
    client->abortbackoff(false);

    string path = localNode->getLocalPath(true).toPath(*fsAccess);
    LOG_debug << "Sync - local folder deletion detected: " << path.c_str();

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);


    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_LOCAL_FOLDER_DELETION);
    event->setPath(path.c_str());
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_local_file_addition(Sync *sync, LocalNode *, const char* path)
{
    LOG_debug << "Sync - local file addition detected: " << path;
    client->abortbackoff(false);

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_LOCAL_FILE_ADDITION);
    event->setPath(path);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_local_file_deletion(Sync *sync, LocalNode *localNode)
{
    client->abortbackoff(false);

    string path = localNode->getLocalPath(true).toPath(*fsAccess);
    LOG_debug << "Sync - local file deletion detected: " << path.c_str();

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_LOCAL_FILE_DELETION);
    event->setPath(path.c_str());
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_local_file_change(Sync *sync, LocalNode *, const char* path)
{
    LOG_debug << "Sync - local file change detected: " << path;
    client->abortbackoff(false);

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_LOCAL_FILE_CHANGED);
    event->setPath(path);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_local_move(Sync *sync, LocalNode *localNode, const char *to)
{
    client->abortbackoff(false);

    string path = localNode->getLocalPath(true).toPath(*fsAccess);
    LOG_debug << "Sync - local rename/move " << path.c_str() << " -> " << to;

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_LOCAL_MOVE);
    event->setPath(path.c_str());
    event->setNewPath(to);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_get(Sync *sync, Node* node, const char *path)
{
    LOG_debug << "Sync - requesting file " << path;

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_FILE_GET);
    event->setNodeHandle(node->nodehandle);
    event->setPath(path);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_put(Sync *sync, LocalNode *, const char *path)
{
    LOG_debug << "Sync - sending file " << path;

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_FILE_PUT);
    event->setPath(path);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_remote_file_addition(Sync *sync, Node *n)
{
    LOG_debug << "Sync - remote file addition detected " << n->displayname() << " Nhandle: " << LOG_NODEHANDLE(n->nodehandle);
    client->abortbackoff(false);

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_REMOTE_FILE_ADDITION);
    event->setNodeHandle(n->nodehandle);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_remote_file_deletion(Sync *sync, Node *n)
{
    LOG_debug << "Sync - remote file deletion detected " << n->displayname() << " Nhandle: " << LOG_NODEHANDLE(n->nodehandle);
    client->abortbackoff(false);

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_REMOTE_FILE_DELETION);
    event->setNodeHandle(n->nodehandle);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_remote_folder_addition(Sync *sync, Node *n)
{
    LOG_debug << "Sync - remote folder addition detected " << n->displayname();
    client->abortbackoff(false);

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_REMOTE_FOLDER_ADDITION);
    event->setNodeHandle(n->nodehandle);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_remote_folder_deletion(Sync *sync, Node *n)
{
    LOG_debug << "Sync - remote folder deletion detected " << n->displayname();
    client->abortbackoff(false);

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_REMOTE_FOLDER_DELETION);
    event->setNodeHandle(n->nodehandle);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_remote_copy(Sync *, const char *name)
{
    LOG_debug << "Sync - creating remote file " << name << " by copying existing remote file";
    client->abortbackoff(false);
}

void MegaApiImpl::syncupdate_remote_move(Sync *sync, Node *n, Node *prevparent)
{
    LOG_debug << "Sync - remote move " << n->displayname() <<
                 " from " << (prevparent ? prevparent->displayname() : "?") <<
                 " to " << (n->parent ? n->parent->displayname() : "?");
    client->abortbackoff(false);

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_REMOTE_MOVE);
    event->setNodeHandle(n->nodehandle);
    event->setPrevParent(prevparent ? prevparent->nodehandle : UNDEF);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_remote_rename(Sync *sync, Node *n, const char *prevname)
{
    LOG_debug << "Sync - remote rename from " << prevname << " to " << n->displayname();
    client->abortbackoff(false);

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_REMOTE_RENAME);
    event->setNodeHandle(n->nodehandle);
    event->setPrevName(prevname);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_treestate(LocalNode *l)
{
    LocalPath localpath = l->getLocalPath(true);

    if(syncMap.find(l->sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(l->sync->tag);

    fireOnFileSyncStateChanged(megaSync, localpath.editStringDirect(), (int)l->ts);
}

bool MegaApiImpl::sync_syncable(Sync *sync, const char *name, LocalPath& localpath, Node *node)
{
    if (!sync || !sync->appData || (node->type == FILENODE && !is_syncable(node->size)))
    {
        return false;
    }

    sdkMutex.unlock();
    bool result = is_syncable(sync, name, localpath);
    sdkMutex.lock();
    return result;
}

bool MegaApiImpl::sync_syncable(Sync *sync, const char *name, LocalPath& localpath)
{
    {
        std::lock_guard<std::mutex> g(mSyncable_fa_mutex);
        if (!mSyncable_fa)
        {
            mSyncable_fa = fsAccess->newfileaccess();
        }
        if (!sync || !sync->appData
            || ((syncLowerSizeLimit || syncUpperSizeLimit)
                && mSyncable_fa->fopen(localpath) && !is_syncable(mSyncable_fa->size)))
        {
            return false;
        }
    }

    sdkMutex.unlock();
    bool result = is_syncable(sync, name, localpath);
    sdkMutex.lock();
    return result;
}

void MegaApiImpl::sync_auto_resumed(const string& localPath, const handle remoteNode, const long long localFp, const std::vector<std::string>& regExp)
{
    const int nextTag = client->nextreqtag();

    MegaSyncPrivate *sync = new MegaSyncPrivate(localPath.c_str(), remoteNode, -nextTag);
    sync->setLocalFingerprint(localFp);

    if (!regExp.empty())
    {
        auto re = make_unique<MegaRegExp>();
        for (const auto& v : regExp)
        {
            re->addRegExp(v.c_str());
        }
        sync->setRegExp(re.get());
    }

    Sync *s = client->syncs.back();
    s->appData = sync;
    sync->setState(s->state);
    syncMap[-nextTag] = sync;
}

void MegaApiImpl::syncupdate_local_lockretry(bool waiting)
{
    if (waiting)
    {
        LOG_debug << "Sync - waiting for local filesystem lock";
    }
    else
    {
        LOG_debug << "Sync - local filesystem lock issue resolved, continuing...";
        client->abortbackoff(false);
    }

    this->fireOnGlobalSyncStateChanged();
}
#endif

void MegaApiImpl::backupput_result(const Error&, handle /*backupId*/)
{

}

void MegaApiImpl::backupupdate_result(const Error&, handle /*backupId*/)
{

}

void MegaApiImpl::backupputheartbeat_result(const Error&)
{

}

void MegaApiImpl::backupremove_result(const Error&, handle /*backupId*/)
{

}

// user addition/update (users never get deleted)
void MegaApiImpl::users_updated(User** u, int count)
{
    if(!count)
    {
        return;
    }

    MegaUserList *userList = NULL;
    if(u != NULL)
    {
        userList = new MegaUserListPrivate(u, count);
        fireOnUsersUpdate(userList);
    }
    else
    {
        fireOnUsersUpdate(NULL);
    }
    delete userList;
}

void MegaApiImpl::useralerts_updated(UserAlert::Base** b, int count)
{
    if (count)
    {
        MegaUserAlertList *userAlertList = b ? new MegaUserAlertListPrivate(b, count, client) : NULL;
        fireOnUserAlertsUpdate(userAlertList);
        delete userAlertList;
    }
}

void MegaApiImpl::account_updated()
{
    fireOnAccountUpdate();
}

void MegaApiImpl::pcrs_updated(PendingContactRequest **r, int count)
{
    if(!count)
    {
        return;
    }

    MegaContactRequestList *requestList = NULL;
    if(r != NULL)
    {
        requestList = new MegaContactRequestListPrivate(r, count);
        fireOnContactRequestsUpdate(requestList);
    }
    else
    {
        fireOnContactRequestsUpdate(NULL);
    }
    delete requestList;
}

void MegaApiImpl::setattr_result(handle h, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if (!request || ((request->getType() != MegaRequest::TYPE_RENAME)
            && request->getType() != MegaRequest::TYPE_SET_ATTR_NODE))
    {
        return;
    }

    request->setNodeHandle(h);
    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::rename_result(handle h, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_MOVE)) return;

#ifdef ENABLE_SYNC
    client->syncdownrequired = true;
#endif

    request->setNodeHandle(h);
    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::unlink_result(handle h, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_REMOVE) &&
                    (request->getType() != MegaRequest::TYPE_MOVE)))
    {
        return;
    }

#ifdef ENABLE_SYNC
    client->syncdownrequired = true;
#endif

    if (request->getType() != MegaRequest::TYPE_MOVE)
    {
        request->setNodeHandle(h);
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::unlinkversions_result(error e)
{
    if (requestMap.find(client->restag) == requestMap.end())
    {
        return;
    }

    MegaRequestPrivate* request = requestMap.at(client->restag);
    if (!request || request->getType() != MegaRequest::TYPE_REMOVE_VERSIONS)
    {
        return;
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::fetchnodes_result(const Error &e)
{    
    MegaRequestPrivate* request = NULL;
    if (!client->restag)
    {
        for (map<int, MegaRequestPrivate *>::iterator it = requestMap.begin(); it != requestMap.end(); it++)
        {
            if (it->second->getType() == MegaRequest::TYPE_FETCH_NODES)
            {
                request = it->second;
                break;
            }
        }
        if (!request)
        {
            request = new MegaRequestPrivate(MegaRequest::TYPE_FETCH_NODES);
        }

        if (e == API_OK)
        {
            // check if we fetched a folder link and the key is invalid
            handle h = client->getrootpublicfolder();
            if (h != UNDEF)
            {
                request->setNodeHandle(client->getpublicfolderhandle());
                Node *n = client->nodebyhandle(h);
                if (n && (n->attrs.map.find('n') == n->attrs.map.end()))
                {
                    request->setFlag(true);
                }
            }
        }

        if (!e && client->loggedin() == FULLACCOUNT && client->isNewSession)
        {
            updatePwdReminderData(false, false, false, false, true);
            client->isNewSession = false;
        }

        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
        return;
    }

    if (requestMap.find(client->restag) == requestMap.end())
    {
        return;
    }
    request = requestMap.at(client->restag);
    if (!request || ((request->getType() != MegaRequest::TYPE_FETCH_NODES) &&
                    (request->getType() != MegaRequest::TYPE_CREATE_ACCOUNT)))
    {
        return;
    }

    if (request->getType() == MegaRequest::TYPE_FETCH_NODES)
    {
        if (e == API_OK)
        {
            // check if we fetched a folder link and the key is invalid
            handle h = client->getrootpublicfolder();
            if (h != UNDEF)
            {
                request->setNodeHandle(client->getpublicfolderhandle());
                Node *n = client->nodebyhandle(h);
                if (n && (n->attrs.map.find('n') == n->attrs.map.end()))
                {
                    request->setFlag(true);
                }
            }
        }

        if (!e && client->loggedin() == FULLACCOUNT && client->isNewSession)
        {
            updatePwdReminderData(false, false, false, false, true);
            client->isNewSession = false;
        }

        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
    }
    else    // TYPE_CREATE_ACCOUNT
    {
        if (e != API_OK || request->getParamType() == 1)   // resuming ephemeral session
        {
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
            return;
        }
        else    // new account has been created
        {
            // set names silently...
            int creqtag = client->reqtag;
            client->reqtag = 0;
            string firstname = request->getName() ? request->getName() : "";
            if (!firstname.empty())
            {
                client->putua(ATTR_FIRSTNAME, (const byte*) request->getName(), int(strlen(request->getName())), -1,
                              request->getNodeHandle(), request->getAccess(), request->getTransferredBytes());
            }
            string lastname = request->getText() ? request->getText() : "";
            if (!lastname.empty())
            {
                client->putua(ATTR_LASTNAME, (const byte*) request->getText(), int(strlen(request->getText())));
            }

            // ...and finally send confirmation link
            client->reqtag = client->restag;
            byte pwkey[SymmCipher::KEYLENGTH];
            if (!request->getPrivateKey())
            {
                if (client->nsr_enabled)
                {
                    string fullname = firstname + lastname;
                    string derivedKey = client->sendsignuplink2(request->getEmail(), request->getPassword(), fullname.c_str());
                    string b64derivedKey;
                    Base64::btoa(derivedKey, b64derivedKey);
                    request->setPrivateKey(b64derivedKey.c_str());

                    char buf[SymmCipher::KEYLENGTH * 4 / 3 + 3];
                    Base64::btoa((byte*) &client->me, sizeof client->me, buf);
                    string sid;
                    sid.append(buf);
                    sid.append("#");
                    Base64::btoa((byte *)derivedKey.data(), SymmCipher::KEYLENGTH, buf);
                    sid.append(buf);
                    request->setSessionKey(sid.c_str());
                }
                else
                {
                    client->pw_key(request->getPassword(), pwkey);
                    client->sendsignuplink(request->getEmail(), request->getName(), pwkey);

                    char* buf = new char[SymmCipher::KEYLENGTH * 4 / 3 + 4];
                    Base64::btoa((byte *)pwkey, SymmCipher::KEYLENGTH, buf);
                    request->setPrivateKey(buf);
                    delete [] buf;
                }
            }
            else
            {
                Base64::atob(request->getPrivateKey(), (byte *)pwkey, sizeof pwkey);
                client->sendsignuplink(request->getEmail(), request->getName(), pwkey);
            }
            client->reqtag = creqtag;
        }
    }
}

void MegaApiImpl::putnodes_result(error e, targettype_t t, NewNode* nn)
{
    handle h = UNDEF;
    Node *n = NULL;

    if (!e && t != USER_HANDLE)
    {
        if (client->nodenotify.size())
        {
            n = client->nodenotify.back();
        }

        if(n)
        {
            n->applykey();
            n->setattr();
            h = n->nodehandle;
        }
    }

    MegaTransferPrivate* transfer = getMegaTransferPrivate(client->restag);
    if (transfer)
    {
        if (transfer->getType() == MegaTransfer::TYPE_DOWNLOAD)
        {
            return;
        }

        if(pendingUploads > 0)
        {
            pendingUploads--;
        }

        //scale to get the handle of the new node
        Node *ntmp;
        if (n)
        {
            handle ph = transfer->getParentHandle();
            for (ntmp = n; ((ntmp->parent != NULL) && (ntmp->parent->nodehandle != ph) ); ntmp = ntmp->parent);
            if ((ntmp->parent != NULL) && (ntmp->parent->nodehandle == ph))
            {
                h = ntmp->nodehandle;
            }
        }

        transfer->setNodeHandle(h);
        transfer->setTransferredBytes(transfer->getTotalBytes());

        if (!e)
        {
            transfer->setState(MegaTransfer::STATE_COMPLETED);
        }
        else
        {
            transfer->setState(MegaTransfer::STATE_FAILED);
        }

        DBTableTransactionCommitter committer(client->tctable);
        fireOnTransferFinish(transfer, make_unique<MegaErrorPrivate>(e), committer);
        delete [] nn;
        return;
    }

    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_IMPORT_LINK) &&
                    (request->getType() != MegaRequest::TYPE_CREATE_FOLDER) &&
                    (request->getType() != MegaRequest::TYPE_COPY) &&
                    (request->getType() != MegaRequest::TYPE_MOVE) &&
                    (request->getType() != MegaRequest::TYPE_CREATE_ACCOUNT) &&
                    (request->getType() != MegaRequest::TYPE_RESTORE) &&
                    (request->getType() != MegaRequest::TYPE_COMPLETE_BACKGROUND_UPLOAD))) return;

#ifdef ENABLE_SYNC
    client->syncdownrequired = true;
#endif

    delete [] nn;

    if (request->getType() == MegaRequest::TYPE_COMPLETE_BACKGROUND_UPLOAD)
    {
        request->setNodeHandle(h);
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
        return;
    }

    if (request->getType() == MegaRequest::TYPE_MOVE || request->getType() == MegaRequest::TYPE_COPY)
    {
        //scale to get the handle of the moved/copied node
        Node *ntmp;
        if (n)
        {
            for (ntmp = n; ((ntmp->parent != NULL) && (ntmp->parent->nodehandle != request->getParentHandle()) ); ntmp = ntmp->parent);
            if ((ntmp->parent != NULL) && (ntmp->parent->nodehandle == request->getParentHandle()))
            {
                h = ntmp->nodehandle;
            }
        }
    }

    if (request->getType() != MegaRequest::TYPE_MOVE)
    {
        request->setNodeHandle(h);    
        if (request->getType() == MegaRequest::TYPE_CREATE_ACCOUNT)
        {
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));    // even if import fails, notify account was successfuly created anyway
            return;
        }
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
    }
    else
    {
        if (!e)
        {
            Node * node = client->nodebyhandle(request->getNodeHandle());
            if (!node)
            {
                e = API_ENOENT;
            }
            else
            {
                request->setNodeHandle(h);
                int creqtag = client->reqtag;
                client->reqtag = request->getTag();
                e = client->unlink(node);
                client->reqtag = creqtag;
            }
        }

        if (e)
        {
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
        }
    }
}

void MegaApiImpl::share_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_EXPORT) &&
                    (request->getType() != MegaRequest::TYPE_SHARE))) return;

    // exportnode_result will be called to end the request.
    if (!e && request->getType() == MegaRequest::TYPE_EXPORT)
    {
        Node* node = client->nodebyhandle(request->getNodeHandle());
        if (!node)
        {
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_ENOENT));
            return;
        }

        if (!request->getAccess())
        {
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_EINTERNAL));
            return;
        }

        int creqtag = client->reqtag;
        client->reqtag = client->restag;
        client->getpubliclink(node, false, request->getNumber());
        client->reqtag = creqtag;

        return;
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::share_result(int, error)
{
    //The other callback will be called at the end of the request
}

void MegaApiImpl::setpcr_result(handle h, error e, opcactions_t action)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || request->getType() != MegaRequest::TYPE_INVITE_CONTACT) return;

    if (e)
    {
        LOG_debug << "Outgoing pending contact request failed (" << MegaError::getErrorString(e) << ")";
    }
    else
    {
        switch (action)
        {
            case OPCA_DELETE:
                LOG_debug << "Outgoing pending contact request deleted successfully";
                break;
            case OPCA_REMIND:
                LOG_debug << "Outgoing pending contact request reminded successfully";
                break;
            case OPCA_ADD:
                char buffer[12];
                Base64::btoa((byte*)&h, MegaClient::PCRHANDLE, buffer);
                LOG_debug << "Outgoing pending contact request succeeded, id: " << buffer;
                break;
        }
    }

    request->setNodeHandle(h);
    request->setNumber(action);
    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::updatepcr_result(error e, ipcactions_t action)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || request->getType() != MegaRequest::TYPE_REPLY_CONTACT_REQUEST) return;

    if (e)
    {
        LOG_debug << "Incoming pending contact request update failed (" << MegaError::getErrorString(e) << ")";
    }
    else
    {
        string labels[3] = {"accepted", "denied", "ignored"};
        LOG_debug << "Incoming pending contact request successfully " << labels[(int)action];
    }

    request->setNumber(action);
    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::fa_complete(handle, fatype, const char* data, uint32_t len)
{
    int tag = client->restag;
    while(tag)
    {
        if(requestMap.find(tag) == requestMap.end()) return;
        MegaRequestPrivate* request = requestMap.at(tag);
        if(!request || (request->getType() != MegaRequest::TYPE_GET_ATTR_FILE)) return;

        tag = int(request->getNumber());

        auto f = client->fsaccess->newfileaccess();
        string filePath(request->getFile());
        auto localPath = LocalPath::fromPath(filePath, *fsAccess);
        fsAccess->unlinklocal(localPath);

        bool success = f->fopen(localPath, false, true)
                    && f->fwrite((const byte*)data, len, 0);

        f.reset();

        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(success ? API_OK : API_EWRITE));
    }
}

int MegaApiImpl::fa_failed(handle, fatype, int retries, error e)
{
    int tag = client->restag;
    while(tag)
    {
        if(requestMap.find(tag) == requestMap.end()) return 1;
        MegaRequestPrivate* request = requestMap.at(tag);
        if(!request || (request->getType() != MegaRequest::TYPE_GET_ATTR_FILE))
            return 1;

        tag = int(request->getNumber());
        if(retries >= 2)
        {
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
        }
        else
        {
            fireOnRequestTemporaryError(request, make_unique<MegaErrorPrivate>(e));
        }
    }

    return (retries >= 2);
}

void MegaApiImpl::putfa_result(handle, fatype, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || request->getType() != MegaRequest::TYPE_SET_ATTR_FILE)
        return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::putfa_result(handle h, fatype, const char *)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || request->getType() != MegaRequest::TYPE_SET_ATTR_FILE)
        return;

    if (request->getMegaBackgroundMediaUploadPtr())
    {
        request->setNodeHandle(h);
    }
    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
}

void MegaApiImpl::enumeratequotaitems_result(unsigned type, handle product, unsigned prolevel, int gbstorage, int gbtransfer, unsigned months, unsigned amount, unsigned amountMonth, const char* currency, const char* description, const char* iosid, const char* androidid)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_GET_PRICING) &&
                    (request->getType() != MegaRequest::TYPE_GET_PAYMENT_ID) &&
                    (request->getType() != MegaRequest::TYPE_UPGRADE_ACCOUNT)))
    {
        return;
    }

    request->addProduct(type, product, prolevel, gbstorage, gbtransfer, months, amount, amountMonth, currency, description, iosid, androidid);
}

void MegaApiImpl::enumeratequotaitems_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_GET_PRICING) &&
                    (request->getType() != MegaRequest::TYPE_GET_PAYMENT_ID) &&
                    (request->getType() != MegaRequest::TYPE_UPGRADE_ACCOUNT)))
    {
        return;
    }

    if(request->getType() == MegaRequest::TYPE_GET_PRICING)
    {
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
    }
    else
    {
        MegaPricing *pricing = request->getPricing();
        int i;
        for(i = 0; i < pricing->getNumProducts(); i++)
        {
            if (pricing->getHandle(i) == request->getNodeHandle())
            {
                int phtype = request->getParamType();
                int64_t ts = request->getTransferredBytes();
                requestMap.erase(request->getTag());
                int nextTag = client->nextreqtag();
                request->setTag(nextTag);
                requestMap[nextTag]=request;
                client->purchase_additem(0, request->getNodeHandle(), pricing->getAmount(i),
                                         pricing->getCurrency(i), 0, NULL, request->getParentHandle(),
                                         phtype, ts);
                break;
            }
        }

        if (i == pricing->getNumProducts())
        {
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_ENOENT));
        }
        delete pricing;
    }
}

void MegaApiImpl::additem_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_GET_PAYMENT_ID) &&
                    (request->getType() != MegaRequest::TYPE_UPGRADE_ACCOUNT))) return;

    if(e != API_OK)
    {
        client->purchase_begin();
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
        return;
    }

    if(request->getType() == MegaRequest::TYPE_GET_PAYMENT_ID)
    {
        char saleid[16];
        Base64::btoa((byte *)&client->purchase_basket.back(), 8, saleid);
        request->setLink(saleid);
        client->purchase_begin();
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
        return;
    }

    //MegaRequest::TYPE_UPGRADE_ACCOUNT
    int method = int(request->getNumber());

    int creqtag = client->reqtag;
    client->reqtag = client->restag;
    client->purchase_checkout(method);
    client->reqtag = creqtag;
}

void MegaApiImpl::checkout_result(const char *errortype, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_UPGRADE_ACCOUNT)) return;

    if(!errortype)
    {
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
        return;
    }

    if(!strcmp(errortype, "FP"))
    {
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e - 100));
        return;
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(MegaError::PAYMENT_EGENERIC));
    return;
}

void MegaApiImpl::submitpurchasereceipt_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_SUBMIT_PURCHASE_RECEIPT)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::creditcardquerysubscriptions_result(int number, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CREDIT_CARD_QUERY_SUBSCRIPTIONS)) return;

    request->setNumber(number);
    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::creditcardcancelsubscriptions_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CREDIT_CARD_CANCEL_SUBSCRIPTIONS)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}
void MegaApiImpl::getpaymentmethods_result(int methods, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_GET_PAYMENT_METHODS)) return;

    request->setNumber(methods);
    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::userfeedbackstore_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_SUBMIT_FEEDBACK)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::sendevent_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_SEND_EVENT)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::supportticket_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_SUPPORT_TICKET)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::creditcardstore_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CREDIT_CARD_STORE)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::copysession_result(string *session, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_GET_SESSION_TRANSFER_URL)) return;

    const char *path = request->getText();
    string *data = NULL;
    if(e == API_OK)
    {
        data = client->sessiontransferdata(path, session);
    }

    if(data)
    {
        data->insert(0, "https://mega.nz/#sitetransfer!");
    }
    else
    {
        data = new string("https://mega.nz/#");
        if(path)
        {
            data->append(path);
        }
    }

    request->setLink(data->c_str());
    delete data;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::clearing()
{
#ifdef ENABLE_SYNC
    map<int, MegaSyncPrivate *>::iterator it;
    for (it = syncMap.begin(); it != syncMap.end(); )
    {
        MegaSyncPrivate *sync = (MegaSyncPrivate *)it->second;
        syncMap.erase(it++);
        delete sync;
    }
#endif
}

void MegaApiImpl::notify_retry(dstime dsdelta, retryreason_t reason)
{
#ifdef ENABLE_SYNC
    retryreason_t previousFlag = waitingRequest;
#endif

    if(!dsdelta)
        waitingRequest = RETRY_NONE;
    else if(dsdelta > 40)
        waitingRequest = reason;

#ifdef ENABLE_SYNC
    if(previousFlag != waitingRequest)
        fireOnGlobalSyncStateChanged();
#endif

    if (dsdelta && requestMap.size() == 1)
    {
        MegaRequestPrivate *request = requestMap.begin()->second;
        fireOnRequestTemporaryError(request, make_unique<MegaErrorPrivate>(API_EAGAIN, reason));
    }
}

void MegaApiImpl::notify_dbcommit()
{
    MegaEventPrivate *event = new MegaEventPrivate(MegaEvent::EVENT_COMMIT_DB);
    event->setText(client->scsn.text());
    fireOnEvent(event);
}

void MegaApiImpl::notify_storage(int storageEvent)
{
    MegaEventPrivate *event = new MegaEventPrivate(MegaEvent::EVENT_STORAGE);
    event->setNumber(storageEvent);
    fireOnEvent(event);
}

void MegaApiImpl::notify_change_to_https()
{
    MegaEventPrivate *event = new MegaEventPrivate(MegaEvent::EVENT_CHANGE_TO_HTTPS);
    fireOnEvent(event);
}

void MegaApiImpl::notify_confirmation(const char *email)
{
    MegaEventPrivate *event = new MegaEventPrivate(MegaEvent::EVENT_ACCOUNT_CONFIRMATION);
    event->setText(email);
    fireOnEvent(event);
}

void MegaApiImpl::notify_disconnect()
{
    MegaEventPrivate *event = new MegaEventPrivate(MegaEvent::EVENT_DISCONNECT);
    fireOnEvent(event);
}

void MegaApiImpl::notify_business_status(BizStatus status)
{
    MegaEventPrivate *event = new MegaEventPrivate(MegaEvent::EVENT_BUSINESS_STATUS);
    event->setNumber(status);
    fireOnEvent(event);
}

void MegaApiImpl::http_result(error e, int httpCode, byte *data, int size)
{
    if (requestMap.find(client->restag) == requestMap.end())
    {
        return;
    }

    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_QUERY_DNS
                    && request->getType() != MegaRequest::TYPE_QUERY_GELB
                    && request->getType() != MegaRequest::TYPE_CHAT_STATS
                    && request->getType() != MegaRequest::TYPE_DOWNLOAD_FILE))
    {
        return;
    }

    request->setNumber(httpCode);
    request->setTotalBytes(size);
    if (request->getType() == MegaRequest::TYPE_QUERY_GELB
            || request->getType() == MegaRequest::TYPE_CHAT_STATS
            || request->getType() == MegaRequest::TYPE_QUERY_DNS)
    {
        string result;
        result.assign((const char *)data, size);
        request->setText(result.c_str());
    }
    else if (request->getType() == MegaRequest::TYPE_DOWNLOAD_FILE)
    {
        const char *file = request->getFile();
        if (file && e == API_OK)
        {
            auto f = client->fsaccess->newfileaccess();
            string filePath(file);
            auto localPath = LocalPath::fromPath(filePath, *fsAccess);

            fsAccess->unlinklocal(localPath);
            if (!f->fopen(localPath, false, true))
            {
                e = API_EWRITE;
            }
            else if (size)
            {
                if (!f->fwrite((const byte*)data, size, 0))
                {
                    e = API_EWRITE;
                }
            }
        }
    }
    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}


void MegaApiImpl::timer_result(error e)
{
    if (requestMap.find(client->restag) == requestMap.end())
    {
        return;
    }

    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_TIMER))
    {
        return;
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

// callback for non-EAGAIN request-level errors
// retrying is futile
// this can occur e.g. with syntactically malformed requests (due to a bug) or due to an invalid application key
void MegaApiImpl::request_error(error e)
{
    if (e == API_EBLOCKED && client->sid.size())
    {
        whyAmIBlocked(true);
        return;
    }

    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_LOGOUT);
    request->setFlag(false);
    request->setParamType(e);

    if (e == API_ESSL && client->sslfakeissuer.size())
    {
        request->setText(client->sslfakeissuer.c_str());
    }

    if (e == API_ESID)
    {
        client->locallogout(true);
    }
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::request_response_progress(m_off_t currentProgress, m_off_t totalProgress)
{
    if (!client->isFetchingNodesPendingCS())
    {
        return;
    }
    for (std::map<int,MegaRequestPrivate*>::iterator it = requestMap.begin(); it != requestMap.end(); it++)
    {
        MegaRequestPrivate *request = it->second;
        if (request && request->getType() == MegaRequest::TYPE_FETCH_NODES)
        {
            request->setTransferredBytes(currentProgress);
            if (totalProgress != -1)
            {
                request->setTotalBytes(totalProgress);
            }
            fireOnRequestUpdate(request);
        }
    }
}

void MegaApiImpl::prelogin_result(int version, string* email, string *salt, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if (!request || ((request->getType() != MegaRequest::TYPE_LOGIN)
                    && (request->getType() != MegaRequest::TYPE_CONFIRM_RECOVERY_LINK)))
    {
        return;
    }

    if (e)
    {
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
        return;
    }

    if (request->getType() == MegaRequest::TYPE_LOGIN)
    {
        const char* pin = request->getText();
        if (version == 1)
        {
            const char *password = request->getPassword();
            const char* base64pwkey = request->getPrivateKey();
            if (base64pwkey)
            {
                byte pwkey[SymmCipher::KEYLENGTH];
                Base64::atob(base64pwkey, (byte *)pwkey, sizeof pwkey);
                if (password)
                {
                    uint64_t emailhash;
                    Base64::atob(password, (byte *)&emailhash, sizeof emailhash);

                    int creqtag = client->reqtag;
                    client->reqtag = client->restag;
                    client->fastlogin(email->c_str(), pwkey, emailhash);
                    client->reqtag = creqtag;
                }
                else
                {
                    int creqtag = client->reqtag;
                    client->reqtag = client->restag;
                    client->login(email->c_str(), pwkey, pin);
                    client->reqtag = creqtag;
                }
            }
            else
            {
                error err;
                byte pwkey[SymmCipher::KEYLENGTH];
                if ((err = client->pw_key(password, pwkey)))
                {
                    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(err));
                    return;
                }

                int creqtag = client->reqtag;
                client->reqtag = client->restag;
                client->login(email->c_str(), pwkey, pin);
                client->reqtag = creqtag;
            }
        }
        else if (version == 2 && salt)
        {
            const char *password = request->getPassword();
            const char* base64pwkey = request->getPrivateKey();
            if (base64pwkey)
            {
                byte derivedKey[2 * SymmCipher::KEYLENGTH];
                Base64::atob(base64pwkey, derivedKey, sizeof derivedKey);

                int creqtag = client->reqtag;
                client->reqtag = client->restag;
                client->login2(email->c_str(), derivedKey, pin);
                client->reqtag = creqtag;
            }
            else
            {
                int creqtag = client->reqtag;
                client->reqtag = client->restag;
                client->login2(email->c_str(), password, salt, pin);
                client->reqtag = creqtag;
            }
        }
        else
        {
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_EINTERNAL));
        }
    }
    else if (request->getType() == MegaRequest::TYPE_CONFIRM_RECOVERY_LINK)
    {
        request->setParamType(version);
        const char *link = request->getLink();
        const char* code;
        const char *mk64;

        if ((code = strstr(link, "#recover")))
        {
            code += strlen("#recover");
        }
        else
        {
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_EARGS));
            return;
        }

        long long type = request->getNumber();
        switch (type)
        {
            case RECOVER_WITH_MASTERKEY:
            {
                mk64 = request->getPrivateKey();
                if (!mk64)
                {
                    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_EARGS));
                    return;
                }

                int creqtag = client->reqtag;
                client->reqtag = client->restag;
                client->getprivatekey(code);
                client->reqtag = creqtag;
                break;
            }
            case RECOVER_WITHOUT_MASTERKEY:
            {
                int creqtag = client->reqtag;
                client->reqtag = client->restag;
                client->confirmrecoverylink(code, email->c_str(), request->getPassword(), NULL, version);
                client->reqtag = creqtag;
                break;
            }

        default:
            LOG_debug << "Unknown type of recovery link";

            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_EARGS));
            return;
        }
    }
}

// login result
void MegaApiImpl::login_result(error result)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_LOGIN)) return;

    // if login with user+pwd succeed, update lastLogin timestamp
    if (result == API_OK && request->getEmail() &&
            (request->getPassword() || request->getPrivateKey()))
    {
        client->isNewSession = true;
        client->tsLogin = m_time();
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(result));
}

void MegaApiImpl::logout_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_LOGOUT)) return;

    if(!e || e == API_ESID)
    {
        requestMap.erase(request->getTag());

        error preverror = (error)request->getParamType();
        abortPendingActions(preverror);

        pendingUploads = 0;
        pendingDownloads = 0;
        totalUploads = 0;
        totalDownloads = 0;
        waitingRequest = RETRY_NONE;
        excludedNames.clear();
        excludedPaths.clear();
        syncLowerSizeLimit = 0;
        syncUpperSizeLimit = 0;

        delete mPushSettings;
        mPushSettings = NULL;
        delete mTimezones;
        mTimezones = NULL;
    }
    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::userdata_result(string *name, string* pubk, string* privk, error result)
{
    // notify apps about the availability/update of user-flags, such as `aplvp`
    // (note that usually the API command is triggered internally, so no request is associated)
    if (result == API_OK)
    {
        MegaEventPrivate *event = new MegaEventPrivate(MegaEvent::EVENT_MISC_FLAGS_READY);
        fireOnEvent(event);
    }

    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_GET_USER_DATA)) return;

    if(result == API_OK)
    {
        request->setPassword(pubk->c_str());
        request->setPrivateKey(privk->c_str());
        request->setName(name->c_str());
    }
    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(result));
}

void MegaApiImpl::pubkey_result(User *u)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_GET_USER_DATA)) return;

    if(!u)
    {
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_ENOENT));
        return;
    }

    if(!u->pubk.isvalid())
    {
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_EACCESS));
        return;
    }

    string key;
    u->pubk.serializekey(&key, AsymmCipher::PUBKEY);
    char pubkbuf[AsymmCipher::MAXKEYLENGTH * 4 / 3 + 4];
    Base64::btoa((byte *)key.data(), int(key.size()), pubkbuf);
    request->setPassword(pubkbuf);

    char jid[16];
    Base32::btoa((byte *)&u->userhandle, MegaClient::USERHANDLE, jid);
    request->setText(jid);

    if(u->email.size())
    {
        request->setEmail(u->email.c_str());
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
}

// password change result
void MegaApiImpl::changepw_result(error result)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || request->getType() != MegaRequest::TYPE_CHANGE_PW) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(result));
}

// node export failed
void MegaApiImpl::exportnode_result(error result)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || request->getType() != MegaRequest::TYPE_EXPORT) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(result));
}

void MegaApiImpl::exportnode_result(handle h, handle ph)
{
    Node* n;
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || request->getType() != MegaRequest::TYPE_EXPORT) return;

    if ((n = client->nodebyhandle(h)))
    {
        char key[FILENODEKEYLENGTH*4/3+3];

        // the key
        if (n->type == FILENODE)
        {
            if(n->nodekey().size() >= FILENODEKEYLENGTH)
            {
                Base64::btoa((const byte*)n->nodekey().data(), FILENODEKEYLENGTH, key);
            }
            else
            {
                key[0]=0;
            }
        }
        else if (n->sharekey)
        {
            Base64::btoa(n->sharekey->key, FOLDERNODEKEYLENGTH, key);
        }
        else
        {
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(MegaError::API_EKEY));
            return;
        }

        string link = client->getPublicLink(client->mNewLinkFormat, n->type, ph, key);
        request->setLink(link.c_str());
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(MegaError::API_OK));
    }
    else
    {
        request->setNodeHandle(UNDEF);
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(MegaError::API_ENOENT));
    }
}

// the requested link could not be opened
void MegaApiImpl::openfilelink_result(const Error& result)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_IMPORT_LINK) &&
                    (request->getType() != MegaRequest::TYPE_GET_PUBLIC_NODE) &&
                    (request->getType() != MegaRequest::TYPE_CREATE_ACCOUNT))) return;

    if (request->getType() == MegaRequest::TYPE_CREATE_ACCOUNT)
    {
        return fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));    // if import fails, notify account was successfuly created anyway
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(result));
}

// the requested link was opened successfully
// (it is the application's responsibility to delete n!)
void MegaApiImpl::openfilelink_result(handle ph, const byte* key, m_off_t size, string* a, string* fa, int)
{
    MegaRequestPrivate* request = NULL;
    auto it = requestMap.find(client->restag);
    if (it == requestMap.end() || !(request = it->second)
            || ( request->getType() != MegaRequest::TYPE_IMPORT_LINK
                 && request->getType() != MegaRequest::TYPE_GET_PUBLIC_NODE
                 && request->getType() != MegaRequest::TYPE_CREATE_ACCOUNT) ) return;

    if (!client->loggedin() && (request->getType() == MegaRequest::TYPE_IMPORT_LINK))
    {
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(MegaError::API_EACCESS));
        return;
    }

    // no key provided --> check only that the nodehandle is valid
    if (!key && (request->getType() == MegaRequest::TYPE_GET_PUBLIC_NODE))
    {
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(MegaError::API_EINCOMPLETE));
        return;
    }

    AttrMap attrs;
    string fileName;
    string validName;
    string fingerprint;
    string originalfingerprint;
    FileFingerprint ffp;
    m_time_t mtime = 0;

    string attrstring;
    attrstring.resize(a->length()*4/3+4);
    attrstring.resize(Base64::btoa((const byte *)a->data(), int(a->length()), (char *)attrstring.data()));

    string keystring;
    SymmCipher nodeKey;
    keystring.assign((char*)key, FILENODEKEYLENGTH);
    nodeKey.setkey(key, FILENODE);

    byte *buf = Node::decryptattr(&nodeKey, attrstring.c_str(), attrstring.size());
    if (buf)
    {
        Node::parseattr(buf, attrs, size, mtime, fileName, fingerprint, ffp);

        // Normalize node name to UTF-8 string
        attr_map::iterator it = attrs.map.find('n');
        if (it != attrs.map.end() && !it->second.empty())
        {
            client->fsaccess->normalize(&(it->second));
            fileName = it->second.c_str();
            validName = fileName;
        }
        delete [] buf;
    }
    else
    {
        fileName = "CRYPTO_ERROR";
        request->setFlag(true);
    }

    if ((request->getType() == MegaRequest::TYPE_IMPORT_LINK)
            || (request->getType() == MegaRequest::TYPE_CREATE_ACCOUNT))
    {
        handle ovhandle = UNDEF;
        handle parenthandle = UNDEF;
        if (request->getType() == MegaRequest::TYPE_CREATE_ACCOUNT) // importing Welcome PDF
        {
            parenthandle = client->rootnodes[0];
        }
        else
        {
            parenthandle = request->getParentHandle();
        }

        Node *target = client->nodebyhandle(parenthandle);
        if (!target)
        {
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(MegaError::API_EARGS));
            return;
        }

        Node *ovn = client->childnodebyname(target, validName.c_str(), true);
        if (ovn)
        {
            if (ffp.isvalid && ovn->isvalid && ffp == *(FileFingerprint*)ovn)
            {
                request->setNodeHandle(ovn->nodehandle);
                fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
                return;
            }

            if (!client->versions_disabled)
            {
                ovhandle = ovn->nodehandle;
            }
        }

        NewNode* newnode = new NewNode[1];

        // set up new node as folder node
        newnode->source = NEW_PUBLIC;
        newnode->type = FILENODE;
        newnode->nodehandle = ph;
        newnode->parenthandle = UNDEF;
        newnode->nodekey.assign((char*)key,FILENODEKEYLENGTH);
        newnode->attrstring.reset(new string(*a));
        newnode->ovhandle = ovhandle;

        // add node
        requestMap.erase(request->getTag());
        int nextTag = client->nextreqtag();
        request->setTag(nextTag);
        requestMap[nextTag]=request;

        client->putnodes(parenthandle, newnode, 1);
    }
    else
    {
        MegaNodePrivate *megaNodePrivate = new MegaNodePrivate(fileName.c_str(), FILENODE, size, 0, mtime, ph, &keystring, a,
                                                           fa, fingerprint.size() ? fingerprint.c_str() : NULL,
                                                           originalfingerprint.size() ? originalfingerprint.c_str() : NULL, INVALID_HANDLE);
        request->setPublicNode(megaNodePrivate);
        delete megaNodePrivate;
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(MegaError::API_OK));
    }
}

// reload needed
void MegaApiImpl::reload(const char*)
{
    fireOnReloadNeeded();
}

// nodes have been modified
// (nodes with their removed flag set will be deleted immediately after returning from this call,
// at which point their pointers will become invalid at that point.)
void MegaApiImpl::nodes_updated(Node** n, int count)
{
    LOG_debug << "Nodes updated: " << count;
    if (!count)
    {
        return;
    }

    MegaNodeList *nodeList = NULL;
    if (n != NULL)
    {
        nodeList = new MegaNodeListPrivate(n, count);
        fireOnNodesUpdate(nodeList);
    }
    else
    {
        fireOnNodesUpdate(NULL);
    }
    delete nodeList;
}

void MegaApiImpl::account_details(AccountDetails*, bool, bool, bool, bool, bool, bool)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_ACCOUNT_DETAILS)) return;

    long long numPending = request->getNumber();
    numPending--;
    request->setNumber(numPending);
    if(!numPending)
    {
        bool storage_requested = request->getNumDetails() & 0x01;
        if (storage_requested && !request->getAccountDetails()->storage_max)
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(MegaError::API_EACCESS));
        else
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(MegaError::API_OK));
    }
}

void MegaApiImpl::account_details(AccountDetails*, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_ACCOUNT_DETAILS)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::querytransferquota_result(int code)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_QUERY_TRANSFER_QUOTA)) return;

    // pre-warn about a possible overquota for codes 2 and 3, like in the webclient
    request->setFlag((code == 2 || code == 3) ? true : false);

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
}

void MegaApiImpl::removecontact_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_REMOVE_CONTACT)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::putua_result(error e)
{
    MegaRequestPrivate* request = NULL;
    auto it = requestMap.find(client->restag);
    if (it == requestMap.end() || !(request = it->second)
        || (request->getType() != MegaRequest::TYPE_SET_ATTR_USER &&
            request->getType() != MegaRequest::TYPE_VERIFY_CREDENTIALS))
    {
        return;
    }

    if (e && client->fetchingkeys)
    {
        client->clearKeys();
        client->resetKeyring();
    }

    // if user just set the preferred language... change the GET param to the new language
    if (request->getParamType() == MegaApi::USER_ATTR_LANGUAGE && e == API_OK)
    {
        setLanguage(request->getText());
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::getua_result(error e)
{
    MegaRequestPrivate* request = NULL;
    auto it = requestMap.find(client->restag);
    if (it == requestMap.end() || !(request = it->second)
           || (request->getType() != MegaRequest::TYPE_GET_ATTR_USER && request->getType() != MegaRequest::TYPE_SET_ATTR_USER)) return;

    // if attempted to get ^!prd attribute but not exists yet...
    if (e == API_ENOENT)
    {
        if (request->getParamType() == MegaApi::USER_ATTR_PWD_REMINDER)
        {
            if (request->getType() == MegaRequest::TYPE_SET_ATTR_USER)
            {
                string newValue;
                User::mergePwdReminderData(request->getNumDetails(), NULL, 0, &newValue);
                request->setText(newValue.c_str());

                // set the attribute using same request tag
                client->putua(ATTR_PWD_REMINDER, (byte*) newValue.data(), unsigned(newValue.size()), client->restag);
                return;
            }
            else if (request->getType() == MegaRequest::TYPE_GET_ATTR_USER)
            {
                m_time_t currenttime = m_time();
                if ((currenttime - client->accountsince) > User::PWD_SHOW_AFTER_ACCOUNT_AGE
                        && (currenttime - client->tsLogin) > User::PWD_SHOW_AFTER_LASTLOGIN)
                {
                    request->setFlag(true); // the password reminder dialog should be shown
                }
            }
        }
        else if (request->getParamType() == MegaApi::USER_ATTR_RICH_PREVIEWS &&
                 request->getType() == MegaRequest::TYPE_GET_ATTR_USER)
        {
            if (request->getNumDetails() == 0)  // used to check if rich-links are enabled
            {
                request->setFlag(false);
            }
            else if (request->getNumDetails() == 1) // used to check if should show warning
            {
                request->setFlag(true);
            }
        }
        else if ((request->getParamType() == MegaApi::USER_ATTR_ALIAS
                  || request->getParamType() == MegaApi::USER_ATTR_CAMERA_UPLOADS_FOLDER
                  || request->getParamType() == MegaApi::USER_ATTR_DEVICE_NAMES)
                    && request->getType() == MegaRequest::TYPE_SET_ATTR_USER)
        {
            // The attribute doesn't exists so we have to create it
            TLVstore tlv;
            MegaStringMap *stringMap = request->getMegaStringMap();
            std::unique_ptr<MegaStringList> keys(stringMap->getKeys());
            const char *key;
            for (int i = 0; i < keys->size(); i++)
            {
                key = keys->get(i);
                tlv.set(key, Base64::atob(stringMap->get(key)));
            }

            // serialize and encrypt the TLV container
            attr_t type = static_cast<attr_t>(request->getParamType());
            std::unique_ptr<string> container(tlv.tlvRecordsToContainer(client->rng, &client->key));
            client->putua(type, (byte *)container->data(), unsigned(container->size()));
            return;
        }
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::getua_result(byte* data, unsigned len, attr_t type)
{
    error e = API_OK;

    // update cached notification settings for filtering
    MegaPushNotificationSettingsPrivate *pushSettings = NULL;
    if (type == ATTR_PUSH_SETTINGS)
    {
        string settingsJson((const char*)data, len);
        pushSettings = new MegaPushNotificationSettingsPrivate(settingsJson);
        if (pushSettings->isValid())
        {
            delete mPushSettings;
            mPushSettings = pushSettings->copy();
        }
        else
        {
            e = API_EINTERNAL;
            client->sendevent(99448, "Invalid JSON for received notification settings");
        }
    }

    MegaRequestPrivate* request = NULL;
    auto it = requestMap.find(client->restag);
    if (it == requestMap.end() || !(request = it->second)
        || (request->getType() != MegaRequest::TYPE_GET_ATTR_USER &&
            request->getType() != MegaRequest::TYPE_SET_ATTR_USER))
    {
        delete pushSettings;
        return;
    }
    
    assert(type == request->getParamType());

    if (request->getType() == MegaRequest::TYPE_SET_ATTR_USER)
    {
        static_assert(int(ATTR_PWD_REMINDER) == int(MegaApi::USER_ATTR_PWD_REMINDER), "User Attribute Enum Mismatch");
        if (int(type) == MegaApi::USER_ATTR_PWD_REMINDER)
        {
            // merge received value with updated items
            string newValue;
            bool changed = User::mergePwdReminderData(request->getNumDetails(), (const char*) data, len, &newValue);
            request->setText(newValue.data());

            if (changed)
            {
                // set the attribute using same request tag
                client->putua(ATTR_PWD_REMINDER, (byte*) newValue.data(), unsigned(newValue.size()), client->restag);
            }
            else
            {
                LOG_debug << "Password-reminder data not changed, already up to date";
                fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
            }
        }
        delete pushSettings;
        return;
    }

    // only for TYPE_GET_ATTR_USER
    switch (type)
    {
        case MegaApi::USER_ATTR_AVATAR:
            if (len)
            {

                auto f = client->fsaccess->newfileaccess();
                string filePath(request->getFile());
                auto localPath = LocalPath::fromPath(filePath, *fsAccess);

                fsAccess->unlinklocal(localPath);
                
                bool success = f->fopen(localPath, false, true)
                            && f->fwrite((const byte*)data, len, 0);

                f.reset();

                if (!success)
                {
                    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_EWRITE));
                    return;
                }
            }
            else    // no data for the avatar
            {
                fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_ENOENT));
                return;
            }

            break;

        // null-terminated char arrays
        case MegaApi::USER_ATTR_FIRSTNAME:
        case MegaApi::USER_ATTR_LASTNAME:
        case MegaApi::USER_ATTR_LANGUAGE:   // it's a c-string in binary format, want the plain data
        case MegaApi::USER_ATTR_PWD_REMINDER:
        case MegaApi::USER_ATTR_DISABLE_VERSIONS:
        case MegaApi::USER_ATTR_CONTACT_LINK_VERIFICATION:
            {
                string str((const char*)data,len);
                request->setText(str.c_str());

                static_assert(int(MegaApi::USER_ATTR_DISABLE_VERSIONS) == ATTR_DISABLE_VERSIONS, "User Attribute Enum Mismatch");
                static_assert(int(MegaApi::USER_ATTR_CONTACT_LINK_VERIFICATION) == ATTR_CONTACT_LINK_VERIFICATION, "User Attribute Enum Mismatch");
                static_assert(int(MegaApi::USER_ATTR_PWD_REMINDER) == ATTR_PWD_REMINDER, "User Attribute Enum Mismatch");

                if (int(type) == MegaApi::USER_ATTR_DISABLE_VERSIONS
                        || int(type) == MegaApi::USER_ATTR_CONTACT_LINK_VERIFICATION)
                {
                    request->setFlag(str == "1");
                }
                else if (int(type) == MegaApi::USER_ATTR_PWD_REMINDER)
                {
                    m_time_t currenttime = m_time();
                    bool isMasterKeyExported = User::getPwdReminderData(User::PWD_MK_EXPORTED, (const char*)data, len);
                    bool isLogout = request->getNumber();
                    bool pwdDontShow = User::getPwdReminderData(User::PWD_DONT_SHOW, (const char*)data, len);
                    if ((!isMasterKeyExported
                            && !pwdDontShow
                            && (currenttime - client->accountsince) > User::PWD_SHOW_AFTER_ACCOUNT_AGE
                            && (currenttime - User::getPwdReminderData(User::PWD_LAST_SUCCESS, (const char*)data, len)) > User::PWD_SHOW_AFTER_LASTSUCCESS
                            && (currenttime - User::getPwdReminderData(User::PWD_LAST_LOGIN, (const char*)data, len)) > User::PWD_SHOW_AFTER_LASTLOGIN
                            && (currenttime - User::getPwdReminderData(User::PWD_LAST_SKIPPED, (const char*)data, len)) > (request->getNumber() ? User::PWD_SHOW_AFTER_LASTSKIP_LOGOUT : User::PWD_SHOW_AFTER_LASTSKIP)
                            && (currenttime - client->tsLogin) > User::PWD_SHOW_AFTER_LASTLOGIN)
                            || (isLogout && !pwdDontShow))
                    {
                        request->setFlag(true); // the password reminder dialog should be shown
                    }
                    request->setAccess(isMasterKeyExported ? 1 : 0);
                }
            }
            break;

        // numbers
        case MegaApi::USER_ATTR_RUBBISH_TIME:
        case MegaApi::USER_ATTR_STORAGE_STATE:
            {
                char *endptr;
                string str((const char*)data, len);
                m_off_t value = strtoll(str.c_str(), &endptr, 10);
                if (endptr == str.c_str() || *endptr != '\0' || value == LLONG_MAX || value == LLONG_MIN)
                {
                    value = -1;
                }

                request->setNumber(value);

                static_assert(int(MegaApi::USER_ATTR_STORAGE_STATE) == ATTR_STORAGE_STATE, "User Attribute Enum Mismatch");
                if (int(type) == MegaApi::USER_ATTR_STORAGE_STATE && (value < MegaApi::STORAGE_STATE_GREEN || value > MegaApi::STORAGE_STATE_RED))
                {
                    e = API_EINTERNAL;
                }
            }
            break;

        case MegaApi::USER_ATTR_PUSH_SETTINGS:
        {
            request->setMegaPushNotificationSettings(pushSettings);
        }
        break;

        // byte arrays with possible nulls in the middle --> to Base64
        case MegaApi::USER_ATTR_ED25519_PUBLIC_KEY: // fall-through
        {
            if (request->getFlag()) // asking for the fingerprint
            {
                string fingerprint = AuthRing::fingerprint(string((const char*)data, len), true);
                request->setPassword(fingerprint.c_str());
                break;
            }
        }
        case MegaApi::USER_ATTR_CU25519_PUBLIC_KEY:
        case MegaApi::USER_ATTR_SIG_RSA_PUBLIC_KEY:
        case MegaApi::USER_ATTR_SIG_CU255_PUBLIC_KEY:
        default:
        {
            string str;
            str.resize(len * 4 / 3 + 4);
            str.resize(Base64::btoa(data, len, (char*)str.data()));
            request->setText(str.c_str());
        }
        break;
    }

    delete pushSettings;
    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::getua_result(TLVstore *tlv, attr_t type)
{
    error e = API_OK;
    MegaRequestPrivate* request = NULL;
    auto it = requestMap.find(client->restag);
    if (it == requestMap.end() || !(request = it->second)
           || (request->getType() != MegaRequest::TYPE_GET_ATTR_USER
               && request->getType() != MegaRequest::TYPE_SET_ATTR_USER)) return;

    assert(type == static_cast<attr_t>(request->getParamType()));

    if (tlv)
    {
        if (request->getType() == MegaRequest::TYPE_SET_ATTR_USER)
        {
            const string_map *newValuesMap = static_cast<MegaStringMapPrivate*>(request->getMegaStringMap())->getMap();
            if (User::mergeUserAttribute(type, *newValuesMap, *tlv))
            {
                // serialize and encrypt the TLV container
                std::unique_ptr<string> container(tlv->tlvRecordsToContainer(client->rng, &client->key));
                client->putua(type, (byte *)container->data(), unsigned(container->size()), client->restag);
            }
            else
            {
                LOG_debug << "Attribute " << User::attr2string(type) << " not changed, already up to date";
                fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
            }

            return;
        }   // end of get+set

        // TLV data usually includes byte arrays with zeros in the middle, so values
        // must be converted into Base64 strings to avoid problems
        std::unique_ptr<MegaStringMap> stringMap(new MegaStringMapPrivate(tlv->getMap(), true));
        request->setMegaStringMap(stringMap.get());
        switch (request->getParamType())
        {
            // prepare request params to know if a warning should show or not
            case MegaApi::USER_ATTR_RICH_PREVIEWS:
            {
                const char *num = stringMap->get("num");

                if (request->getNumDetails() == 0)  // used to check if rich-links are enabled
                {
                    if (num)
                    {
                        string sValue = num;
                        string bValue;
                        Base64::atob(sValue, bValue);
                        request->setFlag(bValue == "1");
                    }
                    else
                    {
                        request->setFlag(false);
                    }
                }
                else if (request->getNumDetails() == 1) // used to check if should show warning
                {
                    request->setFlag(!num);
                    // it doesn't matter the value, just if it exists

                    const char *value = stringMap->get("c");
                    if (value)
                    {
                        string sValue = value;
                        string bValue;
                        Base64::atob(sValue, bValue);
                        request->setNumber(atoi(bValue.c_str()));
                    }
                }
                break;
            }
            case MegaApi::USER_ATTR_CAMERA_UPLOADS_FOLDER:
            case MegaApi::USER_ATTR_MY_CHAT_FILES_FOLDER:
            {
                // If attr is CAMERA_UPLOADS_FOLDER determine if we want to retrieve primary or secondary folder
                // If attr is MY_CHAT_FILES_FOLDER there's no secondary folder
                const char *key = request->getParamType() == MegaApi::USER_ATTR_CAMERA_UPLOADS_FOLDER
                        && request->getFlag()
                            ? "sh"
                            : "h";

                const char *value = stringMap->get(key);
                if (!value)
                {
                    e = API_ENOENT;
                    break;
                }
                else
                {
                   handle nodehandle = 0;  // make sure top two bytes are 0
                   Base64::atob(value, (byte*) &nodehandle, MegaClient::NODEHANDLE);
                   request->setNodeHandle(nodehandle);
                }
                break;
            }
            case MegaApi::USER_ATTR_ALIAS:
            {
                // If a uh was set in the request, we have to find it in the aliases map and return it
                const char *uh = request->getText();
                if (uh)
                {
                    const char *buf = stringMap->get(uh);
                    if (!buf)
                    {
                        e = API_ENOENT;
                        break;
                    }
                    else
                    {
                        request->setName(Base64::atob(buf).c_str());
                    }
                }
                break;
            }
            case MegaApi::USER_ATTR_DEVICE_NAMES:
            {
                const char *buf = stringMap->get(client->getDeviceid().c_str());
                if (!buf)
                {
                    e = API_ENOENT;
                    break;
                }
                request->setName(Base64::atob(buf).c_str());
                break;
            }

            default:
                break;
        }
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

#ifdef DEBUG
void MegaApiImpl::delua_result(error)
{
}

void MegaApiImpl::senddevcommand_result(int value)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_SEND_DEV_COMMAND)) return;

    error e = static_cast<error>(value);
    std::string command = request->getName() ? request->getName() :"";
    if (!command.compare("aodq") && value > 0)
    {
        e = API_OK;
        request->setNumber(value);
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}
#endif

void MegaApiImpl::getuseremail_result(string *email, error e)
{
    if (requestMap.find(client->restag) == requestMap.end())
    {
        return;
    }
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if (!request || (request->getType() != MegaRequest::TYPE_GET_USER_EMAIL))
    {
        return;
    }

    if (e == API_OK && email)
    {
        request->setEmail(email->c_str());
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
    return;
}

// user attribute update notification
void MegaApiImpl::userattr_update(User*, int, const char*)
{
}

void MegaApiImpl::nodes_current()
{
    MegaEventPrivate *event = new MegaEventPrivate(MegaEvent::EVENT_NODES_CURRENT);
    fireOnEvent(event);
}

void MegaApiImpl::catchup_result()
{
    // sc requests are sent sequentially, it must be the one at front and already started (tag == 1)
    MegaRequestPrivate *request = scRequestQueue.front();
    if (!request || (request->getType() != MegaRequest::TYPE_CATCHUP) || !request->getTag()) return;
    request = scRequestQueue.pop();

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));

    // if there are more sc requests in the queue, send the next one
    if (scRequestQueue.front())
    {
        waiter->notify();
    }
}

void MegaApiImpl::key_modified(handle userhandle, attr_t attribute)
{
    MegaEventPrivate *event = new MegaEventPrivate(MegaEvent::EVENT_KEY_MODIFIED);
    switch (attribute)
    {
    case ATTR_CU25519_PUBK:
        event->setNumber(0);
        break;
    case ATTR_ED25519_PUBK:
        event->setNumber(1);
        break;
    case ATTR_UNKNOWN: // used internally for RSA
        event->setNumber(2);
        break;
    case ATTR_SIG_CU255_PUBK:
        event->setNumber(3);
        break;
    case ATTR_SIG_RSA_PUBK:
        event->setNumber(4);
        break;
    default:
        event->setNumber(-1);
        break;
    }
    event->setHandle(userhandle);
    fireOnEvent(event);
}

void MegaApiImpl::ephemeral_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_CREATE_ACCOUNT))) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::ephemeral_result(handle uh, const byte* pw)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_CREATE_ACCOUNT))) return;

    // save uh and pwcipher for session resumption of ephemeral accounts
    char buf[SymmCipher::KEYLENGTH * 4 / 3 + 3];
    Base64::btoa((byte*) &uh, sizeof uh, buf);
    string sid;
    sid.append(buf);
    sid.append("#");
    Base64::btoa(pw, SymmCipher::KEYLENGTH, buf);
    sid.append(buf);
    request->setSessionKey(sid.c_str());

    // chain a fetchnodes to get waitlink for ephemeral account
    int creqtag = client->reqtag;
    client->reqtag = client->restag;
    client->fetchnodes();
    client->reqtag = creqtag;
}

void MegaApiImpl::cancelsignup_result(error e)
{
    auto it = requestMap.find(client->restag);
    if (it == requestMap.end()) return;
    MegaRequestPrivate *request = it->second;
    if (!request || ((request->getType() != MegaRequest::TYPE_CREATE_ACCOUNT))) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::whyamiblocked_result(int code)
{
    if (requestMap.find(client->restag) == requestMap.end())
    {
        return;
    }
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if (!request || ((request->getType() != MegaRequest::TYPE_WHY_AM_I_BLOCKED)))
    {
        return;
    }

    if (code <= 0)
    {
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(code));
    }
    else    // code > 0
    {
        string reason = "Your account was terminated due to breach of Mega's Terms of Service, such as abuse of rights of others; sharing and/or importing illegal data; or system abuse.";

        if (code == MegaApi::ACCOUNT_BLOCKED_EXCESS_DATA_USAGE)    // deprecated
        {
            reason = "You have been suspended due to excess data usage.";
        }
        else if (code == MegaApi::ACCOUNT_BLOCKED_TOS_NON_COPYRIGHT)
        {
            reason = "Your account has been suspended due to multiple breaches of Mega's Terms of Service. Please check your email inbox.";
        }
        else if (code == MegaApi::ACCOUNT_BLOCKED_TOS_COPYRIGHT)
        {
            reason = "Your account has been suspended due to copyright violations. Please check your email inbox.";
        }
        else if (code == MegaApi::ACCOUNT_BLOCKED_SUBUSER_DISABLED)
        {
            reason = "Your account has been disabled by your administrator. You may contact your business account administrator for further details.";
        }
        else if (code == MegaApi::ACCOUNT_BLOCKED_SUBUSER_REMOVED)
        {
            reason = "Your account has been removed by your administrator. You may contact your business account administrator for further details.";
        }
        else if (code == MegaApi::ACCOUNT_BLOCKED_VERIFICATION_SMS)
        {
            reason = "Your account has been blocked pending verification via SMS.";
        }
        else if (code == MegaApi::ACCOUNT_BLOCKED_VERIFICATION_EMAIL)
        {
            reason = "Your account has been temporarily suspended for your safety. Please verify your email and follow its steps to unlock your account.";
        }
        //else if (code == ACCOUNT_BLOCKED_DEFAULT) --> default reason

        bool logoutAllowed = request->getFlag();
        request->setNumber(code);
        request->setText(reason.c_str());
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));

        MegaEventPrivate *event = new MegaEventPrivate(MegaEvent::EVENT_ACCOUNT_BLOCKED);
        event->setNumber(code);
        event->setText(reason.c_str());
        fireOnEvent(event);

        // (don't log out if we can be unblocked by email or sms)
        if (logoutAllowed
                && code != MegaApi::ACCOUNT_BLOCKED_VERIFICATION_SMS
                && code != MegaApi::ACCOUNT_BLOCKED_VERIFICATION_EMAIL)
        {
            client->locallogout(true);

            MegaRequestPrivate *logoutRequest = new MegaRequestPrivate(MegaRequest::TYPE_LOGOUT);
            logoutRequest->setFlag(false);
            logoutRequest->setParamType(API_EBLOCKED);
            requestQueue.push(logoutRequest);
            waiter->notify();
        }
    }
}

void MegaApiImpl::contactlinkcreate_result(error e, handle h)
{
    if (requestMap.find(client->restag) == requestMap.end())
    {
        return;
    }
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if (!request || ((request->getType() != MegaRequest::TYPE_CONTACT_LINK_CREATE)))
    {
        return;
    }

    if (!e)
    {
        request->setNodeHandle(h);
    }
    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::contactlinkquery_result(error e, handle h, string *email, string *firstname, string *lastname, string *avatar)
{
    if (requestMap.find(client->restag) == requestMap.end())
    {
        return;
    }
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if (!request || ((request->getType() != MegaRequest::TYPE_CONTACT_LINK_QUERY)))
    {
        return;
    }

    if (!e)
    {
        request->setParentHandle(h);
        request->setEmail(email->c_str());
        request->setName(Base64::atob(*firstname).c_str());
        request->setText(Base64::atob(*lastname).c_str());
        request->setFile(avatar->c_str());
    }
    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::contactlinkdelete_result(error e)
{
    if (requestMap.find(client->restag) == requestMap.end())
    {
        return;
    }
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if (!request || ((request->getType() != MegaRequest::TYPE_CONTACT_LINK_DELETE)))
    {
        return;
    }
    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::keepmealive_result(error e)
{
    if (requestMap.find(client->restag) == requestMap.end())
    {
        return;
    }

    MegaRequestPrivate* request = requestMap.at(client->restag);
    if (!request || ((request->getType() != MegaRequest::TYPE_KEEP_ME_ALIVE)))
    {
        return;
    }
    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::getpsa_result(error e, int id, string *title, string *text, string *image, string *buttontext, string *buttonlink)
{
    if (requestMap.find(client->restag) == requestMap.end())
    {
        return;
    }

    MegaRequestPrivate* request = requestMap.at(client->restag);
    if (!request || ((request->getType() != MegaRequest::TYPE_GET_PSA)))
    {
        return;
    }

    if (!e)
    {
        request->setNumber(id);
        request->setName(title->c_str());
        request->setText(text->c_str());
        request->setFile(image->c_str());
        request->setPassword(buttontext->c_str());
        request->setLink(buttonlink->c_str());
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::multifactorauthsetup_result(string *code, error e)
{
    if (requestMap.find(client->restag) == requestMap.end())
    {
        return;
    }
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if (!request || ((request->getType() != MegaRequest::TYPE_MULTI_FACTOR_AUTH_GET) &&
                    (request->getType() != MegaRequest::TYPE_MULTI_FACTOR_AUTH_SET)))
    {
        return;
    }

    if (request->getType() == MegaRequest::TYPE_MULTI_FACTOR_AUTH_GET && !e)
    {
        if (!code)
        {
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_EINTERNAL));
            return;
        }
        request->setText(code->c_str());
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::multifactorauthcheck_result(int enabled)
{
    if (requestMap.find(client->restag) == requestMap.end())
    {
        return;
    }
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if (!request || ((request->getType() != MegaRequest::TYPE_MULTI_FACTOR_AUTH_CHECK)))
    {
        return;
    }

    if (enabled < 0)
    {
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(enabled));
        return;
    }

    request->setFlag(enabled);
    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
}

void MegaApiImpl::multifactorauthdisable_result(error e)
{
    if (requestMap.find(client->restag) == requestMap.end())
    {
        return;
    }
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if (!request || ((request->getType() != MegaRequest::TYPE_MULTI_FACTOR_AUTH_SET)))
    {
        return;
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::fetchtimezone_result(error e, vector<std::string> *timezones, vector<int> *timezoneoffsets, int defaulttz)
{
    unique_ptr<MegaTimeZoneDetails> tzDetails;
    if (!e)
    {
        tzDetails.reset(new MegaTimeZoneDetailsPrivate(timezones, timezoneoffsets, defaulttz));

        // update the cached timezones for notifications filtering
        delete mTimezones;
        mTimezones = tzDetails->copy();
    }

    if (requestMap.find(client->restag) == requestMap.end())
    {
        return;
    }
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if (!request || ((request->getType() != MegaRequest::TYPE_FETCH_TIMEZONE)))
    {
        return;
    }

    request->setTimeZoneDetails(tzDetails.get());
    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::acknowledgeuseralerts_result(error e)
{
    map<int, MegaRequestPrivate *>::iterator it = requestMap.find(client->restag);
    if (it != requestMap.end())
    {
        MegaRequestPrivate* request = it->second;
        if (request && ((request->getType() == MegaRequest::TYPE_USERALERT_ACKNOWLEDGE)))
        {
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
        }
    }
}

void MegaApiImpl::smsverificationsend_result(error e)
{
    map<int, MegaRequestPrivate *>::iterator it = requestMap.find(client->restag);
    if (it != requestMap.end())
    {
        MegaRequestPrivate* request = it->second;
        if (request && ((request->getType() == MegaRequest::TYPE_SEND_SMS_VERIFICATIONCODE)))
        {
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
        }
    }
}

void MegaApiImpl::smsverificationcheck_result(error e, string* phoneNumber)
{
    map<int, MegaRequestPrivate *>::iterator it = requestMap.find(client->restag);
    if (it != requestMap.end())
    {
        MegaRequestPrivate* request = it->second;
        if (request && ((request->getType() == MegaRequest::TYPE_CHECK_SMS_VERIFICATIONCODE)))
        {
            if (e == API_OK && phoneNumber)
            {
                request->setName(phoneNumber->c_str());
            }
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
        }
    }
}

void MegaApiImpl::getregisteredcontacts_result(error e, vector<tuple<string, string, string>>* data)
{
    auto it = requestMap.find(client->restag);
    if (it != requestMap.end())
    {
        MegaRequestPrivate* request = it->second;
        if (request && ((request->getType() == MegaRequest::TYPE_GET_REGISTERED_CONTACTS)))
        {
            if (data)
            {
                auto stringTable = std::unique_ptr<MegaStringTable>{MegaStringTable::createInstance()};
                for (const auto& row : *data)
                {
                    vector<char*> list;
                    list.emplace_back(MegaApi::strdup(std::get<0>(row).c_str()));
                    list.emplace_back(MegaApi::strdup(std::get<1>(row).c_str()));
                    list.emplace_back(MegaApi::strdup(std::get<2>(row).c_str()));
                    auto stringList = new MegaStringListPrivate{list.data(), static_cast<int>(list.size())};
                    stringTable->append(stringList);
                }
                request->setMegaStringTable(stringTable.get());
            }
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
        }
    }
}

void MegaApiImpl::getcountrycallingcodes_result(error e, map<string, vector<string>>* data)
{
    auto it = requestMap.find(client->restag);
    if (it != requestMap.end())
    {
        MegaRequestPrivate* request = it->second;
        if (request && ((request->getType() == MegaRequest::TYPE_GET_COUNTRY_CALLING_CODES)))
        {
            if (data)
            {
                auto stringListMap = std::unique_ptr<MegaStringListMap>{MegaStringListMap::createInstance()};
                for (const auto& pair : *data)
                {
                    vector<char*> list;
                    for (const auto& value : pair.second)
                    {
                        list.emplace_back(MegaApi::strdup(value.c_str()));
                    }
                    auto stringList = new MegaStringListPrivate{list.data(), static_cast<int>(list.size())};
                    stringListMap->set(pair.first.c_str(), stringList);
                }
                request->setMegaStringListMap(stringListMap.get());
            }
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
        }
    }
}

void MegaApiImpl::sendsignuplink_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_CREATE_ACCOUNT) &&
                    (request->getType() != MegaRequest::TYPE_SEND_SIGNUP_LINK))) return;

    if ((request->getType() == MegaRequest::TYPE_CREATE_ACCOUNT)
            && (e == API_OK) && (request->getParamType() == 0))   // new account has been created
    {
        int creqtag = client->reqtag;
        client->reqtag = client->restag;
        client->getwelcomepdf();
        client->reqtag = creqtag;
        return;
    }

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::querysignuplink_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_QUERY_SIGNUP_LINK) &&
                    (request->getType() != MegaRequest::TYPE_CONFIRM_ACCOUNT))) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::querysignuplink_result(handle uh, const char* email, const char* name, const byte* pwc, const byte*, const byte* c, size_t len)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_QUERY_SIGNUP_LINK) &&
                    (request->getType() != MegaRequest::TYPE_CONFIRM_ACCOUNT))) return;

    request->setEmail(email);
    request->setName(name);

    error e = API_OK;
    if (uh != client->me)
    {
        e = API_EACCESS;
    }
    
    if(request->getType() == MegaRequest::TYPE_QUERY_SIGNUP_LINK || e)
    {
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
        return;
    }

    string signupemail = email;
    string signupcode;
    signupcode.assign((char*)c,len);

    byte signuppwchallenge[SymmCipher::KEYLENGTH];
    byte signupencryptedmasterkey[SymmCipher::KEYLENGTH];

    memcpy(signuppwchallenge,pwc,sizeof signuppwchallenge);
    memcpy(signupencryptedmasterkey,pwc,sizeof signupencryptedmasterkey);

    byte pwkey[SymmCipher::KEYLENGTH];
    if(!request->getPrivateKey())
        client->pw_key(request->getPassword(),pwkey);
    else
        Base64::atob(request->getPrivateKey(), (byte *)pwkey, sizeof pwkey);

    // verify correctness of supplied signup password
    SymmCipher pwcipher(pwkey);
    pwcipher.ecb_decrypt(signuppwchallenge);

    if (*(uint64_t*)(signuppwchallenge+4))
    {
        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_EKEY));
    }
    else
    {
        requestMap.erase(request->getTag());
        int nextTag = client->nextreqtag();
        request->setTag(nextTag);
        requestMap[nextTag] = request;

        client->confirmsignuplink((const byte*)signupcode.data(), int(signupcode.size()), MegaClient::stringhash64(&signupemail,&pwcipher));
    }
}

void MegaApiImpl::confirmsignuplink_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CONFIRM_ACCOUNT)) return;

    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::confirmsignuplink2_result(handle, const char *name, const char *email, error e)
{
    if (requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if (!request || ((request->getType() != MegaRequest::TYPE_CONFIRM_ACCOUNT) &&
                    (request->getType() != MegaRequest::TYPE_QUERY_SIGNUP_LINK))) return;

    if (!e)
    {
        assert(strcmp(email, request->getEmail()) == 0);
        assert(strcmp(name, request->getName()) == 0);
        request->setName(name);
        request->setEmail(email);
        request->setFlag(true);
    }
    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
}

void MegaApiImpl::setkeypair_result(error)
{

}

void MegaApiImpl::checkfile_result(handle h, const Error &e)
{
    if(e)
    {
        for(std::map<int, MegaTransferPrivate *>::iterator iter = transferMap.begin(); iter != transferMap.end(); iter++)
        {
            MegaTransferPrivate *transfer = iter->second;
            if (transfer->getNodeHandle() == h)
            {
                auto megaError = make_unique<MegaErrorPrivate>(e);
                transfer->setLastError(megaError.get());

                if (e == API_ETOOMANY && e.hasExtraInfo())
                {
                    DBTableTransactionCommitter committer(client->tctable);
                    transfer->setState(MegaTransfer::STATE_FAILED);
                    fireOnTransferFinish(transfer, std::move(megaError), committer);
                }
                else
                {
                    transfer->setState(MegaTransfer::STATE_RETRYING);
                    fireOnTransferTemporaryError(transfer, std::move(megaError));
                }
            }
        }
    }
}

void MegaApiImpl::checkfile_result(handle h, error e, byte*, m_off_t, m_time_t, m_time_t, string*, string*, string*)
{
    if(e)
    {
        for(std::map<int, MegaTransferPrivate *>::iterator iter = transferMap.begin(); iter != transferMap.end(); iter++)
        {
            MegaTransferPrivate *transfer = iter->second;
            if (transfer->getNodeHandle() == h)
            {
                auto megaError = make_unique<MegaErrorPrivate>(e);
                transfer->setLastError(megaError.get());
                transfer->setState(MegaTransfer::STATE_RETRYING);
                fireOnTransferTemporaryError(transfer, std::move(megaError));
            }
        }
    }
}

void MegaApiImpl::addListener(MegaListener* listener)
{
    if(!listener) return;

    sdkMutex.lock();
    listeners.insert(listener);
    sdkMutex.unlock();
}

void MegaApiImpl::addRequestListener(MegaRequestListener* listener)
{
    if(!listener) return;

    sdkMutex.lock();
    requestListeners.insert(listener);
    sdkMutex.unlock();
}

void MegaApiImpl::addTransferListener(MegaTransferListener* listener)
{
    if(!listener) return;

    sdkMutex.lock();
    transferListeners.insert(listener);
    sdkMutex.unlock();
}

void MegaApiImpl::addBackupListener(MegaBackupListener* listener)
{
    if(!listener) return;

    sdkMutex.lock();
    backupListeners.insert(listener);
    sdkMutex.unlock();
}

void MegaApiImpl::addGlobalListener(MegaGlobalListener* listener)
{
    if(!listener) return;

    sdkMutex.lock();
    globalListeners.insert(listener);
    sdkMutex.unlock();
}

#ifdef ENABLE_SYNC
void MegaApiImpl::addSyncListener(MegaSyncListener *listener)
{
    if(!listener) return;

    sdkMutex.lock();
    syncListeners.insert(listener);
    sdkMutex.unlock();
}

void MegaApiImpl::removeSyncListener(MegaSyncListener *listener)
{
    if(!listener) return;

    sdkMutex.lock();
    syncListeners.erase(listener);

    std::map<int, MegaSyncPrivate*>::iterator it = syncMap.begin();
    while(it != syncMap.end())
    {
        MegaSyncPrivate* sync = it->second;
        if(sync->getListener() == listener)
            sync->setListener(NULL);

        it++;
    }
    requestQueue.removeListener(listener);

    sdkMutex.unlock();
}
#endif

void MegaApiImpl::removeListener(MegaListener* listener)
{
    if(!listener) return;

    sdkMutex.lock();
    listeners.erase(listener);
    sdkMutex.unlock();
}

void MegaApiImpl::removeRequestListener(MegaRequestListener* listener)
{
    if(!listener) return;

    sdkMutex.lock();
    requestListeners.erase(listener);

    std::map<int, MegaRequestPrivate*>::iterator it = requestMap.begin();
    while(it != requestMap.end())
    {
        MegaRequestPrivate* request = it->second;
        if(request->getListener() == listener)
            request->setListener(NULL);

        it++;
    }

    requestQueue.removeListener(listener);
    sdkMutex.unlock();
}

void MegaApiImpl::removeTransferListener(MegaTransferListener* listener)
{
    if(!listener) return;

    sdkMutex.lock();
    transferListeners.erase(listener);

    std::map<int, MegaTransferPrivate*>::iterator it = transferMap.begin();
    while(it != transferMap.end())
    {
        MegaTransferPrivate* transfer = it->second;
        if(transfer->getListener() == listener)
            transfer->setListener(NULL);

        it++;
    }

    transferQueue.removeListener(listener);
    sdkMutex.unlock();
}

void MegaApiImpl::removeBackupListener(MegaBackupListener* listener)
{
    if(!listener) return;

    sdkMutex.lock();
    backupListeners.erase(listener);

    std::map<int, MegaBackupController*>::iterator it = backupsMap.begin();
    while(it != backupsMap.end())
    {
        MegaBackupController* backup = it->second;
        if(backup->getBackupListener() == listener)
            backup->setBackupListener(NULL);

        it++;
    }

    requestQueue.removeListener(listener);
    sdkMutex.unlock();
}

void MegaApiImpl::removeGlobalListener(MegaGlobalListener* listener)
{
    if(!listener) return;

    sdkMutex.lock();
    globalListeners.erase(listener);
    sdkMutex.unlock();
}

void MegaApiImpl::cancelPendingTransfersByFolderTag(int folderTag)
{
    DBTableTransactionCommitter committer(client->tctable);

    long long cancelledPending = 0;
    transferQueue.removeWithFolderTag(folderTag, [this, &committer, &cancelledPending](MegaTransferPrivate *transfer)
    {
        fireOnTransferStart(transfer);
        transfer->setState(MegaTransfer::STATE_CANCELLED);
        fireOnTransferFinish(transfer, make_unique<MegaErrorPrivate>(API_EINCOMPLETE), committer);
        cancelledPending++;
    });

    LOG_verbose << " Cancelled pending transfers by folder tag = " << cancelledPending;
}

MegaRequest *MegaApiImpl::getCurrentRequest()
{
    return activeRequest;
}

MegaTransfer *MegaApiImpl::getCurrentTransfer()
{
    return activeTransfer;
}

MegaError *MegaApiImpl::getCurrentError()
{
    return activeError;
}

MegaNodeList *MegaApiImpl::getCurrentNodes()
{
    return activeNodes;
}

MegaUserList *MegaApiImpl::getCurrentUsers()
{
    return activeUsers;
}

void MegaApiImpl::fireOnRequestStart(MegaRequestPrivate *request)
{
    activeRequest = request;
    LOG_info << "Request (" << request->getRequestString() << ") starting";
    for(set<MegaRequestListener *>::iterator it = requestListeners.begin(); it != requestListeners.end() ;)
    {
        (*it++)->onRequestStart(api, request);
    }

    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ;)
    {
        (*it++)->onRequestStart(api, request);
    }

    MegaRequestListener* listener = request->getListener();
    if(listener)
    {
        listener->onRequestStart(api, request);
    }
    activeRequest = NULL;
}


void MegaApiImpl::fireOnRequestFinish(MegaRequestPrivate *request, unique_ptr<MegaErrorPrivate> e)
{
    activeRequest = request;
    activeError = e.get();

    if(e->getErrorCode())
    {
        LOG_warn << "Request (" << request->getRequestString() << ") finished with error: " << e->getErrorString();
    }
    else
    {
        LOG_info << "Request (" << request->getRequestString() << ") finished";
    }

    for(set<MegaRequestListener *>::iterator it = requestListeners.begin(); it != requestListeners.end() ;)
    {
        (*it++)->onRequestFinish(api, request, e.get());
    }

    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ;)
    {
        (*it++)->onRequestFinish(api, request, e.get());
    }

    MegaRequestListener* listener = request->getListener();
    if(listener)
    {
        listener->onRequestFinish(api, request, e.get());
    }

    requestMap.erase(request->getTag());

    activeRequest = NULL;
    activeError = NULL;
    delete request;
}

void MegaApiImpl::fireOnRequestUpdate(MegaRequestPrivate *request)
{
    activeRequest = request;

    for(set<MegaRequestListener *>::iterator it = requestListeners.begin(); it != requestListeners.end() ;)
    {
        (*it++)->onRequestUpdate(api, request);
    }

    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ;)
    {
        (*it++)->onRequestUpdate(api, request);
    }

    MegaRequestListener* listener = request->getListener();
    if(listener)
    {
        listener->onRequestUpdate(api, request);
    }

    activeRequest = NULL;
}

void MegaApiImpl::fireOnRequestTemporaryError(MegaRequestPrivate *request, unique_ptr<MegaErrorPrivate> e)
{
    activeRequest = request;
    activeError = e.get();

    request->setNumRetry(request->getNumRetry() + 1);

    for(set<MegaRequestListener *>::iterator it = requestListeners.begin(); it != requestListeners.end() ;)
    {
        (*it++)->onRequestTemporaryError(api, request, e.get());
    }

    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ;)
    {
        (*it++)->onRequestTemporaryError(api, request, e.get());
    }

    MegaRequestListener* listener = request->getListener();
    if(listener)
    {
        listener->onRequestTemporaryError(api, request, e.get());
    }

    activeRequest = NULL;
    activeError = NULL;
}

void MegaApiImpl::fireOnTransferStart(MegaTransferPrivate *transfer)
{
    activeTransfer = transfer;
    notificationNumber++;
    transfer->setNotificationNumber(notificationNumber);

    for(set<MegaTransferListener *>::iterator it = transferListeners.begin(); it != transferListeners.end() ;)
    {
        (*it++)->onTransferStart(api, transfer);
    }

    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ;)
    {
        (*it++)->onTransferStart(api, transfer);
    }

    MegaTransferListener* listener = transfer->getListener();
    if(listener)
    {
        listener->onTransferStart(api, transfer);
    }

    activeTransfer = NULL;
}

void MegaApiImpl::fireOnTransferFinish(MegaTransferPrivate *transfer, unique_ptr<MegaErrorPrivate> e, DBTableTransactionCommitter& committer)
{
    activeTransfer = transfer;
    activeError = e.get();
    notificationNumber++;
    transfer->setNotificationNumber(notificationNumber);
    transfer->setLastError(e.get());

    if(e->getErrorCode())
    {
        LOG_warn << "Transfer (" << transfer->getTransferString() << ") finished with error: " << e->getErrorString()
                    << " File: " << transfer->getFileName();

        if (e->hasExtraInfo() && e->getErrorCode() == API_ETOOMANY)
        {
            LOG_warn << "ETD affected: user status: " << e->getUserStatus() << "  link status: " << e->getLinkStatus();
        }
    }
    else
    {
        LOG_info << "Transfer (" << transfer->getTransferString() << ") finished. File: " << transfer->getFileName();
    }

    for(set<MegaTransferListener *>::iterator it = transferListeners.begin(); it != transferListeners.end() ;)
    {
        (*it++)->onTransferFinish(api, transfer, e.get());
    }

    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ;)
    {
        (*it++)->onTransferFinish(api, transfer, e.get());
    }

    MegaTransferListener* listener = transfer->getListener();
    if(listener)
    {
        listener->onTransferFinish(api, transfer, e.get());
    }

    transferMap.erase(transfer->getTag());
    if (transfer->isFolderTransfer())
    {
        folderTransferMap.erase(transfer->getTag());
    }

    activeTransfer = NULL;
    activeError = NULL;
    delete transfer;  // committer needs to be present for this one, db updated
}

void MegaApiImpl::fireOnTransferTemporaryError(MegaTransferPrivate *transfer, unique_ptr<MegaErrorPrivate> e)
{
    activeTransfer = transfer;
    activeError = e.get();
    notificationNumber++;
    transfer->setNotificationNumber(notificationNumber);

    transfer->setNumRetry(transfer->getNumRetry() + 1);

    for(set<MegaTransferListener *>::iterator it = transferListeners.begin(); it != transferListeners.end() ;)
    {
        (*it++)->onTransferTemporaryError(api, transfer, e.get());
    }

    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ;)
    {
        (*it++)->onTransferTemporaryError(api, transfer, e.get());
    }

    MegaTransferListener* listener = transfer->getListener();
    if(listener)
    {
        listener->onTransferTemporaryError(api, transfer, e.get());
    }

    activeTransfer = NULL;
    activeError = NULL;
}

MegaClient *MegaApiImpl::getMegaClient()
{
    return client;
}

void MegaApiImpl::fireOnTransferUpdate(MegaTransferPrivate *transfer)
{
    activeTransfer = transfer;
    notificationNumber++;
    transfer->setNotificationNumber(notificationNumber);

    for(set<MegaTransferListener *>::iterator it = transferListeners.begin(); it != transferListeners.end() ;)
    {
        (*it++)->onTransferUpdate(api, transfer);
    }

    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ;)
    {
        (*it++)->onTransferUpdate(api, transfer);
    }

    MegaTransferListener* listener = transfer->getListener();
    if(listener)
    {
        listener->onTransferUpdate(api, transfer);
    }

    activeTransfer = NULL;
}

bool MegaApiImpl::fireOnTransferData(MegaTransferPrivate *transfer)
{
    activeTransfer = transfer;
    notificationNumber++;
    transfer->setNotificationNumber(notificationNumber);

    bool result = false;
    MegaTransferListener* listener = transfer->getListener();
    if(listener)
    {
        result = listener->onTransferData(api, transfer, transfer->getLastBytes(), size_t(transfer->getDeltaSize()));
    }

    activeTransfer = NULL;
    return result;
}

void MegaApiImpl::fireOnUsersUpdate(MegaUserList *users)
{
    activeUsers = users;

    for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ;)
    {
        (*it++)->onUsersUpdate(api, users);
    }
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ;)
    {
        (*it++)->onUsersUpdate(api, users);
    }

    activeUsers = NULL;
}

void MegaApiImpl::fireOnUserAlertsUpdate(MegaUserAlertList *userAlerts)
{
    activeUserAlerts = userAlerts;

    for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ;)
    {
        (*it++)->onUserAlertsUpdate(api, userAlerts);
    }
    for (set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end();)
    {
        (*it++)->onUserAlertsUpdate(api, userAlerts);
    }

    activeUserAlerts = NULL;
}

void MegaApiImpl::fireOnContactRequestsUpdate(MegaContactRequestList *requests)
{
    activeContactRequests = requests;

    for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ;)
    {
        (*it++)->onContactRequestsUpdate(api, requests);
    }
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ;)
    {
        (*it++)->onContactRequestsUpdate(api, requests);
    }

    activeContactRequests = NULL;
}

void MegaApiImpl::fireOnNodesUpdate(MegaNodeList *nodes)
{
    activeNodes = nodes;

    for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ;)
    {
        (*it++)->onNodesUpdate(api, nodes);
    }
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ;)
    {
        (*it++)->onNodesUpdate(api, nodes);
    }

    activeNodes = NULL;
}

void MegaApiImpl::fireOnAccountUpdate()
{
    for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ;)
    {
        (*it++)->onAccountUpdate(api);
    }
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ;)
    {
        (*it++)->onAccountUpdate(api);
    }
}

void MegaApiImpl::fireOnReloadNeeded()
{
    for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ;)
    {
        (*it++)->onReloadNeeded(api);
    }

    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ;)
    {
        (*it++)->onReloadNeeded(api);
    }
}

void MegaApiImpl::fireOnEvent(MegaEventPrivate *event)
{
    for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ;)
    {
        (*it++)->onEvent(api, event);
    }

    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ;)
    {
        (*it++)->onEvent(api, event);
    }

    delete event;
}

#ifdef ENABLE_SYNC
void MegaApiImpl::fireOnSyncStateChanged(MegaSyncPrivate *sync)
{
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ;)
    {
        (*it++)->onSyncStateChanged(api, sync);
    }

    for(set<MegaSyncListener *>::iterator it = syncListeners.begin(); it != syncListeners.end() ;)
    {
        (*it++)->onSyncStateChanged(api, sync);
    }

    MegaSyncListener* listener = sync->getListener();
    if(listener)
    {
        listener->onSyncStateChanged(api, sync);
    }
}

void MegaApiImpl::fireOnSyncEvent(MegaSyncPrivate *sync, MegaSyncEvent *event)
{
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ;)
    {
        (*it++)->onSyncEvent(api, sync, event);
    }

    for(set<MegaSyncListener *>::iterator it = syncListeners.begin(); it != syncListeners.end() ;)
    {
        (*it++)->onSyncEvent(api, sync, event);
    }

    MegaSyncListener* listener = sync->getListener();
    if(listener)
    {
        listener->onSyncEvent(api, sync, event);
    }

    delete event;
}

void MegaApiImpl::fireOnGlobalSyncStateChanged()
{
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ;)
    {
        (*it++)->onGlobalSyncStateChanged(api);
    }

    for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ;)
    {
        (*it++)->onGlobalSyncStateChanged(api);
    }
}

void MegaApiImpl::fireOnFileSyncStateChanged(MegaSyncPrivate *sync, string *localPath, int newState)
{
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ;)
    {
        (*it++)->onSyncFileStateChanged(api, sync, localPath, newState);
    }

    for(set<MegaSyncListener *>::iterator it = syncListeners.begin(); it != syncListeners.end() ;)
    {
        (*it++)->onSyncFileStateChanged(api, sync, localPath, newState);
    }

    MegaSyncListener* listener = sync->getListener();
    if(listener)
    {
        listener->onSyncFileStateChanged(api, sync, localPath, newState);
    }
}

#endif

void MegaApiImpl::fireOnBackupStateChanged(MegaBackupController *backup)
{
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ;)
    {
        (*it++)->onBackupStateChanged(api, backup);
    }

    for(set<MegaBackupListener *>::iterator it = backupListeners.begin(); it != backupListeners.end() ;)
    {
        (*it++)->onBackupStateChanged(api, backup);
    }

    MegaBackupListener* listener = backup->getBackupListener();
    if(listener)
    {
        listener->onBackupStateChanged(api, backup);
    }
}


void MegaApiImpl::fireOnBackupStart(MegaBackupController *backup)
{
    for(set<MegaBackupListener *>::iterator it = backupListeners.begin(); it != backupListeners.end() ;)
    {
        (*it++)->onBackupStart(api, backup);
    }

    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ;)
    {
        (*it++)->onBackupStart(api, backup);
    }

    MegaBackupListener* listener = backup->getBackupListener();
    if(listener)
    {
        listener->onBackupStart(api, backup);
    }

}

void MegaApiImpl::fireOnBackupFinish(MegaBackupController *backup, unique_ptr<MegaErrorPrivate> e)
{
    for(set<MegaBackupListener *>::iterator it = backupListeners.begin(); it != backupListeners.end() ;)
    {
        (*it++)->onBackupFinish(api, backup, e.get());
    }

    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ;)
    {
        (*it++)->onBackupFinish(api, backup, e.get());
    }

    MegaBackupListener* listener = backup->getBackupListener();
    if(listener)
    {
        listener->onBackupFinish(api, backup, e.get());
    }
}

void MegaApiImpl::fireOnBackupTemporaryError(MegaBackupController *backup, unique_ptr<MegaErrorPrivate> e)
{
    for(set<MegaBackupListener *>::iterator it = backupListeners.begin(); it != backupListeners.end() ;)
    {
        (*it++)->onBackupTemporaryError(api, backup, e.get());
    }

    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ;)
    {
        (*it++)->onBackupTemporaryError(api, backup, e.get());
    }

    MegaBackupListener* listener = backup->getBackupListener();
    if(listener)
    {
        listener->onBackupTemporaryError(api, backup, e.get());
    }
}

void MegaApiImpl::fireOnBackupUpdate(MegaBackupController *backup)
{
//    notificationNumber++; //TODO: should we use notificationNumber for backups??

    for(set<MegaBackupListener *>::iterator it = backupListeners.begin(); it != backupListeners.end() ;)
    {
        (*it++)->onBackupUpdate(api, backup);
    }

    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ;)
    {
        (*it++)->onBackupUpdate(api, backup);
    }

    MegaBackupListener* listener = backup->getBackupListener();
    if(listener)
    {
        listener->onBackupUpdate(api, backup);
    }
}


#ifdef ENABLE_CHAT

void MegaApiImpl::fireOnChatsUpdate(MegaTextChatList *chats)
{
    for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ;)
    {
        (*it++)->onChatsUpdate(api, chats);
    }
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ;)
    {
        (*it++)->onChatsUpdate(api, chats);
    }
}

#endif

void MegaApiImpl::processTransferPrepare(Transfer *t, MegaTransferPrivate *transfer)
{
    transfer->setTotalBytes(t->size);
    transfer->setState(t->state);
    transfer->setPriority(t->priority);
    LOG_info << "Transfer (" << transfer->getTransferString() << ") starting. File: " << transfer->getFileName();
}

void MegaApiImpl::processTransferUpdate(Transfer *tr, MegaTransferPrivate *transfer)
{
    dstime currentTime = Waiter::ds;
    if (tr->slot)
    {
        m_off_t prevTransferredBytes = transfer->getTransferredBytes();
        m_off_t deltaSize = tr->slot->progressreported - prevTransferredBytes;
        transfer->setStartTime(currentTime);
        transfer->setTransferredBytes(tr->slot->progressreported);
        transfer->setDeltaSize(deltaSize);
        transfer->setSpeed(tr->slot->speed);
        transfer->setMeanSpeed(tr->slot->meanSpeed);

        if (tr->type == GET)
        {
            totalDownloadedBytes += deltaSize;
        }
        else
        {
            totalUploadedBytes += deltaSize;
        }
    }
    else
    {
        transfer->setDeltaSize(0);
        transfer->setSpeed(0);
        transfer->setMeanSpeed(0);
    }

    transfer->setState(tr->state);
    transfer->setPriority(tr->priority);
    transfer->setUpdateTime(currentTime);
    fireOnTransferUpdate(transfer);
}

void MegaApiImpl::processTransferComplete(Transfer *tr, MegaTransferPrivate *transfer)
{
    dstime currentTime = Waiter::ds;
    m_off_t deltaSize = tr->size - transfer->getTransferredBytes();
    transfer->setStartTime(currentTime);
    transfer->setUpdateTime(currentTime);
    transfer->setTransferredBytes(tr->size);
    transfer->setPriority(tr->priority);
    transfer->setDeltaSize(deltaSize);
    transfer->setSpeed(tr->slot ? tr->slot->speed : 0);
    transfer->setMeanSpeed(tr->slot ? tr->slot->meanSpeed : 0);

    if (tr->type == GET)
    {
        totalDownloadedBytes += deltaSize;

        if (pendingDownloads > 0)
        {
            pendingDownloads--;
        }

        transfer->setState(MegaTransfer::STATE_COMPLETED);
        DBTableTransactionCommitter committer(client->tctable);
        fireOnTransferFinish(transfer, make_unique<MegaErrorPrivate>(API_OK), committer);
    }
    else
    {
        totalUploadedBytes += deltaSize;

        transfer->setState(MegaTransfer::STATE_COMPLETING);
        transfer->setTransfer(NULL);
        fireOnTransferUpdate(transfer);
    }
}

void MegaApiImpl::processTransferFailed(Transfer *tr, MegaTransferPrivate *transfer, const Error& e, dstime timeleft)
{
    auto megaError = make_unique<MegaErrorPrivate>(e, timeleft / 10);
    transfer->setStartTime(Waiter::ds);
    transfer->setUpdateTime(Waiter::ds);
    transfer->setDeltaSize(0);
    transfer->setSpeed(0);
    transfer->setMeanSpeed(0);
    transfer->setLastError(megaError.get());
    transfer->setPriority(tr->priority);
    if (e == API_ETOOMANY && e.hasExtraInfo())
    {
        DBTableTransactionCommitter committer(client->tctable);
        transfer->setState(MegaTransfer::STATE_FAILED);
        transfer->setForeignOverquota(false);
        fireOnTransferFinish(transfer, std::move(megaError), committer);
    }
    else
    {
        transfer->setState(MegaTransfer::STATE_RETRYING);
        transfer->setForeignOverquota(e == API_EOVERQUOTA && client->isForeignNode(transfer->getParentHandle()));
        fireOnTransferTemporaryError(transfer, std::move(megaError));
    }

}

void MegaApiImpl::processTransferRemoved(Transfer *tr, MegaTransferPrivate *transfer, const Error& e)
{
    m_off_t deltaSize = tr->size - transfer->getTransferredBytes();
    if (tr->type == GET)
    {
        totalDownloadedBytes += deltaSize;

        if (pendingDownloads > 0)
        {
            pendingDownloads--;
        }

        if (totalDownloads > 0)
        {
            totalDownloads--;
        }
    }
    else
    {
        totalUploadedBytes += deltaSize;

        if (pendingUploads > 0)
        {
            pendingUploads--;
        }

        if (totalUploads > 0)
        {
            totalUploads--;
        }
    }

    transfer->setStartTime(Waiter::ds);
    transfer->setUpdateTime(Waiter::ds);
    transfer->setState(e == API_EINCOMPLETE ? MegaTransfer::STATE_CANCELLED : MegaTransfer::STATE_FAILED);
    transfer->setPriority(tr->priority);
    DBTableTransactionCommitter committer(client->tctable);
    fireOnTransferFinish(transfer, make_unique<MegaErrorPrivate>(e), committer);
}

MegaError MegaApiImpl::checkAccess(MegaNode* megaNode, int level)
{
    std::unique_ptr<MegaError> megaError(checkAccessErrorExtended(megaNode, level));
    return megaError->getErrorCode();
}

MegaError *MegaApiImpl::checkAccessErrorExtended(MegaNode *megaNode, int level)
{
    if(!megaNode || level < MegaShare::ACCESS_UNKNOWN || level > MegaShare::ACCESS_OWNER)
    {
        return new MegaErrorPrivate(API_EARGS);
    }

    SdkMutexGuard g(sdkMutex);
    Node *node = client->nodebyhandle(megaNode->getHandle());
    if(!node)
    {
        return new MegaErrorPrivate(API_ENOENT);
    }

    accesslevel_t a = OWNER;
    switch(level)
    {
        case MegaShare::ACCESS_UNKNOWN:
        case MegaShare::ACCESS_READ:
            a = RDONLY;
            break;
        case MegaShare::ACCESS_READWRITE:
            a = RDWR;
            break;
        case MegaShare::ACCESS_FULL:
            a = FULL;
            break;
        case MegaShare::ACCESS_OWNER:
            a = OWNER;
            break;
    }

    return client->checkaccess(node, a) ? new MegaErrorPrivate(API_OK) : new MegaErrorPrivate(API_EACCESS);
}

MegaError MegaApiImpl::checkMove(MegaNode* megaNode, MegaNode* targetNode)
{
    std::unique_ptr<MegaError> megaError(checkMoveErrorExtended(megaNode, targetNode));
    return megaError->getErrorCode();
}

MegaError *MegaApiImpl::checkMoveErrorExtended(MegaNode *megaNode, MegaNode *targetNode)
{
    if(!megaNode || !targetNode) return new MegaErrorPrivate(API_EARGS);

    SdkMutexGuard g(sdkMutex);
    Node *node = client->nodebyhandle(megaNode->getHandle());
    Node *target = client->nodebyhandle(targetNode->getHandle());
    if(!node || !target)
    {
        return new MegaErrorPrivate(API_ENOENT);
    }

    return new MegaErrorPrivate(client->checkmove(node,target));
}

bool MegaApiImpl::isFilesystemAvailable()
{
    sdkMutex.lock();
    bool result = client->nodebyhandle(client->rootnodes[0]) != NULL;
    sdkMutex.unlock();
    return result;
}

bool isDigit(const char *c)
{
    return (*c >= '0' && *c <= '9');
}

// returns 0 if i==j, +1 if i goes first, -1 if j goes first.
int naturalsorting_compare (const char *i, const char *j)
{
    static uint64_t maxNumber = (ULONG_MAX - 57) / 10; // 57 --> ASCII code for '9'

    bool stringMode = true;

    while (*i && *j)
    {
        if (stringMode)
        {
            char char_i, char_j;
            while ( (char_i = *i) && (char_j = *j) )
            {
                bool char_i_isDigit = isDigit(i);
                bool char_j_isDigit = isDigit(j);

                if (char_i_isDigit && char_j_isDigit)
                {
                    stringMode = false;
                    break;
                }

                if(char_i_isDigit)
                {
                    return -1;
                }

                if(char_j_isDigit)
                {
                    return 1;
                }

                int difference = strncasecmp((char *)&char_i, (char *)&char_j, 1);
                if (difference)
                {
                    return difference;
                }

                ++i;
                ++j;
            }
        }
        else    // we are comparing numbers on both strings
        {
            uint64_t number_i = 0;
            unsigned int i_overflow_count = 0;
            while (*i && isDigit(i))
            {
                number_i = number_i * 10 + (*i - 48); // '0' ASCII code is 48
                ++i;

                // check the number won't overflow upon addition of next char
                if (number_i >= maxNumber)
                {
                    number_i -= maxNumber;
                    i_overflow_count++;
                }
            }

            uint64_t number_j = 0;
            unsigned int j_overflow_count = 0;
            while (*j && isDigit(j))
            {
                number_j = number_j * 10 + (*j - 48);
                ++j;

                // check the number won't overflow upon addition of next char
                if (number_j >= maxNumber)
                {
                    number_j -= maxNumber;
                    j_overflow_count++;
                }
            }

            int difference = i_overflow_count - j_overflow_count;
            if (difference)
            {
                return difference;
            }

            if (number_i != number_j)
            {
                return number_i > number_j ? 1 : -1;
            }

            stringMode = true;
        }
    }

    if (*j)
    {
        return -1;
    }

    if (*i)
    {
        return 1;
    }

    return 0;
}

std::function<bool (Node*, Node*)> MegaApiImpl::getComparatorFunction(int order, MegaClient& mc)
{
    switch (order)
    {
        case MegaApi::ORDER_NONE: return nullptr;
        case MegaApi::ORDER_DEFAULT_ASC: return MegaApiImpl::nodeComparatorDefaultASC;
        case MegaApi::ORDER_DEFAULT_DESC: return MegaApiImpl::nodeComparatorDefaultDESC;
        case MegaApi::ORDER_SIZE_ASC: return MegaApiImpl::nodeComparatorSizeASC;
        case MegaApi::ORDER_SIZE_DESC: return MegaApiImpl::nodeComparatorSizeDESC;
        case MegaApi::ORDER_CREATION_ASC: return MegaApiImpl::nodeComparatorCreationASC;
        case MegaApi::ORDER_CREATION_DESC: return MegaApiImpl::nodeComparatorCreationDESC;
        case MegaApi::ORDER_MODIFICATION_ASC: return MegaApiImpl::nodeComparatorModificationASC;
        case MegaApi::ORDER_MODIFICATION_DESC: return MegaApiImpl::nodeComparatorModificationDESC;
        case MegaApi::ORDER_ALPHABETICAL_ASC: return MegaApiImpl::nodeComparatorDefaultASC;
        case MegaApi::ORDER_ALPHABETICAL_DESC: return MegaApiImpl::nodeComparatorDefaultDESC;
        case MegaApi::ORDER_LINK_CREATION_ASC: return MegaApiImpl::nodeComparatorPublicLinkCreationASC;
        case MegaApi::ORDER_LINK_CREATION_DESC: return MegaApiImpl::nodeComparatorPublicLinkCreationDESC;
        case MegaApi::ORDER_PHOTO_ASC: return [&mc](Node* i, Node*j) { return MegaApiImpl::nodeComparatorPhotoASC(i, j, mc); };
        case MegaApi::ORDER_PHOTO_DESC: return [&mc](Node* i, Node*j) { return MegaApiImpl::nodeComparatorPhotoDESC(i, j, mc); };
        case MegaApi::ORDER_VIDEO_ASC: return [&mc](Node* i, Node*j) { return MegaApiImpl::nodeComparatorVideoASC(i, j, mc); };
        case MegaApi::ORDER_VIDEO_DESC: return [&mc](Node* i, Node*j) { return MegaApiImpl::nodeComparatorVideoDESC(i, j, mc); };
    }
    assert(false);
    return nullptr;
}

void MegaApiImpl::sortByComparatorFunction(node_vector& v, int order, MegaClient& mc)
{
    if (auto f = getComparatorFunction(order, mc))
    {
        std::sort(v.begin(), v.end(), f);
    }
}

bool MegaApiImpl::nodeNaturalComparatorASC(Node *i, Node *j)
{
    int r = naturalsorting_compare(i->displayname(), j->displayname());
    if (r < 0)
    {
        return 1;
    }
    return 0;
}

bool MegaApiImpl::nodeNaturalComparatorDESC(Node *i, Node *j)
{
    int r = naturalsorting_compare(i->displayname(), j->displayname());
    if (r <= 0)
    {
        return 0;
    }
    return 1;
}

bool MegaApiImpl::nodeComparatorDefaultASC(Node *i, Node *j)
{
    int t = typeComparator(i, j);
    if (t >= 0)
    {
        return t;
    }

    return nodeNaturalComparatorASC(i, j);
}

bool MegaApiImpl::nodeComparatorDefaultDESC(Node *i, Node *j)
{
    int t = typeComparator(i, j);
    if (t >= 0)
    {
        return t;
    }

    return nodeNaturalComparatorDESC(i, j);
}

bool MegaApiImpl::nodeComparatorSizeASC(Node *i, Node *j)
{
    int t = typeComparator(i, j);
    if (t >= 0)
    {
        return t;
    }

    if (i->type != FILENODE) // Only file nodes have size
    {
        // If node doesn't have size, order alphabetically ascending
        return nodeNaturalComparatorASC(i, j);
    }

    m_off_t r = i->size - j->size;
    if (r < 0)
    {
        return 1;
    }
    if (r > 0)
    {
        return 0;
    }
    return nodeNaturalComparatorASC(i, j);
}

bool MegaApiImpl::nodeComparatorSizeDESC(Node *i, Node *j)
{
    int t = typeComparator(i, j);
    if (t >= 0)
    {
        return t;
    }

    if (i->type != FILENODE) // Only file nodes have size
    {
        // If node doesn't have size, order alphabetically ascending
        return nodeNaturalComparatorASC(i, j);
    }

    m_off_t r = i->size - j->size;
    if (r < 0)
    {
        return 0;
    }
    if (r > 0)
    {
        return 1;
    }
    return nodeNaturalComparatorDESC(i, j);
}

bool MegaApiImpl::nodeComparatorCreationASC(Node *i, Node *j)
{
    int t = typeComparator(i, j);
    if (t >= 0)
    {
        return t;
    }
    if (i->ctime < j->ctime)
    {
        return 1;
    }
    if (i->ctime > j->ctime)
    {
        return 0;
    }
    return nodeNaturalComparatorASC(i, j);
}

bool MegaApiImpl::nodeComparatorCreationDESC(Node *i, Node *j)
{
    int t = typeComparator(i, j);
    if (t >= 0)
    {
        return t;
    }
    if (i->ctime < j->ctime)
    {
        return 0;
    }
    if (i->ctime > j->ctime)
    {
        return 1;
    }
    return nodeNaturalComparatorDESC(i, j);
}

bool MegaApiImpl::nodeComparatorModificationASC(Node *i, Node *j)
{
    int t = typeComparator(i, j);
    if (t >= 0)
    {
        return t;
    }

    if (i->type != FILENODE) // Only file nodes have last modified date
    {
        // If node doesn't have mtime, order alphabetically ascending
        return nodeNaturalComparatorASC(i, j);
    }

    m_time_t r = i->mtime - j->mtime;
    if (r < 0)
    {
        return 1;
    }
    if (r > 0)
    {
        return 0;
    }
    return nodeNaturalComparatorASC(i, j);
}

bool MegaApiImpl::nodeComparatorModificationDESC(Node *i, Node *j)
{
    int t = typeComparator(i, j);
    if (t >= 0)
    {
        return t;
    }

    if (i->type != FILENODE)
    {
        // If node doesn't have mtime, order alphabetically ascending
        return nodeNaturalComparatorASC(i, j);
    }

    m_time_t r = i->mtime - j->mtime;
    if (r < 0)
    {
        return 0;
    }
    if (r > 0)
    {
        return 1;
    }

    return nodeNaturalComparatorDESC(i, j);
}

bool MegaApiImpl::nodeComparatorPublicLinkCreationASC(Node *i, Node *j)
{
    int t = typeComparator(i, j);
    if (t >= 0)
    {
        return t;
    }
    if (!i->plink || !j->plink)
    {
        return nodeNaturalComparatorASC(i, j);
    }
    if (i->plink->cts < j->plink->cts)
    {
        return 1;
    }
    if (i->plink->cts > j->plink->cts)
    {
        return 0;
    }
    return nodeNaturalComparatorASC(i, j);
}

bool MegaApiImpl::nodeComparatorPublicLinkCreationDESC(Node *i, Node *j)
{
    int t = typeComparator(i, j);
    if (t >= 0)
    {
        return t;
    }
    if (!i->plink || !j->plink)
    {
        return nodeNaturalComparatorDESC(i, j);
    }
    if (i->plink->cts < j->plink->cts)
    {
        return 0;
    }
    if (i->plink->cts > j->plink->cts)
    {
        return 1;
    }
    return nodeNaturalComparatorDESC(i, j);
}

// Compare node types. Returns -1 if i==j, 0 if i goes first, +1 if j goes first.
int MegaApiImpl::typeComparator(Node *i, Node *j)
{
    if (i->type < j->type)
    {
        return 0;
    }
    if (i->type > j->type)
    {
        return 1;
    }
    return -1;
}

bool MegaApiImpl::nodeComparatorPhotoASC(Node *i, Node *j, MegaClient& mc)
{
    bool i_photo = false, i_video = false, j_photo = false, j_video = false;
    bool i_media = mc.nodeIsMedia(i, &i_photo, &i_video);
    bool j_media = mc.nodeIsMedia(j, &j_photo, &j_video);

    if (i_media != j_media)
    {
        return i_media;  // media go first
    }

    if (i_photo != j_photo)
    {
        return i_photo; // photos before videos
    }

    // within photos or videos or non-media, order by date
    return nodeComparatorModificationASC(i, j);
}

bool MegaApiImpl::nodeComparatorPhotoDESC(Node *i, Node *j, MegaClient& mc)
{
    bool i_photo = false, i_video = false, j_photo = false, j_video = false;
    bool i_media = mc.nodeIsMedia(i, &i_photo, &i_video);
    bool j_media = mc.nodeIsMedia(j, &j_photo, &j_video);

    if (i_media != j_media)
    {
        return i_media;  // media go first
    }

    if (i_photo != j_photo)
    {
        return i_photo; // photos before videos
    }

    // within photos or videos or non-media, order by date
    return nodeComparatorModificationDESC(i, j);
}

bool MegaApiImpl::nodeComparatorVideoASC(Node *i, Node *j, MegaClient& mc)
{
    bool i_photo = false, i_video = false, j_photo = false, j_video = false;
    bool i_media = mc.nodeIsMedia(i, &i_photo, &i_video);
    bool j_media = mc.nodeIsMedia(j, &j_photo, &j_video);

    if (i_media != j_media)
    {
        return i_media;  // media go first
    }

    if (i_video != j_video) 
    {
        return i_video; // videos before photos
    }

    // within photos or videos or non-media, order by date
    return nodeComparatorModificationASC(i, j);
}

bool MegaApiImpl::nodeComparatorVideoDESC(Node *i, Node *j, MegaClient& mc)
{
    bool i_photo = false, i_video = false, j_photo = false, j_video = false;
    bool i_media = mc.nodeIsMedia(i, &i_photo, &i_video);
    bool j_media = mc.nodeIsMedia(j, &j_photo, &j_video);

    if (i_media != j_media)
    {
        return i_media;  // media go first
    }

    if (i_video != j_video)
    {
        return i_video; // videos before photos
    }

    // within photos or videos or non-media, order by date
    return nodeComparatorModificationDESC(i, j);
}

int MegaApiImpl::getNumChildren(MegaNode* p)
{
    if (!p || p->getType() == MegaNode::TYPE_FILE)
    {
        return 0;
    }

    sdkMutex.lock();
    Node *parent = client->nodebyhandle(p->getHandle());
    if (!parent || parent->type == FILENODE)
    {
        sdkMutex.unlock();
        return 0;
    }

    int numChildren = int(parent->children.size());
    sdkMutex.unlock();

    return numChildren;
}

int MegaApiImpl::getNumChildFiles(MegaNode* p)
{
    if (!p || p->getType() == MegaNode::TYPE_FILE)
    {
        return 0;
    }

    sdkMutex.lock();
    Node *parent = client->nodebyhandle(p->getHandle());
    if (!parent || parent->type == FILENODE)
    {
        sdkMutex.unlock();
        return 0;
    }

    int numFiles = 0;
    for (node_list::iterator it = parent->children.begin(); it != parent->children.end(); it++)
    {
        if ((*it)->type == FILENODE)
            numFiles++;
    }
    sdkMutex.unlock();

    return numFiles;
}

int MegaApiImpl::getNumChildFolders(MegaNode* p)
{
    if (!p || p->getType() == MegaNode::TYPE_FILE)
    {
        return 0;
    }

    sdkMutex.lock();
    Node *parent = client->nodebyhandle(p->getHandle());
    if (!parent || parent->type == FILENODE)
    {
        sdkMutex.unlock();
        return 0;
    }

    int numFolders = 0;
    for (node_list::iterator it = parent->children.begin(); it != parent->children.end(); it++)
    {
        if ((*it)->type != FILENODE)
            numFolders++;
    }
    sdkMutex.unlock();

    return numFolders;
}


MegaNodeList *MegaApiImpl::getChildren(MegaNode* p, int order)
{
    if (!p || p->getType() == MegaNode::TYPE_FILE)
    {
        return new MegaNodeListPrivate();
    }

    node_vector childrenNodes;

    SdkMutexGuard guard(sdkMutex);

    Node *parent = client->nodebyhandle(p->getHandle());
    if (parent && parent->type != FILENODE)
    {
        childrenNodes.reserve(parent->children.size());
        for (node_list::iterator it = parent->children.begin(); it != parent->children.end(); )
        {
            childrenNodes.push_back(*it++);
        }
        if (std::function<bool(Node*, Node*)> comparatorFunction = getComparatorFunction(order, *client))
        {
            std::sort(childrenNodes.begin(), childrenNodes.end(), comparatorFunction);
        }
    }
    return new MegaNodeListPrivate(childrenNodes.data(), int(childrenNodes.size()));
}

MegaNodeList *MegaApiImpl::getVersions(MegaNode *node)
{
    if (!node || node->getType() != MegaNode::TYPE_FILE)
    {
        return new MegaNodeListPrivate();
    }

    sdkMutex.lock();
    Node *current = client->nodebyhandle(node->getHandle());
    if (!current || current->type != FILENODE)
    {
        sdkMutex.unlock();
        return new MegaNodeListPrivate();
    }

    vector<Node*> versions;
    versions.push_back(current);
    while (current->children.size())
    {
        assert(current->children.back()->parent == current);
        current = current->children.back();
        assert(current->type == FILENODE);
        versions.push_back(current);
    }

    MegaNodeListPrivate *result = new MegaNodeListPrivate(versions.data(), int(versions.size()));
    sdkMutex.unlock();
    return result;
}

int MegaApiImpl::getNumVersions(MegaNode *node)
{
    if (!node || node->getType() != MegaNode::TYPE_FILE)
    {
        return 0;
    }

    sdkMutex.lock();
    Node *current = client->nodebyhandle(node->getHandle());
    if (!current || current->type != FILENODE)
    {
        sdkMutex.unlock();
        return 0;
    }

    int numVersions = 1;
    while (current->children.size())
    {
        assert(current->children.back()->parent == current);
        current = current->children.back();
        assert(current->type == FILENODE);
        numVersions++;
    }
    sdkMutex.unlock();
    return numVersions;
}

bool MegaApiImpl::hasVersions(MegaNode *node)
{
    if (!node || node->getType() != MegaNode::TYPE_FILE)
    {
        return false;
    }

    sdkMutex.lock();
    Node *current = client->nodebyhandle(node->getHandle());
    if (!current || current->type != FILENODE)
    {
        sdkMutex.unlock();
        return false;
    }

    assert(!current->children.size()
           || (current->children.back()->parent == current
               && current->children.back()->type == FILENODE));

    bool result = current->children.size() != 0;
    sdkMutex.unlock();
    return result;
}

void MegaApiImpl::getFolderInfo(MegaNode *node, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_FOLDER_INFO, listener);
    if (node)
    {
        request->setNodeHandle(node->getHandle());
    }
    requestQueue.push(request);
    waiter->notify();
}

MegaChildrenLists *MegaApiImpl::getFileFolderChildren(MegaNode *p, int order)
{
    if (!p || p->getType() == MegaNode::TYPE_FILE)
    {
        return new MegaChildrenListsPrivate();
    }

    SdkMutexGuard guard(sdkMutex);

    Node *parent = client->nodebyhandle(p->getHandle());
    if (!parent || parent->type == FILENODE)
    {
        return new MegaChildrenListsPrivate();
    }

    node_vector files;
    node_vector folders;

    for (node_list::iterator it = parent->children.begin(); it != parent->children.end(); )
    {
        Node *n = *it++;
        if (n->type == FILENODE)
        {
            files.push_back(n);
        }
        else // if (n->type == FOLDERNODE)
        {
            folders.push_back(n);
        }
    }
    if (std::function<bool(Node*, Node*)> comparatorFunction = getComparatorFunction(order, *client))
    {
        std::sort(files.begin(), files.end(), comparatorFunction);
        std::sort(folders.begin(), folders.end(), comparatorFunction);
    }

    auto fileList = make_unique<MegaNodeListPrivate>(files.data(), int(files.size()));
    auto folderList = make_unique<MegaNodeListPrivate>(folders.data(), int(folders.size()));
    return new MegaChildrenListsPrivate(move(folderList), move(fileList));
}

bool MegaApiImpl::hasChildren(MegaNode *parent)
{
    if (!parent || parent->getType() == MegaNode::TYPE_FILE)
    {
        return false;
    }

    sdkMutex.lock();
    Node *p = client->nodebyhandle(parent->getHandle());
    if (!p || p->type == FILENODE)
    {
        sdkMutex.unlock();
        return false;
    }

    bool ret = p->children.size();
    sdkMutex.unlock();

    return ret;
}

MegaNode *MegaApiImpl::getChildNode(MegaNode *parent, const char* name)
{
    if (!parent || !name || parent->getType() == MegaNode::TYPE_FILE)
    {
        return NULL;
    }

    sdkMutex.lock();
    Node *parentNode = client->nodebyhandle(parent->getHandle());
    if (!parentNode || parentNode->type == FILENODE)
    {
        sdkMutex.unlock();
        return NULL;
    }

    MegaNode *node = MegaNodePrivate::fromNode(client->childnodebyname(parentNode, name));
    sdkMutex.unlock();
    return node;
}

Node *MegaApiImpl::getNodeByFingerprintInternal(const char *fingerprint)
{
    FileFingerprint *fp = MegaApiImpl::getFileFingerprintInternal(fingerprint);
    if (!fp)
    {
        return NULL;
    }

    sdkMutex.lock();
    Node *n  = client->nodebyfingerprint(fp);
    sdkMutex.unlock();

    delete fp;
    return n;
}

Node *MegaApiImpl::getNodeByFingerprintInternal(const char *fingerprint, Node *parent)
{

    FileFingerprint *fp = MegaApiImpl::getFileFingerprintInternal(fingerprint);
    if (!fp)
    {
        return NULL;
    }

    Node *n = NULL;
    sdkMutex.lock();
    node_vector *nodes = client->nodesbyfingerprint(fp);
    if (nodes->size())
    {
        n = nodes->at(0);
    }

    if (n && parent && n->parent != parent)
    {
        for (unsigned int i = 1; i < nodes->size(); i++)
        {
            Node* node = nodes->at(i);
            if (node->parent == parent)
            {
                n = node;
                break;
            }
        }
    }
    delete fp;
    delete nodes;
    sdkMutex.unlock();

    return n;
}

FileFingerprint *MegaApiImpl::getFileFingerprintInternal(const char *fingerprint)
{
    if(!fingerprint || !fingerprint[0])
    {
        return NULL;
    }

    m_off_t size = 0;
    unsigned int fsize = unsigned(strlen(fingerprint));
    unsigned int ssize = fingerprint[0] - 'A';
    if(ssize > (sizeof(size) * 4 / 3 + 4) || fsize <= (ssize + 1))
    {
        return NULL;
    }

    int len =  sizeof(size) + 1;
    byte *buf = new byte[len];
    Base64::atob(fingerprint + 1, buf, len);
    int l = Serialize64::unserialize(buf, len, (uint64_t *)&size);
    delete [] buf;
    if(l <= 0)
    {
        return NULL;
    }

    string sfingerprint = fingerprint + ssize + 1;

    FileFingerprint *fp = new FileFingerprint;
    if(!fp->unserializefingerprint(&sfingerprint))
    {
        delete fp;
        return NULL;
    }

    fp->size = size;

    return fp;
}

char *MegaApiImpl::getMegaFingerprintFromSdkFingerprint(const char *sdkFingerprint)
{
    if (!sdkFingerprint || !sdkFingerprint[0])
    {
        return NULL;
    }

    unsigned int sizelen = sdkFingerprint[0] - 'A';
    if (sizelen > (sizeof(m_off_t) * 4 / 3 + 4) || strlen(sdkFingerprint) <= (sizelen + 1))
    {
        return NULL;
    }

    FileFingerprint ffp;
    string result = sdkFingerprint + sizelen + 1;
    if (!ffp.unserializefingerprint(&result))
    {
        return NULL;
    }
    return MegaApi::strdup(result.c_str());
}

char *MegaApiImpl::getSdkFingerprintFromMegaFingerprint(const char *megaFingerprint, m_off_t size)
{
    if (!megaFingerprint || !megaFingerprint[0] || size < 0)
    {
        return NULL;
    }

    FileFingerprint ffp;
    string sMegaFingerprint = megaFingerprint;
    if (!ffp.unserializefingerprint(&sMegaFingerprint))
    {
        return NULL;
    }

    char bsize[sizeof(size) + 1];
    int l = Serialize64::serialize((byte *)bsize, size);
    char *buf = new char[l * 4 / 3 + 4];
    char sizelen = static_cast<char>('A' + Base64::btoa((const byte *)bsize, l, buf));
    string result(1, sizelen);
    result.append(buf);
    result.append(megaFingerprint);
    delete [] buf;

    return MegaApi::strdup(result.c_str());
}

MegaNode* MegaApiImpl::getParentNode(MegaNode* n)
{
    if(!n) return NULL;

    sdkMutex.lock();
    Node *node = client->nodebyhandle(n->getHandle());
    if(!node)
    {
        sdkMutex.unlock();
        return NULL;
    }

    MegaNode *result = MegaNodePrivate::fromNode(node->parent);
    sdkMutex.unlock();

    return result;
}

char* MegaApiImpl::getNodePath(MegaNode *node)
{
    if(!node) return NULL;

    sdkMutex.lock();
    Node *n = client->nodebyhandle(node->getHandle());
    if(!n)
    {
        sdkMutex.unlock();
        return NULL;
    }

    string path = n->displaypath();
    sdkMutex.unlock();

    return stringToArray(path);
}

MegaNode* MegaApiImpl::getNodeByPath(const char *path, MegaNode* node)
{
    if(!path) return NULL;

    sdkMutex.lock();
    Node *cwd = NULL;
    if(node) cwd = client->nodebyhandle(node->getHandle());

    vector<string> c;
    string s;
    int l = 0;
    const char* bptr = path;
    int remote = 0;
    Node* n = nullptr;
    Node* nn;

    // split path by / or :
    do {
        if (!l)
        {
            if (*(const signed char*)path >= 0)
            {
                if (*path == '\\')
                {
                    if (path > bptr)
                    {
                        s.append(bptr, path - bptr);
                    }

                    bptr = ++path;

                    if (*bptr == 0)
                    {
                        c.push_back(s);
                        break;
                    }

                    path++;
                    continue;
                }

                if (*path == '/' || *path == ':' || !*path)
                {
                    if (*path == ':')
                    {
                        if (c.size())
                        {
                            sdkMutex.unlock();
                            return NULL;
                        }
                        remote = 1;
                    }

                    if (path > bptr)
                    {
                        s.append(bptr, path - bptr);
                    }

                    bptr = path + 1;

                    c.push_back(s);

                    s.erase();
                }
            }
            else if ((*path & 0xf0) == 0xe0)
            {
                l = 1;
            }
            else if ((*path & 0xf8) == 0xf0)
            {
                l = 2;
            }
            else if ((*path & 0xfc) == 0xf8)
            {
                l = 3;
            }
            else if ((*path & 0xfe) == 0xfc)
            {
                l = 4;
            }
        }
        else
        {
            l--;
        }
    } while (*path++);

    if (l)
    {
        sdkMutex.unlock();
        return NULL;
    }

    if (remote)
    {
        // target: user inbox - it's not a node - return NULL
        if (c.size() == 2 && !c[1].size())
        {
            sdkMutex.unlock();
            return NULL;
        }

        User* u;

        if ((u = client->finduser(c[0].c_str())))
        {
            // locate matching share from this user
            handle_set::iterator sit;
            string name;
            for (sit = u->sharing.begin(); sit != u->sharing.end(); sit++)
            {
                if ((n = client->nodebyhandle(*sit)))
                {
                    if(!name.size())
                    {
                        name =  c[1];
                        n->client->fsaccess->normalize(&name);
                    }

                    if (!strcmp(name.c_str(), n->displayname()))
                    {
                        l = 2;
                        break;
                    }
                }
            }
        }

        if (!l)
        {
            sdkMutex.unlock();
            return NULL;
        }
    }
    else
    {
        // path starting with /
        if (c.size() > 1 && !c[0].size())
        {
            // path starting with //
            if (c.size() > 2 && !c[1].size())
            {
                if (c[2] == "in")
                {
                    n = client->nodebyhandle(client->rootnodes[1]);
                }
                else if (c[2] == "bin")
                {
                    n = client->nodebyhandle(client->rootnodes[2]);
                }
                else
                {
                    sdkMutex.unlock();
                    return NULL;
                }

                l = 3;
            }
            else
            {
                n = client->nodebyhandle(client->rootnodes[0]);
                l = 1;
            }
        }
        else
        {
            n = cwd;
        }
    }

    // parse relative path
    while (n && l < (int)c.size())
    {
        if (c[l] != ".")
        {
            if (c[l] == "..")
            {
                if (n->parent)
                {
                    n = n->parent;
                }
            }
            else
            {
                // locate child node (explicit ambiguity resolution: not implemented)
                if (c[l].size())
                {
                    nn = client->childnodebyname(n, c[l].c_str());

                    if (!nn)
                    {
                        sdkMutex.unlock();
                        return NULL;
                    }

                    n = nn;
                }
            }
        }

        l++;
    }

    MegaNode *result = MegaNodePrivate::fromNode(n);
    sdkMutex.unlock();
    return result;
}

MegaNode* MegaApiImpl::getNodeByHandle(handle handle)
{
    if(handle == UNDEF) return NULL;
    sdkMutex.lock();
    MegaNode *result = MegaNodePrivate::fromNode(client->nodebyhandle(handle));
    sdkMutex.unlock();
    return result;
}

MegaContactRequest *MegaApiImpl::getContactRequestByHandle(MegaHandle handle)
{
    sdkMutex.lock();
    if(client->pcrindex.find(handle) == client->pcrindex.end())
    {
        sdkMutex.unlock();
        return NULL;
    }
    MegaContactRequest* request = MegaContactRequestPrivate::fromContactRequest(client->pcrindex.at(handle));
    sdkMutex.unlock();
    return request;
}

void MegaApiImpl::updateBackups()
{
    for (std::map<int, MegaBackupController *>::iterator it = backupsMap.begin(); it != backupsMap.end(); ++it)
    {
        MegaBackupController *backupController=it->second;
        backupController->update();
    }
}

unsigned MegaApiImpl::sendPendingTransfers()
{
    auto t0 = std::chrono::steady_clock::now();
    unsigned count = 0;

    SdkMutexGuard guard(sdkMutex);
    DBTableTransactionCommitter committer(client->tctable);

    while(MegaTransferPrivate *transfer = transferQueue.pop())
    {
        error e = API_OK;
        int nextTag = client->nextreqtag();
        transfer->setState(MegaTransfer::STATE_QUEUED);

        switch(transfer->getType())
        {
            case MegaTransfer::TYPE_UPLOAD:
            {
                const char* localPath = transfer->getPath();
                const char* fileName = transfer->getFileName();
                int64_t mtime = transfer->getTime();
                bool isSourceTemporary = transfer->isSourceFileTemporary();
                Node *parent = client->nodebyhandle(transfer->getParentHandle());
                bool startFirst = transfer->shouldStartFirst();

                bool uploadToInbox = ISUNDEF(transfer->getParentHandle()) && transfer->getParentPath() && (strchr(transfer->getParentPath(), '@') || (strlen(transfer->getParentPath()) == 11));
                const char *inboxTarget = uploadToInbox ? transfer->getParentPath() : nullptr;

                if (!localPath || !fileName || !(*fileName)
                        || (!uploadToInbox && (!parent || parent->type == FILENODE) ) )
                {
                    e = API_EARGS;
                    break;
                }

                string tmpString = localPath;
                auto wLocalPath = LocalPath::fromPath(tmpString, *client->fsaccess);

                auto fa = fsAccess->newfileaccess();
                if (!fa->fopen(wLocalPath, true, false))
                {
                    e = API_EREAD;
                    break;
                }

                nodetype_t type = fa->type;
                if (type == FOLDERNODE && uploadToInbox)
                {
                    //Folder upload is not possible when sending to Inbox:
                    //API won't return handle for folder creation, and even if that was the case
                    //doing a put nodes with t = userhandle & the corresponding handle as parent p, API returns EACCESS
                    e = API_EREAD;
                    break;
                }
                m_off_t size = fa->size;
                FileFingerprint fp;
                if (type == FILENODE)
                {
                    fp.genfingerprint(fa.get());
                }
                fa.reset();

                if (type == FILENODE)
                {
                    Node *previousNode = client->childnodebyname(parent, fileName, true);

                    bool forceToUpload = false;
                    if (previousNode && previousNode->type == type)
                    {
                        if (fp.isvalid && previousNode->isvalid && fp == *((FileFingerprint *)previousNode))
                        {
                            forceToUpload= hasToForceUpload(*previousNode, *transfer);
                            if (!forceToUpload)
                            {
                                pendingUploads++;
                                totalUploads++;
                                transfer->setState(MegaTransfer::STATE_QUEUED);
                                transferMap[nextTag] = transfer;
                                transfer->setTag(nextTag);
                                transfer->setTotalBytes(size);
                                transfer->setTransferredBytes(0);
                                transfer->setStartTime(Waiter::ds);
                                transfer->setUpdateTime(Waiter::ds);
                                fireOnTransferStart(transfer);
                                transfer->setNodeHandle(previousNode->nodehandle);
                                transfer->setDeltaSize(size);
                                transfer->setSpeed(0);
                                transfer->setMeanSpeed(0);
                                transfer->setState(MegaTransfer::STATE_COMPLETED);
                                pendingUploads--;
                                fireOnTransferFinish(transfer, make_unique<MegaErrorPrivate>(API_OK), committer);
                                break;
                            }
                        }
                    }

                    // If has been found by name and it's necessary force upload, it isn't necessary look for it again
                    if (!forceToUpload)
                    {
                        Node *samenode = client->nodebyfingerprint(&fp);
                        if (samenode && samenode->nodekey().size() && !hasToForceUpload(*samenode, *transfer))
                        {
                            pendingUploads++;
                            totalUploads++;
                            transfer->setState(MegaTransfer::STATE_QUEUED);
                            transferMap[nextTag] = transfer;
                            transfer->setTag(nextTag);
                            transfer->setTotalBytes(size);
                            transfer->setStartTime(Waiter::ds);
                            transfer->setUpdateTime(Waiter::ds);
                            fireOnTransferStart(transfer);

                            unsigned nc;
                            TreeProcCopy tc;
                            client->proctree(samenode, &tc, false, true);
                            tc.allocnodes();
                            nc = tc.nc;
                            client->proctree(samenode, &tc, false, true);
                            tc.nn->parenthandle = UNDEF;

                            SymmCipher key;
                            AttrMap attrs;
                            string attrstring;
                            key.setkey((const byte*)tc.nn[0].nodekey.data(), samenode->type);
                            attrs = samenode->attrs;
                            string sname = fileName;
                            fsAccess->normalize(&sname);
                            attrs.map['n'] = sname;
                            attrs.getjson(&attrstring);
                            client->makeattr(&key, tc.nn[0].attrstring, attrstring.c_str());
                            if (tc.nn->type == FILENODE && !client->versions_disabled)
                            {
                                tc.nn->ovhandle = client->getovhandle(parent, &sname);
                            }

                            if (uploadToInbox)
                            {
                                client->putnodes(inboxTarget, tc.nn, nc);
                            }
                            else
                            {
                                client->putnodes(parent->nodehandle, tc.nn, nc);
                            }

                            transfer->setDeltaSize(size);
                            transfer->setSpeed(0);
                            transfer->setMeanSpeed(0);
                            transfer->setState(MegaTransfer::STATE_COMPLETING);
                            fireOnTransferUpdate(transfer);
                            break;
                        }

                    }

                    currentTransfer = transfer;
                    string wFileName = fileName;
                    MegaFilePut *f = new MegaFilePut(client, std::move(wLocalPath), &wFileName, transfer->getParentHandle(), uploadToInbox ? inboxTarget : "", mtime, isSourceTemporary);
                    *static_cast<FileFingerprint*>(f) = fp;  // deliberate slicing - startxfer would re-fingerprint if we don't supply this info
                    f->setTransfer(transfer);
                    bool started = client->startxfer(PUT, f, committer, true, startFirst, transfer->isBackupTransfer());
                    if (!started)
                    {
                        transfer->setState(MegaTransfer::STATE_QUEUED);
                        if (!f->isvalid)
                        {
                            //Unable to read the file
                            transferMap[nextTag] = transfer;
                            transfer->setTag(nextTag);
                            fireOnTransferStart(transfer);
                            transfer->setStartTime(Waiter::ds);
                            transfer->setUpdateTime(Waiter::ds);
                            transfer->setState(MegaTransfer::STATE_FAILED);
                            fireOnTransferFinish(transfer, make_unique<MegaErrorPrivate>(API_EREAD), committer);
                        }
                        else
                        {
                            MegaTransferPrivate* prevTransfer = NULL;
                            transfer_map::iterator it = client->transfers[PUT].find(f);
                            if (it != client->transfers[PUT].end())
                            {
                                Transfer *t = it->second;
                                for (file_list::iterator fi = t->files.begin(); fi != t->files.end(); fi++)
                                {
                                    if (f->h != UNDEF && f->h == (*fi)->h && !f->targetuser.size()
                                            && !(*fi)->targetuser.size() && f->name == (*fi)->name)
                                    {
                                        prevTransfer = getMegaTransferPrivate((*fi)->tag);
                                        break;
                                    }
                                }
                            }

                            if (prevTransfer && transfer->getAppData())
                            {
                                string appData = prevTransfer->getAppData() ? string(prevTransfer->getAppData()) + "!" : string();
                                appData.append(transfer->getAppData());
                                prevTransfer->setAppData(appData.c_str());
                            }

                            //Already existing transfer
                            transferMap[nextTag] = transfer;
                            transfer->setTag(nextTag);
                            transfer->setTotalBytes(f->size);
                            fireOnTransferStart(transfer);
                            transfer->setStartTime(Waiter::ds);
                            transfer->setUpdateTime(Waiter::ds);
                            transfer->setState(MegaTransfer::STATE_CANCELLED);
                            fireOnTransferFinish(transfer, make_unique<MegaErrorPrivate>(API_EEXIST), committer);
                        }
                    }
                    currentTransfer = NULL;
                }
                else
                {
                    transferMap[nextTag] = transfer;
                    folderTransferMap[nextTag] = transfer;
                    transfer->setTag(nextTag);
                    transfer->startRecursiveOperation(make_unique<MegaFolderUploadController>(this, transfer), nullptr);
                }
                break;
            }
            case MegaTransfer::TYPE_DOWNLOAD:
            {
                Node *node = NULL;
                MegaNode *publicNode = transfer->getPublicNode();
                const char *parentPath = transfer->getParentPath();
                const char *fileName = transfer->getFileName();
                bool startFirst = transfer->shouldStartFirst();

                if (!publicNode)
                {
                    handle nodehandle = transfer->getNodeHandle();
                    node = client->nodebyhandle(nodehandle);
                }

                if (!node && !publicNode)
                {
                    e = API_ENOENT;
                    break;
                }

                if (!transfer->isStreamingTransfer() && !parentPath && !fileName)
                {
                    e = API_EARGS;
                    break;
                }

                if (!transfer->isStreamingTransfer() && ((node && node->type != FILENODE) || (publicNode && publicNode->getType() != FILENODE)) )
                {
                    // Folder download
                    transferMap[nextTag] = transfer;
                    folderTransferMap[nextTag] = transfer;
                    transfer->setTag(nextTag);
                    transfer->startRecursiveOperation(make_unique<MegaFolderDownloadController>(this, transfer), publicNode);
                    break;
                }

                // File download
                if (!transfer->isStreamingTransfer())
                {
                    LocalPath name;
                    LocalPath wLocalPath;

                    if (parentPath)
                    {
                        wLocalPath = LocalPath::fromPath(parentPath, *fsAccess);
                    }
                    else
                    {
                        wLocalPath = LocalPath::fromPath(".", *fsAccess);
                        wLocalPath.appendWithSeparator(LocalPath::fromPath("", *fsAccess), true, fsAccess->localseparator);
                    }

                    FileSystemType fsType = fsAccess->getFilesystemType(wLocalPath);

                    MegaFileGet *f;
                    if (node)
                    {
                        if (!fileName)
                        {
                            attr_map::iterator ait = node->attrs.map.find('n');
                            if (ait == node->attrs.map.end())
                            {
                                name = LocalPath::fromPath("CRYPTO_ERROR", *fsAccess);
                            }
                            else if(!ait->second.size())
                            {
                                name = LocalPath::fromPath("BLANK", *fsAccess);
                            }
                            else
                            {
                                name =  LocalPath::fromName(ait->second, *fsAccess, fsType);
                            }
                        }
                        else
                        {
                            name = LocalPath::fromName(fileName, *fsAccess, fsType);
                        }
                    }
                    else
                    {
                        if (!transfer->getFileName())
                        {
                            name = LocalPath::fromName(publicNode->getName(), *fsAccess, fsType);
                        }
                        else
                        {
                            name = LocalPath::fromName(transfer->getFileName(), *fsAccess, fsType);
                        }
                    }
                    wLocalPath.appendWithSeparator(name, true, fsAccess->localseparator);

                    FileFingerprint *prevFp = NULL;
                    m_off_t size = 0;
                    auto fa = fsAccess->newfileaccess();
                    if (fa->fopen(wLocalPath, true, false))
                    {
                        if (node)
                        {
                            prevFp = node;
                            size = node->size;
                        }
                        else
                        {
                            const char *fpstring = publicNode->getFingerprint();
                            prevFp = getFileFingerprintInternal(fpstring);
                            size = publicNode->getSize();
                        }

                        bool duplicate = false;
                        if (prevFp && prevFp->isvalid)
                        {
                            FileFingerprint fp;
                            fp.genfingerprint(fa.get());
                            if (fp == *prevFp)
                            {
                                duplicate = true;
                            }
                        }
                        else if (fa->size == size)
                        {
                            duplicate = true;
                        }

                        if (duplicate)
                        {
                            transfer->setState(MegaTransfer::STATE_QUEUED);
                            transferMap[nextTag] = transfer;
                            transfer->setTag(nextTag);
                            transfer->setTotalBytes(fa->size);
                            transfer->setTransferredBytes(0);
                            transfer->setPath(wLocalPath.toPath(*fsAccess).c_str());
                            transfer->setStartTime(Waiter::ds);
                            transfer->setUpdateTime(Waiter::ds);
                            fireOnTransferStart(transfer);
                            if (node)
                            {
                                transfer->setNodeHandle(node->nodehandle);
                            }
                            else
                            {
                                transfer->setNodeHandle(publicNode->getHandle());
                                delete prevFp;
                            }
                            transfer->setDeltaSize(fa->size);
                            transfer->setSpeed(0);
                            transfer->setMeanSpeed(0);
                            transfer->setState(MegaTransfer::STATE_COMPLETED);
                            fireOnTransferFinish(transfer, make_unique<MegaErrorPrivate>(API_OK), committer);
                            break;
                        }
                    }
                    fa.reset();

                    currentTransfer = transfer;
                    if (node)
                    {
                        f = new MegaFileGet(client, node, wLocalPath, fsType);
                    }
                    else
                    {
                        delete prevFp;
                        f = new MegaFileGet(client, publicNode, wLocalPath);
                    }

                    transfer->setPath(wLocalPath.toPath(*fsAccess).c_str());
                    f->setTransfer(transfer);
                    bool ok = client->startxfer(GET, f, committer, true, startFirst);
                    if (!ok)
                    {
                        //Already existing transfer
                        transfer->setState(MegaTransfer::STATE_QUEUED);
                        transferMap[nextTag]=transfer;
                        transfer->setTag(nextTag);
                        fireOnTransferStart(transfer);

                        long long overquotaDelay = getBandwidthOverquotaDelay();
                        if (overquotaDelay)
                        {
                            fireOnTransferTemporaryError(transfer, make_unique<MegaErrorPrivate>(API_EOVERQUOTA, overquotaDelay));
                        }

                        transfer->setStartTime(Waiter::ds);
                        transfer->setUpdateTime(Waiter::ds);
                        transfer->setState(MegaTransfer::STATE_CANCELLED);
                        fireOnTransferFinish(transfer, make_unique<MegaErrorPrivate>(API_EEXIST), committer);
                    }
                }
                else
                {
                    currentTransfer = transfer;
                    m_off_t startPos = transfer->getStartPos();
                    m_off_t endPos = transfer->getEndPos();
                    if (startPos < 0 || endPos < 0 || startPos > endPos)
                    {
                        e = API_EARGS;
                        break;
                    }

                    if (node)
                    {
                        transfer->setFileName(node->displayname());
                        if (startPos >= node->size || endPos >= node->size)
                        {
                            e = API_EARGS;
                            break;
                        }

                        m_off_t totalBytes = endPos - startPos + 1;
                        transferMap[nextTag]=transfer;
                        transfer->setTotalBytes(totalBytes);
                        transfer->setTag(nextTag);
                        transfer->setState(MegaTransfer::STATE_QUEUED);

                        fireOnTransferStart(transfer);
                        client->pread(node, startPos, totalBytes, transfer);
                        waiter->notify();
                    }
                    else
                    {
                        transfer->setFileName(publicNode->getName());
                        if (startPos >= publicNode->getSize() || endPos >= publicNode->getSize())
                        {
                            e = API_EARGS;
                            break;
                        }

                        m_off_t totalBytes = endPos - startPos + 1;
                        transferMap[nextTag]=transfer;
                        transfer->setTotalBytes(totalBytes);
                        transfer->setTag(nextTag);
                        transfer->setState(MegaTransfer::STATE_QUEUED);
                        fireOnTransferStart(transfer);
                        SymmCipher cipher;
                        cipher.setkey(publicNode->getNodeKey());
                        client->pread(publicNode->getHandle(), &cipher,
                            MemAccess::get<int64_t>((const char*)publicNode->getNodeKey()->data() + SymmCipher::KEYLENGTH),
                                      startPos, totalBytes, transfer, publicNode->isForeign(),
                                      publicNode->getPrivateAuth()->c_str(),
                                      publicNode->getPublicAuth()->c_str(),
                                      publicNode->getChatAuth());
                        waiter->notify();
                    }
                }

                currentTransfer = NULL;
                break;
            }
        }

        if (e)
        {
            transferMap[nextTag] = transfer;
            transfer->setTag(nextTag);
            transfer->setState(MegaTransfer::STATE_QUEUED);
            fireOnTransferStart(transfer);
            transfer->setStartTime(Waiter::ds);
            transfer->setUpdateTime(Waiter::ds);
            transfer->setState(MegaTransfer::STATE_FAILED);
            fireOnTransferFinish(transfer, make_unique<MegaErrorPrivate>(e), committer);
        }

        if (++count > 100 || std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() > 100)
        {
            break;
        }
    }
    return count;
}

void MegaApiImpl::removeRecursively(const char *path)
{
#ifndef _WIN32
    auto localpath = LocalPath::fromLocalname(path);
    PosixFileSystemAccess::emptydirlocal(localpath);
#else
    string utf16path;
    MegaApi::utf8ToUtf16(path, &utf16path);
    if (utf16path.size() > 1)
    {
        utf16path.resize(utf16path.size() - 1);
        auto localpath = LocalPath::fromLocalname(utf16path);
        WinFileSystemAccess::emptydirlocal(localpath);
    }
#endif
}


error MegaApiImpl::processAbortBackupRequest(MegaRequestPrivate *request, error e)
{
    int tag = int(request->getNumber());
    bool found = false;

    map<int, MegaBackupController *>::iterator itr = backupsMap.find(tag) ;
    if (itr != backupsMap.end())
    {
        found = true;

        MegaBackupController *backup = itr->second;

        bool flag = request->getFlag();
        if (!flag)
        {
            if (backup->getState() == MegaBackup::BACKUP_ONGOING)
            {
                for (std::map<int, MegaTransferPrivate *>::iterator it = transferMap.begin(); it != transferMap.end(); it++)
                {
                    MegaTransferPrivate *t = it->second;
                    if (t->getFolderTransferTag() == backup->getFolderTransferTag())
                    {
                        api->cancelTransferByTag(t->getTag()); //what if any of these fail? (Although I don't think that's possible)
                    }
                }
                request->setFlag(true);
                requestQueue.push(request);
            }
            else
            {
                LOG_debug << "Abort failed: no ongoing backup";
                fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_ENOENT));
            }
        }
        else
        {
            backup->abortCurrent(); //TODO: THIS MAY CAUSE NEW REQUESTS, should we consider them before fireOnRequestFinish?!!!
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
        }

    }
    if (!found)
    {
        e = API_ENOENT;
    }
    return e;
}

void MegaApiImpl::yield()
{
#if __cplusplus >= 201100L
    std::this_thread::yield();
#elif !defined(_WIN32)
    sched_yield();
#endif
}

static void appendFileAttribute(string& s, int n, MegaHandle h)
{
    if (h != INVALID_HANDLE)
    {
        if (!s.empty())
        {
            s += "/";
        }

        char buf[64];
        sprintf(buf, "%u*", n);
        Base64::btoa((byte*)&h, sizeof(h), strchr(buf + 2, 0));
        s += buf;
    }
}

static error updateAttributesMapWithCoordinates(AttrMap& attrs, int latitude, int longitude, bool unshareable, MegaClient* client)
{
    static const nameid coordsNameShareable = AttrMap::string2nameid("l");
    static const nameid coordsNameUnshareable = AttrMap::string2nameid("gp");

    if (longitude == MegaNode::INVALID_COORDINATE && latitude == MegaNode::INVALID_COORDINATE)
    {
        attrs.map.erase(coordsNameShareable);
        attrs.map.erase(coordsNameUnshareable);
    }
    else
    {
        if (longitude < 0 || longitude >= 0x01000000
            || latitude < 0 || latitude >= 0x01000000)
        {
            return API_EARGS;
        }

        Base64Str<3> latEncoded((const byte*)&latitude);
        Base64Str<3> lonEncoded((const byte*)&longitude);
        string coordsValue = string(latEncoded) + lonEncoded.chars;
        if (coordsValue.size() != 8)
        {
            return API_EARGS;
        }

        if (unshareable)
        {
            if (client->unshareablekey.size() != Base64Str<SymmCipher::KEYLENGTH>::STRLEN)
            {
                return API_EKEY;
            }

            SymmCipher c;
            byte data[SymmCipher::BLOCKSIZE] = { 0 };
            memcpy(data, "unshare/", 8);
            memcpy(data + 8, (void*)coordsValue.data(), coordsValue.size());
            client->setkey(&c, client->unshareablekey.data());
            c.ctr_crypt(data, unsigned(8 + coordsValue.size()), 0, 0, NULL, true);
            attrs.map[coordsNameUnshareable] = Base64Str<SymmCipher::BLOCKSIZE>(data);
            attrs.map.erase(coordsNameShareable);
        }
        else
        {
            attrs.map[coordsNameShareable] = coordsValue;
            attrs.map.erase(coordsNameUnshareable);
        }
    }
    return API_OK;
}

void MegaApiImpl::sendPendingScRequest()
{
    MegaRequestPrivate *request = scRequestQueue.front();
    if (!request || request->getTag())
    {
        return;
    }
    assert(request->getType() == MegaRequest::TYPE_CATCHUP);

    sdkMutex.lock();

    request->setTag(1);
    fireOnRequestStart(request);
    client->catchup();

    sdkMutex.unlock();
}

void MegaApiImpl::sendPendingRequests()
{
    int nextTag = 0;

    SdkMutexGuard g(sdkMutex);
    
    // For multiple consecutive requests of the same type (eg. remove transfer) this committer will put all the database activity into a single commit
    DBTableTransactionCommitter committer(client->tctable);
    int lastRequestType = -1;
    int lastRequestConsecutive = 0;


    while(MegaRequestPrivate *request = requestQueue.pop())
    {

        // also we avoid yielding for consecutive transaction cancel operations (we used to yeild every time, but we need to keep the sdkMutex lock while the database transaction is ongoing)
        if ((lastRequestType == -1 || lastRequestType == request->getType()) && lastRequestConsecutive < 1024)
        {
            ++lastRequestConsecutive;
        }
        else
        {
            committer.commitNow();
            sdkMutex.unlock();
            yield();
            sdkMutex.lock();
            lastRequestConsecutive = 0;
        }

        lastRequestType = request->getType();

        if (!nextTag && request->getType() != MegaRequest::TYPE_LOGOUT)
        {
            client->abortbackoff(false);
        }

        if (!request->getTag())
        {
            nextTag = client->nextreqtag();
            request->setTag(nextTag);
            requestMap[nextTag] = request;
            fireOnRequestStart(request);
        }
        else
        {
            // this case happens when we queue requests already started
            nextTag = request->getTag();
        }

        error e = API_OK;
        switch (request->getType())
        {
        case MegaRequest::TYPE_LOGIN:
        {
            const char *login = request->getEmail();
            const char *password = request->getPassword();
            const char* megaFolderLink = request->getLink();
            const char* base64pwkey = request->getPrivateKey();
            const char* sessionKey = request->getSessionKey();

            if (!megaFolderLink && (!(login && password)) && !sessionKey && (!(login && base64pwkey)))
            {
                e = API_EARGS;
                break;
            }

            string slogin;
            if (login)
            {
                slogin = login;
                slogin.erase(slogin.begin(), std::find_if(slogin.begin(), slogin.end(), char_is_not_space));
                slogin.erase(std::find_if(slogin.rbegin(), slogin.rend(), char_is_not_space).base(), slogin.end());
            }

            requestMap.erase(request->getTag());

            abortPendingActions();

            requestMap[request->getTag()]=request;

            client->locallogout(false);
            if (sessionKey)
            {
                byte session[MAX_SESSION_LENGTH];
                int size = Base64::atob(sessionKey, (byte *)session, sizeof session);
                client->login(session, size);
            }
            else if (login && (base64pwkey || password))
            {
                client->prelogin(slogin.c_str());
            }
            else
            {
                e = client->folderaccess(megaFolderLink);
                if(e == API_OK)
                {
                    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
                }
            }

            break;
        }
        case MegaRequest::TYPE_MULTI_FACTOR_AUTH_CHECK:
        {
            const char *email = request->getEmail();
            if (!email)
            {
                e = API_EARGS;
                break;
            }
            client->multifactorauthcheck(email);
            break;
        }
        case MegaRequest::TYPE_MULTI_FACTOR_AUTH_GET:
        {
            client->multifactorauthsetup();
            break;
        }
        case MegaRequest::TYPE_MULTI_FACTOR_AUTH_SET:
        {
            bool flag = request->getFlag();
            const char *pin = request->getPassword();
            if (!pin)
            {
                e = API_EARGS;
                break;
            }

            if (flag)
            {
                client->multifactorauthsetup(pin);
            }
            else
            {
                client->multifactorauthdisable(pin);
            }
            break;
        }
        case MegaRequest::TYPE_FETCH_TIMEZONE:
        {
            client->fetchtimezone();
            break;
        }
        case MegaRequest::TYPE_CREATE_FOLDER:
        {
            Node *parent = client->nodebyhandle(request->getParentHandle());
            const char *name = request->getName();
            if(!name || !(*name) || !parent) { e = API_EARGS; break; }

            // prevent to create a duplicate folder with same name in same path
            Node *folder = client->childnodebyname(parent, name, false);
            if (folder && folder->type == FOLDERNODE)
            {
                e = API_OK;
                request->setNodeHandle(folder->nodehandle);
                fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
                break;
            }

            NewNode *newnode = new NewNode[1];
            SymmCipher key;
            string attrstring;
            byte buf[FOLDERNODEKEYLENGTH];

            // set up new node as folder node
            newnode->source = NEW_NODE;
            newnode->type = FOLDERNODE;
            newnode->nodehandle = 0;
            newnode->parenthandle = UNDEF;

            // generate fresh random key for this folder node
            client->rng.genblock(buf,FOLDERNODEKEYLENGTH);
            newnode->nodekey.assign((char*)buf,FOLDERNODEKEYLENGTH);
            key.setkey(buf);

            // generate fresh attribute object with the folder name
            AttrMap attrs;
            string sname = name;
            fsAccess->normalize(&sname);
            attrs.map['n'] = sname;

            // JSON-encode object and encrypt attribute string
            attrs.getjson(&attrstring);
            newnode->attrstring.reset(new string);
            client->makeattr(&key, newnode->attrstring, attrstring.c_str());

            // add the newly generated folder node
            client->putnodes(parent->nodehandle,newnode,1);
            break;
        }
        case MegaRequest::TYPE_MOVE:
        {
            Node *node = client->nodebyhandle(request->getNodeHandle());
            Node *newParent = client->nodebyhandle(request->getParentHandle());
            const char *name = request->getName();
            if (!node || !newParent)
            {
                e = API_EARGS;
                break;
            }

            if (node->parent == newParent)
            {
                fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
                break;
            }

            if (node->type == ROOTNODE
                    || node->type == INCOMINGNODE
                    || node->type == RUBBISHNODE
                    || !node->parent) // rootnodes cannot be moved
            {
                e = API_EACCESS;
                break;
            }

            // old versions cannot be moved
            if (node->parent->type == FILENODE)
            {
                e = API_EACCESS;
                break;
            }

            // target must be a folder with enough permissions
            if (newParent->type == FILENODE || !client->checkaccess(newParent, RDWR))
            {
                e = API_EACCESS;
                break;
            }

            if ((e = client->checkmove(node, newParent)))
            {
                // If it's not possible to move the node, try copy-delete,
                // but only when it's not possible due to access rights
                // the node and the target are from different node trees,
                // it's possible to put nodes in the target folder
                // and also to remove the source node
                if (e != API_EACCESS)
                {
                    break;
                }

                Node *nodeRoot = node;
                while (nodeRoot->parent)
                {
                    nodeRoot = nodeRoot->parent;
                }

                Node *parentRoot = newParent;
                while (parentRoot->parent)
                {
                    parentRoot = parentRoot->parent;
                }

                if ((nodeRoot == parentRoot)
                        || !client->checkaccess(node, FULL)
                        || !client->checkaccess(newParent, RDWR))
                {
                    break;
                }

                unsigned nc;
                TreeProcCopy tc;
                handle ovhandle = UNDEF;

                if (node->type == FILENODE)
                {
                    string newName;
                    if (name)
                    {
                        newName.assign(name);
                        client->fsaccess->normalize(&newName);
                    }
                    else
                    {
                        attr_map::iterator it = node->attrs.map.find('n');
                        if (it != node->attrs.map.end())
                        {
                            newName = it->second;
                        }
                    }
                    if (!newName.empty())
                    {
                        Node *ovn = client->childnodebyname(newParent, newName.c_str(), true);
                        if (ovn)
                        {
                            if (node->isvalid && ovn->isvalid && *(FileFingerprint*)node == *(FileFingerprint*)ovn)
                            {
                                e = API_OK; // there is already an identical node in the target folder
                                // continue to complete the copy-delete
                                client->restag = request->getTag();
                                putnodes_result(API_OK, NODE_HANDLE, NULL);
                                break;
                            }

                            if (!client->versions_disabled)
                            {
                                ovhandle = ovn->nodehandle;
                            }
                        }
                    }
                }

                // determine number of nodes to be copied
                client->proctree(node, &tc, ovhandle != UNDEF);
                tc.allocnodes();
                nc = tc.nc;

                // build new nodes array
                client->proctree(node, &tc, ovhandle != UNDEF);
                if (!nc)
                {
                    e = API_EARGS;
                    break;
                }

                tc.nn->parenthandle = UNDEF;
                tc.nn->ovhandle = ovhandle;

                if (name)   // move and rename
                {
                    string newName(name);
                    client->fsaccess->normalize(&newName);

                    AttrMap attrs = node->attrs;
                    attrs.map['n'] = newName;

                    string attrstring;
                    attrs.getjson(&attrstring);

                    SymmCipher key;
                    key.setkey((const byte*)tc.nn->nodekey.data(), node->type);
                    client->makeattr(&key, tc.nn->attrstring, attrstring.c_str());
                }

                client->putnodes(newParent->nodehandle, tc.nn, nc);
                e = API_OK;
                break;
            }

            e = client->rename(node, newParent, SYNCDEL_NONE, UNDEF, name);
            break;
        }
        case MegaRequest::TYPE_COPY:
        {
            Node *node = NULL;
            Node *target = client->nodebyhandle(request->getParentHandle());
            const char* email = request->getEmail();
            MegaNode *megaNode = request->getPublicNode();
            const char *newName = request->getName();
            handle ovhandle = UNDEF;
            unsigned nc = 0;

            if (!megaNode || (!target && !email)
                    || (newName && !(*newName))
                    || (target && target->type == FILENODE))
            {
                e = API_EARGS;
                break;
            }

            if (!megaNode->isForeign() && !megaNode->isPublic())
            {
                node = client->nodebyhandle(request->getNodeHandle());
                if (!node)
                {
                    e = API_ENOENT;
                    break;
                }
            }

            if (!node)
            {
                if (!megaNode->getNodeKey()->size())
                {
                    e = API_EKEY;
                    break;
                }

                string sname = megaNode->getName();
                if (newName)
                {
                    MegaNodePrivate *privateNode = dynamic_cast<MegaNodePrivate *>(megaNode);
                    if (privateNode)
                    {
                        sname = newName;
                        fsAccess->normalize(&sname);
                        privateNode->setName(sname.c_str());
                    }
                    else
                    {
                        LOG_err << "Unknown node type";
                    }
                }

                if (target && megaNode->getType() == MegaNode::TYPE_FILE)
                {
                    Node *ovn = client->childnodebyname(target, sname.c_str(), true);
                    if (ovn)
                    {
                        FileFingerprint *fp = getFileFingerprintInternal(megaNode->getFingerprint());
                        if (fp)
                        {
                            if (fp->isvalid && ovn->isvalid && *fp == *(FileFingerprint*)ovn)
                            {
                                request->setNodeHandle(ovn->nodehandle);
                                fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
                                delete fp;
                                break;
                            }

                            delete fp;
                        }

                        if (!client->versions_disabled)
                        {
                            ovhandle = ovn->nodehandle;
                        }
                    }
                }

                MegaTreeProcCopy tc(client);

                processMegaTree(megaNode, &tc);
                tc.allocnodes();
                nc = tc.nc;

                // build new nodes array
                processMegaTree(megaNode, &tc);

                tc.nn->parenthandle = UNDEF;
                tc.nn->ovhandle = ovhandle;

                if (target)
                {
                    client->putnodes(target->nodehandle, tc.nn, nc, megaNode->getChatAuth());
                }
                else
                {
                    client->putnodes(email, tc.nn, nc);
                }
            }
            else
            {
                unsigned nc;
                TreeProcCopy tc;

                if (!node->nodekey().size())
                {
                    e = API_EKEY;
                    break;
                }

                if (node->attrstring)
                {
                    node->applykey();
                    node->setattr();
                    if (node->attrstring)
                    {
                        e = API_EKEY;
                        break;
                    }
                }

                string sname;
                if (newName)
                {
                    sname = newName;
                    fsAccess->normalize(&sname);
                }
                else
                {
                    attr_map::iterator it = node->attrs.map.find('n');
                    if (it != node->attrs.map.end())
                    {
                        sname = it->second;
                    }
                }

                if (target && node->type == FILENODE)
                {
                    Node *ovn = client->childnodebyname(target, sname.c_str(), true);
                    if (ovn)
                    {
                        if (node->isvalid && ovn->isvalid && *(FileFingerprint*)node == *(FileFingerprint*)ovn)
                        {
                            request->setNodeHandle(ovn->nodehandle);
                            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
                            break;
                        }

                        if (!client->versions_disabled)
                        {
                            ovhandle = ovn->nodehandle;
                        }
                    }
                }

                // determine number of nodes to be copied
                client->proctree(node, &tc, false, ovhandle != UNDEF);
                tc.allocnodes();
                nc = tc.nc;

                // build new nodes array
                client->proctree(node, &tc, false, ovhandle != UNDEF);
                tc.nn->parenthandle = UNDEF;
                tc.nn->ovhandle = ovhandle;

                if (newName)
                {
                    SymmCipher key;
                    AttrMap attrs;
                    string attrstring;

                    key.setkey((const byte*)tc.nn->nodekey.data(), node->type);
                    attrs = node->attrs;

                    attrs.map['n'] = sname;

                    attrs.getjson(&attrstring);
                    client->makeattr(&key, tc.nn->attrstring, attrstring.c_str());
                }

                if (target)
                {
                    client->putnodes(target->nodehandle, tc.nn, nc);
                }
                else
                {
                    client->putnodes(email, tc.nn, nc);
                }
            }
            break;
        }
        case MegaRequest::TYPE_RESTORE:
        {
            Node *version = client->nodebyhandle(request->getNodeHandle());
            if (!version)
            {
                e = API_ENOENT;
                break;
            }

            if (version->type != FILENODE || !version->parent || version->parent->type != FILENODE)
            {
                e = API_EARGS;
                break;
            }

            Node *current = version;
            while (current->parent && current->parent->type == FILENODE)
            {
                current = current->parent;
            }

            if (!current->parent)
            {
                e = API_EINTERNAL;
                break;
            }

            NewNode* newnode = new NewNode[1];
            string attrstring;
            SymmCipher key;

            newnode->source = NEW_NODE;
            newnode->type = FILENODE;
            newnode->nodehandle = version->nodehandle;
            newnode->parenthandle = UNDEF;
            newnode->ovhandle = current->nodehandle;
            newnode->nodekey = version->nodekey();
            newnode->attrstring.reset(new string);
            if (newnode->nodekey.size())
            {
                key.setkey((const byte*)version->nodekey().data(), version->type);
                version->attrs.getjson(&attrstring);
                client->makeattr(&key, newnode->attrstring, attrstring.c_str());
            }

            client->putnodes(current->parent->nodehandle, newnode, 1);
            break;
        }
        case MegaRequest::TYPE_RENAME:
        {
            if (client->ststatus == STORAGE_PAYWALL)
            {
                e = API_EPAYWALL;
                break;
            }

            Node* node = client->nodebyhandle(request->getNodeHandle());
            const char* newName = request->getName();
            if(!node || !newName || !(*newName)) { e = API_EARGS; break; }

            if (!client->checkaccess(node,FULL)) { e = API_EACCESS; break; }

            string sname = newName;
            fsAccess->normalize(&sname);
            node->attrs.map['n'] = sname;
            e = client->setattr(node);
            break;
        }
        case MegaRequest::TYPE_REMOVE:
        {
            Node* node = client->nodebyhandle(request->getNodeHandle());
            bool keepversions = request->getFlag();

            if (!node)
            {
                e = API_ENOENT;
                break;
            }

            if (keepversions && node->type != FILENODE)
            {
                e = API_EARGS;
                break;
            }

            if (node->type == ROOTNODE
                    || node->type == INCOMINGNODE
                    || node->type == RUBBISHNODE) // rootnodes cannot be deleted
            {
                e = API_EACCESS;
                break;
            }

            e = client->unlink(node, keepversions);
            break;
        }
        case MegaRequest::TYPE_REMOVE_VERSIONS:
        {
            client->unlinkversions();
            break;
        }
        case MegaRequest::TYPE_SHARE:
        {
            Node *node = client->nodebyhandle(request->getNodeHandle());
            const char* email = request->getEmail();
            int access = request->getAccess();
            if(!node || !email || !strchr(email, '@'))
            {
                e = API_EARGS;
                break;
            }

            accesslevel_t a = ACCESS_UNKNOWN;
            switch(access)
            {
                case MegaShare::ACCESS_UNKNOWN:
                    a = ACCESS_UNKNOWN;
                    break;
                case MegaShare::ACCESS_READ:
                    a = RDONLY;
                    break;
                case MegaShare::ACCESS_READWRITE:
                    a = RDWR;
                    break;
                case MegaShare::ACCESS_FULL:
                    a = FULL;
                    break;
                case MegaShare::ACCESS_OWNER:
                    a = OWNER;
                    break;
                default:
                    e = API_EARGS;
            }

            if (e == API_OK)
                client->setshare(node, email, a);
            break;
        }
        case MegaRequest::TYPE_IMPORT_LINK:
        case MegaRequest::TYPE_GET_PUBLIC_NODE:
        {
            Node *node = client->nodebyhandle(request->getParentHandle());
            const char* megaFileLink = request->getLink();

            if (!megaFileLink)
            {
                e = API_EARGS;
                break;
            }
            if ((request->getType() == MegaRequest::TYPE_IMPORT_LINK) && !node)
            {
                e = API_EARGS;
                break;
            }

            handle ph = UNDEF;
            byte key[FILENODEKEYLENGTH];
            e = client->parsepubliclink(megaFileLink, ph, key, false);
            if (e == API_OK)
            {
                client->openfilelink(ph, key, 1);
            }
            else if (e == API_EINCOMPLETE)  // no key provided, check only the existence of the node
            {
                client->openfilelink(ph, nullptr, 1);
                e = API_OK;
            }
            break;
        }
        case MegaRequest::TYPE_PASSWORD_LINK:
        {
            const char *link = request->getLink();
            const char *pwd = request->getPassword();
            bool encryptLink = request->getFlag();

            string result;
            if (encryptLink)
            {
                e = client->encryptlink(link, pwd, &result);
            }
            else
            {
                e = client->decryptlink(link, pwd, &result);
            }

            if (!e)
            {
                request->setText(result.c_str());
                fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
            }
            break;
        }
        case MegaRequest::TYPE_EXPORT:
        {
            Node* node = client->nodebyhandle(request->getNodeHandle());
            if(!node) { e = API_EARGS; break; }

            e = client->exportnode(node, !request->getAccess(), request->getNumber());
            break;
        }
        case MegaRequest::TYPE_FETCH_NODES:
        {
            if (nocache)
            {
                client->opensctable();

                if (client->sctable)
                {
                    client->sctable->remove();
                    delete client->sctable;
                    client->sctable = NULL;
                    client->pendingsccommit = false;
                    client->cachedscsn = UNDEF;
                }

                nocache = false;
            }

#ifdef ENABLE_SYNC
            client->allowAutoResumeSyncs = request->getNumber();
#endif
            client->fetchnodes();
#ifdef ENABLE_SYNC
            client->allowAutoResumeSyncs = true;
#endif
            break;
        }
        case MegaRequest::TYPE_GET_CLOUD_STORAGE_USED:
        {
            if (client->loggedin() != FULLACCOUNT)
            {
                e = API_EACCESS;
                break;
            }

            request->setNumber(client->mFingerprints.getSumSizes());
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
            break;
        }
        case MegaRequest::TYPE_ACCOUNT_DETAILS:
        {
            if(client->loggedin() != FULLACCOUNT)
            {
                e = API_EACCESS;
                break;
            }

            int numDetails = request->getNumDetails();
            bool storage = (numDetails & 0x01) != 0;
            bool transfer = (numDetails & 0x02) != 0;
            bool pro = (numDetails & 0x04) != 0;
            bool transactions = (numDetails & 0x08) != 0;
            bool purchases = (numDetails & 0x10) != 0;
            bool sessions = (numDetails & 0x20) != 0;

            int numReqs = int(storage || transfer || pro) + int(transactions) + int(purchases) + int(sessions);
            if (numReqs == 0)
            {
                e = API_EARGS;
                break;
            }
            request->setNumber(numReqs);

            client->getaccountdetails(request->getAccountDetails(), storage, transfer, pro, transactions, purchases, sessions, request->getAccess());
            break;
        }
        case MegaRequest::TYPE_QUERY_TRANSFER_QUOTA:
        {
            m_off_t size = request->getNumber();
            client->querytransferquota(size);
            break;
        }
        case MegaRequest::TYPE_CHANGE_PW:
        {
            const char* oldPassword = request->getPassword();
            const char* newPassword = request->getNewPassword();
            const char* pin = request->getText();
            if (!newPassword)
            {
                e = API_EARGS;
                break;
            }

            if (oldPassword && !checkPassword(oldPassword))
            {
                e = API_EARGS;
                break;
            }

            e = client->changepw(newPassword, pin);
            break;
        }
        case MegaRequest::TYPE_LOGOUT:
        {
            if (request->getParamType() == API_ESSL && client->retryessl)
            {
                e = API_EINCOMPLETE;
                break;
            }

            if(request->getFlag())
            {
                client->logout();
            }
            else
            {
                client->locallogout(false);
                client->restag = nextTag;
                logout_result(API_OK);
            }
            break;
        }
        case MegaRequest::TYPE_GET_ATTR_FILE:
        {
            const char* dstFilePath = request->getFile();
            int type = request->getParamType();
            handle h = request->getNodeHandle();
            const char *fa = request->getText();
            const char *base64key = request->getPrivateKey();

            Node *node = client->nodebyhandle(h);

            if(!dstFilePath || (!fa && !node) || (fa && (!base64key || ISUNDEF(h))))
            {
                e = API_EARGS;
                break;
            }

            string fileattrstring;
            string key;
            if (!fa)
            {
                fileattrstring = node->fileattrstring;
                key = node->nodekey();
            }
            else
            {
                fileattrstring = fa;

                byte nodekey[FILENODEKEYLENGTH];
                if (Base64::atob(base64key, nodekey, sizeof nodekey) != sizeof nodekey)
                {
                    e = API_EKEY;
                    break;
                }
                key.assign((const char *)nodekey, sizeof nodekey);
            }
            e = client->getfa(h, &fileattrstring, key, (fatype) type);
            if(e == API_EEXIST)
            {
                e = API_OK;
                int prevtag = client->restag;
                MegaRequestPrivate* req = NULL;
                while(prevtag)
                {
                    if(requestMap.find(prevtag) == requestMap.end())
                    {
                        LOG_err << "Invalid duplicate getattr request";
                        req = NULL;
                        e = API_EINTERNAL;
                        break;
                    }

                    req = requestMap.at(prevtag);
                    if(!req || (req->getType() != MegaRequest::TYPE_GET_ATTR_FILE))
                    {
                        LOG_err << "Invalid duplicate getattr type";
                        req = NULL;
                        e = API_EINTERNAL;
                        break;
                    }

                    prevtag = int(req->getNumber());
                }

                if(req)
                {
                    LOG_debug << "Duplicate getattr detected";
                    req->setNumber(request->getTag());
                }
            }
            break;
        }
        case MegaRequest::TYPE_GET_ATTR_USER:
        {
            const char* value = request->getFile();
            attr_t type = static_cast<attr_t>(request->getParamType());
            const char *email = request->getEmail();
            const char *ph = request->getSessionKey();

            string attrname = MegaApiImpl::userAttributeToString(type);
            char scope = MegaApiImpl::userAttributeToScope(type);

            if ((!client->loggedin() && ph == NULL) || (ph && !ph[0]))
            {
                e = API_EARGS;
                break;
            }

            User *user = email ? client->finduser(email, 0) : client->finduser(client->me, 0);

            if (!user)  // email/handle not found among (ex)contacts
            {
                if (scope == '*' || scope == '#')
                {
                    LOG_warn << "Cannot retrieve private/protected attributes from users other than yourself.";
                    e = API_EACCESS;
                    break;
                }

                client->getua(email, type, ph);
                break;
            }

            if (attrname.empty() ||    // unknown attribute type
                 (type == ATTR_AVATAR && !value)) // no destination file for avatar
            {
                e = API_EARGS;
                break;
            }

            // if attribute is private and user is not logged in user...
            if (scope == '*' && user->userhandle != client->me)
            {
                e = API_EACCESS;
                break;
            }

            client->getua(user, type);
            break;
        }
        case MegaRequest::TYPE_SET_ATTR_USER:
        {
            const char* file = request->getFile();
            const char* value = request->getText();
            attr_t type = static_cast<attr_t>(request->getParamType());
            MegaStringMap *stringMap = request->getMegaStringMap();

            char scope = MegaApiImpl::userAttributeToScope(type);
            string attrname = MegaApiImpl::userAttributeToString(type);
            if (attrname.empty())   // unknown attribute type
            {
                e = API_EARGS;
                break;
            }

            string attrvalue;

            if (type == ATTR_KEYRING                ||
                    User::isAuthring(type)          ||
                    type == ATTR_CU25519_PUBK       ||
                    type == ATTR_ED25519_PUBK       ||
                    type == ATTR_SIG_CU255_PUBK     ||
                    type == ATTR_SIG_RSA_PUBK)
            {
                e = API_EACCESS;
                break;
            }
            else if (type == ATTR_AVATAR)
            {
                // read the attribute value from file
                if (file)
                {
                    auto localpath = LocalPath::fromPath(file, *fsAccess);

                    auto f = fsAccess->newfileaccess();
                    if (!f->fopen(localpath, 1, 0))
                    {
                        e = API_EREAD;
                        break;
                    }

                    if (!f->fread(&attrvalue, unsigned(f->size), 0, 0))
                    {
                        e = API_EREAD;
                        break;
                    }
                    f.reset();

                    client->putua(type, (byte *)attrvalue.data(), unsigned(attrvalue.size()));
                    break;
                }
                else    // removing current attribute's value
                {
                    client->putua(type);
                    break;
                }
            }
            else if (scope == '*')   // private attribute
            {
                if (!stringMap)
                {
                    e = API_EARGS;
                    break;
                }

                std::unique_ptr<TLVstore> tlv;
                User *ownUser = client->finduser(client->me);
                if (type == ATTR_ALIAS || type == ATTR_CAMERA_UPLOADS_FOLDER || type == ATTR_DEVICE_NAMES)
                {
                    if (!ownUser->isattrvalid(type)) // not fetched yet or outdated
                    {
                        // always get updated value before update it
                        client->getua(ownUser, type);
                        break;
                    }
                    else
                    {
                        tlv.reset(TLVstore::containerToTLVrecords(ownUser->getattr(type), &client->key));
                    }
                }
                else
                {
                    tlv.reset(new TLVstore);
                }

                const string_map *newValuesMap = static_cast<MegaStringMapPrivate*>(request->getMegaStringMap())->getMap();
                if (User::mergeUserAttribute(type, *newValuesMap, *tlv.get()))
                {
                    // serialize and encrypt the TLV container
                    std::unique_ptr<string> container(tlv->tlvRecordsToContainer(client->rng, &client->key));
                    client->putua(type, (byte *)container->data(), unsigned(container->size()));
                }
                else
                {
                    // no changes, current value equal to new value
                    LOG_debug << "Attribute " << User::attr2string(type) << " not changed, already up to date";
                    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
                }
                break;
            }
            else if (scope == '^')
            {
                if (type == ATTR_LANGUAGE)
                {
                    if (!value)
                    {
                        e = API_EARGS;
                        break;
                    }

                    string code;
                    if (!getLanguageCode(value, &code))
                    {
                        e = API_ENOENT;
                        break;
                    }

                    client->putua(type, (byte *)code.data(), unsigned(code.length()));
                    break;
                }
                else if (type == ATTR_PWD_REMINDER)
                {
                    if (request->getNumDetails() == 0)  // nothing to be changed
                    {
                        e = API_EARGS;
                        break;
                    }

                    // always get updated value before update it
                    User *ownUser = client->finduser(client->me);
                    client->getua(ownUser, type);
                    break;
                }
                else if (type == ATTR_DISABLE_VERSIONS)
                {
                    if (!value || strlen(value) != 1 || (value[0] != '0' && value[0] != '1'))
                    {
                        e = API_EARGS;
                        break;
                    }

                    client->putua(type, (byte *)value, 1);
                }
                else if (type == ATTR_CONTACT_LINK_VERIFICATION)
                {
                    if (!value || strlen(value) != 1 || (value[0] != '0' && value[0] != '1'))
                    {
                        e = API_EARGS;
                        break;
                    }

                    client->putua(type, (byte *)value, 1);
                }
                else if (type == ATTR_RUBBISH_TIME || type == ATTR_LAST_PSA)
                {
                    if (!value || !value[0])
                    {
                        e = API_EARGS;
                        break;
                    }

                    char *endptr;
                    m_off_t number = strtoll(value, &endptr, 10);
                    if (endptr == value || *endptr != '\0' || number == LLONG_MAX || number == LLONG_MIN || number < 0)
                    {
                        e = API_EARGS;
                        break;
                    }

                    string tmp(value);
                    client->putua(type, (byte *)tmp.data(), unsigned(tmp.size()));
                }
                else if (type == ATTR_PUSH_SETTINGS)
                {
                    const MegaPushNotificationSettingsPrivate *settings = (MegaPushNotificationSettingsPrivate*)(request->getMegaPushNotificationSettings());
                    if (!settings)
                    {
                        e = API_EARGS;
                        break;
                    }

                    string settingsJson = settings->generateJson();
                    if (settingsJson.empty())
                    {
                        e = API_EARGS;
                        break;
                    }

                    client->putua(type, (byte *)settingsJson.data(), unsigned(settingsJson.size()));
                }
                else
                {
                    e = API_EARGS;
                    break;
                }
            }
            else    // any other type of attribute
            {
                if (!value)
                {
                    e = API_EARGS;
                    break;
                }

                client->putua(type, (byte *)value, unsigned(strlen(value)));
                break;
            }

            break;
        }
        case MegaRequest::TYPE_GET_USER_EMAIL:
        {
            handle uh = request->getNodeHandle();
            if (uh == INVALID_HANDLE)
            {
                e = API_EARGS;
                break;
            }

            char uid[12];
            Base64::btoa((byte*)&uh, MegaClient::USERHANDLE, uid);
            uid[11] = 0;

            client->getUserEmail(uid);
            break;
        }
        case MegaRequest::TYPE_SET_ATTR_FILE:
        {
            const char* srcFilePath = request->getFile();
            int type = request->getParamType();
            Node *node = request->getNodeHandle() == INVALID_HANDLE ? nullptr : client->nodebyhandle(request->getNodeHandle());
            MegaHandle fileattrhandle = (uint64_t)request->getNumber();
            auto bu = static_cast<MegaBackgroundMediaUploadPrivate*>(request->getMegaBackgroundMediaUploadPtr());

            if (!srcFilePath)
            {
                if (!node || fileattrhandle == INVALID_HANDLE) { e = API_EARGS; break; }

                string fileattr;
                appendFileAttribute(fileattr, type, fileattrhandle);

                client->reqs.add(new CommandAttachFA(client, node->nodehandle, fatype(type), fileattr, nextTag));
            }
            else
            {
                if (!node == !bu) { e = API_EARGS; break; }

                auto localpath = LocalPath::fromPath(srcFilePath, *fsAccess);

                std::unique_ptr<string> attributedata(new string);
                std::unique_ptr<FileAccess> f(fsAccess->newfileaccess());
                if (!f->fopen(localpath, 1, 0))
                {
                    e = API_EREAD;
                    break;
                }

                // make the string a little bit larger initially with SymmCipher::BLOCKSIZE to avoid heap activity growing it for the encryption
                attributedata->reserve(size_t(f->size + SymmCipher::BLOCKSIZE));
                if (!f->fread(attributedata.get(), unsigned(f->size), 0, 0))
                {
                    e = API_EREAD;
                    break;
                }

                client->putfa(node ? node->nodehandle : INVALID_HANDLE, (fatype)type, bu ? bu->nodecipher(client) : node->nodecipher(), std::move(attributedata));
            }
            break;
        }
        case MegaRequest::TYPE_SET_ATTR_NODE:
        {
            Node *node = client->nodebyhandle(request->getNodeHandle());
            bool isOfficial = request->getFlag();

            if (!node)
            {
                e = API_EARGS;
                break;
            }

            if (!client->checkaccess(node, FULL))
            {
                e = API_EACCESS;
                break;
            }

            if (isOfficial)
            {
                int type = request->getParamType();
                if (type == MegaApi::NODE_ATTR_DURATION)
                {
                    int secs = int(request->getNumber());
                    if (node->type != FILENODE || secs < MegaNode::INVALID_DURATION)
                    {
                        e = API_EARGS;
                        break;
                    }

                    if (secs == MegaNode::INVALID_DURATION)
                    {
                        node->attrs.map.erase('d');
                    }
                    else
                    {
                        string attrVal;
                        Base64::itoa(secs, &attrVal);
                        if (attrVal.size())
                        {
                            node->attrs.map['d'] = attrVal;
                        }
                    }
                }
                else if (type == MegaApi::NODE_ATTR_COORDINATES)
                {
                    if (node->type != FILENODE)
                    {
                        e = API_EARGS;
                        break;
                    }

                    int longitude = request->getNumDetails();
                    int latitude = request->getTransferTag();
                    int unshareable = request->getAccess();

                    e = updateAttributesMapWithCoordinates(node->attrs, latitude, longitude, !!unshareable, client);
                    if (e != API_OK)
                    {
                        break;
                    }
                }
                else if (type == MegaApi::NODE_ATTR_ORIGINALFINGERPRINT)
                {
                    nameid nid = AttrMap::string2nameid("c0");
                    SymmCipher tkey;
                    string tattrstring;
                    if (!request->getText())
                    {
                        node->attrs.map.erase(nid);
                    }
                    else
                    {
                        node->attrs.map[nid] = request->getText();
                    }
                }
                else
                {
                    e = API_EARGS;
                    break;
                }
            }
            else    // custom attribute, not official
            {
                const char* attrName = request->getName();
                const char* attrValue = request->getText();

                if (!attrName || !attrName[0] || strlen(attrName) > 7)
                {
                    e = API_EARGS;
                    break;
                }

                string sname = attrName;
                fsAccess->normalize(&sname);
                sname.insert(0, "_");
                nameid attr = AttrMap::string2nameid(sname.c_str());

                if (attrValue)
                {
                    string svalue = attrValue;
                    fsAccess->normalize(&svalue);
                    node->attrs.map[attr] = svalue;
                }
                else
                {
                    node->attrs.map.erase(attr);
                }
            }

            if (!e)
            {
                e = client->setattr(node);
            }

            break;
        }
        case MegaRequest::TYPE_CANCEL_ATTR_FILE:
        {
            int type = request->getParamType();
            handle h = request->getNodeHandle();
            const char *fa = request->getText();

            Node *node = client->nodebyhandle(h);

            if((!fa && !node) || (fa && ISUNDEF(h)))
            {
                e = API_EARGS;
                break;
            }

            string fileattrstring = fa ? string(fa) : node->fileattrstring;

            e = client->getfa(h, &fileattrstring, NULL, (fatype) type, 1);
            if (!e)
            {
                std::map<int, MegaRequestPrivate*>::iterator it = requestMap.begin();
                while(it != requestMap.end())
                {
                    MegaRequestPrivate *r = it->second;
                    it++;
                    if (r->getType() == MegaRequest::TYPE_GET_ATTR_FILE &&
                        r->getParamType() == request->getParamType() &&
                        r->getNodeHandle() == request->getNodeHandle())
                    {
                        fireOnRequestFinish(r, make_unique<MegaErrorPrivate>(API_EINCOMPLETE));
                    }
                }
                fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
            }
            break;
        }
        case MegaRequest::TYPE_RETRY_PENDING_CONNECTIONS:
        {
            bool disconnect = request->getFlag();
            bool includexfers = request->getNumber();
            const char *dnsservers = request->getText();

            client->abortbackoff(includexfers);
            if (disconnect)
            {
                client->disconnect();

                string servers;
                if (dnsservers && dnsservers[0])
                {
                    servers = dnsservers;
                }
#if TARGET_OS_IPHONE
                else
                {
                    // Workaround to get the IP of valid DNS servers on iOS
                    __res_state res;
                    bool valid;
                    if (res_ninit(&res) == 0)
                    {
                        union res_sockaddr_union u[MAXNS];
                        int nscount = res_getservers(&res, u, MAXNS);

                        for (int i = 0; i < nscount; i++)
                        {
                            char straddr[INET6_ADDRSTRLEN];
                            straddr[0] = 0;
                            valid = false;

                            if (u[i].sin.sin_family == PF_INET)
                            {
                                valid = mega_inet_ntop(PF_INET, &u[i].sin.sin_addr, straddr, sizeof(straddr)) == straddr;
                            }

                            if (u[i].sin6.sin6_family == PF_INET6)
                            {
                                valid = mega_inet_ntop(PF_INET6, &u[i].sin6.sin6_addr, straddr, sizeof(straddr)) == straddr;
                            }

                            if (valid && straddr[0])
                            {
                                if (servers.size())
                                {
                                    servers.append(",");
                                }
                                servers.append(straddr);
                            }
                        }

                        res_ndestroy(&res);
                    }

                    if (!servers.size())
                    {
                        LOG_warn << "Failed to get DNS servers at Retry Pending Connections";
                        e = API_EACCESS;    // ie. when iOS has no Internet connection at all
                        break;
                    }
                }
#endif
#ifndef __MINGW32__
                if (servers.size())
                {
                    LOG_debug << "Using DNS servers " << servers;
                    httpio->setdnsservers(servers.c_str());
                }
#endif
            }

            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
            break;
        }
        case MegaRequest::TYPE_INVITE_CONTACT:
        {
            const char *email = request->getEmail();
            const char *message = request->getText();
            int action = int(request->getNumber());
            MegaHandle contactLink = request->getNodeHandle();

            if(client->loggedin() != FULLACCOUNT)
            {
                e = API_EACCESS;
                break;
            }

            if(!email || !client->finduser(client->me)->email.compare(email))
            {
                e = API_EARGS;
                break;
            }

            if (action != OPCA_ADD && action != OPCA_REMIND && action != OPCA_DELETE)
            {
                e = API_EARGS;
                break;
            }

            client->setpcr(email, (opcactions_t)action, message, NULL, contactLink);
            break;
        }
        case MegaRequest::TYPE_REPLY_CONTACT_REQUEST:
        {
            handle h = request->getNodeHandle();
            int action = int(request->getNumber());

            if(h == INVALID_HANDLE || action < 0 || action > MegaContactRequest::REPLY_ACTION_IGNORE)
            {
                e = API_EARGS;
                break;
            }

            client->updatepcr(h, (ipcactions_t)action);
            break;
        }
        case MegaRequest::TYPE_REMOVE_CONTACT:
        {
            const char *email = request->getEmail();
            User *u = client->finduser(email);
            if(!u || u->show == HIDDEN || u->userhandle == client->me) { e = API_EARGS; break; }
            if (client->mBizMode == BIZ_MODE_SUBUSER && u->mBizMode != BIZ_MODE_UNKNOWN)
            {
                e = API_EMASTERONLY;
                break;
            }

            e = client->removecontact(email, HIDDEN);
            break;
        }
        case MegaRequest::TYPE_CREATE_ACCOUNT:
        {
            const char *email = request->getEmail();
            const char *password = request->getPassword();
            const char *name = request->getName();
            const char *pwkey = request->getPrivateKey();
            const char *sid = request->getSessionKey();
            bool resumeProcess = (request->getParamType() == 1);   // resume existing ephemeral account
            bool cancelProcess = (request->getParamType() == 2);
            handle lastPublicHandle = request->getNodeHandle();
            int lastPublicHandleType = request->getAccess();
            int64_t lastAccessTimestamp =request->getTransferredBytes();

            if (!ISUNDEF(lastPublicHandle)
                    && ((lastPublicHandleType <= mega::MegaApi::AFFILIATE_TYPE_INVALID
                            || lastPublicHandleType > mega::MegaApi::AFFILIATE_TYPE_CONTACT)
                        || !lastAccessTimestamp))
            {
                e = API_EARGS;
                break;
            }

            if ( (!resumeProcess && !cancelProcess && (!email || !name || (!password && !pwkey))) ||
                 (resumeProcess && !sid) )
            {
                e = API_EARGS; break;
            }

            byte pwbuf[SymmCipher::KEYLENGTH];
            handle uh = UNDEF;
            if (resumeProcess)
            {
                size_t pwkeyLen = strlen(sid);
                size_t pwkeyLenExpected = SymmCipher::KEYLENGTH * 4 / 3 + 3 + 10;
                if (pwkeyLen != pwkeyLenExpected ||
                        Base64::atob(sid, (byte*) &uh, sizeof uh) != sizeof uh ||
                        uh == UNDEF || sid[11] != '#' ||
                        Base64::atob(sid + 12, pwbuf, sizeof pwbuf) != sizeof pwbuf)
                {
                    e = API_EARGS; break;
                }
            }

            if (cancelProcess)
            {
                client->cancelsignup();
            }
            else
            {
                int reqtag = request->getTag();
                requestMap.erase(reqtag);

                abortPendingActions();

                requestMap[reqtag] = request;

                client->locallogout(false);

                if (resumeProcess)
                {
                    client->resumeephemeral(uh, pwbuf);
                }
                else
                {
                    client->createephemeral();
                }
            }
            break;
        }
        case MegaRequest::TYPE_SEND_SIGNUP_LINK:
        {
            const char *email = request->getEmail();
            const char *password = request->getPassword();
            const char *base64pwkey = request->getPrivateKey();
            const char *name = request->getName();

            if (client->loggedin() != EPHEMERALACCOUNT)
            {
                e = API_EACCESS;
                break;
            }

            if (!email || !name || (client->accountversion == 1 && !base64pwkey && !password))
            {
                e = API_EARGS;
                break;
            }

            if (client->accountversion == 1)
            {
                byte pwkey[SymmCipher::KEYLENGTH];
                if (password)
                {
                    client->pw_key(password, pwkey);
                }
                else if (Base64::atob(base64pwkey, (byte *)pwkey, sizeof pwkey) != SymmCipher::KEYLENGTH)
                {
                    e = API_EARGS;
                    break;
                }
                client->sendsignuplink(email, name, pwkey);
            }
            else if (client->accountversion == 2)
            {
                client->resendsignuplink2(email, name);
            }
            else
            {
                e = API_EINTERNAL;
                break;
            }
            break;
        }
        case MegaRequest::TYPE_QUERY_SIGNUP_LINK:
        {
            const char *link = request->getLink();
            if(!link)
            {
                e = API_EARGS;
                break;
            }

            const char* ptr = link;
            const char* tptr;

            if ((tptr = strstr(ptr,"#confirm")))
            {
                ptr = tptr+8;

                string code = Base64::atob(string(ptr));
                if (!code.empty())
                {
                    if (code.find("ConfirmCodeV2") != string::npos)
                    {
                        // “ConfirmCodeV2” (13B) || Email Confirmation Token (15B) || Email (>=5B) || \t || Fullname || Hash (8B)
                        size_t posEmail = 13 + 15;
                        size_t endEmail = code.find("\t", posEmail);
                        if (endEmail != string::npos)
                        {
                            string email = code.substr(posEmail, endEmail - posEmail);
                            request->setEmail(email.c_str());
                            request->setName(code.substr(endEmail + 1, code.size() - endEmail - 9).c_str());

                            sessiontype_t session = client->loggedin();
                            if (session == FULLACCOUNT)
                            {
                                e = (client->ownuser()->email == email) ? API_EEXPIRED : API_EACCESS;
                            }
                            else    // not-logged-in / ephemeral account / partially confirmed
                            {
                                client->confirmsignuplink2((const byte*)code.data(), unsigned(code.size()));
                            }
                        }
                        else
                        {
                            e = API_EARGS;
                        }
                    }
                    else
                    {
                        client->querysignuplink((const byte*)code.data(), unsigned(code.size()));
                    }
                }
                else
                {
                    e = API_EARGS;
                }
                break;
            }
            else if ((tptr = strstr(ptr,"#newsignup")))
            {
                ptr = tptr+10;

                unsigned len = unsigned((strlen(link)-(ptr-link))*3/4+4);
                byte *c = new byte[len];
                len = Base64::atob(ptr,c,len);

                if (len > 8)
                {
                    // extract email and email_hash from link
                    byte *email = c;
                    byte *sha512bytes = c+len-8;    // last 11 chars

                    // get the hash for the received email
                    Hash sha512;
                    sha512.add(email, len-8);
                    string sha512str;
                    sha512.get(&sha512str);

                    // and finally check it
                    if (memcmp(sha512bytes, sha512str.data(), 8) == 0)
                    {
                        email[len-8] = '\0';
                        request->setEmail((const char *)email);
                        delete[] c;

                        fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
                        break;
                    }
                }

                delete[] c;
            }

            e = API_EARGS;
            break;
        }
        case MegaRequest::TYPE_CONFIRM_ACCOUNT:
        {
            const char *link = request->getLink();
            const char *password = request->getPassword();
            const char *pwkey = request->getPrivateKey();

            if (!link)
            {
                e = API_EARGS;
                break;
            }

            const char* ptr = link;
            const char* tptr;

            if ((tptr = strstr(ptr,"#confirm"))) ptr = tptr+8;

            string code = Base64::atob(string(ptr));
            if (!code.empty())
            {
                if (code.find("ConfirmCodeV2") != string::npos)
                {
                    // “ConfirmCodeV2” (13B) || Email Confirmation Token (15B) || Email (>=5B) || \t || Fullname || Hash (8B)
                    size_t posEmail = 13 + 15;
                    size_t endEmail = code.find("\t", posEmail);
                    if (endEmail != string::npos)
                    {
                        string email = code.substr(posEmail, endEmail - posEmail);
                        request->setEmail(email.c_str());
                        request->setName(code.substr(endEmail + 1, code.size() - endEmail - 9).c_str());

                        sessiontype_t session = client->loggedin();
                        if (session == FULLACCOUNT)
                        {
                            e = (client->ownuser()->email == email) ? API_EEXPIRED : API_EACCESS;
                        }
                        else    // not-logged-in / ephemeral account / partially confirmed
                        {
                            client->confirmsignuplink2((const byte*)code.data(), unsigned(code.size()));
                        }
                    }
                    else
                    {
                        e = API_EARGS;
                    }
                }
                else if (!password && !pwkey)
                {
                    e = API_EARGS;
                }
                else
                {
                    client->querysignuplink((const byte*)code.data(), unsigned(code.size()));
                }
            }
            else
            {
                e = API_EARGS;
            }
            break;
        }
        case MegaRequest::TYPE_GET_RECOVERY_LINK:
        {
            const char *email = request->getEmail();
            bool hasMasterKey = request->getFlag();

            if (!email || !email[0])
            {
                e = API_EARGS;
                break;
            }

            client->getrecoverylink(email, hasMasterKey);
            break;
        }
        case MegaRequest::TYPE_QUERY_RECOVERY_LINK:
        {
            const char *link = request->getLink();

            const char* code;
            if (link && (code = strstr(link, "#recover")))
            {
                code += strlen("#recover");
            }
            else if (link && (code = strstr(link, "#verify")))
            {
                code += strlen("#verify");
            }
            else if (link && (code = strstr(link, "#cancel")))
            {
                code += strlen("#cancel");
            }
            else
            {
                e = API_EARGS;
                break;
            }

            client->queryrecoverylink(code);
            break;
        }
        case MegaRequest::TYPE_CONFIRM_RECOVERY_LINK:
        {
            const char *link = request->getLink();
            const char *newPwd = request->getPassword();

            const char* code;
            if (newPwd && link && (code = strstr(link, "#recover")))
            {
                code += strlen("#recover");
            }
            else
            {
                e = API_EARGS;
                break;
            }

            // concatenate query + confirm requests
            client->queryrecoverylink(code);
            break;
        }
        case MegaRequest::TYPE_GET_CANCEL_LINK:
        {
            if (client->loggedin() != FULLACCOUNT)
            {
                e = API_EACCESS;
                break;
            }

            User *u = client->finduser(client->me);
            if (!u)
            {
                e = API_ENOENT;
                break;
            }

            const char *pin = request->getText();
            client->getcancellink(u->email.c_str(), pin);
            break;
        }
        case MegaRequest::TYPE_CONFIRM_CANCEL_LINK:
        {
            const char *link = request->getLink();
            const char *pwd = request->getPassword();

            if (client->loggedin() != FULLACCOUNT)
            {
                e = API_EACCESS;
                break;
            }

            const char* code;
            if (!pwd || !link || !(code = strstr(link, "#cancel")))
            {
                e = API_EARGS;
                break;
            }
            
            if (!checkPassword(pwd))
            {
                e = API_ENOENT;
                break;
            }

            code += strlen("#cancel");
            client->confirmcancellink(code);
            break;
        }
        case MegaRequest::TYPE_GET_CHANGE_EMAIL_LINK:
        {
            if (client->loggedin() != FULLACCOUNT)
            {
                e = API_EACCESS;
                break;
            }

            const char *email = request->getEmail();
            const char *pin = request->getText();
            if (!email)
            {
                e = API_EARGS;
                break;
            }

            client->getemaillink(email, pin);
            break;
        }
        case MegaRequest::TYPE_CONFIRM_CHANGE_EMAIL_LINK:
        {
            const char *link = request->getLink();
            const char *pwd = request->getPassword();

            if (client->loggedin() != FULLACCOUNT)
            {
                e = API_EACCESS;
                break;
            }

            const char* code;
            if (pwd && link && (code = strstr(link, "#verify")))
            {
                code += strlen("#verify");
            }
            else
            {
                e = API_EARGS;
                break;
            }

            // concatenates query + validate pwd + confirm
            client->queryrecoverylink(code);
            break;
        }
        case MegaRequest::TYPE_PAUSE_TRANSFERS:
        {
            bool pause = request->getFlag();
            int direction = int(request->getNumber());
            if(direction != -1
                    && direction != MegaTransfer::TYPE_DOWNLOAD
                    && direction != MegaTransfer::TYPE_UPLOAD)
            {
                e = API_EARGS;
                break;
            }

            if(direction == -1)
            {
                client->pausexfers(PUT, pause, false, committer);
                client->pausexfers(GET, pause, false, committer);
            }
            else if(direction == MegaTransfer::TYPE_DOWNLOAD)
            {
                client->pausexfers(GET, pause, false, committer);
            }
            else
            {
                client->pausexfers(PUT, pause, false, committer);
            }

            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
            break;
        }
        case MegaRequest::TYPE_PAUSE_TRANSFER:
        {
            bool pause = request->getFlag();
            int transferTag = request->getTransferTag();
            MegaTransferPrivate* megaTransfer = getMegaTransferPrivate(transferTag);
            if (!megaTransfer)
            {
                e = API_ENOENT;
                break;
            }

            e = client->transferlist.pause(megaTransfer->getTransfer(), pause, committer);
            if (!e)
            {
                fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
            }
            break;
        }
        case MegaRequest::TYPE_MOVE_TRANSFER:
        {
            bool automove = request->getFlag();
            int transferTag = request->getTransferTag();
            int number = int(request->getNumber());

            if (!transferTag || !number)
            {
                e = API_EARGS;
                break;
            }

            MegaTransferPrivate* megaTransfer = getMegaTransferPrivate(transferTag);
            if (!megaTransfer)
            {
                e = API_ENOENT;
                break;
            }

            Transfer *transfer = megaTransfer->getTransfer();
            if (!transfer)
            {
                e = API_ENOENT;
                break;
            }

            if (automove)
            {
                switch (number)
                {
                    case MegaTransfer::MOVE_TYPE_UP:
                        client->transferlist.moveup(transfer, committer);
                        break;
                    case MegaTransfer::MOVE_TYPE_DOWN:
                        client->transferlist.movedown(transfer, committer);
                        break;
                    case MegaTransfer::MOVE_TYPE_TOP:
                        client->transferlist.movetofirst(transfer, committer);
                        break;
                    case MegaTransfer::MOVE_TYPE_BOTTOM:
                        client->transferlist.movetolast(transfer, committer);
                        break;
                    default:
                        e = API_EARGS;
                        break;
                }
            }
            else
            {
                MegaTransferPrivate* prevMegaTransfer = getMegaTransferPrivate(number);
                if (!prevMegaTransfer)
                {
                    e = API_ENOENT;
                    break;
                }

                Transfer *prevTransfer = prevMegaTransfer->getTransfer();
                if (!prevTransfer)
                {
                    client->transferlist.movetransfer(transfer, client->transferlist.transfers[transfer->type].begin(), committer);
                }
                else
                {
                    if (transfer->type != prevTransfer->type)
                    {
                        e = API_EARGS;
                    }
                    else
                    {
                        client->transferlist.movetransfer(transfer, prevTransfer, committer);
                    }
                }
            }

            if (!e)
            {
                fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
            }
            break;
        }

        case MegaRequest::TYPE_SET_MAX_CONNECTIONS:
        {
            int direction = request->getParamType();
            int connections = int(request->getNumber());

            if (connections <= 0 || (direction != -1
                    && direction != MegaTransfer::TYPE_DOWNLOAD
                    && direction != MegaTransfer::TYPE_UPLOAD))
            {
                e = API_EARGS;
                break;
            }

            if ((unsigned int) connections > MegaClient::MAX_NUM_CONNECTIONS)
            {
                e = API_ETOOMANY;
                break;
            }

            if (direction == -1)
            {
                client->setmaxconnections(GET, connections);
                client->setmaxconnections(PUT, connections);
            }
            else if (direction == MegaTransfer::TYPE_DOWNLOAD)
            {
                client->setmaxconnections(GET, connections);
            }
            else
            {
                client->setmaxconnections(PUT, connections);
            }

            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
            break;
        }
        case MegaRequest::TYPE_CANCEL_TRANSFER:
        {
            int transferTag = request->getTransferTag();
            MegaTransferPrivate* megaTransfer = getMegaTransferPrivate(transferTag);
            if (!megaTransfer)
            {
                e = API_ENOENT;
                break;
            }

            if (megaTransfer->getType() == MegaTransfer::TYPE_LOCAL_TCP_DOWNLOAD)
            {
                e = API_EACCESS;
                break;
            }

            if (megaTransfer->isFolderTransfer())
            {
                megaTransfer->setState(MegaTransfer::STATE_CANCELLED);
                fireOnTransferFinish(megaTransfer, make_unique<MegaErrorPrivate>(API_EINCOMPLETE), committer);
                fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
                break;
            }

            if (!megaTransfer->isStreamingTransfer())
            {
                Transfer *transfer = megaTransfer->getTransfer();
                if (!transfer)
                {
                    e = API_ENOENT;
                    break;
                }
                #ifdef _WIN32
                    if (transfer->type == GET)
                    {
                        transfer->localfilename.editStringDirect()->append("", 1);
                        WIN32_FILE_ATTRIBUTE_DATA fad;
                        if (GetFileAttributesExW((LPCWSTR)transfer->localfilename.editStringDirect()->data(), GetFileExInfoStandard, &fad))
                            SetFileAttributesW((LPCWSTR)transfer->localfilename.editStringDirect()->data(), fad.dwFileAttributes & ~FILE_ATTRIBUTE_HIDDEN);
                        transfer->localfilename.editStringDirect()->resize(transfer->localfilename.editStringDirect()->size()-1);
                    }
                #endif

                MegaErrorPrivate megaError(API_EINCOMPLETE);
                megaTransfer->setLastError(&megaError);

                bool found = false;
                file_list files = transfer->files;
                file_list::iterator iterator = files.begin();
                while (iterator != files.end())
                {
                    File *file = *iterator;
                    iterator++;
                    if (file->tag == transferTag)
                    {
                        found = true;
                        if (!file->syncxfer)
                        {
                            client->stopxfer(file, &committer);
                            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
                        }
                        else
                        {
                            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_EACCESS));
                        }
                        break;
                    }
                }

                if (!found)
                {
                    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_ENOENT));
                }
            }
            else
            {
                m_off_t startPos = megaTransfer->getStartPos();
                m_off_t endPos = megaTransfer->getEndPos();
                m_off_t totalBytes = endPos - startPos + 1;

                MegaNode *publicNode = megaTransfer->getPublicNode();
                if (publicNode)
                {
                    client->preadabort(publicNode->getHandle(), startPos, totalBytes);
                }
                else
                {
                    Node *node = client->nodebyhandle(megaTransfer->getNodeHandle());
                    if (node)
                    {
                        client->preadabort(node, startPos, totalBytes);
                    }
                }
                fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
            }
            break;
        }
        case MegaRequest::TYPE_CANCEL_TRANSFERS:
        {
            int direction = request->getParamType();
            bool flag = request->getFlag();

            if ((direction != MegaTransfer::TYPE_DOWNLOAD) && (direction != MegaTransfer::TYPE_UPLOAD))
            {
                e = API_EARGS;
                break;
            }

            if (!flag)
            {
                long long cancelledPending = 0;
                // 1. cancel queued transfers, not yet started (and not added to cache), up to the last one queued when cancelTransfers was queued
                auto lastQueuedTransfer = request->getTransferTag();

                for (auto transfer : transferQueue.popUpTo(lastQueuedTransfer, direction))
                {
                    fireOnTransferStart(transfer);
                    transfer->setState(MegaTransfer::STATE_CANCELLED);
                    fireOnTransferFinish(transfer, make_unique<MegaErrorPrivate>(API_EINCOMPLETE), committer);
                    cancelledPending++;
                }

                // 2. cancel regular in-transit transfers
                long long cancelledTransit = 0;
                for (transfer_map::iterator it = client->transfers[direction].begin() ; it != client->transfers[direction].end() ; it++)
                {
                    Transfer *t = it->second;
                    for (file_list::iterator it2 = t->files.begin(); it2 != t->files.end(); it2++)
                    {
                        if (!(*it2)->syncxfer)
                        {
                            cancelTransferByTag((*it2)->tag);
                            cancelledTransit++;
                        }
                    }
                }

                // 3. cancel folder in-transit transfers
                long long cancelledFolder = 0;
                for (std::map<int, MegaTransferPrivate *>::iterator it = folderTransferMap.begin(); it != folderTransferMap.end(); it++)
                {
                    MegaTransferPrivate *transfer = it->second;
                    if (!transfer->isSyncTransfer() && transfer->getType() == direction)
                    {
                        cancelTransferByTag(transfer->getTag());
                        cancelledFolder++;
                    }
                }

                LOG_verbose << "Cancelled transfers. dir: " << direction << " pending: " << cancelledPending << " folder: " << cancelledFolder << " transit: " << cancelledTransit;
                request->setFlag(true);
                requestQueue.push(request);
            }
            else
            {
                fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
            }
            break;
        }
        case MegaRequest::TYPE_ADD_BACKUP:
        {
            Node *parent = client->nodebyhandle(request->getNodeHandle());
            const char *localPath = request->getFile();
            if(!parent || (parent->type==FILENODE) || !localPath)
            {
                e = API_EARGS;
                break;
            }

            string utf8name(localPath);
            MegaBackupController *mbc = NULL;
            int tagexisting = 0;
            bool existing = false;
            for (std::map<int, MegaBackupController *>::iterator it = backupsMap.begin(); it != backupsMap.end(); ++it)
            {
                if (!strcmp(it->second->getLocalFolder(), utf8name.c_str()) && it->second->getMegaHandle() == request->getNodeHandle())
                {
                    existing = true;
                    mbc = it->second;
                    tagexisting = it->first;
                }
            }

            if (existing){
                LOG_debug << "Updating existing backup parameters: " <<  utf8name.c_str() << " to " << request->getNodeHandle();
                mbc->setPeriod(request->getNumber());
                mbc->setPeriodstring(request->getText());
                mbc->setMaxBackups(request->getNumRetry());
                mbc->setAttendPastBackups(request->getFlag());

                request->setTransferTag(tagexisting);
                if (!mbc->isValid())
                {
                    LOG_err << "Failed to update backup parameters: Invalid parameters";
                    e = API_EARGS;
                    break;
                }
            }
            else
            {
                int tag = request->getTag();
                int tagForFolderTansferTag = client->nextreqtag();
                string speriod = request->getText();
                bool attendPastBackups= request->getFlag();
                //TODO: add existence of local folder check (optional??)

                MegaBackupController *mbc = new MegaBackupController(this, tag, tagForFolderTansferTag, request->getNodeHandle(),
                                                                     utf8name.c_str(), attendPastBackups, speriod.c_str(),
                                                                     request->getNumber(), request->getNumRetry());
                mbc->setBackupListener(request->getBackupListener()); //TODO: should we add this in setBackup?
                if (mbc->isValid())
                {
                    backupsMap[tag] = mbc;
                    request->setTransferTag(tag);
                }
                else
                {
                    delete mbc;
                    e = API_EARGS;
                    break;
                }
            }

            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));

            break;
        }
        case MegaRequest::TYPE_REMOVE_BACKUP:
        {
            int backuptag = int(request->getNumber());
            bool found = false;
            bool flag = request->getFlag();


            map<int, MegaBackupController *>::iterator itr = backupsMap.find(backuptag) ;
            if (itr != backupsMap.end())
            {
                found = true;
            }

            if (found)
            {
                if (!flag)
                {
                    MegaRequestPrivate *requestabort = new MegaRequestPrivate(MegaRequest::TYPE_ABORT_CURRENT_BACKUP);
                    requestabort->setNumber(backuptag);

                    nextTag = client->nextreqtag();
                    requestabort->setTag(nextTag);
                    requestMap[nextTag]=requestabort;
                    fireOnRequestStart(requestabort);

                    e = processAbortBackupRequest(requestabort, e);
                    if (e)
                    {
                        LOG_err << "Failed to abort backup upon remove request";
                        fireOnRequestFinish(requestabort, make_unique<MegaErrorPrivate>(API_OK));
                    }
                    else
                    {
                        request->setFlag(true);
                        requestQueue.push(request);
                    }
                }
                else
                {
                    MegaBackupController * todelete = itr->second;
                    backupsMap.erase(backuptag);
                    fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
                    delete todelete;
                }
            }
            else
            {
                e = API_ENOENT;
            }

            break;
        }
        case MegaRequest::TYPE_ABORT_CURRENT_BACKUP:
        {
            e = processAbortBackupRequest(request, e);

            break;
        }
        case MegaRequest::TYPE_TIMER:
        {
            int delta = int(request->getNumber());
            TimerWithBackoff *twb = new TimerWithBackoff(client->rng, request->getTag());
            twb->backoff(delta);
            e = client->addtimer(twb);
            break;
        }
#ifdef ENABLE_SYNC
        case MegaRequest::TYPE_ADD_SYNC:
        {
            const char *localPath = request->getFile();
            Node *node = client->nodebyhandle(request->getNodeHandle());
            if(!node || (node->type==FILENODE) || !localPath)
            {
                e = API_EARGS;
                break;
            }

            MegaSyncPrivate *sync = new MegaSyncPrivate(localPath, node->nodehandle, -nextTag);
            sync->setListener(request->getSyncListener());
            sync->setRegExp(request->getRegExp());

            SyncConfig syncConfig{localPath, request->getNodeHandle(), 
                                  static_cast<fsfp_t>(request->getNumber()), 
                                  regExpToVector(request->getRegExp())};
            e = client->addsync(std::move(syncConfig), DEBRISFOLDER, NULL, -nextTag, sync);
            if (!e)
            {
                Sync *s = client->syncs.back();
                fsfp_t fsfp = s->fsfp;
                sync->setState(s->state);
                sync->setLocalFingerprint(fsfp);
                request->setNumber(fsfp);
                syncMap[-nextTag] = sync;
                fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
            }
            else
            {
                delete sync;
            }
            break;
        }
        case MegaRequest::TYPE_REMOVE_SYNCS:
        {
            sync_list::iterator it = client->syncs.begin();
            while (it != client->syncs.end())
            {
                Sync *sync = (*it);
                int tag = sync->tag;
                it++;

                client->delsync(sync);

                if (syncMap.find(tag) != syncMap.end())
                {
                    sync->appData = NULL;
                    MegaSyncPrivate *megaSync = syncMap.at(tag);
                    syncMap.erase(tag);
                    delete megaSync;
                }
            }
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
            break;
        }
        case MegaRequest::TYPE_REMOVE_SYNC:
        {
            handle nodehandle = request->getNodeHandle();
            sync_list::iterator it = client->syncs.begin();
            bool found = false;
            while(it != client->syncs.end())
            {
                Sync *sync = (*it);
                it++;

                int tag = sync->tag;
                if (!sync->localroot->node || sync->localroot->node->nodehandle == nodehandle)
                {
                    string path = sync->localroot->localname.toPath(*fsAccess);
                    if (!request->getFile() || sync->localroot->node)
                    {
                        request->setFile(path.c_str());
                    }

                    client->delsync(sync, request->getFlag());

                    if (syncMap.find(tag) != syncMap.end())
                    {
                        sync->appData = NULL;
                        MegaSyncPrivate *megaSync = syncMap.at(tag);
                        syncMap.erase(tag);
                        delete megaSync;
                    }

                    found = true;
                }
            }

            if (found)
            {
                fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
            }
            else
            {
                e = API_ENOENT;
            }

            break;
        }
#endif
        case MegaRequest::TYPE_REPORT_EVENT:
        {
            const char *details = request->getText();
            if(!details)
            {
                e = API_EARGS;
                break;
            }

            string event = "A"; //Application event
            int size = int(strlen(details));
            char *base64details = new char[size * 4 / 3 + 4];
            Base64::btoa((byte *)details, size, base64details);
            client->reportevent(event.c_str(), base64details);
            delete [] base64details;
            break;
        }
        case MegaRequest::TYPE_DELETE:
        {
#ifdef HAVE_LIBUV
            sdkMutex.unlock();
            httpServerStop();
            ftpServerStop();
            sdkMutex.lock();
#endif
            abortPendingActions();
            threadExit = 1;
            break;
        }
        case MegaRequest::TYPE_GET_PRICING:
        case MegaRequest::TYPE_GET_PAYMENT_ID:
        case MegaRequest::TYPE_UPGRADE_ACCOUNT:
        {
            if ((request->getType() == MegaRequest::TYPE_GET_PAYMENT_ID)
                && (request->getParamType() < mega::MegaApi::AFFILIATE_TYPE_INVALID
                    || request->getParamType() > mega::MegaApi::AFFILIATE_TYPE_CONTACT))
            {
               e = API_EARGS;
               break;
            }

            int method = int(request->getNumber());
            if(method != MegaApi::PAYMENT_METHOD_BALANCE && method != MegaApi::PAYMENT_METHOD_CREDIT_CARD)
            {
                e = API_EARGS;
                break;
            }

            client->purchase_enumeratequotaitems();
            break;
        }
        case MegaRequest::TYPE_SUBMIT_PURCHASE_RECEIPT:
        {
            const char* receipt = request->getText();
            int type = int(request->getNumber());
            handle lph = request->getNodeHandle();
            int phtype = request->getParamType();
            int64_t ts = request->getTransferredBytes();

            if (request->getParamType() < mega::MegaApi::AFFILIATE_TYPE_INVALID
                || request->getParamType() > mega::MegaApi::AFFILIATE_TYPE_CONTACT)
            {
               e = API_EARGS;
               break;
            }

            if(!receipt || (type != MegaApi::PAYMENT_METHOD_GOOGLE_WALLET
                            && type != MegaApi::PAYMENT_METHOD_ITUNES
                            && type != MegaApi::PAYMENT_METHOD_WINDOWS_STORE && type != MegaApi::PAYMENT_METHOD_HUAWEI_WALLET))
            {
                e = API_EARGS;
                break;
            }

            if(type == MegaApi::PAYMENT_METHOD_ITUNES && client->loggedin() != FULLACCOUNT)
            {
                e = API_EACCESS;
                break;
            }

            string base64receipt;
            if (type == MegaApi::PAYMENT_METHOD_GOOGLE_WALLET
                    || type == MegaApi::PAYMENT_METHOD_WINDOWS_STORE || type == MegaApi::PAYMENT_METHOD_HUAWEI_WALLET)
            {
                int len = int(strlen(receipt));
                base64receipt.resize(len * 4 / 3 + 4);
                base64receipt.resize(Base64::btoa((byte *)receipt, len, (char *)base64receipt.data()));
            }
            else // MegaApi::PAYMENT_METHOD_ITUNES
            {
                base64receipt = receipt;
            }

            client->submitpurchasereceipt(type, base64receipt.c_str(), lph, phtype, ts);
            break;
        }
        case MegaRequest::TYPE_CREDIT_CARD_STORE:
        {
            const char *ccplain = request->getText();
            e = client->creditcardstore(ccplain);
            break;
        }
        case MegaRequest::TYPE_CREDIT_CARD_QUERY_SUBSCRIPTIONS:
        {
            client->creditcardquerysubscriptions();
            break;
        }
        case MegaRequest::TYPE_CREDIT_CARD_CANCEL_SUBSCRIPTIONS:
        {
            const char* reason = request->getText();
            client->creditcardcancelsubscriptions(reason);
            break;
        }
        case MegaRequest::TYPE_GET_PAYMENT_METHODS:
        {
            client->getpaymentmethods();
            break;
        }
        case MegaRequest::TYPE_SUBMIT_FEEDBACK:
        {
            int rating = int(request->getNumber());
            const char *message = request->getText();

            if(rating < 1 || rating > 5)
            {
                e = API_EARGS;
                break;
            }

            if(!message)
            {
                message = "";
            }

            int size = int(strlen(message));
            char *base64message = new char[size * 4 / 3 + 4];
            Base64::btoa((byte *)message, size, base64message);

            char base64uhandle[12];
            Base64::btoa((const byte*)&client->me, MegaClient::USERHANDLE, base64uhandle);

            string feedback;
            feedback.resize(128 + strlen(base64message));

            snprintf((char *)feedback.data(), feedback.size(), "{\\\"r\\\":\\\"%d\\\",\\\"m\\\":\\\"%s\\\",\\\"u\\\":\\\"%s\\\"}", rating, base64message, base64uhandle);
            client->userfeedbackstore(feedback.c_str());
            delete [] base64message;
            break;
        }
        case MegaRequest::TYPE_SEND_EVENT:
        {
            int number = int(request->getNumber());
            const char *text = request->getText();

            if(number < 98900 || (number >= 99150 && (number < 99200 || number >= 99600)) || !text)
            {
                e = API_EARGS;
                break;
            }

            client->sendevent(number, text);
            break;
        }
        case MegaRequest::TYPE_SUPPORT_TICKET:
        {
            int type = request->getParamType();
            const char *message = request->getText();

            if ((type < 0 || type > 7) || !message)
            {
                e = API_EARGS;
                break;
            }

            client->supportticket(message, type);
            break;
        }
        case MegaRequest::TYPE_GET_USER_DATA:
        {
            const char *email = request->getEmail();
            if(request->getFlag() && !email)
            {
                e = API_EARGS;
                break;
            }

            if(!request->getFlag())
            {
                client->getuserdata();
            }
            else
            {
                client->getpubkey(email);
            }

            break;
        }
        case MegaRequest::TYPE_SEND_DEV_COMMAND:
        {
            const char *email = request->getEmail();
            const char *command = request->getName();
            if (!command)
            {
                e = API_EARGS;
                break;
            }

            long long q = request->getTotalBytes();
            int bs = request->getAccess();
            int us = request->getNumDetails();
#ifdef DEBUG

            bool isOdqSubcmd = !strcmp(command, "aodq");
            bool isTqSubcmd = !strcmp(command, "tq");
            bool isBsSubcmd = !strcmp(command, "bs");
            bool isUsSubcmd = !strcmp(command, "us");

            if (!isOdqSubcmd && !isTqSubcmd && !isBsSubcmd && !isUsSubcmd)
            {
                e = API_EARGS;
                break;
            }

            if (isTqSubcmd)
            {
                if (q < 0)
                {
                    e = API_EARGS;
                    break;
                }
            }
            else if (isBsSubcmd)
            {
                if (bs < -1 || bs > 2)
                {
                    e = API_EARGS;
                    break;
                }
            }
            else if (isUsSubcmd)
            {
                if (us == 1 || us < 0 || us > 9)
                {
                    e = API_EARGS;
                    break;
                }
            }
            client->senddevcommand(command, email, q, bs, us);
#else
            e = API_EACCESS;
#endif
            break;
        }
        case MegaRequest::TYPE_KILL_SESSION:
        {
            MegaHandle handle = request->getNodeHandle();
            if (handle == INVALID_HANDLE)
            {
                client->killallsessions();
            }
            else
            {
                client->killsession(handle);
            }
            break;
        }
        case MegaRequest::TYPE_GET_SESSION_TRANSFER_URL:
        {
            client->copysession();
            break;
        }
        case MegaRequest::TYPE_RESEND_VERIFICATION_EMAIL:
        {
            client->resendverificationemail();
            break;
        }
        case MegaRequest::TYPE_RESET_SMS_VERIFIED_NUMBER:
        {
            client->resetSmsVerifiedPhoneNumber();
            break;
        }
        case MegaRequest::TYPE_CLEAN_RUBBISH_BIN:
        {
            client->cleanrubbishbin();
            break;
        }
        case MegaRequest::TYPE_USE_HTTPS_ONLY:
        {
            bool usehttps = request->getFlag();
            if (client->usehttps != usehttps)
            {
                client->usehttps = usehttps;
                for (int d = GET; d == GET || d == PUT; d += PUT - GET)
                {
                    for (transfer_map::iterator it = client->transfers[d].begin(); it != client->transfers[d].end(); )
                    {
                        Transfer *t = it->second;
                        it++; // in case the failed() call deletes the transfer (which removes it from the list)
                        if (t->slot)
                        {
                            t->failed(API_EAGAIN, committer);
                        }
                    }
                }
            }
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
            break;
        }
        case MegaRequest::TYPE_SET_PROXY:
        {
            Proxy *proxy = request->getProxy();
            httpio->setproxy(proxy);
            delete proxy;
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
            break;
        }
        case MegaRequest::TYPE_APP_VERSION:
        {
            const char *appKey = request->getText();
            if (!appKey)
            {
                appKey = this->appKey.c_str();
            }
            client->getlastversion(appKey);
            break;
        }
        case MegaRequest::TYPE_GET_LOCAL_SSL_CERT:
        {
            client->getlocalsslcertificate();
            break;
        }
        case MegaRequest::TYPE_QUERY_DNS:
        {
            const char *hostname = request->getName();
            if (!hostname)
            {
                e = API_EARGS;
                break;
            }

            client->dnsrequest(hostname);
            break;
        }
        case MegaRequest::TYPE_QUERY_GELB:
        {
            const char *service = request->getName();
            int timeoutds = int(request->getNumber());
            int maxretries = request->getNumRetry();
            if (!service)
            {
                e = API_EARGS;
                break;
            }

            client->gelbrequest(service, timeoutds, maxretries);
            break;
        }
        case MegaRequest::TYPE_DOWNLOAD_FILE:
        {
            const char *url = request->getLink();
            const char *file = request->getFile();
            if (!url || !file)
            {
                e = API_EARGS;
                break;
            }

            client->httprequest(url, METHOD_GET, true);
            break;
        }
#ifdef ENABLE_CHAT
        case MegaRequest::TYPE_CHAT_CREATE:
        {
            MegaTextChatPeerList *chatPeers = request->getMegaTextChatPeerList();
            bool group = request->getFlag();
            const char *title = request->getText();
            bool publicchat = (request->getAccess() == 1);
            MegaStringMap *userKeyMap = request->getMegaStringMap();

            if (!chatPeers) // empty groupchat
            {
                MegaTextChatPeerListPrivate tmp = MegaTextChatPeerListPrivate();
                request->setMegaTextChatPeerList(&tmp);
                chatPeers = request->getMegaTextChatPeerList();
            }

            int numPeers = chatPeers->size();
            const string_map *uhkeymap = NULL;
            if(publicchat)
            {
                if (!group || !userKeyMap
                        || (userKeyMap->size() != numPeers + 1))    // includes our own key
                {
                    e = API_EARGS;
                    break;
                }
                uhkeymap = ((MegaStringMapPrivate*)userKeyMap)->getMap();
            }
            else
            {
                if (!group && numPeers != 1)
                {
                    e = API_EACCESS;
                    break;
                }
            }

            const userpriv_vector *userpriv = ((MegaTextChatPeerListPrivate*)chatPeers)->getList();

            // if 1:1 chat, peer is enforced to be moderator too
            if (!group && userpriv->at(0).second != PRIV_MODERATOR)
            {
                ((MegaTextChatPeerListPrivate*)chatPeers)->setPeerPrivilege(userpriv->at(0).first, PRIV_MODERATOR);
            }

            client->createChat(group, publicchat, userpriv, uhkeymap, title);
            break;
        }
        case MegaRequest::TYPE_CHAT_INVITE:
        {
            handle chatid = request->getNodeHandle();
            handle uh = request->getParentHandle();
            int access = request->getAccess();
            const char *title = request->getText();
            bool publicMode = request->getFlag();
            const char *unifiedKey = request->getSessionKey();

            if (publicMode && !unifiedKey)
            {
                e = API_EINCOMPLETE;
                break;
            }

            if (chatid == INVALID_HANDLE || uh == INVALID_HANDLE)
            {
                e = API_ENOENT;
                break;
            }

            textchat_map::iterator it = client->chats.find(chatid);
            if (it == client->chats.end())
            {
                e = API_ENOENT;
                break;
            }

            TextChat *chat = it->second;
            if (chat->publicchat != publicMode)
            {
                e = API_EACCESS;
                break;
            }

            // new participants of private chats require the title to be encrypted to them
            if (!chat->publicchat && (!chat->title.empty() && (!title || title[0] == '\0')))
            {
                e = API_EINCOMPLETE;
                break;
            }

            if (!chat->group || chat->priv != PRIV_MODERATOR)
            {
                e = API_EACCESS;
                break;
            }

            client->inviteToChat(chatid, uh, access, unifiedKey, title);
            break;
        }
        case MegaRequest::TYPE_CHAT_REMOVE:
        {
            handle chatid = request->getNodeHandle();
            handle uh = request->getParentHandle();

            if (chatid == INVALID_HANDLE)
            {
                e = API_ENOENT;
                break;
            }

            textchat_map::iterator it = client->chats.find(chatid);
            if (it == client->chats.end())
            {
                e = API_ENOENT;
                break;
            }
            TextChat *chat = it->second;

            // user is optional. If not provided, command apply to own user
            if (uh != INVALID_HANDLE)
            {
                if (!chat->group || (uh != client->me && chat->priv != PRIV_MODERATOR))
                {
                    e = API_EACCESS;
                    break;
                }
                client->removeFromChat(chatid, uh);
            }
            else
            {
                request->setParentHandle(client->me);
                client->removeFromChat(chatid, client->me);
            }
            break;
        }
        case MegaRequest::TYPE_CHAT_URL:
        {
            MegaHandle chatid = request->getNodeHandle();
            if (chatid == INVALID_HANDLE)
            {
                e = API_EARGS;
                break;
            }

            client->getUrlChat(chatid);
            break;
        }
        case MegaRequest::TYPE_CHAT_GRANT_ACCESS:
        {
            handle chatid = request->getParentHandle();
            handle h = request->getNodeHandle();
            const char *uid = request->getEmail();

            if (chatid == INVALID_HANDLE || h == INVALID_HANDLE || !uid)
            {
                e = API_ENOENT;
                break;
            }

            client->grantAccessInChat(chatid, h, uid);
            break;
        }
        case MegaRequest::TYPE_CHAT_REMOVE_ACCESS:
        {
            handle chatid = request->getParentHandle();
            handle h = request->getNodeHandle();
            const char *uid = request->getEmail();

            if (chatid == INVALID_HANDLE || h == INVALID_HANDLE || !uid)
            {
                e = API_ENOENT;
                break;
            }

            client->removeAccessInChat(chatid, h, uid);
            break;
        }
        case MegaRequest::TYPE_CHAT_UPDATE_PERMISSIONS:
        {
            handle chatid = request->getNodeHandle();
            handle uh = request->getParentHandle();
            int access = request->getAccess();

            if (chatid == INVALID_HANDLE || uh == INVALID_HANDLE)
            {
                e = API_ENOENT;
                break;
            }
            textchat_map::iterator it = client->chats.find(chatid);
            if (it == client->chats.end())
            {
                e = API_ENOENT;
                break;
            }
            TextChat *chat = it->second;
            if (!chat->group || chat->priv != PRIV_MODERATOR)
            {
                e = API_EACCESS;
                break;
            }

            client->updateChatPermissions(chatid, uh, access);
            break;
        }
        case MegaRequest::TYPE_CHAT_TRUNCATE:
        {
            MegaHandle chatid = request->getNodeHandle();
            handle messageid = request->getParentHandle();
            if (chatid == INVALID_HANDLE || messageid == INVALID_HANDLE)
            {
                e = API_EARGS;
                break;
            }

            textchat_map::iterator it = client->chats.find(chatid);
            if (it == client->chats.end())
            {
                e = API_ENOENT;
                break;
            }
            TextChat *chat = it->second;
            if (chat->priv != PRIV_MODERATOR)
            {
                e = API_EACCESS;
                break;
            }

            client->truncateChat(chatid, messageid);
            break;
        }
        case MegaRequest::TYPE_CHAT_SET_TITLE:
        {
            MegaHandle chatid = request->getNodeHandle();
            const char *title = request->getText();
            if (chatid == INVALID_HANDLE || title == NULL)
            {
                e = API_EARGS;
                break;
            }

            textchat_map::iterator it = client->chats.find(chatid);
            if (it == client->chats.end())
            {
                e = API_ENOENT;
                break;
            }
            TextChat *chat = it->second;
            if (!chat->group || chat->priv != PRIV_MODERATOR)
            {
                e = API_EACCESS;
                break;
            }

            client->setChatTitle(chatid, title);
            break;
        }
        case MegaRequest::TYPE_CHAT_PRESENCE_URL:
        {
            client->getChatPresenceUrl();
            break;
        }
        case MegaRequest::TYPE_REGISTER_PUSH_NOTIFICATION:
        {
            int deviceType = int(request->getNumber());
            const char *token = request->getText();

            if ((deviceType != MegaApi::PUSH_NOTIFICATION_ANDROID &&
                 deviceType != MegaApi::PUSH_NOTIFICATION_IOS_VOIP &&
                 deviceType != MegaApi::PUSH_NOTIFICATION_IOS_STD &&
                 deviceType != MegaApi::PUSH_NOTIFICATION_ANDROID_HUAWEI)
                    || token == NULL)
            {
                e = API_EARGS;
                break;
            }

            client->registerPushNotification(deviceType, token);
            break;
        }
        case MegaRequest::TYPE_CHAT_ARCHIVE:
        {
            MegaHandle chatid = request->getNodeHandle();
            bool archive = request->getFlag();
            if (chatid == INVALID_HANDLE)
            {
                e = API_ENOENT;
                break;
            }

            client->archiveChat(chatid, archive);
            break;
        }
        case MegaRequest::TYPE_SET_RETENTION_TIME:
        {
            MegaHandle chatid = request->getNodeHandle();
            int period = request->getNumDetails();

            if (chatid == INVALID_HANDLE)
            {
                e = API_EARGS;
                break;
            }

            textchat_map::iterator it = client->chats.find(chatid);
            if (it == client->chats.end())
            {
                e = API_ENOENT;
                break;
            }
            TextChat *chat = it->second;
            if (chat->priv != PRIV_MODERATOR)
            {
                e = API_EACCESS;
                break;
            }

            client->setchatretentiontime(chatid, period);
            break;
        }
        case MegaRequest::TYPE_CHAT_STATS:
        {
            const char *json = request->getName();
            if (!json)
            {
                e = API_EARGS;
                break;
            }

            int port = int(request->getNumber());
            if (port < 0 || port > 65535)
            {
                e = API_EARGS;
                break;
            }

            int type = request->getParamType();
            if (type == 1)
            {
               client->sendchatstats(json, port);
            }
            else if (type == 2)
            {
                const char *aid = request->getSessionKey();
                if (!aid)
                {
                    e = API_EARGS;
                    break;
                }

                client->sendchatlogs(json, aid, port);
            }
            else
            {
                e = API_EARGS;
            }
            break;
        }

        case MegaRequest::TYPE_RICH_LINK:
        {
            const char *url = request->getLink();
            if (!url)
            {
                e = API_EARGS;
                break;
            }

            client->richlinkrequest(url);
            break;
        }
        case MegaRequest::TYPE_CHAT_LINK_HANDLE:
        {
            MegaHandle chatid = request->getNodeHandle();
            bool del = request->getFlag();
            bool createifmissing = request->getAccess();
            if (del && createifmissing)
            {
                e = API_EARGS;
                break;
            }
            if (chatid == INVALID_HANDLE)
            {
                e = API_ENOENT;
                break;
            }
            textchat_map::iterator it = client->chats.find(chatid);
            if (it == client->chats.end())
            {
                e = API_ENOENT;
                break;
            }
            TextChat *chat = it->second;
            if (!chat->group || !chat->publicchat || chat->priv == PRIV_RM
                    || ((del || createifmissing) && chat->priv != PRIV_MODERATOR))
            {
                e = API_EACCESS;
                break;
            }

            client->chatlink(chatid, del, createifmissing);
            break;
        }

        case MegaRequest::TYPE_CHAT_LINK_URL:
        {
            MegaHandle publichandle = request->getNodeHandle();
            if (publichandle == INVALID_HANDLE)
            {
                e = API_ENOENT;
                break;
            }
            client->chatlinkurl(publichandle);
            break;
        }

        case MegaRequest::TYPE_SET_PRIVATE_MODE:
        {
            MegaHandle chatid = request->getNodeHandle();
            const char *title = request->getText();
            if (chatid == INVALID_HANDLE)
            {
                e = API_ENOENT;
                break;
            }

            textchat_map::iterator it = client->chats.find(chatid);
            if (it == client->chats.end())
            {
                e = API_ENOENT;
                break;
            }
            TextChat *chat = it->second;
            if (!chat->publicchat)
            {
                e = API_EEXIST;
                break;
            }
            if (!chat->group || chat->priv != PRIV_MODERATOR)
            {
                e = API_EACCESS;
                break;
            }
            if (!chat->title.empty() && (!title || title[0] == '\0'))
            {
                e = API_EARGS;
                break;
            }

            client->chatlinkclose(chatid, title);
            break;
        }

        case MegaRequest::TYPE_AUTOJOIN_PUBLIC_CHAT:
        {
            MegaHandle publichandle = request->getNodeHandle();
            const char *unifiedkey = request->getSessionKey();

            if (publichandle == INVALID_HANDLE)
            {
                e = API_ENOENT;
                break;
            }

            if (unifiedkey == NULL)
            {
                e = API_EINCOMPLETE;
                break;
            }
            client->chatlinkjoin(publichandle, unifiedkey);
            break;
        }
#endif
        case MegaRequest::TYPE_WHY_AM_I_BLOCKED:
        {
            client->whyamiblocked();
            break;
        }
        case MegaRequest::TYPE_CONTACT_LINK_CREATE:
        {
            client->contactlinkcreate(request->getFlag());
            break;
        }
        case MegaRequest::TYPE_CONTACT_LINK_QUERY:
        {
            handle h = request->getNodeHandle();
            if (ISUNDEF(h))
            {
                e = API_EARGS;
                break;
            }

            client->contactlinkquery(h);
            break;
        }
        case MegaRequest::TYPE_CONTACT_LINK_DELETE:
        {
            handle h = request->getNodeHandle();
            client->contactlinkdelete(h);
            break;
        }
        case MegaRequest::TYPE_KEEP_ME_ALIVE:
        {
            int type = request->getParamType();
            bool enable = request->getFlag();

            if (type != MegaApi::KEEP_ALIVE_CAMERA_UPLOADS)
            {
                e = API_EARGS;
                break;
            }

            client->keepmealive(type, enable);
            break;
        }
        case MegaRequest::TYPE_GET_PSA:
        {
            client->getpsa();
            break;
        }
        case MegaRequest::TYPE_FOLDER_INFO:
        {
            MegaHandle h = request->getNodeHandle();
            if (ISUNDEF(h))
            {
                e = API_EARGS;
                break;
            }

            Node *node = client->nodebyhandle(h);
            if (!node)
            {
                e = API_ENOENT;
                break;
            }

            if (node->type == FILENODE)
            {
                e = API_EARGS;
                break;
            }

            TreeProcFolderInfo folderProcessor;
            client->proctree(node, &folderProcessor, false, false);
            MegaFolderInfo *folderInfo = folderProcessor.getResult();
            request->setMegaFolderInfo(folderInfo);
            delete folderInfo;

            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(API_OK));
            break;
        }
        case MegaRequest::TYPE_GET_ACHIEVEMENTS:
        {
            if (request->getFlag())
            {
                client->getmegaachievements(request->getAchievementsDetails());
            }
            else
            {
                client->getaccountachievements(request->getAchievementsDetails());
            }
            break;
        }
        case MegaRequest::TYPE_USERALERT_ACKNOWLEDGE:
        {
            client->acknowledgeuseralerts();
            break;
        }
        case MegaRequest::TYPE_PUBLIC_LINK_INFORMATION:
        {
            const char *link = request->getLink();
            if (!link)
            {
                e = API_EARGS;
                break;
            }

            handle h = UNDEF;
            byte folderkey[FOLDERNODEKEYLENGTH];
            e = client->parsepubliclink(link, h, folderkey, true);
            if (e == API_OK)
            {
                request->setNodeHandle(h);
                Base64Str<FOLDERNODEKEYLENGTH> folderkeyB64(folderkey);
                request->setPrivateKey(folderkeyB64.chars);
                client->getpubliclinkinfo(h);
            }
            break;
        }
        case MegaRequest::TYPE_GET_BACKGROUND_UPLOAD_URL:
        {
            client->reqs.add(new CommandPutFileBackgroundURL(request->getNumber(), client->putmbpscap, request->getTag()));
            break;
        }
        case MegaRequest::TYPE_COMPLETE_BACKGROUND_UPLOAD:
        {
            MegaBackgroundMediaUploadPrivate* uploadState = static_cast<MegaBackgroundMediaUploadPrivate*>(request->getMegaBackgroundMediaUploadPtr());
            const char* utf8Name = request->getName();
            MegaHandle parentHandle = request->getParentHandle();
            const char *uploadToken = request->getSessionKey();
            const char* fingerprintOriginal = request->getPassword();
            const char* fingerprint = request->getNewPassword();

            if (!fingerprint || !uploadState || !utf8Name || !uploadToken || ISUNDEF(parentHandle))
            {
                e = API_EINCOMPLETE;
                break;
            }
            unsigned char* binTok;
            size_t binTokSize;
            MegaApi::base64ToBinary(uploadToken, &binTok, &binTokSize);
            std::string binaryUploadToken((char*)binTok, binTokSize);
            delete[] binTok;
            if (binaryUploadToken.size() != 36)
            {
                LOG_err << "Invalid upload token";
                e = API_EARGS;
                break;
            }

            Node *parentNode = client->nodebyhandle(parentHandle);
            if (!parentNode)
            {
                LOG_err << "Parent node doesn't exist anymore";
                e = API_ENOENT;
                break;
            }

            std::unique_ptr<char[]> megafingerprint(getMegaFingerprintFromSdkFingerprint(fingerprint));
            if (!megafingerprint)
            {
                LOG_err << "Bad fingerprint";
                e = API_EARGS;
                break;
            }

            NewNode* newnode = new NewNode[1];
            newnode->source = NEW_UPLOAD;
            newnode->type = FILENODE;
            memcpy(newnode->uploadtoken, binaryUploadToken.data(), binaryUploadToken.size());
            newnode->parenthandle = UNDEF;
            newnode->uploadhandle = client->getuploadhandle();
            newnode->attrstring.reset(new string);
            newnode->fileattributes.reset(new string);

            appendFileAttribute(*newnode->fileattributes, GfxProc::THUMBNAIL, uploadState->thumbnailFA);
            appendFileAttribute(*newnode->fileattributes, GfxProc::PREVIEW, uploadState->previewFA);

#ifdef USE_MEDIAINFO
            if (uploadState->mediaproperties.isPopulated())
            {
                if (!newnode->fileattributes->empty())
                {
                    newnode->fileattributes->append("/");
                }
                newnode->fileattributes->append(MediaProperties::encodeMediaPropertiesAttributes(uploadState->mediaproperties,
                                                        (uint32_t*)(uploadState->filekey + FILENODEKEYLENGTH / 2)));
            }
#endif
            AttrMap attrs;
            attrs.map['n'] = utf8Name;
            attrs.map['c'] = megafingerprint.get();
            if (fingerprintOriginal)
            {
                attrs.map[MAKENAMEID2('c', '0')] = fingerprintOriginal;
            }
            int lat, lon;
            encodeCoordinates(uploadState->latitude, uploadState->longitude, lat, lon);
            if (API_OK != (e = updateAttributesMapWithCoordinates(attrs, lat, lon, uploadState->unshareableGPS, client)))
            {
                break;
            }
            string tattrstring;
            attrs.getjson(&tattrstring);
            SymmCipher cipher;
            cipher.setkey(uploadState->filekey);
            client->makeattr(&cipher, newnode->attrstring, tattrstring.c_str());
            newnode->nodekey.assign((char*)uploadState->filekey, FILENODEKEYLENGTH);
            SymmCipher::xorblock((const byte*)newnode->nodekey.data() + SymmCipher::KEYLENGTH, (byte*)newnode->nodekey.data());

            if (!client->versions_disabled)
            {
                string name(utf8Name);
                newnode->ovhandle = client->getovhandle(parentNode, &name);
            }

            client->reqs.add(new CommandPutNodes(client, parentHandle, NULL, newnode, 1, request->getTag(), PUTNODES_APP));
            break;
        }
        case MegaRequest::TYPE_VERIFY_CREDENTIALS:
        {
            handle uh = request->getNodeHandle();
            bool isReset = request->getFlag();
            if (isReset)
            {
                e = client->resetCredentials(uh);
            }
            else
            {
                e = client->verifyCredentials(uh);
            }
            break;
        }
        case MegaRequest::TYPE_SEND_SMS_VERIFICATIONCODE:
        {
            string phoneNumber = request->getText();
            bool reverifying_whitelisted = request->getFlag();

            e = client->smsverificationsend(phoneNumber, reverifying_whitelisted);
            break;
        }
        case MegaRequest::TYPE_CHECK_SMS_VERIFICATIONCODE:
        {
            string code = request->getText();
            e = client->smsverificationcheck(code);
            // FIXME: if the API returned the new state and the verified phone number in
            // the response to the code's verification, the following block can be deleted
            if (e == API_OK)
            {
                client->reqs.add(new CommandGetUserData(client));
            }
            break;
        }
        case MegaRequest::TYPE_GET_REGISTERED_CONTACTS:
        {
            const auto contacts = request->getMegaStringMap();
            if (contacts)
            {
                map<const char*, const char*> contactsMap; // non-owning
                const auto contactsKeys = std::unique_ptr<MegaStringList>{contacts->getKeys()};
                for (int i = 0; i < contactsKeys->size(); ++i)
                {
                    const auto key = contactsKeys->get(i);
                    contactsMap[key] = contacts->get(key);
                }
                client->reqs.add(new CommandGetRegisteredContacts{client, contactsMap});
            }
            else
            {
                e = API_EARGS;
                assert(false && "contacts must be valid");
            }
            break;
        }
        case MegaRequest::TYPE_GET_COUNTRY_CALLING_CODES:
        {
            client->reqs.add(new CommandGetCountryCallingCodes{client});
            break;
        }
        case MegaRequest::TYPE_GET_MISC_FLAGS:
        {
            if (client->loggedin())
            {
                // it only returns not-user-related flags (ie. server-sider-rubbish scheduler is missing)
                e = API_EACCESS;
                break;
            }
            client->getmiscflags();
            break;
        }
        default:
        {
            e = API_EINTERNAL;
        }
        }

        if(e)
        {
            LOG_err << "Error starting request: " << e;
            fireOnRequestFinish(request, make_unique<MegaErrorPrivate>(e));
        }
    }
}

char* MegaApiImpl::stringToArray(string &buffer)
{
    char *newbuffer = new char[buffer.size()+1];
    memcpy(newbuffer, buffer.data(), buffer.size());
    newbuffer[buffer.size()]='\0';
    return newbuffer;
}

void MegaApiImpl::updateStats()
{
    sdkMutex.lock();
    if (pendingDownloads && !client->transfers[GET].size())
    {
        LOG_warn << "Incorrect number of pending downloads: " << pendingDownloads;
        pendingDownloads = 0;
    }

    if (pendingUploads && !client->transfers[PUT].size())
    {
        LOG_warn << "Incorrect number of pending uploads: " << pendingUploads;
        pendingUploads = 0;
    }
    sdkMutex.unlock();
}

long long MegaApiImpl::getNumNodes()
{
    return client->totalNodes;
}

long long MegaApiImpl::getTotalDownloadedBytes()
{
    return totalDownloadedBytes;
}

long long MegaApiImpl::getTotalUploadedBytes()
{
    return totalUploadedBytes;
}

long long MegaApiImpl::getTotalDownloadBytes()
{
    return totalDownloadBytes;
}

long long MegaApiImpl::getTotalUploadBytes()
{
    return totalUploadBytes;
}

void MegaApiImpl::update()
{
#ifdef ENABLE_SYNC
    sdkMutex.lock();

    LOG_debug << "PendingCS? " << (client->pendingcs != NULL);
    LOG_debug << "PendingFA? " << client->activefa.size() << " active, " << client->queuedfa.size() << " queued";
    LOG_debug << "FLAGS: " << client->syncactivity
              << " " << client->syncdownrequired << " " << client->syncdownretry
              << " " << client->syncfslockretry << " " << client->syncfsopsfailed
              << " " << client->syncnagleretry << " " << client->syncscanfailed
              << " " << client->syncops << " " << client->syncscanstate
              << " " << client->faputcompletion.size() << " " << client->synccreate.size()
              << " " << client->fetchingnodes << " " << client->pendingfa.size()
              << " " << client->xferpaused[0] << " " << client->xferpaused[1]
              << " " << client->transfers[0].size() << " " << client->transfers[1].size()
              << " " << client->syncscanstate << " " << client->statecurrent
              << " " << client->syncadding << " " << client->syncdebrisadding
              << " " << client->umindex.size() << " " << client->uhindex.size();
    LOG_debug << "UL speed: " << httpio->uploadSpeed << "  DL speed: " << httpio->downloadSpeed;

    sdkMutex.unlock();
#endif

    waiter->notify();
}

int MegaApiImpl::isWaiting()
{
#ifdef ENABLE_SYNC
    if (client->syncfslockretry || client->syncfsopsfailed)
    {
        LOG_debug << "SDK waiting for a blocked file: " << client->blockedfile.toPath(*fsAccess);
        return RETRY_LOCAL_LOCK;
    }
#endif

    if (waitingRequest)
    {
        LOG_debug << "SDK waiting for a request. Reason: " << waitingRequest;
    }
    return waitingRequest;
}

int MegaApiImpl::areServersBusy()
{
    return isWaiting();
}

void MegaApiImpl::lockMutex()
{
    sdkMutex.lock();
}

void MegaApiImpl::unlockMutex()
{
    sdkMutex.unlock();
}

bool MegaApiImpl::tryLockMutexFor(long long time)
{
    if (time <= 0)
    {
        return sdkMutex.try_lock();
    }
    else
    {
        return sdkMutex.try_lock_for(std::chrono::milliseconds(time));
    }
}

TreeProcCopy::TreeProcCopy()
{
    nn = NULL;
    nc = 0;
}

void TreeProcCopy::allocnodes()
{
    if(nc) nn = new NewNode[nc];
}

TreeProcCopy::~TreeProcCopy()
{
    //Will be deleted in putnodes_result
    //delete[] nn;
}

// determine node tree size (nn = NULL) or write node tree to new nodes array
void TreeProcCopy::proc(MegaClient* client, Node* n)
{
    if (nn)
    {
        string attrstring;
        SymmCipher key;
        NewNode* t = nn+--nc;

        // copy node
        t->source = NEW_NODE;
        t->type = n->type;
        t->nodehandle = n->nodehandle;
        t->parenthandle = n->parent ? n->parent->nodehandle : UNDEF;

        // copy key (if file) or generate new key (if folder)
        if (n->type == FILENODE) t->nodekey = n->nodekey();
        else
        {
            byte buf[FOLDERNODEKEYLENGTH];
            client->rng.genblock(buf,sizeof buf);
            t->nodekey.assign((char*)buf,FOLDERNODEKEYLENGTH);
        }

        t->attrstring.reset(new string);
        if(t->nodekey.size())
        {
            key.setkey((const byte*)t->nodekey.data(),n->type);

            AttrMap tattrs;
            tattrs.map = n->attrs.map;
            nameid rrname = AttrMap::string2nameid("rr");
            attr_map::iterator it = tattrs.map.find(rrname);
            if (it != tattrs.map.end())
            {
                LOG_debug << "Removing rr attribute";
                tattrs.map.erase(it);
            }

            tattrs.getjson(&attrstring);
            client->makeattr(&key, t->attrstring, attrstring.c_str());
        }
    }
    else nc++;
}

long long TransferQueue::getLastPushedTag() const
{
    return lastPushedTransfer;
}

TransferQueue::TransferQueue()
{
}

void TransferQueue::push(MegaTransferPrivate *transfer)
{
    mutex.lock();
    transfers.push_back(transfer);
    transfer->setPlaceInQueue(++lastPushedTransfer);
    mutex.unlock();
}

void TransferQueue::push_front(MegaTransferPrivate *transfer)
{
    mutex.lock();
    transfers.push_front(transfer);
    mutex.unlock();
}

MegaTransferPrivate *TransferQueue::pop()
{
    mutex.lock();
    if(transfers.empty())
    {
        mutex.unlock();
        return NULL;
    }
    MegaTransferPrivate *transfer = transfers.front();
    transfers.pop_front();
    mutex.unlock();
    return transfer;
}

std::vector<MegaTransferPrivate *> TransferQueue::popUpTo(int lastQueuedTransfer, int direction)
{
    std::lock_guard<std::mutex> g(mutex);
    std::vector<MegaTransferPrivate*> toret;
    for (auto it = transfers.begin(); it != transfers.end();)
    {
        MegaTransferPrivate *transfer = *it;
        if (transfer->getPlaceInQueue() > lastQueuedTransfer)
        {
            break;
        }

        if (!transfer->isSyncTransfer() && transfer->getType() == direction)
        {
            toret.push_back(transfer);
            it = transfers.erase(it);
        }
        else
        {
            it++;
        }
    }
    return toret;
}

void TransferQueue::removeWithFolderTag(int folderTag, std::function<void(MegaTransferPrivate *)> callback)
{
    for (auto it = transfers.begin(); it != transfers.end();)
    {
        if ((*it)->getFolderTransferTag() == folderTag)
        {
            if (callback)
            {
                callback(*it);
            }
            it = transfers.erase(it);
        }
        else
        {
            it++;
        }
    }
}

void TransferQueue::removeListener(MegaTransferListener *listener)
{
    mutex.lock();

    std::deque<MegaTransferPrivate *>::iterator it = transfers.begin();
    while(it != transfers.end())
    {
        MegaTransferPrivate *transfer = (*it);
        if(transfer->getListener() == listener)
            transfer->setListener(NULL);
        it++;
    }

    mutex.unlock();
}

RequestQueue::RequestQueue()
{
}

void RequestQueue::push(MegaRequestPrivate *request)
{
    mutex.lock();
    requests.push_back(request);
    mutex.unlock();
}

void RequestQueue::push_front(MegaRequestPrivate *request)
{
    mutex.lock();
    requests.push_front(request);
    mutex.unlock();
}

MegaRequestPrivate *RequestQueue::pop()
{
    mutex.lock();
    if(requests.empty())
    {
        mutex.unlock();
        return NULL;
    }
    MegaRequestPrivate *request = requests.front();
    requests.pop_front();
    mutex.unlock();
    return request;
}

MegaRequestPrivate *RequestQueue::front()
{
    mutex.lock();
    if(requests.empty())
    {
        mutex.unlock();
        return NULL;
    }
    MegaRequestPrivate *request = requests.front();
    mutex.unlock();
    return request;
}

void RequestQueue::removeListener(MegaRequestListener *listener)
{
    mutex.lock();

    std::deque<MegaRequestPrivate *>::iterator it = requests.begin();
    while(it != requests.end())
    {
        MegaRequestPrivate *request = (*it);
        if(request->getListener()==listener)
            request->setListener(NULL);
        it++;
    }

    mutex.unlock();
}

#ifdef ENABLE_SYNC
void RequestQueue::removeListener(MegaSyncListener *listener)
{
    mutex.lock();

    std::deque<MegaRequestPrivate *>::iterator it = requests.begin();
    while(it != requests.end())
    {
        MegaRequestPrivate *request = (*it);
        if(request->getSyncListener()==listener)
            request->setSyncListener(NULL);
        it++;
    }

    mutex.unlock();
}
#endif

void RequestQueue::removeListener(MegaBackupListener *listener)
{
    mutex.lock();

    std::deque<MegaRequestPrivate *>::iterator it = requests.begin();
    while(it != requests.end())
    {
        MegaRequestPrivate *request = (*it);
        if(request->getBackupListener()==listener)
            request->setBackupListener(NULL);
        it++;
    }

    mutex.unlock();
}

MegaHashSignatureImpl::MegaHashSignatureImpl(const char *base64Key)
{
    hashSignature = new HashSignature(new Hash());
    asymmCypher = new AsymmCipher();

    string pubks;
    int len = int(strlen(base64Key)/4*3+3);
    pubks.resize(len);
    pubks.resize(Base64::atob(base64Key, (byte *)pubks.data(), len));
    asymmCypher->setkey(AsymmCipher::PUBKEY,(byte*)pubks.data(), int(pubks.size()));
}

MegaHashSignatureImpl::~MegaHashSignatureImpl()
{
    delete hashSignature;
    delete asymmCypher;
}

void MegaHashSignatureImpl::init()
{
    hashSignature->get(asymmCypher, NULL, 0);
}

void MegaHashSignatureImpl::add(const char *data, unsigned size)
{
    hashSignature->add((const byte *)data, size);
}

bool MegaHashSignatureImpl::checkSignature(const char *base64Signature)
{
    char signature[512];
    int l = Base64::atob(base64Signature, (byte *)signature, sizeof(signature));
    if(l != sizeof(signature))
        return false;

    return hashSignature->checksignature(asymmCypher, (const byte *)signature, sizeof(signature));
}

int MegaAccountDetailsPrivate::getProLevel()
{
    return details.pro_level;
}

int64_t MegaAccountDetailsPrivate::getProExpiration()
{
    return details.pro_until;
}

int MegaAccountDetailsPrivate::getSubscriptionStatus()
{
    if(details.subscription_type == 'S')
    {
        return MegaAccountDetails::SUBSCRIPTION_STATUS_VALID;
    }

    if(details.subscription_type == 'R')
    {
        return MegaAccountDetails::SUBSCRIPTION_STATUS_INVALID;
    }

    return MegaAccountDetails::SUBSCRIPTION_STATUS_NONE;
}

int64_t MegaAccountDetailsPrivate::getSubscriptionRenewTime()
{
    return details.subscription_renew;
}

char *MegaAccountDetailsPrivate::getSubscriptionMethod()
{
    return MegaApi::strdup(details.subscription_method.c_str());
}

char *MegaAccountDetailsPrivate::getSubscriptionCycle()
{
    return MegaApi::strdup(details.subscription_cycle);
}

long long MegaAccountDetailsPrivate::getStorageMax()
{
    return details.storage_max;
}

long long MegaAccountDetailsPrivate::getStorageUsed()
{
    return details.storage_used;
}

long long MegaAccountDetailsPrivate::getVersionStorageUsed()
{
    long long total = 0;

    handlestorage_map::iterator it;
    for (it = details.storage.begin(); it != details.storage.end(); it++)
    {
        total += it->second.version_bytes;
    }

    return total;
}

long long MegaAccountDetailsPrivate::getTransferMax()
{
    return details.transfer_max;
}

long long MegaAccountDetailsPrivate::getTransferOwnUsed()
{
    return details.transfer_own_used;
}

long long MegaAccountDetailsPrivate::getTransferSrvUsed()
{
    return details.transfer_srv_used;
}

long long MegaAccountDetailsPrivate::getTransferUsed()
{
    long long total = details.transfer_srv_used + details.transfer_own_used + getTemporalBandwidth();
    // in case the total exceed the maximum allowance (due to the free IP-based quota)...
    if (details.transfer_max && total > details.transfer_max) //do not limit for free user (no max allowance configured)
    {
        total = details.transfer_max;
    }
    return total;
}

int MegaAccountDetailsPrivate::getNumUsageItems()
{
    return int(details.storage.size());
}

long long MegaAccountDetailsPrivate::getStorageUsed(MegaHandle handle)
{
    handlestorage_map::iterator it = details.storage.find(handle);
    if (it != details.storage.end())
    {
        return it->second.bytes;
    }
    else
    {
        return 0;
    }
}

long long MegaAccountDetailsPrivate::getNumFiles(MegaHandle handle)
{
    handlestorage_map::iterator it = details.storage.find(handle);
    if (it != details.storage.end())
    {
        return it->second.files;
    }
    else
    {
        return 0;
    }
}

long long MegaAccountDetailsPrivate::getNumFolders(MegaHandle handle)
{
    handlestorage_map::iterator it = details.storage.find(handle);
    if (it != details.storage.end())
    {
        return it->second.folders;
    }
    else
    {
        return 0;
    }
}

long long MegaAccountDetailsPrivate::getVersionStorageUsed(MegaHandle handle)
{
    handlestorage_map::iterator it = details.storage.find(handle);
    if (it != details.storage.end())
    {
        return it->second.version_bytes;
    }
    else
    {
        return 0;
    }
}

long long MegaAccountDetailsPrivate::getNumVersionFiles(MegaHandle handle)
{
    handlestorage_map::iterator it = details.storage.find(handle);
    if (it != details.storage.end())
    {
        return it->second.version_files;
    }
    else
    {
        return 0;
    }
}

MegaAccountDetails* MegaAccountDetailsPrivate::copy()
{
    return new MegaAccountDetailsPrivate(&details);
}

int MegaAccountDetailsPrivate::getNumBalances() const
{
    return int(details.balances.size());
}

MegaAccountBalance *MegaAccountDetailsPrivate::getBalance(int i) const
{
    if ((unsigned int)i < details.balances.size())
    {
        return MegaAccountBalancePrivate::fromAccountBalance(&(details.balances[(unsigned int)i]));
    }
    return NULL;
}

int MegaAccountDetailsPrivate::getNumSessions() const
{
    return int(details.sessions.size());
}

MegaAccountSession *MegaAccountDetailsPrivate::getSession(int i) const
{
    if ((unsigned int)i < details.sessions.size())
    {
        return MegaAccountSessionPrivate::fromAccountSession(&(details.sessions[(unsigned int)i]));
    }
    return NULL;
}

int MegaAccountDetailsPrivate::getNumPurchases() const
{
    return int(details.purchases.size());
}

MegaAccountPurchase *MegaAccountDetailsPrivate::getPurchase(int i) const
{
    if ((unsigned int)i < details.purchases.size())
    {
        return MegaAccountPurchasePrivate::fromAccountPurchase(&(details.purchases[(unsigned int)i]));
    }
    return NULL;
}

int MegaAccountDetailsPrivate::getNumTransactions() const
{
    return int(details.transactions.size());
}

MegaAccountTransaction *MegaAccountDetailsPrivate::getTransaction(int i) const
{
    if ((unsigned int)i < details.transactions.size())
    {
        return MegaAccountTransactionPrivate::fromAccountTransaction(&(details.transactions[(unsigned int)i]));
    }
    return NULL;
}

int MegaAccountDetailsPrivate::getTemporalBandwidthInterval()
{
    return int(details.transfer_hist.size());
}

long long MegaAccountDetailsPrivate::getTemporalBandwidth()
{
    long long result = 0;
    for (unsigned int i = 0; i < details.transfer_hist.size(); i++)
    {
        result += details.transfer_hist[i];
    }
    return result;
}

bool MegaAccountDetailsPrivate::isTemporalBandwidthValid()
{
    return details.transfer_hist_valid;
}

MegaErrorPrivate::MegaErrorPrivate(int errorCode)
    : MegaError(errorCode)
{
}

MegaErrorPrivate::MegaErrorPrivate(int errorCode, long long value)
    : MegaError(errorCode)
    , mValue(value)
{
}

MegaErrorPrivate::MegaErrorPrivate(const Error& err)
    : MegaError(err)
    , mValue(0)
    , mUserStatus(err.getUserStatus())
    , mLinkStatus(err.getLinkStatus())
{
}

MegaErrorPrivate::MegaErrorPrivate(const MegaError &megaError)
    : MegaError(megaError.getErrorCode())
    , mValue(megaError.getValue())
    , mUserStatus(megaError.getUserStatus())
    , mLinkStatus(megaError.getLinkStatus())
{
}

MegaErrorPrivate::~MegaErrorPrivate()
{

}

MegaError* MegaErrorPrivate::copy() const
{
    return new MegaErrorPrivate(*this);
}

int MegaErrorPrivate::getErrorCode() const
{
    return errorCode;
}

long long MegaErrorPrivate::getValue() const
{
    return mValue;
}

bool MegaErrorPrivate::hasExtraInfo() const
{
    return mUserStatus != MegaError::UserErrorCode::USER_ETD_UNKNOWN
            || mLinkStatus != MegaError::LinkErrorCode::LINK_UNKNOWN;
}

long long MegaErrorPrivate::getUserStatus() const
{
    return mUserStatus;
}

long long MegaErrorPrivate::getLinkStatus() const
{
    return mLinkStatus;
}

const char* MegaErrorPrivate::getErrorString() const
{
    return MegaError::getErrorString(errorCode);
}

const char* MegaErrorPrivate::toString() const
{
    return getErrorString();
}

const char* MegaErrorPrivate::__str__() const
{
    return getErrorString();
}

const char *MegaErrorPrivate::__toString() const
{
    return getErrorString();
}

ExternalLogger::ExternalLogger()
{
    logToConsole = false;
    SimpleLogger::setOutputClass(this);
}

ExternalLogger::~ExternalLogger()
{
#ifndef ENABLE_LOG_PERFORMANCE
    mutex.lock();
#endif
    SimpleLogger::setOutputClass(NULL);
#ifndef ENABLE_LOG_PERFORMANCE
    mutex.unlock();
#endif
}

void ExternalLogger::addMegaLogger(MegaLogger *logger)
{
#ifndef ENABLE_LOG_PERFORMANCE
    mutex.lock();
#endif
    if (logger && megaLoggers.find(logger) == megaLoggers.end())
    {
        megaLoggers.insert(logger);
    }
#ifndef ENABLE_LOG_PERFORMANCE
    mutex.unlock();
#endif
}

void ExternalLogger::removeMegaLogger(MegaLogger *logger)
{
#ifndef ENABLE_LOG_PERFORMANCE
    mutex.lock();
#endif
    if (logger)
    {
        megaLoggers.erase(logger);
    }
#ifndef ENABLE_LOG_PERFORMANCE
    mutex.unlock();
#endif
}

void ExternalLogger::setLogLevel(int logLevel)
{
    SimpleLogger::setLogLevel((LogLevel)logLevel);
}

void ExternalLogger::setLogToConsole(bool enable)
{
    this->logToConsole = enable;
}

void ExternalLogger::postLog(int logLevel, const char *message, const char *filename, int line)
{
    if (SimpleLogger::logCurrentLevel < logLevel)
    {
        return;
    }

    if (!message)
    {
        message = "";
    }

    if (!filename)
    {
        filename = "";
    }

#ifndef ENABLE_LOG_PERFORMANCE
    mutex.lock();
#endif
    //For direct logging, we could use DirectMessage(message) here
    SimpleLogger{static_cast<LogLevel>(logLevel), filename, line} << message;
#ifndef ENABLE_LOG_PERFORMANCE
    mutex.unlock();
#endif
}

void ExternalLogger::log(const char *time, int loglevel, const char *source, const char *message
#ifdef ENABLE_LOG_PERFORMANCE
          , const char **directMessages = nullptr, size_t *directMessagesSizes = nullptr, unsigned numberMessages = 0
#endif
                         )
{
    if (!time)
    {
        time = "";
    }

    if (!source)
    {
        source = "";
    }

    if (!message)
    {
        message = "";
    }

#ifndef ENABLE_LOG_PERFORMANCE
    mutex.lock();
#endif
    for (auto logger : megaLoggers)
    {
        logger->log(time, loglevel, source, message
#ifdef ENABLE_LOG_PERFORMANCE
                    , directMessages, directMessagesSizes, numberMessages
#endif
                    );
    }

    if (logToConsole)
    {
#ifdef ENABLE_LOG_PERFORMANCE
        mutex.lock();
#endif
        std::cout << "[" << time << "][" << SimpleLogger::toStr((LogLevel)loglevel) << "] " << message << std::endl;
#ifdef ENABLE_LOG_PERFORMANCE
        mutex.unlock();
#endif
    }
#ifndef ENABLE_LOG_PERFORMANCE
    mutex.unlock();
#endif
}


OutShareProcessor::OutShareProcessor(MegaClient& mc)
    : mClient(mc)
{
}

bool OutShareProcessor::processNode(Node *node)
{
    if (node->outshares)
    {
        for (share_map::iterator it = node->outshares->begin(); it != node->outshares->end(); it++)
        {
            Share *share = it->second;
            if (share->user) // public links have no user
            {
                mShares.push_back(share);
                mNodes.push_back(node);
            }
        }
    }

    if (node->pendingshares)
    {
        for (share_map::iterator it = node->pendingshares->begin(); it != node->pendingshares->end(); it++)
        {
            Share *share = it->second;
            if (share->user || share->pcr) // public links have no user
            {
                mShares.push_back(share);
                mNodes.push_back(node);
            }
        }
    }

    return true;
}

vector<Share *> OutShareProcessor::getShares()
{
    return mShares;
}

vector<handle> OutShareProcessor::getHandles()
{
    vector<handle> handles;
    for (Node *node: mNodes)
    {
        handles.push_back(node->nodehandle);
    }

    return handles;
}

void OutShareProcessor::sortShares(int order)
{
    MegaApiImpl::sortByComparatorFunction(mNodes, order, mClient);
}

PendingOutShareProcessor::PendingOutShareProcessor()
{

}

bool PendingOutShareProcessor::processNode(Node *node)
{
    if(!node->pendingshares)
    {
        return true;
    }

    for (share_map::iterator it = node->pendingshares->begin(); it != node->pendingshares->end(); it++)
    {
        shares.push_back(it->second);
        handles.push_back(node->nodehandle);
    }

    return true;
}

vector<Share *> &PendingOutShareProcessor::getShares()
{
    return shares;
}

vector<handle> &PendingOutShareProcessor::getHandles()
{
    return handles;
}

MegaPricingPrivate::~MegaPricingPrivate()
{
    for(unsigned i = 0; i < currency.size(); i++)
    {
        delete[] currency[i];
    }

    for(unsigned i = 0; i < description.size(); i++)
    {
        delete[] description[i];
    }

    for(unsigned i = 0; i < iosId.size(); i++)
    {
        delete[] iosId[i];
    }

    for(unsigned i = 0; i < androidId.size(); i++)
    {
        delete[] androidId[i];
    }
}

int MegaPricingPrivate::getNumProducts()
{
    return int(handles.size());
}

handle MegaPricingPrivate::getHandle(int productIndex)
{
    if((unsigned)productIndex < handles.size())
        return handles[productIndex];

    return UNDEF;
}

int MegaPricingPrivate::getProLevel(int productIndex)
{
    if((unsigned)productIndex < proLevel.size())
        return proLevel[productIndex];

    return 0;
}

int MegaPricingPrivate::getGBStorage(int productIndex)
{
    if (static_cast<size_t>(productIndex) < gbStorage.size())
    {
        return gbStorage[static_cast<size_t>(productIndex)];
    }

    return 0;
}

int MegaPricingPrivate::getGBTransfer(int productIndex)
{
    if (static_cast<size_t>(productIndex) < gbTransfer.size())
    {
        return gbTransfer[static_cast<size_t>(productIndex)];
    }

    return 0;
}

int MegaPricingPrivate::getMonths(int productIndex)
{
    if((unsigned)productIndex < months.size())
        return months[productIndex];

    return 0;
}

int MegaPricingPrivate::getAmount(int productIndex)
{
    if((unsigned)productIndex < amount.size())
        return amount[productIndex];

    return 0;
}

const char *MegaPricingPrivate::getCurrency(int productIndex)
{
    if((unsigned)productIndex < currency.size())
        return currency[productIndex];

    return NULL;
}

const char *MegaPricingPrivate::getDescription(int productIndex)
{
    if((unsigned)productIndex < description.size())
        return description[productIndex];

    return NULL;
}

const char *MegaPricingPrivate::getIosID(int productIndex)
{
    if((unsigned)productIndex < iosId.size())
        return iosId[productIndex];

    return NULL;
}

const char *MegaPricingPrivate::getAndroidID(int productIndex)
{
    if((unsigned)productIndex < androidId.size())
        return androidId[productIndex];

    return NULL;
}

bool MegaPricingPrivate::isBusinessType(int productIndex)
{
    if((unsigned)productIndex < type.size())
        return type[productIndex];

    return false;
}

int MegaPricingPrivate::getAmountMonth(int productIndex)
{
    if((unsigned)productIndex < amountMonth.size())
        return amountMonth[productIndex];

    return 0;
}

MegaPricing *MegaPricingPrivate::copy()
{
    MegaPricingPrivate *megaPricing = new MegaPricingPrivate();
    for(unsigned i=0; i<handles.size(); i++)
    {
        megaPricing->addProduct(type[i], handles[i], proLevel[i], gbStorage[i], gbTransfer[i],
                                months[i], amount[i], amountMonth[i], currency[i], description[i], iosId[i], androidId[i]);
    }

    return megaPricing;
}

void MegaPricingPrivate::addProduct(unsigned int type, handle product, int proLevel, int gbStorage, int gbTransfer, int months, int amount, int amountMonth,
                                    const char *currency, const char* description, const char* iosid, const char* androidid)
{
    this->type.push_back(type);
    this->handles.push_back(product);
    this->proLevel.push_back(proLevel);
    this->gbStorage.push_back(gbStorage);
    this->gbTransfer.push_back(gbTransfer);
    this->months.push_back(months);
    this->amount.push_back(amount);
    this->amountMonth.push_back(amountMonth);
    this->currency.push_back(MegaApi::strdup(currency));
    this->description.push_back(MegaApi::strdup(description));
    this->iosId.push_back(MegaApi::strdup(iosid));
    this->androidId.push_back(MegaApi::strdup(androidid));
}

#ifdef ENABLE_SYNC
MegaSyncPrivate::MegaSyncPrivate(const char *path, handle nodehandle, int tag)
{
    this->tag = tag;
    this->megaHandle = nodehandle;
    this->localFolder = NULL;
    setLocalFolder(path);
    this->state = SYNC_INITIALSCAN;
    this->fingerprint = 0;
    this->regExp = NULL;
    this->listener = NULL;
}

MegaSyncPrivate::MegaSyncPrivate(MegaSyncPrivate *sync)
{
    this->regExp = NULL;
    this->localFolder = NULL;
    this->setTag(sync->getTag());
    this->setLocalFolder(sync->getLocalFolder());
    this->setMegaHandle(sync->getMegaHandle());
    this->setLocalFingerprint(sync->getLocalFingerprint());
    this->setState(sync->getState());
    this->setListener(sync->getListener());
    this->setRegExp(sync->getRegExp());
}

MegaSyncPrivate::~MegaSyncPrivate()
{
    delete [] localFolder;
    delete regExp;
}

MegaSync *MegaSyncPrivate::copy()
{
    return new MegaSyncPrivate(this);
}

MegaHandle MegaSyncPrivate::getMegaHandle() const
{
    return megaHandle;
}

void MegaSyncPrivate::setMegaHandle(MegaHandle handle)
{
    this->megaHandle = handle;
}

const char *MegaSyncPrivate::getLocalFolder() const
{
    return localFolder;
}

void MegaSyncPrivate::setLocalFolder(const char *path)
{
    if (localFolder)
    {
        delete [] localFolder;
    }
    localFolder =  MegaApi::strdup(path);
}

long long MegaSyncPrivate::getLocalFingerprint() const
{
    return fingerprint;
}

void MegaSyncPrivate::setLocalFingerprint(long long fingerprint)
{
    this->fingerprint = fingerprint;
}

int MegaSyncPrivate::getTag() const
{
    return tag;
}

void MegaSyncPrivate::setTag(int tag)
{
    this->tag = tag;
}

void MegaSyncPrivate::setListener(MegaSyncListener *listener)
{
    this->listener = listener;
}

MegaSyncListener *MegaSyncPrivate::getListener()
{
    return this->listener;
}

int MegaSyncPrivate::getState() const
{
    return state;
}

void MegaSyncPrivate::setState(int state)
{
    this->state = state;
}

MegaRegExpPrivate::MegaRegExpPrivate()
{
    patternUpdated = false;

#ifdef USE_PCRE
    options = PCRE_ANCHORED | PCRE_UTF8;
    reCompiled = NULL;
    reOptimization = NULL;
#endif
}

MegaRegExpPrivate::~MegaRegExpPrivate()
{
#ifdef USE_PCRE
    if (reCompiled != NULL)
    {
        pcre_free(reCompiled);
    }
    if (reOptimization != NULL)
    {
        pcre_free(reOptimization);
    }
#endif
}

MegaRegExpPrivate * MegaRegExpPrivate::copy()
{
    MegaRegExpPrivate *regExp = new MegaRegExpPrivate();

    for (unsigned int i = 0; i < this->regExps.size(); i++)
    {
        regExp->addRegExp(this->getRegExp(i));
    }

    if (this->isPatternUpdated())
    {
        regExp->updatePattern();
    }

    return regExp;
}

const char *MegaRegExpPrivate::getFullPattern()
{
    if (!patternUpdated)
    {
        updatePattern();
    }

    return pattern.c_str();
}


bool MegaRegExpPrivate::addRegExp(const char *regExp)
{
    if (!checkRegExp(regExp))
    {
        return false;
    }

    regExps.push_back(regExp);
    patternUpdated = false;

    return true;
}

int MegaRegExpPrivate::getNumRegExp()
{
    return int(regExps.size());
}

const char * MegaRegExpPrivate::getRegExp(int index)
{
    return regExps.at(index).c_str();
}

/**
 * @brief Checks if the given regular expression is correct.
 * @param regExp Regular expression
 * @return True if the regular expression is correct. Otherwise, false.
 */
bool MegaRegExpPrivate::checkRegExp(const char *regExp)
{
    if (!regExp)
    {
        return false;
    }

#ifdef USE_PCRE
    const char *error;
    int eoffset;

    if (!pcre_compile(regExp, options, &error, &eoffset, NULL))
    {
        LOG_info << "Wrong expression " << regExp << ": " << error;
        return false;
    }
#endif

    return true;
}

/**
 * @brief This method clears the previous pattern and creates a new one based on the
 * current regular expressions included in @regExps
 * @return True if compilation of new pattern was successfull. Otherwise, false.
 */
bool MegaRegExpPrivate::updatePattern()
{
    pattern.clear();
    for (unsigned int i = 0; i < regExps.size(); i++)
    {
        string wrapped = "(?:";
        wrapped += regExps.at(i);
        wrapped += ")\\z";

        pattern += wrapped + ((i==(regExps.size()-1)) ? "" :"|");
    }

    patternUpdated = true;
    int result = compile();
    return result == REGEXP_NO_ERROR || result == REGEXP_OPTIMIZATION_ERROR;
}

bool MegaRegExpPrivate::isPatternUpdated()
{
    return patternUpdated;
}

bool MegaRegExpPrivate::match(const char *itemToMatch)
{
    if (!patternUpdated)
    {
        updatePattern();
    }

#ifdef USE_PCRE
    int strVector[30];
    int result;

    result = pcre_exec(reCompiled,
                       reOptimization,
                       itemToMatch,
                       strlen(itemToMatch),
                       0,
                       PCRE_ANCHORED,
                       strVector,
                       30);

    if (result >= 0) // We have a match
    {
        return true;
    }
    else            // Something bad happened..
    {
        switch(result)
        {
            case PCRE_ERROR_NOMATCH      : /*LOG_debug << "PCRE: String did not match the pattern";  */  break;
            case PCRE_ERROR_NULL         : LOG_debug << "PCRE: Something was null";                      break;
            case PCRE_ERROR_BADOPTION    : LOG_debug << "PCRE: A bad option was passed";                 break;
            case PCRE_ERROR_BADMAGIC     : LOG_debug << "PCRE: Magic number bad (compiled re corrupt?)"; break;
            case PCRE_ERROR_UNKNOWN_NODE : LOG_debug << "PCRE: Something kooky in the compiled re";      break;
            case PCRE_ERROR_NOMEMORY     : LOG_debug << "PCRE: Ran out of memory";                       break;
            default                      : LOG_debug << "PCRE: Unknown error";                           break;
        }

        return false;
    }
#endif

    return 0;
}

int MegaRegExpPrivate::compile()
{
#ifdef USE_PCRE
    const char *error;
    int eoffset;

    if (pattern.empty())
    {
        return MegaRegExpPrivate::REGEXP_EMPTY;
    }

    reCompiled = pcre_compile(pattern.c_str(), options, &error, &eoffset, NULL);
    if (reCompiled == NULL)
    {
        LOG_debug << "PCRE error: Could not compile " << pattern.c_str() << ": " << error;
        return MegaRegExpPrivate::REGEXP_COMPILATION_ERROR;
    }

    reOptimization = pcre_study(reCompiled, 0, &error);
    if (error != NULL)
    {
        LOG_debug << "PCRE info: Could not study " << pattern.c_str() << ": " << error;
        return MegaRegExpPrivate::REGEXP_OPTIMIZATION_ERROR;
    }

#endif
    return MegaRegExpPrivate::REGEXP_NO_ERROR;
}

MegaRegExp *MegaSyncPrivate::getRegExp() const
{
    return regExp;
}

void MegaSyncPrivate::setRegExp(MegaRegExp *regExp)
{
    if (this->regExp)
    {
        delete this->regExp;
    }

    if (!regExp)
    {
        this->regExp = NULL;
    }
    else
    {
        this->regExp = regExp->copy();
    }
}

MegaSyncEventPrivate::MegaSyncEventPrivate(int type)
    : type(type)
{
}

MegaSyncEvent *MegaSyncEventPrivate::copy()
{
    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(type);
    event->setPath(this->path.get());
    event->setNodeHandle(this->nodeHandle);
    event->setNewPath(this->newPath.get());
    event->setPrevName(this->prevName.get());
    event->setPrevParent(this->prevParent);
    return event;
}

int MegaSyncEventPrivate::getType() const
{
    return type;
}

const char *MegaSyncEventPrivate::getPath() const
{
    return path.get();
}

MegaHandle MegaSyncEventPrivate::getNodeHandle() const
{
    return nodeHandle;
}

const char *MegaSyncEventPrivate::getNewPath() const
{
    return newPath.get();
}

const char *MegaSyncEventPrivate::getPrevName() const
{
    return prevName.get();
}

MegaHandle MegaSyncEventPrivate::getPrevParent() const
{
    return prevParent;
}

void MegaSyncEventPrivate::setPath(const char *path)
{
    this->path.reset(MegaApi::strdup(path));
}

void MegaSyncEventPrivate::setNodeHandle(MegaHandle nodeHandle)
{
    this->nodeHandle = nodeHandle;
}

void MegaSyncEventPrivate::setNewPath(const char *newPath)
{
    this->newPath.reset(MegaApi::strdup(newPath));
}

void MegaSyncEventPrivate::setPrevName(const char *prevName)
{
    this->prevName.reset(MegaApi::strdup(prevName));
}

void MegaSyncEventPrivate::setPrevParent(MegaHandle prevParent)
{
    this->prevParent = prevParent;
}

#endif


MegaAccountBalance *MegaAccountBalancePrivate::fromAccountBalance(const AccountBalance *balance)
{
    return new MegaAccountBalancePrivate(balance);
}

MegaAccountBalancePrivate::~MegaAccountBalancePrivate()
{

}

MegaAccountBalance *MegaAccountBalancePrivate::copy()
{
    return new MegaAccountBalancePrivate(&balance);
}

double MegaAccountBalancePrivate::getAmount() const
{
    return balance.amount;
}

char *MegaAccountBalancePrivate::getCurrency() const
{
    return MegaApi::strdup(balance.currency);
}

MegaAccountBalancePrivate::MegaAccountBalancePrivate(const AccountBalance *balance)
{
    this->balance = *balance;
}

MegaAccountSession *MegaAccountSessionPrivate::fromAccountSession(const AccountSession *session)
{
    return new MegaAccountSessionPrivate(session);
}

MegaAccountSessionPrivate::~MegaAccountSessionPrivate()
{

}

MegaAccountSession *MegaAccountSessionPrivate::copy()
{
    return new MegaAccountSessionPrivate(&session);
}

int64_t MegaAccountSessionPrivate::getCreationTimestamp() const
{
    return session.timestamp;
}

int64_t MegaAccountSessionPrivate::getMostRecentUsage() const
{
    return session.mru;
}

char *MegaAccountSessionPrivate::getUserAgent() const
{
    return MegaApi::strdup(session.useragent.c_str());
}

char *MegaAccountSessionPrivate::getIP() const
{
    return MegaApi::strdup(session.ip.c_str());
}

char *MegaAccountSessionPrivate::getCountry() const
{
    return MegaApi::strdup(session.country);
}

bool MegaAccountSessionPrivate::isCurrent() const
{
    return session.current;
}

bool MegaAccountSessionPrivate::isAlive() const
{
    return session.alive;
}

MegaHandle MegaAccountSessionPrivate::getHandle() const
{
    return session.id;
}

MegaAccountSessionPrivate::MegaAccountSessionPrivate(const AccountSession *session)
{
    this->session = *session;
}


MegaAccountPurchase *MegaAccountPurchasePrivate::fromAccountPurchase(const AccountPurchase *purchase)
{
    return new MegaAccountPurchasePrivate(purchase);
}

MegaAccountPurchasePrivate::~MegaAccountPurchasePrivate()
{

}

MegaAccountPurchase *MegaAccountPurchasePrivate::copy()
{
    return new MegaAccountPurchasePrivate(&purchase);
}

int64_t MegaAccountPurchasePrivate::getTimestamp() const
{
    return purchase.timestamp;
}

char *MegaAccountPurchasePrivate::getHandle() const
{
    return MegaApi::strdup(purchase.handle);
}

char *MegaAccountPurchasePrivate::getCurrency() const
{
    return MegaApi::strdup(purchase.currency);
}

double MegaAccountPurchasePrivate::getAmount() const
{
    return purchase.amount;
}

int MegaAccountPurchasePrivate::getMethod() const
{
    return purchase.method;
}

MegaAccountPurchasePrivate::MegaAccountPurchasePrivate(const AccountPurchase *purchase)
{
    this->purchase = *purchase;
}


MegaAccountTransaction *MegaAccountTransactionPrivate::fromAccountTransaction(const AccountTransaction *transaction)
{
    return new MegaAccountTransactionPrivate(transaction);
}

MegaAccountTransactionPrivate::~MegaAccountTransactionPrivate()
{

}

MegaAccountTransaction *MegaAccountTransactionPrivate::copy()
{
    return new MegaAccountTransactionPrivate(&transaction);
}

int64_t MegaAccountTransactionPrivate::getTimestamp() const
{
    return transaction.timestamp;
}

char *MegaAccountTransactionPrivate::getHandle() const
{
    return MegaApi::strdup(transaction.handle);
}

char *MegaAccountTransactionPrivate::getCurrency() const
{
    return MegaApi::strdup(transaction.currency);
}

double MegaAccountTransactionPrivate::getAmount() const
{
    return transaction.delta;
}

MegaAccountTransactionPrivate::MegaAccountTransactionPrivate(const AccountTransaction *transaction)
{
    this->transaction = *transaction;
}



ExternalInputStream::ExternalInputStream(MegaInputStream *inputStream)
{
    this->inputStream = inputStream;
}

m_off_t ExternalInputStream::size()
{
    return inputStream->getSize();
}

bool ExternalInputStream::read(byte *buffer, unsigned size)
{
    return inputStream->read((char *)buffer, size);
}


MegaTreeProcCopy::MegaTreeProcCopy(MegaClient *client)
{
    nn = NULL;
    nc = 0;
    this->client = client;
}

void MegaTreeProcCopy::allocnodes()
{
    if (nc)
    {
        nn = new NewNode[nc];
    }
}
bool MegaTreeProcCopy::processMegaNode(MegaNode *n)
{
    if (nn)
    {
        NewNode* t = nn+--nc;

        // copy key (if file) or generate new key (if folder)
        if (n->getType() == FILENODE)
        {
            t->nodekey = *(n->getNodeKey());
        }
        else
        {
            byte buf[FOLDERNODEKEYLENGTH];
            client->rng.genblock(buf,sizeof buf);
            t->nodekey.assign((char*)buf, FOLDERNODEKEYLENGTH);
        }

        t->attrstring.reset(new string);
        if (n->isPublic())
        {
            t->source = NEW_PUBLIC;
        }
        else
        {
            t->source = NEW_NODE;
        }

        SymmCipher key;
        AttrMap attrs;

        key.setkey((const byte*)t->nodekey.data(),n->getType());
        string sname = n->getName();
        client->fsaccess->normalize(&sname);
        attrs.map['n'] = sname;

        const char *fingerprint = n->getFingerprint();
        if (fingerprint && fingerprint[0])
        {
            m_off_t size = 0;
            unsigned int fsize = unsigned(strlen(fingerprint));
            unsigned int ssize = fingerprint[0] - 'A';
            if (!(ssize > (sizeof(size) * 4 / 3 + 4) || fsize <= (ssize + 1)))
            {
                int len =  sizeof(size) + 1;
                byte *buf = new byte[len];
                Base64::atob(fingerprint + 1, buf, len);
                int l = Serialize64::unserialize(buf, len, (uint64_t *)&size);
                delete [] buf;
                if (l > 0)
                {
                    attrs.map['c'] = fingerprint + ssize + 1;
                }
            }
        }

        string attrstring;
        attrs.getjson(&attrstring);
        client->makeattr(&key, t->attrstring, attrstring.c_str());

        t->nodehandle = n->getHandle();
        t->type = (nodetype_t)n->getType();
        t->parenthandle = n->getParentHandle() ? n->getParentHandle() : UNDEF;
    }
    else
    {
        nc++;
    }

    return true;
}

MegaFolderUploadController::MegaFolderUploadController(MegaApiImpl *megaApi, MegaTransferPrivate *transfer)
{
    this->megaApi = megaApi;
    this->client = megaApi->getMegaClient();
    this->transfer = transfer;
    this->listener = transfer->getListener();
    this->recursive = 0;
    this->pendingTransfers = 0;
    this->tag = transfer->getTag();
}

void MegaFolderUploadController::start(MegaNode*)
{
    transfer->setFolderTransferTag(-1);
    transfer->setStartTime(Waiter::ds);
    transfer->setState(MegaTransfer::STATE_QUEUED);
    megaApi->fireOnTransferStart(transfer);

    const char *name = transfer->getFileName();
    MegaNode *parent = megaApi->getNodeByHandle(transfer->getParentHandle());
    if(!parent)
    {
        transfer->setState(MegaTransfer::STATE_FAILED);
        DBTableTransactionCommitter committer(client->tctable);
        megaApi->fireOnTransferFinish(transfer, make_unique<MegaErrorPrivate>(API_EARGS), committer);
    }
    else
    {
        auto localpath = LocalPath::fromPath(transfer->getPath(), *client->fsaccess);
        MegaNode *child = megaApi->getChildNode(parent, name);

        if(!child || !child->isFolder())
        {
            pendingFolders.push_back(localpath);
            megaApi->createFolder(name, parent, this);
        }
        else
        {
            pendingFolders.push_front(localpath);
            onFolderAvailable(child->getHandle());
        }

        delete child;
        delete parent;
    }
}

void MegaFolderUploadController::cancel()
{
    cancelled = true; //we dont want to further checkcompletion, and produce multile fireOnTransferFinish -> multiple deletions

    //remove subtransfers from pending transferQueue
    megaApi->cancelPendingTransfersByFolderTag(tag);

    //remove ongoing subtransfers
    long long cancelledSubTransfers = 0;
    std::unique_ptr<DBTableTransactionCommitter> insideCommiter;
    DBTableTransactionCommitter *committer = client->tctable ? client->tctable->getTransactionCommitter() : nullptr;
    if (!committer)
    {
        insideCommiter.reset(new DBTableTransactionCommitter(client->tctable));
        committer = insideCommiter.get();
    }

    while (!subTransfers.empty())
    {
        auto subTransfer = *subTransfers.begin();

        Transfer *transfer = subTransfer->getTransfer();
        if (!transfer)
        {
            LOG_warn << "Subtransfer without attached Transfer for folder transfer: " << subTransfer->getFileName();

            subTransfer->setState(MegaTransfer::STATE_CANCELLED);
            megaApi->fireOnTransferFinish(subTransfer, make_unique<MegaErrorPrivate>(API_EINCOMPLETE), *committer);

            continue;
        }

#ifdef _WIN32
        if (transfer->type == GET)
        {
            transfer->localfilename.editStringDirect()->append("", 1);
            WIN32_FILE_ATTRIBUTE_DATA fad;
            if (GetFileAttributesExW((LPCWSTR)transfer->localfilename.editStringDirect()->data(), GetFileExInfoStandard, &fad))
                SetFileAttributesW((LPCWSTR)transfer->localfilename.editStringDirect()->data(), fad.dwFileAttributes & ~FILE_ATTRIBUTE_HIDDEN);
            transfer->localfilename.editStringDirect()->resize(transfer->localfilename.editStringDirect()->size()-1);
        }
#endif

        MegaErrorPrivate megaError(API_EINCOMPLETE);
        subTransfer->setLastError(&megaError);

        bool found = false;
        file_list files = transfer->files;
        file_list::iterator iterator = files.begin();
        while (iterator != files.end())
        {
            File *file = *iterator;
            iterator++;
            if (file->tag == subTransfer->getTag())
            {
                found = true;
                if (!file->syncxfer)
                {
                    client->stopxfer(file, committer);
                }
                else
                {
                    LOG_err << "Sync subtransfer found for folder transfer: " << subTransfer->getFileName();
                    assert(false);
                }
                break;
            }
        }

        if (!found)
        {
            LOG_warn << "No file found for subtransfer: " << subTransfer->getFileName();

            subTransfer->setState(MegaTransfer::STATE_CANCELLED);
            megaApi->fireOnTransferFinish(subTransfer, make_unique<MegaErrorPrivate>(API_EINCOMPLETE), *committer);
        }
        cancelledSubTransfers++;
    }

    LOG_verbose << " MegaFolderUploadController, cancelled subTransfers = " << cancelledSubTransfers;

    transfer = nullptr;  // no final callback for this one since it is being destroyed now
}

void MegaFolderUploadController::onFolderAvailable(MegaHandle handle)
{
    recursive++;
    auto localPath = pendingFolders.front();
    pendingFolders.pop_front();

    MegaNode *parent = megaApi->getNodeByHandle(handle);

    LocalPath localname;
    DirAccess* da;
    da = client->fsaccess->newdiraccess();
    if (da->dopen(&localPath, NULL, false))
    {
        FileSystemType fsType = client->fsaccess->getFilesystemType(localPath);

        nodetype_t dirEntryType;
        while (da->dnext(localPath, localname, client->followsymlinks, &dirEntryType))
        {
            ScopedLengthRestore restoreLen(localPath);
            localPath.appendWithSeparator(localname, false, client->fsaccess->localseparator);

            string name = localname.toName(*client->fsaccess);
            if (dirEntryType == FILENODE)
            {
                pendingTransfers++;
                megaApi->startUpload(false, localPath.toPath(*client->fsaccess).c_str(), parent, (const char *)NULL, -1, tag, false, NULL, false, false, fsType, this);
            }
            else if (dirEntryType == FOLDERNODE)
            {
                MegaNode *child = megaApi->getChildNode(parent, name.c_str());
                if(!child || !child->isFolder())
                {
                    pendingFolders.push_back(localPath);
                    megaApi->createFolder(name.c_str(), parent, this);
                }
                else
                {
                    pendingFolders.push_front(localPath);
                    onFolderAvailable(child->getHandle());
                }
                delete child;
            }
        }
    }

    delete da;
    delete parent;
    recursive--;

    checkCompletion();
}

void MegaFolderUploadController::checkCompletion()
{
    if (!cancelled && !recursive && !pendingFolders.size() && !pendingTransfers)
    {
        LOG_debug << "Folder transfer finished - " << transfer->getTransferredBytes() << " of " << transfer->getTotalBytes();
        transfer->setState(MegaTransfer::STATE_COMPLETED);
        transfer->setLastError(&mLastError);
        DBTableTransactionCommitter committer(client->tctable);
        megaApi->fireOnTransferFinish(transfer, make_unique<MegaErrorPrivate>(!mIncompleteTransfers ? API_OK : API_EINCOMPLETE), committer);
    }
}

void MegaFolderUploadController::onRequestFinish(MegaApi *, MegaRequest *request, MegaError *e)
{
    int type = request->getType();
    int errorCode = e->getErrorCode();

    if (type == MegaRequest::TYPE_CREATE_FOLDER)
    {
        if (!errorCode)
        {
            onFolderAvailable(request->getNodeHandle());
        }
        else
        {
            pendingFolders.pop_front();
            mLastError = *e;
            mIncompleteTransfers++;
            checkCompletion();
        }
    }
}

void MegaFolderUploadController::onTransferStart(MegaApi *, MegaTransfer *t)
{
    subTransfers.insert(static_cast<MegaTransferPrivate*>(t));
    assert(transfer);
    if (transfer)
    {
        transfer->setState(t->getState());
        transfer->setPriority(t->getPriority());
        transfer->setTotalBytes(transfer->getTotalBytes() + t->getTotalBytes());
        transfer->setUpdateTime(Waiter::ds);
        megaApi->fireOnTransferUpdate(transfer);
    }
}

void MegaFolderUploadController::onTransferUpdate(MegaApi *, MegaTransfer *t)
{
    assert(transfer);
    if (transfer)
    {
        transfer->setState(t->getState());
        transfer->setPriority(t->getPriority());
        transfer->setTransferredBytes(transfer->getTransferredBytes() + t->getDeltaSize());
        transfer->setUpdateTime(Waiter::ds);
        transfer->setSpeed(t->getSpeed());
        transfer->setMeanSpeed(t->getMeanSpeed());
        megaApi->fireOnTransferUpdate(transfer);
    }
}

void MegaFolderUploadController::onTransferFinish(MegaApi *, MegaTransfer *t, MegaError *e)
{
    subTransfers.erase(static_cast<MegaTransferPrivate*>(t));
    pendingTransfers--;
    assert(transfer);
    if (transfer)
    {
        transfer->setState(MegaTransfer::STATE_ACTIVE);
        transfer->setPriority(t->getPriority());
        transfer->setTransferredBytes(transfer->getTransferredBytes() + t->getDeltaSize());
        transfer->setUpdateTime(Waiter::ds);
        transfer->setSpeed(t->getSpeed());
        transfer->setMeanSpeed(t->getMeanSpeed());
        megaApi->fireOnTransferUpdate(transfer);
        if (e->getErrorCode() != API_OK)
        {
            mLastError = *e;
            mIncompleteTransfers++;
        }
        checkCompletion();
    }
}

MegaFolderUploadController::~MegaFolderUploadController()
{
    //we dettach this as request listener: could be pending create folder req finish
    megaApi->removeRequestListener(this);

    //we shouldn't need to dettach as transfer listener: all listened transfer should have been cancelled/completed
}

MegaBackupController::MegaBackupController(MegaApiImpl *megaApi, int tag, int folderTransferTag, handle parenthandle, const char* filename, bool attendPastBackups, const char *speriod, int64_t period, int maxBackups)
{
    LOG_info << "Registering backup for folder " << filename << " period=" << period << " speriod=" << speriod << " Number-of-Backups=" << maxBackups;

    this->basepath = filename;
    size_t found = basepath.find_last_of("/\\");
    string aux = basepath;
    while (aux.size() && (found == (aux.size()-1)))
    {
        aux = aux.substr(0, found - 1);
        found = aux.find_last_of("/\\");
    }
    this->backupName = aux.substr((found == string::npos)?0:found+1);

    this->parenthandle = parenthandle;

    this->megaApi = megaApi;
    this->client = megaApi->getMegaClient();

    this->attendPastBackups = attendPastBackups;

    this->pendingTags = 0;

    clearCurrentBackupData();

    lastbackuptime = getLastBackupTime();

    this->backupListener = NULL;

    this->maxBackups = maxBackups;

    this->pendingremovals = 0;

    this->lastwakeuptime = 0;

    this->tag = tag;
    this->folderTransferTag = folderTransferTag;

    valid = true;
    this->setPeriod(period);
    this->setPeriodstring(speriod);
    if (!backupName.size())
    {
        valid = false;
    }

    if (valid)
    {
        megaApi->startTimer(this->startTime - Waiter::ds + 1); //wake the sdk when required
        this->state = MegaBackup::BACKUP_ACTIVE;
        megaApi->fireOnBackupStateChanged(this);
        removeexceeding(false);
    }
    else
    {
        this->state = MegaBackup::BACKUP_FAILED;
    }
}

MegaBackupController::MegaBackupController(MegaBackupController *backup)
{
    this->pendingremovals = backup->pendingremovals;
    this->setTag(backup->getTag());
    this->setLocalFolder(backup->getLocalFolder());
    this->setMegaHandle(backup->getMegaHandle());

    this->setFolderTransferTag(backup->getFolderTransferTag());

    this->megaApi = backup->megaApi;
    this->client = backup->client;
    this->setBackupListener(backup->getBackupListener());


    //copy currentBackup data
    this->recursive = backup->recursive;
    this->pendingTransfers = backup->pendingTransfers;
    this->pendingTags = backup->pendingTags;
    for (auto it = backup->pendingFolders.begin(); it != backup->pendingFolders.end(); it++)
    {
        this->pendingFolders.push_back(*it);
    }

    for (std::vector<MegaTransfer *>::iterator it = backup->failedTransfers.begin(); it != backup->failedTransfers.end(); it++)
    {
        this->failedTransfers.push_back(((MegaTransfer *)*it)->copy());
    }
    this->currentHandle = backup->currentHandle;
    this->currentBKStartTime = backup->currentBKStartTime;
    this->updateTime = backup->updateTime;
    this->transferredBytes = backup->transferredBytes;
    this->totalBytes = backup->totalBytes;
    this->speed = backup->speed;
    this->meanSpeed = backup->meanSpeed;
    this->numberFiles = backup->numberFiles;
    this->totalFiles = backup->totalFiles;
    this->numberFolders = backup->numberFolders;
    this->attendPastBackups = backup->attendPastBackups;

    this->offsetds=backup->getOffsetds();
    this->lastbackuptime=backup->getLastBackupTime();
    this->state=backup->getState();
    this->startTime=backup->getStartTime();
    this->period=backup->getPeriod();
    this->ccronexpr=backup->getCcronexpr();
    this->periodstring=backup->getPeriodString();
    this->valid=backup->isValid();

    this->setLocalFolder(backup->getLocalFolder());
    this->setBackupName(backup->getBackupName());
    this->setMegaHandle(backup->getMegaHandle());
    this->setMaxBackups(backup->getMaxBackups());
}


long long MegaBackupController::getNextStartTime(long long oldStartTimeAbsolute) const
{
    if (oldStartTimeAbsolute == -1)
    {
        return (getNextStartTimeDs() + this->offsetds)/10;
    }
    else
    {
        return (getNextStartTimeDs(oldStartTimeAbsolute*10 - this->offsetds) + this->offsetds)/10;
    }
}

long long MegaBackupController::getNextStartTimeDs(long long oldStartTimeds) const
{
    if (oldStartTimeds == -1)
    {
        return startTime;
    }
    if (period != -1)
    {
        return oldStartTimeds + period;
    }
    else
    {
        if (!valid)
        {
            return oldStartTimeds;
        }
        long long current_ds = oldStartTimeds + this->offsetds;  // 64 bit

        long long newt = cron_next(&ccronexpr, time_t(current_ds/10));  // time_t is 32 bit still on many systems
        long long newStarTimeds = newt*10-offsetds;  // 64 bit again
        return newStarTimeds;
    }
}

void MegaBackupController::update()
{
    if (!valid)
    {
        if (!isBusy())
        {
            state = BACKUP_FAILED;
        }
        return;
    }
    if (Waiter::ds > startTime)
    {
        if (!isBusy())
        {
            long long nextStartTime = getNextStartTimeDs(startTime);
            if (nextStartTime <= startTime)
            {
                LOG_err << "Invalid calculated NextStartTime" ;
                valid = false;
                state = BACKUP_FAILED;
                return;
            }

            if (nextStartTime > Waiter::ds)
            {
                start();
            }
            else
            {
                LOG_warn << " BACKUP discarded (too soon, time for the next): " << basepath;
                start(true);
                megaApi->startTimer(1); //wake sdk
            }

            startTime = nextStartTime;
        }
        else
        {
            LOG_verbose << "Backup busy: " << basepath <<
                           ". State=" << ((state==MegaBackup::BACKUP_ONGOING)?"On Going":"Removing exeeding") << ". Postponing ...";
            if ((lastwakeuptime+10) < Waiter::ds )
            {
                megaApi->startTimer(10); //give it a while
                lastwakeuptime = Waiter::ds+10;
            }
        }
    }
    else
    {
        if (lastwakeuptime < Waiter::ds || ((this->startTime + 1) < lastwakeuptime))
        {
            LOG_debug << " Waking in " << (this->startTime - Waiter::ds + 1) << " deciseconds to do backup";
            megaApi->startTimer(this->startTime - Waiter::ds + 1); //wake the sdk when required
            lastwakeuptime = this->startTime + 1;
        }
    }
}

void MegaBackupController::removeexceeding(bool currentoneOK)
{
    map<int64_t, MegaNode *> backupTimesNodes;
    int ncompleted=0;

    MegaNode * parentNode = megaApi->getNodeByHandle(parenthandle);

    if (parentNode)
    {
        MegaNodeList* children = megaApi->getChildren(parentNode, MegaApi::ORDER_NONE);
        if (children)
        {
            for (int i = 0; i < children->size(); i++)
            {
                MegaNode *childNode = children->get(i);
                string childname = childNode->getName();

                if (isBackup(childname, backupName) )
                {
                    const char *backstvalue = childNode->getCustomAttr("BACKST");

                    if ( ( !backstvalue || !strcmp(backstvalue,"ONGOING") ) && ( childNode->getHandle() != currentHandle ) )
                    {
                        LOG_err << "Found unexpected ONGOING backup (probably from previous executions). Changing status to MISCARRIED";
                        this->pendingTags++;
                        megaApi->setCustomNodeAttribute(childNode, "BACKST", "MISCARRIED", this);
                    }

                    if ( (backstvalue && !strcmp(backstvalue,"COMPLETE"))
                            || ( childNode->getHandle() == currentHandle && currentoneOK ) //either its completed or is the current one and it went ok (it might not have backstvalue yet set
                            )
                    {
                        ncompleted++;
                    }

                    int64_t timeofbackup = getTimeOfBackup(childname);
                    if (timeofbackup)
                    {
                        backupTimesNodes[timeofbackup]=childNode;
                    }
                    else
                    {
                        LOG_err << "Failed to get backup time for folder: " << childname << ". Discarded.";
                    }
                }
            }
        }
        while (backupTimesNodes.size() > (unsigned int)maxBackups)
        {
            map<int64_t, MegaNode *>::iterator itr = backupTimesNodes.begin();
            const char *backstvalue = itr->second->getCustomAttr("BACKST");
            if ( (ncompleted == 1) && backstvalue && (!strcmp(backstvalue,"COMPLETE")) && backupTimesNodes.size() > 1)
            {
                itr++;
            }

            MegaNode * nodeToDelete = itr->second;
            int64_t timetodelete = itr->first;
            backstvalue = nodeToDelete->getCustomAttr("BACKST");
            if (backstvalue && !strcmp(backstvalue,"COMPLETE"))
            {
                ncompleted--;
            }

            char * nodepath = megaApi->getNodePath(nodeToDelete);
            LOG_info << " Removing exceeding backup " << nodepath;
            delete []nodepath;
            state = BACKUP_REMOVING_EXCEEDING;
            megaApi->fireOnBackupStateChanged(this);
            pendingremovals++;
            megaApi->remove(nodeToDelete, false, this);

            backupTimesNodes.erase(timetodelete);
        }

        delete children;
    }
    delete parentNode;
}

int64_t MegaBackupController::getLastBackupTime()
{
    map<int64_t, MegaNode *> backupTimesPaths;
    int64_t latesttime=0;

    MegaNode * parentNode = megaApi->getNodeByHandle(parenthandle);
    if (parentNode)
    {
        MegaNodeList* children = megaApi->getChildren(parentNode, MegaApi::ORDER_NONE);
        if (children)
        {
            for (int i = 0; i < children->size(); i++)
            {
                MegaNode *childNode = children->get(i);
                string childname = childNode->getName();
                if (isBackup(childname, backupName) )
                {
                    int64_t timeofbackup = getTimeOfBackup(childname);
                    if (timeofbackup)
                    {
                        backupTimesPaths[timeofbackup]=childNode;
                        latesttime = (std::max)(latesttime, timeofbackup);
                    }
                    else
                    {
                        LOG_err << "Failed to get backup time for folder: " << childname << ". Discarded.";
                    }
                }
            }
            delete children;
        }
        delete parentNode;
    }
    return latesttime;
}

bool MegaBackupController::isBackup(string localname, string backupname) const
{
    return ( localname.compare(0, backupname.length(), backupname) == 0) && (localname.find("_bk_") != string::npos);
}

int64_t MegaBackupController::getTimeOfBackup(string localname) const
{
    size_t pos = localname.find("_bk_");
    if (pos == string::npos || ( (pos+4) >= (localname.size()-1) ) )
    {
        return 0;
    }
    string rest = localname.substr(pos + 4).c_str();

//    int64_t toret = atol(rest.c_str());
    int64_t toret = stringTimeTods(rest);
    return toret;
}

bool MegaBackupController::getAttendPastBackups() const
{
    return attendPastBackups;
}

MegaTransferList *MegaBackupController::getFailedTransfers()
{
    MegaTransferList *result = new MegaTransferListPrivate(failedTransfers.data(), int(failedTransfers.size()));
    return result;
}

void MegaBackupController::setAttendPastBackups(bool value)
{
    attendPastBackups = value;
}

bool MegaBackupController::isValid() const
{
    return valid;
}

void MegaBackupController::setValid(bool value)
{
    valid = value;
}

cron_expr MegaBackupController::getCcronexpr() const
{
    return ccronexpr;
}

void MegaBackupController::setCcronexpr(const cron_expr &value)
{
    ccronexpr = value;
}

MegaBackupListener *MegaBackupController::getBackupListener() const
{
    return backupListener;
}

void MegaBackupController::setBackupListener(MegaBackupListener *value)
{
    backupListener = value;
}

long long MegaBackupController::getTotalFiles() const
{
    return totalFiles;
}

void MegaBackupController::setTotalFiles(long long value)
{
    totalFiles = value;
}

int64_t MegaBackupController::getCurrentBKStartTime() const
{
    return currentBKStartTime;
}

void MegaBackupController::setCurrentBKStartTime(const int64_t &value)
{
    currentBKStartTime = value;
}

int64_t MegaBackupController::getUpdateTime() const
{
    return updateTime;
}

void MegaBackupController::setUpdateTime(const int64_t &value)
{
    updateTime = value;
}

long long MegaBackupController::getTransferredBytes() const
{
    return transferredBytes;
}

void MegaBackupController::setTransferredBytes(long long value)
{
    transferredBytes = value;
}

long long MegaBackupController::getTotalBytes() const
{
    return totalBytes;
}

void MegaBackupController::setTotalBytes(long long value)
{
    totalBytes = value;
}

long long MegaBackupController::getSpeed() const
{
    return speed;
}

void MegaBackupController::setSpeed(long long value)
{
    speed = value;
}

long long MegaBackupController::getMeanSpeed() const
{
    return meanSpeed;
}

void MegaBackupController::setMeanSpeed(long long value)
{
    meanSpeed = value;
}

long long MegaBackupController::getNumberFiles() const
{
    return numberFiles;
}

void MegaBackupController::setNumberFiles(long long value)
{
    numberFiles = value;
}

long long MegaBackupController::getNumberFolders() const
{
    return numberFolders;
}

void MegaBackupController::setNumberFolders(long long value)
{
    numberFolders = value;
}

int64_t MegaBackupController::getLastbackuptime() const
{
    return lastbackuptime;
}

void MegaBackupController::setLastbackuptime(const int64_t &value)
{
    lastbackuptime = value;
}

void MegaBackupController::setState(int value)
{
    state = value;
}

bool MegaBackupController::isBusy() const
{
    return (state == BACKUP_ONGOING) || (state == BACKUP_REMOVING_EXCEEDING || (state == BACKUP_SKIPPING));
}

std::string MegaBackupController::epochdsToString(const int64_t rawtimeds) const
{
    struct tm dt;
    char buffer [40];
    time_t rawtime = rawtimeds/10;
    m_localtime(rawtime, &dt);

    strftime(buffer, sizeof( buffer ), "%Y%m%d%H%M%S", &dt);

    return std::string(buffer);
}

int64_t MegaBackupController::stringTimeTods(string stime) const
{
    struct tm dt;
    memset(&dt, 0, sizeof(struct tm));
#ifdef _WIN32
    if (stime.size() != 14)
    {
        return 0; //better control of this?
    }
    for(int i=0;i<14;i++)
    {
        if ( (stime.at(i) < '0') || (stime.at(i) > '9') )
        {
            return 0; //better control of this?
        }
    }

    dt.tm_year = atoi(stime.substr(0,4).c_str()) - 1900;
    dt.tm_mon = atoi(stime.substr(4,2).c_str()) - 1;
    dt.tm_mday = atoi(stime.substr(6,2).c_str());
    dt.tm_hour = atoi(stime.substr(8,2).c_str());
    dt.tm_min = atoi(stime.substr(10,2).c_str());
    dt.tm_sec = atoi(stime.substr(12,2).c_str());
#else
    strptime(stime.c_str(), "%Y%m%d%H%M%S", &dt);
#endif
    dt.tm_isdst = -1; //let mktime interprete if time has Daylight Saving Time flag correction
                        //TODO: would this work cross platformly? At least I believe it'll be consistent with localtime. Otherwise, we'd need to save that
    return (mktime(&dt))*10;
}

void MegaBackupController::clearCurrentBackupData()
{
    this->recursive = 0;
    this->pendingTransfers = 0;
    this->pendingFolders.clear();
    for (std::vector<MegaTransfer *>::iterator it = failedTransfers.begin(); it != failedTransfers.end(); it++)
    {
        delete *it;
    }
    this->failedTransfers.clear();
    this->currentHandle = UNDEF;
    this->currentBKStartTime = 0;
    this->updateTime = 0;
    this->transferredBytes = 0;
    this->totalBytes = 0;
    this->speed = 0;
    this->meanSpeed = 0;
    this->numberFiles = 0;
    this->totalFiles = 0;
    this->numberFolders = 0;
}


void MegaBackupController::start(bool skip)
{
    LOG_info << "starting backup of " << basepath << ". Next one will be in " << getNextStartTimeDs(startTime)-offsetds << " ds" ;
    clearCurrentBackupData();
    this->setCurrentBKStartTime(Waiter::ds); //notice: this is != StarTime

    size_t plastsep = basepath.find_last_of("\\/");
    if(plastsep == string::npos)
        plastsep = size_t(-1);
    string name = basepath.substr(plastsep+1);

    std::ostringstream ossremotename;
    ossremotename << name;
    ossremotename << "_bk_";
    ossremotename << epochdsToString(offsetds+startTime);
    string backupname = ossremotename.str();
    currentName = backupname;

    lastbackuptime = (std::max)(lastbackuptime,offsetds+startTime);

    megaApi->fireOnBackupStart(this);

    MegaNode *parent = megaApi->getNodeByHandle(parenthandle);
    if(!parent)
    {
        LOG_err << "Could not start backup: "<< name << ". Parent node not found";
        megaApi->fireOnBackupFinish(this, make_unique<MegaErrorPrivate>(API_ENOENT));

    }
    else
    {
        if (skip)
        {
            state = BACKUP_SKIPPING;
        }
        else
        {
            state = BACKUP_ONGOING;
        }
        megaApi->fireOnBackupStateChanged(this);

        auto localpath = LocalPath::fromPath(basepath, *client->fsaccess);

        MegaNode *child = megaApi->getChildNode(parent, backupname.c_str());

        if(!child || !child->isFolder())
        {
            pendingFolders.push_back(localpath);
            megaApi->createFolder(backupname.c_str(), parent, this);
        }
        else
        {
            LOG_err << "Could not start backup: "<< backupname << ". Backup already exists";
            megaApi->fireOnBackupFinish(this, make_unique<MegaErrorPrivate>(API_EEXIST));
            state = BACKUP_ACTIVE;

        }

        delete child;
        delete parent;
    }
}

void MegaBackupController::onFolderAvailable(MegaHandle handle)
{
    MegaNode *parent = megaApi->getNodeByHandle(handle);
    if(currentHandle == UNDEF)//main folder of the backup instance
    {
        currentHandle = handle;
        if (state == BACKUP_ONGOING)
        {
            this->pendingTags++;
            megaApi->setCustomNodeAttribute(parent, "BACKST", "ONGOING", this);
        }
        else
        {
            this->pendingTags++;
            megaApi->setCustomNodeAttribute(parent, "BACKST", "SKIPPED", this);
        }
    }
    else
    {
        numberFolders++;
    }
    recursive++;
    LocalPath localPath = pendingFolders.front();
    pendingFolders.pop_front();

    if (state == BACKUP_ONGOING)
    {
        LocalPath localname;
        DirAccess* da;
        da = client->fsaccess->newdiraccess();
        if (da->dopen(&localPath, NULL, false))
        {
            FileSystemType fsType = client->fsaccess->getFilesystemType(localPath);

            while (da->dnext(localPath, localname, client->followsymlinks))
            {
                ScopedLengthRestore restoreLen(localPath);
                localPath.appendWithSeparator(localname, false, client->fsaccess->localseparator);

                //TODO: add exclude filters here

                auto fa = client->fsaccess->newfileaccess();
                if(fa->fopen(localPath, true, false))
                {
                    string name = localname.toName(*client->fsaccess);
                    if(fa->type == FILENODE)
                    {
                        pendingTransfers++;

                        totalFiles++;
                        megaApi->startUpload(false, localPath.toPath(*client->fsaccess).c_str(), parent, (const char *)NULL, -1, folderTransferTag, true, NULL, false, false, fsType, this);
                    }
                    else
                    {
                        MegaNode *child = megaApi->getChildNode(parent, name.c_str());
                        if(!child || !child->isFolder())
                        {
                            pendingFolders.push_back(localPath);
                            megaApi->createFolder(name.c_str(), parent, this);
                        }
                        else
                        {
                            pendingFolders.push_front(localPath);
                            onFolderAvailable(child->getHandle());
                        }
                        delete child;
                    }
                }
            }
        }

        delete da;
    }
    else if (state == BACKUP_SKIPPING)
    {
        //do nth
    }
    else
    {
        LOG_warn << " Backup folder created while not ONGOING: " << localPath.toPath(*client->fsaccess);
    }

    delete parent;
    recursive--;

    checkCompletion();
}

bool MegaBackupController::checkCompletion()
{
    if(!recursive && !pendingFolders.size() && !pendingTransfers && !pendingTags)
    {
        error e = API_OK;
        LOG_debug << "Folder transfer finished - " << this->getTransferredBytes() << " of " << this->getTotalBytes();
        MegaNode *node = megaApi->getNodeByHandle(currentHandle);
        if (node)
        {
            if (failedTransfers.size())
            {
                this->pendingTags++;
                megaApi->setCustomNodeAttribute(node, "BACKST", "INCOMPLETE", this);
                e = API_EINCOMPLETE;
            }
            else if (state != BACKUP_SKIPPING)
            {
                this->pendingTags++;
                megaApi->setCustomNodeAttribute(node, "BACKST", "COMPLETE", this);
            }
            else
            {
                e = API_EEXPIRED;
            }
            delete node;
        }
        else
        {
            LOG_err << "Could not set backup attribute, node not found for: " << currentName;
            e = API_ENOENT;
        }

        state = BACKUP_ACTIVE;
        megaApi->fireOnBackupFinish(this, make_unique<MegaErrorPrivate>(e));
        megaApi->fireOnBackupStateChanged(this);

        removeexceeding(e == API_OK);

        return true;
    }
    return false;
}

int MegaBackupController::getFolderTransferTag() const
{
    return folderTransferTag;
}

void MegaBackupController::setFolderTransferTag(int value)
{
    folderTransferTag = value;
}

int64_t MegaBackupController::getOffsetds() const
{
    return offsetds;
}

void MegaBackupController::setOffsetds(const int64_t &value)
{
    offsetds = value;
}

void MegaBackupController::abortCurrent()
{
    LOG_debug << "Setting backup as aborted: " << currentName;

    if (state == BACKUP_ONGOING || state == BACKUP_SKIPPING)
    {
        megaApi->fireOnBackupFinish(this, make_unique<MegaErrorPrivate>(API_EINCOMPLETE));
    }

    state = BACKUP_ACTIVE;
    megaApi->fireOnBackupStateChanged(this);

    MegaNode *node = megaApi->getNodeByHandle(currentHandle);
    if (node)
    {
        this->pendingTags++;
        megaApi->setCustomNodeAttribute(node, "BACKST", "ABORTED", this);
        delete node;
    }
    else
    {
        LOG_err << "Could not set backup attribute, node not found for: " << currentName;
    }

    clearCurrentBackupData();

}

void MegaBackupController::onRequestFinish(MegaApi *, MegaRequest *request, MegaError *e)
{
    int type = request->getType();
    int errorCode = e->getErrorCode();

    if(type == MegaRequest::TYPE_CREATE_FOLDER)
    {
        if(!errorCode)
        {
            onFolderAvailable(request->getNodeHandle());
            megaApi->fireOnBackupUpdate(this);
        }
        else
        {
            pendingFolders.pop_front();
            megaApi->fireOnBackupUpdate(this);
            checkCompletion();
        }
    }
    else if(type == MegaRequest::TYPE_REMOVE)
    {
        pendingremovals--;
        if (!pendingremovals)
        {
            assert(pendingTags>=0);
            if (pendingTags <= 0)
            {
                state = BACKUP_ACTIVE;
            }
            megaApi->fireOnBackupStateChanged(this);
        }
    }
    else if(type == MegaRequest::TYPE_SET_ATTR_NODE)
    {
        pendingTags--;
        assert(pendingTags>=0);

        if (!pendingTags)
        {
            if (state == BACKUP_ONGOING || state == BACKUP_SKIPPING)
            {
                checkCompletion();
            }
            else // from REMOVING OR after abort
            {
                if (state != BACKUP_ACTIVE)
                {
                    state = BACKUP_ACTIVE;
                    megaApi->fireOnBackupStateChanged(this);
                }
            }
        }
    }

}

void MegaBackupController::onTransferStart(MegaApi *, MegaTransfer *t)
{
    LOG_verbose << " at MegaBackupController::onTransferStart: "+ string(t->getFileName());

    this->setTotalBytes(this->getTotalBytes() + t->getTotalBytes());
    this->setUpdateTime(Waiter::ds);

    megaApi->fireOnBackupUpdate(this);
}

void MegaBackupController::onTransferUpdate(MegaApi *, MegaTransfer *t)
{
    LOG_verbose << " at MegaBackupController::onTransferUpdate";

    this->setTransferredBytes(this->getTransferredBytes() + t->getDeltaSize());
    this->setUpdateTime(Waiter::ds);
    this->setSpeed(t->getSpeed());
    this->setMeanSpeed(t->getMeanSpeed());

    megaApi->fireOnBackupUpdate(this);
}

void MegaBackupController::onTransferTemporaryError(MegaApi *, MegaTransfer *t, MegaError *e)
{
    LOG_verbose << " at MegaBackupController::onTransferTemporaryError";

    unique_ptr<MegaErrorPrivate> errorPrivate;
    if (dynamic_cast<MegaErrorPrivate *>(e))
    {
        errorPrivate = unique_ptr<MegaErrorPrivate>(dynamic_cast<MegaErrorPrivate *>(e->copy()));
    }
    else
    {
         errorPrivate = make_unique<MegaErrorPrivate>(e->getErrorCode());
    }

    megaApi->fireOnBackupTemporaryError(this, std::move(errorPrivate));  // we received a non-owning pointer but we need to pass ownership to fireOnBackupTemporaryError
}

void MegaBackupController::onTransferFinish(MegaApi *, MegaTransfer *t, MegaError *e)
{
    LOG_verbose << " at MegaackupController::onTransferFinish";

    pendingTransfers--;
//    this->setTransferredBytes(this->getTransferredBytes() + t->getDeltaSize()); //TODO: THIS was in MegaUploaderController (which seems wrong)
    this->setUpdateTime(Waiter::ds);
    this->setSpeed(t->getSpeed());
    this->setMeanSpeed(t->getMeanSpeed());

    if (e->getErrorCode() != MegaError::API_OK)
    {
        failedTransfers.push_back(t->copy());
    }
    else
    {
        numberFiles++;
    }

    megaApi->fireOnBackupUpdate(this);

    checkCompletion();
}

MegaBackup *MegaBackupController::copy()
{
    return new MegaBackupController(this);
}


int MegaBackupController::getMaxBackups() const
{
    return maxBackups;
}

void MegaBackupController::setMaxBackups(int value)
{
    maxBackups = value;
}

string MegaBackupController::getBackupName() const
{
    return backupName;
}

void MegaBackupController::setBackupName(const string &value)
{
    backupName = value;
}

int64_t MegaBackupController::getPeriod() const
{
    return period;
}

const char *MegaBackupController::getPeriodString() const
{
    return periodstring.c_str();
}

void MegaBackupController::setPeriod(const int64_t &value)
{
    period = value;
    if (value != -1)
    {
        this->offsetds=m_time(NULL)*10 - Waiter::ds;
        this->startTime = lastbackuptime?(lastbackuptime+period-offsetds):Waiter::ds;
        if (this->startTime < Waiter::ds)
            this->startTime = Waiter::ds;
    }
}

void MegaBackupController::setPeriodstring(const string &value)
{
    periodstring = value;
    valid = true;
    if (value.size())
    {
        const char* err = NULL;
        memset((cron_expr *)&ccronexpr, 0, sizeof(ccronexpr));
        cron_parse_expr(periodstring.c_str(), &ccronexpr, &err);

        if (err != NULL)
        {
            valid = false;
            return;
        }

        this->offsetds=m_time(NULL)*10 - Waiter::ds;

        if (!lastbackuptime)
        {
            this->startTime = Waiter::ds;
        }
        else
        {
            this->startTime = this->getNextStartTimeDs(lastbackuptime-offsetds);
        }
        if (this->startTime < Waiter::ds)
        {
            //to avoid skipping (do empty backups with SKIPPED attr) for a long while (e.g: period too short or downtime too long)
            // we determine a max number of executions to skip.

            int maxBackupToSkip = maxBackups + 10;
            int64_t* starttimes = new int64_t[maxBackupToSkip];
            int64_t next = lastbackuptime-offsetds;
            int64_t previousnext = next;

            for (int i = 0; i < maxBackupToSkip; i++)
            {
                starttimes[i] = startTime;
            }

            int j = 0;

            do
            {
                previousnext = next;
                next = this->getNextStartTimeDs(next);
                starttimes[j] = next;
                j = (j==(maxBackupToSkip-1))?0:j+1;
            } while (next > previousnext && next < Waiter::ds);

            if (!attendPastBackups)
            {
                this->startTime = next;
            }
            else
            {
                this->startTime = starttimes[j]; //starttimes[j] should have the oldest time
            }
            delete [] starttimes;
        }
        LOG_debug << " Next Backup set in " << startTime - Waiter::ds << " deciseconds. At: " << epochdsToString((this->startTime+this->offsetds));
    }
}

int64_t MegaBackupController::getStartTime() const
{
    return startTime;
}

void MegaBackupController::setStartTime(const int64_t &value)
{
    startTime = value;
}

int MegaBackupController::getTag() const
{
    return this->tag;
}

void MegaBackupController::setTag(int value)
{
    tag = value;
}

MegaHandle MegaBackupController::getMegaHandle() const
{
    return this->parenthandle;
}

void MegaBackupController::setMegaHandle(const MegaHandle &value)
{
    parenthandle = value;
}

const char *MegaBackupController::getLocalFolder() const
{
    return this->basepath.c_str();
}

void MegaBackupController::setLocalFolder(const string &value)
{
    basepath = value;
}

int MegaBackupController::getState() const
{
    return state;
}

MegaBackupController::~MegaBackupController()
{
    megaApi->removeRequestListener(this);
    megaApi->removeTransferListener(this);

    for (std::vector<MegaTransfer *>::iterator it = failedTransfers.begin(); it != failedTransfers.end(); it++)
    {
        delete *it;
    }
}

MegaFolderDownloadController::MegaFolderDownloadController(MegaApiImpl *megaApi, MegaTransferPrivate *transfer)
{
    this->megaApi = megaApi;
    this->client = megaApi->getMegaClient();
    this->transfer = transfer;
    this->listener = transfer->getListener();
    this->recursive = 0;
    this->pendingTransfers = 0;
    this->tag = transfer->getTag();
}

void MegaFolderDownloadController::start(MegaNode *node)
{
    transfer->setFolderTransferTag(-1);
    transfer->setStartTime(Waiter::ds);
    transfer->setState(MegaTransfer::STATE_QUEUED);
    megaApi->fireOnTransferStart(transfer);

    const char *parentPath = transfer->getParentPath();
    const char *fileName = transfer->getFileName();
    bool deleteNode = false;

    if (!node)
    {
        node = megaApi->getNodeByHandle(transfer->getNodeHandle());
        if (!node)
        {
            LOG_debug << "Folder download failed. Node not found";
            DBTableTransactionCommitter committer(client->tctable);
            megaApi->fireOnTransferFinish(transfer, make_unique<MegaErrorPrivate>(API_ENOENT), committer);
            return;
        }
        deleteNode = true;
    }

    LocalPath name;
    LocalPath path;

    if (parentPath)
    {
        path = LocalPath::fromPath(parentPath, *client->fsaccess);
    }
    else
    {
        path = LocalPath::fromPath(".", *client->fsaccess);
        path.appendWithSeparator(LocalPath::fromPath("", *client->fsaccess), true, client->fsaccess->localseparator);
    }

    FileSystemType fsType = client->fsaccess->getFilesystemType(path);

    if (!fileName)
    {
        name = LocalPath::fromName(node->getName(), *client->fsaccess, fsType);
    }
    else
    {
        name = LocalPath::fromName(fileName, *client->fsaccess, fsType);
    }
    
    path.appendWithSeparator(name, true, client->fsaccess->localseparator);

    path.ensureWinExtendedPathLenPrefix();

    transfer->setPath(path.toPath(*client->fsaccess).c_str());
    downloadFolderNode(node, path, fsType);

    if (deleteNode)
    {
        delete node;
    }
}

void MegaFolderDownloadController::cancel()
{
    cancelled = true; //we dont want to further checkcompletion, and produce multile fireOnTransferFinish -> multiple deletions

    //remove subtransfers from pending transferQueue
    megaApi->cancelPendingTransfersByFolderTag(tag);

    //remove ongoing subtransfers
    long long cancelledSubTransfers = 0;

    std::unique_ptr<DBTableTransactionCommitter> insideCommiter;
    DBTableTransactionCommitter *committer = client->tctable ? client->tctable->getTransactionCommitter() : nullptr;
    if (!committer)
    {
        insideCommiter.reset(new DBTableTransactionCommitter(client->tctable));
        committer = insideCommiter.get();
    }

    while (!subTransfers.empty())
    {
        auto subTransfer = *subTransfers.begin();

        Transfer *transfer = subTransfer->getTransfer();
        if (!transfer)
        {
            LOG_warn << "Subtransfer without attached Transfer for folder transfer: " << subTransfer->getFileName();

            subTransfer->setState(MegaTransfer::STATE_CANCELLED);
            megaApi->fireOnTransferFinish(subTransfer, make_unique<MegaErrorPrivate>(API_EINCOMPLETE), *committer);

            continue;
        }

#ifdef _WIN32
        if (transfer->type == GET)
        {
            transfer->localfilename.editStringDirect()->append("", 1);
            WIN32_FILE_ATTRIBUTE_DATA fad;
            if (GetFileAttributesExW((LPCWSTR)transfer->localfilename.editStringDirect()->data(), GetFileExInfoStandard, &fad))
                SetFileAttributesW((LPCWSTR)transfer->localfilename.editStringDirect()->data(), fad.dwFileAttributes & ~FILE_ATTRIBUTE_HIDDEN);
            transfer->localfilename.editStringDirect()->resize(transfer->localfilename.editStringDirect()->size()-1);
        }
#endif

        MegaErrorPrivate megaError(API_EINCOMPLETE);
        subTransfer->setLastError(&megaError);

        bool found = false;
        file_list files = transfer->files;
        file_list::iterator iterator = files.begin();
        while (iterator != files.end())
        {
            File *file = *iterator;
            iterator++;
            if (file->tag == subTransfer->getTag())
            {
                found = true;
                if (!file->syncxfer)
                {
                    client->stopxfer(file, committer);
                }
                else
                {
                    LOG_err << "Sync subtransfer found for folder transfer: " << subTransfer->getFileName();
                    assert(false);
                }
                break;
            }
        }

        if (!found)
        {
            LOG_warn << "No file found for subtransfer: " << subTransfer->getFileName();

            subTransfer->setState(MegaTransfer::STATE_CANCELLED);
            megaApi->fireOnTransferFinish(subTransfer, make_unique<MegaErrorPrivate>(API_EINCOMPLETE), *committer);
        }
        cancelledSubTransfers++;
    }

    LOG_verbose << "MegaFolderDownloadController, cancelled subTransfers = " << cancelledSubTransfers;

    transfer = nullptr;  // no final callback for this one since it is being destroyed now
}


void MegaFolderDownloadController::downloadFolderNode(MegaNode *node, LocalPath& localpath, FileSystemType fsType)
{
    recursive++;

    auto da = client->fsaccess->newfileaccess();
    if (!da->fopen(localpath, true, false))
    {
        if (!client->fsaccess->mkdirlocal(localpath))
        {
            da.reset();
            LOG_err << "Unable to create folder: " << localpath.toPath(*client->fsaccess);

            recursive--;
            mLastError = API_EWRITE;
            mIncompleteTransfers++;
            checkCompletion();
            return;
        }
    }
    else if (da->type != FILENODE)
    {
        LOG_debug << "Already existing folder detected: " << localpath.toPath(*client->fsaccess);
    }
    else
    {
        da.reset();
        LOG_err << "Local file detected where there should be a folder: " << localpath.toPath(*client->fsaccess);

        recursive--;
        mLastError = API_EEXIST;
        mIncompleteTransfers++;
        checkCompletion();
        return;
    }
    da.reset();

    MegaNodeList *children = NULL;
    bool deleteChildren = false;
    if (node->isForeign())
    {
        children = node->getChildren();
    }
    else
    {
        children = megaApi->getChildren(node, MegaApi::ORDER_NONE);  // no order is much faster for a very large folder (or nested folders with large subfolders)
        deleteChildren = true;
    }

    if (!children)
    {
        LOG_err << "Child nodes not found: " << localpath.toPath(*client->fsaccess);
        recursive--;
        mLastError = API_ENOENT;
        mIncompleteTransfers++;
        checkCompletion();
        return;
    }

    for (int i = 0; i < children->size(); i++)
    {
        MegaNode *child = children->get(i);
        
        ScopedLengthRestore restoreLen(localpath);
        localpath.appendWithSeparator(LocalPath::fromName(child->getName(), *client->fsaccess, fsType), true, client->fsaccess->localseparator);

        string utf8path = localpath.toPath(*client->fsaccess);

        if (child->getType() == MegaNode::TYPE_FILE)
        {
            pendingTransfers++;
            megaApi->startDownload(false, child, utf8path.c_str(), tag, transfer->getAppData(), this);
        }
        else
        {
            downloadFolderNode(child, localpath, fsType);
        }
    }

    recursive--;
    checkCompletion();
    if (deleteChildren)
    {
        delete children;
    } 
}

void MegaFolderDownloadController::checkCompletion()
{
    if (!cancelled && !recursive && !pendingTransfers)
    {
        LOG_debug << "Folder download finished - " << transfer->getTransferredBytes() << " of " << transfer->getTotalBytes();
        transfer->setState(MegaTransfer::STATE_COMPLETED);
        transfer->setLastError(&mLastError);
        DBTableTransactionCommitter committer(client->tctable);
        megaApi->fireOnTransferFinish(transfer, make_unique<MegaErrorPrivate>(!mIncompleteTransfers ? API_OK : API_EINCOMPLETE), committer);
    }
}

void MegaFolderDownloadController::onTransferStart(MegaApi *, MegaTransfer *t)
{
    subTransfers.insert(static_cast<MegaTransferPrivate*>(t));
    assert(transfer);
    if (transfer)
    {
        transfer->setState(t->getState());
        transfer->setPriority(t->getPriority());
        transfer->setTotalBytes(transfer->getTotalBytes() + t->getTotalBytes());
        transfer->setUpdateTime(Waiter::ds);
        megaApi->fireOnTransferUpdate(transfer);
    }
}

void MegaFolderDownloadController::onTransferUpdate(MegaApi *, MegaTransfer *t)
{
    assert(transfer);
    if (transfer)
    {
        transfer->setState(t->getState());
        transfer->setPriority(t->getPriority());
        transfer->setTransferredBytes(transfer->getTransferredBytes() + t->getDeltaSize());
        transfer->setUpdateTime(Waiter::ds);
        transfer->setSpeed(t->getSpeed());
        transfer->setMeanSpeed(t->getMeanSpeed());
        megaApi->fireOnTransferUpdate(transfer);
    }
}

void MegaFolderDownloadController::onTransferFinish(MegaApi *, MegaTransfer *t, MegaError *e)
{
    subTransfers.erase(static_cast<MegaTransferPrivate*>(t));
    pendingTransfers--;
    assert(transfer);
    if (transfer)
    {
        transfer->setState(MegaTransfer::STATE_ACTIVE);
        transfer->setPriority(t->getPriority());
        transfer->setTransferredBytes(transfer->getTransferredBytes() + t->getDeltaSize());
        transfer->setUpdateTime(Waiter::ds);
        transfer->setSpeed(t->getSpeed());
        transfer->setMeanSpeed(t->getMeanSpeed());
        megaApi->fireOnTransferUpdate(transfer);
        if (e->getErrorCode())
        {
            mLastError = *e;
            mIncompleteTransfers++;
        }
        checkCompletion();
    }
}

#ifdef HAVE_LIBUV
StreamingBuffer::StreamingBuffer()
{
    this->capacity = 0;
    this->buffer = NULL;
    this->inpos = 0;
    this->outpos = 0;
    this->size = 0;
    this->free = 0;
    this->maxBufferSize = MAX_BUFFER_SIZE;
    this->maxOutputSize = MAX_OUTPUT_SIZE;
}

StreamingBuffer::~StreamingBuffer()
{
    delete [] buffer;
}

void StreamingBuffer::init(m_off_t capacity)
{
    assert(capacity > 0);
    if (capacity > maxBufferSize)
    {
        capacity = maxBufferSize;
    }

    this->capacity = static_cast<unsigned>(capacity);
    this->buffer = new char[this->capacity];
    this->inpos = 0;
    this->outpos = 0;
    this->size = 0;
    this->free = this->capacity;
}

unsigned int StreamingBuffer::append(const char *buf, unsigned int len)
{
    if (!buffer)
    {
        // initialize the buffer if it's not initialized yet
        init(len);
    }

    if (free < len)
    {
        LOG_debug << "Not enough available space";
        len = free;
    }

    // update the internal state
    int currentIndex = inpos;
    inpos += len;
    int remaining = inpos - capacity;
    inpos %= capacity;
    size += len;
    free -= len;

    // append the new data
    if (remaining <= 0)
    {
        memcpy(buffer + currentIndex, buf, len);
    }
    else
    {
        int num = len - remaining;
        memcpy(buffer + currentIndex, buf, num);
        memcpy(buffer, buf + num, remaining);
    }

    return len;
}

unsigned int StreamingBuffer::availableData()
{
    return size;
}

unsigned int StreamingBuffer::availableSpace()
{
    return free;
}

unsigned int StreamingBuffer::availableCapacity()
{
    return capacity;
}

uv_buf_t StreamingBuffer::nextBuffer()
{
    if (!size)
    {
        // no data available
        return uv_buf_init(NULL, 0);
    }

    // prepare output buffer
    char *outbuf = buffer + outpos;
    int len = size < maxOutputSize ? size : maxOutputSize;
    if (outpos + len > capacity)
    {
        len = capacity - outpos;
    }

    // update the internal state
    size -= len;
    outpos += len;
    outpos %= capacity;

    // return the buffer
    return uv_buf_init(outbuf, len);
}

void StreamingBuffer::freeData(unsigned int len)
{
    // update the internal state
    free += len;
}

void StreamingBuffer::setMaxBufferSize(unsigned int bufferSize)
{
    if (bufferSize)
    {
        this->maxBufferSize = bufferSize;
    }
    else
    {
        this->maxBufferSize = MAX_BUFFER_SIZE;
    }
}

void StreamingBuffer::setMaxOutputSize(unsigned int outputSize)
{
    if (outputSize)
    {
        this->maxOutputSize = outputSize;
    }
    else
    {
        this->maxOutputSize = MAX_OUTPUT_SIZE;
    }
}

// http_parser settings
http_parser_settings MegaTCPServer::parsercfg;

MegaTCPServer::MegaTCPServer(MegaApiImpl *megaApi, string basePath, bool tls, string certificatepath, string keypath, bool ipv6)
    : useIPv6(ipv6)
#ifdef ENABLE_EVT_TLS
    , useTLS(tls)
#else
    , useTLS(false)
#endif
{
    this->megaApi = megaApi;
    this->localOnly = true;
    this->started = false;
    this->port = 0;
    this->maxBufferSize = 0;
    this->maxOutputSize = 0;
    this->restrictedMode = MegaApi::TCP_SERVER_ALLOW_CREATED_LOCAL_LINKS;
    this->lastHandle = INVALID_HANDLE;
    this->remainingcloseevents = 0;
    this->closing = false;
    this->thread = new MegaThread();
#ifdef ENABLE_EVT_TLS
    this->certificatepath = certificatepath;
    this->keypath = keypath;
    this->closing = false;
    this->remainingcloseevents = 0;
    this->evtrequirescleaning = false;
#endif
    fsAccess = new MegaFileSystemAccess();

    if (basePath.size())
    {
        string sBasePath = basePath;
        int lastIndex = int(sBasePath.size() - 1);
        if (sBasePath[lastIndex] != '/' && sBasePath[lastIndex] != '\\')
        {
            string utf8Separator;
            fsAccess->local2path(&fsAccess->localseparator, &utf8Separator);
            sBasePath.append(utf8Separator);
        }
        this->basePath = sBasePath;
    }
    semaphoresdestroyed = false;
    uv_sem_init(&semaphoreEnd, 0);
    uv_sem_init(&semaphoreStartup, 0);
}

MegaTCPServer::~MegaTCPServer()
{
    stop();
    semaphoresdestroyed = true;
    uv_sem_destroy(&semaphoreStartup);
    uv_sem_destroy(&semaphoreEnd);
    delete fsAccess;

    LOG_verbose << " MegaTCPServer::~MegaTCPServer joining uv thread";
    thread->join();
    LOG_verbose << " MegaTCPServer::~MegaTCPServer deleting uv thread";
    delete thread;
}

bool MegaTCPServer::start(int port, bool localOnly)
{
    if (started && this->port == port && this->localOnly == localOnly)
    {
        LOG_verbose << "MegaTCPServer::start Alread started at that port, returning " << started;
        return true;
    }
    if (started)
    {
        stop();
    }

    this->port = port;
    this->localOnly = localOnly;

    thread->start(threadEntryPoint, this);
    uv_sem_wait(&semaphoreStartup);

    LOG_verbose << "MegaTCPServer::start. port = " << port << ", returning " << started;
    return started;
}

#ifdef ENABLE_EVT_TLS
int MegaTCPServer::uv_tls_writer(evt_tls_t *evt_tls, void *bfr, int sz)
{
    int rv = 0;
    uv_buf_t b;
    b.base = (char*)bfr;
    b.len = sz;

    MegaTCPContext *tcpctx = (MegaTCPContext*)evt_tls->data;
    assert(tcpctx != NULL);

    if (uv_is_writable((uv_stream_t*)(&tcpctx->tcphandle)))
    {
        uv_write_t *req = new uv_write_t();
        tcpctx->writePointers.push_back((char*)bfr);
        req->data = tcpctx;
        LOG_verbose << "Sending " << sz << " bytes of TLS data on port = " << tcpctx->server->port;
        if (int err = uv_write(req, (uv_stream_t*)&tcpctx->tcphandle, &b, 1, onWriteFinished_tls_async))
        {
            LOG_warn << "At uv_tls_writer: Finishing due to an error sending the response: " << err;
            tcpctx->writePointers.pop_back();
            delete [] (char*)bfr;
            delete req;

            closeTCPConnection(tcpctx);
        }
        rv = sz; //writer should return the written size
    }
    else
    {
        delete [] (char*)bfr;
        LOG_debug << " uv_is_writable returned false";
    }

    return rv;
}
#endif

// todo: a lot of this function is the same as initializeAndStartListening, we should factor them (maybe call that one from this one?)
void MegaTCPServer::run()
{
    LOG_debug << " Running tcp server: " << port << " TLS=" << useTLS;

#ifdef ENABLE_EVT_TLS
    if (useTLS)
    {
        if (evt_ctx_init_ex(&evtctx, certificatepath.c_str(), keypath.c_str()) != 1 )
        {
            LOG_err << "Unable to init evt ctx";
            port = 0;
            uv_sem_post(&semaphoreStartup);
            uv_sem_post(&semaphoreEnd);
            return;
        }
        evt_ctx_set_nio(&evtctx, NULL, uv_tls_writer);
    }
#endif

    uv_loop_init(&uv_loop);

    uv_async_init(&uv_loop, &exit_handle, onCloseRequested);
    exit_handle.data = this;

    uv_tcp_init(&uv_loop, &server);
    server.data = this;

    uv_tcp_keepalive(&server, 0, 0);

    union {
        struct sockaddr_in6 ipv6;
        struct sockaddr_in ipv4;
    } address;

    if (useIPv6)
    {
        if (localOnly)
        {
            uv_ip6_addr("::1", port, &address.ipv6);
        }
        else
        {
            uv_ip6_addr("::", port, &address.ipv6);
        }
    }
    else
    {
        if (localOnly)
        {
            uv_ip4_addr("127.0.0.1", port, &address.ipv4);
        }
        else
        {
            uv_ip4_addr("0.0.0.0", port, &address.ipv4);
        }
    }

    uv_connection_cb onNewClientCB;
#ifdef ENABLE_EVT_TLS
    if (useTLS)
    {
         onNewClientCB = onNewClient_tls;
    }
    else
    {
#endif
        onNewClientCB = onNewClient;
#ifdef ENABLE_EVT_TLS
    }
#endif

    if(uv_tcp_bind(&server, (const struct sockaddr*)&address, 0)
        || uv_listen((uv_stream_t*)&server, 32, onNewClientCB))
    {
        LOG_err << "TCP failed to bind/listen port = " << port;
        port = 0;

        uv_close((uv_handle_t *)&exit_handle,NULL);
        uv_close((uv_handle_t *)&server,NULL);
        uv_sem_post(&semaphoreStartup);
        uv_run(&uv_loop, UV_RUN_ONCE); // so that resources are cleaned peacefully
        uv_sem_post(&semaphoreEnd);
        return;
    }

    LOG_info << "TCP" << (useTLS ? "(tls)" : "") << " server started on port " << port;
    started = true;
    uv_sem_post(&semaphoreStartup);

    LOG_info << "Starting uv loop ...";
    uv_run(&uv_loop, UV_RUN_DEFAULT);

    LOG_info << "UV loop ended";
#ifdef ENABLE_EVT_TLS
    if (useTLS)
    {
        //evt_ctx_free(&evtctx); //This causes invalid free when called second time!! collides with memory allocated elsewhere (e.g: via curl_global_init!)
        SSL_CTX_free(evtctx.ctx);
    }
#endif
    uv_loop_close(&uv_loop);
    started = false;
    port = 0;
    LOG_debug << "UV loop thread exit";
}

void MegaTCPServer::initializeAndStartListening()
{
#ifdef ENABLE_EVT_TLS
    if (useTLS)
    {
        if (evt_ctx_init_ex(&evtctx, certificatepath.c_str(), keypath.c_str()) != 1 )
        {
            LOG_err << "Unable to init evt ctx";
            port = 0;
            uv_sem_post(&semaphoreStartup);
            uv_sem_post(&semaphoreEnd);
            return;
        }
        evtrequirescleaning = true;
        evt_ctx_set_nio(&evtctx, NULL, uv_tls_writer);
    }
#endif

    uv_loop_init(&uv_loop);

    uv_async_init(&uv_loop, &exit_handle, onCloseRequested);
    exit_handle.data = this;

    uv_tcp_init(&uv_loop, &server);
    server.data = this;

    uv_tcp_keepalive(&server, 0, 0);

    union {
        struct sockaddr_in6 ipv6;
        struct sockaddr_in ipv4;
    } address;

    if (useIPv6)
    {
        if (localOnly)
        {
            uv_ip6_addr("::1", port, &address.ipv6);
        }
        else
        {
            uv_ip6_addr("::", port, &address.ipv6);
        }
    }
    else
    {
        if (localOnly)
        {
            uv_ip4_addr("127.0.0.1", port, &address.ipv4);
        }
        else
        {
            uv_ip4_addr("0.0.0.0", port, &address.ipv4);
        }
    }

    uv_connection_cb onNewClientCB;
#ifdef ENABLE_EVT_TLS
    if (useTLS)
    {
         onNewClientCB = onNewClient_tls;
    }
    else
    {
#endif
        onNewClientCB = onNewClient;
#ifdef ENABLE_EVT_TLS
    }
#endif

    if(uv_tcp_bind(&server, (const struct sockaddr*)&address, 0)
        || uv_listen((uv_stream_t*)&server, 32, onNewClientCB))
    {
        LOG_err << "TCP failed to bind/listen port = " << port;
        port = 0;
        uv_async_send(&exit_handle);
        //This is required in case uv_loop was already running so as to free references to "this".
        // a uv_sem_post will be required there, so that we can delete the server accordingly
        return;
    }

    LOG_info << "TCP" << (useTLS ? "(tls)" : "") << " server started on port " << port;
    started = true;
    uv_sem_post(&semaphoreStartup);
    LOG_debug << "UV loop already alive!";
}

void MegaTCPServer::stop(bool doNotWait)
{
    if (!started)
    {
        LOG_verbose << "Stopping non started MegaTCPServer port=" << port;
        return;
    }

    LOG_debug << "Stopping MegaTCPServer port = " << port;
    uv_async_send(&exit_handle);
    if (!doNotWait)
    {
        LOG_verbose << "Waiting for sempahoreEnd to conclude server stop port = " << port;
        uv_sem_wait(&semaphoreEnd); //this is signaled when closed my last connection
    }
    LOG_debug << "Stopped MegaTCPServer port = " << port;
    started = false;
}

int MegaTCPServer::getPort()
{
    return port;
}

bool MegaTCPServer::isLocalOnly()
{
    return localOnly;
}

void MegaTCPServer::setMaxBufferSize(int bufferSize)
{
    this->maxBufferSize = bufferSize <= 0 ? 0 : bufferSize;
}

void MegaTCPServer::setMaxOutputSize(int outputSize)
{
    this->maxOutputSize = outputSize <= 0 ? 0 : outputSize;
}

int MegaTCPServer::getMaxBufferSize()
{
    if (maxBufferSize)
    {
        return maxBufferSize;
    }

    return StreamingBuffer::MAX_BUFFER_SIZE;
}

int MegaTCPServer::getMaxOutputSize()
{
    if (maxOutputSize)
    {
        return maxOutputSize;
    }

    return StreamingBuffer::MAX_OUTPUT_SIZE;
}

void MegaTCPServer::setRestrictedMode(int mode)
{
    this->restrictedMode = mode;
}

int MegaTCPServer::getRestrictedMode()
{
    return restrictedMode;
}

bool MegaTCPServer::isHandleAllowed(handle h)
{
    return restrictedMode == MegaApi::TCP_SERVER_ALLOW_ALL
            || (restrictedMode == MegaApi::TCP_SERVER_ALLOW_CREATED_LOCAL_LINKS && allowedHandles.count(h))
            || (restrictedMode == MegaApi::TCP_SERVER_ALLOW_LAST_LOCAL_LINK && h == lastHandle);
}

void MegaTCPServer::clearAllowedHandles()
{
    allowedHandles.clear();
    lastHandle = INVALID_HANDLE;
}

char *MegaTCPServer::getLink(MegaNode *node, string protocol)
{
    if (!node)
    {
        return NULL;
    }

    lastHandle = node->getHandle();
    allowedHandles.insert(lastHandle);
    
    string localhostIP = useIPv6 ? "[::1]" : "127.0.0.1";

    ostringstream oss;
    oss << protocol << (useTLS ? "s" : "") << "://" << localhostIP << ":" << port << "/";
    char *base64handle = node->getBase64Handle();
    oss << base64handle;
    delete [] base64handle;

    if (node->isPublic() || node->isForeign())
    {
        char *base64key = node->getBase64Key();
        oss << "!" << base64key;
        delete [] base64key;

        if (node->isForeign())
        {
            oss << "!" << node->getSize();
            string *publicAuth = node->getPublicAuth();
            string *privAuth = node->getPrivateAuth();
            const char *chatAuth = node->getChatAuth();
            if (privAuth->size())
            {
                oss << "!f" << *privAuth;
            }
            else if (publicAuth->size())
            {
                oss << "!p" << *publicAuth;
            }
            else if (chatAuth && chatAuth[0])
            {
                oss << "!c" << chatAuth;
            }
        }
    }

    oss << "/";

    string name = node->getName();
    string escapedName;
    URLCodec::escape(&name, &escapedName);
    oss << escapedName;
    string link = oss.str();
    return MegaApi::strdup(link.c_str());
}

set<handle> MegaTCPServer::getAllowedHandles()
{
    return allowedHandles;
}

void MegaTCPServer::removeAllowedHandle(MegaHandle handle)
{
    allowedHandles.erase(handle);
}

void *MegaTCPServer::threadEntryPoint(void *param)
{
#ifndef _WIN32
    struct sigaction noaction;
    memset(&noaction, 0, sizeof(noaction));
    noaction.sa_handler = SIG_IGN;
    ::sigaction(SIGPIPE, &noaction, 0);
#endif

    MegaTCPServer *tcpServer = (MegaTCPServer *)param;
    tcpServer->run();
    return NULL;
}

#ifdef ENABLE_EVT_TLS
void MegaTCPServer::evt_on_rd(evt_tls_t *evt_tls, char *bfr, int sz)
{
    MegaTCPContext *tcpctx = (MegaTCPContext*)evt_tls->data;
    assert(tcpctx != NULL);

    uv_buf_t data;
    data.base = bfr;
    data.len = sz;

    if (!tcpctx->invalid)
    {
        tcpctx->server->processReceivedData(tcpctx, sz, &data);
    }
    else
    {
        LOG_debug << " Not procesing invalid data after failed evt_close";
    }
}

void MegaTCPServer::on_evt_tls_close(evt_tls_t *evt_tls, int status)
{
    MegaTCPContext *tcpctx = (MegaTCPContext*)evt_tls->data;
    assert(tcpctx != NULL);

    LOG_debug << "TLS connection closed. status = " << status;

    if (status == 1)
    {
        closeTCPConnection(tcpctx);
    }
    else
    {
        LOG_debug << "TLS connection closed failed!!! status = " << status;
        tcpctx->invalid = true;
    }
}

void MegaTCPServer::on_hd_complete( evt_tls_t *evt_tls, int status)
{
    MegaTCPContext *tcpctx = (MegaTCPContext*)evt_tls->data;
    LOG_debug << "TLS handshake finished in port = " << tcpctx->server->port << ". Status: " << status;

    if (status)
    {
        evt_tls_read(evt_tls, evt_on_rd); //this only stablish callback
        if ( tcpctx->server->respondNewConnection(tcpctx) )
        {
            // we dont need to explicitally start reading. on_tcp_read will be called
        }
    }
    else
    {
        evt_tls_close(evt_tls, on_evt_tls_close);
    }
}

void MegaTCPServer::onNewClient_tls(uv_stream_t *server_handle, int status)
{
    if (status < 0)
    {
        LOG_warn << " onNewClient_tls unexpected status: " << status;
        return;
    }

    // Create an object to save context information
    MegaTCPContext* tcpctx = ((MegaTCPServer *)server_handle->data)->initializeContext(server_handle);

    LOG_debug << "Connection received at port " << tcpctx->server->port << " ! " << tcpctx->server->connections.size();

    // Mutex to protect the data buffer
    uv_mutex_init(&tcpctx->mutex);

    // Async handle to perform writes
    uv_async_init(&tcpctx->server->uv_loop, &tcpctx->asynchandle, onAsyncEvent);

    // Accept the connection
    uv_tcp_init(&tcpctx->server->uv_loop, &tcpctx->tcphandle);
    if (uv_accept(server_handle, (uv_stream_t*)&tcpctx->tcphandle))
    {
        LOG_err << "uv_accept failed";
        onClose((uv_handle_t*)&tcpctx->tcphandle);
        return;
    }

    tcpctx->evt_tls = evt_ctx_get_tls(&tcpctx->server->evtctx);
    assert(tcpctx->evt_tls != NULL);
    tcpctx->evt_tls->data = tcpctx;
    if (evt_tls_accept(tcpctx->evt_tls, on_hd_complete))
    {
        LOG_err << "evt_tls_accept failed";
        evt_tls_close(tcpctx->evt_tls, on_evt_tls_close);
        return;
    }

    tcpctx->server->connections.push_back(tcpctx);

    tcpctx->server->readData(tcpctx);
}
#endif

void MegaTCPServer::readData(MegaTCPContext* tcpctx)
{
#ifdef ENABLE_EVT_TLS
    if (useTLS)
    {
        uv_read_start((uv_stream_t*)(&tcpctx->tcphandle), allocBuffer, on_tcp_read);
    }
    else
    {
#endif
        uv_read_start((uv_stream_t*)&tcpctx->tcphandle, allocBuffer, onDataReceived);
#ifdef ENABLE_EVT_TLS
    }
#endif
}

void MegaTCPServer::onNewClient(uv_stream_t* server_handle, int status)
{
    if (status < 0)
    {
        return;
    }

    // Create an object to save context information
    MegaTCPContext* tcpctx = ((MegaTCPServer *)server_handle->data)->initializeContext(server_handle);

    LOG_debug << "Connection received at port " << tcpctx->server->port << "! " << tcpctx->server->connections.size() << " tcpctx = " << tcpctx;

    // Mutex to protect the data buffer
    uv_mutex_init(&tcpctx->mutex);

    // Async handle to perform writes
    uv_async_init(&tcpctx->server->uv_loop, &tcpctx->asynchandle, onAsyncEvent);

    // Accept the connection
    uv_tcp_init(&tcpctx->server->uv_loop, &tcpctx->tcphandle);
    if (uv_accept(server_handle, (uv_stream_t*)&tcpctx->tcphandle))
    {
        LOG_err << "uv_accept failed";
        onClose((uv_handle_t*)&tcpctx->tcphandle);
        return;
    }

    tcpctx->server->connections.push_back(tcpctx);
    if (tcpctx->server->respondNewConnection(tcpctx))
    {
        // Start reading
        tcpctx->server->readData(tcpctx);
    }
}

void MegaTCPServer::allocBuffer(uv_handle_t *, size_t suggested_size, uv_buf_t* buf)
{
    // Reserve a buffer with the suggested size
    *buf = uv_buf_init(new char[suggested_size], static_cast<unsigned>(suggested_size));
}

void MegaTCPServer::onDataReceived(uv_stream_t* tcp, ssize_t nread, const uv_buf_t * buf)
{
    MegaTCPContext *tcpctx = (MegaTCPContext*) tcp->data;
    tcpctx->server->processReceivedData(tcpctx, nread, buf);
    delete [] buf->base;
}

#ifdef ENABLE_EVT_TLS
void MegaTCPServer::on_tcp_read(uv_stream_t *tcp, ssize_t nrd, const uv_buf_t *data)
{
    MegaTCPContext *tcpctx = (MegaTCPContext*) tcp->data;
    assert( tcpctx != NULL);

    LOG_debug << "Received " << nrd << " bytes at port " << tcpctx->server->port;
    if (!nrd)
    {
        return;
    }

    if (nrd < 0)
    {
        if (evt_tls_is_handshake_over(tcpctx->evt_tls))
        {
            LOG_verbose << "MegaTCPServer::on_tcp_read calling processReceivedData";
            tcpctx->server->processReceivedData(tcpctx, nrd, data);
            evt_tls_close(tcpctx->evt_tls, on_evt_tls_close);
        }
        else
        {
            //if handshake is not over, simply tear down without close_notify
            closeTCPConnection(tcpctx);
        }
        delete[] data->base;
        return;
    }

    evt_tls_feed_data(tcpctx->evt_tls, data->base, static_cast<int>(nrd));
    delete[] data->base;
}
#endif

void MegaTCPServer::onClose(uv_handle_t* handle)
{
    MegaTCPContext* tcpctx = (MegaTCPContext*) handle->data;

    // streaming transfers are automatically stopped when their listener is removed
    tcpctx->megaApi->removeTransferListener(tcpctx);
    tcpctx->megaApi->removeRequestListener(tcpctx);

    tcpctx->server->connections.remove(tcpctx);
    LOG_debug << "Connection closed: " << tcpctx->server->connections.size() << " port = " << tcpctx->server->port << " closing async handle";
    uv_close((uv_handle_t *)&tcpctx->asynchandle, onAsyncEventClose);
}

void MegaTCPServer::onAsyncEventClose(uv_handle_t *handle)
{
    MegaTCPContext* tcpctx = (MegaTCPContext*) handle->data;
    assert(!tcpctx->writePointers.size());

    int port = tcpctx->server->port;

    tcpctx->server->remainingcloseevents--;
    tcpctx->server->processOnAsyncEventClose(tcpctx);

    LOG_verbose << "At onAsyncEventClose port = " << tcpctx->server->port << " remaining=" << tcpctx->server->remainingcloseevents;

    if (!tcpctx->server->remainingcloseevents && tcpctx->server->closing && !tcpctx->server->semaphoresdestroyed)
    {
        uv_sem_post(&tcpctx->server->semaphoreStartup);
        uv_sem_post(&tcpctx->server->semaphoreEnd);
    }

    uv_mutex_destroy(&tcpctx->mutex);
    delete tcpctx;
    LOG_debug << "Connection deleted, port = " << port;
}

#ifdef ENABLE_EVT_TLS
void MegaTCPServer::onWriteFinished_tls(evt_tls_t *evt_tls, int status)
{
    MegaTCPContext *tcpctx = (MegaTCPContext*)evt_tls->data;
    assert(tcpctx != NULL);

    if (status < 0)
    {
        LOG_warn << " error received at onWriteFinished_tls: " << status;
    }

    if (tcpctx->finished)
    {
        LOG_debug << "At onWriteFinished_tls; TCP link closed, ignoring the result of the write";
        return;
    }
    tcpctx->server->processWriteFinished(tcpctx, status);
}

void MegaTCPServer::onWriteFinished_tls_async(uv_write_t* req, int status)
{
    MegaTCPContext *tcpctx = (MegaTCPContext*)req->data;
    assert(tcpctx->writePointers.size());
    delete [] tcpctx->writePointers.front();
    tcpctx->writePointers.pop_front();
    delete req;

    if (tcpctx->finished)
    {
        if (tcpctx->size == tcpctx->bytesWritten && !tcpctx->writePointers.size())
        {
            LOG_debug << "TCP link closed, shutdown result: " << status << " port = " << tcpctx->server->port;
        }
        else
        {
            LOG_debug << "TCP link closed, ignoring the result of the async TLS write: " << status << " port = " << tcpctx->server->port;
        }
        return;
    }

    if (status < 0)
    {
        LOG_warn << "Finishing request. Async TLS write failed: " << status;
        evt_tls_close(tcpctx->evt_tls, on_evt_tls_close);
        return;
    }

    if (tcpctx->size == tcpctx->bytesWritten && !tcpctx->writePointers.size())
    {
        LOG_debug << "Finishing request. All data delivered";
        evt_tls_close(tcpctx->evt_tls, on_evt_tls_close);
        return;
    }

    LOG_verbose << "Async TLS write finished";
    uv_async_send(&tcpctx->asynchandle);
}
#endif

void MegaTCPServer::onWriteFinished(uv_write_t* req, int status)
{
    MegaTCPContext* tcpctx = (MegaTCPContext*) req->data;
    assert(tcpctx != NULL);
    if (tcpctx->finished)
    {
        LOG_debug << "At onWriteFinished; TCP link closed, ignoring the result of the write";
        delete req;
        return;
    }

    tcpctx->server->processWriteFinished(tcpctx, status);
    delete req;
}

MegaTCPContext::MegaTCPContext()
{
    size = -1;
    finished = false;
    bytesWritten = 0;
#ifdef ENABLE_EVT_TLS
    evt_tls = NULL;
    invalid = false;
#endif
    server = NULL;
    megaApi = NULL;
}

MegaTCPContext::~MegaTCPContext()
{
#ifdef ENABLE_EVT_TLS
    if (evt_tls)
    {
        evt_tls_free(evt_tls);
    }
#endif
}

void MegaTCPServer::onAsyncEvent(uv_async_t* handle)
{
    MegaTCPContext* tcpctx = (MegaTCPContext*) handle->data;

    assert(tcpctx->server != NULL);
#ifdef ENABLE_EVT_TLS
    if (tcpctx->server->useTLS && !evt_tls_is_handshake_over(tcpctx->evt_tls))
    {
        LOG_debug << " skipping processAsyncEvent due to handshake not over on port = " << tcpctx->server->port;
        return;
    }
#endif
    tcpctx->server->processAsyncEvent(tcpctx);
}

void MegaTCPServer::onExitHandleClose(uv_handle_t *handle)
{
    MegaTCPServer *tcpServer = (MegaTCPServer*) handle->data;
    assert(tcpServer != NULL);

    tcpServer->remainingcloseevents--;
    LOG_verbose << "At onExitHandleClose port = " << tcpServer->port << " remainingcloseevent = " << tcpServer->remainingcloseevents;

    tcpServer->processOnExitHandleClose(tcpServer);

    if (!tcpServer->remainingcloseevents && !tcpServer->semaphoresdestroyed)
    {
        uv_sem_post(&tcpServer->semaphoreStartup);
        uv_sem_post(&tcpServer->semaphoreEnd);
    }
}

void MegaTCPServer::onCloseRequested(uv_async_t *handle)
{
    MegaTCPServer *tcpServer = (MegaTCPServer*) handle->data;
    LOG_debug << "TCP server stopping port=" << tcpServer->port;

    tcpServer->closing = true;

    for (list<MegaTCPContext*>::iterator it = tcpServer->connections.begin(); it != tcpServer->connections.end(); it++)
    {
        MegaTCPContext *tcpctx = (*it);
        closeTCPConnection(tcpctx);
    }

    tcpServer->remainingcloseevents++;
    LOG_verbose << "At onCloseRequested: closing server port = " << tcpServer->port << " remainingcloseevent = " << tcpServer->remainingcloseevents;
    uv_close((uv_handle_t *)&tcpServer->server, onExitHandleClose);
    tcpServer->remainingcloseevents++;
    LOG_verbose << "At onCloseRequested: closing exit_handle port = " << tcpServer->port << " remainingcloseevent = " << tcpServer->remainingcloseevents;
    uv_close((uv_handle_t *)&tcpServer->exit_handle, onExitHandleClose);
}

void MegaTCPServer::closeConnection(MegaTCPContext *tcpctx)
{
    LOG_verbose << "At closeConnection port = " << tcpctx->server->port;
#ifdef ENABLE_EVT_TLS
    if (tcpctx->server->useTLS)
    {
        evt_tls_close(tcpctx->evt_tls, on_evt_tls_close);
    }
    else
    {
#endif
        closeTCPConnection(tcpctx);
        return;
#ifdef ENABLE_EVT_TLS
    }
#endif
}

void MegaTCPServer::closeTCPConnection(MegaTCPContext *tcpctx)
{
    tcpctx->finished = true;
    if (!uv_is_closing((uv_handle_t*)&tcpctx->tcphandle))
    {
        tcpctx->server->remainingcloseevents++;
        LOG_verbose << "At closeTCPConnection port = " << tcpctx->server->port << " remainingcloseevent = " << tcpctx->server->remainingcloseevents;
        uv_close((uv_handle_t*)&tcpctx->tcphandle, onClose);
    }
}

void MegaTCPServer::processOnAsyncEventClose(MegaTCPContext *tcpctx) // without this closing breaks!
{
    LOG_debug << "At supposed to be virtual processOnAsyncEventClose";
}

void MegaTCPServer::processOnExitHandleClose(MegaTCPServer *tcpServer) // without this closing breaks!
{
    LOG_debug << "At supposed to be virtual processOnExitHandleClose";
}

void MegaTCPServer::processReceivedData(MegaTCPContext *tcpctx, ssize_t nread, const uv_buf_t *buf)
{
    LOG_debug << "At supposed to be virtual processReceivedData";
}

void MegaTCPServer::processAsyncEvent(MegaTCPContext *tcpctx)
{
    LOG_debug << "At supposed to be virtual processAsyncEvent";
}

///////////////////////////////
//  MegaHTTPServer specifics //
///////////////////////////////

MegaHTTPServer::MegaHTTPServer(MegaApiImpl *megaApi, string basePath, bool useTLS, string certificatepath, string keypath, bool useIPv6)
    : MegaTCPServer(megaApi, basePath, useTLS, certificatepath, keypath, useIPv6)
{
    // parser callbacks
    parsercfg.on_url = onUrlReceived;
    parsercfg.on_message_begin = onMessageBegin;
    parsercfg.on_headers_complete = onHeadersComplete;
    parsercfg.on_message_complete = onMessageComplete;
    parsercfg.on_header_field = onHeaderField;
    parsercfg.on_header_value = onHeaderValue;
    parsercfg.on_body = onBody;

    this->fileServerEnabled = true;
    this->folderServerEnabled = true;
    this->offlineAttribute = false;
    this->subtitlesSupportEnabled = false;
}

MegaTCPContext * MegaHTTPServer::initializeContext(uv_stream_t *server_handle)
{
    MegaHTTPContext* httpctx = new MegaHTTPContext();

    // Initialize the parser
    http_parser_init(&httpctx->parser, HTTP_REQUEST);

    // Set connection data
    MegaHTTPServer *server = (MegaHTTPServer *)(server_handle->data);

    httpctx->server = server;
    httpctx->megaApi = server->megaApi;
    httpctx->parser.data = httpctx;
    httpctx->tcphandle.data = httpctx;
    httpctx->asynchandle.data = httpctx;

    return httpctx;
}

void MegaHTTPServer::processReceivedData(MegaTCPContext *tcpctx, ssize_t nread, const uv_buf_t * buf)
{
    MegaHTTPContext* httpctx = dynamic_cast<MegaHTTPContext *>(tcpctx);

    LOG_debug << "Received " << nread << " bytes";

    ssize_t parsed = -1;
    if (nread >= 0)
    {
        if (nread == 0 && httpctx->parser.method == HTTP_PUT) //otherwise it will fail for files >65k in GVFS-DAV
        {
            LOG_debug << " Skipping parsing 0 length data for HTTP_PUT";
            parsed = 0;
        }
        else
        {
            parsed = http_parser_execute(&httpctx->parser, &parsercfg, buf->base, nread);
        }
    }

    LOG_verbose << " at onDataReceived, received " << nread << " parsed = " << parsed;

    if (parsed < 0 || nread < 0 || parsed < nread || httpctx->parser.upgrade)
    {
        LOG_debug << "Finishing request. Connection reset by peer or unsupported data";
        closeConnection(httpctx);
    }
}

void MegaHTTPServer::processWriteFinished(MegaTCPContext* tcpctx, int status)
{
    MegaHTTPContext* httpctx = dynamic_cast<MegaHTTPContext *>(tcpctx);
    if (httpctx->finished)
    {
        LOG_debug << "HTTP link closed, ignoring the result of the write";
        return;
    }

    httpctx->bytesWritten += httpctx->lastBufferLen;
    LOG_verbose << "Bytes written: " << httpctx->lastBufferLen << " Remaining: " << (httpctx->size - httpctx->bytesWritten);
    httpctx->lastBuffer = NULL;

    if (status < 0 || httpctx->size == httpctx->bytesWritten)
    {
        if (status < 0)
        {
            LOG_warn << "Finishing request. Write failed: " << status;
        }
        else
        {
            LOG_debug << "Finishing request. All data sent";
            if (httpctx->resultCode == API_EINTERNAL)
            {
                httpctx->resultCode = API_OK;
            }
        }

        closeConnection(httpctx);
        return;
    }

    uv_mutex_lock(&httpctx->mutex);
    if (httpctx->lastBufferLen)
    {
        httpctx->streamingBuffer.freeData(httpctx->lastBufferLen);
        httpctx->lastBufferLen = 0;
    }

    if (httpctx->pause)
    {
        if (httpctx->streamingBuffer.availableSpace() > httpctx->streamingBuffer.availableCapacity() / 2)
        {
            httpctx->pause = false;
            m_off_t start = httpctx->rangeStart + httpctx->rangeWritten + httpctx->streamingBuffer.availableData();
            m_off_t len =  httpctx->rangeEnd - httpctx->rangeStart - httpctx->rangeWritten - httpctx->streamingBuffer.availableData();

            LOG_debug << "Resuming streaming from " << start << " len: " << len
                     << " Buffer status: " << httpctx->streamingBuffer.availableSpace()
                     << " of " << httpctx->streamingBuffer.availableCapacity() << " bytes free";
            httpctx->megaApi->startStreaming(httpctx->node, start, len, httpctx);
        }
    }
    uv_mutex_unlock(&httpctx->mutex);

    uv_async_send(&httpctx->asynchandle);
}

void MegaHTTPServer::processOnAsyncEventClose(MegaTCPContext* tcpctx)
{
    MegaHTTPContext* httpctx = dynamic_cast<MegaHTTPContext *>(tcpctx);

    if (httpctx->resultCode == API_EINTERNAL)
    {
        httpctx->resultCode = API_EINCOMPLETE;
    }

    if (httpctx->transfer)
    {
        httpctx->megaApi->cancelTransfer(httpctx->transfer.get());
        httpctx->megaApi->fireOnStreamingFinish(httpctx->transfer.release(), make_unique<MegaErrorPrivate>(httpctx->resultCode)); // transfer will be deleted in fireOnStreamingFinish
    }

    delete httpctx->node;
    httpctx->node = NULL;
}

bool MegaHTTPServer::respondNewConnection(MegaTCPContext* tcpctx)
{
    return true;
}

void MegaHTTPServer::processOnExitHandleClose(MegaTCPServer *tcpServer)
{
}

MegaHTTPServer::~MegaHTTPServer()
{
    // if not stopped, the uv thread might want to access a pointer to this.
    // though this is done in the parent destructor, it could try to access it after vtable has been erased
    stop();
}

bool MegaHTTPServer::isHandleWebDavAllowed(handle h)
{
    return allowedWebDavHandles.count(h);
}

void MegaHTTPServer::clearAllowedHandles()
{
    allowedWebDavHandles.clear();
    MegaTCPServer::clearAllowedHandles();
}

set<handle> MegaHTTPServer::getAllowedWebDavHandles()
{
    return allowedWebDavHandles;
}

void MegaHTTPServer::removeAllowedWebDavHandle(MegaHandle handle)
{
    allowedWebDavHandles.erase(handle);
}

void MegaHTTPServer::enableFileServer(bool enable)
{
    this->fileServerEnabled = enable;
}

void MegaHTTPServer::enableFolderServer(bool enable)
{
    this->folderServerEnabled = enable;
}

void MegaHTTPServer::enableOfflineAttribute(bool enable)
{
    this->offlineAttribute = enable;
}

bool MegaHTTPServer::isFileServerEnabled()
{
    return fileServerEnabled;
}

bool MegaHTTPServer::isFolderServerEnabled()
{
    return folderServerEnabled;
}

bool MegaHTTPServer::isOfflineAttributeEnabled()
{
    return offlineAttribute;
}

bool MegaHTTPServer::isSubtitlesSupportEnabled()
{
    return subtitlesSupportEnabled;
}

void MegaHTTPServer::enableSubtitlesSupport(bool enable)
{
    this->subtitlesSupportEnabled = enable;
}

char *MegaHTTPServer::getWebDavLink(MegaNode *node)
{
    allowedWebDavHandles.insert(node->getHandle());
    return getLink(node);
}

int MegaHTTPServer::onMessageBegin(http_parser *)
{
    return 0;
}

int MegaHTTPServer::onHeadersComplete(http_parser *)
{
    return 0;
}

int MegaHTTPServer::onUrlReceived(http_parser *parser, const char *url, size_t length)
{
    MegaHTTPContext* httpctx = (MegaHTTPContext*) parser->data;
    httpctx->path.assign(url, length);
    LOG_debug << "URL received: " << httpctx->path;

    if (length < 9 || url[0] != '/' || (length >= 10 && url[9] != '/' && url[9] != '!'))
    {
        LOG_debug << "URL without node handle";
        return 0;
    }

    size_t index = 9;
    httpctx->nodehandle.assign(url + 1, 8);
    LOG_debug << "Node handle: " << httpctx->nodehandle;

    if (length > 53 && url[index] == '!')
    {
        httpctx->nodekey.assign(url + 10, 43);
        LOG_debug << "Link key: " << httpctx->nodekey;
        index = 53;

        if (length > 54 && url[index] == '!')
        {
           const char* startsize = url + index + 1;
           const char* endsize = strstr(startsize, "/");
           const char* endparam = strstr(startsize, "!");
           if (endsize && *startsize >= '0' && *startsize <= '9')
           {
               char* endptr;
               m_off_t size = strtoll(startsize, &endptr, 10);
               if ((endptr == endsize || endptr == endparam) && errno != ERANGE)
               {
                   httpctx->nodesize = size;
                   LOG_debug << "Link size: " << size;
                   index += (endptr - startsize) + 1;
                   if (url[index] == '!')
                   {
                       const char *typeauth = url + index + 1;
                       index++;

                       const char *ptr = url + index + 1;
                       string auth;
                       auth.assign(ptr, endsize - ptr);
                       if (*typeauth == 'p')
                       {
                           assert(auth.size() == 8);
                           httpctx->nodepubauth = auth;
                           LOG_debug << "Link public auth: " << auth;
                       }
                       else if (*typeauth == 'c')
                       {
                           assert(auth.size() == 8);
                           httpctx->nodechatauth = auth;
                           LOG_debug << "Chat link auth: " << auth;
                       }
                       else if (*typeauth == 'f')
                       {
                           httpctx->nodeprivauth = auth;
                           LOG_debug << "Link private auth: " << auth;
                       }
                       else
                       {
                           LOG_err << "Unknown type of auth token: " << *typeauth;
                       }
                       index += auth.size() + 1;
                   }
               }
           }
        }
    }

    if (length > index && url[index] != '/')
    {
        LOG_warn << "Invalid URL";
        return 0;
    }

    index++;
    if (length > index)
    {
        string nodename(url + index, length - index);

        //get subpath (used in webdav)
        size_t psep = nodename.find("/");
        if (psep != string::npos)
        {
            string subpathrelative = nodename.substr(psep + 1);
            nodename = nodename.substr(0, psep);
            URLCodec::unescape(&subpathrelative, &httpctx->subpathrelative);
            LOG_debug << "subpathrelative: " << httpctx->subpathrelative;
        }

        URLCodec::unescape(&nodename, &httpctx->nodename);
        httpctx->server->fsAccess->normalize(&httpctx->nodename);
        LOG_debug << "Node name: " << httpctx->nodename;
    }

    return 0;
}

int MegaHTTPServer::onHeaderField(http_parser *parser, const char *at, size_t length)
{
    MegaHTTPContext* httpctx = (MegaHTTPContext*) parser->data;
    httpctx->lastheader = string(at, length);
    tolower_string(httpctx->lastheader);

    if (length == 5 && !memcmp(at, "Range", 5))
    {
        httpctx->range = true;
        LOG_debug << "Range header detected";
    }

    return 0;
}

int MegaHTTPServer::onHeaderValue(http_parser *parser, const char *at, size_t length)
{
    MegaHTTPContext* httpctx = (MegaHTTPContext*) parser->data;
    string value(at, length);
    size_t index;
    char *endptr;

    LOG_verbose << " onHeaderValue: " << httpctx->lastheader << " = " << value;
    if (httpctx->lastheader == "depth")
    {
        httpctx->depth = atoi(value.c_str());
    }
    else if (httpctx->lastheader == "host")
    {
        httpctx->host = value;
    }
    else if (httpctx->lastheader == "destination")
    {
        httpctx->destination = value;
    }
    else if (httpctx->lastheader == "overwrite")
    {
        httpctx->overwrite = (value == "T");
    }
    else if (httpctx->range)
    {
        LOG_debug << "Range header value: " << value;
        httpctx->range = false;
        if (length > 7 && !memcmp(at, "bytes=", 6)
                && ((index = value.find_first_of('-')) != string::npos))
        {
            endptr = (char *)value.c_str();
            unsigned long long number = strtoull(value.c_str() + 6, &endptr, 10);
            if (endptr == value.c_str() || *endptr != '-' || number == ULLONG_MAX)
            {
                return 0;
            }

            httpctx->rangeStart = number;
            if (length > (index + 1))
            {
                number = strtoull(value.c_str() + index + 1, &endptr, 10);
                if (endptr == value.c_str() || *endptr != '\0' || number == ULLONG_MAX)
                {
                    return 0;
                }
                httpctx->rangeEnd = number;
            }
            LOG_debug << "Range value parsed: " << httpctx->rangeStart << " - " << httpctx->rangeEnd;
        }
    }
    return 0;
}

int MegaHTTPServer::onBody(http_parser *parser, const char *b, size_t n)
{
    MegaHTTPContext* httpctx = (MegaHTTPContext*) parser->data;

    if (parser->method == HTTP_PUT)
    {
        //create tmp file with contents in messageBody
        if (!httpctx->tmpFileAccess)
        {
            httpctx->tmpFileName=httpctx->server->basePath;
            httpctx->tmpFileName.append("httputfile");
            LocalPath suffix;
            httpctx->server->fsAccess->tmpnamelocal(suffix);
            httpctx->tmpFileName.append(suffix.toPath(*httpctx->server->fsAccess));

            char ext[8];
            LocalPath localpath = LocalPath::fromPath(httpctx->path, *httpctx->server->fsAccess);
            if (httpctx->server->fsAccess->getextension(localpath, ext, sizeof ext))
            {
                httpctx->tmpFileName.append(ext);
            }

            httpctx->tmpFileAccess = httpctx->server->fsAccess->newfileaccess();
            LocalPath localPath = LocalPath::fromPath(httpctx->tmpFileName, *httpctx->server->fsAccess);
            httpctx->server->fsAccess->unlinklocal(localPath);
            if (!httpctx->tmpFileAccess->fopen(localPath, false, true))
            {
                returnHttpCode(httpctx, 500); //is it ok to have a return here (not int onMessageComplete)?
                return 0;
            }
        }

        if (!httpctx->tmpFileAccess->fwrite((const byte*)b, static_cast<unsigned>(n), httpctx->messageBodySize))
        {
            returnHttpCode(httpctx, 500);
            return 0;
        }
        httpctx->messageBodySize += n;
    }
    else
    {
        char *newbody = new char[n + httpctx->messageBodySize];
        memcpy(newbody, httpctx->messageBody, httpctx->messageBodySize);
        memcpy(newbody + httpctx->messageBodySize, b, n);
        httpctx->messageBodySize += n;
        delete [] httpctx->messageBody;
        httpctx->messageBody = newbody;
    }
    return 0;
}

string MegaHTTPServer::getWebDavProfFindNodeContents(MegaNode *node, string baseURL, bool offlineAttribute)
{
    std::ostringstream web;

    web << "<d:response>\r\n"
           "<d:href>" << webdavurlescape(baseURL) << "</d:href>\r\n"
           "<d:propstat>\r\n"
           "<d:status>HTTP/1.1 200 OK</d:status>\r\n"
           "<d:prop>\r\n"
           "<d:displayname>"<< webdavnameescape(node->getName()) << "</d:displayname>\r\n"

           "<d:creationdate>" << rfc1123_datetime(node->getCreationTime()) << "</d:creationdate>"
           "<d:getlastmodified>" << rfc1123_datetime(node->getModificationTime()) << "</d:getlastmodified>"
           ;

    if (offlineAttribute)
    {
        //(perhaps this could be based on number of files / or even better: size)
          web << "<Z:Win32FileAttributes>00001000</Z:Win32FileAttributes> \r\n"; //FILE_ATTRIBUTE_OFFLINE
//        web << "<Z:Win32FileAttributes>00040000</Z:Win32FileAttributes> \r\n"; // FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS (no actual difference)
    }

    if (node->isFolder())
    {
        web << "<d:resourcetype>\r\n"
               "<d:collection />\r\n"
               "</d:resourcetype>\r\n";
    }
    else
    {
        web << "<d:resourcetype />\r\n";
        web << "<d:getcontentlength>" << node->getSize() << "</d:getcontentlength>\r\n";
    }
    web << "</d:prop>\r\n"
           "</d:propstat>\r\n";
    web << "</d:response>\r\n";
    return web.str();
}

string MegaHTTPServer::getWebDavPropFindResponseForNode(string baseURL, string subnodepath, MegaNode *node, MegaHTTPContext* httpctx)
{
    std::ostringstream response;
    std::ostringstream web;

    web << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
           "<d:multistatus xmlns:d=\"DAV:\" xmlns:Z=\"urn:schemas-microsoft-com::\">\r\n";

    string subbaseURL = baseURL + subnodepath;
    if (node->isFolder() && subbaseURL.size() && subbaseURL.at(subbaseURL.size() - 1) != '/')
    {
        subbaseURL.append("/");
    }
    MegaHTTPServer* httpserver = dynamic_cast<MegaHTTPServer *>(httpctx->server);

    web << getWebDavProfFindNodeContents(node, subbaseURL, httpserver->isOfflineAttributeEnabled());
    if (node->isFolder() && (httpctx->depth != 0))
    {
        MegaNodeList *children = httpctx->megaApi->getChildren(node, MegaApi::ORDER_NONE);
        for (int i = 0; i < children->size(); i++)
        {
            MegaNode *child = children->get(i);
            string childURL = subbaseURL + child->getName();
            web << getWebDavProfFindNodeContents(child, childURL, httpserver->isOfflineAttributeEnabled());
        }
        delete children;
    }

    web << "</d:multistatus>"
           "\r\n";

    string sweb = web.str();
    response << "HTTP/1.1 207 Multi-Status\r\n"
                "content-length: " << sweb.size() << "\r\n"
                                                     "content-type: application/xml; charset=utf-8\r\n"
                                                     "server: MEGAsdk\r\n"
                                                     "\r\n";
    response << sweb;
    httpctx->resultCode = API_OK;
    return response.str();
}

string MegaHTTPServer::getResponseForNode(MegaNode *node, MegaHTTPContext* httpctx)
{
    MegaNode *parent = httpctx->megaApi->getParentNode(node);
    MegaNodeList *children = httpctx->megaApi->getChildren(node, MegaApi::ORDER_NONE);
    std::ostringstream response;
    std::ostringstream web;

    // Title
    web << "<title>MEGA</title>";

    // Styles
    web << "<head><meta charset=\"utf-8\" /><style>"
           ".folder {"
           "padding: 0;"
           "width: 24px;"
           "height: 24px;"
           "margin: 0 0 0 -2px;"
           "display: block;"
           "position: absolute;"
           "background-image: url(https://eu.static.mega.co.nz/3/images/mega/nw-fm-sprite_v12.svg);"
           "background-position: -14px -7465px;"
           "background-repeat: no-repeat;}"

           ".file {"
           "padding: 0;"
           "width: 24px;"
           "height: 24px;"
           "margin: 0 0 0 -6px;"
           "display: block;"
           "position: absolute;"
           "background-image: url(https://eu.static.mega.co.nz/3/images/mega/nw-fm-sprite_v12.svg);"
           "background-position: -7px -1494px;"
           "background-repeat: no-repeat;} "

           ".headerimage {"
           "padding: 0 8px 0 46px;"
           "width: 100%;"
           "height: 24px;"
           "margin: 0 0 0 -12px;"
           "display: block;"
           "position: absolute;"
           "background-image: url(https://eu.static.mega.co.nz/3/images/mega/nw-fm-sprite_v12.svg);"
           "background-position: 5px -1000px;"
           "line-height: 23px;"
           "background-repeat: no-repeat;} "

           ".headertext {"
           "line-height: 23px;"
           "color: #777777;"
           "font-size: 18px;"
           "font-weight: bold;"
           "display: block;"
           "position: absolute;"
           "line-height: 23px;}"

           "a {"
           "text-decoration: none; }"

           ".text {"
           "height: 24px;"
           "padding: 0 10px 0 26px;"
           "word-break: break-all;"
           "white-space: pre-wrap;"
           "overflow: hidden;"
           "max-width: 100%;"
           "text-decoration: none;"
           "-moz-box-sizing: border-box;"
           "-webkit-box-sizing: border-box;"
           "box-sizing: border-box;"
           "font-size: 13px;"
           "line-height: 23px;"
           "color: #666666;}"
           "</style></head>";

    // Folder path
    web << "<span class=\"headerimage\"><span class=\"headertext\">";
    char *path = httpctx->megaApi->getNodePath(node);
    if (path)
    {
        web << path;
        delete [] path;
    }
    else
    {
        web << node->getName();
    }
    web << "</span></span><br /><br />";

    // Child nodes
    web << "<table width=\"100%\" border=\"0\" cellspacing=\"0\" cellpadding=\"0\" style=\"width: auto;\">";
    if (parent)
    {
        web << "<tr><td>";
        char *base64Handle = parent->getBase64Handle();
        if (httpctx->megaApi->httpServerGetRestrictedMode() == MegaApi::TCP_SERVER_ALLOW_ALL)
        {
            web << "<a href=\"/" << base64Handle << "/" << parent->getName();
        }
        else
        {
            web << "<a href=\"" << "../" << parent->getName();
        }

        web << "\"><span class=\"folder\"></span><span class=\"text\">..</span></a>";
        delete [] base64Handle;
        delete parent;
        web << "</td></tr>";
    }

    for (int i = 0; i < children->size(); i++)
    {
        web << "<tr><td>";
        MegaNode *child = children->get(i);
        char *base64Handle = child->getBase64Handle();
        if (httpctx->megaApi->httpServerGetRestrictedMode() == MegaApi::TCP_SERVER_ALLOW_ALL)
        {
            web << "<a href=\"/" << base64Handle << "/" << child->getName();
        }
        else
        {
            web << "<a href=\"" << node->getName() << "/" << child->getName();
        }
        web << "\"><span class=\"" << (child->isFile() ? "file" : "folder") << "\"></span><span class=\"text\">"
            << child->getName() << "</span></a>";
        delete [] base64Handle;

        if (!child->isFile())
        {
            web << "</td><td>";
        }
        else
        {
            unsigned const long long KB = 1024;
            unsigned const long long MB = 1024 * KB;
            unsigned const long long GB = 1024 * MB;
            unsigned const long long TB = 1024 * GB;

            web << "</td><td><span class=\"text\">";
            unsigned long long bytes = child->getSize();
            if (bytes > TB)
                web << ((unsigned long long)((100 * bytes) / TB))/100.0 << " TB";
            else if (bytes > GB)
                web << ((unsigned long long)((100 * bytes) / GB))/100.0 << " GB";
            else if (bytes > MB)
                web << ((unsigned long long)((100 * bytes) / MB))/100.0 << " MB";
            else if (bytes > KB)
                web << ((unsigned long long)((100 * bytes) / KB))/100.0 << " KB";
            web << "</span>";
        }
        web << "</td></tr>";
    }
    web << "</table>";
    delete children;

    string sweb = web.str();
    response << "HTTP/1.1 200 OK\r\n"
        << "Content-Type: text/html; charset=utf-8\r\n"
        << "Connection: close\r\n"
        << "Content-Length: " << sweb.size() << "\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "\r\n";

    if (httpctx->parser.method != HTTP_HEAD)
    {
        response << sweb;
    }
    httpctx->resultCode = API_OK;

    return response.str();
}

string MegaHTTPServer::getHTTPMethodName(int httpmethod)
{
    switch (httpmethod)
    {
    case HTTP_DELETE:
        return "HTTP_DELETE";
    case HTTP_GET:
        return "HTTP_GET";
    case HTTP_HEAD:
        return "HTTP_HEAD";
    case HTTP_POST:
        return "HTTP_POST";
    case HTTP_PUT:
        return "HTTP_PUT";
    case HTTP_CONNECT:
        return "HTTP_CONNECT";
    case HTTP_OPTIONS:
        return "HTTP_OPTIONS";
    case HTTP_TRACE:
        return "HTTP_TRACE";
    case HTTP_COPY:
        return "HTTP_COPY";
    case HTTP_LOCK:
        return "HTTP_LOCK";
    case HTTP_MKCOL:
        return "HTTP_MKCOL";
    case HTTP_MOVE:
        return "HTTP_MOVE";
    case HTTP_PROPFIND:
        return "HTTP_PROPFIND";
    case HTTP_PROPPATCH:
        return "HTTP_PROPPATCH";
    case HTTP_SEARCH:
        return "HTTP_SEARCH";
    case HTTP_UNLOCK:
        return "HTTP_UNLOCK";
    case HTTP_BIND:
        return "HTTP_BIND";
    case HTTP_REBIND:
        return "HTTP_REBIND";
    case HTTP_UNBIND:
        return "HTTP_UNBIND";
    case HTTP_ACL:
        return "HTTP_ACL";
    case HTTP_REPORT:
        return "HTTP_REPORT";
    case HTTP_MKACTIVITY:
        return "HTTP_MKACTIVITY";
    case HTTP_CHECKOUT:
        return "HTTP_CHECKOUT";
    case HTTP_MERGE:
        return "HTTP_MERGE";
    case HTTP_MSEARCH:
        return "HTTP_MSEARCH";
    case HTTP_NOTIFY:
        return "HTTP_NOTIFY";
    case HTTP_SUBSCRIBE:
        return "HTTP_SUBSCRIBE";
    case HTTP_UNSUBSCRIBE:
        return "HTTP_UNSUBSCRIBE";
    case HTTP_PATCH:
        return "HTTP_PATCH";
    case HTTP_PURGE:
        return "HTTP_PURGE";
    case HTTP_MKCALENDAR:
        return "HTTP_MKCALENDAR";
    case HTTP_LINK:
        return "HTTP_LINK";
    case HTTP_UNLINK:
        return "HTTP_UNLINK";
    default:
        return "HTTP_UNKOWN";
    }
}

string MegaHTTPServer::getHTTPErrorString(int errorcode)
{
    switch (errorcode)
    {
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 204:
        return "No Content";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 409:
        return "Conflict";
    case 412:
        return "Precondition Failed";
    case 423:
        return "Locked";
    case 500:
        return "Internal Server Error";
    case 502:
        return "Bad Gateway";
    case 503:
        return "Service Unavailable";
    case 507:
        return "Insufficient Storage";
    case 508:
        return "Loop Detected";
    default:
        return "Unknown Error";
    }
}

void MegaHTTPServer::returnHttpCodeBasedOnRequestError(MegaHTTPContext* httpctx, MegaError *e, bool synchronous)
{
    int reqError = e->getErrorCode();
    int httpreturncode = 500;

    switch(reqError)
    {
    case API_EACCESS:
        httpreturncode = 403;
        break;
    case API_EOVERQUOTA:
    case API_EGOINGOVERQUOTA:
        httpreturncode = 507;
        break;
    case API_EAGAIN:
    case API_ERATELIMIT:
    case API_ETEMPUNAVAIL:
        httpreturncode = 503;
        break;
    case API_ECIRCULAR:
        httpreturncode = 508;
        break;
    default:
        httpreturncode = 500;
        break;
    }

    LOG_debug << "HTTP petition failed. request error = " << reqError << " HTTP status to return = " << httpreturncode;
    string errorMessage = e->getErrorString(reqError);
    return returnHttpCode(httpctx, httpreturncode, errorMessage, synchronous);
}

void MegaHTTPServer::returnHttpCode(MegaHTTPContext* httpctx, int errorCode, string errorMessage, bool synchronous)
{
    std::ostringstream response;
    response << "HTTP/1.1 " << errorCode << " "
             << (errorMessage.size() ? errorMessage : getHTTPErrorString(errorCode))
             << "\r\n"
                "Connection: close\r\n"
              << "\r\n";

    httpctx->resultCode = errorCode;
    string resstr = response.str();
    if (synchronous)
    {
        sendHeaders(httpctx, &resstr);
    }
    else
    {
        uv_mutex_lock(&httpctx->mutex_responses);
        httpctx->responses.push_back(resstr);
        uv_mutex_unlock(&httpctx->mutex_responses);
        uv_async_send(&httpctx->asynchandle);
    }
}

void MegaHTTPServer::returnHttpCodeAsyncBasedOnRequestError(MegaHTTPContext* httpctx, MegaError *e)
{
    return returnHttpCodeBasedOnRequestError(httpctx, e, false);
}

void MegaHTTPServer::returnHttpCodeAsync(MegaHTTPContext* httpctx, int errorCode, string errorMessage)
{
    return returnHttpCode(httpctx, errorCode, errorMessage, false);
}

int MegaHTTPServer::onMessageComplete(http_parser *parser)
{
    LOG_debug << "Message complete";
    MegaNode *node = NULL;
    std::ostringstream response;
    MegaHTTPContext* httpctx = (MegaHTTPContext*) parser->data;
    httpctx->bytesWritten = 0;
    httpctx->size = 0;
    httpctx->streamingBuffer.setMaxBufferSize(httpctx->server->getMaxBufferSize());
    httpctx->streamingBuffer.setMaxOutputSize(httpctx->server->getMaxOutputSize());

    MegaHTTPServer* httpserver = dynamic_cast<MegaHTTPServer *>(httpctx->server);

    switch (parser->method)
    {
    case HTTP_GET:
    case HTTP_POST:
    case HTTP_HEAD:
    case HTTP_OPTIONS:
    case HTTP_MOVE:
    case HTTP_PUT:
    case HTTP_DELETE:
    case HTTP_MKCOL:
    case HTTP_COPY:
    case HTTP_LOCK:
    case HTTP_UNLOCK:
    case HTTP_PROPPATCH:
    case HTTP_PROPFIND:
        LOG_debug << "Request method: " << getHTTPMethodName(parser->method);
        break;
    default:
        LOG_debug << "Method not allowed: " << getHTTPMethodName(parser->method);
        response << "HTTP/1.1 405 Method not allowed\r\n"
                    "Connection: close\r\n"
                    "\r\n";

        httpctx->resultCode = 405;
        string resstr = response.str();
        sendHeaders(httpctx, &resstr);
        return 0;
    }

    if (httpctx->path == "/favicon.ico")
    {
        LOG_debug << "Favicon requested";
        response << "HTTP/1.1 301 Moved Permanently\r\n"
                    "Location: https://mega.nz/favicon.ico\r\n"
                    "Connection: close\r\n"
                    "\r\n";

        httpctx->resultCode = API_OK;
        string resstr = response.str();
        sendHeaders(httpctx, &resstr);
        return 0;
    }

    if (httpctx->path == "/")
    {
        node = httpctx->megaApi->getRootNode();
        char *base64Handle = node->getBase64Handle();
        httpctx->nodehandle = base64Handle;
        delete [] base64Handle;
        httpctx->nodename = node->getName();
    }
    else if (httpctx->nodehandle.size())
    {
        node = httpctx->megaApi->getNodeByHandle(MegaApi::base64ToHandle(httpctx->nodehandle.c_str()));
    }

    if (!httpctx->nodehandle.size())
    {
        response << "HTTP/1.1 404 Not Found\r\n"
                    "Connection: close\r\n"
                  << "\r\n";
        httpctx->resultCode = 404;
        string resstr = response.str();
        sendHeaders(httpctx, &resstr);
        delete node;
        return 0;
    }

    handle h = MegaApi::base64ToHandle(httpctx->nodehandle.c_str());
    if (!httpctx->server->isHandleAllowed(h))
    {
        LOG_debug << "Forbidden due to the restricted mode";
        response << "HTTP/1.1 403 Forbidden\r\n"
                    "Connection: close\r\n"
                  << "\r\n";

        httpctx->resultCode = 403;
        string resstr = response.str();
        sendHeaders(httpctx, &resstr);
        delete node;
        return 0;
    }

    if (parser->method == HTTP_OPTIONS)
    {
        LOG_debug << "Returning HTTP_OPTIONS for a " << (httpserver->isHandleWebDavAllowed(h) ? "" : "non ") << "WEBDAV URI";
        response << "HTTP/1.1 200 OK\r\n";

        if (httpserver->isHandleWebDavAllowed(h))
        {
            response << "Allow: GET, POST, HEAD, OPTIONS, PROPFIND, MOVE, PUT, DELETE, MKCOL, COPY, LOCK, UNLOCK, PROPPATCH\r\n"
                        "dav: 1, 2 \r\n"; // 2 requires LOCK to be fully functional
        }
        else
        {
            response << "Allow: GET,POST,HEAD,OPTIONS\r\n";
        }

        response << "content-length: 0\r\n"
        "Connection: close\r\n"
        "\r\n";

        httpctx->resultCode = API_OK;
        string resstr = response.str();
        sendHeaders(httpctx, &resstr);
        delete node;
        return 0;
    }

    //if webdav method, check is handle is a valid webdav
    if ((parser->method != HTTP_GET) && (parser->method != HTTP_POST)
            && (parser->method != HTTP_PUT) && (parser->method != HTTP_HEAD)
            && !httpserver->isHandleWebDavAllowed(h))
    {
        LOG_debug << "Forbidden due to not webdav allowed";
        returnHttpCode(httpctx, 405);
        delete node;
        return 0;
    }

    if (!node)
    {
        if (!httpctx->nodehandle.size() || !httpctx->nodekey.size())
        {
            LOG_warn << "URL not found: " << httpctx->path;
            response << "HTTP/1.1 404 Not Found\r\n"
                        "Connection: close\r\n"
                      << "\r\n";

            httpctx->resultCode = 404;
            string resstr = response.str();
            sendHeaders(httpctx, &resstr);
            return 0;
        }
        else if (httpctx->nodesize >= 0)
        {
            LOG_debug << "Getting foreign node";
            node = httpctx->megaApi->createForeignFileNode(
                        h, httpctx->nodekey.c_str(),
                        httpctx->nodename.c_str(),
                        httpctx->nodesize,
                        -1, UNDEF, httpctx->nodeprivauth.c_str(), httpctx->nodepubauth.c_str(), httpctx->nodechatauth.c_str());
        }
        else
        {
            handle h = MegaApi::base64ToHandle(httpctx->nodehandle.c_str());
            string link = MegaClient::getPublicLink(httpctx->megaApi->getMegaClient()->mNewLinkFormat, nodetype_t::FILENODE, h, httpctx->nodekey.c_str());
            LOG_debug << "Getting public link: " << link;
            httpctx->megaApi->getPublicNode(link.c_str(), httpctx);
            httpctx->transfer.reset(new MegaTransferPrivate(MegaTransfer::TYPE_LOCAL_TCP_DOWNLOAD));
            httpctx->transfer->setPath(httpctx->path.c_str());
            httpctx->transfer->setFileName(httpctx->nodename.c_str());
            httpctx->transfer->setNodeHandle(MegaApi::base64ToHandle(httpctx->nodehandle.c_str()));
            httpctx->transfer->setStartTime(Waiter::ds);
            return 0;
        }
    }

    if (node && httpctx->nodename != node->getName())
    {
        if (parser->method == HTTP_PROPFIND)
        {
            response << "HTTP/1.1 404 Not Found\r\n"
                        "Connection: close\r\n"
                     << "\r\n";

            httpctx->resultCode = 404;
            string resstr = response.str();
            sendHeaders(httpctx, &resstr);
            delete node;
            return 0;
        }
        else
        {
            //Subtitles support
            bool subtitles = false;

            if (httpserver->isSubtitlesSupportEnabled())
            {
                string originalname = node->getName();
                string::size_type dotpos = originalname.find_last_of('.');
                if (dotpos != string::npos)
                {
                    originalname.resize(dotpos);
                }

                if (dotpos == httpctx->nodename.find_last_of('.') && !memcmp(originalname.data(), httpctx->nodename.data(), originalname.size()))
                {
                    LOG_debug << "Possible subtitles file";
                    MegaNode *parent = httpctx->megaApi->getParentNode(node);
                    if (parent)
                    {
                        MegaNode *child = httpctx->megaApi->getChildNode(parent, httpctx->nodename.c_str());
                        if (child)
                        {
                            LOG_debug << "Matching file found: " << httpctx->nodename << " - " << node->getName();
                            subtitles = true;
                            delete node;
                            node = child;
                        }
                        delete parent;
                    }
                }
            }

            if (!subtitles)
            {
                LOG_warn << "Invalid name: " << httpctx->nodename << " - " << node->getName();
                response << "HTTP/1.1 404 Not Found\r\n"
                            "Connection: close\r\n"
                         << "\r\n";

                httpctx->resultCode = 404;
                string resstr = response.str();
                sendHeaders(httpctx, &resstr);
                delete node;
                return 0;
            }
        }
    }

    MegaNode *baseNode = NULL;
    if (httpctx->subpathrelative.size())
    {
        string subnodepath = httpctx->subpathrelative;
        //remove trailing "/"
        size_t seppos = subnodepath.find_last_of("/");
        while ( (seppos != string::npos) && ((seppos + 1) == subnodepath.size()) )
        {
            subnodepath = subnodepath.substr(0,seppos);
            seppos = subnodepath.find_last_of("/");
        }

        MegaNode *subnode = httpctx->megaApi->getNodeByPath(subnodepath.c_str(), node);
        if (parser->method != HTTP_PUT && parser->method != HTTP_MKCOL && !subnode)
        {
            returnHttpCode(httpctx, 404);
            delete node;
            return 0;
        }
        else
        {
            baseNode = node;
            node = subnode;
        }
    }

    if (parser->method == HTTP_PROPFIND)
    {
        string baseURL = string("http") + (httpctx->server->useTLS ? "s" : "") + "://"
                + httpctx->host + "/" + httpctx->nodehandle + "/" + httpctx->nodename + "/";
        string resstr = getWebDavPropFindResponseForNode(baseURL, httpctx->subpathrelative, node, httpctx);
        sendHeaders(httpctx, &resstr);
        delete node;
        delete baseNode;
        return 0;
    }
    else if (parser->method == HTTP_UNLOCK)
    {
        // let's create a minimum unlock compliant response
        returnHttpCode(httpctx, 204);
        delete node;
        delete baseNode;
        return 0;
    }
    else if (parser->method == HTTP_PROPPATCH)
    {
        std::ostringstream web;

//        Typicall Body received: //TODO: actualy update creation and mod times? does not seem to be required (it seems PUT updates them ... with what time: file or PUT time?)
//        <?xml version="1.0" encoding="utf-8" ?>
//        <D:propertyupdate xmlns:D="DAV:" xmlns:Z="urn:schemas-microsoft-com:">
//        <D:set>
//        <D:prop>
//        <Z:Win32CreationTime>Tue, 20 Feb 2018 18:00:20 GMT</Z:Win32CreationTime>
//        <Z:Win32LastAccessTime>Tue, 20 Feb 2018 18:00:21 GMT</Z:Win32LastAccessTime>
//        <Z:Win32LastModifiedTime>Tue, 20 Feb 2018 18:00:21 GMT</Z:Win32LastModifiedTime>
//        <Z:Win32FileAttributes>00000020</Z:Win32FileAttributes>
//        </D:prop>
//        </D:set>
//        </D:propertyupdate>

        web << "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\r\n"
               "<d:multistatus xmlns:d=\"DAV:\">\r\n"
                  "<d:response>\r\n"
                  //"<d:href>" << urlelement << "</d:href>\r\n" //this should come from the input but seems to be not required!
                    "<d:propstat>\r\n"
                      "<d:status>HTTP/1.1 200 OK</d:status>\r\n"
                        "<d:prop>\r\n"
                        // Here we might want to include the updated properties.
                        //we might want to to do so with original namespace?(not sure if that makes sense). e.g:
//                                 "<Z:Win32CreationTime/>\r\n"
                        "</d:prop>\r\n"
                    "</d:propstat>\r\n"
                  "</d:response>\r\n"
                "</d:multistatus>\r\n\r\n";

        string sweb = web.str();

        response << "HTTP/1.1 207 Multi-Status\r\n"
                    "Content-Type: application/xml; charset=\"utf-8\" \r\n"
                    "Content-Length: " << sweb.size() << "\r\n\r\n";

        response << sweb;
        httpctx->resultCode = 207;
        string resstr = response.str();
        sendHeaders(httpctx, &resstr);
        delete node;
        delete baseNode;
        return 0;
    }
    else if (parser->method == HTTP_LOCK)
    {
        std::ostringstream web;

        // let's create a minimum lock compliant response. we should actually provide locking functionality based on URL
        web << "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\r\n"
          "<D:prop xmlns:D=\"DAV:\">\r\n"
            "<D:lockdiscovery>\r\n"
              "<D:activelock>\r\n"
                "<D:locktype><D:write/></D:locktype>\r\n"
                "<D:lockscope><D:exclusive/></D:lockscope>\r\n"
//                "<D:depth>infinity</D:depth>\r\n" // read from req?
                "<D:owner>\r\n"
//                  "<D:href>" << owner << "</D:href>\r\n" // should be read from req body
                "</D:owner>\r\n"
//                "<D:timeout>Second-604800</D:timeout>\r\n"
                "<D:locktoken>\r\n"
                  //"<D:href>urn:uuid:e71d4fae-5dec-22d6-fea5-00a0c91e6be4</D:href>\r\n" //An unique identifier is required
               "<D:href>urn:uuid:this-is-a-fake-lock</D:href>\r\n" //An unique identifier is required
                "</D:locktoken>\r\n"
                "<D:lockroot>\r\n"
//                  "<D:href>" << urlelement << "</D:href>\r\n"  // should be read from req body
                "</D:lockroot>\r\n"
              "</D:activelock>\r\n"
            "</D:lockdiscovery>\r\n"
          "</D:prop>\r\n\r\n";

        string sweb = web.str();
        response << "HTTP/1.1 200 OK\r\n"
          "Lock-Token: <urn:uuid:e71d4fae-5dec-22d6-fea5-00a0c91e6be4> \r\n"
          "Content-Type: application/xml; charset=\"utf-8\" \r\n"
          "Content-Length: " << sweb.size() << "\r\n\r\n";

        response << sweb;
        httpctx->resultCode = 200;
        string resstr = response.str();
        sendHeaders(httpctx, &resstr);
        delete node;
        delete baseNode;
        return 0;
    }
    else if (parser->method == HTTP_DELETE)
    {
        if (!node)
        {
            returnHttpCode(httpctx, 404);
            delete node;
            delete baseNode;
            return 0;
        }

        httpctx->megaApi->remove(node, false, httpctx);
        delete node;
        delete baseNode;
        return 0;
    }
    else if (parser->method == HTTP_MKCOL)
    {
//        201 (Created)	The collection was created.
//        401 (Access Denied)	Resource requires authorization or authorization was denied.
//        403 (Forbidden)	The server does not allow collections to be created at the specified location, or the parent collection of the specified request URI exists but cannot accept members.
//        405 (Method Not Allowed)	The MKCOL method can only be performed on a deleted or non-existent resource.
//        409 (Conflict)	A resource cannot be created at the destination URI until one or more intermediate collections are created.
//        415 (Unsupported Media Type)	The request type of the body is not supported by the server.
//        507 (Insufficient Storage)	The destination resource does not have sufficient storage space.
        if (node)
        {
            returnHttpCode(httpctx, 405);
            delete node;
            delete baseNode;
            return 0;
        }

        MegaNode *newParentNode = NULL;
        string newname;

        string dest = httpctx->subpathrelative;
        size_t seppos = dest.find_last_of("/");

        while ( (seppos != string::npos) && ((seppos + 1) == dest.size()) )
        {
            dest = dest.substr(0,seppos);
            seppos = dest.find_last_of("/");
        }
        if (seppos == string::npos)
        {
            newParentNode = baseNode ? baseNode->copy() : node->copy();
            newname = dest;
        }
        else
        {
            if ((seppos + 1) < dest.size())
            {
                newname = dest.substr(seppos + 1);
            }
            string newparentpath = dest.substr(0, seppos);
            newParentNode = httpctx->megaApi->getNodeByPath(newparentpath.c_str(),baseNode?baseNode:node);
        }
        if (!newParentNode)
        {
            returnHttpCode(httpctx, 409);
            delete node;
            delete baseNode;
            return 0;
        }

        httpctx->megaApi->createFolder(newname.c_str(), newParentNode, httpctx);
        delete newParentNode;
        delete node;
        delete baseNode;
        return 0;
    }
    else if (parser->method == HTTP_COPY)
    {
//        201 (Created)	The resource was successfully copied.
//        204 (No Content)	The source resource was successfully copied to a pre-existing destination resource.
//        403 (Forbidden)	The source URI and the destination URI are the same.
//        409 (Conflict)	A resource cannot be created at the destination URI until one or more intermediate collections are created.
//        412 (Precondition Failed)	Either the Overwrite header is "F" and the state of the destination resource is not null, or the method was used in a Depth: 0 transaction.
//        423 (Locked)	The destination resource is locked.
//        502 (Bad Gateway)	The COPY destination is located on a different server, which refuses to accept the resource.
//        507 (Insufficient Storage)	The destination resource does not have sufficient storage space.

        if (!node)
        {
            returnHttpCode(httpctx, 404);
            delete node;
            delete baseNode;
            return 0;
        }

        MegaNode *newParentNode = NULL;
        string newname;
        string baseURL = string("http") + (httpctx->server->useTLS ? "s" : "") + "://"
                + httpctx->host + "/" + httpctx->nodehandle + "/" + httpctx->nodename + "/";

        string dest;
        URLCodec::unescape(&httpctx->destination, &dest);
        size_t posBase = dest.find(baseURL);
        if (posBase != 0) // Notice that if 2 WEBDAV locations are enabled we won't be able to copy between the 2
        {
            returnHttpCode(httpctx, 502); // The destination URI is located elsewhere
            delete node;
            delete baseNode;
            return 0;
        }

        dest = dest.substr(baseURL.size());
        MegaNode *destNode = httpctx->megaApi->getNodeByPath(dest.c_str(), baseNode ? baseNode : node);
        if (destNode)
        {
            if (node->getHandle() == destNode->getHandle())
            {
                returnHttpCode(httpctx, 403);
                delete node;
                delete baseNode;
                delete destNode;
                return 0;
            }
            else
            {
                //overwrite?
                if (httpctx->overwrite)
                {
                    newParentNode = httpctx->megaApi->getNodeByHandle(destNode->getParentHandle());
                }
                else
                {
                    returnHttpCode(httpctx, 412);
                    delete node;
                    delete baseNode;
                    delete destNode;
                    return 0;
                }
            }
        }

        if (!newParentNode)
        {
            size_t seppos = dest.find_last_of("/");
            while ( (seppos != string::npos) && ((seppos + 1) == dest.size()) )
            {
                dest = dest.substr(0,seppos);
                seppos = dest.find_last_of("/");
            }
            if (seppos == string::npos)
            {
                newParentNode = baseNode?baseNode->copy():node->copy();
                newname = dest;
            }
            else
            {
                if ((seppos + 1) < dest.size())
                {
                    newname = dest.substr(seppos + 1);
                }
                string newparentpath = dest.substr(0, seppos);
                newParentNode = httpctx->megaApi->getNodeByPath(newparentpath.c_str(),baseNode?baseNode:node);
            }
        }
        if (!newParentNode)
        {
            returnHttpCode(httpctx, 409);
            delete node;
            delete baseNode;
            return 0;
        }
        if (newname.size())
        {
            httpctx->megaApi->copyNode(node, newParentNode, newname.c_str(), httpctx);
        }
        else
        {
            httpctx->megaApi->copyNode(node, newParentNode, httpctx);
        }

        delete node;
        delete baseNode;
        delete newParentNode;
        return 0;
    }
    else if (parser->method == HTTP_PUT)
    {
        if (node && !httpctx->overwrite)
        {
            returnHttpCode(httpctx, 412);
            delete node;
            delete baseNode;
            return 0;
        }
        else
        {
            MegaNode *newParentNode = NULL;
            string newname;

            string dest = httpctx->subpathrelative;
            size_t seppos = dest.find_last_of("/");
            while ( (seppos != string::npos) && ((seppos + 1) == dest.size()) )
            {
                dest = dest.substr(0,seppos);
                seppos = dest.find_last_of("/");
            }
            if (seppos == string::npos)
            {
                newParentNode = baseNode ? baseNode->copy() : node->copy();
                newname = dest;
            }
            else
            {
                if ((seppos + 1) < dest.size())
                {
                    newname = dest.substr(seppos + 1);
                }
                string newparentpath = dest.substr(0, seppos);
                newParentNode = httpctx->megaApi->getNodeByPath(newparentpath.c_str(), baseNode ? baseNode : node);
            }

            if (!newParentNode)
            {
                returnHttpCode(httpctx, 409);
                delete node;
                delete baseNode;
                return 0;
            }

            if (!httpctx->tmpFileAccess) //put with no body contents
            {
                httpctx->tmpFileName=httpctx->server->basePath;
                httpctx->tmpFileName.append("httputfile");
                httpctx->tmpFileName.append(LocalPath::tmpNameLocal(*httpctx->server->fsAccess).toPath(*httpctx->server->fsAccess));
                char ext[8];
                if (httpctx->server->fsAccess->getextension(LocalPath::fromPath(httpctx->path, *httpctx->server->fsAccess), ext, sizeof ext))
                {
                    httpctx->tmpFileName.append(ext);
                }
                httpctx->tmpFileAccess = httpctx->server->fsAccess->newfileaccess();
                auto tmpFileNamePath = LocalPath::fromPath(httpctx->tmpFileName, *httpctx->server->fsAccess);
                httpctx->server->fsAccess->unlinklocal(tmpFileNamePath);
                if (!httpctx->tmpFileAccess->fopen(tmpFileNamePath, false, true))
                {
                    returnHttpCode(httpctx, 500);
                    delete node;
                    delete baseNode;
                    delete newParentNode;
                    return 0;
                }
            }

            FileSystemType fsType = httpctx->server->fsAccess->getFilesystemType(LocalPath::fromPath(httpctx->tmpFileName, *httpctx->server->fsAccess));

            httpctx->megaApi->startUpload(httpctx->tmpFileName.c_str(), newParentNode, newname.c_str(), fsType, httpctx);

            delete node;
            delete baseNode;
            delete newParentNode;
            return 0;
        }
    }
    else if (parser->method == HTTP_MOVE)
    {
        //        201 (Created)	The resource was moved successfully and a new resource was created at the specified destination URI.
        //        204 (No Content)	The resource was moved successfully to a pre-existing destination URI.
        //        403 (Forbidden)	The source URI and the destination URI are the same.
        //        409 (Conflict)	A resource cannot be created at the destination URI until one or more intermediate collections are created.
        //        412 (Precondition Failed)	Either the Overwrite header is "F" and the state of the destination resource is not null, or the method was used in a Depth: 0 transaction.
        //        423 (Locked)	The destination resource is locked.
        //        502 (Bad Gateway)	The destination URI is located on a different server, which refuses to accept the resource.

        if (!node)
        {
            returnHttpCode(httpctx, 404);
            delete node;
            delete baseNode;
            return 0;
        }

        string baseURL = string("http") + (httpctx->server->useTLS ? "s" : "") + "://" + httpctx->host
                + "/" + httpctx->nodehandle + "/" + httpctx->nodename + "/";

        string dest;
        URLCodec::unescape(&httpctx->destination, &dest);
        size_t posBase = dest.find(baseURL);
        if (posBase != 0) // Notice that if 2 WEBDAV locations are enabled we won't be able to copy between the 2
        {
            returnHttpCode(httpctx, 502); // The destination URI is located elsewhere
            delete node;
            delete baseNode;
            return 0;
        }
        dest = dest.substr(baseURL.size());
        MegaNode *destNode = httpctx->megaApi->getNodeByPath(dest.c_str(), baseNode ? baseNode : node);
        if (destNode)
        {
            if (node->getHandle() == destNode->getHandle())
            {
                returnHttpCode(httpctx, 403);
                delete node;
                delete baseNode;
                delete destNode;
                return 0;
            }
            else
            {
                //overwrite?
                if (httpctx->overwrite)
                {
                    httpctx->newParentNode = destNode->getParentHandle();
                    httpctx->newname = destNode->getName();
                    httpctx->nodeToMove = node->getHandle();
                    httpctx->megaApi->remove(destNode, false, httpctx);
                    delete node;
                    delete baseNode;
                    delete destNode;
                    return 0;
                }
                else
                {
                    returnHttpCode(httpctx, 412);
                    delete node;
                    delete baseNode;
                    delete destNode;
                    return 0;
                }
            }
        }
        else
        {
            MegaNode *newParentNode = NULL;
            size_t seppos = dest.find_last_of("/");
            httpctx->newname = dest;
            if (seppos == string::npos)
            {
                newParentNode = baseNode ? baseNode->copy() : node->copy();
            }
            else
            {
                if ((seppos + 1) < httpctx->newname.size())
                {
                    httpctx->newname = httpctx->newname.substr(seppos + 1);
                }
                string newparentpath = dest.substr(0, seppos);
                newParentNode = httpctx->megaApi->getNodeByPath(newparentpath.c_str(), baseNode ? baseNode : node);
            }
            if (!newParentNode)
            {
                returnHttpCode(httpctx, 409);
                delete node;
                delete baseNode;
                return 0;
            }

            if (newParentNode->getHandle() == node->getHandle())
            {
                LOG_warn << "HTTP_MOVE trying to mov a node into itself";
                returnHttpCode(httpctx, 500);
                delete node;
                delete baseNode;
                delete newParentNode;
                return 0;
            }

            if (newParentNode->getHandle() != node->getParentHandle())
            {
                httpctx->megaApi->moveNode(node, newParentNode, httpctx);
            }
            else
            {
                httpctx->megaApi->renameNode(node, httpctx->newname.c_str(), httpctx);
            }
            delete newParentNode;
        }

        delete node;
        delete baseNode;
        return 0;
    }
    else //GET/POST/HEAD
    {
        if (node->isFolder())
        {
            if (!httpserver->isFolderServerEnabled())
            {
                response << "HTTP/1.1 403 Forbidden\r\n"
                            "Connection: close\r\n"
                          << "\r\n";

                httpctx->resultCode = 403;
                string resstr = response.str();
                sendHeaders(httpctx, &resstr);
                delete node;
                delete baseNode;
                return 0;
            }

            string resstr = getResponseForNode(node, httpctx);

            sendHeaders(httpctx, &resstr);
            delete node;
            delete baseNode;
            return 0;
        }

        //File node
        if (!httpserver->isFileServerEnabled())
        {
            response << "HTTP/1.1 403 Forbidden\r\n"
                        "Connection: close\r\n"
                      << "\r\n";

            httpctx->resultCode = 403;
            string resstr = response.str();
            sendHeaders(httpctx, &resstr);
            delete node;
            delete baseNode;
            return 0;
        }

        httpctx->transfer.reset(new MegaTransferPrivate(MegaTransfer::TYPE_LOCAL_TCP_DOWNLOAD));
        httpctx->transfer->setPath(httpctx->path.c_str());
        if (httpctx->nodename.size())
        {
            httpctx->transfer->setFileName(httpctx->nodename.c_str());
        }
        if (httpctx->nodehandle.size())
        {
            httpctx->transfer->setNodeHandle(MegaApi::base64ToHandle(httpctx->nodehandle.c_str()));
        }
        httpctx->transfer->setStartTime(Waiter::ds);

        delete httpctx->node;
        httpctx->node = node;
        streamNode(httpctx);
    }
    delete baseNode;
    return 0;
}

int MegaHTTPServer::streamNode(MegaHTTPContext *httpctx)
{
    std::ostringstream response;
    MegaNode *node = httpctx->node;

    string name;
    const char *extension = NULL;
    const char *nodeName = httpctx->node->getName();
    if (nodeName)
    {
        name = nodeName;
    }

    string::size_type dotindex = name.find_last_of('.');
    if (dotindex != string::npos)
    {
        extension = name.c_str() + dotindex;
    }

    char *mimeType = MegaApi::getMimeType(extension);
    if (!mimeType)
    {
        mimeType = MegaApi::strdup("application/octet-stream");
    }

    m_off_t totalSize = node->getSize();
    m_off_t start = 0;
    m_off_t end = totalSize - 1;
    if (httpctx->rangeStart >= 0)
    {
        start = httpctx->rangeStart;
    }
    httpctx->rangeStart = start;

    if (httpctx->rangeEnd >= 0)
    {
        end = std::min(totalSize - 1, httpctx->rangeEnd);
    }
    httpctx->rangeEnd = end + 1;

    bool rangeRequested = (httpctx->rangeEnd - httpctx->rangeStart) != totalSize;

    m_off_t len = end - start + 1;
    if (totalSize && (start < 0 || start >= totalSize || end < 0 || end >= totalSize || len <= 0 || len > totalSize) )
    {
        response << "HTTP/1.1 416 Requested Range Not Satisfiable\r\n"
            << "Content-Type: " << mimeType << "\r\n"
            << "Connection: close\r\n"
            << "Access-Control-Allow-Origin: *\r\n"
            << "Accept-Ranges: bytes\r\n"
            << "Content-Range: bytes 0-0/" << totalSize << "\r\n"
            << "\r\n";

        delete [] mimeType;
        httpctx->resultCode = 416;
        string resstr = response.str();
        sendHeaders(httpctx, &resstr);
        return 0;
    }

    if (rangeRequested)
    {
        response << "HTTP/1.1 206 Partial Content\r\n";
        response << "Content-Range: bytes " << start << "-" << end << "/" << totalSize << "\r\n";
    }
    else
    {
        response << "HTTP/1.1 200 OK\r\n";
    }

    response << "Content-Type: " << mimeType << "\r\n"
        << "Connection: close\r\n"
        << "Content-Length: " << len << "\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Accept-Ranges: bytes\r\n"
        << "\r\n";

    delete [] mimeType;
    httpctx->pause = false;
    httpctx->lastBuffer = NULL;
    httpctx->lastBufferLen = 0;
    if (httpctx->transfer)
    {
        httpctx->transfer->setStartPos(start);
        httpctx->transfer->setEndPos(end);
    }

    string resstr = response.str();
    if (httpctx->parser.method != HTTP_HEAD)
    {
        httpctx->streamingBuffer.init(len + resstr.size());
        httpctx->size = len;
    }

    sendHeaders(httpctx, &resstr);
    if (httpctx->parser.method == HTTP_HEAD)
    {
        return 0;
    }

    LOG_debug << "Requesting range. From " << start << "  size " << len;
    httpctx->rangeWritten = 0;
    if (start || len)
    {
        httpctx->megaApi->startStreaming(node, start, len, httpctx);
    }
    else
    {
        MegaHTTPServer *httpserver = ((MegaHTTPServer *)httpctx->server);
        LOG_debug << "Skipping startStreaming call since empty file";
        httpserver->processWriteFinished(httpctx, 0);
    }
    return 0;
}

void MegaHTTPServer::sendHeaders(MegaHTTPContext *httpctx, string *headers)
{
    LOG_debug << "Response headers: " << *headers;
    httpctx->streamingBuffer.append(headers->data(), static_cast<unsigned>(headers->size()));
    uv_buf_t resbuf = httpctx->streamingBuffer.nextBuffer();
    httpctx->size += headers->size();
    httpctx->lastBuffer = resbuf.base;
    httpctx->lastBufferLen = resbuf.len;

    if (httpctx->transfer)
    {
        httpctx->transfer->setTotalBytes(httpctx->size);
        httpctx->megaApi->fireOnStreamingStart(httpctx->transfer.get());
    }

#ifdef ENABLE_EVT_TLS
    if (httpctx->server->useTLS)
    {
        assert (resbuf.len);
        int err = evt_tls_write(httpctx->evt_tls, resbuf.base, resbuf.len, onWriteFinished_tls);
        if (err <= 0)
        {
            LOG_warn << "Finishing due to an error sending the response: " << err;
            closeConnection(httpctx);
        }
    }
    else
    {
#endif
        uv_write_t *req = new uv_write_t();
        req->data = httpctx;
        if (int err = uv_write(req, (uv_stream_t*)&httpctx->tcphandle, &resbuf, 1, onWriteFinished))
        {
            delete req;
            LOG_warn << "Finishing due to an error sending the response: " << err;
            closeTCPConnection(httpctx);
        }
#ifdef ENABLE_EVT_TLS
    }
#endif
}

void MegaHTTPServer::processAsyncEvent(MegaTCPContext* tcpctx)
{
    MegaHTTPContext* httpctx = dynamic_cast<MegaHTTPContext *>(tcpctx);

    if (httpctx->finished)
    {
        LOG_debug << "HTTP link closed, ignoring async event";
        return;
    }

    if (httpctx->failed)
    {
        LOG_warn << "Streaming transfer failed. Closing connection.";
        closeConnection(httpctx);
        return;
    }

    uv_mutex_lock(&httpctx->mutex_responses);
    while (httpctx->responses.size())
    {
        sendHeaders(httpctx,&httpctx->responses.front());
        httpctx->responses.pop_front();
    }
    uv_mutex_unlock(&httpctx->mutex_responses);

    if (httpctx->nodereceived)
    {
        httpctx->nodereceived = false;
        if (!httpctx->node || httpctx->nodename != httpctx->node->getName())
        {
            if (!httpctx->node)
            {
                LOG_warn << "Public link not found";
            }
            else
            {
                LOG_warn << "Invalid name for public link";
            }

            httpctx->resultCode = 404;
            string resstr = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";
            sendHeaders(httpctx, &resstr);
            return;
        }

        streamNode(httpctx);
        return;
    }

    sendNextBytes(httpctx);
}

void MegaHTTPServer::sendNextBytes(MegaHTTPContext *httpctx)
{
    if (httpctx->finished)
    {
        LOG_debug << "HTTP link closed, aborting write";
        return;
    }

    if (httpctx->lastBuffer)
    {
        LOG_verbose << "Skipping write due to another ongoing write";
        return;
    }

    uv_mutex_lock(&httpctx->mutex);
    if (httpctx->lastBufferLen)
    {
        httpctx->streamingBuffer.freeData(httpctx->lastBufferLen);
        httpctx->lastBufferLen = 0;
    }

    if (httpctx->tcphandle.write_queue_size > httpctx->streamingBuffer.availableCapacity() / 8)
    {
        LOG_warn << "Skipping write. Too much queued data";
        uv_mutex_unlock(&httpctx->mutex);
        return;
    }

    uv_buf_t resbuf = httpctx->streamingBuffer.nextBuffer();
    uv_mutex_unlock(&httpctx->mutex);

    if (!resbuf.len)
    {
        LOG_verbose << "Skipping write. No data available";
        return;
    }

    LOG_verbose << "Writing " << resbuf.len << " bytes";
    httpctx->rangeWritten += resbuf.len;
    httpctx->lastBuffer = resbuf.base;
    httpctx->lastBufferLen = resbuf.len;

#ifdef ENABLE_EVT_TLS
    if (httpctx->server->useTLS)
    {
        //notice this, contrary to !useTLS is synchronous
        int err = evt_tls_write(httpctx->evt_tls, resbuf.base, resbuf.len, onWriteFinished_tls);
        if (err <= 0)
        {
            LOG_warn << "Finishing due to an error sending the response: " << err;
            evt_tls_close(httpctx->evt_tls, on_evt_tls_close);
        }
    }
    else
    {
#endif
        uv_write_t *req = new uv_write_t();
        req->data = httpctx;

        if (int err = uv_write(req, (uv_stream_t*)&httpctx->tcphandle, &resbuf, 1, onWriteFinished))
        {
            delete req;
            LOG_warn << "Finishing due to an error in uv_write: " << err;
            httpctx->finished = true;
            if (!uv_is_closing((uv_handle_t*)&httpctx->tcphandle))
            {
                uv_close((uv_handle_t*)&httpctx->tcphandle, onClose);
            }
        }
#ifdef ENABLE_EVT_TLS
    }
#endif
}

MegaHTTPContext::MegaHTTPContext()
{
    rangeStart = -1;
    rangeEnd = -1;
    rangeWritten = -1;
    range = false;
    failed = false;
    pause = false;
    nodereceived = false;
    resultCode = API_EINTERNAL;
    node = NULL;
    nodesize = -1;
    messageBody = NULL;
    messageBodySize = 0;
    tmpFileAccess = NULL;
    newParentNode = UNDEF;
    nodeToMove = UNDEF;
    depth = -1;
    overwrite = true; //GVFS-DAV via command line does not include this header (assumed true)
    lastBuffer = NULL;
    lastBufferLen = 0;

    // Mutex to protect the data buffer
    uv_mutex_init(&mutex_responses);
}

MegaHTTPContext::~MegaHTTPContext()
{
    delete node;
    if (tmpFileName.size())
    {
        LocalPath localPath = LocalPath::fromPath(tmpFileName, *server->fsAccess);
        server->fsAccess->unlinklocal(localPath);
    }
    delete [] messageBody;
    uv_mutex_destroy(&mutex_responses);
}

void MegaHTTPContext::onTransferStart(MegaApi *, MegaTransfer *transfer)
{
    if (this->transfer)
    {
        this->transfer->setTag(transfer->getTag());
    }
}

bool MegaHTTPContext::onTransferData(MegaApi *, MegaTransfer *transfer, char *buffer, size_t size)
{
    LOG_verbose << "Streaming data received: " << transfer->getTransferredBytes()
                << " Size: " << size
                << " Queued: " << this->tcphandle.write_queue_size
                << " Buffered: " << streamingBuffer.availableData()
                << " Free: " << streamingBuffer.availableSpace();

    if (finished)
    {
        LOG_info << "Removing streaming transfer after " << transfer->getTransferredBytes() << " bytes";
        return false;
    }

    // append the data to the buffer
    uv_mutex_lock(&mutex);
    long long remaining = size + (transfer->getTotalBytes() - transfer->getTransferredBytes());
    long long availableSpace = streamingBuffer.availableSpace();
    if (remaining > availableSpace && availableSpace < (2 * m_off_t(size)))
    {
        LOG_debug << "Buffer full: " << availableSpace << " of "
                 << streamingBuffer.availableCapacity() << " bytes available only. Pausing streaming";
        pause = true;
    }
    streamingBuffer.append(buffer, static_cast<unsigned>(size));
    uv_mutex_unlock(&mutex);

    // notify the HTTP server
    uv_async_send(&asynchandle);
    return !pause;
}

void MegaHTTPContext::onTransferFinish(MegaApi *, MegaTransfer *, MegaError *e)
{
    if (finished)
    {
        LOG_debug << "HTTP link closed, ignoring the result of the transfer";
        return;
    }

    MegaHTTPServer* httpserver = dynamic_cast<MegaHTTPServer *>(server);

    int ecode = e->getErrorCode();

    if (parser.method == HTTP_PUT)
    {
        if (ecode == API_OK)
        {
            httpserver->returnHttpCodeAsync(this, 201); //TODO actually if resource already existed this should be 200
        }
        else
        {
            httpserver->returnHttpCodeAsyncBasedOnRequestError(this, e);
        }
    }

    if (ecode != API_OK && ecode != API_EINCOMPLETE)
    {
        LOG_warn << "Transfer failed with error code: " << ecode;
        failed = true;
    }
    uv_async_send(&asynchandle);
}

void MegaHTTPContext::onRequestFinish(MegaApi *, MegaRequest *request, MegaError *e)
{
    if (finished)
    {
        LOG_debug << "HTTP link closed, ignoring the result of the request";
        return;
    }
    MegaHTTPServer* httpserver = dynamic_cast<MegaHTTPServer *>(server);

    if (request->getType() == MegaRequest::TYPE_MOVE)
    {
        if (e->getErrorCode() == MegaError::API_OK)
        {
            if (this->newname.size())
            {
                MegaNode *nodetoRename = this->megaApi->getNodeByHandle(request->getNodeHandle());
                if (!nodetoRename || !strcmp(nodetoRename->getName(), newname.c_str()))
                {
                    httpserver->returnHttpCodeAsync(this, 204);
                }
                else
                {
                    this->megaApi->renameNode(nodetoRename, newname.c_str(), this);
                }
                delete nodetoRename;
            }
            else
            {
                httpserver->returnHttpCodeAsync(this, 204);
            }
        }
        else
        {
            httpserver->returnHttpCodeAsyncBasedOnRequestError(this, e);
        }
    }
    else if (request->getType() == MegaRequest::TYPE_RENAME)
    {
        if (e->getErrorCode() == MegaError::API_OK )
        {
            httpserver->returnHttpCodeAsync(this, 204);
        }
        else
        {
            httpserver->returnHttpCodeAsyncBasedOnRequestError(this, e);
        }
    }
    else if (request->getType() == MegaRequest::TYPE_REMOVE)
    {
        if (e->getErrorCode() == MegaError::API_OK)
        {
            MegaNode *n = this->megaApi->getNodeByHandle(nodeToMove);
            MegaNode *p = this->megaApi->getNodeByHandle(newParentNode);
            if (n && p) //delete + move
            {
                this->megaApi->moveNode(n, p, this);
            }
            else
            {
                httpserver->returnHttpCodeAsync(this, 204); // Standard success response
            }

            nodeToMove = UNDEF;
            newParentNode = UNDEF;
            delete n;
            delete p;
        }
        else
        {
            httpserver->returnHttpCodeAsyncBasedOnRequestError(this, e);
        }
    }
    else if (request->getType() == MegaRequest::TYPE_CREATE_FOLDER)
    {
        if (e->getErrorCode() == MegaError::API_OK)
        {
            httpserver->returnHttpCodeAsync(this, 201);
        }
        else
        {
            httpserver->returnHttpCodeAsyncBasedOnRequestError(this, e);
        }
    }
    else if (request->getType() == MegaRequest::TYPE_COPY)
    {
        if (e->getErrorCode() == MegaError::API_OK)
        {
            httpserver->returnHttpCodeAsync(this, 201);
        }
        else
        {
            httpserver->returnHttpCodeAsyncBasedOnRequestError(this, e);
        }
    }
    else if (request->getType() == MegaRequest::TYPE_GET_PUBLIC_NODE)
    {
        node = request->getPublicMegaNode();
        nodereceived = true;
    }
    uv_async_send(&asynchandle);
}


//////////////////////////////
//  MegaFTPServer specifics //
//////////////////////////////

/**
 * Gets permissions string: e.g: 777 -> rwxrwxrwx
 * @param permissions permissions
 * @param permsString permission string buffer
 */
void MegaFTPServer::getPermissionsString(int permissions, char *permsString)
{
    string ps = "";
    for(int i = 0; i<3; i++) // user, group, others
    {
        int curperm = permissions%10;
        permissions = permissions / 10;

        bool read = (curperm >> 2) & 0x1;
        bool write = (curperm >> 1) & 0x1;
        bool exec = (curperm >> 0) & 0x1;

        char rwx[4];
        sprintf(rwx,"%c%c%c",read?'r':'-' ,write?'w':'-', exec?'x':'-');
        rwx[3]='\0';
        ps = rwx + ps;
    }
    strcat(permsString, ps.c_str());
}

//ftp_parser_settings MegaTCPServer::parsercfg;
MegaFTPServer::MegaFTPServer(MegaApiImpl *megaApi, string basePath, int dataportBegin, int dataPortEnd, bool useTLS, string certificatepath, string keypath)
: MegaTCPServer(megaApi, basePath, useTLS, certificatepath, keypath)
{
    nodeHandleToRename = UNDEF;
    this->pport = dataportBegin;
    this->dataportBegin = dataportBegin;
    this->dataPortEnd = dataPortEnd;

    crlfout = "\r\n";
}

MegaFTPServer::~MegaFTPServer()
{
    // if not stopped, the uv thread might want to access a pointer to this.
    // though this is done in the parent destructor, it could try to access it after vtable has been erased
    stop();
}

MegaTCPContext* MegaFTPServer::initializeContext(uv_stream_t *server_handle)
{
    MegaFTPContext* ftpctx = new MegaFTPContext();

    // Set connection data
    MegaFTPServer *server = (MegaFTPServer *)(server_handle->data);
    ftpctx->server = server;
    ftpctx->megaApi = server->megaApi;
    ftpctx->tcphandle.data = ftpctx;
    ftpctx->asynchandle.data = ftpctx;

    return ftpctx;
}

void MegaFTPServer::processWriteFinished(MegaTCPContext *tcpctx, int status)
{
    LOG_verbose << "MegaFTPServer::processWriteFinished. status=" << status;
}

string MegaFTPServer::getListingLineFromNode(MegaNode *child, string nameToShow)
{
    char perms[10];
    memset(perms,0,10);
    //str_perm((statbuf.st_mode & ALLPERMS), perms);
    getPermissionsString(child->isFolder() ? 777 : 664, perms);

    char timebuff[80];
    time_t rawtime = (child->isFolder()?child->getCreationTime():child->getModificationTime());
    struct tm time;
    m_localtime(rawtime, &time);
    strftime(timebuff,80,"%b %d %H:%M",&time);

    char toprint[3000];
    sprintf(toprint,
            "%c%s %5d %4d %4d %8"
            PRId64
            " %s %s",
            (child->isFolder())?'d':'-',
            perms,
            1,//number of contents for folders
            1000, //uid
            1000, //gid
             (child->isFolder())?4:child->getSize(),
            timebuff,
            nameToShow.size()?nameToShow.c_str():child->getName());

    return toprint;
}


string MegaFTPServer::getFTPErrorString(int errorcode, string argument)
{
    switch (errorcode)
    {
    case 110:
        return "Restart marker reply.";
    case 120:
        return "Service ready in " + argument + " minutes.";
    case 125:
        return "Data connection already open; transfer starting.";
    case 150:
        return "File status okay; about to open data connection.";
    case 200:
        return "Command okay.";
    case 202:
        return "Command not implemented, superfluous at this site.";
    case 211:
        return "System status, or system help reply.";
    case 212:
        return "Directory status.";
    case 213:
        return "File status.";
    case 214:
        return "Help message.";
    case 215:
        return "NAME system type.";
    case 220:
        return "Service ready for new user.";
    case 221:
        return "Service closing control connection.";
    case 225:
        return "Data connection open; no transfer in progress.";
    case 226:
        return "Closing data connection. Requested file action successful.";
    case 227:
        return "Entering Passive Mode (h1,h2,h3,h4,p1,p2).";
    case 230:
        return "User logged in, proceed.";
    case 250:
        return "Requested file action okay, completed.";
    case 257:
        return argument + " created.";
    case 331:
        return "User name okay, need password.";
    case 332:
        return "Need account for login.";
    case 350:
        return "Requested file action pending further information.";
    case 421:
        return "Service not available, closing control connection.";
    case 425:
        return "Can't open data connection.";
    case 426:
        return "Connection closed; transfer aborted.";
    case 450:
        return "Requested file action not taken. File unavailable (e.g., file busy).";
    case 451:
        return "Requested action aborted: local error in processing.";
    case 452:
        return "Requested action not taken. Insufficient storage space in system.";
    case 500:
        return "Syntax error, command unrecognized.";
    case 501:
        return "Syntax error in parameters or arguments.";
    case 502:
        return "Command not implemented.";
    case 503:
        return "Bad sequence of commands.";
    case 504:
        return "Command not implemented for that parameter.";
    case 530:
        return "Not logged in.";
    case 532:
        return "Need account for storing files.";
    case 550:
        return "Requested action not taken. File unavailable (e.g., file not found, no access).";
    case 551:
        return "Requested action aborted: page type unknown.";
    case 552:
        return "Requested file action aborted. Exceeded storage allocation."; // (for current directory or dataset).
    case 553:
        return "Requested action not taken. File name not allowed.";
    default:
        return "Unknown Error";
    }
}

void MegaFTPServer::returnFtpCodeBasedOnRequestError(MegaFTPContext* ftpctx, MegaError *e)
{
    int reqError = e->getErrorCode();
    int ftpreturncode = 500;

    switch(reqError)
    {
    case API_OK:
        ftpreturncode = 300;
        break;
    case API_EACCESS:
        ftpreturncode = 550; //this might not be accurate
        break;
    case API_EOVERQUOTA:
    case API_EGOINGOVERQUOTA:
        ftpreturncode = 452; // 552?
        break;
    case API_EAGAIN:
    case API_ERATELIMIT:
    case API_ETEMPUNAVAIL:
        ftpreturncode = 120;
        break;
    case API_EREAD:
        ftpreturncode = 450;
        break;
    case API_ECIRCULAR:
        ftpreturncode = 508;
        break;
    default:
        ftpreturncode = 503;
        break;
    }

    LOG_debug << "FTP petition failed. request error = " << reqError << " FTP status to return = " << ftpreturncode;
    string errorMessage = e->getErrorString(reqError);
    return returnFtpCode(ftpctx, ftpreturncode, errorMessage);
}

void MegaFTPServer::returnFtpCode(MegaFTPContext* ftpctx, int errorCode, string errorMessage)
{
    MegaFTPServer* ftpserver = dynamic_cast<MegaFTPServer *>(ftpctx->server);

    std::ostringstream response;
    response << errorCode << " " << (errorMessage.size() ? errorMessage : getFTPErrorString(errorCode))
             << ftpserver->crlfout;

    string resstr = response.str();
    uv_mutex_lock(&ftpctx->mutex_responses);
    ftpctx->responses.push_back(resstr);
    uv_mutex_unlock(&ftpctx->mutex_responses);
    uv_async_send(&ftpctx->asynchandle);
}

void MegaFTPServer::returnFtpCodeAsyncBasedOnRequestError(MegaFTPContext* ftpctx, MegaError *e)
{
    return returnFtpCodeBasedOnRequestError(ftpctx, e);
}

void MegaFTPServer::returnFtpCodeAsync(MegaFTPContext* ftpctx, int errorCode, string errorMessage)
{
    return returnFtpCode(ftpctx, errorCode, errorMessage);
}

// you get the ownership
MegaNode *MegaFTPServer::getBaseFolderNode(string path)
{
    if (path.size() && path.at(0) == '/')
    {
        string rest = path.substr(1);
        size_t possep = rest.find('/');
        handle h = megaApi->base64ToHandle(rest.substr(0,possep).c_str());
        MegaNode *n = megaApi->getNodeByHandle(h);
        if (possep != string::npos && possep != (rest.size() - 1) )
        {
            if (n)
            {
                if (rest.size() > (possep + 1))
                {
                    rest = rest.substr(possep + 1);
                    if (rest == n->getName())
                    {
                        return n;
                    }
                    if (rest.size() > strlen(n->getName()) && (rest.at(strlen(n->getName())) == '/' ) && (rest.find(n->getName()) == 0) )
                    {
                        return n;
                    }
                }
                delete n;
            }
        }
        else
        {
            return n;
        }
    }
    return NULL;
}

// you get the ownership
MegaNode *MegaFTPServer::getNodeByFullFtpPath(string path)
{
    if (path.size() && path.at(0) == '/')
    {
        string rest = path.substr(1);
        size_t possep = rest.find('/');
        handle h = megaApi->base64ToHandle(rest.substr(0,possep).c_str());
        MegaNode *n = megaApi->getNodeByHandle(h);
        if (possep != string::npos && possep != (rest.size() - 1) )
        {
            if (n)
            {
                if (rest.size() > possep)
                {
                    rest = rest.substr(possep + 1);
                    if (rest == n->getName())
                    {
                        return n;
                    }
                    if (rest.size() > strlen(n->getName()) && (rest.at(strlen(n->getName())) == '/' ) && (rest.find(n->getName()) == 0) )
                    {
                        string relpath = rest.substr(strlen(n->getName())+1);
                        MegaNode *toret = megaApi->getNodeByPath(relpath.c_str(), n);
                        delete n;
                        return toret;
                    }
                }
                delete n;
            }
        }
        else
        {
            return n;
        }
    }
    return NULL;
}

MegaNode * MegaFTPServer::getNodeByFtpPath(MegaFTPContext* ftpctx, string path)
{
    if (ftpctx->atroot && path.size() && path.at(0) != '/')
    {
        path= "/" + path;
    }
    else if (ftpctx->athandle && path.size() && path.at(0) != '/')
    {
        char *cshandle = ftpctx->megaApi->handleToBase64(ftpctx->cwd);
        string handle(cshandle);
        delete []cshandle;

        path= "/" + handle + "/" + path;
    }
    else if (path.size() && path.at(0) != '/')
    {
        path = ftpctx->cwdpath + "/" + path;
        path = shortenpath(path);
    }

    if (path.find("..") == 0)
    {
        string fullpath = ftpctx->cwdpath + "/" + path;
        size_t seppos = fullpath.find("/");
        int count = 0;
        while (seppos != string::npos && fullpath.size() > (seppos +1) )
        {
            string part = fullpath.substr(0,seppos);
            if (part.size() && part != "..")
            {
                count++;
            }
            if (part == "..")
            {
                count--;
                if (count < 2)
                {
                    return NULL; // do not allow to escalate in the path
                }
            }

            fullpath = fullpath.substr(seppos+1);
            if (fullpath == ".." && count == 2)
            {
                return NULL; // do not allow to escalate in the path
            }
            seppos = fullpath.find("/");
        }
    }

    if (path.size() && path.at(0) == '/')
    {
        MegaNode *baseFolderNode = getBaseFolderNode(path);
        if (!baseFolderNode)
        {
            return NULL;
        }
        if (!isHandleAllowed(baseFolderNode->getHandle()) )
        {
            delete baseFolderNode;
            return NULL;
        }
        delete baseFolderNode;

        return getNodeByFullFtpPath(path);
    }
    else //it should only enter here if path == ""
    {
        MegaNode *n = ftpctx->megaApi->getNodeByHandle(ftpctx->cwd);
        if (!n)
        {
            return NULL;
        }
        MegaNode *toret = ftpctx->megaApi->getNodeByPath(path.c_str(), n);
        delete n;
        return toret;
    }
}

std::string MegaFTPServer::shortenpath(std::string path)
{
    string orig = path;

    while ((path.size() > 1) && path.at(path.size() - 1) == '/') // remove trailing /
    {
        path = path.substr(0,path.size()-1);
    }
    list<string> parts;
    size_t seppos = path.find("/");
    while (seppos != string::npos && path.size() > (seppos +1) )
    {
        string part = path.substr(0,seppos);
        if (part.size() && part != "..")
        {
            parts.push_back(part);
        }
        if (part == "..")
        {
            if (!parts.size())
            {
                return "INVALIDPATH"; // FAILURE!
            }
            parts.pop_back();
        }

        path = path.substr(seppos+1);
        if (path == "..")
        {
            if (!parts.size())
            {
                return "INVALIDPATH"; // FAILURE!
            }
            parts.pop_back();
            path = "";
        }
        seppos = path.find("/");
    }
    if (path.size() && path != "..")
    {
        parts.push_back(path);
    }

    string toret;
    if (!parts.size() && orig.size() && orig.at(0) == '/')
    {
        toret = "/";
    }
    else
    {
        while (parts.size())
        {
            toret.append("/");
            toret.append(parts.front());
            parts.pop_front();
        }
    }
    return toret;
}

std::string MegaFTPServer::cdup(handle parentHandle, MegaFTPContext* ftpctx)
{
    string response;
    MegaNode *newcwd = ftpctx->megaApi->getNodeByHandle(parentHandle);
    if (newcwd)
    {
        bool allowed = isHandleAllowed(newcwd->getHandle()) || isHandleAllowed(newcwd->getParentHandle());
        MegaNode *pn = ftpctx->megaApi->getNodeByHandle(newcwd->getHandle());
        while (!allowed && pn)
        {
            MegaNode *aux = pn;
            pn = ftpctx->megaApi->getNodeByHandle(pn->getParentHandle());
            delete aux;
            if (pn)
            {
                allowed = isHandleAllowed(pn->getParentHandle());
            }
        }
        delete pn;

        if (!allowed)
        {
            LOG_warn << "Ftp client trying to access not allowed path";
            response = "550 Path not allowed";
        }
        else if (newcwd->isFolder() && newcwd->getHandle() != UNDEF)
        {
            ftpctx->cwd = newcwd->getHandle();
            ftpctx->cwdpath = ftpctx->cwdpath + "/..";
            ftpctx->cwdpath = shortenpath(ftpctx->cwdpath);
            ftpctx->athandle = false;
            ftpctx->atroot = false;
            size_t seps = std::count(ftpctx->cwdpath.begin(), ftpctx->cwdpath.end(), '/');
            if (seps < 2)
            {
                ftpctx->cwdpath = string("/") + megaApi->handleToBase64(newcwd->getHandle()) + "/" + newcwd->getName();
            }
            ftpctx->parentcwd = newcwd->getParentHandle();

            response = "250 Directory successfully changed";
        }
        else
        {
            response = "550 CDUP failed.";
        }

        delete newcwd;
    }
    else
    {
        response = "550 Not Found";
    }
    return response;
}

std::string MegaFTPServer::cd(string newpath, MegaFTPContext* ftpctx)
{
    string response;
    if (newpath == "/")
    {
        MegaNode *rootNode = megaApi->getRootNode();
        if (rootNode)
        {
            ftpctx->cwd = rootNode->getHandle();
            ftpctx->cwdpath = "/";
            ftpctx->atroot = true;
            ftpctx->athandle = false;
            response = "250 Directory successfully changed";
            delete rootNode;
            return response;
        }
        response = "550 CWD not Found.";
        return response;
    }

    MegaNode *newcwd = getNodeByFtpPath(ftpctx, newpath);
    if (!newcwd)
    {
        response = "550 CWD not Found.";
        return response;
    }

    ftpctx->cwd = newcwd->getHandle();
    if (newpath.size() && newpath.at(0) == '/')
    {
        ftpctx->cwdpath = newpath;
    }
    else // relative paths!
    {
        ftpctx->cwdpath = (ftpctx->cwdpath == "/"?"":ftpctx->cwdpath) + "/" + newpath;
    }
    ftpctx->cwdpath = shortenpath(ftpctx->cwdpath);
    ftpctx->athandle = false;
    string handlepath = "/";

    char *cshandle = megaApi->handleToBase64(newcwd->getHandle());
    string shandle(cshandle);
    delete []cshandle;

    handlepath.append(shandle);
    if (ftpctx->cwdpath == handlepath || ftpctx->cwdpath == shandle || ftpctx->cwdpath == (handlepath +"/") )
    {
        ftpctx->cwdpath = handlepath;
        ftpctx->athandle = true;
    }
    ftpctx->atroot = false;
    ftpctx->parentcwd = newcwd->getParentHandle();

    if (ftpctx->athandle || newcwd->isFolder())
    {
        response = "250 Directory successfully changed";
    }
    else
    {
        response = "550 CWD failed."; //chrome requires this
    }
    delete newcwd;
    return response;
}

void MegaFTPServer::processReceivedData(MegaTCPContext *tcpctx, ssize_t nread, const uv_buf_t * buf)
{
    MegaFTPContext* ftpctx = dynamic_cast<MegaFTPContext *>(tcpctx);

    ssize_t parsed = -1;
    string petition;
    std::string command;
    string response;
    bool delayresponse = false;

    if (!nread)
    {
        LOG_debug << " Discarding processReceivedData read = " << nread;
        return;
    }

    uv_mutex_lock(&tcpctx->mutex);

    if (nread >= 0)
    {
        bool failed = false;
        const char *separators = " ";
        const char *crlf = "\r\n";

        petition = string(buf->base, nread);

        LOG_verbose << "FTP Server received: " << petition << " at port = " << port;

        size_t psep = petition.find_first_of(separators);
        size_t psepend = petition.find(crlf);

        if (psepend != petition.size()-strlen(crlf))
        {
            parsed = -1;
            failed = true;

            LOG_warn << " Failed to parse petition:<" << petition << ">" << " psep=" << psep << " psepend=" << psepend
                     << " petition.size=" << petition.size() << " tcpctx=" << tcpctx;
        }
        else
        {
            parsed = petition.size();
            petition = petition.substr(0,psepend);
            command = petition.substr(0,psep);
            for (char& c : command) { c = static_cast<char>(toupper(c)); };
        }

        if (failed)
        {
            ftpctx->command = FTP_CMD_INVALID;
        }
        else if(command == "USER")
        {
            ftpctx->command = FTP_CMD_USER;
        }
        else if(command == "PASS")
        {
            ftpctx->command = FTP_CMD_PASS;
        }
        else if(command == "ACCT")
        {
            ftpctx->command = FTP_CMD_ACCT;
        }
        else if(command == "CWD")
        {
            ftpctx->command = FTP_CMD_CWD;
        }
        else if(command == "CDUP")
        {
            ftpctx->command = FTP_CMD_CDUP;
        }
        else if(command == "SMNT")
        {
            ftpctx->command = FTP_CMD_SMNT;
        }
        else if(command == "QUIT")
        {
            ftpctx->command = FTP_CMD_QUIT;
        }
        else if(command == "REIN")
        {
            ftpctx->command = FTP_CMD_REIN;
        }
        else if(command == "PORT")
        {
            ftpctx->command = FTP_CMD_PORT;
        }
        else if(command == "PASV")
        {
            ftpctx->command = FTP_CMD_PASV;
        }
        else if(command == "TYPE")
        {
            ftpctx->command = FTP_CMD_TYPE;
        }
        else if(command == "STRU")
        {
            ftpctx->command = FTP_CMD_STRU;
        }
        else if(command == "MODE")
        {
            ftpctx->command = FTP_CMD_MODE;
        }
        else if(command == "RETR")
        {
            ftpctx->command = FTP_CMD_RETR;
        }
        else if(command == "STOR")
        {
            ftpctx->command = FTP_CMD_STOR;
        }
        else if(command == "STOU")
        {
            ftpctx->command = FTP_CMD_STOU;
        }
        else if(command == "APPE")
        {
            ftpctx->command = FTP_CMD_APPE;
        }
        else if(command == "ALLO")
        {
            ftpctx->command = FTP_CMD_ALLO;
        }
        else if(command == "REST")
        {
            ftpctx->command = FTP_CMD_REST;
        }
        else if(command == "RNFR")
        {
            ftpctx->command = FTP_CMD_RNFR;
        }
        else if(command == "RNTO")
        {
            ftpctx->command = FTP_CMD_RNTO;
        }
        else if(command == "ABOR")
        {
            ftpctx->command = FTP_CMD_ABOR;
        }
        else if(command == "DELE")
        {
            ftpctx->command = FTP_CMD_DELE;
        }
        else if(command == "RMD")
        {
            ftpctx->command = FTP_CMD_RMD;
        }
        else if(command == "MKD")
        {
            ftpctx->command = FTP_CMD_MKD;
        }
        else if(command == "PWD")
        {
            ftpctx->command = FTP_CMD_PWD;
        }
        else if(command == "LIST")
        {
            ftpctx->command = FTP_CMD_LIST;
        }
        else if(command == "NLST")
        {
            ftpctx->command = FTP_CMD_NLST;
        }
        else if(command == "SITE")
        {
            ftpctx->command = FTP_CMD_SITE;
        }
        else if(command == "SYST")
        {
            ftpctx->command = FTP_CMD_SYST;
        }
        else if(command == "STAT")
        {
            ftpctx->command = FTP_CMD_STAT;
        }
        else if(command == "HELP")
        {
            ftpctx->command = FTP_CMD_HELP;
        }
        else if(command == "FEAT")
        {
            ftpctx->command = FTP_CMD_FEAT;
        }
        else if(command == "SIZE")
        {
            ftpctx->command = FTP_CMD_SIZE;
        }
        else if(command == "PROT")
        {
            ftpctx->command = FTP_CMD_PROT;
        }
        else if(command == "NOOP")
        {
            ftpctx->command = FTP_CMD_NOOP;
        }
        else if(command == "EPSV")
        {
            ftpctx->command = FTP_CMD_EPSV;
        }
        else if(command == "PBSZ")
        {
            ftpctx->command = FTP_CMD_PBSZ;
        }
        else if(command == "OPTS")
        {
            ftpctx->command = FTP_CMD_OPTS;
        }
        else
        {
            LOG_warn << " Could not match command: " << command;
            ftpctx->command = FTP_CMD_INVALID;
        }

        LOG_debug << " parsed command = " << ftpctx->command << " tcpctx=" << tcpctx;
        ftpctx->arg1 = "";
        ftpctx->arg2 = "";

        switch (ftpctx->command)
        {
        // no args
        case FTP_CMD_ABOR:
        case FTP_CMD_STOU:
        case FTP_CMD_QUIT:
        case FTP_CMD_REIN:
        case FTP_CMD_CDUP:
        case FTP_CMD_PASV:
        case FTP_CMD_EPSV:
        case FTP_CMD_PWD:
        case FTP_CMD_SYST:
        case FTP_CMD_FEAT:
        case FTP_CMD_NOOP:
            if (psep != string::npos)
            {
                parsed = -1;
            }
            break;
        // single arg
        case FTP_CMD_USER:
        case FTP_CMD_PASS:
        case FTP_CMD_ACCT:
        case FTP_CMD_CWD:
        case FTP_CMD_SMNT:
        case FTP_CMD_PORT:
        case FTP_CMD_TYPE:
        case FTP_CMD_STRU:
        case FTP_CMD_MODE:
        case FTP_CMD_RETR:
        case FTP_CMD_STOR:
        case FTP_CMD_APPE:
        case FTP_CMD_DELE:
        case FTP_CMD_RMD:
        case FTP_CMD_MKD:
        case FTP_CMD_REST:
        case FTP_CMD_RNFR:
        case FTP_CMD_RNTO:
        case FTP_CMD_SITE:
        case FTP_CMD_SIZE:
        case FTP_CMD_PBSZ:
        case FTP_CMD_PROT:
            if (psep != string::npos && ( (psep + 1)< petition.size()) )
            {
                string rest = petition.substr(psep+1);
                ftpctx->arg1 = rest;
            }
            else
            {
                parsed = -1;
            }

            break;
            //optional arg
        case FTP_CMD_LIST:
        case FTP_CMD_NLST:
        case FTP_CMD_STAT:
        case FTP_CMD_HELP:
            if (psep != string::npos)
            {
                string rest = petition.substr(psep+1);
                ftpctx->arg1 = rest;
            }
            break;

        case FTP_CMD_ALLO: //ALLO <SP> <decimal-integer> [<SP> R <SP> <decimal-integer>] <CRLF>
            if (psep != string::npos && ((psep + 1) < petition.size()))
            {
                string rest = petition.substr(psep+1);
                psep = rest.find_first_of(separators);
                ftpctx->arg1 = rest.substr(0,psep);
                if (psep != string::npos && ((psep + 1) < petition.size()))
                {  //optional R <SP> <decimal-integer>
                    rest = petition.substr(psep+1);
                    psep = rest.find_first_of(separators);
                    if (psep != 1 && (rest.at(0) != 'R' || rest.at(0) != 'r'))
                    {
                        parsed = -1;
                    }
                    else if ((psep + 1) < petition.size())
                    {
                        rest = petition.substr(psep+1);
                        ftpctx->arg2 = rest;
                    }
                    else
                    {
                        parsed = -1;
                    }
                }
            }
            else
            {
                parsed = -1;
            }
            break;
        case FTP_CMD_OPTS:
            if (psep != string::npos)
            {
                string rest = petition.substr(psep+1);
                psep = rest.find_first_of(separators);
                ftpctx->arg1 = rest.substr(0,psep);
                if (psep != string::npos && ((psep + 1) < rest.size()))
                {
                    ftpctx->arg2 = rest.substr(psep+1);
                }
                else
                {
                    parsed = -1;
                }
            }
            break;
        default:
            parsed = -1;
            break;
        };

        switch (ftpctx->command)
        {
        case FTP_CMD_USER:
        {
            response = "331 User name okay, need password";
            break;
        }
        case FTP_CMD_PASS:
        {
            response = "230 User logged in, proceed";

            MegaNode *n = ftpctx->megaApi->getRootNode();
            if (n)
            {
                ftpctx->cwd = n->getHandle();
                ftpctx->cwdpath = "/";
                ftpctx->atroot = true;
                ftpctx->athandle = false;
                delete n;
            }
            break;
        }
        case FTP_CMD_NOOP:
        {
            response = "200 NOOP from MEGA!";
            break;
        }
        case FTP_CMD_TYPE:
        case FTP_CMD_PROT: // we might want to require that arg1 = "P". or disable useTLS in data channel otherwise
        {
            response = "200 OK";
            break;
        }
        case FTP_CMD_PBSZ: //we don't use received buffer size (some client might fail)
        {
            response = "200 PBSZ=0";
            break;
        }
        case FTP_CMD_PASV:
        case FTP_CMD_EPSV:
        {
            if (!ftpctx->ftpDataServer)
            {
                if (pport > (dataPortEnd))
                {
                    pport = dataportBegin;
                }
                ftpctx->pasiveport = pport++;

                LOG_debug << "Creating new MegaFTPDataServer on port " << ftpctx->pasiveport;
#ifdef ENABLE_EVT_TLS
                MegaFTPDataServer *fds = new MegaFTPDataServer(megaApi, basePath, ftpctx, useTLS, certificatepath, keypath);
#else
                MegaFTPDataServer *fds = new MegaFTPDataServer(megaApi, basePath, ftpctx, useTLS, string(), string());
#endif
                bool result = fds->start(ftpctx->pasiveport, localOnly);
                if (result)
                {
                    ftpctx->ftpDataServer = fds;
                }
                else
                {
                    response = "421 Failed to initialize data channel";
                    break;
                }
            }
            else
            {
                LOG_debug << "Reusing FTP Data connection with port: " << ftpctx->pasiveport;
            }

            // gathering IP connected to
            struct sockaddr_in addr;
            int sadrlen = sizeof (struct sockaddr_in);
            uv_tcp_getsockname(&ftpctx->tcphandle,(struct sockaddr*)&addr, &sadrlen);
#ifdef WIN32
            string sIPtoPASV = inet_ntoa(addr.sin_addr);
#else
            char strIP[INET_ADDRSTRLEN];
            inet_ntop( AF_INET, &addr.sin_addr.s_addr, strIP, INET_ADDRSTRLEN);
            string sIPtoPASV = strIP;
#endif
            replace( sIPtoPASV.begin(), sIPtoPASV.end(), '.', ',');

            if (ftpctx->command == FTP_CMD_PASV)
            {
                char url[30];
                sprintf(url, "%s,%d,%d", sIPtoPASV.c_str(), ftpctx->pasiveport/256, ftpctx->pasiveport%256);
                response = "227 Entering Passive Mode (";
                response.append(url);
                response.append(")");
            }
            else // FTP_CMD_EPSV
            {
                char url[30];
                sprintf(url, "%d", ftpctx->pasiveport);
                response = "229 Entering Extended Passive Mode (|||";
                response.append(url);
                response.append("|)");
            }
            break;
        }
        case FTP_CMD_OPTS:
        {
            for (char& c : ftpctx->arg1) { c = static_cast<char>(toupper(c)); };
            for (char& c : ftpctx->arg2) { c = static_cast<char>(toupper(c)); };
            if (ftpctx->arg1 == "UTF8" && ftpctx->arg2 == "ON")
            {
                response = "200 All good";
            }
            else
            {
                response = "501 Unrecognized OPTS " + ftpctx->arg1 + " " + ftpctx->arg2;
            }
            break;
        }
        case FTP_CMD_PWD:
        {
            MegaNode *n = ftpctx->megaApi->getNodeByHandle(ftpctx->cwd);
            if (n)
            {
                response = "257 ";
                response.append("\"");
                response.append(ftpctx->cwdpath);
                response.append("\"");
                delete n;
            }
            else
            {
                response = "550 Not Found";
            }
            break;
        }
        case FTP_CMD_CWD:
        {
            response = cd(ftpctx->arg1, ftpctx);
            break;
        }
        case FTP_CMD_CDUP:
        {
            MegaNode *n = ftpctx->megaApi->getNodeByHandle(ftpctx->cwd);
            if (n)
            {
                handle parentHandle = n->getParentHandle();
                response = cdup(parentHandle, ftpctx);
                delete n;
            }
            else
            {
                response = "550 CWD not Found.";
            }
            break;
        }
        case FTP_CMD_LIST:
        case FTP_CMD_NLST:
        {
            if (!ftpctx->ftpDataServer)
            {
                response = "425 No Data Connection available";
                break;
            }

            MegaNode *node = NULL;
            if (ftpctx->arg1.size() && ftpctx->arg1 != "-l" && ftpctx->arg1 != "-a")
            {
                node = getNodeByFtpPath(ftpctx, ftpctx->arg1);
            }
            else
            {
                if (ftpctx->atroot)
                {
                    assert(!ftpctx->ftpDataServer->resultmsj.size());
                    set<handle> handles = getAllowedHandles();
                    for (std::set<handle>::iterator it = handles.begin(); it != handles.end(); ++it)
                    {
                        string name = megaApi->handleToBase64(*it);
                        MegaNode *n = megaApi->getNodeByHandle(*it);
                        if (n)
                        {
                            string toret = getListingLineFromNode(n, name);
                            toret.append(crlfout);
                            ftpctx->ftpDataServer->resultmsj.append(toret);
                            delete n;
                        }
                    }

                    ftpctx->ftpDataServer->resultmsj.append(crlfout);

                    response = "150 Here comes the directory listing";
                    break;
                }
                else if (ftpctx->athandle)
                {
                    MegaNode *n = megaApi->getNodeByHandle(ftpctx->cwd);
                    if (n)
                    {
                        string toret = getListingLineFromNode(n);
                        toret.append(crlfout);
                        assert(!ftpctx->ftpDataServer->resultmsj.size());
                        ftpctx->ftpDataServer->resultmsj.append(toret);
                        delete n;
                    }
                    ftpctx->ftpDataServer->resultmsj.append(crlfout);

                    response = "150 Here comes the directory listing";
                    break;
                }

                node = ftpctx->megaApi->getNodeByHandle(ftpctx->cwd);
            }
            if (node)
            {
                if (node->isFolder())
                {
                    MegaNodeList *children = ftpctx->megaApi->getChildren(node, MegaApi::ORDER_NONE);
                    assert(!ftpctx->ftpDataServer->resultmsj.size());

                    if (ftpctx->command == FTP_CMD_LIST)
                    {
                        ftpctx->ftpDataServer->resultmsj.append(getListingLineFromNode(node,"."));
                        ftpctx->ftpDataServer->resultmsj.append(crlfout);
                    }

                    for (int i = 0; i < children->size(); i++)
                    {
                        MegaNode *child = children->get(i);
                        //string childURL = subbaseURL + child->getName();
                        string toret;
                        if (ftpctx->command == FTP_CMD_LIST)
                        {
                            toret = getListingLineFromNode(child);
                        }
                        else //NLST
                        {
                            toret = child->getName();
                        }
                        toret.append(crlfout);
                        ftpctx->ftpDataServer->resultmsj.append(toret);
                    }
                    delete children;

                    ftpctx->ftpDataServer->resultmsj.append(crlfout);

                    response = "150 Here comes the directory listing";
                }
                else
                {
                    string toret;
                    if (ftpctx->command == FTP_CMD_LIST)
                    {
                        toret = getListingLineFromNode(node);
                    }
                    else //NLST
                    {
                        toret = node->getName();
                    }
                    toret.append(crlfout);
                    assert(!ftpctx->ftpDataServer->resultmsj.size());
                    ftpctx->ftpDataServer->resultmsj.append(toret);
                    response = "150 Here comes the file listing";
                }
                delete node;
            }
            else
            {
                response = "550 Not Found";
            }
            break;
        }
        case FTP_CMD_RETR:
        {
            if (!ftpctx->ftpDataServer)
            {
                response = "425 No Data Connection available";
                break;
            }

            MegaNode *node = getNodeByFtpPath(ftpctx, ftpctx->arg1);
            if (node)
            {
                if (node->isFile())
                {
                    uv_mutex_lock(&ftpctx->mutex_nodeToDownload);

                    MegaNode *oldNodeToDownload = ftpctx->ftpDataServer->nodeToDownload;
                    ftpctx->ftpDataServer->nodeToDownload = node;
                    delete oldNodeToDownload;

                    uv_mutex_unlock(&ftpctx->mutex_nodeToDownload);

                    response = "150 Here comes the file: ";
                    response.append(node->getName());
                }
                else
                {
                    response = "501 Not a file";
                    delete node;
                }
            }
            else
            {
                response = "550 Not Found";
            }
            break;
        }
//        case FTP_CMD_STOU: //do create random unique name
        case FTP_CMD_STOR:
        {
            //We might want to deal with foreign node / public link and so on ?
            if (!ftpctx->ftpDataServer)
            {
                response = "425 No Data Connection available";
                break;
            }

            MegaNode *newParentNode = NULL;
            size_t seppos = ftpctx->arg1.find_last_of("/");
            ftpctx->ftpDataServer->newNameToUpload = ftpctx->arg1;
            if (seppos == string::npos)
            {
                ftpctx->ftpDataServer->newParentNodeHandle = ftpctx->cwd;
            }
            else
            {
                if (!seppos) //new folder structure does not allow this
                {
                    response = "550 Not Found";
                    break;
                }

                if ((seppos + 1) < ftpctx->ftpDataServer->newNameToUpload.size())
                {
                    ftpctx->ftpDataServer->newNameToUpload = ftpctx->ftpDataServer->newNameToUpload.substr(seppos + 1);
                }
                string newparentpath = ftpctx->arg1.substr(0, seppos);
                newParentNode = getNodeByFtpPath(ftpctx, newparentpath);
                if (newParentNode)
                {
                    ftpctx->ftpDataServer->newParentNodeHandle = newParentNode->getHandle();
                    delete newParentNode;
                }
                else
                {
                    ftpctx->ftpDataServer->newNameToUpload = ""; //empty
                }
            }

            if (ftpctx->ftpDataServer->newNameToUpload.size())
            {
                ftpctx->ftpDataServer->remotePathToUpload = ftpctx->arg1;
                response = "150 Opening data connection for storing ";
                response.append(ftpctx->arg1);
            }
            else
            {
                response = "550 Destiny Not Found";
            }
            break;
        }
        case FTP_CMD_RNFR:
        {
            MegaNode *nodetoRename = getNodeByFtpPath(ftpctx, ftpctx->arg1);
            if (nodetoRename)
            {
                this->nodeHandleToRename = nodetoRename->getHandle();
                response = "350 Pending RNTO";
                delete nodetoRename;
            }
            else
            {
                response = "550 Not Found";
            }
            break;
        }
        case FTP_CMD_RMD:
        case FTP_CMD_DELE:
        {
            MegaNode *nodeToDelete = getNodeByFtpPath(ftpctx, ftpctx->arg1);

            if (nodeToDelete)
            {
                if ((ftpctx->command == FTP_CMD_DELE) ? nodeToDelete->isFile() : nodeToDelete->isFolder())
                {
                    ftpctx->megaApi->remove(nodeToDelete, false, ftpctx);
                    delayresponse = true;
                }
                else
                {
                    response = "501 Wrong type";
                }
                delete nodeToDelete;
            }
            else
            {
                response = "550 Not Found";
            }
            break;
        }
        case FTP_CMD_RNTO:
        {
            if (this->nodeHandleToRename == UNDEF)
            {
                response = "503 Bad sequence of commands, required RNFR first";
            }
            else
            {
                MegaNode *nodeToRename = ftpctx->megaApi->getNodeByHandle(this->nodeHandleToRename);
                if (nodeToRename)
                {
                    MegaNode *n = getNodeByFtpPath(ftpctx, ftpctx->arg1);
                    if (n)
                    {
                        ftpctx->nodeToDeleteAfterMove = n;
                    }

                    MegaNode *newParentNode = NULL;
                    size_t seppos = ftpctx->arg1.find_last_of("/");
                    string newName = ftpctx->arg1;

                    if (seppos != string::npos)
                    {
                        if (!seppos) //new folder structure does not allow this
                        {
                            response = "553 Requested action not taken: Invalid destiny";
                            delete n;
                            ftpctx->nodeToDeleteAfterMove = NULL;
                            delete nodeToRename;
                            break;
                        }

                        if ((seppos + 1) < newName.size())
                        {
                            newName = newName.substr(seppos + 1);
                        }
                        string newparentpath = ftpctx->arg1.substr(0, seppos);
                        newParentNode = getNodeByFtpPath(ftpctx, newparentpath);
                        if (!newParentNode)
                        {
                            newName = ""; //empty
                        }
                    }

                    if (newName.size())
                    {
                        if (newParentNode && newParentNode->getHandle() != nodeToRename->getParentHandle())
                        {
                            newNameAfterMove = newName;
                            ftpctx->megaApi->moveNode(nodeToRename, newParentNode, ftpctx);
                            delayresponse = true;
                        }
                        else
                        {
                            ftpctx->megaApi->renameNode(nodeToRename, newName.c_str(), ftpctx);
                            delayresponse = true;
                        }
                    }
                    else
                    {
                        response = "553 Requested action not taken. Invalid destiny";
                        delete n;
                        ftpctx->nodeToDeleteAfterMove = NULL;
                    }
                    delete newParentNode;
                    delete nodeToRename;
                }
                else
                {
                    response = "553 Requested action not taken. Origin not found: no longer available";
                }

                nodeHandleToRename = UNDEF;
            }
            break;
        }
        case FTP_CMD_MKD:
        {
            MegaNode *newParentNode = NULL;
            size_t seppos = ftpctx->arg1.find_last_of("/");
            string newNameFolder = ftpctx->arg1;
            MegaNode *n = getNodeByFtpPath(ftpctx, newNameFolder);
            if (n)
            {
                response = "550 already existing!";
                delete n;
            }
            else
            {
                if (!seppos) //new folder structure does not allow this
                {
                    response = "550 Not Found";
                    break;
                }

                if (seppos != string::npos)
                {
                    if ((seppos + 1) < newNameFolder.size())
                    {
                        newNameFolder = newNameFolder.substr(seppos + 1);
                    }
                    string newparentpath = ftpctx->arg1.substr(0, seppos);
                    newParentNode = getNodeByFtpPath(ftpctx, newparentpath);
                    if (!newParentNode)
                    {
                        newNameFolder = ""; //empty
                    }
                }

                if (newNameFolder.size())
                {
                    MegaNode *nodecwd = ftpctx->megaApi->getNodeByHandle(ftpctx->cwd);
                    ftpctx->megaApi->createFolder(newNameFolder.c_str(),newParentNode ? newParentNode : nodecwd, ftpctx);
                    delete nodecwd;
                    delayresponse = true;
                }
                else
                {
                    response = "550 Not Found";
                }
                delete newParentNode;
            }
            break;
        }
        case FTP_CMD_REST:
        {
            if (ftpctx->ftpDataServer && ftpctx->ftpDataServer->nodeToDownload)
            {
                unsigned long long number = strtoull(ftpctx->arg1.c_str(), NULL, 10);
                if (number != ULLONG_MAX)
                {
                    ftpctx->ftpDataServer->rangeStartREST = number;
                    response = "350 Restarting at: ";
                    response.append(ftpctx->arg1);
                }
                else
                {
                    response = "500 Syntax error, invalid start point";
                }
            }
            else
            {
                response = "350 Requested file action pending further information: Download file not available";
            }
            break;
        }
        case FTP_CMD_FEAT:
        {
            response = "211-Features:";
            response.append(crlfout);
            response.append(" SIZE");
            response.append(crlfout);
            response.append(" PROT");
            response.append(crlfout);
            response.append(" EPSV");
            response.append(crlfout);
            response.append(" PBSZ");
            response.append(crlfout);
//            response.append(" OPTS"); // This is actually compulsory when FEAT exists (no need to return it)
//            response.append(crlfout);
            response.append(" UTF8 ON");
            response.append(crlfout);
            response.append("211 End");
            break;
        }
        case FTP_CMD_SIZE: //This has to be exact, and depends ond STRU, MODE & TYPE!! (since assumed TYPE I, it's ok)
        {
            MegaNode *nodeToGetSize = getNodeByFtpPath(ftpctx, ftpctx->arg1);
            if (nodeToGetSize)
            {
                if (nodeToGetSize->isFile())
                {
                    response = "213 ";

                    ostringstream sizenumber;
                    sizenumber << nodeToGetSize->getSize();
                    response.append(sizenumber.str());
                }
                else
                {
                    response = "213 ";
                    response.append("0");
                }
                delete nodeToGetSize;
            }
            else
            {
                response = "550 Not Found";
            }
            break;
        }
        case FTP_CMD_INVALID:
        {
            response = "500 Syntax error, command unrecognized";
            break;
        }
        default:
            response = "502 Command not implemented";
            break;
        }

        response += crlfout;
    }

    if (nread < 0)
    {
        LOG_debug << "FTP Control Server received invalid read size. Closing connection";
        closeConnection(ftpctx);
    }
    else
    {
        if (!delayresponse)
        {
            LOG_verbose << " Processed: " << petition << ". command="<< ftpctx->command << ".\n   Responding: " << response << " tpctx=" << ftpctx;
            answer(ftpctx,response.c_str(),response.size());
        }
        else
        {
            LOG_verbose << " Processed: " << petition << ". command="<< ftpctx->command << ".\n   Delaying response. tpctx=" << ftpctx;
        }

        // Initiate data transfer for required commands
        if (ftpctx->ftpDataServer &&
                ( ftpctx->command == FTP_CMD_LIST
                  || ftpctx->command == FTP_CMD_NLST
                  || ftpctx->command == FTP_CMD_REST
                  || ftpctx->command == FTP_CMD_STOR
                  || ftpctx->command == FTP_CMD_RETR )
            )
        {
            LOG_debug << " calling sending data... ";
            ftpctx->ftpDataServer->sendData(); //rename to sth more sensefull: like wake data channel
        }
    }
    uv_mutex_unlock(&tcpctx->mutex);
}

void MegaFTPServer::processAsyncEvent(MegaTCPContext *tcpctx)
{
    LOG_verbose << "Processing FTP Server async event";
    if (tcpctx->finished)
    {
        LOG_debug << "FTP link closed, ignoring async event";
        return;
    }

    MegaFTPContext* ftpctx = dynamic_cast<MegaFTPContext *>(tcpctx);

    uv_mutex_lock(&ftpctx->mutex_responses);
    while (ftpctx->responses.size())
    {
        answer(tcpctx,ftpctx->responses.front().c_str(),ftpctx->responses.front().size());
        ftpctx->responses.pop_front();
    }
    uv_mutex_unlock(&ftpctx->mutex_responses);
}

void MegaFTPServer::processOnAsyncEventClose(MegaTCPContext* tcpctx)
{
    LOG_verbose << "At MegaFTPServer::processOnAsyncEventClose";
}

void MegaTCPServer::answer(MegaTCPContext* tcpctx, const char *rsp, size_t rlen)
{
    LOG_verbose << " answering in port " << tcpctx->server->port << " : " << string(rsp,rlen);

    uv_buf_t resbuf = uv_buf_init((char *)rsp, static_cast<unsigned>(rlen));
#ifdef ENABLE_EVT_TLS
    if (tcpctx->server->useTLS)
    {
        // we are sending the response as a whole
        int err = evt_tls_write(tcpctx->evt_tls, resbuf.base, resbuf.len, onWriteFinished_tls);
        if (err <= 0)
        {
            LOG_warn << "Finishing due to an error sending the response: " << err;
            closeConnection(tcpctx);
        }
    }
    else
    {
#endif
        uv_write_t *req = new uv_write_t();
        req->data = tcpctx;
        if (int err = uv_write(req, (uv_stream_t*)&tcpctx->tcphandle, &resbuf, 1, onWriteFinished))
        {
            delete req;
            LOG_warn << "Finishing due to an error sending the response: " << err;
            closeTCPConnection(tcpctx);
        }
#ifdef ENABLE_EVT_TLS
    }
#endif
}

bool MegaFTPServer::respondNewConnection(MegaTCPContext* tcpctx)
{
    MegaFTPContext* ftpctx = dynamic_cast<MegaFTPContext *>(tcpctx);

    string response = "220 Wellcome to FTP MEGA Server";
    response.append(crlfout);

    answer(ftpctx, response.c_str(), response.size());
    return true;

}

void MegaFTPServer::processOnExitHandleClose(MegaTCPServer *tcpServer)
{

}

MegaFTPContext::MegaFTPContext()
{
    command = 0;
    resultcode = 0;

    cwd = UNDEF;
    atroot = false;
    athandle = false;
    parentcwd = UNDEF;
    pasiveport = -1;
    ftpDataServer = NULL;
    nodeToDeleteAfterMove = NULL;
    uv_mutex_init(&mutex_responses);
    uv_mutex_init(&mutex_nodeToDownload);
}

MegaFTPContext::~MegaFTPContext()
{
    if (ftpDataServer)
    {
        LOG_verbose << "Deleting ftpDataServer associated with ftp context";
        delete ftpDataServer;
    }
    if (tmpFileName.size())
    {
        LocalPath localPath = LocalPath::fromPath(tmpFileName, *server->fsAccess);
        server->fsAccess->unlinklocal(localPath);
        tmpFileName = "";
    }
    uv_mutex_destroy(&mutex_responses);
    uv_mutex_destroy(&mutex_nodeToDownload);
}

void MegaFTPContext::onTransferStart(MegaApi *, MegaTransfer *transfer)
{
}

bool MegaFTPContext::onTransferData(MegaApi *, MegaTransfer *transfer, char *buffer, size_t size)
{
    LOG_verbose << "MegaFTPContext::onTransferData";
    return true;
}

void MegaFTPContext::onTransferFinish(MegaApi *, MegaTransfer *, MegaError *e)
{
    if (finished)
    {
        LOG_debug << "FTP link closed, ignoring the result of the transfer";
        return;
    }

    if (e->getErrorCode() == MegaError::API_OK)
    {
        MegaFTPServer::returnFtpCodeAsync(this, 250);
    }
    else
    {
        MegaFTPServer::returnFtpCodeAsyncBasedOnRequestError(this, e);
    }
    if (tmpFileName.size())
    {
        LocalPath localPath = LocalPath::fromPath(tmpFileName, *server->fsAccess);
        server->fsAccess->unlinklocal(localPath);
        tmpFileName = "";
    }
}

void MegaFTPContext::onRequestFinish(MegaApi *, MegaRequest *request, MegaError *e)
{
    if (finished)
    {
        LOG_debug << "HTTP link closed, ignoring the result of the request";
        return;
    }

    MegaFTPServer* ftpserver = dynamic_cast<MegaFTPServer *>(this->server);

    if (request->getType() == MegaRequest::TYPE_REMOVE)
    {
        if (e->getErrorCode() == MegaError::API_OK)
        {
            if (cwd == request->getNodeHandle())
            {
                LOG_verbose << " Removing cwd node, going back to parent";
                ftpserver->cdup(parentcwd, this);
            }
            else // This could be unexpected if CWD is removed elsewhere by the time this RequestFinish completes //TODO: decide upon revision: is this OK?
            {
                MegaNode *n = megaApi->getNodeByHandle(cwd);
                size_t seps = std::count(cwdpath.begin(), cwdpath.end(), '/');
                unsigned int isep = 0;
                string sup = cwdpath;
                while (!n && (isep++ < seps))
                {
                    sup.append("/..");
                    string nsup = ftpserver->shortenpath(sup);
                    ftpserver->cd(nsup, this);
                    n = megaApi->getNodeByHandle(cwd);
                }
                delete n;
            }
            MegaFTPServer::returnFtpCodeAsync(this, 250);
        }
        else
        {
            MegaFTPServer::returnFtpCodeAsyncBasedOnRequestError(this, e);
        }
    }
    else if (request->getType() == MegaRequest::TYPE_CREATE_FOLDER)
    {
        if (e->getErrorCode() == MegaError::API_OK)
        {
            MegaFTPServer::returnFtpCodeAsync(this, 257, request->getName());
        }
        else
        {
            MegaFTPServer::returnFtpCodeAsyncBasedOnRequestError(this, e);
        }
    }
    else if (request->getType() == MegaRequest::TYPE_RENAME)
    {
        if (e->getErrorCode() == MegaError::API_OK )
        {
            if (nodeToDeleteAfterMove)
            {
                this->megaApi->remove(nodeToDeleteAfterMove, false, this);
                nodeToDeleteAfterMove = NULL;
                delete nodeToDeleteAfterMove;
            }
            else
            {
                MegaFTPServer::returnFtpCodeAsync(this, 250);
            }
        }
        else
        {
            MegaFTPServer::returnFtpCodeAsyncBasedOnRequestError(this, e);
        }
    }
    else  if (request->getType() == MegaRequest::TYPE_MOVE)
    {
        if (e->getErrorCode() == MegaError::API_OK)
        {
            if (ftpserver->newNameAfterMove.size())
            {
                MegaNode *nodetoRename = this->megaApi->getNodeByHandle(request->getNodeHandle());
                if (!nodetoRename)
                {
                    MegaFTPServer::returnFtpCodeAsync(this, 550, "Moved node not found");
                }
                else if (!strcmp(nodetoRename->getName(), ftpserver->newNameAfterMove.c_str()))
                {
                    if (nodeToDeleteAfterMove)
                    {
                        this->megaApi->remove(nodeToDeleteAfterMove, false, this);
                        nodeToDeleteAfterMove = NULL;
                        delete nodeToDeleteAfterMove;
                    }
                    else
                    {
                        MegaFTPServer::returnFtpCodeAsync(this, 250);
                    }
                }
                else
                {
                    this->megaApi->renameNode(nodetoRename, ftpserver->newNameAfterMove.c_str(), this);
                }
                delete nodetoRename;
            }
            else
            {
                if (nodeToDeleteAfterMove)
                {
                    this->megaApi->remove(nodeToDeleteAfterMove, false, this);
                    nodeToDeleteAfterMove = NULL;
                    delete nodeToDeleteAfterMove;
                }
                else
                {
                    MegaFTPServer::returnFtpCodeAsync(this, 250);
                }
            }
        }
        else
        {
            MegaFTPServer::returnFtpCodeAsyncBasedOnRequestError(this, e);
        }
    }

    uv_async_send(&asynchandle);
}


// FTP DATA Server
MegaFTPDataServer::MegaFTPDataServer(MegaApiImpl *megaApi, string basePath, MegaFTPContext * controlftpctx, bool useTLS, string certificatepath, string keypath)
: MegaTCPServer(megaApi, basePath, useTLS, certificatepath, keypath)
{
    this->controlftpctx = controlftpctx;
    this->nodeToDownload = NULL;
    this->rangeStartREST = 0;
    this->notifyNewConnectionRequired = false;
    this->newParentNodeHandle = UNDEF;
}

MegaFTPDataServer::~MegaFTPDataServer()
{
    LOG_verbose << "MegaFTPDataServer::~MegaFTPDataServer";
    delete nodeToDownload;

    // if not stopped, the uv thread might want to access a pointer to this.
    // though this is done in the parent destructor, it could try to access it after vtable has been erased
    stop();
    LOG_verbose << "MegaFTPDataServer::~MegaFTPDataServer. end";
}

MegaTCPContext* MegaFTPDataServer::initializeContext(uv_stream_t *server_handle)
{
    MegaFTPDataContext* ftpctx = new MegaFTPDataContext();

    // Set connection data
    MegaFTPDataServer *server = (MegaFTPDataServer *)(server_handle->data);
    ftpctx->server = server;
    ftpctx->megaApi = server->megaApi;
    ftpctx->tcphandle.data = ftpctx;
    ftpctx->asynchandle.data = ftpctx;

    return ftpctx;
}

void MegaFTPDataServer::processWriteFinished(MegaTCPContext *tcpctx, int status)
{
    if (status < 0)
    {
        LOG_warn << " error received at processWriteFinished: " << status << ": " << uv_err_name(status);
    }

    MegaFTPDataContext* ftpdatactx = dynamic_cast<MegaFTPDataContext *>(tcpctx);

    LOG_debug << " processWriteFinished on MegaFTPDataServer. status = " << status;
    if (resultmsj.size())
    {
        resultmsj = ""; // empty result msj // this would be incorrect if we used partial writes (does not seem to be required)

        if (this->controlftpctx)
        {
            ftpdatactx->setControlCodeUponDataClose(226);
        }
        else
        {
            LOG_verbose << "Avoiding waking controlftp aync handle, ftpctx already closed";
        }
        closeConnection(tcpctx);
    }
    else // transfering node (download)
    {
        ftpdatactx->bytesWritten += ftpdatactx->lastBufferLen;
        LOG_verbose << "Bytes written: " << ftpdatactx->lastBufferLen << " Remaining: " << (ftpdatactx->size - ftpdatactx->bytesWritten);
        ftpdatactx->lastBuffer = NULL;

        if (status < 0 || ftpdatactx->size == ftpdatactx->bytesWritten)
        {
            if (status < 0)
            {
                LOG_warn << "Finishing request. Write failed: " << status << ": " << uv_err_name(status);
            }
            else
            {
                LOG_debug << "Finishing request. All data sent";
            }

            if (this->controlftpctx)
            {
                ftpdatactx->setControlCodeUponDataClose(226);
            }
            else
            {
                LOG_verbose << "Avoiding waking controlftp aync handle, ftpctx already closed";
            }
            closeConnection(ftpdatactx);
            return;
        }

        uv_mutex_lock(&ftpdatactx->mutex);
        if (ftpdatactx->lastBufferLen)
        {
            ftpdatactx->streamingBuffer.freeData(ftpdatactx->lastBufferLen);
            ftpdatactx->lastBufferLen = 0;
        }

        if (ftpdatactx->pause)
        {
            if (ftpdatactx->streamingBuffer.availableSpace() > ftpdatactx->streamingBuffer.availableCapacity() / 2)
            {
                ftpdatactx->pause = false;
                m_off_t start = ftpdatactx->rangeStart + ftpdatactx->rangeWritten + ftpdatactx->streamingBuffer.availableData();
                m_off_t len =  ftpdatactx->rangeEnd - ftpdatactx->rangeStart -  ftpdatactx->rangeWritten - ftpdatactx->streamingBuffer.availableData();

                LOG_debug << "Resuming streaming from " << start << " len: " << len
                         << " Buffer status: " << ftpdatactx->streamingBuffer.availableSpace()
                         << " of " << ftpdatactx->streamingBuffer.availableCapacity() << " bytes free";
                ftpdatactx->megaApi->startStreaming(ftpdatactx->node, start, len, ftpdatactx);
            }
        }
        uv_mutex_unlock(&ftpdatactx->mutex);

        uv_async_send(&ftpdatactx->asynchandle);
    }
}

void MegaFTPDataServer::sendData()
{
    MegaTCPContext * tcpctx = NULL;

    this->notifyNewConnectionRequired = true;

    if (connections.size())
    {
        tcpctx = connections.back(); //only interested in the last connection received (the one that needs response)
    }
    //Some client might create connections before receiving a 150 in the control channel (e.g: ftp linux command)
    // This could cause never answered / never closed connections.
    //assert(connections.size() <= 1); //This might not be true due to that

    if (tcpctx)
    {
        LOG_verbose << "MegaFTPDataServer::sendData. triggering asyncsend for tcpctx=" << tcpctx;
#ifdef ENABLE_EVT_TLS
        if (tcpctx->evt_tls == NULL)
        {
            LOG_warn << "MegaFTPDataServer::sendData, evt_tls is NULL";
        }

        if (useTLS && (!tcpctx->evt_tls || tcpctx->finished || !evt_tls_is_handshake_over(tcpctx->evt_tls)))
        {
            if (!tcpctx->evt_tls)
            {
                LOG_verbose << "MegaFTPDataServer::sendData. no evt_tls";
            }
            else if (tcpctx->finished)
            {
                LOG_verbose << "MegaFTPDataServer::sendData. tcpctx->finished";
                this->notifyNewConnectionRequired = false;
            }
            else
            {
                LOG_verbose << "MegaFTPDataServer::sendData. handshake not over";
            }
        }
        else
        {
#endif
            LOG_verbose << "MegaFTPDataServer::sendData. do triggering asyncsend 03";
            this->notifyNewConnectionRequired = false;
            uv_async_send(&tcpctx->asynchandle);
#ifdef ENABLE_EVT_TLS
        }
#endif
    }
    else
    {
        LOG_verbose << "MegaFTPDataServer::sendData. no tcpctx. notifyNewConnectionRequired";
        this->notifyNewConnectionRequired = true;
    }
}

void MegaFTPDataServer::processReceivedData(MegaTCPContext *tcpctx, ssize_t nread, const uv_buf_t * buf)
{
    MegaFTPDataContext* ftpdatactx = dynamic_cast<MegaFTPDataContext *>(tcpctx);
    MegaFTPDataServer* fds = dynamic_cast<MegaFTPDataServer *>(ftpdatactx->server);

    if (fds->newNameToUpload.size())
    {
        //create tmp file with contents in messageBody
        if (!ftpdatactx->tmpFileAccess)
        {
            ftpdatactx->tmpFileName = fds->basePath;
            ftpdatactx->tmpFileName.append("ftpstorfile");
            LocalPath suffix;
            fds->fsAccess->tmpnamelocal(suffix);
            ftpdatactx->tmpFileName.append(suffix.toPath(*fds->fsAccess));

            char ext[8];
            if (ftpdatactx->server->fsAccess->getextension(LocalPath::fromPath(fds->controlftpctx->arg1, *ftpdatactx->server->fsAccess), ext, sizeof ext))
            {
                ftpdatactx->tmpFileName.append(ext);
            }

            ftpdatactx->tmpFileAccess = fds->fsAccess->newfileaccess();
            LocalPath localPath = LocalPath::fromPath(ftpdatactx->tmpFileName, *fds->fsAccess);
            fds->fsAccess->unlinklocal(localPath);

            if (!ftpdatactx->tmpFileAccess->fopen(localPath, false, true))
            {
                ftpdatactx->setControlCodeUponDataClose(450);
                remotePathToUpload = ""; //empty, so that we don't read in the next connections
                closeConnection(tcpctx);
                return;
            }
        }

        if (nread > 0)
        {
            LOG_verbose << " Writing " << nread << " bytes " << " to temporal file: " << ftpdatactx->tmpFileName;
            if (!ftpdatactx->tmpFileAccess->fwrite((const byte*)buf->base, static_cast<unsigned>(nread), ftpdatactx->tmpFileSize) )
            {
                ftpdatactx->setControlCodeUponDataClose(450);
                remotePathToUpload = ""; //empty, so that we don't read in the next connections
                closeConnection(tcpctx);
            }
            ftpdatactx->tmpFileSize += nread;
        }
    }
    else
    {
        LOG_err << "FTPData server receiving unexpected data: " << nread << " bytes";
    }


    if (nread < 0) //transfer finish
    {
        LOG_verbose << "FTP Data Channel received invalid read size: " << nread << ". Closing connection";
        if (ftpdatactx->tmpFileName.size())
        {
            MegaNode *newParentNode = ftpdatactx->megaApi->getNodeByHandle(fds->newParentNodeHandle);
            if (newParentNode)
            {
                LOG_debug << "Starting upload of file " << fds->newNameToUpload;
                fds->controlftpctx->tmpFileName = ftpdatactx->tmpFileName;

                FileSystemType fsType = fds->fsAccess->getFilesystemType(LocalPath::fromPath(ftpdatactx->tmpFileName, *fds->fsAccess));

                ftpdatactx->megaApi->startUpload(ftpdatactx->tmpFileName.c_str(), newParentNode, fds->newNameToUpload.c_str(), fsType, fds->controlftpctx);
                ftpdatactx->controlRespondedElsewhere = true;
            }
            else
            {
                LOG_err << "Unable to start upload: " << fds->newNameToUpload;
                ftpdatactx->setControlCodeUponDataClose(550, "Destination folder not available");
            }
            ftpdatactx->tmpFileName="";
        }
        else
        {
            LOG_err << "Data channel received close without tmp file created!";
            ftpdatactx->setControlCodeUponDataClose(426);
        }

        ftpdatactx->tmpFileName = ""; // empty so that we don't try to upload it
        remotePathToUpload = ""; //empty, so that we don't read in the next connections
        closeConnection(tcpctx);
        return;
    }
}

void MegaFTPDataServer::processAsyncEvent(MegaTCPContext *tcpctx)
{
    LOG_verbose << "MegaFTPDataServer::processAsyncEvent. tcptcx= " << tcpctx;
    MegaFTPDataContext* ftpdatactx = dynamic_cast<MegaFTPDataContext *>(tcpctx);
    MegaFTPDataServer* fds = dynamic_cast<MegaFTPDataServer *>(tcpctx->server);

    if (ftpdatactx->finished)
    {
        LOG_debug << "FTP DATA link closed, ignoring async event";
        return;
    }

    if (ftpdatactx->failed)
    {
        LOG_warn << "Streaming transfer failed. Closing connection.";
        closeConnection(ftpdatactx);
        return;
    }


    uv_mutex_lock(&fds->controlftpctx->mutex_nodeToDownload);

    if (resultmsj.size())
    {
        LOG_debug << " responding DATA: " << resultmsj;
        answer(ftpdatactx, resultmsj.c_str(), resultmsj.size());
    }
    else if (remotePathToUpload.size())
    {
        LOG_debug << " receive data to store in tmp file:";
        readData(ftpdatactx);
    }
    else if (nodeToDownload)
    {
        if (!ftpdatactx->node || rangeStartREST) //alterantive to || rangeStart, define aborted?
        {
            if (!rangeStartREST)
            {
                LOG_debug << "Initiating node download via port " << fds->port;
            }
            else
            {
                LOG_debug << "Initiating node download from: " << rangeStartREST << " via port " << fds->port;
            }

            ftpdatactx->rangeStart = rangeStartREST;
            rangeStartREST = 0;// so as not to start again
            ftpdatactx->bytesWritten = 0;
            ftpdatactx->size = 0;
            ftpdatactx->streamingBuffer.setMaxBufferSize(ftpdatactx->server->getMaxBufferSize());
            ftpdatactx->streamingBuffer.setMaxOutputSize(ftpdatactx->server->getMaxOutputSize());

            ftpdatactx->transfer = new MegaTransferPrivate(MegaTransfer::TYPE_LOCAL_TCP_DOWNLOAD);

            ftpdatactx->transfer->setPath(fds->controlftpctx->arg1.c_str());
            if (ftpdatactx->nodename.size())
            {
                ftpdatactx->transfer->setFileName(ftpdatactx->nodename.c_str());
            }
            if (ftpdatactx->nodehandle.size())
            {
                ftpdatactx->transfer->setNodeHandle(MegaApi::base64ToHandle(ftpdatactx->nodehandle.c_str()));
            }

            ftpdatactx->transfer->setStartTime(Waiter::ds);

            m_off_t totalSize = nodeToDownload->getSize();
            m_off_t start = 0;
            m_off_t end = totalSize - 1;
            if (ftpdatactx->rangeStart > 0)
            {
                start = ftpdatactx->rangeStart;
            }
            ftpdatactx->rangeEnd = end + 1;
            ftpdatactx->rangeStart = start;

            //bool rangeRequested = (/*ftpdatactx->rangeEnd*/ - ftpdatactx->rangeStart) != totalSize;
            m_off_t len = end - start + 1;

            ftpdatactx->pause = false;
            ftpdatactx->lastBuffer = NULL;
            ftpdatactx->lastBufferLen = 0;
            ftpdatactx->transfer->setStartPos(0);
            ftpdatactx->transfer->setEndPos(end); //This will actually be override later
            ftpdatactx->node = nodeToDownload->copy();

            ftpdatactx->streamingBuffer.init(len);
            ftpdatactx->size = len;

            ftpdatactx->megaApi->fireOnFtpStreamingStart(ftpdatactx->transfer);

            LOG_debug << "Requesting range. From " << start << "  size " << len;
            ftpdatactx->rangeWritten = 0;
            if (start || len)
            {
                ftpdatactx->megaApi->startStreaming(nodeToDownload, start, len, ftpdatactx);
            }
            else
            {
                LOG_debug << "Skipping startStreaming call since empty file";
                ftpdatactx->megaApi->fireOnFtpStreamingFinish(ftpdatactx->transfer, make_unique<MegaErrorPrivate>(API_OK));
                ftpdatactx->transfer = NULL; // this has been deleted in fireOnStreamingFinish
                fds->processWriteFinished(ftpdatactx, 0);
            }
        }
        else
        {
            LOG_debug << "Calling sendNextBytes port = " << fds->port;
            sendNextBytes(ftpdatactx);
        }
    }
    else
    {

        LOG_err << " Async event with no result mesj!!!";
    }

    uv_mutex_unlock(&fds->controlftpctx->mutex_nodeToDownload);
}

void MegaFTPDataServer::processOnAsyncEventClose(MegaTCPContext* tcpctx)
{
    MegaFTPDataContext* ftpdatactx = dynamic_cast<MegaFTPDataContext *>(tcpctx);
    MegaFTPDataServer *fds = ((MegaFTPDataServer *)ftpdatactx->server);

    LOG_verbose << "MegaFTPDataServer::processOnAsyncEventClose. tcpctx=" << tcpctx << " port = " << fds->port << " remaining = " << fds->remainingcloseevents;

    fds->remotePathToUpload = "";

    if (ftpdatactx->transfer)
    {
        ftpdatactx->megaApi->cancelTransfer(ftpdatactx->transfer);
        ftpdatactx->megaApi->fireOnFtpStreamingFinish(ftpdatactx->transfer, make_unique<MegaErrorPrivate>(ftpdatactx->ecode));
        ftpdatactx->transfer = NULL; // this has been deleted in fireOnStreamingFinish
    }

    if (!fds->remainingcloseevents && fds->closing)
    {
        LOG_verbose << "MegaFTPDataServer::processOnAsyncEventClose stopping without waiting. port = " << fds->port;
        fds->stop(true);
    }

    if (!ftpdatactx->controlRespondedElsewhere && fds->started && !this->controlftpctx->finished)
    {
        LOG_debug << "MegaFTPDataServer::processOnAsyncEventClose port = " << fds->port << ". Responding " << ftpdatactx->controlResponseCode << ". " << ftpdatactx->controlResponseMessage;
        MegaFTPServer* ftpControlServer = dynamic_cast<MegaFTPServer *>(fds->controlftpctx->server);
        ftpControlServer->returnFtpCode(this->controlftpctx, ftpdatactx->controlResponseCode, ftpdatactx->controlResponseMessage);
    }
}

bool MegaFTPDataServer::respondNewConnection(MegaTCPContext* tcpctx)
{
    MegaFTPDataContext* ftpdatactx = dynamic_cast<MegaFTPDataContext *>(tcpctx);
    if (notifyNewConnectionRequired) //in some cases, control connection tried this before the ftpdatactx was ready. this fixes that
    {
        LOG_verbose << "MegaFTPDataServer::respondNewConnection async sending to notify new connection";
        uv_async_send(&ftpdatactx->asynchandle);
        notifyNewConnectionRequired = false;
    }

    return false;
}

void MegaFTPDataServer::processOnExitHandleClose(MegaTCPServer *tcpServer)
{
}

void MegaFTPDataServer::sendNextBytes(MegaFTPDataContext *ftpdatactx)
{
    if (ftpdatactx->finished)
    {
        LOG_debug << "FTP link closed, aborting write";
        return;
    }

    if (ftpdatactx->lastBuffer)
    {
        LOG_verbose << "Skipping write due to another ongoing write";
        return;
    }

    uv_mutex_lock(&ftpdatactx->mutex);
    if (ftpdatactx->lastBufferLen)
    {
        ftpdatactx->streamingBuffer.freeData(ftpdatactx->lastBufferLen);
        ftpdatactx->lastBufferLen = 0;
    }

    if (ftpdatactx->tcphandle.write_queue_size > ftpdatactx->streamingBuffer.availableCapacity() / 8)
    {
        LOG_warn << "Skipping write. Too much queued data";
        uv_mutex_unlock(&ftpdatactx->mutex);
        return;
    }

    uv_buf_t resbuf = ftpdatactx->streamingBuffer.nextBuffer();
    uv_mutex_unlock(&ftpdatactx->mutex);

    if (!resbuf.len)
    {
        LOG_verbose << "Skipping write. No data available." << " buffered = " << ftpdatactx->streamingBuffer.availableData();
        return;
    }

    LOG_verbose << "Writing " << resbuf.len << " bytes" << " buffered = " << ftpdatactx->streamingBuffer.availableData();
    ftpdatactx->rangeWritten += resbuf.len;
    ftpdatactx->lastBuffer = resbuf.base;
    ftpdatactx->lastBufferLen = resbuf.len;

#ifdef ENABLE_EVT_TLS
    if (ftpdatactx->server->useTLS)
    {
        //notice this, contrary to !useTLS is synchronous
        int err = evt_tls_write(ftpdatactx->evt_tls, resbuf.base, resbuf.len, onWriteFinished_tls);
        if (err <= 0)
        {
            LOG_warn << "Finishing due to an error sending the response: " << err;
            closeConnection(ftpdatactx);
        }
    }
    else
    {
#endif
        uv_write_t *req = new uv_write_t();
        req->data = ftpdatactx;

        if (int err = uv_write(req, (uv_stream_t*)&ftpdatactx->tcphandle, &resbuf, 1, onWriteFinished))
        {
            delete req;
            LOG_warn << "Finishing due to an error in uv_write: " << err;
            closeTCPConnection(ftpdatactx);
        }
#ifdef ENABLE_EVT_TLS
    }
#endif
}


MegaFTPDataContext::MegaFTPDataContext()
{
    transfer = NULL;
    lastBuffer = NULL;
    lastBufferLen = 0;
    failed = false;
    ecode = API_OK;
    pause = false;
    node = NULL;
    rangeWritten = 0;
    rangeStart = 0;
    tmpFileAccess = NULL;
    tmpFileSize = 0;
    this->controlRespondedElsewhere = false;
    this->controlResponseCode = 426;
}

MegaFTPDataContext::~MegaFTPDataContext()
{
    delete transfer;
    delete node;
}

void MegaFTPDataContext::setControlCodeUponDataClose(int code, string msg)
{
    controlResponseCode = code;
    controlResponseMessage = msg;
}

void MegaFTPDataContext::onTransferStart(MegaApi *, MegaTransfer *transfer)
{
    this->transfer->setTag(transfer->getTag());
}

bool MegaFTPDataContext::onTransferData(MegaApi *, MegaTransfer *transfer, char *buffer, size_t size)
{
    LOG_verbose << "Streaming data received: " << transfer->getTransferredBytes()
                << " Size: " << size
                << " Remaining from transfer: "   << (size + (transfer->getTotalBytes() - transfer->getTransferredBytes()) )
                << " Remaining to write TCP: "   << (this->size - bytesWritten)
                << " Queued: " << this->tcphandle.write_queue_size
                << " Buffered: " << streamingBuffer.availableData()
                << " Free: " << streamingBuffer.availableSpace();

    if (finished)
    {
        LOG_info << "Removing streaming transfer after " << transfer->getTransferredBytes() << " bytes";
        return false;
    }

    // append the data to the buffer
    uv_mutex_lock(&mutex);
    long long remaining = size + (transfer->getTotalBytes() - transfer->getTransferredBytes());
    long long availableSpace = streamingBuffer.availableSpace();
    if (remaining > availableSpace && availableSpace < (2 * m_off_t(size)))
    {
        LOG_debug << "Buffer full: " << availableSpace << " of "
                 << streamingBuffer.availableCapacity() << " bytes available only. Pausing streaming";
        pause = true;
    }
    streamingBuffer.append(buffer, static_cast<unsigned>(size));
    uv_mutex_unlock(&mutex);

    // notify the HTTP server
    uv_async_send(&asynchandle);
    return !pause;
}

void MegaFTPDataContext::onTransferFinish(MegaApi *, MegaTransfer *, MegaError *e)
{
    LOG_verbose << "MegaFTPDataContext::onTransferFinish";
    if (finished)
    {
        LOG_debug << "FTP Data link closed";
        return;
    }
    ecode = e->getErrorCode();
    if (ecode != API_OK && ecode != API_EINCOMPLETE)
    {
        LOG_warn << "Transfer failed with error code: " << ecode;
        failed = true;
    }
    uv_async_send(&asynchandle);
}

void MegaFTPDataContext::onRequestFinish(MegaApi *, MegaRequest *request, MegaError *)
{
    if (finished)
    {
        LOG_debug << "FTP data link closed, ignoring the result of the request";
        return;
    }

    uv_async_send(&asynchandle);
}

#endif

#ifdef ENABLE_CHAT
MegaTextChatPeerListPrivate::MegaTextChatPeerListPrivate()
{

}

MegaTextChatPeerListPrivate::~MegaTextChatPeerListPrivate()
{

}

MegaTextChatPeerList *MegaTextChatPeerListPrivate::copy() const
{
    MegaTextChatPeerListPrivate *ret = new MegaTextChatPeerListPrivate;

    for (int i = 0; i < size(); i++)
    {
        ret->addPeer(list.at(i).first, list.at(i).second);
    }

    return ret;
}

void MegaTextChatPeerListPrivate::addPeer(MegaHandle h, int priv)
{
    list.push_back(userpriv_pair(h, (privilege_t) priv));
}

MegaHandle MegaTextChatPeerListPrivate::getPeerHandle(int i) const
{
    if (i > size())
    {
        return INVALID_HANDLE;
    }
    else
    {
        return list.at(i).first;
    }
}

int MegaTextChatPeerListPrivate::getPeerPrivilege(int i) const
{
    if (i > size())
    {
        return PRIV_UNKNOWN;
    }
    else
    {
        return list.at(i).second;
    }
}

int MegaTextChatPeerListPrivate::size() const
{
    return int(list.size());
}

const userpriv_vector *MegaTextChatPeerListPrivate::getList() const
{
    return &list;
}

void MegaTextChatPeerListPrivate::setPeerPrivilege(handle uh, privilege_t priv)
{
    for (unsigned int i = 0; i < list.size(); i++)
    {
        if (list.at(i).first == uh)
        {
            list.at(i).second = priv;
            return;
        }
    }

    // handle not found. Create new item
    addPeer(uh, priv);
}

MegaTextChatPeerListPrivate::MegaTextChatPeerListPrivate(userpriv_vector *userpriv)
{
    handle uh;
    privilege_t priv;

    for (unsigned i = 0; i < userpriv->size(); i++)
    {
        uh = userpriv->at(i).first;
        priv = userpriv->at(i).second;

        this->addPeer(uh, priv);
    }
}

MegaTextChatPrivate::MegaTextChatPrivate(const MegaTextChat *chat)
{
    this->id = chat->getHandle();
    this->priv = chat->getOwnPrivilege();
    this->shard = chat->getShard();
    this->peers = chat->getPeerList() ? chat->getPeerList()->copy() : NULL;
    this->group = chat->isGroup();
    this->ou = chat->getOriginatingUser();
    this->title = chat->getTitle() ? chat->getTitle() : "";
    this->ts = chat->getCreationTime();
    this->archived = chat->isArchived();
    this->publicchat = chat->isPublicChat();
    this->tag = chat->isOwnChange();
    this->changed = chat->getChanges();
    this->unifiedKey = chat->getUnifiedKey() ? chat->getUnifiedKey() : "";
}

MegaTextChatPrivate::MegaTextChatPrivate(const TextChat *chat)
{
    this->id = chat->id;
    this->priv = chat->priv;
    this->shard = chat->shard;
    this->peers = chat->userpriv ? new MegaTextChatPeerListPrivate(chat->userpriv) : NULL;
    this->group = chat->group;
    this->ou = chat->ou;
    this->title = chat->title;
    this->tag = chat->tag;
    this->ts = chat->ts;
    this->archived = chat->isFlagSet(TextChat::FLAG_OFFSET_ARCHIVE);
    this->publicchat = chat->publicchat;
    this->unifiedKey = chat->unifiedKey;
    this->changed = 0;

    if (chat->changed.attachments)
    {
        changed |= MegaTextChat::CHANGE_TYPE_ATTACHMENT;
    }
    if (chat->changed.flags)
    {
        changed |= MegaTextChat::CHANGE_TYPE_FLAGS;
    }
    if (chat->changed.mode)
    {
        changed |= MegaTextChat::CHANGE_TYPE_MODE;
    }
}

MegaTextChat *MegaTextChatPrivate::copy() const
{
    return new MegaTextChatPrivate(this);
}

MegaTextChatPrivate::~MegaTextChatPrivate()
{
    delete peers;
}

MegaHandle MegaTextChatPrivate::getHandle() const
{
    return id;
}

int MegaTextChatPrivate::getOwnPrivilege() const
{
    return priv;
}

int MegaTextChatPrivate::getShard() const
{
    return shard;
}

const MegaTextChatPeerList *MegaTextChatPrivate::getPeerList() const
{
    return peers;
}

void MegaTextChatPrivate::setPeerList(const MegaTextChatPeerList *peers)
{
    if (this->peers)
    {
        delete this->peers;
    }
    this->peers = peers ? peers->copy() : NULL;
}

bool MegaTextChatPrivate::isGroup() const
{
    return group;
}

MegaHandle MegaTextChatPrivate::getOriginatingUser() const
{
    return ou;
}

const char *MegaTextChatPrivate::getTitle() const
{
    return !title.empty() ? title.c_str() : NULL;
}

const char *MegaTextChatPrivate::getUnifiedKey() const
{
    return !unifiedKey.empty() ? unifiedKey.c_str() : NULL;
}

int MegaTextChatPrivate::isOwnChange() const
{
    return tag;
}

int64_t MegaTextChatPrivate::getCreationTime() const
{
    return ts;
}

bool MegaTextChatPrivate::isArchived() const
{
    return archived;
}

bool MegaTextChatPrivate::isPublicChat() const
{
    return publicchat;
}

bool MegaTextChatPrivate::hasChanged(int changeType) const
{
    return (changed & changeType);
}

int MegaTextChatPrivate::getChanges() const
{
    return changed;
}

MegaTextChatListPrivate::~MegaTextChatListPrivate()
{
    for (unsigned int i = 0; i < (unsigned int) size(); i++)
    {
        delete list.at(i);
    }
}

MegaTextChatList *MegaTextChatListPrivate::copy() const
{
    return new MegaTextChatListPrivate(this);
}

const MegaTextChat *MegaTextChatListPrivate::get(unsigned int i) const
{
    if (i >= (unsigned int) size())
    {
        return NULL;
    }
    else
    {
        return list.at(i);
    }
}

int MegaTextChatListPrivate::size() const
{
    return int(list.size());
}

void MegaTextChatListPrivate::addChat(MegaTextChatPrivate *chat)
{
    list.push_back(chat);
}

MegaTextChatListPrivate::MegaTextChatListPrivate(const MegaTextChatListPrivate *list)
{
    MegaTextChatPrivate *chat;

    for (unsigned int i = 0; i < (unsigned int) list->size(); i++)
    {
        chat = new MegaTextChatPrivate(list->get(i));
        this->list.push_back(chat);
    }
}

MegaTextChatListPrivate::MegaTextChatListPrivate()
{

}

MegaTextChatListPrivate::MegaTextChatListPrivate(textchat_map *list)
{
    MegaTextChatPrivate *megaChat;

    textchat_map::iterator it;
    for (it = list->begin(); it != list->end(); it++)
    {
        megaChat = new MegaTextChatPrivate(it->second);
        this->list.push_back(megaChat);
    }
}
#endif

MegaTransferDataPrivate::MegaTransferDataPrivate(TransferList *transferList, long long notificationNumber)
{
    numDownloads = int(transferList->transfers[GET].size());
    downloadTags.reserve(numDownloads);
    downloadPriorities.reserve(numDownloads);
    for (auto it = transferList->begin(GET); it != transferList->end(GET); it++)
    {
        Transfer *transfer = (*it);
        for (file_list::iterator fit = transfer->files.begin(); fit != transfer->files.end(); fit++)
        {
            File *file = (*fit);
            downloadTags.push_back(file->tag);
            downloadPriorities.push_back(transfer->priority);
        }
    }
    numDownloads = int(downloadTags.size());

    numUploads = int(transferList->transfers[PUT].size());
    uploadTags.reserve(numUploads);
    uploadPriorities.reserve(numUploads);
    for (auto it = transferList->begin(PUT); it != transferList->end(PUT); it++)
    {
        Transfer *transfer = (*it);
        for (file_list::iterator fit = transfer->files.begin(); fit != transfer->files.end(); fit++)
        {
            File *file = (*fit);
            uploadTags.push_back(file->tag);
            uploadPriorities.push_back(transfer->priority);
        }
    }
    numUploads = int(uploadTags.size());

    this->notificationNumber = notificationNumber;
}

MegaTransferDataPrivate::MegaTransferDataPrivate(const MegaTransferDataPrivate *transferData)
{
    this->numDownloads = transferData->numDownloads;
    this->numUploads = transferData->numUploads;
    this->downloadTags = transferData->downloadTags;
    this->uploadTags = transferData->uploadTags;
    this->downloadPriorities = transferData->downloadPriorities;
    this->uploadPriorities = transferData->uploadPriorities;
    this->notificationNumber = transferData->notificationNumber;
}

MegaTransferDataPrivate::~MegaTransferDataPrivate()
{

}

MegaTransferData *MegaTransferDataPrivate::copy() const
{
    return new MegaTransferDataPrivate(this);
}

int MegaTransferDataPrivate::getNumDownloads() const
{
    return numDownloads;
}

int MegaTransferDataPrivate::getNumUploads() const
{
    return numUploads;
}

int MegaTransferDataPrivate::getDownloadTag(int i) const
{
    return downloadTags[i];
}

int MegaTransferDataPrivate::getUploadTag(int i) const
{
    return uploadTags[i];
}

unsigned long long MegaTransferDataPrivate::getDownloadPriority(int i) const
{
    return downloadPriorities[i];
}

unsigned long long MegaTransferDataPrivate::getUploadPriority(int i) const
{
    return uploadPriorities[i];
}

long long MegaTransferDataPrivate::getNotificationNumber() const
{
    return notificationNumber;
}

MegaSizeProcessor::MegaSizeProcessor()
{
    totalBytes = 0;
}

bool MegaSizeProcessor::processMegaNode(MegaNode *node)
{
    if (node->getType() == MegaNode::TYPE_FILE)
    {
        totalBytes += node->getSize();
    }
    return true;
}

long long MegaSizeProcessor::getTotalBytes()
{
    return totalBytes;
}


MegaEventPrivate::MegaEventPrivate(int type)
{
    this->type = type;
    this->text = NULL;
    this->number = 0;
}

MegaEventPrivate::MegaEventPrivate(MegaEventPrivate *event)
{
    this->text = NULL;

    this->type = event->getType();
    this->setText(event->getText());
    this->setNumber(event->getNumber());
}

MegaEventPrivate::~MegaEventPrivate()
{
    delete [] text;
}

MegaEvent *MegaEventPrivate::copy()
{
    return new MegaEventPrivate(this);
}

int MegaEventPrivate::getType() const
{
    return type;
}

const char *MegaEventPrivate::getText() const
{
    return text;
}

int64_t MegaEventPrivate::getNumber() const
{
    return number;
}

void MegaEventPrivate::setText(const char *text)
{
    if(this->text)
    {
        delete [] this->text;
    }
    this->text = MegaApi::strdup(text);
}

void MegaEventPrivate::setNumber(int64_t number)
{
    this->number = number;
}

MegaHandle MegaEventPrivate::getHandle() const
{
    return mHandle;
}

const char *MegaEventPrivate::getEventString() const
{
    return MegaEventPrivate::getEventString(type);
}

const char *MegaEventPrivate::getEventString(int type)
{
    switch (type)
    {
        case MegaEvent::EVENT_COMMIT_DB: return "EVENT_COMMIT_DB";
        case MegaEvent::EVENT_ACCOUNT_CONFIRMATION: return "EVENT_ACCOUNT_CONFIRMATION";
        case MegaEvent::EVENT_CHANGE_TO_HTTPS: return "EVENT_CHANGE_TO_HTTPS";
        case MegaEvent::EVENT_DISCONNECT: return "EVENT_DISCONNECT";
        case MegaEvent::EVENT_ACCOUNT_BLOCKED: return "EVENT_ACCOUNT_BLOCKED";
        case MegaEvent::EVENT_STORAGE: return "EVENT_STORAGE";
        case MegaEvent::EVENT_NODES_CURRENT: return "EVENT_NODES_CURRENT";
        case MegaEvent::EVENT_MEDIA_INFO_READY: return "EVENT_MEDIA_INFO_READY";
        case MegaEvent::EVENT_STORAGE_SUM_CHANGED: return "EVENT_STORAGE_SUM_CHANGED";
        case MegaEvent::EVENT_BUSINESS_STATUS: return "BUSINESS_STATUS";
        case MegaEvent::EVENT_KEY_MODIFIED: return "KEY_MODIFIED";
        case MegaEvent::EVENT_MISC_FLAGS_READY: return "MISC_FLAGS_READY";
    }

    return "UNKNOWN";
}

void MegaEventPrivate::setHandle(const MegaHandle &handle)
{
    mHandle = handle;
}

MegaHandleListPrivate::MegaHandleListPrivate()
{

}

MegaHandleListPrivate::MegaHandleListPrivate(const MegaHandleListPrivate *hList)
{
    mList = hList->mList;
}

MegaHandleListPrivate::~MegaHandleListPrivate()
{

}

MegaHandleList *MegaHandleListPrivate::copy() const
{
    return new MegaHandleListPrivate(this);
}

MegaHandle MegaHandleListPrivate::get(unsigned int i) const
{
    MegaHandle h = INVALID_HANDLE;

    if (i < mList.size())
    {
        h = mList.at(i);
    }

    return h;
}

unsigned int MegaHandleListPrivate::size() const
{
    return unsigned(mList.size());
}

void MegaHandleListPrivate::addMegaHandle(MegaHandle h)
{
    mList.push_back(h);
}

MegaIntegerListPrivate::MegaIntegerListPrivate(const vector<int64_t> &integers)
    : mIntegers(integers)
{

}

MegaIntegerListPrivate::~MegaIntegerListPrivate()
{

}

MegaIntegerList* MegaIntegerListPrivate::copy() const
{
    return new MegaIntegerListPrivate(mIntegers);
}

int64_t MegaIntegerListPrivate::get(int i) const
{
    if (i >= static_cast<int>(mIntegers.size()))
        {
                return -1;
        }

    return mIntegers.at(i);
}

int MegaIntegerListPrivate::size() const
{
    return static_cast<int>(mIntegers.size());
}

MegaChildrenListsPrivate::MegaChildrenListsPrivate(MegaChildrenLists *list)
    : files(list->getFileList()->copy())
    , folders(list->getFolderList()->copy())
{
}

MegaChildrenListsPrivate::MegaChildrenListsPrivate(unique_ptr<MegaNodeListPrivate> folderList, unique_ptr<MegaNodeListPrivate> fileList)
    : folders(move(folderList))
    , files(move(fileList))
{
}

MegaChildrenLists *MegaChildrenListsPrivate::copy()
{
    return new MegaChildrenListsPrivate(this);
}

MegaNodeList *MegaChildrenListsPrivate::getFileList()
{
    return files.get();
}

MegaNodeList *MegaChildrenListsPrivate::getFolderList()
{
    return folders.get();
}

MegaChildrenListsPrivate::MegaChildrenListsPrivate()
    :  files(new MegaNodeListPrivate())
    , folders(new MegaNodeListPrivate())
{
}

MegaAchievementsDetails *MegaAchievementsDetailsPrivate::fromAchievementsDetails(AchievementsDetails *details)
{
    return new MegaAchievementsDetailsPrivate(details);
}

MegaAchievementsDetailsPrivate::~MegaAchievementsDetailsPrivate()
{ }

MegaAchievementsDetails *MegaAchievementsDetailsPrivate::copy()
{
    return new MegaAchievementsDetailsPrivate(&details);
}

long long MegaAchievementsDetailsPrivate::getBaseStorage()
{
    return details.permanent_size;
}

long long MegaAchievementsDetailsPrivate::getClassStorage(int class_id)
{
    achievements_map::iterator it = details.achievements.find(class_id);
    if (it != details.achievements.end())
    {
        return it->second.storage;
    }

    return 0;
}

long long MegaAchievementsDetailsPrivate::getClassTransfer(int class_id)
{
    achievements_map::iterator it = details.achievements.find(class_id);
    if (it != details.achievements.end())
    {
        return it->second.transfer;
    }

    return 0;
}

int MegaAchievementsDetailsPrivate::getClassExpire(int class_id)
{
    achievements_map::iterator it = details.achievements.find(class_id);
    if (it != details.achievements.end())
    {
        return it->second.expire;
    }

    return 0;
}

unsigned int MegaAchievementsDetailsPrivate::getAwardsCount()
{
    return unsigned(details.awards.size());
}

int MegaAchievementsDetailsPrivate::getAwardClass(unsigned int index)
{
    if (index < details.awards.size())
    {
        return details.awards.at(index).achievement_class;
    }

    return 0;
}

int MegaAchievementsDetailsPrivate::getAwardId(unsigned int index)
{
    if (index < details.awards.size())
    {
        return details.awards.at(index).award_id;
    }

    return 0;
}

int64_t MegaAchievementsDetailsPrivate::getAwardTimestamp(unsigned int index)
{
    if (index < details.awards.size())
    {
        return details.awards.at(index).ts;
    }

    return 0;
}

int64_t MegaAchievementsDetailsPrivate::getAwardExpirationTs(unsigned int index)
{
    if (index < details.awards.size())
    {
        return details.awards.at(index).expire;
    }

    return 0;
}

MegaStringList *MegaAchievementsDetailsPrivate::getAwardEmails(unsigned int index)
{
    if (index < details.awards.size())
    {
        if (details.awards.at(index).achievement_class == MEGA_ACHIEVEMENT_INVITE)
        {
            vector<char*> data;
            vector<string>::iterator it = details.awards.at(index).emails_invited.begin();
            while (it != details.awards.at(index).emails_invited.end())
            {
                data.push_back(MegaApi::strdup(it->c_str()));
                it++;
            }
            return new MegaStringListPrivate(data.data(), int(data.size()));
        }
    }

    return new MegaStringListPrivate();
}

int MegaAchievementsDetailsPrivate::getRewardsCount()
{
    return int(details.rewards.size());
}

int MegaAchievementsDetailsPrivate::getRewardAwardId(unsigned int index)
{
    if (index < details.rewards.size())
    {
        return details.rewards.at(index).award_id;
    }
    
    return -1;
}

long long MegaAchievementsDetailsPrivate::getRewardStorage(unsigned int index)
{
    if (index < details.rewards.size())
    {
        return details.rewards.at(index).storage;
    }

    return 0;
}

long long MegaAchievementsDetailsPrivate::getRewardTransfer(unsigned int index)
{
    if (index < details.rewards.size())
    {
        return details.rewards.at(index).transfer;
    }

    return 0;
}

long long MegaAchievementsDetailsPrivate::getRewardStorageByAwardId(int award_id)
{
    for (vector<Reward>::iterator itr = details.rewards.begin(); itr != details.rewards.end(); itr++)
    {
        if (itr->award_id == award_id)
        {
            return itr->storage;
        }
    }
    
    return 0;
}

long long MegaAchievementsDetailsPrivate::getRewardTransferByAwardId(int award_id)
{
    for (vector<Reward>::iterator itr = details.rewards.begin(); itr != details.rewards.end(); itr++)
    {
        if (itr->award_id == award_id)
        {
            return itr->transfer;
        }
    }
    
    return 0;
}

int MegaAchievementsDetailsPrivate::getRewardExpire(unsigned int index)
{
    if (index < details.rewards.size())
    {
        return details.rewards.at(index).expire;
    }

    return 0;
}

long long MegaAchievementsDetailsPrivate::currentStorage()
{
    long long total = 0;
    m_time_t ts = m_time();

    for (vector<Award>::iterator it = details.awards.begin(); it != details.awards.end(); it++)
    {
        if (it->expire > ts)
        {
            for (vector<Reward>::iterator itr = details.rewards.begin(); itr != details.rewards.end(); itr++)
            {
                if (itr->award_id == it->award_id)
                {
                    total += itr->storage;
                }
            }
        }
    }

    return total;
}

long long MegaAchievementsDetailsPrivate::currentTransfer()
{
    long long total = 0;
    m_time_t ts = m_time();

    for (vector<Award>::iterator it = details.awards.begin(); it != details.awards.end(); it++)
    {
        if (it->expire > ts)
        {
            for (vector<Reward>::iterator itr = details.rewards.begin(); itr != details.rewards.end(); itr++)
            {
                if (itr->award_id == it->award_id)
                {
                    total += itr->transfer;
                }
            }
        }
    }

    return total;
}

long long MegaAchievementsDetailsPrivate::currentStorageReferrals()
{
    long long total = 0;
    m_time_t ts = m_time();

    for (vector<Award>::iterator it = details.awards.begin(); it != details.awards.end(); it++)
    {
        if ( (it->expire > ts) && (it->achievement_class == MEGA_ACHIEVEMENT_INVITE) )
        {
            for (vector<Reward>::iterator itr = details.rewards.begin(); itr != details.rewards.end(); itr++)
            {
                if (itr->award_id == it->award_id)
                {
                    total += itr->storage;
                }
            }
        }
    }

    return total;
}

long long MegaAchievementsDetailsPrivate::currentTransferReferrals()
{
    long long total = 0;
    m_time_t ts = m_time();

    for (vector<Award>::iterator it = details.awards.begin(); it != details.awards.end(); it++)
    {
        if ( (it->expire > ts) && (it->achievement_class == MEGA_ACHIEVEMENT_INVITE) )
        {
            for (vector<Reward>::iterator itr = details.rewards.begin(); itr != details.rewards.end(); itr++)
            {
                if (itr->award_id == it->award_id)
                {
                    total += itr->transfer;
                }
            }
        }
    }

    return total;
}

MegaAchievementsDetailsPrivate::MegaAchievementsDetailsPrivate(AchievementsDetails *details)
{
    this->details = (*details);
}

MegaFolderInfoPrivate::MegaFolderInfoPrivate(int numFiles, int numFolders, int numVersions, long long currentSize, long long versionsSize)
{
    this->numFiles = numFiles;
    this->numFolders = numFolders;
    this->numVersions = numVersions;
    this->currentSize = currentSize;
    this->versionsSize = versionsSize;
}

MegaFolderInfoPrivate::MegaFolderInfoPrivate(const MegaFolderInfoPrivate *folderData)
{
    this->numFiles = folderData->getNumFiles();
    this->numFolders = folderData->getNumFolders();
    this->numVersions = folderData->getNumVersions();
    this->currentSize = folderData->getCurrentSize();
    this->versionsSize = folderData->getVersionsSize();
}

MegaFolderInfoPrivate::~MegaFolderInfoPrivate()
{

}

MegaFolderInfo *MegaFolderInfoPrivate::copy() const
{
    return new MegaFolderInfoPrivate(this);
}

int MegaFolderInfoPrivate::getNumVersions() const
{
    return numVersions;
}

int MegaFolderInfoPrivate::getNumFiles() const
{
    return numFiles;
}

int MegaFolderInfoPrivate::getNumFolders() const
{
    return numFolders;
}

long long MegaFolderInfoPrivate::getCurrentSize() const
{
    return currentSize;
}

long long MegaFolderInfoPrivate::getVersionsSize() const
{
    return versionsSize;
}

TreeProcFolderInfo::TreeProcFolderInfo()
{
    numFiles = 0;
    numFolders = 0;
    numVersions = 0;
    currentSize = 0;
    versionsSize = 0;
}

void TreeProcFolderInfo::proc(MegaClient *, Node *node)
{
    if (node->parent && node->parent->type == FILENODE)
    {
        numVersions++;
        versionsSize += node->size;
    }
    else
    {
        if (node->type == FILENODE)
        {
            numFiles++;
            currentSize += node->size;
        }
        else
        {
            numFolders++;
        }
    }
}

MegaFolderInfo *TreeProcFolderInfo::getResult()
{
    return new MegaFolderInfoPrivate(numFiles, numFolders - 1, numVersions, currentSize, versionsSize);
}

MegaTimeZoneDetailsPrivate::MegaTimeZoneDetailsPrivate(vector<std::string> *timeZones, vector<int> *timeZoneOffsets, int defaultTimeZone)
{
    this->timeZones = *timeZones;
    this->timeZoneOffsets = *timeZoneOffsets;
    this->defaultTimeZone = defaultTimeZone;
}

MegaTimeZoneDetailsPrivate::MegaTimeZoneDetailsPrivate(const MegaTimeZoneDetailsPrivate *timeZoneDetails)
{
    this->timeZones = timeZoneDetails->timeZones;
    this->timeZoneOffsets = timeZoneDetails->timeZoneOffsets;
    this->defaultTimeZone = timeZoneDetails->defaultTimeZone;
}

MegaTimeZoneDetailsPrivate::~MegaTimeZoneDetailsPrivate()
{

}

MegaTimeZoneDetails *MegaTimeZoneDetailsPrivate::copy() const
{
    return new MegaTimeZoneDetailsPrivate(this);
}

int MegaTimeZoneDetailsPrivate::getNumTimeZones() const
{
    return int(timeZones.size());
}

const char *MegaTimeZoneDetailsPrivate::getTimeZone(int index) const
{
    if (index >= 0 && index < int(timeZones.size()))
    {
        return timeZones[index].c_str();
    }
    return "";
}

int MegaTimeZoneDetailsPrivate::getTimeOffset(int index) const
{
    if (index >= 0 && index < int(timeZoneOffsets.size()))
    {
        return timeZoneOffsets[index];
    }
    return 0;
}

int MegaTimeZoneDetailsPrivate::getDefault() const
{
    return defaultTimeZone;
}

MegaPushNotificationSettingsPrivate::MegaPushNotificationSettingsPrivate(const string &settingsJSON)
{
    JSON json;
    json.begin(settingsJSON.c_str() + 1);
    std::string name = json.getname();
    while (!name.empty())
    {
        std::string globalObject;
        json.storeobject(&globalObject);
        if (name == "GLOBAL")
        {
            JSON jsonGlobal;
            jsonGlobal.begin(globalObject.c_str() + 1);
            std::string globalSubsetting = jsonGlobal.getname();
            while (!globalSubsetting.empty())
            {
                if (globalSubsetting == "dnd")
                {
                    if (jsonGlobal.isnumeric())
                    {
                        mGlobalDND = jsonGlobal.getint();
                    }

                    if (mGlobalDND < 0)
                    {
                        LOG_err << "Invalid format in GLOBAL.dnd notification settings";
                        mJsonInvalid = true;
                    }
                }
                else if (globalSubsetting == "nsch")
                {
                    std::string schedule;
                    jsonGlobal.storeobject(&schedule);
                    JSON scheduleJson;
                    scheduleJson.begin(schedule.c_str() + 1);
                    std::string scheduleSubTitle = scheduleJson.getname();
                    while (!scheduleSubTitle.empty())
                    {
                        if (scheduleSubTitle == "tz")
                        {
                            scheduleJson.storeobject(&mGlobalScheduleTimezone);
                        }
                        else if (scheduleSubTitle == "start" && scheduleJson.isnumeric())
                        {
                            mGlobalScheduleStart = scheduleJson.getint32();
                        }
                        else if (scheduleSubTitle == "end" && scheduleJson.isnumeric())
                        {
                            mGlobalScheduleEnd = scheduleJson.getint32();
                        }
                        else
                        {
                            LOG_warn << "Unknown option in GLOBAL.nsch notification settings";
                        }

                        scheduleSubTitle = scheduleJson.getname();
                    }

                    if (mGlobalScheduleTimezone.empty() || mGlobalScheduleStart < 0 || mGlobalScheduleEnd < 0)
                    {
                        LOG_err << "Invalid format in GLOBAL.nsch notification settings";
                        mJsonInvalid = true;
                    }
                }
                else
                {
                    LOG_warn << "Unknown option in Global notification settings";
                    mJsonInvalid = true;
                }

                globalSubsetting = jsonGlobal.getname();
            }
        }
        else if (name == "PCR")
        {
            JSON jsonContact;
            jsonContact.begin(globalObject.c_str() + 1);
            std::string subname = jsonContact.getname();
            if (subname != "dnd")
            {
                LOG_warn << "Unknown option in PCR notification settings";
                mJsonInvalid = true;
            }
            else if (jsonContact.isnumeric())
            {
                mContactsDND = jsonContact.getint();
            }
            else
            {
                LOG_err << "Invalid format in PCR.dnd notification settings";
                mJsonInvalid = true;
            }
        }
        else if (name == "INSHARE")
        {
            JSON jsonShares;
            jsonShares.begin(globalObject.c_str() + 1);
            std::string subname = jsonShares.getname();
            if (subname != "dnd")
            {
                LOG_warn << "Unknown option in SHARES notification settings";
                mJsonInvalid = true;
            }
            else if (jsonShares.isnumeric())
            {
                mSharesDND = jsonShares.getint();
            }
            else
            {
                LOG_err << "Invalid format in SHARES.dnd notification settings";
                mJsonInvalid = true;
            }
        }
        else if (name == "CHAT")
        {
            JSON jsonChats;
            jsonChats.begin(globalObject.c_str() + 1);
            std::string subname = jsonChats.getname();
            if (subname != "dnd")
            {
                LOG_warn << "Unknown option in CHAT notification settings";
                mJsonInvalid = true;
            }
            else if (jsonChats.isnumeric())
            {
                mGlobalChatsDND = jsonChats.getint();
            }
            else
            {
                LOG_err << "Invalid format in CHAT.dnd notification settings";
                mJsonInvalid = true;
            }
        }
        else if (name.length() == 11)    // settings for specific 'chatid'
        {
            MegaHandle chatid;
            if (Base64::atob(name.c_str(), (byte*)&chatid, MegaClient::CHATHANDLE) == MegaClient::CHATHANDLE)
            {
                JSON jsonChat;
                jsonChat.begin(globalObject.c_str() + 1);
                std::string subname = jsonChat.getname();
                if (subname == "dnd" && jsonChat.isnumeric())
                {
                    mChatDND[chatid] = jsonChat.getint();
                }
                else if (subname == "an" && jsonChat.isnumeric())
                {
                    bool alwaysNotify = jsonChat.getint();
                    if (alwaysNotify)
                    {
                        mChatAlwaysNotify[chatid] = true;
                    }
                }
                else
                {
                    LOG_err << "Unknown option for chatid '" << name << "' in notification settings";
                    mJsonInvalid = true;
                }
            }
            else
            {
                LOG_warn << "Invalid format for chatid '" << name << "' in notification settings";
                mJsonInvalid = true;
            }
        }
        else
        {
            LOG_err << "Unknown option in notification settings";
            mJsonInvalid = true;
        }

        if (mJsonInvalid)
        {
            return;
        }

        name = json.getname();
    }
}

MegaPushNotificationSettingsPrivate::MegaPushNotificationSettingsPrivate()
{
}

MegaPushNotificationSettingsPrivate::MegaPushNotificationSettingsPrivate(const MegaPushNotificationSettingsPrivate *settings)
{
    mGlobalDND = settings->mGlobalDND;
    mGlobalScheduleStart = settings->mGlobalScheduleStart;
    mGlobalScheduleEnd = settings->mGlobalScheduleEnd;
    mGlobalScheduleTimezone = settings->mGlobalScheduleTimezone;
    mChatDND = settings->mChatDND;
    mChatAlwaysNotify = settings->mChatAlwaysNotify;
    mContactsDND = settings->mContactsDND;
    mSharesDND = settings->mSharesDND;
    mGlobalChatsDND = settings->mGlobalChatsDND;
}

string MegaPushNotificationSettingsPrivate::generateJson() const
{
    std::string json;
    if ((mGlobalScheduleStart > -1 && mGlobalScheduleEnd == -1)
            || (mGlobalScheduleStart == -1 && mGlobalScheduleEnd > -1))
    {
        LOG_warn << "Invalid notification settings for GLOBAL.nsch";
        return json;
    }

    json = "{";
    if (mGlobalDND > -1 || isGlobalScheduleEnabled())
    {        
        json.append("\"GLOBAL\":{");
        if (isGlobalDndEnabled())
        {
            json.append("\"dnd\":").append(std::to_string(mGlobalDND));
            json.append(",");
        }

        if (isGlobalScheduleEnabled())
        {
            json.append("\"nsch\":{\"start\":").append(std::to_string(mGlobalScheduleStart));
            json.append(",\"end\":").append(std::to_string(mGlobalScheduleEnd));
            json.append(",\"tz\":\"").append(mGlobalScheduleTimezone).append("\"}");
        }

        if (json.at(json.length() - 1) == ',')  // clear a tailing comma (not needed)
        {
            json.pop_back();
        }

        json.append("},");
    }

    if (mContactsDND > -1)
    {
        json.append("\"PCR\":{\"dnd\":").append(std::to_string(mContactsDND)).append("}");
        json.append(",");
    }

    if (mSharesDND > -1)
    {
        json.append("\"INSHARE\":{\"dnd\":").append(std::to_string(mSharesDND)).append("}");
        json.append(",");
    }

    if (isGlobalChatsDndEnabled())
    {
        json.append("\"CHAT\":{\"dnd\":").append(std::to_string(mGlobalChatsDND)).append("}");
        json.append(",");
    }

    char chatid[MegaClient::CHATHANDLE * 4 / 3 + 4];

    std::map<uint64_t, m_time_t>::const_iterator itDND;
    for (itDND = mChatDND.begin(); itDND != mChatDND.end(); itDND++)
    {
        if (!isChatAlwaysNotifyEnabled(itDND->first) && isChatDndEnabled(itDND->first))
        {
            Base64::btoa((byte*)&(itDND->first), MegaClient::CHATHANDLE, chatid);
            json.append("\"").append(chatid).append("\":{");
            json.append("\"dnd\":").append(std::to_string(itDND->second)).append("}");
            json.append(",");
        }
    }

    std::map<uint64_t, bool>::const_iterator itAn;
    for (itAn = mChatAlwaysNotify.begin(); itAn != mChatAlwaysNotify.end(); itAn++)
    {
        assert(isChatAlwaysNotifyEnabled(itAn->first));
        assert(!isChatDndEnabled(itAn->first));

        if (isChatAlwaysNotifyEnabled(itAn->first))
        {
            Base64::btoa((byte*)&(itAn->first), MegaClient::CHATHANDLE, chatid);
            json.append("\"").append(chatid).append("\":{");
            json.append("\"an\":").append("1").append("}");
            json.append(",");
        }
    }

    if (json.at(json.length() - 1) == ',')  // clear a tailing comma (not needed)
    {
        json.pop_back();
    }
    json.append("}");
    return json;
}

bool MegaPushNotificationSettingsPrivate::isValid() const
{
    return !mJsonInvalid;
}

MegaPushNotificationSettingsPrivate::~MegaPushNotificationSettingsPrivate()
{

}

bool MegaPushNotificationSettingsPrivate::isGlobalEnabled() const
{
    return !isGlobalDndEnabled();
}

bool MegaPushNotificationSettingsPrivate::isGlobalDndEnabled() const
{
    return (mGlobalDND == 0 || mGlobalDND > m_time(NULL));
}

bool MegaPushNotificationSettingsPrivate::isChatsEnabled() const
{
    return !isGlobalChatsDndEnabled();
}

bool MegaPushNotificationSettingsPrivate::isGlobalChatsDndEnabled() const
{
    return (mGlobalChatsDND == 0 || mGlobalChatsDND > m_time(NULL));
}

int64_t MegaPushNotificationSettingsPrivate::getGlobalDnd() const
{
    return mGlobalDND;
}

int64_t MegaPushNotificationSettingsPrivate::getGlobalChatsDnd() const
{
    return mGlobalChatsDND;
}

bool MegaPushNotificationSettingsPrivate::isGlobalScheduleEnabled() const
{
    return (mGlobalScheduleStart != -1 && mGlobalScheduleEnd != -1 && !mGlobalScheduleTimezone.empty());
}

int MegaPushNotificationSettingsPrivate::getGlobalScheduleStart() const
{
    return mGlobalScheduleStart;
}

int MegaPushNotificationSettingsPrivate::getGlobalScheduleEnd() const
{
    return mGlobalScheduleEnd;
}

const char *mega::MegaPushNotificationSettingsPrivate::getGlobalScheduleTimezone() const
{
    return MegaApi::strdup(mGlobalScheduleTimezone.c_str());
}

bool MegaPushNotificationSettingsPrivate::isChatEnabled(MegaHandle chatid) const
{
    return !isChatDndEnabled(chatid);
}

bool MegaPushNotificationSettingsPrivate::isChatDndEnabled(MegaHandle chatid) const
{
    std::map<uint64_t, m_time_t>::const_iterator it = mChatDND.find(chatid);
    return (it != mChatDND.end() && (it->second == 0 || it->second > m_time(NULL)));
}

int64_t MegaPushNotificationSettingsPrivate::getChatDnd(MegaHandle chatid) const
{
    std::map<uint64_t, m_time_t>::const_iterator it = mChatDND.find(chatid);
    if (it != mChatDND.end())
    {
        assert(it->second != -1);
        return it->second;
    }

    return -1;
}

bool MegaPushNotificationSettingsPrivate::isChatAlwaysNotifyEnabled(MegaHandle chatid) const
{
    std::map<uint64_t, bool>::const_iterator it = mChatAlwaysNotify.find(chatid);
    return (it != mChatAlwaysNotify.end() && it->second);
}

bool MegaPushNotificationSettingsPrivate::isContactsEnabled() const
{
    return (mContactsDND == -1 || (mContactsDND > 0 && mContactsDND < m_time(NULL)));
}

bool MegaPushNotificationSettingsPrivate::isSharesEnabled() const
{
    return (mSharesDND == -1 || (mSharesDND > 0 && mSharesDND < m_time(NULL)));
}

MegaPushNotificationSettings *MegaPushNotificationSettingsPrivate::copy() const
{
    return new MegaPushNotificationSettingsPrivate(this);
}

void MegaPushNotificationSettingsPrivate::enableGlobal(bool enable)
{
    if (!isGlobalDndEnabled() == enable)
    {
        return;
    }

    mGlobalDND = enable ? -1 : 0;
}

void MegaPushNotificationSettingsPrivate::setGlobalDnd(int64_t timestamp)
{
    assert(timestamp > 0);
    if (isGlobalDndEnabled())
    {
        LOG_warn << "setGlobalDnd(): global notifications are currently disabled."
                    " Setting a new time period for DND mode";
    }
    mGlobalDND = timestamp;
}

void MegaPushNotificationSettingsPrivate::disableGlobalDnd()
{
    if (isGlobalDndEnabled())
    {
        LOG_warn << "disableGlobalDnd(): global notifications were disabled. Now are enabled";
    }
    mGlobalDND = -1;
}

void MegaPushNotificationSettingsPrivate::setGlobalSchedule(int start, int end, const char *timezone)
{
    if (start <= -1 || end <= -1 || !timezone || !timezone[0] || start == end)
    {
        LOG_warn << "setGlobalSchedule(): wrong arguments";
        assert(false);
        return;
    }

    mGlobalScheduleStart = start;
    mGlobalScheduleEnd = end;
    mGlobalScheduleTimezone.assign(timezone);
}

void MegaPushNotificationSettingsPrivate::disableGlobalSchedule()
{
    mGlobalScheduleStart = -1;
    mGlobalScheduleEnd = -1;
    mGlobalScheduleTimezone.clear();
}

void MegaPushNotificationSettingsPrivate::enableChat(MegaHandle chatid, bool enable)
{
    assert(!ISUNDEF(chatid));
    if (!isChatDndEnabled(chatid) == enable)
    {
        return;
    }

    if (enable)
    {
        mChatDND.erase(chatid);
    }
    else    // disable
    {
        mChatDND[chatid] = 0;
        if (isChatAlwaysNotifyEnabled(chatid))
        {
            LOG_warn << "enableChat(): always notify was enabled. Now is disabled";
            enableChatAlwaysNotify(chatid, false);
        }
    }
}

void MegaPushNotificationSettingsPrivate::setChatDnd(MegaHandle chatid, int64_t timestamp)
{
    assert(timestamp > 0);
    assert(!ISUNDEF(chatid));
    if (isChatAlwaysNotifyEnabled(chatid))
    {
        LOG_warn << "setChatDnd(): always notify was enabled. Now is disabled";
        enableChatAlwaysNotify(chatid, false);
    }

    mChatDND[chatid] = timestamp;
}

void MegaPushNotificationSettingsPrivate::setGlobalChatsDnd(int64_t timestamp)
{
    assert(timestamp > 0);
    if (isGlobalChatsDndEnabled())
    {
        LOG_warn << "setChatsDnd(): global chats notifications are currently disabled."
                    " Setting a new time period for chats DND mode";
    }
    mGlobalChatsDND = timestamp;
}

void MegaPushNotificationSettingsPrivate::enableChatAlwaysNotify(MegaHandle chatid, bool enable)
{
    assert(!ISUNDEF(chatid));
    if (enable)
    {
        if (isChatDndEnabled(chatid))
        {
            LOG_warn << "enableChatAlwaysNotify(): notifications are now disabled, DND mode is enabled";
            enableChat(chatid, true);
        }

        mChatAlwaysNotify[chatid] = 1;
    }
    else
    {
        mChatAlwaysNotify.erase(chatid);
    }
}

void MegaPushNotificationSettingsPrivate::enableContacts(bool enable)
{
    mContactsDND = enable ? -1 : 0;
}

void MegaPushNotificationSettingsPrivate::enableShares(bool enable)
{
    mSharesDND = enable ? -1 : 0;
}

void MegaPushNotificationSettingsPrivate::enableChats(bool enable)
{
    mGlobalChatsDND = enable ? -1 : 0;
}

MegaCancelTokenPrivate::~MegaCancelTokenPrivate()
{

}

void MegaCancelTokenPrivate::cancel(bool newValue)
{
    cancelFlag = newValue;
}

bool MegaCancelTokenPrivate::isCancelled() const
{
    return cancelFlag;
}

}
