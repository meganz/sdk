//
//  MegaSDK.h
//
//  Created by Javier Navarro on 06/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>

#import "MNode.h"
#import "MUser.h"
#import "MTransfer.h"
#import "MRequest.h"
#import "MError.h"
#import "MTransferList.h"
#import "MNodeList.h"
#import "MUserList.h"
#import "MShareList.h"
#import "MRequestDelegate.h"
#import "MListenerDelegate.h"
#import "MTransferDelegate.h"
#import "MGlobalListenerDelegate.h"

typedef NS_ENUM (NSInteger, MSortOrderType) {
    MSortOrderTypeNone,
    MSortOrderTypeDefaultAsc,
    MSortOrderTypeDefaultDesc,
    MSortOrderTypeSizeAsc,
    MSortOrderTypeSizeDesc,
    MSortOrderTypeCreationAsc,
    MSortOrderTypeCreationDesc,
    MSortOrderTypeModificationAsc,
    MSortOrderTypeModificationDesc,
    MSortOrderTypeAlphabeticalAsc,
    MSortOrderTypeAlphabeticalDesc
};

@interface MegaSDK : NSObject

- (instancetype)initWithAppKey:(NSString *)appKey userAgent:(NSString *)userAgent;
- (instancetype)initWithAppKey:(NSString *)appKey userAgent:(NSString *)userAgent basePath:(NSString *)basePath;

- (void)addDelegate:(id<MListenerDelegate>)delegate;
- (void)addRequestDelegate:(id<MRequestDelegate>)delegate;
- (void)addMTransferDelegate:(id<MTransferDelegate>)delegate;
- (void)addGlobalDelegate:(id<MGlobalListenerDelegate>)delegate;
- (void)removeDelegate:(id<MListenerDelegate>)delegate;
- (void)removeRequestDelegate:(id<MRequestDelegate>)delegate;
- (void)removeTransferDelegate:(id<MTransferDelegate>)delegate;
- (void)removeGlobalDelegate:(id<MGlobalListenerDelegate>)delegate;

