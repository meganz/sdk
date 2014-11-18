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

/**
 * @brief Allows to control a MEGA account or a shared folder
 *
 * You must provide an appKey to use this SDK. You can generate an appKey for your app for free here:
 * - https://mega.co.nz/#sdk
 *
 * You can enable local node caching by passing a local path in the constructor of this class. That saves many data usage
 * and many time starting your app because the entire filesystem won't have to be downloaded each time. The persistent
 * node cache will only be loaded by logging in with a session key. To take advantage of this feature, apart of passing the
 * local path to the constructor, your application have to save the session key after login ([MEGASdk dumpSession]) and use
 * it to log in the next time. This is highly recommended also to enhance the security, because in this was the access password
 * doesn't have to be stored by the application.
 *
 * To access MEGA using this SDK, you have to create an object of this class and use one of the [MEGASdk loginWithEmail:password:]
 * options (to log in to a MEGA account or a public folder). If the login request succeed, call [MEGASdk fetchnodes] to get the
 * filesystem in MEGA.
 * After that, you can use all other requests, manage the files and start transfers.
 *
 * After using [MEGASdk logout] you can reuse the same MEGASdk object to log in to another MEGA account or a public folder.
 *
 */
@interface MEGASdk : NSObject 

@property (readonly, nonatomic) NSString *myEmail;
@property (readonly, nonatomic) MEGANode *rootNode;
@property (readonly, nonatomic) MEGANode *rubbishNode;
@property (readonly, nonatomic) MEGATransferList *transfers;
@property (readonly, nonatomic) NSInteger pendingUploads;
@property (readonly, nonatomic) NSInteger pendingDownloads;
@property (readonly, nonatomic) NSInteger totalUploads;
@property (readonly, nonatomic) NSInteger totalDownloads;
@property (readonly, nonatomic) NSNumber *totalsDownloadedBytes;
@property (readonly, nonatomic) NSNumber *totalsUploadedBytes;
@property (readonly, nonatomic) NSString *masterKey;

/**
 * @brief Constructor suitable for most applications
 * @param appKey AppKey of your application
 * You can generate your AppKey for free here:
 * - https://mega.co.nz/#sdk
 *
 * @param userAgent User agent to use in network requests
 * If you pass nil to this parameter, a default user agent will be used
 *
 */
- (instancetype)initWithAppKey:(NSString *)appKey userAgent:(NSString *)userAgent;

/**
 * @brief Constructor suitable for most applications
 * @param appKey AppKey of your application
 * You can generate your AppKey for free here:
 * - https://mega.co.nz/#sdk
 *
 * @param userAgent User agent to use in network requests
 * If you pass nil to this parameter, a default user agent will be used
 *
 * @param basePath Base path to store the local cache
 * If you pass nil to this parameter, the SDK won't use any local cache.
 *
 */
- (instancetype)initWithAppKey:(NSString *)appKey userAgent:(NSString *)userAgent basePath:(NSString *)basePath;

/**
 * @brief Register a delegate to receive all events (requests, transfers, global)
 *
 * You can use [MEGASdk removeMEGADelegate] to stop receiving events.
 *
 * @param delegate Delegate that will receive all events (requests, transfers, global)
 */
- (void)addMEGADelegate:(id<MEGADelegate>)delegate;

/**
 * @brief Register a delegate to receive all events about requests
 *
 * You can use [MEGASdk removeMEGARequestDelegate] to stop receiving events.
 *
 * @param delegate Delegate that will receive all events about requests
 */
- (void)addMEGARequestDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Register a delegate to receive all events about transfers
 *
 * You can use [MEGASdk removeMEGATransferDelegate] to stop receiving events.
 *
 * @param delegate Delegate that will receive all events about transfers
 */
- (void)addMEGATransferDelegate:(id<MEGATransferDelegate>)delegate;

/**
 * @brief Register a delegate to receive global events
 *
 * You can use [MEGASdk removeMEGAGlobalDelegate] to stop receiving events.
 *
 * @param delegate Delegate that will receive global events
 */
- (void)addMEGAGlobalDelegate:(id<MEGAGlobalDelegate>)delegate;

/**
 * @brief Unregister a delegate
 *
 * This delegate won't receive more events.
 *
 * @param delegate Delegate that is unregistered
 */
