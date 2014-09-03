#define _POSIX_SOURCE
#define _LARGE_FILES

#ifndef _WIN32
#define _LARGEFILE64_SOURCE
#include <signal.h>
#endif

#define _GNU_SOURCE 1
#define _FILE_OFFSET_BITS 64

#define __DARWIN_C_LEVEL 199506L

#define USE_VARARGS
#define PREFER_STDARG
#include "megaapi_impl.h"
#include "megaapi.h"

#ifdef __APPLE__
    #include "xlocale.h"
    #include "strings.h"
#endif

#ifdef _WIN32
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif

using namespace mega;

extern bool debug;

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
    this->removed = false;
    this->syncdeleted = false;
    this->thumbnailAvailable = false;
    this->previewAvailable = false;
    this->tag = 0;
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
    this->removed = node->isRemoved();
    this->syncdeleted = node->isSyncDeleted();
    this->thumbnailAvailable = node->hasThumbnail();
    this->previewAvailable = node->hasPreview();
    this->tag = node->getTag();
    this->localPath = node->getLocalPath();
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
    this->attrstring.assign(node->attrstring.data(), node->attrstring.size());
    this->nodekey.assign(node->nodekey.data(),node->nodekey.size());
    this->removed = node->removed;
	this->syncdeleted = (node->syncdeleted != SYNCDEL_NONE);
    this->thumbnailAvailable = (node->hasfileattribute(0) != 0);
    this->previewAvailable = (node->hasfileattribute(1) != 0);
    this->tag = node->tag;
    if(node->localnode)
    {
        node->localnode->getlocalpath(&localPath, true);
        localPath.append("", 1);
    }
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

string *MegaNodePrivate::getAttrString()
{
	return &attrstring;
}

int MegaNodePrivate::getTag()
{
    return tag;
}

bool MegaNodePrivate::isRemoved()
{
    return removed;
}

bool MegaNodePrivate::isFile()
{
	return type == TYPE_FILE;
}

bool MegaNodePrivate::isFolder()
{
	return (type != TYPE_FILE) && (type != TYPE_UNKNOWN);
}

bool MegaNodePrivate::isSyncDeleted()
{
    return syncdeleted;
}

string MegaNodePrivate::getLocalPath()
{
    return localPath;
}

bool MegaNodePrivate::hasThumbnail()
{
	return thumbnailAvailable;
}

bool MegaNodePrivate::hasPreview()
{
	return previewAvailable;
}


