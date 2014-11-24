//
//  MEGASdk.h
//
//  Created by Javier Navarro on 06/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>

#import "MEGANode.h"
#import "MEGAUser.h"
#import "MEGATransfer.h"
#import "MEGARequest.h"
#import "MEGAError.h"
#import "MEGATransferList.h"
#import "MEGANodeList.h"
#import "MEGAUserList.h"
#import "MEGAShareList.h"
#import "MEGARequestDelegate.h"
#import "MEGADelegate.h"
#import "MEGATransferDelegate.h"
#import "MEGAGlobalDelegate.h"

typedef NS_ENUM (NSInteger, MEGASortOrderType) {
    MEGASortOrderTypeNone,
    MEGASortOrderTypeDefaultAsc,
    MEGASortOrderTypeDefaultDesc,
    MEGASortOrderTypeSizeAsc,
    MEGASortOrderTypeSizeDesc,
    MEGASortOrderTypeCreationAsc,
    MEGASortOrderTypeCreationDesc,
    MEGASortOrderTypeModificationAsc,
    MEGASortOrderTypeModificationDesc,
    MEGASortOrderTypeAlphabeticalAsc,
    MEGASortOrderTypeAlphabeticalDesc
};

@interface MEGASdk : NSObject 

@property (readonly) NSString *myEmail;
@property (readonly) MEGANode *rootNode;
@property (readonly) MEGANode *rubbishNode;
@property (readonly) MEGATransferList *transfers;
@property (readonly) NSInteger pendingUploads;
@property (readonly) NSInteger pendingDownloads;
@property (readonly) NSInteger totalUploads;
@property (readonly) NSInteger totalDownloads;
@property (readonly) NSNumber *totalsDownloadedBytes;
@property (readonly) NSNumber *totalsUploadedBytes;
@property (readonly) NSString *masterKey;

- (instancetype)initWithAppKey:(NSString *)appKey userAgent:(NSString *)userAgent;
- (instancetype)initWithAppKey:(NSString *)appKey userAgent:(NSString *)userAgent basePath:(NSString *)basePath;

- (void)addMEGADelegate:(id<MEGADelegate>)delegate;
- (void)addMEGARequestDelegate:(id<MEGARequestDelegate>)delegate;
- (void)addMEGATransferDelegate:(id<MEGATransferDelegate>)delegate;
- (void)addMEGAGlobalDelegate:(id<MEGAGlobalDelegate>)delegate;
- (void)removeMEGADelegate:(id<MEGADelegate>)delegate;
- (void)removeMEGARequestDelegate:(id<MEGARequestDelegate>)delegate;
- (void)removeMEGATransferDelegate:(id<MEGATransferDelegate>)delegate;
- (void)removeMEGAGlobalDelegate:(id<MEGAGlobalDelegate>)delegate;

