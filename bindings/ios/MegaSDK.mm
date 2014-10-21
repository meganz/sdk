//
//  MegaSDK.m
//
//  Created by Javier Navarro on 06/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MegaSDK.h"
#import "megaapi.h"
#import "MNode+init.h"
#import "MUser+init.h"
#import "MTransfer+init.h"
#import "MTransferList+init.h"
#import "MNodeList+init.h"
#import "MUserList+init.h"
#import "MError+init.h"
#import "MShareList+init.h"
#import "DelegateMRequestListener.h"
#import "DelegateMTransferListener.h"
#import "DelegateMGlobalListener.h"
#import "DelegateMListener.h"

#import <set>

using namespace mega;

@interface MegaSDK ()

@property std::set<DelegateMRequestListener *>activeRequestListeners;
@property std::set<DelegateMTransferListener *>activeTransferListeners;
@property std::set<DelegateMGlobalListener *>activeGlobalListeners;
@property std::set<DelegateMListener *>activeMegaListeners;
//CRITICAL_SECTION listenerMutex;

- (MegaRequestListener *)createDelegateMRequestListener:(id<MRequestDelegate>)delegate singleListener:(BOOL)singleListener;
- (MegaTransferListener *)createDelegateMTransferListener:(id<MTransferDelegate>)delegate singleListener:(BOOL)singleListener;
- (MegaGlobalListener *)createDelegateMGlobalListener:(id<MGlobalListenerDelegate>)delegate;
- (MegaListener *)createDelegateMListener:(id<MListenerDelegate>)delegate;

- (void)freeRequestListener:(DelegateMRequestListener *)delegate;
- (void)freeTransferListener:(DelegateMTransferListener *)delegate;

@property MegaApi *megaApi;
- (MegaApi *) getCPtr;

@end

@implementation MegaSDK

- (void)dealloc {
    delete _megaApi;
//    DeleteCriticalSection(&listenerMutex);
}

- (MegaApi *)getCPtr {
    return _megaApi;
}

#pragma mark - Init with app Key

- (instancetype)initWithAppKey:(NSString *)appKey userAgent:(NSString *)userAgent {
    self.megaApi = new MegaApi((appKey != nil) ? [appKey UTF8String] : (const char *)NULL, (const char *)NULL, (userAgent != nil) ? [userAgent UTF8String] : (const char *)NULL);
    
    return  self;
}

- (instancetype)initWithAppKey:(NSString *)appKey userAgent:(NSString *)userAgent basePath:(NSString *)basePath {
    self.megaApi = new MegaApi((appKey != nil) ? [appKey UTF8String] : (const char *)NULL, (basePath != nil) ? [basePath UTF8String] : (const char*)NULL, (userAgent != nil) ? [userAgent UTF8String] : (const char *)NULL);
    
    return self;
}

#pragma mark - Add delegates

- (void)addDelegate:(id<MListenerDelegate>)delegate {
    self.megaApi->addListener([self createDelegateMListener:delegate]);
}

- (void)addRequestDelegate:(id<MRequestDelegate>)delegate {
    self.megaApi->addRequestListener([self createDelegateMRequestListener:delegate singleListener:YES]);
}

- (void)addMTransferDelegate:(id<MTransferDelegate>)delegate {
    self.megaApi->addTransferListener([self createDelegateMTransferListener:delegate singleListener:YES]);
}

- (void)addGlobalDelegate:(id<MGlobalListenerDelegate>)delegate {
    self.megaApi->addGlobalListener([self createDelegateMGlobalListener:delegate]);
}

#pragma mark - Remove delegates
//TODO: remove delegates
- (void)removeDelegate:(id<MListenerDelegate>)delegate {
}

- (void)removeRequestDelegate:(id<MRequestDelegate>)delegate {

}

- (void)removeTransferDelegate:(id<MTransferDelegate>)delegate {

}

- (void)removeGlobalDelegate:(id<MGlobalListenerDelegate>)delegate {

}

- (NSString *)getBase64pwkeyWithPassword:(NSString *)password {
    if(password == nil) return nil;
    
    return self.megaApi->getBase64PwKey([password UTF8String]) ? [[NSString alloc] initWithUTF8String:self.megaApi->getBase64PwKey([password UTF8String])] : nil;
}