MegaNodePrivate::~MegaNodePrivate()
{
 	delete name;
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
	this->slot = -1;
	this->tag = -1;
	this->path = NULL;
	this->nodeHandle = UNDEF;
	this->parentHandle = UNDEF;
	this->startPos = 0;
	this->endPos = 0;
	this->numConnections = 1;
	this->maxSpeed = 1;
	this->parentPath = NULL;
	this->listener = listener;
	this->retry = 0;
	this->maxRetries = 3;
	this->time = 0;
	this->startTime = 0;
	this->transferredBytes = 0;
	this->totalBytes = 0;
	this->fileName = NULL;
	this->base64Key = NULL;
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
    base64Key = NULL;
    publicNode = NULL;
	lastBytes = NULL;

    this->listener = transfer.getListener();
    this->transfer = transfer.getTransfer();
	this->type = transfer.getType();
	this->setSlot(transfer.getSlot());
	this->setTag(transfer.getTag());
	this->setPath(transfer.getPath());
	this->setNodeHandle(transfer.getNodeHandle());
	this->setParentHandle(transfer.getParentHandle());
	this->setStartPos(transfer.getStartPos());
	this->setEndPos(transfer.getEndPos());
	this->setNumConnections(transfer.getNumConnections());
	this->setMaxSpeed(transfer.getMaxSpeed());
	this->setParentPath(transfer.getParentPath());
	this->setNumRetry(transfer.getNumRetry());
	this->setMaxRetries(transfer.getMaxRetries());
	this->setTime(transfer.getTime());
	this->setStartTime(transfer.getStartTime());
	this->setTransferredBytes(transfer.getTransferredBytes());
	this->setTotalBytes(transfer.getTotalBytes());
	this->setFileName(transfer.getFileName());
	this->setBase64Key(transfer.getBase64Key());
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

int MegaTransferPrivate::getSlot() const
{
	return slot;
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

int MegaTransferPrivate::getNumConnections() const
{
	return numConnections;
}

long long MegaTransferPrivate::getStartPos() const
{
	return startPos;
}

long long MegaTransferPrivate::getEndPos() const
{
	return endPos;
}

int MegaTransferPrivate::getMaxSpeed() const
{
	return maxSpeed;
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

const char* MegaTransferPrivate::getBase64Key() const
{
	return base64Key;
}

char * MegaTransferPrivate::getLastBytes() const
{
	return lastBytes;
}

void MegaTransferPrivate::setSlot(int slot)
{
	this->slot = slot;
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
    if(this->publicNode) delete this->publicNode;
    if(!publicNode) this->publicNode = NULL;
    else this->publicNode = publicNode->copy();
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

void MegaTransferPrivate::setBase64Key(const char* base64Key)
{
	if(this->base64Key) delete [] this->base64Key;
    this->base64Key =  MegaApi::strdup(base64Key);
}

void MegaTransferPrivate::setNodeHandle(uint64_t nodeHandle)
{
	this->nodeHandle = nodeHandle;
}

void MegaTransferPrivate::setParentHandle(uint64_t parentHandle)
{
	this->parentHandle = parentHandle;
}

void MegaTransferPrivate::setNumConnections(int connections)
{
	this->numConnections = connections;
}

void MegaTransferPrivate::setStartPos(long long startPos)
{
	this->startPos = startPos;
}

void MegaTransferPrivate::setEndPos(long long endPos)
{
	this->endPos = endPos;
}

void MegaTransferPrivate::setMaxSpeed(int maxSpeed)
{
	this->maxSpeed = maxSpeed;
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
		return "upload";
	case TYPE_DOWNLOAD:
		return "download";
	}

	return "unknown";
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
    delete [] base64Key;
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
	this->transfer = 0;
	this->listener = listener;
	this->nodeHandle = UNDEF;
	this->link = NULL;
	this->parentHandle = UNDEF;
    this->sessionKey = NULL;
	this->name = NULL;
	this->email = NULL;
	this->password = NULL;
	this->newPassword = NULL;
	this->privateKey = NULL;
	this->access = MegaShare::ACCESS_UNKNOWN;
	this->numRetry = 0;
	this->nextRetryDelay = 0;
	this->publicNode = NULL;
	this->numDetails = 0;
	this->file = NULL;
	this->attrType = 0;
    this->flag = false;
    this->totalBytes = -1;
    this->transferredBytes = 0;

	if(type == MegaRequest::TYPE_ACCOUNT_DETAILS) this->accountDetails = new AccountDetails();
	else this->accountDetails = NULL;
}

MegaRequestPrivate::MegaRequestPrivate(MegaRequestPrivate &request)
{
    this->link = NULL;
    this->sessionKey = NULL;
    this->name = NULL;
    this->email = NULL;
    this->password = NULL;
    this->newPassword = NULL;
    this->privateKey = NULL;
    this->access = MegaShare::ACCESS_UNKNOWN;
    this->publicNode = NULL;
    this->file = NULL;
    this->publicNode = NULL;

    this->type = request.getType();
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
	this->setNextRetryDelay(request.getNextRetryDelay());
	this->numDetails = 0;
	this->setFile(request.getFile());
    this->setParamType(request.getParamType());
    this->setPublicNode(request.getPublicNode());
    this->setFlag(request.getFlag());
    this->setTransfer(request.getTransfer());
    this->setTotalBytes(request.getTotalBytes());
    this->setTransferredBytes(request.getTransferredBytes());
	this->listener = request.getListener();
	this->accountDetails = NULL;
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

int MegaRequestPrivate::getNextRetryDelay() const
{
	return nextRetryDelay;
}

int MegaRequestPrivate::getNumDetails() const
{
	return numDetails;
}

void MegaRequestPrivate::setNumDetails(int numDetails)
{
	this->numDetails = numDetails;
}

MegaNode *MegaRequestPrivate::getPublicNode()
{
	return publicNode;
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

void MegaRequestPrivate::setNextRetryDelay(int nextRetryDelay)
{
	this->nextRetryDelay = nextRetryDelay;
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

void MegaRequestPrivate::setFlag(bool flag)
{
    this->flag = flag;
}

void MegaRequestPrivate::setTransfer(int transfer)
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
		case TYPE_LOGIN: return "login";
		case TYPE_MKDIR: return "mkdir";
		case TYPE_MOVE: return "move";
		case TYPE_COPY: return "copy";
		case TYPE_RENAME: return "rename";
		case TYPE_REMOVE: return "remove";
		case TYPE_SHARE: return "share";
		case TYPE_FOLDER_ACCESS: return "folderaccess";
		case TYPE_IMPORT_LINK: return "importlink";
		case TYPE_IMPORT_NODE: return "importnode";
		case TYPE_EXPORT: return "export";
		case TYPE_FETCH_NODES: return "fetchnodes";
		case TYPE_ACCOUNT_DETAILS: return "accountdetails";
		case TYPE_CHANGE_PW: return "changepw";
		case TYPE_UPLOAD: return "upload";
		case TYPE_LOGOUT: return "logout";
		case TYPE_FAST_LOGIN: return "fastlogin";
		case TYPE_GET_PUBLIC_NODE: return "getpublicnode";
		case TYPE_GET_ATTR_FILE: return "getattrfile";
        case TYPE_SET_ATTR_FILE: return "setattrfile";
        case TYPE_GET_ATTR_USER: return "getattruser";
        case TYPE_SET_ATTR_USER: return "setattruser";
        case TYPE_RETRY_PENDING_CONNECTIONS: return "retrypending";
        case TYPE_ADD_CONTACT: return "addcontact";
        case TYPE_REMOVE_CONTACT: return "removecontact";
        case TYPE_CREATE_ACCOUNT: return "createaccount";
        case TYPE_FAST_CREATE_ACCOUNT: return "fastcreateaccount";
        case TYPE_CONFIRM_ACCOUNT: return "confirmaccount";
        case TYPE_FAST_CONFIRM_ACCOUNT: return "fastconfirmaccount";
        case TYPE_QUERY_SIGNUP_LINK: return "querysignuplink";
        case TYPE_ADD_SYNC: return "addsync";
        case TYPE_REMOVE_SYNC: return "removesync";
        case TYPE_REMOVE_SYNCS: return "removesyncs";
        case TYPE_PAUSE_TRANSFERS: return "pausetransfers";
        case TYPE_CANCEL_TRANSFER: return "canceltransfer";
        case TYPE_CANCEL_TRANSFERS: return "canceltransfers";
        case TYPE_DELETE: return "delete";
	}
	return "unknown";
}

MegaRequestListener *MegaRequestPrivate::getListener() const
{
	return listener;
}

int MegaRequestPrivate::getTransfer() const
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

NodeListPrivate::NodeListPrivate()
{
	list = NULL;
	s = 0;
}

NodeListPrivate::NodeListPrivate(Node** newlist, int size)
{
	list = NULL; s = size;
	if(!size) return;

	list = new MegaNode*[size];
	for(int i=0; i<size; i++)
		list[i] = MegaNodePrivate::fromNode(newlist[i]);
}

NodeListPrivate::~NodeListPrivate()
{
	if(!list)
		return;

	for(int i=0; i<s; i++)
		delete list[i];
	delete [] list;
}

MegaNode *NodeListPrivate::get(int i)
{
	if(!list || (i < 0) || (i >= s))
		return NULL;

	return list[i];
}

int NodeListPrivate::size()
{
	return s;
}

UserListPrivate::UserListPrivate()
{
	list = NULL;
	s = 0;
}

UserListPrivate::UserListPrivate(User** newlist, int size)
{
	list = NULL;
	s = size;

	if(!size)
		return;

	list = new MegaUser*[size];
	for(int i=0; i<size; i++)
		list[i] = MegaUserPrivate::fromUser(newlist[i]);
}

UserListPrivate::~UserListPrivate()
{
	if(!list)
		return;

	for(int i=0; i<s; i++)
		delete list[i];

	delete [] list;
}

MegaUser *UserListPrivate::get(int i)
{
	if(!list || (i < 0) || (i >= s))
		return NULL;

	return list[i];
}

int UserListPrivate::size()
{
	return s;
}


ShareListPrivate::ShareListPrivate()
{
	list = NULL;
	s = 0;
}

ShareListPrivate::ShareListPrivate(Share** newlist, uint64_t *uint64_tlist, int size)
{
	list = NULL; s = size;
	if(!size) return;

	list = new MegaShare*[size];
	for(int i=0; i<size; i++)
        list[i] = MegaSharePrivate::fromShare(uint64_tlist[i], newlist[i]);
}

ShareListPrivate::~ShareListPrivate()
{
	if(!list)
		return;

	for(int i=0; i<s; i++)
		delete list[i];

	delete [] list;
}

MegaShare *ShareListPrivate::get(int i)
{
	if(!list || (i < 0) || (i >= s))
		return NULL;

	return list[i];
}

int ShareListPrivate::size()
{
	return s;
}

TransferListPrivate::TransferListPrivate()
{
	list = NULL;
	s = 0;
}

TransferListPrivate::TransferListPrivate(MegaTransfer** newlist, int size)
{
	list = NULL;
	s = size;

	if(!size)
		return;

    list = new MegaTransfer*[size];
	for(int i=0; i<size; i++)
		list[i] = newlist[i]->copy();
}

TransferListPrivate::~TransferListPrivate()
{
	if(!list)
		return;

	for(int i=0; i<s; i++)
		delete list[i];

	delete [] list;
}

MegaTransfer *TransferListPrivate::get(int i)
{
	if(!list || (i < 0) || (i >= s))
		return NULL;

	return list[i];
}

int TransferListPrivate::size()
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

void MegaFileGet::completed(Transfer*, LocalNode*)
{
    delete this;
}

MegaFilePut::MegaFilePut(MegaClient *client, string* clocalname, string *filename, handle ch, const char* ctargetuser) : MegaFile()
{
    // full local path
    localname = *clocalname;

    // target parent node
    h = ch;

    // target user
    targetuser = ctargetuser;

    // new node name
    name = *filename;
}

void MegaFilePut::completed(Transfer* t, LocalNode*)
{
    File::completed(t,NULL);
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

#ifdef SHOW_LOGS
    debug = true;
#else
    debug = false;
#endif

    sdkMutex.init(true);
	maxRetries = 5;
	currentTransfer = NULL;
    pausetime = 0;
    pendingUploads = 0;
    pendingDownloads = 0;
    totalUploads = 0;
    totalDownloads = 0;
    client = NULL;
    waiting = false;
    waitingRequest = false;
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
		#ifndef WINDOWS_PHONE
			gfxAccess = new MegaGfxProc();
		#endif
	}

    client = new MegaClient(this, waiter, httpio, fsAccess, dbAccess, gfxAccess, appKey, userAgent);

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
		return NULL;

    const char *result = MegaApi::strdup(u->email.c_str());
    sdkMutex.unlock();
	return result;
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

void MegaApiImpl::retryPendingConnections()
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_RETRY_PENDING_CONNECTIONS);
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::fastLogin(const char* email, const char *stringHash, const char *base64pwkey, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_FAST_LOGIN, listener);
	request->setEmail(email);
	request->setPassword(stringHash);
	request->setPrivateKey(base64pwkey);
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::fastLogin(const char *session, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_FAST_LOGIN, listener);
    request->setSessionKey(session);
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
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_FAST_CREATE_ACCOUNT, listener);
	request->setEmail(email);
	request->setPassword(base64pwkey);
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
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_FAST_CONFIRM_ACCOUNT, listener);
	request->setLink(link);
	request->setPassword(base64pwkey);
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
    localurl.append("", 1);
    localProxySettings.setProxyURL(&localurl);

    if(proxySettings->credentialsNeeded())
    {
        string username;
        if(proxySettings->getUsername())
            username = proxySettings->getUsername();

        string localusername;
        fsAccess->path2local(&username, &localusername);
        localusername.append("", 1);

        string password;
        if(proxySettings->getPassword())
            password = proxySettings->getPassword();

        string localpassword;
        fsAccess->path2local(&password, &localpassword);
        localpassword.append("", 1);

        localProxySettings.setCredentials(&localusername, &localpassword);
    }

    httpio->setproxy(&localProxySettings);
}

MegaProxy *MegaApiImpl::getAutoProxySettings()
{
    MegaProxy *proxySettings = new MegaProxy;
    Proxy *localProxySettings = httpio->getautoproxy();
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
    while(true)
	{
        int r = client->wait();
        if(r & Waiter::NEEDEXEC)
        {
            sdkMutex.lock();
            sendPendingTransfers();
            sendPendingRequests();
            if(threadExit)
                break;

            client->exec();
            sdkMutex.unlock();
        }
	}

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
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_MKDIR, listener);
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
    if(node) request->setNodeHandle(node->getHandle());
    if(target) request->setParentHandle(target->getHandle());
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

