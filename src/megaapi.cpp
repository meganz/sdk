/**
 * @file megaapi.cpp
 * @brief Intermediate layer for the MEGA C++ SDK.
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

#include "mega.h"
#include "megaapi.h"
#include "megaapi_impl.h"

using namespace mega;

MegaProxy::MegaProxy()
{
    proxyType = PROXY_AUTO;
    username = NULL;
    password = NULL;
    proxyURL = NULL;
}

MegaProxy::~MegaProxy()
{
	delete username;
	delete password;
	delete proxyURL;
}

void MegaProxy::setProxyType(int proxyType)
{
    this->proxyType = proxyType;
}

void MegaProxy::setProxyURL(const char *proxyURL)
{
    if(this->proxyURL)
        delete this->proxyURL;

    this->proxyURL = MegaApi::strdup(proxyURL);
}

void MegaProxy::setCredentials(const char *username, const char *password)
{
    if(this->username)
        delete this->username;

    if(this->password)
        delete this->password;

    this->username = MegaApi::strdup(username);
    this->password = MegaApi::strdup(password);
}

int MegaProxy::getProxyType()
{
    return proxyType;
}

const char * MegaProxy::getProxyURL()
{
    return this->proxyURL;
}

bool MegaProxy::credentialsNeeded()
{
    return (username != NULL);
}

const char *MegaProxy::getUsername()
{
    return username;
}

const char *MegaProxy::getPassword()
{
    return password;
}

MegaStringList::~MegaStringList()
{

}

MegaStringList *MegaStringList::copy()
{
    return NULL;
}

const char *MegaStringList::get(int)
{
    return NULL;
}

int MegaStringList::size()
{
    return 0;
}

MegaNodeList *MegaNodeList::createInstance()
{
    return new MegaNodeListPrivate();
}

MegaNodeList::~MegaNodeList() { }

MegaNodeList *MegaNodeList::copy()
{
    return NULL;
}

MegaNode *MegaNodeList::get(int)
{
    return NULL;
}

int MegaNodeList::size()
{
    return 0;
}

void MegaNodeList::addNode(MegaNode *node)
{

}

MegaTransferList::~MegaTransferList() { }

MegaTransfer *MegaTransferList::get(int)
{
    return NULL;
}

int MegaTransferList::size()
{
    return 0;
}

MegaContactRequestList::~MegaContactRequestList() { }

MegaContactRequestList *MegaContactRequestList::copy()
{
    return NULL;
}

MegaContactRequest *MegaContactRequestList::get(int)
{
    return NULL;
}

int MegaContactRequestList::size()
{
    return 0;
}

MegaUserList::~MegaUserList() { }

MegaUserList *MegaUserList::copy()
{
    return NULL;
}

MegaUser *MegaUserList::get(int)
{
    return NULL;
}

int MegaUserList::size()
{
    return 0;
}

MegaShareList::~MegaShareList() { }

MegaShare *MegaShareList::get(int)
{
    return NULL;
}

int MegaShareList::size()
{
    return 0;
}

const double MegaNode::INVALID_COORDINATE  = -200;

MegaNode::~MegaNode() { }

MegaNode *MegaNode::copy()
{
    return NULL;
}

int MegaNode::getType()
{
    return 0;
}

const char *MegaNode::getName()
{
    return NULL;
}

const char *MegaNode::getFingerprint()
{
    return NULL;
}

bool MegaNode::hasCustomAttrs()
{
    return false;
}

MegaStringList *MegaNode::getCustomAttrNames()
{
    return NULL;
}

const char *MegaNode::getCustomAttr(const char* /*attrName*/)
{
    return NULL;
}

int MegaNode::getDuration()
{
    return -1;
}

double MegaNode::getLatitude()
{
    return INVALID_COORDINATE;
}

double MegaNode::getLongitude()
{
    return INVALID_COORDINATE;
}

char *MegaNode::getBase64Handle()
{
    return NULL;
}

int64_t MegaNode::getSize()
{
    return 0;
}

int64_t MegaNode::getCreationTime()
{
    return 0;
}

int64_t MegaNode::getModificationTime()
{
    return 0;
}

MegaHandle MegaNode::getHandle()
{
    return INVALID_HANDLE;
}

MegaHandle MegaNode::getParentHandle()
{
    return INVALID_HANDLE;
}

char *MegaNode::getBase64Key()
{
    return NULL;
}

int MegaNode::getTag()
{
    return 0;
}

int64_t MegaNode::getExpirationTime()
{
    return -1;
}

MegaHandle MegaNode::getPublicHandle()
{
    return INVALID_HANDLE;
}

MegaNode* MegaNode::getPublicNode()
{
    return NULL;
}

char * MegaNode::getPublicLink(bool includeKey)
{
    return NULL;
}

bool MegaNode::isFile()
{
    return false;
}

bool MegaNode::isFolder()
{
    return false;
}

bool MegaNode::isRemoved()
{
    return false;
}

bool MegaNode::hasChanged(int /*changeType*/)
{
    return false;
}

int MegaNode::getChanges()
{
    return 0;
}

bool MegaNode::hasThumbnail()
{
    return false;
}

bool MegaNode::hasPreview()
{
    return false;
}

bool MegaNode::isPublic()
{
    return false;
}

bool MegaNode::isShared()
{
    return false;
}

bool MegaNode::isOutShare()
{
    return false;
}

bool MegaNode::isInShare()
{
    return false;
}

bool MegaNode::isExported()
{
    return false;
}

bool MegaNode::isExpired()
{
  return false;
}

bool MegaNode::isTakenDown()
{
    return false;
}

bool MegaNode::isForeign()
{
    return false;
}

string *MegaNode::getNodeKey()
{
    return NULL;
}

string *MegaNode::getAttrString()
{
    return NULL;
}

char *MegaNode::getFileAttrString()
{
    return NULL;
}

string *MegaNode::getPrivateAuth()
{
    return NULL;
}

void MegaNode::setPrivateAuth(const char *)
{
    return;
}

string *MegaNode::getPublicAuth()
{
    return NULL;
}

MegaNodeList *MegaNode::getChildren()
{
    return NULL;
}

char *MegaNode::serialize()
{
    return NULL;
}

MegaNode *MegaNode::unserialize(const char *d)
{
    if (!d)
    {
        return NULL;
    }

    string data;
    data.resize(strlen(d) * 3 / 4 + 3);
    data.resize(Base64::atob(d, (byte*)data.data(), data.size()));

    return MegaNodePrivate::unserialize(&data);
}

#ifdef ENABLE_SYNC
bool MegaNode::isSyncDeleted()
{
    return false;
}

string MegaNode::getLocalPath()
{
    return string();
}
#endif

MegaUser::~MegaUser() { }

MegaUser *MegaUser::copy()
{
    return NULL;
}

const char *MegaUser::getEmail()
{
    return NULL;
}

MegaHandle MegaUser::getHandle()
{
    return INVALID_HANDLE;
}

int MegaUser::getVisibility()
{
    return 0;
}

int64_t MegaUser::getTimestamp()
{
    return 0;
}

bool MegaUser::hasChanged(int)
{
    return false;
}

int MegaUser::getChanges()
{
    return 0;
}

int MegaUser::isOwnChange()
{
    return 0;
}

MegaShare::~MegaShare() { }

MegaShare *MegaShare::copy()
{
    return NULL;
}

const char *MegaShare::getUser()
{
    return NULL;
}

MegaHandle MegaShare::getNodeHandle()
{
    return INVALID_HANDLE;
}

int MegaShare::getAccess()
{
    return 0;
}

int64_t MegaShare::getTimestamp()
{
    return 0;
}

MegaRequest::~MegaRequest() { }
MegaRequest *MegaRequest::copy()
{
	return NULL;
}

int MegaRequest::getType() const
{
	return 0;
}

const char *MegaRequest::getRequestString() const
{
	return NULL;
}

const char *MegaRequest::toString() const
{
	return NULL;
}

const char *MegaRequest::__str__() const
{
	return NULL;
}

const char *MegaRequest::__toString() const
{
	return NULL;
}

MegaHandle MegaRequest::getNodeHandle() const
{
	return INVALID_HANDLE;
}

const char *MegaRequest::getLink() const
{
	return NULL;
}

MegaHandle MegaRequest::getParentHandle() const
{
	return INVALID_HANDLE;
}

const char *MegaRequest::getSessionKey() const
{
	return NULL;
}

const char *MegaRequest::getName() const
{
	return NULL;
}

const char *MegaRequest::getEmail() const
{
	return NULL;
}

const char *MegaRequest::getPassword() const
{
	return NULL;
}

const char *MegaRequest::getNewPassword() const
{
	return NULL;
}

const char *MegaRequest::getPrivateKey() const
{
	return NULL;
}

int MegaRequest::getAccess() const
{
	return 0;
}

const char *MegaRequest::getFile() const
{
	return NULL;
}

int MegaRequest::getNumRetry() const
{
	return 0;
}

MegaNode *MegaRequest::getPublicNode() const
{
	return NULL;
}

MegaNode *MegaRequest::getPublicMegaNode() const
{
	return NULL;
}

int MegaRequest::getParamType() const
{
	return 0;
}

const char *MegaRequest::getText() const
{
	return NULL;
}

long long MegaRequest::getNumber() const
{
	return 0;
}

bool MegaRequest::getFlag() const
{
	return false;
}

long long MegaRequest::getTransferredBytes() const
{
	return 0;
}

long long MegaRequest::getTotalBytes() const
{
	return 0;
}

MegaRequestListener *MegaRequest::getListener() const
{
	return NULL;
}

MegaAccountDetails *MegaRequest::getMegaAccountDetails() const
{
	return NULL;
}

MegaPricing *MegaRequest::getPricing() const
{
    return NULL;
}

MegaAchievementsDetails *MegaRequest::getMegaAchievementsDetails() const
{
    return NULL;
}

int MegaRequest::getTransferTag() const
{
	return 0;
}

int MegaRequest::getNumDetails() const
{
    return 0;
}

int MegaRequest::getTag() const
{
    return 0;
}

#ifdef ENABLE_CHAT
MegaTextChatPeerList *MegaRequest::getMegaTextChatPeerList() const
{
    return NULL;
}

MegaTextChatList *MegaRequest::getMegaTextChatList() const
{
    return NULL;
}
#endif

MegaStringMap *MegaRequest::getMegaStringMap() const
{
    return NULL;
}

MegaTransfer::~MegaTransfer() { }

MegaTransfer *MegaTransfer::copy()
{
	return NULL;
}

int MegaTransfer::getType() const
{
	return 0;
}

const char *MegaTransfer::getTransferString() const
{
	return NULL;
}

const char *MegaTransfer::toString() const
{
	return NULL;
}

const char *MegaTransfer::__str__() const
{
	return NULL;
}

const char *MegaTransfer::__toString() const
{
	return NULL;
}

int64_t MegaTransfer::getStartTime() const
{
	return 0;
}

long long MegaTransfer::getTransferredBytes() const
{
	return 0;
}

long long MegaTransfer::getTotalBytes() const
{
	return 0;
}

const char *MegaTransfer::getPath() const
{
	return NULL;
}

const char *MegaTransfer::getParentPath() const
{
	return NULL;
}

MegaHandle MegaTransfer::getNodeHandle() const
{
	return INVALID_HANDLE;
}

MegaHandle MegaTransfer::getParentHandle() const
{
	return INVALID_HANDLE;
}

long long MegaTransfer::getStartPos() const
{
	return 0;
}

long long MegaTransfer::getEndPos() const
{
	return 0;
}

const char *MegaTransfer::getFileName() const
{
	return NULL;
}

MegaTransferListener *MegaTransfer::getListener() const
{
	return NULL;
}

int MegaTransfer::getNumRetry() const
{
	return 0;
}

int MegaTransfer::getMaxRetries() const
{
	return 0;
}

int MegaTransfer::getTag() const
{
	return 0;
}

long long MegaTransfer::getSpeed() const
{
    return 0;
}

long long MegaTransfer::getMeanSpeed() const
{
    return 0;
}

long long MegaTransfer::getDeltaSize() const
{
	return 0;
}

int64_t MegaTransfer::getUpdateTime() const
{
	return 0;
}

MegaNode *MegaTransfer::getPublicMegaNode() const
{
	return NULL;
}

bool MegaTransfer::isSyncTransfer() const
{
	return false;
}

bool MegaTransfer::isStreamingTransfer() const
{
	return false;
}

char *MegaTransfer::getLastBytes() const
{
    return NULL;
}

bool MegaTransfer::isFolderTransfer() const
{
    return false;
}

int MegaTransfer::getFolderTransferTag() const
{
    return 0;
}

const char *MegaTransfer::getAppData() const
{
    return NULL;
}

int MegaTransfer::getState() const
{
    return STATE_NONE;
}

unsigned long long MegaTransfer::getPriority() const
{
    return 0;
}

long long MegaTransfer::getNotificationNumber() const
{
    return 0;
}

MegaError::MegaError(int errorCode)
{
    this->errorCode = errorCode;
    this->value = 0;
}

MegaError::MegaError(int errorCode, long long value)
{
    this->errorCode = errorCode;
    this->value = value;
}

MegaError::MegaError(const MegaError &megaError)
{
	errorCode = megaError.getErrorCode();
    value = megaError.getValue();
}

MegaError::~MegaError()
{

}

MegaError* MegaError::copy()
{
	return new MegaError(*this);
}

int MegaError::getErrorCode() const 
{ 
    return errorCode;
}

long long MegaError::getValue() const
{
    return value;
}

const char* MegaError::getErrorString() const
{
    return MegaError::getErrorString(errorCode);
}

const char* MegaError::getErrorString(int errorCode)
{
    if(errorCode <= 0)
    {
        switch(errorCode)
        {
        case API_OK:
            return "No error";
        case API_EINTERNAL:
            return "Internal error";
        case API_EARGS:
            return "Invalid argument";
        case API_EAGAIN:
            return "Request failed, retrying";
        case API_ERATELIMIT:
            return "Rate limit exceeded";
        case API_EFAILED:
            return "Failed permanently";
        case API_ETOOMANY:
            return "Too many concurrent connections or transfers";
        case API_ERANGE:
            return "Out of range";
        case API_EEXPIRED:
            return "Expired";
        case API_ENOENT:
            return "Not found";
        case API_ECIRCULAR:
            return "Circular linkage detected";
        case API_EACCESS:
            return "Access denied";
        case API_EEXIST:
            return "Already exists";
        case API_EINCOMPLETE:
            return "Incomplete";
        case API_EKEY:
            return "Invalid key/Decryption error";
        case API_ESID:
            return "Bad session ID";
        case API_EBLOCKED:
            return "Blocked";
        case API_EOVERQUOTA:
            return "Over quota";
        case API_ETEMPUNAVAIL:
            return "Temporarily not available";
        case API_ETOOMANYCONNECTIONS:
            return "Connection overflow";
        case API_EWRITE:
            return "Write error";
        case API_EREAD:
            return "Read error";
        case API_EAPPKEY:
            return "Invalid application key";
        case API_ESSL:
            return "SSL verification failed";
        case API_EGOINGOVERQUOTA:
            return "Not enough quota";
        case PAYMENT_ECARD:
            return "Credit card rejected";
        case PAYMENT_EBILLING:
            return "Billing failed";
        case PAYMENT_EFRAUD:
            return "Rejected by fraud protection";
        case PAYMENT_ETOOMANY:
            return "Too many requests";
        case PAYMENT_EBALANCE:
            return "Balance error";
        case PAYMENT_EGENERIC:
        default:
            return "Unknown error";
        }
    }
    return "HTTP Error";
}

const char* MegaError::toString() const 
{ 
	return getErrorString(); 
}

const char* MegaError::__str__() const 
{ 
	return getErrorString();
}

const char *MegaError::__toString() const
{
	return getErrorString();
}

MegaContactRequest::~MegaContactRequest()
{

}

MegaContactRequest *MegaContactRequest::copy() const
{
    return NULL;
}

MegaHandle MegaContactRequest::getHandle() const
{
    return INVALID_HANDLE;
}

char *MegaContactRequest::getSourceEmail() const
{
    return NULL;
}

char *MegaContactRequest::getSourceMessage() const
{
    return NULL;
}

char *MegaContactRequest::getTargetEmail() const
{
    return NULL;
}

