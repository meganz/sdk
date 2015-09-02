/**
 * @file MEGASdk.h
 * @brief Allows to control a MEGA account or a public folder
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

#import <Foundation/Foundation.h>

#import "MEGANode.h"
#import "MEGAUser.h"
#import "MEGATransfer.h"
#import "MEGARequest.h"
#import "MEGAError.h"
#import "MEGAPricing.h"
#import "MEGAAccountDetails.h"
#import "MEGATransferList.h"
#import "MEGANodeList.h"
#import "MEGAUserList.h"
#import "MEGAShareList.h"
#import "MEGARequestDelegate.h"
#import "MEGADelegate.h"
#import "MEGATransferDelegate.h"
#import "MEGAGlobalDelegate.h"
#import "MEGALoggerDelegate.h"

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

typedef NS_ENUM (NSInteger, MEGAEventType) {
    MEGAEventTypeFeedback = 0,
    MEGAEventTypeDebug,
    MEGAEventTypeInvalid
};

typedef NS_ENUM (NSInteger, MEGALogLevel) {
    MEGALogLevelFatal = 0,
    MEGALogLevelError,      // Error information but will continue application to keep running.
    MEGALogLevelWarning,    // Information representing errors in application but application will keep running
    MEGALogLevelInfo,       // Mainly useful to represent current progress of application.
    MEGALogLevelDebug,      // Informational logs, that are useful for developers. Only applicable if DEBUG is defined.
    MEGALogLevelMax
};

typedef NS_ENUM (NSInteger, MEGAAttributeType) {
    MEGAAttributeTypeThumbnail = 0,
    MEGAAttributeTypePreview
};

typedef NS_ENUM(NSInteger, MEGAUserAttribute) {
    MEGAUserAttributeFirstname = 1,
    MEGAUserAttributeLastname  = 2
};

typedef NS_ENUM(NSInteger, MEGAPaymentMethod) {
    MEGAPaymentMethodBalance      = 0,
    MEGAPaymentMethodPaypal       = 1,
    MEGAPaymentMethodItunes       = 2,
    MEGAPaymentMethodGoogleWallet = 3,
    MEGAPaymentMethodBitcoin      = 4,
    MEGAPaymentMethodUnionPay     = 5,
    MEGAPaymentMethodFortumo      = 6,
    MEGAPaymentMethodCreditCard   = 8,
    MEGAPaymentMethodCentili      = 9
};

/**
 * @brief Allows to control a MEGA account or a public folder.
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
 * options (to log in to a MEGA account or a public folder). If the login request succeed, you must call [MEGASdk fetchnodes] to get the
 * filesystem in MEGA.
 * After that, you can use all other requests, manage the files and start transfers.
 *
 * After using [MEGASdk logout] you can reuse the same MEGASdk object to log in to another MEGA account or a public folder.
 *
 */
@interface MEGASdk : NSObject 

#pragma mark - Properties

/**
 * @brief Email of the currently open account.
 *
 * If the MEGASdk object isn't logged in or the email isn't available,
 * this property is nil.
 *
 */
@property (readonly, nonatomic) NSString *myEmail;

/**
 * @brief Root node of the account.
 *
 * If you haven't successfully called [MEGASdk fetchNodes] before,
 * this property is nil.
 *
 */
@property (readonly, nonatomic) MEGANode *rootNode;

/**
 * @brief Rubbish node of the account.
 *
 * If you haven't successfully called [MEGASdk fetchNodes] before,
 * this property is nil.
 *
 */
@property (readonly, nonatomic) MEGANode *rubbishNode;

/**
 * @brief Inbox node of the account.
 *
 * If you haven't successfully called [MEGASdk fetchNodes] before,
 * this property is nil.
 *
 */
@property (readonly, nonatomic) MEGANode *inboxNode;

/**
 * @brief All active transfers.
 */
@property (readonly, nonatomic) MEGATransferList *transfers;

/**
 * @brief Total downloaded bytes since the creation of the MEGASdk object.
 *
 * @deprecated Property related to statistics will be reviewed in future updates to
 * provide more data and avoid race conditions. They could change or be removed in the current form.
 */
@property (readonly, nonatomic) NSNumber *totalsDownloadedBytes;

/**
 * @brief Total uploaded bytes since the creation of the MEGASdk object.
 *
 * @deprecated Property related to statistics will be reviewed in future updates to
 * provide more data and avoid race conditions. They could change or be removed in the current form.
 *
 */
@property (readonly, nonatomic) NSNumber *totalsUploadedBytes;

/**
 * @brief The master key of the account.
 *
 * The value is a Base64-encoded string.
 *
 * With the master key, it's possible to start the recovery of an account when the
 * password is lost:
 * - https://mega.co.nz/#recovery
 *
 */
@property (readonly, nonatomic) NSString *masterKey;

/**
 * @brief User-Agent header used by the SDK
 *
 * The User-Agent used by the SDK
 */
@property (readonly, nonatomic) NSString *userAgent;

#pragma mark - Init

/**
 * @brief Constructor suitable for most applications.
 * @param appKey AppKey of your application.
 * You can generate your AppKey for free here:
 * - https://mega.co.nz/#sdk
 *
 * @param userAgent User agent to use in network requests.
 * If you pass nil to this parameter, a default user agent will be used[].
 *
 */
- (instancetype)initWithAppKey:(NSString *)appKey userAgent:(NSString *)userAgent;

/**
 * @brief Constructor suitable for most applications.
 * @param appKey AppKey of your application.
 * You can generate your AppKey for free here:
 * - https://mega.co.nz/#sdk
 *
 * @param userAgent User agent to use in network requests.
 * If you pass nil to this parameter, a default user agent will be used.
 *
 * @param basePath Base path to store the local cache.
 * If you pass nil to this parameter, the SDK won't use any local cache.
 *
 */
- (instancetype)initWithAppKey:(NSString *)appKey userAgent:(NSString *)userAgent basePath:(NSString *)basePath;

#pragma mark - Add and remove delegates

/**
 * @brief Register a delegate to receive all events (requests, transfers, global).
 *
 * You can use [MEGASdk removeMEGADelegate:] to stop receiving events.
 *
 * @param delegate Delegate that will receive all events (requests, transfers, global).
 */
- (void)addMEGADelegate:(id<MEGADelegate>)delegate;

/**
 * @brief Register a delegate to receive all events about requests.
 *
 * You can use [MEGASdk removeMEGARequestDelegate:] to stop receiving events.
 *
 * @param delegate Delegate that will receive all events about requests.
 */
- (void)addMEGARequestDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Register a delegate to receive all events about transfers.
 *
 * You can use [MEGASdk removeMEGATransferDelegate:] to stop receiving events.
 *
 * @param delegate Delegate that will receive all events about transfers.
 */
- (void)addMEGATransferDelegate:(id<MEGATransferDelegate>)delegate;

/**
 * @brief Register a delegate to receive global events.
 *
 * You can use [MEGASdk removeMEGAGlobalDelegate:] to stop receiving events.
 *
 * @param delegate Delegate that will receive global events.
 */
- (void)addMEGAGlobalDelegate:(id<MEGAGlobalDelegate>)delegate;

/**
 * @brief Unregister a delegate.
 *
 * This delegate won't receive more events.
 *
 * @param delegate Delegate that is unregistered.
 */
- (void)removeMEGADelegate:(id<MEGADelegate>)delegate;

/**
 * @brief Unregister a MEGARequestDelegate.
 *
 * This delegate won't receive more events.
 *
 * @param delegate Delegate that is unregistered.
 */
- (void)removeMEGARequestDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Unregister a MEGATransferDelegate.
 *
 * This delegate won't receive more events.
 *
 * @param delegate Delegate that is unregistered.
 */
- (void)removeMEGATransferDelegate:(id<MEGATransferDelegate>)delegate;

/**
 * @brief Unregister a MEGAGlobalDelegate.
 *
 * This delegate won't receive more events.
 *
 * @param delegate Delegate that is unregistered.
 */
- (void)removeMEGAGlobalDelegate:(id<MEGAGlobalDelegate>)delegate;

#pragma mark - Utils

/**
 * @brief Generates a private key based on the access password.
 *
 * This is a time consuming operation (specially for low-end mobile devices). Since the resulting key is
 * required to log in, this function allows to do this step in a separate function. You should run this function
 * in a background thread, to prevent UI hangs. The resulting key can be used in 
 * [MEGASdk fastLoginWithEmail:stringHash:base64pwKey:].
 *
 * @param password Access password.
 * @return Base64-encoded private key
 */
- (NSString *)base64pwkeyForPassword:(NSString *)password;

/**
 * @brief Generates a hash based in the provided private key and email.
 *
 * This is a time consuming operation (specially for low-end mobile devices). Since the resulting key is
 * required to log in, this function allows to do this step in a separate function. You should run this function
 * in a background thread, to prevent UI hangs. The resulting key can be used in 
 * [MEGASdk fastLoginWithEmail:stringHash:base64pwKey:].
 *
 * @param base64pwkey Private key returned by [MEGASdk base64PwKeybase64pwkeyForPassword:]
 * @param email Email to create the hash
 * @return Base64-encoded hash
 */
- (NSString *)hashForBase64pwkey:(NSString *)base64pwkey email:(NSString *)email;

/**
 * @brief Converts a Base64-encoded node handle to a MegaHandle.
 *
 * The returned value can be used to recover a MEGANode using [MEGASdk nodeForHandle:].
 * You can revert this operation using [MEGASdk base64handleForHandle:].
 *
 * @param base64Handle Base64-encoded node handle.
 * @return Node handle.
 */
+ (uint64_t)handleForBase64Handle:(NSString *)base64Handle;

/**
 * @brief Converts the handle of a node to a Base64-encoded NSString
 *
 * You take the ownership of the returned value
 * You can revert this operation using [MEGASdk handleForBase64Handle:]
 *
 * @param handle Node handle to be converted
 * @return Base64-encoded node handle
 */
+ (NSString *)base64HandleForHandle:(uint64_t)handle;

/**
 * @brief Retry all pending requests.
 *
 * When requests fails they wait some time before being retried. That delay grows exponentially if the request
 * fails again.
 *
 * The associated request type with this request is MEGARequestTypeRetryPendingConnections.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest flag] - Returns the first parameter
 * - [MEGARequest number] - Returns the second parameter
 */
- (void)retryPendingConnections;