void MegaApiImpl::folderAccess(const char* megaFolderLink, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_FOLDER_ACCESS, listener);
	request->setLink(megaFolderLink);
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

void MegaApiImpl::importPublicNode(MegaNode *publicNode, MegaNode* parent, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_IMPORT_NODE, listener);
    request->setPublicNode(publicNode);
    if(parent)	request->setParentHandle(parent->getHandle());
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

void MegaApiImpl::setThumbnail(MegaNode* node, const char *srcFilePath, MegaRequestListener *listener)
{
	setNodeAttribute(node, 0, srcFilePath, listener);
}

void MegaApiImpl::getPreview(MegaNode* node, const char *dstFilePath, MegaRequestListener *listener)
{
	getNodeAttribute(node, 1, dstFilePath, listener);
}

void MegaApiImpl::setPreview(MegaNode* node, const char *srcFilePath, MegaRequestListener *listener)
{
	setNodeAttribute(node, 1, srcFilePath, listener);
}

void MegaApiImpl::getUserAvatar(MegaUser* user, const char *dstFilePath, MegaRequestListener *listener)
{
	getUserAttribute(user, 0, dstFilePath, listener);
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

void MegaApiImpl::getNodeAttribute(MegaNode *node, int type, const char *dstFilePath, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_ATTR_FILE, listener);
	request->setFile(dstFilePath);
    request->setParamType(type);
    if(node) request->setNodeHandle(node->getHandle());
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
	request->setFile(dstFilePath);
    request->setParamType(type);
    if(user) request->setEmail(user->getEmail());
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setUserAttribute(MegaUser *user, int type, const char *srcFilePath, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_ATTR_USER, listener);
	request->setFile(srcFilePath);
    request->setParamType(type);
    if(user) request->setEmail(user->getEmail());
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

void MegaApiImpl::removeContact(const char* email, MegaRequestListener* listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_REMOVE_CONTACT, listener);
	request->setEmail(email);
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

TransferList *MegaApiImpl::getTransfers()
{
    sdkMutex.lock();

    vector<MegaTransfer *> transfers;
    for (map<int, MegaTransferPrivate *>::iterator it = transferMap.begin(); it != transferMap.end(); it++)
    	transfers.push_back(it->second);

    TransferList *result = new TransferListPrivate(transfers.data(), transfers.size());

    sdkMutex.unlock();
    return result;
}

void MegaApiImpl::startUpload(const char* localPath, MegaNode* parent, int connections, int maxSpeed, const char* fileName, MegaTransferListener *listener)
{
	MegaTransferPrivate* transfer = new MegaTransferPrivate(MegaTransfer::TYPE_UPLOAD, listener);
    if(localPath)
    {
        string path(localPath);
#ifdef _WIN32
        if((path.size()<2) || path.compare(0, 2, "\\\\"))
            path.insert(0, "\\\\?\\");
#endif
        transfer->setPath(path.data());
    }
    if(parent) transfer->setParentHandle(parent->getHandle());
	transfer->setNumConnections(connections);
	transfer->setMaxSpeed(maxSpeed);
	transfer->setMaxRetries(maxRetries);
	if(fileName) transfer->setFileName(fileName);

	transferQueue.push(transfer);
    waiter->notify();
}

void MegaApiImpl::startUpload(const char* localPath, MegaNode* parent, MegaTransferListener *listener)
{ return startUpload(localPath, parent, 1, 0, (const char *)NULL, listener); }

void MegaApiImpl::startUpload(const char* localPath, MegaNode* parent, const char* fileName, MegaTransferListener *listener)
{ return startUpload(localPath, parent, 1, 0, fileName, listener); }

void MegaApiImpl::startUpload(const char* localPath, MegaNode* parent, int maxSpeed, MegaTransferListener *listener)
{ return startUpload(localPath, parent, 1, maxSpeed, (const char *)NULL, listener); }

void MegaApiImpl::startDownload(handle nodehandle, const char* localPath, int connections, long startPos, long endPos, const char* base64key, MegaTransferListener *listener)
{
	MegaTransferPrivate* transfer = new MegaTransferPrivate(MegaTransfer::TYPE_DOWNLOAD, listener);

    if(localPath)
    {
#ifdef _WIN32
        string path(localPath);
        if((path.size()<2) || path.compare(0, 2, "\\\\"))
            path.insert(0, "\\\\?\\");
        localPath = path.data();
#endif

        int c = localPath[strlen(localPath)-1];
        if((c=='/') || (c == '\\')) transfer->setParentPath(localPath);
        else transfer->setPath(localPath);
    }

	transfer->setNodeHandle(nodehandle);
	transfer->setBase64Key(base64key);
	transfer->setNumConnections(connections);
	transfer->setStartPos(startPos);
	transfer->setEndPos(endPos);
	transfer->setMaxRetries(maxRetries);

	transferQueue.push(transfer);
	waiter->notify();
}

void MegaApiImpl::startDownload(MegaNode *node, const char* localFolder, MegaTransferListener *listener)
{ startDownload((node != NULL) ? node->getHandle() : UNDEF, localFolder, 1, 0, 0, NULL, listener); }

void MegaApiImpl::startPublicDownload(MegaNode* node, const char* localPath, MegaTransferListener *listener)
{
	MegaTransferPrivate* transfer = new MegaTransferPrivate(MegaTransfer::TYPE_DOWNLOAD, listener);
    if(localPath)
    {
        string path(localPath);
#ifdef _WIN32
        if((path.size()<2) || path.compare(0, 2, "\\\\"))
            path.insert(0, "\\\\?\\");
#endif
        transfer->setParentPath(path.data());
    }

    if(node)
    {
        transfer->setNodeHandle(node->getHandle());
        transfer->setPublicNode(node);
    }

	transferQueue.push(transfer);
    waiter->notify();
}

void MegaApiImpl::cancelTransfer(MegaTransfer *t, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CANCEL_TRANSFER, listener);
    request->setTransfer(t->getTag());
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

//Move local files inside synced folders to the "Rubbish" folder.
bool MegaApiImpl::moveToLocalDebris(const char *path)
{
    sdkMutex.lock();

    string utf8path = path;
#ifdef _WIN32
        if((utf8path.size()<2) || utf8path.compare(0, 2, "\\\\"))
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
#ifdef _WIN32
    string prefix("\\\\?\\");
    string localPrefix;
    fsAccess->path2local(&prefix, &localPrefix);
    if(path->size()<4 || memcmp(path->data(), localPrefix.data(), 4))
        path->insert(0, localPrefix);
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
        LocalNode * localNode = sync->localnodebypath(NULL, path);
        if(localNode) node = MegaNodePrivate::fromNode(localNode->node);
    }
    sdkMutex.unlock();
    return node;
}

void MegaApiImpl::syncFolder(const char *localFolder, MegaNode *megaFolder)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_ADD_SYNC);
    if(megaFolder) request->setNodeHandle(megaFolder->getHandle());
    if(localFolder)
    {
        string path(localFolder);
#ifdef _WIN32
        if((path.size()<2) || path.compare(0, 2, "\\\\"))
            path.insert(0, "\\\\?\\");
#endif
        request->setFile(path.data());
        path.clear();
    }
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::resumeSync(const char *localFolder, MegaNode *megaFolder)
{
    sdkMutex.lock();

    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_ADD_SYNC);
    if(megaFolder) request->setNodeHandle(megaFolder->getHandle());
    if(localFolder)
    {
        string path(localFolder);
#ifdef _WIN32
        if((path.size()<2) || path.compare(0, 2, "\\\\"))
            path.insert(0, "\\\\?\\");
#endif
        request->setFile(path.data());
        path.clear();
    }

    int nextTag = client->nextreqtag();
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
        e = client->addsync(&localname, DEBRISFOLDER, NULL, node, -1);
    }

    int prevTag = client->restag;
    client->restag = nextTag;
    fireOnRequestFinish(request, MegaError(e));
    client->restag = prevTag;
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


