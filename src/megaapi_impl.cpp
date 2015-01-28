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

#ifndef _WIN32
#define _LARGEFILE64_SOURCE
#include <signal.h>
#endif


#ifdef __APPLE__
    #include <xlocale.h>
    #include <strings.h>

    #ifdef TARGET_OS_IPHONE
    #include <resolv.h>
    #endif
#endif

#ifdef _WIN32
#ifndef WINDOWS_PHONE
#include <shlwapi.h>
#endif

#define snprintf _snprintf
#define vsnprintf _vsnprintf
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif

#if !defined(_WIN32) || defined(WINDOWS_PHONE)
#include <openssl/rand.h>
#endif

using namespace mega;

MegaNodePrivate::MegaNodePrivate(const char *name, int type, int64_t size, int64_t ctime, int64_t mtime, uint64_t nodehandle, string *nodekey, string *attrstring)
: MegaNode()
{
    this->name = MegaApi::strdup(name);
    this->type = type;
    this->size = size;
    this->ctime = ctime;
    this->mtime = mtime;
    this->nodehandle = nodehandle;
    this->attrstring.assign(attrstring->data(), attrstring->size());
    this->nodekey.assign(nodekey->data(),nodekey->size());
    this->changed = 0;
    this->thumbnailAvailable = false;
    this->previewAvailable = false;
    this->tag = 0;
    this->isPublicNode = true;

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
    string * attrstring = node->getAttrString();
    this->attrstring.assign(attrstring->data(), attrstring->size());
    string *nodekey = node->getNodeKey();
    this->nodekey.assign(nodekey->data(),nodekey->size());
    this->changed = node->getChanges();
    this->thumbnailAvailable = node->hasThumbnail();
    this->previewAvailable = node->hasPreview();
    this->tag = node->getTag();
    this->isPublicNode = node->isPublic();

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
}

MegaNode *MegaNodePrivate::copy()
{
	return new MegaNodePrivate(this);
}

const char *MegaNodePrivate::getBase64Handle()
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
	return name;
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

uint64_t MegaNodePrivate::getHandle()
{
	return nodehandle;
}

string *MegaNodePrivate::getNodeKey()
{
    return &nodekey;
}

const char *MegaNodePrivate::getBase64Key()
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