int64_t MegaContactRequest::getCreationTime() const
{
    return 0;
}

int64_t MegaContactRequest::getModificationTime() const
{
    return 0;
}

int MegaContactRequest::getStatus() const
{
    return 0;
}

bool MegaContactRequest::isOutgoing() const
{
    return true;
}

//Request callbacks
void MegaRequestListener::onRequestStart(MegaApi *, MegaRequest *)
{ }
void MegaRequestListener::onRequestFinish(MegaApi *, MegaRequest *, MegaError *)
{ }
void MegaRequestListener::onRequestUpdate(MegaApi *, MegaRequest *)
{ }
void MegaRequestListener::onRequestTemporaryError(MegaApi *, MegaRequest *, MegaError *)
{ }
MegaRequestListener::~MegaRequestListener() {}


SynchronousRequestListener::SynchronousRequestListener()
{
    listener = NULL;
    megaApi = NULL;
    megaRequest = NULL;
    megaError = NULL;
    semaphore = new MegaSemaphore();
}
SynchronousRequestListener::~SynchronousRequestListener()
{
    delete semaphore;
    if (megaRequest)
    {
        delete megaRequest;
    }
    if (megaError)
    {
        delete megaError;
    }
}

void SynchronousRequestListener::onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *error)
{
    this->megaApi = api;
    if (megaRequest)
    {
        delete megaRequest;              //in case of reused listener
    }
    this->megaRequest = request->copy();
    if (megaError)
    {
        delete megaError;            //in case of reused listener
    }
    this->megaError = error->copy();

    doOnRequestFinish(api, request, error);
    semaphore->release();
}

void SynchronousRequestListener::doOnRequestFinish(MegaApi *api, MegaRequest *request, MegaError *error)
{ }

void SynchronousRequestListener::wait()
{
    semaphore->wait();
}

int SynchronousRequestListener::trywait(int milliseconds)
{
    return semaphore->timedwait(milliseconds);
}

MegaRequest *SynchronousRequestListener::getRequest() const
{
    return megaRequest;
}

MegaApi *SynchronousRequestListener::getApi() const
{
    return megaApi;
}

MegaError *SynchronousRequestListener::getError() const
{
    return megaError;
}


//Transfer callbacks
void MegaTransferListener::onTransferStart(MegaApi *, MegaTransfer *)
{ }
void MegaTransferListener::onTransferFinish(MegaApi*, MegaTransfer *, MegaError*)
{ }
void MegaTransferListener::onTransferUpdate(MegaApi *, MegaTransfer *)
{ }
bool MegaTransferListener::onTransferData(MegaApi *, MegaTransfer *, char *, size_t)
{ return true; }
void MegaTransferListener::onTransferTemporaryError(MegaApi *, MegaTransfer *, MegaError*)
{ }
MegaTransferListener::~MegaTransferListener()
{ }



SynchronousTransferListener::SynchronousTransferListener()
{
    listener = NULL;
    megaApi = NULL;
    megaTransfer = NULL;
    megaError = NULL;
    semaphore = new MegaSemaphore();
}
SynchronousTransferListener::~SynchronousTransferListener()
{
    delete semaphore;
    delete megaTransfer;
    delete megaError;
}

void SynchronousTransferListener::onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *error)
{
    this->megaApi = api;
    delete megaTransfer;               //in case of reused listener
    this->megaTransfer = transfer->copy();
    delete megaError;            //in case of reused listener
    this->megaError = error->copy();

    doOnTransferFinish(api, transfer, error);
    semaphore->release();
}

void SynchronousTransferListener::doOnTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *error)
{ }

void SynchronousTransferListener::wait()
{
    semaphore->wait();
}

int SynchronousTransferListener::trywait(int milliseconds)
{
    return semaphore->timedwait(milliseconds);
}

MegaTransfer *SynchronousTransferListener::getTransfer() const
{
    return megaTransfer;
}

MegaApi *SynchronousTransferListener::getApi() const
{
    return megaApi;
}

MegaError *SynchronousTransferListener::getError() const
{
    return megaError;
}


//Global callbacks
void MegaGlobalListener::onUsersUpdate(MegaApi *, MegaUserList *)
{ }
void MegaGlobalListener::onNodesUpdate(MegaApi *, MegaNodeList *)
{ }
void MegaGlobalListener::onAccountUpdate(MegaApi *)
{ }
void MegaGlobalListener::onContactRequestsUpdate(MegaApi *, MegaContactRequestList *)
{ }
void MegaGlobalListener::onReloadNeeded(MegaApi *)
{ }
void MegaGlobalListener::onEvent(MegaApi *api, MegaEvent *event)
{ }
MegaGlobalListener::~MegaGlobalListener()
{ }

//All callbacks
void MegaListener::onRequestStart(MegaApi *, MegaRequest *)
{ }
void MegaListener::onRequestFinish(MegaApi *, MegaRequest *, MegaError *)
{ }
void MegaListener::onRequestUpdate(MegaApi * , MegaRequest *)
{ }
void MegaListener::onRequestTemporaryError(MegaApi *, MegaRequest *, MegaError *)
{ }
void MegaListener::onTransferStart(MegaApi *, MegaTransfer *)
{ }
void MegaListener::onTransferFinish(MegaApi *, MegaTransfer *, MegaError *)
{ }
void MegaListener::onTransferUpdate(MegaApi *, MegaTransfer *)
{ }
void MegaListener::onTransferTemporaryError(MegaApi *, MegaTransfer *, MegaError *)
{ }
void MegaListener::onUsersUpdate(MegaApi *, MegaUserList *)
{ }
void MegaListener::onNodesUpdate(MegaApi *, MegaNodeList *)
{ }
void MegaListener::onAccountUpdate(MegaApi *)
{ }
void MegaListener::onContactRequestsUpdate(MegaApi *, MegaContactRequestList *)
{ }
void MegaListener::onReloadNeeded(MegaApi *)
{ }
void MegaListener::onEvent(MegaApi *api, MegaEvent *event)
{ }

#ifdef ENABLE_SYNC
void MegaGlobalListener::onGlobalSyncStateChanged(MegaApi *)
{ }
void MegaListener::onSyncFileStateChanged(MegaApi *, MegaSync *, string *, int)
{ }
void MegaListener::onSyncEvent(MegaApi *, MegaSync *, MegaSyncEvent *)
{ }
void MegaListener::onSyncStateChanged(MegaApi *, MegaSync *)
{ }
void MegaListener::onGlobalSyncStateChanged(MegaApi *)
{ }
#endif

#ifdef ENABLE_CHAT
void MegaGlobalListener::onChatsUpdate(MegaApi *api, MegaTextChatList *chats)
{}
void MegaListener::onChatsUpdate(MegaApi *api, MegaTextChatList *chats)
{}
#endif

MegaListener::~MegaListener() {}

bool MegaTreeProcessor::processMegaNode(MegaNode*)
{ return false; /* Stops the processing */ }
MegaTreeProcessor::~MegaTreeProcessor()
{ }

MegaApi::MegaApi(const char *appKey, MegaGfxProcessor* processor, const char *basePath, const char *userAgent)
{
    pImpl = new MegaApiImpl(this, appKey, processor, basePath, userAgent);
}

MegaApi::MegaApi(const char *appKey, const char *basePath, const char *userAgent)
{
    pImpl = new MegaApiImpl(this, appKey, basePath, userAgent);
}

#ifdef ENABLE_SYNC
MegaApi::MegaApi(const char *appKey, const char *basePath, const char *userAgent, int fseventsfd)
{
    pImpl = new MegaApiImpl(this, appKey, basePath, userAgent, fseventsfd);
}
#endif

MegaApi::~MegaApi()
{
    delete pImpl;
}

int MegaApi::isLoggedIn()
{
    return pImpl->isLoggedIn();
}

char *MegaApi::getMyEmail()
{
    return pImpl->getMyEmail();
}

char *MegaApi::getMyUserHandle()
{
    return pImpl->getMyUserHandle();
}

MegaHandle MegaApi::getMyUserHandleBinary()
{
    return pImpl->getMyUserHandleBinary();
}
MegaUser *MegaApi::getMyUser()
{
    return pImpl->getMyUser();
}

char *MegaApi::getMyXMPPJid()
{
    return pImpl->getMyXMPPJid();
}

bool MegaApi::isAchievementsEnabled()
{
    return pImpl->isAchievementsEnabled();
}

#ifdef ENABLE_CHAT
char *MegaApi::getMyFingerprint()
{
    return pImpl->getMyFingerprint();
}
#endif

void MegaApi::setLogLevel(int logLevel)
{
    MegaApiImpl::setLogLevel(logLevel);
}

void MegaApi::setLogToConsole(bool enable)
{
    MegaApiImpl::setLogToConsole(enable);
}

void MegaApi::addLoggerObject(MegaLogger *megaLogger)
{
    MegaApiImpl::addLoggerClass(megaLogger);
}

void MegaApi::removeLoggerObject(MegaLogger *megaLogger)
{
    MegaApiImpl::removeLoggerClass(megaLogger);
}

void MegaApi::log(int logLevel, const char *message, const char *filename, int line)
{
    MegaApiImpl::log(logLevel, message, filename, line);
}

char *MegaApi::getBase64PwKey(const char *password)
{
    return pImpl->getBase64PwKey(password);
}

char *MegaApi::getStringHash(const char* base64pwkey, const char* inBuf)
{
    return pImpl->getStringHash(base64pwkey, inBuf);
}

long long MegaApi::getSDKtime()
{
    return pImpl->getSDKtime();
}

void MegaApi::getSessionTransferURL(const char *path, MegaRequestListener *listener)
{
    pImpl->getSessionTransferURL(path, listener);
}

MegaHandle MegaApi::base32ToHandle(const char *base32Handle)
{
    return MegaApiImpl::base32ToHandle(base32Handle);
}

uint64_t MegaApi::base64ToHandle(const char* base64Handle)
{
    return MegaApiImpl::base64ToHandle(base64Handle);
}

uint64_t MegaApi::base64ToUserHandle(const char* base64Handle)
{
    return MegaApiImpl::base64ToUserHandle(base64Handle);
}

char *MegaApi::handleToBase64(MegaHandle handle)
{
    return MegaApiImpl::handleToBase64(handle);
}

char *MegaApi::userHandleToBase64(MegaHandle handle)
{
    return MegaApiImpl::userHandleToBase64(handle);
}

void MegaApi::retryPendingConnections(bool disconnect, bool includexfers, MegaRequestListener* listener)
{
    pImpl->retryPendingConnections(disconnect, includexfers, listener);
}

void MegaApi::addEntropy(char *data, unsigned int size)
{
    MegaApiImpl::addEntropy(data, size);
}

#ifdef WINDOWS_PHONE
void MegaApi::setStatsID(const char *id)
{
    MegaApiImpl::setStatsID(id);
}
#endif

void MegaApi::fastLogin(const char* email, const char *stringHash, const char *base64pwkey, MegaRequestListener *listener)
{
    pImpl->fastLogin(email, stringHash, base64pwkey,listener);
}

void MegaApi::fastLogin(const char *session, MegaRequestListener *listener)
{
    pImpl->fastLogin(session, listener);
}

void MegaApi::killSession(MegaHandle sessionHandle, MegaRequestListener *listener)
{
    pImpl->killSession(sessionHandle, listener);
}

void MegaApi::getUserData(MegaRequestListener *listener)
{
    pImpl->getUserData(listener);
}

void MegaApi::getUserData(MegaUser *user, MegaRequestListener *listener)
{
    pImpl->getUserData(user, listener);
}

void MegaApi::getUserData(const char *user, MegaRequestListener *listener)
{
    pImpl->getUserData(user, listener);
}

void MegaApi::login(const char *login, const char *password, MegaRequestListener *listener)
{
    pImpl->login(login, password, listener);
}

char *MegaApi::dumpSession()
{
    return pImpl->dumpSession();
}

char *MegaApi::getSequenceNumber()
{
    return pImpl->getSequenceNumber();
}

char *MegaApi::dumpXMPPSession()
{
    return pImpl->dumpXMPPSession();
}

char *MegaApi::getAccountAuth()
{
    return pImpl->getAccountAuth();
}

void MegaApi::setAccountAuth(const char *auth)
{
    pImpl->setAccountAuth(auth);
}

void MegaApi::createAccount(const char* email, const char* password, const char* name, MegaRequestListener *listener)
{
    pImpl->createAccount(email, password, name, listener);
}

void MegaApi::createAccount(const char* email, const char* password, const char* firstname, const char*  lastname, MegaRequestListener *listener)
{
    pImpl->createAccount(email, password, firstname, lastname, listener);
}

void MegaApi::resumeCreateAccount(const char* sid, MegaRequestListener *listener)
{
    pImpl->resumeCreateAccount(sid, listener);
}

void MegaApi::fastCreateAccount(const char* email, const char *base64pwkey, const char* name, MegaRequestListener *listener)
{
    pImpl->fastCreateAccount(email, base64pwkey, name, listener);
}

void MegaApi::sendSignupLink(const char *email, const char *name, const char *password, MegaRequestListener *listener)
{
    pImpl->sendSignupLink(email, name, password, listener);
}

void MegaApi::fastSendSignupLink(const char *email, const char *base64pwkey, const char *name, MegaRequestListener *listener)
{
    pImpl->fastSendSignupLink(email, base64pwkey, name, listener);
}

void MegaApi::querySignupLink(const char* link, MegaRequestListener *listener)
{
    pImpl->querySignupLink(link, listener);
}

void MegaApi::confirmAccount(const char* link, const char *password, MegaRequestListener *listener)
{
    pImpl->confirmAccount(link, password, listener);
}

void MegaApi::fastConfirmAccount(const char* link, const char *base64pwkey, MegaRequestListener *listener)
{
    pImpl->fastConfirmAccount(link, base64pwkey, listener);
}

void MegaApi::resetPassword(const char *email, bool hasMasterKey, MegaRequestListener *listener)
{
    pImpl->resetPassword(email, hasMasterKey, listener);
}

void MegaApi::queryResetPasswordLink(const char *link, MegaRequestListener *listener)
{
    pImpl->queryRecoveryLink(link, listener);
}

void MegaApi::confirmResetPassword(const char *link, const char *newPwd, const char *masterKey, MegaRequestListener *listener)
{
    pImpl->confirmResetPasswordLink(link, newPwd, masterKey, listener);
}

void MegaApi::cancelAccount(MegaRequestListener *listener)
{
    pImpl->cancelAccount(listener);
}

void MegaApi::queryCancelLink(const char *link, MegaRequestListener *listener)
{
    pImpl->queryRecoveryLink(link, listener);
}

void MegaApi::confirmCancelAccount(const char *link, const char *pwd, MegaRequestListener *listener)
{
    pImpl->confirmCancelAccount(link, pwd, listener);
}

void MegaApi::changeEmail(const char *email, MegaRequestListener *listener)
{
    pImpl->changeEmail(email, listener);
}

void MegaApi::queryChangeEmailLink(const char *link, MegaRequestListener *listener)
{
    pImpl->queryRecoveryLink(link, listener);
}

void MegaApi::confirmChangeEmail(const char *link, const char *pwd, MegaRequestListener *listener)
{
    pImpl->confirmChangeEmail(link, pwd, listener);
}

void MegaApi::setProxySettings(MegaProxy *proxySettings)
{
    pImpl->setProxySettings(proxySettings);
}

MegaProxy *MegaApi::getAutoProxySettings()
{
    return pImpl->getAutoProxySettings();
}

void MegaApi::createFolder(const char *name, MegaNode *parent, MegaRequestListener *listener)
{
    pImpl->createFolder(name, parent, listener);
}

bool MegaApi::createLocalFolder(const char *localPath)
{
    return pImpl->createLocalFolder(localPath);
}

void MegaApi::moveNode(MegaNode *node, MegaNode *newParent, MegaRequestListener *listener)
{
    pImpl->moveNode(node, newParent, listener);
}

void MegaApi::copyNode(MegaNode *node, MegaNode* target, MegaRequestListener *listener)
{
    pImpl->copyNode(node, target, listener);
}

void MegaApi::copyNode(MegaNode *node, MegaNode *newParent, const char *newName, MegaRequestListener *listener)
{
    pImpl->copyNode(node, newParent, newName, listener);
}