UserList* MegaApiImpl::getContacts()
{
    sdkMutex.lock();

	vector<User*> vUsers;
	for (user_map::iterator it = client->users.begin() ; it != client->users.end() ; it++ )
	{
		User *u = &(it->second);
        vector<User *>::iterator i = std::lower_bound(vUsers.begin(), vUsers.end(), u, MegaApiImpl::userComparatorDefaultASC);
		vUsers.insert(i, u);
	}
    UserList *userList = new UserListPrivate(vUsers.data(), vUsers.size());

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


NodeList* MegaApiImpl::getInShares(MegaUser *megaUser)
{
    if(!megaUser) return new NodeListPrivate();

    sdkMutex.lock();
    vector<Node*> vNodes;
    User *user = client->finduser(megaUser->getEmail(), 0);
    if(!user)
    {
        sdkMutex.unlock();
        return new NodeListPrivate();
    }

	for (handle_set::iterator sit = user->sharing.begin(); sit != user->sharing.end(); sit++)
	{
        Node *n;
		if ((n = client->nodebyhandle(*sit)))
            vNodes.push_back(n);
	}
	NodeList *nodeList;
    if(vNodes.size()) nodeList = new NodeListPrivate(vNodes.data(), vNodes.size());
    else nodeList = new NodeListPrivate();

    sdkMutex.unlock();
	return nodeList;
}

NodeList* MegaApiImpl::getInShares()
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

	NodeList *nodeList = new NodeListPrivate(vNodes.data(), vNodes.size());
    sdkMutex.unlock();
	return nodeList;
}


ShareList* MegaApiImpl::getOutShares(MegaNode *megaNode)
{
    if(!megaNode) return new ShareListPrivate();

    sdkMutex.lock();
	Node *node = client->nodebyhandle(megaNode->getHandle());
	if(!node)
	{
        sdkMutex.unlock();
        return new ShareListPrivate();
	}

	vector<Share*> vShares;
	vector<handle> vHandles;

	for (share_map::iterator it = node->outshares.begin(); it != node->outshares.end(); it++)
	{
		vShares.push_back(it->second);
		vHandles.push_back(node->nodehandle);
	}

    ShareList *shareList = new ShareListPrivate(vShares.data(), vHandles.data(), vShares.size());
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

NodeList* MegaApiImpl::search(MegaNode* n, const char* searchString, bool recursive)
{
	if(!n || !searchString) return new NodeListPrivate();
    sdkMutex.lock();
	Node *node = client->nodebyhandle(n->getHandle());
	if(!node)
	{
        sdkMutex.unlock();
		return new NodeListPrivate();
	}

	SearchTreeProcessor searchProcessor(searchString);
	processTree(node, &searchProcessor, recursive);
    vector<Node *>& vNodes = searchProcessor.getResults();

	NodeList *nodeList;
    if(vNodes.size()) nodeList = new NodeListPrivate(vNodes.data(), vNodes.size());
    else nodeList = new NodeListPrivate();

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

SearchTreeProcessor::SearchTreeProcessor(const char *search) { this->search = search; }

bool SearchTreeProcessor::processNode(Node* node)
{
	if(!node) return true;
	if(!search) return false;
#ifndef _WIN32
#ifndef __APPLE__
    if(strcasestr(node->displayname(), search)!=NULL) results.push_back(node);
//TODO: Implement this for Windows and MacOS
#endif
#endif
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
    updateStatics();
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

    if (t->type == GET) totalDownloads++;
    else totalUploads++;

    fireOnTransferStart(transfer);
}

void MegaApiImpl::transfer_removed(Transfer *t)
{
    updateStatics();

    if (t->files.size() == 1)
    {
        if (t->type == GET)
        {
            if(pendingDownloads > 0)
                pendingDownloads--;
        }
        else
        {
            if(pendingUploads > 0)
                pendingUploads --;
        }

        if(transferMap.find(t->tag) == transferMap.end()) return;
        MegaTransferPrivate* transfer = transferMap.at(t->tag);
        fireOnTransferFinish(transfer, MegaError(API_EINCOMPLETE));
    }
}

void MegaApiImpl::transfer_prepare(Transfer *t)
{
    updateStatics();
    if(transferMap.find(t->tag) == transferMap.end()) return;
    MegaTransferPrivate* transfer = transferMap.at(t->tag);

	if (t->type == GET)
	{
        transfer->setNodeHandle(t->files.front()->h);
        if((!t->localfilename.size()) || (!t->files.front()->syncxfer))
        {
            if(!t->localfilename.size())
                t->localfilename = t->files.front()->localname;

            int index = t->localfilename.find_last_of(fsAccess->localseparator);
            if(index != string::npos)
                t->localfilename.resize(index+1);

            string suffix;
            fsAccess->tmpnamelocal(&suffix);
            t->localfilename.append(suffix);
        }
	}

    string path;
    fsAccess->local2path(&(t->files.front()->localname), &path);
    transfer->setPath(path.c_str());
    transfer->setTotalBytes(t->size);
}

void MegaApiImpl::transfer_update(Transfer *tr)
{
    updateStatics();
    if(transferMap.find(tr->tag) == transferMap.end()) return;
    MegaTransferPrivate* transfer = transferMap.at(tr->tag);

    if(tr->slot)
    {
#ifdef _WIN32
        if(!tr->files.front()->syncxfer && !tr->slot->progressreported && (tr->type==GET))
        {
            tr->localfilename.append("",1);

			WIN32_FILE_ATTRIBUTE_DATA fad;
			if (GetFileAttributesExW((LPCWSTR)tr->localfilename.data(), GetFileExInfoStandard, &fad))
				SetFileAttributesW((LPCWSTR)tr->localfilename.data(), fad.dwFileAttributes | FILE_ATTRIBUTE_HIDDEN);
			tr->localfilename.resize(tr->localfilename.size() - 1);
        }
#endif

        if((transfer->getUpdateTime() != Waiter::ds) || !tr->slot->progressreported ||
           (tr->slot->progressreported == tr->size))
        {
            transfer->setTime(tr->slot->lastdata);
            if(!transfer->getStartTime()) transfer->setStartTime(Waiter::ds);
            transfer->setDeltaSize(tr->slot->progressreported - transfer->getTransferredBytes());
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
    updateStatics();
    if(transferMap.find(tr->tag) == transferMap.end()) return;
    MegaError megaError(e);
    MegaTransferPrivate* transfer = transferMap.at(tr->tag);

	if(tr->slot) transfer->setTime(tr->slot->lastdata);

    fireOnTransferTemporaryError(transfer, megaError);
}

void MegaApiImpl::transfer_limit(Transfer* t)
{
    updateStatics();
    if(transferMap.find(t->tag) == transferMap.end()) return;
    MegaTransferPrivate* transfer = transferMap.at(t->tag);
    fireOnTransferTemporaryError(transfer, MegaError(API_EOVERQUOTA));
}

void MegaApiImpl::transfer_complete(Transfer* tr)
{
    updateStatics();
    if (tr->type == GET)
    {
        if(pendingDownloads > 0)
            pendingDownloads--;
    }
    else
    {
        if(pendingUploads > 0)
            pendingUploads --;
    }

    if(transferMap.find(tr->tag) == transferMap.end()) return;
    MegaTransferPrivate* transfer = transferMap.at(tr->tag);

    dstime currentTime = Waiter::ds;
    if(!transfer->getStartTime())
        transfer->setStartTime(currentTime);
    if(currentTime<transfer->getStartTime())
        transfer->setStartTime(currentTime);

    long long speed = 0;
    long long deltaTime = currentTime-transfer->getStartTime();
    if(deltaTime<=0)
        deltaTime = 1;
    if(transfer->getTotalBytes()>0)
        speed = (10*transfer->getTotalBytes())/deltaTime;

    transfer->setSpeed(speed);
    transfer->setTime(currentTime);
    transfer->setDeltaSize(tr->size - transfer->getTransferredBytes());
    transfer->setTransferredBytes(tr->size);

	string tmpPath;
	fsAccess->local2path(&tr->localfilename, &tmpPath);

#ifdef _WIN32
    if((!tr->files.front()->syncxfer) && (tr->type==GET))
    {
		WIN32_FILE_ATTRIBUTE_DATA fad;
		if (GetFileAttributesExW((LPCWSTR)tr->localfilename.data(), GetFileExInfoStandard, &fad))
			SetFileAttributesW((LPCWSTR)tr->localfilename.data(), fad.dwFileAttributes & ~FILE_ATTRIBUTE_HIDDEN);
    }
#endif

    fireOnTransferFinish(transfer, MegaError(API_OK));
}

dstime MegaApiImpl::pread_failure(error e, int retry, void* param)
{
	MegaTransferPrivate *transfer = (MegaTransferPrivate *)param;
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
	transfer->setLastBytes((char *)buffer);
	transfer->setDeltaSize(len);
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

void MegaApiImpl::syncupdate_state(Sync *, syncstate_t)
{
    fireOnSyncStateChanged();
}

void MegaApiImpl::syncupdate_scanning(bool scanning)
{
    if(client) client->syncscanstate = scanning;
    fireOnSyncStateChanged();
}

void MegaApiImpl::syncupdate_stuck(string *)
{
    //TODO: Notice the app about this
}

void MegaApiImpl::syncupdate_local_folder_addition(Sync *, const char *)
{

}

void MegaApiImpl::syncupdate_local_folder_deletion(Sync *, const char *)
{

}

void MegaApiImpl::syncupdate_local_file_addition(Sync *, const char *)
{

}

void MegaApiImpl::syncupdate_local_file_deletion(Sync *, const char *)
{

}

void MegaApiImpl::syncupdate_get(Sync *, const char *)
{

}

void MegaApiImpl::syncupdate_put(Sync *, const char *)
{

}

void MegaApiImpl::syncupdate_remote_file_addition(Node *)
{

}

void MegaApiImpl::syncupdate_remote_file_deletion(Node *)
{

}

void MegaApiImpl::syncupdate_remote_folder_addition(Node *)
{

}

void MegaApiImpl::syncupdate_remote_folder_deletion(Node *)
{

}

void MegaApiImpl::syncupdate_remote_copy(Sync *, const char *)
{

}

void MegaApiImpl::syncupdate_remote_move(string *, string *)
{

}

void MegaApiImpl::syncupdate_treestate(LocalNode *l)
{
    string local;
    string path;
    l->getlocalpath(&local, true);
    fsAccess->local2path(&local, &path);
    fireOnFileSyncStateChanged(path.data(), (int)l->ts);
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
    this->waiting = waiting;
    this->fireOnSyncStateChanged();
}


// user addition/update (users never get deleted)
void MegaApiImpl::users_updated(User** u, int count)
{
#if defined(__ANDROID__) || defined(WINDOWS_PHONE)
    fireOnUsersUpdate(NULL);
#else
    UserList* userList = new UserListPrivate(u, count);
    fireOnUsersUpdate(userList);
    delete userList;
#endif
}

void MegaApiImpl::setattr_result(handle h, error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request) return;

	if(request->getType() != MegaRequest::TYPE_RENAME)
	{}

	request->setNodeHandle(h);
    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::rename_result(handle h, error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request) return;

    if(request->getType() != MegaRequest::TYPE_MOVE)
    {}

    request->setNodeHandle(h);
    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::unlink_result(handle h, error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request) return;

    if(request->getType() != MegaRequest::TYPE_REMOVE)
    {}

    request->setNodeHandle(h);
    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::fetchnodes_result(error e)
{
	MegaError megaError(e);

    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request) return;

	if((request->getType() != MegaRequest::TYPE_FETCH_NODES) && (request->getType() != MegaRequest::TYPE_FOLDER_ACCESS))
    {}

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::putnodes_result(error e, targettype_t t, NewNode* nn)
{
	MegaError megaError(e);
	if(requestMap.find(client->restag) == requestMap.end()) return;
	MegaRequestPrivate* request = requestMap.at(client->restag);
	if(!request) return;

	if (t == USER_HANDLE)
	{
        fireOnRequestFinish(request, megaError);
		delete[] nn;	// free array allocated by the app
		return;
	}

	if((request->getType() != MegaRequest::TYPE_IMPORT_LINK) && (request->getType() != MegaRequest::TYPE_MKDIR) &&
            (request->getType() != MegaRequest::TYPE_COPY) &&
			(request->getType() != MegaRequest::TYPE_IMPORT_NODE))
    {}

	handle h = UNDEF;
	Node *n = NULL;
	if(client->nodenotify.size()) n = client->nodenotify.back();
    if(n) n->applykey();
	if(n) h = n->nodehandle;
	request->setNodeHandle(h);
    fireOnRequestFinish(request, megaError);
	delete [] nn;
}

void MegaApiImpl::share_result(error e)
{
	MegaError megaError(e);

    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request) return;

	if(request->getType() == MegaRequest::TYPE_EXPORT)
	{
		return;
		//exportnode_result will be called to end the request.
	}
    if(request->getType() != MegaRequest::TYPE_SHARE)
    {}

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::share_result(int, error)
{
	//The other callback will be called at the end of the request
}

void MegaApiImpl::fa_complete(Node* n, fatype type, const char* data, uint32_t len)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request) return;

    if(request->getType() != MegaRequest::TYPE_GET_ATTR_FILE)
    {}

    FileAccess *f = client->fsaccess->newfileaccess();
    string filePath(request->getFile());
    f->fopen(&filePath, false, true);
	f->fwrite((const byte*)data, len, 0);
	delete f;
    fireOnRequestFinish(request, MegaError(API_OK));
}

int MegaApiImpl::fa_failed(handle, fatype, int retries)
{
    if(requestMap.find(client->restag) == requestMap.end()) return 1;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request) return 1;

    if(request->getType() != MegaRequest::TYPE_GET_ATTR_FILE)
    {}

	if(retries > 3)
	{
        fireOnRequestFinish(request, MegaError(API_EINTERNAL));
		return 1;
	}
    fireOnRequestTemporaryError(request, MegaError(API_EAGAIN));
	return 0;
}

void MegaApiImpl::putfa_result(handle, fatype, error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request) return;
    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::clearing()
{

}

void MegaApiImpl::notify_retry(dstime dsdelta)
{
    bool previousFlag = waitingRequest;
    if(!dsdelta)
        waitingRequest = false;
    else if(dsdelta > 10)
        waitingRequest = true;

    if(previousFlag != waitingRequest)
        fireOnSyncStateChanged();
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
        if(request)
        {
            request->setTransferredBytes(currentProgress);
            request->setTotalBytes(totalProgress);
            fireOnRequestUpdate(request);
        }
    }
}

// login result
void MegaApiImpl::login_result(error result)
{
	MegaError megaError(result);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request) return;

	if((request->getType() != MegaRequest::TYPE_LOGIN) && (request->getType() != MegaRequest::TYPE_FAST_LOGIN))
    {}

    fireOnRequestFinish(request, megaError);
}