- (NSString *)getStringHashWithBase64pwkey:(NSString *)base64pwkey inBuf:(NSString *)inBuf {
    if(base64pwkey == nil || inBuf == nil)  return  nil;
    
    return self.megaApi->getStringHash([base64pwkey UTF8String], [inBuf UTF8String]) ? [[NSString alloc] initWithUTF8String:self.megaApi->getStringHash([base64pwkey UTF8String], [inBuf UTF8String])] : nil;
}

+ (uint64_t)base64ToHandle:(NSString *)base64Handle {
    if(base64Handle == nil) return ::mega::INVALID_HANDLE;
    
    return MegaApi::base64ToHandle([base64Handle UTF8String]);
}

+ (NSString *)ebcEncryptKeyWithEncryptionKey:(NSString *)encryptionKey plainKey:(NSString *)plainKey {
    if(encryptionKey == nil || plainKey == nil) return nil;
    
    return MegaApi::ebcEncryptKey([encryptionKey UTF8String], [plainKey UTF8String]) ? [[NSString alloc] initWithUTF8String:MegaApi::ebcEncryptKey([encryptionKey UTF8String], [plainKey UTF8String])] : nil;
}

- (void)retryPendingConnections {
    self.megaApi->retryPendingConnections();
}

//TODO: Listener
//- (void)retryPendingConnections

#pragma mark - Login

- (void)loginWithEmail:(NSString *)email password:(NSString *)password {
    self.megaApi->login((email != nil) ? [email UTF8String] : NULL, (password != nil) ? [password UTF8String] : NULL);
}

- (void)loginWithEmail:(NSString *)email password:(NSString *)password delegate:(id<MRequestDelegate>)delegateObject{
    self.megaApi->login((email != nil) ? [email UTF8String] : NULL, (password != nil) ? [password UTF8String] : NULL, [self createDelegateMRequestListener:delegateObject singleListener:YES]);
}

- (NSString *)dumpSession {
    return self.megaApi->dumpSession() ? [[NSString alloc] initWithUTF8String:self.megaApi->dumpSession()] : nil;
}

- (void)fastLoginWithEmail:(NSString *)email stringHash:(NSString *)stringHash base64pwKey:(NSString *)base64pwKey {
    self.megaApi->fastLogin((email != nil) ? [email UTF8String] : NULL, (stringHash != nil) ? [stringHash UTF8String] : NULL, (base64pwKey != nil) ? [base64pwKey UTF8String] : NULL);
}

- (void)fastLoginWithEmail:(NSString *)email stringHash:(NSString *)stringHash base64pwKey:(NSString *)base64pwKey delegate:(id<MRequestDelegate>)delegateObject {
    self.megaApi->fastLogin((email != nil) ? [email UTF8String] : NULL, (stringHash != nil) ? [stringHash UTF8String] : NULL, (base64pwKey != nil) ? [base64pwKey UTF8String] : NULL, [self createDelegateMRequestListener:delegateObject singleListener:YES]);
}

- (void)fastLoginWithSession:(NSString *)session {
    self.megaApi->fastLogin((session != nil) ? [session UTF8String] : NULL);
}

- (void)fastLoginWithSession:(NSString *)session delegate:(id<MRequestDelegate>)delegateObject {
    self.megaApi->fastLogin((session != nil) ? [session UTF8String] : NULL, [self createDelegateMRequestListener:delegateObject singleListener:YES]);
}

#pragma mark - Create account

- (void)createAccountWithEmail:(NSString *)email password:(NSString *)password name:(NSString *)name {
    self.megaApi->createAccount((email != nil) ? [email UTF8String] : NULL, (password != nil) ? [password UTF8String] : NULL, (name != nil) ? [name UTF8String] : NULL);
}

- (void)createAccountWithEmail:(NSString *)email password:(NSString *)password name:(NSString *)name delegate:(id<MRequestDelegate>)delegateObject {
    self.megaApi->createAccount((email != nil) ? [email UTF8String] : NULL, (password != nil) ? [password UTF8String] : NULL, (name != nil) ? [name UTF8String] : NULL, [self createDelegateMRequestListener:delegateObject singleListener:YES]);
}

- (void)fastCreateAccountWithEmail:(NSString *)email base64pwkey:(NSString *)base64pwkey name:(NSString *)name {
    self.megaApi->fastCreateAccount((email != nil) ? [email UTF8String] : NULL, (base64pwkey != nil) ? [base64pwkey UTF8String] : NULL, (name != nil) ? [name UTF8String] : NULL);
}