void MegaApi::renameNode(MegaNode *node, const char *newName, MegaRequestListener *listener)
{
    pImpl->renameNode(node, newName, listener);
}

void MegaApi::remove(MegaNode *node, MegaRequestListener *listener)
{
    pImpl->remove(node, false, listener);
}

void MegaApi::removeVersions(MegaRequestListener *listener)
{
    pImpl->removeVersions(listener);
}

void MegaApi::removeVersion(MegaNode *node, MegaRequestListener *listener)
{
    pImpl->remove(node, true, listener);
}

void MegaApi::restoreVersion(MegaNode *version, MegaRequestListener *listener)
{
    pImpl->restoreVersion(version, listener);
}

void MegaApi::cleanRubbishBin(MegaRequestListener *listener)
{
    pImpl->cleanRubbishBin(listener);
}

void MegaApi::sendFileToUser(MegaNode *node, MegaUser *user, MegaRequestListener *listener)
{
    pImpl->sendFileToUser(node, user, listener);
}

void MegaApi::sendFileToUser(MegaNode *node, const char* email, MegaRequestListener *listener)
{
    pImpl->sendFileToUser(node, email, listener);
}

void MegaApi::share(MegaNode* node, MegaUser *user, int access, MegaRequestListener *listener)
{
    pImpl->share(node, user, access, listener);
}

void MegaApi::share(MegaNode *node, const char* email, int access, MegaRequestListener *listener)
{
    pImpl->share(node, email, access, listener);
}

void MegaApi::loginToFolder(const char* megaFolderLink, MegaRequestListener *listener)
{
    pImpl->loginToFolder(megaFolderLink, listener);
}

void MegaApi::importFileLink(const char* megaFileLink, MegaNode *parent, MegaRequestListener *listener)
{
    pImpl->importFileLink(megaFileLink, parent, listener);
}

void MegaApi::decryptPasswordProtectedLink(const char *link, const char *password, MegaRequestListener *listener)
{
    pImpl->decryptPasswordProtectedLink(link, password, listener);
}

void MegaApi::encryptLinkWithPassword(const char *link, const char *password, MegaRequestListener *listener)
{
    pImpl->encryptLinkWithPassword(link, password, listener);
}

void MegaApi::getPublicNode(const char* megaFileLink, MegaRequestListener *listener)
{
    pImpl->getPublicNode(megaFileLink, listener);
}

void MegaApi::getThumbnail(MegaNode* node, const char *dstFilePath, MegaRequestListener *listener)
{
    pImpl->getThumbnail(node, dstFilePath, listener);
}

void MegaApi::cancelGetThumbnail(MegaNode* node, MegaRequestListener *listener)
{
	pImpl->cancelGetThumbnail(node, listener);
}

void MegaApi::setThumbnail(MegaNode* node, const char *srcFilePath, MegaRequestListener *listener)
{
    pImpl->setThumbnail(node, srcFilePath, listener);
}

void MegaApi::getPreview(MegaNode* node, const char *dstFilePath, MegaRequestListener *listener)
{
    pImpl->getPreview(node, dstFilePath, listener);
}

void MegaApi::cancelGetPreview(MegaNode* node, MegaRequestListener *listener)
{
	pImpl->cancelGetPreview(node, listener);
}

void MegaApi::setPreview(MegaNode* node, const char *srcFilePath, MegaRequestListener *listener)
{
    pImpl->setPreview(node, srcFilePath, listener);
}

void MegaApi::getUserAvatar(MegaUser* user, const char *dstFilePath, MegaRequestListener *listener)
{
    pImpl->getUserAvatar(user, dstFilePath, listener);
}

void MegaApi::getUserAvatar(const char* email_or_handle, const char *dstFilePath, MegaRequestListener *listener)
{
    pImpl->getUserAvatar(email_or_handle, dstFilePath, listener);
}

void MegaApi::getUserAvatar(const char *dstFilePath, MegaRequestListener *listener)
{
    pImpl->getUserAvatar((MegaUser*)NULL, dstFilePath, listener);
}

char *MegaApi::getUserAvatarColor(MegaUser *user)
{
    return MegaApiImpl::getUserAvatarColor(user);
}

char *MegaApi::getUserAvatarColor(const char *userhandle)
{
    return MegaApiImpl::getUserAvatarColor(userhandle);
}

void MegaApi::setAvatar(const char *dstFilePath, MegaRequestListener *listener)
{
    pImpl->setAvatar(dstFilePath, listener);
}

void MegaApi::getUserAttribute(MegaUser* user, int type, MegaRequestListener *listener)
{
    pImpl->getUserAttribute(user, type, listener);
}

void MegaApi::getUserAttribute(int type, MegaRequestListener *listener)
{
    pImpl->getUserAttribute((MegaUser*)NULL, type, listener);
}

void MegaApi::getUserEmail(MegaHandle handle, MegaRequestListener *listener)
{
    pImpl->getUserEmail(handle, listener);
}

void MegaApi::getUserAttribute(const char *email_or_handle, int type, MegaRequestListener *listener)
{
    pImpl->getUserAttribute(email_or_handle, type, listener);
}

void MegaApi::setUserAttribute(int type, const char *value, MegaRequestListener *listener)
{
    pImpl->setUserAttribute(type, value, listener);
}

void MegaApi::setUserAttribute(int type, const MegaStringMap *value, MegaRequestListener *listener)
{
    pImpl->setUserAttribute(type, value, listener);
}

void MegaApi::setCustomNodeAttribute(MegaNode *node, const char *attrName, const char *value, MegaRequestListener *listener)
{
    pImpl->setCustomNodeAttribute(node, attrName, value, listener);
}

void MegaApi::setNodeDuration(MegaNode *node, int secs, MegaRequestListener *listener)
{
    pImpl->setNodeDuration(node, secs, listener);
}

void MegaApi::setNodeCoordinates(MegaNode *node, double latitude, double longitude, MegaRequestListener *listener)
{
    pImpl->setNodeCoordinates(node, latitude, longitude, listener);
}

void MegaApi::exportNode(MegaNode *node, MegaRequestListener *listener)
{
    pImpl->exportNode(node, 0, listener);
}

void MegaApi::exportNode(MegaNode *node, int64_t expireTime, MegaRequestListener *listener)
{
    pImpl->exportNode(node, expireTime, listener);
}

void MegaApi::disableExport(MegaNode *node, MegaRequestListener *listener)
{
    pImpl->disableExport(node, listener);
}

void MegaApi::fetchNodes(MegaRequestListener *listener)
{
    pImpl->fetchNodes(listener);
}

void MegaApi::getAccountDetails(MegaRequestListener *listener)
{
    pImpl->getAccountDetails(true, true, true, false, false, false, listener);
}

void MegaApi::getExtendedAccountDetails(bool sessions, bool purchases, bool transactions, MegaRequestListener *listener)
{
    pImpl->getAccountDetails(true, true, true, sessions, purchases, transactions, listener);
}

void MegaApi::queryTransferQuota(long long size, MegaRequestListener *listener)
{
    pImpl->queryTransferQuota(size, listener);
}

void MegaApi::getPricing(MegaRequestListener *listener)
{
    pImpl->getPricing(listener);
}

void MegaApi::getPaymentId(MegaHandle productHandle, MegaRequestListener *listener)
{
    pImpl->getPaymentId(productHandle, listener);
}

void MegaApi::upgradeAccount(MegaHandle productHandle, int paymentMethod, MegaRequestListener *listener)
{
    pImpl->upgradeAccount(productHandle, paymentMethod, listener);
}

void MegaApi::submitPurchaseReceipt(const char *receipt, MegaRequestListener *listener)
{
    pImpl->submitPurchaseReceipt(MegaApi::PAYMENT_METHOD_GOOGLE_WALLET, receipt, listener);
}

void MegaApi::submitPurchaseReceipt(int gateway, const char *receipt, MegaRequestListener *listener)
{
    pImpl->submitPurchaseReceipt(gateway, receipt, listener);
}

void MegaApi::creditCardStore(const char* address1, const char* address2, const char* city,
                     const char* province, const char* country, const char *postalcode,
                     const char* firstname, const char* lastname, const char* creditcard,
                     const char* expire_month, const char* expire_year, const char* cv2,
                     MegaRequestListener *listener)
{
    pImpl->creditCardStore(address1, address2, city, province, country, postalcode, firstname,
                           lastname, creditcard, expire_month, expire_year, cv2, listener);
}

void MegaApi::creditCardQuerySubscriptions(MegaRequestListener *listener)
{
    pImpl->creditCardQuerySubscriptions(listener);
}

void MegaApi::creditCardCancelSubscriptions(const char* reason, MegaRequestListener *listener)
{
    pImpl->creditCardCancelSubscriptions(reason, listener);
}

void MegaApi::getPaymentMethods(MegaRequestListener *listener)
{
    pImpl->getPaymentMethods(listener);
}

char *MegaApi::exportMasterKey()
{
    return pImpl->exportMasterKey();
}

void MegaApi::masterKeyExported(MegaRequestListener *listener)
{
    pImpl->updatePwdReminderData(false, false, true, false, false, listener);
}

void MegaApi::changePassword(const char *oldPassword, const char *newPassword, MegaRequestListener *listener)
{
    pImpl->changePassword(oldPassword, newPassword, listener);
}

void MegaApi::logout(MegaRequestListener *listener)
{
    pImpl->logout(listener);
}

void MegaApi::localLogout(MegaRequestListener *listener)
{
    pImpl->localLogout(listener);
}

void MegaApi::invalidateCache()
{
    pImpl->invalidateCache();
}

int MegaApi::getPasswordStrength(const char *password)
{
    return pImpl->getPasswordStrength(password);
}

void MegaApi::submitFeedback(int rating, const char *comment, MegaRequestListener* listener)
{
    pImpl->submitFeedback(rating, comment, listener);
}

void MegaApi::sendEvent(int eventType, const char *message, MegaRequestListener *listener)
{
    pImpl->sendEvent(eventType, message, listener);
}

void MegaApi::reportDebugEvent(const char *text, MegaRequestListener *listener)
{
    pImpl->reportEvent(text, listener);
}

void MegaApi::useHttpsOnly(bool httpsOnly, MegaRequestListener *listener)
{
    pImpl->useHttpsOnly(httpsOnly, listener);
}

bool MegaApi::usingHttpsOnly()
{
    return pImpl->usingHttpsOnly();
}

void MegaApi::inviteContact(const char *email, const char *message, int action, MegaRequestListener *listener)
{
    pImpl->inviteContact(email, message, action, listener);
}

void MegaApi::replyContactRequest(MegaContactRequest *r, int action, MegaRequestListener *listener)
{
    pImpl->replyContactRequest(r, action, listener);
}

void MegaApi::removeContact(MegaUser *user, MegaRequestListener* listener)
{
    pImpl->removeContact(user, listener);
}

void MegaApi::pauseTransfers(bool pause, MegaRequestListener* listener)
{
    pImpl->pauseTransfers(pause, -1, listener);
}

void MegaApi::pauseTransfers(bool pause, int direction, MegaRequestListener *listener)
{
    pImpl->pauseTransfers(pause, direction, listener);
}

void MegaApi::pauseTransfer(MegaTransfer *transfer, bool pause, MegaRequestListener *listener)
{
    pImpl->pauseTransfer(transfer ? transfer->getTag() : 0, pause, listener);
}

void MegaApi::pauseTransferByTag(int transferTag, bool pause, MegaRequestListener *listener)
{
    pImpl->pauseTransfer(transferTag, pause, listener);
}

void MegaApi::enableTransferResumption(const char *loggedOutId)
{
    pImpl->enableTransferResumption(loggedOutId);
}

void MegaApi::disableTransferResumption(const char *loggedOutId)
{
    pImpl->disableTransferResumption(loggedOutId);
}

bool MegaApi::areTransfersPaused(int direction)
{
    return pImpl->areTransfersPaused(direction);
}

//-1 -> AUTO, 0 -> NONE, >0 -> b/s
void MegaApi::setUploadLimit(int bpslimit)
{
    pImpl->setUploadLimit(bpslimit);
}

void MegaApi::setMaxConnections(int direction, int connections, MegaRequestListener *listener)
{
    pImpl->setMaxConnections(direction,  connections, listener);
}

void MegaApi::setMaxConnections(int connections, MegaRequestListener *listener)
{
    pImpl->setMaxConnections(-1,  connections, listener);
}

void MegaApi::setDownloadMethod(int method)
{
    pImpl->setDownloadMethod(method);
}

void MegaApi::setUploadMethod(int method)
{
    pImpl->setUploadMethod(method);
}

int MegaApi::getMaxDownloadSpeed()
{
    return pImpl->getMaxDownloadSpeed();
}

int MegaApi::getMaxUploadSpeed()
{
    return pImpl->getMaxUploadSpeed();
}

bool MegaApi::setMaxDownloadSpeed(long long bpslimit)
{
    return pImpl->setMaxDownloadSpeed(bpslimit);
}

bool MegaApi::setMaxUploadSpeed(long long bpslimit)
{
    return pImpl->setMaxUploadSpeed(bpslimit);
}

int MegaApi::getCurrentDownloadSpeed()
{
    return pImpl->getCurrentDownloadSpeed();
}

int MegaApi::getCurrentUploadSpeed()
{
    return pImpl->getCurrentUploadSpeed();
}

int MegaApi::getCurrentSpeed(int type)
{
    return pImpl->getCurrentSpeed(type);
}

int MegaApi::getDownloadMethod()
{
    return pImpl->getDownloadMethod();
}

int MegaApi::getUploadMethod()
{
    return pImpl->getUploadMethod();
}

MegaTransferData *MegaApi::getTransferData(MegaTransferListener *listener)
{
    return pImpl->getTransferData(listener);
}

MegaTransfer *MegaApi::getFirstTransfer(int type)
{
    return pImpl->getFirstTransfer(type);
}

void MegaApi::notifyTransfer(MegaTransfer *transfer, MegaTransferListener *listener)
{
    pImpl->notifyTransfer(transfer ? transfer->getTag() : 0, listener);
}

void MegaApi::notifyTransferByTag(int transferTag, MegaTransferListener *listener)
{
    pImpl->notifyTransfer(transferTag, listener);
}

MegaTransferList *MegaApi::getTransfers()
{
    return pImpl->getTransfers();
}

MegaTransferList *MegaApi::getStreamingTransfers()
{
    return pImpl->getStreamingTransfers();
}

MegaTransfer *MegaApi::getTransferByTag(int transferTag)
{
    return pImpl->getTransferByTag(transferTag);
}

MegaTransferList *MegaApi::getTransfers(int type)
{
    return pImpl->getTransfers(type);
}

MegaTransferList *MegaApi::getChildTransfers(int transferTag)
{
    return pImpl->getChildTransfers(transferTag);
}

void MegaApi::startUpload(const char* localPath, MegaNode* parent, MegaTransferListener *listener)
{
    pImpl->startUpload(localPath, parent, listener);
}

void MegaApi::startUploadWithData(const char *localPath, MegaNode *parent, const char *appData, MegaTransferListener *listener)
{
    pImpl->startUpload(localPath, parent, (const char *)NULL, -1, 0, appData, false, listener);
}

void MegaApi::startUploadWithData(const char *localPath, MegaNode *parent, const char *appData, bool isSourceTemporary, MegaTransferListener *listener)
{
    pImpl->startUpload(localPath, parent, (const char *)NULL, -1, 0, appData, isSourceTemporary, listener);
}

void MegaApi::startUpload(const char *localPath, MegaNode *parent, int64_t mtime, MegaTransferListener *listener)
{
    pImpl->startUpload(localPath, parent, mtime, listener);
}

void MegaApi::startUpload(const char *localPath, MegaNode *parent, int64_t mtime, bool isSourceTemporary, MegaTransferListener *listener)
{
    pImpl->startUpload(localPath, parent, (const char *)NULL, mtime, 0, NULL, isSourceTemporary, listener);
}

void MegaApi::startUpload(const char* localPath, MegaNode* parent, const char* fileName, MegaTransferListener *listener)
{
    pImpl->startUpload(localPath, parent, fileName, listener);
}

void MegaApi::startUpload(const char *localPath, MegaNode *parent, const char *fileName, int64_t mtime, MegaTransferListener *listener)
{
    pImpl->startUpload(localPath, parent, fileName, mtime, 0, NULL, false, listener);
}