- (void)removeMEGADelegate:(id<MEGADelegate>)delegate;

/**
 * @brief Unregister a MEGARequestDelegate
 *
 * This delegate won't receive more events.
 *
 * @param delegate Delegate that is unregistered
 */
- (void)removeMEGARequestDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Unregister a MEGATransferDelegate
 *
 * This delegate won't receive more events.
 *
 * @param delegate Delegate that is unregistered
 */
- (void)removeMEGATransferDelegate:(id<MEGATransferDelegate>)delegate;

/**
 * @brief Unregister a MEGAGlobalDelegate
 *
 * This delegate won't receive more events.
 *
 * @param delegate Delegate that is unregistered
 */
- (void)removeMEGAGlobalDelegate:(id<MEGAGlobalDelegate>)delegate;

/**
 * @brief Generates a private key based on the access password
 *
 * This is a time consuming operation (specially for low-end mobile devices). Since the resulting key is
 * required to log in, this function allows to do this step in a separate function. You should run this function
 * in a background thread, to prevent UI hangs. The resulting key can be used in [MEGASdk fastLogin:]
 *
 * You take the ownership of the returned value.
 *
 * @param password Access password
 * @return Base64-encoded private key
 */
- (NSString *)base64pwkeyWithPassword:(NSString *)password;

/**
 * @brief Generates a hash based in the provided private key and email
 *
 * This is a time consuming operation (specially for low-end mobile devices). Since the resulting key is
 * required to log in, this function allows to do this step in a separate function. You should run this function
 * in a background thread, to prevent UI hangs. The resulting key can be used in [MEGASdk fastLogin]
 *
 * You take the ownership of the returned value.
 *
 * @param base64pwkey Private key returned by [MEGASdk base64PwKeybase64pwkeyWithPassword:]
 * @return Base64-encoded hash
 */
- (NSString *)stringHashWithBase64pwkey:(NSString *)base64pwkey email:(NSString *)email;

/**
 * @brief Converts a Base64-encoded node handle to a MegaHandle
 *
 * The returned value can be used to recover a MEGANode using [MEGASdk nodeWithHandle:]
 * You can revert this operation using [MEGASdk handleToBase64]
 *
 * @param base64Handle Base64-encoded node handle
 * @return Node handle
 */
+ (uint64_t)base64ToHandle:(NSString *)base64Handle;

/**
 * @brief Retry all pending requests
 *
 * When requests fails they wait some time before being retried. That delay grows exponentially if the request
 * fails again. For this reason, and since this request is very lightweight, it's recommended to call it with
 * the default parameters on every user interaction with the application. This will prevent very big delays
 * completing requests.
 *
 * The associated request type with this request is MEGARequestTypeRetryPendingConnections.
 */

- (void)retryPendingConnections;

/**
 * @brief Log in to a MEGA account
 *
 * The associated request type with this request is MEGARequestTypeLogin.
 * Valid data in the MegaRequest object received on callbacks:
 * - [MEGARequest email] - Returns the first parameter
 * - [MEGARequest password] - Returns the second parameter
 *
 * If the email/password aren't valid the error code provided in onRequestFinish is
 * MEGAErrorTypeApiENoent.
 *
 * @param email Email of the user
 * @param password Password
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)loginWithEmail:(NSString *)email password:(NSString *)password delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Log in to a MEGA account
 *
 * The associated request type with this request is MEGARequestTypeLogin.
 * Valid data in the MegaRequest object received on callbacks:
 * - [MEGARequest email] - Returns the first parameter
 * - [MEGARequest password] - Returns the second parameter
 *
 * If the email/password aren't valid the error code provided in onRequestFinish is
 * MEGAErrorTypeApiENoent.
 *
 * @param email Email of the user
 * @param password Password
 */
- (void)loginWithEmail:(NSString *)email password:(NSString *)password;