- (void)fastCreateAccountWithEmail:(NSString *)email base64pwkey:(NSString *)base64pwkey name:(NSString *)name delegate:(id<MRequestDelegate>)delegateObject {
    self.megaApi->fastCreateAccount((email != nil) ? [email UTF8String] : NULL, (base64pwkey != nil) ? [base64pwkey UTF8String] : NULL, (name != nil) ? [name UTF8String] : NULL, [self createDelegateMRequestListener:delegateObject singleListener:YES]);
}

- (void)querySignupWithLink:(NSString *)link {
    self.megaApi->querySignupLink((link != nil) ? [link UTF8String] : NULL);
}

//TODO:listener
//- (void)querySignupWithLink:(NSString *)link {

- (void)confirmAccountWithLink:(NSString *)link password:(NSString *)password {
    self.megaApi->confirmAccount((link != nil) ? [link UTF8String] : NULL, (password != nil) ? [password UTF8String] : NULL);
}

//TODO:LIstener
//- (void)confirmAccountWithLink:(NSString *)link password:(NSString *)password {

- (void)fastConfirmAccountWithLink:(NSString *)link base64pwkey:(NSString *)base64pwkey {
    self.megaApi->fastConfirmAccount((link != nil) ? [link UTF8String] : NULL, (base64pwkey != nil) ? [base64pwkey UTF8String] : NULL);
}

//TODO:listener
//- (void)fastConfirmAccountWithLink:(NSString *)link base64pwkey:(NSString *)base64pwkey {

- (NSInteger)isLoggedIn {
    return self.megaApi->isLoggedIn();
}

- (NSString *)getMyEmail {
    return self.megaApi->getMyEmail() ? [[NSString alloc] initWithUTF8String:self.megaApi->getMyEmail()] : nil;
}

//TODO: listener
//- (void)createFolderWithName:(NSString *)name parent:(MNode *)parent {

- (void)createFolderWithName:(NSString *)name parent:(MNode *)parent {
    self.megaApi->createFolder((name != nil) ? [name UTF8String] : NULL, (parent != nil) ? [parent getCPtr] : NULL);
}

//TODO:listener
//- (void)moveNode:(MNode *)node newParent:(MNode *)newParent

- (void)moveNode:(MNode *)node newParent:(MNode *)newParent {
    self.megaApi->moveNode((node != nil) ? [node getCPtr] : NULL, (newParent != nil) ? [newParent getCPtr] : NULL);
}

//TODO:listener
//- (void)copyNode:(MNode *)node newParent:(MNode *)newParent {

- (void)copyNode:(MNode *)node newParent:(MNode *)newParent {
    self.megaApi->copyNode((node != nil) ? [node getCPtr] : NULL, (newParent != nil) ? [newParent getCPtr] : NULL);
}

//TODO: Listener
//- (void)renameNode:(MNode *)node newName:(NSString *)newName {

- (void)renameNode:(MNode *)node newName:(NSString *)newName {
    self.megaApi->renameNode((node != nil) ? [node getCPtr] : NULL, (newName != nil) ? [newName UTF8String] : NULL);
}

//TODO:listener
//- (void)removeNode:(MNode *)node {

- (void)removeNode:(MNode *)node {
    self.megaApi->remove((node != nil) ? [node getCPtr] : NULL);
}

//TODO:listener
//- (void)shareNode:(MNode *)node withUser:(MUser *)user level:(NSInteger)level {

- (void)shareNode:(MNode *)node withUser:(MUser *)user level:(NSInteger)level {
    self.megaApi->share((node != nil) ? [node getCPtr] : NULL, (user != nil) ? [user getCPtr] : NULL, (int)level);
}

//TODO:Listener
//- (void)shareNode:(MNode *)node withEmail:(NSString *)email level:(NSInteger)level {

- (void)shareNode:(MNode *)node withEmail:(NSString *)email level:(NSInteger)level {
    self.megaApi->share((node != nil) ? [node getCPtr] : NULL, (email != nil) ? [email UTF8String] : NULL, (int)level);
}

//TODO: listener
//- (void)folderAccessWithMegaFileLink:(NSString *)megaFolderLink {

- (void)folderAccessWithMegaFileLink:(NSString *)megaFolderLink {
    self.megaApi->folderAccess((megaFolderLink != nil) ? [megaFolderLink UTF8String] : NULL);
}