void MegaApi::startDownload(MegaNode *node, const char* localFolder, MegaTransferListener *listener)
{
    pImpl->startDownload(node, localFolder, listener);
}

void MegaApi::startDownloadWithData(MegaNode *node, const char *localPath, const char *appData, MegaTransferListener *listener)
{
    pImpl->startDownload(node, localPath, 0, 0, 0, appData, listener);
}

void MegaApi::cancelTransfer(MegaTransfer *t, MegaRequestListener *listener)
{
    pImpl->cancelTransfer(t, listener);
}

void MegaApi::retryTransfer(MegaTransfer *transfer, MegaTransferListener *listener)
{
    pImpl->retryTransfer(transfer, listener);
}

void MegaApi::moveTransferUp(MegaTransfer *transfer, MegaRequestListener *listener)
{
    pImpl->moveTransferUp(transfer ? transfer->getTag() : 0, listener);
}

void MegaApi::moveTransferUpByTag(int transferTag, MegaRequestListener *listener)
{
    pImpl->moveTransferUp(transferTag, listener);
}

void MegaApi::moveTransferDown(MegaTransfer *transfer, MegaRequestListener *listener)
{
    pImpl->moveTransferDown(transfer ? transfer->getTag() : 0, listener);
}

void MegaApi::moveTransferDownByTag(int transferTag, MegaRequestListener *listener)
{
    pImpl->moveTransferDown(transferTag, listener);
}

void MegaApi::moveTransferToFirst(MegaTransfer *transfer, MegaRequestListener *listener)
{
    pImpl->moveTransferToFirst(transfer ? transfer->getTag() : 0, listener);
}

void MegaApi::moveTransferToFirstByTag(int transferTag, MegaRequestListener *listener)
{
    pImpl->moveTransferToFirst(transferTag, listener);
}

void MegaApi::moveTransferToLast(MegaTransfer *transfer, MegaRequestListener *listener)
{
    pImpl->moveTransferToLast(transfer ? transfer->getTag() : 0, listener);
}

void MegaApi::moveTransferToLastByTag(int transferTag, MegaRequestListener *listener)
{
    pImpl->moveTransferToLast(transferTag, listener);
}

void MegaApi::moveTransferBefore(MegaTransfer *transfer, MegaTransfer *prevTransfer, MegaRequestListener *listener)
{
    pImpl->moveTransferBefore(transfer ? transfer->getTag() : 0, prevTransfer ? prevTransfer->getTag() : 0, listener);
}

void MegaApi::moveTransferBeforeByTag(int transferTag, int prevTransferTag, MegaRequestListener *listener)
{
    pImpl->moveTransferBefore(transferTag, prevTransferTag, listener);
}

void MegaApi::cancelTransferByTag(int transferTag, MegaRequestListener *listener)
{
    pImpl->cancelTransferByTag(transferTag, listener);
}

void MegaApi::cancelTransfers(int direction, MegaRequestListener *listener)
{
    pImpl->cancelTransfers(direction, listener);
}

void MegaApi::startStreaming(MegaNode* node, int64_t startPos, int64_t size, MegaTransferListener *listener)
{
    pImpl->startStreaming(node, startPos, size, listener);
}

#ifdef ENABLE_SYNC

//Move local files inside synced folders to the "Rubbish" folder.
bool MegaApi::moveToLocalDebris(const char *path)
{
    return pImpl->moveToLocalDebris(path);
}

int MegaApi::syncPathState(string* path)
{
    return pImpl->syncPathState(path);
}

MegaNode *MegaApi::getSyncedNode(string *path)
{
    return pImpl->getSyncedNode(path);
}

void MegaApi::syncFolder(const char *localFolder, MegaNode *megaFolder, MegaRequestListener *listener)
{
    pImpl->syncFolder(localFolder, megaFolder, NULL, listener);
}

void MegaApi::resumeSync(const char *localFolder, MegaNode *megaFolder, long long localfp, MegaRequestListener *listener)
{
    pImpl->resumeSync(localFolder, localfp, megaFolder, NULL, listener);
}

#ifdef USE_PCRE
void MegaApi::syncFolder(const char *localFolder, MegaNode *megaFolder, MegaRegExp *regExp, MegaRequestListener *listener)
{
    pImpl->syncFolder(localFolder, megaFolder, regExp, listener);
}

void MegaApi::resumeSync(const char *localFolder, MegaNode *megaFolder, long long localfp, MegaRegExp *regExp, MegaRequestListener *listener)
{
    pImpl->resumeSync(localFolder, localfp, megaFolder, regExp, listener);
}
#endif

void MegaApi::removeSync(MegaNode *megaFolder, MegaRequestListener* listener)
{
    pImpl->removeSync(megaFolder ? megaFolder->getHandle() : UNDEF, listener);
}

void MegaApi::removeSync(MegaSync *sync, MegaRequestListener *listener)
{
    pImpl->removeSync(sync ? sync->getMegaHandle() : UNDEF, listener);
}

void MegaApi::disableSync(MegaNode *megaFolder, MegaRequestListener *listener)
{
    pImpl->disableSync(megaFolder ? megaFolder->getHandle() : UNDEF, listener);
}

void MegaApi::disableSync(MegaSync *sync, MegaRequestListener *listener)
{
    pImpl->disableSync(sync ? sync->getMegaHandle() : UNDEF, listener);
}

void MegaApi::removeSyncs(MegaRequestListener *listener)
{
   pImpl->stopSyncs(listener);
}

int MegaApi::getNumActiveSyncs()
{
    return pImpl->getNumActiveSyncs();
}

string MegaApi::getLocalPath(MegaNode *n)
{
    return pImpl->getLocalPath(n);
}

long long MegaApi::getNumLocalNodes()
{
    return pImpl->getNumLocalNodes();
}

char *MegaApi::getBlockedPath()
{
    return pImpl->getBlockedPath();
}

MegaSync *MegaApi::getSyncByTag(int tag)
{
    return pImpl->getSyncByTag(tag);
}

MegaSync *MegaApi::getSyncByNode(MegaNode *node)
{
    return pImpl->getSyncByNode(node);
}

MegaSync *MegaApi::getSyncByPath(const char *localPath)
{
    return pImpl->getSyncByPath(localPath);
}

bool MegaApi::isScanning()
{
    return pImpl->isIndexing();
}

bool MegaApi::isSynced(MegaNode *n)
{
    return pImpl->isSynced(n);
}

bool MegaApi::isSyncable(const char *path, long long size)
{
    return pImpl->isSyncable(path, size);
}

int MegaApi::isNodeSyncable(MegaNode *node)
{
    return pImpl->isNodeSyncable(node);
}

void MegaApi::setExcludedNames(vector<string> *excludedNames)
{
    pImpl->setExcludedNames(excludedNames);
}

void MegaApi::setExcludedPaths(vector<string> *excludedPaths)
{
    pImpl->setExcludedPaths(excludedPaths);
}

void MegaApi::setExclusionLowerSizeLimit(long long limit)
{
    pImpl->setExclusionLowerSizeLimit(limit);
}

void MegaApi::setExclusionUpperSizeLimit(long long limit)
{
    pImpl->setExclusionUpperSizeLimit(limit);
}

#ifdef USE_PCRE
void MegaApi::setExcludedRegularExpressions(MegaSync *sync, MegaRegExp *regExp)
{
    pImpl->setExcludedRegularExpressions(sync, regExp);
}
#endif
#endif

int MegaApi::getNumPendingUploads()
{
    return pImpl->getNumPendingUploads();
}

int MegaApi::getNumPendingDownloads()
{
    return pImpl->getNumPendingDownloads();
}

int MegaApi::getTotalUploads()
{
    return pImpl->getTotalUploads();
}

int MegaApi::getTotalDownloads()
{
    return pImpl->getTotalDownloads();
}

void MegaApi::resetTotalDownloads()
{
    pImpl->resetTotalDownloads();
}

void MegaApi::resetTotalUploads()
{
    pImpl->resetTotalUploads();
}

MegaNode *MegaApi::getRootNode()
{
    return pImpl->getRootNode();
}

MegaNode* MegaApi::getInboxNode()
{
    return pImpl->getInboxNode();
}

MegaNode* MegaApi::getRubbishNode()
{
    return pImpl->getRubbishNode();
}

MegaNode *MegaApi::getRootNode(MegaNode *node)
{
    return pImpl->getRootNode(node);
}

bool MegaApi::isInCloud(MegaNode *node)
{
    return pImpl->isInRootnode(node, 0);
}

bool MegaApi::isInRubbish(MegaNode *node)
{
    return pImpl->isInRootnode(node, 2);
}

bool MegaApi::isInInbox(MegaNode *node)
{
    return pImpl->isInRootnode(node, 1);
}

void MegaApi::setDefaultFilePermissions(int permissions)
{
    pImpl->setDefaultFilePermissions(permissions);
}

int MegaApi::getDefaultFilePermissions()
{
    return pImpl->getDefaultFilePermissions();
}

void MegaApi::setDefaultFolderPermissions(int permissions)
{
    pImpl->setDefaultFolderPermissions(permissions);
}

int MegaApi::getDefaultFolderPermissions()
{
    return pImpl->getDefaultFolderPermissions();
}

long long MegaApi::getBandwidthOverquotaDelay()
{
    return pImpl->getBandwidthOverquotaDelay();
}

MegaUserList* MegaApi::getContacts()
{
    return pImpl->getContacts();
}

MegaUser* MegaApi::getContact(const char* user)
{
    return pImpl->getContact(user);
}

MegaNodeList* MegaApi::getInShares(MegaUser *megaUser)
{
    return pImpl->getInShares(megaUser);
}

MegaNodeList* MegaApi::getInShares()
{
    return pImpl->getInShares();
}

MegaShareList* MegaApi::getInSharesList()
{
    return pImpl->getInSharesList();
}

MegaUser *MegaApi::getUserFromInShare(MegaNode *node)
{
    return pImpl->getUserFromInShare(node);
}

bool MegaApi::isShared(MegaNode *node)
{
    if (!node)
    {
        return false;
    }

    return node->isShared();
}

bool MegaApi::isOutShare(MegaNode *node)
{
    if (!node)
    {
        return false;
    }

    return node->isOutShare();
}

bool MegaApi::isInShare(MegaNode *node)
{
    if (!node)
    {
        return false;
    }

    return node->isInShare();
}

bool MegaApi::isPendingShare(MegaNode *node)
{
    return pImpl->isPendingShare(node);
}

MegaShareList *MegaApi::getOutShares()
{
    return pImpl->getOutShares();
}

MegaShareList* MegaApi::getOutShares(MegaNode *megaNode)
{
    return pImpl->getOutShares(megaNode);
}

MegaShareList *MegaApi::getPendingOutShares()
{
    return pImpl->getPendingOutShares();
}

MegaShareList *MegaApi::getPendingOutShares(MegaNode *node)
{
    return pImpl->getPendingOutShares(node);
}

MegaNodeList *MegaApi::getPublicLinks()
{
    return pImpl->getPublicLinks();
}

MegaContactRequestList *MegaApi::getIncomingContactRequests()
{
    return pImpl->getIncomingContactRequests();
}

MegaContactRequestList *MegaApi::getOutgoingContactRequests()
{
    return pImpl->getOutgoingContactRequests();
}

int MegaApi::getAccess(MegaNode* megaNode)
{
    return pImpl->getAccess(megaNode);
}

bool MegaApi::processMegaTree(MegaNode* n, MegaTreeProcessor* processor, bool recursive)
{
    return pImpl->processMegaTree(n, processor, recursive);
}

MegaNode *MegaApi::createForeignFileNode(MegaHandle handle, const char *key,
                                    const char *name, int64_t size, int64_t mtime,
                                        MegaHandle parentHandle, const char *privateAuth, const char *publicAuth)
{
    return pImpl->createForeignFileNode(handle, key, name, size, mtime, parentHandle, privateAuth, publicAuth);
}

void MegaApi::getLastAvailableVersion(const char *appKey, MegaRequestListener *listener)
{
    return pImpl->getLastAvailableVersion(appKey, listener);
}

void MegaApi::getLocalSSLCertificate(MegaRequestListener *listener)
{
    pImpl->getLocalSSLCertificate(listener);
}

void MegaApi::queryDNS(const char *hostname, MegaRequestListener *listener)
{
    pImpl->queryDNS(hostname, listener);
}

void MegaApi::queryGeLB(const char *service, int timeoutms, int maxretries, MegaRequestListener *listener)
{
    pImpl->queryGeLB(service, timeoutms, maxretries, listener);
}

void MegaApi::downloadFile(const char *url, const char *dstpath, MegaRequestListener *listener)
{
    pImpl->downloadFile(url, dstpath, listener);
}

MegaNode *MegaApi::createForeignFolderNode(MegaHandle handle, const char *name, MegaHandle parentHandle, const char *privateAuth, const char *publicAuth)
{
    return pImpl->createForeignFolderNode(handle, name, parentHandle, privateAuth, publicAuth);
}

MegaNode *MegaApi::authorizeNode(MegaNode *node)
{
    return pImpl->authorizeNode(node);
}

const char *MegaApi::getVersion()
{
    return pImpl->getVersion();
}

char *MegaApi::getOperatingSystemVersion()
{
    return pImpl->getOperatingSystemVersion();
}

const char *MegaApi::getUserAgent()
{
    return pImpl->getUserAgent();
}

const char *MegaApi::getBasePath()
{
    return pImpl->getBasePath();
}

void MegaApi::changeApiUrl(const char *apiURL, bool disablepkp)
{
    pImpl->changeApiUrl(apiURL, disablepkp);
}

bool MegaApi::setLanguage(const char *languageCode)
{
    return pImpl->setLanguage(languageCode);
}

void MegaApi::setLanguagePreference(const char *languageCode, MegaRequestListener *listener)
{
    pImpl->setLanguagePreference(languageCode, listener);
}

void MegaApi::getLanguagePreference(MegaRequestListener *listener)
{
    pImpl->getLanguagePreference(listener);
}

void MegaApi::setFileVersionsOption(bool disable, MegaRequestListener *listener)
{
    pImpl->setFileVersionsOption(disable, listener);
}

void MegaApi::getFileVersionsOption(MegaRequestListener *listener)
{
    pImpl->getFileVersionsOption(listener);
}

void MegaApi::retrySSLerrors(bool enable)
{
    pImpl->retrySSLerrors(enable);
}

void MegaApi::setPublicKeyPinning(bool enable)
{
    pImpl->setPublicKeyPinning(enable);
}

void MegaApi::pauseActionPackets()
{
    pImpl->pauseActionPackets();
}

void MegaApi::resumeActionPackets()
{
    pImpl->resumeActionPackets();
}

char *MegaApi::base64ToBase32(const char *base64)
{
    if(!base64)
    {
        return NULL;
    }

    unsigned binarylen = strlen(base64) * 3/4 + 4;
    byte *binary = new byte[binarylen];
    binarylen = Base64::atob(base64, binary, binarylen);

    char *result = new char[binarylen * 8/5 + 6];
    Base32::btoa(binary, binarylen, result);
    delete [] binary;

    return result;
}

char *MegaApi::base32ToBase64(const char *base32)
{
    if(!base32)
    {
        return NULL;
    }

    unsigned binarylen = strlen(base32) * 5/8 + 8;
    byte *binary = new byte[binarylen];
    binarylen = Base32::atob(base32, binary, binarylen);

    char *result = new char[binarylen * 4/3 + 4];
    Base64::btoa(binary, binarylen, result);
    delete [] binary;

    return result;
}

MegaNodeList* MegaApi::search(MegaNode* n, const char* searchString, bool recursive)
{
    return pImpl->search(n, searchString, recursive);
}

MegaNodeList *MegaApi::search(const char *searchString)
{
    return pImpl->search(searchString);
}

long long MegaApi::getSize(MegaNode *n)
{
    return pImpl->getSize(n);
}

char *MegaApi::getFingerprint(const char *filePath)
{
    return pImpl->getFingerprint(filePath);
}

char *MegaApi::getFingerprint(MegaNode *node)
{
    return pImpl->getFingerprint(node);
}

char *MegaApi::getFingerprint(MegaInputStream *inputStream, int64_t mtime)
{
    return pImpl->getFingerprint(inputStream, mtime);
}

MegaNode *MegaApi::getNodeByFingerprint(const char *fingerprint)
{
    return pImpl->getNodeByFingerprint(fingerprint);
}