/**
 * @brief Retry all pending requests and transfers.
 *
 * When requests and/or transfers fails they wait some time before being retried. That delay grows exponentially 
 * if the request or transfers fails again.
 *
 * Disconnect already connected requests and transfers
 *
 * The associated request type with this request is MEGARequestTypeRetryPendingConnections.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest flag] - Returns the first parameter
 * - [MEGARequest number] - Returns the second parameter
 */
- (void)reconnect;

#pragma mark - Login Requests

/**
 * @brief Log in to a MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeLogin.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the first parameter
 * - [MEGARequest password] - Returns the second parameter
 *
 * If the email/password aren't valid the error code provided in onRequestFinish is
 * MEGAErrorTypeApiENoent.
 *
 * @param email Email of the user.
 * @param password Password.
 * @param delegate Delegate to track this request.
 */
- (void)loginWithEmail:(NSString *)email password:(NSString *)password delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Log in to a MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeLogin.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the first parameter
 * - [MEGARequest password] - Returns the second parameter
 *
 * If the email/password aren't valid the error code provided in onRequestFinish is
 * MEGAErrorTypeApiENoent.
 *
 * @param email Email of the user.
 * @param password Password.
 */
- (void)loginWithEmail:(NSString *)email password:(NSString *)password;

/**
 * @brief Log in to a MEGA account using precomputed keys.
 *
 * The associated request type with this request is MEGARequestTypeLogin.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the first parameter
 * - [MEGARequest password] - Returns the second parameter
 * - [MEGARequest privateKey] - Returns the third parameter
 *
 * If the email/stringHash/base64pwKey aren't valid the error code provided in onRequestFinish is
 * MEGAErrorTypeApiENoent.
 *
 * @param email Email of the user.
 * @param stringHash Hash of the email returned by [MEGASdk hashForBase64pwkey:email:].
 * @param base64pwkey Private key calculated using [MEGASdk base64PwKeyWithPassword:].
 * @param delegate Delegate to track this request.
 */
- (void)fastLoginWithEmail:(NSString *)email stringHash:(NSString *)stringHash base64pwKey:(NSString *)base64pwKey delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Log in to a MEGA account using precomputed keys.
 *
 * The associated request type with this request is MEGARequestTypeLogin.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the first parameter
 * - [MEGARequest password] - Returns the second parameter
 * - [MEGARequest privateKey] - Returns the third parameter
 *
 * If the email/stringHash/base64pwKey aren't valid the error code provided in onRequestFinish is
 * MEGAErrorTypeApiENoent.
 *
 * @param email Email of the user.
 * @param stringHash Hash of the email returned by [MEGASdk hashForBase64pwkey:email:].
 * @param base64pwkey Private key calculated using [MEGASdk base64PwKeyWithPassword:].
 */
- (void)fastLoginWithEmail:(NSString *)email stringHash:(NSString *)stringHash base64pwKey:(NSString *)base64pwKey;

/**
 * @brief Log in to a MEGA account using a session key.
 *
 * The associated request type with this request is MEGARequestTypeFastLogin.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest sessionKey] - Returns the session key.
 *
 * @param session Session key previously dumped with [MEGASdk dumpSession].
 * @param delegate Delegate to track this request.
 */
- (void)fastLoginWithSession:(NSString *)session delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Log in to a MEGA account using a session key.
 *
 * The associated request type with this request is MEGARequestTypeFastLogin.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest sessionKey] - Returns the session key
 *
 * @param session Session key previously dumped with [MEGASdk dumpSession].
 */
- (void)fastLoginWithSession:(NSString *)session;

/**
 * @brief Log in to a public folder using a folder link.
 *
 * After a successful login, you should call [MEGAsdk fetchnodes] to get filesystem and
 * start working with the folder.
 *
 * The associated request type with this request is MEGARequestTypeLogin.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Retuns the string "FOLDER"
 * - [MEGARequest link] - Returns the public link to the folder
 *
 * @param folderLink Link to a folder in MEGA.
 * @param delegate Delegate to track this request.
 */
- (void)loginToFolderLink:(NSString *)folderLink delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Log in to a public folder using a folder link.
 *
 * After a successful login, you should call [MEGAsdk fetchnodes] to get filesystem and
 * start working with the folder.
 *
 * The associated request type with this request is MEGARequestTypeLogin.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Retuns the string "FOLDER"
 * - [MEGARequest link] - Returns the public link to the folder
 *
 * @param folderLink Link to a folder in MEGA.
 */
- (void)loginToFolderLink:(NSString *)folderLink;

/**
 * @brief Returns the current session key.
 *
 * You have to be logged in to get a valid session key. Otherwise,
 * this function returns nil.
 *
 * @return Current session key.
 */
- (NSString *)dumpSession;

/**
 * @brief Check if the MEGASdk object is logged in.
 * @return 0 if not logged in, Otherwise, a number >= 0.
 */
- (NSInteger)isLoggedIn;

/**
 * @brief Fetch the filesystem in MEGA.
 *
 * The MEGASdk object must be logged in in an account or a public folder
 * to successfully complete this request.
 *
 * The associated request type with this request is MEGARequestTypeFetchNodes.
 *
 * @param delegate Delegate to track this request.
 */
- (void)fetchNodesWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Fetch the filesystem in MEGA.
 *
 * The MEGASdk object must be logged in in an account or a public folder
 * to successfully complete this request.
 *
 * The associated request type with this request is MEGARequestTypeFetchNodes.
 *
 */
- (void)fetchNodes;

/**
 * @brief Logout of the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeLogout.
 *
 * @param delegate Delegate to track this request.
 */
- (void)logoutWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Logout of the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeLogout.
 *
 */
- (void)logout;

#pragma mark - Create account and confirm account Requests

/**
 * @brief Initialize the creation of a new MEGA account.
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
 * @param email Email for the account.
 * @param password Password for the account.
 * @param name Name of the user.
 * @param delegate Delegate to track this request.
 */
- (void)createAccountWithEmail:(NSString *)email password:(NSString *)password name:(NSString *)name delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Initialize the creation of a new MEGA account.
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
 * @param email Email for the account.
 * @param password Password for the account.
 * @param name Name of the user.
 */
- (void)createAccountWithEmail:(NSString *)email password:(NSString *)password name:(NSString *)name;

/**
 * @brief Initialize the creation of a new MEGA account with precomputed keys
 *
 * The associated request type with this request is MEGARequestTypeCreateAccount.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email for the account
 * - [MEGARequest privateKey] - Returns the private key calculated with [MEGASdk base64pwkeyForPassword:]
 * - [MEGARequest name] - Returns the name of the user
 *
 * If this request succeed, a confirmation email will be sent to the users.
 * If an account with the same email already exists, you will get the error code
 * MEGAErrorTypeApiEExist in onRequestFinish
 *
 * @param email Email for the account.
 * @param base64pwkey Private key calculated with [MEGASdk base64pwkeyForPassword:].
 * @param name Name of the user.
 * @param delegate Delegate to track this request.
 */
- (void)fastCreateAccountWithEmail:(NSString *)email base64pwkey:(NSString *)base64pwkey name:(NSString *)name delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Initialize the creation of a new MEGA account with precomputed keys.
 *
 * The associated request type with this request is MEGARequestTypeCreateAccount.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email for the account
 * - [MEGARequest privateKey] - Returns the private key calculated with [MEGASdk base64pwkeyForPassword:]
 * - [MEGARequest name] - Returns the name of the user
 *
 * If this request succeed, a confirmation email will be sent to the users.
 * If an account with the same email already exists, you will get the error code
 * MEGAErrorTypeApiEExist in onRequestFinish.
 *
 * @param email Email for the account.
 * @param base64pwkey Private key calculated with [MEGASdk base64pwkeyForPassword:].
 * @param name Name of the user.
 */
- (void)fastCreateAccountWithEmail:(NSString *)email base64pwkey:(NSString *)base64pwkey name:(NSString *)name;

/**
 * @brief Get information about a confirmation link.
 *
 * The associated request type with this request is MEGARequestTypeQuerySignUpLink.
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest link] - Returns the confirmation link
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest email] - Return the email associated with the confirmation link.
 * - [MEGARequest name] - Returns the name associated with the confirmation link.
 *
 * @param link Confirmation link
 * @param delegate Delegate to track this request
 */
- (void)querySignupLink:(NSString *)link delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get information about a confirmation link.
 *
 * The associated request type with this request is MEGARequestTypeQuerySignUpLink.
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest link] - Returns the confirmation link
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest email] - Return the email associated with the confirmation link.
 * - [MEGARequest name] - Returns the name associated with the confirmation link.
 *
 * @param link Confirmation link.
 */
- (void)querySignupLink:(NSString *)link;

/**
 * @brief Confirm a MEGA account using a confirmation link and the user password.
 *
 * The associated request type with this request is MEGARequestTypeConfirmAccount.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest link] - Returns the confirmation link
 * - [MEGARequest password] - Returns the password
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest email] - Email of the account
 * - [MEGARequest name] - Name of the user
 *
 * @param link Confirmation link.
 * @param password Password for the account.
 * @param delegate Delegate to track this request.
 */
- (void)confirmAccountWithLink:(NSString *)link password:(NSString *)password delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Confirm a MEGA account using a confirmation link and the user password.
 *
 * The associated request type with this request is MEGARequestTypeConfirmAccount.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest link] - Returns the confirmation link
 * - [MEGARequest password] - Returns the password
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest email] - Email of the account
 * - [MEGARequest name] - Name of the user
 *
 * @param link Confirmation link.
 * @param password Password for the account.
 */
- (void)confirmAccountWithLink:(NSString *)link password:(NSString *)password;

/**
 * @brief Confirm a MEGA account using a confirmation link and a precomputed key.
 *
 * The associated request type with this request is MEGARequestTypeConfirmAccount.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest link] - Returns the confirmation link
 * - [MEGARequest privateKey] - Returns the base64pwkey parameter
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest email] - Email of the account
 * - [MEGARequest name] - Name of the user
 *
 * @param link Confirmation link.
 * @param base64pwkey Private key precomputed with [MEGASdk base64pwkeyForPassword:].
 * @param delegate Delegate to track this request.
 */
- (void)fastConfirmAccountWithLink:(NSString *)link base64pwkey:(NSString *)base64pwkey delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Confirm a MEGA account using a confirmation link and a precomputed key.
 *
 * The associated request type with this request is MEGARequestTypeConfirmAccount.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest link] - Returns the confirmation link
 * - [MEGARequest privateKey] - Returns the base64pwkey parameter
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest email] - Email of the account
 * - [MEGARequest name] - Name of the user
 *
 * @param link Confirmation link.
 * @param base64pwkey Private key precomputed with [MEGASdk base64pwkeyForPassword:].
 */