//TODO:listener
//- (void)importMegaFileLink:(NSString *)megaFileLink parent:(MNode *)parent {

- (void)importMegaFileLink:(NSString *)megaFileLink parent:(MNode *)parent {
    self.megaApi->importFileLink((megaFileLink != nil) ? [megaFileLink UTF8String] : NULL, (parent != nil) ? [parent getCPtr] : NULL);
}

//TODO: listener
//- (void)importPublicNode:(MNode *)publicNode parent:(MNode *)parent {

- (void)importPublicNode:(MNode *)publicNode parent:(MNode *)parent {
    self.megaApi->importPublicNode((publicNode != nil) ? [publicNode getCPtr] : NULL, (parent != nil) ? [parent getCPtr] : NULL);
}

//TODO:Listener
//- (void)getPublicNodeWithMegaFileLink:(NSString *)megaFileLink {

- (void)getPublicNodeWithMegaFileLink:(NSString *)megaFileLink {
    self.megaApi->getPublicNode((megaFileLink != nil) ? [megaFileLink UTF8String] : NULL);
}

- (void)getThumbnailWithNode:(MNode *)node destinationFilePath:(NSString *)destinationFilePath delegate:(id<MRequestDelegate>)delegateObject {
    self.megaApi->getThumbnail((node != nil) ? [node getCPtr] : NULL, (destinationFilePath != nil) ? [destinationFilePath UTF8String] : NULL, [self createDelegateMRequestListener:delegateObject singleListener:YES]);
}

- (void)getThumbnailWithNode:(MNode *)node destinationFilePath:(NSString *)destinationFilePath {
    self.megaApi->getThumbnail((node != nil) ? [node getCPtr] : NULL, (destinationFilePath != nil) ? [destinationFilePath UTF8String] : NULL);
}

//TODO: listener
//- (void)setThumbnailWithNode:(MNode *)node sourceFilePath:(NSString *)sourceFilePath {

- (void)setThumbnailWithNode:(MNode *)node sourceFilePath:(NSString *)sourceFilePath {
    self.megaApi->setThumbnail((node != nil) ? [node getCPtr] : NULL, (sourceFilePath != nil) ? [sourceFilePath UTF8String] : NULL);
}

- (void)getPreviewWithNode:(MNode *)node destinationFilePath:(NSString *)destinationFilePath delegate:(id<MRequestDelegate>)delegateObject {
    self.megaApi->getPreview((node != nil) ? [node getCPtr] : NULL, (destinationFilePath != nil) ? [destinationFilePath UTF8String] : NULL, [self createDelegateMRequestListener:delegateObject singleListener:YES]);
}

- (void)getPreviewWithNode:(MNode *)node destinationFilePath:(NSString *)destinationFilePath {
    self.megaApi->getPreview((node != nil) ? [node getCPtr] : NULL, (destinationFilePath != nil) ? [destinationFilePath UTF8String] : NULL);
}

//TODO: listener
//- (void)setPreviewWithNode:(MNode *)node sourceFilePath:(NSString *)sourceFilePath {

- (void)setPreviewWithNode:(MNode *)node sourceFilePath:(NSString *)sourceFilePath {
    self.megaApi->setPreview((node != nil) ? [node getCPtr] : NULL, (sourceFilePath != nil) ? [sourceFilePath UTF8String] : NULL);
}

//TODO: listener
//- (void)getAvatarWithUser:(MUser *)user destinationFilePath:(NSString *)destinationFilePath {

- (void)getAvatarWithUser:(MUser *)user destinationFilePath:(NSString *)destinationFilePath {
    self.megaApi->getUserAvatar((user != nil) ? [user getCPtr] : NULL, (destinationFilePath != nil) ? [destinationFilePath UTF8String] : NULL);
}

//TODO: listener
//- (void)exportNode:(MNode *)node {

- (void)exportNode:(MNode *)node {
    self.megaApi->exportNode((node != nil) ? [node getCPtr] : NULL);
}

//TODO:listener
//- (void)disableExportNode:(MNode *)node {

- (void)disableExportNode:(MNode *)node {
    self.megaApi->disableExport((node != nil) ? [node getCPtr] : NULL);
}

- (void)fetchNodesWithListener:(id<MRequestDelegate>)delegateObject {
    self.megaApi->fetchNodes([self createDelegateMRequestListener:delegateObject singleListener:YES]);
}


- (void)fetchNodes {
    self.megaApi->fetchNodes();
}