MegaNode *MegaApi::getNodeByFingerprint(const char *fingerprint, MegaNode *parent)
{
    return pImpl->getNodeByFingerprint(fingerprint, parent);
}

MegaNodeList *MegaApi::getNodesByFingerprint(const char *fingerprint)
{
    return pImpl->getNodesByFingerprint(fingerprint);
}

MegaNode *MegaApi::getExportableNodeByFingerprint(const char *fingerprint, const char *name)
{
    return pImpl->getExportableNodeByFingerprint(fingerprint, name);
}

bool MegaApi::hasFingerprint(const char *fingerprint)
{
    return pImpl->hasFingerprint(fingerprint);
}

char *MegaApi::getCRC(const char *filePath)
{
    return pImpl->getCRC(filePath);
}

char *MegaApi::getCRCFromFingerprint(const char *fingerprint)
{
    return pImpl->getCRCFromFingerprint(fingerprint);
}

char *MegaApi::getCRC(MegaNode *node)
{
    return pImpl->getCRC(node);
}

MegaNode *MegaApi::getNodeByCRC(const char *crc, MegaNode *parent)
{
    return pImpl->getNodeByCRC(crc, parent);
}

void MegaApi::addListener(MegaListener* listener)
{
    pImpl->addListener(listener);
}

void MegaApi::addRequestListener(MegaRequestListener* listener)
{
    pImpl->addRequestListener(listener);
}

void MegaApi::addTransferListener(MegaTransferListener* listener)
{
    pImpl->addTransferListener(listener);
}

void MegaApi::addGlobalListener(MegaGlobalListener* listener)
{
    pImpl->addGlobalListener(listener);
}

#ifdef ENABLE_SYNC
void MegaApi::addSyncListener(MegaSyncListener *listener)
{
    pImpl->addSyncListener(listener);
}

void MegaApi::removeSyncListener(MegaSyncListener *listener)
{
    pImpl->removeSyncListener(listener);
}
#endif

void MegaApi::removeListener(MegaListener* listener)
{
    pImpl->removeListener(listener);
}

void MegaApi::removeRequestListener(MegaRequestListener* listener)
{
    pImpl->removeRequestListener(listener);
}

void MegaApi::removeTransferListener(MegaTransferListener* listener)
{
    pImpl->removeTransferListener(listener);
}

void MegaApi::removeGlobalListener(MegaGlobalListener* listener)
{
    pImpl->removeGlobalListener(listener);
}

MegaRequest *MegaApi::getCurrentRequest()
{
    return pImpl->getCurrentRequest();
}

MegaTransfer *MegaApi::getCurrentTransfer()
{
    return pImpl->getCurrentTransfer();
}

MegaError *MegaApi::getCurrentError()
{
    return pImpl->getCurrentError();
}

MegaNodeList *MegaApi::getCurrentNodes()
{
    return pImpl->getCurrentNodes();
}

MegaUserList *MegaApi::getCurrentUsers()
{
    return pImpl->getCurrentUsers();
}

MegaError MegaApi::checkAccess(MegaNode* megaNode, int level)
{
    return pImpl->checkAccess(megaNode, level);
}

MegaError MegaApi::checkMove(MegaNode* megaNode, MegaNode* targetNode)
{
    return pImpl->checkMove(megaNode, targetNode);
}

bool MegaApi::isFilesystemAvailable()
{
    return pImpl->isFilesystemAvailable();
}

int MegaApi::getNumChildren(MegaNode* parent)
{
	return pImpl->getNumChildren(parent);
}

int MegaApi::getNumChildFiles(MegaNode* parent)
{
	return pImpl->getNumChildFiles(parent);
}

int MegaApi::getNumChildFolders(MegaNode* parent)
{
	return pImpl->getNumChildFolders(parent);
}

MegaNodeList *MegaApi::getChildren(MegaNode* p, int order)
{
    return pImpl->getChildren(p, order);
}

MegaNodeList *MegaApi::getVersions(MegaNode *node)
{
    return pImpl->getVersions(node);
}

int MegaApi::getNumVersions(MegaNode *node)
{
    return pImpl->getNumVersions(node);
}

bool MegaApi::hasVersions(MegaNode *node)
{
    return pImpl->hasVersions(node);
}

MegaChildrenLists *MegaApi::getFileFolderChildren(MegaNode *p, int order)
{
    return pImpl->getFileFolderChildren(p, order);
}

bool MegaApi::hasChildren(MegaNode *parent)
{
    return pImpl->hasChildren(parent);
}

int MegaApi::getIndex(MegaNode *node, int order)
{
    return pImpl->getIndex(node, order);
}

MegaNode *MegaApi::getChildNode(MegaNode *parent, const char* name)
{
    return pImpl->getChildNode(parent, name);
}

MegaNode* MegaApi::getParentNode(MegaNode* n)
{
    return pImpl->getParentNode(n);
}

char *MegaApi::getNodePath(MegaNode *node)
{
    return pImpl->getNodePath(node);
}

MegaNode* MegaApi::getNodeByPath(const char *path, MegaNode* node)
{
    return pImpl->getNodeByPath(path, node);
}

MegaNode* MegaApi::getNodeByHandle(uint64_t h)
{
    return pImpl->getNodeByHandle(h);
}

MegaContactRequest *MegaApi::getContactRequestByHandle(MegaHandle handle)
{
    return pImpl->getContactRequestByHandle(handle);
}

void MegaApi::updateStats()
{
    pImpl->updateStats();
}

long long MegaApi::getNumNodes()
{
    return pImpl->getNumNodes();
}

long long MegaApi::getTotalDownloadedBytes()
{
    return pImpl->getTotalDownloadedBytes();
}

long long MegaApi::getTotalUploadedBytes()
{
    return pImpl->getTotalUploadedBytes();
}

long long MegaApi::getTotalDownloadBytes()
{
    return pImpl->getTotalDownloadBytes();
}

long long MegaApi::getTotalUploadBytes()
{
    return pImpl->getTotalUploadBytes();
}

void MegaApi::update()
{
   pImpl->update();
}

bool MegaApi::isWaiting()
{
    return pImpl->isWaiting();
}

bool MegaApi::areServersBusy()
{
    return pImpl->areServersBusy();
}

void MegaApi::removeRecursively(const char *path)
{
    MegaApiImpl::removeRecursively(path);
}

bool MegaApi::isOnline()
{
    return pImpl->isOnline();
}

void MegaApi::getAccountAchievements(MegaRequestListener *listener)
{
    pImpl->getAccountAchievements(listener);
}

void MegaApi::getMegaAchievements(MegaRequestListener *listener)
{
    pImpl->getMegaAchievements(listener);
}

#ifdef HAVE_LIBUV
bool MegaApi::httpServerStart(bool localOnly, int port)
{
    return pImpl->httpServerStart(localOnly, port);
}

void MegaApi::httpServerStop()
{
    pImpl->httpServerStop();
}

int MegaApi::httpServerIsRunning()
{
    return pImpl->httpServerIsRunning();
}

bool MegaApi::httpServerIsLocalOnly()
{
    return pImpl->httpServerIsLocalOnly();
}

void MegaApi::httpServerEnableFileServer(bool enable)
{
    pImpl->httpServerEnableFileServer(enable);
}

bool MegaApi::httpServerIsFileServerEnabled()
{
    return pImpl->httpServerIsFileServerEnabled();
}

void MegaApi::httpServerEnableFolderServer(bool enable)
{
    pImpl->httpServerEnableFolderServer(enable);
}

bool MegaApi::httpServerIsFolderServerEnabled()
{
    return pImpl->httpServerIsFolderServerEnabled();
}

void MegaApi::httpServerSetRestrictedMode(int mode)
{
    pImpl->httpServerSetRestrictedMode(mode);
}

int MegaApi::httpServerGetRestrictedMode()
{
    return pImpl->httpServerGetRestrictedMode();
}

void MegaApi::httpServerEnableSubtitlesSupport(bool enable)
{
    pImpl->httpServerEnableSubtitlesSupport(enable);
}

bool MegaApi::httpServerIsSubtitlesSupportEnabled()
{
    return pImpl->httpServerIsSubtitlesSupportEnabled();
}

void MegaApi::httpServerAddListener(MegaTransferListener *listener)
{
    pImpl->httpServerAddListener(listener);
}

void MegaApi::httpServerRemoveListener(MegaTransferListener *listener)
{
    pImpl->httpServerRemoveListener(listener);
}

char *MegaApi::httpServerGetLocalLink(MegaNode *node)
{
    return pImpl->httpServerGetLocalLink(node);
}

void MegaApi::httpServerSetMaxBufferSize(int bufferSize)
{
    pImpl->httpServerSetMaxBufferSize(bufferSize);
}

int MegaApi::httpServerGetMaxBufferSize()
{
    return pImpl->httpServerGetMaxBufferSize();
}

void MegaApi::httpServerSetMaxOutputSize(int outputSize)
{
    pImpl->httpServerSetMaxOutputSize(outputSize);
}

int MegaApi::httpServerGetMaxOutputSize()
{
    return pImpl->httpServerGetMaxOutputSize();
}

