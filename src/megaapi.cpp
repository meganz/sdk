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

#include "megaapi.h"

#include "mega.h"
#include "megaapi_impl.h"
#include "megautils.h"

#include <mega/fuse/common/service.h>

#include <cstdint>

namespace
{
inline const char* nullToEmpty(const char* param)
{
    return param ? param : "";
}
}

namespace mega {

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

void MegaProxy::setProxyType(int newProxyType)
{
    proxyType = newProxyType;
}

void MegaProxy::setProxyURL(const char* newProxyURL)
{
    if (proxyURL)
        delete proxyURL;

    proxyURL = MegaApi::strdup(newProxyURL);
}

void MegaProxy::setCredentials(const char* newUsername, const char* newPassword)
{
    if (username)
        delete username;

    if (password)
        delete password;

    username = MegaApi::strdup(newUsername);
    password = MegaApi::strdup(newPassword);
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

MegaStringList *MegaStringList::createInstance()
{
    return new MegaStringListPrivate();
}

MegaStringList::~MegaStringList()
{

}

MegaStringList *MegaStringList::copy() const
{
    return NULL;
}

const char *MegaStringList::get(int) const
{
    return NULL;
}

int MegaStringList::size() const
{
    return 0;
}

void MegaStringList::add(const char *)
{

}

MegaStringListMap::MegaStringListMap()
{

}

MegaStringListMap::~MegaStringListMap()
{
}

MegaStringListMap* MegaStringListMap::createInstance()
{
    return new MegaStringListMapPrivate;
}

MegaStringListMap* MegaStringListMap::copy() const
{
    return nullptr;
}

const MegaStringList* MegaStringListMap::get(const char*) const
{
    return nullptr;
}

MegaStringList *MegaStringListMap::getKeys() const
{
    return nullptr;
}

void MegaStringListMap::set(const char*, const MegaStringList*)
{
}

int MegaStringListMap::size() const
{
    return 0;
}

MegaStringTable::MegaStringTable()
{
}

MegaStringTable::~MegaStringTable()
{
}

MegaStringTable* MegaStringTable::createInstance()
{
    return new MegaStringTablePrivate;
}

MegaStringTable* MegaStringTable::copy() const
{
    return nullptr;
}

void MegaStringTable::append(const MegaStringList*)
{
}

const MegaStringList* MegaStringTable::get(int) const
{
    return nullptr;
}

int MegaStringTable::size() const
{
    return 0;
}


MegaNodeList *MegaNodeList::createInstance()
{
    return new MegaNodeListPrivate();
}

MegaNodeList::MegaNodeList()
{

}

MegaNodeList::~MegaNodeList()
{

}

MegaNodeList *MegaNodeList::copy() const
{
    return NULL;
}

MegaNode *MegaNodeList::get(int) const
{
    return NULL;
}

int MegaNodeList::size() const
{
    return 0;
}

void MegaNodeList::addNode(MegaNode*) {}

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

MegaContactRequestList* MegaContactRequestList::copy() const
{
    return NULL;
}

MegaContactRequest* MegaContactRequestList::get(int index)
{
    auto self = static_cast<const MegaContactRequestList*>(this);

    return const_cast<MegaContactRequest*>(self->get(index));
}

const MegaContactRequest* MegaContactRequestList::get(int) const
{
    return NULL;
}

int MegaContactRequestList::size() const
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

MegaUserAlertList::~MegaUserAlertList() { }

MegaUserAlertList *MegaUserAlertList::copy() const
{
    return NULL;
}

MegaUserAlert *MegaUserAlertList::get(int) const
{
    return NULL;
}

int MegaUserAlertList::size() const
{
    return 0;
}

void MegaUserAlertList::clear() { }


MegaRecentActionBucket::~MegaRecentActionBucket()
{
}

MegaRecentActionBucket* MegaRecentActionBucket::copy() const
{
    return NULL;
}

int64_t MegaRecentActionBucket::getTimestamp() const
{
    return 0;
}

const char* MegaRecentActionBucket::getUserEmail() const
{
    return NULL;
}

MegaHandle MegaRecentActionBucket::getParentHandle() const
{
    return INVALID_HANDLE;
}

bool MegaRecentActionBucket::isUpdate() const
{
    return false;
}

bool MegaRecentActionBucket::isMedia() const
{
    return false;
}

const MegaNodeList* MegaRecentActionBucket::getNodes() const
{
    return NULL;
}

MegaRecentActionBucketList::~MegaRecentActionBucketList()
{
}

MegaRecentActionBucketList* MegaRecentActionBucketList::copy() const
{
    return NULL;
}

MegaRecentActionBucket* MegaRecentActionBucketList::get(int /*i*/) const
{
    return NULL;
}

int MegaRecentActionBucketList::size() const
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

int MegaNode::getType() const
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

const char *MegaNode::getOriginalFingerprint()
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

bool MegaNode::isFavourite()
{
    return false;
}

int MegaNode::getLabel()
{
    return 0;
}

int MegaNode::getWidth()
{
    return -1;
}

int MegaNode::getHeight()
{
    return -1;
}

int MegaNode::getShortformat()
{
    return -1;
}

int MegaNode::getVideocodecid()
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

const char* MegaNode::getDescription()
{
    return NULL;
}

MegaStringList* MegaNode::getTags()
{
    return NULL;
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

MegaHandle MegaNode::getHandle() const
{
    return INVALID_HANDLE;
}

MegaHandle MegaNode::getRestoreHandle()
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

char* MegaNode::getPublicLink(bool /*includeKey*/)
{
    return NULL;
}

int64_t MegaNode::getPublicLinkCreationTime()
{
    return 0;
}

const char * MegaNode::getWritableLinkAuthKey()
{
    return nullptr;
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

bool MegaNode::isMarkedSensitive()
{
    return false;
}

bool MegaNode::hasChanged(uint64_t /*changeType*/)
{
    return false;
}

uint64_t MegaNode::getChanges()
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

bool MegaNode::isCreditCardNode() const
{
    return false;
}

bool MegaNode::isPasswordNode() const
{
    return false;
}

bool MegaNode::isPasswordManagerNode() const
{
    return false;
}

MegaNode::CreditCardNodeData*
    MegaNode::CreditCardNodeData::createInstance(const char* cardNumber,
                                                 const char* notes,
                                                 const char* cardHolderName,
                                                 const char* cvv,
                                                 const char* expirationDate)
{
    return new MegaNodePrivate::CCNDataPrivate(cardNumber,
                                               notes,
                                               cardHolderName,
                                               cvv,
                                               expirationDate);
}

MegaNode::PasswordNodeData::TotpData* MegaNode::PasswordNodeData::TotpData::createRemovalInstance()
{
    return MegaNodePrivate::PNDataPrivate::TotpDataPrivate::createRemovalInstance();
}

MegaNode::PasswordNodeData::TotpData*
    MegaNode::PasswordNodeData::TotpData::createInstance(const char* sharedSecret,
                                                         const int expirationTimeSecs,
                                                         const int hashAlgorithm,
                                                         const int ndigits)
{
    return new MegaNodePrivate::PNDataPrivate::TotpDataPrivate(sharedSecret,
                                                               expirationTimeSecs,
                                                               hashAlgorithm,
                                                               ndigits);
}

MegaNode::PasswordNodeData* MegaNode::PasswordNodeData::createInstance(const char* pwd,
                                                                       const char* notes,
                                                                       const char* url,
                                                                       const char* userName,
                                                                       const TotpData* totpData)
{
    return new MegaNodePrivate::PNDataPrivate(pwd, notes, url, userName, totpData);
}

MegaNode::CreditCardNodeData* MegaNode::getCreditCardData() const
{
    return NULL;
}

MegaNode::PasswordNodeData* MegaNode::getPasswordData() const
{
    return NULL;
}

string *MegaNode::getNodeKey()
{
    return NULL;
}

bool MegaNode::isNodeKeyDecrypted()
{
    return false;
}

char *MegaNode::getFileAttrString()
{
    return NULL;
}

void MegaNode::setPrivateAuth(const char *)
{
    return;
}

MegaNodeList *MegaNode::getChildren()
{
    return NULL;
}

MegaHandle MegaNode::getOwner() const
{
    return INVALID_HANDLE;
}

const char* MegaNode::getDeviceId() const
{
    return nullptr;
}

const char* MegaNode::getS4() const
{
    return nullptr;
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
    data.resize(static_cast<size_t>(Base64::atob(d, (byte*)data.data(), int(data.size()))));

    return MegaNodePrivate::unserialize(&data);
}

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

bool MegaUser::hasChanged(uint64_t)
{
    return false;
}

uint64_t MegaUser::getChanges()
{
    return 0;
}

int MegaUser::isOwnChange()
{
    return 0;
}

MegaUserAlert::~MegaUserAlert() { }

MegaUserAlert *MegaUserAlert::copy() const
{
    return NULL;
}

unsigned MegaUserAlert::getId() const
{
    return (unsigned)-1;
}

bool MegaUserAlert::getSeen() const
{
    return false;
}

bool MegaUserAlert::getRelevant() const
{
    return false;
}

int MegaUserAlert::getType() const
{
    return -1;
}

const char *MegaUserAlert::getTypeString() const
{
    return NULL;
}

MegaHandle MegaUserAlert::getUserHandle() const
{
    return INVALID_HANDLE;
}

MegaHandle MegaUserAlert::getNodeHandle() const
{
    return INVALID_HANDLE;
}

MegaHandle MegaUserAlert::getPcrHandle() const
{
    return INVALID_HANDLE;
}

const char* MegaUserAlert::getEmail() const
{
    return NULL;
}

const char* MegaUserAlert::getPath() const
{
    return NULL;
}

const char *MegaUserAlert::getName() const
{
    return NULL;
}

const char *MegaUserAlert::getHeading() const
{
    return NULL;
}

const char *MegaUserAlert::getTitle() const
{
    return NULL;
}

int64_t MegaUserAlert::getNumber(unsigned) const
{
    return -1;
}

int64_t MegaUserAlert::getTimestamp(unsigned) const
{
    return -1;
}

const char* MegaUserAlert::getString(unsigned) const
{
    return NULL;
}
#ifdef ENABLE_CHAT
MegaHandle MegaUserAlert::getSchedId() const
{
    return INVALID_HANDLE;
}

bool MegaUserAlert::hasSchedMeetingChanged(uint64_t) const
{
    return false;
}

MegaStringList* MegaUserAlert::getUpdatedTitle() const
{
    return NULL;
}

MegaStringList* MegaUserAlert::getUpdatedTimeZone() const
{
    return NULL;
}

MegaIntegerList* MegaUserAlert::getUpdatedStartDate() const
{
    return NULL;
}

MegaIntegerList* MegaUserAlert::getUpdatedEndDate() const
{
    return NULL;
}

#endif
MegaHandle MegaUserAlert::getHandle(unsigned) const
{
    return INVALID_HANDLE;
}

bool MegaUserAlert::isOwnChange() const
{
    return false;
}

bool MegaUserAlert::isRemoved() const
{
    return false;
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

bool MegaShare::isPending()
{
    return false;
}

bool MegaShare::isVerified()
{
    return false;
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

MegaCurrency *MegaRequest::getCurrency() const
{
    return nullptr;
}

MegaAchievementsDetails *MegaRequest::getMegaAchievementsDetails() const
{
    return NULL;
}

MegaTimeZoneDetails *MegaRequest::getMegaTimeZoneDetails() const
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

MegaScheduledMeetingList* MegaRequest::getMegaScheduledMeetingList() const
{
    return nullptr;
}
#endif

MegaHandleList* MegaRequest::getMegaHandleList() const
{
    return nullptr;
}

MegaRecentActionBucketList *MegaRequest::getRecentActions() const
{
    return nullptr;
}

MegaStringMap *MegaRequest::getMegaStringMap() const
{
    return NULL;
}

MegaStringListMap* MegaRequest::getMegaStringListMap() const
{
    return nullptr;
}

MegaStringTable* MegaRequest::getMegaStringTable() const
{
    return nullptr;
}

MegaFolderInfo *MegaRequest::getMegaFolderInfo() const
{
    return NULL;
}

const MegaPushNotificationSettings *MegaRequest::getMegaPushNotificationSettings() const
{
    return NULL;
}

MegaBackgroundMediaUpload* MegaRequest::getMegaBackgroundMediaUploadPtr() const
{
    return NULL;
}

MegaBannerList* MegaRequest::getMegaBannerList() const
{
    return nullptr;
}

MegaStringList* MegaRequest::getMegaStringList() const
{
    return nullptr;
}

MegaStringIntegerMap* MegaRequest::getMegaStringIntegerMap() const
{
    return nullptr;
}

const MegaIntegerList* MegaRequest::getMegaIntegerList() const
{
    return nullptr;
}

MegaSet* MegaRequest::getMegaSet() const
{
    return nullptr;
}

MegaSetElementList* MegaRequest::getMegaSetElementList() const
{
    return nullptr;
}

MegaBackupInfoList* MegaRequest::getMegaBackupInfoList() const
{
    return nullptr;
}

#ifdef ENABLE_SYNC

MegaSyncStallList* MegaRequest::getMegaSyncStallList() const
{
    return nullptr;
}

MegaSyncStallMap* MegaRequest::getMegaSyncStallMap() const
{
    return nullptr;
}
#endif // ENABLE_SYNC

MegaVpnRegionList* MegaRequest::getMegaVpnRegionsDetailed() const
{
    return nullptr;
}

MegaVpnCredentials* MegaRequest::getMegaVpnCredentials() const
{
    return nullptr;
}

MegaNetworkConnectivityTestResults* MegaRequest::getMegaNetworkConnectivityTestResults() const
{
    return nullptr;
}

const MegaNotificationList* MegaRequest::getMegaNotifications() const
{
    return nullptr;
}

const MegaNodeTree* MegaRequest::getMegaNodeTree() const
{
    return nullptr;
}

const MegaCancelSubscriptionReasonList* MegaRequest::getMegaCancelSubscriptionReasons() const
{
    return nullptr;
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

unsigned MegaTransfer::getStage() const
{
    return 0;
}

uint32_t MegaTransfer::getUniqueId() const
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

bool MegaTransfer::isFinished() const
{
    return false;
}

bool MegaTransfer::isBackupTransfer() const
{
    return false;
}

bool MegaTransfer::isForeignOverquota() const
{
    return false;
}

bool MegaTransfer::isForceNewUpload() const
{
    return false;
}

char *MegaTransfer::getLastBytes() const
{
    return NULL;
}

const MegaError *MegaTransfer::getLastErrorExtended() const
{
    return nullptr;
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

bool MegaTransfer::getTargetOverride() const
{
    return false;
}

MegaCancelToken* MegaTransfer::getCancelToken()
{
    return NULL;
}

const char* MegaTransfer::stageToString(unsigned stage)
{
    switch (stage)
    {
        case MegaTransfer::STAGE_NONE:                      return "Not initialized stage";
        case MegaTransfer::STAGE_SCAN:                      return "Scan stage";
        case MegaTransfer::STAGE_CREATE_TREE:               return "Create tree stage";
        case MegaTransfer::STAGE_TRANSFERRING_FILES:        return "Transferring files stage";
        default:                                            return "Invalid stage";
    }
}

MegaError::MegaError(int e)
{
    errorCode = e;
    syncError = NO_SYNC_ERROR;
}

MegaError::MegaError(int e, int se)
{
    errorCode = e;
    syncError = se;
}

MegaError::~MegaError()
{

}

MegaError* MegaError::copy() const
{
    return new MegaError(*this);
}

int MegaError::getErrorCode() const
{
    return errorCode;
}

int MegaError::getMountResult() const
{
    return MegaMount::SUCCESS;
}

int MegaError::getSyncError() const
{
    return syncError;
}

long long MegaError::getValue() const
{
    return 0;
}

bool MegaError::hasExtraInfo() const
{
    return false;
}

long long MegaError::getUserStatus() const
{
    return 0;
}

long long MegaError::getLinkStatus() const
{
    return 0;
}

const char* MegaError::getErrorString() const
{
    return MegaError::getErrorString(errorCode);
}

const char* MegaError::getErrorString(int errorCode)
{
    return MegaError::getErrorString(errorCode, API_EC_DEFAULT);
}

const char* MegaError::getErrorString(int errorCode, ErrorContexts context)
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
            switch (context)
            {
                case API_EC_DOWNLOAD:
                    return "Terms of Service breached";
                default:
                    return "Too many concurrent connections or transfers";
            }
        case API_ERANGE:
            return "Out of range";
        case API_EEXPIRED:
            return "Expired";
        case API_ENOENT:
            return "Not found";
        case API_ECIRCULAR:
            switch (context)
            {
                case API_EC_UPLOAD:
                    return "Upload produces recursivity";
                default:
                    return "Circular linkage detected";
            }
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
            switch (context)
            {
                case API_EC_IMPORT:
                case API_EC_DOWNLOAD:
                    return "File removed as it violated our Terms of Service";
                default:
                    return "Blocked";
            }
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
        case API_EROLLEDBACK:
            return "Strongly-grouped request rolled back";
        case API_EMFAREQUIRED:
            return "Multi-factor authentication required";
        case API_EMASTERONLY:
            return "Access denied for users";
        case API_EBUSINESSPASTDUE:
            return "Business account has expired";
        case API_EPAYWALL:
            return "Storage Quota Exceeded. Upgrade now";
        case API_ESUBUSERKEYMISSING:
            return "A business error where a subuser has not yet encrypted their master key for "
                   "the admin user and tries to perform a disallowed command (currently u and p)";
        case LOCAL_ENOSPC:
            return "Insufficient disk space";
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

bool MegaContactRequest::isAutoAccepted() const
{
    return false;
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
MegaRequestListener::~MegaRequestListener()
{ }


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
    delete megaRequest;              //in case of reused listener
    this->megaRequest = request ? request->copy() : nullptr;
    delete megaError;            //in case of reused listener
    this->megaError = error->copy();

    doOnRequestFinish(api, request, error);
    semaphore->release();
}

void SynchronousRequestListener::doOnRequestFinish(MegaApi*, MegaRequest*, MegaError*) {}

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

void MegaTransferListener::onFolderTransferUpdate(MegaApi*,
                                                  MegaTransfer*,
                                                  int /*stage*/,
                                                  uint32_t /*foldercount*/,
                                                  uint32_t /*filecount*/,
                                                  uint32_t /*createdfoldercount*/,
                                                  const char* /*currentFolder*/,
                                                  const char* /*currentFileLeafname*/)
{}

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
    this->megaTransfer = transfer ? transfer->copy() : nullptr;
    delete megaError;            //in case of reused listener
    this->megaError = error ? error->copy() : nullptr;

    doOnTransferFinish(api, transfer, error);
    semaphore->release();
}

void SynchronousTransferListener::doOnTransferFinish(MegaApi*, MegaTransfer*, MegaError*) {}

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
void MegaGlobalListener::onUsersUpdate(MegaApi*, MegaUserList*) {}

void MegaGlobalListener::onUserAlertsUpdate(MegaApi*, MegaUserAlertList*) {}

void MegaGlobalListener::onNodesUpdate(MegaApi*, MegaNodeList*) {}

void MegaGlobalListener::onAccountUpdate(MegaApi*) {}

void MegaGlobalListener::onSetsUpdate(MegaApi*, MegaSetList*) {}

void MegaGlobalListener::onSetElementsUpdate(MegaApi*, MegaSetElementList*) {}

void MegaGlobalListener::onContactRequestsUpdate(MegaApi*, MegaContactRequestList*) {}

void MegaGlobalListener::onEvent(MegaApi*, MegaEvent*) {}

void MegaGlobalListener::onDrivePresenceChanged(MegaApi*,
                                                bool /*present*/,
                                                const char* /*rootPathInUtf8*/)
{}

void MegaGlobalListener::onSeqTagUpdate(MegaApi*, const std::string* /*seqTag*/) {}

MegaGlobalListener::~MegaGlobalListener()
{ }

//All callbacks
void MegaListener::onRequestStart(MegaApi*, MegaRequest*) {}

void MegaListener::onRequestFinish(MegaApi*, MegaRequest*, MegaError*) {}

void MegaListener::onRequestUpdate(MegaApi*, MegaRequest*) {}

void MegaListener::onRequestTemporaryError(MegaApi*, MegaRequest*, MegaError*) {}

void MegaListener::onTransferStart(MegaApi*, MegaTransfer*) {}

void MegaListener::onTransferFinish(MegaApi*, MegaTransfer*, MegaError*) {}

void MegaListener::onTransferUpdate(MegaApi*, MegaTransfer*) {}

void MegaListener::onTransferTemporaryError(MegaApi*, MegaTransfer*, MegaError*) {}

void MegaListener::onUsersUpdate(MegaApi*, MegaUserList*) {}

void MegaListener::onUserAlertsUpdate(MegaApi*, MegaUserAlertList*) {}

void MegaListener::onNodesUpdate(MegaApi*, MegaNodeList*) {}

void MegaListener::onAccountUpdate(MegaApi*) {}

void MegaListener::onSetsUpdate(MegaApi*, MegaSetList*) {}

void MegaListener::onSetElementsUpdate(MegaApi*, MegaSetElementList*) {}

void MegaListener::onContactRequestsUpdate(MegaApi*, MegaContactRequestList*) {}

void MegaListener::onEvent(MegaApi*, MegaEvent*) {}

#ifdef ENABLE_SYNC
void MegaGlobalListener::onGlobalSyncStateChanged(MegaApi*) {}

void MegaListener::onSyncFileStateChanged(MegaApi*, MegaSync*, string*, int) {}

void MegaListener::onSyncAdded(MegaApi*, MegaSync*) {}

void MegaListener::onSyncDeleted(MegaApi*, MegaSync*) {}

void MegaListener::onSyncStateChanged(MegaApi*, MegaSync*) {}

void MegaListener::onSyncStatsUpdated(MegaApi*, MegaSyncStats*) {}

void MegaListener::onGlobalSyncStateChanged(MegaApi*) {}

void MegaListener::onSyncRemoteRootChanged(MegaApi*, MegaSync*) {}
#endif

void MegaListener::onBackupStateChanged(MegaApi *, MegaScheduledCopy *)
{ }
void MegaListener::onBackupStart(MegaApi *, MegaScheduledCopy *)
{ }
void MegaListener::onBackupFinish(MegaApi *, MegaScheduledCopy *, MegaError *)
{ }
void MegaListener::onBackupUpdate(MegaApi *, MegaScheduledCopy *)
{ }
void MegaListener::onBackupTemporaryError(MegaApi *, MegaScheduledCopy *, MegaError *)
{ }

#ifdef ENABLE_CHAT
void MegaGlobalListener::onChatsUpdate(MegaApi*, MegaTextChatList*) {}

void MegaListener::onChatsUpdate(MegaApi*, MegaTextChatList*) {}
#endif

MegaListener::~MegaListener() {}

void MegaListener::onMountAdded(MegaApi*, const char*, int)
{
}

void MegaListener::onMountChanged(MegaApi*, const char*, int)
{
}

void MegaListener::onMountDisabled(MegaApi*, const char*, int)
{
}

void MegaListener::onMountEnabled(MegaApi*, const char*, int)
{
}

void MegaListener::onMountRemoved(MegaApi*, const char*, int)
{
}

bool MegaTreeProcessor::processMegaNode(MegaNode*)
{ return false; /* Stops the processing */ }
MegaTreeProcessor::~MegaTreeProcessor()
{ }

/* BEGIN MEGAAPI */

MegaApi::MegaApi(const char *appKey, MegaGfxProvider* provider, const char *basePath, const char *userAgent, unsigned workerThreadCount, int clientType)
{
    pImpl = new MegaApiImpl(this, appKey, provider, basePath, userAgent, workerThreadCount, clientType);
}

MegaApi::MegaApi(const char *appKey, const char *basePath, const char *userAgent, unsigned workerThreadCount, int clientType)
{
    pImpl = new MegaApiImpl(this, appKey, static_cast<MegaGfxProcessor*>(nullptr), basePath, userAgent, workerThreadCount, clientType);
}

#ifdef HAVE_MEGAAPI_RPC
MegaApi::MegaApi() {}
#endif

MegaApi::~MegaApi()
{
    delete pImpl;
}

int MegaApi::isLoggedIn()
{
    return pImpl->isLoggedIn();
}

bool MegaApi::isEphemeralPlusPlus()
{
    return pImpl->isEphemeralPlusPlus();
}

void MegaApi::whyAmIBlocked(MegaRequestListener *listener)
{
    pImpl->whyAmIBlocked(false, listener);
}

void MegaApi::contactLinkCreate(bool renew, MegaRequestListener *listener)
{
    pImpl->contactLinkCreate(renew, listener);
}

void MegaApi::contactLinkQuery(MegaHandle handle, MegaRequestListener *listener)
{
    pImpl->contactLinkQuery(handle, listener);
}

void MegaApi::contactLinkDelete(MegaHandle handle, MegaRequestListener *listener)
{
    pImpl->contactLinkDelete(handle, listener);
}

void MegaApi::keepMeAlive(int type, bool enable, MegaRequestListener *listener)
{
    pImpl->keepMeAlive(type, enable, listener);
}

void MegaApi::setPSA(int id, MegaRequestListener *listener)
{
    pImpl->setPSA(id, listener);
}

void MegaApi::getPSA(MegaRequestListener *listener)
{
    pImpl->getPSA(false, listener);
}

void MegaApi::getPSAWithUrl(MegaRequestListener *listener)
{
    pImpl->getPSA(true, listener);
}

void MegaApi::acknowledgeUserAlerts(MegaRequestListener *listener)
{
    pImpl->acknowledgeUserAlerts(listener);
}

char *MegaApi::getMyEmail()
{
    return pImpl->getMyEmail();
}

void MegaApi::getRecommendedProLevel(MegaRequestListener* listener)
{
    pImpl->getRecommendedProLevel(listener);
}

int64_t MegaApi::getAccountCreationTs()
{
    return pImpl->getAccountCreationTs();
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

bool MegaApi::isAchievementsEnabled()
{
    return pImpl->isAchievementsEnabled();
}

bool MegaApi::isProFlexiAccount()
{
    return pImpl->isProFlexiAccount();
}

bool MegaApi::isBusinessAccount()
{
    return pImpl->isBusinessAccount();
}

bool MegaApi::isMasterBusinessAccount()
{
    return pImpl->isMasterBusinessAccount();
}

int MegaApi::getBusinessStatus()
{
    return pImpl->getBusinessStatus();
}

bool MegaApi::isBusinessAccountActive()
{
    return pImpl->isBusinessAccountActive();
}

bool MegaApi::checkPassword(const char *password)
{
    return pImpl->checkPassword(password);
}

char *MegaApi::getMyCredentials()
{
    return pImpl->getMyCredentials();
}

void MegaApi::getUserCredentials(MegaUser *user, MegaRequestListener *listener)
{
    pImpl->getUserCredentials(user, listener);
}

bool MegaApi::areCredentialsVerified(MegaUser *user)
{
    return pImpl->areCredentialsVerified(user);
}

void MegaApi::verifyCredentials(MegaUser *user, MegaRequestListener *listener)
{
    pImpl->verifyCredentials(user, listener);
}

void MegaApi::resetCredentials(MegaUser *user, MegaRequestListener *listener)
{
    pImpl->resetCredentials(user, listener);
}

void MegaApi::setLogLevel(int logLevel)
{
    MegaApiImpl::setLogLevel(logLevel);
}

void MegaApi::setLogExtraForModules(bool networking, bool syncs)
{
    return pImpl->setLogExtraForModules(networking, syncs);
}

void MegaApi::setMaxPayloadLogSize(long long maxSize)
{
    MegaApiImpl::setMaxPayloadLogSize(maxSize);
}

void MegaApi::setLogToConsole(bool enable)
{
    MegaApiImpl::setLogToConsole(enable);
}

void MegaApi::setLogJSONContent(bool enable)
{
    MegaApiImpl::setLogJSONContent(enable);
}

void MegaApi::addLoggerObject(MegaLogger *megaLogger, bool singleExclusiveLogger)
{
    MegaApiImpl::addLoggerClass(megaLogger, singleExclusiveLogger);
}

void MegaApi::removeLoggerObject(MegaLogger *megaLogger, bool singleExclusiveLogger)
{
    MegaApiImpl::removeLoggerClass(megaLogger, singleExclusiveLogger);
}

void MegaApi::log(int logLevel, const char *message, const char *filename, int line)
{
    MegaApiImpl::log(logLevel, message, filename, line);
}

void MegaApi::setLoggingName(const char* loggingName)
{
    pImpl->setLoggingName(loggingName);
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

MegaHandle MegaApi::base64ToBackupId(const char* backupId)
{
    return MegaApiImpl::base64ToBackupId(backupId);
}

char *MegaApi::handleToBase64(MegaHandle handle)
{
    return MegaApiImpl::handleToBase64(handle);
}

char *MegaApi::userHandleToBase64(MegaHandle handle)
{
    return MegaApiImpl::userHandleToBase64(handle);
}

const char* MegaApi::backupIdToBase64(MegaHandle backupId)
{
    return MegaApiImpl::backupIdToBase64(backupId);
}

void MegaApi::base64ToBinary(const char *base64string, unsigned char **binary, size_t* binarysize)
{
    return MegaApiImpl::base64ToBinary(base64string, binary, binarysize);
}

char *MegaApi::binaryToBase64(const char* binaryData, size_t length)
{
    return MegaApiImpl::binaryToBase64(binaryData, length);
}

void MegaApi::retryPendingConnections(bool disconnect, bool includexfers, MegaRequestListener* listener)
{
    pImpl->retryPendingConnections(disconnect, includexfers, listener);
}

void MegaApi::setDnsServers(const char *dnsServers, MegaRequestListener *listener)
{
    pImpl->setDnsServers(dnsServers, listener);
}

bool MegaApi::serverSideRubbishBinAutopurgeEnabled()
{
    return pImpl->serverSideRubbishBinAutopurgeEnabled();
}

bool MegaApi::appleVoipPushEnabled()
{
    return pImpl->appleVoipPushEnabled();
}

bool MegaApi::newLinkFormatEnabled()
{
    return pImpl->newLinkFormatEnabled();
}

bool MegaApi::accountIsNew() const
{
    return pImpl->accountIsNew();
}

unsigned int MegaApi::getABTestValue(const char* flag)
{
    return pImpl->getABTestValue(flag);
}

int MegaApi::smsAllowedState()
{
    return pImpl->smsAllowedState();
}

char* MegaApi::smsVerifiedPhoneNumber()
{
    return pImpl->smsVerifiedPhoneNumber();
}

void MegaApi::resetSmsVerifiedPhoneNumber(MegaRequestListener *listener)
{
    pImpl->resetSmsVerifiedPhoneNumber(listener);
}

bool MegaApi::multiFactorAuthAvailable()
{
    return pImpl->multiFactorAuthAvailable();
}

void MegaApi::multiFactorAuthCheck(const char *email, MegaRequestListener *listener)
{
    pImpl->multiFactorAuthCheck(email, listener);
}

void MegaApi::multiFactorAuthGetCode(MegaRequestListener *listener)
{
    pImpl->multiFactorAuthGetCode(listener);
}

void MegaApi::multiFactorAuthEnable(const char *pin, MegaRequestListener *listener)
{
    pImpl->multiFactorAuthEnable(pin, listener);
}

void MegaApi::multiFactorAuthDisable(const char *pin, MegaRequestListener *listener)
{
    pImpl->multiFactorAuthDisable(pin, listener);
}

void MegaApi::multiFactorAuthLogin(const char *email, const char *password, const char *pin, MegaRequestListener *listener)
{
    pImpl->multiFactorAuthLogin(email, password, pin, listener);
}

void MegaApi::multiFactorAuthChangePassword(const char *oldPassword, const char *newPassword, const char *pin, MegaRequestListener *listener)
{
    pImpl->multiFactorAuthChangePassword(oldPassword, newPassword, pin, listener);
}

void MegaApi::multiFactorAuthChangeEmail(const char *email, const char *pin, MegaRequestListener *listener)
{
    pImpl->multiFactorAuthChangeEmail(email, pin, listener);
}

void MegaApi::multiFactorAuthCancelAccount(const char *pin, MegaRequestListener *listener)
{
    pImpl->multiFactorAuthCancelAccount(pin, listener);
}

void MegaApi::fetchTimeZone(MegaRequestListener *listener)
{
    pImpl->fetchTimeZone(true /*forceApiFetch*/, listener);
}

void MegaApi::fetchTimeZoneFromLocal(MegaRequestListener* listener)
{
    pImpl->fetchTimeZone(false /*forceApiFetch*/, listener);
}

void MegaApi::addEntropy(char *data, unsigned int size)
{
    pImpl->addEntropy(data, size);
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

void MegaApi::getMiscFlags(MegaRequestListener *listener)
{
    pImpl->getMiscFlags(listener);
}

void MegaApi::sendDevCommand(const char *command, const char *email, MegaRequestListener *listener)
{
    pImpl->sendDevCommand(command, email, 0, 0, 0, listener);
}

void MegaApi::sendOdqDevCommand(const char *email, MegaRequestListener *listener)
{
    pImpl->sendDevCommand("aodq", email, 0, 0, 0, listener);
}

void MegaApi::sendUsedTransferQuotaDevCommand(long long quota, const char *email, MegaRequestListener *listener)
{
    pImpl->sendDevCommand("tq", email, quota, 0, 0, listener);
}

void MegaApi::sendBusinessStatusDevCommand(int businessStatus, const char *email, MegaRequestListener *listener)
{
    pImpl->sendDevCommand("bs", email, 0, businessStatus, 0, listener);
}

void MegaApi::sendSetAccountLevelDevCommand(int accountLevel,
                                            int quotaLengthInMonths,
                                            const char* email,
                                            MegaRequestListener* listener)
{
    pImpl->sendDevCommand("sal", email, quotaLengthInMonths, 0, accountLevel, listener);
}

void MegaApi::sendUserStatusDevCommand(int userStatus, const char *email, MegaRequestListener *listener)
{
    pImpl->sendDevCommand("us", email, 0, 0, userStatus, listener);
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

char *MegaApi::getSequenceTag()
{
    return pImpl->getSequenceTag();
}

char *MegaApi::getAccountAuth()
{
    return pImpl->getAccountAuth();
}

void MegaApi::setAccountAuth(const char *auth)
{
    pImpl->setAccountAuth(auth);
}

void MegaApi::createAccount(const char* email, const char* password, const char* firstname, const char*  lastname, MegaRequestListener *listener)
{
    pImpl->createAccount(email, password, firstname, lastname, UNDEF, AFFILIATE_TYPE_INVALID, 0, listener);
}

void MegaApi::createEphemeralAccountPlusPlus(const char *firstname, const char *lastname, MegaRequestListener *listener)
{
    pImpl->createEphemeralAccountPlusPlus(firstname, lastname, listener);
}

void MegaApi::resumeCreateAccount(const char* sid, MegaRequestListener *listener)
{
    pImpl->resumeCreateAccount(sid, listener);
}

void MegaApi::resumeCreateAccountEphemeralPlusPlus(const char *sid, MegaRequestListener *listener)
{
    pImpl->resumeCreateAccountEphemeralPlusPlus(sid, listener);
}

void MegaApi::cancelCreateAccount(MegaRequestListener *listener)
{
    pImpl->cancelCreateAccount(listener);
}

void MegaApi::resendSignupLink(const char *email, const char *name, MegaRequestListener *listener)
{
    pImpl->resendSignupLink(email, name, listener);
}

void MegaApi::querySignupLink(const char* link, MegaRequestListener *listener)
{
    pImpl->querySignupLink(link, listener);
}

void MegaApi::confirmAccount(const char* link, const char *password, MegaRequestListener *listener)
{
    pImpl->confirmAccount(link, password, listener);
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

void MegaApi::checkRecoveryKey(const char* link, const char* recoveryKey, MegaRequestListener* listener)
{
    pImpl->checkRecoveryKey(link, recoveryKey, listener);
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

void MegaApi::resendVerificationEmail(MegaRequestListener *listener)
{
    pImpl->resendVerificationEmail(listener);
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

void MegaApi::setProxySettings(MegaProxy *proxySettings, MegaRequestListener *listener)
{
    pImpl->setProxySettings(proxySettings, listener);
}

MegaProxy *MegaApi::getAutoProxySettings()
{
    return pImpl->getAutoProxySettings();
}

void MegaApi::createFolder(const char *name, MegaNode *parent, MegaRequestListener *listener)
{
    pImpl->createFolder(name, parent, listener);
}

void MegaApi::getPasswordManagerBase(MegaRequestListener *listener)
{
    pImpl->getPasswordManagerBase(listener);
}

bool MegaApi::isPasswordNodeFolder(MegaHandle node) const
{
    return pImpl->isPasswordManagerNodeFolder(node);
}

bool MegaApi::isPasswordManagerNodeFolder(MegaHandle node) const
{
    return pImpl->isPasswordManagerNodeFolder(node);
}

void MegaApi::createCreditCardNode(const char* name,
                                   const MegaNode::CreditCardNodeData* data,
                                   MegaHandle parent,
                                   MegaRequestListener* listener)
{
    pImpl->createCreditCardNode(name, data, parent, listener);
}

void MegaApi::createPasswordNode(const char* name, const MegaNode::PasswordNodeData* data,
                                 MegaHandle parent, MegaRequestListener* listener)
{
    pImpl->createPasswordNode(name, data, parent, listener);
}

void MegaApi::updateCreditCardNode(MegaHandle node,
                                   const MegaNode::CreditCardNodeData* newData,
                                   MegaRequestListener* listener)
{
    pImpl->updateCreditCardNode(node, newData, listener);
}

void MegaApi::updatePasswordNode(MegaHandle node, const MegaNode::PasswordNodeData* newData,
                                 MegaRequestListener* listener)
{
    pImpl->updatePasswordNode(node, newData, listener);
}

void MegaApi::importPasswordsFromFile(const char* filePath,
                                      const int fileSource,
                                      MegaHandle parent,
                                      MegaRequestListener* listener)
{
    pImpl->importPasswordsFromFile(filePath, fileSource, parent, listener);
}

bool MegaApi::createLocalFolder(const char *localPath)
{
    return pImpl->createLocalFolder(localPath);
}

void MegaApi::moveNode(MegaNode *node, MegaNode *newParent, MegaRequestListener *listener)
{
    pImpl->moveNode(node, newParent, listener);
}

void MegaApi::moveNode(MegaNode *node, MegaNode *newParent, const char *newName, MegaRequestListener *listener)
{
    pImpl->moveNode(node, newParent, newName, listener);
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

void MegaApi::upgradeSecurity(MegaRequestListener* listener)
{
    pImpl->upgradeSecurity(listener);
}

bool MegaApi::contactVerificationWarningEnabled()
{
    return pImpl->contactVerificationWarningEnabled();
}

void MegaApi::setManualVerificationFlag(bool enable)
{
    pImpl->setManualVerificationFlag(enable);
}

void MegaApi::openShareDialog(MegaNode *node, MegaRequestListener *listener)
{
    pImpl->openShareDialog(node, listener);
}

MegaShareList *MegaApi::getUnverifiedInShares(int order)
{
    return pImpl->getUnverifiedInShares(order);
}

MegaShareList *MegaApi::getUnverifiedOutShares(int order)
{
    return pImpl->getUnverifiedOutShares(order);
}

void MegaApi::share(MegaNode* node, MegaUser *user, int access, MegaRequestListener *listener)
{
    pImpl->share(node, user, access, listener);
}

void MegaApi::share(MegaNode *node, const char* email, int access, MegaRequestListener *listener)
{
    pImpl->share(node, email, access, listener);
}

void MegaApi::loginToFolder(const char* megaFolderLink, MegaRequestListener* listener)
{
    pImpl->loginToFolder(megaFolderLink, nullptr, false, listener);
}

void MegaApi::loginToFolder(const char* megaFolderLink, const char* authKey, MegaRequestListener *listener)
{
    pImpl->loginToFolder(megaFolderLink, authKey, false, listener);
}

void MegaApi::loginToFolder(const char* megaFolderLink,
                            const char* authKey,
                            const bool tryToResumeFolderLinkFromCache,
                            MegaRequestListener* listener)
{
    pImpl->loginToFolder(megaFolderLink, authKey, tryToResumeFolderLinkFromCache, listener);
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

void MegaApi::getDownloadUrl(MegaNode* node, bool singleUrl, MegaRequestListener *listener)
{
    pImpl->getDownloadUrl(node, singleUrl, listener);
}

const char *MegaApi::buildPublicLink(const char *publicHandle, const char *key, bool isFolder)
{
    return pImpl->buildPublicLink(publicHandle, key, isFolder);
}

void MegaApi::getThumbnail(MegaNode* node, const char *dstFilePath, MegaRequestListener *listener)
{
    pImpl->getThumbnail(node, dstFilePath, listener);
}

void MegaApi::getThumbnail(MegaHandle handle,
                           const char* dstFilePath,
                           MegaRequestListener* listener)
{
    pImpl->getThumbnail(handle, dstFilePath, listener);
}

void MegaApi::cancelGetThumbnail(MegaNode* node, MegaRequestListener *listener)
{
	pImpl->cancelGetThumbnail(node, listener);
}

void MegaApi::setThumbnail(MegaNode* node, const char *srcFilePath, MegaRequestListener *listener)
{
    pImpl->setThumbnail(node, srcFilePath, listener);
}

void MegaApi::putThumbnail(MegaBackgroundMediaUpload* bu, const char *srcFilePath, MegaRequestListener *listener)
{
    pImpl->putThumbnail(bu, srcFilePath, listener);
}

void MegaApi::setThumbnailByHandle(MegaNode* node, MegaHandle fileattribute, MegaRequestListener *listener)
{
    pImpl->setThumbnailByHandle(node, fileattribute, listener);
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

void MegaApi::putPreview(MegaBackgroundMediaUpload* bu, const char *srcFilePath, MegaRequestListener *listener)
{
    pImpl->putPreview(bu, srcFilePath, listener);
}

void MegaApi::setPreviewByHandle(MegaNode* node, MegaHandle fileattribute, MegaRequestListener *listener)
{
    pImpl->setPreviewByHandle(node, fileattribute, listener);
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

char *MegaApi::getUserAvatarSecondaryColor(MegaUser *user)
{
    return MegaApiImpl::getUserAvatarSecondaryColor(user);
}

char *MegaApi::getUserAvatarSecondaryColor(const char *userhandle)
{
    return MegaApiImpl::getUserAvatarSecondaryColor(userhandle);
}

void MegaApi::setAvatar(const char *dstFilePath, MegaRequestListener *listener)
{
    pImpl->setAvatar(dstFilePath, listener);
}

char* MegaApi::getPrivateKey(int type)
{
    return pImpl->getPrivateKey(type);
}

bool MegaApi::testAllocation(unsigned allocCount, size_t allocSize)
{
    return pImpl->testAllocation(allocCount, allocSize);
}

void MegaApi::getUserAttribute(MegaUser* user, int type, MegaRequestListener *listener)
{
    pImpl->getUserAttribute(user, type, listener);
}

void MegaApi::getChatUserAttribute(const char *email_or_handle, int type, const char *ph, MegaRequestListener *listener)
{
    pImpl->getChatUserAttribute(email_or_handle, type, ph, listener);
}

void MegaApi::getUserAttribute(int type, MegaRequestListener *listener)
{
    pImpl->getUserAttribute((MegaUser*)NULL, type, listener);
}

const char *MegaApi::userAttributeToString(int attr)
{
    return MegaApi::strdup(pImpl->userAttributeToString(attr).c_str());
}

const char *MegaApi::userAttributeToLongName(int attr)
{
    return MegaApi::strdup(pImpl->userAttributeToLongName(attr).c_str());
}

int MegaApi::userAttributeFromString(const char *name)
{
    return pImpl->userAttributeFromString(name);
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

void MegaApi::setNodeS4(MegaNode *node, const char *value, MegaRequestListener *listener)
{
    pImpl->setNodeS4(node, value, listener);
}

void MegaApi::setNodeLabel(MegaNode *node, int label, MegaRequestListener *listener)
{
    pImpl->setNodeLabel(node, label, listener);
}

void MegaApi::resetNodeLabel(MegaNode *node, MegaRequestListener *listener)
{
    pImpl->setNodeLabel(node, MegaNode::NODE_LBL_UNKNOWN, listener);
}

void MegaApi::setNodeFavourite(MegaNode *node, bool fav, MegaRequestListener *listener)
{
    pImpl->setNodeFavourite(node, fav, listener);
}

void MegaApi::getFavourites(MegaNode* node, int count, MegaRequestListener* listener)
{
    pImpl->getFavourites(node, count, listener);
}

void MegaApi::setNodeSensitive(MegaNode* node, bool sensitive, MegaRequestListener* listener)
{
    pImpl->setNodeSensitive(node, sensitive, listener);
}

void MegaApi::setNodeCoordinates(MegaNode *node, double latitude, double longitude, MegaRequestListener *listener)
{
    pImpl->setNodeCoordinates(node, false, latitude, longitude, listener);
}

void MegaApi::setUnshareableNodeCoordinates(MegaNode *node, double latitude, double longitude, MegaRequestListener *listener)
{
    pImpl->setNodeCoordinates(node, true, latitude, longitude, listener);
}

void MegaApi::setNodeDescription(MegaNode* node, const char* description, MegaRequestListener* listener)
{
    pImpl->setNodeDescription(node, description, listener);
}

void MegaApi::addNodeTag(MegaNode* node, const char* tag, MegaRequestListener* listener)
{
    pImpl->addNodeTag(node, tag, listener);
}

void MegaApi::removeNodeTag(MegaNode* node, const char* tag, MegaRequestListener* listener)
{
    pImpl->removeNodeTag(node, tag, listener);
}

void MegaApi::updateNodeTag(MegaNode* node, const char* newTag, const char* oldTag, MegaRequestListener* listener)
{
    pImpl->updateNodeTag(node, newTag, oldTag, listener);
}

MegaStringList* MegaApi::getAllNodeTags(const char* pattern, MegaCancelToken* cancelToken)
{
    return getAllNodeTagsBelow(UNDEF, pattern, cancelToken);
}

MegaStringList* MegaApi::getAllNodeTagsBelow(const MegaNode* node,
                                             const char* pattern,
                                             MegaCancelToken* cancelToken)
{
    // Sanity.
    assert(node);

    // Caller's provided a sane node.
    if (node)
        return getAllNodeTagsBelow(node->getHandle(), pattern, cancelToken);

    // Caller didn't provide a sane node.
    return MegaStringList::createInstance();
}

MegaStringList* MegaApi::getAllNodeTagsBelow(MegaHandle handle,
                                             const char* pattern,
                                             MegaCancelToken* cancelToken)
{
    return pImpl->getAllNodeTagsBelow(handle,
                                      pattern ? pattern : "",
                                      convertToCancelToken(cancelToken));
}

void MegaApi::exportNode(MegaNode *node, int64_t expireTime, bool writable, bool megaHosted, MegaRequestListener *listener)
{
    pImpl->exportNode(node, expireTime, writable, megaHosted, listener);
}

void MegaApi::disableExport(MegaNode *node, MegaRequestListener *listener)
{
    pImpl->disableExport(node, listener);
}

void MegaApi::fetchNodes(MegaRequestListener *listener)
{
    pImpl->fetchNodes(listener);
}

void MegaApi::getCloudStorageUsed(MegaRequestListener *listener)
{
    pImpl->getCloudStorageUsed(listener);
}

void MegaApi::getAccountDetails(MegaRequestListener *listener)
{
    pImpl->getAccountDetails(true, true, true, false, false, false, -1, listener);
}

void MegaApi::getSpecificAccountDetails(bool storage, bool transfer, bool pro, int source, MegaRequestListener *listener)
{
    pImpl->getAccountDetails(storage, transfer, pro, false, false, false, source, listener);
}

void MegaApi::getExtendedAccountDetails(bool sessions, bool purchases, bool transactions, MegaRequestListener *listener)
{
    pImpl->getAccountDetails(false, false, false, sessions, purchases, transactions, -1, listener);
}

void MegaApi::queryTransferQuota(long long size, MegaRequestListener *listener)
{
    pImpl->queryTransferQuota(size, listener);
}

int64_t MegaApi::getOverquotaDeadlineTs()
{
    return pImpl->getOverquotaDeadlineTs();
}

MegaIntegerList* MegaApi::getOverquotaWarningsTs()
{
    return pImpl->getOverquotaWarningsTs();
}

void MegaApi::getPricing(MegaRequestListener *listener)
{
    pImpl->getPricing(listener);
}

void MegaApi::getPaymentId(MegaHandle productHandle, MegaRequestListener *listener)
{
    pImpl->getPaymentId(productHandle, UNDEF, AFFILIATE_TYPE_INVALID, 0, listener);
}

void MegaApi::upgradeAccount(MegaHandle productHandle, int paymentMethod, MegaRequestListener *listener)
{
    pImpl->upgradeAccount(productHandle, paymentMethod, listener);
}

void MegaApi::submitPurchaseReceipt(int gateway, const char *receipt, MegaRequestListener *listener)
{
    pImpl->submitPurchaseReceipt(gateway, receipt, UNDEF, AFFILIATE_TYPE_INVALID, 0, listener);
}

void MegaApi::submitPurchaseReceipt(int gateway, const char *receipt, MegaHandle lastPublicHandle, MegaRequestListener *listener)
{
    pImpl->submitPurchaseReceipt(gateway, receipt, lastPublicHandle, AFFILIATE_TYPE_INVALID, 0, listener);
}

void MegaApi::submitPurchaseReceipt(int gateway, const char *receipt, MegaHandle lastPublicHandle, int lastPublicHandleType, int64_t lastAccessTimestamp, MegaRequestListener *listener)
{
    pImpl->submitPurchaseReceipt(gateway, receipt, lastPublicHandle, lastPublicHandleType, lastAccessTimestamp, listener);
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

void MegaApi::creditCardCancelSubscriptions(const char* reason,
                                            const char* id,
                                            int canContact,
                                            MegaRequestListener* listener)
{
    pImpl->creditCardCancelSubscriptions(reason, id, canContact, listener);
}

void MegaApi::creditCardCancelSubscriptions(const MegaCancelSubscriptionReasonList* reasonList,
                                            const char* id,
                                            int canContact,
                                            MegaRequestListener* listener)
{
    pImpl->creditCardCancelSubscriptions(reasonList, id, canContact, listener);
}

void MegaApi::getPaymentMethods(MegaRequestListener* listener)
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

void MegaApi::passwordReminderDialogSucceeded(MegaRequestListener *listener)
{
    pImpl->updatePwdReminderData(true, false, false, false, false, listener);
}

void MegaApi::passwordReminderDialogSkipped(MegaRequestListener *listener)
{
    pImpl->updatePwdReminderData(false, true, false, false, false, listener);
}

void MegaApi::passwordReminderDialogBlocked(MegaRequestListener *listener)
{
    pImpl->updatePwdReminderData(false, false, false, true, false, listener);
}

void MegaApi::shouldShowPasswordReminderDialog(bool atLogout, MegaRequestListener *listener)
{
    pImpl->getUserAttr((const char*)NULL, MegaApi::USER_ATTR_PWD_REMINDER, NULL, atLogout, listener);
}

void MegaApi::isMasterKeyExported(MegaRequestListener *listener)
{
    pImpl->getUserAttr((const char*)NULL, MegaApi::USER_ATTR_PWD_REMINDER, NULL, 0, listener);
}

void MegaApi::getPushNotificationSettings(MegaRequestListener *listener)
{
    pImpl->getPushNotificationSettings(listener);
}

void MegaApi::setPushNotificationSettings(MegaPushNotificationSettings *settings, MegaRequestListener *listener)
{
    pImpl->setPushNotificationSettings(settings, listener);
}

#ifdef ENABLE_CHAT
void MegaApi::enableRichPreviews(bool enable, MegaRequestListener *listener)
{
    pImpl->enableRichPreviews(enable, listener);
}

void MegaApi::isRichPreviewsEnabled(MegaRequestListener *listener)
{
    pImpl->isRichPreviewsEnabled(listener);
}

void MegaApi::shouldShowRichLinkWarning(MegaRequestListener *listener)
{
    pImpl->shouldShowRichLinkWarning(listener);
}

void MegaApi::setRichLinkWarningCounterValue(int value, MegaRequestListener *listener)
{
    pImpl->setRichLinkWarningCounterValue(value, listener);
}

void MegaApi::enableGeolocation(MegaRequestListener *listener)
{
    pImpl->enableGeolocation(listener);
}

void MegaApi::isGeolocationEnabled(MegaRequestListener *listener)
{
    pImpl->isGeolocationEnabled(listener);
}

/* Class MegaScheduledMeeting */
MegaScheduledMeeting* MegaScheduledMeeting::createInstance(MegaHandle chatid, MegaHandle schedId, MegaHandle parentSchedId, MegaHandle organizerUserId,
                                                                   int cancelled, const char* timezone, MegaTimeStamp startDateTime,
                                                                   MegaTimeStamp endDateTime, const char* title, const char* description, const char* attributes,
                                                                   MegaTimeStamp overrides, MegaScheduledFlags* flags, MegaScheduledRules* rules)
{
    return new MegaScheduledMeetingPrivate(chatid, timezone, startDateTime, endDateTime, title,
                                               description, schedId, parentSchedId, organizerUserId, cancelled,
                                               attributes, overrides, flags, rules);
}

MegaScheduledMeeting::~MegaScheduledMeeting()                           {}
int MegaScheduledMeeting::cancelled() const                             { return 0; }
MegaHandle MegaScheduledMeeting::chatid() const                         { return INVALID_HANDLE; }
MegaHandle MegaScheduledMeeting::schedId() const                        { return INVALID_HANDLE; }
MegaHandle MegaScheduledMeeting::organizerUserid() const                { return INVALID_HANDLE; }
MegaHandle MegaScheduledMeeting::parentSchedId() const                  { return INVALID_HANDLE; }
MegaScheduledMeeting* MegaScheduledMeeting::copy() const                { return NULL; }
const char* MegaScheduledMeeting::timezone() const                      { return NULL; }
MegaTimeStamp MegaScheduledMeeting::startDateTime() const               { return MEGA_INVALID_TIMESTAMP; }
MegaTimeStamp MegaScheduledMeeting::endDateTime() const                 { return MEGA_INVALID_TIMESTAMP; }
const char* MegaScheduledMeeting::title() const                         { return NULL; }
const char* MegaScheduledMeeting::description() const                   { return NULL; }
const char* MegaScheduledMeeting::attributes() const                    { return NULL; }
MegaTimeStamp MegaScheduledMeeting::overrides() const                   { return MEGA_INVALID_TIMESTAMP; }
MegaScheduledRules* MegaScheduledMeeting::rules() const                 { return NULL; }
MegaScheduledFlags* MegaScheduledMeeting::flags() const                 { return NULL; }

/* class MegaScheduledFlags */
MegaScheduledFlags* MegaScheduledFlags::createInstance()
{
    return new MegaScheduledFlagsPrivate();
}

void MegaScheduledFlags::importFlagsValue(unsigned long /*val*/)
{
}

MegaScheduledFlags* MegaScheduledFlags::copy() const
{
    return NULL;
}

MegaScheduledFlags::~MegaScheduledFlags()
{
}

void MegaScheduledFlags::reset()                                {}
bool MegaScheduledFlags::isEmpty() const                        { return false; }
unsigned long MegaScheduledFlags::getNumericValue() const       { return ScheduledFlags::schedEmptyFlags; }

/* Class MegaScheduledRules */
MegaScheduledRules* MegaScheduledRules::createInstance(int freq,
                               int interval,
                               MegaTimeStamp until,
                               const ::mega::MegaIntegerList* byWeekDay,
                               const ::mega::MegaIntegerList* byMonthDay,
                               const ::mega::MegaIntegerMap* byMonthWeekDay)
{
    return new MegaScheduledRulesPrivate(freq, interval, until, byWeekDay, byMonthDay, byMonthWeekDay);
}

MegaScheduledRules::~MegaScheduledRules()                               {}
MegaScheduledRules* MegaScheduledRules::copy() const                    { return NULL; }
int MegaScheduledRules::freq() const                                    { return 0; }
int MegaScheduledRules::interval() const                                { return 0; }
MegaTimeStamp MegaScheduledRules::until() const                         { return MEGA_INVALID_TIMESTAMP; }
const mega::MegaIntegerList* MegaScheduledRules::byWeekDay() const      { return nullptr; }
const mega::MegaIntegerList* MegaScheduledRules::byMonthDay() const     { return nullptr; }
const mega::MegaIntegerMap* MegaScheduledRules::byMonthWeekDay() const  { return nullptr; }
bool MegaScheduledRules::isValidFreq(int freq)                          { return MegaScheduledRulesPrivate::isValidFreq(freq);}
bool MegaScheduledRules::isValidInterval(int interval)                  { return MegaScheduledRulesPrivate::isValidInterval(interval);}

/* Class MegaScheduledMeetingList */
MegaScheduledMeetingList* MegaScheduledMeetingList::createInstance()
{
    return new MegaScheduledMeetingListPrivate();
}

MegaScheduledMeetingList::~MegaScheduledMeetingList()
{

}

MegaScheduledMeetingList* MegaScheduledMeetingList::copy() const                        { return NULL; }
unsigned long MegaScheduledMeetingList::size() const                                    { return 0; }
MegaScheduledMeeting* MegaScheduledMeetingList::at(unsigned long) const                 { return NULL; }
MegaScheduledMeeting* MegaScheduledMeetingList::getBySchedId(MegaHandle) const          { return NULL; }
void MegaScheduledMeetingList::insert(MegaScheduledMeeting*)                            {}
void MegaScheduledMeetingList::clear()                                                  {}
#endif

void MegaApi::setCameraUploadsFolder(MegaHandle nodehandle, MegaRequestListener *listener)
{
    pImpl->setCameraUploadsFolder(nodehandle, false, listener);
}

void MegaApi::setCameraUploadsFolders(MegaHandle primaryFolder, MegaHandle secondaryFolder, MegaRequestListener *listener)
{
    pImpl->setCameraUploadsFolders(primaryFolder, secondaryFolder, listener);
}

void MegaApi::getCameraUploadsFolder(MegaRequestListener *listener)
{
    pImpl->getCameraUploadsFolder(false, listener);
}

void MegaApi::getCameraUploadsFolderSecondary(MegaRequestListener *listener)
{
    pImpl->getCameraUploadsFolder(true, listener);
}

void MegaApi::setMyChatFilesFolder(MegaHandle nodehandle, MegaRequestListener *listener)
{
    pImpl->setMyChatFilesFolder(nodehandle, listener);
}

void MegaApi::getMyChatFilesFolder(MegaRequestListener *listener)
{
    pImpl->getMyChatFilesFolder(listener);
}

void MegaApi::setMyBackupsFolder(const char *localizedName, MegaRequestListener *listener)
{
    pImpl->setMyBackupsFolder(localizedName, listener);
}

void MegaApi::getUserAlias(MegaHandle uh, MegaRequestListener *listener)
{
    pImpl->getUserAlias(uh, listener);
}

void MegaApi::setUserAlias(MegaHandle uh, const char *alias, MegaRequestListener *listener)
{
    pImpl->setUserAlias(uh, alias, listener);
}

void MegaApi::getRubbishBinAutopurgePeriod(MegaRequestListener *listener)
{
    pImpl->getRubbishBinAutopurgePeriod(listener);
}

void MegaApi::setRubbishBinAutopurgePeriod(int days, MegaRequestListener *listener)
{
    pImpl->setRubbishBinAutopurgePeriod(days, listener);
}

const char* MegaApi::getDeviceId() const
{
    return pImpl->getDeviceId();
}

void MegaApi::getDeviceName(const char *deviceId, MegaRequestListener *listener)
{
    pImpl->getDeviceName(deviceId, listener);
}

void MegaApi::setDeviceName(const char *deviceId, const char *deviceName, MegaRequestListener *listener)
{
    pImpl->setDeviceName(deviceId, deviceName, listener);
}

void MegaApi::getDriveName(const char *pathToDrive, MegaRequestListener *listener)
{
    pImpl->getDriveName(pathToDrive, listener);
}

void MegaApi::setDriveName(const char *pathToDrive, const char *driveName, MegaRequestListener *listener)
{
    pImpl->setDriveName(pathToDrive, driveName, listener);
}

void MegaApi::changePassword(const char *oldPassword, const char *newPassword, MegaRequestListener *listener)
{
    pImpl->changePassword(oldPassword, newPassword, listener);
}
#ifdef ENABLE_SYNC
void MegaApi::logout(bool keepSyncConfigsFile, MegaRequestListener *listener)
{
    pImpl->logout(keepSyncConfigsFile, listener);
}
#else
void MegaApi::logout(MegaRequestListener *listener)
{
    pImpl->logout(false, listener);
}
#endif

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

char* MegaApi::generateRandomCharsPassword(bool uU, bool uD, bool uS, unsigned int l)
{
    return MegaApiImpl::generateRandomCharsPassword(uU, uD, uS, l);
}

void MegaApi::submitFeedback(int rating, const char* comment, MegaRequestListener* listener)
{
    pImpl->submitFeedback(rating,
                          comment,
                          false /*transferFeedback*/,
                          TRANSFER_STATS_BOTH,
                          listener);
}

void MegaApi::submitFeedbackForTransfers(int rating,
                                         const char* comment,
                                         int transferType,
                                         MegaRequestListener* listener)
{
    pImpl->submitFeedback(rating, comment, true /*transferFeedback*/, transferType, listener);
}

void MegaApi::sendEvent(int eventType, const char *message, bool addJourneyId, const char *viewId, MegaRequestListener *listener)
{
    pImpl->sendEvent(eventType, message, addJourneyId, viewId, listener);
}

void MegaApi::createSupportTicket(const char *message, int type, MegaRequestListener *listener)
{
    pImpl->createSupportTicket(message, type, listener);
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
    pImpl->inviteContact(email, message, action, UNDEF, listener);
}

void MegaApi::inviteContact(const char *email, const char *message, int action, MegaHandle contactLink, MegaRequestListener *listener)
{
    pImpl->inviteContact(email, message, action, contactLink, listener);
}

void MegaApi::replyContactRequest(const MegaContactRequest* r,
                                  int action,
                                  MegaRequestListener* listener)
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

bool MegaApi::areTransfersPaused(int direction)
{
    return pImpl->areTransfersPaused(direction);
}

//-1 -> AUTO, 0 -> NONE, >0 -> b/s
void MegaApi::setUploadLimit(int /*bpslimit*/) {}

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

MegaTransfer* MegaApi::getTransferByUniqueId(uint32_t transferUniqueId) const
{
    return pImpl->getTransferByUniqueId(transferUniqueId);
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

void MegaApi::startUploadForSupport(const char* localPath, bool isSourceTemporary, MegaTransferListener *listener)
{
    pImpl->startUploadForSupport(localPath, isSourceTemporary, FS_UNKNOWN, listener);
}

MegaStringList *MegaApi::getBackupFolders(int backuptag) const
{
    return pImpl->getBackupFolders(backuptag);
}

void MegaApi::setScheduledCopy(const char* localPath, MegaNode* parent, bool attendPastBackups, int64_t period, const char *periodstring, int numBackups, MegaRequestListener *listener)
{
    pImpl->setScheduledCopy(localPath, parent, attendPastBackups, period, periodstring ? periodstring : "", numBackups, listener);
}

void MegaApi::removeScheduledCopy(int tag, MegaRequestListener *listener)
{
    pImpl->removeScheduledCopy(tag, listener);
}

void MegaApi::abortCurrentScheduledCopy(int tag, MegaRequestListener *listener)
{
    pImpl->abortCurrentScheduledCopy(tag, listener);
}

void MegaApi::startTimer( int64_t period, MegaRequestListener *listener)
{
    pImpl->startTimer(period, listener);
}

void MegaApi::startUpload(const char *localPath, MegaNode *parent, const char *fileName, int64_t mtime, const char *appData,  bool isSourceTemporary, bool startFirst, MegaCancelToken *cancelToken, MegaTransferListener *listener)
{
    pImpl->startUpload(startFirst, localPath, parent, fileName, NULL /*targetUser*/, mtime,
                       0 /*folderTransferTag*/, false /*isBackup*/, appData, isSourceTemporary,
                       false /*forceNewUpload*/, FS_UNKNOWN, convertToCancelToken(cancelToken), listener);
}

void MegaApi::startUploadForChat(const char *localPath, MegaNode *parent, const char *appData, bool isSourceTemporary, const char* fileName, MegaTransferListener *listener)
{
    pImpl->startUpload(true /*startFirst*/, localPath, parent, fileName, NULL /*targetUser*/, INVALID_CUSTOM_MOD_TIME /*mtime*/,
                       0 /*folderTransferTag*/, false /*isBackup*/, appData, isSourceTemporary,
                       true /*forceNewUpload*/, FS_UNKNOWN, CancelToken(), listener);
}

void MegaApi::startDownload(MegaNode* node, const char* localPath, const char *customName, const char *appData, bool startFirst, MegaCancelToken *cancelToken, int collisionCheck, int collisionResolution, bool undelete, MegaTransferListener *listener)
{
    pImpl->startDownload(startFirst, node, localPath, customName, 0 /*folderTransferTag*/, appData, convertToCancelToken(cancelToken), collisionCheck, collisionResolution, undelete, listener);
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

void MegaApi::setStreamingMinimumRate(int bytesPerSecond)
{
    pImpl->setStreamingMinimumRate(bytesPerSecond);
}

#ifdef ENABLE_SYNC

int MegaApi::syncPathState(string* path)
{
    return pImpl->syncPathState(path);
}

MegaNode *MegaApi::getSyncedNode(string *path)
{
    return pImpl->getSyncedNode(LocalPath::fromPlatformEncodedAbsolute(*path));
}

void MegaApi::syncFolder(const MegaSync::SyncType syncType,
                         const char* localSyncRootFolder,
                         const char* name,
                         const MegaHandle remoteSyncRootFolder,
                         const char* driveRootIfExternal,
                         MegaRequestListener* const listener,
                         const char* /* excludePath */)
{
    syncFolder(syncType,
               std::string(nullToEmpty(localSyncRootFolder)),
               std::string(nullToEmpty(name)),
               remoteSyncRootFolder,
               std::string(nullToEmpty(driveRootIfExternal)),
               listener);
}

void MegaApi::syncFolder(const MegaSync::SyncType syncType,
                         const std::string& localSyncRootFolder,
                         const std::string& name,
                         const MegaHandle remoteSyncRootFolder,
                         const std::string& driveRootIfExternal,
                         MegaRequestListener* const listener)
{
    MegaRequestSyncFolderParams params{localSyncRootFolder,
                                       name,
                                       remoteSyncRootFolder,
                                       static_cast<SyncConfig::Type>(syncType),
                                       driveRootIfExternal};

    pImpl->syncFolder(std::move(params), listener);
}

void MegaApi::prevalidateSyncFolder(const MegaSync::SyncType syncType,
                                    const std::string& localSyncRootFolder,
                                    const std::string& name,
                                    const MegaHandle remoteSyncRootFolder,
                                    const std::string& driveRootIfExternal,
                                    MegaRequestListener* const listener)
{
    MegaRequestSyncFolderParams params{localSyncRootFolder,
                                       name,
                                       remoteSyncRootFolder,
                                       static_cast<SyncConfig::Type>(syncType),
                                       driveRootIfExternal};

    pImpl->prevalidateSyncFolder(std::move(params), listener);
}

void MegaApi::loadExternalBackupSyncsFromExternalDrive(const char* externalDriveRoot, MegaRequestListener* listener)
{
    pImpl->loadExternalBackupSyncsFromExternalDrive(externalDriveRoot, listener);
}

void MegaApi::closeExternalBackupSyncsFromExternalDrive(const char* externalDriveRoot, MegaRequestListener* listener)
{
    pImpl->closeExternalBackupSyncsFromExternalDrive(externalDriveRoot, listener);
}

void MegaApi::copySyncDataToCache(const char *localFolder, const char *name, MegaHandle megaHandle, const char *remotePath,
                                  long long localfp, bool enabled, bool temporaryDisabled, MegaRequestListener *listener)
{
    pImpl->copySyncDataToCache(localFolder, name, megaHandle, remotePath, localfp, enabled, temporaryDisabled, listener);
}

void MegaApi::copySyncDataToCache(const char *localFolder, MegaHandle megaHandle, const char *remotePath,
                                  long long localfp, bool enabled, bool temporaryDisabled, MegaRequestListener *listener)
{
    pImpl->copySyncDataToCache(localFolder, nullptr, megaHandle, remotePath, localfp, enabled, temporaryDisabled, listener);
}

void MegaApi::copyCachedStatus(int storageStatus, int blockStatus, int businessStatus, MegaRequestListener *listener)
{
    pImpl->copyCachedStatus(storageStatus, blockStatus, businessStatus, listener);
}

void MegaApi::removeSync(MegaHandle backupId, MegaRequestListener *listener)
{
    pImpl->removeSyncById(backupId, listener);
}

void MegaApi::setSyncRunState(MegaHandle backupId, MegaSync::SyncRunningState targetState, MegaRequestListener *listener)
{
    pImpl->setSyncRunState(backupId, targetState, listener);
}

void MegaApi::rescanSync(MegaHandle backupId, bool reFingerprint)
{
    pImpl->rescanSync(backupId, reFingerprint);
}

void MegaApi::importSyncConfigs(const char* configs, MegaRequestListener* listener)
{
    pImpl->importSyncConfigs(configs, listener);
}

const char* MegaApi::exportSyncConfigs()
{
    return pImpl->exportSyncConfigs();
}

MegaSyncList* MegaApi::getSyncs()
{
   return pImpl->getSyncs();
}

long long MegaApi::getNumLocalNodes()
{
    return pImpl->getNumLocalNodes();
}

void MegaApi::getMegaSyncStallList(MegaRequestListener* listener)
{
    pImpl->getMegaSyncStallList(listener);
}

void MegaApi::getMegaSyncStallMap(MegaRequestListener* listener)
{
    pImpl->getMegaSyncStallMap(listener);
}

void MegaApi::clearStalledPath(MegaSyncStall* stall)
{
    pImpl->clearStalledPath(stall);
}

void MegaApi::moveToDebris(const char* path, MegaHandle syncBackupId, MegaRequestListener* listener)
{
    pImpl->moveToDebris(path, syncBackupId, listener);
}

void MegaApi::changeSyncRemoteRoot(const MegaHandle syncBackupId,
                                   const MegaHandle newRootNodeHandle,
                                   MegaRequestListener* listener)
{
    pImpl->changeSyncRemoteRoot(syncBackupId, newRootNodeHandle, listener);
}

void MegaApi::changeSyncLocalRoot(const MegaHandle syncBackupId,
                                  const char* newLocalSyncRootPath,
                                  MegaRequestListener* listener)
{
    pImpl->changeSyncLocalRoot(syncBackupId, newLocalSyncRootPath, listener);
}

void MegaApi::setSyncUploadThrottleUpdateRate(const unsigned updateRateInSeconds,
                                              MegaRequestListener* const listener)
{
    pImpl->setSyncUploadThrottleUpdateRate(updateRateInSeconds, listener);
}

void MegaApi::setSyncMaxUploadsBeforeThrottle(const unsigned maxUploadsBeforeThrottle,
                                              MegaRequestListener* const listener)
{
    pImpl->setSyncMaxUploadsBeforeThrottle(maxUploadsBeforeThrottle, listener);
}

void MegaApi::getSyncUploadThrottleValues(MegaRequestListener* const listener)
{
    pImpl->getSyncUploadThrottleValues(listener);
}

void MegaApi::getSyncUploadThrottleLowerLimits(MegaRequestListener* const listener)
{
    const bool upperLimits = false;
    pImpl->getSyncUploadThrottleLimits(upperLimits, listener);
}

void MegaApi::getSyncUploadThrottleUpperLimits(MegaRequestListener* const listener)
{
    const bool upperLimits = true;
    pImpl->getSyncUploadThrottleLimits(upperLimits, listener);
}

void MegaApi::checkSyncUploadsThrottled(MegaRequestListener* const listener)
{
    pImpl->checkSyncUploadsThrottled(listener);
}

MegaSync *MegaApi::getSyncByBackupId(MegaHandle backupId)
{
    return pImpl->getSyncByBackupId(backupId);
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
    return pImpl->isScanning();
}

bool MegaApi::isSyncing()
{
    return pImpl->isSyncing();
}

int MegaApi::isNodeSyncable(MegaNode *node)
{
    return pImpl->isNodeSyncable(node);
}

MegaError *MegaApi::isNodeSyncableWithError(MegaNode* node) {
    return pImpl->isNodeSyncableWithError(node);
}

void MegaApi::setLegacyExcludedNames(vector<string> *excludedNames)
{
    pImpl->setLegacyExcludedNames(excludedNames);
}

void MegaApi::setLegacyExcludedPaths(vector<string> *excludedPaths)
{
    pImpl->setLegacyExcludedPaths(excludedPaths);
}

void MegaApi::setLegacyExclusionLowerSizeLimit(unsigned long long limit)
{
    pImpl->setLegacyExclusionLowerSizeLimit(limit);
}

void MegaApi::setLegacyExclusionUpperSizeLimit(unsigned long long limit)
{
    pImpl->setLegacyExclusionUpperSizeLimit(limit);
}

MegaError* MegaApi::exportLegacyExclusionRules(const char* absolutePath)
{
    return pImpl->exportLegacyExclusionRules(absolutePath);
}
#endif


void MegaApi::moveOrRemoveDeconfiguredBackupNodes(MegaHandle deconfiguredBackupRoot, MegaHandle backupDestination, MegaRequestListener *listener)
{
    pImpl->moveOrRemoveDeconfiguredBackupNodes(deconfiguredBackupRoot, backupDestination, listener);
}

MegaScheduledCopy *MegaApi::getScheduledCopyByTag(int tag)
{
    return pImpl->getScheduledCopyByTag(tag);
}

MegaScheduledCopy *MegaApi::getScheduledCopyByNode(MegaNode *node)
{
    return pImpl->getScheduledCopyByNode(node);
}

MegaScheduledCopy *MegaApi::getScheduledCopyByPath(const char *localPath)
{
    return pImpl->getScheduledCopyByPath(localPath);
}

MegaNode *MegaApi::getRootNode()
{
    return pImpl->getRootNode();
}

MegaNode *MegaApi::getVaultNode()
{
    return pImpl->getVaultNode();
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

bool MegaApi::isSensitiveInherited(MegaNode* node)
{
    return pImpl->isSensitiveInherited(node);
}

bool MegaApi::isInVault(MegaNode *node)
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

MegaUserAlertList* MegaApi::getUserAlerts()
{
    return pImpl->getUserAlerts();
}

int MegaApi::getNumUnreadUserAlerts()
{
    return pImpl->getNumUnreadUserAlerts();
}

MegaNodeList* MegaApi::getInShares(MegaUser *megaUser, int order)
{
    return pImpl->getInShares(megaUser, order);
}

MegaNodeList* MegaApi::getInShares(int order)
{
    return pImpl->getInShares(order);
}

MegaShareList* MegaApi::getInSharesList(int order)
{
    return pImpl->getInSharesList(order);
}

MegaUser *MegaApi::getUserFromInShare(MegaNode *node, bool recurse)
{
    return pImpl->getUserFromInShare(node, recurse);
}

bool MegaApi::isPendingShare(MegaNode *node)
{
    return pImpl->isPendingShare(node);
}

MegaShareList *MegaApi::getOutShares(int order)
{
    return pImpl->getOutShares(order);
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

bool MegaApi::isPrivateNode(MegaHandle handle)
{
    return pImpl->isPrivateNode(handle);
}

bool MegaApi::isForeignNode(MegaHandle handle)
{
    return pImpl->isForeignNode(handle);
}

MegaNodeList *MegaApi::getPublicLinks(int order)
{
    return pImpl->getPublicLinks(order);
}

MegaContactRequestList* MegaApi::getIncomingContactRequests() const
{
    return pImpl->getIncomingContactRequests();
}

MegaContactRequestList* MegaApi::getOutgoingContactRequests() const
{
    return pImpl->getOutgoingContactRequests();
}

int MegaApi::getAccess(MegaNode* megaNode)
{
    return pImpl->getAccess(megaNode);
}

void MegaApi::getRecentActionsAsync(unsigned days, unsigned maxnodes, MegaRequestListener *listener)
{
    pImpl->getRecentActionsAsync(days, maxnodes, listener);
}

void MegaApi::getRecentActionsAsync(unsigned days,
                                    unsigned maxnodes,
                                    bool excludeSensitives,
                                    MegaRequestListener* listener)
{
    pImpl->getRecentActionsAsync(days, maxnodes, excludeSensitives, listener);
}

bool MegaApi::processMegaTree(MegaNode* n, MegaTreeProcessor* processor, bool recursive)
{
    return pImpl->processMegaTree(n, processor, recursive);
}

MegaNode *MegaApi::createForeignFileNode(MegaHandle handle, const char *key,
                                    const char *name, int64_t size, int64_t mtime, const char* fingerprintCrc,
                                        MegaHandle parentHandle, const char *privateAuth, const char *publicAuth, const char *chatAuth)
{
    return pImpl->createForeignFileNode(handle, key, name, size, mtime, fingerprintCrc, parentHandle, privateAuth, publicAuth, chatAuth);
}

void MegaApi::getLastAvailableVersion(const char *appKey, MegaRequestListener *listener)
{
    pImpl->getLastAvailableVersion(appKey, listener);
}

void MegaApi::getLocalSSLCertificate(MegaRequestListener *listener)
{
    pImpl->getLocalSSLCertificate(listener);
}

void MegaApi::queryDNS(const char *hostname, MegaRequestListener *listener)
{
    pImpl->queryDNS(hostname, listener);
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

#ifdef ENABLE_CHAT
MegaNode *MegaApi::authorizeChatNode(MegaNode *node, const char *cauth)
{
    return pImpl->authorizeChatNode(node, cauth);
}
#endif

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

void MegaApi::disableGfxFeatures(bool disable)
{
    pImpl->disableGfxFeatures(disable);
}

bool MegaApi::areGfxFeaturesDisabled()
{
    return pImpl->areGfxFeaturesDisabled();
}

void MegaApi::changeApiUrl(const char *apiURL, bool disablepkp)
{
    pImpl->changeApiUrl(apiURL, disablepkp);
}

bool MegaApi::setLanguage(const char *languageCode)
{
    return pImpl->setLanguage(languageCode);
}

const char* MegaApi::generateViewId()
{
    return strdup(pImpl->generateViewId().c_str());
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

void MegaApi::setContactLinksOption(bool enable, MegaRequestListener* listener)
{
    pImpl->setContactLinksOption(enable, listener);
}

void MegaApi::getFileVersionsOption(MegaRequestListener *listener)
{
    pImpl->getFileVersionsOption(listener);
}

void MegaApi::getContactLinksOption(MegaRequestListener *listener)
{
    pImpl->getContactLinksOption(listener);
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

    unsigned binarylen = unsigned(strlen(base64) * 3/4 + 4);
    byte *binary = new byte[binarylen];
    binarylen = static_cast<unsigned>(Base64::atob(base64, binary, static_cast<int>(binarylen)));

    char *result = new char[binarylen * 8/5 + 6];
    Base32::btoa(binary, static_cast<int>(binarylen), result);
    delete [] binary;

    return result;
}

char *MegaApi::base32ToBase64(const char *base32)
{
    if(!base32)
    {
        return NULL;
    }

    unsigned binarylen = unsigned(strlen(base32) * 5/8 + 8);
    byte *binary = new byte[binarylen];
    binarylen = static_cast<unsigned>(Base32::atob(base32, binary, static_cast<int>(binarylen)));

    char *result = new char[binarylen * 4/3 + 4];
    Base64::btoa(binary, static_cast<int>(binarylen), result);
    delete [] binary;

    return result;
}

MegaNodeList* MegaApi::search(const MegaSearchFilter* filter, int order, MegaCancelToken* cancelToken, const MegaSearchPage* searchPage)
{
    return pImpl->search(filter, order, convertToCancelToken(cancelToken), searchPage);
}

long long MegaApi::getSize(MegaNode *n)
{
    return pImpl->getSize(n);
}

char *MegaApi::getFingerprint(const char *filePath)
{
    return pImpl->getFingerprint(filePath);
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

MegaNodeList *MegaApi::getNodesByOriginalFingerprint(const char* originalFingerprint, MegaNode* parent)
{
    return pImpl->getNodesByOriginalFingerprint(originalFingerprint, parent);
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

void MegaApi::addScheduledCopyListener(MegaScheduledCopyListener *listener)
{
    pImpl->addScheduledCopyListener(listener);
}

bool MegaApi::removeScheduledCopyListener(MegaScheduledCopyListener *listener)
{
    return pImpl->removeScheduledCopyListener(listener);
}

bool MegaApi::removeListener(MegaListener* listener)
{
    return pImpl->removeListener(listener);
}

bool MegaApi::removeRequestListener(MegaRequestListener* listener)
{
    return pImpl->removeRequestListener(listener);
}

bool MegaApi::removeTransferListener(MegaTransferListener* listener)
{
    return pImpl->removeTransferListener(listener);
}

bool MegaApi::removeGlobalListener(MegaGlobalListener* listener)
{
    return pImpl->removeGlobalListener(listener);
}

MegaError *MegaApi::checkAccessErrorExtended(MegaNode *node, int level)
{
    return pImpl->checkAccessErrorExtended(node, level);
}

MegaError *MegaApi::checkMoveErrorExtended(MegaNode *node, MegaNode *target)
{
    return pImpl->checkMoveErrorExtended(node, target);
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

MegaNodeList *MegaApi::getChildren(const MegaSearchFilter* filter, int order, MegaCancelToken* cancelToken, const MegaSearchPage* searchPage)
{
    return pImpl->getChildren(filter, order, convertToCancelToken(cancelToken), searchPage);
}

MegaNodeList *MegaApi::getChildren(MegaNode* p, int order, MegaCancelToken* cancelToken)
{
    return pImpl->getChildren(p, order, convertToCancelToken(cancelToken));
}

MegaNodeList *MegaApi::getChildren(MegaNodeList *parentNodes, int order)
{
    return pImpl->getChildren(parentNodes, order);
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

void MegaApi::getFolderInfo(MegaNode *node, MegaRequestListener *listener)
{
    pImpl->getFolderInfo(node, listener);
}

bool MegaApi::hasChildren(MegaNode *parent)
{
    return pImpl->hasChildren(parent);
}

MegaNode *MegaApi::getChildNode(MegaNode *parent, const char* name)
{
    return pImpl->getChildNode(parent, name);
}

MegaNode* MegaApi::getChildNodeOfType(MegaNode *parent, const char *name, int type)
{
    return pImpl->getChildNodeOfType(parent, name, type);
}

MegaNode* MegaApi::getParentNode(MegaNode* n)
{
    return pImpl->getParentNode(n);
}

char *MegaApi::getNodePath(MegaNode *node)
{
    return pImpl->getNodePath(node);
}

char *MegaApi::getNodePathByNodeHandle(MegaHandle handle)
{
    return pImpl->getNodePathByNodeHandle(handle);
}

MegaNode* MegaApi::getNodeByPath(const char *path, MegaNode* node)
{
    return pImpl->getNodeByPath(path, node);
}

MegaNode* MegaApi::getNodeByPathOfType(const char* path, MegaNode* n, int type)
{
    return pImpl->getNodeByPathOfType(path, n, type);
}

MegaNode* MegaApi::getNodeByHandle(uint64_t h)
{
    return pImpl->getNodeByHandle(h);
}

MegaTotpTokenGenResult MegaApi::generateTotpTokenFromNode(MegaHandle handle)
{
    return pImpl->generateTotpTokenFromNode(handle);
}

MegaContactRequest *MegaApi::getContactRequestByHandle(MegaHandle handle)
{
    return pImpl->getContactRequestByHandle(handle);
}

unsigned long long MegaApi::getNumNodes()
{
    return pImpl->getNumNodes();
}

unsigned long long MegaApi::getAccurateNumNodes()
{
    return pImpl->getAccurateNumNodes();
}

void MegaApi::setLRUCacheSize(unsigned long long size)
{
    pImpl->setLRUCacheSize(size);
}

unsigned long long MegaApi::getNumNodesAtCacheLRU() const
{
    return pImpl->getNumNodesAtCacheLRU();
}

int MegaApi::isWaiting()
{
    return pImpl->isWaiting();
}

bool MegaApi::isSyncStalled()
{
    return pImpl->isSyncStalled();
}

bool MegaApi::isSyncStalledChanged()
{
    return pImpl->isSyncStalledChanged();
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

void MegaApi::catchup(MegaRequestListener *listener)
{
    pImpl->catchup(listener);
}

void MegaApi::getPublicLinkInformation(const char *megaFolderLink, MegaRequestListener *listener)
{
    pImpl->getPublicLinkInformation(megaFolderLink, listener);
}

MegaApiLock* MegaApi::getMegaApiLock(bool lockNow)
{
    return new MegaApiLock(pImpl, lockNow);
}

bool MegaApi::platformSetRLimitNumFile(int newNumFileLimit) const
{
    return mega::platformSetRLimitNumFile(newNumFileLimit);
}

int MegaApi::platformGetRLimitNumFile() const
{
    return mega::platformGetRLimitNumFile();
}

void MegaApi::sendSMSVerificationCode(const char* phoneNumber, MegaRequestListener *listener, bool reverifying_whitelisted)
{
    pImpl->sendSMSVerificationCode(phoneNumber, listener, reverifying_whitelisted);
}

void MegaApi::checkSMSVerificationCode(const char* verificationCode, MegaRequestListener *listener)
{
    pImpl->checkSMSVerificationCode(verificationCode, listener);
}

void MegaApi::getCountryCallingCodes(MegaRequestListener *listener)
{
    pImpl->getCountryCallingCodes(listener);
}


#ifdef HAVE_LIBUV
bool MegaApi::httpServerStart(bool localOnly, int port, bool useTLS, const char * certificatepath, const char * keypath, bool useIPv6)
{
    return pImpl->httpServerStart(localOnly, port, useTLS, certificatepath, keypath, useIPv6);
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

void MegaApi::httpServerEnableOfflineAttribute(bool enable)
{
    pImpl->httpServerEnableOfflineAttribute(enable);
}

bool MegaApi::httpServerIsOfflineAttributeEnabled()
{
    return pImpl->httpServerIsOfflineAttributeEnabled();
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

char *MegaApi::httpServerGetLocalWebDavLink(MegaNode *node)
{
    return pImpl->httpServerGetLocalWebDavLink(node);
}

MegaStringList *MegaApi::httpServerGetWebDavLinks()
{
    return pImpl->httpServerGetWebDavLinks();
}

MegaNodeList *MegaApi::httpServerGetWebDavAllowedNodes()
{
    return pImpl->httpServerGetWebDavAllowedNodes();
}

void MegaApi::httpServerRemoveWebDavAllowedNode(MegaHandle handle)
{
    pImpl->httpServerRemoveWebDavAllowedNode(handle);
}

void MegaApi::httpServerRemoveWebDavAllowedNodes()
{
    pImpl->httpServerRemoveWebDavAllowedNodes();
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

//FTP Server:
bool MegaApi::ftpServerStart(bool localOnly, int port, int dataportBegin, int dataPortEnd, bool useTLS, const char * certificatepath, const char * keypath)
{
    return pImpl->ftpServerStart(localOnly, port, dataportBegin, dataPortEnd, useTLS, certificatepath, keypath);
}

void MegaApi::ftpServerStop()
{
    pImpl->ftpServerStop();
}

int MegaApi::ftpServerIsRunning()
{
    return pImpl->ftpServerIsRunning();
}

bool MegaApi::ftpServerIsLocalOnly()
{
    return pImpl->ftpServerIsLocalOnly();
}

void MegaApi::ftpServerSetRestrictedMode(int mode)
{
    pImpl->ftpServerSetRestrictedMode(mode);
}

int MegaApi::ftpServerGetRestrictedMode()
{
    return pImpl->ftpServerGetRestrictedMode();
}

void MegaApi::ftpServerAddListener(MegaTransferListener *listener)
{
    pImpl->ftpServerAddListener(listener);
}

void MegaApi::ftpServerRemoveListener(MegaTransferListener *listener)
{
    pImpl->ftpServerRemoveListener(listener);
}

char *MegaApi::ftpServerGetLocalLink(MegaNode *node)
{
    return pImpl->ftpServerGetLocalLink(node);
}

MegaStringList *MegaApi::ftpServerGetLinks()
{
    return pImpl->ftpServerGetLinks();
}

MegaNodeList *MegaApi::ftpServerGetAllowedNodes()
{
    return pImpl->ftpServerGetAllowedNodes();
}

void MegaApi::ftpServerRemoveAllowedNode(MegaHandle handle)
{
    pImpl->ftpServerRemoveAllowedNode(handle);
}

void MegaApi::ftpServerRemoveAllowedNodes()
{
    pImpl->ftpServerRemoveAllowedNodes();
}

void MegaApi::ftpServerSetMaxBufferSize(int bufferSize)
{
    pImpl->ftpServerSetMaxBufferSize(bufferSize);
}

int MegaApi::ftpServerGetMaxBufferSize()
{
    return pImpl->ftpServerGetMaxBufferSize();
}

void MegaApi::ftpServerSetMaxOutputSize(int outputSize)
{
    pImpl->ftpServerSetMaxOutputSize(outputSize);
}

int MegaApi::ftpServerGetMaxOutputSize()
{
    return pImpl->ftpServerGetMaxOutputSize();
}

#endif

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

    static const map<string, string> mimeMap{
        // list copied from extmime in Webclient's filetypes.js
        // from c++11, this sort of static local initialization is one-time and thread safe
        {"3ds", "image/x-3ds"},
        {"3g2", "video/3gpp2"},
        {"3gp", "video/3gpp"},
        {"7z", "application/x-7z-compressed"},
        {"aac", "audio/x-aac"},
        {"abw", "application/x-abiword"},
        {"ace", "application/x-ace-compressed"},
        {"adp", "audio/adpcm"},
        {"aif", "audio/x-aiff"},
        {"aifc", "audio/x-aiff"},
        {"aiff", "audio/x-aiff"},
        {"apk", "application/vnd.android.package-archive"},
        {"asf", "video/x-ms-asf"},
        {"asx", "video/x-ms-asf"},
        {"atom", "application/atom+xml"},
        {"au", "audio/basic"},
        {"avi", "video/x-msvideo"},
        {"avif", "image/avif"},
        {"bat", "application/x-msdownload"},
        {"bmp", "image/bmp"},
        {"btif", "image/prs.btif"},
        {"bz2", "application/x-bzip2"},
        {"caf", "audio/x-caf"},
        {"cgm", "image/cgm"},
        {"cmx", "image/x-cmx"},
        {"com", "application/x-msdownload"},
        {"conf", "text/plain"},
        {"css", "text/css"},
        {"csv", "text/csv"},
        {"dbk", "application/docbook+xml"},
        {"deb", "application/x-debian-package"},
        {"def", "text/plain"},
        {"djv", "image/vnd.djvu"},
        {"djvu", "image/vnd.djvu"},
        {"dll", "application/x-msdownload"},
        {"dmg", "application/x-apple-diskimage"},
        {"doc", "application/msword"},
        {"docm", "application/vnd.ms-word.document.macroenabled.12"},
        {"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {"dot", "application/msword"},
        {"dotm", "application/vnd.ms-word.template.macroenabled.12"},
        {"dotx", "application/vnd.openxmlformats-officedocument.wordprocessingml.template"},
        {"dra", "audio/vnd.dra"},
        {"dtd", "application/xml-dtd"},
        {"dts", "audio/vnd.dts"},
        {"dtshd", "audio/vnd.dts.hd"},
        {"dvb", "video/vnd.dvb.file"},
        {"dwg", "image/vnd.dwg"},
        {"dxf", "image/vnd.dxf"},
        {"ecelp4800", "audio/vnd.nuera.ecelp4800"},
        {"ecelp7470", "audio/vnd.nuera.ecelp7470"},
        {"ecelp9600", "audio/vnd.nuera.ecelp9600"},
        {"emf", "application/x-msmetafile"},
        {"emz", "application/x-msmetafile"},
        {"eol", "audio/vnd.digital-winds"},
        {"epub", "application/epub+zip"},
        {"exe", "application/x-msdownload"},
        {"f4v", "video/x-f4v"},
        {"fbs", "image/vnd.fastbidsheet"},
        {"fh", "image/x-freehand"},
        {"fh4", "image/x-freehand"},
        {"fh5", "image/x-freehand"},
        {"fh7", "image/x-freehand"},
        {"fhc", "image/x-freehand"},
        {"flac", "audio/x-flac"},
        {"fli", "video/x-fli"},
        {"flv", "video/x-flv"},
        {"fpx", "image/vnd.fpx"},
        {"fst", "image/vnd.fst"},
        {"fvt", "video/vnd.fvt"},
        {"g3", "image/g3fax"},
        {"gif", "image/gif"},
        {"gz", "application/x-gzip"},
        {"h261", "video/h261"},
        {"h263", "video/h263"},
        {"h264", "video/h264"},
        {"heic", "image/heic"},
        {"heif", "image/heif"},
        {"htm", "text/html"},
        {"html", "text/html"},
        {"ico", "image/x-icon"},
        {"ief", "image/ief"},
        {"iso", "application/x-iso9660-image"},
        {"jpe", "image/jpeg"},
        {"jpeg", "image/jpeg"},
        {"jpg", "image/jpeg"},
        {"jpgm", "video/jpm"},
        {"jpgv", "video/jpeg"},
        {"jpm", "video/jpm"},
        {"json", "application/json"},
        {"jsonml", "application/jsonml+json"},
        {"jxl", "image/jxl"},
        {"kar", "audio/midi"},
        {"ktx", "image/ktx"},
        {"list", "text/plain"},
        {"log", "text/plain"},
        {"lvp", "audio/vnd.lucent.voice"},
        {"m13", "application/x-msmediaview"},
        {"m14", "application/x-msmediaview"},
        {"m1v", "video/mpeg"},
        {"m21", "application/mp21"},
        {"m2a", "audio/mpeg"},
        {"m2v", "video/mpeg"},
        {"m3a", "audio/mpeg"},
        {"m3u", "audio/x-mpegurl"},
        {"m3u8", "application/vnd.apple.mpegurl"},
        {"m4a", "audio/mp4"},
        {"m4u", "video/vnd.mpegurl"},
        {"m4v", "video/x-m4v"},
        {"mdi", "image/vnd.ms-modi"},
        {"mid", "audio/midi"},
        {"midi", "audio/midi"},
        {"mj2", "video/mj2"},
        {"mjp2", "video/mj2"},
        {"mk3d", "video/x-matroska"},
        {"mka", "audio/x-matroska"},
        {"mks", "video/x-matroska"},
        {"mkv", "video/x-matroska"},
        {"mmr", "image/vnd.fujixerox.edmics-mmr"},
        {"mng", "video/x-mng"},
        {"mov", "video/quicktime"},
        {"movie", "video/x-sgi-movie"},
        {"mp2", "audio/mpeg"},
        {"mp21", "application/mp21"},
        {"mp2a", "audio/mpeg"},
        {"mp3", "audio/mpeg"},
        {"mp4", "video/mp4"},
        {"mp4a", "audio/mp4"},
        {"mp4s", "application/mp4"},
        {"mp4v", "video/mp4"},
        {"mpe", "video/mpeg"},
        {"mpeg", "video/mpeg"},
        {"mpg", "video/mpeg"},
        {"mpg4", "video/mp4"},
        {"mpga", "audio/mpeg"},
        {"mpkg", "application/vnd.apple.installer+xml"},
        {"msi", "application/x-msdownload"},
        {"mvb", "application/x-msmediaview"},
        {"mxf", "application/mxf"},
        {"mxml", "application/xv+xml"},
        {"mxu", "video/vnd.mpegurl"},
        {"npx", "image/vnd.net-fpx"},
        {"odb", "application/vnd.oasis.opendocument.database"},
        {"odc", "application/vnd.oasis.opendocument.chart"},
        {"odf", "application/vnd.oasis.opendocument.formula"},
        {"odft", "application/vnd.oasis.opendocument.formula-template"},
        {"odg", "application/vnd.oasis.opendocument.graphics"},
        {"odi", "application/vnd.oasis.opendocument.image"},
        {"odm", "application/vnd.oasis.opendocument.text-master"},
        {"odp", "application/vnd.oasis.opendocument.presentation"},
        {"ods", "application/vnd.oasis.opendocument.spreadsheet"},
        {"odt", "application/vnd.oasis.opendocument.text"},
        {"oga", "audio/ogg"},
        {"ogg", "audio/ogg"},
        {"ogv", "video/ogg"},
        {"ogx", "application/ogg"},
        {"otc", "application/vnd.oasis.opendocument.chart-template"},
        {"otf", "application/octet-stream"},
        {"otg", "application/vnd.oasis.opendocument.graphics-template"},
        {"oth", "application/vnd.oasis.opendocument.text-web"},
        {"oti", "application/vnd.oasis.opendocument.image-template"},
        {"otp", "application/vnd.oasis.opendocument.presentation-template"},
        {"ots", "application/vnd.oasis.opendocument.spreadsheet-template"},
        {"ott", "application/vnd.oasis.opendocument.text-template"},
        {"oxt", "application/vnd.openofficeorg.extension"},
        {"pbm", "image/x-portable-bitmap"},
        {"pct", "image/x-pict"},
        {"pcx", "image/x-pcx"},
        {"pdf", "application/pdf"},
        {"pgm", "image/x-portable-graymap"},
        {"pic", "image/x-pict"},
        {"plb", "application/vnd.3gpp.pic-bw-large"},
        {"png", "image/png"},
        {"pnm", "image/x-portable-anymap"},
        {"pot", "application/vnd.ms-powerpoint"},
        {"potx", "application/vnd.openxmlformats-officedocument.presentationml.template"},
        {"ppm", "image/x-portable-pixmap"},
        {"pps", "application/vnd.ms-powerpoint"},
        {"ppsx", "application/vnd.openxmlformats-officedocument.presentationml.slideshow"},
        {"ppt", "application/vnd.ms-powerpoint"},
        {"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
        {"psb", "application/vnd.3gpp.pic-bw-small"},
        {"psd", "image/vnd.adobe.photoshop"},
        {"pvb", "application/vnd.3gpp.pic-bw-var"},
        {"pya", "audio/vnd.ms-playready.media.pya"},
        {"pyv", "video/vnd.ms-playready.media.pyv"},
        {"qt", "video/quicktime"},
        {"ra", "audio/x-pn-realaudio"},
        {"ram", "audio/x-pn-realaudio"},
        {"rar", "application/octet-stream"},
        {"ras", "image/x-cmu-raster"},
        {"rgb", "image/x-rgb"},
        {"rip", "audio/vnd.rip"},
        {"rlc", "image/vnd.fujixerox.edmics-rlc"},
        {"rmi", "audio/midi"},
        {"rmp", "audio/x-pn-realaudio-plugin"},
        {"s3m", "audio/s3m"},
        {"sgi", "image/sgi"},
        {"sgm", "text/sgml"},
        {"sgml", "text/sgml"},
        {"sh", "application/x-sh"},
        {"sid", "image/x-mrsid-image"},
        {"sil", "audio/silk"},
        {"sldx", "application/vnd.openxmlformats-officedocument.presentationml.slide"},
        {"smv", "video/x-smv"},
        {"snd", "audio/basic"},
        {"spx", "audio/ogg"},
        {"srt", "application/x-subrip"},
        {"sub", "text/vnd.dvb.subtitle"},
        {"svg", "image/svg+xml"},
        {"svgz", "image/svg+xml"},
        {"swf", "application/x-shockwave-flash"},
        {"tar", "application/x-tar"},
        {"tcap", "application/vnd.3gpp2.tcap"},
        {"text", "text/plain"},
        {"tga", "image/x-tga"},
        {"tif", "image/tiff"},
        {"tiff", "image/tiff"},
        {"torrent", "application/x-bittorrent"},
        {"tsv", "text/tab-separated-values"},
        {"ttf", "application/octet-stream"},
        {"ttl", "text/turtle"},
        {"txt", "text/plain"},
        {"udeb", "application/x-debian-package"},
        {"uva", "audio/vnd.dece.audio"},
        {"uvg", "image/vnd.dece.graphic"},
        {"uvh", "video/vnd.dece.hd"},
        {"uvi", "image/vnd.dece.graphic"},
        {"uvm", "video/vnd.dece.mobile"},
        {"uvp", "video/vnd.dece.pd"},
        {"uvs", "video/vnd.dece.sd"},
        {"uvu", "video/vnd.uvvu.mp4"},
        {"uvv", "video/vnd.dece.video"},
        {"uvva", "audio/vnd.dece.audio"},
        {"uvvg", "image/vnd.dece.graphic"},
        {"uvvh", "video/vnd.dece.hd"},
        {"uvvi", "image/vnd.dece.graphic"},
        {"uvvm", "video/vnd.dece.mobile"},
        {"uvvp", "video/vnd.dece.pd"},
        {"uvvs", "video/vnd.dece.sd"},
        {"uvvu", "video/vnd.uvvu.mp4"},
        {"uvvv", "video/vnd.dece.video"},
        {"viv", "video/vnd.vivo"},
        {"vob", "video/x-ms-vob"},
        {"wav", "audio/x-wav"},
        {"wax", "audio/x-ms-wax"},
        {"wbmp", "image/vnd.wap.wbmp"},
        {"wdp", "image/vnd.ms-photo"},
        {"weba", "audio/webm"},
        {"webm", "video/webm"},
        {"webp", "image/webp"},
        {"wm", "video/x-ms-wm"},
        {"wma", "audio/x-ms-wma"},
        {"wmf", "application/x-msmetafile"},
        {"wmv", "video/x-ms-wmv"},
        {"wmx", "video/x-ms-wmx"},
        {"wvx", "video/x-ms-wvx"},
        {"xap", "application/x-silverlight-app"},
        {"xbm", "image/x-xbitmap"},
        {"xht", "application/xhtml+xml"},
        {"xhtml", "application/xhtml+xml"},
        {"xhvml", "application/xv+xml"},
        {"xif", "image/vnd.xiff"},
        {"xls", "application/vnd.ms-excel"},
        {"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {"xltx", "application/vnd.openxmlformats-officedocument.spreadsheetml.template"},
        {"xm", "audio/xm"},
        {"xml", "application/xml"},
        {"xop", "application/xop+xml"},
        {"xpl", "application/xproc+xml"},
        {"xpm", "image/x-xpixmap"},
        {"xsl", "application/xml"},
        {"xslt", "application/xslt+xml"},
        {"xspf", "application/xspf+xml"},
        {"xvm", "application/xv+xml"},
        {"xvml", "application/xv+xml"},
        {"xwd", "image/x-xwindowdump"},
        {"zip", "application/zip"},

        // RAW Images
        {"3fr", "image/x-hasselblad-3fr"},
        {"ari", "image/z-arrialexa-ari"},
        {"arq", "image/x-sony-arq"},
        {"arw", "image/x-sony-arw"},
        {"bay", "image/x-casio-bay"},
        {"bmq", "image/x-nucore-bmq"},
        {"cap", "image/x-phaseone-cap"},
        {"cr2", "image/x-canon-cr2"},
        {"cr3", "image/x-canon-cr3"},
        {"crw", "image/x-canon-crw"},
        {"cs1", "image/x-sinar-cs1"},
        {"dc2", "image/x-kodak-dc2"},
        {"dcr", "image/x-kodak-dcr"},
        {"dng", "image/x-dcraw"},
        {"dsc", "image/x-kodak-dsc"},
        {"drf", "image/x-kodak-drf"},
        {"eip", "image/x-phaseone-eip"},
        {"erf", "image/x-epson-erf"},
        {"fff", "image/x-hasselblad-fff"},
        {"iiq", "image/x-phaseone-iiq"},
        {"k25", "image/x-kodak-k25"},
        {"kc2", "image/x-kodak-kc2"},
        {"kdc", "image/x-kodak-kdc"},
        {"mdc", "image/x-monolta-mdc"},
        {"mef", "image/x-mamiya-mef"},
        {"mos", "image/x-leaf-mos"},
        {"mrw", "image/x-minolta-mrw"},
        {"nef", "image/x-nikon-nef"},
        {"nrw", "image/x-nikon-nrw"},
        {"obm", "image/x-olympus-obm"},
        {"orf", "image/x-olympus-orf"},
        {"ori", "image/x-olympus-ori"},
        {"pef", "image/x-pentax-pef"},
        {"ptx", "image/x-pentax-ptx"},
        {"pxn", "image/x-logitech-pxn"},
        {"qtk", "image/x-apple-qtx"},
        {"raf", "image/x-fuji-raf"},
        {"raw", "image/x-panasonic-raw"},
        {"rdc", "image/x-difoma-rdc"},
        {"rw2", "image/x-panasonic-rw2"},
        {"rwl", "image/x-leica-rwl"},
        {"rwz", "image/x-rawzor-rwz"},
        {"sr2", "image/x-sony-sr2"},
        {"srf", "image/x-sony-srf"},
        {"srw", "image/x-samsung-srw"},
        {"sti", "image/x-sinar-sti"},
        {"x3f", "image/x-sigma-x3f"},
        {"ciff", "image/x-canon-crw"},
        {"cine", "image/x-phantom-cine"},
        {"ia", "image/x-sinar-ia"},

        // Uncommon Images
        {"aces", "image/aces"},
        {"avci", "image/avci"},
        {"avcs", "image/avcs"},
        {"fits", "image/fits"},
        {"g3fax", "image/g3fax"},
        {"hej2k", "image/hej2k"},
        {"hsj2", "image/hsj2"},
        {"jls", "image/jls"},
        {"jp2", "image/jp2"},
        {"jph", "image/jph"},
        {"jphc", "image/jphc"},
        {"jpx", "image/jpx"},
        {"jxr", "image/jxr"},
        {"jxrA", "image/jxrA"},
        {"jxrS", "image/jxrS"},
        {"jxs", "image/jxs"},
        {"jxsc", "image/jxsc"},
        {"jxsi", "image/jxsi"},
        {"jxss", "image/jxss"},
        {"naplps", "image/naplps"},
        {"pti", "image/prs.pti"},
        {"t38", "image/t38"}
    };

    string key = extension;
    tolower_string(key);
    map<string, string>::const_iterator it = mimeMap.find(key);
    return it == mimeMap.cend() ? nullptr : MegaApi::strdup(it->second.c_str());
}

#ifdef ENABLE_CHAT
void MegaApi::createChat(bool group, MegaTextChatPeerList* peers, const char* title, int chatOptions, const MegaScheduledMeeting* scheduledMeeting, MegaRequestListener* listener)
{
    pImpl->createChat(group, false, peers, NULL, title, false, chatOptions, scheduledMeeting, listener);
}

void MegaApi::createPublicChat(MegaTextChatPeerList* peers, const MegaStringMap* userKeyMap, const char* title, bool meetingRoom, int chatOptions, const MegaScheduledMeeting* scheduledMeeting, MegaRequestListener* listener)
{
    pImpl->createChat(true, true, peers, userKeyMap, title, meetingRoom, chatOptions, scheduledMeeting, listener);
}

void MegaApi::setChatOption(MegaHandle chatid, int option, bool enabled, MegaRequestListener* listener)
{
     pImpl->setChatOption(chatid, option, enabled, listener);
}

void MegaApi::createOrUpdateScheduledMeeting(const MegaScheduledMeeting* scheduledMeeting, const char* chatTitle, MegaRequestListener* listener)
{
   pImpl->createOrUpdateScheduledMeeting(scheduledMeeting, chatTitle, listener);
}

void MegaApi::removeScheduledMeeting(MegaHandle chatid, MegaHandle schedId, MegaRequestListener* listener)
{
    pImpl->removeScheduledMeeting(chatid, schedId, listener);
}

void MegaApi::fetchScheduledMeeting(MegaHandle chatid, MegaHandle schedId, MegaRequestListener* listener)
{
    pImpl->fetchScheduledMeeting(chatid, schedId, listener);
}

void MegaApi::fetchScheduledMeetingEvents(MegaHandle chatid, MegaTimeStamp since, MegaTimeStamp until, unsigned int count, MegaRequestListener* listener)
{
    pImpl->fetchScheduledMeetingEvents(chatid, since, until, count, listener);
}

void MegaApi::inviteToChat(MegaHandle chatid,  MegaHandle uh, int privilege, const char *title, MegaRequestListener *listener)
{
    pImpl->inviteToChat(chatid, uh, privilege, false, NULL, title, listener);
}

void MegaApi::inviteToPublicChat(MegaHandle chatid, MegaHandle uh, int privilege, const char *unifiedKey, MegaRequestListener *listener)
{
    pImpl->inviteToChat(chatid, uh, privilege, true, unifiedKey, NULL, listener);
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

void MegaApi::sendChatStats(const char *data, int port, MegaRequestListener *listener)
{
    pImpl->sendChatStats(data, port, listener);
}

void MegaApi::sendChatLogs(const char *data, MegaHandle userid, MegaHandle callid, int port, MegaRequestListener *listener)
{
    pImpl->sendChatLogs(data, userid, callid, port, listener);
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

void MegaApi::archiveChat(MegaHandle chatid, int archive, MegaRequestListener *listener)
{
    pImpl->archiveChat(chatid, archive, listener);
}

void MegaApi::setChatRetentionTime(MegaHandle chatid, unsigned period, MegaRequestListener *listener)
{
    pImpl->setChatRetentionTime(chatid, period, listener);
}

void MegaApi::requestRichPreview(const char *url, MegaRequestListener *listener)
{
    pImpl->requestRichPreview(url, listener);
}

void MegaApi::chatLinkQuery(MegaHandle chatid, MegaRequestListener *listener)
{
    pImpl->chatLinkHandle(chatid, false, false, listener);
}

void MegaApi::chatLinkCreate(MegaHandle chatid, MegaRequestListener *listener)
{
    pImpl->chatLinkHandle(chatid, false, true, listener);
}

void MegaApi::chatLinkDelete(MegaHandle chatid, MegaRequestListener *listener)
{
    pImpl->chatLinkHandle(chatid, true, false, listener);
}

void MegaApi::getChatLinkURL(MegaHandle publichandle, MegaRequestListener *listener)
{
    pImpl->getChatLinkURL(publichandle, listener);
}

void MegaApi::chatLinkClose(MegaHandle chatid, const char *title, MegaRequestListener *listener)
{
    pImpl->chatLinkClose(chatid, title, listener);
}

void MegaApi::chatLinkJoin(MegaHandle publichandle, const char *unifiedKey, MegaRequestListener *listener)
{
    pImpl->chatLinkJoin(publichandle, unifiedKey, listener);
}

bool MegaApi::isChatNotifiable(MegaHandle chatid)
{
    return pImpl->isChatNotifiable(chatid);
}

void MegaApi::startChatCall(MegaHandle chatid, bool notRinging, MegaRequestListener* listener)
{
    pImpl->startChatCall(chatid, notRinging, listener);
}

void MegaApi::joinChatCall(MegaHandle chatid, MegaHandle callid, MegaRequestListener *listener)
{
    pImpl->joinChatCall(chatid, callid, listener);
}

void MegaApi::endChatCall(MegaHandle chatid, MegaHandle callid, int reason, MegaRequestListener *listener)
{
    pImpl->endChatCall(chatid, callid, reason, listener);
}

void MegaApi::ringIndividualInACall(MegaHandle chatid, MegaHandle userid, MegaRequestListener* listener)
{
    pImpl->ringIndividualInACall(chatid, userid, listener);
}

void MegaApi::setSFUid(int sfuid)
{
    pImpl->setSFUid(sfuid);
}
#endif

bool MegaApi::isSharesNotifiable()
{
    return pImpl->isSharesNotifiable();
}

bool MegaApi::isContactsNotifiable()
{
    return pImpl->isContactsNotifiable();
}

char* MegaApi::strdup(const char* buffer)
{
    if (!buffer)
        return NULL;
    int tam = int(strlen(buffer) + 1);
    char* newbuffer = new char[static_cast<size_t>(tam)];
    memcpy(newbuffer, buffer, static_cast<size_t>(tam));
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
        int(utf8string->size() + 1),
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

    int size = int(strlen(utf8data) + 1);

    // make space for the worst case
    utf16string->resize(size * sizeof(wchar_t));

    // resize to actual result
    utf16string->resize(sizeof(wchar_t) * MultiByteToWideChar(CP_UTF8, 0, utf8data, size, (wchar_t*)utf16string->data(),
                                                              int(utf16string->size() / sizeof(wchar_t) + 1)));
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

char *MegaApi::escapeFsIncompatible(const char *filename, const char *dstPath)
{
    return pImpl->escapeFsIncompatible(filename, dstPath);
}

char *MegaApi::unescapeFsIncompatible(const char *name, const char *localPath)
{
    return pImpl->unescapeFsIncompatible(name, localPath);
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

void MegaApi::backgroundMediaUploadRequestUploadURL(int64_t fullFileSize, MegaBackgroundMediaUpload* state, MegaRequestListener *listener)
{
    return pImpl->backgroundMediaUploadRequestUploadURL(fullFileSize, state, listener);
}

void MegaApi::completeUpload(const char* utf8Name, MegaNode *parent, const char* fingerprint, const char* fingerprintoriginal,
                                  const char *string64UploadToken, const char *string64FileKey,  MegaRequestListener *listener)
{
    return pImpl->completeUpload(utf8Name, parent, fingerprint, fingerprintoriginal, string64UploadToken, string64FileKey, listener);
}


void MegaApi::getUploadURL(int64_t fullFileSize, bool forceSSL, MegaRequestListener *listener)
{
    return pImpl->getUploadURL(fullFileSize, forceSSL, listener);
}

void MegaApi::getThumbnailUploadURL(MegaHandle nodeHandle, int64_t fullFileSize, bool forceSSL, MegaRequestListener *listener)
{
    return pImpl->getFileAttributeUploadURL(nodeHandle, fullFileSize, GfxProc::THUMBNAIL, forceSSL, listener);
}

void MegaApi::getPreviewUploadURL(MegaHandle nodeHandle, int64_t fullFileSize, bool forceSSL, MegaRequestListener *listener)
{
    return pImpl->getFileAttributeUploadURL(nodeHandle, fullFileSize, GfxProc::PREVIEW, forceSSL, listener);
}

void MegaApi::backgroundMediaUploadComplete(MegaBackgroundMediaUpload* state, const char* utf8Name, MegaNode *parent, const char* fingerprint, const char* fingerprintoriginal,
    const char *string64UploadToken, MegaRequestListener *listener)
{
    pImpl->backgroundMediaUploadComplete(state, utf8Name, parent, fingerprint, fingerprintoriginal, string64UploadToken, listener);
}

bool MegaApi::ensureMediaInfo()
{
    return pImpl->ensureMediaInfo();
}

void MegaApi::setOriginalFingerprint(MegaNode* node, const char* originalFingerprint, MegaRequestListener *listener)
{
    return pImpl->setOriginalFingerprint(node, originalFingerprint, listener);
}

void MegaApi::getBanners(MegaRequestListener *listener)
{
    pImpl->getBanners(listener);
}

void MegaApi::dismissBanner(int id, MegaRequestListener *listener)
{
    pImpl->dismissBanner(id, listener);
}

void MegaApi::setBackup(int backupType, MegaHandle targetNode, const char* localFolder, const char* backupName, int state, int subState, MegaRequestListener* listener)
{
    pImpl->setBackup(backupType, targetNode, localFolder, backupName, state, subState, listener);
}

void MegaApi::updateBackup(MegaHandle backupId, int backupType, MegaHandle targetNode, const char* localFolder,  const char* backupName, int state, int subState, MegaRequestListener* listener)
{
    pImpl->updateBackup(backupId, backupType, targetNode, localFolder, backupName, state, subState, listener);
}

void MegaApi::removeBackup(MegaHandle backupId, MegaRequestListener *listener)
{
    pImpl->removeBackup(backupId, listener);
}

void MegaApi::removeFromBC(MegaHandle backupId, MegaHandle moveDestination, MegaRequestListener* listener)
{
    pImpl->removeFromBC(backupId, moveDestination, listener);
}

void MegaApi::pauseFromBC(MegaHandle backupId, MegaRequestListener* listener)
{
    pImpl->pauseFromBC(backupId, listener);
}

void MegaApi::resumeFromBC(MegaHandle backupId, MegaRequestListener* listener)
{
    pImpl->resumeFromBC(backupId, listener);
}

void MegaApi::getBackupInfo(MegaRequestListener* listener)
{
    pImpl->getBackupInfo(listener);
}

void MegaApi::sendBackupHeartbeat(MegaHandle backupId, int status, int progress, int ups, int downs, long long ts, MegaHandle lastNode, MegaRequestListener *listener)
{
    pImpl->sendBackupHeartbeat(backupId, status, progress, ups, downs, ts, lastNode, listener);
}

void MegaApi::fetchAds(int adFlags, MegaStringList *adUnits, MegaHandle publicHandle, MegaRequestListener *listener)
{
    pImpl->fetchAds(adFlags, adUnits, publicHandle, listener);
}

void MegaApi::queryAds(int adFlags, MegaHandle publicHandle, MegaRequestListener *listener)
{
    pImpl->queryAds(adFlags, publicHandle, listener);
}

void MegaApi::setCookieSettings(int settings, MegaRequestListener *listener)
{
    pImpl->setCookieSettings(settings, listener);
}

void MegaApi::getCookieSettings(MegaRequestListener *listener)
{
    pImpl->getCookieSettings(listener);
}

bool MegaApi::cookieBannerEnabled()
{
    return pImpl->cookieBannerEnabled();
}

bool MegaApi::startDriveMonitor()
{
    return pImpl->startDriveMonitor();
}

void MegaApi::stopDriveMonitor()
{
    pImpl->stopDriveMonitor();
}

bool MegaApi::driveMonitorEnabled()
{
    return pImpl->driveMonitorEnabled();
}

void MegaApi::createSet(const char* name, int type, MegaRequestListener* listener)
{
    int options = CREATE_SET | (name ? OPTION_SET_NAME : 0);
    pImpl->putSet(INVALID_HANDLE, options, name, INVALID_HANDLE, type, listener);
}

void MegaApi::updateSetName(MegaHandle sid, const char* name, MegaRequestListener* listener)
{
    pImpl->putSet(sid, OPTION_SET_NAME, name, INVALID_HANDLE, MegaSet::SET_TYPE_IGNORE, listener);
}

void MegaApi::putSetCover(MegaHandle sid, MegaHandle eid, MegaRequestListener* listener)
{
    pImpl->putSet(sid, OPTION_SET_COVER, nullptr, eid, MegaSet::SET_TYPE_IGNORE, listener);
}

void MegaApi::removeSet(MegaHandle sid, MegaRequestListener* listener)
{
    pImpl->removeSet(sid, listener);
}

void MegaApi::createSetElements(MegaHandle sid, const MegaHandleList* nodes, const MegaStringList* names, MegaRequestListener* listener)
{
    pImpl->putSetElements(sid, nodes, names, listener);
}

void MegaApi::createSetElement(MegaHandle sid, MegaHandle node, const char* name, MegaRequestListener* listener)
{
    int options = CREATE_ELEMENT | (name ? OPTION_ELEMENT_NAME : 0);
    pImpl->putSetElement(sid, INVALID_HANDLE, node, options, 0, name, listener);
}

void MegaApi::updateSetElementOrder(MegaHandle sid, MegaHandle eid, int64_t order, MegaRequestListener* listener)
{
    pImpl->putSetElement(sid, eid, INVALID_HANDLE, OPTION_ELEMENT_ORDER, order, nullptr, listener);
}

void MegaApi::updateSetElementName(MegaHandle sid, MegaHandle eid, const char* name, MegaRequestListener* listener)
{
    pImpl->putSetElement(sid, eid, INVALID_HANDLE, OPTION_ELEMENT_NAME, 0, name, listener);
}

void MegaApi::removeSetElements(MegaHandle sid, const MegaHandleList* eids, MegaRequestListener* listener)
{
    pImpl->removeSetElements(sid, eids, listener);
}

void MegaApi::removeSetElement(MegaHandle sid, MegaHandle eid, MegaRequestListener* listener)
{
    pImpl->removeSetElement(sid, eid, listener);
}

MegaSetList* MegaApi::getSets()
{
    return pImpl->getSets();
}

MegaSet* MegaApi::getSet(MegaHandle sid)
{
    return pImpl->getSet(sid);
}

MegaHandle MegaApi::getSetCover(MegaHandle sid)
{
    return pImpl->getSetCover(sid);
}

unsigned MegaApi::getSetElementCount(MegaHandle sid, bool includeElementsInRubbishBin)
{
    return pImpl->getSetElementCount(sid, includeElementsInRubbishBin);
}

MegaSetElementList* MegaApi::getSetElements(MegaHandle sid, bool includeElementsInRubbishBin)
{
    return pImpl->getSetElements(sid, includeElementsInRubbishBin);
}

MegaSetElement* MegaApi::getSetElement(MegaHandle sid, MegaHandle eid)
{
    return pImpl->getSetElement(sid, eid);
}

bool MegaApi::isExportedSet(MegaHandle sid)
{
    return pImpl->isExportedSet(sid);
}

void MegaApi::exportSet(MegaHandle sid, MegaRequestListener *listener)
{
    return pImpl->exportSet(sid, listener);
}

void MegaApi::disableExportSet(MegaHandle sid, MegaRequestListener *listener)
{
    return pImpl->disableExportSet(sid, listener);
}

int MegaApi::getSetElementHandleSize()
{
    return MegaApiImpl::getSetElementHandleSize();
}

void MegaApi::fetchPublicSet(const char* publicSetLink, MegaRequestListener* listener)
{
    pImpl->fetchPublicSet(publicSetLink, listener);
}

void MegaApi::stopPublicSetPreview()
{
    return pImpl->stopPublicSetPreview();
}

bool MegaApi::inPublicSetPreview()
{
    return pImpl->inPublicSetPreview();
}

MegaSet* MegaApi::getPublicSetInPreview()
{
    return pImpl->getPublicSetInPreview();
}


MegaSetElementList* MegaApi::getPublicSetElementsInPreview()
{
    return pImpl->getPublicSetElementsInPreview();
}

void MegaApi::getPreviewElementNode(MegaHandle eid, MegaRequestListener* listener)
{
    return pImpl->getPreviewElementNode(eid, listener);
}

const char* MegaApi::getPublicLinkForExportedSet(MegaHandle sid)
{
    return pImpl->getPublicLinkForExportedSet(sid);
}

void MegaApi::enableRequestStatusMonitor(bool enable)
{
    return pImpl->enableRequestStatusMonitor(enable);
}

bool MegaApi::requestStatusMonitorEnabled()
{
    return pImpl->requestStatusMonitorEnabled();
}

void MegaApi::addMount(const MegaMount* mount, MegaRequestListener* listener)
{
    assert(listener);
    assert(mount);

    pImpl->addMount(mount, listener);
}

void MegaApi::disableMount(const char* path,
                           MegaRequestListener* listener,
                           bool remember)
{
    assert(listener);
    assert(path);

    pImpl->disableMount(path, listener, remember);
}

void MegaApi::enableMount(const char* path,
                          MegaRequestListener* listener,
                          bool remember)
{
    assert(listener);
    assert(path);

    pImpl->enableMount(path, listener, remember);
}

MegaFuseFlags* MegaApi::getFUSEFlags()
{
    return pImpl->getFUSEFlags();
}

MegaMountFlags* MegaApi::getMountFlags(const char* path)
{
    assert(path);

    return pImpl->getMountFlags(path);
}

MegaMount* MegaApi::getMountInfo(const char* path)
{
    assert(path);

    return pImpl->getMountInfo(path);
}

char* MegaApi::getMountPath(const char* name)
{
    assert(name);

    return pImpl->getMountPath(name);
}

MegaMountList* MegaApi::listMounts(bool enabled)
{
    return pImpl->listMounts(enabled);
}

bool MegaApi::isCachedByPath(const char* path)
{
    assert(path);

    return pImpl->isCached(path);
}

bool MegaApi::isFUSESupported()
{
    return pImpl->isFUSESupported();
}

bool MegaApi::isMountEnabled(const char* path)
{
    assert(path);

    return pImpl->isMountEnabled(path);
}

void MegaApi::removeMount(const char* path, MegaRequestListener* listener)
{
    assert(listener);
    assert(path);

    pImpl->removeMount(path, listener);
}

void MegaApi::setFUSEFlags(const MegaFuseFlags* flags)
{
    assert(flags);

    pImpl->setFUSEFlags(*flags);
}

void MegaApi::setMountFlags(const MegaMountFlags* flags,
                            const char* path,
                            MegaRequestListener* listener)
{
    assert(flags);
    assert(path);
    assert(listener);

    pImpl->setMountFlags(flags, path, listener);
}

void MegaApi::getVpnRegions(MegaRequestListener* listener)
{
    pImpl->getVpnRegions(listener);
}

void MegaApi::getVpnCredentials(MegaRequestListener* listener)
{
    pImpl->getVpnCredentials(listener);
}

void MegaApi::putVpnCredential(const char* region, MegaRequestListener* listener)
{
    pImpl->putVpnCredential(region, listener);
}

void MegaApi::delVpnCredential(int slotID, MegaRequestListener* listener)
{
    pImpl->delVpnCredential(slotID, listener);
}

void MegaApi::checkVpnCredential(const char* userPubKey, MegaRequestListener* listener)
{
    pImpl->checkVpnCredential(userPubKey, listener);
}

void MegaApi::fetchCreditCardInfo(MegaRequestListener* listener)
{
    pImpl->fetchCreditCardInfo(listener);
}

void MegaApi::getVisibleWelcomeDialog(MegaRequestListener* listener)
{
    pImpl->getVisibleWelcomeDialog(listener);
}

void MegaApi::setVisibleWelcomeDialog(bool visible, MegaRequestListener* listener)
{
    pImpl->setVisibleWelcomeDialog(visible, listener);
}

void MegaApi::getVisibleTermsOfService(MegaRequestListener* listener)
{
    pImpl->getVisibleTermsOfService(listener);
}

void MegaApi::setVisibleTermsOfService(bool visible, MegaRequestListener* listener)
{
    pImpl->setVisibleTermsOfService(visible, listener);
}

void MegaApi::createNodeTree(const MegaNode* parentNode,
                             MegaNodeTree* nodeTree,
                             MegaRequestListener* listener)
{
    pImpl->createNodeTree(parentNode, nodeTree, nullptr, listener);
}

void MegaApi::createNodeTree(const MegaNode* parentNode,
                             MegaNodeTree* nodeTree,
                             const char* customerIpPort,
                             MegaRequestListener* listener)
{
    pImpl->createNodeTree(parentNode, nodeTree, customerIpPort, listener);
}

MegaIntegerList* MegaApi::getEnabledNotifications()
{
    return pImpl->getEnabledNotifications();
}

void MegaApi::enableTestNotifications(const MegaIntegerList* notificationIds, MegaRequestListener* listener)
{
    pImpl->enableTestNotifications(notificationIds, listener);
}

void MegaApi::getNotifications(MegaRequestListener* listener)
{
    pImpl->getNotifications(listener);
}

void MegaApi::setLastReadNotification(uint32_t notificationId, MegaRequestListener* listener)
{
    pImpl->setLastReadNotification(notificationId, listener);
}

void MegaApi::getLastReadNotification(MegaRequestListener* listener)
{
    pImpl->getLastReadNotification(listener);
}

void MegaApi::setLastActionedBanner(uint32_t notificationId, MegaRequestListener* listener)
{
    pImpl->setLastActionedBanner(notificationId, listener);
}

void MegaApi::getLastActionedBanner(MegaRequestListener* listener)
{
    pImpl->getLastActionedBanner(listener);
}

MegaFlag* MegaApi::getFlag(const char* flagName, bool commit, MegaRequestListener* listener)
{
    return pImpl->getFlag(flagName, commit, listener);
}

MegaFlag* MegaApi::getFlag(const char* flagName, bool commit)
{
    return pImpl->getFlag(flagName, commit);
}

void MegaApi::deleteUserAttribute(int type, MegaRequestListener* listener)
{
    return pImpl->deleteUserAttribute(type, listener);
}

void MegaApi::getActiveSurveyTriggerActions(MegaRequestListener* listener)
{
    return pImpl->getActiveSurveyTriggerActions(listener);
}

void MegaApi::getSurvey(unsigned int triggerActionId, MegaRequestListener* listener)
{
    return pImpl->getSurvey(triggerActionId, listener);
}

void MegaApi::enableTestSurveys(const MegaHandleList* surveyHandles, MegaRequestListener* listener)
{
    return pImpl->enableTestSurveys(surveyHandles, listener);
}

void MegaApi::answerSurvey(MegaHandle surveyHandle,
                           unsigned int triggerActionId,
                           const char* response,
                           const char* comment,
                           MegaRequestListener* listener)
{
    return pImpl->answerSurvey(surveyHandle, triggerActionId, response, comment, listener);
}

void MegaApi::getWelcomePdfCopied(MegaRequestListener* listener)
{
    pImpl->getWelcomePdfCopied(listener);
}

void MegaApi::setWelcomePdfCopied(bool copied, MegaRequestListener* listener)
{
    pImpl->setWelcomePdfCopied(copied, listener);
}

void MegaApi::getMyIp(MegaRequestListener* listener)
{
    pImpl->getMyIp(listener);
}

void MegaApi::runNetworkConnectivityTest(MegaRequestListener* listener)
{
    pImpl->runNetworkConnectivityTest(listener);
}

/* END MEGAAPI */

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

int MegaAccountDetails::getSubscriptionMethodId()
{
    return 0;
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

long long MegaAccountDetails::getTransferSrvUsed()
{
    return 0;
}

long long MegaAccountDetails::getTransferUsed()
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

void MegaLogger::log(const char* /*time*/, int /*loglevel*/, const char* /*source*/, const char* /*message*/
#ifdef ENABLE_LOG_PERFORMANCE
                     , const char ** /*directMessages*/, size_t * /*directMessagesSizes*/, int /*numberMessages*/
#endif
                     )
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

int MegaGfxProcessor::getBitmapDataSize(int /*width*/,
                                        int /*height*/,
                                        int /*px*/,
                                        int /*py*/,
                                        int /*rw*/,
                                        int /*rh*/,
                                        int /*hint*/)
{
    return 0;
}

bool MegaGfxProcessor::getBitmapData(char* /*bitmapData*/, size_t /*size*/)
{
    return 0;
}

void MegaGfxProcessor::freeBitmap() { }

const char* MegaGfxProcessor::supportedImageFormats()
{
    // This special string will cause all files to be attempted
    // (backwards compatibility for Android)
    return "all";
}

const char* MegaGfxProcessor::supportedVideoFormats()
{
    // This special string will cause all files to be attempted
    // (backwards compatibility for Android)
    return "all";
}

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

int MegaPricing::getGBStorage(int)
{
    return 0;
}

int MegaPricing::getGBTransfer(int)
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

int MegaPricing::getLocalPrice(int /*productIndex*/)
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

bool MegaPricing::isBusinessType(int)
{
    return false;
}

bool MegaPricing::isFeaturePlan(int) const
{
    return false;
}

int MegaPricing::getAmountMonth(int)
{
    return 0;
}

MegaPricing *MegaPricing::copy()
{
    return NULL;
}

int MegaPricing::getGBStoragePerUser(int)
{
    return 0;
}

int MegaPricing::getGBTransferPerUser(int)
{
    return 0;
}

unsigned int MegaPricing::getMinUsers(int)
{
    return 0;
}

unsigned int MegaPricing::getPricePerUser(int)
{
    return 0;
}

unsigned int MegaPricing::getLocalPricePerUser(int)
{
    return 0;
}

unsigned int MegaPricing::getPricePerStorage(int)
{
    return 0;
}

unsigned int MegaPricing::getLocalPricePerStorage(int)
{
    return 0;
}

int MegaPricing::getGBPerStorage(int)
{
    return 0;
}

unsigned int MegaPricing::getPricePerTransfer(int)
{
    return 0;
}

unsigned int MegaPricing::getLocalPricePerTransfer(int)
{
    return 0;
}

int MegaPricing::getGBPerTransfer(int)
{
    return 0;
}

MegaStringIntegerMap* MegaPricing::getFeatures(int) const
{
    return nullptr;
}

const char *MegaCurrency::getCurrencySymbol()
{
    return nullptr;
}

const char *MegaCurrency::getCurrencyName()
{
    return nullptr;
}

const char *MegaCurrency::getLocalCurrencySymbol()
{
    return nullptr;
}

const char *MegaCurrency::getLocalCurrencyName()
{
    return nullptr;
}

unsigned int MegaPricing::getTestCategory(int) const
{
    return 0;
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

const char *MegaSync::getName() const
{
    return NULL;
}

const char *MegaSync::getLastKnownMegaFolder() const
{
    return NULL;
}

MegaHandle MegaSync::getBackupId() const
{
    return INVALID_HANDLE;
}

int MegaSync::getError() const
{
    return MegaSync::Error::NO_SYNC_ERROR;
}

int MegaSync::getWarning() const
{
    return MegaSync::Warning::NO_SYNC_WARNING;
}

int MegaSync::getType() const
{
    return MegaSync::SyncType::TYPE_UNKNOWN;
}

int MegaSync::getRunState() const
{
    return 0;
}

const char* MegaSync::getMegaSyncErrorCode()
{
    return MegaSync::getMegaSyncErrorCode(getError());
}

const char* MegaSync::getMegaSyncErrorCode(int errorCode)
{
    return MegaApi::strdup(SyncConfig::syncErrorToStr(static_cast<SyncError>(errorCode)).c_str());
}

const char* MegaSync::getMegaSyncWarningCode()
{
    return MegaSync::getMegaSyncWarningCode(getWarning());
}

const char* MegaSync::getMegaSyncWarningCode(int warningCode)
{
    switch(warningCode)
    {
    case MegaSync::Warning::NO_SYNC_WARNING:
        return "No error";
    case MegaSync::Warning::LOCAL_IS_FAT:
        return "Local filesystem is FAT";
    case MegaSync::Warning::LOCAL_IS_HGFS:
        return "Local filesystem is HGFS";
    default:
        return "Undefined warning";
    }
}

MegaSyncList *MegaSyncList::createInstance()
{
    return new MegaSyncListPrivate();
}

MegaSyncList::MegaSyncList()
{

}

MegaSyncList::~MegaSyncList()
{

}

MegaSyncList *MegaSyncList::copy() const
{
    return NULL;
}

MegaSync *MegaSyncList::get(int) const
{
    return NULL;
}

int MegaSyncList::size() const
{
    return 0;
}

void MegaSyncList::addSync(MegaSync*) {}

MegaSyncStallList* MegaSyncStallList::copy() const
{
    return nullptr;
}

size_t MegaSyncStallList::size() const
{
    return 0;
}

size_t MegaSyncStallList::getHash() const
{
    uint64_t hash = 0;
    for (size_t i = 0; i < size(); ++i)
    {
        hash = hashCombine(hash, get(i)->getHash());
    }
    return static_cast<size_t>(hash);
}

const MegaSyncStall* MegaSyncStallList::get(size_t /*i*/) const
{
    return nullptr;
}

MegaSyncStallMap* MegaSyncStallMap::copy() const
{
    return nullptr;
}

size_t MegaSyncStallMap::size() const
{
    return 0;
}

size_t MegaSyncStallMap::getHash() const
{
    uint64_t hash = 0;
    const MegaHandleList* keys = getKeys();
    for (unsigned int i = 0; i < keys->size(); ++i)
    {
        hash = hashCombine(hash, get(keys->get(i))->getHash());
    }
    return static_cast<size_t>(hash);
}

const MegaSyncStallList* MegaSyncStallMap::get(const MegaHandle) const
{
    return nullptr;
}

MegaHandleList* MegaSyncStallMap::getKeys() const
{
    return nullptr;
}
#endif


void MegaScheduledCopyListener::onBackupStateChanged(MegaApi *, MegaScheduledCopy *)
{ }
void MegaScheduledCopyListener::onBackupStart(MegaApi *, MegaScheduledCopy *)
{ }
void MegaScheduledCopyListener::onBackupFinish(MegaApi*, MegaScheduledCopy *, MegaError*)
{ }
void MegaScheduledCopyListener::onBackupUpdate(MegaApi *, MegaScheduledCopy *)
{ }
void MegaScheduledCopyListener::onBackupTemporaryError(MegaApi *, MegaScheduledCopy *, MegaError*)
{ }
MegaScheduledCopyListener::~MegaScheduledCopyListener()
{ }

MegaScheduledCopy::~MegaScheduledCopy() { }

MegaScheduledCopy *MegaScheduledCopy::copy()
{
    return NULL;
}

MegaHandle MegaScheduledCopy::getMegaHandle() const
{
    return INVALID_HANDLE;
}

const char *MegaScheduledCopy::getLocalFolder() const
{
    return NULL;
}

int MegaScheduledCopy::getTag() const
{
    return 0;
}

bool MegaScheduledCopy::getAttendPastBackups() const
{
    return false;
}

int64_t MegaScheduledCopy::getPeriod() const
{
    return 0;
}

const char *MegaScheduledCopy::getPeriodString() const
{
    return NULL;
}

long long MegaScheduledCopy::getNextStartTime(long long /*oldStartTimeAbsolute*/) const
{
    return 0;
}

int MegaScheduledCopy::getMaxBackups() const
{
    return 0;
}

int MegaScheduledCopy::getState() const
{
    return MegaScheduledCopy::SCHEDULED_COPY_FAILED;
}

long long MegaScheduledCopy::getNumberFolders() const
{
     return 0;
}

long long MegaScheduledCopy::getNumberFiles() const
{
     return 0;
}

long long MegaScheduledCopy::getTotalFiles() const
{
     return 0;
}

int64_t MegaScheduledCopy::getCurrentBKStartTime() const
{
     return 0;
}

long long MegaScheduledCopy::getTransferredBytes() const
{
     return 0;
}

long long MegaScheduledCopy::getTotalBytes() const
{
     return 0;
}

long long MegaScheduledCopy::getSpeed() const
{
     return 0;
}

long long MegaScheduledCopy::getMeanSpeed() const
{
     return 0;
}

int64_t MegaScheduledCopy::getUpdateTime() const
{
     return 0;
}

MegaTransferList *MegaScheduledCopy::getFailedTransfers()
{
    return NULL;
}

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

char *MegaAccountSession::getDeviceId() const
{
    return nullptr;
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

MegaBackgroundMediaUpload *MegaBackgroundMediaUpload::createInstance(MegaApi *api)
{
    return new MegaBackgroundMediaUploadPrivate(api);
}

MegaBackgroundMediaUpload* MegaBackgroundMediaUpload::unserialize(const char* d, MegaApi* api)
{
    unsigned char* binary;
    size_t binSize;
    MegaApi::base64ToBinary(d, &binary, &binSize);
    std::string binString((char*)binary, binSize);
    delete[] binary;
    return d ? new MegaBackgroundMediaUploadPrivate(binString, api) : NULL;
}

bool MegaBackgroundMediaUpload::analyseMediaInfo(const char* /*inputFilepath*/)
{
    return false;
}

char* MegaBackgroundMediaUpload::encryptFile(const char* /*inputFilepath*/,
                                             int64_t /*startPos*/,
                                             int64_t* /*length*/,
                                             const char* /*outputFilepath*/,
                                             bool /*adjustsizeonly*/)
{
    return NULL;
}

char *MegaBackgroundMediaUpload::getUploadURL()
{
    return NULL;
}

char *MegaBackgroundMediaUpload::serialize()
{
    return NULL;
}

void MegaBackgroundMediaUpload::setThumbnail(MegaHandle) {}

void MegaBackgroundMediaUpload::setPreview(MegaHandle) {}

void MegaBackgroundMediaUpload::setCoordinates(double /*lat*/, double /*lon*/, bool /*unshareable*/)
{}

MegaBackgroundMediaUpload::MegaBackgroundMediaUpload()
{
}

MegaBackgroundMediaUpload::~MegaBackgroundMediaUpload()
{
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


MegaSearchFilter::MegaSearchFilter()
{
}

MegaSearchFilter* MegaSearchFilter::createInstance()
{
    return new MegaSearchFilterPrivate();
}

MegaSearchFilter* MegaSearchFilter::copy() const
{
    return nullptr;
}

MegaSearchFilter::~MegaSearchFilter()
{
}

void MegaSearchFilter::byName(const char* /*searchString*/)
{
}

void MegaSearchFilter::byNodeType(int /*nodeType*/)
{
}

void MegaSearchFilter::byCategory(int /*mimeType*/)
{
}

void MegaSearchFilter::byFavourite(int /*boolFilterOption*/)
{
}

void MegaSearchFilter::bySensitivity(int /*boolFilterOption*/)
{

}

void MegaSearchFilter::byLocationHandle(MegaHandle /*ancestorHandle*/)
{
}

void MegaSearchFilter::byLocation(int /*locationType*/)
{
}

void MegaSearchFilter::byCreationTime(int64_t /*lowerLimit*/, int64_t /*upperLimit*/)
{
}

void MegaSearchFilter::byModificationTime(int64_t /*lowerLimit*/, int64_t /*upperLimit*/)
{
}

void MegaSearchFilter::byDescription(const char* /*searchString*/)
{
}

void MegaSearchFilter::byTag(const char* /*searchString*/)
{
}

void MegaSearchFilter::useAndForTextQuery(bool /*useAnd*/) {}

const char* MegaSearchFilter::byName() const
{
    return nullptr;
}

int MegaSearchFilter::byNodeType() const
{
    return MegaNode::TYPE_UNKNOWN;
}

int MegaSearchFilter::byCategory() const
{
    return MegaApi::FILE_TYPE_DEFAULT;
}

int MegaSearchFilter::byFavourite() const
{
    return MegaSearchFilter::BOOL_FILTER_DISABLED;
}

int MegaSearchFilter::bySensitivity() const
{
    return BOOL_FILTER_DISABLED;
}

MegaHandle MegaSearchFilter::byLocationHandle() const
{
    return INVALID_HANDLE;
}

int MegaSearchFilter::byLocation() const
{
    return MegaApi::SEARCH_TARGET_ALL;
}

int64_t MegaSearchFilter::byCreationTimeLowerLimit() const
{
    return 0;
}

int64_t MegaSearchFilter::byCreationTimeUpperLimit() const
{
    return 0;
}

int64_t MegaSearchFilter::byModificationTimeLowerLimit() const
{
    return 0;
}

int64_t MegaSearchFilter::byModificationTimeUpperLimit() const
{
    return 0;
}

const char* MegaSearchFilter::byDescription() const
{
    return nullptr;
}

const char* MegaSearchFilter::byTag() const
{
    return nullptr;
}

bool MegaSearchFilter::useAndForTextQuery() const
{
    return false;
}

MegaSearchPage::MegaSearchPage()
{
}

MegaSearchPage* MegaSearchPage::createInstance(size_t startingOffset, size_t size)
{
    return new MegaSearchPagePrivate(startingOffset, size);
}

MegaSearchPage* MegaSearchPage::copy() const
{
    return nullptr;
}

MegaSearchPage::~MegaSearchPage()
{
}

size_t MegaSearchPage::startingOffset() const
{
    return 0u;
}

size_t MegaSearchPage::size() const
{
    return 0u;
}

MegaApiLock::MegaApiLock(MegaApiImpl* ptr, bool lock) : api(ptr)
{
    if (lock)
    {
        lockOnce();
    }
}

MegaApiLock::~MegaApiLock()
{
    unlockOnce();
}

void MegaApiLock::lockOnce()
{
    if (!locked)
    {
        api->lockMutex();
        locked = true;
    }
}


bool MegaApiLock::tryLockFor(long long time)
{
    if (!locked)
    {
        locked = api->tryLockMutexFor(time);
    }

    return locked;
}

void MegaApiLock::unlockOnce()
{
    if (locked)
    {
        api->unlockMutex();
        locked = false;
    }
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

const char * MegaTextChat::getUnifiedKey() const
{
    return NULL;
}

unsigned char MegaTextChat::getChatOptions() const
{
    return 0;
}

bool MegaTextChat::hasChanged(uint64_t) const
{
    return false;
}

uint64_t MegaTextChat::getChanges() const
{
    return 0;
}

int MegaTextChat::isOwnChange() const
{
    return 0;
}

const MegaScheduledMeetingList* MegaTextChat::getScheduledMeetingList() const
{
    return NULL;
}

const MegaScheduledMeetingList* MegaTextChat::getUpdatedOccurrencesList() const
{
    return NULL;
}

const MegaHandleList* MegaTextChat::getSchedMeetingsChanged() const
{
    return NULL;
}

int64_t MegaTextChat::getCreationTime() const
{
    return 0;
}

bool MegaTextChat::isArchived() const
{
    return false;
}

bool MegaTextChat::isPublicChat() const
{
    return false;
}

bool MegaTextChat::isMeeting() const
{
    return false;
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


MegaStringMap *MegaStringMap::createInstance()
{
    return new MegaStringMapPrivate();
}

MegaStringMap::MegaStringMap()
{

}

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

MegaIntegerMap* MegaIntegerMap::createInstance()
{
    return new MegaIntegerMapPrivate();
}

MegaIntegerMap::~MegaIntegerMap()
{
}

MegaIntegerMap* MegaIntegerMap::copy() const
{
    return NULL;
}

MegaIntegerList* MegaIntegerMap::getKeys() const
{
    return NULL;
}

MegaIntegerList* MegaIntegerMap::get(int64_t /*key*/) const
{
    return NULL;
}

void MegaIntegerMap::set(int64_t /*key*/, int64_t /*value*/)
{

}

int64_t MegaIntegerMap::size() const
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

int MegaTransferData::getDownloadTag(int /*i*/) const
{
    return 0;
}

int MegaTransferData::getUploadTag(int /*i*/) const
{
    return 0;
}

unsigned long long MegaTransferData::getDownloadPriority(int /*i*/) const
{
    return 0;
}

unsigned long long MegaTransferData::getUploadPriority(int /*i*/) const
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

int64_t MegaEvent::getNumber() const
{
    return 0;
}

MegaHandle MegaEvent::getHandle() const
{
    return INVALID_HANDLE;
}

const char *MegaEvent::getEventString() const
{
    return NULL;
}

std::optional<int64_t> MegaEvent::getNumber(const std::string& /* key */) const
{
    return std::nullopt;
}

MegaHandleList *MegaHandleList::createInstance()
{
    return new MegaHandleListPrivate();
}

MegaHandleList::MegaHandleList()
{

}

MegaHandleList::~MegaHandleList()
{

}

MegaHandleList *MegaHandleList::copy() const
{
    return NULL;
}

MegaHandle MegaHandleList::get(unsigned int /*i*/) const
{
    return INVALID_HANDLE;
}

unsigned int MegaHandleList::size() const
{
    return 0;
}

void MegaHandleList::addMegaHandle(MegaHandle) {}

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

long long MegaAchievementsDetails::getClassStorage(int /*class_id*/)
{
    return 0;
}

long long MegaAchievementsDetails::getClassTransfer(int /*class_id*/)
{
    return 0;
}

int MegaAchievementsDetails::getClassExpire(int /*class_id*/)
{
    return 0;
}

unsigned int MegaAchievementsDetails::getAwardsCount()
{
    return 0;
}

int MegaAchievementsDetails::getAwardClass(unsigned int /*index*/)
{
    return 0;
}

int MegaAchievementsDetails::getAwardId(unsigned int /*index*/)
{
    return 0;
}

int64_t MegaAchievementsDetails::getAwardTimestamp(unsigned int /*index*/)
{
    return 0;
}

int64_t MegaAchievementsDetails::getAwardExpirationTs(unsigned int /*index*/)
{
    return 0;
}

MegaStringList* MegaAchievementsDetails::getAwardEmails(unsigned int /*index*/)
{
    return NULL;
}

int MegaAchievementsDetails::getRewardsCount()
{
    return 0;
}

int MegaAchievementsDetails::getRewardAwardId(unsigned int /*index*/)
{
    return 0;
}

long long MegaAchievementsDetails::getRewardStorage(unsigned int /*index*/)
{
    return 0;
}

long long MegaAchievementsDetails::getRewardTransfer(unsigned int /*index*/)
{
    return 0;
}

long long MegaAchievementsDetails::getRewardStorageByAwardId(int /*award_id*/)
{
    return 0;
}

long long MegaAchievementsDetails::getRewardTransferByAwardId(int /*award_id*/)
{
    return 0;
}

int MegaAchievementsDetails::getRewardExpire(unsigned int /*index*/)
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

MegaFolderInfo::~MegaFolderInfo()
{

}

MegaFolderInfo *MegaFolderInfo::copy() const
{
    return NULL;
}

int MegaFolderInfo::getNumVersions() const
{
    return 0;
}

int MegaFolderInfo::getNumFiles() const
{
    return 0;
}

int MegaFolderInfo::getNumFolders() const
{
    return 0;
}

long long MegaFolderInfo::getCurrentSize() const
{
    return 0;
}

long long MegaFolderInfo::getVersionsSize() const
{
    return 0;
}

MegaTimeZoneDetails::~MegaTimeZoneDetails()
{

}

MegaTimeZoneDetails *MegaTimeZoneDetails::copy() const
{
    return NULL;
}

int MegaTimeZoneDetails::getNumTimeZones() const
{
    return 0;
}

const char *MegaTimeZoneDetails::getTimeZone(int /*index*/) const
{
    return NULL;
}

int MegaTimeZoneDetails::getTimeOffset(int /*index*/) const
{
    return 0;
}

int MegaTimeZoneDetails::getDefault() const
{
    return -1;
}

MegaPushNotificationSettings *MegaPushNotificationSettings::createInstance()
{
    return new MegaPushNotificationSettingsPrivate();
}

MegaPushNotificationSettings::~MegaPushNotificationSettings()
{

}

MegaPushNotificationSettings *MegaPushNotificationSettings::copy() const
{
    return NULL;
}

bool MegaPushNotificationSettings::isGlobalDndEnabled() const
{
    return false;
}

bool MegaPushNotificationSettings::isGlobalChatsDndEnabled() const
{
    return false;
}

int64_t MegaPushNotificationSettings::getGlobalDnd() const
{
    return 0;
}

int64_t MegaPushNotificationSettings::getGlobalChatsDnd() const
{
    return 0;
}

bool MegaPushNotificationSettings::isGlobalScheduleEnabled() const
{
    return false;
}

int MegaPushNotificationSettings::getGlobalScheduleStart() const
{
    return 0;
}

int MegaPushNotificationSettings::getGlobalScheduleEnd() const
{
    return 0;
}

const char *MegaPushNotificationSettings::getGlobalScheduleTimezone() const
{
    return NULL;
}

bool MegaPushNotificationSettings::isChatDndEnabled(MegaHandle /*chatid*/) const
{
    return false;
}

int64_t MegaPushNotificationSettings::getChatDnd(MegaHandle /*chatid*/) const
{
    return 0;
}

bool MegaPushNotificationSettings::isChatAlwaysNotifyEnabled(MegaHandle /*chatid*/) const
{
    return false;
}

bool MegaPushNotificationSettings::isContactsEnabled() const
{
    return false;
}

bool MegaPushNotificationSettings::isSharesEnabled() const
{
    return false;
}

void MegaPushNotificationSettings::enableGlobal(bool /*enable*/)
{

}

void MegaPushNotificationSettings::setGlobalDnd(int64_t /*timestamp*/)
{

}

void MegaPushNotificationSettings::disableGlobalDnd()
{

}

void MegaPushNotificationSettings::setGlobalSchedule(int /*start*/, int /*end*/, const char * /*timezone*/)
{

}

void MegaPushNotificationSettings::disableGlobalSchedule()
{

}

void MegaPushNotificationSettings::enableChat(MegaHandle /*chatid*/, bool /*enable*/)
{

}

void MegaPushNotificationSettings::setChatDnd(MegaHandle /*chatid*/, int64_t /*timestamp*/)
{

}

void MegaPushNotificationSettings::setGlobalChatsDnd(int64_t /*timestamp*/)
{

}

void MegaPushNotificationSettings::enableChatAlwaysNotify(MegaHandle /*chatid*/, bool /*enable*/)
{

}

void MegaPushNotificationSettings::enableContacts(bool /*enable*/)
{

}

void MegaPushNotificationSettings::enableShares(bool /*enable*/)
{

}

void MegaPushNotificationSettings::enableChats(bool /*enable*/)
{

}

MegaPushNotificationSettings::MegaPushNotificationSettings()
{

}

MegaCancelToken *MegaCancelToken::createInstance()
{
    return new MegaCancelTokenPrivate(CancelToken(false));
}

MegaCancelToken::MegaCancelToken()
{

}

MegaCancelToken::~MegaCancelToken()
{

}

MegaIntegerList::~MegaIntegerList()
{

}

MegaIntegerList* MegaIntegerList::createInstance()
{
    return new MegaIntegerListPrivate();
}

MegaIntegerList *MegaIntegerList::copy() const
{
    return nullptr;
}

int64_t MegaIntegerList::get(int /*i*/) const
{
    return -1;
}

void MegaIntegerList::add(long long /*i*/)
{

}

int MegaIntegerList::size() const
{
    return 0;
}

MegaBanner::MegaBanner()
{
}

MegaBanner::~MegaBanner()
{
}

MegaBanner* MegaBanner::copy() const
{
    return nullptr;
}

int MegaBanner::getId() const
{
    return 0;
}

const char* MegaBanner::getTitle() const
{
    return nullptr;
}

const char* MegaBanner::getDescription() const
{
    return nullptr;
}

const char* MegaBanner::getImage() const
{
    return nullptr;
}

const char* MegaBanner::getUrl() const
{
    return nullptr;
}

const char* MegaBanner::getBackgroundImage() const
{
    return nullptr;
}

const char* MegaBanner::getImageLocation() const
{
    return nullptr;
}


MegaBannerList::MegaBannerList()
{
}

MegaBannerList::~MegaBannerList()
{
}

MegaBannerList* MegaBannerList::copy() const
{
    return nullptr;
}

const MegaBanner* MegaBannerList::get(int /*i*/) const
{
    return nullptr;
}

int MegaBannerList::size() const
{
    return 0;
}

MegaCurrency::~MegaCurrency()
{
}

MegaCurrency *MegaCurrency::copy()
{
    return nullptr;
}

/* MegaVpnCredentials BEGIN */
MegaVpnCredentials::MegaVpnCredentials()
{
}

MegaVpnCredentials::~MegaVpnCredentials()
{
}

MegaIntegerList* MegaVpnCredentials::getSlotIDs() const
{
    return nullptr;
}

MegaStringList* MegaVpnCredentials::getVpnRegions() const
{
    return nullptr;
}

MegaVpnRegionList* MegaVpnCredentials::getVpnRegionsDetailed() const
{
    return nullptr;
}

const char* MegaVpnCredentials::getIPv4(int /*slotID*/) const
{
    return nullptr;
}

const char* MegaVpnCredentials::getIPv6(int /*slotID*/) const
{
    return nullptr;
}

const char* MegaVpnCredentials::getDeviceID(int /*slotID*/) const
{
    return nullptr;
}

int MegaVpnCredentials::getClusterID(int /*slotID*/) const
{
    return 0;
}

const char* MegaVpnCredentials::getClusterPublicKey(int /*clusterID*/) const
{
    return nullptr;
}

MegaVpnCredentials* MegaVpnCredentials::copy() const
{
    return nullptr;
}
/* MegaVpnCredentials END */

MegaNodeTree* MegaNodeTree::createInstance(const MegaNodeTree* nodeTreeChild,
                                           const char* name,
                                           const char* s4AttributeValue,
                                           const MegaCompleteUploadData* completeUploadData,
                                           MegaHandle sourceHandle)
{
    return new MegaNodeTreePrivate(nodeTreeChild,
                                   name ? name : "",
                                   s4AttributeValue ? s4AttributeValue : "",
                                   completeUploadData,
                                   sourceHandle,
                                   INVALID_HANDLE);
}

MegaCompleteUploadData* MegaCompleteUploadData::createInstance(const char* fingerprint,
                                                               const char* string64UploadToken,
                                                               const char* string64FileKey)
{
    return new MegaCompleteUploadDataPrivate(fingerprint ? fingerprint : "",
                                             string64UploadToken ? string64UploadToken : "",
                                             string64FileKey ? string64FileKey : "");
}

MegaGfxProvider::~MegaGfxProvider() = default;

MegaGfxProvider* MegaGfxProvider::createIsolatedInstance(const char* endpointName,
                                                         const char* executable,
                                                         unsigned int keepAliveInSeconds,
                                                         const MegaStringList* extraArgs)
{
    auto provider = MegaGfxProviderPrivate::createIsolatedInstance(endpointName,
                                                                   executable,
                                                                   keepAliveInSeconds,
                                                                   extraArgs);

    return provider.release();
}

MegaGfxProvider* MegaGfxProvider::createExternalInstance(MegaGfxProcessor* processor)
{
    return MegaGfxProviderPrivate::createExternalInstance(processor).release();
}

MegaGfxProvider* MegaGfxProvider::createInternalInstance()
{
    return MegaGfxProviderPrivate::createInternalInstance().release();
}

MegaFuseExecutorFlags::MegaFuseExecutorFlags() = default;

MegaFuseExecutorFlags::~MegaFuseExecutorFlags() = default;

MegaFuseFlags::MegaFuseFlags() = default;

MegaFuseFlags::~MegaFuseFlags() = default;

MegaFuseFlags* MegaFuseFlags::create()
{
    return new MegaFuseFlagsPrivate(fuse::ServiceFlags());
}

MegaFuseInodeCacheFlags::MegaFuseInodeCacheFlags() = default;

MegaFuseInodeCacheFlags::~MegaFuseInodeCacheFlags() = default;

MegaMount::MegaMount() = default;

MegaMount::~MegaMount() = default;

MegaMount* MegaMount::create()
{
    return new MegaMountPrivate();
}

const char* MegaMount::getResultDescription(int result)
{
    assert(result >= ABORTED && result <= UNSUPPORTED);

    return fuse::toDescription(static_cast<fuse::MountResult>(result));
}

const char* MegaMount::getResultString(int result)
{
    assert(result >= ABORTED && result <= UNSUPPORTED);

    return fuse::toString(static_cast<fuse::MountResult>(result));
}

MegaMountFlags::MegaMountFlags() = default;

MegaMountFlags::~MegaMountFlags() = default;

MegaMountFlags* MegaMountFlags::create()
{
    return new MegaMountFlagsPrivate();
}

MegaMountList::MegaMountList() = default;

MegaMountList::~MegaMountList() = default;

MegaCancelSubscriptionReason* MegaCancelSubscriptionReason::create(const char* text,
                                                                   const char* position)
{
    return new MegaCancelSubscriptionReasonPrivate(text, position);
}

MegaCancelSubscriptionReasonList* MegaCancelSubscriptionReasonList::create()
{
    return new MegaCancelSubscriptionReasonListPrivate();
}
}