// password change result
void MegaApiImpl::changepw_result(error result)
{
	MegaError megaError(result);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request) return;

    if(request->getType() != MegaRequest::TYPE_CHANGE_PW)
    {}

    fireOnRequestFinish(request, megaError);
}

// node export failed
void MegaApiImpl::exportnode_result(error result)
{
	MegaError megaError(result);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request) return;

    if(request->getType() != MegaRequest::TYPE_EXPORT)
    {}

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::exportnode_result(handle h, handle ph)
{
	Node* n;
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request) return;

    if(request->getType() != MegaRequest::TYPE_EXPORT)
    {}

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
    if(!request) return;

	if((request->getType() != MegaRequest::TYPE_IMPORT_LINK) && (request->getType() != MegaRequest::TYPE_GET_PUBLIC_NODE))
    {}

    fireOnRequestFinish(request, megaError);
}

// the requested link was opened successfully
// (it is the application's responsibility to delete n!)
void MegaApiImpl::openfilelink_result(handle ph, const byte* key, m_off_t size, string* a, const char*, m_time_t ts, m_time_t tm, int)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request) return;

	if((request->getType() != MegaRequest::TYPE_IMPORT_LINK) && (request->getType() != MegaRequest::TYPE_GET_PUBLIC_NODE))
    {}

	if (!client->loggedin() && (request->getType() == MegaRequest::TYPE_IMPORT_LINK))
	{
        fireOnRequestFinish(request, MegaError(MegaError::API_EACCESS));
		return;
	}

	if(request->getType() == MegaRequest::TYPE_IMPORT_LINK)
	{
		NewNode* newnode = new NewNode[1];

		// set up new node as folder node
		newnode->source = NEW_PUBLIC;
		newnode->type = FILENODE;
		newnode->nodehandle = ph;
		newnode->clienttimestamp = tm;
		newnode->parenthandle = UNDEF;
		newnode->nodekey.assign((char*)key,FILENODEKEYLENGTH);
		newnode->attrstring = *a;

		// add node
		requestMap.erase(client->restag);
		requestMap[client->nextreqtag()]=request;
		client->putnodes(request->getParentHandle(),newnode,1);
	}
	else
	{
		string attrstring;
		string fileName;
		string keystring;

		attrstring.resize(a->length()*4/3+4);
		attrstring.resize(Base64::btoa((const byte *)a->data(),a->length(), (char *)attrstring.data()));

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
				while ((name = json.getnameid()) != EOO && json.storeobject((t = &attrs.map[name]))) JSON::unescape(t);
				delete[] buf;

				attr_map::iterator it;
				it = attrs.map.find('n');
				if (it == attrs.map.end()) fileName = "CRYPTO_ERROR";
				else if (!it->second.size()) fileName = "BLANK";
				else fileName = it->second.c_str();
			}
			else fileName = "CRYPTO_ERROR";
		}
		else fileName = "NO_KEY";

		request->setPublicNode(new MegaNodePrivate(fileName.c_str(), FILENODE, size, ts, tm, ph, &keystring, a));
        fireOnRequestFinish(request, MegaError(MegaError::API_OK));
	}
}