char *MegaApi::getMimeType(const char *extension)
{
    if (!extension)
    {
        return NULL;
    }

    if (*extension == '.')
    {
        extension++;
    }

    static map<string, string> *mimeMap = NULL;
    if (!mimeMap)
    {
        mimeMap = new map<string, string>();
        (*mimeMap)["323"]="text/h323";
        (*mimeMap)["3g2"]="video/3gpp2";
        (*mimeMap)["3gp"]="video/3gpp";
        (*mimeMap)["3gp2"]="video/3gpp2";
        (*mimeMap)["3gpp"]="video/3gpp";
        (*mimeMap)["7z"]="application/x-7z-compressed";
        (*mimeMap)["aa"]="audio/audible";
        (*mimeMap)["AAC"]="audio/aac";
        (*mimeMap)["aaf"]="application/octet-stream";
        (*mimeMap)["aax"]="audio/vnd.audible.aax";
        (*mimeMap)["ac3"]="audio/ac3";
        (*mimeMap)["aca"]="application/octet-stream";
        (*mimeMap)["accda"]="application/msaccess.addin";
        (*mimeMap)["accdb"]="application/msaccess";
        (*mimeMap)["accdc"]="application/msaccess.cab";
        (*mimeMap)["accde"]="application/msaccess";
        (*mimeMap)["accdr"]="application/msaccess.runtime";
        (*mimeMap)["accdt"]="application/msaccess";
        (*mimeMap)["accdw"]="application/msaccess.webapplication";
        (*mimeMap)["accft"]="application/msaccess.ftemplate";
        (*mimeMap)["acx"]="application/internet-property-stream";
        (*mimeMap)["AddIn"]="text/xml";
        (*mimeMap)["ade"]="application/msaccess";
        (*mimeMap)["adobebridge"]="application/x-bridge-url";
        (*mimeMap)["adp"]="application/msaccess";
        (*mimeMap)["ADT"]="audio/vnd.dlna.adts";
        (*mimeMap)["ADTS"]="audio/aac";
        (*mimeMap)["afm"]="application/octet-stream";
        (*mimeMap)["ai"]="application/postscript";
        (*mimeMap)["aif"]="audio/x-aiff";
        (*mimeMap)["aifc"]="audio/aiff";
        (*mimeMap)["aiff"]="audio/aiff";
        (*mimeMap)["air"]="application/vnd.adobe.air-application-installer-package+zip";
        (*mimeMap)["amc"]="application/x-mpeg";
        (*mimeMap)["application"]="application/x-ms-application";
        (*mimeMap)["art"]="image/x-jg";
        (*mimeMap)["asa"]="application/xml";
        (*mimeMap)["asax"]="application/xml";
        (*mimeMap)["ascx"]="application/xml";
        (*mimeMap)["asd"]="application/octet-stream";
        (*mimeMap)["asf"]="video/x-ms-asf";
        (*mimeMap)["ashx"]="application/xml";
        (*mimeMap)["asi"]="application/octet-stream";
        (*mimeMap)["asm"]="text/plain";
        (*mimeMap)["asmx"]="application/xml";
        (*mimeMap)["aspx"]="application/xml";
        (*mimeMap)["asr"]="video/x-ms-asf";
        (*mimeMap)["asx"]="video/x-ms-asf";
        (*mimeMap)["atom"]="application/atom+xml";
        (*mimeMap)["au"]="audio/basic";
        (*mimeMap)["avi"]="video/x-msvideo";
        (*mimeMap)["axs"]="application/olescript";
        (*mimeMap)["bas"]="text/plain";
        (*mimeMap)["bcpio"]="application/x-bcpio";
        (*mimeMap)["bin"]="application/octet-stream";
        (*mimeMap)["bmp"]="image/bmp";
        (*mimeMap)["c"]="text/plain";
        (*mimeMap)["cab"]="application/octet-stream";
        (*mimeMap)["caf"]="audio/x-caf";
        (*mimeMap)["calx"]="application/vnd.ms-office.calx";
        (*mimeMap)["cat"]="application/vnd.ms-pki.seccat";
        (*mimeMap)["cc"]="text/plain";
        (*mimeMap)["cd"]="text/plain";
        (*mimeMap)["cdda"]="audio/aiff";
        (*mimeMap)["cdf"]="application/x-cdf";
        (*mimeMap)["cer"]="application/x-x509-ca-cert";
        (*mimeMap)["chm"]="application/octet-stream";
        (*mimeMap)["class"]="application/x-java-applet";
        (*mimeMap)["clp"]="application/x-msclip";
        (*mimeMap)["cmx"]="image/x-cmx";
        (*mimeMap)["cnf"]="text/plain";
        (*mimeMap)["cod"]="image/cis-cod";
        (*mimeMap)["config"]="application/xml";
        (*mimeMap)["contact"]="text/x-ms-contact";
        (*mimeMap)["coverage"]="application/xml";
        (*mimeMap)["cpio"]="application/x-cpio";
        (*mimeMap)["cpp"]="text/plain";
        (*mimeMap)["crd"]="application/x-mscardfile";
        (*mimeMap)["crl"]="application/pkix-crl";
        (*mimeMap)["crt"]="application/x-x509-ca-cert";
        (*mimeMap)["cs"]="text/plain";
        (*mimeMap)["csdproj"]="text/plain";
        (*mimeMap)["csh"]="application/x-csh";
        (*mimeMap)["csproj"]="text/plain";
        (*mimeMap)["css"]="text/css";
        (*mimeMap)["csv"]="text/csv";
        (*mimeMap)["cur"]="application/octet-stream";
        (*mimeMap)["cxx"]="text/plain";
        (*mimeMap)["dat"]="application/octet-stream";
        (*mimeMap)["datasource"]="application/xml";
        (*mimeMap)["dbproj"]="text/plain";
        (*mimeMap)["dcr"]="application/x-director";
        (*mimeMap)["def"]="text/plain";
        (*mimeMap)["deploy"]="application/octet-stream";
        (*mimeMap)["der"]="application/x-x509-ca-cert";
        (*mimeMap)["dgml"]="application/xml";
        (*mimeMap)["dib"]="image/bmp";
        (*mimeMap)["dif"]="video/x-dv";
        (*mimeMap)["dir"]="application/x-director";
        (*mimeMap)["disco"]="text/xml";
        (*mimeMap)["dll"]="application/x-msdownload";
        (*mimeMap)["dll.config"]="text/xml";
        (*mimeMap)["dlm"]="text/dlm";
        (*mimeMap)["doc"]="application/msword";
        (*mimeMap)["docm"]="application/vnd.ms-word.document.macroEnabled.12";
        (*mimeMap)["docx"]="application/vnd.openxmlformats-officedocument.wordprocessingml.document";
        (*mimeMap)["dot"]="application/msword";
        (*mimeMap)["dotm"]="application/vnd.ms-word.template.macroEnabled.12";
        (*mimeMap)["dotx"]="application/vnd.openxmlformats-officedocument.wordprocessingml.template";
        (*mimeMap)["dsp"]="application/octet-stream";
        (*mimeMap)["dsw"]="text/plain";
        (*mimeMap)["dtd"]="text/xml";
        (*mimeMap)["dtsConfig"]="text/xml";
        (*mimeMap)["dv"]="video/x-dv";
        (*mimeMap)["dvi"]="application/x-dvi";
        (*mimeMap)["dwf"]="drawing/x-dwf";
        (*mimeMap)["dwp"]="application/octet-stream";
        (*mimeMap)["dxr"]="application/x-director";
        (*mimeMap)["eml"]="message/rfc822";
        (*mimeMap)["emz"]="application/octet-stream";
        (*mimeMap)["eot"]="application/octet-stream";
        (*mimeMap)["eps"]="application/postscript";
        (*mimeMap)["etl"]="application/etl";
        (*mimeMap)["etx"]="text/x-setext";
        (*mimeMap)["evy"]="application/envoy";
        (*mimeMap)["exe"]="application/octet-stream";
        (*mimeMap)["exe.config"]="text/xml";
        (*mimeMap)["fdf"]="application/vnd.fdf";
        (*mimeMap)["fif"]="application/fractals";
        (*mimeMap)["filters"]="Application/xml";
        (*mimeMap)["fla"]="application/octet-stream";
        (*mimeMap)["flr"]="x-world/x-vrml";
        (*mimeMap)["flv"]="video/x-flv";
        (*mimeMap)["fsscript"]="application/fsharp-script";
        (*mimeMap)["fsx"]="application/fsharp-script";
        (*mimeMap)["generictest"]="application/xml";
        (*mimeMap)["gif"]="image/gif";
        (*mimeMap)["group"]="text/x-ms-group";
        (*mimeMap)["gsm"]="audio/x-gsm";
        (*mimeMap)["gtar"]="application/x-gtar";
        (*mimeMap)["gz"]="application/x-gzip";
        (*mimeMap)["h"]="text/plain";
        (*mimeMap)["hdf"]="application/x-hdf";
        (*mimeMap)["hdml"]="text/x-hdml";
        (*mimeMap)["hhc"]="application/x-oleobject";
        (*mimeMap)["hhk"]="application/octet-stream";
        (*mimeMap)["hhp"]="application/octet-stream";
        (*mimeMap)["hlp"]="application/winhlp";
        (*mimeMap)["hpp"]="text/plain";
        (*mimeMap)["hqx"]="application/mac-binhex40";
        (*mimeMap)["hta"]="application/hta";
        (*mimeMap)["htc"]="text/x-component";
        (*mimeMap)["htm"]="text/html";
        (*mimeMap)["html"]="text/html";
        (*mimeMap)["htt"]="text/webviewhtml";
        (*mimeMap)["hxa"]="application/xml";
        (*mimeMap)["hxc"]="application/xml";
        (*mimeMap)["hxd"]="application/octet-stream";
        (*mimeMap)["hxe"]="application/xml";
        (*mimeMap)["hxf"]="application/xml";
        (*mimeMap)["hxh"]="application/octet-stream";
        (*mimeMap)["hxi"]="application/octet-stream";
        (*mimeMap)["hxk"]="application/xml";
        (*mimeMap)["hxq"]="application/octet-stream";
        (*mimeMap)["hxr"]="application/octet-stream";
        (*mimeMap)["hxs"]="application/octet-stream";
        (*mimeMap)["hxt"]="text/html";
        (*mimeMap)["hxv"]="application/xml";
        (*mimeMap)["hxw"]="application/octet-stream";
        (*mimeMap)["hxx"]="text/plain";
        (*mimeMap)["i"]="text/plain";
        (*mimeMap)["ico"]="image/x-icon";
        (*mimeMap)["ics"]="application/octet-stream";
        (*mimeMap)["idl"]="text/plain";
        (*mimeMap)["ief"]="image/ief";
        (*mimeMap)["iii"]="application/x-iphone";
        (*mimeMap)["inc"]="text/plain";
        (*mimeMap)["inf"]="application/octet-stream";
        (*mimeMap)["inl"]="text/plain";
        (*mimeMap)["ins"]="application/x-internet-signup";
        (*mimeMap)["ipa"]="application/x-itunes-ipa";
        (*mimeMap)["ipg"]="application/x-itunes-ipg";
        (*mimeMap)["ipproj"]="text/plain";
        (*mimeMap)["ipsw"]="application/x-itunes-ipsw";
        (*mimeMap)["iqy"]="text/x-ms-iqy";
        (*mimeMap)["isp"]="application/x-internet-signup";
        (*mimeMap)["ite"]="application/x-itunes-ite";
        (*mimeMap)["itlp"]="application/x-itunes-itlp";
        (*mimeMap)["itms"]="application/x-itunes-itms";
        (*mimeMap)["itpc"]="application/x-itunes-itpc";
        (*mimeMap)["IVF"]="video/x-ivf";
        (*mimeMap)["jar"]="application/java-archive";
        (*mimeMap)["java"]="application/octet-stream";
        (*mimeMap)["jck"]="application/liquidmotion";
        (*mimeMap)["jcz"]="application/liquidmotion";
        (*mimeMap)["jfif"]="image/pjpeg";
        (*mimeMap)["jnlp"]="application/x-java-jnlp-file";
        (*mimeMap)["jpb"]="application/octet-stream";
        (*mimeMap)["jpe"]="image/jpeg";
        (*mimeMap)["jpeg"]="image/jpeg";
        (*mimeMap)["jpg"]="image/jpeg";
        (*mimeMap)["js"]="application/x-javascript";
        (*mimeMap)["json"]="application/json";
        (*mimeMap)["jsx"]="text/jscript";
        (*mimeMap)["jsxbin"]="text/plain";
        (*mimeMap)["latex"]="application/x-latex";
        (*mimeMap)["library-ms"]="application/windows-library+xml";
        (*mimeMap)["lit"]="application/x-ms-reader";
        (*mimeMap)["loadtest"]="application/xml";
        (*mimeMap)["lpk"]="application/octet-stream";
        (*mimeMap)["lsf"]="video/x-la-asf";
        (*mimeMap)["lst"]="text/plain";
        (*mimeMap)["lsx"]="video/x-la-asf";
        (*mimeMap)["lzh"]="application/octet-stream";
        (*mimeMap)["m13"]="application/x-msmediaview";
        (*mimeMap)["m14"]="application/x-msmediaview";
        (*mimeMap)["m1v"]="video/mpeg";
        (*mimeMap)["m2t"]="video/vnd.dlna.mpeg-tts";
        (*mimeMap)["m2ts"]="video/vnd.dlna.mpeg-tts";
        (*mimeMap)["m2v"]="video/mpeg";
        (*mimeMap)["m3u"]="audio/x-mpegurl";
        (*mimeMap)["m3u8"]="audio/x-mpegurl";
        (*mimeMap)["m4a"]="audio/m4a";
        (*mimeMap)["m4b"]="audio/m4b";
        (*mimeMap)["m4p"]="audio/m4p";
        (*mimeMap)["m4r"]="audio/x-m4r";
        (*mimeMap)["m4v"]="video/x-m4v";
        (*mimeMap)["mac"]="image/x-macpaint";
        (*mimeMap)["mak"]="text/plain";
        (*mimeMap)["man"]="application/x-troff-man";
        (*mimeMap)["manifest"]="application/x-ms-manifest";
        (*mimeMap)["map"]="text/plain";
        (*mimeMap)["master"]="application/xml";
        (*mimeMap)["mda"]="application/msaccess";
        (*mimeMap)["mdb"]="application/x-msaccess";
        (*mimeMap)["mde"]="application/msaccess";
        (*mimeMap)["mdp"]="application/octet-stream";
        (*mimeMap)["me"]="application/x-troff-me";
        (*mimeMap)["mfp"]="application/x-shockwave-flash";
        (*mimeMap)["mht"]="message/rfc822";
        (*mimeMap)["mhtml"]="message/rfc822";
        (*mimeMap)["mid"]="audio/mid";
        (*mimeMap)["midi"]="audio/mid";
        (*mimeMap)["mix"]="application/octet-stream";
        (*mimeMap)["mk"]="text/plain";
        (*mimeMap)["mmf"]="application/x-smaf";
        (*mimeMap)["mno"]="text/xml";
        (*mimeMap)["mny"]="application/x-msmoney";
        (*mimeMap)["mod"]="video/mpeg";
        (*mimeMap)["mov"]="video/quicktime";
        (*mimeMap)["movie"]="video/x-sgi-movie";
        (*mimeMap)["mp2"]="video/mpeg";
        (*mimeMap)["mp2v"]="video/mpeg";
        (*mimeMap)["mp3"]="audio/mpeg";
        (*mimeMap)["mp4"]="video/mp4";
        (*mimeMap)["mp4v"]="video/mp4";
        (*mimeMap)["mpa"]="video/mpeg";
        (*mimeMap)["mpe"]="video/mpeg";
        (*mimeMap)["mpeg"]="video/mpeg";
        (*mimeMap)["mpf"]="application/vnd.ms-mediapackage";
        (*mimeMap)["mpg"]="video/mpeg";
        (*mimeMap)["mpp"]="application/vnd.ms-project";
        (*mimeMap)["mpv2"]="video/mpeg";
        (*mimeMap)["mqv"]="video/quicktime";
        (*mimeMap)["ms"]="application/x-troff-ms";
        (*mimeMap)["msi"]="application/octet-stream";
        (*mimeMap)["mso"]="application/octet-stream";
        (*mimeMap)["mts"]="video/vnd.dlna.mpeg-tts";
        (*mimeMap)["mtx"]="application/xml";
        (*mimeMap)["mvb"]="application/x-msmediaview";
        (*mimeMap)["mvc"]="application/x-miva-compiled";
        (*mimeMap)["mxp"]="application/x-mmxp";
        (*mimeMap)["nc"]="application/x-netcdf";
        (*mimeMap)["nsc"]="video/x-ms-asf";
        (*mimeMap)["nws"]="message/rfc822";
        (*mimeMap)["ocx"]="application/octet-stream";
        (*mimeMap)["oda"]="application/oda";
        (*mimeMap)["odc"]="text/x-ms-odc";
        (*mimeMap)["odh"]="text/plain";
        (*mimeMap)["odl"]="text/plain";
        (*mimeMap)["odp"]="application/vnd.oasis.opendocument.presentation";
        (*mimeMap)["ods"]="application/oleobject";
        (*mimeMap)["odt"]="application/vnd.oasis.opendocument.text";
        (*mimeMap)["one"]="application/onenote";
        (*mimeMap)["onea"]="application/onenote";
        (*mimeMap)["onepkg"]="application/onenote";
        (*mimeMap)["onetmp"]="application/onenote";
        (*mimeMap)["onetoc"]="application/onenote";
        (*mimeMap)["onetoc2"]="application/onenote";
        (*mimeMap)["orderedtest"]="application/xml";
        (*mimeMap)["osdx"]="application/opensearchdescription+xml";
        (*mimeMap)["p10"]="application/pkcs10";
        (*mimeMap)["p12"]="application/x-pkcs12";
        (*mimeMap)["p7b"]="application/x-pkcs7-certificates";
        (*mimeMap)["p7c"]="application/pkcs7-mime";
        (*mimeMap)["p7m"]="application/pkcs7-mime";
        (*mimeMap)["p7r"]="application/x-pkcs7-certreqresp";
        (*mimeMap)["p7s"]="application/pkcs7-signature";
        (*mimeMap)["pbm"]="image/x-portable-bitmap";
        (*mimeMap)["pcast"]="application/x-podcast";
        (*mimeMap)["pct"]="image/pict";
        (*mimeMap)["pcx"]="application/octet-stream";
        (*mimeMap)["pcz"]="application/octet-stream";
        (*mimeMap)["pdf"]="application/pdf";
        (*mimeMap)["pfb"]="application/octet-stream";
        (*mimeMap)["pfm"]="application/octet-stream";
        (*mimeMap)["pfx"]="application/x-pkcs12";
        (*mimeMap)["pgm"]="image/x-portable-graymap";
        (*mimeMap)["pic"]="image/pict";
        (*mimeMap)["pict"]="image/pict";
        (*mimeMap)["pkgdef"]="text/plain";
        (*mimeMap)["pkgundef"]="text/plain";
        (*mimeMap)["pko"]="application/vnd.ms-pki.pko";
        (*mimeMap)["pls"]="audio/scpls";
        (*mimeMap)["pma"]="application/x-perfmon";
        (*mimeMap)["pmc"]="application/x-perfmon";
        (*mimeMap)["pml"]="application/x-perfmon";
        (*mimeMap)["pmr"]="application/x-perfmon";
        (*mimeMap)["pmw"]="application/x-perfmon";
        (*mimeMap)["png"]="image/png";
        (*mimeMap)["pnm"]="image/x-portable-anymap";
        (*mimeMap)["pnt"]="image/x-macpaint";
        (*mimeMap)["pntg"]="image/x-macpaint";
        (*mimeMap)["pnz"]="image/png";
        (*mimeMap)["pot"]="application/vnd.ms-powerpoint";
        (*mimeMap)["potm"]="application/vnd.ms-powerpoint.template.macroEnabled.12";
        (*mimeMap)["potx"]="application/vnd.openxmlformats-officedocument.presentationml.template";
        (*mimeMap)["ppa"]="application/vnd.ms-powerpoint";
        (*mimeMap)["ppam"]="application/vnd.ms-powerpoint.addin.macroEnabled.12";
        (*mimeMap)["ppm"]="image/x-portable-pixmap";
        (*mimeMap)["pps"]="application/vnd.ms-powerpoint";
        (*mimeMap)["ppsm"]="application/vnd.ms-powerpoint.slideshow.macroEnabled.12";
        (*mimeMap)["ppsx"]="application/vnd.openxmlformats-officedocument.presentationml.slideshow";
        (*mimeMap)["ppt"]="application/vnd.ms-powerpoint";
        (*mimeMap)["pptm"]="application/vnd.ms-powerpoint.presentation.macroEnabled.12";
        (*mimeMap)["pptx"]="application/vnd.openxmlformats-officedocument.presentationml.presentation";
        (*mimeMap)["prf"]="application/pics-rules";
        (*mimeMap)["prm"]="application/octet-stream";
        (*mimeMap)["prx"]="application/octet-stream";
        (*mimeMap)["ps"]="application/postscript";
        (*mimeMap)["psc1"]="application/PowerShell";
        (*mimeMap)["psd"]="application/octet-stream";
        (*mimeMap)["psess"]="application/xml";
        (*mimeMap)["psm"]="application/octet-stream";
        (*mimeMap)["psp"]="application/octet-stream";
        (*mimeMap)["pub"]="application/x-mspublisher";
        (*mimeMap)["pwz"]="application/vnd.ms-powerpoint";
        (*mimeMap)["qht"]="text/x-html-insertion";
        (*mimeMap)["qhtm"]="text/x-html-insertion";
        (*mimeMap)["qt"]="video/quicktime";
        (*mimeMap)["qti"]="image/x-quicktime";
        (*mimeMap)["qtif"]="image/x-quicktime";
        (*mimeMap)["qtl"]="application/x-quicktimeplayer";
        (*mimeMap)["qxd"]="application/octet-stream";
        (*mimeMap)["ra"]="audio/x-pn-realaudio";
        (*mimeMap)["ram"]="audio/x-pn-realaudio";
        (*mimeMap)["rar"]="application/octet-stream";
        (*mimeMap)["ras"]="image/x-cmu-raster";
        (*mimeMap)["rat"]="application/rat-file";
        (*mimeMap)["rc"]="text/plain";
        (*mimeMap)["rc2"]="text/plain";
        (*mimeMap)["rct"]="text/plain";
        (*mimeMap)["rdlc"]="application/xml";
        (*mimeMap)["resx"]="application/xml";
        (*mimeMap)["rf"]="image/vnd.rn-realflash";
        (*mimeMap)["rgb"]="image/x-rgb";
        (*mimeMap)["rgs"]="text/plain";
        (*mimeMap)["rm"]="application/vnd.rn-realmedia";
        (*mimeMap)["rmi"]="audio/mid";
        (*mimeMap)["rmp"]="application/vnd.rn-rn_music_package";
        (*mimeMap)["roff"]="application/x-troff";
        (*mimeMap)["rpm"]="audio/x-pn-realaudio-plugin";
        (*mimeMap)["rqy"]="text/x-ms-rqy";
        (*mimeMap)["rtf"]="application/rtf";
        (*mimeMap)["rtx"]="text/richtext";
        (*mimeMap)["ruleset"]="application/xml";
        (*mimeMap)["s"]="text/plain";
        (*mimeMap)["safariextz"]="application/x-safari-safariextz";
        (*mimeMap)["scd"]="application/x-msschedule";
        (*mimeMap)["sct"]="text/scriptlet";
        (*mimeMap)["sd2"]="audio/x-sd2";
        (*mimeMap)["sdp"]="application/sdp";
        (*mimeMap)["sea"]="application/octet-stream";
        (*mimeMap)["searchConnector-ms"]="application/windows-search-connector+xml";
        (*mimeMap)["setpay"]="application/set-payment-initiation";
        (*mimeMap)["setreg"]="application/set-registration-initiation";
        (*mimeMap)["settings"]="application/xml";
        (*mimeMap)["sgimb"]="application/x-sgimb";
        (*mimeMap)["sgml"]="text/sgml";
        (*mimeMap)["sh"]="application/x-sh";
        (*mimeMap)["shar"]="application/x-shar";
        (*mimeMap)["shtml"]="text/html";
        (*mimeMap)["sit"]="application/x-stuffit";
        (*mimeMap)["sitemap"]="application/xml";
        (*mimeMap)["skin"]="application/xml";
        (*mimeMap)["sldm"]="application/vnd.ms-powerpoint.slide.macroEnabled.12";
        (*mimeMap)["sldx"]="application/vnd.openxmlformats-officedocument.presentationml.slide";
        (*mimeMap)["slk"]="application/vnd.ms-excel";
        (*mimeMap)["sln"]="text/plain";
        (*mimeMap)["slupkg-ms"]="application/x-ms-license";
        (*mimeMap)["smd"]="audio/x-smd";
        (*mimeMap)["smi"]="application/octet-stream";
        (*mimeMap)["smx"]="audio/x-smd";
        (*mimeMap)["smz"]="audio/x-smd";
        (*mimeMap)["snd"]="audio/basic";
        (*mimeMap)["snippet"]="application/xml";
        (*mimeMap)["snp"]="application/octet-stream";
        (*mimeMap)["sol"]="text/plain";
        (*mimeMap)["sor"]="text/plain";
        (*mimeMap)["spc"]="application/x-pkcs7-certificates";
        (*mimeMap)["spl"]="application/futuresplash";
        (*mimeMap)["src"]="application/x-wais-source";
        (*mimeMap)["srf"]="text/plain";
        (*mimeMap)["SSISDeploymentManifest"]="text/xml";
        (*mimeMap)["ssm"]="application/streamingmedia";
        (*mimeMap)["sst"]="application/vnd.ms-pki.certstore";
        (*mimeMap)["stl"]="application/vnd.ms-pki.stl";
        (*mimeMap)["sv4cpio"]="application/x-sv4cpio";
        (*mimeMap)["sv4crc"]="application/x-sv4crc";
        (*mimeMap)["svc"]="application/xml";
        (*mimeMap)["swf"]="application/x-shockwave-flash";
        (*mimeMap)["t"]="application/x-troff";
        (*mimeMap)["tar"]="application/x-tar";
        (*mimeMap)["tcl"]="application/x-tcl";
        (*mimeMap)["testrunconfig"]="application/xml";
        (*mimeMap)["testsettings"]="application/xml";
        (*mimeMap)["tex"]="application/x-tex";
        (*mimeMap)["texi"]="application/x-texinfo";
        (*mimeMap)["texinfo"]="application/x-texinfo";
        (*mimeMap)["tgz"]="application/x-compressed";
        (*mimeMap)["thmx"]="application/vnd.ms-officetheme";
        (*mimeMap)["thn"]="application/octet-stream";
        (*mimeMap)["tif"]="image/tiff";
        (*mimeMap)["tiff"]="image/tiff";
        (*mimeMap)["tlh"]="text/plain";
        (*mimeMap)["tli"]="text/plain";
        (*mimeMap)["toc"]="application/octet-stream";
        (*mimeMap)["tr"]="application/x-troff";
        (*mimeMap)["trm"]="application/x-msterminal";
        (*mimeMap)["trx"]="application/xml";
        (*mimeMap)["ts"]="video/vnd.dlna.mpeg-tts";
        (*mimeMap)["tsv"]="text/tab-separated-values";
        (*mimeMap)["ttf"]="application/octet-stream";
        (*mimeMap)["tts"]="video/vnd.dlna.mpeg-tts";
        (*mimeMap)["txt"]="text/plain";
        (*mimeMap)["u32"]="application/octet-stream";
        (*mimeMap)["uls"]="text/iuls";
        (*mimeMap)["user"]="text/plain";
        (*mimeMap)["ustar"]="application/x-ustar";
        (*mimeMap)["vb"]="text/plain";
        (*mimeMap)["vbdproj"]="text/plain";
        (*mimeMap)["vbk"]="video/mpeg";
        (*mimeMap)["vbproj"]="text/plain";
        (*mimeMap)["vbs"]="text/vbscript";
        (*mimeMap)["vcf"]="text/x-vcard";
        (*mimeMap)["vcproj"]="Application/xml";
        (*mimeMap)["vcs"]="text/plain";
        (*mimeMap)["vcxproj"]="Application/xml";
        (*mimeMap)["vddproj"]="text/plain";
        (*mimeMap)["vdp"]="text/plain";
        (*mimeMap)["vdproj"]="text/plain";
        (*mimeMap)["vdx"]="application/vnd.ms-visio.viewer";
        (*mimeMap)["vml"]="text/xml";
        (*mimeMap)["vscontent"]="application/xml";
        (*mimeMap)["vsct"]="text/xml";
        (*mimeMap)["vsd"]="application/vnd.visio";
        (*mimeMap)["vsi"]="application/ms-vsi";
        (*mimeMap)["vsix"]="application/vsix";
        (*mimeMap)["vsixlangpack"]="text/xml";
        (*mimeMap)["vsixmanifest"]="text/xml";
        (*mimeMap)["vsmdi"]="application/xml";
        (*mimeMap)["vspscc"]="text/plain";
        (*mimeMap)["vss"]="application/vnd.visio";
        (*mimeMap)["vsscc"]="text/plain";
        (*mimeMap)["vssettings"]="text/xml";
        (*mimeMap)["vssscc"]="text/plain";
        (*mimeMap)["vst"]="application/vnd.visio";
        (*mimeMap)["vstemplate"]="text/xml";
        (*mimeMap)["vsto"]="application/x-ms-vsto";
        (*mimeMap)["vsw"]="application/vnd.visio";
        (*mimeMap)["vsx"]="application/vnd.visio";
        (*mimeMap)["vtx"]="application/vnd.visio";
        (*mimeMap)["wav"]="audio/wav";
        (*mimeMap)["wave"]="audio/wav";
        (*mimeMap)["wax"]="audio/x-ms-wax";
        (*mimeMap)["wbk"]="application/msword";
        (*mimeMap)["wbmp"]="image/vnd.wap.wbmp";
        (*mimeMap)["wcm"]="application/vnd.ms-works";
        (*mimeMap)["wdb"]="application/vnd.ms-works";
        (*mimeMap)["wdp"]="image/vnd.ms-photo";
        (*mimeMap)["webarchive"]="application/x-safari-webarchive";
        (*mimeMap)["webtest"]="application/xml";
        (*mimeMap)["wiq"]="application/xml";
        (*mimeMap)["wiz"]="application/msword";
        (*mimeMap)["wks"]="application/vnd.ms-works";
        (*mimeMap)["WLMP"]="application/wlmoviemaker";
        (*mimeMap)["wlpginstall"]="application/x-wlpg-detect";
        (*mimeMap)["wlpginstall3"]="application/x-wlpg3-detect";
        (*mimeMap)["wm"]="video/x-ms-wm";
        (*mimeMap)["wma"]="audio/x-ms-wma";
        (*mimeMap)["wmd"]="application/x-ms-wmd";
        (*mimeMap)["wmf"]="application/x-msmetafile";
        (*mimeMap)["wml"]="text/vnd.wap.wml";
        (*mimeMap)["wmlc"]="application/vnd.wap.wmlc";
        (*mimeMap)["wmls"]="text/vnd.wap.wmlscript";
        (*mimeMap)["wmlsc"]="application/vnd.wap.wmlscriptc";
        (*mimeMap)["wmp"]="video/x-ms-wmp";
        (*mimeMap)["wmv"]="video/x-ms-wmv";
        (*mimeMap)["wmx"]="video/x-ms-wmx";
        (*mimeMap)["wmz"]="application/x-ms-wmz";
        (*mimeMap)["wpl"]="application/vnd.ms-wpl";
        (*mimeMap)["wps"]="application/vnd.ms-works";
        (*mimeMap)["wri"]="application/x-mswrite";
        (*mimeMap)["wrl"]="x-world/x-vrml";
        (*mimeMap)["wrz"]="x-world/x-vrml";
        (*mimeMap)["wsc"]="text/scriptlet";
        (*mimeMap)["wsdl"]="text/xml";
        (*mimeMap)["wvx"]="video/x-ms-wvx";
        (*mimeMap)["x"]="application/directx";
        (*mimeMap)["xaf"]="x-world/x-vrml";
        (*mimeMap)["xaml"]="application/xaml+xml";
        (*mimeMap)["xap"]="application/x-silverlight-app";
        (*mimeMap)["xbap"]="application/x-ms-xbap";
        (*mimeMap)["xbm"]="image/x-xbitmap";
        (*mimeMap)["xdr"]="text/plain";
        (*mimeMap)["xht"]="application/xhtml+xml";
        (*mimeMap)["xhtml"]="application/xhtml+xml";
        (*mimeMap)["xla"]="application/vnd.ms-excel";
        (*mimeMap)["xlam"]="application/vnd.ms-excel.addin.macroEnabled.12";
        (*mimeMap)["xlc"]="application/vnd.ms-excel";
        (*mimeMap)["xld"]="application/vnd.ms-excel";
        (*mimeMap)["xlk"]="application/vnd.ms-excel";
        (*mimeMap)["xll"]="application/vnd.ms-excel";
        (*mimeMap)["xlm"]="application/vnd.ms-excel";
        (*mimeMap)["xls"]="application/vnd.ms-excel";
        (*mimeMap)["xlsb"]="application/vnd.ms-excel.sheet.binary.macroEnabled.12";
        (*mimeMap)["xlsm"]="application/vnd.ms-excel.sheet.macroEnabled.12";
        (*mimeMap)["xlsx"]="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
        (*mimeMap)["xlt"]="application/vnd.ms-excel";
        (*mimeMap)["xltm"]="application/vnd.ms-excel.template.macroEnabled.12";
        (*mimeMap)["xltx"]="application/vnd.openxmlformats-officedocument.spreadsheetml.template";
        (*mimeMap)["xlw"]="application/vnd.ms-excel";
        (*mimeMap)["xml"]="text/xml";
        (*mimeMap)["xmta"]="application/xml";
        (*mimeMap)["xof"]="x-world/x-vrml";
        (*mimeMap)["XOML"]="text/plain";
        (*mimeMap)["xpm"]="image/x-xpixmap";
        (*mimeMap)["xps"]="application/vnd.ms-xpsdocument";
        (*mimeMap)["xrm-ms"]="text/xml";
        (*mimeMap)["xsc"]="application/xml";
        (*mimeMap)["xsd"]="text/xml";
        (*mimeMap)["xsf"]="text/xml";
        (*mimeMap)["xsl"]="text/xml";
        (*mimeMap)["xslt"]="text/xml";
        (*mimeMap)["xsn"]="application/octet-stream";
        (*mimeMap)["xss"]="application/xml";
        (*mimeMap)["xtp"]="application/octet-stream";
        (*mimeMap)["xwd"]="image/x-xwindowdump";
        (*mimeMap)["z"]="application/x-compress";
        (*mimeMap)["zip"]="application/x-zip-compressed";
    }

    string key = extension;
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
    map<string, string>::iterator it = mimeMap->find(key);
    if (it == mimeMap->end())
    {
        return NULL;
    }

    return MegaApi::strdup(it->second.c_str());
}
#endif