- (void)fastConfirmAccountWithLink:(NSString *)link base64pwkey:(NSString *)base64pwkey;

#pragma mark - Filesystem changes Requests

/**
 * @brief Create a folder in the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeCreateAccount.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest parentHandle] - Returns the handle of the parent folder
 * - [MEGARequest name] - Returns the name of the new folder
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest nodeHandle] - Handle of the new folder
 *
 * @param name Name of the new folder.
 * @param parent Parent folder.
 * @param delegate Delegate to track this request.
 */
- (void)createFolderWithName:(NSString *)name parent:(MEGANode *)parent delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Create a folder in the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeCreateAccount.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest parentHandle] - Returns the handle of the parent folder
 * - [MEGARequest name] - Returns the name of the new folder
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest nodeHandle] - Handle of the new folder
 *
 * @param name Name of the new folder.
 * @param parent Parent folder.
 */
- (void)createFolderWithName:(NSString *)name parent:(MEGANode *)parent;

/**
 * @brief Move a node in the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeMove.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node to move
 * - [MEGARequest parentHandle] - Returns the handle of the new parent for the node
 *
 * @param node Node to move.
 * @param newParent New parent for the node.
 * @param delegate Delegate to track this request.
 */
- (void)moveNode:(MEGANode *)node newParent:(MEGANode *)newParent delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Move a node in the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeMove.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node to move
 * - [MEGARequest parentHandle] - Returns the handle of the new parent for the node
 *
 * @param node Node to move.
 * @param newParent New parent for the node.
 */
- (void)moveNode:(MEGANode *)node newParent:(MEGANode *)newParent;

/**
 * @brief Copy a node in the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeCopy.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node to move
 * - [MEGARequest parentHandle] - Returns the handle of the new parent for the node
 * - [MEGARequest publicNode] - Returns the node to copy (if it is a public node)
 *
 * @param node Node to copy.
 * @param newParent New parent for the node.
 * @param delegate Delegate to track this request.
 */
- (void)copyNode:(MEGANode *)node newParent:(MEGANode *)newParent delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Copy a node in the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeCopy.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node to move
 * - [MEGARequest parentHandle] - Returns the handle of the new parent for the node
 * - [MEGARequest publicNode] - Returns the node to copy (if it is a public node)
 *
 * @param node Node to copy.
 * @param newParent New parent for the node.
 */
- (void)copyNode:(MEGANode *)node newParent:(MEGANode *)newParent;

/**
 * @brief Copy a node in the MEGA account changing the file name
 *
 * The associated request type with this request is MEGARequestTypeCopy
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node to copy
 * - [MEGARequest parentHandle] - Returns the handle of the new parent for the new node
 * - [MEGARequest publicNode] - Returns the node to copy
 * - [MEGARequest name] - Returns the name for the new node
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest nodeHandle] - Handle of the new node
 *
 * @param node Node to copy
 * @param newParent Parent for the new node
 * @param newName Name for the new node
 *
 * This parameter is only used if the original node is a file and it isn't a public node,
 * otherwise, it's ignored.
 *
 * @param delegate Delegate to track this request
 */
- (void)copyNode:(MEGANode *)node newParent:(MEGANode *)newParent newName:(NSString *)newName delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Copy a node in the MEGA account changing the file name
 *
 * The associated request type with this request is MEGARequestTypeCopy
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node to copy
 * - [MEGARequest parentHandle] - Returns the handle of the new parent for the new node
 * - [MEGARequest publicNode] - Returns the node to copy
 * - [MEGARequest name] - Returns the name for the new node
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest nodeHandle] - Handle of the new node
 *
 * @param node Node to copy
 * @param newParent Parent for the new node
 * @param newName Name for the new node
 *
 * This parameter is only used if the original node is a file and it isn't a public node,
 * otherwise, it's ignored.
 */
- (void)copyNode:(MEGANode *)node newParent:(MEGANode *)newParent newName:(NSString *)newName;

/**
 * @brief Rename a node in the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeRename.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node to rename
 * - [MEGARequest name] - Returns the new name for the node
 *
 * @param node Node to modify.
 * @param newName New name for the node.
 * @param delegate Delegate to track this request.
 */
- (void)renameNode:(MEGANode *)node newName:(NSString *)newName delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Rename a node in the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeRename.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node to rename
 * - [MEGARequest name] - Returns the new name for the node
 *
 * @param node Node to modify.
 * @param newName New name for the node.
 */
- (void)renameNode:(MEGANode *)node newName:(NSString *)newName;

/**
 * @brief Remove a node in the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeRemove.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node to rename
 *
 * @param node Node to remove.
 * @param delegate Delegate to track this request.
 */
- (void)removeNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Remove a node in the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeRemove.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node to rename
 *
 * @param node Node to remove.
 */
- (void)removeNode:(MEGANode *)node;

#pragma mark - Sharing Requests

/**
 * @brief Share or stop sharing a folder in MEGA with another user using a MEGAUser.
 *
 * To share a folder with an user, set the desired access level in the level parameter. If you
 * want to stop sharing a folder use the access level MEGAShareTypeAccessUnkown.
 *
 * The associated request type with this request is MEGARequestTypeCopy.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the folder to share
 * - [MEGARequest email] - Returns the email of the user that receives the shared folder
 * - [MEGARequest access] - Returns the access that is granted to the user
 *
 * @param node The folder to share. It must be a non-root folder.
 * @param user User that receives the shared folder.
 * @param level Permissions that are granted to the user.
 * Valid values for this parameter:
 * - MEGAShareTypeAccessUnkown = -1
 * Stop sharing a folder with this user
 *
 * - MEGAShareTypeAccessRead = 0
 * - MEGAShareTypeAccessReadWrite = 1
 * - MEGAShareTypeAccessFull = 2
 * - MEGAShareTypeAccessOwner = 3
 *
 * @param delegate Delegate to track this request.
 */
- (void)shareNode:(MEGANode *)node withUser:(MEGAUser *)user level:(NSInteger)level delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Share or stop sharing a folder in MEGA with another user using a MEGAUser.
 *
 * To share a folder with an user, set the desired access level in the level parameter. If you
 * want to stop sharing a folder use the access level MEGAShareTypeAccessUnkown.
 *
 * The associated request type with this request is MEGARequestTypeCopy.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the folder to share
 * - [MEGARequest email] - Returns the email of the user that receives the shared folder
 * - [MEGARequest access] - Returns the access that is granted to the user
 *
 * @param node The folder to share. It must be a non-root folder.
 * @param user User that receives the shared folder.
 * @param level Permissions that are granted to the user.
 * Valid values for this parameter:
 * - MEGAShareTypeAccessUnkown = -1
 * Stop sharing a folder with this user
 *
 * - MEGAShareTypeAccessRead = 0
 * - MEGAShareTypeAccessReadWrite = 1
 * - MEGAShareTypeAccessFull = 2
 * - MEGAShareTypeAccessOwner = 3
 *
 */
- (void)shareNode:(MEGANode *)node withUser:(MEGAUser *)user level:(NSInteger)level;

/**
 * @brief Share or stop sharing a folder in MEGA with another user using his email
 *
 * To share a folder with an user, set the desired access level in the level parameter. If you
 * want to stop sharing a folder use the access level MEGAShareTypeAccessUnkown
 *
 * The associated request type with this request is MEGARequestTypeCopy
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the folder to share
 * - [MEGARequest email] - Returns the email of the user that receives the shared folder
 * - [MEGARequest access] - Returns the access that is granted to the user
 *
 * @param node The folder to share. It must be a non-root folder
 * @param email Email of the user that receives the shared folder. If it doesn't have a MEGA account, the folder will be shared anyway
 * and the user will be invited to register an account.
 *
 * @param level Permissions that are granted to the user
 * Valid values for this parameter:
 * - MEGAShareTypeAccessUnkown = -1
 * Stop sharing a folder with this user
 *
 * - MEGAShareTypeAccessRead = 0
 * - MEGAShareTypeAccessReadWrite = 1
 * - MEGAShareTypeAccessFull = 2
 * - MEGAShareTypeAccessOwner = 3
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)shareNode:(MEGANode *)node withEmail:(NSString *)email level:(NSInteger)level delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Share or stop sharing a folder in MEGA with another user using his email
 *
 * To share a folder with an user, set the desired access level in the level parameter. If you
 * want to stop sharing a folder use the access level MEGAShareTypeAccessUnkown
 *
 * The associated request type with this request is MEGARequestTypeCopy
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the folder to share
 * - [MEGARequest email] - Returns the email of the user that receives the shared folder
 * - [MEGARequest access] - Returns the access that is granted to the user
 *
 * @param node The folder to share. It must be a non-root folder
 * @param email Email of the user that receives the shared folder. If it doesn't have a MEGA account, the folder will be shared anyway
 * and the user will be invited to register an account.
 *
 * @param level Permissions that are granted to the user
 * Valid values for this parameter:
 * - MEGAShareTypeAccessUnkown = -1
 * Stop sharing a folder with this user
 *
 * - MEGAShareTypeAccessRead = 0
 * - MEGAShareTypeAccessReadWrite = 1
 * - MEGAShareTypeAccessFull = 2
 * - MEGAShareTypeAccessOwner = 3
 *
 */
- (void)shareNode:(MEGANode *)node withEmail:(NSString *)email level:(NSInteger)level;

/**
 * @brief Import a public link to the account.
 *
 * The associated request type with this request is MEGARequestTypeImportLink.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest link] - Returns the public link to the file
 * - [MEGARequest parentHandle] - Returns the folder that receives the imported file
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest nodeHandle] - Handle of the new node in the account
 *
 * @param megaFileLink Public link to a file in MEGA.
 * @param parent Parent folder for the imported file.
 * @param delegate Delegate to track this request.
 */
- (void)importMegaFileLink:(NSString *)megaFileLink parent:(MEGANode *)parent delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Import a public link to the account.
 *
 * The associated request type with this request is MEGARequestTypeImportLink.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest link] - Returns the public link to the file
 * - [MEGARequest parentHandle] - Returns the folder that receives the imported file
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest nodeHandle] - Handle of the new node in the account
 *
 * @param megaFileLink Public link to a file in MEGA.
 * @param parent Parent folder for the imported file.
 */