- (NSString *)getBase64pwkeyWithPassword:(NSString *)password;
- (NSString *)getStringHashWithBase64pwkey:(NSString *)base64pwkey inBuf:(NSString *)inBuf;
+ (uint64_t)base64ToHandle:(NSString *)base64Handle;
+ (NSString *)ebcEncryptKeyWithEncryptionKey:(NSString *)encryptionKey plainKey:(NSString *)plainKey;
- (void)retryPendingConnections;
//- (void)retryPendingConnectionsWithDelegate:(id<MRequestDelegate>)delegateObject;
- (void)loginWithEmail:(NSString *)email password:(NSString *)password delegate:(id<MRequestDelegate>)delegateObject;
- (void)loginWithEmail:(NSString *)email password:(NSString *)password;
- (NSString *)dumpSession;
- (void)fastLoginWithEmail:(NSString *)email stringHash:(NSString *)stringHash base64pwKey:(NSString *)base64pwKey delegate:(id<MRequestDelegate>)delegateObject;
- (void)fastLoginWithEmail:(NSString *)email stringHash:(NSString *)stringHash base64pwKey:(NSString *)base64pwKey;
- (void)fastLoginWithSession:(NSString *)session delegate:(id<MRequestDelegate>)delegateObject;
- (void)fastLoginWithSession:(NSString *)session;
- (void)createAccountWithEmail:(NSString *)email password:(NSString *)password name:(NSString *)name delegate:(id<MRequestDelegate>)delegateObject;
- (void)createAccountWithEmail:(NSString *)email password:(NSString *)password name:(NSString *)name;
- (void)fastCreateAccountWithEmail:(NSString *)email base64pwkey:(NSString *)base64pwkey name:(NSString *)name delegate:(id<MRequestDelegate>)delegateObject;
- (void)fastCreateAccountWithEmail:(NSString *)email base64pwkey:(NSString *)base64pwkey name:(NSString *)name;
//- (void)querySignupWithLink:(NSString *)link delegate:(id<MRequestDelegate>)delegateObject;
- (void)querySignupWithLink:(NSString *)link;
//- (void)confirmAccountWithLink:(NSString *)link password:(NSString *)password delegate:(id<MRequestDelegate>)delegateObject;
- (void)confirmAccountWithLink:(NSString *)link password:(NSString *)password;
//- (void)fastConfirmAccountWithLink:(NSString *)link base64pwkey:(NSString *)base64pwkey delegate:(id<MRequestDelegate>)delegateObject;
- (void)fastConfirmAccountWithLink:(NSString *)link base64pwkey:(NSString *)base64pwkey;
- (NSInteger)isLoggedIn;
- (NSString *)getMyEmail;
//- (void)createFolderWithName:(NSString *)name parent:(MNode *)parent delegate:(id<MRequestDelegate>)delegateObject;
- (void)createFolderWithName:(NSString *)name parent:(MNode *)parent;
//- (void)moveNode:(MNode *)node newParent:(MNode *)newParent delegate:(id<MRequestDelegate>)delegateObject;
- (void)moveNode:(MNode *)node newParent:(MNode *)newParent;
//- (void)copyNode:(MNode *)node newParent:(MNode *)newParent delegate:(id<MRequestDelegate>)delegateObject;
- (void)copyNode:(MNode *)node newParent:(MNode *)newParent;
//- (void)renameNode:(MNode *)node newName:(NSString *)newName delegate:(id<MRequestDelegate>)delegateObject;
- (void)renameNode:(MNode *)node newName:(NSString *)newName;
//- (void)removeNode:(MNode *)node delegate:(id<MRequestDelegate>)delegateObject;
- (void)removeNode:(MNode *)node;
//- (void)shareNode:(MNode *)node withUser:(MUser *)user level:(NSInteger)level delegate:(id<MRequestDelegate>)delegateObject;
- (void)shareNode:(MNode *)node withUser:(MUser *)user level:(NSInteger)level;
//- (void)shareNode:(MNode *)node withEmail:(NSString *)email level:(NSInteger)level delegate:(id<MRequestDelegate>)delegateObject;
- (void)shareNode:(MNode *)node withEmail:(NSString *)email level:(NSInteger)level;
//- (void)folderAccessWithMegaFileLink:(NSString *)megaFolderLink delegate:(id<MRequestDelegate>)delegateObject;
- (void)folderAccessWithMegaFileLink:(NSString *)megaFolderLink;
//- (void)importMegaFileLink:(NSString *)megaFileLink parent:(MNode *)parent delegate:(id<MRequestDelegate>)delegateObject;
- (void)importMegaFileLink:(NSString *)megaFileLink parent:(MNode *)parent;
//- (void)importPublicNode:(MNode *)publicNode parent:(MNode *)parent delegate:(id<MRequestDelegate>)delegateObject;
- (void)importPublicNode:(MNode *)publicNode parent:(MNode *)parent;
//- (void)getPublicNodeWithMegaFileLink:(NSString *)megaFileLink delegate:(id<MRequestDelegate>)delegateObject;
- (void)getPublicNodeWithMegaFileLink:(NSString *)megaFileLink;
- (void)getThumbnailWithNode:(MNode *)node destinationFilePath:(NSString *)destinationFilePath delegate:(id<MRequestDelegate>)delegateObject;
- (void)getThumbnailWithNode:(MNode *)node destinationFilePath:(NSString *)destinationFilePath;
//- (void)setThumbnailWithNode:(MNode *)node sourceFilePath:(NSString *)sourceFilePath delegate:(id<MRequestDelegate>)delegateObject;
- (void)setThumbnailWithNode:(MNode *)node sourceFilePath:(NSString *)sourceFilePath;
- (void)getPreviewWithNode:(MNode *)node destinationFilePath:(NSString *)destinationFilePath delegate:(id<MRequestDelegate>)delegateObject;
- (void)getPreviewWithNode:(MNode *)node destinationFilePath:(NSString *)destinationFilePath;
//- (void)setPreviewWithNode:(MNode *)node sourceFilePath:(NSString *)sourceFilePath delegate:(id<MRequestDelegate>)delegateObject;
- (void)setPreviewWithNode:(MNode *)node sourceFilePath:(NSString *)sourceFilePath;
//- (void)getAvatarWithUser:(MUser *)user destinationFilePath:(NSString *)destinationFilePath delegate:(id<MRequestDelegate>)delegateObject;
- (void)getAvatarWithUser:(MUser *)user destinationFilePath:(NSString *)destinationFilePath;
//- (void)exportNode:(MNode *)node delegate:(id<MRequestDelegate>)delegateObject;
- (void)exportNode:(MNode *)node;
//- (void)disableExportNode:(MNode *)node delegate:(id<MRequestDelegate>)delegateObject;
- (void)disableExportNode:(MNode *)node;
- (void)fetchNodesWithListener:(id<MRequestDelegate>)delegateObject;
- (void)fetchNodes;
//- (void)getAccountDetailsWithDelegate:(id<MRequestDelegate>)delegateObject;
- (void)getAccountDetails;
//- (void)changePasswordWithOldPassword:(NSString *)oldPassword newPassword:(NSString *)newPassword delegate:(id<MRequestDelegate>)delegateObject;
- (void)changePasswordWithOldPassword:(NSString *)oldPassword newPassword:(NSString *)newPassword;
//- (void)addContactWithEmail:(NSString *)email delegate:(id<MRequestDelegate>)delegateObject;
- (void)addContactWithEmail:(NSString *)email;
//- (void)removeContactWithEmail:(NSString *)email delegate:(id<MRequestDelegate>)delegateObject;
- (void)removeContactWithEmail:(NSString *)email;
- (void)logoutWithDelegate:(id<MRequestDelegate>)delegateObject;
- (void)logout;
//- (void)startUploadWithLocalPath:(NSString *)localPath parent:(MNode *)parent delegate:(id<MTransferDelegate>)delegateObject;
- (void)startUploadWithLocalPath:(NSString *)localPath parent:(MNode *)parent;
//- (void)startUploadToFileWithLocalPath:(NSString *)localPath parent:(MNode *)parent filename:(NSString *)filename delegate:(id<MTransferDelegate>)delegateObject;
- (void)startUploadToFileWithLocalPath:(NSString *)localPath parent:(MNode *)parent filename:(NSString *)filename;
//- (void)startDownloadWithNode:(MNode *)node localPath:(NSString *)localPath delegate:(id<MTransferDelegate>)delegateObject;
- (void)startDownloadWithNode:(MNode *)node localPath:(NSString *)localPath;
//- (void)startPublicDownloadWithNode:(MNode *)node localPath:(NSString *)localPath delegate:(id<MRequestDelegate>)delegateObject;
- (void)startPublicDownloadWithNode:(MNode *)node localPath:(NSString *)localPath;
//- (void)cancelTransferWithTransfer:(MTransfer *)transfer delegate:(id<MRequestDelegate>)delegateObject;
- (void)cancelTransferWithTransfer:(MTransfer *)transfer;
//- (void)cancelTransfersWithDirection:(NSInteger)direction delegate:(id<MRequestDelegate>)delegateObject;
- (void)cancelTransfersWithDirection:(NSInteger)direction;
//- (void)pauseTransersWithPause:(BOOL)pause delegate:(id<MRequestDelegate>)delegateObject;
- (void)pauseTransersWithPause:(BOOL)pause;
- (void)setUploadLimitWithBpsLimit:(NSInteger)bpsLimit;
- (MTransferList *)getTransfers;
- (NSInteger)getNumPendingUploads;
- (NSInteger)getNumPendingDownloads;
- (NSInteger)getTotalUploads;
- (NSInteger)getTotalDownloads;
- (NSNumber *)getTotalsDownloadedBytes;
- (NSNumber *)getTotalsUploadedBytes;
- (void)resetTotalDownloads;
- (void)resetTotalUploads;
- (NSInteger)getNumChildrenWithParent:(MNode *)parent;
- (NSInteger)getNumChildFilesWithParent:(MNode *)parent;
- (NSInteger)getNumChildFoldersWithParent:(MNode *)parent;
- (MNodeList *)getChildrenWithParent:(MNode *)parent order:(NSInteger)order;
- (MNodeList *)getChildrenWithParent:(MNode *)parent;
- (MNode *)getChildNodeWithParent:(MNode *)parent name:(NSString *)name;
- (MNode *)getParentNodeWithNode:(MNode *)node;
- (NSString *)getNodePathWithNode:(MNode *)node;
- (MNode *)getNodeWithPath:(NSString *)path node:(MNode *)node;
- (MNode *)getNodeWithPath:(NSString *)path;
- (MNode *)getNodeWithHandle:(uint64_t)handle;
- (MUserList *)getContacts;
- (MUser *)getContactWithEmail:(NSString *)email;
- (MNodeList *)getInSharesWithUser:(MUser *)user;
- (MNodeList *)getInShares;
- (MShareList *)getOutSharesWithNode:(MNode *)node;
- (NSString *)getFileFingerprintWithFilePath:(NSString *)filePath;
- (NSString *)getNodeFinferprintWithNode:(MNode *)node;
- (MNode *)getNodeWithFingerprint:(NSString *)fingerprint;
- (BOOL)hasFingerprint:(NSString *)fingerprint;
- (NSInteger)getAccessWithNode:(MNode *)node;
- (MError *)checkAccessWithNode:(MNode *)node level:(NSInteger)level;
- (MError *)checkMoveWithMnode:(MNode *)node target:(MNode *)target;
- (MNode *)getRootNode;
- (MNode *)getRubbishNode;

@end
