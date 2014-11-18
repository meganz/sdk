//
//  MEGASdk.m
//
//  Created by Javier Navarro on 06/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

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
#import "DelegateMEGARequestListener.h"
#import "DelegateMEGATransferListener.h"
#import "DelegateMEGAGlobalListener.h"
#import "DelegateMEGAListener.h"

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

#pragma mark - Init with app Key

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

#pragma mark - Add delegates

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

#pragma mark - Remove delegates

- (void)removeMEGADelegate:(id<MEGADelegate>)delegate {
    pthread_mutex_lock(&listenerMutex);
    std::set<DelegateMEGAListener *>::iterator it = _activeMegaListeners.begin();
    while (it != _activeMegaListeners.end()) {
        DelegateMEGAListener *delegateListener = *it;
        if (delegateListener->getUserListener() == delegate) {
            self.megaApi->removeListener(delegateListener);
            _activeMegaListeners.erase(it++);
        }
        else it++;
    }
    pthread_mutex_unlock(&listenerMutex);
}

- (void)removeMEGARequestDelegate:(id<MEGARequestDelegate>)delegate {
    pthread_mutex_lock(&listenerMutex);
    std::set<DelegateMEGARequestListener *>::iterator it = _activeRequestListeners.begin();
    while (it != self.activeRequestListeners.end()) {
        DelegateMEGARequestListener *delegateListener = *it;
        if (delegateListener->getUserListener() == delegate) {
            self.megaApi->removeRequestListener(delegateListener);
            _activeRequestListeners.erase(it++);
        }
        else it++;
    }
    pthread_mutex_unlock(&listenerMutex);
}

- (void)removeMEGATransferDelegate:(id<MEGATransferDelegate>)delegate {
    pthread_mutex_lock(&listenerMutex);
    std::set<DelegateMEGATransferListener *>::iterator it = _activeTransferListeners.begin();
    while (it != _activeTransferListeners.end()) {
        DelegateMEGATransferListener *delegateListener = *it;
        if (delegateListener->getUserListener() == delegate) {
            self.megaApi->removeTransferListener(delegateListener);
            _activeTransferListeners.erase(it++);
        }
        else it++;
    }
    pthread_mutex_unlock(&listenerMutex);

}

- (void)removeMEGAGlobalDelegate:(id<MEGAGlobalDelegate>)delegate {
    pthread_mutex_lock(&listenerMutex);
    std::set<DelegateMEGAGlobalListener *>::iterator it = _activeGlobalListeners.begin();
    while (it != _activeGlobalListeners.end()) {
        DelegateMEGAGlobalListener *delegateListener = *it;
        if (delegateListener->getUserListener() == delegate) {
            self.megaApi->removeGlobalListener(delegateListener);
            _activeGlobalListeners.erase(it++);
        }
        else it++;
    }
    pthread_mutex_unlock(&listenerMutex);

}

#pragma mark - Generic methods

- (NSString *)base64pwkeyWithPassword:(NSString *)password {
    if(password == nil) return nil;
    
    const char *val = self.megaApi->getBase64PwKey([password UTF8String]);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete val;
    return ret;
}

- (NSString *)stringHashWithBase64pwkey:(NSString *)base64pwkey email:(NSString *)email {
    if(base64pwkey == nil || email == nil)  return  nil;
    
    const char *val = self.megaApi->getStringHash([base64pwkey UTF8String], [email UTF8String]);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete val;
    return ret;
}

+ (uint64_t)base64ToHandle:(NSString *)base64Handle {
    if(base64Handle == nil) return ::mega::INVALID_HANDLE;
    
    return MegaApi::base64ToHandle([base64Handle UTF8String]);
}
- (void)retryPendingConnections {
    self.megaApi->retryPendingConnections();
}

#pragma mark - Login

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
    
    delete val;
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

#pragma mark - Create account and confirm account

- (void)createAccountWithEmail:(NSString *)email password:(NSString *)password name:(NSString *)name {
    self.megaApi->createAccount((email != nil) ? [email UTF8String] : NULL, (password != nil) ? [password UTF8String] : NULL, (name != nil) ? [name UTF8String] : NULL);
}