// reload needed
void MegaApiImpl::reload(const char*)
{
    fireOnReloadNeeded();
}


void MegaApiImpl::debug_log(const char*)
{

}


// nodes have been modified
// (nodes with their removed flag set will be deleted immediately after returning from this call,
// at which point their pointers will become invalid at that point.)
void MegaApiImpl::nodes_updated(Node** n, int count)
{
#if defined(__ANDROID__) || defined(WINDOWS_PHONE)
    fireOnNodesUpdate(NULL);
#else
    NodeList *nodeList = NULL;
    if(n != NULL)
    {
        vector<Node *> list;
        for(int i=0; i<count; i++)
        {
            Node *node = n[i];
            if(node->changed.parent || node->changed.attrs || node->removed)
            {
                node->changed.parent = false;
                node->changed.attrs = false;
                list.push_back(node);
            }
        }

        if(list.size())
        {
            nodeList = new NodeListPrivate(list.data(), list.size());
            fireOnNodesUpdate(nodeList);
        }
    }
    else
    {
        for (node_map::iterator it = client->nodes.begin(); it != client->nodes.end(); it++)
            memset(&(it->second->changed), 0,sizeof it->second->changed);
        fireOnNodesUpdate(nodeList);
    }
#endif
}

void MegaApiImpl::account_details(AccountDetails*, bool, bool, bool, bool, bool, bool)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request) return;

	int numDetails = request->getNumDetails();
	numDetails--;
	request->setNumDetails(numDetails);
	if(!numDetails)
	{
        if(request->getType() != MegaRequest::TYPE_ACCOUNT_DETAILS)
        {}

        fireOnRequestFinish(request, MegaError(MegaError::API_OK));
	}
}

void MegaApiImpl::account_details(AccountDetails*, error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request) return;

    if(request->getType() != MegaRequest::TYPE_ACCOUNT_DETAILS)
    {}

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::invite_result(error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request) return;
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
	if(!request) return;
    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::getua_result(byte* data, unsigned len)
{
	MegaError megaError(API_OK);
	if(requestMap.find(client->restag) == requestMap.end()) return;
	MegaRequestPrivate* request = requestMap.at(client->restag);
	if(!request) return;

	FileAccess *f = client->fsaccess->newfileaccess();
	string filePath(request->getFile());
	f->fopen(&filePath, false, true);
	f->fwrite((const byte*)data, len, 0);
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
    if(!request) return;
    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::ephemeral_result(handle, const byte*)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request) return;

	if((request->getType() != MegaRequest::TYPE_CREATE_ACCOUNT) &&
		(request->getType() != MegaRequest::TYPE_FAST_CREATE_ACCOUNT))
    {

    }

	requestMap.erase(client->restag);
	requestMap[client->nextreqtag()]=request;

	byte pwkey[SymmCipher::KEYLENGTH];
	if(request->getType() == MegaRequest::TYPE_CREATE_ACCOUNT)
		client->pw_key(request->getPassword(),pwkey);
	else
		Base64::atob(request->getPassword(), (byte *)pwkey, sizeof pwkey);

    client->sendsignuplink(request->getEmail(),request->getName(),pwkey);
}

void MegaApiImpl::sendsignuplink_result(error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request) return;

	if((request->getType() != MegaRequest::TYPE_CREATE_ACCOUNT) &&
		(request->getType() != MegaRequest::TYPE_FAST_CREATE_ACCOUNT))
    {

    }

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::querysignuplink_result(error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request) return;
    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::querysignuplink_result(handle, const char* email, const char* name, const byte* pwc, const byte*, const byte* c, size_t len)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request) return;

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
	if(request->getType() == MegaRequest::TYPE_CONFIRM_ACCOUNT)
		client->pw_key(request->getPassword(),pwkey);
	else
		Base64::atob(request->getPassword(), (byte *)pwkey, sizeof pwkey);

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

		requestMap.erase(client->restag);
		requestMap[client->nextreqtag()]=request;
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

void MegaApiImpl::checkfile_result(handle, error)
{

}

void MegaApiImpl::checkfile_result(handle, error, byte*, m_off_t, m_time_t, m_time_t, string*, string*, string*)
{

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

void MegaApiImpl::fireOnRequestStart(MegaRequestPrivate *request)
{
	for(set<MegaRequestListener *>::iterator it = requestListeners.begin(); it != requestListeners.end() ; it++)
		(*it)->onRequestStart(api, request);

	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
		(*it)->onRequestStart(api, request);

	MegaRequestListener* listener = request->getListener();
	if(listener) listener->onRequestStart(api, request);
}


void MegaApiImpl::fireOnRequestFinish(MegaRequestPrivate *request, MegaError e)
{
	MegaError *megaError = new MegaError(e);

	for(set<MegaRequestListener *>::iterator it = requestListeners.begin(); it != requestListeners.end() ; it++)
		(*it)->onRequestFinish(api, request, megaError);

	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
		(*it)->onRequestFinish(api, request, megaError);

	MegaRequestListener* listener = request->getListener();
	if(listener) listener->onRequestFinish(api, request, megaError);

	requestMap.erase(client->restag);
	delete request;
    delete megaError;
}

void MegaApiImpl::fireOnRequestUpdate(MegaRequestPrivate *request)
{
    for(set<MegaRequestListener *>::iterator it = requestListeners.begin(); it != requestListeners.end() ; it++)
        (*it)->onRequestUpdate(api, request);

    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
        (*it)->onRequestUpdate(api, request);

    MegaRequestListener* listener = request->getListener();
    if(listener) listener->onRequestUpdate(api, request);
}

void MegaApiImpl::fireOnRequestTemporaryError(MegaRequestPrivate *request, MegaError e)
{
	MegaError *megaError = new MegaError(e);

	for(set<MegaRequestListener *>::iterator it = requestListeners.begin(); it != requestListeners.end() ; it++)
		(*it)->onRequestTemporaryError(api, request, megaError);

	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
		(*it)->onRequestTemporaryError(api, request, megaError);

	MegaRequestListener* listener = request->getListener();
	if(listener) listener->onRequestTemporaryError(api, request, megaError);
	delete megaError;
}

void MegaApiImpl::fireOnTransferStart(MegaTransferPrivate *transfer)
{
	for(set<MegaTransferListener *>::iterator it = transferListeners.begin(); it != transferListeners.end() ; it++)
		(*it)->onTransferStart(api, transfer);

	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
		(*it)->onTransferStart(api, transfer);

	MegaTransferListener* listener = transfer->getListener();
	if(listener) listener->onTransferStart(api, transfer);
}

void MegaApiImpl::fireOnTransferFinish(MegaTransferPrivate *transfer, MegaError e)
{
	MegaError *megaError = new MegaError(e);

	for(set<MegaTransferListener *>::iterator it = transferListeners.begin(); it != transferListeners.end() ; it++)
		(*it)->onTransferFinish(api, transfer, megaError);

	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
		(*it)->onTransferFinish(api, transfer, megaError);

	MegaTransferListener* listener = transfer->getListener();
	if(listener) listener->onTransferFinish(api, transfer, megaError);

    transferMap.erase(transfer->getTag());
	delete transfer;
	delete megaError;
}

void MegaApiImpl::fireOnTransferTemporaryError(MegaTransferPrivate *transfer, MegaError e)
{
	MegaError *megaError = new MegaError(e);

	for(set<MegaTransferListener *>::iterator it = transferListeners.begin(); it != transferListeners.end() ; it++)
		(*it)->onTransferTemporaryError(api, transfer, megaError);

	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
		(*it)->onTransferTemporaryError(api, transfer, megaError);

	MegaTransferListener* listener = transfer->getListener();
	if(listener) listener->onTransferTemporaryError(api, transfer, megaError);
	delete megaError;
}

void MegaApiImpl::fireOnTransferUpdate(MegaTransferPrivate *transfer)
{
	for(set<MegaTransferListener *>::iterator it = transferListeners.begin(); it != transferListeners.end() ; it++)
		(*it)->onTransferUpdate(api, transfer);

	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
		(*it)->onTransferUpdate(api, transfer);

	MegaTransferListener* listener = transfer->getListener();
	if(listener) listener->onTransferUpdate(api, transfer);
}

bool MegaApiImpl::fireOnTransferData(MegaTransferPrivate *transfer)
{
	MegaTransferListener* listener = transfer->getListener();
	if(listener)
		return listener->onTransferData(api, transfer, transfer->getLastBytes(), transfer->getDeltaSize());
	return false;
}

void MegaApiImpl::fireOnUsersUpdate(UserList *users)
{
	for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ; it++)
    {
#if defined(__ANDROID__) || defined(WINDOWS_PHONE)
        (*it)->onUsersUpdate(api);
#else
        (*it)->onUsersUpdate(api, users);
#endif
    }
	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
    {
#if defined(__ANDROID__) || defined(WINDOWS_PHONE)
        (*it)->onUsersUpdate(api);
#else
        (*it)->onUsersUpdate(api, users);
#endif
    }
}

