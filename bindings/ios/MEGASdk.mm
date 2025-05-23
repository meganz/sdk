/**
 * @file MEGASdk.mm
 * @brief Allows to control a MEGA account or a shared folder
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

#import "MEGASdk.h"
#import "megaapi.h"
#import "MEGANode+init.h"
#import "MEGATOTPData+init.h"
#import "MEGASet+init.h"
#import "MEGASetElement+init.h"
#import "MEGAUser+init.h"
#import "MEGATransfer+init.h"
#import "MEGATransferList+init.h"
#import "MEGANodeList+init.h"
#import "MEGAUserList+init.h"
#import "MEGAUserAlertList+init.h"
#import "MEGAIntegerList+init.h"
#import "MEGAStringList+init.h"
#import "MEGAError+init.h"
#import "MEGAShareList+init.h"
#import "MEGAContactRequest+init.h"
#import "MEGAContactRequestList+init.h"
#import "MEGARecentActionBucket+init.h"
#import "MEGABackgroundMediaUpload+init.h"
#import "DelegateMEGARequestListener.h"
#import "DelegateMEGATransferListener.h"
#import "DelegateMEGAGlobalListener.h"
#import "DelegateMEGAListener.h"
#import "DelegateMEGALoggerListener.h"
#import "DelegateMEGATreeProcessorListener.h"
#import "DelegateMEGAScheduledCopyListener.h"
#import "MEGAFileInputStream.h"
#import "MEGADataInputStream.h"
#import "MEGACancelToken+init.h"
#import "MEGAPushNotificationSettings+init.h"
#import "MEGACancelSubscriptionReasonList+init.h"

#import <set>
#import <pthread.h>

NSString * const MEGAIsBeingLogoutNotification = @"nz.mega.isBeingLogout";

using namespace mega;

@interface MEGASdk () {
    pthread_mutex_t listenerMutex;
}

@property (nonatomic, assign) std::set<DelegateMEGARequestListener *>activeRequestListeners;
@property (nonatomic, assign) std::set<DelegateMEGATransferListener *>activeTransferListeners;
@property (nonatomic, assign) std::set<DelegateMEGAGlobalListener *>activeGlobalListeners;
@property (nonatomic, assign) std::set<DelegateMEGAListener *>activeMegaListeners;
@property (nonatomic, assign) std::set<DelegateMEGALoggerListener *>activeLoggerListeners;
@property (nonatomic, assign) std::set<DelegateMEGAScheduledCopyListener *>activeScheduledCopyListeners;

- (MegaRequestListener *)createDelegateMEGARequestListener:(id<MEGARequestDelegate>)delegate singleListener:(BOOL)singleListener;
- (MegaRequestListener *)createDelegateMEGARequestListener:(id<MEGARequestDelegate>)delegate singleListener:(BOOL)singleListener queueType:(ListenerQueueType)queueType;
- (MegaTransferListener *)createDelegateMEGATransferListener:(id<MEGATransferDelegate>)delegate singleListener:(BOOL)singleListener;
- (MegaTransferListener *)createDelegateMEGATransferListener:(id<MEGATransferDelegate>)delegate singleListener:(BOOL)singleListener queueType:(ListenerQueueType)queueType;
- (MegaGlobalListener *)createDelegateMEGAGlobalListener:(id<MEGAGlobalDelegate>)delegate  queueType:(ListenerQueueType)queueType;
- (MegaListener *)createDelegateMEGAListener:(id<MEGADelegate>)delegate;
- (MegaLogger *)createDelegateMegaLogger:(id<MEGALoggerDelegate>)delegate;
- (MegaScheduledCopyListener *)createDelegateMEGAScheduledCopyListener:(id<MEGAScheduledCopyDelegate>)delegate queueType:(ListenerQueueType)queueType;

@property (nonatomic, nullable) MegaApi *megaApi;

@end

@implementation MEGASdk

#pragma mark - Properties

- (NSString *)myEmail {
    if (self.megaApi == nil) return nil;
    const char *val = self.megaApi->getMyEmail();
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (NSDate *)accountCreationDate {
    if (self.megaApi == nil) return nil;
    NSTimeInterval accountCreationTs = self.megaApi->getAccountCreationTs();
    return accountCreationTs ? [NSDate dateWithTimeIntervalSince1970:accountCreationTs] : nil;
}

- (MEGANode *)rootNode {
    if (self.megaApi == nil) return nil;
    MegaNode *node = self.megaApi->getRootNode();
    return node ? [[MEGANode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MEGANode *)rubbishNode {
    if (self.megaApi == nil) return nil;
    MegaNode *node = self.megaApi->getRubbishNode();
    return node ? [[MEGANode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MEGATransferList *)transfers {
    if (self.megaApi == nil) return nil;
    return [[MEGATransferList alloc] initWithTransferList:self.megaApi->getTransfers() cMemoryOwn:YES];
}

- (MEGATransferList *)downloadTransfers {
    if (self.megaApi == nil) return nil;
    return [[MEGATransferList alloc] initWithTransferList:self.megaApi->getTransfers(MegaTransfer::TYPE_DOWNLOAD) cMemoryOwn:YES];
}

- (MEGATransferList *)uploadTransfers {
    if (self.megaApi == nil) return nil;
    return [[MEGATransferList alloc] initWithTransferList:self.megaApi->getTransfers(MegaTransfer::TYPE_UPLOAD) cMemoryOwn:YES];
}

- (Retry)waiting {
    if (self.megaApi == nil) return RetryUnknown;
    return (Retry) self.megaApi->isWaiting();
}

- (unsigned long long)totalNodes {
    if (self.megaApi == nil) return 0;
    return self.megaApi->getNumNodes();
}

- (NSString *)masterKey {
    if (self.megaApi == nil) return nil;
    const char *val = self.megaApi->exportMasterKey();
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (NSString *)userAgent {
    if (self.megaApi == nil) return nil;
    const char *val = self.megaApi->getUserAgent();
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    return ret;
}

- (MEGAUser *)myUser {
    if (self.megaApi == nil) return nil;
    MegaUser *user = self.megaApi->getMyUser();
    return user ? [[MEGAUser alloc] initWithMegaUser:user cMemoryOwn:YES] : nil;
}

- (BOOL)isAchievementsEnabled {
    if (self.megaApi == nil) return NO;
    return self.megaApi->isAchievementsEnabled();
}

- (BOOL)isContactVerificationWarningEnabled {
    if (self.megaApi == nil) return NO;
    return self.megaApi->contactVerificationWarningEnabled();
}

- (BOOL)isNewAccount {
    if (self.megaApi == nil) return NO;
    return self.megaApi->accountIsNew();
}

#pragma mark - Business

- (BOOL)isBusinessAccount {
    if (self.megaApi == nil) return NO;
    return self.megaApi->isBusinessAccount();
}

- (BOOL)isMasterBusinessAccount {
    if (self.megaApi == nil) return NO;
    return self.megaApi->isMasterBusinessAccount();
}

- (BOOL)isBusinessAccountActive {
    if (self.megaApi == nil) return NO;
    return self.megaApi->isBusinessAccountActive();
}

- (BusinessStatus)businessStatus {
    if (self.megaApi == nil) return BusinessStatusInactive;
    return (BusinessStatus) self.megaApi->getBusinessStatus();
}

- (NSInteger)numUnreadUserAlerts {
    if (self.megaApi == nil) return 0;
    return self.megaApi->getNumUnreadUserAlerts();
}

- (long long)bandwidthOverquotaDelay {
    if (self.megaApi == nil) return 0;
    return self.megaApi->getBandwidthOverquotaDelay();
}

#pragma mark - Init

- (instancetype)initWithAppKey:(NSString *)appKey userAgent:(NSString *)userAgent {
    self.megaApi = new MegaApi(appKey.UTF8String, (const char *)NULL, userAgent.UTF8String);
    
    if (pthread_mutex_init(&listenerMutex, NULL)) {
        return nil;
    }
    
    return self;
}

- (instancetype)initWithAppKey:(NSString *)appKey userAgent:(NSString *)userAgent basePath:(NSString *)basePath {
    self.megaApi = new MegaApi(appKey.UTF8String, basePath.UTF8String, userAgent.UTF8String);
    
    if (pthread_mutex_init(&listenerMutex, NULL)) {
        return nil;
    }
    
    return self;
}

- (instancetype)initWithAppKey:(NSString *)appKey userAgent:(NSString *)userAgent basePath:(NSString *)basePath clientType:(MEGAClientType)clientType {
    self.megaApi = new MegaApi(appKey.UTF8String, basePath.UTF8String, userAgent.UTF8String, 1, (int)clientType);

    if (pthread_mutex_init(&listenerMutex, NULL)) {
        return nil;
    }

    return self;
}

- (void)deleteMegaApi {    
    delete _megaApi;
    _megaApi = nil;
    pthread_mutex_destroy(&listenerMutex);
}

- (void)dealloc {
    delete _megaApi;
    _megaApi = nil;
    pthread_mutex_destroy(&listenerMutex);
}

- (MegaApi *)getCPtr {
    return _megaApi;
}

#pragma mark - Add and remove delegates

- (void)addMEGADelegate:(id<MEGADelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->addListener([self createDelegateMEGAListener:delegate]);
    }
}

- (void)addMEGARequestDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->addRequestListener([self createDelegateMEGARequestListener:delegate singleListener:NO]);
    }
}

- (void)addMEGARequestDelegate:(id<MEGARequestDelegate>)delegate queueType:(ListenerQueueType)queueType {
    if (self.megaApi) {
        self.megaApi->addRequestListener([self createDelegateMEGARequestListener:delegate singleListener:NO queueType:queueType]);
    }
}

- (void)addMEGATransferDelegate:(id<MEGATransferDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->addTransferListener([self createDelegateMEGATransferListener:delegate singleListener:NO]);
    }
}

- (void)addMEGATransferDelegate:(id<MEGATransferDelegate>)delegate queueType:(ListenerQueueType)queueType {
    if (self.megaApi) {
        self.megaApi->addTransferListener([self createDelegateMEGATransferListener:delegate singleListener:NO queueType:queueType]);
    }
}

- (void)addMEGAGlobalDelegate:(id<MEGAGlobalDelegate>)delegate {
    [self addMEGAGlobalDelegate:delegate queueType:ListenerQueueTypeMain];
}

- (void)addMEGAGlobalDelegate:(id<MEGAGlobalDelegate>)delegate queueType:(ListenerQueueType)queueType {
    if (self.megaApi) {
        self.megaApi->addGlobalListener([self createDelegateMEGAGlobalListener:delegate queueType:queueType]);
    }
}

- (void)removeMEGADelegate:(id<MEGADelegate>)delegate {
    std::vector<DelegateMEGAListener *> listenersToRemove;
    
    pthread_mutex_lock(&listenerMutex);
    std::set<DelegateMEGAListener *>::iterator it = _activeMegaListeners.begin();
    while (it != _activeMegaListeners.end()) {
        DelegateMEGAListener *delegateListener = *it;
        if (delegateListener->getUserListener() == delegate) {
            listenersToRemove.push_back(delegateListener);
            _activeMegaListeners.erase(it++);
        }
        else {
            it++;
        }
    }
    pthread_mutex_unlock(&listenerMutex);
    
    for (int i = 0; i < listenersToRemove.size(); i++)
    {
        if (self.megaApi) {
            self.megaApi->removeListener(listenersToRemove[i]);
        }
        delete listenersToRemove[i];
    }
}

- (void)removeMEGARequestDelegate:(id<MEGARequestDelegate>)delegate {
    std::vector<DelegateMEGARequestListener *> listenersToRemove;
    
    pthread_mutex_lock(&listenerMutex);
    std::set<DelegateMEGARequestListener *>::iterator it = _activeRequestListeners.begin();
    while (it != _activeRequestListeners.end()) {
        DelegateMEGARequestListener *delegateListener = *it;
        if (delegateListener->getUserListener() == delegate) {
            listenersToRemove.push_back(delegateListener);
            _activeRequestListeners.erase(it++);
        }
        else {
            it++;
        }
    }
    pthread_mutex_unlock(&listenerMutex);
    
    for (int i = 0; i < listenersToRemove.size(); i++)
    {
        if (self.megaApi) {
            self.megaApi->removeRequestListener(listenersToRemove[i]);
        }
        delete listenersToRemove[i];
    }
}

- (void)removeMEGATransferDelegate:(id<MEGATransferDelegate>)delegate {
    std::vector<DelegateMEGATransferListener *> listenersToRemove;
    
    pthread_mutex_lock(&listenerMutex);
    std::set<DelegateMEGATransferListener *>::iterator it = _activeTransferListeners.begin();
    while (it != _activeTransferListeners.end()) {
        DelegateMEGATransferListener *delegateListener = *it;
        if (delegateListener->getUserListener() == delegate) {
            listenersToRemove.push_back(delegateListener);
            _activeTransferListeners.erase(it++);
        }
        else {
            it++;
        }
    }
    pthread_mutex_unlock(&listenerMutex);
    
    for (int i = 0; i < listenersToRemove.size(); i++)
    {
        if (self.megaApi) {
            self.megaApi->removeTransferListener(listenersToRemove[i]);
        }
        delete listenersToRemove[i];
    }
}

- (void)removeMEGAGlobalDelegate:(id<MEGAGlobalDelegate>)delegate {
    std::vector<DelegateMEGAGlobalListener *> listenersToRemove;
    
    pthread_mutex_lock(&listenerMutex);
    std::set<DelegateMEGAGlobalListener *>::iterator it = _activeGlobalListeners.begin();
    while (it != _activeGlobalListeners.end()) {
        DelegateMEGAGlobalListener *delegateListener = *it;
        if (delegateListener->getUserListener() == delegate) {
            listenersToRemove.push_back(delegateListener);
            _activeGlobalListeners.erase(it++);
        }
        else {
            it++;
        }
    }
    pthread_mutex_unlock(&listenerMutex);
    
    
    for (int i = 0; i < listenersToRemove.size(); i++)
    {
        if (self.megaApi) {
            self.megaApi->removeGlobalListener(listenersToRemove[i]);
        }
        delete listenersToRemove[i];
    }
    
}

- (void)addLoggerDelegate:(id<MEGALoggerDelegate>)delegate {
    MegaApi::addLoggerObject([self createDelegateMegaLogger:delegate]);
}

- (void)removeLoggerDelegate:(id<MEGALoggerDelegate>)delegate {
    std::vector<DelegateMEGALoggerListener *> listenersToRemove;
    
    pthread_mutex_lock(&listenerMutex);
    std::set<DelegateMEGALoggerListener *>::iterator it = _activeLoggerListeners.begin();
    while (it != _activeLoggerListeners.end()) {
        DelegateMEGALoggerListener *delegateListener = *it;
        if (delegateListener->getUserListener() == delegate) {
            listenersToRemove.push_back(delegateListener);
            _activeLoggerListeners.erase(it++);
        }
        else {
            it++;
        }
    }
    pthread_mutex_unlock(&listenerMutex);
    
    for (int i = 0; i < listenersToRemove.size(); i++)
    {
        MegaApi::removeLoggerObject(listenersToRemove[i]);
        delete listenersToRemove[i];
    }
}

- (void)addMEGAScheduledCopyDelegate:(id<MEGAScheduledCopyDelegate>)delegate {
    [self addMEGAScheduledCopyDelegate:delegate queueType:ListenerQueueTypeMain];
}

- (void)addMEGAScheduledCopyDelegate:(id<MEGAScheduledCopyDelegate>)delegate queueType:(ListenerQueueType)queueType {
    if (self.megaApi) {
        self.megaApi->addScheduledCopyListener([self createDelegateMEGAScheduledCopyListener:delegate queueType:queueType]);
    }
}

- (void)removeMEGAScheduledCopyDelegate:(id<MEGAScheduledCopyDelegate>)delegate {
    std::vector<DelegateMEGAScheduledCopyListener *> listenersToRemove;
    
    pthread_mutex_lock(&listenerMutex);
    std::set<DelegateMEGAScheduledCopyListener *>::iterator it = _activeScheduledCopyListeners.begin();
    while (it != _activeScheduledCopyListeners.end()) {
        DelegateMEGAScheduledCopyListener *delegateListener = *it;
        if (delegateListener->getUserListener() == delegate) {
            listenersToRemove.push_back(delegateListener);
            _activeScheduledCopyListeners.erase(it++);
        }
        else {
            it++;
        }
    }
    pthread_mutex_unlock(&listenerMutex);
    
    
    for (int i = 0; i < listenersToRemove.size(); i++)
    {
        if (self.megaApi) {
            self.megaApi->removeScheduledCopyListener(listenersToRemove[i]);
        }
        delete listenersToRemove[i];
    }
    
}

#pragma mark - Utils

+ (uint64_t)handleForBase64Handle:(NSString *)base64Handle {
    if(base64Handle == nil) return ::mega::INVALID_HANDLE;
    
    return MegaApi::base64ToHandle([base64Handle UTF8String]);
}

+ (uint64_t)handleForBase64UserHandle:(NSString *)base64UserHandle {
    if(base64UserHandle == nil) return ::mega::INVALID_HANDLE;
    
    return MegaApi::base64ToUserHandle([base64UserHandle UTF8String]);
}

+ (NSString *)base64HandleForHandle:(uint64_t)handle {
    const char *val = MegaApi::handleToBase64(handle);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

+ (NSString *)base64HandleForUserHandle:(uint64_t)userhandle {
    const char *val = MegaApi::userHandleToBase64(userhandle);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (void)retryPendingConnections {
    if (self.megaApi) {
        self.megaApi->retryPendingConnections();
    }
}

- (void)reconnect {
    if (self.megaApi) {
        self.megaApi->retryPendingConnections(true, true);
    }
}

- (BOOL)serverSideRubbishBinAutopurgeEnabled {
    if (self.megaApi == nil) return NO;
    return self.megaApi->serverSideRubbishBinAutopurgeEnabled();
}

- (BOOL)appleVoipPushEnabled {
    if (self.megaApi == nil) return NO;
    return self.megaApi->appleVoipPushEnabled();
}

- (void)getSessionTransferURL:(NSString *)path delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getSessionTransferURL(path.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getSessionTransferURL:(NSString *)path {
    if (self.megaApi) {
        self.megaApi->getSessionTransferURL(path.UTF8String);
    }
}

- (MEGAStringList *)megaStringListFor:(NSArray<NSString *>*)stringList {
    MegaStringList* list = mega::MegaStringList::createInstance();
    for (NSString* string in stringList) {
        list->add([string UTF8String]);
    }
    
    return [[MEGAStringList alloc] initWithMegaStringList:list cMemoryOwn:YES];
}

#pragma mark - Login Requests

- (BOOL)multiFactorAuthAvailable {
    if (self.megaApi == nil) return NO;
    return self.megaApi->multiFactorAuthAvailable();
}

- (void)multiFactorAuthCheckWithEmail:(NSString *)email delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->multiFactorAuthCheck(email.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)multiFactorAuthCheckWithEmail:(NSString *)email {
    if (self.megaApi) {
        self.megaApi->multiFactorAuthCheck(email.UTF8String);
    }
}

- (void)multiFactorAuthGetCodeWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->multiFactorAuthGetCode([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)multiFactorAuthGetCode {
    if (self.megaApi) {
        self.megaApi->multiFactorAuthGetCode();
    }
}

- (void)multiFactorAuthEnableWithPin:(NSString *)pin delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->multiFactorAuthEnable(pin.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)multiFactorAuthEnableWithPin:(NSString *)pin  {
    if (self.megaApi) {
        self.megaApi->multiFactorAuthEnable(pin.UTF8String);
    }
}

- (void)multiFactorAuthDisableWithPin:(NSString *)pin delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->multiFactorAuthDisable(pin.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)multiFactorAuthDisableWithPin:(NSString *)pin {
    if (self.megaApi) {
        self.megaApi->multiFactorAuthDisable(pin.UTF8String);
    }
}

- (void)multiFactorAuthLoginWithEmail:(NSString *)email password:(NSString *)password pin:(NSString *)pin delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->multiFactorAuthLogin(email.UTF8String, password.UTF8String, pin.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)multiFactorAuthLoginWithEmail:(NSString *)email password:(NSString *)password pin:(NSString *)pin {
    if (self.megaApi) {
        self.megaApi->multiFactorAuthLogin(email.UTF8String, password.UTF8String, pin.UTF8String);
    }
}

- (void)multiFactorAuthChangePassword:(NSString *)oldPassword newPassword:(NSString *)newPassword pin:(NSString *)pin delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->multiFactorAuthChangePassword(oldPassword.UTF8String, newPassword.UTF8String, pin.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)multiFactorAuthChangePassword:(NSString *)oldPassword newPassword:(NSString *)newPassword pin:(NSString *)pin {
    if (self.megaApi) {
        self.megaApi->multiFactorAuthChangePassword(oldPassword.UTF8String, newPassword.UTF8String, pin.UTF8String);
    }
}

- (void)multiFactorAuthChangeEmail:(NSString *)email pin:(NSString *)pin delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->multiFactorAuthChangeEmail(email.UTF8String, pin.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)multiFactorAuthChangeEmail:(NSString *)email pin:(NSString *)pin {
    if (self.megaApi) {
        self.megaApi->multiFactorAuthChangeEmail(email.UTF8String, pin.UTF8String);
    }
}

- (void)multiFactorAuthCancelAccountWithPin:(NSString *)pin delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->multiFactorAuthCancelAccount(pin.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)multiFactorAuthCancelAccountWithPin:(NSString *)pin {
    if (self.megaApi) {
        self.megaApi->multiFactorAuthCancelAccount(pin.UTF8String);
    }
}

- (void)fetchTimeZoneWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->fetchTimeZone([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)fetchTimeZone {
    if (self.megaApi) {
        self.megaApi->fetchTimeZone();
    }
}

- (void)loginWithEmail:(NSString *)email password:(NSString *)password {
    if (self.megaApi) {
        self.megaApi->login(email.UTF8String, password.UTF8String);
    }
}

- (void)loginWithEmail:(NSString *)email password:(NSString *)password delegate:(id<MEGARequestDelegate>)delegate{
    if (self.megaApi) {
        self.megaApi->login(email.UTF8String, password.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)sendDevCommand:(NSString *)command email:(NSString *)email delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->sendDevCommand(command.UTF8String, email.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (NSString *)dumpSession {
    if (self.megaApi == nil) return nil;
    const char *val = self.megaApi->dumpSession();
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (NSString *)sequenceNumber {
    if (self.megaApi == nil) return nil;
    const char *val = self.megaApi->getSequenceNumber();
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (NSString *)accountAuth {
    if (self.megaApi == nil) return nil;
    const char *val = self.megaApi->getAccountAuth();
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (void)setAccountAuth:(NSString *)accountAuth {
    if (self.megaApi) {
        self.megaApi->setAccountAuth(accountAuth.UTF8String);
    }
}

- (void)fastLoginWithSession:(NSString *)session {
    if (self.megaApi) {
        self.megaApi->fastLogin(session.UTF8String);
    }
}

- (void)fastLoginWithSession:(NSString *)session delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->fastLogin(session.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)loginToFolderLink:(NSString *)folderLink delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->loginToFolder(folderLink.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)loginToFolderLink:(NSString *)folderLink {
    if (self.megaApi) {
        self.megaApi->loginToFolder(folderLink.UTF8String);
    }
}

- (NSInteger)isLoggedIn {
    if (self.megaApi == nil) return 0;
    return self.megaApi->isLoggedIn();
}

- (BOOL)isEphemeralPlusPlus {
    if (self.megaApi == nil) return false;
    return self.megaApi->isEphemeralPlusPlus();
}

- (void)fetchNodesWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->fetchNodes([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)fetchNodes {
    if (self.megaApi) {
        self.megaApi->fetchNodes();
    }
}

- (void)logoutWithDelegate:(id<MEGARequestDelegate>)delegate {
    [NSNotificationCenter.defaultCenter postNotificationName:MEGAIsBeingLogoutNotification object:nil];
    if (self.megaApi) {
        self.megaApi->logout([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)logout {
    [NSNotificationCenter.defaultCenter postNotificationName:MEGAIsBeingLogoutNotification object:nil];
    if (self.megaApi) {
        self.megaApi->logout(NULL);
    }
}

- (void)localLogoutWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->localLogout([self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)localLogout {
    if (self.megaApi) {
        self.megaApi->localLogout();
    }
}

- (void)invalidateCache {
    if (self.megaApi) {
        self.megaApi->invalidateCache();
    }
}

- (PasswordStrength)passwordStrength:(NSString *)password {
    if (self.megaApi == nil) return PasswordStrengthVeryWeak;
    return (PasswordStrength) self.megaApi->getPasswordStrength(password.UTF8String);
}

- (BOOL)checkPassword:(NSString *)password {
    if (self.megaApi == nil) return NO;
    return self.megaApi->checkPassword(password.UTF8String);
}

- (NSString *)myCredentials {
    if (self.megaApi == nil) return nil;
    const char *val = self.megaApi->getMyCredentials();
    if (val) {
        NSString *ret = [NSString.alloc initWithUTF8String:val];
        delete [] val;
        return ret;
    } else {
        return nil;
    }
}

- (void)getUserCredentials:(MEGAUser *)user delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getUserCredentials(user.getCPtr, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getUserCredentials:(MEGAUser *)user {
    if (self.megaApi) {
        self.megaApi->getUserCredentials(user.getCPtr);
    }
}

- (BOOL)areCredentialsVerifiedOfUser:(MEGAUser *)user {
    if (self.megaApi == nil) return NO;
    return self.megaApi->areCredentialsVerified(user.getCPtr);
}

- (void)verifyCredentialsOfUser:(MEGAUser *)user delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->verifyCredentials(user.getCPtr, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)verifyCredentialsOfUser:(MEGAUser *)user {
    if (self.megaApi) {
        self.megaApi->verifyCredentials(user.getCPtr);
    }
}

- (void)resetCredentialsOfUser:(MEGAUser *)user delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->resetCredentials(user.getCPtr, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)resetCredentialsOfUser:(MEGAUser *)user {
    if (self.megaApi) {
        self.megaApi->resetCredentials(user.getCPtr);
    }
}

#pragma mark - Create account and confirm account Requests

- (void)createEphemeralAccountPlusPlusWithFirstname:(NSString *)firstname lastname:(NSString *)lastname {
    if (self.megaApi) {
        self.megaApi->createEphemeralAccountPlusPlus(firstname.UTF8String, lastname.UTF8String);
    }
}

- (void)createEphemeralAccountPlusPlusWithFirstname:(NSString *)firstname lastname:(NSString *)lastname delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->createEphemeralAccountPlusPlus(firstname.UTF8String, lastname.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)createAccountWithEmail:(NSString *)email password:(NSString *)password firstname:(NSString *)firstname lastname:(NSString *)lastname {
    if (self.megaApi) {
        self.megaApi->createAccount(email.UTF8String, password.UTF8String, firstname.UTF8String, lastname.UTF8String);
    }
}

- (void)createAccountWithEmail:(NSString *)email password:(NSString *)password firstname:(NSString *)firstname lastname:(NSString *)lastname delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->createAccount(email.UTF8String, password.UTF8String, firstname.UTF8String, lastname.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)resumeCreateAccountWithSessionId:(NSString *)sessionId delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->resumeCreateAccount(sessionId.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)resumeCreateAccountWithSessionId:(NSString *)sessionId {
    if (self.megaApi) {
        self.megaApi->resumeCreateAccount(sessionId.UTF8String);
    }
}

- (void)cancelCreateAccountWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->cancelCreateAccount([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)cancelCreateAccount {
    if (self.megaApi) {
        self.megaApi->cancelCreateAccount();
    }
}

- (void)resendSignupLinkWithEmail:(NSString *)email name:(NSString *)name delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->resendSignupLink(email.UTF8String, name.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)querySignupLink:(NSString *)link {
    if (self.megaApi) {
        self.megaApi->querySignupLink(link.UTF8String);
    }
}

- (void)querySignupLink:(NSString *)link delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->querySignupLink(link.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)confirmAccountWithLink:(NSString *)link password:(NSString *)password {
    if (self.megaApi) {
        self.megaApi->confirmAccount(link.UTF8String, password.UTF8String);
    }
}

- (void)confirmAccountWithLink:(NSString *)link password:(NSString *)password delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->confirmAccount(link.UTF8String, password.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)resetPasswordWithEmail:(NSString *)email hasMasterKey:(BOOL)hasMasterKey delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->resetPassword(email.UTF8String, hasMasterKey, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)resetPasswordWithEmail:(NSString *)email hasMasterKey:(BOOL)hasMasterKey {
    if (self.megaApi) {
        self.megaApi->resetPassword(email.UTF8String, hasMasterKey);
    }
}

- (void)queryResetPasswordLink:(NSString *)link delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->queryResetPasswordLink(link.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)queryResetPasswordLink:(NSString *)link {
    if (self.megaApi) {
        self.megaApi->queryResetPasswordLink(link.UTF8String);
    }
}

- (void)confirmResetPasswordWithLink:(NSString *)link newPassword:(NSString *)newPassword masterKey:(nullable NSString *)masterKey delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->confirmResetPassword(link.UTF8String, newPassword.UTF8String, masterKey.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)confirmResetPasswordWithLink:(NSString *)link newPassword:(NSString *)newPassword masterKey:(nullable NSString *)masterKey {
    if (self.megaApi) {
        self.megaApi->confirmResetPassword(link.UTF8String, newPassword.UTF8String, masterKey.UTF8String);
    }
}

- (void)checkRecoveryKey:(NSString *)link recoveryKey:(NSString *)recoveryKey delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->checkRecoveryKey(link.UTF8String, recoveryKey.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)cancelAccountWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->cancelAccount([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)cancelAccount {
    if (self.megaApi) {
        self.megaApi->cancelAccount();
    }
}

- (void)queryCancelLink:(NSString *)link {
    if (self.megaApi) {
        self.megaApi->queryCancelLink(link.UTF8String);
    }
}

- (void)queryCancelLink:(NSString *)link delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->queryCancelLink(link.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)confirmCancelAccountWithLink:(NSString *)link password:(NSString *)password delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->confirmCancelAccount(link.UTF8String, password.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)confirmCancelAccountWithLink:(NSString *)link password:(NSString *)password {
    if (self.megaApi) {
        self.megaApi->confirmCancelAccount(link.UTF8String, password.UTF8String);
    }
}

- (void)resendVerificationEmailWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->resendVerificationEmail([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)resendVerificationEmail {
    if (self.megaApi) {
        self.megaApi->resendVerificationEmail();
    }
}

- (void)changeEmail:(NSString *)email delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->changeEmail(email.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)changeEmail:(NSString *)email {
    if (self.megaApi) {
        self.megaApi->changeEmail(email.UTF8String);
    }
}

- (void)queryChangeEmailLink:(NSString *)link delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->queryChangeEmailLink(link.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)queryChangeEmailLink:(NSString *)link {
    if (self.megaApi) {
        self.megaApi->queryChangeEmailLink(link.UTF8String);
    }
}

- (void)confirmChangeEmailWithLink:(NSString *)link password:(NSString *)password delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->confirmChangeEmail(link.UTF8String, password.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)confirmChangeEmailWithLink:(NSString *)link password:(NSString *)password {
    if (self.megaApi) {
        self.megaApi->confirmChangeEmail(link.UTF8String, password.UTF8String);
    }
}

- (void)contactLinkCreateRenew:(BOOL)renew delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->contactLinkCreate(renew, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)contactLinkCreateRenew:(BOOL)renew {
    if (self.megaApi) {
        self.megaApi->contactLinkCreate(renew);
    }
}

- (void)contactLinkQueryWithHandle:(uint64_t)handle delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->contactLinkQuery(handle, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)contactLinkQueryWithHandle:(uint64_t)handle {
    if (self.megaApi) {
        self.megaApi->contactLinkQuery(handle);
    }
}

- (void)contactLinkDeleteWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->contactLinkDelete(INVALID_HANDLE, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)contactLinkDelete {
    if (self.megaApi) {
        self.megaApi->contactLinkDelete();
    }
}

- (void)keepMeAliveWithType:(KeepMeAlive)type enable:(BOOL)enable delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->keepMeAlive((int) type, enable, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)keepMeAliveWithType:(KeepMeAlive)type enable:(BOOL)enable {
    if (self.megaApi) {
        self.megaApi->keepMeAlive((int) type, enable);
    }
}

- (void)whyAmIBlockedWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->whyAmIBlocked([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)whyAmIBlocked {
    if (self.megaApi) {
        self.megaApi->whyAmIBlocked();
    }
}

- (void)getPSAWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getPSA([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getPSA{
    if (self.megaApi) {
        self.megaApi->getPSA();
    }
}

- (void)getURLPublicServiceAnnouncementWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getPSAWithUrl([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)setPSAWithIdentifier:(NSInteger)identifier delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setPSA((int)identifier, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)setPSAWithIdentifier:(NSInteger)identifier {
    if (self.megaApi) {
        self.megaApi->setPSA((int)identifier);
    }
}

- (void)acknowledgeUserAlertsWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->acknowledgeUserAlerts([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)acknowledgeUserAlerts {
    if (self.megaApi) {
        self.megaApi->acknowledgeUserAlerts();
    }
}

#pragma mark - Notifications

- (void)getLastReadNotificationWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getLastReadNotification([self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)setLastReadNotificationWithNotificationId:(uint32_t)notificationId delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setLastReadNotification(notificationId, [self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (nullable MEGAIntegerList *)getEnabledNotifications {
    if (self.megaApi == nil) return nil;

    MegaIntegerList* enabledNotifications = self.megaApi->getEnabledNotifications();
    
    return enabledNotifications != nil ? [[MEGAIntegerList alloc] initWithMegaIntegerList:enabledNotifications cMemoryOwn:YES] : nil;
}

- (void)getNotificationsWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getNotifications([self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

#pragma mark - Filesystem changes Requests

- (void)createFolderWithName:(NSString *)name parent:(MEGANode *)parent delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->createFolder(name.UTF8String, parent.getCPtr, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)createFolderWithName:(NSString *)name parent:(MEGANode *)parent {
    if (self.megaApi) {
        self.megaApi->createFolder(name.UTF8String, parent.getCPtr);
    }
}

- (void)moveNode:(MEGANode *)node newParent:(MEGANode *)newParent delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->moveNode(node.getCPtr, newParent.getCPtr, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)moveNode:(MEGANode *)node newParent:(MEGANode *)newParent {
    if (self.megaApi) {
        self.megaApi->moveNode(node.getCPtr, newParent.getCPtr);
    }
}

- (void)moveNode:(MEGANode *)node newParent:(MEGANode *)newParent newName:(NSString *)newName delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->moveNode(node.getCPtr, newParent.getCPtr, newName.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)moveNode:(MEGANode *)node newParent:(MEGANode *)newParent newName:(NSString *)newName {
    if (self.megaApi) {
        self.megaApi->moveNode(node.getCPtr, newParent.getCPtr, newName.UTF8String);
    }
}

- (void)copyNode:(MEGANode *)node newParent:(MEGANode *)newParent delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->copyNode(node.getCPtr, newParent.getCPtr, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)copyNode:(MEGANode *)node newParent:(MEGANode *)newParent {
    if (self.megaApi) {
        self.megaApi->copyNode(node.getCPtr, newParent.getCPtr);
    }
}

- (void)copyNode:(MEGANode *)node newParent:(MEGANode *)newParent newName:(NSString *)newName delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->copyNode(node.getCPtr, newParent.getCPtr, newName.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)copyNode:(MEGANode *)node newParent:(MEGANode *)newParent newName:(NSString *)newName {
    if (self.megaApi) {
        self.megaApi->copyNode(node.getCPtr, newParent.getCPtr, newName.UTF8String);
    }
}

- (void)renameNode:(MEGANode *)node newName:(NSString *)newName delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->renameNode(node.getCPtr, newName.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)renameNode:(MEGANode *)node newName:(NSString *)newName {
    if (self.megaApi) {
        self.megaApi->renameNode(node.getCPtr, newName.UTF8String);
    }
}

- (void)removeNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->remove(node.getCPtr, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)removeNode:(MEGANode *)node {
    if (self.megaApi) {
        self.megaApi->remove(node.getCPtr);
    }
}

- (void)removeVersionsWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->removeVersions([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)removeVersions {
    if (self.megaApi) {
        self.megaApi->removeVersions();
    }
}

- (void)removeVersionNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->removeVersion(node.getCPtr, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)removeVersionNode:(MEGANode *)node {
    if (self.megaApi) {
        self.megaApi->removeVersion(node.getCPtr);
    }
}

- (void)restoreVersionNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->restoreVersion(node.getCPtr, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)restoreVersionNode:(MEGANode *)node {
    if (self.megaApi) {
        self.megaApi->restoreVersion(node.getCPtr);
    }
}

- (void)cleanRubbishBinWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->cleanRubbishBin([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)cleanRubbishBin {
    if (self.megaApi) {
        self.megaApi->cleanRubbishBin();
    }
}

#pragma mark - Sharing Requests

- (void)shareNode:(MEGANode *)node withUser:(MEGAUser *)user level:(NSInteger)level delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->share(node.getCPtr, user.getCPtr, (int)level, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)shareNode:(MEGANode *)node withUser:(MEGAUser *)user level:(NSInteger)level {
    if (self.megaApi) {
        self.megaApi->share(node.getCPtr, user.getCPtr, (int)level);
    }
}

- (void)shareNode:(MEGANode *)node withEmail:(NSString *)email level:(NSInteger)level delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->share(node.getCPtr, email.UTF8String, (int)level, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)shareNode:(MEGANode *)node withEmail:(NSString *)email level:(NSInteger)level {
    if (self.megaApi) {
        self.megaApi->share(node.getCPtr, email.UTF8String, (int)level);
    }
}

- (void)importMegaFileLink:(NSString *)megaFileLink parent:(MEGANode *)parent delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->importFileLink(megaFileLink.UTF8String, parent.getCPtr, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)importMegaFileLink:(NSString *)megaFileLink parent:(MEGANode *)parent {
    if (self.megaApi) {
        self.megaApi->importFileLink(megaFileLink.UTF8String, parent.getCPtr);
    }
}


- (void)decryptPasswordProtectedLink:(NSString *)link password:(NSString *)password delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->decryptPasswordProtectedLink(link.UTF8String, password.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)decryptPasswordProtectedLink:(NSString *)link password:(NSString *)password {
    if (self.megaApi) {
        self.megaApi->decryptPasswordProtectedLink(link.UTF8String, password.UTF8String);
    }
}

- (void)encryptLinkWithPassword:(NSString *)link password:(NSString *)password delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->encryptLinkWithPassword(link.UTF8String, password.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)encryptLinkWithPassword:(NSString *)link password:(NSString *)password {
    if (self.megaApi) {
        self.megaApi->encryptLinkWithPassword(link.UTF8String, password.UTF8String);
    }
}

- (void)publicNodeForMegaFileLink:(NSString *)megaFileLink delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getPublicNode(megaFileLink.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}


- (void)getDownloadUrl:(MEGANode *)node singleUrl:(BOOL)singleUrl delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getDownloadUrl(node.getCPtr, singleUrl, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)publicNodeForMegaFileLink:(NSString *)megaFileLink {
    if (self.megaApi) {
        self.megaApi->getPublicNode(megaFileLink.UTF8String);
    }
}

- (NSString *)buildPublicLinkForHandle:(NSString *)publicHandle key:(NSString *)key isFolder:(BOOL)isFolder {
    if (self.megaApi == nil) return nil;
    const char *link = self.megaApi->buildPublicLink(publicHandle.UTF8String, key.UTF8String, isFolder);
    
    if (!link) return nil;
    NSString *stringLink = [NSString.alloc initWithUTF8String:link];
    
    delete [] link;
    return stringLink;
}

- (void)setNodeLabel:(MEGANode *)node label:(MEGANodeLabel)label delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setNodeLabel(node.getCPtr, (int)label, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)setNodeLabel:(MEGANode *)node label:(MEGANodeLabel)label {
    if (self.megaApi) {
        self.megaApi->setNodeLabel(node.getCPtr, (int)label);
    }
}

- (void)resetNodeLabel:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->resetNodeLabel(node.getCPtr, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)resetNodeLabel:(MEGANode *)node {
    if (self.megaApi) {
        self.megaApi->resetNodeLabel(node.getCPtr);
    }
}

- (void)setNodeFavourite:(MEGANode *)node favourite:(BOOL)favourite delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setNodeFavourite(node.getCPtr, favourite, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)setNodeFavourite:(MEGANode *)node favourite:(BOOL)favourite {
    if (self.megaApi) {
        self.megaApi->setNodeFavourite(node.getCPtr, favourite);
    }
}

- (void)setNodeSensitive:(MEGANode *)node sensitive:(BOOL)sensitive delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setNodeSensitive(node.getCPtr, sensitive, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)setNodeSensitive:(MEGANode *)node sensitive:(BOOL)sensitive {
    if (self.megaApi) {
        self.megaApi->setNodeSensitive(node.getCPtr, sensitive);
    }
}

- (void)setDescription:(nullable NSString *)description
               forNode:(MEGANode *)node
              delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setNodeDescription(
                                         node.getCPtr,
                                         description.UTF8String,
                                         [self createDelegateMEGARequestListener:delegate
                                                                  singleListener:YES
                                                                       queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)favouritesForParent:(nullable MEGANode *)node count:(NSInteger)count delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getFavourites(node.getCPtr, (int)count, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)favouritesForParent:(nullable MEGANode *)node count:(NSInteger)count {
    if (self.megaApi) {
        self.megaApi->getFavourites(node.getCPtr, (int)count);
    }
}

- (void)createSet:(nullable NSString *)name type:(MEGASetType)type delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->createSet(name.UTF8String, (int)type, [self createDelegateMEGARequestListener:delegate
                                                                                     singleListener:YES
                                                                                          queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)exportSet:(MEGAHandle)sid delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->exportSet(sid, [self createDelegateMEGARequestListener:delegate
                                                              singleListener:YES
                                                                   queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)disableExportSet:(MEGAHandle)sid delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->disableExportSet(sid, [self createDelegateMEGARequestListener:delegate
                                                                     singleListener:YES
                                                                          queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)stopPublicSetPreview {
    if (self.megaApi) {
        self.megaApi->stopPublicSetPreview();
    }
}

- (BOOL)inPublicSetPreview {
    if (self.megaApi) {
        return self.megaApi->inPublicSetPreview();
    }
}

- (nullable MEGASet *)publicSetInPreview {
    if (self.megaApi) {
        MegaSet *set = self.megaApi->getPublicSetInPreview();
        return set ? [[MEGASet alloc] initWithMegaSet:set->copy() cMemoryOwn:YES] : nil;
    }
}

- (void)fetchPublicSet:(NSString *)publicSetLink delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->fetchPublicSet(publicSetLink.UTF8String, [self createDelegateMEGARequestListener:delegate
                                                                                        singleListener:YES
                                                                                             queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)previewElementNode:(MEGAHandle)eid delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getPreviewElementNode(eid, [self createDelegateMEGARequestListener:delegate
                                                                          singleListener:YES
                                                                               queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)updateSetName:(MEGAHandle)sid name:(NSString *)name delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->updateSetName(sid, name.UTF8String, [self createDelegateMEGARequestListener:delegate
                                                                                   singleListener:YES
                                                                                        queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)removeSet:(MEGAHandle)sid delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->removeSet(sid, [self createDelegateMEGARequestListener:delegate
                                                              singleListener:YES
                                                                   queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)putSetCover:(MEGAHandle)sid eid:(MEGAHandle)eid delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->putSetCover(sid, eid, [self createDelegateMEGARequestListener:delegate
                                                                     singleListener:YES
                                                                          queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)createSetElement:(MEGAHandle)sid
                  nodeId:(MEGAHandle)nodeId
                    name:(NSString *)name
                delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->createSetElement(sid,
                                       nodeId,
                                       name.UTF8String,
                                       [self createDelegateMEGARequestListener:delegate
                                                                singleListener:YES
                                                                     queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)updateSetElement:(MEGAHandle)sid
                     eid:(MEGAHandle)eid
                    name:(NSString *)name
                delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->updateSetElementName(sid,
                                           eid,
                                           name.UTF8String,
                                           [self createDelegateMEGARequestListener:delegate
                                                                    singleListener:YES
                                                                         queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)updateSetElementOrder:(MEGAHandle)sid
                          eid:(MEGAHandle)eid
                        order:(int64_t)order
                     delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->updateSetElementOrder(sid,
                                            eid,
                                            order,
                                            [self createDelegateMEGARequestListener:delegate
                                                                     singleListener:YES
                                                                          queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)removeSetElement:(MEGAHandle)sid
                     eid:(MEGAHandle)eid
                delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->removeSetElement(sid, eid, [self createDelegateMEGARequestListener:delegate
                                                                          singleListener:YES
                                                                               queueType:ListenerQueueTypeCurrent]);
    }
}

- (nullable MEGASet *)setBySid:(MEGAHandle)sid {
    if (self.megaApi == nil || sid == ::mega::INVALID_HANDLE) return nil;
    
    MegaSet *set = self.megaApi->getSet(sid);
    return set ? [[MEGASet alloc] initWithMegaSet:set->copy() cMemoryOwn:YES] : nil;
}

- (BOOL)isExportedSet:(MEGAHandle)sid {
    if (self.megaApi == nil || sid == ::mega::INVALID_HANDLE) return NO;
    
    return self.megaApi->isExportedSet(sid);
}

- (NSArray<MEGASet *> *)megaSets {
    if (self.megaApi == nil) return nil;
    
    MegaSetList *setList = self.megaApi->getSets();
    int size = setList->size();
    
    NSMutableArray *sets = [[NSMutableArray alloc] initWithCapacity:size];
    
    for (int i = 0; i < size; i++) {
        MEGASet *megaSet = [[MEGASet alloc] initWithMegaSet:setList->get(i)->copy() cMemoryOwn:YES];
        [sets addObject:megaSet];
    }
    
    delete setList;
    
    return [sets copy];
}

- (MEGAHandle)megaSetCoverBySid:(MEGAHandle)sid {
    if (self.megaApi == nil || sid == ::mega::INVALID_HANDLE) return ::mega::INVALID_HANDLE;
    
    return self.megaApi->getSetCover(sid);
}

- (nullable NSString *)publicLinkForExportedSetBySid:(MEGAHandle)sid {
    if (self.megaApi == nil || sid == ::mega::INVALID_HANDLE) return NULL;
    
    const char *link = self.megaApi->getPublicLinkForExportedSet(sid);
    if (!link) return nil;
    
    NSString *linkStr = [[NSString alloc] initWithUTF8String:link];
    
    delete [] link;
    return linkStr;
}

- (nullable MEGASetElement *)megaSetElementBySid:(MEGAHandle)sid eid:(MEGAHandle)eid {
    if (self.megaApi == nil || sid == ::mega::INVALID_HANDLE || eid == ::mega::INVALID_HANDLE) return nil;
    
    MegaSetElement *element = self.megaApi->getSetElement(sid, eid);
    MEGASetElement *setElement = element ? [[MEGASetElement alloc] initWithMegaSetElement:element->copy() cMemoryOwn:YES] : nil;
    
    delete element;
    
    return setElement;
}

- (NSArray<MEGASetElement *> *)megaSetElementsBySid:(MEGAHandle)sid includeElementsInRubbishBin:(BOOL)includeElementsInRubbishBin {
    if (self.megaApi == nil) return nil;
    
    MegaSetElementList *setElementList = self.megaApi->getSetElements(sid, includeElementsInRubbishBin);
    int size = setElementList->size();
    
    NSMutableArray *setElements = [[NSMutableArray alloc] initWithCapacity:size];
    
    for (int i = 0; i < size; i++) {
        MEGASetElement *megaSetElement = [[MEGASetElement alloc] initWithMegaSetElement:setElementList->get(i)->copy() cMemoryOwn:YES];
        [setElements addObject:megaSetElement];
    }
    
    delete setElementList;
    
    return [setElements copy];
}

- (NSArray<MEGASetElement *> *)publicSetElementsInPreview {
    if (self.megaApi == nil) return nil;
    
    MegaSetElementList *setElementList = self.megaApi->getPublicSetElementsInPreview();
    int size = setElementList->size();
    
    NSMutableArray *setElements = [[NSMutableArray alloc] initWithCapacity:size];
    
    for (int i = 0; i < size; i++) {
        MEGASetElement *megaSetElement = [[MEGASetElement alloc] initWithMegaSetElement:setElementList->get(i)->copy() cMemoryOwn:YES];
        [setElements addObject:megaSetElement];
    }
    
    delete setElementList;
    
    return [setElements copy];
}

- (NSUInteger)megaSetElementCount:(MEGAHandle)sid includeElementsInRubbishBin:(BOOL)includeElementsInRubbishBin {
    if (self.megaApi == nil || sid == ::mega::INVALID_HANDLE) return 0;
    
    return self.megaApi->getSetElementCount(sid, includeElementsInRubbishBin);
}

- (void)setNodeCoordinates:(MEGANode *)node latitude:(NSNumber *)latitude longitude:(NSNumber *)longitude delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setNodeCoordinates(node.getCPtr, (latitude ? latitude.doubleValue : MegaNode::INVALID_COORDINATE), (longitude ? longitude.doubleValue : MegaNode::INVALID_COORDINATE), [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)setNodeCoordinates:(MEGANode *)node latitude:(NSNumber *)latitude longitude:(NSNumber *)longitude {
    if (self.megaApi) {
        self.megaApi->setNodeCoordinates(node.getCPtr, (latitude ? latitude.doubleValue : MegaNode::INVALID_COORDINATE), (longitude ? longitude.doubleValue : MegaNode::INVALID_COORDINATE));
    }
}

- (void)setUnshareableNodeCoordinates:(MEGANode *)node latitude:(double)latitude longitude:(double)longitude delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setUnshareableNodeCoordinates(node.getCPtr, latitude, longitude , [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)setUnshareableNodeCoordinates:(MEGANode *)node latitude:(double)latitude longitude:(double)longitude {
    if (self.megaApi) {
        self.megaApi->setUnshareableNodeCoordinates(node.getCPtr, latitude, longitude);
    }
}

- (void)exportNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->exportNode(node.getCPtr, 0, false, false, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)exportNode:(MEGANode *)node {
    if (self.megaApi) {
        self.megaApi->exportNode(node.getCPtr, 0, false, false);
    }
}

- (void)exportNode:(MEGANode *)node expireTime:(NSDate *)expireTime delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->exportNode(node.getCPtr, (int64_t)[expireTime timeIntervalSince1970], false, false, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)exportNode:(MEGANode *)node expireTime:(NSDate *)expireTime {
    if (self.megaApi) {
        self.megaApi->exportNode(node.getCPtr, (int64_t)[expireTime timeIntervalSince1970], false, false);
    }
}

- (void)disableExportNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->disableExport(node.getCPtr, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)disableExportNode:(MEGANode *)node {
    if (self.megaApi) {
        self.megaApi->disableExport(node.getCPtr);
    }
}

- (void)openShareDialog:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->openShareDialog(node.getCPtr, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

#pragma mark - Attributes Requests

- (void)getThumbnailNode:(MEGANode *)node destinationFilePath:(NSString *)destinationFilePath delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getThumbnail(node.getCPtr, destinationFilePath.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getThumbnailNode:(MEGANode *)node destinationFilePath:(NSString *)destinationFilePath {
    if (self.megaApi) {
        self.megaApi->getThumbnail(node.getCPtr, destinationFilePath.UTF8String);
    }
}

- (void)cancelGetThumbnailNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->cancelGetThumbnail(node.getCPtr, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)cancelGetThumbnailNode:(MEGANode *)node {
    if (self.megaApi) {
        self.megaApi->cancelGetThumbnail(node.getCPtr);
    }
}

- (void)setThumbnailNode:(MEGANode *)node sourceFilePath:(NSString *)sourceFilePath delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setThumbnail(node.getCPtr, sourceFilePath.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)setThumbnailNode:(MEGANode *)node sourceFilePath:(NSString *)sourceFilePath {
    if (self.megaApi) {
        self.megaApi->setThumbnail(node.getCPtr, sourceFilePath.UTF8String);
    }
}

- (void)getPreviewNode:(MEGANode *)node destinationFilePath:(NSString *)destinationFilePath delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getPreview(node.getCPtr, destinationFilePath.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getPreviewNode:(MEGANode *)node destinationFilePath:(NSString *)destinationFilePath {
    if (self.megaApi) {
        self.megaApi->getPreview(node.getCPtr, destinationFilePath.UTF8String);
    }
}

- (void)cancelGetPreviewNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->cancelGetPreview(node.getCPtr, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)cancelGetPreviewNode:(MEGANode *)node {
    if (self.megaApi) {
        self.megaApi->cancelGetPreview(node.getCPtr);
    }
}

- (void)setPreviewNode:(MEGANode *)node sourceFilePath:(NSString *)sourceFilePath delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setPreview(node.getCPtr, sourceFilePath.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)setPreviewNode:(MEGANode *)node sourceFilePath:(NSString *)sourceFilePath {
    if (self.megaApi) {
        self.megaApi->setPreview(node.getCPtr, sourceFilePath.UTF8String);
    }
}

- (void)getAvatarUser:(MEGAUser *)user destinationFilePath:(NSString *)destinationFilePath delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getUserAvatar(user.getCPtr, destinationFilePath.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getAvatarUser:(MEGAUser *)user destinationFilePath:(NSString *)destinationFilePath {
    if (self.megaApi) {
        self.megaApi->getUserAvatar(user.getCPtr, destinationFilePath.UTF8String);
    }
}

- (void)getAvatarUserWithEmailOrHandle:(NSString *)emailOrHandle destinationFilePath:(NSString *)destinationFilePath delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getUserAvatar(emailOrHandle.UTF8String, destinationFilePath.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getAvatarUserWithEmailOrHandle:(NSString *)emailOrHandle destinationFilePath:(NSString *)destinationFilePath delegate:(id<MEGARequestDelegate>)delegate queueType:(ListenerQueueType)queueType {
    if (self.megaApi) {
        self.megaApi->getUserAvatar(emailOrHandle.UTF8String, destinationFilePath.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES queueType:queueType]);
    }
}


- (void)getAvatarUserWithEmailOrHandle:(NSString *)emailOrHandle destinationFilePath:(NSString *)destinationFilePath {
    if (self.megaApi) {
        self.megaApi->getUserAvatar(emailOrHandle.UTF8String, destinationFilePath.UTF8String);
    }
}

+ (NSString *)avatarColorForUser:(MEGAUser *)user {
    const char *val = MegaApi::getUserAvatarColor(user.getCPtr);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

+ (NSString *)avatarColorForBase64UserHandle:(NSString *)base64UserHandle {
    const char *val = MegaApi::getUserAvatarColor(base64UserHandle.UTF8String);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

+ (NSString *)avatarSecondaryColorForUser:(MEGAUser *)user {
    const char *val = MegaApi::getUserAvatarSecondaryColor(user.getCPtr);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

+ (NSString *)avatarSecondaryColorForBase64UserHandle:(NSString *)base64UserHandle {
    const char *val = MegaApi::getUserAvatarSecondaryColor(base64UserHandle.UTF8String);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (void)setAvatarUserWithSourceFilePath:(NSString *)sourceFilePath delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setAvatar(sourceFilePath.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)setAvatarUserWithSourceFilePath:(NSString *)sourceFilePath {
    if (self.megaApi) {
        self.megaApi->setAvatar(sourceFilePath.UTF8String);
    }
}

- (void)getUserAttributeForUser:(MEGAUser *)user type:(MEGAUserAttribute)type {
    if (self.megaApi) {
        self.megaApi->getUserAttribute(user.getCPtr, (int)type);
    }
}

- (void)getUserAttributeForUser:(MEGAUser *)user type:(MEGAUserAttribute)type delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getUserAttribute(user.getCPtr, (int)type, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getUserAttributeForEmailOrHandle:(NSString *)emailOrHandle type:(MEGAUserAttribute)type {
    if (self.megaApi) {
        self.megaApi->getUserAttribute(emailOrHandle.UTF8String, (int)type);
    }
}

- (void)getUserAttributeForEmailOrHandle:(NSString *)emailOrHandle type:(MEGAUserAttribute)type delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getUserAttribute(emailOrHandle.UTF8String, (int)type, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getUserAttributeType:(MEGAUserAttribute)type {
    if (self.megaApi) {
        self.megaApi->getUserAttribute((int)type);
    }
}

- (void)getUserAttributeType:(MEGAUserAttribute)type delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getUserAttribute((int)type, [self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)setUserAttributeType:(MEGAUserAttribute)type value:(NSString *)value {
    if (self.megaApi) {
        self.megaApi->setUserAttribute((int)type, value.UTF8String);
    }
}

- (void)setUserAttributeType:(MEGAUserAttribute)type value:(NSString *)value delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setUserAttribute((int)type, value.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)setUserAttributeType:(MEGAUserAttribute)type key:(NSString *)key value:(NSString *)value {
    if (self.megaApi) {
        const char *base64Value = MegaApi::binaryToBase64((const char *)value.UTF8String, value.length);
        MegaStringMap *stringMap = MegaStringMap::createInstance();
        stringMap->set(key.UTF8String, base64Value);

        self.megaApi->setUserAttribute((int)type, stringMap);
    }
}

- (void)setUserAttributeType:(MEGAUserAttribute)type key:(NSString *)key value:(NSString *)value delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        const char *base64Value = MegaApi::binaryToBase64((const char *)value.UTF8String, value.length);
        MegaStringMap *stringMap = MegaStringMap::createInstance();
        stringMap->set(key.UTF8String, base64Value);

        self.megaApi->setUserAttribute((int)type, stringMap, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getUserAliasWithHandle:(uint64_t)handle delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getUserAlias(handle, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getUserAliasWithHandle:(uint64_t)handle {
    if (self.megaApi) {
        self.megaApi->getUserAlias(handle);
    }
}

- (void)setUserAlias:(nullable NSString *)alias forHandle:(uint64_t)handle delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setUserAlias(handle,
                                   alias.UTF8String,
                                   [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)setUserAlias:(nullable NSString *)alias forHandle:(uint64_t)handle {
    if (self.megaApi) {
        self.megaApi->setUserAlias(handle, alias.UTF8String);
    }
}

#pragma mark - Account management Requests

- (void)getAccountDetailsWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getAccountDetails([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getAccountDetails {
    if (self.megaApi) {
        self.megaApi->getAccountDetails();
    }
}

- (void)queryTransferQuotaWithSize:(long long)size delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->queryTransferQuota(size, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)queryTransferQuotaWithSize:(long long)size {
    if (self.megaApi) {
        self.megaApi->queryTransferQuota(size);
    }
}

- (void)getRecommendedProLevelWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getRecommendedProLevel([self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)getPricingWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getPricing([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getPricing {
    if (self.megaApi) {
        self.megaApi->getPricing();
    }
}

- (void)getPaymentIdForProductHandle:(uint64_t)productHandle delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getPaymentId(productHandle, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getPaymentIdForProductHandle:(uint64_t)productHandle {
    if (self.megaApi) {
        self.megaApi->getPaymentId(productHandle);
    }
}

- (void)submitPurchase:(MEGAPaymentMethod)gateway receipt:(NSString *)receipt delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->submitPurchaseReceipt((int)gateway, receipt.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)submitPurchase:(MEGAPaymentMethod)gateway receipt:(NSString *)receipt {
    if (self.megaApi) {
        self.megaApi->submitPurchaseReceipt((int)gateway, receipt.UTF8String);
    }
}

- (void)creditCardCancelSubscriptions:(nullable NSString *)reason subscriptionId:(nullable NSString *)subscriptionId canContact:(BOOL)canContact delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->creditCardCancelSubscriptions(reason.UTF8String, subscriptionId.UTF8String, canContact, [self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)creditCardCancelSubscriptionsWithReasons:(nullable MEGACancelSubscriptionReasonList *)reasonList subscriptionId:(nullable NSString *)subscriptionId canContact:(BOOL)canContact delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->creditCardCancelSubscriptions(reasonList.getCPtr, subscriptionId.UTF8String, canContact, [self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)changePassword:(NSString *)oldPassword newPassword:(NSString *)newPassword delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->changePassword(oldPassword.UTF8String, newPassword.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)changePassword:(NSString *)oldPassword newPassword:(NSString *)newPassword {
    if (self.megaApi) {
        self.megaApi->changePassword(oldPassword.UTF8String, newPassword.UTF8String);
    }
}

- (void)masterKeyExportedWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->masterKeyExported([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)masterKeyExported {
    if (self.megaApi) {
        self.megaApi->masterKeyExported();
    }
}

- (void)passwordReminderDialogSucceededWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->passwordReminderDialogSucceeded([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)passwordReminderDialogSucceeded {
    if (self.megaApi) {
        self.megaApi->passwordReminderDialogSucceeded();
    }
}

- (void)passwordReminderDialogSkippedWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->passwordReminderDialogSkipped([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)passwordReminderDialogSkipped {
    if (self.megaApi) {
        self.megaApi->passwordReminderDialogSkipped();
    }
}

- (void)passwordReminderDialogBlockedWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->passwordReminderDialogBlocked([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)passwordReminderDialogBlocked {
    if (self.megaApi) {
        self.megaApi->passwordReminderDialogBlocked();
    }
}

- (void)shouldShowPasswordReminderDialogAtLogout:(BOOL)atLogout delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->shouldShowPasswordReminderDialog(atLogout, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)shouldShowPasswordReminderDialogAtLogout:(BOOL)atLogout {
    if (self.megaApi) {
        self.megaApi->shouldShowPasswordReminderDialog(atLogout);
    }
}

- (void)isMasterKeyExportedWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->isMasterKeyExported([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)isMasterKeyExported {
    if (self.megaApi) {
        self.megaApi->isMasterKeyExported();
    }
}

- (void)getVisibleTermsOfServiceWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getVisibleTermsOfService([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)setVisibleTermsOfService:(BOOL)visible delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setVisibleTermsOfService(visible, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

#ifdef ENABLE_CHAT

- (void)enableRichPreviews:(BOOL)enable delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->enableRichPreviews(enable, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)enableRichPreviews:(BOOL)enable {
    if (self.megaApi) {
        self.megaApi->enableRichPreviews(enable);
    }
}

- (void)isRichPreviewsEnabledWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->isRichPreviewsEnabled([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)isRichPreviewsEnabled {
    if (self.megaApi) {
        self.megaApi->isRichPreviewsEnabled();
    }
}

- (void)shouldShowRichLinkWarningWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->shouldShowRichLinkWarning([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)shouldShowRichLinkWarning {
    if (self.megaApi) {
        self.megaApi->shouldShowRichLinkWarning();
    }
}

- (void)setRichLinkWarningCounterValue:(NSUInteger)value delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setRichLinkWarningCounterValue((int)value, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)setRichLinkWarningCounterValue:(NSUInteger)value {
    if (self.megaApi) {
        self.megaApi->setRichLinkWarningCounterValue((int)value);
    }
}

- (void)enableGeolocationWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->enableGeolocation([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)enableGeolocation {
    if (self.megaApi) {
        self.megaApi->enableGeolocation();
    }
}

- (void)isGeolocationEnabledWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->isGeolocationEnabled([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)isGeolocationEnabled {
    if (self.megaApi) {
        self.megaApi->isGeolocationEnabled();
    }
}

#endif

- (void)setMyChatFilesFolderWithHandle:(uint64_t)handle delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setMyChatFilesFolder(handle, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)setMyChatFilesFolderWithHandle:(uint64_t)handle {
    if (self.megaApi) {
        self.megaApi->setMyChatFilesFolder(handle);
    }
}

- (void)getMyChatFilesFolderWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getMyChatFilesFolder([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getMyChatFilesFolder {
    if (self.megaApi) {
        self.megaApi->getMyChatFilesFolder();
    }
}

- (void)setCameraUploadsFolderWithHandle:(uint64_t)handle delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setCameraUploadsFolder(handle, [self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)setCameraUploadsFolderWithHandle:(uint64_t)handle {
    if (self.megaApi) {
        self.megaApi->setCameraUploadsFolder(handle);
    }
}

- (void)getCameraUploadsFolderWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getCameraUploadsFolder([self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)getCameraUploadsFolder {
    if (self.megaApi) {
        self.megaApi->getCameraUploadsFolder();
    }
}

- (void)getCameraUploadsFolderSecondaryWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getCameraUploadsFolderSecondary([self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)getCameraUploadsFolderSecondary {
    if (self.megaApi) {
        self.megaApi->getCameraUploadsFolderSecondary();
    }
}

- (void)getRubbishBinAutopurgePeriodWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getRubbishBinAutopurgePeriod([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getRubbishBinAutopurgePeriod {
    if (self.megaApi) {
        self.megaApi->getRubbishBinAutopurgePeriod();
    }
}

- (void)setRubbishBinAutopurgePeriodInDays:(NSInteger)days delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setRubbishBinAutopurgePeriod((int)days, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)setRubbishBinAutopurgePeriodInDays:(NSInteger)days {
    if (self.megaApi) {
        self.megaApi->setRubbishBinAutopurgePeriod((int)days);
    }
}

- (void)useHttpsOnly:(BOOL)httpsOnly delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->useHttpsOnly(httpsOnly, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)useHttpsOnly:(BOOL)httpsOnly {
    if (self.megaApi) {
        self.megaApi->useHttpsOnly(httpsOnly);
    }
}

- (BOOL)usingHttpsOnly {
    if (self.megaApi == nil) return NO;
    return self.megaApi->usingHttpsOnly();
}

- (void)inviteContactWithEmail:(NSString *)email message:(NSString *)message action:(MEGAInviteAction)action delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->inviteContact(email.UTF8String, message.UTF8String, (int)action, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)inviteContactWithEmail:(NSString *)email message:(NSString *)message action:(MEGAInviteAction)action {
    if (self.megaApi) {
        self.megaApi->inviteContact(email.UTF8String, message.UTF8String, (int)action);
    }
}

- (void)inviteContactWithEmail:(NSString *)email message:(NSString *)message action:(MEGAInviteAction)action handle:(uint64_t)handle delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->inviteContact(email.UTF8String, message.UTF8String, (int)action, handle, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)inviteContactWithEmail:(NSString *)email message:(NSString *)message action:(MEGAInviteAction)action handle:(uint64_t)handle {
    if (self.megaApi) {
        self.megaApi->inviteContact(email.UTF8String, message.UTF8String, (int)action, handle);
    }
}

- (void)replyContactRequest:(MEGAContactRequest *)request action:(MEGAReplyAction)action delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->replyContactRequest(request.getCPtr, (int)action, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)replyContactRequest:(MEGAContactRequest *)request action:(MEGAReplyAction)action {
    if (self.megaApi) {
        self.megaApi->replyContactRequest(request.getCPtr, (int)action);
    }
}

- (void)removeContactUser:(MEGAUser *)user delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->removeContact(user.getCPtr, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)removeContactUser:(MEGAUser *)user {
    if (self.megaApi) {
        self.megaApi->removeContact(user.getCPtr);
    }
}

- (void)submitFeedbackWithRating:(NSInteger)rating comment:(NSString *)comment delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->submitFeedback((int)rating, comment.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)submitFeedbackWithRating:(NSInteger)rating comment:(NSString *)comment {
    if (self.megaApi) {
        self.megaApi->submitFeedback((int)rating, comment.UTF8String);
    }
}

- (void)reportDebugEventWithText:(NSString *)text delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->reportDebugEvent(text.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)reportDebugEventWithText:(NSString *)text {
    if (self.megaApi) {
        self.megaApi->reportDebugEvent(text.UTF8String);
    }
}

- (void)getUserDataWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getUserData([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getUserData {
    if (self.megaApi) {
        self.megaApi->getUserData();
    }
}

- (void)getUserDataWithMEGAUser:(MEGAUser *)user delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getUserData(user.getCPtr, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getUserDataWithMEGAUser:(MEGAUser *)user {
    if (self.megaApi) {
        self.megaApi->getUserData(user.getCPtr);
    }
}

- (void)getUserDataWithUser:(NSString *)user delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getUserData(user.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getUserDataWithUser:(NSString *)user {
    if (self.megaApi) {
        self.megaApi->getUserData(user.UTF8String);
    }
}

- (void)getMiscFlagsWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getMiscFlags([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getMiscFlags {
    if (self.megaApi) {
        self.megaApi->getMiscFlags();
    }
}

- (void)killSession:(uint64_t)sessionHandle delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->killSession(sessionHandle, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)killSession:(uint64_t)sessionHandle {
    if (self.megaApi) {
        self.megaApi->killSession(sessionHandle);
    }
}

- (NSDate *)overquotaDeadlineDate {
    if (self.megaApi == nil) return nil;
    return [[NSDate alloc] initWithTimeIntervalSince1970:self.megaApi->getOverquotaDeadlineTs()];
}

- (NSArray<NSDate *> *)overquotaWarningDateList {
    if (self.megaApi == nil) return nil;
    MegaIntegerList *warningTimeIntervalList = self.megaApi->getOverquotaWarningsTs();
    int sizeOfWarningTimestamps = warningTimeIntervalList->size();
    NSMutableArray *warningDateList = [[NSMutableArray alloc] initWithCapacity:sizeOfWarningTimestamps];
    
    for (int i = 0; i < sizeOfWarningTimestamps; i++) {
        NSDate *warningDate = [[NSDate alloc] initWithTimeIntervalSince1970:warningTimeIntervalList->get(i)];
        [warningDateList addObject:warningDate];
    }
    return [warningDateList copy];
}

- (BOOL)setRLimitFileCount:(NSInteger)fileCount {
    if (self.megaApi == nil) return NO;
    return self.megaApi->platformSetRLimitNumFile((int)fileCount);
}

- (void)upgradeSecurityWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->upgradeSecurity([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

#pragma mark - Transfer

- (MEGATransfer *)transferByTag:(NSInteger)transferTag {
    if (self.megaApi == nil) return nil;
    MegaTransfer *transfer = self.megaApi->getTransferByTag((int)transferTag);
    
    return transfer ? [[MEGATransfer alloc] initWithMegaTransfer:transfer cMemoryOwn:YES] : nil;
}

- (void)startUploadForSupportWithLocalPath:(NSString *)localPath isSourceTemporary:(BOOL)isSourceTemporary delegate:(id<MEGATransferDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->startUploadForSupport(localPath.UTF8String, isSourceTemporary, [self createDelegateMEGATransferListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)startUploadForSupportWithLocalPath:(NSString *)localPath isSourceTemporary:(BOOL)isSourceTemporary {
    if (self.megaApi) {
        self.megaApi->startUploadForSupport(localPath.UTF8String, isSourceTemporary);
    }
}

- (void)startUploadWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent fileName:(nullable NSString *)fileName appData:(nullable NSString *)appData isSourceTemporary:(BOOL)isSourceTemporary startFirst:(BOOL)startFirst cancelToken:(nullable MEGACancelToken *)cancelToken {
    if (self.megaApi) {
        self.megaApi->startUpload(localPath.UTF8String, parent.getCPtr, fileName.UTF8String, MegaApi::INVALID_CUSTOM_MOD_TIME, appData.UTF8String, isSourceTemporary, startFirst, cancelToken.getCPtr);
    }
}

- (void)startUploadWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent fileName:(nullable NSString *)fileName appData:(nullable NSString *)appData isSourceTemporary:(BOOL)isSourceTemporary startFirst:(BOOL)startFirst cancelToken:(nullable MEGACancelToken *)cancelToken delegate:(id<MEGATransferDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->startUpload(localPath.UTF8String, parent.getCPtr, fileName.UTF8String, MegaApi::INVALID_CUSTOM_MOD_TIME, appData.UTF8String, isSourceTemporary, startFirst, cancelToken.getCPtr, [self createDelegateMEGATransferListener:delegate singleListener:YES]);
    }
}

- (void)startUploadForChatWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent appData:(nullable NSString *)appData isSourceTemporary:(BOOL)isSourceTemporary fileName:(nullable NSString*)fileName {
    if (self.megaApi) {
        self.megaApi->startUploadForChat(localPath.UTF8String, parent.getCPtr, appData.UTF8String, isSourceTemporary, fileName.UTF8String);
    }
}

- (void)startUploadForChatWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent appData:(nullable NSString *)appData isSourceTemporary:(BOOL)isSourceTemporary fileName:(nullable NSString*)fileName delegate:(id<MEGATransferDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->startUploadForChat(localPath.UTF8String, parent.getCPtr, appData.UTF8String, isSourceTemporary, fileName.UTF8String, [self createDelegateMEGATransferListener:delegate singleListener:YES]);
    }
}

- (void)startDownloadNode:(MEGANode *)node localPath:(NSString *)localPath  fileName:(nullable NSString*)fileName appData:(nullable NSString *)appData startFirst:(BOOL) startFirst cancelToken:(nullable MEGACancelToken *)cancelToken collisionCheck:(CollisionCheck)collisionCheck collisionResolution:(CollisionResolution)collisionResolution {
    if (self.megaApi) {
        self.megaApi->startDownload(node.getCPtr, localPath.UTF8String, fileName.UTF8String, appData.UTF8String, startFirst, cancelToken.getCPtr, (int)collisionCheck, (int)collisionResolution, false);
    }
}

- (void)startDownloadNode:(MEGANode *)node localPath:(NSString *)localPath  fileName:(nullable NSString*)fileName appData:(nullable NSString *)appData startFirst:(BOOL) startFirst cancelToken:(nullable MEGACancelToken *)cancelToken collisionCheck:(CollisionCheck)collisionCheck collisionResolution:(CollisionResolution)collisionResolution delegate:(id<MEGATransferDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->startDownload(node.getCPtr, localPath.UTF8String, fileName.UTF8String, appData.UTF8String, startFirst, cancelToken.getCPtr, (int)collisionCheck, (int)collisionResolution, false, [self createDelegateMEGATransferListener:delegate singleListener:YES]);
    }
}

- (void)startStreamingNode:(MEGANode *)node startPos:(NSNumber *)startPos size:(NSNumber *)size delegate:(id<MEGATransferDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->startStreaming(node.getCPtr, (startPos != nil) ? [startPos longLongValue] : 0, (size != nil) ? [size longLongValue] : 0, [self createDelegateMEGATransferListener:delegate singleListener:YES]);
    }
}

- (void)startStreamingNode:(MEGANode *)node startPos:(NSNumber *)startPos size:(NSNumber *)size {
    if (self.megaApi) {
        self.megaApi->startStreaming(node.getCPtr, (startPos != nil) ? [startPos longLongValue] : 0, (size != nil) ? [size longLongValue] : 0, NULL);
    }
}

- (void)cancelTransfer:(MEGATransfer *)transfer delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->cancelTransfer(transfer.getCPtr, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)cancelTransfer:(MEGATransfer *)transfer {
    if (self.megaApi) {
        self.megaApi->cancelTransfer(transfer.getCPtr);
    }
}

- (void)retryTransfer:(MEGATransfer *)transfer delegate:(id<MEGATransferDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->retryTransfer(transfer.getCPtr, [self createDelegateMEGATransferListener:delegate singleListener:YES]);
    }
}

- (void)retryTransfer:(MEGATransfer *)transfer {
    if (self.megaApi) {
        self.megaApi->retryTransfer(transfer.getCPtr);
    }
}

- (void)moveTransferToFirst:(MEGATransfer *)transfer delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->moveTransferToFirst(transfer.getCPtr, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)moveTransferToFirst:(MEGATransfer *)transfer {
    if (self.megaApi) {
        self.megaApi->moveTransferToFirst(transfer.getCPtr);
    }
}

- (void)moveTransferToLast:(MEGATransfer *)transfer delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->moveTransferToLast(transfer.getCPtr, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)moveTransferToLast:(MEGATransfer *)transfer {
    if (self.megaApi) {
        self.megaApi->moveTransferToLast(transfer.getCPtr);
    }
}

- (void)moveTransferBefore:(MEGATransfer *)transfer prevTransfer:(MEGATransfer *)prevTransfer delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->moveTransferBefore(transfer.getCPtr, prevTransfer.getCPtr, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)moveTransferBefore:(MEGATransfer *)transfer prevTransfer:(MEGATransfer *)prevTransfer {
    if (self.megaApi) {
        self.megaApi->moveTransferBefore(transfer.getCPtr, prevTransfer.getCPtr);
    }
}

- (void)cancelTransfersForDirection:(NSInteger)direction delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->cancelTransfers((int)direction, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)cancelTransfersForDirection:(NSInteger)direction {
    if (self.megaApi) {
        self.megaApi->cancelTransfers((int)direction);
    }
}

- (void)cancelTransferByTag:(NSInteger)transferTag delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->cancelTransferByTag((int)transferTag, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)cancelTransferByTag:(NSInteger)transferTag {
    if (self.megaApi) {
        self.megaApi->cancelTransferByTag((int)transferTag);
    }
}

- (void)pauseTransfers:(BOOL)pause delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->pauseTransfers(pause, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)pauseTransfers:(BOOL)pause {
    if (self.megaApi) {
        self.megaApi->pauseTransfers(pause);
    }
}

- (void)pauseTransfers:(BOOL)pause forDirection:(NSInteger)direction delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->pauseTransfers(pause, (int)direction, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)pauseTransfers:(BOOL)pause forDirection:(NSInteger)direction {
    if (self.megaApi) {
        self.megaApi->pauseTransfers(pause, (int)direction);
    }
}

- (void)pauseTransfer:(MEGATransfer *)transfer pause:(BOOL)pause delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->pauseTransfer(transfer.getCPtr, pause, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)pauseTransfer:(MEGATransfer *)transfer pause:(BOOL)pause {
    if (self.megaApi) {
        self.megaApi->pauseTransfer(transfer.getCPtr, pause);
    }
}

- (void)pauseTransferByTag:(NSInteger)transferTag pause:(BOOL)pause delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->pauseTransferByTag((int)transferTag, pause, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)pauseTransferByTag:(NSInteger)transferTag pause:(BOOL)pause {
    if (self.megaApi) {
        self.megaApi->pauseTransferByTag((int)transferTag, pause);
    }
}

- (BOOL)areTransferPausedForDirection:(NSInteger)direction {
    if (self.megaApi == nil) return NO;
    return self.megaApi->areTransfersPaused((int)direction);
}

- (void)requestBackgroundUploadURLWithFileSize:(int64_t)filesize mediaUpload:(MEGABackgroundMediaUpload *)mediaUpload delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->backgroundMediaUploadRequestUploadURL(filesize, mediaUpload.getCPtr, [self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)completeBackgroundMediaUpload:(MEGABackgroundMediaUpload *)mediaUpload fileName:(NSString *)fileName parentNode:(MEGANode *)parentNode fingerprint:(NSString *)fingerprint originalFingerprint:(NSString *)originalFingerprint binaryUploadToken:(NSData *)token delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        const char *base64Token = MegaApi::binaryToBase64((const char *)token.bytes, token.length);
        self.megaApi->backgroundMediaUploadComplete(mediaUpload.getCPtr, fileName.UTF8String, parentNode.getCPtr, fingerprint.UTF8String, originalFingerprint.UTF8String, base64Token, [self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (BOOL)ensureMediaInfo {
    if (self.megaApi == nil) return NO;
    return self.megaApi->ensureMediaInfo();
}

- (BOOL)testAllocationByAllocationCount:(NSUInteger)count allocationSize:(NSUInteger)size {
    if (self.megaApi == nil) return NO;
    return self.megaApi->testAllocation((unsigned)count, size);
}

#pragma mark - Filesystem inspection

- (NSInteger)numberChildrenForParent:(MEGANode *)parent {
    if (self.megaApi == nil) return 0;
    return self.megaApi->getNumChildren(parent.getCPtr);
}

- (NSInteger)numberChildFilesForParent:(MEGANode *)parent {
    if (self.megaApi == nil) return 0;
    return self.megaApi->getNumChildFiles(parent.getCPtr);
}

- (NSInteger)numberChildFoldersForParent:(MEGANode *)parent {
    if (self.megaApi == nil) return 0;
    return self.megaApi->getNumChildFolders(parent.getCPtr);
}

- (MEGANodeList *)childrenForParent:(MEGANode *)parent order:(NSInteger)order {
    if (self.megaApi == nil) return nil;
    return [[MEGANodeList alloc] initWithNodeList:self.megaApi->getChildren(parent.getCPtr, (int)order) cMemoryOwn:YES];
}

- (MEGANodeList *)childrenForParent:(MEGANode *)parent {
    if (self.megaApi == nil) return nil;
    return [[MEGANodeList alloc] initWithNodeList:self.megaApi->getChildren(parent.getCPtr) cMemoryOwn:YES];
}

- (MEGANodeList *)versionsForNode:(MEGANode *)node {
    if (self.megaApi == nil) return nil;
    return [[MEGANodeList alloc] initWithNodeList:self.megaApi->getVersions(node.getCPtr) cMemoryOwn:YES];
}

- (NSInteger)numberOfVersionsForNode:(MEGANode *)node {
    if (self.megaApi == nil) return 0;
    return self.megaApi->getNumVersions(node.getCPtr);
}

- (BOOL)hasVersionsForNode:(MEGANode *)node {
    if (self.megaApi == nil) return NO;
    return self.megaApi->hasVersions(node.getCPtr);
}

- (void)getFolderInfoForNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getFolderInfo(node.getCPtr, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getFolderInfoForNode:(MEGANode *)node {
    if (self.megaApi) {
        self.megaApi->getFolderInfo(node.getCPtr);
    }
}

- (MEGANode *)childNodeForParent:(MEGANode *)parent name:(NSString *)name {
    if (parent == nil || name == nil || self.megaApi == nil) return nil;
    
    MegaNode *node = self.megaApi->getChildNode([parent getCPtr], [name UTF8String]);
    
    return node ? [[MEGANode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MEGANode *)childNodeForParent:(MEGANode *)parent name:(NSString *)name type:(MEGANodeType)type {
    if (parent == nil || name == nil || self.megaApi == nil) return nil;
    
    MegaNode *node = self.megaApi->getChildNodeOfType(parent.getCPtr, name.UTF8String, (int)type);
    
    return node ? [[MEGANode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MEGANode *)parentNodeForNode:(MEGANode *)node {
    if (node == nil || self.megaApi == nil) return nil;
    
    MegaNode *parent = self.megaApi->getParentNode([node getCPtr]);
    
    return parent ? [[MEGANode alloc] initWithMegaNode:parent cMemoryOwn:YES] : nil;
}

- (NSString *)nodePathForNode:(MEGANode *)node {
    if (node == nil || self.megaApi == nil) return nil;
    
    const char *val = self.megaApi->getNodePath([node getCPtr]);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (MEGANode *)nodeForPath:(NSString *)path node:(MEGANode *)node {
    if (path == nil || node == nil || self.megaApi == nil) return nil;
    
    MegaNode *n = self.megaApi->getNodeByPath([path UTF8String], [node getCPtr]);
    
    return n ? [[MEGANode alloc] initWithMegaNode:n cMemoryOwn:YES] : Nil;
}

- (MEGANode *)nodeForPath:(NSString *)path {
    if (path == nil || self.megaApi == nil) return nil;
    
    MegaNode *node = self.megaApi->getNodeByPath([path UTF8String]);
    
    return node ? [[MEGANode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MEGANode *)nodeForHandle:(uint64_t)handle {
    if (handle == ::mega::INVALID_HANDLE || self.megaApi == nil) return nil;
    
    MegaNode *node = self.megaApi->getNodeByHandle(handle);
    
    return node ? [[MEGANode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MEGAUserList *)contacts {
    if (self.megaApi == nil) return nil;
    return [[MEGAUserList alloc] initWithUserList:self.megaApi->getContacts() cMemoryOwn:YES];
}

- (MEGAUser *)contactForEmail:(NSString *)email {
    if (email == nil || self.megaApi == nil) return nil;
    
    MegaUser *user = self.megaApi->getContact([email UTF8String]);
    return user ? [[MEGAUser alloc] initWithMegaUser:user cMemoryOwn:YES] : nil;
}

- (MEGAUserAlertList *)userAlertList {
    if (self.megaApi == nil) return nil;
    return [[MEGAUserAlertList alloc] initWithMegaUserAlertList:self.megaApi->getUserAlerts() cMemoryOwn:YES];
}

- (MEGANodeList *)inSharesForUser:(MEGAUser *)user {
    if (self.megaApi == nil) return nil;
    return [[MEGANodeList alloc] initWithNodeList:self.megaApi->getInShares(user.getCPtr) cMemoryOwn:YES];
}

- (MEGANodeList *)inShares {
    if (self.megaApi == nil) return nil;
    return [[MEGANodeList alloc] initWithNodeList:self.megaApi->getInShares() cMemoryOwn:YES];
}

- (MEGAShareList *)inSharesList:(MEGASortOrderType)order {
    if (self.megaApi == nil) return nil;
    return [[MEGAShareList alloc] initWithShareList:self.megaApi->getInSharesList((int)order) cMemoryOwn:YES];
}

- (MEGAShareList *)getUnverifiedInShares:(MEGASortOrderType)order {
    if (self.megaApi == nil) return nil;
    return [[MEGAShareList alloc] initWithShareList:self.megaApi->getUnverifiedInShares((int)order) cMemoryOwn:YES];
}

- (MEGAUser *)userFromInShareNode:(MEGANode *)node {
    if (self.megaApi == nil) return nil;
    return [[MEGAUser alloc] initWithMegaUser:self.megaApi->getUserFromInShare(node.getCPtr) cMemoryOwn:YES];
}

- (MEGAUser *)userFromInShareNode:(MEGANode *)node recurse:(BOOL)recurse {
    if (self.megaApi == nil) return nil;
    return [MEGAUser.alloc initWithMegaUser:self.megaApi->getUserFromInShare(node.getCPtr, recurse) cMemoryOwn:YES];
}

- (MEGAShareList *)outShares:(MEGASortOrderType)order {
    if (self.megaApi == nil) return nil;
    return [[MEGAShareList alloc] initWithShareList:self.megaApi->getOutShares((int)order) cMemoryOwn:YES];
}

- (MEGAShareList *)getUnverifiedOutShares:(MEGASortOrderType)order {
    if (self.megaApi == nil) return nil;
    return [[MEGAShareList alloc] initWithShareList:self.megaApi->getUnverifiedOutShares((int)order) cMemoryOwn:YES];
}

- (MEGAShareList *)outSharesForNode:(MEGANode *)node {
    if (self.megaApi == nil) return nil;
    return [[MEGAShareList alloc] initWithShareList:self.megaApi->getOutShares(node.getCPtr) cMemoryOwn:YES];
}

- (BOOL)isPrivateNode:(uint64_t)handle {
    return self.megaApi->isPrivateNode(handle);
}

- (BOOL)isForeignNode:(uint64_t)handle{
    return self.megaApi->isForeignNode(handle);
}

- (MEGANodeList *)publicLinks:(MEGASortOrderType)order {
    if (self.megaApi == nil) return nil;
    return [[MEGANodeList alloc] initWithNodeList:self.megaApi->getPublicLinks((int)order) cMemoryOwn:YES];
}

- (MEGAContactRequestList *)incomingContactRequests {
    if (self.megaApi == nil) return nil;
    return [[MEGAContactRequestList alloc] initWithMegaContactRequestList:self.megaApi->getIncomingContactRequests() cMemoryOwn:YES];
}

- (MEGAContactRequestList *)outgoingContactRequests {
    if (self.megaApi == nil) return nil;
    return [[MEGAContactRequestList alloc] initWithMegaContactRequestList:self.megaApi->getOutgoingContactRequests() cMemoryOwn:YES];
}

- (NSString *)fingerprintForFilePath:(NSString *)filePath {
    if (filePath == nil || self.megaApi == nil) return nil;
    
    const char *val = self.megaApi->getFingerprint([filePath UTF8String]);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (NSString *)fingerprintForData:(NSData *)data modificationTime:(NSDate *)modificationTime {
    if (data == nil || self.megaApi == nil) return nil;
    
    MEGADataInputStream mis = MEGADataInputStream(data);
    return [self fingerprintForInputStream:&mis modificationTime:modificationTime];
}

- (NSString *)fingerprintForFilePath:(NSString *)filePath modificationTime:(NSDate *)modificationTime {
    if (filePath.length == 0) return nil;
    
    MEGAFileInputStream mis = MEGAFileInputStream(filePath);
    return [self fingerprintForInputStream:&mis modificationTime:modificationTime];
}

- (NSString *)fingerprintForInputStream:(MegaInputStream *)stream modificationTime:(NSDate *)modificationTime {
    if (self.megaApi == nil) return nil;
    const char *val = self.megaApi->getFingerprint(stream, (long long)[modificationTime timeIntervalSince1970]);
    if (val != NULL) {
        NSString *ret = [[NSString alloc] initWithUTF8String:val];
        delete [] val;
        return ret;
    } else {
        return nil;
    }
}

- (MEGANode *)nodeForFingerprint:(NSString *)fingerprint {
    if (fingerprint == nil || self.megaApi == nil) return nil;
    
    MegaNode *node = self.megaApi->getNodeByFingerprint([fingerprint UTF8String]);
    
    return node ? [[MEGANode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MEGANode *)nodeForFingerprint:(NSString *)fingerprint parent:(MEGANode *)parent {
    if (fingerprint == nil || self.megaApi == nil) return nil;
    
    MegaNode *node = self.megaApi->getNodeByFingerprint(fingerprint.UTF8String, parent.getCPtr);
    
    return node ? [[MEGANode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MEGANodeList *)nodesForOriginalFingerprint:(NSString *)fingerprint {
    if (fingerprint.length == 0) {
        return nil;
    }
    
    return [[MEGANodeList alloc] initWithNodeList:self.megaApi->getNodesByOriginalFingerprint([fingerprint UTF8String], NULL) cMemoryOwn:YES];
}

- (BOOL)hasFingerprint:(NSString *)fingerprint{
    if (fingerprint == nil || self.megaApi == nil) return NO;
    
    return self.megaApi->hasFingerprint([fingerprint UTF8String]);
}

- (NSString *)CRCForFilePath:(NSString *)filePath {
    if (filePath == nil || self.megaApi == nil) return nil;
    
    const char *val = self.megaApi->getCRC([filePath UTF8String]);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (NSString *)CRCForFingerprint:(NSString *)fingerprint{
    if (fingerprint == nil || self.megaApi == nil) {
        return nil;
    }
    
    const char *val = self.megaApi->getCRCFromFingerprint([fingerprint UTF8String]);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (NSString *)CRCForNode:(MEGANode *)node {
    if (node == nil || self.megaApi == nil) return nil;
    
    const char *val = self.megaApi->getCRC([node getCPtr]);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (MEGANode *)nodeByCRC:(NSString *)crc parent:(MEGANode *)parent {
    if (crc == nil || self.megaApi == nil) return nil;
    
    MegaNode *node = self.megaApi->getNodeByCRC([crc UTF8String], parent.getCPtr);
    
    return node ? [[MEGANode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MEGAShareType)accessLevelForNode:(MEGANode *)node {
    if (node == nil || self.megaApi == nil) return MEGAShareTypeAccessUnknown;
    
    return (MEGAShareType) self.megaApi->getAccess([node getCPtr]);
}

- (MEGAError *)checkAccessErrorExtendedForNode:(MEGANode *)node level:(MEGAShareType)level {
    if (self.megaApi == nil) return nil;
    return [[MEGAError alloc] initWithMegaError:self.megaApi->checkAccessErrorExtended(node.getCPtr, (int)level) cMemoryOwn:YES];
}

- (BOOL)isNodeInRubbish:(MEGANode *)node {
    if (self.megaApi == nil) return NO;
    return self.megaApi->isInRubbish(node.getCPtr);
}

-(BOOL)isNodeInheritingSensitivity:(MEGANode *)node {
    if (self.megaApi == nil) return NO;
    return self.megaApi->isSensitiveInherited(node.getCPtr);
}

- (nullable NSArray<NSString *> *)nodeTagsForSearchString:(nullable NSString *)searchString cancelToken:(MEGACancelToken *)cancelToken {
    if (self.megaApi == nil) return nil;
    MegaStringList *result = self.megaApi->getAllNodeTags(searchString.UTF8String, cancelToken.getCPtr);
    MEGAStringList *tagsStringList = [MEGAStringList.alloc initWithMegaStringList:result cMemoryOwn:YES];
    return tagsStringList.toStringArray;
}

- (void)addTag:(NSString *)tag toNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi != nil) {
        self.megaApi->addNodeTag(node.getCPtr, tag.UTF8String, [self createDelegateMEGARequestListener:delegate
                                                                                        singleListener:YES
                                                                                             queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)removeTag:(NSString *)tag fromNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi != nil) {
        self.megaApi->removeNodeTag(node.getCPtr, tag.UTF8String, [self createDelegateMEGARequestListener:delegate
                                                                                           singleListener:YES
                                                                                                queueType:ListenerQueueTypeCurrent]);
    }
}

- (MEGAError *)checkMoveErrorExtendedForNode:(MEGANode *)node target:(MEGANode *)target {
    if (self.megaApi == nil) return nil;
    return [[MEGAError alloc] initWithMegaError:self.megaApi->checkMoveErrorExtended(node.getCPtr, target.getCPtr) cMemoryOwn:YES];
}

- (MEGANodeList *)searchWith:(MEGASearchFilter *)filter
                   orderType:(MEGASortOrderType)orderType
                        page:(nullable MEGASearchPage *)page
                 cancelToken:(MEGACancelToken *)cancelToken {
    if (self.megaApi == nil) return nil;
    return [MEGANodeList.alloc initWithNodeList:self.megaApi->search([self generateSearchFilterFrom: filter], (int)orderType, cancelToken.getCPtr, [self generateSearchPageFrom:page]) cMemoryOwn:YES];
}
- (MEGANodeList *)searchNonRecursivelyWith:(MEGASearchFilter *)filter
                                 orderType:(MEGASortOrderType)orderType
                                      page:(nullable MEGASearchPage *)page
                               cancelToken:(MEGACancelToken *)cancelToken {

    if (self.megaApi == nil) return nil;
    return [MEGANodeList.alloc initWithNodeList:self.megaApi->getChildren([self generateSearchFilterFrom: filter], (int)orderType, cancelToken.getCPtr, [self generateSearchPageFrom:page]) cMemoryOwn:YES];
}

- (void)getRecentActionsAsyncSinceDays:(NSInteger)days maxNodes:(NSInteger)maxNodes excludeSensitives:(BOOL)excludeSensitives delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi != nil) {
        self.megaApi->getRecentActionsAsync((int)days, (unsigned int)maxNodes, excludeSensitives, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (BOOL)processMEGANodeTree:(MEGANode *)node recursive:(BOOL)recursive delegate:(id<MEGATreeProcessorDelegate>)delegate {
    if (self.megaApi == nil) return NO;
    return self.megaApi->processMegaTree(node.getCPtr, [self createMegaTreeProcessor:delegate], recursive);
}

- (MEGANode *)authorizeNode:(MEGANode *)node {
    if (self.megaApi == nil) return nil;
    return [[MEGANode alloc] initWithMegaNode:self.megaApi->authorizeNode(node.getCPtr) cMemoryOwn:YES];
}

#ifdef ENABLE_CHAT

- (MEGANode *)authorizeChatNode:(MEGANode *)node cauth:(NSString *)cauth {
    if (self.megaApi == nil) return nil;
    return [[MEGANode alloc] initWithMegaNode:self.megaApi->authorizeChatNode(node.getCPtr, cauth.UTF8String) cMemoryOwn:YES];
}

#endif

- (NSNumber *)sizeForNode:(MEGANode *)node {
    if (self.megaApi == nil) return nil;
    return [[NSNumber alloc] initWithLongLong:self.megaApi->getSize([node getCPtr])];
}

- (NSString *)escapeFsIncompatible:(NSString *)name destinationPath:(NSString *)destinationPath {
    if (name == nil || self.megaApi == nil) return nil;
    
    const char *val = self.megaApi->escapeFsIncompatible(name.UTF8String, destinationPath.UTF8String);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (NSString *)unescapeFsIncompatible:(NSString *)localName destinationPath:(NSString *)destinationPath {
    if (localName == nil || self.megaApi == nil) return nil;
    
    const char *val = self.megaApi->unescapeFsIncompatible(localName.UTF8String, destinationPath.UTF8String);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (void)changeApiUrl:(NSString *)apiURL disablepkp:(BOOL)disablepkp {
    if (self.megaApi) {
        self.megaApi->changeApiUrl(apiURL.UTF8String, disablepkp);
    }
}

- (BOOL)setLanguageCode:(NSString *)languageCode {
    if (self.megaApi == nil) return NO;
    return self.megaApi->setLanguage(languageCode.UTF8String);
}

- (void)setLanguangePreferenceCode:(NSString *)languageCode delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setLanguagePreference(languageCode.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)setLanguangePreferenceCode:(NSString *)languageCode {
    if (self.megaApi) {
        self.megaApi->setLanguagePreference(languageCode.UTF8String);
    }
}

- (void)getLanguagePreferenceWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getLanguagePreference([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getLanguagePreference {
    if (self.megaApi) {
        self.megaApi->getLanguagePreference();
    }
}

- (void)setFileVersionsOption:(BOOL)disable delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setFileVersionsOption(disable, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)setFileVersionsOption:(BOOL)disable {
    if (self.megaApi) {
        self.megaApi->setFileVersionsOption(disable);
    }
}

- (void)getFileVersionsOptionWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getFileVersionsOption([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getFileVersionsOption {
    if (self.megaApi) {
        self.megaApi->getFileVersionsOption();
    }
}

- (void)setContactLinksOption:(BOOL)enable delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setContactLinksOption(enable, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getContactLinksOptionWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getContactLinksOption([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getContactLinksOption {
    if (self.megaApi) {
        self.megaApi->getContactLinksOption();
    }
}

- (void)retrySSLErrors:(BOOL)enable {
    if (self.megaApi) {
        self.megaApi->retrySSLerrors(enable);
    }
}

- (void)setPublicKeyPinning:(BOOL)enable {
    if (self.megaApi) {
        self.megaApi->setPublicKeyPinning(enable);
    }
}

- (BOOL)createThumbnail:(NSString *)imagePath destinatioPath:(NSString *)destinationPath {
    if (imagePath == nil || destinationPath == nil || self.megaApi == nil) return NO;
    
    return self.megaApi->createThumbnail([imagePath UTF8String], [destinationPath UTF8String]);
}

- (BOOL)createPreview:(NSString *)imagePath destinatioPath:(NSString *)destinationPath {
    if (imagePath == nil || destinationPath == nil || self.megaApi == nil) return NO;
    
    return self.megaApi->createPreview([imagePath UTF8String], [destinationPath UTF8String]);
}

- (BOOL)createAvatar:(NSString *)imagePath destinationPath:(NSString *)destinationPath {
    if (imagePath == nil || destinationPath == nil || self.megaApi == nil) return NO;
    
    return self.megaApi->createAvatar([imagePath UTF8String], [destinationPath UTF8String]);
}

#ifdef HAVE_LIBUV

#pragma mark - HTTP Proxy Server

- (BOOL)httpServerStart:(BOOL)localOnly port:(NSInteger)port {
    if (self.megaApi == nil) return NO;
    return self.megaApi->httpServerStart(localOnly, (int)port, false, NULL, NULL, true);
}

- (void)httpServerStop {
    if (self.megaApi) {
        self.megaApi->httpServerStop();
    }
}

- (NSInteger)httpServerIsRunning {
    if (self.megaApi == nil) return 0;
    return (NSInteger)self.megaApi->httpServerIsRunning();
}

- (BOOL)httpServerIsLocalOnly {
    if (self.megaApi == nil) return NO;
    return self.megaApi->httpServerIsLocalOnly();
}

- (void)httpServerEnableFileServer:(BOOL)enable {
    if (self.megaApi) {
        self.megaApi->httpServerEnableFileServer(enable);
    }
}

- (BOOL)httpServerIsFileServerEnabled {
    if (self.megaApi == nil) return NO;
    return self.megaApi->httpServerIsFileServerEnabled();
}

- (void)httpServerEnableFolderServer:(BOOL)enable {
    if (self.megaApi) {
        self.megaApi->httpServerEnableFolderServer(enable);
    }
}

- (BOOL)httpServerIsFolderServerEnabled {
    if (self.megaApi == nil) return NO;
    return self.megaApi->httpServerIsFolderServerEnabled();
}

- (void)httpServerSetRestrictedMode:(NSInteger)mode {
    if (self.megaApi) {
        self.megaApi->httpServerSetRestrictedMode((int)mode);
    }
}

- (NSInteger)httpServerGetRestrictedMode {
    if (self.megaApi == nil) return 0;
    return (NSInteger)self.megaApi->httpServerGetRestrictedMode();
}

- (void)httpServerEnableSubtitlesSupport:(BOOL)enable {
    if (self.megaApi) {
        self.megaApi->httpServerEnableSubtitlesSupport(enable);
    }
}

- (BOOL)httpServerIsSubtitlesSupportEnabled {
    if (self.megaApi == nil) return NO;
    return self.megaApi->httpServerIsSubtitlesSupportEnabled();
}

- (void)httpServerAddDelegate:(id<MEGATransferDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->httpServerAddListener([self createDelegateMEGATransferListener:delegate singleListener:NO]);
    }
}

- (void)httpServerRemoveDelegate:(id<MEGATransferDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->httpServerRemoveListener([self createDelegateMEGATransferListener:delegate singleListener:NO]);
    }
}

- (NSURL *)httpServerGetLocalLink:(MEGANode *)node {
    if (self.megaApi == nil) return nil;
    const char *val = self.megaApi->httpServerGetLocalLink([node getCPtr]);
    if (!val) return nil;
    
    NSURL *ret = [NSURL URLWithString:[NSString stringWithUTF8String:val]];
    
    delete [] val;
    return ret;
}

- (void)httpServerSetMaxBufferSize:(NSInteger)bufferSize {
    if (self.megaApi) {
        self.megaApi->httpServerSetMaxBufferSize((int)bufferSize);
    }
}

- (NSInteger)httpServerGetMaxBufferSize {
    if (self.megaApi == nil) return 0;
    return (NSInteger)self.megaApi->httpServerGetMaxBufferSize();
}

- (void)httpServerSetMaxOutputSize:(NSInteger)outputSize {
    if (self.megaApi) {
        self.megaApi->httpServerSetMaxOutputSize((int)outputSize);
    }
}

- (NSInteger)httpServerGetMaxOutputSize {
    if (self.megaApi == nil) return 0;
    return (NSInteger)self.megaApi->httpServerGetMaxOutputSize();
}

#endif

+ (NSString *)mimeTypeByExtension:(NSString *)extension {
    const char *val = MegaApi::getMimeType([extension UTF8String]);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

#ifdef ENABLE_CHAT

- (void)registeriOSdeviceToken:(NSString *)deviceToken delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->registerPushNotifications(PushNotificationTokenTypeiOSStandard, deviceToken.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)registeriOSdeviceToken:(NSString *)deviceToken {
    if (self.megaApi) {
        self.megaApi->registerPushNotifications(PushNotificationTokenTypeiOSStandard, deviceToken.UTF8String);
    }
}

- (void)registeriOSVoIPdeviceToken:(NSString *)deviceToken delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->registerPushNotifications(PushNotificationTokenTypeiOSVoIP, deviceToken.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)registeriOSVoIPdeviceToken:(NSString *)deviceToken {
    if (self.megaApi) {
        self.megaApi->registerPushNotifications(PushNotificationTokenTypeiOSVoIP, deviceToken.UTF8String);
    }
}

#endif

- (void)getAccountAchievementsWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getAccountAchievements([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getAccountAchievements {
    if (self.megaApi) {
        self.megaApi->getAccountAchievements();
    }
}

- (void)getMegaAchievementsWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getMegaAchievements([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getMegaAchievements {
    if (self.megaApi) {
        self.megaApi->getMegaAchievements();
    }
}

- (void)catchupWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->catchup([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getPublicLinkInformationWithFolderLink:(NSString *)folderLink delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getPublicLinkInformation(folderLink.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getPublicLinkInformationWithFolderLink:(NSString *)folderLink {
    if (self.megaApi) {
        self.megaApi->getPublicLinkInformation(folderLink.UTF8String);
    }
}

#pragma mark - SMS

- (SMSState)smsAllowedState {
    if (self.megaApi == nil) return SMSStateNotAllowed;
    return (SMSState)self.megaApi->smsAllowedState();
}

- (nullable NSString *)smsVerifiedPhoneNumber {
    if (self.megaApi == nil) return nil;
    char *number = self.megaApi->smsVerifiedPhoneNumber();
    
    if (number == NULL) {
        return nil;
    }
    
    NSString *numberString = @(number);
    delete [] number;
    return numberString;
}

- (void)getCountryCallingCodesWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getCountryCallingCodes([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)sendSMSVerificationCodeToPhoneNumber:(NSString *)phoneNumber delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->sendSMSVerificationCode([phoneNumber UTF8String], [self createDelegateMEGARequestListener:delegate singleListener:YES], YES);
    }
}

- (void)checkSMSVerificationCode:(NSString *)verificationCode delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->checkSMSVerificationCode([verificationCode UTF8String], [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)resetSmsVerifiedPhoneNumberWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->resetSmsVerifiedPhoneNumber([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)resetSmsVerifiedPhoneNumber {
    if (self.megaApi) {
        self.megaApi->resetSmsVerifiedPhoneNumber();
    }
}

#pragma mark - Push Notification Settings

- (void)getPushNotificationSettingsWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getPushNotificationSettings([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)getPushNotificationSettings {
    if (self.megaApi) {
        self.megaApi->getPushNotificationSettings();
    }
}

- (void)setPushNotificationSettings:(MEGAPushNotificationSettings *)pushNotificationSettings
                           delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setPushNotificationSettings(pushNotificationSettings.getCPtr,
                                                  [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)setPushNotificationSettings:(MEGAPushNotificationSettings *)pushNotificationSettings {
    if (self.megaApi) {
        self.megaApi->setPushNotificationSettings(pushNotificationSettings.getCPtr);
    }
}

#pragma mark - Banner

- (void)getBanners:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi -> getBanners([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)dismissBanner:(NSInteger)bannerIdentifier delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi -> dismissBanner((int)bannerIdentifier, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

#pragma mark - Backup Heartbeat

- (void)registerBackup:(BackUpType)type targetNode:(MEGANode *)node folderPath:(NSString *)path name:(NSString *)name state:(BackUpState)state delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setBackup((int)type, node.handle, path.UTF8String, name.UTF8String, (int)state, 0, [self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)updateBackup:(MEGAHandle)backupId backupType:(BackUpType)type targetNode:(nullable MEGANode *)node folderPath:(nullable NSString *)path backupName:(nullable NSString *)name state:(BackUpState)state subState:(BackUpSubState)subState delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->updateBackup(backupId, (int)type, node.handle, path.UTF8String, name.UTF8String, (int)state, (int)subState, [self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)unregisterBackup:(MEGAHandle)backupId delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->removeBackup(backupId, [self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)getBackupInfo:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getBackupInfo([self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)sendBackupHeartbeat:(MEGAHandle)backupId status:(BackupHeartbeatStatus)status progress:(NSInteger)progress pendingUploadCount:(NSUInteger)pendingUploadCount lastActionDate:(nullable NSDate *)lastActionDate lastBackupNode:(nullable MEGANode *)lastBackupNode delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->sendBackupHeartbeat(backupId, (int)status, (int)progress, (int)pendingUploadCount, 0, lastActionDate != nil ? (long long)[lastActionDate timeIntervalSince1970] : (long long)0, lastBackupNode != nil ? lastBackupNode.handle : INVALID_HANDLE, [self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (nullable NSString *)deviceId {
    if (self.megaApi) {
        const char *val = self.megaApi->getDeviceId();
        
        if (!val) return nil;
        
        NSString *ret = [[NSString alloc] initWithUTF8String:val];
        
        delete [] val;
        return ret;
    }
    return nil;
}

- (void)getDeviceName:(nullable NSString *)deviceId delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getDeviceName(deviceId.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)renameDevice:(nullable NSString *)deviceId newName:(NSString *)name delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setDeviceName(deviceId.UTF8String, name.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

#pragma mark - Debug

+ (void)setLogLevel:(MEGALogLevel)logLevel {
    MegaApi::setLogLevel((int)logLevel);
}

+ (void)setLogToConsole:(BOOL)enable {
    MegaApi::setLogToConsole(enable);
}

+ (void)logWithLevel:(MEGALogLevel)logLevel message:(NSString *)message filename:(NSString *)filename line:(NSInteger)line {
    MegaApi::log((int)logLevel, message.UTF8String, filename.UTF8String, (int)line);
}

+ (void)logWithLevel:(MEGALogLevel)logLevel message:(NSString *)message filename:(NSString *)filename {
    MegaApi::log((int)logLevel, message.UTF8String, filename.UTF8String);
}

+ (void)logWithLevel:(MEGALogLevel)logLevel message:(NSString *)message {
    MegaApi::log((int)logLevel, message.UTF8String);
}

- (void)sendEvent:(NSInteger)eventType message:(NSString *)message addJourneyId:(BOOL)addJourneyId viewId:(nullable NSString *)viewId delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->sendEvent((int)eventType, message.UTF8String, addJourneyId, viewId.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)sendEvent:(NSInteger)eventType message:(NSString *)message addJourneyId:(BOOL)addJourneyId viewId:(nullable NSString *)viewId {
    if (self.megaApi) {
        self.megaApi->sendEvent((int)eventType, message.UTF8String, addJourneyId, viewId.UTF8String);
    }
}

- (NSString *)generateViewId {
    if (self.megaApi == nil) return nil;
    const char *val = self.megaApi->generateViewId();
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (void)createSupportTicketWithMessage:(NSString *)message type:(NSInteger)type delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->createSupportTicket(message.UTF8String, (int)type, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)createSupportTicketWithMessage:(NSString *)message type:(NSInteger)type {
    if (self.megaApi) {
        self.megaApi->createSupportTicket(message.UTF8String, (int)type);
    }
}

#pragma mark - Private methods

- (MegaRequestListener *)createDelegateMEGARequestListener:(id<MEGARequestDelegate>)delegate singleListener:(BOOL)singleListener {
    return [self createDelegateMEGARequestListener:delegate singleListener:singleListener queueType:ListenerQueueTypeMain];
}

- (MegaRequestListener *)createDelegateMEGARequestListener:(id<MEGARequestDelegate>)delegate singleListener:(BOOL)singleListener queueType:(ListenerQueueType)queueType {
    if (delegate == nil) return nil;
    
    DelegateMEGARequestListener *delegateListener = new DelegateMEGARequestListener(self, delegate, singleListener, queueType);
    pthread_mutex_lock(&listenerMutex);
    _activeRequestListeners.insert(delegateListener);
    pthread_mutex_unlock(&listenerMutex);
    return delegateListener;
}

- (MegaTransferListener *)createDelegateMEGATransferListener:(id<MEGATransferDelegate>)delegate singleListener:(BOOL)singleListener {
    return [self createDelegateMEGATransferListener:delegate singleListener:singleListener queueType:ListenerQueueTypeMain];
}

- (MegaTransferListener *)createDelegateMEGATransferListener:(id<MEGATransferDelegate>)delegate singleListener:(BOOL)singleListener queueType:(ListenerQueueType)queueType {
    if (delegate == nil) return nil;
    
    DelegateMEGATransferListener *delegateListener = new DelegateMEGATransferListener(self, delegate, singleListener, queueType);
    pthread_mutex_lock(&listenerMutex);
    _activeTransferListeners.insert(delegateListener);
    pthread_mutex_unlock(&listenerMutex);
    return delegateListener;
}

- (MegaGlobalListener *)createDelegateMEGAGlobalListener:(id<MEGAGlobalDelegate>)delegate
                                               queueType:(ListenerQueueType)queueType {
    if (delegate == nil) return nil;
    
    DelegateMEGAGlobalListener *delegateListener = new DelegateMEGAGlobalListener(self, delegate, queueType);
    pthread_mutex_lock(&listenerMutex);
    _activeGlobalListeners.insert(delegateListener);
    pthread_mutex_unlock(&listenerMutex);
    return delegateListener;
}

- (MegaListener *)createDelegateMEGAListener:(id<MEGADelegate>)delegate {
    if (delegate == nil) return nil;
    
    DelegateMEGAListener *delegateListener = new DelegateMEGAListener(self, delegate);
    pthread_mutex_lock(&listenerMutex);
    _activeMegaListeners.insert(delegateListener);
    pthread_mutex_unlock(&listenerMutex);
    return delegateListener;
}

- (MegaLogger *)createDelegateMegaLogger:(id<MEGALoggerDelegate>)delegate {
    if (delegate == nil) return nil;
    
    DelegateMEGALoggerListener *delegateListener = new DelegateMEGALoggerListener(delegate);
    pthread_mutex_lock(&listenerMutex);
    _activeLoggerListeners.insert(delegateListener);
    pthread_mutex_unlock(&listenerMutex);
    return delegateListener;
}

- (MegaScheduledCopyListener *)createDelegateMEGAScheduledCopyListener:(id<MEGAScheduledCopyDelegate>)delegate queueType:(ListenerQueueType)queueType {
    if (delegate == nil) return nil;
    
    DelegateMEGAScheduledCopyListener *delegateListener = new DelegateMEGAScheduledCopyListener(self, delegate, queueType);
    pthread_mutex_lock(&listenerMutex);
    _activeScheduledCopyListeners.insert(delegateListener);
    pthread_mutex_unlock(&listenerMutex);
    return delegateListener;
}

- (MegaTreeProcessor *)createMegaTreeProcessor:(id<MEGATreeProcessorDelegate>)delegate {
    if (delegate == nil) return nil;
    
    DelegateMEGATreeProcessorListener *delegateListener = new DelegateMEGATreeProcessorListener(delegate);
    
    return delegateListener;
}

- (void)freeRequestListener:(DelegateMEGARequestListener *)delegate {
    if (delegate == nil) return;
    
    pthread_mutex_lock(&listenerMutex);
    _activeRequestListeners.erase(delegate);
    pthread_mutex_unlock(&listenerMutex);
    delete delegate;
}

- (void)freeTransferListener:(DelegateMEGATransferListener *)delegate {
    if (delegate == nil) return;
    
    pthread_mutex_lock(&listenerMutex);
    _activeTransferListeners.erase(delegate);
    pthread_mutex_unlock(&listenerMutex);
    delete delegate;
}

- (MegaSearchFilter *)generateSearchFilterFrom:(MEGASearchFilter *)filter {
    MegaSearchFilter *megaFilter = MegaSearchFilter::createInstance();

    megaFilter->byName(filter.term.UTF8String);
    megaFilter->byDescription(filter.searchDescription.UTF8String);
    megaFilter->byNodeType((int)filter.nodeType);
    megaFilter->byCategory((int)filter.category);
    megaFilter->bySensitivity((int)filter.sensitiveFilter);
    megaFilter->byFavourite((int)filter.favouriteFilter);
    megaFilter->byTag(filter.searchTag.UTF8String);
    megaFilter->useAndForTextQuery(filter.useAndForTextQuery);

    if (filter.didSetLocationType) {
        megaFilter->byLocation(filter.locationType);
    }

    if (filter.didSetParentNodeHandle) {
        megaFilter->byLocationHandle(filter.parentNodeHandle);
    }

    if (filter.creationTimeFrame != nil) {
        megaFilter->byCreationTime(filter.creationTimeFrame.lowerLimit, filter.creationTimeFrame.upperLimit);
    }
    
    if (filter.modificationTimeFrame != nil) {
        megaFilter->byModificationTime(filter.modificationTimeFrame.lowerLimit, filter.modificationTimeFrame.upperLimit);
    }

    return megaFilter;
}

-(nullable MegaSearchPage *)generateSearchPageFrom:(nullable MEGASearchPage *)page {
    if(page == nil) return nil;
    return MegaSearchPage::createInstance(page.startingOffset, page.pageSize);
}

#pragma mark - Cookie Dialog

- (void)setCookieSettings:(NSInteger)settings delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->setCookieSettings((int)settings, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)setCookieSettings:(NSInteger)settings {
    if (self.megaApi) {
        self.megaApi->setCookieSettings((int)settings);
    }
}

- (void)cookieSettingsWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getCookieSettings([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)cookieSettings {
    if (self.megaApi) {
        self.megaApi->getCookieSettings();
    }
}

- (BOOL)cookieBannerEnabled {
    if (self.megaApi == nil) return NO;
    return self.megaApi->cookieBannerEnabled();
}

#pragma mark - A/B Testing
- (NSInteger)getABTestValue:(NSString*)flag {
    if (self.megaApi == nil) return 0;
    return self.megaApi->getABTestValue((const char *)flag.UTF8String);
}

#pragma mark - Remote Feature Flags
- (NSInteger)remoteFeatureFlagValue:(NSString *)flag {
    if (self.megaApi == nil) return 0;
    MegaFlag *flagValue = self.megaApi->getFlag(flag.UTF8String);
    uint32_t group = flagValue->getGroup();
    delete flagValue;
    return NSInteger(group);
}

#pragma mark - Ads
- (void)fetchAds:(AdsFlag)adFlags adUnits:(MEGAStringList *)adUnits publicHandle:(MEGAHandle)publicHandle delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->fetchAds((int)adFlags, adUnits.getCPtr, publicHandle, [self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)queryAds:(AdsFlag)adFlags publicHandle:(MEGAHandle)publicHandle delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->queryAds((int)adFlags, publicHandle, [self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)enableRequestStatusMonitor:(BOOL)enable {
    if (self.megaApi) {
        self.megaApi->enableRequestStatusMonitor(enable);
    }
}

- (BOOL)isRequestStatusMonitorEnabled {
    if (self.megaApi) {
        return self.megaApi->requestStatusMonitorEnabled();
    } else {
        return NO;
    }
}

#pragma mark - VPN

- (void)getVpnRegionsWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getVpnRegions([self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)getVpnCredentialsWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getVpnCredentials([self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)putVpnCredentialWithRegion:(NSString *)region delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->putVpnCredential(region.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)delVpnCredentialWithSlotID:(NSInteger)slotID delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->delVpnCredential((int)slotID, [self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)checkVpnCredentialWithUserPubKey:(NSString *)userPubKey delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->checkVpnCredential(userPubKey.UTF8String, [self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)getMyIPWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getMyIp([self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)runNetworkConnectivityTestWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->runNetworkConnectivityTest([self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

#pragma mark - Password Manager

- (void)getPasswordManagerBaseWithDelegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->getPasswordManagerBase([self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (BOOL)isPasswordManagerNodeFolderWithHandle:(MEGAHandle)node {
    if (self.megaApi == nil) return NO;

    return self.megaApi->isPasswordManagerNodeFolder(node);
}

- (void)createPasswordNodeWithName:(NSString *)name data:(PasswordNodeData *)data parent:(MEGAHandle)parent delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        MegaNode::PasswordNodeData *passwordNodeData = MegaNode::PasswordNodeData::createInstance(data.password.UTF8String, data.notes.UTF8String, data.url.UTF8String, data.userName.UTF8String, data.totp.getCPtr);
        self.megaApi->createPasswordNode(name.UTF8String, passwordNodeData, parent, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)updatePasswordNodeWithHandle:(MEGAHandle)node newData:(PasswordNodeData *)newData delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        MegaNode::PasswordNodeData::TotpData *totpData = nil;
        if (newData.totp) {
            totpData = newData.totp.getCPtr;
        }
        
        MegaNode::PasswordNodeData *passwordNodeData = MegaNode::PasswordNodeData::createInstance(newData.password.UTF8String, newData.notes.UTF8String, newData.url.UTF8String, newData.userName.UTF8String, totpData);
        
        self.megaApi->updatePasswordNode(node, passwordNodeData, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (void)createCreditCardNodeWithName:(NSString *)name data:(MEGACreditCardNodeData *)data parent:(MEGAHandle)parent delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        MegaNode::CreditCardNodeData *creditCardNodeData = MegaNode::CreditCardNodeData::createInstance(data.cardNumber.UTF8String, data.notes.UTF8String, data.cardHolderName.UTF8String, data.cvv.UTF8String, data.expirationDate.UTF8String);
        self.megaApi->createCreditCardNode(name.UTF8String, creditCardNodeData, parent, [self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)updateCreditCardNodeWithHandle:(MEGAHandle)node newData:(MEGACreditCardNodeData *)newData delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        MegaNode::CreditCardNodeData *creditCardNodeData = MegaNode::CreditCardNodeData::createInstance(newData.cardNumber.UTF8String, newData.notes.UTF8String, newData.cardHolderName.UTF8String, newData.cvv.UTF8String, newData.expirationDate.UTF8String);
        self.megaApi->updateCreditCardNode(node, creditCardNodeData, [self createDelegateMEGARequestListener:delegate singleListener:YES queueType:ListenerQueueTypeCurrent]);
    }
}

- (void)importPasswordsFromFile:(NSString *)filePath fileSource:(ImportPasswordFileSource)fileSource parent:(MEGAHandle)parent delegate:(id<MEGARequestDelegate>)delegate {
    if (self.megaApi) {
        self.megaApi->importPasswordsFromFile(filePath.UTF8String, (int)fileSource, parent, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
    }
}

- (nullable MEGATotpTokenGenResult *)generateTotpTokenFromNode:(MEGAHandle)handle {
    if (self.megaApi == nil) return nil;

    MegaTotpTokenGenResult tokenGenResult = self.megaApi->generateTotpTokenFromNode(handle);
    MegaTotpTokenLifetime tokenLifetime = tokenGenResult.result;
    NSString *token = [NSString stringWithUTF8String:tokenLifetime.token.c_str()];
    MEGATotpTokenLifetime *result = [[MEGATotpTokenLifetime alloc] initWithToken:token remainingLifeTimeSeconds:tokenLifetime.remainingLifeTimeSeconds];
    MEGATotpTokenGenResult *tokenGenResultObj = [[MEGATotpTokenGenResult alloc] initWithErrorCode:tokenGenResult.errorCode result:result];
    return tokenGenResultObj;
}

+ (nullable NSString *)generateRandomPasswordWithCapitalLetters:(BOOL)includeCapitalLetters digits:(BOOL)includeDigits symbols:(BOOL)includeSymbols length:(int)length {
    const char *result = MegaApi::generateRandomCharsPassword(includeCapitalLetters, includeDigits, includeSymbols, length);
    if (!result) return nil;
    NSString *password = [NSString stringWithUTF8String:result];
    delete [] result;
    return password;
}

@end