- (void)importMegaFileLink:(NSString *)megaFileLink parent:(MEGANode *)parent;

/**
 * @brief Get a MEGANode from a public link to a file.
 *
 * A public node can be imported using [MEGASdk copyNode:newParent:] or downloaded using [MEGASdk startDownloadNode:localPath:]
 *
 * The associated request type with this request is MEGARequestTypeGetPublicNode.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest link] - Returns the public link to the file
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest publicNode] - Public MEGANode corresponding to the public link
 *
 * @param megaFileLink Public link to a file in MEGA.
 * @param delegate Delegate to track this request.
 */
- (void)publicNodeForMegaFileLink:(NSString *)megaFileLink delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get a MEGANode from a public link to a file.
 *
 * A public node can be imported using [MEGASdk copyNode:newParent:] or downloaded using [MEGASdk startDownloadNode:localPath:].
 *
 * The associated request type with this request is MEGARequestTypeGetPublicNode.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest link] - Returns the public link to the file
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest publicNode] - Public MEGANode corresponding to the public link
 *
 * @param megaFileLink Public link to a file in MEGA.
 */
- (void)publicNodeForMegaFileLink:(NSString *)megaFileLink;

/**
 * @brief Generate a public link of a file/folder in MEGA.
 *
 * The associated request type with this request is MEGARequestTypeExport.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node
 * - [MEGARequest access] - Returns true
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest link] - Public link
 *
 * @param node MEGANode to get the public link.
 * @param delegate Delegate to track this request.
 */
- (void)exportNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Generate a public link of a file/folder in MEGA.
 *
 * The associated request type with this request is MEGARequestTypeExport.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node
 * - [MEGARequest access] - Returns true
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest link] - Public link
 *
 * @param node MEGANode to get the public link.
 */
- (void)exportNode:(MEGANode *)node;

/**
 * @brief Stop sharing a file/folder.
 *
 * The associated request type with this request is MEGARequestTypeExport.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node
 * - [MEGARequest access] - Returns false
 *
 * @param node MEGANode to stop sharing.
 * @param delegate Delegate to track this request.
 */
- (void)disableExportNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Stop sharing a file/folder.
 *
 * The associated request type with this request is MEGARequestTypeExport.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node
 * - [MEGARequest access] - Returns false
 *
 * @param node MEGANode to stop sharing.
 */
- (void)disableExportNode:(MEGANode *)node;

#pragma mark - Attributes Requests

/**
 * @brief Get the thumbnail of a node.
 *
 * If the node doesn't have a thumbnail the request fails with the MEGAErrorTypeApiENoent
 * error code.
 *
 * The associated request type with this request is MEGARequestTypeGetAttrFile.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node
 * - [MEGARequest file] - Returns the destination path
 * - [MEGARequest paramType] - Returns MEGAAttributeTypeThumbnail
 *
 * @param node Node to get the thumbnail.
 * @param destinationFilePath Destination path for the thumbnail.
 * If this path is a local folder, it must end with a '\' or '/' character and (Base64-encoded handle + "0.jpg")
 * will be used as the file name inside that folder. If the path doesn't finish with
 * one of these characters, the file will be downloaded to a file in that path.
 *
 * @param delegate Delegate to track this request.
 */
- (void)getThumbnailNode:(MEGANode *)node destinationFilePath:(NSString *)destinationFilePath delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get the thumbnail of a node.
 *
 * If the node doesn't have a thumbnail the request fails with the MEGAErrorTypeApiENoent
 * error code.
 *
 * The associated request type with this request is MEGARequestTypeGetAttrFile.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node
 * - [MEGARequest file] - Returns the destination path
 * - [MEGARequest paramType] - Returns MEGAAttributeTypeThumbnail
 *
 * @param node Node to get the thumbnail.
 * @param destinationFilePath Destination path for the thumbnail.
 * If this path is a local folder, it must end with a '\' or '/' character and (Base64-encoded handle + "0.jpg")
 * will be used as the file name inside that folder. If the path doesn't finish with
 * one of these characters, the file will be downloaded to a file in that path.
 *
 */
- (void)getThumbnailNode:(MEGANode *)node destinationFilePath:(NSString *)destinationFilePath;

/**
 * @brief Cancel the retrieval of a thumbnail.
 *
 * The associated request type with this request is MEGARequestTypeGetAttrFile.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node
 * - [MEGARequest paramType] - Returns MEGAAttributeTypeThumbnail
 *
 * @param node Node to cancel the retrieval of the thumbnail.
 * @param delegate Delegate to track this request.
 *
 * @see [MEGASdk getThumbnailNode:destinationFilePath:].
 */
- (void)cancelGetThumbnailNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Cancel the retrieval of a thumbnail.
 *
 * The associated request type with this request is MEGARequestTypeGetAttrFile.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node
 * - [MEGARequest paramType] - Returns MEGAAttributeTypeThumbnail
 *
 * @param node Node to cancel the retrieval of the thumbnail.
 *
 * @see [MEGASdk getThumbnailNode:destinationFilePath:].
 */
- (void)cancelGetThumbnailNode:(MEGANode *)node;

/**
 * @brief Set the thumbnail of a MEGANode.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrFile.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node
 * - [MEGARequest file] - Returns the source path
 * - [MEGARequest paramType] - Returns MegaApi::ATTR_TYPE_THUMBNAIL
 *
 * @param node MEGANode to set the thumbnail.
 * @param sourceFilePath Source path of the file that will be set as thumbnail.
 * @param delegate Delegate to track this request.
 */
- (void)setThumbnailNode:(MEGANode *)node sourceFilePath:(NSString *)sourceFilePath delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Set the thumbnail of a MEGANode.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrFile.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node
 * - [MEGARequest file] - Returns the source path
 * - [MEGARequest paramType] - Returns MEGAAttributeTypeThumbnail
 *
 * @param node MEGANode to set the thumbnail.
 * @param sourceFilePath Source path of the file that will be set as thumbnail.
 */
- (void)setThumbnailNode:(MEGANode *)node sourceFilePath:(NSString *)sourceFilePath;

/**
 * @brief Get the preview of a node.
 *
 * If the node doesn't have a preview the request fails with the MEGAErrorTypeApiENoent
 * error code
 *
 * The associated request type with this request is MEGARequestTypeGetAttrFile.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node
 * - [MEGARequest file] - Returns the destination path
 * - [MEGARequest paramType] - Returns MEGAAttributeTypePreview
 *
 * @param node Node to get the preview.
 * @param destinationFilePath Destination path for the preview.
 * If this path is a local folder, it must end with a '\' or '/' character and (Base64-encoded handle + "0.jpg")
 * will be used as the file name inside that folder. If the path doesn't finish with
 * one of these characters, the file will be downloaded to a file in that path.
 *
 * @param delegate Delegate to track this request.
 */
- (void)getPreviewNode:(MEGANode *)node destinationFilePath:(NSString *)destinationFilePath delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get the preview of a node.
 *
 * If the node doesn't have a preview the request fails with the MEGAErrorTypeApiENoent
 * error code.
 *
 * The associated request type with this request is MEGARequestTypeGetAttrFile.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node
 * - [MEGARequest file] - Returns the destination path
 * - [MEGARequest paramType] - Returns MEGAAttributeTypePreview
 *
 * @param node Node to get the preview.
 * @param destinationFilePath Destination path for the preview.
 * If this path is a local folder, it must end with a '\' or '/' character and (Base64-encoded handle + "0.jpg")
 * will be used as the file name inside that folder. If the path doesn't finish with
 * one of these characters, the file will be downloaded to a file in that path.
 *
 * @param delegate Delegate to track this request.
 */
- (void)getPreviewNode:(MEGANode *)node destinationFilePath:(NSString *)destinationFilePath;

/**
 * @brief Cancel the retrieval of a preview.
 *
 * The associated request type with this request is MEGARequestTypeGetAttrFile.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node
 * - [MEGARequest paramType] - Returns MEGAAttributeTypePreview
 *
 * @param node Node to cancel the retrieval of the preview.
 * @param delegate Delegate to track this request.
 *
 * @see [MEGASdk getPreviewNode:destinationFilePath:].
 */
- (void)cancelGetPreviewNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Cancel the retrieval of a preview.
 *
 * The associated request type with this request is MEGARequestTypeGetAttrFile.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node
 * - [MEGARequest paramType] - Returns MEGAAttributeTypePreview
 *
 * @param node Node to cancel the retrieval of the preview.
 * @param delegate Delegate to track this request.
 *
 * @see [MEGASdk getPreviewNode:destinationFilePath:].
 */
- (void)cancelGetPreviewNode:(MEGANode *)node;

/**
 * @brief Set the preview of a node.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrFile.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node
 * - [MEGARequest file] - Returns the source path
 * - [MEGARequest paramType] - Returns MEGAAttributeTypePreview
 *
 * @param node Node to set the preview.
 * @param sourceFilePath Source path of the file that will be set as preview.
 * @param delegate Delegate to track this request.
 */
- (void)setPreviewNode:(MEGANode *)node sourceFilePath:(NSString *)sourceFilePath delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Set the preview of a MEGANode.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrFile.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node
 * - [MEGARequest file] - Returns the source path
 * - [MEGARequest paramType] - Returns MEGAAttributeTypePreview
 *
 * @param node Node to set the preview.
 * @param sourceFilePath Source path of the file that will be set as preview.
 */
- (void)setPreviewNode:(MEGANode *)node sourceFilePath:(NSString *)sourceFilePath;

/**
 * @brief Get the avatar of a MEGAUser.
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest file] - Returns the destination path
 * - [MEGARequest email] - Returns the email of the user
 *
 * @param user MEGAUser to get the avatar.
 * @param destinationFilePath Destination path for the avatar. It has to be a path to a file, not to a folder.
 * If this path is a local folder, it must end with a '\' or '/' character and (email + "0.jpg")
 * will be used as the file name inside that folder. If the path doesn't finish with
 * one of these characters, the file will be downloaded to a file in that path.
 *
 * @param delegate Delegate to track this request.
 */
- (void)getAvatarUser:(MEGAUser *)user destinationFilePath:(NSString *)destinationFilePath delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get the avatar of a MEGAUser.
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest file] - Returns the destination path
 * - [MEGARequest email] - Returns the email of the user
 *
 * @param user MEGAUser to get the avatar.
 * @param destinationFilePath Destination path for the avatar. It has to be a path to a file, not to a folder.
 * If this path is a local folder, it must end with a '\' or '/' character and (email + "0.jpg")
 * will be used as the file name inside that folder. If the path doesn't finish with
 * one of these characters, the file will be downloaded to a file in that path.
 *
 */