#ifdef ENABLE_CHAT
void MegaApi::createChat(bool group, MegaTextChatPeerList *peers, MegaRequestListener *listener)
{
    pImpl->createChat(group, peers, listener);
}

void MegaApi::inviteToChat(MegaHandle chatid,  MegaHandle uh, int privilege, const char *title, MegaRequestListener *listener)
{
    pImpl->inviteToChat(chatid, uh, privilege, title, listener);
}

void MegaApi::removeFromChat(MegaHandle chatid, MegaHandle uh, MegaRequestListener *listener)
{
    pImpl->removeFromChat(chatid, uh, listener);
}

void MegaApi::getUrlChat(MegaHandle chatid, MegaRequestListener *listener)
{
    pImpl->getUrlChat(chatid, listener);
}

void MegaApi::grantAccessInChat(MegaHandle chatid, MegaNode *n, MegaHandle uh,  MegaRequestListener *listener)
{
    pImpl->grantAccessInChat(chatid, n, uh, listener);
}

void MegaApi::removeAccessInChat(MegaHandle chatid, MegaNode *n, MegaHandle uh,  MegaRequestListener *listener)
{
    pImpl->removeAccessInChat(chatid, n, uh, listener);
}

void MegaApi::updateChatPermissions(MegaHandle chatid, MegaHandle uh, int privilege, MegaRequestListener *listener)
{
    pImpl->updateChatPermissions(chatid, uh, privilege, listener);
}

void MegaApi::truncateChat(MegaHandle chatid, MegaHandle messageid, MegaRequestListener *listener)
{
    pImpl->truncateChat(chatid, messageid, listener);
}

void MegaApi::setChatTitle(MegaHandle chatid, const char* title, MegaRequestListener *listener)
{
    pImpl->setChatTitle(chatid, title, listener);
}

void MegaApi::getChatPresenceURL(MegaRequestListener *listener)
{
    pImpl->getChatPresenceURL(listener);
}

void MegaApi::registerPushNotifications(int deviceType, const char *token, MegaRequestListener *listener)
{
    pImpl->registerPushNotification(deviceType, token, listener);
}

void MegaApi::sendChatStats(const char *data, MegaRequestListener *listener)
{
    pImpl->sendChatStats(data, listener);
}

void MegaApi::sendChatLogs(const char *data, const char *aid, MegaRequestListener *listener)
{
    pImpl->sendChatLogs(data, aid, listener);
}

MegaTextChatList* MegaApi::getChatList()
{
    return pImpl->getChatList();
}

MegaHandleList* MegaApi::getAttachmentAccess(MegaHandle chatid, MegaHandle h)
{
    return pImpl->getAttachmentAccess(chatid, h);
}

bool MegaApi::hasAccessToAttachment(MegaHandle chatid, MegaHandle h, MegaHandle uh)
{
    return pImpl->hasAccessToAttachment(chatid, h, uh);
}

const char* MegaApi::getFileAttribute(MegaHandle h)
{
    return pImpl->getFileAttribute(h);
}

#endif

char* MegaApi::strdup(const char* buffer)
{
    if(!buffer)
        return NULL;
    int tam = strlen(buffer)+1;
    char *newbuffer = new char[tam];
    memcpy(newbuffer, buffer, tam);
    return newbuffer;
}

#ifdef _WIN32

// convert Windows Unicode to UTF-8
void MegaApi::utf16ToUtf8(const wchar_t* utf16data, int utf16size, string* utf8string)
{
    if(!utf16size)
    {
        utf8string->clear();
        return;
    }

    utf8string->resize((utf16size + 1) * 4);

    utf8string->resize(WideCharToMultiByte(CP_UTF8, 0, utf16data,
        utf16size,
        (char*)utf8string->data(),
        utf8string->size() + 1,
        NULL, NULL));
}

void MegaApi::utf8ToUtf16(const char* utf8data, string* utf16string)
{
    if(!utf8data)
    {
        utf16string->clear();
        utf16string->append("", 1);
        return;
    }

    int size = strlen(utf8data) + 1;

    // make space for the worst case
    utf16string->resize(size * sizeof(wchar_t));

    // resize to actual result
    utf16string->resize(sizeof(wchar_t) * MultiByteToWideChar(CP_UTF8, 0, utf8data, size, (wchar_t*)utf16string->data(),
                                                              utf16string->size() / sizeof(wchar_t) + 1));
    if (utf16string->size())
    {
        utf16string->resize(utf16string->size() - 1);
    }
    else
    {
        utf16string->append("", 1);
    }
}

