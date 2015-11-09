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

#define _POSIX_SOURCE
#define _LARGE_FILES

#define _GNU_SOURCE 1
#define _FILE_OFFSET_BITS 64

#define __DARWIN_C_LEVEL 199506L

#define USE_VARARGS
#define PREFER_STDARG
#include "megaapi_impl.h"
#include "megaapi.h"

#include <iomanip>
#include <algorithm>
#include <functional>
#include <cctype>
#include <locale>

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

#if (!defined(_WIN32) && !defined(USE_CURL_PUBLIC_KEY_PINNING)) || defined(WINDOWS_PHONE)
#include <openssl/rand.h>
#endif

#ifdef WINDOWS_PHONE
const char* inet_ntop(int af, const void* src, char* dst, int cnt);
#endif

using namespace mega;

MegaNodePrivate::MegaNodePrivate(const char *name, int type, int64_t size, int64_t ctime, int64_t mtime, uint64_t nodehandle, string *nodekey, string *attrstring, MegaHandle parentHandle, const char*auth)
: MegaNode()
{
    this->name = MegaApi::strdup(name);
    this->type = type;
    this->size = size;
    this->ctime = ctime;
    this->mtime = mtime;
    this->nodehandle = nodehandle;
    this->parenthandle = parentHandle;
    this->attrstring.assign(attrstring->data(), attrstring->size());
    this->nodekey.assign(nodekey->data(),nodekey->size());
    this->changed = 0;
    this->thumbnailAvailable = false;
    this->previewAvailable = false;
    this->tag = 0;
    this->isPublicNode = true;
    this->outShares = false;
    this->inShare = false;
    this->plink = NULL;

    if(auth)
    {
        this->auth = auth;
    }

#ifdef ENABLE_SYNC
    this->syncdeleted = false;
#endif
}

MegaNodePrivate::MegaNodePrivate(MegaNode *node)
: MegaNode()
{
    this->name = MegaApi::strdup(node->getName());
    this->type = node->getType();
    this->size = node->getSize();
    this->ctime = node->getCreationTime();
    this->mtime = node->getModificationTime();
    this->nodehandle = node->getHandle();
    this->parenthandle = node->getParentHandle();
    string * attrstring = node->getAttrString();
    this->attrstring.assign(attrstring->data(), attrstring->size());
    string *nodekey = node->getNodeKey();
    this->nodekey.assign(nodekey->data(),nodekey->size());
    this->changed = node->getChanges();
    this->thumbnailAvailable = node->hasThumbnail();
    this->previewAvailable = node->hasPreview();
    this->tag = node->getTag();
    this->isPublicNode = node->isPublic();
    this->auth = *node->getAuth();
    this->outShares = node->isOutShare();
    this->inShare = node->isInShare();
    if (node->isExported())
    {
        this->plink = new PublicLink(node->getPublicHandle(), node->getExpirationTime(), node->isTakenDown());
    }
    else
        this->plink = NULL;

#ifdef ENABLE_SYNC
    this->syncdeleted = node->isSyncDeleted();
    this->localPath = node->getLocalPath();
#endif
}

MegaNodePrivate::MegaNodePrivate(Node *node)
: MegaNode()
{
    this->name = MegaApi::strdup(node->displayname());
    this->type = node->type;
    this->size = node->size;
    this->ctime = node->ctime;
    this->mtime = node->mtime;
    this->nodehandle = node->nodehandle;
    this->parenthandle = node->parent ? node->parent->nodehandle : INVALID_HANDLE;

    if(node->attrstring)
    {
        this->attrstring.assign(node->attrstring->data(), node->attrstring->size());
    }
    this->nodekey.assign(node->nodekey.data(),node->nodekey.size());

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


#ifdef ENABLE_SYNC
	this->syncdeleted = (node->syncdeleted != SYNCDEL_NONE);
    if(node->localnode)
    {
        node->localnode->getlocalpath(&localPath, true);
        localPath.append("", 1);
    }
#endif

    this->thumbnailAvailable = (node->hasfileattribute(0) != 0);
    this->previewAvailable = (node->hasfileattribute(1) != 0);
    this->tag = node->tag;
    this->isPublicNode = false;
    // if there's only one share and it has no user --> public link
    this->outShares = (node->outshares) ? (node->outshares->size() > 1 || node->outshares->begin()->second->user) : false;
    this->inShare = (node->inshare != NULL) && !node->parent;
    this->plink = node->plink ? new PublicLink(node->plink) : NULL;
}

MegaNode *MegaNodePrivate::copy()
{
	return new MegaNodePrivate(this);
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
        key = new char[FILENODEKEYLENGTH*4/3+3];
        Base64::btoa((const byte*)nodekey.data(),FILENODEKEYLENGTH, key);
    }

    return key;
}

string *MegaNodePrivate::getAttrString()
{
	return &attrstring;
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
                plink->ph, &key, &attrstring);

    delete [] skey;

    return node;
}