bool MegaApiImpl::isIndexing()
{
    if(client->syncs.size()==0) return false;
    if(!client || client->syncscanstate) return true;

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


MegaNodePrivate::~MegaNodePrivate()
{
 	delete[] name;
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
}

MegaTransferPrivate::MegaTransferPrivate(const MegaTransferPrivate &transfer)
{
    path = NULL;
    parentPath = NULL;
    fileName = NULL;
    publicNode = NULL;
	lastBytes = NULL;

    this->listener = transfer.getListener();
    this->transfer = transfer.getTransfer();
	this->type = transfer.getType();
	this->setTag(transfer.getTag());
	this->setPath(transfer.getPath());
	this->setNodeHandle(transfer.getNodeHandle());
	this->setParentHandle(transfer.getParentHandle());
	this->setStartPos(transfer.getStartPos());
	this->setEndPos(transfer.getEndPos());
	this->setParentPath(transfer.getParentPath());
	this->setNumRetry(transfer.getNumRetry());
	this->setMaxRetries(transfer.getMaxRetries());
	this->setTime(transfer.getTime());
	this->setStartTime(transfer.getStartTime());
	this->setTransferredBytes(transfer.getTransferredBytes());
	this->setTotalBytes(transfer.getTotalBytes());
	this->setFileName(transfer.getFileName());
	this->setSpeed(transfer.getSpeed());
	this->setDeltaSize(transfer.getDeltaSize());
	this->setUpdateTime(transfer.getUpdateTime());
    this->setPublicNode(transfer.getPublicNode());
    this->setTransfer(transfer.getTransfer());
    this->setSyncTransfer(transfer.isSyncTransfer());
}

MegaTransfer* MegaTransferPrivate::copy()
{
	return new MegaTransferPrivate(*this);
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
            delete parentPath;
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

MegaAccountDetails *MegaAccountDetailsPrivate::fromAccountDetails(AccountDetails *details)
{
    return new MegaAccountDetailsPrivate(details);
}

MegaAccountDetailsPrivate::MegaAccountDetailsPrivate(AccountDetails *details)
{
    this->details = new AccountDetails;
    (*(this->details)) = (*details);
}

MegaAccountDetailsPrivate::~MegaAccountDetailsPrivate()
{ }

MegaRequest *MegaRequestPrivate::copy()
{
	return new MegaRequestPrivate(*this);
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

    if((type == MegaRequest::TYPE_GET_PRICING) || (type == MegaRequest::TYPE_GET_PAYMENT_URL))
    {
        this->megaPricing = new MegaPricingPrivate();
    }
    else
    {
        megaPricing = NULL;
    }
}

MegaRequestPrivate::MegaRequestPrivate(MegaRequestPrivate &request)
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

    this->type = request.getType();
    this->setTag(request.getTag());
	this->setNodeHandle(request.getNodeHandle());
	this->setLink(request.getLink());
	this->setParentHandle(request.getParentHandle());
    this->setSessionKey(request.getSessionKey());
	this->setName(request.getName());
	this->setEmail(request.getEmail());
	this->setPassword(request.getPassword());
	this->setNewPassword(request.getNewPassword());
	this->setPrivateKey(request.getPrivateKey());
	this->setAccess(request.getAccess());
	this->setNumRetry(request.getNumRetry());
	this->numDetails = 0;
	this->setFile(request.getFile());
    this->setParamType(request.getParamType());
    this->setText(request.getText());
    this->setNumber(request.getNumber());
    this->setPublicNode(request.getPublicNode());
    this->setFlag(request.getFlag());
    this->setTransferTag(request.getTransferTag());
    this->setTotalBytes(request.getTotalBytes());
    this->setTransferredBytes(request.getTransferredBytes());
	this->listener = request.getListener();
#ifdef ENABLE_SYNC
    this->syncListener = request.getSyncListener();
#endif
	this->accountDetails = NULL;
    this->megaPricing = (MegaPricingPrivate *)request.getPricing();
	if(request.getAccountDetails())
    {
		this->accountDetails = new AccountDetails();
        AccountDetails *temp = request.getAccountDetails();
        this->accountDetails->pro_level = temp->pro_level;
        this->accountDetails->subscription_type = temp->subscription_type;
        this->accountDetails->pro_until = temp->pro_until;
        this->accountDetails->storage_used = temp->storage_used;
        this->accountDetails->storage_max = temp->storage_max;
        this->accountDetails->transfer_own_used = temp->transfer_own_used;
        this->accountDetails->transfer_srv_used = temp->transfer_srv_used;
        this->accountDetails->transfer_max = temp->transfer_max;
        this->accountDetails->transfer_own_reserved = temp->transfer_own_reserved;
        this->accountDetails->transfer_srv_reserved = temp->transfer_srv_reserved;
        this->accountDetails->srv_ratio = temp->srv_ratio;
        this->accountDetails->transfer_hist_starttime = temp->transfer_hist_starttime;
        this->accountDetails->transfer_hist_interval = temp->transfer_hist_interval;
        this->accountDetails->transfer_reserved = temp->transfer_reserved;
        this->accountDetails->transfer_limit = temp->transfer_limit;
        this->accountDetails->storage = temp->storage;
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
	return MegaAccountDetailsPrivate::fromAccountDetails(accountDetails);
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

void MegaRequestPrivate::addProduct(handle product, int proLevel, int gbStorage, int gbTransfer, int months, int amount, const char *currency)
{
    if(megaPricing)
    {
        megaPricing->addProduct(product, proLevel, gbStorage, gbTransfer, months, amount, currency);
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
        case TYPE_GET_PAYMENT_URL: return "GET_PAYMENT_URL";
        case TYPE_GET_USER_DATA: return "GET_USER_DATA";
        case TYPE_LOAD_BALANCING: return "LOAD_BALANCING";
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

MegaNodeListPrivate::MegaNodeListPrivate(MegaNodeListPrivate& nodeList)
{
	s = nodeList.size();
	if (!s)
	{
		list = NULL;
		return;
	}

	list = new MegaNode*[s];
	for (int i = 0; i<s; i++)
		list[i] = new MegaNodePrivate(nodeList.get(i));
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
	return new MegaNodeListPrivate(*this);
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

MegaUserListPrivate::MegaUserListPrivate(MegaUserListPrivate &userList)
{
	s = userList.size();
	if (!s)
	{
		list = NULL;
		return;
	}
	list = new MegaUser*[s];
	for (int i = 0; i<s; i++)
		list[i] = new MegaUserPrivate(userList.get(i));
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
	return new MegaUserListPrivate(*this);
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

	for(int i=0; i<s; i++)
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

int MegaFile::nextseqno = 0;

bool MegaFile::failed(error e)
{
    return e != API_EKEY && e != API_EBLOCKED && transfer->failcount < 10;
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
    hprivate = false;
}

void MegaFileGet::prepare()
{
    if (!transfer->localfilename.size())
    {
        transfer->localfilename = localname;

        int index = transfer->localfilename.find_last_of(transfer->client->fsaccess->localseparator);
        if(index != string::npos)
            transfer->localfilename.resize(index+1);

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
    pausetime = 0;
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

const char* MegaApiImpl::getMyEmail()
{
	User* u;
    sdkMutex.lock();
	if (!client->loggedin() || !(u = client->finduser(client->me)))
	{
		sdkMutex.unlock();
		return NULL;
	}

    const char *result = MegaApi::strdup(u->email.c_str());
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

const char* MegaApiImpl::getBase64PwKey(const char *password)
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

const char* MegaApiImpl::getStringHash(const char* base64pwkey, const char* inBuf)
{
	if(!base64pwkey || !inBuf) return NULL;

	char pwkey[SymmCipher::KEYLENGTH];
	Base64::atob(base64pwkey, (byte *)pwkey, sizeof pwkey);

	SymmCipher key;
	key.setkey((byte*)pwkey);

	byte strhash[SymmCipher::KEYLENGTH];
	string neBuf = inBuf;

	transform(neBuf.begin(),neBuf.end(),neBuf.begin(),::tolower);
	client->stringhash(neBuf.c_str(),strhash,&key);

	char* buf = new char[8*4/3+4];
	Base64::btoa(strhash,8,buf);
	return buf;
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

const char *MegaApiImpl::handleToBase64(MegaHandle handle)
{
    char *base64Handle = new char[12];
    Base64::btoa((byte*)&(handle),MegaClient::NODEHANDLE,base64Handle);
    return base64Handle;
}

const char *MegaApiImpl::userHandleToBase64(MegaHandle handle)
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

#if !defined(_WIN32) || defined(WINDOWS_PHONE)
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

const char *MegaApiImpl::dumpSession()
{
    sdkMutex.lock();
    byte session[64];
    char* buf = NULL;
    int size;
    size = client->dumpsession(session, sizeof session);
    if (size > 0)
    {
        buf = new char[sizeof(session)*4/3+4];
        Base64::btoa(session, size, buf);
    }

    sdkMutex.unlock();
    return buf;
}

const char *MegaApiImpl::dumpXMPPSession()
{
    sdkMutex.lock();
    byte session[64];
    char* buf = NULL;
    int size;
    size = client->dumpsession(session, sizeof session);
    if (size > sizeof(client->key.key))
    {
        buf = new char[sizeof(session)*4/3+4];
        Base64::btoa(session + sizeof(client->key.key), size - sizeof(client->key.key), buf);
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
#ifdef WINDOWS_PHONE
	// Workaround to get the IP of valid DNS servers on Windows Phone
	struct hostent *hp;
	struct in_addr **addr_list;
	string servers;

	while (true)
	{
		hp = gethostbyname("ns.mega.co.nz");
		if (hp != NULL && hp->h_addr != NULL)
		{
			addr_list = (struct in_addr **)hp->h_addr_list;
			for (int i = 0; addr_list[i] != NULL; i++)
			{
				const char *ip = inet_ntoa(*addr_list[i]);
				if (i > 0) servers.append(",");
				servers.append(ip);
			}

			if (servers.size())
				break;
		}
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}

	httpio->setdnsservers(servers.c_str());
#elif TARGET_OS_IPHONE
    string servers;
    if ((_res.options & RES_INIT) == 0) res_init();

    for (int i = 0; i < _res.nscount; i++)
    {
        const char *ip = inet_ntoa(_res.nsaddr_list[i].sin_addr);
        if (i > 0) servers.append(",");
        servers.append(ip);
    }

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
    delete client->dbaccess; //Warning, it's deleted in MegaClient's destructor
    delete client->sctable;  //Warning, it's deleted in MegaClient's destructor

	//It doesn't seem fully safe to delete those objects :-/
    // delete client;
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
	getUserAttribute(user, 0, dstFilePath, listener);
}

void MegaApiImpl::setAvatar(const char *dstFilePath, MegaRequestListener *listener)
{
	setUserAttribute(0, dstFilePath, listener);
}

void MegaApiImpl::exportNode(MegaNode *node, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_EXPORT, listener);
    if(node) request->setNodeHandle(node->getHandle());
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

void MegaApiImpl::getAccountDetails(MegaRequestListener *listener)
{
    getAccountDetails(true, true, true, false, false, false, listener);
}

void MegaApiImpl::getPricing(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_PRICING, listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getPaymentUrl(handle productHandle, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_PAYMENT_URL, listener);
    request->setNodeHandle(productHandle);
    requestQueue.push(request);
    waiter->notify();
}

const char *MegaApiImpl::exportMasterKey()
{
    sdkMutex.lock();
    byte session[64];
    char* buf = NULL;
    int size;
    size = client->dumpsession(session, sizeof session);
    if (size > 0)
    {
        buf = new char[16*4/3+4];
        Base64::btoa(session, 16, buf);
    }

    sdkMutex.unlock();
    return buf;
}

void MegaApiImpl::getAccountDetails(bool storage, bool transfer, bool pro, bool transactions, bool purchases, bool sessions, MegaRequestListener *listener)
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
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::submitFeedback(int rating, const char *comment, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_REPORT_EVENT, listener);
    request->setParamType(MegaApi::EVENT_FEEDBACK);
    request->setText(comment);
    request->setNumber(rating);
    request->setListener(listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::reportEvent(int event, const char *details, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_REPORT_EVENT, listener);
    request->setParamType(event);
    request->setText(details);
    request->setListener(listener);
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

void MegaApiImpl::getUserAttribute(MegaUser *user, int type, const char *dstFilePath, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_ATTR_USER, listener);
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
            const char *email = user->getEmail();
            path.append(email);
            path.push_back('0' + type);
            path.append(".jpg");
            delete [] email;
        }

        request->setFile(path.c_str());
    }

    request->setParamType(type);
    if(user) request->setEmail(user->getEmail());
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setUserAttribute(int type, const char *srcFilePath, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_ATTR_USER, listener);
	request->setFile(srcFilePath);
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

void MegaApiImpl::pauseTransfers(bool pause, MegaRequestListener* listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_PAUSE_TRANSFERS, listener);
    request->setFlag(pause);
    requestQueue.push(request);
    waiter->notify();
}

//-1 -> AUTO, 0 -> NONE, >0 -> b/s
void MegaApiImpl::setUploadLimit(int bpslimit)
{
    client->putmbpscap = bpslimit;
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

void MegaApiImpl::startUpload(const char *localPath, MegaNode *parent, const char *fileName, int64_t mtime, MegaTransferListener *listener)
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
    if(parent) transfer->setParentHandle(parent->getHandle());
	transfer->setMaxRetries(maxRetries);
	if(fileName) transfer->setFileName(fileName);
    transfer->setTime(mtime);

	transferQueue.push(transfer);
    waiter->notify();
}

void MegaApiImpl::startUpload(const char* localPath, MegaNode* parent, MegaTransferListener *listener)
{ return startUpload(localPath, parent, (const char *)NULL, -1, listener); }

void MegaApiImpl::startUpload(const char *localPath, MegaNode *parent, int64_t mtime, MegaTransferListener *listener)
{ return startUpload(localPath, parent, (const char *)NULL, mtime, listener); }

void MegaApiImpl::startUpload(const char* localPath, MegaNode* parent, const char* fileName, MegaTransferListener *listener)
{ return startUpload(localPath, parent, fileName, -1, listener); }

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
    request->setTransferTag(t->getTag());
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
	if(node) transfer->setNodeHandle(node->getHandle());
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

const char *MegaApiImpl::nameToLocal(const char *name)
{
    string local = name;
    client->fsaccess->name2local(&local);
    return MegaApi::strdup(local.c_str());
}

const char *MegaApiImpl::localToName(const char *localName)
{
    string name;
    client->fsaccess->local2name(&name);
    return MegaApi::strdup(name.c_str());
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
		if ((n = client->nodebyhandle(*sit)))
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
			if ((n = client->nodebyhandle(*sit)))
				vNodes.push_back(n);
		}
	}

    MegaNodeList *nodeList = new MegaNodeListPrivate(vNodes.data(), vNodes.size());
    sdkMutex.unlock();
	return nodeList;
}

bool MegaApiImpl::isShared(MegaNode *megaNode)
{
	if(!megaNode) return false;

	sdkMutex.lock();
	Node *node = client->nodebyhandle(megaNode->getHandle());
	if(!node)
	{
		sdkMutex.unlock();
		return false;
	}

    bool result = (node->outshares != NULL);
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
    accesslevel_t a = FULL;
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
		default: return MegaShare::ACCESS_FULL;
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

const char *MegaApiImpl::getFingerprint(const char *filePath)
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

const char *MegaApiImpl::getFingerprint(MegaNode *n)
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

    fireOnTransferFinish(transfer, MegaError(API_EINCOMPLETE));
}

void MegaApiImpl::transfer_prepare(Transfer *t)
{
    if(transferMap.find(t->tag) == transferMap.end()) return;
    MegaTransferPrivate* transfer = transferMap.at(t->tag);

	if (t->type == GET)
        transfer->setNodeHandle(t->files.front()->h);

    string path;
    fsAccess->local2path(&(t->files.front()->localname), &path);
    transfer->setPath(path.c_str());
    transfer->setTotalBytes(t->size);

    LOG_info << "Transfer (" << transfer->getTransferString() << ") starting. File: " << transfer->getFileName();
}

void MegaApiImpl::transfer_update(Transfer *tr)
{
    if(transferMap.find(tr->tag) == transferMap.end()) return;
    MegaTransferPrivate* transfer = transferMap.at(tr->tag);

    if(tr->slot)
    {
        if((transfer->getUpdateTime() != Waiter::ds) || !tr->slot->progressreported ||
           (tr->slot->progressreported == tr->size))
        {
            if(!transfer->getStartTime()) transfer->setStartTime(Waiter::ds);
            transfer->setDeltaSize(tr->slot->progressreported - transfer->getTransferredBytes());

            if(tr->type == GET)
                totalDownloadedBytes += transfer->getDeltaSize();
            else
                totalUploadedBytes += transfer->getDeltaSize();

            transfer->setTransferredBytes(tr->slot->progressreported);

            dstime currentTime = Waiter::ds;
            if(currentTime<transfer->getStartTime())
                transfer->setStartTime(currentTime);

            long long speed = 0;
            long long deltaTime = currentTime-transfer->getStartTime();
            if(deltaTime<=0)
                deltaTime = 1;
            if(transfer->getTransferredBytes()>0)
                speed = (10*transfer->getTransferredBytes())/deltaTime;

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

#ifdef ENABLE_SYNC
void MegaApiImpl::syncupdate_state(Sync *sync, syncstate_t newstate)
{
    if(newstate == SYNC_FAILED && sync->localroot.node)
    {
        MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_ADD_SYNC);
        request->setNodeHandle(sync->localroot.node->nodehandle);
        int nextTag = client->nextreqtag();
        request->setTag(nextTag);
        requestMap[nextTag]=request;
        fireOnRequestFinish(request, MegaError(API_EFAILED));
    }

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);
    megaSync->setState(newstate);

    fireOnSyncStateChanged(megaSync);
}

void MegaApiImpl::syncupdate_scanning(bool scanning)
{
    if(client) client->syncscanstate = scanning;
    fireOnGlobalSyncStateChanged();
}

void MegaApiImpl::syncupdate_local_folder_addition(Sync *sync, LocalNode *localNode, const char* path)
{
    LOG_debug << "Sync - local folder addition detected: " << path;

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_LOCAL_FOLDER_ADITION);
    event->setPath(path);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_local_folder_deletion(Sync *sync, LocalNode *localNode)
{
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

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_LOCAL_FILE_ADDITION);
    event->setPath(path);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_local_file_deletion(Sync *sync, LocalNode *localNode)
{
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

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_LOCAL_FILE_CHANGED);
    event->setPath(path);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_local_move(Sync *sync, LocalNode *localNode, const char *to)
{
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

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_REMOTE_FILE_ADDITION);
    event->setNodeHandle(n->nodehandle);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_remote_file_deletion(Sync *sync, Node *n)
{
    LOG_debug << "Sync - remote file deletion detected " << n->displayname();

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_REMOTE_FILE_DELETION);
    event->setNodeHandle(n->nodehandle);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_remote_folder_addition(Sync *sync, Node *n)
{
    LOG_debug << "Sync - remote folder addition detected " << n->displayname();

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_REMOTE_FOLDER_ADDITION);
    event->setNodeHandle(n->nodehandle);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_remote_folder_deletion(Sync *sync, Node *n)
{
    LOG_debug << "Sync - remote folder deletion detected " << n->displayname();

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_REMOTE_FOLDER_DELETION);
    event->setNodeHandle(n->nodehandle);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_remote_copy(Sync *, const char *name)
{
    LOG_debug << "Sync - creating remote file " << name << " by copying existing remote file";
}

void MegaApiImpl::syncupdate_remote_move(Sync *sync, Node *n, Node *prevparent)
{
    LOG_debug << "Sync - remote move " << n->displayname() <<
                 " from " << (prevparent ? prevparent->displayname() : "?") <<
                 " to " << (n->parent ? n->parent->displayname() : "?");

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
    const char *name = node->displayname();
    sdkMutex.unlock();
    bool result = is_syncable(name);
    sdkMutex.lock();
    return result;
}

bool MegaApiImpl::sync_syncable(const char *name, string *, string *)
{
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
    }

    this->waiting = waiting;
    this->fireOnGlobalSyncStateChanged();
}
#endif


// user addition/update (users never get deleted)
void MegaApiImpl::users_updated(User** u, int count)
{
    MegaUserList* userList = new MegaUserListPrivate(u, count);
    fireOnUsersUpdate(userList);
    delete userList;
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

    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
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

int MegaApiImpl::fa_failed(handle, fatype, int retries)
{
    int tag = client->restag;
    while(tag)
    {
        if(requestMap.find(tag) == requestMap.end()) return 1;
        MegaRequestPrivate* request = requestMap.at(tag);
        if(!request || (request->getType() != MegaRequest::TYPE_GET_ATTR_FILE))
            return 1;

        tag = request->getNumber();
        if(retries > 3)
        {
            fireOnRequestFinish(request, MegaError(API_EINTERNAL));
        }
        else
        {
            fireOnRequestTemporaryError(request, MegaError(API_EAGAIN));
        }
    }

    return (retries > 3);
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

void MegaApiImpl::enumeratequotaitems_result(handle product, unsigned prolevel, unsigned gbstorage, unsigned gbtransfer, unsigned months, unsigned amount, const char *currency)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_GET_PRICING) &&
                    (request->getType() != MegaRequest::TYPE_GET_PAYMENT_URL)))
    {
        return;
    }

    request->addProduct(product, prolevel, gbstorage, gbtransfer, months, amount, currency);
}

void MegaApiImpl::enumeratequotaitems_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_GET_PRICING) &&
                    (request->getType() != MegaRequest::TYPE_GET_PAYMENT_URL)))
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
                request->setNumber(i);
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
    if(!request || (request->getType() != MegaRequest::TYPE_GET_PAYMENT_URL)) return;

    if(e != API_OK)
    {
        client->purchase_begin();
        fireOnRequestFinish(request, MegaError(e));
    }

    requestMap.erase(request->getTag());
    int nextTag = client->nextreqtag();
    request->setTag(nextTag);
    requestMap[nextTag]=request;
    client->purchase_checkout(1);
}

void MegaApiImpl::checkout_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_GET_PAYMENT_URL)) return;

    fireOnRequestFinish(request, MegaError(e));
}