- (void)getAvatarUser:(MEGAUser *)user destinationFilePath:(NSString *)destinationFilePath;

/**
 * @brief Set the avatar of the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrFile.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest file] - Returns the source path
 *
 * @param sourceFilePath Source path of the file that will be set as avatar.
 * @param delegate Delegate to track this request.
 */
- (void)setAvatarUserWithSourceFilePath:(NSString *)sourceFilePath delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Set the avatar of the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrFile.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest file] - Returns the source path
 *
 * @param sourceFilePath Source path of the file that will be set as avatar.
 */
- (void)setAvatarUserWithSourceFilePath:(NSString *)sourceFilePath;

/**
 * @brief Get an attribute of a MEGAUser.
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest text] - Returns the value of the attribute
 *
 * @param user MEGAUser to get the attribute. If this parameter is set to nil, the attribute
 * is obtained for the active account
 * @param type Attribute type
 *
 * Valid values are:
 *
 * MEGAUserAttributeFirstname = 1
 * Get the firstname of the user
 * MEGAUserAttributeLastname = 2
 * Get the lastname of the user
 *
 */
- (void)getUserAttibuteForUser:(MEGAUser *)user type:(MEGAUserAttribute)type;


/**
 * @brief Get an attribute of a MEGAUser.
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest text] - Returns the value of the attribute
 *
 * @param user MEGAUser to get the attribute. If this parameter is set to nil, the attribute
 * is obtained for the active account
 * @param type Attribute type
 *
 * Valid values are:
 *
 * MEGAUserAttributeFirstname = 1
 * Get the firstname of the user
 * MEGAUserAttributeLastname = 2
 * Get the lastname of the user
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)getUserAttibuteForUser:(MEGAUser *)user type:(MEGAUserAttribute)type delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get an attribute of the current account.
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest text] - Returns the value of the attribute
 *
 * @param type Attribute type
 *
 * Valid values are:
 *
 * MEGAUserAttributeFirstname = 1
 * Get the firstname of the user
 * MEGAUserAttributeLastname = 2
 * Get the lastname of the user
 */
- (void)getUserAttibuteType:(MEGAUserAttribute)type;

/**
 * @brief Get an attribute of the current account.
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest text] - Returns the value of the attribute
 *
 * @param type Attribute type
 *
 * Valid values are:
 *
 * MEGAUserAttributeFirstname = 1
 * Get the firstname of the user
 * MEGAUserAttributeLastname = 2
 * Get the lastname of the user
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)getUserAttibuteType:(MEGAUserAttribute)type delegate:(id<MEGARequestDelegate>)delegate;


/**
 * @brief Set an attribute of the current user.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type
 * - [MEGARequest text] - Return the new value for the attibute
 *
 * @param type Attribute type
 *
 * Valid values are:
 *
 * MEGAUserAttributeFirstname = 1
 * Get the firstname of the user
 * MEGAUserAttributeLastname = 2
 * Get the lastname of the user
 *
 * @param value New attribute value
 */
- (void)setUserAttibuteType:(MEGAUserAttribute)type value:(NSString *)value;

/**
 * @brief Set an attribute of the current user.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type
 * - [MEGARequest text] - Return the new value for the attibute
 *
 * @param type Attribute type
 *
 * Valid values are:
 *
 * MEGAUserAttributeFirstname = 1
 * Get the firstname of the user
 * MEGAUserAttributeLastname = 2
 * Get the lastname of the user
 *
 * @param value New attribute value
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)setUserAttibuteType:(MEGAUserAttribute)type value:(NSString *)value delegate:(id<MEGARequestDelegate>)delegate;

#pragma mark - Account management Requests

/**
 * @brief Get details about the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeAccountDetails.
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest megaAccountDetails] - Details of the MEGA account
 *
 * @param delegate Delegate to track this request.
 */
- (void)getAccountDetailsWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get details about the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeAccountDetails.
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest megaAccountDetails] - Details of the MEGA account.
 *
 */
- (void)getAccountDetails;

/**
 * @brief Get the available pricing plans to upgrade a MEGA account.
 *
 * You can get a payment URL for any of the pricing plans provided by this function
 * using [MEGASdk getPaymentIdForProductHandle:].
 *
 * The associated request type with this request is MEGARequestTypeGetPricing.
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest pricing] - MEGAPricing object with all pricing plans
 *
 * @param delegate Delegate to track this request.
 *
 * @see [MEGASdk getPaymentIdForProductHandle:].
 */
- (void)getPricingWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get the available getPricing plans to upgrade a MEGA account.
 *
 * You can get a payment URL for any of the getPricing plans provided by this function
 * using [MEGASdk getPaymentIdForProductHandle:].
 *
 * The associated request type with this request is MEGARequestTypeGetPricing.
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest pricing] - MEGAPricing object with all pricing plans
 *
 * @see [MEGASdk getPaymentIdForProductHandle:].
 */
- (void)getPricing;

/**
 * @brief Get the payment URL for an upgrade.
 *
 * The associated request type with this request is MEGARequestTypeGetPaymentId.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the product
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest link] - Payment link
 *
 * @param productHandle Handle of the product (see [MEGASdk getPricing]).
 * @param delegate Delegate to track this request.
 *
 * @see [MEGASdk getPricing].
 */
- (void)getPaymentIdForProductHandle:(uint64_t)productHandle delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get the payment URL for an upgrade.
 *
 * The associated request type with this request is MEGARequestTypeGetPaymentId.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the product
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest link] - Payment link
 *
 * @param productHandle Handle of the product (see [MEGASdk getPricing]).
 *
 * @see [MEGASdk getPricing].
 */
- (void)getPaymentIdForProductHandle:(uint64_t)productHandle;

/**
 * @brief Submit a purchase receipt for verification
 *
 * The associated request type with this request is MEGARequestTypeSubmitPurchaseReceipt.
 *
 * @param gateway Payment gateway
 * Currently supported payment gateways are:
 * - MEGAPaymentMethodItunes = 2
 * - MEGAPaymentMethodGoogleWallet = 3
 *
 * @param receipt Purchase receipt
 * @param delegate Delegate to track this request
 */
- (void)submitPurchase:(MEGAPaymentMethod)gateway receipt:(NSString *)receipt delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Submit a purchase receipt for verification
 * @param gateway Payment gateway
 * Currently supported payment gateways are:
 * - MEGAPaymentMethodItunes = 2
 * - MEGAPaymentMethodGoogleWallet = 3
 *
 * @param receipt Purchase receipt
 */
- (void)submitPurchase:(MEGAPaymentMethod)gateway receipt:(NSString *)receipt;

/**
 * @brief Change the password of the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeChangePassword.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest password] - Returns the old password
 * - [MEGARequest newPassword] - Returns the new password
 *
 * @param oldPassword Old password.
 * @param newPassword New password.
 * @param delegate Delegate to track this request.
 */
- (void)changePassword:(NSString *)oldPassword newPassword:(NSString *)newPassword delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Change the password of the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeChangePassword.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest password] - Returns the old password
 * - [MEGARequest newPassword] - Returns the new password
 *
 * @param oldPassword Old password.
 * @param newPassword New password.
 */
- (void)changePassword:(NSString *)oldPassword newPassword:(NSString *)newPassword;

/**
 * @brief Add a new contact to the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeAddContact.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email of the contact
 *
 * @param email Email of the new contact.
 * @param delegate Delegate to track this request.
 */
- (void)addContactWithEmail:(NSString *)email delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Add a new contact to the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeAddContact.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email of the contact
 *
 * @param email Email of the new contact.
 */
- (void)addContactWithEmail:(NSString *)email;

/**
 * @brief Remove a contact to the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeRemoveContact.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email of the contact
 *
 * @param user User of the new contact.
 * @param delegate Delegate to track this request.
 */
- (void)removeContactUser:(MEGAUser *)user delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Add a new contact to the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeAddContact.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email of the contact
 *
 * @param user User of the new contact.
 */
- (void)removeContactUser:(MEGAUser *)user;

/**
 * @brief Submit feedback about the app.
 *
 * The User-Agent is used to identify the app. It can be set in [MEGASdk initWithAppKey:userAgent:]
 *
 * The associated request type with this request is MEGARequestTypeReportEvent.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns MEGAEventTypeFeedback
 * - [MEGARequest text] - Retuns the comment about the app
 * - [MEGARequest number] - Returns the rating for the app
 *
 * @param rating Integer to rate the app. Valid values: from 1 to 5.
 * @param comment Comment about the app.
 * @param delegate Delegate to track this request.
 *
 * @deprecated This function is for internal usage of MEGA apps. This feedback
 * is sent to MEGA servers.
 *
 */
- (void)submitFeedbackWithRating:(NSInteger)rating comment:(NSString *)comment delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Submit feedback about the app.
 *
 * The User-Agent is used to identify the app. It can be set in [MEGASdk initWithAppKey:userAgent:]
 *
 * The associated request type with this request is MEGARequestTypeReportEvent.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns MEGAEventTypeFeedback
 * - [MEGARequest text] - Retuns the comment about the app
 * - [MEGARequest number] - Returns the rating for the app
 *
 * @param rating Integer to rate the app. Valid values: from 1 to 5.
 * @param comment Comment about the app.
 *
 * @deprecated This function is for internal usage of MEGA apps. This feedback
 * is sent to MEGA servers.
 *
 */
- (void)submitFeedbackWithRating:(NSInteger)rating comment:(NSString *)comment;

/**
 * @brief Send a debug report.
 *
 * The User-Agent is used to identify the app. It can be set in [MEGASdk initWithAppKey:userAgent:]
 *
 * The associated request type with this request is MEGARequestTypeReportEvent.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns MEGAEventTypeFeedback
 * - [MEGARequest text] - Retuns the debug message
 *
 * @param text Debug message.
 * @param delegate Delegate to track this request.
 *
 * @deprecated This function is for internal usage of MEGA apps. This feedback
 * is sent to MEGA servers.
 */
- (void)reportDebugEventWithText:(NSString *)text delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Send a debug report.
 *
 * The User-Agent is used to identify the app. It can be set in [MEGASdk initWithAppKey:userAgent:]
 *
 * The associated request type with this request is MEGARequestTypeReportEvent.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns MEGAEventTypeFeedback
 * - [MEGARequest text] - Retuns the debug message
 *
 * @param text Debug message.
 *
 * @deprecated This function is for internal usage of MEGA apps. This feedback
 * is sent to MEGA servers.
 */