char *MegaNodePrivate::getPublicLink()
{
    if (!plink)
    {
        return NULL;
    }

    char *base64ph = new char[12];
    Base64::btoa((byte*)&(plink->ph), MegaClient::NODEHANDLE, base64ph);

    char *base64k = getBase64Key();

    string strlink = "https://mega.nz/#";
    strlink += (type ? "F" : "");
    strlink += "!";
    strlink += base64ph;
    strlink += "!";
    strlink += base64k;

    char *link = MegaApi::strdup(strlink.c_str());

    delete [] base64ph;
    delete [] base64k;

    return link;
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


const unsigned int MegaApiImpl::MAX_SESSION_LENGTH = 64;

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
    const char *cp;
    const char *mp;

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

bool MegaApiImpl::is_syncable(const char *name)
{
    for(unsigned int i=0; i< excludedNames.size(); i++)
    {
        if(WildcardMatch(name, excludedNames[i].c_str()))
        {
            return false;
        }
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
#endif

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

string *MegaNodePrivate::getAuth()
{
    return &auth;
}

MegaNodePrivate::~MegaNodePrivate()
{
 	delete[] name;
    delete plink;
}

MegaUserPrivate::MegaUserPrivate(User *user) : MegaUser()
{
    email = MegaApi::strdup(user->email.c_str());
	visibility = user->show;
	ctime = user->ctime;
}

MegaUserPrivate::MegaUserPrivate(MegaUser *user) : MegaUser()
{
	email = MegaApi::strdup(user->getEmail());
	visibility = user->getVisibility();
	ctime = user->getTimestamp();
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

int MegaUserPrivate::getVisibility()
{
	return visibility;
}

time_t MegaUserPrivate::getTimestamp()
{
	return ctime;
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
}

MegaShare *MegaSharePrivate::copy()
{
	return new MegaSharePrivate(this);
}

MegaSharePrivate::MegaSharePrivate(uint64_t handle, Share *share)
{
    this->nodehandle = handle;
    this->user = share->user ? MegaApi::strdup(share->user->email.c_str()) : NULL;
	this->access = share->access;
	this->ts = share->ts;
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


MegaTransferPrivate::MegaTransferPrivate(int type, MegaTransferListener *listener)
{
	this->type = type;
	this->tag = -1;
	this->path = NULL;
	this->nodeHandle = UNDEF;
	this->parentHandle = UNDEF;
	this->startPos = 0;
	this->endPos = 0;
	this->parentPath = NULL;
	this->listener = listener;
	this->retry = 0;
	this->maxRetries = 3;
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
    this->lastError = API_OK;
    this->folderTransferTag = 0;
}

MegaTransferPrivate::MegaTransferPrivate(const MegaTransferPrivate *transfer)
{
    path = NULL;
    parentPath = NULL;
    fileName = NULL;
    publicNode = NULL;
	lastBytes = NULL;

    this->listener = transfer->getListener();
    this->transfer = transfer->getTransfer();
    this->type = transfer->getType();
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
    this->setStartTime(transfer->getStartTime());
    this->setTransferredBytes(transfer->getTransferredBytes());
    this->setTotalBytes(transfer->getTotalBytes());
    this->setFileName(transfer->getFileName());
    this->setSpeed(transfer->getSpeed());
    this->setDeltaSize(transfer->getDeltaSize());
    this->setUpdateTime(transfer->getUpdateTime());
    this->setPublicNode(transfer->getPublicNode());
    this->setTransfer(transfer->getTransfer());
    this->setSyncTransfer(transfer->isSyncTransfer());
    this->setLastErrorCode(transfer->getLastErrorCode());
    this->setFolderTransferTag(transfer->getFolderTransferTag());
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
	return (transfer == NULL);
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

error MegaTransferPrivate::getLastErrorCode() const
{
    return this->lastError;
}

bool MegaTransferPrivate::isFolderTransfer() const
{
    return folderTransferTag < 0;
}

int MegaTransferPrivate::getFolderTransferTag() const
{
    return this->folderTransferTag;
}

void MegaTransferPrivate::setTag(int tag)
{
	this->tag = tag;
}

void MegaTransferPrivate::setSpeed(long long speed)
{
	this->speed = speed;
}

void MegaTransferPrivate::setDeltaSize(long long deltaSize)
{
	this->deltaSize = deltaSize;
}

void MegaTransferPrivate::setUpdateTime(int64_t updateTime)
{
	this->updateTime = updateTime;
}
void MegaTransferPrivate::setPublicNode(MegaNode *publicNode)
{
    if(this->publicNode)
    	delete this->publicNode;

    if(!publicNode)
    	this->publicNode = NULL;
    else
    	this->publicNode = publicNode->copy();
}

void MegaTransferPrivate::setSyncTransfer(bool syncTransfer)
{
	this->syncTransfer = syncTransfer;
}

void MegaTransferPrivate::setStartTime(int64_t startTime)
{
	this->startTime = startTime;
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

void MegaTransferPrivate::setLastErrorCode(error errorCode)
{
    this->lastError = errorCode;
}

void MegaTransferPrivate::setFolderTransferTag(int tag)
{
    this->folderTransferTag = tag;
}

void MegaTransferPrivate::setPath(const char* path)
{
	if(this->path) delete [] this->path;
    this->path = MegaApi::strdup(path);
	if(!this->path) return;

	for(int i = strlen(path)-1; i>=0; i--)
	{
		if((path[i]=='\\') || (path[i]=='/'))
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
	}

    return "UNKNOWN";
}

MegaTransferListener* MegaTransferPrivate::getListener() const
{
	return listener;
}

MegaTransferPrivate::~MegaTransferPrivate()
{
	delete[] path;
	delete[] parentPath;
	delete [] fileName;
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
#endif
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

    if(type == MegaRequest::TYPE_ACCOUNT_DETAILS)
    {
        this->accountDetails = new AccountDetails();
    }
    else
    {
        this->accountDetails = NULL;
    }

    if((type == MegaRequest::TYPE_GET_PRICING) || (type == MegaRequest::TYPE_GET_PAYMENT_ID) || type == MegaRequest::TYPE_UPGRADE_ACCOUNT)
    {
        this->megaPricing = new MegaPricingPrivate();
    }
    else
    {
        megaPricing = NULL;
    }
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
    this->publicNode = NULL;
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
	this->numDetails = 0;
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
    this->syncListener = request->getSyncListener();
#endif
    this->megaPricing = (MegaPricingPrivate *)request->getPricing();

    this->accountDetails = NULL;
    if(request->getAccountDetails())
    {
		this->accountDetails = new AccountDetails();
        *(this->accountDetails) = *(request->getAccountDetails());
	}
}

AccountDetails *MegaRequestPrivate::getAccountDetails() const
{
    return accountDetails;
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
#endif

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
    delete [] text;
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

void MegaRequestPrivate::addProduct(handle product, int proLevel, int gbStorage, int gbTransfer, int months, int amount, const char *currency, const char* description, const char* iosid, const char* androidid)
{
    if(megaPricing)
    {
        megaPricing->addProduct(product, proLevel, gbStorage, gbTransfer, months, amount, currency, description, iosid, androidid);
    }
}

void MegaRequestPrivate::setPublicNode(MegaNode *publicNode)
{
    if(this->publicNode)
		delete this->publicNode;

    if(!publicNode)
		this->publicNode = NULL;
    else
		this->publicNode = publicNode->copy();
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
        case TYPE_ADD_CONTACT: return "ADD_CONTACT";
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

MegaNodeListPrivate::MegaNodeListPrivate()
{
	list = NULL;
	s = 0;
}

MegaNodeListPrivate::MegaNodeListPrivate(Node** newlist, int size)
{
	list = NULL; s = size;
	if(!size) return;

	list = new MegaNode*[size];
	for(int i=0; i<size; i++)
		list[i] = MegaNodePrivate::fromNode(newlist[i]);
}

MegaNodeListPrivate::MegaNodeListPrivate(MegaNodeListPrivate *nodeList)
{
    s = nodeList->size();
	if (!s)
	{
		list = NULL;
		return;
	}

	list = new MegaNode*[s];
	for (int i = 0; i<s; i++)
        list[i] = new MegaNodePrivate(nodeList->get(i));
}

MegaNodeListPrivate::~MegaNodeListPrivate()
{
	if(!list)
		return;

	for(int i=0; i<s; i++)
		delete list[i];
	delete [] list;
}

MegaNodeList *MegaNodeListPrivate::copy()
{
    return new MegaNodeListPrivate(this);
}

MegaNode *MegaNodeListPrivate::get(int i)
{
	if(!list || (i < 0) || (i >= s))
		return NULL;

	return list[i];
}

int MegaNodeListPrivate::size()
{
	return s;
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
        list[i] = MegaSharePrivate::fromShare(uint64_tlist[i], newlist[i]);
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

int MegaFile::nextseqno = 0;

bool MegaFile::failed(error e)
{
    return e != API_EKEY && e != API_EBLOCKED && e != API_EOVERQUOTA && transfer->failcount < 10;
}

MegaFile::MegaFile() : File()
{
    seqno = ++nextseqno;
}

MegaFileGet::MegaFileGet(MegaClient *client, Node *n, string dstPath) : MegaFile()
{
    h = n->nodehandle;
    *(FileFingerprint*)this = *n;

    string securename = n->displayname();
    client->fsaccess->name2local(&securename);
    client->fsaccess->local2path(&securename, &name);

    string finalPath;
    if(dstPath.size())
    {
        char c = dstPath[dstPath.size()-1];
        if((c == '\\') || (c == '/')) finalPath = dstPath+name;
        else finalPath = dstPath;
    }
    else finalPath = name;

    size = n->size;
    mtime = n->mtime;

    if(n->nodekey.size()>=sizeof(filekey))
        memcpy(filekey,n->nodekey.data(),sizeof filekey);

    client->fsaccess->path2local(&finalPath, &localname);
    hprivate = true;
}

MegaFileGet::MegaFileGet(MegaClient *client, MegaNode *n, string dstPath) : MegaFile()
{
    h = n->getHandle();
    name = n->getName();
	string finalPath;
	if(dstPath.size())
	{
		char c = dstPath[dstPath.size()-1];
		if((c == '\\') || (c == '/')) finalPath = dstPath+name;
		else finalPath = dstPath;
	}
	else finalPath = name;

    size = n->getSize();
    mtime = n->getModificationTime();

    if(n->getNodeKey()->size()>=sizeof(filekey))
        memcpy(filekey,n->getNodeKey()->data(),sizeof filekey);

    client->fsaccess->path2local(&finalPath, &localname);
    hprivate = !n->isPublic();

    if(n->getAuth()->size())
    {
        auth = *n->getAuth();
    }
}

void MegaFileGet::prepare()
{
    if (!transfer->localfilename.size())
    {
        transfer->localfilename = localname;

        size_t index =  string::npos;
        while ((index = transfer->localfilename.rfind(transfer->client->fsaccess->localseparator, index)) != string::npos)
        {
            if(!(index % transfer->client->fsaccess->localseparator.size()))
            {
                break;
            }

            index--;
        }

        if(index != string::npos)
        {
            transfer->localfilename.resize(index + transfer->client->fsaccess->localseparator.size());
        }

        string suffix;
        transfer->client->fsaccess->tmpnamelocal(&suffix);
        transfer->localfilename.append(suffix);
    }
}

void MegaFileGet::updatelocalname()
{
#ifdef _WIN32
    transfer->localfilename.append("", 1);
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW((LPCWSTR)transfer->localfilename.data(), GetFileExInfoStandard, &fad))
        SetFileAttributesW((LPCWSTR)transfer->localfilename.data(), fad.dwFileAttributes & ~FILE_ATTRIBUTE_HIDDEN);
    transfer->localfilename.resize(transfer->localfilename.size()-1);
#endif
}

void MegaFileGet::progress()
{
#ifdef _WIN32
    if(transfer->slot && !transfer->slot->progressreported)
    {
        transfer->localfilename.append("", 1);
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW((LPCWSTR)transfer->localfilename.data(), GetFileExInfoStandard, &fad))
            SetFileAttributesW((LPCWSTR)transfer->localfilename.data(), fad.dwFileAttributes | FILE_ATTRIBUTE_HIDDEN);
        transfer->localfilename.resize(transfer->localfilename.size()-1);
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

MegaFilePut::MegaFilePut(MegaClient *client, string* clocalname, string *filename, handle ch, const char* ctargetuser, int64_t mtime) : MegaFile()
{
    // full local path
    localname = *clocalname;

    // target parent node
    h = ch;

    // target user
    targetuser = ctargetuser;

    // new node name
    name = *filename;

    customMtime = mtime;
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

ExternalLogger *MegaApiImpl::externalLogger = NULL;

MegaApiImpl::MegaApiImpl(MegaApi *api, const char *appKey, MegaGfxProcessor* processor, const char *basePath, const char *userAgent)
{
	init(api, appKey, processor, basePath, userAgent);
}

MegaApiImpl::MegaApiImpl(MegaApi *api, const char *appKey, const char *basePath, const char *userAgent)
{
	init(api, appKey, NULL, basePath, userAgent);
}

MegaApiImpl::MegaApiImpl(MegaApi *api, const char *appKey, const char *basePath, const char *userAgent, int fseventsfd)
{
	init(api, appKey, NULL, basePath, userAgent, fseventsfd);
}

void MegaApiImpl::init(MegaApi *api, const char *appKey, MegaGfxProcessor* processor, const char *basePath, const char *userAgent, int fseventsfd)
{
    this->api = api;

    sdkMutex.init(true);
    maxRetries = 10;
	currentTransfer = NULL;
    pendingUploads = 0;
    pendingDownloads = 0;
    totalUploads = 0;
    totalDownloads = 0;
    client = NULL;
    waiting = false;
    waitingRequest = false;
    totalDownloadedBytes = 0;
    totalUploadedBytes = 0;
    activeRequest = NULL;
    activeTransfer = NULL;
    activeError = NULL;
    activeNodes = NULL;
    activeUsers = NULL;
    syncLowerSizeLimit = 0;
    syncUpperSizeLimit = 0;
    downloadSpeed = 0;
    uploadSpeed = 0;
    uploadPartialBytes = 0;
    downloadPartialBytes = 0;

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
		int lastIndex = sBasePath.size() - 1;
		if (sBasePath[lastIndex] != '/' && sBasePath[lastIndex] != '\\')
		{
			string utf8Separator;
			fsAccess->local2path(&fsAccess->localseparator, &utf8Separator);
			sBasePath.append(utf8Separator);
		}
		dbAccess = new MegaDbAccess(&sBasePath);
	}
	else dbAccess = NULL;

	gfxAccess = NULL;
	if(processor)
	{
		GfxProcExternal *externalGfx = new GfxProcExternal();
		externalGfx->setProcessor(processor);
		gfxAccess = externalGfx;
	}
	else
	{
		gfxAccess = new MegaGfxProc();
	}

	if(!userAgent)
	{
		userAgent = "";
	}

    client = new MegaClient(this, waiter, httpio, fsAccess, dbAccess, gfxAccess, appKey, userAgent);

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
}

int MegaApiImpl::isLoggedIn()
{
    sdkMutex.lock();
    int result = client->loggedin();
    sdkMutex.unlock();
	return result;
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

void MegaApiImpl::setLogLevel(int logLevel)
{
    if(!externalLogger)
    {
        externalLogger = new ExternalLogger();
    }
    externalLogger->setLogLevel(logLevel);
}

void MegaApiImpl::setLoggerClass(MegaLogger *megaLogger)
{
    if(!externalLogger)
    {
        externalLogger = new ExternalLogger();
    }
    externalLogger->setMegaLogger(megaLogger);
}

void MegaApiImpl::log(int logLevel, const char *message, const char *filename, int line)
{
    if(!externalLogger)
    {
        return;
    }

    externalLogger->postLog(logLevel, message, filename, line);
}

char* MegaApiImpl::getBase64PwKey(const char *password)
{
	if(!password) return NULL;

	byte pwkey[SymmCipher::KEYLENGTH];
	error e = client->pw_key(password,pwkey);
	if(e)
		return NULL;

	char* buf = new char[SymmCipher::KEYLENGTH*4/3+4];
	Base64::btoa((byte *)pwkey, SymmCipher::KEYLENGTH, buf);
	return buf;
}

char* MegaApiImpl::getStringHash(const char* base64pwkey, const char* inBuf)
{
	if(!base64pwkey || !inBuf) return NULL;

	char pwkey[SymmCipher::KEYLENGTH];
	Base64::atob(base64pwkey, (byte *)pwkey, sizeof pwkey);

	SymmCipher key;
	key.setkey((byte*)pwkey);

    uint64_t strhash;
	string neBuf = inBuf;

    strhash = client->stringhash64(&neBuf, &key);

	char* buf = new char[8*4/3+4];
    Base64::btoa((byte*)&strhash, 8, buf);
    return buf;
}

void MegaApiImpl::getSessionTransferURL(const char *path, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_SESSION_TRANSFER_URL);
    request->setText(path);
    request->setListener(listener);
    requestQueue.push(request);
    waiter->notify();
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

void MegaApiImpl::retryPendingConnections(bool disconnect, bool includexfers, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_RETRY_PENDING_CONNECTIONS);
	request->setFlag(disconnect);
	request->setNumber(includexfers);
	request->setListener(listener);
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::addEntropy(char *data, unsigned int size)
{
    if(PrnGen::rng.CanIncorporateEntropy())
        PrnGen::rng.IncorporateEntropy((const byte*)data, size);

#ifdef USE_SODIUM
    if(EdDSA::rng.CanIncorporateEntropy())
        EdDSA::rng.IncorporateEntropy((const byte*)data, size);
#endif

#if (!defined(_WIN32) && !defined(USE_CURL_PUBLIC_KEY_PINNING)) || defined(WINDOWS_PHONE)
    RAND_seed(data, size);
#endif
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

char *MegaApiImpl::dumpXMPPSession()
{
    sdkMutex.lock();
    char* buf = NULL;

    if (client->loggedin())
    {
        buf = new char[MAX_SESSION_LENGTH * 4 / 3 + 4];
        Base64::btoa((const byte *)client->sid.data(), client->sid.size(), buf);
    }

    sdkMutex.unlock();
    return buf;
}

void MegaApiImpl::createAccount(const char* email, const char* password, const char* name, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CREATE_ACCOUNT, listener);
	request->setEmail(email);
	request->setPassword(password);
	request->setName(name);
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::fastCreateAccount(const char* email, const char *base64pwkey, const char* name, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CREATE_ACCOUNT, listener);
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

void MegaApiImpl::setProxySettings(MegaProxy *proxySettings)
{
    Proxy localProxySettings;
    localProxySettings.setProxyType(proxySettings->getProxyType());

    string url;
    if(proxySettings->getProxyURL())
        url = proxySettings->getProxyURL();

    string localurl;
    fsAccess->path2local(&url, &localurl);

    localProxySettings.setProxyURL(&localurl);

    if(proxySettings->credentialsNeeded())
    {
        string username;
        if(proxySettings->getUsername())
            username = proxySettings->getUsername();

        string localusername;
        fsAccess->path2local(&username, &localusername);

        string password;
        if(proxySettings->getPassword())
            password = proxySettings->getPassword();

        string localpassword;
        fsAccess->path2local(&password, &localpassword);

        localProxySettings.setCredentials(&localusername, &localpassword);
    }

    sdkMutex.lock();
    httpio->setproxy(&localProxySettings);
    sdkMutex.unlock();
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
        proxySettings->setProxyURL(proxyURL.c_str());
    }

    delete localProxySettings;
    return proxySettings;
}

void MegaApiImpl::loop()
{
#if (WINDOWS_PHONE || TARGET_OS_IPHONE)
    // Workaround to get the IP of valid DNS servers on Windows Phone/iOS
    string servers;

    while (true)
    {
    #ifdef WINDOWS_PHONE
        struct hostent *hp;
        hp = gethostbyname("ns.mega.co.nz");
        if (hp != NULL && hp->h_addr != NULL)
        {
            struct in_addr **addr_list;
            addr_list = (struct in_addr **)hp->h_addr_list;
            for (int i = 0; addr_list[i] != NULL; i++)
            {
                char str[INET_ADDRSTRLEN];
                const char *ip = inet_ntop(AF_INET, addr_list[i], str, INET_ADDRSTRLEN);
                if(ip == str)
                {
                    if (servers.size())
                    {
                        servers.append(",");
                    }
                    servers.append(ip);
                }
            }
        }
    #else
        __res_state res;
        if(res_ninit(&res) == 0)
        {
            union res_sockaddr_union u[MAXNS];
            int nscount = res_getservers(&res, u, MAXNS);

            for(int i = 0; i < nscount; i++)
            {
                char straddr[INET6_ADDRSTRLEN];
                straddr[0] = 0;

                if(u[i].sin.sin_family == PF_INET)
                {
                    inet_ntop(PF_INET, &u[i].sin.sin_addr, straddr, sizeof(straddr));
                }

                if(u[i].sin6.sin6_family == PF_INET6)
                {
                    inet_ntop(PF_INET6, &u[i].sin6.sin6_addr, straddr, sizeof(straddr));
                }

                if(straddr[0])
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

    LOG_debug << "Using MEGA DNS servers " << servers;
    httpio->setdnsservers(servers.c_str());

#elif _WIN32
    httpio->lock();
#endif

    while(true)
	{
        int r = client->wait();
        if(r & Waiter::NEEDEXEC)
        {
            sendPendingTransfers();
            sendPendingRequests();
            if(threadExit)
                break;

            sdkMutex.lock();
            client->exec();
            sdkMutex.unlock();
        }
	}

    sdkMutex.lock();
    delete client;

	//It doesn't seem fully safe to delete those objects :-/
    // delete httpio;
    // delete waiter;
    // delete fsAccess;
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

void MegaApiImpl::moveNode(MegaNode *node, MegaNode *newParent, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_MOVE, listener);
    if(node) request->setNodeHandle(node->getHandle());
    if(newParent) request->setParentHandle(newParent->getHandle());
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::copyNode(MegaNode *node, MegaNode* target, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_COPY, listener);
    if(node)
    {
        if(node->isPublic())
        {
            request->setPublicNode(node);
        }
        else
        {
            request->setNodeHandle(node->getHandle());
        }
    }
    if(target) request->setParentHandle(target->getHandle());
	requestQueue.push(request);
	waiter->notify();
}

void MegaApiImpl::copyNode(MegaNode *node, MegaNode *target, const char *newName, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_COPY, listener);
    if(node)
    {
        if(node->isPublic())
        {
            request->setPublicNode(node);
        }
        else
        {
            request->setNodeHandle(node->getHandle());
        }
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

void MegaApiImpl::remove(MegaNode *node, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_REMOVE, listener);
    if(node) request->setNodeHandle(node->getHandle());
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
    if(node) request->setNodeHandle(node->getHandle());
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

void MegaApiImpl::getPublicNode(const char* megaFileLink, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_PUBLIC_NODE, listener);
	request->setLink(megaFileLink);
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getThumbnail(MegaNode* node, const char *dstFilePath, MegaRequestListener *listener)
{
	getNodeAttribute(node, 0, dstFilePath, listener);
}

void MegaApiImpl::cancelGetThumbnail(MegaNode* node, MegaRequestListener *listener)
{
	cancelGetNodeAttribute(node, 0, listener);
}

void MegaApiImpl::setThumbnail(MegaNode* node, const char *srcFilePath, MegaRequestListener *listener)
{
	setNodeAttribute(node, 0, srcFilePath, listener);
}

void MegaApiImpl::getPreview(MegaNode* node, const char *dstFilePath, MegaRequestListener *listener)
{
	getNodeAttribute(node, 1, dstFilePath, listener);
}

void MegaApiImpl::cancelGetPreview(MegaNode* node, MegaRequestListener *listener)
{
	cancelGetNodeAttribute(node, 1, listener);
}

void MegaApiImpl::setPreview(MegaNode* node, const char *srcFilePath, MegaRequestListener *listener)
{
	setNodeAttribute(node, 1, srcFilePath, listener);
}

void MegaApiImpl::getUserAvatar(MegaUser* user, const char *dstFilePath, MegaRequestListener *listener)
{
    getUserAttr(user, 0, dstFilePath, listener);
}

void MegaApiImpl::setAvatar(const char *dstFilePath, MegaRequestListener *listener)
{
	setUserAttr(0, dstFilePath, listener);
}

void MegaApiImpl::getUserAttribute(MegaUser* user, int type, MegaRequestListener *listener)
{
    getUserAttr(user, type ? type : -1, NULL, listener);
}

void MegaApiImpl::setUserAttribute(int type, const char *value, MegaRequestListener *listener)
{
	setUserAttr(type ? type : -1, value, listener);
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

void MegaApiImpl::fetchNodes(MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_FETCH_NODES, listener);
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getPricing(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_PRICING, listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getPaymentId(handle productHandle, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_PAYMENT_ID, listener);
    request->setNodeHandle(productHandle);
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

void MegaApiImpl::submitPurchaseReceipt(int gateway, const char *receipt, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SUBMIT_PURCHASE_RECEIPT, listener);
    request->setNumber(gateway);
    request->setText(receipt);
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
            screditcard.erase(remove_if(screditcard.begin(), screditcard.end(),
                                     not1(ptr_fun(static_cast<int(*)(int)>(isdigit)))), screditcard.end());
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

        int tam = 256 + sfirstname.size() + slastname.size() + screditcard.size()
                + sexpire_month.size() + sexpire_year.size() + scv2.size() + saddress1.size()
                + saddress2.size() + scity.size() + sprovince.size() + spostalcode.size()
                + scountry.size() + email.size();

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
        delete ccplain;
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

void MegaApiImpl::getAccountDetails(bool storage, bool transfer, bool pro, bool sessions, bool purchases, bool transactions, MegaRequestListener *listener)
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
            path.push_back('0' + type);
            path.append(".jpg");
            delete [] base64Handle;
        }

        request->setFile(path.c_str());
    }

    request->setParamType(type);
    if(node) request->setNodeHandle(node->getHandle());
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::cancelGetNodeAttribute(MegaNode *node, int type, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CANCEL_ATTR_FILE, listener);
	request->setParamType(type);
	if (node) request->setNodeHandle(node->getHandle());
	requestQueue.push(request);
	waiter->notify();
}

void MegaApiImpl::setNodeAttribute(MegaNode *node, int type, const char *srcFilePath, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_ATTR_FILE, listener);
	request->setFile(srcFilePath);
    request->setParamType(type);
    if(node) request->setNodeHandle(node->getHandle());
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getUserAttr(MegaUser *user, int type, const char *dstFilePath, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_ATTR_USER, listener);
    if(!type && dstFilePath)
    {
        string path(dstFilePath);
#if defined(_WIN32) && !defined(WINDOWS_PHONE)
        if(!PathIsRelativeA(path.c_str()) && ((path.size()<2) || path.compare(0, 2, "\\\\")))
            path.insert(0, "\\\\?\\");
#endif

        int c = path[path.size()-1];
        if((c=='/') || (c == '\\'))
        {
            const char *email = user->getEmail();
            path.append(email);
            path.push_back('0' + type);
            path.append(".jpg");
            delete [] email;
        }

        request->setFile(path.c_str());
    }

    request->setParamType(type);
    if(user)
    {
        request->setEmail(user->getEmail());
    }
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setUserAttr(int type, const char *srcFilePath, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_ATTR_USER, listener);
    if(!type)
    {
        request->setFile(srcFilePath);
    }
    else
    {
        request->setText(srcFilePath);
    }

    request->setParamType(type);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::addContact(const char* email, MegaRequestListener* listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_ADD_CONTACT, listener);
	request->setEmail(email);
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::inviteContact(const char *email, const char *message,int action, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_INVITE_CONTACT, listener);
    request->setNumber(action);
    request->setEmail(email);
    request->setText(message);
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
        default:
            break;
    }
}

int MegaApiImpl::getDownloadMethod()
{
    if (client->autodownport)
    {
        return MegaApi::TRANSFER_METHOD_AUTO;
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
        return MegaApi::TRANSFER_METHOD_AUTO;
    }

    if (client->usealtupport)
    {
        return MegaApi::TRANSFER_METHOD_ALTERNATIVE_PORT;
    }

    return MegaApi::TRANSFER_METHOD_NORMAL;
}

MegaTransferList *MegaApiImpl::getTransfers()
{
    sdkMutex.lock();

    vector<MegaTransfer *> transfers;
    for (int d = GET; d == GET || d == PUT; d += PUT - GET)
    {
        for (transfer_map::iterator it = client->transfers[d].begin(); it != client->transfers[d].end(); it++)
        {
            Transfer *t = it->second;
            if(transferMap.find(t->tag) == transferMap.end())
            {
                continue;
            }
            MegaTransferPrivate* transfer = transferMap.at(t->tag);
            transfers.push_back(transfer);
        }
    }

    MegaTransferList *result = new MegaTransferListPrivate(transfers.data(), transfers.size());

    sdkMutex.unlock();
    return result;
}

MegaTransfer *MegaApiImpl::getTransferByTag(int transferTag)
{
    MegaTransfer* value = NULL;
    sdkMutex.lock();

    if(transferMap.find(transferTag) == transferMap.end())
    {
        sdkMutex.unlock();
        return NULL;
    }

    value = transferMap.at(transferTag)->copy();
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
    for (transfer_map::iterator it = client->transfers[type].begin(); it != client->transfers[type].end(); it++)
    {
        Transfer *t = it->second;
        if(transferMap.find(t->tag) == transferMap.end())
        {
            continue;
        }
        MegaTransferPrivate* transfer = transferMap.at(t->tag);
        transfers.push_back(transfer);
    }

    MegaTransferList *result = new MegaTransferListPrivate(transfers.data(), transfers.size());

    sdkMutex.unlock();
    return result;
}

MegaTransferList *MegaApiImpl::getChildTransfers(int transferTag)
{
    sdkMutex.lock();

    if(transferMap.find(transferTag) == transferMap.end())
    {
        sdkMutex.unlock();
        return new MegaTransferListPrivate();
    }

    MegaTransfer *transfer = transferMap.at(transferTag);
    if(!transfer->isFolderTransfer())
    {
        sdkMutex.unlock();
        return new MegaTransferListPrivate();
    }

    vector<MegaTransfer *> transfers;
    for(std::map<int, MegaTransferPrivate *>::iterator it = transferMap.begin(); it != transferMap.end(); it++)
    {
        MegaTransferPrivate *t = it->second;
        if(t->getFolderTransferTag() == transferTag)
        {
            transfers.push_back(transfer);
        }
    }

    MegaTransferList *result = new MegaTransferListPrivate(transfers.data(), transfers.size());

    sdkMutex.unlock();
    return result;
}

void MegaApiImpl::startUpload(const char *localPath, MegaNode *parent, const char *fileName, int64_t mtime, int folderTransferTag, MegaTransferListener *listener)
{
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

    transfer->setMaxRetries(maxRetries);

    if(fileName)
    {
        transfer->setFileName(fileName);
    }

    transfer->setTime(mtime);

    if(folderTransferTag)
    {
        transfer->setFolderTransferTag(folderTransferTag);
    }

	transferQueue.push(transfer);
    waiter->notify();
}

void MegaApiImpl::startUpload(const char* localPath, MegaNode* parent, MegaTransferListener *listener)
{ return startUpload(localPath, parent, (const char *)NULL, -1, 0, listener); }

void MegaApiImpl::startUpload(const char *localPath, MegaNode *parent, int64_t mtime, MegaTransferListener *listener)
{ return startUpload(localPath, parent, (const char *)NULL, mtime, 0, listener); }

void MegaApiImpl::startUpload(const char* localPath, MegaNode* parent, const char* fileName, MegaTransferListener *listener)
{ return startUpload(localPath, parent, fileName, -1, 0, listener); }

void MegaApiImpl::startDownload(MegaNode *node, const char* localPath, long startPos, long endPos, MegaTransferListener *listener)
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
        if((c=='/') || (c == '\\')) transfer->setParentPath(localPath);
        else transfer->setPath(localPath);
    }

    if(node)
    {
        transfer->setNodeHandle(node->getHandle());
        if(node->isPublic())
            transfer->setPublicNode(node);
    }
	transfer->setStartPos(startPos);
	transfer->setEndPos(endPos);
	transfer->setMaxRetries(maxRetries);

	transferQueue.push(transfer);
	waiter->notify();
}

void MegaApiImpl::startDownload(MegaNode *node, const char* localFolder, MegaTransferListener *listener)
{ startDownload(node, localFolder, 0, 0, listener); }

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
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::startStreaming(MegaNode* node, m_off_t startPos, m_off_t size, MegaTransferListener *listener)
{
	MegaTransferPrivate* transfer = new MegaTransferPrivate(MegaTransfer::TYPE_DOWNLOAD, listener);
	if(node && !node->isPublic())
	{
		transfer->setNodeHandle(node->getHandle());
	}
	else
	{
		transfer->setPublicNode(node);
	}

	transfer->setStartPos(startPos);
	transfer->setEndPos(startPos + size - 1);
	transfer->setMaxRetries(maxRetries);
	transferQueue.push(transfer);
	waiter->notify();
}

#ifdef ENABLE_SYNC

//Move local files inside synced folders to the "Rubbish" folder.
bool MegaApiImpl::moveToLocalDebris(const char *path)
{
    sdkMutex.lock();

    string utf8path = path;
#if defined(_WIN32) && !defined(WINDOWS_PHONE)
        if(!PathIsRelativeA(utf8path.c_str()) && ((utf8path.size()<2) || utf8path.compare(0, 2, "\\\\")))
            utf8path.insert(0, "\\\\?\\");
#endif

    string localpath;
    fsAccess->path2local(&utf8path, &localpath);

    Sync *sync = NULL;
    for (sync_list::iterator it = client->syncs.begin(); it != client->syncs.end(); it++)
    {
        string *localroot = &((*it)->localroot.localname);
        if(((localroot->size()+fsAccess->localseparator.size())<localpath.size()) &&
            !memcmp(localroot->data(), localpath.data(), localroot->size()) &&
            !memcmp(fsAccess->localseparator.data(), localpath.data()+localroot->size(), fsAccess->localseparator.size()))
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

    bool result = sync->movetolocaldebris(&localpath);
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
#endif

    int state = MegaApi::STATE_NONE;
    sdkMutex.lock();
    for (sync_list::iterator it = client->syncs.begin(); it != client->syncs.end(); it++)
    {
        Sync *sync = (*it);
        unsigned int ssize = sync->localroot.localname.size();
        if(path->size() < ssize || memcmp(path->data(), sync->localroot.localname.data(), ssize))
            continue;

        if(path->size() == ssize)
        {
            state = sync->localroot.ts;
            break;
        }
        else if(!memcmp(path->data()+ssize, client->fsaccess->localseparator.data(), client->fsaccess->localseparator.size()))
        {
            LocalNode* l = sync->localnodebypath(NULL, path);
            if(l)
                state = l->ts;
            else
                state = MegaApi::STATE_IGNORED;
            break;
        }
    }
    sdkMutex.unlock();
    return state;
}


MegaNode *MegaApiImpl::getSyncedNode(string *path)
{
    sdkMutex.lock();
    MegaNode *node = NULL;
    for (sync_list::iterator it = client->syncs.begin(); (it != client->syncs.end()) && (node == NULL); it++)
    {
        Sync *sync = (*it);
        if(path->size() == sync->localroot.localname.size() &&
                !memcmp(path->data(), sync->localroot.localname.data(), path->size()))
        {
            node = MegaNodePrivate::fromNode(sync->localroot.node);
            break;
        }

        LocalNode * localNode = sync->localnodebypath(NULL, path);
        if(localNode) node = MegaNodePrivate::fromNode(localNode->node);
    }
    sdkMutex.unlock();
    return node;
}

void MegaApiImpl::syncFolder(const char *localFolder, MegaNode *megaFolder, MegaRequestListener *listener)
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
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::resumeSync(const char *localFolder, long long localfp, MegaNode *megaFolder, MegaRequestListener* listener)
{
    sdkMutex.lock();

#ifdef __APPLE__
    localfp = 0;
#endif

    LOG_debug << "Resume sync";

    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_ADD_SYNC);
    request->setListener(listener);
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
    request->setNumber(localfp);

    int nextTag = client->nextreqtag();
    request->setTag(nextTag);
    requestMap[nextTag]=request;
    error e = API_OK;
    fireOnRequestStart(request);

    const char *localPath = request->getFile();
    Node *node = client->nodebyhandle(request->getNodeHandle());
    if(!node || (node->type==FILENODE) || !localPath)
    {
        e = API_EARGS;
    }
    else
    {
        string utf8name(localPath);
        string localname;
        client->fsaccess->path2local(&utf8name, &localname);
        e = client->addsync(&localname, DEBRISFOLDER, NULL, node, localfp, -nextTag);
        if(!e)
        {
            MegaSyncPrivate *sync = new MegaSyncPrivate(client->syncs.back());
            sync->setListener(request->getSyncListener());
            syncMap[-nextTag] = sync;

            request->setNumber(client->syncs.back()->fsfp);
        }
    }

    fireOnRequestFinish(request, MegaError(e));
    sdkMutex.unlock();
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
    int num = client->syncs.size();
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
    if(!excludedNames)
    {
        this->excludedNames.clear();
        sdkMutex.unlock();
        return;
    }

    for(unsigned int i=0; i<excludedNames->size(); i++)
    {
        LOG_debug << "Excluded name: " << excludedNames->at(i);
    }

    this->excludedNames = *excludedNames;
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
    node->localnode->getlocalpath(&result, true);
    result.append("", 1);
    sdkMutex.unlock();
    return result;
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
}

void MegaApiImpl::resetTotalUploads()
{
    totalUploads = 0;
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

bool MegaApiImpl::userComparatorDefaultASC (User *i, User *j)
{
	if(strcasecmp(i->email.c_str(), j->email.c_str())<=0) return 1;
    return 0;
}

char *MegaApiImpl::escapeFsIncompatible(const char *filename)
{
    if(!filename)
    {
        return NULL;
    }
    string name = filename;
    client->fsaccess->escapefsincompatible(&name);
    return MegaApi::strdup(name.c_str());
}

char *MegaApiImpl::unescapeFsIncompatible(const char *name)
{
    if(!name)
    {
        return NULL;
    }
    string filename = name;
    client->fsaccess->unescapefsincompatible(&filename);
    return MegaApi::strdup(filename.c_str());
}

bool MegaApiImpl::createThumbnail(const char *imagePath, const char *dstPath)
{
    if (!gfxAccess)
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
    bool result = gfxAccess->savefa(&localImagePath, GfxProc::THUMBNAIL120X120, &localDstPath);
    sdkMutex.unlock();

    return result;
}

bool MegaApiImpl::createPreview(const char *imagePath, const char *dstPath)
{
    if (!gfxAccess)
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
    bool result = gfxAccess->savefa(&localImagePath, GfxProc::PREVIEW1000x1000, &localDstPath);
    sdkMutex.unlock();

    return result;
}

bool MegaApiImpl::isOnline()
{
    return !client->httpio->noinetds;
}

MegaUserList* MegaApiImpl::getContacts()
{
    sdkMutex.lock();

	vector<User*> vUsers;
	for (user_map::iterator it = client->users.begin() ; it != client->users.end() ; it++ )
	{
		User *u = &(it->second);
        vector<User *>::iterator i = std::lower_bound(vUsers.begin(), vUsers.end(), u, MegaApiImpl::userComparatorDefaultASC);
		vUsers.insert(i, u);
	}
    MegaUserList *userList = new MegaUserListPrivate(vUsers.data(), vUsers.size());

    sdkMutex.unlock();

	return userList;
}


MegaUser* MegaApiImpl::getContact(const char* email)
{
    sdkMutex.lock();
	MegaUser *user = MegaUserPrivate::fromUser(client->finduser(email, 0));
    sdkMutex.unlock();
	return user;
}


MegaNodeList* MegaApiImpl::getInShares(MegaUser *megaUser)
{
    if(!megaUser) return new MegaNodeListPrivate();

    sdkMutex.lock();
    vector<Node*> vNodes;
    User *user = client->finduser(megaUser->getEmail(), 0);
    if(!user)
    {
        sdkMutex.unlock();
        return new MegaNodeListPrivate();
    }

	for (handle_set::iterator sit = user->sharing.begin(); sit != user->sharing.end(); sit++)
	{
        Node *n;
        if ((n = client->nodebyhandle(*sit)) && !n->parent)
            vNodes.push_back(n);
	}
    MegaNodeList *nodeList;
    if(vNodes.size()) nodeList = new MegaNodeListPrivate(vNodes.data(), vNodes.size());
    else nodeList = new MegaNodeListPrivate();

    sdkMutex.unlock();
	return nodeList;
}

MegaNodeList* MegaApiImpl::getInShares()
{
    sdkMutex.lock();

    vector<Node*> vNodes;
	for(user_map::iterator it = client->users.begin(); it != client->users.end(); it++)
	{
		User *user = &(it->second);
		Node *n;

		for (handle_set::iterator sit = user->sharing.begin(); sit != user->sharing.end(); sit++)
		{
            if ((n = client->nodebyhandle(*sit)) && !n->parent)
				vNodes.push_back(n);
		}
	}

    MegaNodeList *nodeList = new MegaNodeListPrivate(vNodes.data(), vNodes.size());
    sdkMutex.unlock();
	return nodeList;
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

MegaShareList *MegaApiImpl::getOutShares()
{
    sdkMutex.lock();

    OutShareProcessor shareProcessor;
    processTree(client->nodebyhandle(client->rootnodes[0]), &shareProcessor, true);
    MegaShareList *shareList = new MegaShareListPrivate(shareProcessor.getShares().data(), shareProcessor.getHandles().data(), shareProcessor.getShares().size());

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

    if(!node->outshares)
    {
        sdkMutex.unlock();
        return new MegaShareListPrivate();
    }

	vector<Share*> vShares;
	vector<handle> vHandles;

    for (share_map::iterator it = node->outshares->begin(); it != node->outshares->end(); it++)
	{
		vShares.push_back(it->second);
		vHandles.push_back(node->nodehandle);
	}

    MegaShareList *shareList = new MegaShareListPrivate(vShares.data(), vHandles.data(), vShares.size());
    sdkMutex.unlock();
    return shareList;
}

MegaShareList *MegaApiImpl::getPendingOutShares()
{
    sdkMutex.lock();

    PendingOutShareProcessor shareProcessor;
    processTree(client->nodebyhandle(client->rootnodes[0]), &shareProcessor, true);
    MegaShareList *shareList = new MegaShareListPrivate(shareProcessor.getShares().data(), shareProcessor.getHandles().data(), shareProcessor.getShares().size());

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

    MegaShareList *shareList = new MegaShareListPrivate(vShares.data(), vHandles.data(), vShares.size());
    sdkMutex.unlock();
    return shareList;
}

MegaContactRequestList *MegaApiImpl::getIncomingContactRequests()
{
    sdkMutex.lock();
    vector<PendingContactRequest*> vContactRequests;
    for (handlepcr_map::iterator it = client->pcrindex.begin(); it != client->pcrindex.end(); it++)
    {
        if(!it->second->isoutgoing)
        {
            vContactRequests.push_back(it->second);
        }
    }

    MegaContactRequestList *requestList = new MegaContactRequestListPrivate(vContactRequests.data(), vContactRequests.size());
    sdkMutex.unlock();

    return requestList;
}

MegaContactRequestList *MegaApiImpl::getOutgoingContactRequests()
{
    sdkMutex.lock();
    vector<PendingContactRequest*> vContactRequests;
    for (handlepcr_map::iterator it = client->pcrindex.begin(); it != client->pcrindex.end(); it++)
    {
        if(it->second->isoutgoing)
        {
            vContactRequests.push_back(it->second);
        }
    }

    MegaContactRequestList *requestList = new MegaContactRequestListPrivate(vContactRequests.data(), vContactRequests.size());
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

bool MegaApiImpl::processMegaTree(MegaNode* n, MegaTreeProcessor* processor, bool recursive)
{
	if(!n) return true;
	if(!processor) return false;

    sdkMutex.lock();
	Node *node = client->nodebyhandle(n->getHandle());
	if(!node)
	{
        sdkMutex.unlock();
		return true;
	}

	if (node->type != FILENODE)
	{
		for (node_list::iterator it = node->children.begin(); it != node->children.end(); )
		{
			MegaNode *megaNode = MegaNodePrivate::fromNode(*it++);
			if(recursive)
			{
				if(!processMegaTree(megaNode,processor))
				{
					delete megaNode;
                    sdkMutex.unlock();
					return 0;
				}
			}
			else
			{
				if(!processor->processMegaNode(megaNode))
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

MegaNode *MegaApiImpl::createPublicFileNode(MegaHandle handle, const char *key, const char *name, m_off_t size, m_off_t mtime, MegaHandle parentHandle, const char* auth)
{
    string nodekey;
    string attrstring;
    nodekey.resize(strlen(key) * 3 / 4 + 3);
    nodekey.resize(Base64::atob(key, (byte *)nodekey.data(), nodekey.size()));
    return new MegaNodePrivate(name, FILENODE, size, mtime, mtime, handle, &nodekey, &attrstring, parentHandle, auth);
}

MegaNode *MegaApiImpl::createPublicFolderNode(MegaHandle handle, const char *name, MegaHandle parentHandle, const char *auth)
{
    string nodekey;
    string attrstring;
    return new MegaNodePrivate(name, FOLDERNODE, 0, 0, 0, handle, &nodekey, &attrstring, parentHandle, auth);
}

void MegaApiImpl::loadBalancing(const char* service, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_LOAD_BALANCING, listener);
    request->setName(service);
    requestQueue.push(request);
    waiter->notify();
}

const char *MegaApiImpl::getVersion()
{
    return client->version();
}

const char *MegaApiImpl::getUserAgent()
{
    return client->useragent.c_str();
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

bool MegaApiImpl::processTree(Node* node, TreeProcessor* processor, bool recursive)
{
	if(!node) return 1;
	if(!processor) return 0;

    sdkMutex.lock();
	node = client->nodebyhandle(node->nodehandle);
	if(!node)
	{
        sdkMutex.unlock();
		return 1;
	}

	if (node->type != FILENODE)
	{
		for (node_list::iterator it = node->children.begin(); it != node->children.end(); )
		{
			if(recursive)
			{
				if(!processTree(*it++,processor))
				{
                    sdkMutex.unlock();
					return 0;
				}
			}
			else
			{
				if(!processor->processNode(*it++))
				{
                    sdkMutex.unlock();
					return 0;
				}
			}
		}
	}
	bool result = processor->processNode(node);

    sdkMutex.unlock();
	return result;
}

MegaNodeList* MegaApiImpl::search(MegaNode* n, const char* searchString, bool recursive)
{
    if(!n || !searchString) return new MegaNodeListPrivate();
    sdkMutex.lock();
	Node *node = client->nodebyhandle(n->getHandle());
	if(!node)
	{
        sdkMutex.unlock();
        return new MegaNodeListPrivate();
	}

	SearchTreeProcessor searchProcessor(searchString);
	processTree(node, &searchProcessor, recursive);
    vector<Node *>& vNodes = searchProcessor.getResults();

    MegaNodeList *nodeList;
    if(vNodes.size()) nodeList = new MegaNodeListPrivate(vNodes.data(), vNodes.size());
    else nodeList = new MegaNodeListPrivate();

    sdkMutex.unlock();

    return nodeList;
}

long long MegaApiImpl::getSize(MegaNode *n)
{
    if(!n) return 0;

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

    string path = filePath;
    string localpath;
    fsAccess->path2local(&path, &localpath);

    FileAccess *fa = fsAccess->newfileaccess();
    if(!fa->fopen(&localpath, true, false))
        return NULL;

    FileFingerprint fp;
    fp.genfingerprint(fa);
    m_off_t size = fa->size;
    delete fa;
    if(fp.size < 0)
        return NULL;

    string fingerprint;
    fp.serializefingerprint(&fingerprint);

    char bsize[sizeof(size)+1];
    int l = Serialize64::serialize((byte *)bsize, size);
    char *buf = new char[l * 4 / 3 + 4];
    char ssize = 'A' + Base64::btoa((const byte *)bsize, l, buf);

    string result(1, ssize);
    result.append(buf);
    result.append(fingerprint);
    delete [] buf;

    return MegaApi::strdup(result.c_str());
}

char *MegaApiImpl::getFingerprint(MegaNode *n)
{
    if(!n) return NULL;

    sdkMutex.lock();
    Node *node = client->nodebyhandle(n->getHandle());
    if(!node || node->type != FILENODE || node->size < 0 || !node->isvalid)
    {
        sdkMutex.unlock();
        return NULL;
    }

    string fingerprint;
    node->serializefingerprint(&fingerprint);
    m_off_t size = node->size;
    sdkMutex.unlock();

    char bsize[sizeof(size)+1];
    int l = Serialize64::serialize((byte *)bsize, size);
    char *buf = new char[l * 4 / 3 + 4];
    char ssize = 'A' + Base64::btoa((const byte *)bsize, l, buf);
    string result(1, ssize);
    result.append(buf);
    result.append(fingerprint);
    delete [] buf;

    return MegaApi::strdup(result.c_str());
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
    char ssize = 'A' + Base64::btoa((const byte *)bsize, l, buf);

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

    string path = filePath;
    string localpath;
    fsAccess->path2local(&path, &localpath);

    FileAccess *fa = fsAccess->newfileaccess();
    if(!fa->fopen(&localpath, true, false))
        return NULL;

    FileFingerprint fp;
    fp.genfingerprint(fa);
    delete fa;
    if(fp.size < 0)
        return NULL;

    string result;
    result.resize((sizeof fp.crc) * 4 / 3 + 4);
    result.resize(Base64::btoa((const byte *)fp.crc, sizeof fp.crc, (char*)result.c_str()));
    return MegaApi::strdup(result.c_str());
}

char *MegaApiImpl::getCRCFromFingerprint(const char *fingerprint)
{
    if(!fingerprint || !fingerprint[0]) return NULL;
    
    m_off_t size = 0;
    unsigned int fsize = strlen(fingerprint);
    unsigned int ssize = fingerprint[0] - 'A';
    if(ssize > (sizeof(size) * 4 / 3 + 4) || fsize <= (ssize + 1))
        return NULL;
    
    int len =  sizeof(size) + 1;
    byte *buf = new byte[len];
    Base64::atob(fingerprint + 1, buf, len);
    int l = Serialize64::unserialize(buf, len, (uint64_t *)&size);
    delete [] buf;
    if(l <= 0)
        return NULL;
    
    string sfingerprint = fingerprint + ssize + 1;
    
    FileFingerprint fp;
    if(!fp.unserializefingerprint(&sfingerprint))
    {
        return NULL;
    }
    
    string result;
    result.resize((sizeof fp.crc) * 4 / 3 + 4);
    result.resize(Base64::btoa((const byte *)fp.crc, sizeof fp.crc,(char*)result.c_str()));
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
    result.resize(Base64::btoa((const byte *)node->crc, sizeof node->crc, (char*)result.c_str()));

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
        if(!memcmp(child->crc, binarycrc, sizeof(node->crc)))
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
	if(!node) return true;
	if(!search) return false;

	if(strcasestr(node->displayname(), search)!=NULL)
		results.push_back(node);

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

void MegaApiImpl::transfer_added(Transfer *t)
{
	MegaTransferPrivate *transfer = currentTransfer;
    if(!transfer)
    {
        transfer = new MegaTransferPrivate(t->type);
        transfer->setSyncTransfer(true);
    }

	currentTransfer = NULL;
    transfer->setTransfer(t);
    transfer->setTotalBytes(t->size);
    transfer->setTag(t->tag);
	transferMap[t->tag]=transfer;

    if (t->type == GET)
    {
        totalDownloads++;
        pendingDownloads++;
    }
    else
    {
        totalUploads++;
        pendingUploads++;
    }

    fireOnTransferStart(transfer);
}

void MegaApiImpl::transfer_removed(Transfer *t)
{
    if(transferMap.find(t->tag) == transferMap.end()) return;
    MegaTransferPrivate* transfer = transferMap.at(t->tag);
    if(!transfer)
    {
        return;
    }

    if (t->type == GET)
    {
        if(pendingDownloads > 0)
            pendingDownloads--;

        if(totalDownloads > 0)
            totalDownloads--;
    }
    else
    {
        if(pendingUploads > 0)
            pendingUploads--;

        if(totalUploads > 0)
            totalUploads--;
    }

    fireOnTransferFinish(transfer, MegaError(transfer->getLastErrorCode()));
}

void MegaApiImpl::transfer_prepare(Transfer *t)
{
    if(transferMap.find(t->tag) == transferMap.end()) return;
    MegaTransferPrivate* transfer = transferMap.at(t->tag);

	if (t->type == GET)
		transfer->setNodeHandle(t->files.back()->h);

    string path;
    fsAccess->local2path(&(t->files.back()->localname), &path);
    transfer->setPath(path.c_str());
    transfer->setTotalBytes(t->size);

    LOG_info << "Transfer (" << transfer->getTransferString() << ") starting. File: " << transfer->getFileName();
}

void MegaApiImpl::transfer_update(Transfer *tr)
{
    if(transferMap.find(tr->tag) == transferMap.end()) return;
    MegaTransferPrivate* transfer = transferMap.at(tr->tag);
    if(!transfer)
    {
        return;
    }

    if(tr->slot)
    {
        if((transfer->getUpdateTime() != Waiter::ds) || !tr->slot->progressreported || (tr->slot->progressreported == tr->size))
        {
            if(!transfer->getStartTime())
            {
                transfer->setStartTime(Waiter::ds);
            }

            m_off_t deltaSize = tr->slot->progressreported - transfer->getTransferredBytes();
            transfer->setDeltaSize(deltaSize);

            dstime currentTime = Waiter::ds;
            long long speed = 0;
            if(tr->type == GET)
            {
                totalDownloadedBytes += deltaSize;

                while(downloadBytes.size())
                {
                    dstime deltaTime = currentTime - downloadTimes[0];
                    if(deltaTime <= 50)
                    {
                        break;
                    }

                    downloadPartialBytes -= downloadBytes[0];
                    downloadBytes.erase(downloadBytes.begin());
                    downloadTimes.erase(downloadTimes.begin());
                }

                downloadBytes.push_back(deltaSize);
                downloadTimes.push_back(currentTime);
                downloadPartialBytes += deltaSize;

                downloadSpeed = (downloadPartialBytes * 10) / 50;
                speed = downloadSpeed;
            }
            else
            {
                totalUploadedBytes += deltaSize;

                while(uploadBytes.size())
                {
                    dstime deltaTime = currentTime - uploadTimes[0];
                    if(deltaTime <= 50)
                    {
                        break;
                    }

                    uploadPartialBytes -= uploadBytes[0];
                    uploadBytes.erase(uploadBytes.begin());
                    uploadTimes.erase(uploadTimes.begin());
                }

                uploadBytes.push_back(deltaSize);
                uploadTimes.push_back(currentTime);
                uploadPartialBytes += deltaSize;

                uploadSpeed = (uploadPartialBytes * 10) / 50;
                speed = uploadSpeed;
            }

            transfer->setTransferredBytes(tr->slot->progressreported);

            if(currentTime < transfer->getStartTime())
                transfer->setStartTime(currentTime);

            transfer->setSpeed(speed);
            transfer->setUpdateTime(currentTime);

            fireOnTransferUpdate(transfer);
        }
	}
}

void MegaApiImpl::transfer_failed(Transfer* tr, error e)
{
    if(transferMap.find(tr->tag) == transferMap.end()) return;
    MegaError megaError(e);
    MegaTransferPrivate* transfer = transferMap.at(tr->tag);
    transfer->setUpdateTime(Waiter::ds);
    transfer->setDeltaSize(0);
    transfer->setSpeed(0);
    transfer->setLastErrorCode(e);
    fireOnTransferTemporaryError(transfer, megaError);
}

void MegaApiImpl::transfer_limit(Transfer* t)
{
    if(transferMap.find(t->tag) == transferMap.end()) return;
    MegaTransferPrivate* transfer = transferMap.at(t->tag);
    transfer->setUpdateTime(Waiter::ds);
    transfer->setDeltaSize(0);
    transfer->setSpeed(0);
    fireOnTransferTemporaryError(transfer, MegaError(API_EOVERQUOTA));
}

void MegaApiImpl::transfer_complete(Transfer* tr)
{
    if(transferMap.find(tr->tag) == transferMap.end()) return;
    MegaTransferPrivate* transfer = transferMap.at(tr->tag);

    dstime currentTime = Waiter::ds;
    if(!transfer->getStartTime())
        transfer->setStartTime(currentTime);
    if(currentTime<transfer->getStartTime())
        transfer->setStartTime(currentTime);

    transfer->setUpdateTime(currentTime);

    if(tr->size != transfer->getTransferredBytes())
    {
        long long speed = 0;
        long long deltaTime = currentTime-transfer->getStartTime();
        if(deltaTime<=0)
            deltaTime = 1;
        if(transfer->getTotalBytes()>0)
            speed = (10*transfer->getTotalBytes())/deltaTime;

        transfer->setSpeed(speed);
        transfer->setDeltaSize(tr->size - transfer->getTransferredBytes());
        if(tr->type == GET)
            totalDownloadedBytes += transfer->getDeltaSize();
        else
            totalUploadedBytes += transfer->getDeltaSize();

        transfer->setTransferredBytes(tr->size);
    }

    if (tr->type == GET)
    {
        if(pendingDownloads > 0)
            pendingDownloads--;

        string path;
        fsAccess->local2path(&tr->localfilename, &path);
        transfer->setPath(path.c_str());

        fireOnTransferFinish(transfer, MegaError(API_OK));
    }
    else
    {
        if(tr->size != transfer->getTransferredBytes())
        {
            fireOnTransferUpdate(transfer);
        }
    }
}

dstime MegaApiImpl::pread_failure(error e, int retry, void* param)
{
	MegaTransferPrivate *transfer = (MegaTransferPrivate *)param;
	transfer->setUpdateTime(Waiter::ds);
	transfer->setDeltaSize(0);
	transfer->setSpeed(0);
	transfer->setLastBytes(NULL);
	if (retry < transfer->getMaxRetries())
	{
        fireOnTransferTemporaryError(transfer, MegaError(e));
		return (dstime)(retry*10);
	}
	else
	{
        fireOnTransferFinish(transfer, MegaError(e));
		return ~(dstime)0;
	}
}

bool MegaApiImpl::pread_data(byte *buffer, m_off_t len, m_off_t, void* param)
{
	MegaTransferPrivate *transfer = (MegaTransferPrivate *)param;
	transfer->setUpdateTime(Waiter::ds);
    transfer->setLastBytes((char *)buffer);
    transfer->setDeltaSize(len);
    totalDownloadedBytes += len;
	transfer->setTransferredBytes(transfer->getTransferredBytes()+len);

	bool end = (transfer->getTransferredBytes() == transfer->getTotalBytes());
    fireOnTransferUpdate(transfer);
    if(!fireOnTransferData(transfer) || end)
	{
        fireOnTransferFinish(transfer, end ? MegaError(API_OK) : MegaError(API_EINCOMPLETE));
		return end;
	}
    return true;
}

void MegaApiImpl::reportevent_result(error e)
{
    MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_REPORT_EVENT)) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::loadbalancing_result(string *servers, error e)
{
    MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_LOAD_BALANCING)) return;

    if(!e)
    {
        request->setText(servers->c_str());
    }
    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::sessions_killed(handle, error e)
{
    MegaError megaError(e);

    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_KILL_SESSION)) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::cleanrubbishbin_result(error e)
{
    MegaError megaError(e);

    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CLEAN_RUBBISH_BIN)) return;

    fireOnRequestFinish(request, megaError);
}

#ifdef ENABLE_SYNC
void MegaApiImpl::syncupdate_state(Sync *sync, syncstate_t newstate)
{
    LOG_debug << "Sync state change: " << newstate << " Path: " << sync->localroot.name;
    client->abortbackoff(false);

    if(newstate == SYNC_FAILED)
    {
        MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_ADD_SYNC);

        if(sync->localroot.node)
        {
            request->setNodeHandle(sync->localroot.node->nodehandle);
        }

        int nextTag = client->nextreqtag();
        request->setTag(nextTag);
        requestMap[nextTag]=request;
        fireOnRequestFinish(request, MegaError(sync->errorcode));
    }

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);
    megaSync->setState(newstate);

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

void MegaApiImpl::syncupdate_local_folder_addition(Sync *sync, LocalNode *localNode, const char* path)
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

    string local;
    string path;
    localNode->getlocalpath(&local, true);
    fsAccess->local2path(&local, &path);
    LOG_debug << "Sync - local folder deletion detected: " << path.c_str();

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);


    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_LOCAL_FOLDER_DELETION);
    event->setPath(path.c_str());
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_local_file_addition(Sync *sync, LocalNode *localNode, const char* path)
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

    string local;
    string path;
    localNode->getlocalpath(&local, true);
    fsAccess->local2path(&local, &path);
    LOG_debug << "Sync - local file deletion detected: " << path.c_str();

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_LOCAL_FILE_DELETION);
    event->setPath(path.c_str());
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_local_file_change(Sync *sync, LocalNode *localNode, const char* path)
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

    string local;
    string path;
    localNode->getlocalpath(&local, true);
    fsAccess->local2path(&local, &path);
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

void MegaApiImpl::syncupdate_put(Sync *sync, LocalNode *localNode, const char *path)
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
    LOG_debug << "Sync - remote file addition detected " << n->displayname();
    client->abortbackoff(false);

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_REMOTE_FILE_ADDITION);
    event->setNodeHandle(n->nodehandle);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_remote_file_deletion(Sync *sync, Node *n)
{
    LOG_debug << "Sync - remote file deletion detected " << n->displayname();
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
    string local;
    string path;
    l->getlocalpath(&local, true);
    fsAccess->local2path(&local, &path);

    if(syncMap.find(l->sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(l->sync->tag);

    fireOnFileSyncStateChanged(megaSync, path.data(), (int)l->ts);
}

bool MegaApiImpl::sync_syncable(Node *node)
{
    if(node->type == FILENODE && !is_syncable(node->size))
    {
        return false;
    }

    const char *name = node->displayname();
    sdkMutex.unlock();
    bool result = is_syncable(name);
    sdkMutex.lock();
    return result;
}

bool MegaApiImpl::sync_syncable(const char *name, string *localpath, string *)
{
    static FileAccess* f = fsAccess->newfileaccess();
    if(f->fopen(localpath) && !is_syncable(f->size))
    {
        return false;
    }

    sdkMutex.unlock();
    bool result =  is_syncable(name);
    sdkMutex.lock();
    return result;
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

    this->waiting = waiting;
    this->fireOnGlobalSyncStateChanged();
}
#endif


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
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_RENAME)) return;

	request->setNodeHandle(h);
    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::rename_result(handle h, error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_MOVE)) return;

    request->setNodeHandle(h);
    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::unlink_result(handle h, error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_REMOVE)) return;

    request->setNodeHandle(h);
    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::fetchnodes_result(error e)
{
    MegaError megaError(e);
    MegaRequestPrivate* request;
    if (!client->restag)
    {
        request = new MegaRequestPrivate(MegaRequest::TYPE_FETCH_NODES);
        fireOnRequestFinish(request, megaError);
        return;
    }

    if (requestMap.find(client->restag) == requestMap.end())
    {
        return;
    }
    request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_FETCH_NODES))
    {
        return;
    }

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::putnodes_result(error e, targettype_t t, NewNode* nn)
{
    handle h = UNDEF;
    Node *n = NULL;

    if(!e && t != USER_HANDLE)
    {
        if(client->nodenotify.size())
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

	MegaError megaError(e);
    if(transferMap.find(client->restag) != transferMap.end())
    {
        MegaTransferPrivate* transfer = transferMap.at(client->restag);
        if(transfer->getType() == MegaTransfer::TYPE_DOWNLOAD)
        {
            return;
        }

        if(pendingUploads > 0)
        {
            pendingUploads--;
        }

        transfer->setNodeHandle(h);
        fireOnTransferFinish(transfer, megaError);
        delete [] nn;
        return;
    }

	if(requestMap.find(client->restag) == requestMap.end()) return;
	MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_IMPORT_LINK) &&
                    (request->getType() != MegaRequest::TYPE_CREATE_FOLDER) &&
                    (request->getType() != MegaRequest::TYPE_COPY))) return;

	request->setNodeHandle(h);
    fireOnRequestFinish(request, megaError);
	delete [] nn;
}

void MegaApiImpl::share_result(error e)
{
	MegaError megaError(e);

    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_EXPORT) &&
                    (request->getType() != MegaRequest::TYPE_SHARE))) return;

    //exportnode_result will be called to end the request.
	if(request->getType() == MegaRequest::TYPE_EXPORT)
		return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::share_result(int, error)
{
    //The other callback will be called at the end of the request
}

void MegaApiImpl::setpcr_result(handle h, error e, opcactions_t action)
{
    MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || request->getType() != MegaRequest::TYPE_INVITE_CONTACT) return;

    if (e)
    {
        LOG_debug << "Outgoing pending contact request failed (" << megaError.getErrorString() << ")";
    }
    else
    {
        if (h == UNDEF)
        {
            // must have been deleted
            LOG_debug << "Outgoing pending contact request " << (action == OPCA_DELETE ? "deleted" : "reminded") << " successfully";
        }
        else
        {
            char buffer[12];
            Base64::btoa((byte*)&h, sizeof(h), buffer);
            LOG_debug << "Outgoing pending contact request succeeded, id: " << buffer;
        }
    }

    request->setNodeHandle(h);
    request->setNumber(action);
    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::updatepcr_result(error e, ipcactions_t action)
{
    MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || request->getType() != MegaRequest::TYPE_REPLY_CONTACT_REQUEST) return;

    if (e)
    {
        LOG_debug << "Incoming pending contact request update failed (" << megaError.getErrorString() << ")";
    }
    else
    {
        string labels[3] = {"accepted", "denied", "ignored"};
        LOG_debug << "Incoming pending contact request successfully " << labels[(int)action];
    }

    request->setNumber(action);
    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::fa_complete(Node* n, fatype type, const char* data, uint32_t len)
{
    int tag = client->restag;
    while(tag)
    {
        if(requestMap.find(tag) == requestMap.end()) return;
        MegaRequestPrivate* request = requestMap.at(tag);
        if(!request || (request->getType() != MegaRequest::TYPE_GET_ATTR_FILE)) return;

        tag = request->getNumber();

        FileAccess *f = client->fsaccess->newfileaccess();
        string filePath(request->getFile());
        string localPath;
        fsAccess->path2local(&filePath, &localPath);

        totalDownloadedBytes += len;

        fsAccess->unlinklocal(&localPath);
        if(!f->fopen(&localPath, false, true))
        {
            delete f;
            fireOnRequestFinish(request, MegaError(API_EWRITE));
            continue;
        }

        if(!f->fwrite((const byte*)data, len, 0))
        {
            delete f;
            fireOnRequestFinish(request, MegaError(API_EWRITE));
            continue;
        }

        delete f;
        fireOnRequestFinish(request, MegaError(API_OK));
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

        tag = request->getNumber();
        if(retries >= 2)
        {
            fireOnRequestFinish(request, MegaError(e));
        }
        else
        {
            fireOnRequestTemporaryError(request, MegaError(e));
        }
    }

    return (retries >= 2);
}

void MegaApiImpl::putfa_result(handle, fatype, error e)
{
    MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || request->getType() != MegaRequest::TYPE_SET_ATTR_FILE)
        return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::putfa_result(handle, fatype, const char *)
{
    MegaError megaError(API_OK);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || request->getType() != MegaRequest::TYPE_SET_ATTR_FILE)
        return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::enumeratequotaitems_result(handle product, unsigned prolevel, unsigned gbstorage, unsigned gbtransfer, unsigned months, unsigned amount, const char* currency, const char* description, const char* iosid, const char* androidid)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_GET_PRICING) &&
                    (request->getType() != MegaRequest::TYPE_GET_PAYMENT_ID) &&
                    (request->getType() != MegaRequest::TYPE_UPGRADE_ACCOUNT)))
    {
        return;
    }

    request->addProduct(product, prolevel, gbstorage, gbtransfer, months, amount, currency, description, iosid, androidid);
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
        fireOnRequestFinish(request, MegaError(e));
    }
    else
    {
        MegaPricing *pricing = request->getPricing();
        int i;
        for(i = 0; i < pricing->getNumProducts(); i++)
        {
            if(pricing->getHandle(i) == request->getNodeHandle())
            {
                requestMap.erase(request->getTag());
                int nextTag = client->nextreqtag();
                request->setTag(nextTag);
                requestMap[nextTag]=request;
                client->purchase_additem(0, request->getNodeHandle(), pricing->getAmount(i),
                                         pricing->getCurrency(i), 0, NULL, NULL);
                break;
            }
        }

        if(i == pricing->getNumProducts())
        {
            fireOnRequestFinish(request, MegaError(API_ENOENT));
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
        fireOnRequestFinish(request, MegaError(e));
        return;
    }

    if(request->getType() == MegaRequest::TYPE_GET_PAYMENT_ID)
    {
        char saleid[16];
        Base64::btoa((byte *)&client->purchase_basket.back(), 8, saleid);
        request->setLink(saleid);
        client->purchase_begin();
        fireOnRequestFinish(request, MegaError(API_OK));
        return;
    }

    //MegaRequest::TYPE_UPGRADE_ACCOUNT
    int method = request->getNumber();
    client->purchase_checkout(method);
}