/**
 * @brief Log in to a MEGA account using precomputed keys
 *
 * The associated request type with this request is MEGARequestTypeFastLogin.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the first parameter
 * - [MEGARequest password] - Returns the second parameter
 * - [MEGARequest privateKey] - Returns the third parameter
 *
 * If the email/stringHash/base64pwKey aren't valid the error code provided in onRequestFinish is
 * MEGAErrorTypeApiENoent.
 *
 * @param email Email of the user
 * @param stringHash Hash of the email returned by [MEGASdk stringHashWithBase64pwkey:email:]
 * @param base64pwkey Private key calculated using [MEGASdk base64PwKeyWithPassword:]
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)fastLoginWithEmail:(NSString *)email stringHash:(NSString *)stringHash base64pwKey:(NSString *)base64pwKey delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Log in to a MEGA account using precomputed keys
 *
 * The associated request type with this request is MEGARequestTypeFastLogin.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the first parameter
 * - [MEGARequest password] - Returns the second parameter
 * - [MEGARequest privateKey] - Returns the third parameter
 *
 * If the email/stringHash/base64pwKey aren't valid the error code provided in onRequestFinish is
 * MEGAErrorTypeApiENoent.
 *
 * @param email Email of the user
 * @param stringHash Hash of the email returned by [MEGASdk stringHashWithBase64pwkey:email:]
 * @param base64pwkey Private key calculated using [MEGASdk base64PwKeyWithPassword:]
 */
- (void)fastLoginWithEmail:(NSString *)email stringHash:(NSString *)stringHash base64pwKey:(NSString *)base64pwKey;

/**
 * @brief Log in to a MEGA account using a session key
 *
 * The associated request type with this request is MEGARequestTypeFastLogin.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest sessionKey] - Returns the session key
 *
 * @param session Session key previously dumped with [MEGASdk dumpSession]
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)fastLoginWithSession:(NSString *)session delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Log in to a MEGA account using a session key
 *
 * The associated request type with this request is MEGARequestTypeFastLogin.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest sessionKey] - Returns the session key
 *
 * @param session Session key previously dumped with [MEGASdk dumpSession]
 */
- (void)fastLoginWithSession:(NSString *)session;

/**
 * @brief Returns the current session key
 *
 * You have to be logged in to get a valid session key. Otherwise,
 * this function returns nil.
 *
 * You take the ownership of the returned value.
 *
 * @return Current session key
 */
- (NSString *)dumpSession;

/**
 * @brief Initialize the creation of a new MEGA account
 *
 * The associated request type with this request is MEGARequestTypeCreateAccount.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email for the account
 * - [MEGARequest password] - Returns the password for the account
 * - [MEGARequest name] - Returns the name of the user
 *
 * If this request succeed, a confirmation email will be sent to the users.
 * If an account with the same email already exists, you will get the error code
 * MEGAErrorTypeApiEExist in onRequestFinish
 *
 * @param email Email for the account
 * @param password Password for the account
 * @param name Name of the user
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)createAccountWithEmail:(NSString *)email password:(NSString *)password name:(NSString *)name delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Initialize the creation of a new MEGA account
 *
 * The associated request type with this request is MEGARequestTypeCreateAccount.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email for the account
 * - [MEGARequest password] - Returns the password for the account
 * - [MEGARequest name] - Returns the name of the user
 *
 * If this request succeed, a confirmation email will be sent to the users.
 * If an account with the same email already exists, you will get the error code
 * MEGAErrorTypeApiEExist in onRequestFinish
 *
 * @param email Email for the account
 * @param password Password for the account
 * @param name Name of the user
 */
- (void)createAccountWithEmail:(NSString *)email password:(NSString *)password name:(NSString *)name;

/**
 * @brief Initialize the creation of a new MEGA account with precomputed keys
 *
 * The associated request type with this request is MEGARequestTypeFastCreateAccount.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email for the account
 * - [MEGARequest: privateKey] - Returns the private key calculated with [MEGASdk base64pwkeyWithPassword:]
 * - [MEGARequest name] - Returns the name of the user
 *
 * If this request succeed, a confirmation email will be sent to the users.
 * If an account with the same email already exists, you will get the error code
 * MEGAErrorTypeApiEExist in onRequestFinish
 *
 * @param email Email for the account
 * @param base64pwkey Private key calculated with [MEGASdk base64pwkeyWithPassword]
 * @param name Name of the user
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)fastCreateAccountWithEmail:(NSString *)email base64pwkey:(NSString *)base64pwkey name:(NSString *)name delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Initialize the creation of a new MEGA account with precomputed keys
 *
 * The associated request type with this request is MEGARequestTypeFastCreateAccount.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email for the account
 * - [MEGARequest: privateKey] - Returns the private key calculated with [MEGASdk base64pwkeyWithPassword:]
 * - [MEGARequest name] - Returns the name of the user
 *
 * If this request succeed, a confirmation email will be sent to the users.
 * If an account with the same email already exists, you will get the error code
 * MEGAErrorTypeApiEExist in onRequestFinish
 *
 * @param email Email for the account
 * @param base64pwkey Private key calculated with [MEGASdk base64pwkeyWithPassword:]
 * @param name Name of the user
 */