- (void)reportDebugEventWithText:(NSString *)text;

/**
 * @brief Get data about the logged account
 *
 * The associated request type with this request is MEGARequestTypeGetUserData.
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest name] - Returns the name of the logged user
 * - [MEGARequest password] - Returns the public RSA key of the account, Base64-encoded
 * - [MEGARequest privateKey] - Returns the private RSA key of the account, Base64-encoded
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)getUserDataWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get data about the logged account
 *
 * The associated request type with this request is MEGARequestTypeGetUserData.
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest name] - Returns the name of the logged user
 * - [MEGARequest password] - Returns the public RSA key of the account, Base64-encoded
 * - [MEGARequest privateKey] - Returns the private RSA key of the account, Base64-encoded
 *
 */
- (void)getUserData;

/**
 * @brief Get data about a contact
 *
 * The associated request type with this request is MEGARequestTypeGetUserData.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email of the contact
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest text] - Returns the XMPP ID of the contact
 * - [MEGARequest password] - Returns the public RSA key of the contact, Base64-encoded
 *
 * @param user Contact to get the data
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)getUserDataWithMEGAUser:(MEGAUser *)user delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get data about a contact
 *
 * The associated request type with this request is MEGARequestTypeGetUserData.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email of the contact
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest text] - Returns the XMPP ID of the contact
 * - [MEGARequest password] - Returns the public RSA key of the contact, Base64-encoded
 *
 * @param user Contact to get the data
 */
- (void)getUserDataWithMEGAUser:(MEGAUser *)user;

/**
 * @brief Get data about a contact
 *
 * The associated request type with this request is MEGARequestTypeGetUserData.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email or the Base64 handle of the contact
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest text] - Returns the XMPP ID of the contact
 * - [MEGARequest password] - Returns the public RSA key of the contact, Base64-encoded
 *
 * @param user Email or Base64 handle of the contact
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)getUserDataWithUser:(NSString *)user delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get data about a contact
 *
 * The associated request type with this request is MEGARequestTypeGetUserData.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email or the Base64 handle of the contact
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest text] - Returns the XMPP ID of the contact
 * - [MEGARequest password] - Returns the public RSA key of the contact, Base64-encoded
 *
 * @param user Email or Base64 handle of the contact
 */
- (void)getUserDataWithUser:(NSString *)user;

#pragma mark - Transfers

/**
 * @brief Get the transfer with a transfer tag
 *
 * That tag can be got using [MEGATransfer tag]
 *
 * You take the ownership of the returned value
 *
 * @param transferTag tag to check
 * @return MEGATransfer object with that tag, or nil if there isn't any
 * active transfer with it
 *
 */
- (MEGATransfer *)transferByTag:(NSInteger)transferTag;

/**
 * @brief Upload a file.
 * @param localPath Local path of the file.
 * @param parent Node for the file in the MEGA account.
 * @param delegate Delegate to track this transfer.
 */
- (void)startUploadWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent delegate:(id<MEGATransferDelegate>)delegate;

/**
 * @brief Upload a file.
 * @param localPath Local path of the file.
 * @param parent Node for the file in the MEGA account.
 */
- (void)startUploadWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent;

/**
 * @brief Upload a file with a custom name.
 * @param localPath Local path of the file.
 * @param parent Parent node for the file in the MEGA account.
 * @param fileName Custom file name for the file in MEGA.
 * @param delegate Delegate to track this transfer.
 */
- (void)startUploadToFileWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent filename:(NSString *)filename delegate:(id<MEGATransferDelegate>)delegate;

/**
 * @brief Upload a file with a custom name.
 * @param localPath Local path of the file.
 * @param parent Parent node for the file in the MEGA account.
 * @param fileName Custom file name for the file in MEGA.
 */
- (void)startUploadToFileWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent filename:(NSString *)filename;

/**
 * @brief Download a file from MEGA.
 * @param node MEGANode that identifies the file.
 * @param localPath Destination path for the file.
 * If this path is a local folder, it must end with a '\' or '/' character and the file name
 * in MEGA will be used to store a file inside that folder. If the path doesn't finish with
 * one of these characters, the file will be downloaded to a file in that path.
 *
 * @param delegate Delegate to track this transfer.
 */
- (void)startDownloadNode:(MEGANode *)node localPath:(NSString *)localPath delegate:(id<MEGATransferDelegate>)delegate;

/**
 * @brief Download a file from MEGA.
 * @param node MEGANode that identifies the file.
 * @param localPath Destination path for the file.
 * If this path is a local folder, it must end with a '\' or '/' character and the file name
 * in MEGA will be used to store a file inside that folder. If the path doesn't finish with
 * one of these characters, the file will be downloaded to a file in that path.
 *
 * @param delegate Delegate to track this transfer.
 */
- (void)startDownloadNode:(MEGANode *)node localPath:(NSString *)localPath;

/**
 * @brief Start an streaming download
 *
 * Streaming downloads don't save the downloaded data into a local file. It is provided 
 * in the callback [MEGATransferDelegate onTransferData:transfer:]. Only the MEGATransferDelegate
 * passed to this function will receive [MEGATransferDelegate onTransferData:transfer:] callbacks.
 * MEGATransferDelegate objects registered with [MEGASdk addMEGATransferDelegate:] won't 
 * receive them for performance reasons.
 *
 * @param node MEGANode that identifies the file (public nodes aren't supported yet)
 * @param startPos First byte to download from the file
 * @param size Size of the data to download
 * @param delegate MEGATransferDelegate to track this transfer
 */
- (void)startStreamingNode:(MEGANode *)node startPos:(NSNumber *)startPos size:(NSNumber *)size delegate:(id<MEGATransferDelegate>)delegate;

/**
 * @brief Start an streaming download
 *
 * Streaming downloads don't save the downloaded data into a local file. It is provided
 * in the callback [MEGATransferDelegate onTransferData:transfer:]. Only the MEGATransferDelegate
 * passed to this function will receive [MEGATransferDelegate onTransferData:transfer:] callbacks.
 * MEGATransferDelegate objects registered with [MEGASdk addMEGATransferDelegate:] won't
 * receive them for performance reasons.
 *
 * @param node MEGANode that identifies the file (public nodes aren't supported yet)
 * @param startPos First byte to download from the file
 * @param size Size of the data to download
 */
- (void)startStreamingNode:(MEGANode *)node startPos:(NSNumber *)startPos size:(NSNumber *)size;

/**
 * @brief Cancel a transfer.
 *
 * When a transfer is cancelled, it will finish and will provide the error code
 * MEGAErrorTypeApiEIncomplete in [MEGATransferDelegate onTransferFinish:transfer:error:] and
 * [MEGADelegate onTransferFinish:transfer:error:].
 *
 * The associated request type with this request is MEGARequestTypeCancelTransfer.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest transferTag] - Returns the tag of the cancelled transfer ([MEGATransfer tag])
 *
 * @param transfer MEGATransfer object that identifies the transfer.
 * You can get this object in any MEGATransferDelegate callback or any MEGADelegate callback
 * related to transfers.
 *
 * @param delegate Delegate to track this request.
 */
- (void)cancelTransfer:(MEGATransfer *)transfer delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Cancel a transfer.
 *
 * When a transfer is cancelled, it will finish and will provide the error code
 * MEGAErrorTypeApiEIncomplete in [MEGATransferDelegate onTransferFinish] and
 * [MEGADelegate onTransferFinish]
 *
 * The associated request type with this request is MEGARequestTypeCancelTransfer.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest transferTag] - Returns the tag of the cancelled transfer ([MEGATransfer tag])
 *
 * @param transfer MEGATransfer object that identifies the transfer
 * You can get this object in any MEGATransferDelegate callback or any MEGADelegate callback
 * related to transfers.
 *
 */
- (void)cancelTransfer:(MEGATransfer *)transfer;

/**
 * @brief Cancel all transfers of the same type.
 *
 * The associated request type with this request is MEGARequestTypeCancelTransfers.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the first parameter
 *
 * @param type Type of transfers to cancel.
 * Valid values are:
 * - MEGATransferTypeDownload = 0
 * - MEGATransferTypeUpload = 1
 *
 * @param delegate Delegate to track this request.
 */
- (void)cancelTransfersForDirection:(NSInteger)direction delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Cancel all transfers of the same type.
 *
 * The associated request type with this request is MEGARequestTypeCancelTransfers.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the first parameter
 *
 * @param type Type of transfers to cancel.
 * Valid values are:
 * - MEGATransferTypeDownload = 0
 * - MEGATransferTypeUpload = 1
 *
 */
- (void)cancelTransfersForDirection:(NSInteger)direction;

/**
 * @brief Cancel the transfer with a specific tag
 *
 * When a transfer is cancelled, it will finish and will provide the error code
 * MEGAErrorTypeApiEIncomplete in [MEGATransferDelegate onTransferFinish:] and
 * [MEGADelegate onTransferFinish:]
 *
 * The associated request type with this request is MEGARequestTypeCancelTransfer
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest transferTag] - Returns the tag of the cancelled transfer ([MEGATransfer tag])
 *
 * @param transferTag tag that identifies the transfer
 * You can get this tag using [MEGATransfer tag]
 *
 * @param delegate MEGARequestDelegate to track this request
 */

- (void)cancelTransferByTag:(NSInteger)transferTag delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Cancel the transfer with a specific tag
 *
 * When a transfer is cancelled, it will finish and will provide the error code
 * MEGAErrorTypeApiEIncomplete in [MEGATransferDelegate onTransferFinish:] and
 * [MEGADelegate onTransferFinish:]
 *
 * The associated request type with this request is MEGARequestTypeCancelTransfer
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest transferTag] - Returns the tag of the cancelled transfer ([MEGATransfer tag])
 *
 * @param transferTag tag that identifies the transfer
 * You can get this tag using [MEGATransfer tag]
 *
 */

- (void)cancelTransferByTag:(NSInteger)transferTag;

/**
 * @brief Pause/resume all transfers.
 *
 * The associated request type with this request is MEGARequestTypePauseTransfers.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest flag] - Returns the first parameter
 *
 * @param pause YES to pause all transfers / NO to resume all transfers.
 * @param delegate Delegate to track this request.
 */