void MegaApiImpl::checkout_result(const char *errortype, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_UPGRADE_ACCOUNT)) return;

    if(!errortype)
    {
        fireOnRequestFinish(request, MegaError(e));
        return;
    }

    if(!strcmp(errortype, "FP"))
    {
        fireOnRequestFinish(request, MegaError(e - 100));
        return;
    }

    fireOnRequestFinish(request, MegaError(MegaError::PAYMENT_EGENERIC));
    return;
}

void MegaApiImpl::submitpurchasereceipt_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_SUBMIT_PURCHASE_RECEIPT)) return;

    fireOnRequestFinish(request, MegaError(e));
}

void MegaApiImpl::creditcardquerysubscriptions_result(int number, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CREDIT_CARD_QUERY_SUBSCRIPTIONS)) return;

    request->setNumber(number);
    fireOnRequestFinish(request, MegaError(e));
}

void MegaApiImpl::creditcardcancelsubscriptions_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CREDIT_CARD_CANCEL_SUBSCRIPTIONS)) return;

    fireOnRequestFinish(request, MegaError(e));
}
void MegaApiImpl::getpaymentmethods_result(int methods, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_GET_PAYMENT_METHODS)) return;

    request->setNumber(methods);
    fireOnRequestFinish(request, MegaError(e));
}