void MegaApiImpl::fireOnNodesUpdate(NodeList *nodes)
{
	for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ; it++)
    {
#if defined(__ANDROID__) || defined(WINDOWS_PHONE)
        (*it)->onNodesUpdate(api);
#else
        (*it)->onNodesUpdate(api, nodes);
#endif
    }
	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
    {
#if defined(__ANDROID__) || defined(WINDOWS_PHONE)
        (*it)->onNodesUpdate(api);
#else
        (*it)->onNodesUpdate(api, nodes);
#endif
    }
}

void MegaApiImpl::fireOnReloadNeeded()
{
	for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ; it++)
		(*it)->onReloadNeeded(api);

	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
		(*it)->onReloadNeeded(api);
}

void MegaApiImpl::fireOnSyncStateChanged()
{
#if defined(__ANDROID__) || defined(WINDOWS_PHONE)
    return;
#else
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
        (*it)->onSyncStateChanged(api);
#endif
}

void MegaApiImpl::fireOnFileSyncStateChanged(const char *filePath, int newState)
{
#if defined(__ANDROID__) || defined(WINDOWS_PHONE)
    return;
#else
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
        (*it)->onSyncFileStateChanged(api, filePath, newState);
#endif
}

MegaError MegaApiImpl::checkAccess(MegaNode* megaNode, int level)
{
	if(!megaNode || !level)	return MegaError(API_EINTERNAL);

    sdkMutex.lock();
    Node *node = client->nodebyhandle(megaNode->getHandle());
	if(!node)
	{
        sdkMutex.unlock();
        return MegaError(API_EINTERNAL);
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
	if(!megaNode || !targetNode) return MegaError(API_EINTERNAL);

    sdkMutex.lock();
    Node *node = client->nodebyhandle(megaNode->getHandle());
	Node *target = client->nodebyhandle(targetNode->getHandle());
	if(!node || !target)
	{
        sdkMutex.unlock();
        return MegaError(API_EINTERNAL);
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


NodeList *MegaApiImpl::getChildren(MegaNode* p, int order)
{
    if(!p) return new NodeListPrivate();

    sdkMutex.lock();
    Node *parent = client->nodebyhandle(p->getHandle());
	if(!parent)
	{
        sdkMutex.unlock();
        return new NodeListPrivate();
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

    if(childrenNodes.size()) return new NodeListPrivate(childrenNodes.data(), childrenNodes.size());
    else return new NodeListPrivate();
}

MegaNode *MegaApiImpl::getChildNode(MegaNode *parent, const char* name)
{
	if(!parent || !name) return NULL;
    sdkMutex.lock();
    Node *parentNode = client->nodebyhandle(parent->getHandle());
	if(!parentNode)
	{
        sdkMutex.unlock();
        return NULL;
	}

	MegaNode *node = MegaNodePrivate::fromNode(getChildNodeInternal(parentNode, name));
    sdkMutex.unlock();
    return node;
}

Node* MegaApiImpl::getChildNodeInternal(Node *parent, const char* name)
{
	if(!parent || !name) return NULL;
    sdkMutex.lock();
    parent = client->nodebyhandle(parent->nodehandle);
	if(!parent)
	{
        sdkMutex.unlock();
        return NULL;
	}

	Node *result = NULL;
    string nname = name;
    fsAccess->normalize(&nname);
	for (node_list::iterator it = parent->children.begin(); it != parent->children.end(); it++)
	{
        if (!strcmp(nname.c_str(),(*it)->displayname()))
		{
			result = *it;
			break;
		}
	}
    sdkMutex.unlock();
    return result;
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
					if (path > bptr) s.append(bptr,path-bptr);
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

					if (path > bptr) s.append(bptr,path-bptr);

					bptr = path+1;

					c.push_back(s);

					s.erase();
				}
			}
			else if ((*path & 0xf0) == 0xe0) l = 1;
			else if ((*path & 0xf8) == 0xf0) l = 2;
			else if ((*path & 0xfc) == 0xf8) l = 3;
			else if ((*path & 0xfe) == 0xfc) l = 4;
		}
		else l--;
	} while (*path++);

	if (l)
	{
        sdkMutex.unlock();
        return NULL;
	}

	if (remote)
	{
		// target: user inbox - record username/email and return NULL
		if (c.size() == 2 && !c[1].size())
		{
			//if (user) *user = c[0];
            sdkMutex.unlock();
            return NULL;
		}

		User* u;

		if ((u = client->finduser(c[0].c_str())))
		{
			// locate matching share from this user
			handle_set::iterator sit;

			for (sit = u->sharing.begin(); sit != u->sharing.end(); sit++)
			{
				if ((n = client->nodebyhandle(*sit)))
				{
					l = 2;
					break;
				}

				if (l) break;
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
				if (c[2] == "in") n = client->nodebyhandle(client->rootnodes[1]);
				else if (c[2] == "bin") n = client->nodebyhandle(client->rootnodes[2]);
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
		else n = cwd;
	}

	// parse relative path
	while (n && l < (int)c.size())
	{
		if (c[l] != ".")
		{
			if (c[l] == "..")
			{
                if (n->parent) n = n->parent;
			}
			else
			{
				// locate child node (explicit ambiguity resolution: not implemented)
				if (c[l].size())
				{
					nn = getChildNodeInternal(n,c[l].c_str());

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
		e = API_OK;
		nextTag=client->nextreqtag();

		switch(transfer->getType())
		{
			case MegaTransfer::TYPE_UPLOAD:
			{
                const char* localPath = transfer->getPath();
                const char* fileName = transfer->getFileName();

                if(!localPath) { e = API_EARGS; break; }
                currentTransfer = transfer;
				string tmpString = localPath;
				string wLocalPath;
				client->fsaccess->path2local(&tmpString, &wLocalPath);

                string wFileName = fileName;
                MegaFilePut *f = new MegaFilePut(client, &wLocalPath, &wFileName, transfer->getParentHandle(), "");

                client->startxfer(PUT,f);
                if(transfer->getTag() == -1)
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
                if(!node && !publicNode) { e = API_EARGS; break; }

                currentTransfer=transfer;
                if(parentPath)
                {
                    string name;
                    string securename;
					string path = parentPath;
					MegaFileGet *f;

					if(node)
					{
						if(!transfer->getFileName())
                            name = node->displayname();
                        else
                            name = transfer->getFileName();

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
		{
			client->restag = nextTag;
            fireOnTransferFinish(transfer, MegaError(e));
		}
    }
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
	int nextTag;

	while((request = requestQueue.pop()))
	{
		nextTag = client->nextreqtag();
		requestMap[nextTag]=request;
		e = API_OK;

        fireOnRequestStart(request);
		switch(request->getType())
		{
		case MegaRequest::TYPE_LOGIN:
		{
			const char *login = request->getEmail();
			const char *password = request->getPassword();
			if(!login || !password) { e = API_EARGS; break; }

			byte pwkey[SymmCipher::KEYLENGTH];
			if((e = client->pw_key(password,pwkey))) break;
			client->login(login, pwkey);
			break;
		}
		case MegaRequest::TYPE_MKDIR:
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
            newnode->clienttimestamp = time(NULL);

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
			client->makeattr(&key,&newnode->attrstring,attrstring.c_str());

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
			if(!node || (!target && !email)) { e = API_EARGS; break; }

			if (target){
				unsigned nc;
				TreeProcCopy tc;
				// determine number of nodes to be copied
				client->proctree(node,&tc);
				tc.allocnodes();
				nc = tc.nc;
				// build new nodes array
				client->proctree(node,&tc);
				if (!nc) { e = API_EARGS; break; }

				tc.nn->parenthandle = UNDEF;

				client->putnodes(target->nodehandle,tc.nn,nc);
			}
			else{
				TreeProcCopy tc;
				unsigned nc;

				// determine number of nodes to be copied
				client->proctree(node, &tc);

				tc.allocnodes();
				nc = tc.nc;

				// build new nodes array
				client->proctree(node, &tc);
				if (!nc) { e = API_EARGS; break; }

				// tree root: no parent
				tc.nn->parenthandle = UNDEF;

				client->putnodes(email, tc.nn, nc);
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

			if (!client->checkaccess(node,FULL)) { e = API_EACCESS; break; }
			e = client->unlink(node);
			break;
		}
		case MegaRequest::TYPE_SHARE:
		{
			Node *node = client->nodebyhandle(request->getNodeHandle());
			const char* email = request->getEmail();
			int access = request->getAccess();
			if(!node || !email) { e = API_EARGS; break; }

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
		case MegaRequest::TYPE_FOLDER_ACCESS:
		{
			const char* megaFolderLink = request->getLink();
			if(!megaFolderLink) { e = API_EARGS; break; }

			const char* ptr;
			if (!((ptr = strstr(megaFolderLink,"#F!")) && (strlen(ptr)>12) && ptr[11] == '!'))
			{ e = API_EARGS; break; }
			if((e = client->folderaccess(ptr+3,ptr+12))) break;
			client->fetchnodes();
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
		case MegaRequest::TYPE_IMPORT_NODE:
		{
            MegaNode *publicNode = request->getPublicNode();
			Node *parent = client->nodebyhandle(request->getParentHandle());

            if(!publicNode || !parent) { e = API_EARGS; break; }

            NewNode *newnode = new NewNode[1];
            newnode->nodekey.assign(publicNode->getNodeKey()->data(), publicNode->getNodeKey()->size());
            newnode->attrstring.assign(publicNode->getAttrString()->data(), publicNode->getAttrString()->size());
            newnode->nodehandle = publicNode->getHandle();
            newnode->clienttimestamp = publicNode->getModificationTime();
            newnode->source = NEW_PUBLIC;
            newnode->type = FILENODE;
            newnode->parenthandle = UNDEF;

			// add node
			client->putnodes(parent->nodehandle,newnode,1);

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
                client->restag = it->first;
                if(it->second) fireOnRequestFinish(it->second, MegaError(MegaError::API_EACCESS));
            }

            while(!transferMap.empty())
            {
                std::map<int, MegaTransferPrivate *>::iterator it=transferMap.begin();
                if(it->second) fireOnTransferFinish(it->second, MegaError(MegaError::API_EACCESS));
            }

			client->logout();

            requestMap[nextTag]=request;
			client->restag = nextTag;
            pausetime = 0;
            pendingUploads = 0;
            pendingDownloads = 0;
            totalUploads = 0;
            totalDownloads = 0;
            waiting = false;
            waitingRequest = false;
            fireOnRequestFinish(request, MegaError(errorCode));
			break;
		}
		case MegaRequest::TYPE_FAST_LOGIN:
		{
			const char* email = request->getEmail();
			const char* stringHash = request->getPassword();
			const char* base64pwkey = request->getPrivateKey();
            const char* sessionKey = request->getSessionKey();
            if(!sessionKey && (!email || !base64pwkey || !stringHash)) { e = API_EARGS; break; }

            if(!sessionKey)
            {
                byte pwkey[SymmCipher::KEYLENGTH];
                Base64::atob(base64pwkey, (byte *)pwkey, sizeof pwkey);
                client->login(email, pwkey);
            }
            else
            {
                byte session[sizeof client->key.key + MegaClient::SIDLEN];
                Base64::atob(sessionKey, (byte *)session, sizeof session);
                client->login(session, sizeof session);
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
			const char* dstFilePath = request->getFile();
            int type = request->getParamType();
            User *user = client->finduser(request->getEmail(), 0);

			if(!dstFilePath || !user || (type != 0)) { e = API_EARGS; break; }

			e = API_EACCESS; //TODO: Use putua
			break;
		}
		case MegaRequest::TYPE_SET_ATTR_FILE:
		{
            /*const char* srcFilePath = request->getFile();
			int type = request->getAttrType();
			Node *node = client->nodebyhandle(request->getNodeHandle());

			if(!srcFilePath || !node) { e = API_EARGS; break; }

			string thumbnail;
			FileAccess *f = this->newfile();
			f->fopen(srcFilePath, 1, 0);
			f->fread(&thumbnail, f->size, 0, 0);
			delete f;

            client->putfa(&(node->key),node->nodehandle,type,(const byte*)thumbnail.data(),thumbnail.size());*/
			break;
		}
		case MegaRequest::TYPE_RETRY_PENDING_CONNECTIONS:
		{
			client->abortbackoff();
			client->disconnect();
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
		case MegaRequest::TYPE_FAST_CREATE_ACCOUNT:
		{
			const char *email = request->getEmail();
			const char *password = request->getPassword();
			const char *name = request->getName();

			if(!email || !password || !name) { e = API_EARGS; break; }

			client->createephemeral();
			break;
		}
		case MegaRequest::TYPE_QUERY_SIGNUP_LINK:
		case MegaRequest::TYPE_CONFIRM_ACCOUNT:
		case MegaRequest::TYPE_FAST_CONFIRM_ACCOUNT:
		{
			const char *link = request->getLink();
			const char *password = request->getPassword();
			if(((request->getType()!=MegaRequest::TYPE_QUERY_SIGNUP_LINK) && !password) || (!link))
				{ e = API_EARGS; break; }

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
            e = client->addsync(&localname, DEBRISFOLDER, NULL, node, -1);
            if(!e)
            {
                client->restag = nextTag;
                fireOnRequestFinish(request, MegaError(API_OK));
            }
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
            client->restag = nextTag;
            fireOnRequestFinish(request, MegaError(API_OK));
            break;
        }
        case MegaRequest::TYPE_CANCEL_TRANSFER:
        {
            int transferTag = request->getTransfer();
            if(transferMap.find(transferTag) == transferMap.end()) { e = API_ENOENT; break; };

            MegaTransferPrivate* megaTransfer = transferMap.at(transferTag);
            megaTransfer->setSyncTransfer(true);
            Transfer *transfer = megaTransfer->getTransfer();

            file_list files = transfer->files;
            file_list::iterator iterator = files.begin();
            while (iterator != files.end())
            {
                File *file = *iterator;
                iterator++;
                if(!file->syncxfer) client->stopxfer(file);
            }
            client->restag = nextTag;
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
            client->restag = nextTag;
            fireOnRequestFinish(request, MegaError(API_OK));
            break;
        }
        case MegaRequest::TYPE_REMOVE_SYNCS:
        {
            sync_list::iterator it = client->syncs.begin();
            while(it != client->syncs.end())
            {
                Sync *sync = (*it);
                it++;
                client->delsync(sync);
            }
            client->restag = nextTag;
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
                if(!sync->localroot.node || sync->localroot.node->nodehandle == nodehandle)
                {
                    string path;
                    fsAccess->local2path(&sync->localroot.localname, &path);
                    request->setFile(path.c_str());
                    client->delsync(sync);
                    client->restag = nextTag;
                    fireOnRequestFinish(request, MegaError(API_OK));
                    found = true;
                    break;
                }
                it++;
            }

            if(!found) e = API_ENOENT;
            break;
        }
        case MegaRequest::TYPE_DELETE:
            threadExit = 1;
            break;
		}

		if(e)
		{
			client->restag = nextTag;
            fireOnRequestFinish(request, MegaError(e));
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

void MegaApiImpl::updateStatics()
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

void MegaApiImpl::update()
{
    waiter->notify();
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

bool MegaApiImpl::isWaiting()
{
    return waiting || waitingRequest;
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
    this->excludedNames = *excludedNames;
    sdkMutex.unlock();
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
        t->clienttimestamp = n->clienttimestamp;

		// copy key (if file) or generate new key (if folder)
		if (n->type == FILENODE) t->nodekey = n->nodekey;
		else
		{
			byte buf[FOLDERNODEKEYLENGTH];
			PrnGen::genblock(buf,sizeof buf);
			t->nodekey.assign((char*)buf,FOLDERNODEKEYLENGTH);
		}

        //TODO: Check if nodekey is empty (this code isn't used in the current release)
		key.setkey((const byte*)t->nodekey.data(),n->type);

		n->attrs.getjson(&attrstring);
		client->makeattr(&key,&t->attrstring,attrstring.c_str());
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

bool MegaHashSignatureImpl::check(const char *base64Signature)
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