#endif

char *MegaApi::escapeFsIncompatible(const char *filename)
{
    return pImpl->escapeFsIncompatible(filename);
}

char *MegaApi::unescapeFsIncompatible(const char *name)
{
    return pImpl->unescapeFsIncompatible(name);
}

bool MegaApi::createThumbnail(const char *imagePath, const char *dstPath)
{
    return pImpl->createThumbnail(imagePath, dstPath);
}

bool MegaApi::createPreview(const char *imagePath, const char *dstPath)
{
    return pImpl->createPreview(imagePath, dstPath);
}

bool MegaApi::createAvatar(const char *imagePath, const char *dstPath)
{
    return pImpl->createAvatar(imagePath, dstPath);
}

MegaHashSignature::MegaHashSignature(const char *base64Key)
{
    pImpl = new MegaHashSignatureImpl(base64Key);
}

MegaHashSignature::~MegaHashSignature()
{
    delete pImpl;
}

void MegaHashSignature::init()
{
	pImpl->init();
}

void MegaHashSignature::add(const char *data, unsigned size)
{
	pImpl->add(data, size);
}

bool MegaHashSignature::checkSignature(const char *base64Signature)
{
    return pImpl->checkSignature(base64Signature);
}

MegaAccountDetails::~MegaAccountDetails() { }

int MegaAccountDetails::getProLevel()
{
    return 0;
}

int64_t MegaAccountDetails::getProExpiration()
{
    return 0;
}

int MegaAccountDetails::getSubscriptionStatus()
{
    return 0;
}

int64_t MegaAccountDetails::getSubscriptionRenewTime()
{
    return 0;
}

char *MegaAccountDetails::getSubscriptionMethod()
{
    return NULL;
}

char *MegaAccountDetails::getSubscriptionCycle()
{
    return NULL;
}

long long MegaAccountDetails::getStorageMax()
{
    return 0;
}

long long MegaAccountDetails::getStorageUsed()
{
    return 0;
}

long long MegaAccountDetails::getVersionStorageUsed()
{
    return 0;
}

long long MegaAccountDetails::getTransferMax()
{
    return 0;
}

long long MegaAccountDetails::getTransferOwnUsed()
{
    return 0;
}

int MegaAccountDetails::getNumUsageItems()
{
    return 0;
}

long long MegaAccountDetails::getStorageUsed(MegaHandle)
{
    return 0;
}

long long MegaAccountDetails::getNumFiles(MegaHandle)
{
    return 0;
}

long long MegaAccountDetails::getNumFolders(MegaHandle)
{
    return 0;
}

long long MegaAccountDetails::getNumVersionFiles(MegaHandle)
{
    return 0;
}

long long MegaAccountDetails::getVersionStorageUsed(MegaHandle)
{
    return 0;
}

MegaAccountDetails *MegaAccountDetails::copy()
{
    return NULL;
}

int MegaAccountDetails::getNumBalances() const
{
    return 0;
}

MegaAccountBalance *MegaAccountDetails::getBalance(int) const
{
    return NULL;
}

int MegaAccountDetails::getNumSessions() const
{
    return 0;
}

MegaAccountSession *MegaAccountDetails::getSession(int) const
{
    return NULL;
}

int MegaAccountDetails::getNumPurchases() const
{
    return 0;
}

MegaAccountPurchase *MegaAccountDetails::getPurchase(int) const
{
    return NULL;
}

int MegaAccountDetails::getNumTransactions() const
{
    return 0;
}

MegaAccountTransaction *MegaAccountDetails::getTransaction(int) const
{
    return NULL;
}

int MegaAccountDetails::getTemporalBandwidthInterval()
{
    return 0;
}

long long MegaAccountDetails::getTemporalBandwidth()
{
    return 0;
}

bool MegaAccountDetails::isTemporalBandwidthValid()
{
    return false;
}

void MegaLogger::log(const char* /*time*/, int /*loglevel*/, const char* /*source*/, const char* /*message*/)
{

}

bool MegaGfxProcessor::readBitmap(const char* /*path*/)
{
    return false;
}

int MegaGfxProcessor::getWidth()
{
    return 0;
}

int MegaGfxProcessor::getHeight()
{
    return 0;
}

int MegaGfxProcessor::getBitmapDataSize(int /*width*/, int /*height*/, int /*px*/, int /*py*/, int /*rw*/, int /*rh*/)
{
    return 0;
}

bool MegaGfxProcessor::getBitmapData(char* /*bitmapData*/, size_t /*size*/)
{
    return 0;
}

void MegaGfxProcessor::freeBitmap() { }

MegaGfxProcessor::~MegaGfxProcessor() { }
MegaPricing::~MegaPricing() { }

int MegaPricing::getNumProducts()
{
    return 0;
}

MegaHandle MegaPricing::getHandle(int)
{
    return INVALID_HANDLE;
}

int MegaPricing::getProLevel(int)
{
    return 0;
}

unsigned int MegaPricing::getGBStorage(int)
{
    return 0;
}

unsigned int MegaPricing::getGBTransfer(int)
{
    return 0;
}

int MegaPricing::getMonths(int)
{
    return 0;
}

int MegaPricing::getAmount(int)
{
    return 0;
}

const char *MegaPricing::getCurrency(int)
{
    return 0;
}

const char *MegaPricing::getDescription(int)
{
    return NULL;
}

const char *MegaPricing::getIosID(int)
{
    return NULL;
}

const char *MegaPricing::getAndroidID(int)
{
    return NULL;
}

MegaPricing *MegaPricing::copy()
{
    return NULL;
}

#ifdef ENABLE_SYNC
MegaSync::~MegaSync() { }

MegaSync *MegaSync::copy()
{
    return NULL;
}

MegaHandle MegaSync::getMegaHandle() const
{
    return INVALID_HANDLE;
}

const char *MegaSync::getLocalFolder() const
{
    return NULL;
}

long long MegaSync::getLocalFingerprint() const
{
    return 0;
}

int MegaSync::getTag() const
{
    return 0;
}

int MegaSync::getState() const
{
    return MegaSync::SYNC_FAILED;
}


void MegaSyncListener::onSyncFileStateChanged(MegaApi *, MegaSync *, string *, int)
{ }

void MegaSyncListener::onSyncStateChanged(MegaApi *, MegaSync *)
{ }

void MegaSyncListener::onSyncEvent(MegaApi *, MegaSync *, MegaSyncEvent *)
{ }

MegaSyncEvent::~MegaSyncEvent()
{ }

MegaSyncEvent *MegaSyncEvent::copy()
{
    return NULL;
}

int MegaSyncEvent::getType() const
{
    return 0;
}

const char *MegaSyncEvent::getPath() const
{
    return NULL;
}

MegaHandle MegaSyncEvent::getNodeHandle() const
{
    return INVALID_HANDLE;
}

const char *MegaSyncEvent::getNewPath() const
{
    return NULL;
}

const char *MegaSyncEvent::getPrevName() const
{
    return NULL;
}

MegaHandle MegaSyncEvent::getPrevParent() const
{
    return INVALID_HANDLE;
}

MegaRegExp::MegaRegExp()
{
    pImpl = new MegaRegExpPrivate();
}

MegaRegExp::MegaRegExp(MegaRegExpPrivate *pImpl)
{
    this->pImpl = pImpl;
}

MegaRegExp::~MegaRegExp() { }

MegaRegExp *MegaRegExp::copy()
{
    return new MegaRegExp(pImpl->copy());
}

bool MegaRegExp::addRegExp(const char *regExp)
{
    return pImpl->addRegExp(regExp);
}

int MegaRegExp::getNumRegExp()
{
    return pImpl->getNumRegExp();
}

const char *MegaRegExp::getRegExp(int index)
{
    return pImpl->getRegExp(index);
}

bool MegaRegExp::match(const char *s)
{
    return pImpl->match(s);
}

const char *MegaRegExp::getFullPattern()
{
    return pImpl->getFullPattern();
}
#endif

MegaAccountBalance::~MegaAccountBalance()
{

}

double MegaAccountBalance::getAmount() const
{
    return 0;
}

char *MegaAccountBalance::getCurrency() const
{
    return NULL;
}


MegaAccountSession::~MegaAccountSession()
{

}

int64_t MegaAccountSession::getCreationTimestamp() const
{
    return 0;
}

int64_t MegaAccountSession::getMostRecentUsage() const
{
    return 0;
}

char *MegaAccountSession::getUserAgent() const
{
    return NULL;
}

char *MegaAccountSession::getIP() const
{
    return NULL;
}

char *MegaAccountSession::getCountry() const
{
    return NULL;
}

bool MegaAccountSession::isCurrent() const
{
    return false;
}

bool MegaAccountSession::isAlive() const
{
    return false;
}

MegaHandle MegaAccountSession::getHandle() const
{
    return INVALID_HANDLE;
}


MegaAccountPurchase::~MegaAccountPurchase()
{

}

int64_t MegaAccountPurchase::getTimestamp() const
{
    return 0;
}

char *MegaAccountPurchase::getHandle() const
{
    return NULL;
}

char *MegaAccountPurchase::getCurrency() const
{
    return NULL;
}

double MegaAccountPurchase::getAmount() const
{
    return 0;
}

int MegaAccountPurchase::getMethod() const
{
    return 0;
}


MegaAccountTransaction::~MegaAccountTransaction()
{

}

int64_t MegaAccountTransaction::getTimestamp() const
{
    return 0;
}

char *MegaAccountTransaction::getHandle() const
{
    return NULL;
}

char *MegaAccountTransaction::getCurrency() const
{
    return NULL;
}

double MegaAccountTransaction::getAmount() const
{
    return 0;
}

int64_t MegaInputStream::getSize()
{
    return 0;
}

bool MegaInputStream::read(char* /*buffer*/, size_t /*size*/)
{
    return false;
}

MegaInputStream::~MegaInputStream()
{

}

#ifdef ENABLE_CHAT
MegaTextChatPeerList * MegaTextChatPeerList::createInstance()
{
    return new MegaTextChatPeerListPrivate();
}

MegaTextChatPeerList::MegaTextChatPeerList()
{

}

MegaTextChatPeerList::~MegaTextChatPeerList()
{

}

MegaTextChatPeerList *MegaTextChatPeerList::copy() const
{
    return NULL;
}

void MegaTextChatPeerList::addPeer(MegaHandle, int)
{
}

MegaHandle MegaTextChatPeerList::getPeerHandle(int) const
{
    return INVALID_HANDLE;
}

int MegaTextChatPeerList::getPeerPrivilege(int) const
{
    return PRIV_UNKNOWN;
}

int MegaTextChatPeerList::size() const
{
    return 0;
}

MegaTextChat::~MegaTextChat()
{

}

MegaTextChat *MegaTextChat::copy() const
{
    return NULL;
}

MegaHandle MegaTextChat::getHandle() const
{
    return INVALID_HANDLE;
}

int MegaTextChat::getOwnPrivilege() const
{
    return PRIV_UNKNOWN;
}

int MegaTextChat::getShard() const
{
    return -1;
}

const MegaTextChatPeerList *MegaTextChat::getPeerList() const
{
    return NULL;
}

void MegaTextChat::setPeerList(const MegaTextChatPeerList *)
{

}

bool MegaTextChat::isGroup() const
{
    return false;
}

MegaHandle MegaTextChat::getOriginatingUser() const
{
    return INVALID_HANDLE;
}

const char * MegaTextChat::getTitle() const
{
    return NULL;
}

bool MegaTextChat::hasChanged(int) const
{
    return false;
}

int MegaTextChat::getChanges() const
{
    return 0;
}

int MegaTextChat::isOwnChange() const
{
    return 0;
}

int64_t MegaTextChat::getCreationTime() const
{
    return 0;
}

MegaTextChatList::~MegaTextChatList()
{

}

MegaTextChatList *MegaTextChatList::copy() const
{
    return NULL;
}

const MegaTextChat *MegaTextChatList::get(unsigned int) const
{
    return NULL;
}

int MegaTextChatList::size() const
{
    return 0;
}

#endif  // ENABLE_CHAT


MegaStringMap::~MegaStringMap()
{

}

MegaStringMap *MegaStringMap::copy() const
{
    return NULL;
}

const char *MegaStringMap::get(const char*) const
{
    return NULL;
}

MegaStringList *MegaStringMap::getKeys() const
{
    return NULL;
}

void MegaStringMap::set(const char *, const char *)
{

}

int MegaStringMap::size() const
{
    return 0;
}

MegaTransferData::~MegaTransferData()
{

}

MegaTransferData *MegaTransferData::copy() const
{
    return NULL;
}

int MegaTransferData::getNumDownloads() const
{
    return 0;
}

int MegaTransferData::getNumUploads() const
{
    return 0;
}

int MegaTransferData::getDownloadTag(int i) const
{
    return 0;
}

int MegaTransferData::getUploadTag(int i) const
{
    return 0;
}

unsigned long long MegaTransferData::getDownloadPriority(int i) const
{
    return 0;
}

unsigned long long MegaTransferData::getUploadPriority(int i) const
{
    return 0;
}

long long MegaTransferData::getNotificationNumber() const
{
    return 0;
}

MegaEvent::~MegaEvent() { }
MegaEvent *MegaEvent::copy()
{
    return NULL;
}

int MegaEvent::getType() const
{
    return 0;
}

const char *MegaEvent::getText() const
{
    return NULL;
}

MegaHandleList *MegaHandleList::createInstance()
{
    return new MegaHandleListPrivate();
}

MegaHandleList::~MegaHandleList()
{

}

MegaHandleList *MegaHandleList::copy() const
{
    return NULL;
}

MegaHandle MegaHandleList::get(unsigned int i) const
{
    return INVALID_HANDLE;
}

unsigned int MegaHandleList::size() const
{
    return 0;
}

void MegaHandleList::addMegaHandle(MegaHandle megaHandle)
{

}

MegaChildrenLists::~MegaChildrenLists()
{

}

MegaChildrenLists *MegaChildrenLists::copy()
{
    return NULL;
}

MegaNodeList *MegaChildrenLists::getFileList()
{
    return NULL;
}

MegaNodeList *MegaChildrenLists::getFolderList()
{
    return NULL;
}

MegaAchievementsDetails::~MegaAchievementsDetails()
{

}

long long MegaAchievementsDetails::getBaseStorage()
{
    return 0;
}

long long MegaAchievementsDetails::getClassStorage(int class_id)
{
    return 0;
}

long long MegaAchievementsDetails::getClassTransfer(int class_id)
{
    return 0;
}

int MegaAchievementsDetails::getClassExpire(int class_id)
{
    return 0;
}

unsigned int MegaAchievementsDetails::getAwardsCount()
{
    return 0;
}

int MegaAchievementsDetails::getAwardClass(unsigned int index)
{
    return 0;
}

int MegaAchievementsDetails::getAwardId(unsigned int index)
{
    return 0;
}

int64_t MegaAchievementsDetails::getAwardTimestamp(unsigned int index)
{
    return 0;
}

int64_t MegaAchievementsDetails::getAwardExpirationTs(unsigned int index)
{
    return 0;
}

MegaStringList* MegaAchievementsDetails::getAwardEmails(unsigned int index)
{
    return NULL;
}

int MegaAchievementsDetails::getRewardsCount()
{
    return 0;
}

int MegaAchievementsDetails::getRewardAwardId(unsigned int index)
{
    return 0;
}

long long MegaAchievementsDetails::getRewardStorage(unsigned int index)
{
    return 0;
}

long long MegaAchievementsDetails::getRewardTransfer(unsigned int index)
{
    return 0;
}

long long MegaAchievementsDetails::getRewardStorageByAwardId(int award_id)
{
    return 0;
}

long long MegaAchievementsDetails::getRewardTransferByAwardId(int award_id)
{
    return 0;
}

int MegaAchievementsDetails::getRewardExpire(unsigned int index)
{
    return 0;
}

MegaAchievementsDetails *MegaAchievementsDetails::copy()
{
    return NULL;
}

long long MegaAchievementsDetails::currentStorage()
{
    return 0;
}

long long MegaAchievementsDetails::currentTransfer()
{
    return 0;
}

long long MegaAchievementsDetails::currentStorageReferrals()
{
    return 0;
}

long long MegaAchievementsDetails::currentTransferReferrals()
{
    return 0;
}