void MegaApiImpl::userfeedbackstore_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_SUBMIT_FEEDBACK)) return;

    fireOnRequestFinish(request, MegaError(e));
}

void MegaApiImpl::sendevent_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_SEND_EVENT)) return;

    fireOnRequestFinish(request, MegaError(e));
}

void MegaApiImpl::creditcardstore_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CREDIT_CARD_STORE)) return;

    fireOnRequestFinish(request, MegaError(e));
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

    fireOnRequestFinish(request, MegaError(e));
}

void MegaApiImpl::clearing()
{

}

void MegaApiImpl::notify_retry(dstime dsdelta)
{
#ifdef ENABLE_SYNC
    bool previousFlag = waitingRequest;
#endif

    if(!dsdelta)
        waitingRequest = false;
    else if(dsdelta > 10)
        waitingRequest = true;

#ifdef ENABLE_SYNC
    if(previousFlag != waitingRequest)
        fireOnGlobalSyncStateChanged();
#endif
}

// callback for non-EAGAIN request-level errors
// retrying is futile
// this can occur e.g. with syntactically malformed requests (due to a bug) or due to an invalid application key
void MegaApiImpl::request_error(error e)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_LOGOUT);
    request->setFlag(false);
    request->setParamType(e);

    if (e == API_ESSL && client->sslfakeissuer.size())
    {
        request->setText(client->sslfakeissuer.c_str());
    }

    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::request_response_progress(m_off_t currentProgress, m_off_t totalProgress)
{
    if(requestMap.size() == 1)
    {
        MegaRequestPrivate *request = requestMap.begin()->second;
        if(request && request->getType() == MegaRequest::TYPE_FETCH_NODES)
        {
            if(request->getTransferredBytes() != currentProgress)
            {
                request->setTransferredBytes(currentProgress);
                if(totalProgress != -1)
                {
                    request->setTotalBytes(totalProgress);
                }
                fireOnRequestUpdate(request);
            }
        }
    }
}

// login result
void MegaApiImpl::login_result(error result)
{
	MegaError megaError(result);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_LOGIN)) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::logout_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_LOGOUT)) return;

    if(!e)
    {
        requestMap.erase(request->getTag());

        error preverror = (error)request->getParamType();
        while(!requestMap.empty())
        {
            std::map<int,MegaRequestPrivate*>::iterator it=requestMap.begin();
            if(it->second) fireOnRequestFinish(it->second, MegaError(preverror ? preverror : API_EACCESS));
        }

        while(!transferMap.empty())
        {
            std::map<int, MegaTransferPrivate *>::iterator it=transferMap.begin();
            if(it->second) fireOnTransferFinish(it->second, MegaError(preverror ? preverror : API_EACCESS));
        }

        pendingUploads = 0;
        pendingDownloads = 0;
        totalUploads = 0;
        totalDownloads = 0;
        waiting = false;
        waitingRequest = false;
        excludedNames.clear();
        syncLowerSizeLimit = 0;
        syncUpperSizeLimit = 0;
        uploadSpeed = 0;
        downloadSpeed = 0;
        downloadTimes.clear();
        downloadBytes.clear();
        uploadTimes.clear();
        uploadBytes.clear();
        uploadPartialBytes = 0;
        downloadPartialBytes = 0;

        fireOnRequestFinish(request, MegaError(preverror));
        return;
    }
    fireOnRequestFinish(request,MegaError(e));
}

void MegaApiImpl::userdata_result(string *name, string* pubk, string* privk, handle bjid, error result)
{
    MegaError megaError(result);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_GET_USER_DATA)) return;

    if(result == API_OK)
    {
        char jid[16];
        Base32::btoa((byte *)&bjid, MegaClient::USERHANDLE, jid);

        request->setPassword(pubk->c_str());
        request->setPrivateKey(privk->c_str());
        request->setName(name->c_str());
        request->setText(jid);
    }
    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::pubkey_result(User *u)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_GET_USER_DATA)) return;

    if(!u)
    {
        fireOnRequestFinish(request, MegaError(API_ENOENT));
        return;
    }

    if(!u->pubk.isvalid())
    {
        fireOnRequestFinish(request, MegaError(API_EACCESS));
        return;
    }

    string key;
    u->pubk.serializekey(&key, AsymmCipher::PUBKEY);
    char pubkbuf[AsymmCipher::MAXKEYLENGTH * 4 / 3 + 4];
    Base64::btoa((byte *)key.data(), key.size(), pubkbuf);
    request->setPassword(pubkbuf);

    char jid[16];
    Base32::btoa((byte *)&u->userhandle, MegaClient::USERHANDLE, jid);
    request->setText(jid);

    if(u->email.size())
    {
        request->setEmail(u->email.c_str());
    }

    fireOnRequestFinish(request, MegaError(API_OK));
}

// password change result
void MegaApiImpl::changepw_result(error result)
{
	MegaError megaError(result);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || request->getType() != MegaRequest::TYPE_CHANGE_PW) return;

    fireOnRequestFinish(request, megaError);
}