//TODO:listener
//- (void)getAccountDetails {

- (void)getAccountDetails {
    self.megaApi->getAccountDetails();
}

//TODO:Listener
//- (void)changePasswordWithOldPassword:(NSString *)oldPassword newPassword:(NSString *)newPassword {

- (void)changePasswordWithOldPassword:(NSString *)oldPassword newPassword:(NSString *)newPassword {
    self.megaApi->changePassword((oldPassword != nil) ? [oldPassword UTF8String] : NULL, (newPassword != nil) ? [newPassword UTF8String] : NULL);
}

//TODO:listener
//- (void)addContactWithEmail:(NSString *)email {

- (void)addContactWithEmail:(NSString *)email {
    self.megaApi->addContact((email != nil) ? [email UTF8String] : NULL);
}

//TODO: listener
//- (void)removeContactWithEmail:(NSString *)email {

- (void)removeContactWithEmail:(NSString *)email {
    self.megaApi->removeContact((email != nil) ? [email UTF8String] : NULL);
}

- (void)logoutWithDelegate:(id<MRequestDelegate>)delegateObject {
    self.megaApi->logout([self createDelegateMRequestListener:delegateObject singleListener:YES]);
}

- (void)logout {
    self.megaApi->logout();
}

//TODO:listener
//- (void)startUploadWithLocalPath:(NSString *)localPath parent:(MNode *)parent {

- (void)startUploadWithLocalPath:(NSString *)localPath parent:(MNode *)parent {
    self.megaApi->startUpload((localPath != nil) ? [localPath UTF8String] : NULL, (parent != nil) ? [parent getCPtr] : NULL);
}

//TODO: listener
//- (void)startUploadToFileWithLocalPath:(NSString *)localPath parent:(MNode *)parent filename:(NSString *)filename {

- (void)startUploadToFileWithLocalPath:(NSString *)localPath parent:(MNode *)parent filename:(NSString *)filename {
    self.megaApi->startUpload((localPath != nil) ? [localPath UTF8String] : NULL, (parent != nil) ? [parent getCPtr] : NULL, (filename != nil) ? [filename UTF8String] : NULL);
}

//TODO:Listener
//- (void)startDownloadWithNode:(MNode *)node localPath:(NSString *)localPath {

- (void)startDownloadWithNode:(MNode *)node localPath:(NSString *)localPath {
    self.megaApi->startDownload((node != nil) ? [node getCPtr] : NULL, (localPath != nil) ? [localPath UTF8String] : NULL);
}

//TODO:listener
//- (void)startPublicDownloadWithNode:(MNode *)node localPath:(NSString *)localPath {

- (void)startPublicDownloadWithNode:(MNode *)node localPath:(NSString *)localPath {
    self.megaApi->startPublicDownload((node != nil) ? [node getCPtr] : NULL, (localPath != nil) ? [localPath UTF8String] : NULL);
}

//TODO:listener
//- (void)cancelTransferWithTransfer:(MTransfer *)transfer {

- (void)cancelTransferWithTransfer:(MTransfer *)transfer {
    self.megaApi->cancelTransfer((transfer != nil) ? [transfer getCPtr] : NULL);
}

//TODO: listener
//- (void)cancelTransfersWithDirection:(NSInteger)direction {

- (void)cancelTransfersWithDirection:(NSInteger)direction {
    self.megaApi->cancelTransfers((int)direction);
}

//TODO:listener
//- (void)pauseTransersWithPause:(BOOL)pause {

- (void)pauseTransersWithPause:(BOOL)pause {
    self.megaApi->pauseTransfers(pause);
}

- (void)setUploadLimitWithBpsLimit:(NSInteger)bpsLimit {
    self.megaApi->setUploadLimit((int)bpsLimit);
}

- (MTransferList *)getTransfers {
    return [[MTransferList alloc] initWithTransferList:self.megaApi->getTransfers() cMemoryOwn:YES];
}

- (NSInteger)getNumPendingUploads {
    return self.megaApi->getNumPendingUploads();
}

- (NSInteger)getNumPendingDownloads {
    return self.megaApi->getNumPendingDownloads();
}

- (NSInteger)getTotalUploads {
    return self.megaApi->getTotalUploads();
}

- (NSInteger)getTotalDownloads {
    return self.megaApi->getTotalDownloads();
}

- (NSNumber *)getTotalsDownloadedBytes {
    return [[NSNumber alloc] initWithLongLong:self.megaApi->getTotalDownloadedBytes()];
}