- (void)fastCreateAccountWithEmail:(NSString *)email base64pwkey:(NSString *)base64pwkey name:(NSString *)name;

/**
 * @brief Get information about a confirmation link
 *
 * The associated request type with this request is MEGARequestTypeQuerySignUpLink.
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest link] - Returns the confirmation link
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk
 * - [MEGARequest email] - Return the email associated with the confirmation link
 * - [MEGARequest name] - Returns the name associated with the confirmation link
 *
 * @param link Confirmation link
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)querySignupWithLink:(NSString *)link delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get information about a confirmation link
 *
 * The associated request type with this request is MEGARequestTypeQuerySignUpLink.
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest link] - Returns the confirmation link
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk
 * - [MEGARequest email] - Return the email associated with the confirmation link
 * - [MEGARequest name] - Returns the name associated with the confirmation link
 *
 * @param link Confirmation link
 */
- (void)querySignupWithLink:(NSString *)link;

/**
 * @brief Confirm a MEGA account using a confirmation link and the user password
 *
 * The associated request type with this request is MEGARequestTypeConfirmAccount.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest link] - Returns the confirmation link
 * - [MEGARequest password] - Returns the password
 *
 * Valid data in the MegaRequest object received in onRequestFinish when the error code
 * is MegaError::API_OK:
 * - [MegaRequest::getEmail - Email of the account
 * - [MegaRequest::getName - Name of the user
 *
 * @param link Confirmation link
 * @param listener MegaRequestListener to track this request
 */