// node export failed
void MegaApiImpl::exportnode_result(error result)
{
	MegaError megaError(result);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || request->getType() != MegaRequest::TYPE_EXPORT) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::exportnode_result(handle h, handle ph)
{
    Node* n;
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || request->getType() != MegaRequest::TYPE_EXPORT) return;

    if ((n = client->nodebyhandle(h)))
    {
        char node[9];
        char key[FILENODEKEYLENGTH*4/3+3];

        Base64::btoa((byte*)&ph,MegaClient::NODEHANDLE,node);

        // the key
        if (n->type == FILENODE)
        {
            if(n->nodekey.size()>=FILENODEKEYLENGTH)
                Base64::btoa((const byte*)n->nodekey.data(),FILENODEKEYLENGTH,key);
            else
                key[0]=0;
        }
        else if (n->sharekey) Base64::btoa(n->sharekey->key,FOLDERNODEKEYLENGTH,key);
        else
        {
            fireOnRequestFinish(request, MegaError(MegaError::API_EKEY));
            return;
        }

        string link = "https://mega.nz/#";
        link += (n->type ? "F" : "");
        link += "!";
        link += node;
        link += "!";
        link += key;
        request->setLink(link.c_str());
        fireOnRequestFinish(request, MegaError(MegaError::API_OK));
    }
    else
    {
        request->setNodeHandle(UNDEF);
        fireOnRequestFinish(request, MegaError(MegaError::API_ENOENT));
    }
}

// the requested link could not be opened
void MegaApiImpl::openfilelink_result(error result)
{
	MegaError megaError(result);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_IMPORT_LINK) &&
                    (request->getType() != MegaRequest::TYPE_GET_PUBLIC_NODE))) return;

    fireOnRequestFinish(request, megaError);
}

// the requested link was opened successfully
// (it is the application's responsibility to delete n!)
void MegaApiImpl::openfilelink_result(handle ph, const byte* key, m_off_t size, string* a, string*, int)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_IMPORT_LINK) &&
                    (request->getType() != MegaRequest::TYPE_GET_PUBLIC_NODE))) return;

	if (!client->loggedin() && (request->getType() == MegaRequest::TYPE_IMPORT_LINK))
	{
        fireOnRequestFinish(request, MegaError(MegaError::API_EACCESS));
		return;
	}

    string attrstring;
    string fileName;
    string keystring;

    attrstring.resize(a->length()*4/3+4);
    attrstring.resize(Base64::btoa((const byte *)a->data(),a->length(), (char *)attrstring.data()));

    m_time_t mtime = 0;

    SymmCipher nodeKey;
    keystring.assign((char*)key,FILENODEKEYLENGTH);
    nodeKey.setkey(key, FILENODE);

    byte *buf = Node::decryptattr(&nodeKey,attrstring.c_str(),attrstring.size());
    if(buf)
    {
        JSON json;
        nameid name;
        string* t;
        AttrMap attrs;

        json.begin((char*)buf+5);
        while ((name = json.getnameid()) != EOO && json.storeobject((t = &attrs.map[name])))
            JSON::unescape(t);

        delete[] buf;

        attr_map::iterator it;
        it = attrs.map.find('n');
        if (it == attrs.map.end()) fileName = "CRYPTO_ERROR";
        else if (!it->second.size()) fileName = "BLANK";
        else fileName = it->second.c_str();

        it = attrs.map.find('c');
        if(it != attrs.map.end())
        {
            FileFingerprint ffp;
            if(ffp.unserializefingerprint(&it->second))
            {
                mtime = ffp.mtime;
            }
        }
    }
    else fileName = "CRYPTO_ERROR";

	if(request->getType() == MegaRequest::TYPE_IMPORT_LINK)
	{
		NewNode* newnode = new NewNode[1];

		// set up new node as folder node
		newnode->source = NEW_PUBLIC;
		newnode->type = FILENODE;
		newnode->nodehandle = ph;
        newnode->parenthandle = UNDEF;
		newnode->nodekey.assign((char*)key,FILENODEKEYLENGTH);
        newnode->attrstring = new string(*a);

		// add node
        requestMap.erase(request->getTag());
        int nextTag = client->nextreqtag();
        request->setTag(nextTag);
        requestMap[nextTag]=request;
        client->putnodes(request->getParentHandle(), newnode, 1);
	}
	else
	{
        request->setPublicNode(new MegaNodePrivate(fileName.c_str(), FILENODE, size, 0, mtime, ph, &keystring, a));
        fireOnRequestFinish(request, MegaError(MegaError::API_OK));
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
    if(!count)
    {
        return;
    }

    MegaNodeList *nodeList = NULL;
    if(n != NULL)
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

	int numDetails = request->getNumDetails();
	numDetails--;
	request->setNumDetails(numDetails);
	if(!numDetails)
    {
        if(!request->getAccountDetails()->storage_max)
            fireOnRequestFinish(request, MegaError(MegaError::API_EACCESS));
        else
            fireOnRequestFinish(request, MegaError(MegaError::API_OK));
    }
}

void MegaApiImpl::account_details(AccountDetails*, error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_ACCOUNT_DETAILS)) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::invite_result(error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_ADD_CONTACT) &&
                    (request->getType() != MegaRequest::TYPE_REMOVE_CONTACT))) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::putua_result(error e)
{
    MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_SET_ATTR_USER)) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::getua_result(error e)
{
	MegaError megaError(e);
	if(requestMap.find(client->restag) == requestMap.end()) return;
	MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_GET_ATTR_USER)) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::getua_result(byte* data, unsigned len)
{
	if(requestMap.find(client->restag) == requestMap.end()) return;
	MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_GET_ATTR_USER)) return;

    if(request->getParamType() == 0)
    {
        FileAccess *f = client->fsaccess->newfileaccess();
        string filePath(request->getFile());
        string localPath;
        fsAccess->path2local(&filePath, &localPath);

        totalDownloadedBytes += len;

        fsAccess->unlinklocal(&localPath);
        if(!f->fopen(&localPath, false, true))
        {
            delete f;
            fireOnRequestFinish(request, MegaError(API_EWRITE));
            return;
        }

        if(!f->fwrite((const byte*)data, len, 0))
        {
            delete f;
            fireOnRequestFinish(request, MegaError(API_EWRITE));
            return;
        }

        delete f;
    }
    else
    {
        string str((const char*)data,len);
        request->setText(str.c_str());
    }
    fireOnRequestFinish(request, MegaError(API_OK));
}

// user attribute update notification
void MegaApiImpl::userattr_update(User*, int, const char*)
{

}

void MegaApiImpl::ephemeral_result(error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_CREATE_ACCOUNT))) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::ephemeral_result(handle, const byte*)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_CREATE_ACCOUNT))) return;

    requestMap.erase(request->getTag());
    int nextTag = client->nextreqtag();
    request->setTag(nextTag);
    requestMap[nextTag] = request;

	byte pwkey[SymmCipher::KEYLENGTH];
    if(!request->getPrivateKey())
		client->pw_key(request->getPassword(),pwkey);
	else
		Base64::atob(request->getPrivateKey(), (byte *)pwkey, sizeof pwkey);

    client->sendsignuplink(request->getEmail(),request->getName(),pwkey);
}

void MegaApiImpl::sendsignuplink_result(error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_CREATE_ACCOUNT))) return;

    requestMap.erase(request->getTag());
    while(!requestMap.empty())
    {
        std::map<int,MegaRequestPrivate*>::iterator it=requestMap.begin();
        if(it->second) fireOnRequestFinish(it->second, MegaError(MegaError::API_EACCESS));
    }

    while(!transferMap.empty())
    {
        std::map<int, MegaTransferPrivate *>::iterator it=transferMap.begin();
        if(it->second) fireOnTransferFinish(it->second, MegaError(MegaError::API_EACCESS));
    }

    client->locallogout();
    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::querysignuplink_result(error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_QUERY_SIGNUP_LINK) &&
                    (request->getType() != MegaRequest::TYPE_CONFIRM_ACCOUNT))) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::querysignuplink_result(handle, const char* email, const char* name, const byte* pwc, const byte*, const byte* c, size_t len)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_QUERY_SIGNUP_LINK) &&
                    (request->getType() != MegaRequest::TYPE_CONFIRM_ACCOUNT))) return;

	request->setEmail(email);
	request->setName(name);

	if(request->getType() == MegaRequest::TYPE_QUERY_SIGNUP_LINK)
	{
        fireOnRequestFinish(request, MegaError(API_OK));
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
        fireOnRequestFinish(request, MegaError(API_ENOENT));
	}
	else
	{
		// decrypt and set master key, then proceed with the confirmation
		pwcipher.ecb_decrypt(signupencryptedmasterkey);
		client->key.setkey(signupencryptedmasterkey);

        requestMap.erase(request->getTag());
        int nextTag = client->nextreqtag();
        request->setTag(nextTag);
        requestMap[nextTag] = request;

		client->confirmsignuplink((const byte*)signupcode.data(),signupcode.size(),MegaClient::stringhash64(&signupemail,&pwcipher));
	}
}

void MegaApiImpl::confirmsignuplink_result(error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::setkeypair_result(error e)
{

}

void MegaApiImpl::checkfile_result(handle h, error e)
{
    if(e)
    {
        for(std::map<int, MegaTransferPrivate *>::iterator iter = transferMap.begin(); iter != transferMap.end(); iter++)
        {
            MegaTransferPrivate *transfer = iter->second;
            if(transfer->getNodeHandle() == h)
                fireOnTransferTemporaryError(transfer, MegaError(e));
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
            if(transfer->getNodeHandle() == h)
                fireOnTransferTemporaryError(transfer, MegaError(e));
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

    std::map<int,MegaRequestPrivate*>::iterator it=requestMap.begin();
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
    sdkMutex.unlock();
}

void MegaApiImpl::removeGlobalListener(MegaGlobalListener* listener)
{
    if(!listener) return;

    sdkMutex.lock();
    globalListeners.erase(listener);
    sdkMutex.unlock();
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
	for(set<MegaRequestListener *>::iterator it = requestListeners.begin(); it != requestListeners.end() ; it++)
		(*it)->onRequestStart(api, request);

	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
		(*it)->onRequestStart(api, request);

	MegaRequestListener* listener = request->getListener();
	if(listener) listener->onRequestStart(api, request);
	activeRequest = NULL;
}


void MegaApiImpl::fireOnRequestFinish(MegaRequestPrivate *request, MegaError e)
{
	MegaError *megaError = new MegaError(e);
	activeRequest = request;
	activeError = megaError;

    if(e.getErrorCode())
    {
        LOG_warn << "Request (" << request->getRequestString() << ") finished with error: " << e.getErrorString();
    }
    else
    {
        LOG_info << "Request (" << request->getRequestString() << ") finished";
    }

	for(set<MegaRequestListener *>::iterator it = requestListeners.begin(); it != requestListeners.end() ; it++)
		(*it)->onRequestFinish(api, request, megaError);

	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
		(*it)->onRequestFinish(api, request, megaError);

	MegaRequestListener* listener = request->getListener();
	if(listener) listener->onRequestFinish(api, request, megaError);

    requestMap.erase(request->getTag());

	activeRequest = NULL;
	activeError = NULL;
	delete request;
    delete megaError;
}

void MegaApiImpl::fireOnRequestUpdate(MegaRequestPrivate *request)
{
    activeRequest = request;

    for(set<MegaRequestListener *>::iterator it = requestListeners.begin(); it != requestListeners.end() ; it++)
        (*it)->onRequestUpdate(api, request);

    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
        (*it)->onRequestUpdate(api, request);

    MegaRequestListener* listener = request->getListener();
    if(listener) listener->onRequestUpdate(api, request);

    activeRequest = NULL;
}

void MegaApiImpl::fireOnRequestTemporaryError(MegaRequestPrivate *request, MegaError e)
{
	MegaError *megaError = new MegaError(e);
	activeRequest = request;
	activeError = megaError;

    request->setNumRetry(request->getNumRetry() + 1);

	for(set<MegaRequestListener *>::iterator it = requestListeners.begin(); it != requestListeners.end() ; it++)
		(*it)->onRequestTemporaryError(api, request, megaError);

	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
		(*it)->onRequestTemporaryError(api, request, megaError);

	MegaRequestListener* listener = request->getListener();
	if(listener) listener->onRequestTemporaryError(api, request, megaError);

	activeRequest = NULL;
	activeError = NULL;
	delete megaError;
}

void MegaApiImpl::fireOnTransferStart(MegaTransferPrivate *transfer)
{
	activeTransfer = transfer;

	for(set<MegaTransferListener *>::iterator it = transferListeners.begin(); it != transferListeners.end() ; it++)
		(*it)->onTransferStart(api, transfer);

	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
		(*it)->onTransferStart(api, transfer);

	MegaTransferListener* listener = transfer->getListener();
	if(listener) listener->onTransferStart(api, transfer);

	activeTransfer = NULL;
}

void MegaApiImpl::fireOnTransferFinish(MegaTransferPrivate *transfer, MegaError e)
{
	MegaError *megaError = new MegaError(e);
	activeTransfer = transfer;
	activeError = megaError;

    if(e.getErrorCode())
    {
        LOG_warn << "Transfer (" << transfer->getTransferString() << ") finished with error: " << e.getErrorString()
                    << " File: " << transfer->getFileName();
    }
    else
    {
        LOG_info << "Transfer (" << transfer->getTransferString() << ") finished. File: " << transfer->getFileName();
    }

	for(set<MegaTransferListener *>::iterator it = transferListeners.begin(); it != transferListeners.end() ; it++)
		(*it)->onTransferFinish(api, transfer, megaError);

	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
		(*it)->onTransferFinish(api, transfer, megaError);

	MegaTransferListener* listener = transfer->getListener();
	if(listener) listener->onTransferFinish(api, transfer, megaError);

    transferMap.erase(transfer->getTag());

	activeTransfer = NULL;
	activeError = NULL;
	delete transfer;
	delete megaError;
}

void MegaApiImpl::fireOnTransferTemporaryError(MegaTransferPrivate *transfer, MegaError e)
{
	MegaError *megaError = new MegaError(e);
	activeTransfer = transfer;
	activeError = megaError;

    transfer->setNumRetry(transfer->getNumRetry() + 1);

	for(set<MegaTransferListener *>::iterator it = transferListeners.begin(); it != transferListeners.end() ; it++)
		(*it)->onTransferTemporaryError(api, transfer, megaError);

	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
		(*it)->onTransferTemporaryError(api, transfer, megaError);

	MegaTransferListener* listener = transfer->getListener();
	if(listener) listener->onTransferTemporaryError(api, transfer, megaError);

	activeTransfer = NULL;
	activeError = NULL;
    delete megaError;
}

MegaClient *MegaApiImpl::getMegaClient()
{
    return client;
}

void MegaApiImpl::fireOnTransferUpdate(MegaTransferPrivate *transfer)
{
	activeTransfer = transfer;

	for(set<MegaTransferListener *>::iterator it = transferListeners.begin(); it != transferListeners.end() ; it++)
		(*it)->onTransferUpdate(api, transfer);

	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
		(*it)->onTransferUpdate(api, transfer);

	MegaTransferListener* listener = transfer->getListener();
	if(listener) listener->onTransferUpdate(api, transfer);

	activeTransfer = NULL;
}

bool MegaApiImpl::fireOnTransferData(MegaTransferPrivate *transfer)
{
	activeTransfer = transfer;
	bool result = false;
	MegaTransferListener* listener = transfer->getListener();
	if(listener)
		result = listener->onTransferData(api, transfer, transfer->getLastBytes(), transfer->getDeltaSize());

	activeTransfer = NULL;
	return result;
}

void MegaApiImpl::fireOnUsersUpdate(MegaUserList *users)
{
	activeUsers = users;

	for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ; it++)
    {
        (*it)->onUsersUpdate(api, users);
    }
	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
    {
        (*it)->onUsersUpdate(api, users);
    }

    activeUsers = NULL;
}

void MegaApiImpl::fireOnContactRequestsUpdate(MegaContactRequestList *requests)
{
    activeContactRequests = requests;

    for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ; it++)
    {
        (*it)->onContactRequestsUpdate(api, requests);
    }
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
    {
        (*it)->onContactRequestsUpdate(api, requests);
    }

    activeContactRequests = NULL;
}

void MegaApiImpl::fireOnNodesUpdate(MegaNodeList *nodes)
{
	activeNodes = nodes;

	for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ; it++)
    {
        (*it)->onNodesUpdate(api, nodes);
    }
	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
    {
        (*it)->onNodesUpdate(api, nodes);
    }

    activeNodes = NULL;
}

void MegaApiImpl::fireOnAccountUpdate()
{
    for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ; it++)
    {
        (*it)->onAccountUpdate(api);
    }
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
    {
        (*it)->onAccountUpdate(api);
    }
}

void MegaApiImpl::fireOnReloadNeeded()
{
	for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ; it++)
		(*it)->onReloadNeeded(api);

	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
		(*it)->onReloadNeeded(api);
}

#ifdef ENABLE_SYNC
void MegaApiImpl::fireOnSyncStateChanged(MegaSyncPrivate *sync)
{
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
        (*it)->onSyncStateChanged(api, sync);

    for(set<MegaSyncListener *>::iterator it = syncListeners.begin(); it != syncListeners.end() ; it++)
        (*it)->onSyncStateChanged(api, sync);

    MegaSyncListener* listener = sync->getListener();
    if(listener)
    {
        listener->onSyncStateChanged(api, sync);
    }
}

void MegaApiImpl::fireOnSyncEvent(MegaSyncPrivate *sync, MegaSyncEvent *event)
{
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
        (*it)->onSyncEvent(api, sync, event);

    for(set<MegaSyncListener *>::iterator it = syncListeners.begin(); it != syncListeners.end() ; it++)
        (*it)->onSyncEvent(api, sync, event);

    MegaSyncListener* listener = sync->getListener();
    if(listener)
    {
        listener->onSyncEvent(api, sync, event);
    }

    delete event;
}

void MegaApiImpl::fireOnGlobalSyncStateChanged()
{
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
        (*it)->onGlobalSyncStateChanged(api);

    for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ; it++)
        (*it)->onGlobalSyncStateChanged(api);
}

void MegaApiImpl::fireOnFileSyncStateChanged(MegaSyncPrivate *sync, const char *filePath, int newState)
{
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
        (*it)->onSyncFileStateChanged(api, sync, filePath, newState);

    for(set<MegaSyncListener *>::iterator it = syncListeners.begin(); it != syncListeners.end() ; it++)
        (*it)->onSyncFileStateChanged(api, sync, filePath, newState);

    MegaSyncListener* listener = sync->getListener();
    if(listener)
    {
        listener->onSyncFileStateChanged(api, sync, filePath, newState);
    }
}

#endif