- (NSNumber *)getTotalsUploadedBytes {
    return [[NSNumber alloc] initWithLongLong:self.megaApi->getTotalUploadedBytes()];
}

- (void)resetTotalDownloads {
    self.megaApi->resetTotalDownloads();
}

- (void)resetTotalUploads {
    self.megaApi->resetTotalUploads();
}

- (NSInteger)getNumChildrenWithParent:(MNode *)parent {
    return self.megaApi->getNumChildren((parent != nil) ? [parent getCPtr] : NULL);
}

- (NSInteger)getNumChildFilesWithParent:(MNode *)parent {
    return self.megaApi->getNumChildFiles((parent != nil) ? [parent getCPtr] : NULL);
}

- (NSInteger)getNumChildFoldersWithParent:(MNode *)parent {
    return self.megaApi->getNumChildFolders((parent != nil) ? [parent getCPtr] : NULL);
}

- (MNodeList *)getChildrenWithParent:(MNode *)parent order:(NSInteger)order {
    return [[MNodeList alloc] initWithNodeList:self.megaApi->getChildren((parent != nil) ? [parent getCPtr] : NULL, (int)order) cMemoryOwn:YES];
}

- (MNodeList *)getChildrenWithParent:(MNode *)parent {
    return [[MNodeList alloc] initWithNodeList:self.megaApi->getChildren((parent != nil) ? [parent getCPtr] : NULL) cMemoryOwn:YES];
}

