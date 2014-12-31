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

MegaNodeList::~MegaNodeList() { }
MegaTransferList::~MegaTransferList() { }
MegaUserList::~MegaUserList() { }
MegaShareList::~MegaShareList() { }
MegaNode::~MegaNode() { }
MegaUser::~MegaUser() { }
MegaShare::~MegaShare() { }
MegaRequest::~MegaRequest() { }
MegaTransfer::~MegaTransfer() { }


MegaError::MegaError(int errorCode)
{
	this->errorCode = errorCode;
}

MegaError::MegaError(const MegaError &megaError)
{
	errorCode = megaError.getErrorCode();
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

//Global callbacks
void MegaGlobalListener::onUsersUpdate(MegaApi *, MegaUserList *)
{ }
void MegaGlobalListener::onNodesUpdate(MegaApi *, MegaNodeList *)
{ }
void MegaGlobalListener::onReloadNeeded(MegaApi *)
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

void MegaListener::onReloadNeeded(MegaApi *)
{ }

#ifdef ENABLE_SYNC
void MegaGlobalListener::onGlobalSyncStateChanged(MegaApi *)
{ }
void MegaListener::onSyncFileStateChanged(MegaApi *api, MegaSync *sync, const char *filePath, int newState)
{ }
void MegaListener::onSyncEvent(MegaApi *api, MegaSync *sync, MegaSyncEvent *event)
{ }
void MegaListener::onSyncStateChanged(MegaApi *api, MegaSync *sync)
{ }
void MegaListener::onGlobalSyncStateChanged(MegaApi *api)
{ }
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

const char* MegaApi::getMyEmail()
{
    return pImpl->getMyEmail();
}

void MegaApi::setLogLevel(int logLevel)
{
    MegaApiImpl::setLogLevel(logLevel);
}

void MegaApi::setLoggerObject(MegaLogger *megaLogger)
{
    MegaApiImpl::setLoggerClass(megaLogger);
}

void MegaApi::log(int logLevel, const char *message, const char *filename, int line)
{
    MegaApiImpl::log(logLevel, message, filename, line);
}

const char* MegaApi::getBase64PwKey(const char *password)
{
    return pImpl->getBase64PwKey(password);
}

const char* MegaApi::getStringHash(const char* base64pwkey, const char* inBuf)
{
    return pImpl->getStringHash(base64pwkey, inBuf);
}

uint64_t MegaApi::base64ToHandle(const char* base64Handle)
{
    return MegaApiImpl::base64ToHandle(base64Handle);
}

const char *MegaApi::handleToBase64(MegaHandle handle)
{
    return MegaApiImpl::handleToBase64(handle);
}

const char *MegaApi::userHandleToBase64(MegaHandle handle)
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

void MegaApi::fastLogin(const char* email, const char *stringHash, const char *base64pwkey, MegaRequestListener *listener)
{
    pImpl->fastLogin(email, stringHash, base64pwkey,listener);
}

void MegaApi::fastLogin(const char *session, MegaRequestListener *listener)
{
    pImpl->fastLogin(session, listener);
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

const char *MegaApi::dumpSession()
{
    return pImpl->dumpSession();
}

const char *MegaApi::dumpXMPPSession()
{
    return pImpl->dumpXMPPSession();
}

void MegaApi::createAccount(const char* email, const char* password, const char* name, MegaRequestListener *listener)
{
    pImpl->createAccount(email, password, name, listener);
}

void MegaApi::fastCreateAccount(const char* email, const char *base64pwkey, const char* name, MegaRequestListener *listener)
{
    pImpl->fastCreateAccount(email, base64pwkey, name, listener);
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

void MegaApi::moveNode(MegaNode *node, MegaNode *newParent, MegaRequestListener *listener)
{
    pImpl->moveNode(node, newParent, listener);
}

void MegaApi::copyNode(MegaNode *node, MegaNode* target, MegaRequestListener *listener)
{
    pImpl->copyNode(node, target, listener);
}

void MegaApi::renameNode(MegaNode *node, const char *newName, MegaRequestListener *listener)
{
    pImpl->renameNode(node, newName, listener);
}

void MegaApi::remove(MegaNode *node, MegaRequestListener *listener)
{
    pImpl->remove(node, listener);
}

void MegaApi::sendFileToUser(MegaNode *node, MegaUser *user, MegaRequestListener *listener)
{
    pImpl->sendFileToUser(node, user, listener);
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

void MegaApi::setAvatar(const char *dstFilePath, MegaRequestListener *listener)
{
	pImpl->setAvatar(dstFilePath, listener);
}

void MegaApi::exportNode(MegaNode *node, MegaRequestListener *listener)
{
    pImpl->exportNode(node, listener);
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
    pImpl->getAccountDetails(listener);
}

void MegaApi::getPricing(MegaRequestListener *listener)
{
    pImpl->getPricing(listener);
}

void MegaApi::getPaymentUrl(MegaHandle productHandle, MegaRequestListener *listener)
{
    pImpl->getPaymentUrl(productHandle, listener);
}

const char *MegaApi::exportMasterKey()
{
    return pImpl->exportMasterKey();
}

void MegaApi::changePassword(const char *oldPassword, const char *newPassword, MegaRequestListener *listener)
{
    pImpl->changePassword(oldPassword, newPassword, listener);
}

void MegaApi::logout(MegaRequestListener *listener)
{
    pImpl->logout(listener);
}

void MegaApi::submitFeedback(int rating, const char *comment, MegaRequestListener* listener)
{
    pImpl->submitFeedback(rating, comment, listener);
}

void MegaApi::reportDebugEvent(const char *text, MegaRequestListener *listener)
{
    pImpl->reportEvent(MegaApi::EVENT_DEBUG, text, listener);
}

void MegaApi::addContact(const char* email, MegaRequestListener* listener)
{
    pImpl->addContact(email, listener);
}

void MegaApi::removeContact(MegaUser *user, MegaRequestListener* listener)
{
    pImpl->removeContact(user, listener);
}

void MegaApi::pauseTransfers(bool pause, MegaRequestListener* listener)
{
    pImpl->pauseTransfers(pause, listener);
}

//-1 -> AUTO, 0 -> NONE, >0 -> b/s
void MegaApi::setUploadLimit(int bpslimit)
{
    pImpl->setUploadLimit(bpslimit);
}

MegaTransferList *MegaApi::getTransfers()
{
    return pImpl->getTransfers();
}

MegaTransferList *MegaApi::getTransfers(int type)
{
    return pImpl->getTransfers(type);
}

void MegaApi::startUpload(const char* localPath, MegaNode* parent, MegaTransferListener *listener)
{
    pImpl->startUpload(localPath, parent, listener);
}

void MegaApi::startUpload(const char *localPath, MegaNode *parent, int64_t mtime, MegaTransferListener *listener)
{
    pImpl->startUpload(localPath, parent, mtime, listener);
}

void MegaApi::startUpload(const char* localPath, MegaNode* parent, const char* fileName, MegaTransferListener *listener)
{
    pImpl->startUpload(localPath, parent, fileName, listener);
}

void MegaApi::startUpload(const char *localPath, MegaNode *parent, const char *fileName, int64_t mtime, MegaTransferListener *listener)
{
    pImpl->startUpload(localPath, parent, fileName, mtime, listener);
}

void MegaApi::startDownload(MegaNode *node, const char* localFolder, MegaTransferListener *listener)
{
    pImpl->startDownload(node, localFolder, listener);
}

void MegaApi::cancelTransfer(MegaTransfer *t, MegaRequestListener *listener)
{
    pImpl->cancelTransfer(t, listener);
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
   pImpl->syncFolder(localFolder, megaFolder, listener);
}

void MegaApi::resumeSync(const char *localFolder, MegaNode *megaFolder, long long localfp, MegaRequestListener* listener)
{
    pImpl->resumeSync(localFolder, localfp, megaFolder, listener);
}

void MegaApi::removeSync(MegaNode *megaFolder, MegaRequestListener* listener)
{
    pImpl->removeSync(megaFolder ? megaFolder->getHandle() : UNDEF, listener);
}

void MegaApi::removeSync(MegaSync *sync, MegaRequestListener *listener)
{
    pImpl->removeSync(sync ? sync->getMegaHandle() : UNDEF, listener);
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

bool MegaApi::isScanning()
{
    return pImpl->isIndexing();
}

bool MegaApi::isSynced(MegaNode *n)
{
    return pImpl->isSynced(n);
}

bool MegaApi::isSyncable(const char *name)
{
    return pImpl->is_syncable(name);
}

void MegaApi::setExcludedNames(vector<string> *excludedNames)
{
    pImpl->setExcludedNames(excludedNames);
}

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

MegaUserList* MegaApi::getContacts()
{
    return pImpl->getContacts();
}

MegaUser* MegaApi::getContact(const char* email)
{
    return pImpl->getContact(email);
}

MegaNodeList* MegaApi::getInShares(MegaUser *megaUser)
{
    return pImpl->getInShares(megaUser);
}

MegaNodeList* MegaApi::getInShares()
{
    return pImpl->getInShares();
}

bool MegaApi::isShared(MegaNode *node)
{
    return pImpl->isShared(node);
}

MegaShareList *MegaApi::getOutShares()
{
    return pImpl->getOutShares();
}

MegaShareList* MegaApi::getOutShares(MegaNode *megaNode)
{
    return pImpl->getOutShares(megaNode);
}

int MegaApi::getAccess(MegaNode* megaNode)
{
    return pImpl->getAccess(megaNode);
}

bool MegaApi::processMegaTree(MegaNode* n, MegaTreeProcessor* processor, bool recursive)
{
    return pImpl->processMegaTree(n, processor, recursive);
}

const char *MegaApi::base64ToBase32(const char *base64)
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

const char *MegaApi::base32ToBase64(const char *base32)
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

void MegaApi::loadBalancing(const char *service, MegaRequestListener *listener)
{
    pImpl->loadBalancing(service, listener);
}

MegaNodeList* MegaApi::search(MegaNode* n, const char* searchString, bool recursive)
{
    return pImpl->search(n, searchString, recursive);
}

long long MegaApi::getSize(MegaNode *n)
{
    return pImpl->getSize(n);
}

const char *MegaApi::getFingerprint(const char *filePath)
{
    return pImpl->getFingerprint(filePath);
}

const char *MegaApi::getFingerprint(MegaNode *node)
{
    return pImpl->getFingerprint(node);
}

MegaNode *MegaApi::getNodeByFingerprint(const char *fingerprint)
{
    return pImpl->getNodeByFingerprint(fingerprint);
}

bool MegaApi::hasFingerprint(const char *fingerprint)
{
    return pImpl->hasFingerprint(fingerprint);
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

MegaError MegaApi::checkAccess(MegaNode* megaNode, int level)
{
    return pImpl->checkAccess(megaNode, level);
}

MegaError MegaApi::checkMove(MegaNode* megaNode, MegaNode* targetNode)
{
    return pImpl->checkMove(megaNode, targetNode);
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

const char* MegaApi::getNodePath(MegaNode *node)
{
    return pImpl->getNodePath(node);
}

MegaNode* MegaApi::getNodeByPath(const char *path, MegaNode* node)
{
    return pImpl->getNodeByPath(path, node);
}

MegaNode* MegaApi::getNodeByHandle(uint64_t uint64_t)
{
    return pImpl->getNodeByHandle(uint64_t);
}

void MegaApi::updateStats()
{
    pImpl->updateStats();
}

long long MegaApi::getTotalDownloadedBytes()
{
    return pImpl->getTotalDownloadedBytes();
}

long long MegaApi::getTotalUploadedBytes()
{
    return pImpl->getTotalUploadedBytes();
}

void MegaApi::update()
{
   pImpl->update();
}

bool MegaApi::isWaiting()
{
    return pImpl->isWaiting();
}

void MegaApi::removeRecursively(const char *path)
{
    MegaApiImpl::removeRecursively(path);
}

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
        return;
    }

    int size = strlen(utf8data) + 1;

    // make space for the worst case
    utf16string->resize(size * sizeof(wchar_t));

    // resize to actual result
    utf16string->resize(sizeof(wchar_t) * (MultiByteToWideChar(CP_UTF8, 0,
        utf8data,
        size,
        (wchar_t*)utf16string->data(),
        utf16string->size())));
}
#endif


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

void MegaLogger::log(const char *time, int loglevel, const char *source, const char *message)
{

}

MegaGfxProcessor::~MegaGfxProcessor() { }
MegaPricing::~MegaPricing() { }

#ifdef ENABLE_SYNC
MegaSync::~MegaSync() { }


void MegaSyncListener::onSyncFileStateChanged(MegaApi *, MegaSync *, const char *, int )
{ }

void MegaSyncListener::onSyncStateChanged(MegaApi *, MegaSync *)
{ }

void MegaSyncListener::onSyncEvent(MegaApi *api, MegaSync *sync, MegaSyncEvent *event)
{ }

MegaSyncEvent::~MegaSyncEvent()
{ }

#endif