MegaError MegaApiImpl::checkAccess(MegaNode* megaNode, int level)
{
    if(!megaNode || level < MegaShare::ACCESS_UNKNOWN || level > MegaShare::ACCESS_OWNER)
    {
        return MegaError(API_EARGS);
    }

    sdkMutex.lock();
    Node *node = client->nodebyhandle(megaNode->getHandle());
	if(!node)
	{
        sdkMutex.unlock();
        return MegaError(API_ENOENT);
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

	MegaError e(client->checkaccess(node, a) ? API_OK : API_EACCESS);
    sdkMutex.unlock();

	return e;
}

MegaError MegaApiImpl::checkMove(MegaNode* megaNode, MegaNode* targetNode)
{
	if(!megaNode || !targetNode) return MegaError(API_EARGS);

    sdkMutex.lock();
    Node *node = client->nodebyhandle(megaNode->getHandle());
	Node *target = client->nodebyhandle(targetNode->getHandle());
	if(!node || !target)
	{
        sdkMutex.unlock();
        return MegaError(API_ENOENT);
	}

	MegaError e(client->checkmove(node,target));
    sdkMutex.unlock();

	return e;
}

bool MegaApiImpl::nodeComparatorDefaultASC (Node *i, Node *j)
{
    if(i->type < j->type) return 0;
    if(i->type > j->type) return 1;
    if(strcasecmp(i->displayname(), j->displayname())<=0) return 1;
	return 0;
}

bool MegaApiImpl::nodeComparatorDefaultDESC (Node *i, Node *j)
{
    if(i->type < j->type) return 1;
    if(i->type > j->type) return 0;
    if(strcasecmp(i->displayname(), j->displayname())<=0) return 0;
	return 1;
}

bool MegaApiImpl::nodeComparatorSizeASC (Node *i, Node *j)
{ if(i->size < j->size) return 1; return 0;}
bool MegaApiImpl::nodeComparatorSizeDESC (Node *i, Node *j)
{ if(i->size < j->size) return 0; return 1;}

bool MegaApiImpl::nodeComparatorCreationASC  (Node *i, Node *j)
{ if(i->ctime < j->ctime) return 1; return 0;}
bool MegaApiImpl::nodeComparatorCreationDESC  (Node *i, Node *j)
{ if(i->ctime < j->ctime) return 0; return 1;}

bool MegaApiImpl::nodeComparatorModificationASC  (Node *i, Node *j)
{ if(i->mtime < j->mtime) return 1; return 0;}
bool MegaApiImpl::nodeComparatorModificationDESC  (Node *i, Node *j)
{ if(i->mtime < j->mtime) return 0; return 1;}

bool MegaApiImpl::nodeComparatorAlphabeticalASC  (Node *i, Node *j)
{ if(strcasecmp(i->displayname(), j->displayname())<=0) return 1; return 0; }
bool MegaApiImpl::nodeComparatorAlphabeticalDESC  (Node *i, Node *j)
{ if(strcasecmp(i->displayname(), j->displayname())<=0) return 0; return 1; }

int MegaApiImpl::getNumChildren(MegaNode* p)
{
	if (!p) return 0;

	sdkMutex.lock();
	Node *parent = client->nodebyhandle(p->getHandle());
	if (!parent)
	{
		sdkMutex.unlock();
		return 0;
	}

	int numChildren = parent->children.size();
	sdkMutex.unlock();

	return numChildren;
}

int MegaApiImpl::getNumChildFiles(MegaNode* p)
{
	if (!p) return 0;

	sdkMutex.lock();
	Node *parent = client->nodebyhandle(p->getHandle());
	if (!parent)
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
	if (!p) return 0;

	sdkMutex.lock();
	Node *parent = client->nodebyhandle(p->getHandle());
	if (!parent)
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
    if(!p) return new MegaNodeListPrivate();

    sdkMutex.lock();
    Node *parent = client->nodebyhandle(p->getHandle());
	if(!parent)
	{
        sdkMutex.unlock();
        return new MegaNodeListPrivate();
	}

    vector<Node *> childrenNodes;

    if(!order || order> MegaApi::ORDER_ALPHABETICAL_DESC)
	{
		for (node_list::iterator it = parent->children.begin(); it != parent->children.end(); )
            childrenNodes.push_back(*it++);
	}
	else
	{
        bool (*comp)(Node*, Node*);
		switch(order)
		{
        case MegaApi::ORDER_DEFAULT_ASC: comp = MegaApiImpl::nodeComparatorDefaultASC; break;
        case MegaApi::ORDER_DEFAULT_DESC: comp = MegaApiImpl::nodeComparatorDefaultDESC; break;
        case MegaApi::ORDER_SIZE_ASC: comp = MegaApiImpl::nodeComparatorSizeASC; break;
        case MegaApi::ORDER_SIZE_DESC: comp = MegaApiImpl::nodeComparatorSizeDESC; break;
        case MegaApi::ORDER_CREATION_ASC: comp = MegaApiImpl::nodeComparatorCreationASC; break;
        case MegaApi::ORDER_CREATION_DESC: comp = MegaApiImpl::nodeComparatorCreationDESC; break;
        case MegaApi::ORDER_MODIFICATION_ASC: comp = MegaApiImpl::nodeComparatorModificationASC; break;
        case MegaApi::ORDER_MODIFICATION_DESC: comp = MegaApiImpl::nodeComparatorModificationDESC; break;
        case MegaApi::ORDER_ALPHABETICAL_ASC: comp = MegaApiImpl::nodeComparatorAlphabeticalASC; break;
        case MegaApi::ORDER_ALPHABETICAL_DESC: comp = MegaApiImpl::nodeComparatorAlphabeticalDESC; break;
        default: comp = MegaApiImpl::nodeComparatorDefaultASC; break;
		}

		for (node_list::iterator it = parent->children.begin(); it != parent->children.end(); )
		{
            Node *n = *it++;
            vector<Node *>::iterator i = std::lower_bound(childrenNodes.begin(),
					childrenNodes.end(), n, comp);
            childrenNodes.insert(i, n);
		}
	}
    sdkMutex.unlock();

    if(childrenNodes.size()) return new MegaNodeListPrivate(childrenNodes.data(), childrenNodes.size());
    else return new MegaNodeListPrivate();
}

int MegaApiImpl::getIndex(MegaNode *n, int order)
{
    if(!n)
    {
        return -1;
    }

    sdkMutex.lock();
    Node *node = client->nodebyhandle(n->getHandle());
    if(!node)
    {
        sdkMutex.unlock();
        return -1;
    }

    Node *parent = node->parent;
    if(!parent)
    {
        sdkMutex.unlock();
        return -1;
    }


    if(!order || order> MegaApi::ORDER_ALPHABETICAL_DESC)
    {
        sdkMutex.unlock();
        return 0;
    }

    bool (*comp)(Node*, Node*);
    switch(order)
    {
        case MegaApi::ORDER_DEFAULT_ASC: comp = MegaApiImpl::nodeComparatorDefaultASC; break;
        case MegaApi::ORDER_DEFAULT_DESC: comp = MegaApiImpl::nodeComparatorDefaultDESC; break;
        case MegaApi::ORDER_SIZE_ASC: comp = MegaApiImpl::nodeComparatorSizeASC; break;
        case MegaApi::ORDER_SIZE_DESC: comp = MegaApiImpl::nodeComparatorSizeDESC; break;
        case MegaApi::ORDER_CREATION_ASC: comp = MegaApiImpl::nodeComparatorCreationASC; break;
        case MegaApi::ORDER_CREATION_DESC: comp = MegaApiImpl::nodeComparatorCreationDESC; break;
        case MegaApi::ORDER_MODIFICATION_ASC: comp = MegaApiImpl::nodeComparatorModificationASC; break;
        case MegaApi::ORDER_MODIFICATION_DESC: comp = MegaApiImpl::nodeComparatorModificationDESC; break;
        case MegaApi::ORDER_ALPHABETICAL_ASC: comp = MegaApiImpl::nodeComparatorAlphabeticalASC; break;
        case MegaApi::ORDER_ALPHABETICAL_DESC: comp = MegaApiImpl::nodeComparatorAlphabeticalDESC; break;
        default: comp = MegaApiImpl::nodeComparatorDefaultASC; break;
    }

    vector<Node *> childrenNodes;
    for (node_list::iterator it = parent->children.begin(); it != parent->children.end(); )
    {
        Node *temp = *it++;
        vector<Node *>::iterator i = std::lower_bound(childrenNodes.begin(),
                childrenNodes.end(), temp, comp);
        childrenNodes.insert(i, temp);
    }

    vector<Node *>::iterator i = std::lower_bound(childrenNodes.begin(),
            childrenNodes.end(), node, comp);

    sdkMutex.unlock();
    return i - childrenNodes.begin();
}

MegaNode *MegaApiImpl::getChildNode(MegaNode *parent, const char* name)
{
    if(!parent || !name)
    {
        return NULL;
    }

    sdkMutex.lock();
    Node *parentNode = client->nodebyhandle(parent->getHandle());
	if(!parentNode)
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
    if(!fingerprint || !fingerprint[0]) return NULL;

    m_off_t size = 0;
    unsigned int fsize = strlen(fingerprint);
    unsigned int ssize = fingerprint[0] - 'A';
    if(ssize > (sizeof(size) * 4 / 3 + 4) || fsize <= (ssize + 1))
        return NULL;

    int len =  sizeof(size) + 1;
    byte *buf = new byte[len];
    Base64::atob(fingerprint + 1, buf, len);
    int l = Serialize64::unserialize(buf, len, (uint64_t *)&size);
    delete [] buf;
    if(l <= 0)
        return NULL;

    string sfingerprint = fingerprint + ssize + 1;

    FileFingerprint fp;
    if(!fp.unserializefingerprint(&sfingerprint))
        return NULL;

    fp.size = size;

    sdkMutex.lock();
    Node *n  = client->nodebyfingerprint(&fp);
    sdkMutex.unlock();

    return n;
}

Node *MegaApiImpl::getNodeByFingerprintInternal(const char *fingerprint, Node *parent)
{
    if(!fingerprint || !fingerprint[0]) return NULL;

    m_off_t size = 0;
    unsigned int fsize = strlen(fingerprint);
    unsigned int ssize = fingerprint[0] - 'A';
    if(ssize > (sizeof(size) * 4 / 3 + 4) || fsize <= (ssize + 1))
        return NULL;

    int len =  sizeof(size) + 1;
    byte *buf = new byte[len];
    Base64::atob(fingerprint + 1, buf, len);
    int l = Serialize64::unserialize(buf, len, (uint64_t *)&size);
    delete [] buf;
    if(l <= 0)
        return NULL;

    string sfingerprint = fingerprint + ssize + 1;

    FileFingerprint fp;
    if(!fp.unserializefingerprint(&sfingerprint))
        return NULL;

    fp.size = size;

    sdkMutex.lock();
    Node *n  = client->nodebyfingerprint(&fp);
    if(n && parent && n->parent != parent)
    {
        for (node_list::iterator it = parent->children.begin(); it != parent->children.end(); it++)
        {
            Node* node = (*it);
            if(*((FileFingerprint *)node) == *((FileFingerprint *)n))
            {
                n = node;
                break;
            }
        }
    }
    sdkMutex.unlock();

    return n;
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

	string path;
	if (n->nodehandle == client->rootnodes[0])
	{
		path = "/";
        sdkMutex.unlock();
        return stringToArray(path);
	}

	while (n)
	{
		switch (n->type)
		{
		case FOLDERNODE:
			path.insert(0,n->displayname());

			if (n->inshare)
			{
				path.insert(0,":");
				if (n->inshare->user) path.insert(0,n->inshare->user->email);
				else path.insert(0,"UNKNOWN");
                sdkMutex.unlock();
                return stringToArray(path);
			}
			break;

		case INCOMINGNODE:
			path.insert(0,"//in");
            sdkMutex.unlock();
            return stringToArray(path);

		case ROOTNODE:
            sdkMutex.unlock();
            return stringToArray(path);

		case RUBBISHNODE:
			path.insert(0,"//bin");
            sdkMutex.unlock();
            return stringToArray(path);

		case TYPE_UNKNOWN:
		case FILENODE:
			path.insert(0,n->displayname());
		}

		path.insert(0,"/");

        n = n->parent;
	}
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
	Node* n;
	Node* nn;

	// split path by / or :
	do {
		if (!l)
		{
			if (*path >= 0)
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

void MegaApiImpl::sendPendingTransfers()
{
    MegaTransferPrivate *transfer;
    error e;
    int nextTag;

    while((transfer = transferQueue.pop()))
    {
        sdkMutex.lock();
        e = API_OK;
        nextTag = client->nextreqtag();

        switch(transfer->getType())
        {
            case MegaTransfer::TYPE_UPLOAD:
            {
                const char* localPath = transfer->getPath();
                const char* fileName = transfer->getFileName();
                int64_t mtime = transfer->getTime();
                Node *parent = client->nodebyhandle(transfer->getParentHandle());

                if(!localPath || !parent || !fileName || !(*fileName))
                {
                    e = API_EARGS;
                    break;
                }

                string tmpString = localPath;
                string wLocalPath;
                client->fsaccess->path2local(&tmpString, &wLocalPath);

                FileAccess *fa = fsAccess->newfileaccess();
                if(!fa->fopen(&wLocalPath, true, false))
                {
                    e = API_EREAD;
                    break;
                }

                nodetype_t type = fa->type;
                delete fa;

                if(type == FILENODE)
                {
                    currentTransfer = transfer;
                    string wFileName = fileName;
                    MegaFilePut *f = new MegaFilePut(client, &wLocalPath, &wFileName, transfer->getParentHandle(), "", mtime);

                    bool started = client->startxfer(PUT, f, true);
                    if(!started)
                    {
                        if(!f->isvalid)
                        {
                            //Unable to read the file
                            transfer->setSyncTransfer(false);
                            transferMap[nextTag]=transfer;
                            transfer->setTag(nextTag);
                            fireOnTransferStart(transfer);
                            fireOnTransferFinish(transfer, MegaError(API_EREAD));
                        }
                        else
                        {
                            //Already existing transfer
                            transferMap[nextTag]=transfer;
                            transfer->setTag(nextTag);
                            fireOnTransferStart(transfer);
                            fireOnTransferFinish(transfer, MegaError(API_EEXIST));
                        }
                    }
                    else if(transfer->getTag() == -1)
                    {
                        //Already existing transfer
                        //Delete the new one and set the transfer as regular
                        transfer_map::iterator it = client->transfers[PUT].find(f);
                        if(it != client->transfers[PUT].end())
                        {
                            int previousTag = it->second->tag;
                            if(transferMap.find(previousTag) != transferMap.end())
                            {
                                MegaTransferPrivate* previousTransfer = transferMap.at(previousTag);
                                previousTransfer->setSyncTransfer(false);
                                delete transfer;
                            }
                        }
                    }
                    currentTransfer=NULL;
                }
                else
                {
                    transferMap[nextTag]=transfer;
                    transfer->setTag(nextTag);
                    MegaFolderUploadController *uploader = new MegaFolderUploadController(this, transfer);
                    uploader->start();
                }
                break;
            }
            case MegaTransfer::TYPE_DOWNLOAD:
            {
                handle nodehandle = transfer->getNodeHandle();
				Node *node = client->nodebyhandle(nodehandle);
                MegaNode *publicNode = transfer->getPublicNode();
                const char *parentPath = transfer->getParentPath();
                const char *fileName = transfer->getFileName();
                if(!node && !publicNode) { e = API_EARGS; break; }

                currentTransfer=transfer;
                if(parentPath || fileName)
                {
                    string name;
                    string securename;
                    string path;

					if(parentPath)
					{
						path = parentPath;
					}
					else
					{
						string separator;
						client->fsaccess->local2path(&client->fsaccess->localseparator, &separator);
						path = ".";
						path.append(separator);
					}

					MegaFileGet *f;

					if(node)
					{
						if(!fileName)
                        {
                            attr_map::iterator ait = node->attrs.map.find('n');
                            if(ait == node->attrs.map.end())
                            {
                                name = "CRYPTO_ERROR";
                            }
                            else if(!ait->second.size())
                            {
                                name = "BLANK";
                            }
                            else
                            {
                                name = ait->second;
                            }
                        }
                        else
                        {
                            name = fileName;
                        }

                        client->fsaccess->name2local(&name);
                        client->fsaccess->local2path(&name, &securename);
                        path += securename;
						f = new MegaFileGet(client, node, path);
					}
					else
					{
						if(!transfer->getFileName())
                            name = publicNode->getName();
                        else
                            name = transfer->getFileName();

                        client->fsaccess->name2local(&name);
                        client->fsaccess->local2path(&name, &securename);
                        path += securename;
						f = new MegaFileGet(client, publicNode, path);
					}

					transfer->setPath(path.c_str());
                    bool ok = client->startxfer(GET, f, true);
                    if(transfer->getTag() == -1)
                    {
                        //Already existing transfer
                        if (ok)
                        {
                            //Set the transfer as regular
                            transfer_map::iterator it = client->transfers[GET].find(f);
                            if(it != client->transfers[GET].end())
                            {
                                int previousTag = it->second->tag;
                                if(transferMap.find(previousTag) != transferMap.end())
                                {
                                    MegaTransferPrivate* previousTransfer = transferMap.at(previousTag);
                                    previousTransfer->setSyncTransfer(false);
                                }
                            }
                        }
                        else
                        {
                            //Already existing transfer
                            transferMap[nextTag]=transfer;
                            transfer->setTag(nextTag);
                            fireOnTransferStart(transfer);
                            fireOnTransferFinish(transfer, MegaError(API_EEXIST));
                        }
                    }
                }
                else
                {
                	m_off_t startPos = transfer->getStartPos();
                	m_off_t endPos = transfer->getEndPos();
                	if(startPos < 0 || endPos < 0 || startPos > endPos) { e = API_EARGS; break; }
                	if(node)
                	{
                        transfer->setFileName(node->displayname());
                		if(startPos >= node->size || endPos >= node->size)
                		{ e = API_EARGS; break; }

                		m_off_t totalBytes = endPos - startPos + 1;
                	    transferMap[nextTag]=transfer;
						transfer->setTotalBytes(totalBytes);
						transfer->setTag(nextTag);
                        fireOnTransferStart(transfer);
                	    client->pread(node, startPos, totalBytes, transfer);
                	    waiter->notify();
                	}
                	else
                	{
                        transfer->setFileName(publicNode->getName());
                        if(startPos >= publicNode->getSize() || endPos >= publicNode->getSize())
                        { e = API_EARGS; break; }

                        m_off_t totalBytes = endPos - startPos + 1;
                        transferMap[nextTag]=transfer;
                        transfer->setTotalBytes(totalBytes);
                        transfer->setTag(nextTag);
                        fireOnTransferStart(transfer);
                        SymmCipher cipher;
                        cipher.setkey(publicNode->getNodeKey());
                        client->pread(publicNode->getHandle(), &cipher,
                            MemAccess::get<int64_t>((const char*)publicNode->getNodeKey()->data() + SymmCipher::KEYLENGTH),
                                      startPos, totalBytes, transfer);
                        waiter->notify();
                	}
                }

                currentTransfer=NULL;
				break;
			}
		}

		if(e)
            fireOnTransferFinish(transfer, MegaError(e));

        sdkMutex.unlock();
    }
}

void MegaApiImpl::removeRecursively(const char *path)
{
#ifndef _WIN32
    string spath = path;
    PosixFileSystemAccess::emptydirlocal(&spath);
#else
    string utf16path;
    MegaApi::utf8ToUtf16(path, &utf16path);
    if(utf16path.size())
    {
        utf16path.resize(utf16path.size()-2);
        WinFileSystemAccess::emptydirlocal(&utf16path);
    }
#endif
}


void MegaApiImpl::sendPendingRequests()
{
	MegaRequestPrivate *request;
	error e;
    int nextTag = 0;

	while((request = requestQueue.pop()))
	{
        if(!nextTag)
        {
            client->abortbackoff(false);
        }

		sdkMutex.lock();
		nextTag = client->nextreqtag();
        request->setTag(nextTag);
		requestMap[nextTag]=request;
		e = API_OK;

        fireOnRequestStart(request);
		switch(request->getType())
		{
		case MegaRequest::TYPE_LOGIN:
		{
			const char *login = request->getEmail();
			const char *password = request->getPassword();
            const char* megaFolderLink = request->getLink();
            const char* base64pwkey = request->getPrivateKey();
            const char* sessionKey = request->getSessionKey();

            if(!megaFolderLink && (!(login && password)) && !sessionKey && (!(login && base64pwkey)))
            {
                e = API_EARGS;
                break;
            }

            string slogin;
            if(login)
            {
                slogin = login;
                slogin.erase(slogin.begin(), std::find_if(slogin.begin(), slogin.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
                slogin.erase(std::find_if(slogin.rbegin(), slogin.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), slogin.end());
            }

            requestMap.erase(request->getTag());
            while(!requestMap.empty())
            {
                std::map<int,MegaRequestPrivate*>::iterator it=requestMap.begin();
                if(it->second) fireOnRequestFinish(it->second, MegaError(MegaError::API_EACCESS));
            }

            while(!transferMap.empty())
            {
                std::map<int, MegaTransferPrivate *>::iterator it=transferMap.begin();
                if(it->second) fireOnTransferFinish(it->second, MegaError(MegaError::API_EACCESS));
            }
            requestMap[request->getTag()]=request;

            if(sessionKey)
            {
                byte session[MAX_SESSION_LENGTH];
                int size = Base64::atob(sessionKey, (byte *)session, sizeof session);
                client->login(session, size);
            }
            else if(login && base64pwkey)
            {
                byte pwkey[SymmCipher::KEYLENGTH];
                Base64::atob(base64pwkey, (byte *)pwkey, sizeof pwkey);

                if(password)
                {
                    uint64_t emailhash;
                    Base64::atob(password, (byte *)&emailhash, sizeof emailhash);
                    client->fastlogin(slogin.c_str(), pwkey, emailhash);
                }
                else
                {
                    client->login(slogin.c_str(), pwkey);
                }
            }
            else if(login && password)
            {
                byte pwkey[SymmCipher::KEYLENGTH];
                if((e = client->pw_key(password,pwkey))) break;
                client->login(slogin.c_str(), pwkey);
            }
            else
            {
                const char* ptr;
                if (!((ptr = strstr(megaFolderLink,"#F!")) && (strlen(ptr)>12) && ptr[11] == '!'))
                {
                    e = API_EARGS;
                    break;
                }
                e = client->folderaccess(ptr+3,ptr+12);
                if(e == API_OK)
                {
                    fireOnRequestFinish(request, MegaError(e));
                }
            }

            break;
		}
        case MegaRequest::TYPE_CREATE_FOLDER:
		{
			Node *parent = client->nodebyhandle(request->getParentHandle());
			const char *name = request->getName();
			if(!name || !parent) { e = API_EARGS; break; }

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
			PrnGen::genblock(buf,FOLDERNODEKEYLENGTH);
			newnode->nodekey.assign((char*)buf,FOLDERNODEKEYLENGTH);
			key.setkey(buf);

			// generate fresh attribute object with the folder name
			AttrMap attrs;
            string sname = name;
            fsAccess->normalize(&sname);
            attrs.map['n'] = sname;

			// JSON-encode object and encrypt attribute string
			attrs.getjson(&attrstring);
            newnode->attrstring = new string;
            client->makeattr(&key,newnode->attrstring,attrstring.c_str());

			// add the newly generated folder node
			client->putnodes(parent->nodehandle,newnode,1);
			break;
		}
		case MegaRequest::TYPE_MOVE:
		{
			Node *node = client->nodebyhandle(request->getNodeHandle());
			Node *newParent = client->nodebyhandle(request->getParentHandle());
			if(!node || !newParent) { e = API_EARGS; break; }

            if(node->parent == newParent)
            {
                fireOnRequestFinish(request, MegaError(API_OK));
                break;
            }
			if((e = client->checkmove(node,newParent))) break;

			e = client->rename(node, newParent);
			break;
		}
		case MegaRequest::TYPE_COPY:
		{
			Node *node = client->nodebyhandle(request->getNodeHandle());
			Node *target = client->nodebyhandle(request->getParentHandle());
			const char* email = request->getEmail();
            MegaNode *publicNode = request->getPublicNode();
            const char *newName = request->getName();

            if((!node && !publicNode) || (!target && !email) || (newName && !(*newName))) { e = API_EARGS; break; }

            if(publicNode)
            {
                if(publicNode->getAuth()->size())
                {
                    e = API_EACCESS;
                    break;
                }

                NewNode *newnode = new NewNode[1];
                newnode->nodekey.assign(publicNode->getNodeKey()->data(), publicNode->getNodeKey()->size());
                newnode->attrstring = new string;
                newnode->attrstring->assign(publicNode->getAttrString()->data(), publicNode->getAttrString()->size());
                newnode->nodehandle = publicNode->getHandle();
                newnode->source = NEW_PUBLIC;
                newnode->type = FILENODE;
                newnode->parenthandle = UNDEF;

                if(target)
                {
                    client->putnodes(target->nodehandle, newnode, 1);
                }
                else
                {
                    client->putnodes(email, newnode, 1);
                }
            }
            else
            {
                unsigned nc;
                TreeProcCopy tc;

                // determine number of nodes to be copied
                client->proctree(node,&tc);
                tc.allocnodes();
                nc = tc.nc;

                // build new nodes array
                client->proctree(node,&tc);
                if (!nc)
                {
                    e = API_EARGS;
                    break;
                }

                tc.nn->parenthandle = UNDEF;

                if(nc == 1 && newName && tc.nn[0].nodekey.size())
                {
                    SymmCipher key;
                    AttrMap attrs;
                    string attrstring;

                    key.setkey((const byte*)tc.nn[0].nodekey.data(), node->type);
                    attrs = node->attrs;

                    string sname = newName;
                    fsAccess->normalize(&sname);
                    attrs.map['n'] = sname;

                    attrs.getjson(&attrstring);
                    client->makeattr(&key,tc.nn[0].attrstring, attrstring.c_str());
                }

                if (target)
                {
                    client->putnodes(target->nodehandle,tc.nn,nc);
                }
                else
                {
                    client->putnodes(email, tc.nn, nc);
                }
            }
			break;
		}
        case MegaRequest::TYPE_RENAME:
        {
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
			if(!node) { e = API_EARGS; break; }

			e = client->unlink(node);
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

            accesslevel_t a;
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

            if(e == API_OK)
                client->setshare(node, email, a);
			break;
		}
		case MegaRequest::TYPE_IMPORT_LINK:
		case MegaRequest::TYPE_GET_PUBLIC_NODE:
		{
			Node *node = client->nodebyhandle(request->getParentHandle());
			const char* megaFileLink = request->getLink();
			if(!megaFileLink) { e = API_EARGS; break; }
			if((request->getType()==MegaRequest::TYPE_IMPORT_LINK) && (!node)) { e = API_EARGS; break; }

			e = client->openfilelink(megaFileLink, 1);
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
			client->fetchnodes();
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

			numDetails = 1;
			if(transactions) numDetails++;
			if(purchases) numDetails++;
			if(sessions) numDetails++;

			request->setNumDetails(numDetails);

			client->getaccountdetails(request->getAccountDetails(), storage, transfer, pro, transactions, purchases, sessions);
			break;
		}
		case MegaRequest::TYPE_CHANGE_PW:
		{
			const char* oldPassword = request->getPassword();
			const char* newPassword = request->getNewPassword();
			if(!oldPassword || !newPassword) { e = API_EARGS; break; }

			byte pwkey[SymmCipher::KEYLENGTH];
			byte newpwkey[SymmCipher::KEYLENGTH];
			if((e = client->pw_key(oldPassword, pwkey))) { e = API_EARGS; break; }
			if((e = client->pw_key(newPassword, newpwkey))) { e = API_EARGS; break; }
			e = client->changepw(pwkey, newpwkey);
			break;
		}
		case MegaRequest::TYPE_LOGOUT:
		{
            if(request->getFlag())
            {
                client->logout();
            }
            else
            {
                client->locallogout();
                client->restag = nextTag;
                logout_result(API_OK);
            }
			break;
		}
		case MegaRequest::TYPE_GET_ATTR_FILE:
		{
			const char* dstFilePath = request->getFile();
            int type = request->getParamType();
			Node *node = client->nodebyhandle(request->getNodeHandle());

			if(!dstFilePath || !node) { e = API_EARGS; break; }

			e = client->getfa(node, type);
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

                    prevtag = req->getNumber();
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
            int type = request->getParamType();
            const char *email = request->getEmail();

            User *user;
            if(email)
            {
                user = client->finduser(email, 0);
            }
            else
            {
                user = client->finduser(client->me, 0);
            }

            if((!type && !value) || !user || (type < 0)) { e = API_EARGS; break; }

            if(!type)
            {
                client->getua(user, "a", 0);
            }
            else
            {
                string attrname;
                switch(type)
                {
                    case MegaApi::USER_ATTR_FIRSTNAME:
                    {
                        attrname = "firstname";
                        break;
                    }

                    case MegaApi::USER_ATTR_LASTNAME:
                    {
                        attrname = "lastname";
                        break;
                    }

                    default:
                    {
                        e = API_EARGS;
                        break;
                    }
                }

                if(!e)
                {
                    client->getua(user, attrname.c_str(), 2);
                }
            }
            break;
		}
		case MegaRequest::TYPE_SET_ATTR_USER:
		{
            const char* file = request->getFile();
            const char* value = request->getText();
            int type = request->getParamType();

            if ((!type && !file) || (type < 0) || (type && !value))
            {
                e = API_EARGS;
                break;
            }

            if(!type)
            {
                string path = file;
                string localpath;
                fsAccess->path2local(&path, &localpath);

                string attributedata;
                FileAccess *f = fsAccess->newfileaccess();
                if (!f->fopen(&localpath, 1, 0))
                {
                    delete f;
                    e = API_EREAD;
                    break;
                }

                if (!f->fread(&attributedata, f->size, 0, 0))
                {
                    delete f;
                    e = API_EREAD;
                    break;
                }
                delete f;

                client->putua("a", (byte *)attributedata.data(), attributedata.size(), 0);
            }
            else
            {
                string attrname;
                string attrvalue = value;
                switch(type)
                {
                    case MegaApi::USER_ATTR_FIRSTNAME:
                    {
                        attrname = "firstname";
                        break;
                    }

                    case MegaApi::USER_ATTR_LASTNAME:
                    {
                        attrname = "lastname";
                        break;
                    }

                    default:
                    {
                        e = API_EARGS;
                        break;
                    }
                }
                if(!e)
                {
                    client->putua(attrname.c_str(), (byte *)attrvalue.data(), attrvalue.size(), 2);
                }
            }
            break;
		}
        case MegaRequest::TYPE_SET_ATTR_FILE:
        {
            const char* srcFilePath = request->getFile();
            int type = request->getParamType();
            Node *node = client->nodebyhandle(request->getNodeHandle());

            if(!srcFilePath || !node) { e = API_EARGS; break; }

            string path = srcFilePath;
            string localpath;
            fsAccess->path2local(&path, &localpath);

            string *attributedata = new string;
            FileAccess *f = fsAccess->newfileaccess();
            if (!f->fopen(&localpath, 1, 0))
            {
                delete f;
                delete attributedata;
                e = API_EREAD;
                break;
            }

            if(!f->fread(attributedata, f->size, 0, 0))
            {
                delete f;
                delete attributedata;
                e = API_EREAD;
                break;
            }
            delete f;

            client->putfa(node->nodehandle, type, node->nodecipher(), attributedata);
            //attributedata is not deleted because putfa takes its ownership
            break;
        }
		case MegaRequest::TYPE_CANCEL_ATTR_FILE:
		{
			int type = request->getParamType();
			Node *node = client->nodebyhandle(request->getNodeHandle());

			if (!node) { e = API_EARGS; break; }

			e = client->getfa(node, type, 1);
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
						fireOnRequestFinish(r, MegaError(API_EINCOMPLETE));
					}
				}
				fireOnRequestFinish(request, MegaError(e));
			}
			break;
		}
		case MegaRequest::TYPE_RETRY_PENDING_CONNECTIONS:
		{
			bool disconnect = request->getFlag();
			bool includexfers = request->getNumber();
			client->abortbackoff(includexfers);
			if(disconnect)
            {
                client->disconnect();

#if (WINDOWS_PHONE || TARGET_OS_IPHONE)
                // Workaround to get the IP of valid DNS servers on Windows Phone/iOS
                string servers;

                while (true)
                {
                #ifdef WINDOWS_PHONE
                    struct hostent *hp;
                    hp = gethostbyname("ns.mega.co.nz");
                    if (hp != NULL && hp->h_addr != NULL)
                    {
                        struct in_addr **addr_list;
                        addr_list = (struct in_addr **)hp->h_addr_list;
                        for (int i = 0; addr_list[i] != NULL; i++)
                        {
                            char str[INET_ADDRSTRLEN];
                            const char *ip = inet_ntop(AF_INET, addr_list[i], str, INET_ADDRSTRLEN);
                            if(ip == str)
                            {
                                if (servers.size())
                                {
                                    servers.append(",");
                                }
                                servers.append(ip);
                            }
                        }
                    }
                #else
                    __res_state res;
                    if(res_ninit(&res) == 0)
                    {
                        union res_sockaddr_union u[MAXNS];
                        int nscount = res_getservers(&res, u, MAXNS);

                        for(int i = 0; i < nscount; i++)
                        {
                            char straddr[INET6_ADDRSTRLEN];
                            straddr[0] = 0;

                            if(u[i].sin.sin_family == PF_INET)
                            {
                                inet_ntop(PF_INET, &u[i].sin.sin_addr, straddr, sizeof(straddr));
                            }

                            if(u[i].sin6.sin6_family == PF_INET6)
                            {
                                inet_ntop(PF_INET6, &u[i].sin6.sin6_addr, straddr, sizeof(straddr));
                            }

                            if(straddr[0])
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

                LOG_debug << "Using MEGA DNS servers " << servers;
                httpio->setdnsservers(servers.c_str());
#endif
            }

			fireOnRequestFinish(request, MegaError(API_OK));
			break;
		}
		case MegaRequest::TYPE_ADD_CONTACT:
		{
            const char *email = request->getEmail();

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

			e = client->invite(email, VISIBLE);
			break;
		}
        case MegaRequest::TYPE_INVITE_CONTACT:
        {
            const char *email = request->getEmail();
            const char *message = request->getText();
            int action = request->getNumber();

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

            client->setpcr(email, (opcactions_t)action, message);
            break;
        }
        case MegaRequest::TYPE_REPLY_CONTACT_REQUEST:
        {
            handle h = request->getNodeHandle();
            int action = request->getNumber();

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
			if(!email) { e = API_EARGS; break; }
			e = client->invite(email, HIDDEN);
			break;
		}
		case MegaRequest::TYPE_CREATE_ACCOUNT:
		{
			const char *email = request->getEmail();
			const char *password = request->getPassword();
			const char *name = request->getName();
			const char *pwkey = request->getPrivateKey();

            if(!email || !name || (!password && !pwkey))
			{
				e = API_EARGS; break;
			}

            requestMap.erase(request->getTag());
            while(!requestMap.empty())
            {
                std::map<int,MegaRequestPrivate*>::iterator it=requestMap.begin();
                if(it->second) fireOnRequestFinish(it->second, MegaError(MegaError::API_EACCESS));
            }

            while(!transferMap.empty())
            {
                std::map<int, MegaTransferPrivate *>::iterator it=transferMap.begin();
                if(it->second) fireOnTransferFinish(it->second, MegaError(MegaError::API_EACCESS));
            }
            requestMap[request->getTag()]=request;

			client->createephemeral();
			break;
		}
		case MegaRequest::TYPE_QUERY_SIGNUP_LINK:
		case MegaRequest::TYPE_CONFIRM_ACCOUNT:
		{
			const char *link = request->getLink();
			const char *password = request->getPassword();
			const char *pwkey = request->getPrivateKey();

            if(!link || (request->getType() == MegaRequest::TYPE_CONFIRM_ACCOUNT && !password && !pwkey))
			{
				e = API_EARGS;
				break;
			}

			const char* ptr = link;
			const char* tptr;

			if ((tptr = strstr(ptr,"#confirm"))) ptr = tptr+8;

			unsigned len = (strlen(link)-(ptr-link))*3/4+4;
			byte *c = new byte[len];
            len = Base64::atob(ptr,c,len);
			client->querysignuplink(c,len);
			delete[] c;
			break;
		}
        case MegaRequest::TYPE_PAUSE_TRANSFERS:
        {
            bool pause = request->getFlag();
            int direction = request->getNumber();
            if(direction != -1
                    && direction != MegaTransfer::TYPE_DOWNLOAD
                    && direction != MegaTransfer::TYPE_UPLOAD)
            {
                e = API_EARGS;
                break;
            }

            if(direction == -1)
            {
                client->pausexfers(PUT, pause);
                client->pausexfers(GET, pause);
            }
            else if(direction == MegaTransfer::TYPE_DOWNLOAD)
            {
                client->pausexfers(GET, pause);
            }
            else
            {
                client->pausexfers(PUT, pause);
            }

            fireOnRequestFinish(request, MegaError(API_OK));
            break;
        }
        case MegaRequest::TYPE_CANCEL_TRANSFER:
        {
            int transferTag = request->getTransferTag();
            if(transferMap.find(transferTag) == transferMap.end()) { e = API_ENOENT; break; };

            MegaTransferPrivate* megaTransfer = transferMap.at(transferTag);
            Transfer *transfer = megaTransfer->getTransfer();

            #ifdef _WIN32
                if(transfer->type==GET)
                {
                    transfer->localfilename.append("", 1);
                    WIN32_FILE_ATTRIBUTE_DATA fad;
                    if (GetFileAttributesExW((LPCWSTR)transfer->localfilename.data(), GetFileExInfoStandard, &fad))
                        SetFileAttributesW((LPCWSTR)transfer->localfilename.data(), fad.dwFileAttributes & ~FILE_ATTRIBUTE_HIDDEN);
                    transfer->localfilename.resize(transfer->localfilename.size()-1);
                }
            #endif

            megaTransfer->setSyncTransfer(true);
            megaTransfer->setLastErrorCode(API_EINCOMPLETE);

            file_list files = transfer->files;
            file_list::iterator iterator = files.begin();
            while (iterator != files.end())
            {
                File *file = *iterator;
                iterator++;
                if(!file->syncxfer) client->stopxfer(file);
            }
            fireOnRequestFinish(request, MegaError(API_OK));
            break;
        }
        case MegaRequest::TYPE_CANCEL_TRANSFERS:
        {
            int direction = request->getParamType();
            if((direction != MegaTransfer::TYPE_DOWNLOAD) && (direction != MegaTransfer::TYPE_UPLOAD))
                { e = API_EARGS; break; }

            for (transfer_map::iterator it = client->transfers[direction].begin() ; it != client->transfers[direction].end() ; )
            {
                Transfer *transfer = it->second;
                if(transferMap.find(transfer->tag) != transferMap.end())
                {
                    MegaTransferPrivate* megaTransfer = transferMap.at(transfer->tag);
                    megaTransfer->setSyncTransfer(true);
                    megaTransfer->setLastErrorCode(API_EINCOMPLETE);
                }

                it++;

                file_list files = transfer->files;
				file_list::iterator iterator = files.begin();
				while (iterator != files.end())
				{
					File *file = *iterator;
					iterator++;
					if(!file->syncxfer) client->stopxfer(file);
				}
            }
            fireOnRequestFinish(request, MegaError(API_OK));
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

            string utf8name(localPath);
            string localname;
            client->fsaccess->path2local(&utf8name, &localname);
            e = client->addsync(&localname, DEBRISFOLDER, NULL, node, 0, -nextTag);
            if(!e)
            {
                MegaSyncPrivate *sync = new MegaSyncPrivate(client->syncs.back());
                sync->setListener(request->getSyncListener());
                syncMap[-nextTag] = sync;

                request->setNumber(client->syncs.back()->fsfp);
                fireOnRequestFinish(request, MegaError(API_OK));
            }
            break;
        }
        case MegaRequest::TYPE_REMOVE_SYNCS:
        {
            sync_list::iterator it = client->syncs.begin();
            while(it != client->syncs.end())
            {
                Sync *sync = (*it);
                int tag = sync->tag;
                it++;

                client->delsync(sync);

                if(syncMap.find(tag) == syncMap.end())
                {
                    MegaSyncPrivate *megaSync = syncMap.at(tag);
                    syncMap.erase(tag);
                    delete megaSync;
                }
            }
            fireOnRequestFinish(request, MegaError(API_OK));
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
                int tag = sync->tag;
                if(!sync->localroot.node || sync->localroot.node->nodehandle == nodehandle)
                {
                    string path;
                    fsAccess->local2path(&sync->localroot.localname, &path);
                    request->setFile(path.c_str());
                    client->delsync(sync, request->getFlag());

                    if(syncMap.find(tag) == syncMap.end())
                    {
                        MegaSyncPrivate *megaSync = syncMap.at(tag);
                        syncMap.erase(tag);
                        delete megaSync;
                    }

                    fireOnRequestFinish(request, MegaError(API_OK));
                    found = true;
                    break;
                }
                it++;
            }

            if(!found) e = API_ENOENT;
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
            int size = strlen(details);
            char *base64details = new char[size * 4 / 3 + 4];
            Base64::btoa((byte *)details, size, base64details);
            client->reportevent(event.c_str(), base64details);
            delete base64details;
            break;
        }
        case MegaRequest::TYPE_DELETE:
        {
            threadExit = 1;
            break;
        }
        case MegaRequest::TYPE_GET_PRICING:
        case MegaRequest::TYPE_GET_PAYMENT_ID:
        case MegaRequest::TYPE_UPGRADE_ACCOUNT:
        {
            int method = request->getNumber();
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
            int type = request->getNumber();

            if(!receipt || (type != MegaApi::PAYMENT_METHOD_GOOGLE_WALLET
                            && type != MegaApi::PAYMENT_METHOD_ITUNES))
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
            if(type == MegaApi::PAYMENT_METHOD_GOOGLE_WALLET)
            {
                int len = strlen(receipt);
                base64receipt.resize(len * 4 / 3 + 4);
                base64receipt.resize(Base64::btoa((byte *)receipt, len, (char *)base64receipt.data()));
            }
            else //MegaApi::PAYMENT_METHOD_ITUNES
            {
                base64receipt = receipt;
            }

            client->submitpurchasereceipt(type, base64receipt.c_str());
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
            int rating = request->getNumber();
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

            int size = strlen(message);
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
            int number = request->getNumber();
            const char *text = request->getText();

            if(number < 99500 || number >= 99600 || !text)
            {
                e = API_EARGS;
                break;
            }

            client->sendevent(number, text);
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
        case MegaRequest::TYPE_LOAD_BALANCING:
        {
            const char* service = request->getName();
            if(!service)
            {
                e = API_EARGS;
                break;
            }

            client->loadbalancing(service);
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
        case MegaRequest::TYPE_CLEAN_RUBBISH_BIN:
        {
            client->cleanrubbishbin();
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
            fireOnRequestFinish(request, MegaError(e));
        }

		sdkMutex.unlock();
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
    transfer_map::iterator it;
    transfer_map::iterator end;
    int downloadCount = 0;
    int uploadCount = 0;

    sdkMutex.lock();
    it = client->transfers[0].begin();
    end = client->transfers[0].end();
    while(it != end)
    {
        Transfer *transfer = it->second;
        if((transfer->failcount<2) || (transfer->slot && (Waiter::ds - transfer->slot->lastdata) < TransferSlot::XFERTIMEOUT))
            downloadCount++;
        it++;
    }

    it = client->transfers[1].begin();
    end = client->transfers[1].end();
    while(it != end)
    {
        Transfer *transfer = it->second;
        if((transfer->failcount<2) || (transfer->slot && (Waiter::ds - transfer->slot->lastdata) < TransferSlot::XFERTIMEOUT))
            uploadCount++;
        it++;
    }

    pendingDownloads = downloadCount;
    pendingUploads = uploadCount;
    sdkMutex.unlock();
}

long long MegaApiImpl::getTotalDownloadedBytes()
{
    return totalDownloadedBytes;
}

long long MegaApiImpl::getTotalUploadedBytes()
{
    return totalUploadedBytes;
}

void MegaApiImpl::update()
{
#ifdef ENABLE_SYNC
    sdkMutex.lock();

    LOG_debug << "PendingCS? " << (client->pendingcs != NULL);
    if(client->curfa == client->newfa.end())
    {
        LOG_debug << "PendingFA? 0";
    }
    else
    {
        HttpReqCommandPutFA* fa = *client->curfa;
        if(fa)
        {
            LOG_debug << "PendingFA? " << client->newfa.size() << " STATUS: " << fa->status;
        }
    }

    LOG_debug << "FLAGS: " << client->syncactivity << " " << client->syncadded
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

    sdkMutex.unlock();
#endif

    waiter->notify();
}

bool MegaApiImpl::isWaiting()
{
    return waiting || waitingRequest;
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
		if (n->type == FILENODE) t->nodekey = n->nodekey;
		else
		{
			byte buf[FOLDERNODEKEYLENGTH];
			PrnGen::genblock(buf,sizeof buf);
			t->nodekey.assign((char*)buf,FOLDERNODEKEYLENGTH);
		}

		t->attrstring = new string;
		if(t->nodekey.size())
		{
			key.setkey((const byte*)t->nodekey.data(),n->type);

			n->attrs.getjson(&attrstring);
			client->makeattr(&key,t->attrstring,attrstring.c_str());
		}
	}
	else nc++;
}

TransferQueue::TransferQueue()
{
    mutex.init(false);
}

void TransferQueue::push(MegaTransferPrivate *transfer)
{
    mutex.lock();
    transfers.push_back(transfer);
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

RequestQueue::RequestQueue()
{
    mutex.init(false);
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

MegaHashSignatureImpl::MegaHashSignatureImpl(const char *base64Key)
{
    hashSignature = new HashSignature(new Hash());
    asymmCypher = new AsymmCipher();

    string pubks;
    int len = strlen(base64Key)/4*3+3;
    pubks.resize(len);
    pubks.resize(Base64::atob(base64Key, (byte *)pubks.data(), len));
    asymmCypher->setkey(AsymmCipher::PUBKEY,(byte*)pubks.data(), pubks.size());
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

    return hashSignature->check(asymmCypher, (const byte *)signature, sizeof(signature));
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

long long MegaAccountDetailsPrivate::getTransferMax()
{
    return details.transfer_max;
}

long long MegaAccountDetailsPrivate::getTransferOwnUsed()
{
    return details.transfer_own_used;
}

int MegaAccountDetailsPrivate::getNumUsageItems()
{
    return details.storage.size();
}

long long MegaAccountDetailsPrivate::getStorageUsed(MegaHandle handle)
{
    return details.storage[handle].bytes;
}

long long MegaAccountDetailsPrivate::getNumFiles(MegaHandle handle)
{
    return details.storage[handle].files;
}

long long MegaAccountDetailsPrivate::getNumFolders(MegaHandle handle)
{
    return details.storage[handle].folders;
}

MegaAccountDetails* MegaAccountDetailsPrivate::copy()
{
    return new MegaAccountDetailsPrivate(&details);
}

int MegaAccountDetailsPrivate::getNumBalances() const
{
    return details.balances.size();
}

MegaAccountBalance *MegaAccountDetailsPrivate::getBalance(int i) const
{
    if(i < details.balances.size())
    {
        return MegaAccountBalancePrivate::fromAccountBalance(&(details.balances[i]));
    }
    return NULL;
}

int MegaAccountDetailsPrivate::getNumSessions() const
{
    return details.sessions.size();
}

MegaAccountSession *MegaAccountDetailsPrivate::getSession(int i) const
{
    if(i < details.sessions.size())
    {
        return MegaAccountSessionPrivate::fromAccountSession(&(details.sessions[i]));
    }
    return NULL;
}

int MegaAccountDetailsPrivate::getNumPurchases() const
{
    return details.purchases.size();
}

MegaAccountPurchase *MegaAccountDetailsPrivate::getPurchase(int i) const
{
    if(i < details.purchases.size())
    {
        return MegaAccountPurchasePrivate::fromAccountPurchase(&(details.purchases[i]));
    }
    return NULL;
}

int MegaAccountDetailsPrivate::getNumTransactions() const
{
    return details.transactions.size();
}

MegaAccountTransaction *MegaAccountDetailsPrivate::getTransaction(int i) const
{
    if(i < details.transactions.size())
    {
        return MegaAccountTransactionPrivate::fromAccountTransaction(&(details.transactions[i]));
    }
    return NULL;
}

ExternalLogger::ExternalLogger()
{
	mutex.init(true);
	this->megaLogger = NULL;
	SimpleLogger::setOutputClass(this);

    //Initialize outputSettings map
    SimpleLogger::outputSettings[(LogLevel)logFatal];
    SimpleLogger::outputSettings[(LogLevel)logError];
    SimpleLogger::outputSettings[(LogLevel)logWarning];
    SimpleLogger::outputSettings[(LogLevel)logInfo];
    SimpleLogger::outputSettings[(LogLevel)logDebug];
    SimpleLogger::outputSettings[(LogLevel)logMax];
}

void ExternalLogger::setMegaLogger(MegaLogger *logger)
{
	this->megaLogger = logger;
}

void ExternalLogger::setLogLevel(int logLevel)
{
	SimpleLogger::setLogLevel((LogLevel)logLevel);
}

void ExternalLogger::postLog(int logLevel, const char *message, const char *filename, int line)
{
    if(SimpleLogger::logCurrentLevel < logLevel)
        return;

	if(!message)
	{
		message = "";
	}

	if(!filename)
	{
		filename = "";
	}

    mutex.lock();
	SimpleLogger((LogLevel)logLevel, filename, line) << message;
    mutex.unlock();
}

void ExternalLogger::log(const char *time, int loglevel, const char *source, const char *message)
{
	if(!time)
	{
		time = "";
	}

	if(!source)
	{
		source = "";
	}

	if(!message)
	{
		message = "";
	}

	mutex.lock();
	if(megaLogger)
	{
        megaLogger->log(time, loglevel, source, message);
	}
	else
	{
		cout << "[" << time << "][" << SimpleLogger::toStr((LogLevel)loglevel) << "] " << message << endl;
	}
	mutex.unlock();
}


OutShareProcessor::OutShareProcessor()
{

}

bool OutShareProcessor::processNode(Node *node)
{
    if(!node->outshares)
    {
        return true;
    }

    for (share_map::iterator it = node->outshares->begin(); it != node->outshares->end(); it++)
	{
		shares.push_back(it->second);
		handles.push_back(node->nodehandle);
	}

	return true;
}

vector<Share *> &OutShareProcessor::getShares()
{
	return shares;
}

vector<handle> &OutShareProcessor::getHandles()
{
	return handles;
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
    return handles.size();
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
    if((unsigned)productIndex < gbStorage.size())
        return gbStorage[productIndex];

    return 0;
}

int MegaPricingPrivate::getGBTransfer(int productIndex)
{
    if((unsigned)productIndex < gbTransfer.size())
        return gbTransfer[productIndex];

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

MegaPricing *MegaPricingPrivate::copy()
{
    MegaPricingPrivate *megaPricing = new MegaPricingPrivate();
    for(unsigned i=0; i<handles.size(); i++)
    {
        megaPricing->addProduct(handles[i], proLevel[i], gbStorage[i], gbTransfer[i],
                                months[i], amount[i], currency[i], description[i], iosId[i], androidId[i]);
    }

    return megaPricing;
}

void MegaPricingPrivate::addProduct(handle product, int proLevel, int gbStorage, int gbTransfer, int months, int amount, const char *currency,
                                    const char* description, const char* iosid, const char* androidid)
{
    this->handles.push_back(product);
    this->proLevel.push_back(proLevel);
    this->gbStorage.push_back(gbStorage);
    this->gbTransfer.push_back(gbTransfer);
    this->months.push_back(months);
    this->amount.push_back(amount);
    this->currency.push_back(MegaApi::strdup(currency));
    this->description.push_back(MegaApi::strdup(description));
    this->iosId.push_back(MegaApi::strdup(iosid));
    this->androidId.push_back(MegaApi::strdup(androidid));
}

#ifdef ENABLE_SYNC
MegaSyncPrivate::MegaSyncPrivate(Sync *sync)
{
    this->tag = sync->tag;
    sync->client->fsaccess->local2path(&sync->localroot.localname, &localFolder);
    this->megaHandle = sync->localroot.node->nodehandle;
    this->fingerprint = sync->fsfp;
    this->state = sync->state;
    this->listener = NULL;
}

MegaSyncPrivate::MegaSyncPrivate(MegaSyncPrivate *sync)
{
    this->setTag(sync->getTag());
    this->setLocalFolder(sync->getLocalFolder());
    this->setMegaHandle(sync->getMegaHandle());
    this->setLocalFingerprint(sync->getLocalFingerprint());
    this->setState(sync->getState());
    this->setListener(sync->getListener());
}

MegaSyncPrivate::~MegaSyncPrivate()
{
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
    if(!localFolder.size())
        return NULL;

    return localFolder.c_str();
}

void MegaSyncPrivate::setLocalFolder(const char *path)
{
    this->localFolder = path;
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

MegaSyncEventPrivate::MegaSyncEventPrivate(int type)
{
    this->type = type;
    path = NULL;
    newPath = NULL;
    prevName = NULL;
    nodeHandle = INVALID_HANDLE;
    prevParent = INVALID_HANDLE;
}

MegaSyncEventPrivate::~MegaSyncEventPrivate()
{
    delete [] path;
}

MegaSyncEvent *MegaSyncEventPrivate::copy()
{
    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(type);
    event->setPath(this->path);
    event->setNodeHandle(this->nodeHandle);
    event->setNewPath(this->newPath);
    event->setPrevName(this->prevName);
    event->setPrevParent(this->prevParent);
    return event;
}

int MegaSyncEventPrivate::getType() const
{
    return type;
}

const char *MegaSyncEventPrivate::getPath() const
{
    return path;
}

MegaHandle MegaSyncEventPrivate::getNodeHandle() const
{
    return nodeHandle;
}

const char *MegaSyncEventPrivate::getNewPath() const
{
    return newPath;
}

const char *MegaSyncEventPrivate::getPrevName() const
{
    return prevName;
}

MegaHandle MegaSyncEventPrivate::getPrevParent() const
{
    return prevParent;
}

void MegaSyncEventPrivate::setPath(const char *path)
{
    if(this->path)
    {
        delete [] this->path;
    }
    this->path =  MegaApi::strdup(path);
}

void MegaSyncEventPrivate::setNodeHandle(MegaHandle nodeHandle)
{
    this->nodeHandle = nodeHandle;
}

void MegaSyncEventPrivate::setNewPath(const char *newPath)
{
    if(this->newPath)
    {
        delete [] this->newPath;
    }
    this->newPath =  MegaApi::strdup(newPath);
}

void MegaSyncEventPrivate::setPrevName(const char *prevName)
{
    if(this->prevName)
    {
        delete [] this->prevName;
    }
    this->prevName =  MegaApi::strdup(prevName);
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


FileInputStream::FileInputStream(FileAccess *fileAccess)
{
    this->fileAccess = fileAccess;
    this->offset = 0;
}

m_off_t FileInputStream::size()
{
    return fileAccess->size;
}

bool FileInputStream::read(byte *buffer, unsigned size)
{
    if (!buffer)
    {
        if ((offset + size) <= fileAccess->size)
        {
            offset += size;
            return true;
        }

        LOG_warn << "Invalid seek on FileInputStream";
        return false;
    }

    if (fileAccess->sysread(buffer, size, offset))
    {
        offset += size;
        return true;
    }

    LOG_warn << "Invalid read on FileInputStream";
    return false;
}

FileInputStream::~FileInputStream()
{

}


MegaFolderUploadController::MegaFolderUploadController(MegaApiImpl *megaApi, MegaTransferPrivate *transfer)
{
    this->megaApi = megaApi;
    this->client = megaApi->getMegaClient();
    this->parenthandle = transfer->getParentHandle();
    this->name = transfer->getFileName();
    this->transfer = transfer;
    this->listener = transfer->getListener();
    this->recursive = 0;
    this->pendingTransfers = 0;
    this->tag = transfer->getTag();
}

void MegaFolderUploadController::start()
{
    transfer->setFolderTransferTag(-1);
    transfer->setStartTime(Waiter::ds);
    megaApi->fireOnTransferStart(transfer);

    MegaNode *parent = megaApi->getNodeByHandle(parenthandle);
    if(!parent)
    {
        megaApi->fireOnTransferFinish(transfer, MegaError(API_EARGS));
        delete this;
    }
    else
    {
        string path = transfer->getPath();
        string localpath;
        client->fsaccess->path2local(&path, &localpath);

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

void MegaFolderUploadController::onFolderAvailable(MegaHandle handle)
{
    recursive++;
    string localPath = pendingFolders.front();
    pendingFolders.pop_front();

    MegaNode *parent = megaApi->getNodeByHandle(handle);

    string localname;
    DirAccess* da;
    da = client->fsaccess->newdiraccess();
    if (da->dopen(&localPath, NULL, false))
    {
        size_t t = localPath.size();

        while (da->dnext(&localPath, &localname, client->followsymlinks))
        {
            if (t)
            {
                localPath.append(client->fsaccess->localseparator);
            }

            localPath.append(localname);

            FileAccess *fa = client->fsaccess->newfileaccess();
            if(fa->fopen(&localPath, true, false))
            {
                string name = localname;
                client->fsaccess->local2name(&name);

                if(fa->type == FILENODE)
                {
                    pendingTransfers++;
                    MegaNode *child = megaApi->getChildNode(parent, name.c_str());
                    if(!child || child->isFolder() || (fa->size != child->getSize()))
                    {                        
                        FileFingerprint fp;
                        fp.genfingerprint(fa);
                        Node *node = client->nodebyfingerprint(&fp);
                        if(!node)
                        {
                            string utf8path;
                            client->fsaccess->local2path(&localPath, &utf8path);
                            megaApi->startUpload(utf8path.c_str(), parent, (const char *)NULL, -1, tag, this);
                        }
                        else
                        {
                            string utf8path;
                            client->fsaccess->local2path(&localPath, &utf8path);
                            #if defined(_WIN32) && !defined(WINDOWS_PHONE)
                                    if(!PathIsRelativeA(utf8path.c_str()) && ((utf8path.size()<2) || utf8path.compare(0, 2, "\\\\")))
                                        utf8path.insert(0, "\\\\?\\");
                            #endif

                            int nextTag = client->nextreqtag();
                            MegaTransferPrivate* t = new MegaTransferPrivate(MegaTransfer::TYPE_UPLOAD, this);
                            t->setPath(utf8path.c_str());
                            t->setParentHandle(parent->getHandle());
                            t->setTag(nextTag);
                            t->setFolderTransferTag(tag);
                            t->setTotalBytes(node->size);
                            megaApi->transferMap[nextTag] = t;
                            pendingSkippedTransfers.push_back(t);
                            megaApi->fireOnTransferStart(t);

                            MegaNode *duplicate = MegaNodePrivate::fromNode(node);
                            megaApi->copyNode(duplicate, parent, name.c_str(), this);
                            delete duplicate;
                        }
                    }
                    else
                    {
                        string utf8path;
                        client->fsaccess->local2path(&localPath, &utf8path);
                        #if defined(_WIN32) && !defined(WINDOWS_PHONE)
                                if(!PathIsRelativeA(utf8path.c_str()) && ((utf8path.size()<2) || utf8path.compare(0, 2, "\\\\")))
                                    utf8path.insert(0, "\\\\?\\");
                        #endif

                        int nextTag = client->nextreqtag();
                        MegaTransferPrivate* t = new MegaTransferPrivate(MegaTransfer::TYPE_UPLOAD, this);
                        t->setPath(utf8path.data());
                        t->setParentHandle(parent->getHandle());
                        t->setTag(nextTag);
                        t->setFolderTransferTag(tag);
                        t->setTotalBytes(child->getSize());
                        megaApi->transferMap[nextTag] = t;
                        megaApi->fireOnTransferStart(t);

                        t->setTransferredBytes(child->getSize());
                        t->setDeltaSize(child->getSize());
                        megaApi->fireOnTransferFinish(t, MegaError(API_OK));
                    }

                    delete child;
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

            localPath.resize(t);
            delete fa;
        }
    }

    delete da;
    delete parent;
    recursive--;

    checkCompletion();
}

void MegaFolderUploadController::checkCompletion()
{
    if(!recursive && !pendingFolders.size() && !pendingTransfers && !pendingSkippedTransfers.size())
    {
        LOG_debug << "Folder transfer finished - " << transfer->getTransferredBytes() << " of " << transfer->getTotalBytes();
        megaApi->fireOnTransferFinish(transfer, MegaError(API_OK));
        delete this;
    }
}

void MegaFolderUploadController::onRequestFinish(MegaApi *, MegaRequest *request, MegaError *e)
{
    int type = request->getType();
    int errorCode = e->getErrorCode();

    if(type == MegaRequest::TYPE_CREATE_FOLDER)
    {
        if(!errorCode)
        {
            onFolderAvailable(request->getNodeHandle());
        }
        else
        {
            pendingFolders.pop_front();
            checkCompletion();
        }
    }
    else if(type == MegaRequest::TYPE_COPY)
    {
        Node *node = client->nodebyhandle(request->getNodeHandle());

        MegaTransferPrivate *t = pendingSkippedTransfers.front();
        t->setTransferredBytes(node->size);
        t->setDeltaSize(node->size);
        megaApi->fireOnTransferFinish(t, MegaError(API_OK));
        pendingSkippedTransfers.pop_front();
        checkCompletion();
    }
}

void MegaFolderUploadController::onTransferStart(MegaApi *, MegaTransfer *t)
{
    transfer->setTotalBytes(transfer->getTotalBytes() + t->getTotalBytes());
    transfer->setUpdateTime(Waiter::ds);
    megaApi->fireOnTransferUpdate(transfer);
}

void MegaFolderUploadController::onTransferUpdate(MegaApi *, MegaTransfer *t)
{
    transfer->setTransferredBytes(transfer->getTransferredBytes() + t->getDeltaSize());
    transfer->setUpdateTime(Waiter::ds);
    transfer->setSpeed(t->getSpeed());
    megaApi->fireOnTransferUpdate(transfer);
}

void MegaFolderUploadController::onTransferFinish(MegaApi *, MegaTransfer *t, MegaError *)
{
    pendingTransfers--;
    transfer->setTransferredBytes(transfer->getTransferredBytes() + t->getDeltaSize());
    transfer->setUpdateTime(Waiter::ds);

    if(t->getSpeed())
    {
        transfer->setSpeed(t->getSpeed());
    }

    megaApi->fireOnTransferUpdate(transfer);
    checkCompletion();
}
