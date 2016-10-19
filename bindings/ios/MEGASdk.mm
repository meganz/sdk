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
#import "MEGAUser+init.h"
#import "MEGATransfer+init.h"
#import "MEGATransferList+init.h"
#import "MEGANodeList+init.h"
#import "MEGAUserList+init.h"
#import "MEGAError+init.h"
#import "MEGAShareList+init.h"
#import "MEGAContactRequest+init.h"
#import "MEGAContactRequestList+init.h"
#import "DelegateMEGARequestListener.h"
#import "DelegateMEGATransferListener.h"
#import "DelegateMEGAGlobalListener.h"
#import "DelegateMEGAListener.h"
#import "DelegateMEGALoggerListener.h"
#import "MEGAInputStream.h"

#import <set>
#import <pthread.h>

using namespace mega;

@interface MEGASdk () {
    pthread_mutex_t listenerMutex;
}

@property (nonatomic, assign) std::set<DelegateMEGARequestListener *>activeRequestListeners;
@property (nonatomic, assign) std::set<DelegateMEGATransferListener *>activeTransferListeners;
@property (nonatomic, assign) std::set<DelegateMEGAGlobalListener *>activeGlobalListeners;
@property (nonatomic, assign) std::set<DelegateMEGAListener *>activeMegaListeners;

- (MegaRequestListener *)createDelegateMEGARequestListener:(id<MEGARequestDelegate>)delegate singleListener:(BOOL)singleListener;
- (MegaTransferListener *)createDelegateMEGATransferListener:(id<MEGATransferDelegate>)delegate singleListener:(BOOL)singleListener;
- (MegaGlobalListener *)createDelegateMEGAGlobalListener:(id<MEGAGlobalDelegate>)delegate;
- (MegaListener *)createDelegateMEGAListener:(id<MEGADelegate>)delegate;

@property MegaApi *megaApi;
- (MegaApi *)getCPtr;

@end

@implementation MEGASdk

static DelegateMEGALogerListener *externalLogger = new DelegateMEGALogerListener(nil);

#pragma mark - Properties