- (NSString *)base64pwkeyWithPassword:(NSString *)password;
- (NSString *)stringHashWithBase64pwkey:(NSString *)base64pwkey inBuf:(NSString *)inBuf;
+ (uint64_t)base64ToHandle:(NSString *)base64Handle;
+ (NSString *)ebcEncryptKeyWithEncryptionKey:(NSString *)encryptionKey plainKey:(NSString *)plainKey;
- (void)retryPendingConnections;
- (void)loginWithEmail:(NSString *)email password:(NSString *)password delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)loginWithEmail:(NSString *)email password:(NSString *)password;
- (NSString *)dumpSession;
- (void)fastLoginWithEmail:(NSString *)email stringHash:(NSString *)stringHash base64pwKey:(NSString *)base64pwKey delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)fastLoginWithEmail:(NSString *)email stringHash:(NSString *)stringHash base64pwKey:(NSString *)base64pwKey;
- (void)fastLoginWithSession:(NSString *)session delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)fastLoginWithSession:(NSString *)session;
- (void)createAccountWithEmail:(NSString *)email password:(NSString *)password name:(NSString *)name delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)createAccountWithEmail:(NSString *)email password:(NSString *)password name:(NSString *)name;
- (void)fastCreateAccountWithEmail:(NSString *)email base64pwkey:(NSString *)base64pwkey name:(NSString *)name delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)fastCreateAccountWithEmail:(NSString *)email base64pwkey:(NSString *)base64pwkey name:(NSString *)name;
- (void)querySignupWithLink:(NSString *)link delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)querySignupWithLink:(NSString *)link;
- (void)confirmAccountWithLink:(NSString *)link password:(NSString *)password delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)confirmAccountWithLink:(NSString *)link password:(NSString *)password;
- (void)fastConfirmAccountWithLink:(NSString *)link base64pwkey:(NSString *)base64pwkey delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)fastConfirmAccountWithLink:(NSString *)link base64pwkey:(NSString *)base64pwkey;
- (NSInteger)isLoggedIn;
- (void)createFolderWithName:(NSString *)name parent:(MEGANode *)parent delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)createFolderWithName:(NSString *)name parent:(MEGANode *)parent;
- (void)moveNode:(MEGANode *)node newParent:(MEGANode *)newParent delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)moveNode:(MEGANode *)node newParent:(MEGANode *)newParent;
- (void)copyNode:(MEGANode *)node newParent:(MEGANode *)newParent delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)copyNode:(MEGANode *)node newParent:(MEGANode *)newParent;
- (void)renameNode:(MEGANode *)node newName:(NSString *)newName delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)renameNode:(MEGANode *)node newName:(NSString *)newName;
- (void)removeNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)removeNode:(MEGANode *)node;
- (void)shareNode:(MEGANode *)node withUser:(MEGAUser *)user level:(NSInteger)level delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)shareNode:(MEGANode *)node withUser:(MEGAUser *)user level:(NSInteger)level;
- (void)shareNode:(MEGANode *)node withEmail:(NSString *)email level:(NSInteger)level delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)shareNode:(MEGANode *)node withEmail:(NSString *)email level:(NSInteger)level;
- (void)folderAccessWithMegaFolderLink:(NSString *)megaFolderLink delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)folderAccessWithMegaFolderLink:(NSString *)megaFolderLink;
- (void)importMegaFileLink:(NSString *)megaFileLink parent:(MEGANode *)parent delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)importMegaFileLink:(NSString *)megaFileLink parent:(MEGANode *)parent;
- (void)importPublicNode:(MEGANode *)publicNode parent:(MEGANode *)parent delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)importPublicNode:(MEGANode *)publicNode parent:(MEGANode *)parent;
- (void)publicNodeWithMegaFileLink:(NSString *)megaFileLink delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)publicNodeWithMegaFileLink:(NSString *)megaFileLink;
- (void)getThumbnailWithNode:(MEGANode *)node destinationFilePath:(NSString *)destinationFilePath delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)getThumbnailWithNode:(MEGANode *)node destinationFilePath:(NSString *)destinationFilePath;
- (void)setThumbnailWithNode:(MEGANode *)node sourceFilePath:(NSString *)sourceFilePath delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)setThumbnailWithNode:(MEGANode *)node sourceFilePath:(NSString *)sourceFilePath;
- (void)getPreviewWithNode:(MEGANode *)node destinationFilePath:(NSString *)destinationFilePath delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)getPreviewWithNode:(MEGANode *)node destinationFilePath:(NSString *)destinationFilePath;
- (void)setPreviewWithNode:(MEGANode *)node sourceFilePath:(NSString *)sourceFilePath delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)setPreviewWithNode:(MEGANode *)node sourceFilePath:(NSString *)sourceFilePath;
- (void)getAvatarWithUser:(MEGAUser *)user destinationFilePath:(NSString *)destinationFilePath delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)getAvatarWithUser:(MEGAUser *)user destinationFilePath:(NSString *)destinationFilePath;
- (void)exportNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)exportNode:(MEGANode *)node;
- (void)disableExportNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)disableExportNode:(MEGANode *)node;
- (void)fetchNodesWithListener:(id<MEGARequestDelegate>)delegateObject;
- (void)fetchNodes;
- (void)getAccountDetailsWithDelegate:(id<MEGARequestDelegate>)delegateObject;
- (void)getAccountDetails;
- (void)pricingWithDelegate:(id<MEGARequestDelegate>)delegateObject;
- (void)pricing;
- (void)getPaymentURLWithProductHandle:(uint64_t)productHandle delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)getPaymentULRWithProductHandle:(uint64_t)productHandle;
- (void)changePasswordWithOldPassword:(NSString *)oldPassword newPassword:(NSString *)newPassword delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)changePasswordWithOldPassword:(NSString *)oldPassword newPassword:(NSString *)newPassword;
- (void)addContactWithEmail:(NSString *)email delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)addContactWithEmail:(NSString *)email;
- (void)removeContactWithEmail:(NSString *)email delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)removeContactWithEmail:(NSString *)email;
- (void)logoutWithDelegate:(id<MEGARequestDelegate>)delegateObject;
- (void)logout;
- (void)startUploadWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent delegate:(id<MEGATransferDelegate>)delegateObject;
- (void)startUploadWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent;
- (void)startUploadToFileWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent filename:(NSString *)filename delegate:(id<MEGATransferDelegate>)delegateObject;
- (void)startUploadToFileWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent filename:(NSString *)filename;
- (void)startDownloadWithNode:(MEGANode *)node localPath:(NSString *)localPath delegate:(id<MEGATransferDelegate>)delegateObject;
- (void)startDownloadWithNode:(MEGANode *)node localPath:(NSString *)localPath;
- (void)startPublicDownloadWithNode:(MEGANode *)node localPath:(NSString *)localPath delegate:(id<MEGATransferDelegate>)delegateObject;
- (void)startPublicDownloadWithNode:(MEGANode *)node localPath:(NSString *)localPath;
- (void)cancelTransferWithTransfer:(MEGATransfer *)transfer delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)cancelTransferWithTransfer:(MEGATransfer *)transfer;
- (void)cancelTransfersWithDirection:(NSInteger)direction delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)cancelTransfersWithDirection:(NSInteger)direction;
- (void)pauseTransersWithPause:(BOOL)pause delegate:(id<MEGARequestDelegate>)delegateObject;
- (void)pauseTransersWithPause:(BOOL)pause;
- (void)setUploadLimitWithBpsLimit:(NSInteger)bpsLimit;- (void)resetTotalDownloads;
- (void)resetTotalUploads;
- (NSInteger)numberChildrenWithParent:(MEGANode *)parent;
- (NSInteger)numberChildFilesWithParent:(MEGANode *)parent;
- (NSInteger)numberChildFoldersWithParent:(MEGANode *)parent;
- (MEGANodeList *)childrenWithParent:(MEGANode *)parent order:(NSInteger)order;
- (MEGANodeList *)childrenWithParent:(MEGANode *)parent;
- (MEGANode *)childNodeWithParent:(MEGANode *)parent name:(NSString *)name;
- (MEGANode *)parentNodeWithNode:(MEGANode *)node;
- (NSString *)nodePathWithNode:(MEGANode *)node;
- (MEGANode *)nodeWithPath:(NSString *)path node:(MEGANode *)node;
- (MEGANode *)nodeWithPath:(NSString *)path;
- (MEGANode *)nodeWithHandle:(uint64_t)handle;
- (MEGAUserList *)contacts;
- (MEGAUser *)contactWithEmail:(NSString *)email;
- (MEGANodeList *)inSharesWithUser:(MEGAUser *)user;
- (MEGANodeList *)inShares;
- (MEGAShareList *)outSharesWithNode:(MEGANode *)node;
- (NSString *)fingerprintWithFilePath:(NSString *)filePath;
- (NSString *)finferprintWithNode:(MEGANode *)node;
- (MEGANode *)nodeWithFingerprint:(NSString *)fingerprint;
- (BOOL)hasFingerprint:(NSString *)fingerprint;
- (NSInteger)accessLevelWithNode:(MEGANode *)node;
- (MEGAError *)checkAccessWithNode:(MEGANode *)node level:(NSInteger)level;
- (MEGAError *)checkMoveWithMnode:(MEGANode *)node target:(MEGANode *)target;

@end