string urlEncode(const string &value) {
    ostringstream result;
    result.fill('0');
    result << hex;

    for(unsigned int i = 0; i < value.size(); i++)
    {
        char c = value[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            result << c;
        }
        else
        {
            result << '%' << setw(2) << (int)c;
        }
    }

    return result.str();
}

void MegaApiImpl::checkout_result(const char *response)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_GET_PAYMENT_URL)) return;

    MegaPricing *pricing = request->getPricing();
    if(strcmp(response, pricing->getCurrency(request->getNumber())))
    {
        fireOnRequestFinish(request, MegaError(API_EINTERNAL));
        delete pricing;
        return;
    }
    delete pricing;

    client->json.pos++;
    client->json.enterobject();
    ostringstream oss;
    oss << "https://www.paypal.com/cgi-bin/webscr?";
    string buffer;
    int i = 0;
    while(client->json.storeobject(&buffer))
    {
        if (i)
        {
            oss << "&";
        }

        i++;
        oss << buffer;

        client->json.pos++;
        if(!client->json.storeobject(&buffer))
        {
            fireOnRequestFinish(request, MegaError(API_EINTERNAL));
        }

        buffer = urlEncode(buffer);
        oss << "=" << buffer;
    }


    request->setLink(oss.str().c_str());
    fireOnRequestFinish(request, MegaError(API_OK));
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
    if(e == API_ENOENT)
    {
        fetchNodes();
        return;
    }

    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_LOGOUT);
    request->setParamType(e);
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

		string link = "https://mega.co.nz/#";
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

    if(key)
    {
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
    }
    else fileName = "NO_KEY";

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
    MegaNodeList *nodeList = NULL;
    if(n != NULL)
    {
        if(count)
        {
            nodeList = new MegaNodeListPrivate(n, count);
            fireOnNodesUpdate(nodeList);
        }
    }
    else
    {
        fireOnNodesUpdate(NULL);
    }
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
        fireOnRequestFinish(request, MegaError(MegaError::API_OK));
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
    //TODO: Support user attribute changes
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

    client->logout();
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
    if(!megaNode || !level)	return MegaError(API_EARGS);

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