- (NSString *)myEmail {
    const char *val = self.megaApi->getMyEmail();
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (MEGANode *)rootNode {
    MegaNode *node = self.megaApi->getRootNode();
    return node ? [[MEGANode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MEGANode *)rubbishNode {
    MegaNode *node = self.megaApi->getRubbishNode();
    return node ? [[MEGANode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MEGANode *)inboxNode {
    MegaNode *node = self.megaApi->getInboxNode();
    return node ? [[MEGANode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MEGATransferList *)transfers {
    return [[MEGATransferList alloc] initWithTransferList:self.megaApi->getTransfers() cMemoryOwn:YES];
}

- (MEGATransferList *)downloadTransfers {
    return [[MEGATransferList alloc] initWithTransferList:self.megaApi->getTransfers(MegaTransfer::TYPE_DOWNLOAD) cMemoryOwn:YES];
}

- (MEGATransferList *)uploadTransfers {
    return [[MEGATransferList alloc] initWithTransferList:self.megaApi->getTransfers(MegaTransfer::TYPE_UPLOAD) cMemoryOwn:YES];
}

- (NSNumber *)totalsDownloadedBytes {
    return [[NSNumber alloc] initWithLongLong:self.megaApi->getTotalDownloadedBytes()];
}

- (NSNumber *)totalsUploadedBytes {
    return [[NSNumber alloc] initWithLongLong:self.megaApi->getTotalUploadedBytes()];
}

- (NSString *)masterKey {
    const char *val = self.megaApi->exportMasterKey();
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (NSString *)userAgent {
    const char *val = self.megaApi->getUserAgent();
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    return ret;
}

- (MEGAUser *)myUser {
    MegaUser *user = self.megaApi->getMyUser();
    return user ? [[MEGAUser alloc] initWithMegaUser:user cMemoryOwn:YES] : nil;
}

#ifdef ENABLE_CHAT

- (NSString *)myFingerprint {
    const char *val = self.megaApi->getMyFingerprint();
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

#endif

#pragma mark - Init

- (instancetype)initWithAppKey:(NSString *)appKey userAgent:(NSString *)userAgent {
    self.megaApi = new MegaApi((appKey != nil) ? [appKey UTF8String] : (const char *)NULL, (const char *)NULL, (userAgent != nil) ? [userAgent UTF8String] : (const char *)NULL);
    
    if (pthread_mutex_init(&listenerMutex, NULL)) {
        return nil;
    }
    
    return self;
}

- (instancetype)initWithAppKey:(NSString *)appKey userAgent:(NSString *)userAgent basePath:(NSString *)basePath {
    self.megaApi = new MegaApi((appKey != nil) ? [appKey UTF8String] : (const char *)NULL, (basePath != nil) ? [basePath UTF8String] : (const char*)NULL, (userAgent != nil) ? [userAgent UTF8String] : (const char *)NULL);
    
    if (pthread_mutex_init(&listenerMutex, NULL)) {
        return nil;
    }
    
    return self;
}

- (void)dealloc {
    delete _megaApi;
    pthread_mutex_destroy(&listenerMutex);
}

- (MegaApi *)getCPtr {
    return _megaApi;
}

#pragma mark - Add and remove delegates

- (void)addMEGADelegate:(id<MEGADelegate>)delegate {
    self.megaApi->addListener([self createDelegateMEGAListener:delegate]);
}

- (void)addMEGARequestDelegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->addRequestListener([self createDelegateMEGARequestListener:delegate singleListener:NO]);
}

- (void)addMEGATransferDelegate:(id<MEGATransferDelegate>)delegate {
    self.megaApi->addTransferListener([self createDelegateMEGATransferListener:delegate singleListener:NO]);
}

- (void)addMEGAGlobalDelegate:(id<MEGAGlobalDelegate>)delegate {
    self.megaApi->addGlobalListener([self createDelegateMEGAGlobalListener:delegate]);
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
        self.megaApi->removeListener(listenersToRemove[i]);
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
        self.megaApi->removeRequestListener(listenersToRemove[i]);
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
        self.megaApi->removeTransferListener(listenersToRemove[i]);
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
        self.megaApi->removeGlobalListener(listenersToRemove[i]);
    }

}

#pragma mark - Utils

- (NSString *)base64pwkeyForPassword:(NSString *)password {
    if(password == nil) return nil;
    
    const char *val = self.megaApi->getBase64PwKey([password UTF8String]);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (NSString *)hashForBase64pwkey:(NSString *)base64pwkey email:(NSString *)email {
    if(base64pwkey == nil || email == nil)  return  nil;
    
    const char *val = self.megaApi->getStringHash([base64pwkey UTF8String], [email UTF8String]);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

+ (uint64_t)handleForBase64Handle:(NSString *)base64Handle {
    if(base64Handle == nil) return ::mega::INVALID_HANDLE;
    
    return MegaApi::base64ToHandle([base64Handle UTF8String]);
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
    self.megaApi->retryPendingConnections();
}

- (void)reconnect {
    self.megaApi->retryPendingConnections(true, true);
}

#pragma mark - Login Requests

- (void)loginWithEmail:(NSString *)email password:(NSString *)password {
    self.megaApi->login((email != nil) ? [email UTF8String] : NULL, (password != nil) ? [password UTF8String] : NULL);
}

- (void)loginWithEmail:(NSString *)email password:(NSString *)password delegate:(id<MEGARequestDelegate>)delegate{
    self.megaApi->login((email != nil) ? [email UTF8String] : NULL, (password != nil) ? [password UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (NSString *)dumpSession {
    const char *val = self.megaApi->dumpSession();
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (void)fastLoginWithEmail:(NSString *)email stringHash:(NSString *)stringHash base64pwKey:(NSString *)base64pwKey {
    self.megaApi->fastLogin((email != nil) ? [email UTF8String] : NULL, (stringHash != nil) ? [stringHash UTF8String] : NULL, (base64pwKey != nil) ? [base64pwKey UTF8String] : NULL);
}

- (void)fastLoginWithEmail:(NSString *)email stringHash:(NSString *)stringHash base64pwKey:(NSString *)base64pwKey delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->fastLogin((email != nil) ? [email UTF8String] : NULL, (stringHash != nil) ? [stringHash UTF8String] : NULL, (base64pwKey != nil) ? [base64pwKey UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)fastLoginWithSession:(NSString *)session {
    self.megaApi->fastLogin((session != nil) ? [session UTF8String] : NULL);
}

- (void)fastLoginWithSession:(NSString *)session delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->fastLogin((session != nil) ? [session UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)loginToFolderLink:(NSString *)folderLink delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->loginToFolder((folderLink != nil) ? [folderLink UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)loginToFolderLink:(NSString *)folderLink {
    self.megaApi->loginToFolder((folderLink != nil) ? [folderLink UTF8String] : NULL);
}

- (NSInteger)isLoggedIn {
    return self.megaApi->isLoggedIn();
}

- (void)logoutWithDelegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->logout([self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)logout {
    self.megaApi->logout();
}

- (void)fetchNodesWithDelegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->fetchNodes([self createDelegateMEGARequestListener:delegate singleListener:YES]);
}


- (void)fetchNodes {
    self.megaApi->fetchNodes();
}

#pragma mark - Create account and confirm account Requests

- (void)createAccountWithEmail:(NSString *)email password:(NSString *)password name:(NSString *)name {
    self.megaApi->createAccount((email != nil) ? [email UTF8String] : NULL, (password != nil) ? [password UTF8String] : NULL, (name != nil) ? [name UTF8String] : NULL);
}

- (void)createAccountWithEmail:(NSString *)email password:(NSString *)password name:(NSString *)name delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->createAccount((email != nil) ? [email UTF8String] : NULL, (password != nil) ? [password UTF8String] : NULL, (name != nil) ? [name UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)createAccountWithEmail:(NSString *)email password:(NSString *)password firstname:(NSString *)firstname lastname:(NSString *)lastname {
    self.megaApi->createAccount((email != nil) ? [email UTF8String] : NULL, (password != nil) ? [password UTF8String] : NULL, (firstname != nil) ? [firstname UTF8String] : NULL, (lastname != nil) ? [lastname UTF8String] : NULL);
}

- (void)createAccountWithEmail:(NSString *)email password:(NSString *)password firstname:(NSString *)firstname lastname:(NSString *)lastname delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->createAccount((email != nil) ? [email UTF8String] : NULL, (password != nil) ? [password UTF8String] : NULL, (firstname != nil) ? [firstname UTF8String] : NULL, (lastname != nil) ? [lastname UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)fastCreateAccountWithEmail:(NSString *)email base64pwkey:(NSString *)base64pwkey name:(NSString *)name {
    self.megaApi->fastCreateAccount((email != nil) ? [email UTF8String] : NULL, (base64pwkey != nil) ? [base64pwkey UTF8String] : NULL, (name != nil) ? [name UTF8String] : NULL);
}

- (void)fastCreateAccountWithEmail:(NSString *)email base64pwkey:(NSString *)base64pwkey name:(NSString *)name delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->fastCreateAccount((email != nil) ? [email UTF8String] : NULL, (base64pwkey != nil) ? [base64pwkey UTF8String] : NULL, (name != nil) ? [name UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)querySignupLink:(NSString *)link {
    self.megaApi->querySignupLink((link != nil) ? [link UTF8String] : NULL);
}

- (void)querySignupLink:(NSString *)link delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->querySignupLink((link != nil) ? [link UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)confirmAccountWithLink:(NSString *)link password:(NSString *)password {
    self.megaApi->confirmAccount((link != nil) ? [link UTF8String] : NULL, (password != nil) ? [password UTF8String] : NULL);
}

- (void)confirmAccountWithLink:(NSString *)link password:(NSString *)password delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->confirmAccount((link != nil) ? [link UTF8String] : NULL, (password != nil) ? [password UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)fastConfirmAccountWithLink:(NSString *)link base64pwkey:(NSString *)base64pwkey {
    self.megaApi->fastConfirmAccount((link != nil) ? [link UTF8String] : NULL, (base64pwkey != nil) ? [base64pwkey UTF8String] : NULL);
}


- (void)fastConfirmAccountWithLink:(NSString *)link base64pwkey:(NSString *)base64pwkey delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->fastConfirmAccount((link != nil) ? [link UTF8String] : NULL, (base64pwkey != nil) ? [base64pwkey UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)resetPasswordWithEmail:(NSString *)email hasMasterKey:(BOOL)hasMasterKey delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->resetPassword((email != nil) ? [email UTF8String] : NULL, hasMasterKey, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)resetPasswordWithEmail:(NSString *)email hasMasterKey:(BOOL)hasMasterKey {
    self.megaApi->resetPassword((email != nil) ? [email UTF8String] : NULL, hasMasterKey);
}

- (void)queryResetPasswordLink:(NSString *)link delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->queryResetPasswordLink((link != nil) ? [link UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)queryResetPasswordLink:(NSString *)link {
    self.megaApi->queryResetPasswordLink((link != nil) ? [link UTF8String] : NULL);
}

- (void)confirmResetPasswordWithLink:(NSString *)link newPassword:(NSString *)newPassword masterKey:(NSString *)masterKey delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->confirmResetPassword((link != nil) ? [link UTF8String] : NULL, (newPassword != nil) ? [newPassword UTF8String] : NULL, (masterKey != nil) ? [masterKey UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)confirmResetPasswordWithLink:(NSString *)link newPassword:(NSString *)newPassword masterKey:(NSString *)masterKey {
    self.megaApi->confirmResetPassword((link != nil) ? [link UTF8String] : NULL, (newPassword != nil) ? [newPassword UTF8String] : NULL, (masterKey != nil) ? [masterKey UTF8String] : NULL);
}

- (void)cancelAccountWithDelegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->cancelAccount([self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)cancelAccount {
    self.megaApi->cancelAccount();
}

- (void)confirmCancelAccountWithLink:(NSString *)link password:(NSString *)password delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->confirmCancelAccount((link != nil) ? [link UTF8String] : NULL, (password != nil) ? [password UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)confirmCancelAccountWithLink:(NSString *)link password:(NSString *)password {
    self.megaApi->confirmCancelAccount((link != nil) ? [link UTF8String] : NULL, (password != nil) ? [password UTF8String] : NULL);
}

- (void)changeEmail:(NSString *)email delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->changeEmail((email != nil) ? [email UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)changeEmail:(NSString *)email {
    self.megaApi->changeEmail((email != nil) ? [email UTF8String] : NULL);
}

- (void)queryChangeEmailLink:(NSString *)link delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->queryChangeEmailLink((link != nil) ? [link UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)queryChangeEmailLink:(NSString *)link {
    self.megaApi->queryChangeEmailLink((link != nil) ? [link UTF8String] : NULL);
}

- (void)confirmChangeEmailWithLink:(NSString *)link password:(NSString *)password delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->confirmChangeEmail((link != nil) ? [link UTF8String] : NULL, (password != nil) ? [password UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)confirmChangeEmailWithLink:(NSString *)link password:(NSString *)password {
    self.megaApi->confirmChangeEmail((link != nil) ? [link UTF8String] : NULL, (password != nil) ? [password UTF8String] : NULL);
}

#pragma mark - Filesystem changes Requests

- (void)createFolderWithName:(NSString *)name parent:(MEGANode *)parent delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->createFolder((name != nil) ? [name UTF8String] : NULL, (parent != nil) ? [parent getCPtr] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)createFolderWithName:(NSString *)name parent:(MEGANode *)parent {
    self.megaApi->createFolder((name != nil) ? [name UTF8String] : NULL, (parent != nil) ? [parent getCPtr] : NULL);
}

- (void)moveNode:(MEGANode *)node newParent:(MEGANode *)newParent delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->moveNode((node != nil) ? [node getCPtr] : NULL, (newParent != nil) ? [newParent getCPtr] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)moveNode:(MEGANode *)node newParent:(MEGANode *)newParent {
    self.megaApi->moveNode((node != nil) ? [node getCPtr] : NULL, (newParent != nil) ? [newParent getCPtr] : NULL);
}

- (void)copyNode:(MEGANode *)node newParent:(MEGANode *)newParent delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->copyNode((node != nil) ? [node getCPtr] : NULL, (newParent != nil) ? [newParent getCPtr] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)copyNode:(MEGANode *)node newParent:(MEGANode *)newParent {
    self.megaApi->copyNode((node != nil) ? [node getCPtr] : NULL, (newParent != nil) ? [newParent getCPtr] : NULL);
}

- (void)copyNode:(MEGANode *)node newParent:(MEGANode *)newParent newName:(NSString *)newName delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->copyNode((node != nil) ? [node getCPtr] : NULL, (newParent != nil) ? [newParent getCPtr] : NULL, (newName != nil) ? [newName UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)copyNode:(MEGANode *)node newParent:(MEGANode *)newParent newName:(NSString *)newName {
    self.megaApi->copyNode((node != nil) ? [node getCPtr] : NULL, (newParent != nil) ? [newParent getCPtr] : NULL, (newName != nil) ? [newName UTF8String] : NULL);
}

- (void)renameNode:(MEGANode *)node newName:(NSString *)newName delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->renameNode((node != nil) ? [node getCPtr] : NULL, (newName != nil) ? [newName UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)renameNode:(MEGANode *)node newName:(NSString *)newName {
    self.megaApi->renameNode((node != nil) ? [node getCPtr] : NULL, (newName != nil) ? [newName UTF8String] : NULL);
}

- (void)removeNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->remove((node != nil) ? [node getCPtr] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)removeNode:(MEGANode *)node {
    self.megaApi->remove((node != nil) ? [node getCPtr] : NULL);
}

- (void)cleanRubbishBinWithDelegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->cleanRubbishBin([self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)cleanRubbishBin {
    self.megaApi->cleanRubbishBin();
}

#pragma mark - Sharing Requests

- (void)shareNode:(MEGANode *)node withUser:(MEGAUser *)user level:(NSInteger)level delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->share((node != nil) ? [node getCPtr] : NULL, (user != nil) ? [user getCPtr] : NULL, (int)level, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)shareNode:(MEGANode *)node withUser:(MEGAUser *)user level:(NSInteger)level {
    self.megaApi->share((node != nil) ? [node getCPtr] : NULL, (user != nil) ? [user getCPtr] : NULL, (int)level);
}

- (void)shareNode:(MEGANode *)node withEmail:(NSString *)email level:(NSInteger)level delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->share((node != nil) ? [node getCPtr] : NULL, (email != nil) ? [email UTF8String] : NULL, (int)level, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)shareNode:(MEGANode *)node withEmail:(NSString *)email level:(NSInteger)level {
    self.megaApi->share((node != nil) ? [node getCPtr] : NULL, (email != nil) ? [email UTF8String] : NULL, (int)level);
}

- (void)importMegaFileLink:(NSString *)megaFileLink parent:(MEGANode *)parent delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->importFileLink((megaFileLink != nil) ? [megaFileLink UTF8String] : NULL, (parent != nil) ? [parent getCPtr] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)importMegaFileLink:(NSString *)megaFileLink parent:(MEGANode *)parent {
    self.megaApi->importFileLink((megaFileLink != nil) ? [megaFileLink UTF8String] : NULL, (parent != nil) ? [parent getCPtr] : NULL);
}

- (void)publicNodeForMegaFileLink:(NSString *)megaFileLink delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->getPublicNode((megaFileLink != nil) ? [megaFileLink UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)publicNodeForMegaFileLink:(NSString *)megaFileLink {
    self.megaApi->getPublicNode((megaFileLink != nil) ? [megaFileLink UTF8String] : NULL);
}

- (void)exportNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->exportNode((node != nil) ? [node getCPtr] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)exportNode:(MEGANode *)node {
    self.megaApi->exportNode((node != nil) ? [node getCPtr] : NULL);
}

- (void)disableExportNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->disableExport((node != nil) ? [node getCPtr] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)disableExportNode:(MEGANode *)node {
    self.megaApi->disableExport((node != nil) ? [node getCPtr] : NULL);
}

#pragma mark - Attributes Requests

- (void)getThumbnailNode:(MEGANode *)node destinationFilePath:(NSString *)destinationFilePath delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->getThumbnail((node != nil) ? [node getCPtr] : NULL, (destinationFilePath != nil) ? [destinationFilePath UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)getThumbnailNode:(MEGANode *)node destinationFilePath:(NSString *)destinationFilePath {
    self.megaApi->getThumbnail((node != nil) ? [node getCPtr] : NULL, (destinationFilePath != nil) ? [destinationFilePath UTF8String] : NULL);
}

- (void)cancelGetThumbnailNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->cancelGetThumbnail((node != nil) ? [node getCPtr] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)cancelGetThumbnailNode:(MEGANode *)node {
    self.megaApi->cancelGetThumbnail((node != nil) ? [node getCPtr] : NULL);
}

- (void)setThumbnailNode:(MEGANode *)node sourceFilePath:(NSString *)sourceFilePath delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->setThumbnail((node != nil) ? [node getCPtr] : NULL, (sourceFilePath != nil) ? [sourceFilePath UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)setThumbnailNode:(MEGANode *)node sourceFilePath:(NSString *)sourceFilePath {
    self.megaApi->setThumbnail((node != nil) ? [node getCPtr] : NULL, (sourceFilePath != nil) ? [sourceFilePath UTF8String] : NULL);
}

- (void)getPreviewNode:(MEGANode *)node destinationFilePath:(NSString *)destinationFilePath delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->getPreview((node != nil) ? [node getCPtr] : NULL, (destinationFilePath != nil) ? [destinationFilePath UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)getPreviewNode:(MEGANode *)node destinationFilePath:(NSString *)destinationFilePath {
    self.megaApi->getPreview((node != nil) ? [node getCPtr] : NULL, (destinationFilePath != nil) ? [destinationFilePath UTF8String] : NULL);
}

- (void)cancelGetPreviewNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->cancelGetPreview((node != nil) ? [node getCPtr] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)cancelGetPreviewNode:(MEGANode *)node {
    self.megaApi->cancelGetPreview((node != nil) ? [node getCPtr] : NULL);
}

- (void)setPreviewNode:(MEGANode *)node sourceFilePath:(NSString *)sourceFilePath delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->setPreview((node != nil) ? [node getCPtr] : NULL, (sourceFilePath != nil) ? [sourceFilePath UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)setPreviewNode:(MEGANode *)node sourceFilePath:(NSString *)sourceFilePath {
    self.megaApi->setPreview((node != nil) ? [node getCPtr] : NULL, (sourceFilePath != nil) ? [sourceFilePath UTF8String] : NULL);
}

- (void)getAvatarUser:(MEGAUser *)user destinationFilePath:(NSString *)destinationFilePath delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->getUserAvatar((user != nil) ? [user getCPtr] : NULL, (destinationFilePath != nil) ? [destinationFilePath UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)getAvatarUser:(MEGAUser *)user destinationFilePath:(NSString *)destinationFilePath {
    self.megaApi->getUserAvatar((user != nil) ? [user getCPtr] : NULL, (destinationFilePath != nil) ? [destinationFilePath UTF8String] : NULL);
}

- (NSString *)avatarColorForUser:(MEGAUser *)user {
    if (user == nil) return nil;
    
    const char *val = self.megaApi->getUserAvatarColor((user != nil) ? [user getCPtr] : NULL);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (void)setAvatarUserWithSourceFilePath:(NSString *)sourceFilePath delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->setAvatar((sourceFilePath != nil) ? [sourceFilePath UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)setAvatarUserWithSourceFilePath:(NSString *)sourceFilePath {
    self.megaApi->setAvatar((sourceFilePath != nil) ? [sourceFilePath UTF8String] : NULL);
}

- (void)getUserAttributeForUser:(MEGAUser *)user type:(MEGAUserAttribute)type {
    self.megaApi->getUserAttribute((user != nil) ? [user getCPtr] : NULL, type);
}

- (void)getUserAttributeForUser:(MEGAUser *)user type:(MEGAUserAttribute)type delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->getUserAttribute((user != nil) ? [user getCPtr] : NULL, type, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)getUserAttributeType:(MEGAUserAttribute)type {
    self.megaApi->getUserAttribute(type);
}

- (void)getUserAttributeType:(MEGAUserAttribute)type delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->getUserAttribute(type, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)setUserAttributeType:(MEGAUserAttribute)type value:(NSString *)value {
    self.megaApi->setUserAttribute(type, (value != nil) ? [value UTF8String] : NULL);
}

- (void)setUserAttributeType:(MEGAUserAttribute)type value:(NSString *)value delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->setUserAttribute(type, (value != nil) ? [value UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

#pragma mark - Account management Requests

- (void)getAccountDetailsWithDelegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->getAccountDetails([self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)getAccountDetails {
    self.megaApi->getAccountDetails();
}

- (void)getPricingWithDelegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->getPricing([self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)getPricing {
    self.megaApi->getPricing();
}

- (void)getPaymentIdForProductHandle:(uint64_t)productHandle delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->getPaymentId(productHandle, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)getPaymentIdForProductHandle:(uint64_t)productHandle {
    self.megaApi->getPaymentId(productHandle);
}

- (void)submitPurchase:(MEGAPaymentMethod)gateway receipt:(NSString *)receipt delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->submitPurchaseReceipt(gateway, (receipt != nil) ? [receipt UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)submitPurchase:(MEGAPaymentMethod)gateway receipt:(NSString *)receipt {
    self.megaApi->submitPurchaseReceipt(gateway, (receipt != nil) ? [receipt UTF8String] : NULL);
}

- (void)changePassword:(NSString *)oldPassword newPassword:(NSString *)newPassword delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->changePassword((oldPassword != nil) ? [oldPassword UTF8String] : NULL, (newPassword != nil) ? [newPassword UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)changePassword:(NSString *)oldPassword newPassword:(NSString *)newPassword {
    self.megaApi->changePassword((oldPassword != nil) ? [oldPassword UTF8String] : NULL, (newPassword != nil) ? [newPassword UTF8String] : NULL);
}

- (void)inviteContactWithEmail:(NSString *)email message:(NSString *)message action:(MEGAInviteAction)action delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->inviteContact((email != nil) ? [email UTF8String] : NULL, (message != nil) ? [message UTF8String] : NULL, (int)action, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)inviteContactWithEmail:(NSString *)email message:(NSString *)message action:(MEGAInviteAction)action {
    self.megaApi->inviteContact((email != nil) ? [email UTF8String] : NULL, (message != nil) ? [message UTF8String] : NULL, (int)action);
}

- (void)replyContactRequest:(MEGAContactRequest *)request action:(MEGAReplyAction)action delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->replyContactRequest((request != nil) ? [request getCPtr] : NULL, (int)action, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)replyContactRequest:(MEGAContactRequest *)request action:(MEGAReplyAction)action {
    self.megaApi->replyContactRequest((request != nil) ? [request getCPtr] : NULL, (int)action);
}

- (void)removeContactUser:(MEGAUser *)user delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->removeContact((user != nil) ? [user getCPtr] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)removeContactUser:(MEGAUser *)user {
    self.megaApi->removeContact((user != nil) ? [user getCPtr] : NULL);
}

- (void)submitFeedbackWithRating:(NSInteger)rating comment:(NSString *)comment delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->submitFeedback((int)rating, (comment != nil) ? [comment UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)submitFeedbackWithRating:(NSInteger)rating comment:(NSString *)comment {
    self.megaApi->submitFeedback((int)rating, (comment != nil) ? [comment UTF8String] : NULL);
}

- (void)reportDebugEventWithText:(NSString *)text delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->reportDebugEvent((text != nil) ? [text UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)reportDebugEventWithText:(NSString *)text {
    self.megaApi->reportDebugEvent((text != nil) ? [text UTF8String] : NULL);
}

- (void)getUserDataWithDelegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->getUserData([self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)getUserData {
    self.megaApi->getUserData();
}

- (void)getUserDataWithMEGAUser:(MEGAUser *)user delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->getUserData((user != nil) ? [user getCPtr] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)getUserDataWithMEGAUser:(MEGAUser *)user {
    self.megaApi->getUserData((user != nil) ? [user getCPtr] : NULL);
}

- (void)getUserDataWithUser:(NSString *)user delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->getUserData((user != nil) ? [user UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)getUserDataWithUser:(NSString *)user {
    self.megaApi->getUserData((user != nil) ? [user UTF8String] : NULL);
}

#pragma mark - Transfer

- (MEGATransfer *)transferByTag:(NSInteger)transferTag {
    MegaTransfer *transfer = self.megaApi->getTransferByTag((int)transferTag);
    
    return transfer ? [[MEGATransfer alloc] initWithMegaTransfer:transfer cMemoryOwn:YES] : nil;
}

- (void)startUploadWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent delegate:(id<MEGATransferDelegate>)delegate {
    self.megaApi->startUpload((localPath != nil) ? [localPath UTF8String] : NULL, (parent != nil) ? [parent getCPtr] : NULL, [self createDelegateMEGATransferListener:delegate singleListener:YES]);
}

- (void)startUploadWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent {
    self.megaApi->startUpload((localPath != nil) ? [localPath UTF8String] : NULL, (parent != nil) ? [parent getCPtr] : NULL);
}

- (void)startUploadToFileWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent filename:(NSString *)filename delegate:(id<MEGATransferDelegate>)delegate {
    self.megaApi->startUpload((localPath != nil) ? [localPath UTF8String] : NULL, (parent != nil) ? [parent getCPtr] : NULL, (filename != nil) ? [filename UTF8String] : NULL, [self createDelegateMEGATransferListener:delegate singleListener:YES]);
}

- (void)startUploadToFileWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent filename:(NSString *)filename {
    self.megaApi->startUpload((localPath != nil) ? [localPath UTF8String] : NULL, (parent != nil) ? [parent getCPtr] : NULL, (filename != nil) ? [filename UTF8String] : NULL);
}

- (void)startUploadWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent appData:(NSString *)appData delegate:(id<MEGATransferDelegate>)delegate {
    self.megaApi->startUploadWithData((localPath != nil) ? [localPath UTF8String] : NULL, (parent != nil) ? [parent getCPtr] : NULL, (appData !=nil) ? [appData UTF8String] : NULL, [self createDelegateMEGATransferListener:delegate singleListener:YES]);
}

- (void)startUploadWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent appData:(NSString *)appData {
    self.megaApi->startUploadWithData((localPath != nil) ? [localPath UTF8String] : NULL, (parent != nil) ? [parent getCPtr] : NULL, (appData !=nil) ? [appData UTF8String] : NULL);
}

- (void)startUploadWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent appData:(NSString *)appData isSourceTemporary:(BOOL)isSourceTemporary delegate:(id<MEGATransferDelegate>)delegate {
    self.megaApi->startUploadWithData((localPath != nil) ? [localPath UTF8String] : NULL, (parent != nil) ? [parent getCPtr] : NULL, (appData !=nil) ? [appData UTF8String] : NULL, isSourceTemporary, [self createDelegateMEGATransferListener:delegate singleListener:YES]);
}

- (void)startUploadWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent appData:(NSString *)appData isSourceTemporary:(BOOL)isSourceTemporary {    
    self.megaApi->startUploadWithData((localPath != nil) ? [localPath UTF8String] : NULL, (parent != nil) ? [parent getCPtr] : NULL, (appData !=nil) ? [appData UTF8String] : NULL, isSourceTemporary);
}

- (void)startDownloadNode:(MEGANode *)node localPath:(NSString *)localPath delegate:(id<MEGATransferDelegate>)delegate {
    self.megaApi->startDownload((node != nil) ? [node getCPtr] : NULL, (localPath != nil) ? [localPath UTF8String] : NULL, [self createDelegateMEGATransferListener:delegate singleListener:YES]);
}

- (void)startDownloadNode:(MEGANode *)node localPath:(NSString *)localPath {
    self.megaApi->startDownload((node != nil) ? [node getCPtr] : NULL, (localPath != nil) ? [localPath UTF8String] : NULL);
}

- (void)startDownloadNode:(MEGANode *)node localPath:(NSString *)localPath appData:(NSString *)appData {
    self.megaApi->startDownloadWithData((node != nil) ? [node getCPtr] : NULL, (localPath != nil) ? [localPath UTF8String] : NULL, (appData != nil) ? [appData UTF8String] : NULL);
}

- (void)startDownloadNode:(MEGANode *)node localPath:(NSString *)localPath appData:(NSString *)appData delegate:(id<MEGATransferDelegate>)delegate{
    self.megaApi->startDownloadWithData((node != nil) ? [node getCPtr] : NULL, (localPath != nil) ? [localPath UTF8String] : NULL, (appData != nil) ? [appData UTF8String] : NULL, [self createDelegateMEGATransferListener:delegate singleListener:YES]);
}

- (void)startStreamingNode:(MEGANode *)node startPos:(NSNumber *)startPos size:(NSNumber *)size delegate:(id<MEGATransferDelegate>)delegate {
    self.megaApi->startStreaming((node != nil) ? [node getCPtr] : NULL, (startPos != nil) ? [startPos longLongValue] : 0, (size != nil) ? [size longLongValue] : 0, [self createDelegateMEGATransferListener:delegate singleListener:YES]);
}

- (void)startStreamingNode:(MEGANode *)node startPos:(NSNumber *)startPos size:(NSNumber *)size {
    self.megaApi->startStreaming((node != nil) ? [node getCPtr] : NULL, (startPos != nil) ? [startPos longLongValue] : 0, (size != nil) ? [size longLongValue] : 0, NULL);
}

- (void)cancelTransfer:(MEGATransfer *)transfer delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->cancelTransfer((transfer != nil) ? [transfer getCPtr] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)cancelTransfer:(MEGATransfer *)transfer {
    self.megaApi->cancelTransfer((transfer != nil) ? [transfer getCPtr] : NULL);
}

- (void)cancelTransfersForDirection:(NSInteger)direction delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->cancelTransfers((int)direction, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)cancelTransfersForDirection:(NSInteger)direction {
    self.megaApi->cancelTransfers((int)direction);
}

- (void)cancelTransferByTag:(NSInteger)transferTag delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->cancelTransferByTag((int)transferTag, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)cancelTransferByTag:(NSInteger)transferTag {
    self.megaApi->cancelTransferByTag((int)transferTag);
}

- (void)pauseTransfers:(BOOL)pause delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->pauseTransfers(pause, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)pauseTransfers:(BOOL)pause {
    self.megaApi->pauseTransfers(pause);
}

- (void)pauseTransfers:(BOOL)pause forDirection:(NSInteger)direction delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->pauseTransfers(pause, (int)direction, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)pauseTransfers:(BOOL)pause forDirection:(NSInteger)direction {
    self.megaApi->pauseTransfers(pause, (int)direction);
}

- (void)enableTransferResumption:(NSString *)loggedOutId {
    self.megaApi->enableTransferResumption((loggedOutId != nil) ? [loggedOutId UTF8String] : NULL);
}

- (void)enableTransferResumption {
    self.megaApi->enableTransferResumption();
}

- (void)disableTransferResumption:(NSString *)loggedOutId {
    self.megaApi->disableTransferResumption((loggedOutId != nil) ? [loggedOutId UTF8String] : NULL);
}

- (void)disableTransferResumption {
    self.megaApi->disableTransferResumption();
}

- (BOOL)areTransferPausedForDirection:(NSInteger)direction {
    return self.megaApi->areTransfersPaused((int)direction);
}

- (void)setUploadLimitWithBpsLimit:(NSInteger)bpsLimit {
    self.megaApi->setUploadLimit((int)bpsLimit);
}

#pragma mark - Filesystem inspection

- (NSInteger)numberChildrenForParent:(MEGANode *)parent {
    return self.megaApi->getNumChildren((parent != nil) ? [parent getCPtr] : NULL);
}

- (NSInteger)numberChildFilesForParent:(MEGANode *)parent {
    return self.megaApi->getNumChildFiles((parent != nil) ? [parent getCPtr] : NULL);
}

- (NSInteger)numberChildFoldersForParent:(MEGANode *)parent {
    return self.megaApi->getNumChildFolders((parent != nil) ? [parent getCPtr] : NULL);
}

- (MEGANodeList *)childrenForParent:(MEGANode *)parent order:(NSInteger)order {
    return [[MEGANodeList alloc] initWithNodeList:self.megaApi->getChildren((parent != nil) ? [parent getCPtr] : NULL, (int)order) cMemoryOwn:YES];
}

- (MEGANodeList *)childrenForParent:(MEGANode *)parent {
    return [[MEGANodeList alloc] initWithNodeList:self.megaApi->getChildren((parent != nil) ? [parent getCPtr] : NULL) cMemoryOwn:YES];
}

- (MEGANode *)childNodeForParent:(MEGANode *)parent name:(NSString *)name {
    if (parent == nil || name == nil) return nil;
    
    MegaNode *node = self.megaApi->getChildNode([parent getCPtr], [name UTF8String]);
    
    return node ? [[MEGANode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MEGANode *)parentNodeForNode:(MEGANode *)node {
    if (node == nil) return nil;
    
    MegaNode *parent = self.megaApi->getParentNode([node getCPtr]);
    
    return parent ? [[MEGANode alloc] initWithMegaNode:parent cMemoryOwn:YES] : nil;
}

- (NSString *)nodePathForNode:(MEGANode *)node {
    if (node == nil) return nil;
    
    const char *val = self.megaApi->getNodePath([node getCPtr]);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (MEGANode *)nodeForPath:(NSString *)path node:(MEGANode *)node {
    if (path == nil || node == nil) return nil;
    
    MegaNode *n = self.megaApi->getNodeByPath([path UTF8String], [node getCPtr]);
    
    return n ? [[MEGANode alloc] initWithMegaNode:n cMemoryOwn:YES] : Nil;
}

- (MEGANode *)nodeForPath:(NSString *)path {
    if (path == nil) return nil;
    
    MegaNode *node = self.megaApi->getNodeByPath([path UTF8String]);
    
    return node ? [[MEGANode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MEGANode *)nodeForHandle:(uint64_t)handle {
    if (handle == ::mega::INVALID_HANDLE) return nil;
    
    MegaNode *node = self.megaApi->getNodeByHandle(handle);
    
    return node ? [[MEGANode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MEGAUserList *)contacts {
    return [[MEGAUserList alloc] initWithUserList:self.megaApi->getContacts() cMemoryOwn:YES];
}

- (MEGAUser *)contactForEmail:(NSString *)email {
    if (email == nil) return nil;
    
    MegaUser *user = self.megaApi->getContact([email UTF8String]);
    return user ? [[MEGAUser alloc] initWithMegaUser:user cMemoryOwn:YES] : nil;
}

- (MEGANodeList *)inSharesForUser:(MEGAUser *)user {
    return [[MEGANodeList alloc] initWithNodeList:self.megaApi->getInShares((user != nil) ? [user getCPtr] : NULL) cMemoryOwn:YES];
}

- (MEGANodeList *)inShares {
    return [[MEGANodeList alloc] initWithNodeList:self.megaApi->getInShares() cMemoryOwn:YES];
}

- (MEGAShareList *)inSharesList {
    return [[MEGAShareList alloc] initWithShareList:self.megaApi->getInSharesList() cMemoryOwn:YES];
}

- (BOOL)isSharedNode:(MEGANode *)node {
    if (!node) return NO;
    
    return self.megaApi->isShared([node getCPtr]);
}

- (MEGAShareList *)outShares {
    return [[MEGAShareList alloc] initWithShareList:self.megaApi->getOutShares() cMemoryOwn:YES];
}

- (MEGAShareList *)outSharesForNode:(MEGANode *)node {
    return [[MEGAShareList alloc] initWithShareList:self.megaApi->getOutShares((node != nil) ? [node getCPtr] : NULL) cMemoryOwn:YES];
}

- (MEGAContactRequestList *)incomingContactRequests {
    return [[MEGAContactRequestList alloc] initWithMegaContactRequestList:self.megaApi->getIncomingContactRequests() cMemoryOwn:YES];
}

- (MEGAContactRequestList *)outgoingContactRequests {
    return [[MEGAContactRequestList alloc] initWithMegaContactRequestList:self.megaApi->getOutgoingContactRequests() cMemoryOwn:YES];
}

- (NSString *)fingerprintForFilePath:(NSString *)filePath {
    if (filePath == nil) return nil;
    
    const char *val = self.megaApi->getFingerprint([filePath UTF8String]);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (NSString *)fingerprintForAssetRepresentation:(ALAssetRepresentation *)assetRepresentation modificationTime:(NSDate *)modificationTime {
    if (assetRepresentation == nil) return nil;
    
    MEGAInputStream mis = MEGAInputStream(assetRepresentation);
    const char *val = self.megaApi->getFingerprint(&mis, (long long)[modificationTime timeIntervalSince1970]);
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (NSString *)fingerprintForData:(NSData *)data modificationTime:(NSDate *)modificationTime {
    if (data == nil) return nil;
    
    MEGAInputStream mis = MEGAInputStream(data);
    const char *val = self.megaApi->getFingerprint(&mis, (long long)[modificationTime timeIntervalSince1970]);
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (NSString *)fingerprintForNode:(MEGANode *)node {
    if (node == nil) return nil;
    
    const char *val = self.megaApi->getFingerprint([node getCPtr]);
    if (!val) return nil;

    NSString *ret = [[NSString alloc] initWithUTF8String:val];

    delete [] val;
    return ret;
}

- (MEGANode *)nodeForFingerprint:(NSString *)fingerprint {
    if (fingerprint == nil) return nil;
    
    MegaNode *node = self.megaApi->getNodeByFingerprint([fingerprint UTF8String]);
    
    return node ? [[MEGANode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MEGANode *)nodeForFingerprint:(NSString *)fingerprint parent:(MEGANode *)parent {
    if (fingerprint == nil) return nil;
    
    MegaNode *node = self.megaApi->getNodeByFingerprint([fingerprint UTF8String], (parent != nil) ? [parent getCPtr] : NULL);
    
    return node ? [[MEGANode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (BOOL)hasFingerprint:(NSString *)fingerprint{
    if (fingerprint == nil) return NO;
    
    return self.megaApi->hasFingerprint([fingerprint UTF8String]);
}

- (NSString *)CRCForFilePath:(NSString *)filePath {
    if (filePath == nil) return nil;
    
    const char *val = self.megaApi->getCRC([filePath UTF8String]);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (NSString *)CRCForFingerprint:(NSString *)fingerprint{
    if (fingerprint == nil) {
        return nil;
    }
    
    const char *val = self.megaApi->getCRCFromFingerprint([fingerprint UTF8String]);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (NSString *)CRCForNode:(MEGANode *)node {
    if (node == nil) return nil;
    
    const char *val = self.megaApi->getCRC([node getCPtr]);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (MEGANode *)nodeByCRC:(NSString *)crc parent:(MEGANode *)parent {
    if (crc == nil) return nil;
    
    MegaNode *node = self.megaApi->getNodeByCRC([crc UTF8String], (parent != nil) ? [parent getCPtr] : NULL);
    
    return node ? [[MEGANode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MEGAShareType)accessLevelForNode:(MEGANode *)node {
    if (node == nil) return MEGAShareTypeAccessUnkown;
    
    return (MEGAShareType) self.megaApi->getAccess([node getCPtr]);
}

- (MEGAError *)checkAccessForNode:(MEGANode *)node level:(MEGAShareType)level {
    if (node == nil) return nil;
    
    return [[MEGAError alloc] initWithMegaError:self.megaApi->checkAccess((node != nil) ? [node getCPtr] : NULL, (int) level).copy() cMemoryOwn:YES];
}

- (MEGAError *)checkMoveForNode:(MEGANode *)node target:(MEGANode *)target {
    return [[MEGAError alloc] initWithMegaError:self.megaApi->checkMove((node != nil) ? [node getCPtr] : NULL, (target != nil) ? [target getCPtr] : NULL).copy() cMemoryOwn:YES];
}

- (MEGANodeList *)nodeListSearchForNode:(MEGANode *)node searchString:(NSString *)searchString recursive:(BOOL)recursive {
    return [[MEGANodeList alloc] initWithNodeList:self.megaApi->search((node != nil) ? [node getCPtr] : NULL, (searchString != nil) ? [searchString UTF8String] : NULL, recursive) cMemoryOwn:YES];
}

- (MEGANodeList *)nodeListSearchForNode:(MEGANode *)node searchString:(NSString *)searchString {
    return [[MEGANodeList alloc] initWithNodeList:self.megaApi->search((node != nil) ? [node getCPtr] : NULL, (searchString != nil) ? [searchString UTF8String] : NULL, YES) cMemoryOwn:YES];
}

- (MEGANode *)authorizeNode:(MEGANode *)node {
    return [[MEGANode alloc] initWithMegaNode:self.megaApi->authorizeNode((node != nil) ? [node getCPtr] : NULL) cMemoryOwn:YES];
}

- (NSNumber *)sizeForNode:(MEGANode *)node {
    return [[NSNumber alloc] initWithLongLong:self.megaApi->getSize([node getCPtr])];
}

- (NSString *)escapeFsIncompatible:(NSString *)name {
    if (name == nil) return nil;
    
    const char *val = self.megaApi->escapeFsIncompatible([name UTF8String]);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (NSString *)unescapeFsIncompatible:(NSString *)localName {
    if (localName == nil) return nil;
    
    const char *val = self.megaApi->unescapeFsIncompatible([localName UTF8String]);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
    return ret;
}

- (void)changeApiUrl:(NSString *)apiURL disablepkp:(BOOL)disablepkp {
    self.megaApi->changeApiUrl((apiURL != nil) ? [apiURL UTF8String] : NULL, disablepkp);
}

- (BOOL)createThumbnail:(NSString *)imagePath destinatioPath:(NSString *)destinationPath {
    if (imagePath == nil || destinationPath == nil) return NO;
    
    return self.megaApi->createThumbnail([imagePath UTF8String], [destinationPath UTF8String]);
}

- (BOOL)createPreview:(NSString *)imagePath destinatioPath:(NSString *)destinationPath {
    if (imagePath == nil || destinationPath == nil) return NO;
    
    return self.megaApi->createPreview([imagePath UTF8String], [destinationPath UTF8String]);
}

#ifdef HAVE_LIBUV

#pragma mark - HTTP Proxy Server

- (BOOL)httpServerStart:(BOOL)localOnly port:(NSInteger)port {
    return self.megaApi->httpServerStart();
}

- (void)httpServerStop {
    self.megaApi->httpServerStop();
}

- (NSInteger)httpServerIsRunning {
    return (NSInteger)self.megaApi->httpServerIsRunning();
}

- (BOOL)httpServerIsLocalOnly {
    return self.megaApi->httpServerIsLocalOnly();
}

- (void)httpServerEnableFileServer:(BOOL)enable {
    self.megaApi->httpServerEnableFileServer(enable);
}

- (BOOL)httpServerIsFileServerEnabled {
    return self.megaApi->httpServerIsFileServerEnabled();
}

- (void)httpServerEnableFolderServer:(BOOL)enable {
    self.megaApi->httpServerEnableFolderServer(enable);
}

- (BOOL)httpServerIsFolderServerEnabled {
    return self.megaApi->httpServerIsFolderServerEnabled();
}

- (void)httpServerSetRestrictedMode:(NSInteger)mode {
    self.megaApi->httpServerSetRestrictedMode((int)mode);
}

- (NSInteger)httpServerGetRestrictedMode {
    return (NSInteger)self.megaApi->httpServerGetRestrictedMode();
}

- (void)httpServerEnableSubtitlesSupport:(BOOL)enable {
    self.megaApi->httpServerEnableSubtitlesSupport(enable);
}

- (BOOL)httpServerIsSubtitlesSupportEnabled {
    return self.megaApi->httpServerIsSubtitlesSupportEnabled();
}

- (void)httpServerAddDelegate:(id<MEGATransferDelegate>)delegate {
    self.megaApi->httpServerAddListener([self createDelegateMEGATransferListener:delegate singleListener:NO]);
}

- (void)httpServerRemoveDelegate:(id<MEGATransferDelegate>)delegate {
    self.megaApi->httpServerRemoveListener([self createDelegateMEGATransferListener:delegate singleListener:NO]);
}

- (NSURL *)httpServerGetLocalLink:(MEGANode *)node {
    char *localLink = self.megaApi->httpServerGetLocalLink([node getCPtr]);
    
    return localLink ? [NSURL URLWithString:[NSString stringWithUTF8String:localLink]] : nil;
}

- (void)httpServerSetMaxBufferSize:(NSInteger)bufferSize {
    self.megaApi->httpServerSetMaxBufferSize((int)bufferSize);
}

- (NSInteger)httpServerGetMaxBufferSize {
    return (NSInteger)self.megaApi->httpServerGetMaxBufferSize();
}

- (void)httpServerSetMaxOutputSize:(NSInteger)outputSize {
    self.megaApi->httpServerSetMaxOutputSize((int)outputSize);
}

- (NSInteger)httpServerGetMaxOutputSize {
    return (NSInteger)self.megaApi->httpServerGetMaxOutputSize();
}

#endif

#pragma mark - Debug log messages

+ (void)setLogLevel:(MEGALogLevel)logLevel {
    MegaApi::setLogLevel((int)logLevel);
}

+ (void)setLogObject:(id<MEGALoggerDelegate>)delegate {
    DelegateMEGALogerListener *newLogger = new DelegateMEGALogerListener(delegate);
    delete externalLogger;
    externalLogger = newLogger;
}

+ (void)logWithLevel:(MEGALogLevel)logLevel message:(NSString *)message filename:(NSString *)filename line:(NSInteger)line {
    MegaApi::log((int)logLevel, (message != nil) ? [message UTF8String] : NULL, (filename != nil) ? [filename UTF8String] : NULL, (int)line);
}

+ (void)logWithLevel:(MEGALogLevel)logLevel message:(NSString *)message filename:(NSString *)filename {
    MegaApi::log((int)logLevel, (message != nil) ? [message UTF8String] : NULL, (filename != nil) ? [filename UTF8String] : NULL);
}

+ (void)logWithLevel:(MEGALogLevel)logLevel message:(NSString *)message {
    MegaApi::log((int)logLevel, (message != nil) ? [message UTF8String] : NULL);
}

#pragma mark - Private methods

- (MegaRequestListener *)createDelegateMEGARequestListener:(id<MEGARequestDelegate>)delegate singleListener:(BOOL)singleListener {
    if (delegate == nil) return nil;
    
    DelegateMEGARequestListener *delegateListener = new DelegateMEGARequestListener(self, delegate, singleListener);
    pthread_mutex_lock(&listenerMutex);
    _activeRequestListeners.insert(delegateListener);
    pthread_mutex_unlock(&listenerMutex);
    return delegateListener;
}

- (MegaTransferListener *)createDelegateMEGATransferListener:(id<MEGATransferDelegate>)delegate singleListener:(BOOL)singleListener {
    if (delegate == nil) return nil;
    
    DelegateMEGATransferListener *delegateListener = new DelegateMEGATransferListener(self, delegate, singleListener);
    pthread_mutex_lock(&listenerMutex);
    _activeTransferListeners.insert(delegateListener);
    pthread_mutex_unlock(&listenerMutex);
    return delegateListener;
}

- (MegaGlobalListener *)createDelegateMEGAGlobalListener:(id<MEGAGlobalDelegate>)delegate {
    if (delegate == nil) return nil;
    
    DelegateMEGAGlobalListener *delegateListener = new DelegateMEGAGlobalListener(self, delegate);
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

@end