- (void)pauseTransfers:(BOOL)pause delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Pause/resume all transfers.
 *
 * The associated request type with this request is MEGARequestTypePauseTransfers.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest flag] - Returns the first parameter
 *
 * @param pause YES to pause all transfers / NO to resume all transfers.
 * @param delegate Delegate to track this request.
 */
- (void)pauseTransfers:(BOOL)pause;

/**
 * @brief Pause/resume all transfers in one direction (uploads or downloads)
 *
 * The associated request type with this request is MEGARequestTypePauseTransfers
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest flag] - Returns the first parameter
 * - [MEGARequest number] - Returns the direction of the transfers to pause/resume
 *
 * @param pause YES to pause transfers / NO to resume transfers
 * @param direction Direction of transfers to pause/resume
 * Valid values for this parameter are:
 * - MEGATransferTypeDownload = 0
 * - MEGATransferTypeUpload = 1
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)pauseTransfers:(BOOL)pause forDirection:(NSInteger)direction delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Pause/resume all transfers in one direction (uploads or downloads)
 *
 * The associated request type with this request is MEGARequestTypePauseTransfers
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest flag] - Returns the first parameter
 * - [MEGARequest number] - Returns the direction of the transfers to pause/resume
 *
 * @param pause YES to pause transfers / NO to resume transfers
 * @param direction Direction of transfers to pause/resume
 * Valid values for this parameter are:
 * - MEGATransferTypeDownload = 0
 * - MEGATransferTypeUpload = 1
 *
 */
- (void)pauseTransfers:(BOOL)pause forDirection:(NSInteger)direction;

/**
 * @brief Returns the state (paused/unpaused) of transfers
 * @param direction Direction of transfers to check
 * Valid values for this parameter are:
 * - MEGATransferTypeDownload = 0
 * - MEGATransferTypeUpload = 1
 *
 * @return YES if transfers on that direction are paused, NO otherwise
 */
- (BOOL)areTransferPausedForDirection:(NSInteger)direction;

/**
 * @brief Set the upload speed limit.
 *
 * The limit will be applied on the server side when starting a transfer. Thus the limit won't be
 * applied for already started uploads and it's applied per storage server.
 *
 * @param bpslimit -1 to automatically select the limit, 0 for no limit, otherwise the speed limit
 * in bytes per second.
 */
- (void)setUploadLimitWithBpsLimit:(NSInteger)bpsLimit;

#pragma mark - Filesystem inspection

/**
 * @brief Get the number of child nodes.
 *
 * If the node doesn't exist in MEGA or isn't a folder,
 * this function returns 0.
 *
 * This function doesn't search recursively, only returns the direct child nodes.
 *
 * @param parent Parent node.
 * @return Number of child nodes.
 */
- (NSInteger)numberChildrenForParent:(MEGANode *)parent;

/**
 * @brief Get the number of child files of a node.
 *
 * If the node doesn't exist in MEGA or isn't a folder,
 * this function returns 0.
 *
 * This function doesn't search recursively, only returns the direct child files.
 *
 * @param parent Parent node.
 * @return Number of child files.
 */
- (NSInteger)numberChildFilesForParent:(MEGANode *)parent;

/**
 * @brief Get the number of child folders of a node.
 *
 * If the node doesn't exist in MEGA or isn't a folder,
 * this function returns 0.
 *
 * This function doesn't search recursively, only returns the direct child folders.
 *
 * @param parent Parent node.
 * @return Number of child folders.
 */
- (NSInteger)numberChildFoldersForParent:(MEGANode *)parent;

/**
 * @brief Get all children of a MEGANode.
 *
 * If the parent node doesn't exist or it isn't a folder, this function
 * returns nil.
 *
 * @param parent Parent node.
 * @param order Order for the returned list.
 * Valid values for this parameter are:
 * - MEGASortOrderTypeNone = 0
 * Undefined order
 *
 * - MEGASortOrderTypeDefaultAsc = 1
 * Folders first in alphabetical order, then files in the same order
 *
 * - MEGASortOrderTypeDefaultDesc = 2
 * Files first in reverse alphabetical order, then folders in the same order
 *
 * - MEGASortOrderTypeSizeAsc = 3
 * Sort by size, ascending
 *
 * - MEGASortOrderTypeSizeDesc = 4
 * Sort by size, descending
 *
 * - MEGASortOrderTypeCreationAsc = 5
 * Sort by creation time in MEGA, ascending
 *
 * - MEGASortOrderTypeCreationDesc = 6
 * Sort by creation time in MEGA, descending
 *
 * - MEGASortOrderTypeModificationAsc = 7
 * Sort by modification time of the original file, ascending
 *
 * - MEGASortOrderTypeModificationDesc = 8
 * Sort by modification time of the original file, descending
 *
 * - MEGASortOrderTypeAlphabeticalAsc = 9
 * Sort in alphabetical order, ascending
 *
 * - MEGASortOrderTypeAlphabeticalDesc = 10
 * Sort in alphabetical order, descending
 *
 * @return List with all child MEGANode objects.
 */
- (MEGANodeList *)childrenForParent:(MEGANode *)parent order:(NSInteger)order;

/**
 * @brief Get all children of a MEGANode.
 *
 * If the parent node doesn't exist or it isn't a folder, this function
 * returns nil.
 *
 * @param parent Parent node. Sort in alphabetical order, descending
 *
 * @return List with all child MEGANode objects.
 */
- (MEGANodeList *)childrenForParent:(MEGANode *)parent;

/**
 * @brief Get the child node with the provided name.
 *
 * If the node doesn't exist, this function returns nil.
 *
 * @param parent Parent node.
 * @param name Name of the node.
 * @return The MEGANode that has the selected parent and name.
 */
- (MEGANode *)childNodeForParent:(MEGANode *)parent name:(NSString *)name;

/**
 * @brief Get the parent node of a MEGANode.
 *
 * If the node doesn't exist in the account or
 * it is a root node, this function returns nil.
 *
 * @param node MEGANode to get the parent.
 * @return The parent of the provided node.
 */
- (MEGANode *)parentNodeForNode:(MEGANode *)node;

/**
 * @brief Get the path of a MEGANode.
 *
 * If the node doesn't exist, this function returns nil.
 * You can recoved the node later using [MEGASdk nodeForPath:]
 * except if the path contains names with  '/', '\' or ':' characters.
 *
 * @param node MEGANode for which the path will be returned.
 * @return The path of the node.
 */
- (NSString *)nodePathForNode:(MEGANode *)node;

/**
 * @brief Get the MEGANode in a specific path in the MEGA account.
 *
 * The path separator character is '/'
 * The root node is /
 * The Inbox root node is //in/
 * The Rubbish root node is //bin/
 *
 * Paths with names containing '/', '\' or ':' aren't compatible
 * with this function.
 *
 * It is needed to be logged in and to have successfully completed a fetchNodes
 * request before calling this function. Otherwise, it will return nil.
 *
 * @param path Path to check.
 * @param node Base node if the path is relative.
 * @return The MEGANode object in the path, otherwise nil.
 */
- (MEGANode *)nodeForPath:(NSString *)path node:(MEGANode *)node;

/**
 * @brief Get the MEGANode in a specific path in the MEGA account.
 *
 * The path separator character is '/'
 * The root node is /
 * The Inbox root node is //in/
 * The Rubbish root node is //bin/
 *
 * Paths with names containing '/', '\' or ':' aren't compatible
 * with this function.
 *
 * It is needed to be logged in and to have successfully completed a fetchNodes
 * request before calling this function. Otherwise, it will return nil.
 *
 * @param path Path to check.
 * @return The MEGANode object in the path, otherwise nil.
 */
- (MEGANode *)nodeForPath:(NSString *)path;

/**
 * @brief Get the MEGANode that has a specific handle.
 *
 * You can get the handle of a MEGANode using [MEGANode handle]. The same handle
 * can be got in a Base64-encoded string using [MEGANode base64Handle]. Conversions
 * between these formats can be done using [MEGASdk handleForBase64Handle:] and [MEGASdk base64HandleForHandle:].
 *
 * It is needed to be logged in and to have successfully completed a fetchNodes
 * request before calling this function. Otherwise, it will return nil.
 *
 * @param handle Node handle to check.
 * @return MEGANode object with the handle, otherwise nil.
 */
- (MEGANode *)nodeForHandle:(uint64_t)handle;

/**
 * @brief Get all contacts of this MEGA account.
 *
 * @return List of MEGAUser object with all contacts of this account.
 */
- (MEGAUserList *)contacts;

/**
 * @brief Get the MEGAUser that has a specific email address.
 *
 * You can get the email of a MEGAUser using [MEGAUser email].
 *
 * @param email Email address to check.
 * @return MEGAUser that has the email address, otherwise nil.
 */
- (MEGAUser *)contactForEmail:(NSString *)email;

/**
 * @brief Get a list with all inbound sharings from one MEGAUser.
 *
 * @param user MEGAUser sharing folders with this account.
 * @return List of MEGANode objects that this user is sharing with this account.
 */
- (MEGANodeList *)inSharesForUser:(MEGAUser *)user;

/**
 * @brief Get a list with all inboud sharings.
 *
 * @return List of MEGANode objects that other users are sharing with this account.
 */
- (MEGANodeList *)inShares;

/**
 * @brief Check if a MEGANode is being shared.
 *
 * For nodes that are being shared, you can get a a list of MEGAShare
 * objects using [MEGASdk outSharesForNode:].
 *
 * @param node Node to check.
 * @return YES is the MEGANode is being shared, otherwise NO.
 */
- (BOOL)isSharedNode:(MEGANode *)node;

/**
 * @brief Get a list with all active outbound sharings
 *
 * @return List of MegaShare objects
 */
- (MEGAShareList *)outShares;

/**
 * @brief Get a list with the active outbound sharings for a MEGANode.
 *
 * If the node doesn't exist in the account, this function returns an empty list.
 *
 * @param node MEGANode to check.
 * @return List of MEGAShare objects.
 */
- (MEGAShareList *)outSharesForNode:(MEGANode *)node;

/**
 * @brief Get a Base64-encoded fingerprint for a local file.
 *
 * The fingerprint is created taking into account the modification time of the file
 * and file contents. This fingerprint can be used to get a corresponding node in MEGA
 * using [MEGASdk nodeForFingerprint:].
 *
 * If the file can't be found or can't be opened, this function returns nil.
 *
 * @param filePath Local file path.
 * @return Base64-encoded fingerprint for the file.
 */
- (NSString *)fingerprintForFilePath:(NSString *)filePath;