- (MNode *)getChildNodeWithParent:(MNode *)parent name:(NSString *)name {
    if (parent == nil || name == nil) return nil;
    
    MegaNode *node = self.megaApi->getChildNode([parent getCPtr], [name UTF8String]);
    
    return node ? [[MNode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MNode *)getParentNodeWithNode:(MNode *)node {
    if (node == nil) return nil;
    
    MegaNode *parent = self.megaApi->getParentNode([node getCPtr]);
    
    return parent ? [[MNode alloc] initWithMegaNode:parent cMemoryOwn:YES] : nil;
}

- (NSString *)getNodePathWithNode:(MNode *)node {
    if (node == nil) return nil;
    
    return self.megaApi->getNodePath([node getCPtr]) ? [[NSString alloc] initWithUTF8String:self.megaApi->getNodePath([node getCPtr])] : nil;
}

- (MNode *)getNodeWithPath:(NSString *)path node:(MNode *)node {
    if (path == nil || node == nil) return nil;
    
    MegaNode *n = self.megaApi->getNodeByPath([path UTF8String], [node getCPtr]);
    
    return n ? [[MNode alloc] initWithMegaNode:n cMemoryOwn:YES] : Nil;
}

- (MNode *)getNodeWithPath:(NSString *)path {
    if (path == nil) return nil;
    
    MegaNode *node = self.megaApi->getNodeByPath([path UTF8String]);
    
    return node ? [[MNode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MNode *)getNodeWithHandle:(uint64_t)handle {
    if (handle == ::mega::INVALID_HANDLE) return nil;
    
    MegaNode *node = self.megaApi->getNodeByHandle(handle);
    
    return node ? [[MNode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MUserList *)getContacts {
    return [[MUserList alloc] initWithUserList:self.megaApi->getContacts() cMemoryOwn:YES];
}

- (MUser *)getContactWithEmail:(NSString *)email {
    if (email == nil) return nil;
    
    MegaUser *user = self.megaApi->getContact([email UTF8String]);
    return user ? [[MUser alloc] initWithMegaUser:user cMemoryOwn:YES] : nil;
}

- (MNodeList *)getInSharesWithUser:(MUser *)user {
    return [[MNodeList alloc] initWithNodeList:self.megaApi->getInShares((user != nil) ? [user getCPtr] : NULL) cMemoryOwn:YES];
}

- (MNodeList *)getInShares {
    return [[MNodeList alloc] initWithNodeList:self.megaApi->getInShares() cMemoryOwn:YES];
}

- (MShareList *)getOutSharesWithNode:(MNode *)node {
    return [[MShareList alloc] initWithShareList:self.megaApi->getOutShares((node != nil) ? [node getCPtr] : NULL) cMemoryOwn:YES];
}

- (NSString *)getFileFingerprintWithFilePath:(NSString *)filePath {
    if (filePath == nil) return nil;
    
    return self.megaApi->getFingerprint([filePath UTF8String]) ? [[NSString alloc] initWithUTF8String:self.megaApi->getFingerprint([filePath UTF8String])] : nil;
}

- (NSString *)getNodeFinferprintWithNode:(MNode *)node {
    if (node == nil) return nil;
    
    return self.megaApi->getFingerprint([node getCPtr]) ? [[NSString alloc] initWithUTF8String:self.megaApi->getFingerprint([node getCPtr])] : nil;
}

- (MNode *)getNodeWithFingerprint:(NSString *)fingerprint {
    if (fingerprint == nil) return nil;
    
    MegaNode *node = self.megaApi->getNodeByFingerprint([fingerprint UTF8String]);
    
    return node ? [[MNode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (BOOL)hasFingerprint:(NSString *)fingerprint{
    if (fingerprint == nil) return NO;
    
    return self.megaApi->hasFingerprint([fingerprint UTF8String]);
}

- (NSInteger)getAccessWithNode:(MNode *)node {
    if (node == nil) return -1;
    
    return self.megaApi->getAccess([node getCPtr]);
}

- (MError *)checkAccessWithNode:(MNode *)node level:(NSInteger)level {
    if (node == nil) return nil;
    
    return [[MError alloc] initWithMegaError:self.megaApi->checkAccess((node != nil) ? [node getCPtr] : NULL, (int) level).copy() cMemoryOwn:YES];
}

- (MError *)checkMoveWithMnode:(MNode *)node target:(MNode *)target {
    return [[MError alloc] initWithMegaError:self.megaApi->checkMove((node != nil) ? [node getCPtr] : NULL, (target != nil) ? [target getCPtr] : NULL).copy() cMemoryOwn:YES];
}

- (MNode *)getRootNode {
    MegaNode *node = self.megaApi->getRootNode();
    return node ? [[MNode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MNode *)getRubbishNode {
    MegaNode *node = self.megaApi->getRubbishNode();
    return node ? [[MNode alloc] initWithMegaNode:node cMemoryOwn:YES] : nil;
}

- (MegaRequestListener *)createDelegateMRequestListener:(id<MRequestDelegate>)delegate singleListener:(BOOL)singleListener {
    if (delegate == nil) return nil;
    
    DelegateMRequestListener *delegateListener = new DelegateMRequestListener(self, (__bridge void*)delegate, singleListener);
    //Enter critical section
    self.activeRequestListeners.insert(delegateListener);
    //leave critical section
    return delegateListener;
}

- (MegaTransferListener *)createDelegateMTransferListener:(id<MTransferDelegate>)delegate singleListener:(BOOL)singleListener {
    if (delegate == nil) return nil;
    
    DelegateMTransferListener *delegateListener = new DelegateMTransferListener(self, (__bridge void*)delegate, singleListener);
    //Enter critical section
    self.activeTransferListeners.insert(delegateListener);
    //leave critical section
    return delegateListener;
}

- (MegaGlobalListener *)createDelegateMGlobalListener:(id<MGlobalListenerDelegate>)delegate {
    if (delegate == nil) return nil;
    
    DelegateMGlobalListener *delegateListener = [[DelegateMGlobalListener alloc] initDelegateMGlobalListenerWithMegaSDK:self delegate:delegate];
    //Enter critical section
    self.activeGlobalListeners.insert(delegateListener);
    //leave critical section
    return (__bridge MegaGlobalListener *)delegateListener;
}

- (MegaListener *)createDelegateMListener:(id<MListenerDelegate>)delegate {
    if (delegate == nil) return nil;
    
    DelegateMListener *delegateListener = new DelegateMListener(self, (__bridge void*)delegate);
    //Enter critical section
    self.activeMegaListeners.insert(delegateListener);
    //leave critical section
    return delegateListener;
}

- (void)freeRequestListener:(DelegateMRequestListener *)delegate {
    if (delegate == nil) return;
    
//    EnterCriticalSection(&listenerMutex);
    self.activeRequestListeners.erase(delegate);
//    LeaveCriticalSection(&listenerMutex);
//    delete delegate;
}

- (void)freeTransferListener:(DelegateMTransferListener *)delegate {
    if (delegate == nil) return;
    
    //    EnterCriticalSection(&listenerMutex);
    self.activeTransferListeners.erase(delegate);
    //    LeaveCriticalSection(&listenerMutex);
    //    delete delegate;

}

@end