- (void)createAccountWithEmail:(NSString *)email password:(NSString *)password name:(NSString *)name delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->createAccount((email != nil) ? [email UTF8String] : NULL, (password != nil) ? [password UTF8String] : NULL, (name != nil) ? [name UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)fastCreateAccountWithEmail:(NSString *)email base64pwkey:(NSString *)base64pwkey name:(NSString *)name {
    self.megaApi->fastCreateAccount((email != nil) ? [email UTF8String] : NULL, (base64pwkey != nil) ? [base64pwkey UTF8String] : NULL, (name != nil) ? [name UTF8String] : NULL);
}

- (void)fastCreateAccountWithEmail:(NSString *)email base64pwkey:(NSString *)base64pwkey name:(NSString *)name delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->fastCreateAccount((email != nil) ? [email UTF8String] : NULL, (base64pwkey != nil) ? [base64pwkey UTF8String] : NULL, (name != nil) ? [name UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)querySignupWithLink:(NSString *)link {
    self.megaApi->querySignupLink((link != nil) ? [link UTF8String] : NULL);
}

- (void)querySignupWithLink:(NSString *)link delegate:(id<MEGARequestDelegate>)delegate {
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

- (NSInteger)isLoggedIn {
    return self.megaApi->isLoggedIn();
}

- (NSString *)myEmail {
    const char *val = self.megaApi->getMyEmail();
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete val;
    return ret;
}

#pragma mark - Node actions

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


- (void)shareNode:(MEGANode *)node withUser:(MEGAUser *)user level:(NSInteger)level delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->share((node != nil) ? [node getCPtr] : NULL, (user != nil) ? [user getCPtr] : NULL, (int)level, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)shareNode:(MEGANode *)node withUser:(MEGAUser *)user level:(NSInteger)level {
    self.megaApi->share((node != nil) ? [node getCPtr] : NULL, (user != nil) ? [user getCPtr] : NULL, (int)level);
}

- (void)loginWithFolderLink:(NSString *)folderLink delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->loginToFolder((folderLink != nil) ? [folderLink UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)loginWithFolderLink:(NSString *)folderLink {
    self.megaApi->loginToFolder((folderLink != nil) ? [folderLink UTF8String] : NULL);
}

- (void)importMegaFileLink:(NSString *)megaFileLink parent:(MEGANode *)parent delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->importFileLink((megaFileLink != nil) ? [megaFileLink UTF8String] : NULL, (parent != nil) ? [parent getCPtr] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)importMegaFileLink:(NSString *)megaFileLink parent:(MEGANode *)parent {
    self.megaApi->importFileLink((megaFileLink != nil) ? [megaFileLink UTF8String] : NULL, (parent != nil) ? [parent getCPtr] : NULL);
}

- (void)publicNodeWithMegaFileLink:(NSString *)megaFileLink delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->getPublicNode((megaFileLink != nil) ? [megaFileLink UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)publicNodeWithMegaFileLink:(NSString *)megaFileLink {
    self.megaApi->getPublicNode((megaFileLink != nil) ? [megaFileLink UTF8String] : NULL);
}

#pragma mark - Attributes node

- (void)getThumbnailWithNode:(MEGANode *)node destinationFilePath:(NSString *)destinationFilePath delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->getThumbnail((node != nil) ? [node getCPtr] : NULL, (destinationFilePath != nil) ? [destinationFilePath UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)getThumbnailWithNode:(MEGANode *)node destinationFilePath:(NSString *)destinationFilePath {
    self.megaApi->getThumbnail((node != nil) ? [node getCPtr] : NULL, (destinationFilePath != nil) ? [destinationFilePath UTF8String] : NULL);
}

- (void)setThumbnailWithNode:(MEGANode *)node sourceFilePath:(NSString *)sourceFilePath delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->setThumbnail((node != nil) ? [node getCPtr] : NULL, (sourceFilePath != nil) ? [sourceFilePath UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)setThumbnailWithNode:(MEGANode *)node sourceFilePath:(NSString *)sourceFilePath {
    self.megaApi->setThumbnail((node != nil) ? [node getCPtr] : NULL, (sourceFilePath != nil) ? [sourceFilePath UTF8String] : NULL);
}

- (void)getPreviewWithNode:(MEGANode *)node destinationFilePath:(NSString *)destinationFilePath delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->getPreview((node != nil) ? [node getCPtr] : NULL, (destinationFilePath != nil) ? [destinationFilePath UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)getPreviewWithNode:(MEGANode *)node destinationFilePath:(NSString *)destinationFilePath {
    self.megaApi->getPreview((node != nil) ? [node getCPtr] : NULL, (destinationFilePath != nil) ? [destinationFilePath UTF8String] : NULL);
}

- (void)setPreviewWithNode:(MEGANode *)node sourceFilePath:(NSString *)sourceFilePath delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->setPreview((node != nil) ? [node getCPtr] : NULL, (sourceFilePath != nil) ? [sourceFilePath UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)setPreviewWithNode:(MEGANode *)node sourceFilePath:(NSString *)sourceFilePath {
    self.megaApi->setPreview((node != nil) ? [node getCPtr] : NULL, (sourceFilePath != nil) ? [sourceFilePath UTF8String] : NULL);
}

#pragma mark - Attributes user

- (void)getAvatarWithUser:(MEGAUser *)user destinationFilePath:(NSString *)destinationFilePath delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->getUserAvatar((user != nil) ? [user getCPtr] : NULL, (destinationFilePath != nil) ? [destinationFilePath UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)getAvatarWithUser:(MEGAUser *)user destinationFilePath:(NSString *)destinationFilePath {
    self.megaApi->getUserAvatar((user != nil) ? [user getCPtr] : NULL, (destinationFilePath != nil) ? [destinationFilePath UTF8String] : NULL);
}

#pragma mark - Export, import and fetch nodes

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

- (void)fetchNodesWithListener:(id<MEGARequestDelegate>)delegate {
    self.megaApi->fetchNodes([self createDelegateMEGARequestListener:delegate singleListener:YES]);
}


- (void)fetchNodes {
    self.megaApi->fetchNodes();
}

#pragma mark - User account details and actions

- (void)getAccountDetailsWithDelegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->getAccountDetails([self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)getAccountDetails {
    self.megaApi->getAccountDetails();
}

- (void)pricingWithDelegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->getPricing([self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)pricing {
    self.megaApi->getPricing();
}

- (void)getPaymentURLWithProductHandle:(uint64_t)productHandle delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->getPaymentUrl(productHandle, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)getPaymentULRWithProductHandle:(uint64_t)productHandle {
    self.megaApi->getPaymentUrl(productHandle);
}

- (NSString *)masterKey {
    const char *val = self.megaApi->exportMasterKey();
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete val;
    return ret;
}

- (void)changePasswordWithOldPassword:(NSString *)oldPassword newPassword:(NSString *)newPassword delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->changePassword((oldPassword != nil) ? [oldPassword UTF8String] : NULL, (newPassword != nil) ? [newPassword UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)changePasswordWithOldPassword:(NSString *)oldPassword newPassword:(NSString *)newPassword {
    self.megaApi->changePassword((oldPassword != nil) ? [oldPassword UTF8String] : NULL, (newPassword != nil) ? [newPassword UTF8String] : NULL);
}

- (void)addContactWithEmail:(NSString *)email delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->addContact((email != nil) ? [email UTF8String] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)addContactWithEmail:(NSString *)email {
    self.megaApi->addContact((email != nil) ? [email UTF8String] : NULL);
}

- (void)removeContactWithUser:(MEGAUser *)user delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->removeContact((user != nil) ? [user getCPtr] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)removeContactWithUser:(MEGAUser *)user {
    self.megaApi->removeContact((user != nil) ? [user getCPtr] : NULL);
}

- (void)logoutWithDelegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->logout([self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)logout {
    self.megaApi->logout();
}

#pragma mark - Transfer

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

- (void)startDownloadWithNode:(MEGANode *)node localPath:(NSString *)localPath delegate:(id<MEGATransferDelegate>)delegate {
    self.megaApi->startDownload((node != nil) ? [node getCPtr] : NULL, (localPath != nil) ? [localPath UTF8String] : NULL, [self createDelegateMEGATransferListener:delegate singleListener:YES]);
}

- (void)startDownloadWithNode:(MEGANode *)node localPath:(NSString *)localPath {
    self.megaApi->startDownload((node != nil) ? [node getCPtr] : NULL, (localPath != nil) ? [localPath UTF8String] : NULL);
}

- (void)cancelTransferWithTransfer:(MEGATransfer *)transfer delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->cancelTransfer((transfer != nil) ? [transfer getCPtr] : NULL, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)cancelTransferWithTransfer:(MEGATransfer *)transfer {
    self.megaApi->cancelTransfer((transfer != nil) ? [transfer getCPtr] : NULL);
}

- (void)cancelTransfersWithDirection:(NSInteger)direction delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->cancelTransfers((int)direction, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)cancelTransfersWithDirection:(NSInteger)direction {
    self.megaApi->cancelTransfers((int)direction);
}

- (void)pauseTransersWithPause:(BOOL)pause delegate:(id<MEGARequestDelegate>)delegate {
    self.megaApi->pauseTransfers(pause, [self createDelegateMEGARequestListener:delegate singleListener:YES]);
}

- (void)pauseTransersWithPause:(BOOL)pause {
    self.megaApi->pauseTransfers(pause);
}

- (void)setUploadLimitWithBpsLimit:(NSInteger)bpsLimit {
    self.megaApi->setUploadLimit((int)bpsLimit);
}

- (MEGATransferList *)transfers {
    return [[MEGATransferList alloc] initWithTransferList:self.megaApi->getTransfers() cMemoryOwn:YES];
}

- (NSInteger)pendingUploads {
    return self.megaApi->getNumPendingUploads();
}

- (NSInteger)pendingDownloads {
    return self.megaApi->getNumPendingDownloads();
}

- (NSInteger)totalUploads {
    return self.megaApi->getTotalUploads();
}

- (NSInteger)totalDownloads {
    return self.megaApi->getTotalDownloads();
}

- (NSNumber *)totalsDownloadedBytes {
    return [[NSNumber alloc] initWithLongLong:self.megaApi->getTotalDownloadedBytes()];
}

- (NSNumber *)totalsUploadedBytes {
    return [[NSNumber alloc] initWithLongLong:self.megaApi->getTotalUploadedBytes()];
}

- (void)resetTotalDownloads {
    self.megaApi->resetTotalDownloads();
}

- (void)resetTotalUploads {
    self.megaApi->resetTotalUploads();
}

- (NSInteger)numberChildrenWithParent:(MEGANode *)parent {
    return self.megaApi->getNumChildren((parent != nil) ? [parent getCPtr] : NULL);
}

- (NSInteger)numberChildFilesWithParent:(MEGANode *)parent {
    return self.megaApi->getNumChildFiles((parent != nil) ? [parent getCPtr] : NULL);
}

- (NSInteger)numberChildFoldersWithParent:(MEGANode *)parent {
    return self.megaApi->getNumChildFolders((parent != nil) ? [parent getCPtr] : NULL);
}

- (MEGANodeList *)childrenWithParent:(MEGANode *)parent order:(NSInteger)order {
    return [[MEGANodeList alloc] initWithNodeList:self.megaApi->getChildren((parent != nil) ? [parent getCPtr] : NULL, (int)order) cMemoryOwn:YES];
}

- (MEGANodeList *)childrenWithParent:(MEGANode *)parent {
    return [[MEGANodeList alloc] initWithNodeList:self.megaApi->getChildren((parent != nil) ? [parent getCPtr] : NULL) cMemoryOwn:YES];
}

- (MEGANode *)childNodeWithParent:(MEGANode *)parent name:(NSString *)name {
    if (parent == nil || name == nil) return nil;
    
    MegaNode *node = self.megaApi->getChildNode([parent getCPtr], [name UTF8String]);
    
    return node ? [[MEGANode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MEGANode *)parentNodeWithNode:(MEGANode *)node {
    if (node == nil) return nil;
    
    MegaNode *parent = self.megaApi->getParentNode([node getCPtr]);
    
    return parent ? [[MEGANode alloc] initWithMegaNode:parent cMemoryOwn:YES] : nil;
}

- (NSString *)nodePathWithNode:(MEGANode *)node {
    if (node == nil) return nil;
    
    const char *val = self.megaApi->getNodePath([node getCPtr]);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete val;
    return ret;
}

- (MEGANode *)nodeWithPath:(NSString *)path node:(MEGANode *)node {
    if (path == nil || node == nil) return nil;
    
    MegaNode *n = self.megaApi->getNodeByPath([path UTF8String], [node getCPtr]);
    
    return n ? [[MEGANode alloc] initWithMegaNode:n cMemoryOwn:YES] : Nil;
}

- (MEGANode *)nodeWithPath:(NSString *)path {
    if (path == nil) return nil;
    
    MegaNode *node = self.megaApi->getNodeByPath([path UTF8String]);
    
    return node ? [[MEGANode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MEGANode *)nodeWithHandle:(uint64_t)handle {
    if (handle == ::mega::INVALID_HANDLE) return nil;
    
    MegaNode *node = self.megaApi->getNodeByHandle(handle);
    
    return node ? [[MEGANode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MEGAUserList *)contacts {
    return [[MEGAUserList alloc] initWithUserList:self.megaApi->getContacts() cMemoryOwn:YES];
}

- (MEGAUser *)contactWithEmail:(NSString *)email {
    if (email == nil) return nil;
    
    MegaUser *user = self.megaApi->getContact([email UTF8String]);
    return user ? [[MEGAUser alloc] initWithMegaUser:user cMemoryOwn:YES] : nil;
}

- (MEGANodeList *)inSharesWithUser:(MEGAUser *)user {
    return [[MEGANodeList alloc] initWithNodeList:self.megaApi->getInShares((user != nil) ? [user getCPtr] : NULL) cMemoryOwn:YES];
}

- (MEGANodeList *)inShares {
    return [[MEGANodeList alloc] initWithNodeList:self.megaApi->getInShares() cMemoryOwn:YES];
}

- (MEGAShareList *)outSharesWithNode:(MEGANode *)node {
    return [[MEGAShareList alloc] initWithShareList:self.megaApi->getOutShares((node != nil) ? [node getCPtr] : NULL) cMemoryOwn:YES];
}

- (NSString *)fingerprintWithFilePath:(NSString *)filePath {
    if (filePath == nil) return nil;
    
    const char *val = self.megaApi->getFingerprint([filePath UTF8String]);
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete val;
    return ret;
}

- (NSString *)finferprintWithNode:(MEGANode *)node {
    if (node == nil) return nil;
    
    return self.megaApi->getFingerprint([node getCPtr]) ? [[NSString alloc] initWithUTF8String:self.megaApi->getFingerprint([node getCPtr])] : nil;
}

- (MEGANode *)nodeWithFingerprint:(NSString *)fingerprint {
    if (fingerprint == nil) return nil;
    
    MegaNode *node = self.megaApi->getNodeByFingerprint([fingerprint UTF8String]);
    
    return node ? [[MEGANode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (BOOL)hasFingerprint:(NSString *)fingerprint{
    if (fingerprint == nil) return NO;
    
    return self.megaApi->hasFingerprint([fingerprint UTF8String]);
}

- (NSInteger)accessLevelWithNode:(MEGANode *)node {
    if (node == nil) return -1;
    
    return self.megaApi->getAccess([node getCPtr]);
}

- (MEGAError *)checkAccessWithNode:(MEGANode *)node level:(NSInteger)level {
    if (node == nil) return nil;
    
    return [[MEGAError alloc] initWithMegaError:self.megaApi->checkAccess((node != nil) ? [node getCPtr] : NULL, (int) level).copy() cMemoryOwn:YES];
}

- (MEGAError *)checkMoveWithMnode:(MEGANode *)node target:(MEGANode *)target {
    return [[MEGAError alloc] initWithMegaError:self.megaApi->checkMove((node != nil) ? [node getCPtr] : NULL, (target != nil) ? [target getCPtr] : NULL).copy() cMemoryOwn:YES];
}

- (MEGANode *)rootNode {
    MegaNode *node = self.megaApi->getRootNode();
    return node ? [[MEGANode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MEGANode *)rubbishNode {
    MegaNode *node = self.megaApi->getRubbishNode();
    return node ? [[MEGANode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

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