/**
 * @brief Get a Base64-encoded fingerprint for a node.
 *
 * If the node doesn't exist or doesn't have a fingerprint, this function returns nil.
 *
 * @param node Node for which we want to get the fingerprint.
 * @return Base64-encoded fingerprint for the file.
 */
- (NSString *)fingerprintForNode:(MEGANode *)node;

/**
 * @brief Returns a node with the provided fingerprint.
 *
 * If there isn't any node in the account with that fingerprint, this function returns nil.
 *
 * @param fingerprint Fingerprint to check.
 * @return MEGANode object with the provided fingerprint.
 */
- (MEGANode *)nodeForFingerprint:(NSString *)fingerprint;

/**
 * @brief Returns a node with the provided fingerprint.
 *
 * If there isn't any node in the account with that fingerprint, this function returns nil.
 *
 * @param fingerprint Fingerprint to check.
 * @param parent Preferred parent node
 * @return MEGANode object with the provided fingerprint.
 */
- (MEGANode *)nodeForFingerprint:(NSString *)fingerprint parent:(MEGANode *)parent;

/**
 * @brief Check if the account already has a node with the provided fingerprint.
 *
 * A fingerprint for a local file can be generated using [MEGASdk fingerprintForFilePath:].
 *
 * @param fingerprint Fingerprint to check.
 * @return YES if the account contains a node with the same fingerprint.
 */
- (BOOL)hasFingerprint:(NSString *)fingerprint;

/**
 * @brief Get the CRC of a file
 *
 * The CRC of a file is a hash of its contents.
 * If you need a more realiable method to check files, use fingerprint functions
 * ([MEGASdk fingerprintForFilePath:], [MEGASdk nodeForFingerprint:]) that also takes into
 * account the size and the modification time of the file to create the fingerprint.
 *
 * @param filePath Local file path
 * @return Base64-encoded CRC of the file
 */
- (NSString *)CRCForFilePath:(NSString *)filePath;

/**
 * @brief Get the CRC of a node
 *
 * The CRC of a node is a hash of its contents.
 * If you need a more realiable method to check files, use fingerprint functions
 * ([MEGASdk fingerprintForFilePath:], [MEGASdk nodeForFingerprint:]) that also takes into
 * account the size and the modification time of the node to create the fingerprint.
 *
 * @param node MEGANode for which we want to get the CRC
 * @return Base64-encoded CRC of the node
 */
- (NSString *)CRCForNode:(MEGANode *)node;

/**
 * @brief Returns a node with the provided CRC
 *
 * If there isn't any node in the selected folder with that CRC, this function returns nil.
 * If there are several nodes with the same CRC, anyone can be returned.
 *
 * @param crc CRC to check
 * @param parent Parent MEGANode to scan. It must be a folder.
 * @return node with the selected CRC in the selected folder, or nil
 * if it's not found.
 */

- (MEGANode *)nodeByCRC:(NSString *)crc parent:(MEGANode *)parent;

/**
 * @brief Get the access level of a MEGANode.
 * @param node MEGANode to check.
 * @return Access level of the node.
 * Valid values are:
 * - MEGAShareTypeAccessOwner
 * - MEGAShareTypeAccessFull
 * - MEGAShareTypeAccessReadWrite
 * - MEGAShareTypeAccessRead
 * - MEGAShareTypeAccessUnkown
 */
- (MEGAShareType)accessLevelForNode:(MEGANode *)node;

/**
 * @brief Check if a node has an access level.
 *
 * @param node Node to check.
 * @param level Access level to check.
 * Valid values for this parameter are:
 * - MEGAShareTypeAccessOwner
 * - MEGAShareTypeAccessFull
 * - MEGAShareTypeAccessReadWrite
 * - MEGAShareTypeAccessRead
 *
 * @return MEGAError object with the result.
 * Valid values for the error code are:
 * - MEGAErrorTypeApiOk - The node has the required access level
 * - MEGAErrorTypeApiEAccess - The node doesn't have the required access level
 * - MEGAErrorTypeApiENoent - The node doesn't exist in the account
 * - MEGAErrorTypeApiEArgs - Invalid parameters
 */
- (MEGAError *)checkAccessForNode:(MEGANode *)node level:(MEGAShareType)level;

/**
 * @brief Check if a node can be moved to a target node.
 * @param node Node to check.
 * @param target Target for the move operation.
 * @return MEGAError object with the result:
 * Valid values for the error code are:
 * - MEGAErrorTypeApiOk - The node can be moved to the target
 * - MEGAErrorTypeApiEAccess - The node can't be moved because of permissions problems
 * - MEGAErrorTypeApiECircular - The node can't be moved because that would create a circular linkage
 * - MEGAErrorTypeApiENoent - The node or the target doesn't exist in the account
 * - MEGAErrorTypeApiEArgs - Invalid parameters
 */
- (MEGAError *)checkMoveForNode:(MEGANode *)node target:(MEGANode *)target;

/**
 * @brief Search nodes containing a search string in their name.
 *
 * The search is case-insensitive.
 *
 * @param node The parent node of the tree to explore.
 * @param searchString Search string. The search is case-insensitive.
 * @param recursive YES if you want to seach recursively in the node tree.
 * NO if you want to seach in the children of the node only
 *
 * @return List of nodes that contain the desired string in their name.
 */
- (MEGANodeList *)nodeListSearchForNode:(MEGANode *)node searchString:(NSString *)searchString recursive:(BOOL)recursive;

/**
 * @brief Search nodes containing a search string in their name.
 *
 * The search is case-insensitive.
 *
 * @param node The parent node of the tree to explore.
 * @param searchString Search string. The search is case-insensitive.
 *
 * @return List of nodes that contain the desired string in their name.
 */
- (MEGANodeList *)nodeListSearchForNode:(MEGANode *)node searchString:(NSString *)searchString;

/**
 * @brief Get the size of a node tree.
 *
 * If the MEGANode is a file, this function returns the size of the file.
 * If it's a folder, this fuction returns the sum of the sizes of all nodes
 * in the node tree.
 *
 * @param node Parent node.
 * @return Size of the node tree.
 */
- (NSNumber *)sizeForNode:(MEGANode *)node;

/**
 * @brief Make a name suitable for a file name in the local filesystem
 *
 * This function escapes (%xx) forbidden characters in the local filesystem if needed.
 * You can revert this operation using [MEGASdk unescapeFsIncompatible:]
 *
 * The input string must be UTF8 encoded. The returned value will be UTF8 too.
 *
 * You take the ownership of the returned value
 *
 * @param filename Name to convert (UTF8)
 * @return Converted name (UTF8)
 */
- (NSString *)escapeFsIncompatible:(NSString *)name;

/**
 * @brief Unescape a file name escaped with [MEGASdk escapeFsIncompatible:]
 *
 * The input string must be UTF8 encoded. The returned value will be UTF8 too.
 *
 * @param name Escaped name to convert (UTF8)
 * @return Converted name (UTF8)
 */
- (NSString *)unescapeFsIncompatible:(NSString *)localName;

- (void)changeApiUrl:(NSString *)apiURL disablepkp:(BOOL)disablepkp;

/**
 * @brief Create a thumbnail for an image
 * @param imagePath Image path
 * @param destinationPath Destination path for the thumbnail (including the file name)
 * @return YES if the thumbnail was successfully created, otherwise NO.
 */
- (BOOL)createThumbnail:(NSString *)imagePath destinatioPath:(NSString *)destinationPath;

/**
 * @brief Create a preview for an image
 * @param imagePath Image path
 * @param destinationPath Destination path for the thumbnail (including the file name)
 * @return YES if the preview was successfully created, otherwise NO.
 */
- (BOOL)createPreview:(NSString *)imagePath destinatioPath:(NSString *)destinationPath;

#pragma mark - Debug log messages

/**
 * @brief Set the active log level
 *
 * This function sets the log level of the logging system. If you set a log delegate using
 * [MEGASdk setLoggerObject:], you will receive logs with the same or a lower level than
 * the one passed to this function.
 *
 * @param logLevel Active log level.
 *
 * These are the valid values for this parameter:
 * - MEGALogLevelFatal = 0
 * - MEGALogLevelError = 1
 * - MEGALogLevelWarning = 2
 * - MEGALogLevelInfo = 3
 * - MEGALogLevelDebug = 4
 * - MEGALogLevelMax = 5
 */
+ (void)setLogLevel:(MEGALogLevel)logLevel;

/**
 * @brief Set a MEGALogger implementation to receive SDK logs.
 *
 * Logs received by this objects depends on the active log level.
 * By default, it is MEGALogLevelInfo. You can change it
 * using [MEGASdk setLogLevel].
 *
 * @param delegate Delegate implementation.
 */

+ (void)setLogObject:(id<MEGALoggerDelegate>)delegate;

/**
 * @brief Send a log to the logging system
 *
 * This log will be received by the active logger object ([MEGASdk setLogObject]) if
 * the log level is the same or lower than the active log level ([MEGASdk setLogLevel])
 *
 * The third and the fouth parameget are optional. You may want to use  __FILE__ and __LINE__
 * to complete them.
 *
 * @param logLevel Log level for this message
 * @param message Message for the logging system
 * @param filename Origin of the log message
 * @param line Line of code where this message was generated
 */
+ (void)logWithLevel:(MEGALogLevel)logLevel message:(NSString *)message filename:(NSString *)filename line:(NSInteger)line;

/**
 * @brief Send a log to the logging system
 *
 * This log will be received by the active logger object ([MEGASdk setLogObject]) if
 * the log level is the same or lower than the active log level ([MEGASdk setLogLevel])
 *
 * The third and the fouth parameget are optional. You may want to use  __FILE__ and __LINE__
 * to complete them.
 *
 * @param logLevel Log level for this message
 * @param message Message for the logging system
 * @param filename Origin of the log message
 */
+ (void)logWithLevel:(MEGALogLevel)logLevel message:(NSString *)message filename:(NSString *)filename;

/**
 * @brief Send a log to the logging system
 *
 * This log will be received by the active logger object ([MEGASdk setLogObject]) if
 * the log level is the same or lower than the active log level ([MEGASdk setLogLevel])
 *
 * The third and the fouth parameget are optional. You may want to use  __FILE__ and __LINE__
 * to complete them.
 *
 * @param logLevel Log level for this message
 * @param message Message for the logging system
 */
+ (void)logWithLevel:(MEGALogLevel)logLevel message:(NSString *)message;

@end