- (void)confirmAccountWithLink:(NSString *)link password:(NSString *)password delegate:(id<MEGARequestDelegate>)delegate;
- (void)confirmAccountWithLink:(NSString *)link password:(NSString *)password;
- (void)fastConfirmAccountWithLink:(NSString *)link base64pwkey:(NSString *)base64pwkey delegate:(id<MEGARequestDelegate>)delegate;
- (void)fastConfirmAccountWithLink:(NSString *)link base64pwkey:(NSString *)base64pwkey;
- (NSInteger)isLoggedIn;
- (void)createFolderWithName:(NSString *)name parent:(MEGANode *)parent delegate:(id<MEGARequestDelegate>)delegate;
- (void)createFolderWithName:(NSString *)name parent:(MEGANode *)parent;
- (void)moveNode:(MEGANode *)node newParent:(MEGANode *)newParent delegate:(id<MEGARequestDelegate>)delegate;
- (void)moveNode:(MEGANode *)node newParent:(MEGANode *)newParent;
- (void)copyNode:(MEGANode *)node newParent:(MEGANode *)newParent delegate:(id<MEGARequestDelegate>)delegate;
- (void)copyNode:(MEGANode *)node newParent:(MEGANode *)newParent;
- (void)renameNode:(MEGANode *)node newName:(NSString *)newName delegate:(id<MEGARequestDelegate>)delegate;
- (void)renameNode:(MEGANode *)node newName:(NSString *)newName;
- (void)removeNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate;
- (void)removeNode:(MEGANode *)node;
- (void)shareNode:(MEGANode *)node withUser:(MEGAUser *)user level:(NSInteger)level delegate:(id<MEGARequestDelegate>)delegate;
- (void)shareNode:(MEGANode *)node withUser:(MEGAUser *)user level:(NSInteger)level;
- (void)loginWithFolderLink:(NSString *)folderLink delegate:(id<MEGARequestDelegate>)delegate;
- (void)loginWithFolderLink:(NSString *)folderLink;
- (void)importMegaFileLink:(NSString *)megaFileLink parent:(MEGANode *)parent delegate:(id<MEGARequestDelegate>)delegate;
- (void)importMegaFileLink:(NSString *)megaFileLink parent:(MEGANode *)parent;
- (void)publicNodeWithMegaFileLink:(NSString *)megaFileLink delegate:(id<MEGARequestDelegate>)delegate;
- (void)publicNodeWithMegaFileLink:(NSString *)megaFileLink;
- (void)getThumbnailWithNode:(MEGANode *)node destinationFilePath:(NSString *)destinationFilePath delegate:(id<MEGARequestDelegate>)delegate;
- (void)getThumbnailWithNode:(MEGANode *)node destinationFilePath:(NSString *)destinationFilePath;
- (void)setThumbnailWithNode:(MEGANode *)node sourceFilePath:(NSString *)sourceFilePath delegate:(id<MEGARequestDelegate>)delegate;
- (void)setThumbnailWithNode:(MEGANode *)node sourceFilePath:(NSString *)sourceFilePath;
- (void)getPreviewWithNode:(MEGANode *)node destinationFilePath:(NSString *)destinationFilePath delegate:(id<MEGARequestDelegate>)delegate;
- (void)getPreviewWithNode:(MEGANode *)node destinationFilePath:(NSString *)destinationFilePath;
- (void)setPreviewWithNode:(MEGANode *)node sourceFilePath:(NSString *)sourceFilePath delegate:(id<MEGARequestDelegate>)delegate;
- (void)setPreviewWithNode:(MEGANode *)node sourceFilePath:(NSString *)sourceFilePath;
- (void)getAvatarWithUser:(MEGAUser *)user destinationFilePath:(NSString *)destinationFilePath delegate:(id<MEGARequestDelegate>)delegate;
- (void)getAvatarWithUser:(MEGAUser *)user destinationFilePath:(NSString *)destinationFilePath;
- (void)exportNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate;
- (void)exportNode:(MEGANode *)node;
- (void)disableExportNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate;
- (void)disableExportNode:(MEGANode *)node;
- (void)fetchNodesWithListener:(id<MEGARequestDelegate>)delegate;
- (void)fetchNodes;
- (void)getAccountDetailsWithDelegate:(id<MEGARequestDelegate>)delegate;
- (void)getAccountDetails;
- (void)pricingWithDelegate:(id<MEGARequestDelegate>)delegate;
- (void)pricing;
- (void)getPaymentURLWithProductHandle:(uint64_t)productHandle delegate:(id<MEGARequestDelegate>)delegate;
- (void)getPaymentULRWithProductHandle:(uint64_t)productHandle;
- (void)changePasswordWithOldPassword:(NSString *)oldPassword newPassword:(NSString *)newPassword delegate:(id<MEGARequestDelegate>)delegate;
- (void)changePasswordWithOldPassword:(NSString *)oldPassword newPassword:(NSString *)newPassword;
- (void)addContactWithEmail:(NSString *)email delegate:(id<MEGARequestDelegate>)delegate;
- (void)addContactWithEmail:(NSString *)email;
- (void)removeContactWithUser:(MEGAUser *)user delegate:(id<MEGARequestDelegate>)delegate;
- (void)removeContactWithUser:(MEGAUser *)user;
- (void)logoutWithDelegate:(id<MEGARequestDelegate>)delegate;
- (void)logout;
- (void)startUploadWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent delegate:(id<MEGATransferDelegate>)delegate;
- (void)startUploadWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent;
- (void)startUploadToFileWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent filename:(NSString *)filename delegate:(id<MEGATransferDelegate>)delegate;
- (void)startUploadToFileWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent filename:(NSString *)filename;
- (void)startDownloadWithNode:(MEGANode *)node localPath:(NSString *)localPath delegate:(id<MEGATransferDelegate>)delegate;
- (void)startDownloadWithNode:(MEGANode *)node localPath:(NSString *)localPath;- (void)cancelTransferWithTransfer:(MEGATransfer *)transfer delegate:(id<MEGARequestDelegate>)delegate;
- (void)cancelTransferWithTransfer:(MEGATransfer *)transfer;
- (void)cancelTransfersWithDirection:(NSInteger)direction delegate:(id<MEGARequestDelegate>)delegate;
- (void)cancelTransfersWithDirection:(NSInteger)direction;
- (void)pauseTransersWithPause:(BOOL)pause delegate:(id<MEGARequestDelegate>)delegate;
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