const char* MegaApiImpl::getNodePath(MegaNode *node)
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

                if (l)
                {
                    break;
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

                if(!localPath || !parent)
                {
                    e = API_EARGS;
                    break;
                }

                currentTransfer = transfer;
				string tmpString = localPath;
				string wLocalPath;
				client->fsaccess->path2local(&tmpString, &wLocalPath);

                string wFileName = fileName;
                MegaFilePut *f = new MegaFilePut(client, &wLocalPath, &wFileName, transfer->getParentHandle(), "", mtime);

                bool started = client->startxfer(PUT,f);
                if(!started)
                {
                    //Unable to read the file
                    transfer->setSyncTransfer(false);
                    transferMap[nextTag]=transfer;
                    transfer->setTag(nextTag);
                    fireOnTransferStart(transfer);
                    fireOnTransferFinish(transfer, MegaError(API_EREAD));
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
                            name = node->displayname();
                        else
                            name = fileName;

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
					client->startxfer(GET,f);
                    if(transfer->getTag() == -1)
                    {
                        //Already existing transfer
                        //Delete the new one and set the transfer as regular
                        transfer_map::iterator it = client->transfers[GET].find(f);
                        if(it != client->transfers[GET].end())
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
                }
                else
                {
                	m_off_t startPos = transfer->getStartPos();
                	m_off_t endPos = transfer->getEndPos();
                	if(startPos < 0 || endPos < 0 || startPos > endPos) { e = API_EARGS; break; }
                	if(node)
                	{
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
                		{ e = API_EARGS; break; }
                		//TODO: Implement streaming of public nodes
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

            if(!megaFolderLink && (!login || !password) && !sessionKey && (!login || !base64pwkey))
            {
                e = API_EARGS;
                break;
            }

            if(base64pwkey)
            {
                byte pwkey[SymmCipher::KEYLENGTH];
                Base64::atob(base64pwkey, (byte *)pwkey, sizeof pwkey);
                client->login(login, pwkey);
            }
            else if(sessionKey)
            {
                byte session[sizeof client->key.key + MegaClient::SIDLEN];
                Base64::atob(sessionKey, (byte *)session, sizeof session);
                client->login(session, sizeof session);
            }
            else if(megaFolderLink)
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
            else
            {
                byte pwkey[SymmCipher::KEYLENGTH];
                if((e = client->pw_key(password,pwkey))) break;
                client->login(login, pwkey);
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

            if((!node && !publicNode) || (!target && !email)) { e = API_EARGS; break; }

            if(publicNode)
            {
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
                    client->putnodes(target->nodehandle,newnode, 1);
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
			if(!node || !newName) { e = API_EARGS; break; }

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

            e = client->exportnode(node, !request->getAccess());
			break;
		}
		case MegaRequest::TYPE_FETCH_NODES:
		{
			client->fetchnodes();
			break;
		}
		case MegaRequest::TYPE_ACCOUNT_DETAILS:
		{
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
            int errorCode = request->getParamType();
            requestMap.erase(nextTag);
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

			client->logout();

            pausetime = 0;
            pendingUploads = 0;
            pendingDownloads = 0;
            totalUploads = 0;
            totalDownloads = 0;
            waiting = false;
            waitingRequest = false;
            requestMap[nextTag] = request;
            fireOnRequestFinish(request, MegaError(errorCode));
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
			const char* dstFilePath = request->getFile();
            int type = request->getParamType();
            User *user = client->finduser(request->getEmail(), 0);

			if(!dstFilePath || !user || (type != 0)) { e = API_EARGS; break; }

			client->getua(user, "a", false);
			break;
		}
		case MegaRequest::TYPE_SET_ATTR_USER:
		{
			const char* srcFilePath = request->getFile();
            int type = request->getParamType();

			if (!srcFilePath || (type != 0)) { e = API_EARGS; break; }

			string path = srcFilePath;
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

			client->putua("a", (byte *)attributedata.data(), attributedata.size(), false);
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

			string attributedata;
			FileAccess *f = fsAccess->newfileaccess();
			if (!f->fopen(&localpath, 1, 0))
			{
				delete f;
				e = API_EREAD;
				break;
			}

			if(!f->fread(&attributedata, f->size, 0, 0))
			{
				delete f;
				e = API_EREAD;
				break;
			}
			delete f;

            client->putfa(node->nodehandle, type, node->nodecipher(), &attributedata);
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
				client->disconnect();

			fireOnRequestFinish(request, MegaError(API_OK));
			break;
		}
		case MegaRequest::TYPE_ADD_CONTACT:
		{
			const char *email = request->getEmail();
			if(!email) { e = API_EARGS; break; }
			e = client->invite(email, VISIBLE);
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
            if(pause)
            {
                if(!pausetime) pausetime = Waiter::ds;
            }
            else if(pausetime)
            {
                for(std::map<int, MegaTransferPrivate *>::iterator iter = transferMap.begin(); iter != transferMap.end(); iter++)
                {
                    MegaTransfer *transfer = iter->second;
                    if(!transfer)
                        continue;

                    m_time_t starttime = transfer->getStartTime();
                    if(starttime)
                    {
						m_time_t timepaused = Waiter::ds - pausetime;
                        iter->second->setStartTime(starttime + timepaused);
                    }
                }
                pausetime = 0;
            }

            client->pausexfers(PUT, pause);
            client->pausexfers(GET, pause);
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
                    transferMap.at(transfer->tag)->setSyncTransfer(true);

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
                    client->delsync(sync);

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
            int evtType = request->getParamType();
            const char *details = request->getText();
            int number = request->getNumber();

            if(evtType < 0 || evtType >= MegaApi::EVENT_INVALID)
            {
                e = API_EARGS;
                break;
            }

            string event;
            switch(evtType)
            {
                case MegaApi::EVENT_FEEDBACK:
                    event = "F";
                    if(number < 1 || number > 5)
                        e = API_EARGS;
                    else
                        event.append(1, '0'+number);
                    break;
                case MegaApi::EVENT_DEBUG:
                    event = "A"; //Application event
                    break;
                default:
                    e = API_EINTERNAL;
            }

            if(!e)
                client->reportevent(event.c_str(), details);
            break;
        }
        case MegaRequest::TYPE_DELETE:
        {
            threadExit = 1;
            break;
        }
        case MegaRequest::TYPE_GET_PRICING:
        case MegaRequest::TYPE_GET_PAYMENT_URL:
        {
            client->purchase_enumeratequotaitems();
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
    return details->pro_level;
}

long long MegaAccountDetailsPrivate::getStorageMax()
{
    return details->storage_max;
}

long long MegaAccountDetailsPrivate::getStorageUsed()
{
    return details->storage_used;
}

long long MegaAccountDetailsPrivate::getTransferMax()
{
    return details->transfer_max;
}

long long MegaAccountDetailsPrivate::getTransferOwnUsed()
{
    return details->transfer_own_used;
}

long long MegaAccountDetailsPrivate::getStorageUsed(MegaHandle handle)
{
    return details->storage[handle].bytes;
}

long long MegaAccountDetailsPrivate::getNumFiles(MegaHandle handle)
{
    return details->storage[handle].files;
}

long long MegaAccountDetailsPrivate::getNumFolders(MegaHandle handle)
{
    return details->storage[handle].folders;
}

MegaAccountDetails* MegaAccountDetailsPrivate::copy()
{
	return new MegaAccountDetailsPrivate(details);
}

ExternalLogger::ExternalLogger()
{
	mutex.init(true);
	this->megaLogger = NULL;
	SimpleLogger::setOutputClass(this);
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

	SimpleLogger((LogLevel)logLevel, filename, line) << message;
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

MegaPricingPrivate::~MegaPricingPrivate()
{
    for(unsigned i = 0; i < currency.size(); i++)
    {
        delete[] currency[i];
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

MegaPricing *MegaPricingPrivate::copy()
{
    MegaPricingPrivate *megaPricing = new MegaPricingPrivate();
    for(unsigned i=0; i<handles.size(); i++)
    {
        megaPricing->addProduct(handles[i], proLevel[i], gbStorage[i], gbTransfer[i],
                                months[i], amount[i], currency[i]);
    }

    return megaPricing;
}

void MegaPricingPrivate::addProduct(handle product, int proLevel, int gbStorage, int gbTransfer, int months, int amount, const char *currency)
{
    this->handles.push_back(product);
    this->proLevel.push_back(proLevel);
    this->gbStorage.push_back(gbStorage);
    this->gbTransfer.push_back(gbTransfer);
    this->months.push_back(months);
    this->amount.push_back(amount);
    this->currency.push_back(MegaApi::strdup(currency));
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

MegaSyncPrivate::MegaSyncPrivate(MegaSyncPrivate &sync)
{
    this->setTag(sync.getTag());
    this->setLocalFolder(sync.getLocalFolder());
    this->setMegaHandle(sync.getMegaHandle());
    this->setLocalFingerprint(sync.getLocalFingerprint());
    this->setState(sync.getState());
    this->setListener(sync.getListener());
}

MegaSyncPrivate::~MegaSyncPrivate()
{
}

MegaSync *MegaSyncPrivate::copy()
{
    return new MegaSyncPrivate(*this);
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
    delete path;
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
