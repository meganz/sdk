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
#import <AssetsLibrary/AssetsLibrary.h>

#import "MEGAAccountDetails.h"
#import "MEGAAchievementsDetails.h"
#import "MEGAChildrenLists.h"
#import "MEGAContactRequest.h"
#import "MEGAContactRequestList.h"
#import "MEGADelegate.h"
#import "MEGAError.h"
#import "MEGAEvent.h"
#import "MEGAGlobalDelegate.h"
#import "MEGANodeList.h"
#import "MEGANode.h"
#import "MEGALoggerDelegate.h"
#import "MEGAPricing.h"
#import "MEGARecentActionBucket.h"
#import "MEGARequest.h"
#import "MEGARequestDelegate.h"
#import "MEGAShareList.h"
#import "MEGATransfer.h"
#import "MEGATransferDelegate.h"
#import "MEGATransferList.h"
#import "MEGATreeProcessorDelegate.h"
#import "MEGAUser.h"
#import "MEGAUserList.h"
#import "MEGABackgroundMediaUpload.h"
#import "MEGACancelToken.h"
#import "MEGAPushNotificationSettings.h"

NS_ASSUME_NONNULL_BEGIN

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
    MEGASortOrderTypeAlphabeticalDesc,
    MEGASortOrderTypePhotoAsc,
    MEGASortOrderTypePhotoDesc,
    MEGASortOrderTypeVideoAsc,
    MEGASortOrderTypeVideoDesc,
    MEGASortOrderTypeLinkCreationAsc,
    MEGASortOrderTypeLinkCreationDesc
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
    MEGAUserAttributeAvatar                  = 0, // public - char array
    MEGAUserAttributeFirstname               = 1, // public - char array
    MEGAUserAttributeLastname                = 2, // public - char array
    MEGAUserAttributeAuthRing                = 3, // private - byte array
    MEGAUserAttributeLastInteraction         = 4, // private - byte array
    MEGAUserAttributeED25519PublicKey        = 5, // public - byte array
    MEGAUserAttributeCU25519PublicKey        = 6, // public - byte array
    MEGAUserAttributeKeyring                 = 7, // private - byte array
    MEGAUserAttributeSigRsaPublicKey         = 8, // public - byte array
    MEGAUserAttributeSigCU255PublicKey       = 9, // public - byte array
    MEGAUserAttributeLanguage                = 14, // private - char array
    MEGAUserAttributePwdReminder             = 15, // private - char array
    MEGAUserAttributeDisableVersions         = 16, // private - byte array
    MEGAUserAttributeContactLinkVerification = 17, // private - byte array
    MEGAUserAttributeRichPreviews            = 18, // private - byte array
    MEGAUserAttributeRubbishTime             = 19, // private - byte array
    MEGAUserAttributeLastPSA                 = 20, // private - char array
    MEGAUserAttributeStorageState            = 21, // private - char array
    MEGAUserAttributeGeolocation             = 22, // private - byte array
    MEGAUserAttributeCameraUploadsFolder     = 23, // private - byte array
    MEGAUserAttributeMyChatFilesFolder       = 24, // private - byte array
    MEGAUserAttributePushSettings            = 25, // private - char array
    MEGAUserAttributeAlias                   = 27 // private - char array
};

typedef NS_ENUM(NSInteger, MEGANodeAttribute) {
    MEGANodeAttributeDuration       = 0,
    MEGANodeAttributeCoordinates    = 1
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
    MEGAPaymentMethodCentili      = 9,
    MEGAPaymentMethodWindowsStore = 13
};

typedef NS_ENUM(NSInteger, HTTPServer) {
    HTTPServerDenyAll                = -1,
    HTTPServerAllowAll               = 0,
    HTTPServerAllowCreatedLocalLinks = 1,
    HTTPServerAllowLastLocalLink     = 2
};

typedef NS_ENUM(NSUInteger, PushNotificationTokenType) {
    PushNotificationTokenTypeAndroid = 1,
    PushNotificationTokenTypeiOSVoIP = 2,
    PushNotificationTokenTypeiOSStandard = 3
};

typedef NS_ENUM(NSUInteger, PasswordStrength) {
    PasswordStrengthVeryWeak = 0,
    PasswordStrengthWeak = 1,
    PasswordStrengthMedium = 2,
    PasswordStrengthGood = 3,
    PasswordStrengthStrong = 4
};

typedef NS_ENUM(NSUInteger, Retry) {
    RetryNone = 0,
    RetryConnectivity = 1,
    RetryServersBusy = 2,
    RetryApiLock = 3,
    RetryRateLimit = 4,
    RetryLocalLock = 5,
    RetryUnknown = 6
};

typedef NS_ENUM(NSInteger, KeepMeAlive) {
    KeepMeAliveCameraUploads = 0
};

typedef NS_ENUM(NSUInteger, StorageState) {
    StorageStateGreen = 0,
    StorageStateOrange = 1,
    StorageStateRed = 2,
    StorageStateChange = 3
};

typedef NS_ENUM(NSInteger, SMSState) {
    SMSStateNotAllowed = 0,
    SMSStateOnlyUnblock = 1,
    SMSStateOptInAndUnblock = 2,
};

typedef NS_ENUM(NSInteger, AccountSuspensionType) {
    AccountSuspensionTypeNone = 0, // The account is not blocked
    AccountSuspensionTypeNonCopyright = 200, // suspension for any type of suspension, but copyright suspension
    AccountSuspensionTypeCopyright = 300, // suspension only for multiple copyright violations
    AccountSuspensionTypeBusinessDisabled = 400, // the subuser of a business account has been disabled
    AccountSuspensionTypeBusinessRemoved = 401, // the subuser of a business account has been removed
    AccountSuspensionTypeSMSVerification = 500, // The account needs to be verified by an SMS code.
    AccountSuspensionTypeEmailVerification = 700, // The account needs to be verified by password change trough email.
};

typedef NS_ENUM(NSInteger, BusinessStatus) {
    BusinessStatusExpired = -1,
    BusinessStatusInactive = 0, // no business subscription
    BusinessStatusActive = 1,
    BusinessStatusGracePeriod = 2
};

typedef NS_ENUM(NSInteger, AffiliateType) {
    AffiliateTypeInvalid = 0, // legacy mode
    AffiliateTypeId = 1,
    AffiliateTypeFileFolder = 2,
    AffiliateTypeChat = 3,
    AffiliateTypeContact = 4
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
@property (readonly, nonatomic, nullable) NSString *myEmail;

/**
 * @brief Root node of the account.
 *
 * If you haven't successfully called [MEGASdk fetchNodes] before,
 * this property is nil.
 *
 */
@property (readonly, nonatomic, nullable) MEGANode *rootNode;

/**
 * @brief Rubbish node of the account.
 *
 * If you haven't successfully called [MEGASdk fetchNodes] before,
 * this property is nil.
 *
 */
@property (readonly, nonatomic, nullable) MEGANode *rubbishNode;

/**
 * @brief Inbox node of the account.
 *
 * If you haven't successfully called [MEGASdk fetchNodes] before,
 * this property is nil.
 *
 */
@property (readonly, nonatomic, nullable) MEGANode *inboxNode;

/**
 * @brief All active transfers.
 */
@property (readonly, nonatomic) MEGATransferList *transfers;

/**
 * @brief Download active transfers.
 */
@property (readonly, nonatomic) MEGATransferList *downloadTransfers;

/**
 * @brief Upload active transfers.
 */
@property (readonly, nonatomic) MEGATransferList *uploadTransfers;

/**
 * @brief Check if the SDK is waiting to complete a request and get the reason
 * @return State of SDK.
 *
 * Valid values are:
 * - RetryNone = 0
 * SDK is not waiting for the server to complete a request
 *
 * - RetryConnectivity = 1
 * SDK is waiting for the server to complete a request due to connectivity issues
 *
 * - RetryServersBusy = 2
 * SDK is waiting for the server to complete a request due to a HTTP error 500
 *
 * - RetryApiLock = 3
 * SDK is waiting for the server to complete a request due to an API lock (API error -3)
 *
 * - RetryRateLimit = 4,
 * SDK is waiting for the server to complete a request due to a rate limit (API error -4)
 *
 * - RetryLocalLock = 5
 * SDK is waiting for a local locked file
 *
 * - RetryUnknown = 6
 * SDK is waiting for the server to complete a request with unknown reason
 *
 */
@property (readonly, nonatomic) Retry waiting;

/**
 * @brief Total downloaded bytes since the creation of the MEGASdk object.
 *
 * @deprecated Property related to statistics will be reviewed in future updates to
 * provide more data and avoid race conditions. They could change or be removed in the current form.
 */
@property (readonly, nonatomic) NSNumber *totalsDownloadedBytes __attribute__((deprecated("They could change or be removed in the current form.")));

/**
 * @brief Total uploaded bytes since the creation of the MEGASdk object.
 *
 * @deprecated Property related to statistics will be reviewed in future updates to
 * provide more data and avoid race conditions. They could change or be removed in the current form.
 *
 */
@property (readonly, nonatomic) NSNumber *totalsUploadedBytes __attribute__((deprecated("They could change or be removed in the current form.")));

/**
 * @brief The total number of nodes in the account
 */
@property (readonly, nonatomic) NSUInteger totalNodes;

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
@property (readonly, nonatomic, nullable) NSString *masterKey;

/**
 * @brief User-Agent header used by the SDK
 *
 * The User-Agent used by the SDK
 */
@property (readonly, nonatomic, nullable) NSString *userAgent;

/**
 * @brief MEGAUser of the currently open account
 *
 * If the MEGASdk object isn't logged in, this property is nil.
 */
@property (readonly, nonatomic, nullable) MEGAUser *myUser;

/**
 * @brief Returns whether MEGA Achievements are enabled for the open account
 * YES if enabled, NO otherwise.
 */
@property (readonly, nonatomic, getter=isAchievementsEnabled) BOOL achievementsEnabled;

#pragma mark - Business

/**
 * @brief Returns YES if it's a business account, otherwise NO.
 *
 * @note This function must be called only if we have received the callback
 * [MEGAGlobalDelegate onEvent:event:] and the callback [MEGADelegate onEvent:event:]
 * with the event type EventBusinessStatus
 *
 */
@property (readonly, nonatomic, getter=isBusinessAccount) BOOL businessAccount;

/**
 * @brief Returns YES if it's a master account, NO if it's a sub-user account.
 *
 * When a business account is a sub-user, not the master, some user actions will be blocked.
 * In result, the API will return the error code MEGAErrorTypeApiEMasterOnly. Some examples of
 * requests that may fail with this error are:
 *  - [MEGASdk cancelAccount]
 *  - [MEGASdk changeEmail]
 *  - [MEGASdk remove]
 *  - [MEGASdk removeVersion]
 *
 * @note This function must be called only if we have received the callback
 * [MEGAGlobalDelegate onEvent:event:] and the callback [MEGADelegate onEvent:event:]
 * with the event type EventBusinessStatus
 *
 */
@property (readonly, nonatomic, getter=isMasterBusinessAccount) BOOL masterBusinessAccount;

/**
 * @brief Returns YES if it is an active business account, otherwise NO.
 *
 * When a business account is not active, some user actions will be blocked. In result, the API
 * will return the error code MEGAErrorTypeApiEBusinessPastDue. Some examples of requests
 * that may fail with this error are:
 *  - [MEGASdk startDownload]
 *  - [MEGASdk startUpload]
 *  - [MEGASdk copyNode]
 *  - [MEGASdk shareNode]
 *  - [MEGASdk cleanRubbishBin]
 *
 * @note This function must be called only if we have received the callback
 * [MEGAGlobalDelegate onEvent:event:] and the callback [MEGADelegate onEvent:event:]
 * with the event type EventBusinessStatus
 */
@property (readonly, nonatomic, getter=isBusinessAccountActive) BOOL businessAccountActive;

/**
 * @brief Get the status of a business account.
 *
 * @note This function must be called only if we have received the callback
 * [MEGAGlobalDelegate onEvent:event:] and the callback [MEGADelegate onEvent:event:]
 * with the event type EventBusinessStatus
 *
 * @return Returns the business account status, possible values:
 *      BusinessStatusExpired = -1
 *      BusinessStatusInactive = 0
 *      BusinessStatusActive = 1
 *      BusinessStatusGracePeriod = 2
 */
@property (readonly, nonatomic) BusinessStatus businessStatus;

/**
 * @brief The number of unread user alerts for the logged in user
 */
@property (readonly, nonatomic) NSInteger numUnreadUserAlerts;

/**
 * @brief The time (in seconds) during which transfers will be stopped due to a bandwidth overquota, otherwise 0
 */
@property (readonly, nonatomic) long long bandwidthOverquotaDelay;

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
- (nullable instancetype)initWithAppKey:(NSString *)appKey userAgent:(nullable NSString *)userAgent;

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
- (nullable instancetype)initWithAppKey:(NSString *)appKey userAgent:(nullable NSString *)userAgent basePath:(nullable NSString *)basePath;

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


/**
 * @brief Add a MEGALoggerDelegate implementation to receive SDK logs
 *
 * Logs received by this objects depends on the active log level.
 * By default, it is MEGALogLevelInfo. You can change it
 * using [MEGASdk setLogLevel].
 *
 * You can remove the existing logger by using [MEGASdk removeLoggerObject:].
 *
 * @param delegate Delegate implementation
 */
- (void)addLoggerDelegate:(id<MEGALoggerDelegate>)delegate;

/**
 * @brief Remove a MEGALoggerDelegate implementation to stop receiving SDK logs
 *
 * If the logger was registered in the past, it will stop receiving log
 * messages after the call to this function.
 *
 * @param delegate Previously registered MegaLogger implementation
 */
- (void)removeLoggerDelegate:(id<MEGALoggerDelegate>)delegate;

#pragma mark - Utils

/**
 * @brief Generates a hash based in the provided private key and email.
 *
 * This is a time consuming operation (specially for low-end mobile devices). Since the resulting key is
 * required to log in, this function allows to do this step in a separate function. You should run this function
 * in a background thread, to prevent UI hangs. The resulting key can be used in 
 * [MEGASdk fastLoginWithEmail:stringHash:base64pwKey:].
 *
 * @param base64pwkey Private key returned by [MEGARequest privateKey] in the onRequestFinish callback of createAccount
 * @param email Email to create the hash
 * @return Base64-encoded hash
 *
 * @deprecated This function is only useful for old accounts. Once enabled the new registration logic,
 * this function will return an empty string for new accounts and will be removed few time after.
 */
- (nullable NSString *)hashForBase64pwkey:(NSString *)base64pwkey email:(NSString *)email __attribute__((deprecated("This function will return an empty string for new accounts and will be removed few time after")));

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
 * @brief Converts a Base64-encoded user handle to a MegaHandle
 *
 * You can revert this operation using [MEGASdk base64handleForUserHandle:].
 *
 * @param base64UserHandle Base64-encoded user handle
 * @return User handle
 */
+ (uint64_t)handleForBase64UserHandle:(NSString *)base64UserHandle;

/**
 * @brief Converts the handle of a node to a Base64-encoded NSString
 *
 * You take the ownership of the returned value
 * You can revert this operation using [MEGASdk handleForBase64Handle:]
 *
 * @param handle Node handle to be converted
 * @return Base64-encoded node handle
 */
+ (nullable NSString *)base64HandleForHandle:(uint64_t)handle;

/**
 * @brief Converts the handle of a user to a Base64-encoded string
 *
 * @param userhandle User handle to be converted
 * @return Base64-encoded user handle
 */
+ (nullable NSString *)base64HandleForUserHandle:(uint64_t)userhandle;

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

/**
 * @brief Check if server-side Rubbish Bin autopurging is enabled for the current account
 * @return YES if this feature is enabled. Otherwise NO.
 */
- (BOOL)serverSideRubbishBinAutopurgeEnabled;

/**
 * @brief Check if the account has VOIP push enabled
 * @return YES if this feature is enabled. Otherwise NO.
 */
- (BOOL)appleVoipPushEnabled;

/* This function creates a new session for the link so logging out in the web client won't log out
* the current session.
*
* The associated request type with this request is MEGARequestTypeGetSessionTransferUrl
* Valid data in the MEGARequest object received in onRequestFinish when the error code
* is MEGAErrorTypeApiOk:
* - [MEGARequest link] - URL to open the desired page with the same account
*
* @param url URL inside https://mega.nz/# that we want to open with the current session
*
* For example, if you want to open https://mega.nz/#pro, the parameter of this function should be "pro".
*
* @param delegate MEGARequestDelegate to track this request
*/
- (void)getSessionTransferURL:(NSString *)path delegate:(id<MEGARequestDelegate>)delegate;

/* This function creates a new session for the link so logging out in the web client won't log out
* the current session.
*
* The associated request type with this request is MEGARequestTypeGetSessionTransferUrl
* Valid data in the MEGARequest object received in onRequestFinish when the error code
* is MEGAErrorTypeApiOk:
* - [MEGARequest link] - URL to open the desired page with the same account
*
* @param url URL inside https://mega.nz/# that we want to open with the current session
*
* For example, if you want to open https://mega.nz/#pro, the parameter of this function should be "pro".
*/
- (void)getSessionTransferURL:(NSString *)path;

#pragma mark - Login Requests

/**
 * @brief Check if multi-factor authentication can be enabled for the current account.
 *
 * It's needed to be logged into an account and with the nodes loaded (login + fetchNodes) before
 * using this function. Otherwise it will always return NO.
 *
 * @return YES if multi-factor authentication can be enabled for the current account, otherwise NO.
 */
- (BOOL)multiFactorAuthAvailable;

/**
 * @brief Check if multi-factor authentication is enabled for an account
 *
 * The associated request type with this request is MEGARequestTypeMultiFactorAuthCheck
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email sent in the first parameter
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest flag] - Returns YES if multi-factor authentication is enabled or NO if it's disabled.
 *
 * @param email Email to check
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)multiFactorAuthCheckWithEmail:(NSString *)email delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Check if multi-factor authentication is enabled for an account
 *
 * The associated request type with this request is MEGARequestTypeMultiFactorAuthCheck
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email sent in the first parameter
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest flag] - Returns YES if multi-factor authentication is enabled or NO if it's disabled.
 *
 * @param email Email to check
 */
- (void)multiFactorAuthCheckWithEmail:(NSString *)email;

/**
 * @brief Get the secret code of the account to enable multi-factor authentication
 * The MEGASdk object must be logged into an account to successfully use this function.
 *
 * The associated request type with this request is MEGARequestTypeMultiFactorAuthGet
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest text] - Returns the Base32 secret code needed to configure multi-factor authentication.
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)multiFactorAuthGetCodeWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get the secret code of the account to enable multi-factor authentication
 * The MEGASdk object must be logged into an account to successfully use this function.
 *
 * The associated request type with this request is MEGARequestTypeMultiFactorAuthGet
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest text] - Returns the Base32 secret code needed to configure multi-factor authentication.
 *
 */
- (void)multiFactorAuthGetCode;

/**
 * @brief Enable multi-factor authentication for the account
 * The MEGASdk object must be logged into an account to successfully use this function.
 *
 * The associated request type with this request is MEGARequestTypeMultiFactorAuthSet
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest flag] - Returns YES
 * - [MEGARequest password] - Returns the pin sent in the first parameter
 *
 * @param pin Valid pin code for multi-factor authentication
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)multiFactorAuthEnableWithPin:(NSString *)pin delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Enable multi-factor authentication for the account
 * The MEGASdk object must be logged into an account to successfully use this function.
 *
 * The associated request type with this request is MEGARequestTypeMultiFactorAuthSet
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest flag] - Returns YES
 * - [MEGARequest password] - Returns the pin sent in the first parameter
 *
 * @param pin Valid pin code for multi-factor authentication
 */
- (void)multiFactorAuthEnableWithPin:(NSString *)pin;

/**
 * @brief Disable multi-factor authentication for the account
 * The MEGASdk object must be logged into an account to successfully use this function.
 *
 * The associated request type with this request is MEGARequestTypeMultiFactorAuthSet
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest flag] - Returns NO
 * - [MEGARequest password] - Returns the pin sent in the first parameter
 *
 * @param pin Valid pin code for multi-factor authentication
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)multiFactorAuthDisableWithPin:(NSString *)pin delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Disable multi-factor authentication for the account
 * The MEGASdk object must be logged into an account to successfully use this function.
 *
 * The associated request type with this request is MEGARequestTypeMultiFactorAuthSet
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest flag] - Returns NO
 * - [MEGARequest password] - Returns the pin sent in the first parameter
 *
 * @param pin Valid pin code for multi-factor authentication
 */
- (void)multiFactorAuthDisableWithPin:(NSString *)pin;

/**
 * @brief Log in to a MEGA account with multi-factor authentication enabled
 *
 * The associated request type with this request is MEGARequestTypeLogin.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the first parameter
 * - [MEGARequest password] - Returns the second parameter
 * - [MEGARequest text] - Returns the third parameter
 *
 * If the email/password aren't valid the error code provided in onRequestFinish is
 * MEGAErrorTypeApiENoent.
 *
 * @param email Email of the user
 * @param password Password
 * @param pin Pin code for multi-factor authentication
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)multiFactorAuthLoginWithEmail:(NSString *)email password:(NSString *)password pin:(NSString *)pin delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Log in to a MEGA account with multi-factor authentication enabled
 *
 * The associated request type with this request is MEGARequestTypeLogin.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the first parameter
 * - [MEGARequest password] - Returns the second parameter
 * - [MEGARequest text] - Returns the third parameter
 *
 * If the email/password aren't valid the error code provided in onRequestFinish is
 * MEGAErrorTypeApiENoent.
 *
 * @param email Email of the user
 * @param password Password
 * @param pin Pin code for multi-factor authentication
 */
- (void)multiFactorAuthLoginWithEmail:(NSString *)email password:(NSString *)password pin:(NSString *)pin;

/**
 * @brief Change the password of a MEGA account with multi-factor authentication enabled
 *
 * The associated request type with this request is MEGARequestTypeChangePassword
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest password] - Returns the old password (if it was passed as parameter)
 * - [MEGARequest newPassword] - Returns the new password
 * - [MEGARequest text] - Returns the pin code for multi-factor authentication
 *
 * @param oldPassword Old password (optional, it can be nil to not check the old password)
 * @param newPassword New password
 * @param pin Pin code for multi-factor authentication
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)multiFactorAuthChangePassword:(nullable NSString *)oldPassword newPassword:(NSString *)newPassword pin:(NSString *)pin delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Change the password of a MEGA account with multi-factor authentication enabled
 *
 * The associated request type with this request is MEGARequestTypeChangePassword
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest password] - Returns the old password (if it was passed as parameter)
 * - [MEGARequest newPassword] - Returns the new password
 * - [MEGARequest text] - Returns the pin code for multi-factor authentication
 *
 * @param oldPassword Old password (optional, it can be nil to not check the old password)
 * @param newPassword New password
 * @param pin Pin code for multi-factor authentication
 */
- (void)multiFactorAuthChangePassword:(nullable NSString *)oldPassword newPassword:(NSString *)newPassword pin:(NSString *)pin;

/**
 * @brief Initialize the change of the email address associated to an account with multi-factor authentication enabled.
 *
 * The associated request type with this request is MEGARequestTypeGetChangeEmailLink.
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest email] - Returns the email for the account
 * - [MEGARequest text] - Returns the pin code for multi-factor authentication
 *
 * If this request succeeds, a change-email link will be sent to the specified email address.
 * If no user is logged in, you will get the error code MEGAErrorTypeApiEAccess in onRequestFinish().
 *
 * If the MEGA account is a sub-user business account, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEMasterOnly.
 *
 * @param email The new email to be associated to the account.
 * @param pin Pin code for multi-factor authentication
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)multiFactorAuthChangeEmail:(NSString *)email pin:(NSString *)pin delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Initialize the change of the email address associated to an account with multi-factor authentication enabled.
 *
 * The associated request type with this request is MEGARequestTypeGetChangeEmailLink.
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest email] - Returns the email for the account
 * - [MEGARequest text] - Returns the pin code for multi-factor authentication
 *
 * If this request succeeds, a change-email link will be sent to the specified email address.
 * If no user is logged in, you will get the error code MEGAErrorTypeApiEAccess in onRequestFinish().
 *
 * If the MEGA account is a sub-user business account, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEMasterOnly.
 *
 * @param email The new email to be associated to the account.
 * @param pin Pin code for multi-factor authentication
 */
- (void)multiFactorAuthChangeEmail:(NSString *)email pin:(NSString *)pin;

/**
 * @brief Initialize the cancellation of an account.
 *
 * The associated request type with this request is MEGARequestTypeGetCancelLink.
 *
 * If this request succeeds, a cancellation link will be sent to the email address of the user.
 * If no user is logged in, you will get the error code MEGAErrorTypeApiEAccess in onRequestFinish().
 *
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest text] - Returns the pin code for multi-factor authentication
 *
 * If the MEGA account is a sub-user business account, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEMasterOnly.
 *
 * @see [MEGASdk confirmCancelAccountWithLink:password:]
 *
 * @param pin Pin code for multi-factor authentication
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)multiFactorAuthCancelAccountWithPin:(NSString *)pin delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Initialize the cancellation of an account.
 *
 * The associated request type with this request is MEGARequestTypeGetCancelLink.
 *
 * If this request succeeds, a cancellation link will be sent to the email address of the user.
 * If no user is logged in, you will get the error code MEGAErrorTypeApiEAccess in onRequestFinish().
 *
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest text] - Returns the pin code for multi-factor authentication
 *
 * If the MEGA account is a sub-user business account, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEMasterOnly.
 *
 * @see [MEGASdk confirmCancelAccountWithLink:password:]
 *
 * @param pin Pin code for multi-factor authentication
 */
- (void)multiFactorAuthCancelAccountWithPin:(NSString *)pin;

/**
 * @brief Fetch details related to time zones and the current default
 *
 * The associated request type with this request is MEGARequestTypeFetchTimeZone.
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest megaTimeZoneDetails] - Returns details about timezones and the current default
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)fetchTimeZoneWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Fetch details related to time zones and the current default
 *
 * The associated request type with this request is MEGARequestTypeFetchTimeZone.
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest megaTimeZoneDetails] - Returns details about timezones and the current default
 *
 */
- (void)fetchTimeZone;

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
 * @param base64pwKey Private key calculated using [MEGASdk base64PwKeyWithPassword:].
 * @param delegate Delegate to track this request.
 *
 * @deprecated The parameter stringHash is no longer for new accounts so this function will be replaced by another
 * one soon. Please use [MEGASdk loginWithEmail:password:delegate:] or [MEGASdk fastLoginWithSession:delegate]
 * instead when possible.
 */
- (void)fastLoginWithEmail:(NSString *)email stringHash:(NSString *)stringHash base64pwKey:(NSString *)base64pwKey delegate:(id<MEGARequestDelegate>)delegate __attribute__((deprecated("The parameter stringHash is no longer for new accounts so this function will be replaced by another one soon. Please use [MEGASdk loginWithEmail:password:delegate:] or [MEGASdk fastLoginWithSession:delegate:] instead when possible.")));

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
 * @param base64pwKey Private key calculated using [MEGASdk base64PwKeyWithPassword:].
 *
 * @deprecated The parameter stringHash is no longer for new accounts so this function will be replaced by another
 * one soon. Please use [MEGASdk loginWithEmail:password:] or [MEGASdk fastLoginWithSession:] instead when possible.
 */
- (void)fastLoginWithEmail:(NSString *)email stringHash:(NSString *)stringHash base64pwKey:(NSString *)base64pwKey __attribute__((deprecated("The parameter stringHash is no longer for new accounts so this function will be replaced by another one soon. Please use [MEGASdk loginWithEmail:password:] or [MEGASdk fastLoginWithSession:] instead when possible.")));

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
- (nullable NSString *)dumpSession;

/**
 * @brief Returns the current sequence number
 *
 * The sequence number indicates the state of a MEGA account known by the SDK.
 * When external changes are received via actionpackets, the sequence number is
 * updated and changes are commited to the local cache.
 *
 * @return The current sequence number
*/
- (nullable NSString *)sequenceNumber;

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

/**
 * @brief Logout of the MEGA account without invalidating the session
 *
 * The associated request type with this request is MEGARequestTypeLogout
 *
 * @param delegate Delegate to track this request.
 */
- (void)localLogoutWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Logout of the MEGA account without invalidating the session
 *
 * The associated request type with this request is MEGARequestTypeLogout
 *
 */
- (void)localLogout;

/**
 * @brief Invalidate the existing cache and create a fresh one
 */
- (void)invalidateCache;

/**
 * @brief Estimate the strength of a password
 *
 * Possible return values are:
 * - PasswordStrengthVeryWeak = 0
 * - PasswordStrengthWeak = 1
 * - PasswordStrengthMedium = 2
 * - PasswordStrengthGood = 3
 * - PasswordStrengthStrong = 4
 *
 * @param password Password to check
 * @return Estimated strength of the password
 */
- (PasswordStrength)passwordStrength:(NSString *)password;

/**
 * @brief Check if the password is correct for the current account
 * @param password Password to check
 * @return YES if the password is correct for the current account, otherwise NO.
 */
- (BOOL)checkPassword:(NSString *)password;

/**
 * @brief Returns the credentials of the currently open account
 *
 * If the MEGASdk object isn't logged in or there's no signing key available,
 * this function returns nil
 *
 * @return Fingerprint of the signing key of the current account
 */
- (NSString *)myCredentials;

/**
 * Returns the credentials of a given user
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns MEGAUserAttributeED25519PublicKey
 * - [MEGARequest flag] - Returns YES
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest password] - Returns the credentials in hexadecimal format
 *
 * @param user MEGAUser of the contact (@see [MEGASDK contactForEmail:]) to get the fingerprint
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)getUserCredentials:(MEGAUser *)user delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Checks if credentials are verified for the given user
 *
 * @param user MEGAUser of the contact whose credentiasl want to be checked
 * @return YES if verified, NO otherwise
 */
- (BOOL)areCredentialsVerifiedOfUser:(MEGAUser *)user;

/**
 * @brief Verify credentials of a given user
 *
 * This function allow to tag credentials of a user as verified. It should be called when the
 * logged in user compares the fingerprint of the user (provided by an independent and secure
 * method) with the fingerprint shown by the app (@see [MEGASDK getUserCredentials:]).
 *
 * The associated request type with this request is MEGARequestTypeVerifyCredentials
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns userhandle
 *
 * @param user MEGAUser of the contact whose credentials want to be verified
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)verifyCredentialsOfUser:(MEGAUser *)user delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Reset credentials of a given user
 *
 * Call this function to forget the existing authentication of keys and signatures for a given
 * user. A full reload of the account will start the authentication process again.
 *
 * The associated request type with this request is MEGARequestTypeVerifyCredentials
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest  nodeHandle] - Returns userhandle
 * - [MEGARequest flag] - Returns YES
 *
 * @param user MEGAUser of the contact whose credentials want to be reset
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)resetCredentialsOfUser:(MEGAUser *)user delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Reset credentials of a given user
 *
 * Call this function to forget the existing authentication of keys and signatures for a given
 * user. A full reload of the account will start the authentication process again.
 *
 * The associated request type with this request is MEGARequestTypeVerifyCredentials
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest  nodeHandle] - Returns userhandle
 * - [MEGARequest flag] - Returns YES
 *
 * @param user MEGAUser of the contact whose credentials want to be reset
 */
- (void)resetCredentialsOfUser:(MEGAUser *)user;

#pragma mark - Create account and confirm account Requests

/**
 * @brief Initialize the creation of a new MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeCreateAccount.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email for the account
 * - [MEGARequest password] - Returns the password for the account
 * - [MEGARequest name] - Returns the firstname of the user
 * - [MEGARequest text] - Returns the lastname of the user
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest sessionKey] - Returns the session id to resume the process
 *
 * If this request succeed, a new ephemeral session will be created for the new user
 * and a confirmation email will be sent to the specified email address. The app may
 * resume the create-account process by using [MEGASdk resumeCreateAccountWithSessionId:].
 *
 * If an account with the same email already exists, you will get the error code
 * MEGAErrorTypeApiEExist in onRequestFinish
 *
 * @param email Email for the account
 * @param password Password for the account
 * @param firstname Firstname of the user
 * @param lastname Lastname of the user
 * @param delegate Delegate to track this request.
 */
- (void)createAccountWithEmail:(NSString *)email password:(NSString *)password firstname:(NSString *)firstname lastname:(NSString *)lastname delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Initialize the creation of a new MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeCreateAccount.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email for the account
 * - [MEGARequest password] - Returns the password for the account
 * - [MEGARequest name] - Returns the firstname of the user
 * - [MEGARequest text] - Returns the lastname of the user
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest sessionKey] - Returns the session id to resume the process
 *
 * If this request succeed, a new ephemeral session will be created for the new user
 * and a confirmation email will be sent to the specified email address. The app may
 * resume the create-account process by using [MEGASdk resumeCreateAccountWithSessionId:].
 *
 * If an account with the same email already exists, you will get the error code
 * MEGAErrorTypeApiEExist in onRequestFinish
 *
 * @param email Email for the account
 * @param password Password for the account
 * @param firstname Firstname of the user
 * @param lastname Lastname of the user
 */
- (void)createAccountWithEmail:(NSString *)email password:(NSString *)password firstname:(NSString *)firstname lastname:(NSString *)lastname;

/**
 * @brief Initialize the creation of a new MEGA account, with firstname and lastname
 *
 * The associated request type with this request is MEGARequestTypeCreateAccount.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email for the account
 * - [MEGARequest password] - Returns the password for the account
 * - [MEGARequest name] - Returns the firstname of the user
 * - [MEGARequest text] - Returns the lastname of the user
 * - [MEGARequest nodeHandle] - Returns the last public node handle accessed
 * - [MEGARequest access] - Returns the type of lastPublicHandle
 * - [MEGARequest transferredBytes] - Returns the timestamp of the last access
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest sessionKey] - Returns the session id to resume the process
 *
 * If this request succeed, a new ephemeral session will be created for the new user
 * and a confirmation email will be sent to the specified email address. The app may
 * resume the create-account process by using [MEGASdk resumeCreateAccountWithSessionId:].
 *
 * If an account with the same email already exists, you will get the error code
 * MEGAErrorTypeApiEExist in onRequestFinish
 *
 * @param email Email for the account
 * @param password Password for the account
 * @param firstname Firstname of the user
 * @param lastname Lastname of the user
 * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
 * @param lastPublicHandleType Indicates the type of lastPublicHandle, valid values are:
 *      - AffiliateTypeId = 1
 *      - AffiliateTypeFileFolder = 2
 *      - AffiliateTypeChat = 3
 *      - AffiliateTypeContact = 4
 *
 * @param lastAccessTimestamp Timestamp of the last access
 * @param delegate Delegate to track this request.
 */
- (void)createAccountWithEmail:(NSString *)email password:(NSString *)password firstname:(NSString *)firstname lastname:(NSString *)lastname lastPublicHandle:(uint64_t)lastPublicHandle lastPublicHandleType:(AffiliateType)lastPublicHandleType lastAccessTimestamp:(uint64_t)lastAccessTimestamp delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Initialize the creation of a new MEGA account, with firstname and lastname
 *
 * The associated request type with this request is MEGARequestTypeCreateAccount.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email for the account
 * - [MEGARequest password] - Returns the password for the account
 * - [MEGARequest name] - Returns the firstname of the user
 * - [MEGARequest text] - Returns the lastname of the user
 * - [MEGARequest nodeHandle] - Returns the last public node handle accessed
 * - [MEGARequest access] - Returns the type of lastPublicHandle
 * - [MEGARequest transferredBytes] - Returns the timestamp of the last access
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest sessionKey] - Returns the session id to resume the process
 *
 * If this request succeed, a new ephemeral session will be created for the new user
 * and a confirmation email will be sent to the specified email address. The app may
 * resume the create-account process by using [MEGASdk resumeCreateAccountWithSessionId:].
 *
 * If an account with the same email already exists, you will get the error code
 * MEGAErrorTypeApiEExist in onRequestFinish
 *
 * @param email Email for the account
 * @param password Password for the account
 * @param firstname Firstname of the user
 * @param lastname Lastname of the user
 * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
 * @param lastPublicHandleType Indicates the type of lastPublicHandle, valid values are:
 *      - AffiliateTypeId = 1
 *      - AffiliateTypeFileFolder = 2
 *      - AffiliateTypeChat = 3
 *      - AffiliateTypeContact = 4
 *
 * @param lastAccessTimestamp Timestamp of the last access
 */
- (void)createAccountWithEmail:(NSString *)email password:(NSString *)password firstname:(NSString *)firstname lastname:(NSString *)lastname lastPublicHandle:(uint64_t)lastPublicHandle lastPublicHandleType:(AffiliateType)lastPublicHandleType lastAccessTimestamp:(uint64_t)lastAccessTimestamp;

/**
 * @brief Resume a registration process
 *
 * When a user begins the account registration process by calling [MEGASdk createAccountWithEmail:
 * password:firstname:lastname:delegate:], an ephemeral account is created.
 *
 * Until the user successfully confirms the signup link sent to the provided email address,
 * you can resume the ephemeral session in order to change the email address, resend the
 * signup link (@see [MEGASdk sendSignupLinkWithEmail:name:password:delegate:]) and also
 * to receive notifications in case the user confirms the account using another client
 * ([MEGAGlobalDelegate onAccountUpdate:] or [MEGADelegate onAccountUpdate:]).It is also possible
 * to cancel the registration process by [MEGASdk cancelCreateAccount:delegate:], which invalidates
 * the signup link associated to the ephemeral session (the session will be still valid).
 *
 * The associated request type with this request is MEGARequestTypeCreateAccount.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest sessionKey] - Returns the session id to resume the process
 * - [MEGARequest paramType] - Returns the value 1
 *
 * In case the account is already confirmed, the associated request will fail with
 * error MEGAErrorTypeApiEArgs.
 *
 * @param sessionId Session id valid for the ephemeral account (@see [MEGASdk createAccountWithEmail:password:firstname:lastname:])
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)resumeCreateAccountWithSessionId:(NSString *)sessionId delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Resume a registration process
 *
 * When a user begins the account registration process by calling [MEGASdk createAccountWithEmail:
 * password:firstname:lastname:delegate:], an ephemeral account is created.
 *
 * Until the user successfully confirms the signup link sent to the provided email address,
 * you can resume the ephemeral session in order to change the email address, resend the
 * signup link (@see [MEGASdk sendSignupLinkWithEmail:name:password:delegate:]) and also
 * to receive notifications in case the user confirms the account using another client
 * ([MEGAGlobalDelegate onAccountUpdate:] or [MEGADelegate onAccountUpdate:]).It is also possible
 * to cancel the registration process by [MEGASdk cancelCreateAccount:delegate:], which invalidates
 * the signup link associated to the ephemeral session (the session will be still valid).
 *
 * The associated request type with this request is MEGARequestTypeCreateAccount.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest sessionKey] - Returns the session id to resume the process
 * - [MEGARequest paramType] - Returns the value 1
 *
 * In case the account is already confirmed, the associated request will fail with
 * error MEGAErrorTypeApiEArgs.
 *
 * @param sessionId Session id valid for the ephemeral account (@see [MEGASdk createAccountWithEmail:password:firstname:lastname:])
 */
- (void)resumeCreateAccountWithSessionId:(NSString *)sessionId;

/**
 * @brief Cancel a registration process
 *
 * If a signup link has been generated during registration process, call this function
 * to invalidate it. The ephemeral session will not be invalidated, only the signup link.
 *
 * The associated request type with this request is MEGARequestTypeCreateAccount.
 * Valid data in the MegaRequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the value 2
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)cancelCreateAccountWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Cancel a registration process
 *
 * If a signup link has been generated during registration process, call this function
 * to invalidate it. The ephemeral session will not be invalidated, only the signup link.
 *
 * The associated request type with this request is MEGARequestTypeCreateAccount.
 * Valid data in the MegaRequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the value 2
 *
 */
- (void)cancelCreateAccount;

/**
 * @brief Sends the confirmation email for a new account
 *
 * This function is useful to send the confirmation link again or to send it to a different
 * email address, in case the user mistyped the email at the registration form.
 *
 * @param email Email for the account
 * @param name Firstname of the user
 * @param password Password for the account
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)sendSignupLinkWithEmail:(NSString *)email name:(NSString *)name password:(NSString *)password delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Sends the confirmation email for a new account
 *
 * This function is useful to send the confirmation link again or to send it to a different
 * email address, in case the user mistyped the email at the registration form.
 *
 * @param email Email for the account
 * @param name Firstname of the user
 * @param password Password for the account
 */
- (void)sendSignupLinkWithEmail:(NSString *)email name:(NSString *)name password:(NSString *)password;

/**
 * @brief Sends the confirmation email for a new account
 *
 * This function is useful to send the confirmation link again or to send it to a different
 * email address, in case the user mistyped the email at the registration form.
 *
 * @param email Email for the account
 * @param name Firstname of the user
 * @param base64pwkey key returned by [MEGARequest privateKey] in the onRequestFinish callback of createAccount
 * @param delegate MEGARequestDelegate to track this request
 *
 * @deprecated This function only works using the old registration method and will be removed soon.
 * Please use [MEGASdk sendSignupLinkWithEmail:name:password:delegate:] instead.
 */
- (void)fastSendSignupLinkWithEmail:(NSString *)email base64pwkey:(NSString *)base64pwkey name:(NSString *)name delegate:(id<MEGARequestDelegate>)delegate __attribute__((deprecated("This function only works using the old registration method and will be removed soon. Please use [MEGASdk sendSignupLinkWithEmail:name:password:delegate:] instead.")));

/**
 * @brief Sends the confirmation email for a new account
 *
 * This function is useful to send the confirmation link again or to send it to a different
 * email address, in case the user mistyped the email at the registration form.
 *
 * @param email Email for the account
 * @param name Firstname of the user
 * @param base64pwkey key returned by [MEGARequest privateKey] in the onRequestFinish callback of createAccount
 *
 * @deprecated This function only works using the old registration method and will be removed soon.
 * Please use [MEGASdk sendSignupLinkWithEmail:name:password:] instead.
 */
- (void)fastSendSignupLinkWithEmail:(NSString *)email base64pwkey:(NSString *)base64pwkey name:(NSString *)name __attribute__((deprecated("This function only works using the old registration method and will be removed soon. Please use [MEGASdk sendSignupLinkWithEmail:name:password:] instead.")));

/**
 * @brief Get information about a confirmation link or a new signup link.
 *
 * The associated request type with this request is MEGARequestTypeQuerySignUpLink.
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest link] - Returns the confirmation link
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest email] - Return the email associated with the confirmation link.
 * - [MEGARequest name] - Returns the name associated with the confirmation link.
 * - [MEGARequest flag] - Returns true if the account was automatically confirmed, otherwise false
 
 * If [MEGARequest flag] returns YES, the account was automatically confirmed and it's not needed
 * to call [MEGASdk confirmAccountWithLink:password:delegate]. If it returns NO, it's needed to call [MEGASdk confirmAccountWithLink:password:delegate]
 * as usual. New accounts (V2, starting from April 2018) do not require a confirmation with the password,
 * but old confirmation links (V1) require it, so it's needed to check that parameter in onRequestFinish
 * to know how to proceed.
 *
 * If already logged-in into a different account, you will get the error code MEGAErrorTypeApiEAccess
 * in onRequestFinish.
 * If logged-in into the account that is attempted to confirm and the account is already confirmed, you
 * will get the error code MEGAErrorTypeApiEExpired in onRequestFinish.
 * In both cases, the [MEGARequest email] will return the email of the account that was attempted
 * to confirm, and the [MEGARequest name] will return the name.
 *
 * @param link Confirmation link
 * @param delegate Delegate to track this request
 */
- (void)querySignupLink:(NSString *)link delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get information about a confirmation link or a new signup link.
 *
 * The associated request type with this request is MEGARequestTypeQuerySignUpLink.
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest link] - Returns the confirmation link
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest email] - Return the email associated with the confirmation link.
 * - [MEGARequest name] - Returns the name associated with the confirmation link.
 * - [MEGARequest flag] - Returns true if the account was automatically confirmed, otherwise false
 
 * If [MEGARequest flag] returns YES, the account was automatically confirmed and it's not needed
 * to call [MEGASdk confirmAccountWithLink:password:delegate]. If it returns NO, it's needed to call [MEGASdk confirmAccountWithLink:password:delegate]
 * as usual. New accounts (V2, starting from April 2018) do not require a confirmation with the password,
 * but old confirmation links (V1) require it, so it's needed to check that parameter in onRequestFinish
 * to know how to proceed.
 *
 * If already logged-in into a different account, you will get the error code MEGAErrorTypeApiEAccess
 * in onRequestFinish.
 * If logged-in into the account that is attempted to confirm and the account is already confirmed, you
 * will get the error code MEGAErrorTypeApiEExpired in onRequestFinish.
 * In both cases, the [MEGARequest email] will return the email of the account that was attempted
 * to confirm, and the [MEGARequest name] will return the name.
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
 * As a result of a successfull confirmation, the app will receive the callback
 * [MEGADelegate onEvent: event:] and [MEGAGlobalDelegate onEvent: event:] with an event of type
 * EventAccountConfirmation. You can check the email used to confirm
 * the account by checking [MEGAEvent text]. @see [MEGADelegate onEvent: event:].
 *
 * If already logged-in into a different account, you will get the error code MEGAErrorTypeApiEAccess
 * in onRequestFinish.
 * If logged-in into the account that is attempted to confirm and the account is already confirmed, you
 * will get the error code MEGAErrorTypeApiEExpired in onRequestFinish.
 * In both cases, the [MEGARequest email] will return the email of the account that was attempted
 * to confirm, and the [MEGARequest name] will return the name.
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
 * As a result of a successfull confirmation, the app will receive the callback
 * [MEGADelegate onEvent: event:] and [MEGAGlobalDelegate onEvent: event:] with an event of type
 * EventAccountConfirmation. You can check the email used to confirm
 * the account by checking [MEGAEvent text]. @see [MEGADelegate onEvent: event:].
 *
 * If already logged-in into a different account, you will get the error code MEGAErrorTypeApiEAccess
 * in onRequestFinish.
 * If logged-in into the account that is attempted to confirm and the account is already confirmed, you
 * will get the error code MEGAErrorTypeApiEExpired in onRequestFinish.
 * In both cases, the [MEGARequest email] will return the email of the account that was attempted
 * to confirm, and the [MEGARequest name] will return the name.
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
 * @deprecated This function only works using the old registration method and will be removed soon.
 * Please use [MEGASdk confirmAccountWithLink:password:delegate] instead.
 */
- (void)fastConfirmAccountWithLink:(NSString *)link base64pwkey:(NSString *)base64pwkey delegate:(id<MEGARequestDelegate>)delegate __attribute__((deprecated("This function only works using the old registration method and will be removed soon.")));

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
 * @deprecated This function only works using the old registration method and will be removed soon.
 * Please use [MEGASdk confirmAccountWithLink:password] instead.
 */
- (void)fastConfirmAccountWithLink:(NSString *)link base64pwkey:(NSString *)base64pwkey __attribute__((deprecated("This function only works using the old registration method and will be removed soon.")));

/**
 * @brief Initialize the reset of the existing password, with and without the Master Key.
 *
 * The associated request type with this request is MEGARequestTypeGetRecoveryLink.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email for the account
 * - [MEGARequest flag] - Returns whether the user has a backup of the master key or not.
 *
 * If this request succeed, a recovery link will be sent to the user.
 * If no account is registered under the provided email, you will get the error code
 * MEGAErrorTypeApiENoent in onRequestFinish
 *
 * @param email Email used to register the account whose password wants to be reset.
 * @param hasMasterKey YES if the user has a backup of the master key. Otherwise, NO.
 * @param delegate Delegate to track this request.
 */
- (void)resetPasswordWithEmail:(NSString *)email hasMasterKey:(BOOL)hasMasterKey delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Initialize the reset of the existing password, with and without the Master Key.
 *
 * The associated request type with this request is MEGARequestTypeGetRecoveryLink.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email for the account
 * - [MEGARequest flag] - Returns whether the user has a backup of the master key or not.
 *
 * If this request succeed, a recovery link will be sent to the user.
 * If no account is registered under the provided email, you will get the error code
 * MEGAErrorTypeApiENoent in onRequestFinish
 *
 * @param email Email used to register the account whose password wants to be reset.
 * @param hasMasterKey YES if the user has a backup of the master key. Otherwise, NO.
 */
- (void)resetPasswordWithEmail:(NSString *)email hasMasterKey:(BOOL)hasMasterKey;

/**
 * @brief Get information about a recovery link created by [MEGASdk resetPasswordWithEmail:hasMasterKey:].
 *
 * The associated request type with this request is MEGARequestTypeQueryRecoveryLink
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest link] - Returns the recovery link
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest email] - Return the email associated with the link
 * - [MEGARequest flag] - Return whether the link requires masterkey to reset password.
 *
 * @param link Recovery link (#recover)
 * @param delegate Delegate to track this request
 */
- (void)queryResetPasswordLink:(NSString *)link delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get information about a recovery link created by [MEGASdk resetPasswordWithEmail:hasMasterKey:].
 *
 * The associated request type with this request is MEGARequestTypeQueryRecoveryLink
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest link] - Returns the recovery link
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest email] - Return the email associated with the link
 * - [MEGARequest flag] - Return whether the link requires masterkey to reset password.
 *
 * @param link Recovery link (#recover)
 */
- (void)queryResetPasswordLink:(NSString *)link;

/**
 * @brief Set a new password for the account pointed by the recovery link.
 *
 * Recovery links are created by calling [MEGASdk resetPasswordWithEmail:hasMasterKey:] and may or may not
 * require to provide the master key.
 *
 * @see The flag of the MEGARequestTypeQueryRecoveryLink in [MEGASdk queryResetPasswordLink:]
 *
 * The associated request type with this request is MEGARequestTypeConfirmRecoveryLink
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest link] - Returns the recovery link
 * - [MEGARequest password] - Returns the new password
 * - [MEGARequest privateKey] - Returns the Master Key, when provided
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest email] - Return the email associated with the link
 * - [MEGARequest flag] - Return whether the link requires masterkey to reset password.
 *
 * @param link The recovery link sent to the user's email address.
 * @param newPassword The new password to be set.
 * @param masterKey Base64-encoded string containing the master key (optional).
 * @param delegate Delegate to track this request
 */
- (void)confirmResetPasswordWithLink:(NSString *)link newPassword:(NSString *)newPassword masterKey:(nullable NSString *)masterKey delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Set a new password for the account pointed by the recovery link.
 *
 * Recovery links are created by calling [MEGASdk resetPasswordWithEmail:hasMasterKey:] and may or may not
 * require to provide the master key.
 *
 * @see The flag of the MEGARequestTypeQueryRecoveryLink in [MEGASdk queryResetPasswordLink:]
 *
 * The associated request type with this request is MEGARequestTypeConfirmRecoveryLink
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest link] - Returns the recovery link
 * - [MEGARequest password] - Returns the new password
 * - [MEGARequest privateKey] - Returns the Master Key, when provided
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest email] - Return the email associated with the link
 * - [MEGARequest flag] - Return whether the link requires masterkey to reset password.
 *
 * @param link The recovery link sent to the user's email address.
 * @param newPassword The new password to be set.
 * @param masterKey Base64-encoded string containing the master key (optional).
 */
- (void)confirmResetPasswordWithLink:(NSString *)link newPassword:(NSString *)newPassword masterKey:(nullable NSString *)masterKey;

/**
 * @brief Initialize the cancellation of an account.
 *
 * The associated request type with this request is MEGARequestTypeGetCancelLink.
 *
 * If this request succeed, a cancellation link will be sent to the email address of the user.
 * If no user is logged in, you will get the error code MEGAErrorTypeApiEAccess in onRequestFinish.
 *
 * If the MEGA account is a sub-user business account, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEMasterOnly.
 *
 * @see [MEGASdk confirmCancelAccountWithLink:password:]
 *
 * @param delegate Delegate to track this request
 */
- (void)cancelAccountWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Initialize the cancellation of an account.
 *
 * The associated request type with this request is MEGARequestTypeGetCancelLink.
 *
 * If this request succeed, a cancellation link will be sent to the email address of the user.
 * If no user is logged in, you will get the error code MEGAErrorTypeApiEAccess in onRequestFinish.
 *
 * If the MEGA account is a sub-user business account, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEMasterOnly.
 *
 * @see [MEGASdk confirmCancelAccountWithLink:password:]
 *
 */
- (void)cancelAccount;

/**
 * @brief Get information about a cancel link created by [MEGASdk cancelAccount].
 *
 * The associated request type with this request is MEGARequestTypeQueryRecoveryLink
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest link] - Returns the cancel link
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest email] - Return the email associated with the link
 *
 * @param link Cancel link (#cancel)
 * @param delegate Delegate to track this request
 */
- (void)queryCancelLink:(NSString *)link delegate:(id<MEGARequestDelegate>)delegate;

/**
* @brief Get information about a cancel link created by [MEGASdk cancelAccount].
*
* The associated request type with this request is MEGARequestTypeQueryRecoveryLink
* Valid data in the MEGARequest object received on all callbacks:
* - [MEGARequest link] - Returns the cancel link
*
* Valid data in the MEGARequest object received in onRequestFinish when the error code
* is MEGAErrorTypeApiOk:
* - [MEGARequest email] - Return the email associated with the link
*
* @param link Cancel link (#cancel)
*/
- (void)queryCancelLink:(NSString *)link;

/**
 * @brief Effectively parks the user's account without creating a new fresh account.
 *
 * If no user is logged in, you will get the error code MEGAErrorTypeApiEAccess in onRequestFinish.
 *
 * The contents of the account will then be purged after 60 days. Once the account is
 * parked, the user needs to contact MEGA support to restore the account.
 *
 * The associated request type with this request is MEGARequestTypeConfirmCancelLink.
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest link] - Returns the recovery link
 * - [MEGARequest password] - Returns the new password
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest email] - Return the email associated with the link
 *
 * @param link Cancellation link sent to the user's email address;
 * @param password Password for the account.
 * @param delegate Delegate to track this request
 */
- (void)confirmCancelAccountWithLink:(NSString *)link password:(NSString *)password delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Effectively parks the user's account without creating a new fresh account.
 *
 * If no user is logged in, you will get the error code MEGAErrorTypeApiEAccess in onRequestFinish.
 *
 * The contents of the account will then be purged after 60 days. Once the account is
 * parked, the user needs to contact MEGA support to restore the account.
 *
 * The associated request type with this request is MEGARequestTypeConfirmCancelLink.
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest link] - Returns the recovery link
 * - [MEGARequest password] - Returns the new password
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest email] - Return the email associated with the link
 *
 * @param link Cancellation link sent to the user's email address;
 * @param password Password for the account.
 */
- (void)confirmCancelAccountWithLink:(NSString *)link password:(NSString *)password;

/**
* @brief Allow to resend the verification email for Weak Account Protection
*
* The verification email will be resent to the same address as it was previously sent to.
*
* This function can be called if the the reason for being blocked is:
*      700: the account is supended for Weak Account Protection.
*
* If the logged in account is not suspended or is suspended for some other reason,
* onRequestFinish will be called with the error code MEGAErrorTypeApiEAccess.
*
* If the logged in account has not been sent the unlock email before,
* onRequestFinish will be called with the error code MEGAErrorTypeApiEArgs.
*
* @param delegate MEGARequestDelegate to track this request
*/
- (void)resendVerificationEmailWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
* @brief Allow to resend the verification email for Weak Account Protection
*
* The verification email will be resent to the same address as it was previously sent to.
*
* This function can be called if the the reason for being blocked is:
*      700: the account is supended for Weak Account Protection.
*
* If the logged in account is not suspended or is suspended for some other reason,
* onRequestFinish will be called with the error code MEGAErrorTypeApiEAccess.
*
* If the logged in account has not been sent the unlock email before,
* onRequestFinish will be called with the error code MEGAErrorTypeApiEArgs.
*/
- (void)resendVerificationEmail;

/**
 * @brief Initialize the change of the email address associated to the account.
 *
 * The associated request type with this request is MEGARequestTypeGetChangeEmailLink.
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest email] - Return the email associated with the link
 *
 * If this request succeed, a change-email link will be sent to the specified email address.
 * If no user is logged in, you will get the error code MEGAErrorTypeApiEAccess in onRequestFinish.
 *
 * If the MEGA account is a sub-user business account, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEMasterOnly.
 *
 * @param email The new email to be associated to the account.
 * @param delegate Delegate to track this request
 */
- (void)changeEmail:(NSString *)email delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Initialize the change of the email address associated to the account.
 *
 * The associated request type with this request is MEGARequestTypeGetChangeEmailLink.
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest email] - Return the email associated with the link
 *
 * If this request succeed, a change-email link will be sent to the specified email address.
 * If no user is logged in, you will get the error code MEGAErrorTypeApiEAccess in onRequestFinish.
 *
 * @param email The new email to be associated to the account.
 */
- (void)changeEmail:(NSString *)email;

/**
 * @brief Get information about a change-email link created by [MEGASdk changeEmail:].
 *
 * If no user is logged in, you will get the error code MEGAErrorTypeApiEAccess in onRequestFinish.
 *
 * The associated request type with this request is MEGARequestTypeQueryRecoveryLink
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest link] - Returns the recovery link
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest email] - Return the email associated with the link
 *
 * @param link Change-email link (#verify)
 * @param delegate Delegate to track this request
 */
- (void)queryChangeEmailLink:(NSString *)link delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get information about a change-email link created by [MEGASdk changeEmail:].
 *
 * If no user is logged in, you will get the error code MEGAErrorTypeApiEAccess in onRequestFinish.
 *
 * The associated request type with this request is MEGARequestTypeQueryRecoveryLink
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest link] - Returns the recovery link
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest email] - Return the email associated with the link
 *
 * If the account logged-in is different account than the one for which the link
 * was generated, onRequestFinish will be called with the error code MEGAErrorTypeApiEAccess.
 *
 * @param link Change-email link (#verify)
 */
- (void)queryChangeEmailLink:(NSString *)link;

/**
 * @brief Effectively changes the email address associated to the account.
 *
 * If no user is logged in, you will get the error code MEGAErrorTypeApiEAccess in onRequestFinish.
 *
 * The associated request type with this request is MEGARequestTypeConfirmChangeEmailLink.
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest link] - Returns the recovery link
 * - [MEGARequest password] - Returns the new password
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest email] - Return the email associated with the link
 *
 * @param link Change-email link sent to the user's email address.
 * @param password Password for the account.
 * @param delegate Delegate to track this request
 */
- (void)confirmChangeEmailWithLink:(NSString *)link password:(NSString *)password delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Effectively changes the email address associated to the account.
 *
 * If no user is logged in, you will get the error code MEGAErrorTypeApiEAccess in onRequestFinish.
 *
 * The associated request type with this request is MEGARequestTypeConfirmChangeEmailLink.
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest link] - Returns the recovery link
 * - [MEGARequest password] - Returns the new password
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest email] - Return the email associated with the link
 *
 * @param link Change-email link sent to the user's email address.
 * @param password Password for the account.
 */
- (void)confirmChangeEmailWithLink:(NSString *)link password:(NSString *)password;

/**
 * @brief Create a contact link
 *
 * The associated request type with this request is MEGARequestTypeContactLinkCreate.
 *
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest flag] - Returns the value of \c renew parameter
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest nodeHandle] - Return the handle of the new contact link
 *
 * @param renew YES to invalidate the previous contact link (if any).
 * @param delegate Delegate to track this request
 */
- (void)contactLinkCreateRenew:(BOOL)renew delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Create a contact link
 *
 * The associated request type with this request is MEGARequestTypeContactLinkCreate.
 *
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest flag] - Returns the value of \c renew parameter
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest nodeHandle] - Return the handle of the new contact link
 *
 * @param renew YES to invalidate the previous contact link (if any).
 */
- (void)contactLinkCreateRenew:(BOOL)renew;

/**
 * @brief Get information about a contact link
 *
 * The associated request type with this request is MEGARequestTypeContactLinkQuery.
 *
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the contact link
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest parentHandle] - Returns the userhandle of the contact
 * - [MEGARequest email] - Returns the email of the contact
 * - [MEGARequest name] - Returns the first name of the contact
 * - [MEGARequest text] - Returns the last name of the contact
 *
 * @param handle Handle of the contact link to check
 * @param delegate Delegate to track this request
 */
- (void)contactLinkQueryWithHandle:(uint64_t)handle delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get information about a contact link
 *
 * The associated request type with this request is MEGARequestTypeContactLinkQuery.
 *
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the contact link
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest parentHandle] - Returns the userhandle of the contact
 * - [MEGARequest email] - Returns the email of the contact
 * - [MEGARequest name] - Returns the first name of the contact
 * - [MEGARequest text] - Returns the last name of the contact
 *
 * @param handle Handle of the contact link to check
 */
- (void)contactLinkQueryWithHandle:(uint64_t)handle;

/**
 * @brief Delete the active contact link
 *
 * The associated request type with this request is MEGARequestTypeContactLinkDelete.
 *
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the contact link
 *
 * @param delegate Delegate to track this request
 */
- (void)contactLinkDeleteWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Delete the active contact link
 *
 * The associated request type with this request is MEGARequestTypeContactLinkDelete.
 *
 * Valid data in the MEGARequest object received on all callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the contact link
 */
- (void)contactLinkDelete;

/**
 * @brief Command to keep mobile apps alive when needed
 *
 * When this feature is enabled, API servers will regularly send push notifications
 * to keep the application running. Before using this function, it's needed to register
 * a notification token using [MEGASdk registeriOSdeviceToken:]
 *
 * The associated request type with this request is MEGARequestTypeKeepMeAlive.
 *
 * Valid data in the MEGARequest object received on all callbacks:
 * - MEGARequest.paramType - Returns the type send in the first parameter
 * - MEGARequest.flag - Returns YES when the feature is being enabled, otherwise NO
 *
 * @param type Type of keep alive desired
 * Valid values for this parameter:
 * - KeepMeAliveCameraUploads = 0
 *
 * @param enable YES to enable this feature, NO to disable it
 * @param delegate MEGARequestDelegate to track this request
 *
 * @see [MEGASdk registeriOSdeviceToken:]
 */
- (void)keepMeAliveWithType:(KeepMeAlive)type enable:(BOOL)enable delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Command to keep mobile apps alive when needed
 *
 * When this feature is enabled, API servers will regularly send push notifications
 * to keep the application running. Before using this function, it's needed to register
 * a notification token using [MEGASdk registeriOSdeviceToken:]
 *
 * The associated request type with this request is MEGARequestTypeKeepMeAlive.
 *
 * Valid data in the MEGARequest object received on all callbacks:
 * - MEGARequest.paramType - Returns the type send in the first parameter
 * - MEGARequest.flag - Returns YES when the feature is being enabled, otherwise NO
 *
 * @param type Type of keep alive desired
 * Valid values for this parameter:
 * - KeepMeAliveCameraUploads = 0
 *
 * @param enable YES to enable this feature, NO to disable it
 *
 * @see [MEGASdk registeriOSdeviceToken:]
 */
- (void)keepMeAliveWithType:(KeepMeAlive)type enable:(BOOL)enable;

/**
 * @brief Check the reason of being blocked.
 *
 * The associated request type with this request is MEGARequestTypeWhyAmIBlocked.
 *
 * This request can be sent internally at anytime (whenever an account gets blocked), so
 * a MEGAGlobalListener should process the result, show the reason and logout.
 *
 * Valid data in the MegaRequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - MEGARequest.text - Returns the reason string (in English)
 * - MEGARequest.number - Returns the reason code. Possible values:
 *     0: The account is not blocked
 *     200: suspension message for any type of suspension, but copyright suspension.
 *     300: suspension only for multiple copyright violations.
 *     400: the subuser account has been disabled.
 *     401: the subuser account has been removed.
 *     500: The account needs to be verified by an SMS code.
 *     700: the account is supended for Weak Account Protection.
 *
 * If the error code in the MEGARequest object received in onRequestFinish
 * is MEGAErrorTypeApiOk, the user is not blocked.
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)whyAmIBlockedWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Check the reason of being blocked.
 *
 * The associated request type with this request is MEGARequestTypeWhyAmIBlocked.
 *
 * This request can be sent internally at anytime (whenever an account gets blocked), so
 * a MEGAGlobalListener should process the result, show the reason and logout.
 *
 * Valid data in the MegaRequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - MEGARequest.text - Returns the reason string (in English)
 * - MEGARequest.number - Returns the reason code. Possible values:
 *     0: The account is not blocked
 *     200: suspension message for any type of suspension, but copyright suspension.
 *     300: suspension only for multiple copyright violations.
 *     400: the subuser account has been disabled.
 *     401: the subuser account has been removed.
 *     500: The account needs to be verified by an SMS code.
 *     700: the account is supended for Weak Account Protection.
 *
 * If the error code in the MEGARequest object received in onRequestFinish
 * is MEGAErrorTypeApiOk, the user is not blocked.
*/
- (void)whyAmIBlocked;

/**
 * @brief Get the next PSA (Public Service Announcement) that should be shown to the user
 *
 * After the PSA has been accepted or dismissed by the user, app should
 * use [MEGASdk setPSAWithIdentifier:] [MEGASdk setPSAWithIdentifier:delegate:] to notify API servers about
 * this event and do not get the same PSA again in the next call to this function.
 *
 * The associated request type with this request is MEGARequestTypeGetPSA.
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest number] - Returns the id of the PSA (useful to call [MEGASdk setPSAWithIdentifier:]
 *                          [MEGASdk setPSAWithIdentifier:delegate:] later)
 * - [MEGARequest name] - Returns the title of the PSA
 * - [MEGARequest text] - Returns the text of the PSA
 * - [MEGARequest file] - Returns the URL of the image of the PSA
 * - [MEGARequest password] - Returns the text for the possitive button (or an empty string)
 * - [MEGARequest link] - Returns the link for the possitive button (or an empty string)
 *
 * If there isn't any new PSA to show, onRequestFinish will be called with the error
 * code MEGAErrorTypeApiENoent
 *
 * @param delegate MEGARequestDelegate to track this request
 * @see [MEGASdk setPSAWithIdentifier:] [MEGASdk setPSAWithIdentifier:delegate:]
 */
- (void)getPSAWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get the next PSA (Public Service Announcement) that should be shown to the user
 *
 * After the PSA has been accepted or dismissed by the user, app should
 * use [MEGASdk setPSAWithIdentifier:] [MEGASdk setPSAWithIdentifier:delegate:] to notify API servers about
 * this event and do not get the same PSA again in the next call to this function.
 *
 * The associated request type with this request is MEGARequestTypeGetPSA.
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest number] - Returns the id of the PSA (useful to call [MEGASdk setPSAWithIdentifier:]
 *                          [MEGASdk setPSAWithIdentifier:delegate:] later)
 * - [MEGARequest name] - Returns the title of the PSA
 * - [MEGARequest text] - Returns the text of the PSA
 * - [MEGARequest file] - Returns the URL of the image of the PSA
 * - [MEGARequest password] - Returns the text for the possitive button (or an empty string)
 * - [MEGARequest link] - Returns the link for the possitive button (or an empty string)
 *
 * If there isn't any new PSA to show, onRequestFinish will be called with the error
 * code MEGAErrorTypeApiENoent
 *
 * @see [MEGASdk setPSAWithIdentifier:] [MEGASdk setPSAWithIdentifier:delegate:]
 */
- (void)getPSA;

/**
 * @brief Notify API servers that a PSA (Public Service Announcement) has been already seen
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser.
 *
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the value MEGAUserAttributeLastPSA
 * - [MEGARequest text] - Returns the id passed in the first parameter (as a string)
 *
 * @param identifier Identifier of the PSA
 * @param delegate MEGARequestDelegate to track this request
 *
 * @see [MEGASdk getPSA] [MEGASdk getPSAWithDelegate:]
 */
- (void)setPSAWithIdentifier:(NSInteger)identifier delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Notify API servers that a PSA (Public Service Announcement) has been already seen
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser.
 *
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the value MEGAUserAttributeLastPSA
 * - [MEGARequest text] - Returns the id passed in the first parameter (as a string)
 *
 * @param identifier Identifier of the PSA
 *
 * @see [MEGASdk getPSA] [MEGASdk getPSAWithDelegate:]
 */
- (void)setPSAWithIdentifier:(NSInteger)identifier;

/**
 * @brief Command to acknowledge user alerts.
 *
 * Other clients will be notified that alerts to this point have been seen.
 *
 * @see [MEGASdk userAlertList]
 */
- (void)acknowledgeUserAlertsWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Command to acknowledge user alerts.
 *
 * Other clients will be notified that alerts to this point have been seen.
 *
 * @see [MEGASdk userAlertList]
 */
- (void)acknowledgeUserAlerts;

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
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
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
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
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
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
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
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
 *
 * @param node Node to move.
 * @param newParent New parent for the node.
 */
- (void)moveNode:(MEGANode *)node newParent:(MEGANode *)newParent;

/**
 * @brief Move a node in the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeMove.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node to move
 * - [MEGARequest parentHandle] - Returns the handle of the new parent for the node
 * - [MEGARequest name] - Returns the name for the new node
 *
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
 *
 * @param node Node to move.
 * @param newParent New parent for the node.
 * @param newName Name for the new node.
 * @param delegate Delegate to track this request.
 */
- (void)moveNode:(MEGANode *)node newParent:(MEGANode *)newParent newName:(NSString *)newName delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Move a node in the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeMove.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node to move
 * - [MEGARequest parentHandle] - Returns the handle of the new parent for the node
 * - [MEGARequest name] - Returns the name for the new node
 *
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
 *
 * @param node Node to move.
 * @param newParent New parent for the node.
 * @param newName Name for the new node.
 */
- (void)moveNode:(MEGANode *)node newParent:(MEGANode *)newParent newName:(NSString *)newName;

/**
 * @brief Copy a node in the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeCopy.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node to move
 * - [MEGARequest parentHandle] - Returns the handle of the new parent for the node
 * - [MEGARequest publicNode] - Returns the node to copy (if it is a public node)
 *
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
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
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
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
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
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
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
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
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
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
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
 *
 * @param node Node to modify.
 * @param newName New name for the node.
 */
- (void)renameNode:(MEGANode *)node newName:(NSString *)newName;

/**
 * @brief Remove a node from the MEGA account.
 *
 * This function doesn't move the node to the Rubbish Bin, it fully removes the node. To move
 * the node to the Rubbish Bin use [MEGASdk moveNode:newParent:delegate:].
 *
 * If the node has previous versions, they will be deleted too.
 *
 * The associated request type with this request is MEGARequestTypeRemove.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node to rename
 * - [MEGARequest flag] - Returns NO because previous versions won't be preserved
 *
 * If the MEGA account is a sub-user business account, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEMasterOnly.
 *
 * @param node Node to remove.
 * @param delegate Delegate to track this request.
 */
- (void)removeNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Remove a node from the MEGA account.
 *
 * This function doesn't move the node to the Rubbish Bin, it fully removes the node. To move
 * the node to the Rubbish Bin use [MEGASdk moveNode:newParent:delegate:].
 *
 * If the node has previous versions, they will be deleted too.
 *
 * The associated request type with this request is MEGARequestTypeRemove.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node to rename
 * - [MEGARequest flag] - Returns NO because previous versions won't be preserved
 *
 * If the MEGA account is a sub-user business account, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEMasterOnly.
 *
 * @param node Node to remove.
 */
- (void)removeNode:(MEGANode *)node;

/**
 * @brief Remove all versions from the MEGA account
 *
 * The associated request type with this request is MEGARequestTypeRemoveVersions
 *
 * When the request finishes, file versions might not be deleted yet.
 * Deletions are notified using onNodesUpdate callbacks.
 *
 * @param delegate MEGARequestDelegate Delegate to track this request
 */
- (void)removeVersionsWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Remove all versions from the MEGA account
 *
 * The associated request type with this request is MEGARequestTypeRemoveVersions
 *
 * When the request finishes, file versions might not be deleted yet.
 * Deletions are notified using onNodesUpdate callbacks.
 *
 */
- (void)removeVersions;

/**
 * @brief Remove a version of a file from the MEGA account
 *
 * This function doesn't move the node to the Rubbish Bin, it fully removes the node. To move
 * the node to the Rubbish Bin use [MEGASdk moveNode:newParent:delegate:].
 *
 * If the node has previous versions, they won't be deleted.
 *
 * The associated request type with this request is MEGARequestTypeRemove
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node to remove
 * - [MEGARequest flag] - Returns YES because previous versions will be preserved
 *
 * If the MEGA account is a sub-user business account, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEMasterOnly.
 *
 * @param node Node to remove
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)removeVersionNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Remove a version of a file from the MEGA account
 *
 * This function doesn't move the node to the Rubbish Bin, it fully removes the node. To move
 * the node to the Rubbish Bin use [MEGASdk moveNode:newParent:delegate:].
 *
 * If the node has previous versions, they won't be deleted.
 *
 * The associated request type with this request is MEGARequestTypeRemove
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node to remove
 * - [MEGARequest flag] - Returns YES because previous versions will be preserved
 *
 * If the MEGA account is a sub-user business account, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEMasterOnly.
 *
 * @param node Node to remove
 */
- (void)removeVersionNode:(MEGANode *)node;

/**
 * @brief Restore a previous version of a file
 *
 * Only versions of a file can be restored, not the current version (because it's already current).
 * The node will be copied and set as current. All the version history will be preserved without changes,
 * being the old current node the previous version of the new current node, and keeping the restored
 * node also in its previous place in the version history.
 *
 * The associated request type with this request is MEGARequestTypeRestore
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node to restore
 *
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
 *
 * @param node Node with the version to restore
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)restoreVersionNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Restore a previous version of a file
 *
 * Only versions of a file can be restored, not the current version (because it's already current).
 * The node will be copied and set as current. All the version history will be preserved without changes,
 * being the old current node the previous version of the new current node, and keeping the restored
 * node also in its previous place in the version history.
 *
 * The associated request type with this request is MEGARequestTypeRestore
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node to restore
 *
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
 *
 * @param node Node with the version to restore
 */
- (void)restoreVersionNode:(MEGANode *)node;

/**
 * @brief Clean the Rubbish Bin in the MEGA account
 *
 * This function effectively removes every node contained in the Rubbish Bin. In order to
 * avoid accidental deletions, you might want to warn the user about the action.
 *
 * The associated request type with this request is MEGARequestTypeCleanRubbishBin. This
 * request returns MEGAErrorTypeApiENoent if the Rubbish bin is already empty.
 *
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)cleanRubbishBinWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Clean the Rubbish Bin in the MEGA account
 *
 * This function effectively removes every node contained in the Rubbish Bin. In order to
 * avoid accidental deletions, you might want to warn the user about the action.
 *
 * The associated request type with this request is MEGARequestTypeCleanRubbishBin. This
 * request returns MEGAErrorTypeApiENoent if the Rubbish bin is already empty.
 *
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
 *
 */
- (void)cleanRubbishBin;

#pragma mark - Sharing Requests

/**
 * @brief Share or stop sharing a folder in MEGA with another user using a MEGAUser.
 *
 * To share a folder with an user, set the desired access level in the level parameter. If you
 * want to stop sharing a folder use the access level MEGAShareTypeAccessUnknown.
 *
 * The associated request type with this request is MEGARequestTypeShare.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the folder to share
 * - [MEGARequest email] - Returns the email of the user that receives the shared folder
 * - [MEGARequest access] - Returns the access that is granted to the user
 *
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
 *
 * @param node The folder to share. It must be a non-root folder.
 * @param user User that receives the shared folder.
 * @param level Permissions that are granted to the user.
 * Valid values for this parameter:
 * - MEGAShareTypeAccessUnknown = -1
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
 * want to stop sharing a folder use the access level MEGAShareTypeAccessUnknown.
 *
 * The associated request type with this request is MEGARequestTypeShare.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the folder to share
 * - [MEGARequest email] - Returns the email of the user that receives the shared folder
 * - [MEGARequest access] - Returns the access that is granted to the user
 *
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
 *
 * @param node The folder to share. It must be a non-root folder.
 * @param user User that receives the shared folder.
 * @param level Permissions that are granted to the user.
 * Valid values for this parameter:
 * - MEGAShareTypeAccessUnknown = -1
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
 * want to stop sharing a folder use the access level MEGAShareTypeAccessUnknown
 *
 * The associated request type with this request is MEGARequestTypeShare
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the folder to share
 * - [MEGARequest email] - Returns the email of the user that receives the shared folder
 * - [MEGARequest access] - Returns the access that is granted to the user
 *
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
 *
 * @param node The folder to share. It must be a non-root folder
 * @param email Email of the user that receives the shared folder. If it doesn't have a MEGA account, the folder will be shared anyway
 * and the user will be invited to register an account.
 *
 * @param level Permissions that are granted to the user
 * Valid values for this parameter:
 * - MEGAShareTypeAccessUnknown = -1
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
 * want to stop sharing a folder use the access level MEGAShareTypeAccessUnknown
 *
 * The associated request type with this request is MEGARequestTypeShare
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the folder to share
 * - [MEGARequest email] - Returns the email of the user that receives the shared folder
 * - [MEGARequest access] - Returns the access that is granted to the user
 *
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
 *
 * @param node The folder to share. It must be a non-root folder
 * @param email Email of the user that receives the shared folder. If it doesn't have a MEGA account, the folder will be shared anyway
 * and the user will be invited to register an account.
 *
 * @param level Permissions that are granted to the user
 * Valid values for this parameter:
 * - MEGAShareTypeAccessUnknown = -1
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
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
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
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
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
 * @brief Decrypt password-protected public link
 *
 * The associated request type with this request is MEGARequestTypePasswordLink
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest link] - Returns the encrypted public link to the file/folder
 * - [MEGARequest password] - Returns the password to decrypt the link
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest text] - Decrypted public link
 *
 * @param link Password/protected public link to a file/folder in MEGA
 * @param password Password to decrypt the link
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)decryptPasswordProtectedLink:(NSString *)link password:(NSString *)password delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Decrypt password-protected public link
 *
 * The associated request type with this request is MEGARequestTypePasswordLink
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest link] - Returns the encrypted public link to the file/folder
 * - [MEGARequest password] - Returns the password to decrypt the link
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest text] - Decrypted public link
 *
 * @param link Password/protected public link to a file/folder in MEGA
 * @param password Password to decrypt the link
 */
- (void)decryptPasswordProtectedLink:(NSString *)link password:(NSString *)password;

/**
 * @brief Encrypt public link with password
 *
 * The associated request type with this request is MEGARequestTypePasswordLink
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest link] - Returns the public link to be encrypted
 * - [MEGARequest password] - Returns the password to encrypt the link
 * - [MEGARequest flag] - Returns YES
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest text] - Encrypted public link
 *
 * @param link Public link to be encrypted, including encryption key for the link
 * @param password Password to encrypt the link
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)encryptLinkWithPassword:(NSString *)link password:(NSString *)password delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Encrypt public link with password
 *
 * The associated request type with this request is MEGARequestTypePasswordLink
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest link] - Returns the public link to be encrypted
 * - [MEGARequest password] - Returns the password to encrypt the link
 * - [MEGARequest flag] - Returns YES
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest text] - Encrypted public link
 *
 * @param link Public link to be encrypted, including encryption key for the link
 * @param password Password to encrypt the link
 */
- (void)encryptLinkWithPassword:(NSString *)link password:(NSString *)password;

/**
 * @brief Get a MEGANode from a public link to a file.
 *
 * A public node can be imported using [MEGASdk copyNode:newParent:] or downloaded using [MEGASdk startDownloadNode:localPath:]
 *
 * The associated request type with this request is MEGARequestTypeGetPublicNode.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest link] - Returns the public link to the file
 *
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
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
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest publicNode] - Public MEGANode corresponding to the public link
 *
 * @param megaFileLink Public link to a file in MEGA.
 */
- (void)publicNodeForMegaFileLink:(NSString *)megaFileLink;

/**
* @brief Build the URL for a public link
*
* @note This function does not create the public link itself. It simply builds the URL
* from the provided data.
*
* @param publicHandle Public handle of the link, in B64url encoding.
* @param key Encryption key of the link.
* @param isFolder True for folder links, false for file links.
* @return The public link for the provided data
*/
- (NSString *)buildPublicLinkForHandle:(NSString *)publicHandle key:(NSString *)key isFolder:(BOOL)isFolder;

/**
 * @brief Set the GPS coordinates of image files as a node attribute.
 *
 * To remove the existing coordinates, set both the latitude and longitude to nil.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrNode
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node that receive the attribute
 * - [MEGARequest flag] - Returns YES (official attribute)
 * - [MEGARequest paramType] - Returns MEGANodeAttributeCoordinates
 * - [MEGARequest numDetails] - Returns the longitude, scaled to integer in the range of [0, 2^24]
 * - [MEGARequest transferTag] - Returns the latitude, scaled to integer in the range of [0, 2^24)
 *
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
 *
 * @param node MEGANode that will receive the information.
 * @param latitude Latitude in signed decimal degrees notation.
 * @param longitude Longitude in signed decimal degrees notation.
 * @param delegate Delegate to track this request.
 */
- (void)setNodeCoordinates:(MEGANode *)node latitude:(nullable NSNumber *)latitude longitude:(nullable NSNumber *)longitude delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Set the GPS coordinates of image files as a node attribute.
 *
 * To remove the existing coordinates, set both the latitude and longitude to nil.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrNode
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node that receive the attribute
 * - [MEGARequest flag] - Returns YES (official attribute)
 * - [MEGARequest paramType] - Returns MEGANodeAttributeCoordinates
 * - [MEGARequest numDetails] - Returns the longitude, scaled to integer in the range of [0, 2^24]
 * - [MEGARequest transferTag] - Returns the latitude, scaled to integer in the range of [0, 2^24)
 *
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
 *
 * @param node MEGANode that will receive the information.
 * @param latitude Latitude in signed decimal degrees notation.
 * @param longitude Longitude in signed decimal degrees notation.
 */
- (void)setNodeCoordinates:(MEGANode *)node latitude:(nullable NSNumber *)latitude longitude:(nullable NSNumber *)longitude;

/**
 * @brief Set the GPS coordinates of image files as a node attribute.
 *
 * To remove the existing coordinates, set both the latitude and longitude to nil.
 *
 * The 'unshareable' variant of this function stores the coordinates with an extra
 * layer of encryption which only this user can decrypt, so that even if this node is shared
 * with others, they cannot read the coordinates.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrNode
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node that receive the attribute
 * - [MEGARequest flag] - Returns YES (official attribute)
 * - [MEGARequest paramType] - Returns MEGANodeAttributeCoordinates
 * - [MEGARequest numDetails] - Returns the longitude, scaled to integer in the range of [0, 2^24]
 * - [MEGARequest transferTag] - Returns the latitude, scaled to integer in the range of [0, 2^24)
 *
 * @param node MEGANode that will receive the information.
 * @param latitude Latitude in signed decimal degrees notation.
 * @param longitude Longitude in signed decimal degrees notation.
 * @param delegate Delegate to track this request.
 */
- (void)setUnshareableNodeCoordinates:(MEGANode *)node latitude:(nullable NSNumber *)latitude longitude:(nullable NSNumber *)longitude delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Generate a public link of a file/folder in MEGA.
 *
 * The associated request type with this request is MEGARequestTypeExport.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node
 * - [MEGARequest access] - Returns YES
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest link] - Public link
 *
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
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
 * - [MEGARequest access] - Returns YES
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest link] - Public link
 *
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
 *
 * @param node MEGANode to get the public link.
 */
- (void)exportNode:(MEGANode *)node;

/**
 * @brief Generate a public link of a file/folder in MEGA.
 *
 * The associated request type with this request is MEGARequestTypeExport.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node
 * - [MEGARequest access] - Returns YES
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest link] - Public link
 *
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
 *
 * @param node MEGANode to get the public link.
 * @param expireTime NSDate until the public link will be valid
 * @param delegate MEGARequestDelegate to track this request.
 */
- (void)exportNode:(MEGANode *)node expireTime:(NSDate *)expireTime delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Generate a public link of a file/folder in MEGA.
 *
 * The associated request type with this request is MEGARequestTypeExport.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node
 * - [MEGARequest access] - Returns YES
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest link] - Public link
 *
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
 *
 * @param node MEGANode to get the public link.
 * @param expireTime NSDate until the public link will be valid
 */
- (void)exportNode:(MEGANode *)node expireTime:(NSDate *)expireTime;

/**
 * @brief Stop sharing a file/folder.
 *
 * The associated request type with this request is MEGARequestTypeExport.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the node
 * - [MEGARequest access] - Returns NO
 *
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
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
 * - [MEGARequest access] - Returns NO
 *
 * If the MEGA account is a business account and it's status is expired, onRequestFinish will
 * be called with the error code MEGAErrorTypeApiEBusinessPastDue.
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
 * - [MEGARequest paramType] - Returns MEGAAttributeTypeThumbnail
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
 * @brief Get the avatar of any user in MEGA
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest file] - Returns the destination path
 * - [MEGARequest email] - Returns the email or the handle of the user (the provided one as parameter)
 *
 * @param emailOrHandle Email or user handle (Base64 encoded) to get the avatar. If this parameter is
 * set to nil, the avatar is obtained for the active account
 * @param destinationFilePath Destination path for the avatar. It has to be a path to a file, not to a folder.
 * If this path is a local folder, it must end with a '\' or '/' character and (email + "0.jpg")
 * will be used as the file name inside that folder. If the path doesn't finish with
 * one of these characters, the file will be downloaded to a file in that path.
 *
 * @param delegate MEGARequestDelegate to track this request.
 */
- (void)getAvatarUserWithEmailOrHandle:(nullable NSString *)emailOrHandle destinationFilePath:(NSString *)destinationFilePath delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get the avatar of any user in MEGA
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest file] - Returns the destination path
 * - [MEGARequest email] - Returns the email or the handle of the user (the provided one as parameter)
 *
 * @param emailOrHandle Email or user handle (Base64 encoded) to get the avatar. If this parameter is
 * set to nil, the avatar is obtained for the active account
 * @param destinationFilePath Destination path for the avatar. It has to be a path to a file, not to a folder.
 * If this path is a local folder, it must end with a '\' or '/' character and (email + "0.jpg")
 * will be used as the file name inside that folder. If the path doesn't finish with
 * one of these characters, the file will be downloaded to a file in that path.
 *
 */
- (void)getAvatarUserWithEmailOrHandle:(nullable NSString *)emailOrHandle destinationFilePath:(NSString *)destinationFilePath;

/**
 * @brief Get the default color for the avatar.
 *
 * This color should be used only when the user doesn't have an avatar.
 *
 * @param user MEGAUser to get the color of the avatar. If this parameter is set to nil, the color
 * is obtained for the active account.
 * @return The RGB color as a string with 3 components in hex: #RGB. Ie. "#FF6A19"
 * If the user is not found, this function always returns the same color.
 */
+ (nullable NSString *)avatarColorForUser:(nullable MEGAUser *)user;

/**
 * @brief Get the default color for the avatar.
 *
 * This color should be used only when the user doesn't have an avatar.
 *
 * @param base64UserHandle User handle (Base64 encoded) to get the avatar. If this parameter is
 * set to nil, the avatar is obtained for the active account.
 * @return The RGB color as a string with 3 components in hex: #RGB. Ie. "#FF6A19"
 * If the user is not found, this function always returns the same color.
 */
+ (nullable NSString *)avatarColorForBase64UserHandle:(nullable NSString *)base64UserHandle;

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
- (void)setAvatarUserWithSourceFilePath:(nullable NSString *)sourceFilePath delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Set the avatar of the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrFile.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest file] - Returns the source path
 *
 * @param sourceFilePath Source path of the file that will be set as avatar.
 */
- (void)setAvatarUserWithSourceFilePath:(nullable NSString *)sourceFilePath;

/**
 * @brief Get an attribute of a MEGAUser.
 *
 * User attributes can be private or public. Private attributes are accessible only by
 * your own user, while public ones are retrievable by any of your contacts.
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
 * Get the firstname of the user (public)
 * MEGAUserAttributeLastname = 2
 * Get the lastname of the user (public)
 * MEGAUserAttributeAuthRing = 3
 * Get the authentication ring of the user (private)
 * MEGAUserAttributeLastInteraction = 4
 * Get the last interaction of the contacts of the user (private)
 * MEGAUserAttributeED25519PublicKey = 5
 * Get the public key Ed25519 of the user (public)
 * MEGAUserAttributeCU25519PublicKey = 6
 * Get the public key Cu25519 of the user (public)
 * MEGAUserAttributeKeyring = 7
 * Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
 * MEGAUserAttributeSigRsaPublicKey = 8
 * Get the signature of RSA public key of the user (public)
 * MEGAUserAttributeSigCU255PublicKey = 9
 * Get the signature of Cu25519 public key of the user (public)
 * MEGAUserAttributeLanguage = 14
 * Get the preferred language of the user (private, non-encrypted)
 * MEGAUserAttributePwdReminder = 15
 * Get the password-reminder-dialog information (private, non-encrypted)
 * MEGAUserAttributeDisableVersions = 16
 * Get whether user has versions disabled or enabled (private, non-encrypted)
 * MEGAUserAttributeRichPreviews = 18
 * Get whether user generates rich-link messages or not (private)
 * MEGAUserAttributeRubbishTime = 19
 * Get number of days for rubbish-bin cleaning scheduler (private, non-encrypted)
 * MEGAUserAttributeStorageState = 21
 * Get the state of the storage (private non-encrypted)
 * MEGAUserAttributeGeolocation = 22
 * Get whether the user has enabled send geolocation messages (private)
 *
 */
- (void)getUserAttributeForUser:(nullable MEGAUser *)user type:(MEGAUserAttribute)type;

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
 * Get the firstname of the user (public)
 * MEGAUserAttributeLastname = 2
 * Get the lastname of the user (public)
 * MEGAUserAttributeAuthRing = 3
 * Get the authentication ring of the user (private)
 * MEGAUserAttributeLastInteraction = 4
 * Get the last interaction of the contacts of the user (private)
 * MEGAUserAttributeED25519PublicKey = 5
 * Get the public key Ed25519 of the user (public)
 * MEGAUserAttributeCU25519PublicKey = 6
 * Get the public key Cu25519 of the user (public)
 * MEGAUserAttributeKeyring = 7
 * Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
 * MEGAUserAttributeSigRsaPublicKey = 8
 * Get the signature of RSA public key of the user (public)
 * MEGAUserAttributeSigCU255PublicKey = 9
 * Get the signature of Cu25519 public key of the user (public)
 * MEGAUserAttributeLanguage = 14
 * Get the preferred language of the user (private, non-encrypted)
 * MEGAUserAttributePwdReminder = 15
 * Get the password-reminder-dialog information (private, non-encrypted)
 * MEGAUserAttributeDisableVersions = 16
 * Get whether user has versions disabled or enabled (private, non-encrypted)
 * MEGAUserAttributeRichPreviews = 18
 * Get whether user generates rich-link messages or not (private)
 * MEGAUserAttributeRubbishTime = 19
 * Get number of days for rubbish-bin cleaning scheduler (private, non-encrypted)
 * MEGAUserAttributeStorageState = 21
 * Get the state of the storage (private non-encrypted)
 * MEGAUserAttributeGeolocation = 22
 * Get whether the user has enabled send geolocation messages (private)
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)getUserAttributeForUser:(nullable MEGAUser *)user type:(MEGAUserAttribute)type delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get an attribute of any user in MEGA.
 *
 * User attributes can be private or public. Private attributes are accessible only by
 * your own user, while public ones are retrievable by any of your contacts.
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type
 * - [MEGARequest email] - Returns the email or the handle of the user (the provided one as parameter)
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest text] - Returns the value for public attributes
 *
 * @param emailOrHandle Email or user handle (Base64 encoded) to get the attribute.
 * @param type Attribute type
 *
 * Valid values are:
 *
 * MEGAUserAttributeFirstname = 1
 * Get the firstname of the user (public)
 * MEGAUserAttributeLastname = 2
 * Get the lastname of the user (public)
 * MEGAUserAttributeAuthRing = 3
 * Get the authentication ring of the user (private)
 * MEGAUserAttributeLastInteraction = 4
 * Get the last interaction of the contacts of the user (private)
 * MEGAUserAttributeED25519PublicKey = 5
 * Get the public key Ed25519 of the user (public)
 * MEGAUserAttributeCU25519PublicKey = 6
 * Get the public key Cu25519 of the user (public)
 * MEGAUserAttributeKeyring = 7
 * Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
 * MEGAUserAttributeSigRsaPublicKey = 8
 * Get the signature of RSA public key of the user (public)
 * MEGAUserAttributeSigCU255PublicKey = 9
 * Get the signature of Cu25519 public key of the user (public)
 * MEGAUserAttributeLanguage = 14
 * Get the preferred language of the user (private, non-encrypted)
 * MEGAUserAttributePwdReminder = 15
 * Get the password-reminder-dialog information (private, non-encrypted)
 * MEGAUserAttributeDisableVersions = 16
 * Get whether user has versions disabled or enabled (private, non-encrypted)
 * MEGAUserAttributeRichPreviews = 18
 * Get whether user generates rich-link messages or not (private)
 * MEGAUserAttributeRubbishTime = 19
 * Get number of days for rubbish-bin cleaning scheduler (private, non-encrypted)
 * MEGAUserAttributeStorageState = 21
 * Get the state of the storage (private non-encrypted)
 * MEGAUserAttributeGeolocation = 22
 * Get whether the user has enabled send geolocation messages (private)
 *
 */
- (void)getUserAttributeForEmailOrHandle:(NSString *)emailOrHandle type:(MEGAUserAttribute)type;

/**
 * @brief Get an attribute of any user in MEGA.
 *
 * User attributes can be private or public. Private attributes are accessible only by
 * your own user, while public ones are retrievable by any of your contacts.
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type
 * - [MEGARequest email] - Returns the email or the handle of the user (the provided one as parameter)
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest text] - Returns the value for public attributes
 *
 * @param emailOrHandle Email or user handle (Base64 encoded) to get the attribute.
 * @param type Attribute type
 *
 * Valid values are:
 *
 * MEGAUserAttributeFirstname = 1
 * Get the firstname of the user (public)
 * MEGAUserAttributeLastname = 2
 * Get the lastname of the user (public)
 * MEGAUserAttributeAuthRing = 3
 * Get the authentication ring of the user (private)
 * MEGAUserAttributeLastInteraction = 4
 * Get the last interaction of the contacts of the user (private)
 * MEGAUserAttributeED25519PublicKey = 5
 * Get the public key Ed25519 of the user (public)
 * MEGAUserAttributeCU25519PublicKey = 6
 * Get the public key Cu25519 of the user (public)
 * MEGAUserAttributeKeyring = 7
 * Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
 * MEGAUserAttributeSigRsaPublicKey = 8
 * Get the signature of RSA public key of the user (public)
 * MEGAUserAttributeSigCU255PublicKey = 9
 * Get the signature of Cu25519 public key of the user (public)
 * MEGAUserAttributeLanguage = 14
 * Get the preferred language of the user (private, non-encrypted)
 * MEGAUserAttributePwdReminder = 15
 * Get the password-reminder-dialog information (private, non-encrypted)
 * MEGAUserAttributeDisableVersions = 16
 * Get whether user has versions disabled or enabled (private, non-encrypted)
 * MEGAUserAttributeRichPreviews = 18
 * Get whether user generates rich-link messages or not (private)
 * MEGAUserAttributeRubbishTime = 19
 * Get number of days for rubbish-bin cleaning scheduler (private, non-encrypted)
 * MEGAUserAttributeStorageState = 21
 * Get the state of the storage (private non-encrypted)
 * MEGAUserAttributeGeolocation = 22
 * Get whether the user has enabled send geolocation messages (private)
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)getUserAttributeForEmailOrHandle:(NSString *)emailOrHandle type:(MEGAUserAttribute)type delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get an attribute of the current account.
 *
 * User attributes can be private or public. Private attributes are accessible only by
 * your own user, while public ones are retrievable by any of your contacts.
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
 * Get the firstname of the user (public)
 * MEGAUserAttributeLastname = 2
 * Get the lastname of the user (public)
 * MEGAUserAttributeAuthRing = 3
 * Get the authentication ring of the user (private)
 * MEGAUserAttributeLastInteraction = 4
 * Get the last interaction of the contacts of the user (private)
 * MEGAUserAttributeED25519PublicKey = 5
 * Get the public key Ed25519 of the user (public)
 * MEGAUserAttributeCU25519PublicKey = 6
 * Get the public key Cu25519 of the user (public)
 * MEGAUserAttributeKeyring = 7
 * Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
 * MEGAUserAttributeSigRsaPublicKey = 8
 * Get the signature of RSA public key of the user (public)
 * MEGAUserAttributeSigCU255PublicKey = 9
 * Get the signature of Cu25519 public key of the user (public)
 * MEGAUserAttributeLanguage = 14
 * Get the preferred language of the user (private, non-encrypted)
 * MEGAUserAttributePwdReminder = 15
 * Get the password-reminder-dialog information (private, non-encrypted)
 * MEGAUserAttributeDisableVersions = 16
 * Get whether user has versions disabled or enabled (private, non-encrypted)
 * MEGAUserAttributeRichPreviews = 18
 * Get whether user generates rich-link messages or not (private)
 * MEGAUserAttributeRubbishTime = 19
 * Get number of days for rubbish-bin cleaning scheduler (private, non-encrypted)
 * MEGAUserAttributeStorageState = 21
 * Get the state of the storage (private non-encrypted)
 * MEGAUserAttributeGeolocation = 22
 * Get whether the user has enabled send geolocation messages (private)
 *
 */
- (void)getUserAttributeType:(MEGAUserAttribute)type;

/**
 * @brief Get an attribute of the current account.
 *
 * User attributes can be private or public. Private attributes are accessible only by
 * your own user, while public ones are retrievable by any of your contacts.
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
 * Get the firstname of the user (public)
 * MEGAUserAttributeLastname = 2
 * Get the lastname of the user (public)
 * MEGAUserAttributeAuthRing = 3
 * Get the authentication ring of the user (private)
 * MEGAUserAttributeLastInteraction = 4
 * Get the last interaction of the contacts of the user (private)
 * MEGAUserAttributeED25519PublicKey = 5
 * Get the public key Ed25519 of the user (public)
 * MEGAUserAttributeCU25519PublicKey = 6
 * Get the public key Cu25519 of the user (public)
 * MEGAUserAttributeKeyring = 7
 * Get the key ring of the user: private keys for Cu25519 and Ed25519 (private)
 * MEGAUserAttributeSigRsaPublicKey = 8
 * Get the signature of RSA public key of the user (public)
 * MEGAUserAttributeSigCU255PublicKey = 9
 * Get the signature of Cu25519 public key of the user (public)
 * MEGAUserAttributeLanguage = 14
 * Get the preferred language of the user (private, non-encrypted)
 * MEGAUserAttributePwdReminder = 15
 * Get the password-reminder-dialog information (private, non-encrypted)
 * MEGAUserAttributeDisableVersions = 16
 * Get whether user has versions disabled or enabled (private, non-encrypted)
 * MEGAUserAttributeRichPreviews = 18
 * Get whether user generates rich-link messages or not (private)
 * MEGAUserAttributeRubbishTime = 19
 * Get number of days for rubbish-bin cleaning scheduler (private, non-encrypted)
 * MEGAUserAttributeStorageState = 21
 * Get the state of the storage (private non-encrypted)
 * MEGAUserAttributeGeolocation = 22
 * Get whether the user has enabled send geolocation messages (private)
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)getUserAttributeType:(MEGAUserAttribute)type delegate:(id<MEGARequestDelegate>)delegate;

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
 * Set the firstname of the user
 * MEGAUserAttributeLastname = 2
 * Set the lastname of the user
 * MEGAUserAttributeRubbishTime = 19
 * Set the number of days for rubbish-bin cleaning scheduler (private, non-encrypted)
 *
 * If the MEGA account is a sub-user business account, and the value of the parameter
 * type is equal to MEGAUserAttributeFirstname or MEGAUserAttributeLastname
 * be called with the error code MEGAErrorTypeApiEMasterOnly.
 *
 * @param value New attribute value
 */
- (void)setUserAttributeType:(MEGAUserAttribute)type value:(NSString *)value;

/**
 * @brief Gets the alias for an user
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeAlias
 * - [MEGARequest nodeHandle] - Returns the handle of the node as binary
 * - [MEGARequest text] - Return the handle of the node as base 64 string.
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest name] - Returns the user alias.
 *
 * If the user alias doesn't exists the request will fail with the error code MEGAErrorTypeApiENoent
 *
 * @param handle Handle of the contact
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)getUserAliasWithHandle:(uint64_t)handle delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Gets the alias for an user
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeAlias
 * - [MEGARequest nodeHandle] - Returns the handle of the node as binary
 * - [MEGARequest text] - Return the handle of the node as base64 string.
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest name] - Returns the user alias.
 *
 * If the user alias doesn't exists the request will fail with the error code MEGAErrorTypeApiENoent
 *
 * @param handle Handle of the contact
 */
- (void)getUserAliasWithHandle:(uint64_t)handle;

/**
 * @brief Set or reset an alias for a user
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeAlias
 * - [MEGARequest nodeHandle] - Returns the handle of the node as binary
 * - [MEGARequest text] - Return the handle of the node as base 64 string.
 *
 * @param alias the user alias, or null to reset the existing
 * @param handle Handle of the contact
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)setUserAlias:(nullable NSString *)alias forHandle:(uint64_t)handle delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Set or reset an alias for a user
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeAlias
 * - [MEGARequest nodeHandle] - Returns the handle of the node as binary
 * - [MEGARequest text] - Return the handle of the node as base 64 string.
 *
 * @param alias the user alias, or null to reset the existing
 * @param handle Handle of the contact
 */
- (void)setUserAlias:(nullable NSString *)alias forHandle:(uint64_t)handle;

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
 * Set the firstname of the user
 * MEGAUserAttributeLastname = 2
 * Set the lastname of the user
 * MEGAUserAttributeRubbishTime = 19
 * Set the number of days for rubbish-bin cleaning scheduler (private, non-encrypted)
 *
 * If the MEGA account is a sub-user business account, and the value of the parameter
 * type is equal to MEGAUserAttributeFirstname or MEGAUserAttributeLastname
 * be called with the error code MEGAErrorTypeApiEMasterOnly.
 *
 * @param value New attribute value
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)setUserAttributeType:(MEGAUserAttribute)type value:(NSString *)value delegate:(id<MEGARequestDelegate>)delegate;

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
 * @brief Check if the available bandwidth quota is enough to transfer an amount of bytes
 *
 * The associated request type with this request is MEGARequestTypeQueryTransferQuota
 *
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest number] - Returns the amount of bytes to be transferred
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest flag] - YES if it is expected to get an overquota error, otherwise NO
 *
 * @param size Amount of bytes to be transferred
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)queryTransferQuotaWithSize:(long long)size delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Check if the available bandwidth quota is enough to transfer an amount of bytes
 *
 * The associated request type with this request is MEGARequestTypeQueryTransferQuota
 *
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest number] - Returns the amount of bytes to be transferred
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest flag] - YES if it is expected to get an overquota error, otherwise NO
 *
 * @param size Amount of bytes to be transferred
 */
- (void)queryTransferQuotaWithSize:(long long)size;

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
 * @brief Get the payment URL for an upgrade
 *
 * The associated request type with this request is MEGARequestTypeGetPaymentId
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the product
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest link] - Payment ID
 * - [MEGARequest parentHandle] - Returns the last public node handle accessed
 * - [MEGARequest paramType] - Returns the type of lastPublicHandle
 * - [MEGARequest transferredBytes] - Returns the timestamp of the last access
 *
 * @param productHandle Handle of the product (see [MEGASdk getPricing])
 * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
 * @param lastPublicHandleType Indicates the type of lastPublicHandle, valid values are:
 *      - AffiliateTypeId = 1
 *      - AffiliateTypeFileFolder = 2
 *      - AffiliateTypeChat = 3
 *      - AffiliateTypeContact = 4
 *
 * @param lastAccessTimestamp Timestamp of the last access
 * @param delegate Delegate to track this request
 *
 * @see [MEGASdk getPricing]
 */
- (void)getPaymentIdForProductHandle:(uint64_t)productHandle lastPublicHandle:(uint64_t)lastPublicHandle lastPublicHandleType:(AffiliateType)lastPublicHandleType lastAccessTimestamp:(uint64_t)lastAccessTimestamp delegate:(id<MEGARequestDelegate>)delegate;

/**
* @brief Get the payment URL for an upgrade
*
* The associated request type with this request is MEGARequestTypeGetPaymentId
* Valid data in the MEGARequest object received on callbacks:
* - [MEGARequest nodeHandle] - Returns the handle of the product
*
* Valid data in the MEGARequest object received in onRequestFinish when the error code
* is MEGAErrorTypeApiOk:
* - [MEGARequest link] - Payment ID
* - [MEGARequest parentHandle] - Returns the last public node handle accessed
* - [MEGARequest paramType] - Returns the type of lastPublicHandle
* - [MEGARequest transferredBytes] - Returns the timestamp of the last access
*
* @param productHandle Handle of the product (see [MEGASdk getPricing])
* @param lastPublicHandle Last public node handle accessed by the user in the last 24h
* @param lastPublicHandleType Indicates the type of lastPublicHandle, valid values are:
*      - AffiliateTypeId = 1
*      - AffiliateTypeFileFolder = 2
*      - AffiliateTypeChat = 3
*      - AffiliateTypeContact = 4
*
* @param lastAccessTimestamp Timestamp of the last access
*
* @see [MEGASdk getPricing]
*/
- (void)getPaymentIdForProductHandle:(uint64_t)productHandle lastPublicHandle:(uint64_t)lastPublicHandle lastPublicHandleType:(AffiliateType)lastPublicHandleType lastAccessTimestamp:(uint64_t)lastAccessTimestamp;

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
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest number] - Returns the payment gateway
 * - [MEGARequest text] - Returns the purchase receipt
 * - [MEGARequest parentHandle] - Returns the last public node handle accessed
 *
 * @param gateway Payment gateway
 * Currently supported payment gateways are:
 * - MEGAPaymentMethodItunes = 2
 * - MEGAPaymentMethodGoogleWallet = 3
 * - MEGAPaymentMethodWindowsStore = 13
 *
 * @param receipt Purchase receipt
 * @param delegate Delegate to track this request
 */
- (void)submitPurchase:(MEGAPaymentMethod)gateway receipt:(NSString *)receipt delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Submit a purchase receipt for verification
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest number] - Returns the payment gateway
 * - [MEGARequest text] - Returns the purchase receipt
 * - [MEGARequest parentHandle] - Returns the last public node handle accessed
 *
 * @param gateway Payment gateway
 * Currently supported payment gateways are:
 * - MEGAPaymentMethodItunes = 2
 * - MEGAPaymentMethodGoogleWallet = 3
 * - MEGAPaymentMethodWindowsStore = 13
 *
 * @param receipt Purchase receipt
 */
- (void)submitPurchase:(MEGAPaymentMethod)gateway receipt:(NSString *)receipt;

/**
 * @brief Submit a purchase receipt for verification
 *
 * The associated request type with this request is MEGARequestTypeSubmitPurchaseReceipt
 *
 * @param gateway Payment gateway
 * Currently supported payment gateways are:
 * Currently supported payment gateways are:
 * - MEGAPaymentMethodItunes = 2
 *
 * @param receipt Purchase receipt
 * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
 * @param delegate Delegate to track this request
 */
- (void)submitPurchase:(MEGAPaymentMethod)gateway receipt:(NSString *)receipt lastPublicHandle:(uint64_t)lastPublicHandle delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Submit a purchase receipt for verification
 *
 * The associated request type with this request is MEGARequestTypeSubmitPurchaseReceipt
 *
 * @param gateway Payment gateway
 * Currently supported payment gateways are:
 * Currently supported payment gateways are:
 * - MEGAPaymentMethodItunes = 2
 *
 * @param receipt Purchase receipt
 * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
 */
- (void)submitPurchase:(MEGAPaymentMethod)gateway receipt:(NSString *)receipt lastPublicHandle:(uint64_t)lastPublicHandle;

/**
 * @brief Submit a purchase receipt for verification
 *
 * The associated request type with this request is MEGARequestTypeSubmitPurchaseReceipt
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest number] - Returns the payment gateway
 * - [MEGARequest text] - Returns the purchase receipt
 * - [MEGARequest parentHandle] - Returns the last public node handle accessed
 * - [MEGARequest paramType] - Returns the type of lastPublicHandle
 * - [MEGARequest transferredBytes] - Returns the timestamp of the last access
 *
 * @param gateway Payment gateway
 * Currently supported payment gateways are:
 * - MEGAPaymentMethodItunes = 2
 * - MEGAPaymentMethodGoogleWallet = 3
 * - MEGAPaymentMethodWindowsStore = 13
 *
 * @param receipt Purchase receipt
 * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
 * @param lastPublicHandleType Indicates the type of lastPublicHandle, valid values are:
 *      - AffiliateTypeId = 1
 *      - AffiliateTypeFileFolder = 2
 *      - AffiliateTypeChat = 3
 *      - AffiliateTypeContact = 4
 *
 * @param lastAccessTimestamp Timestamp of the last access
 * @param delegate Delegate to track this request
 */
- (void)submitPurchase:(MEGAPaymentMethod)gateway receipt:(NSString *)receipt lastPublicHandle:(uint64_t)lastPublicHandle lastPublicHandleType:(AffiliateType)lastPublicHandleType lastAccessTimestamp:(uint64_t)lastAccessTimestamp delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Submit a purchase receipt for verification
 *
 * The associated request type with this request is MEGARequestTypeSubmitPurchaseReceipt
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest number] - Returns the payment gateway
 * - [MEGARequest text] - Returns the purchase receipt
 * - [MEGARequest parentHandle] - Returns the last public node handle accessed
 * - [MEGARequest paramType] - Returns the type of lastPublicHandle
 * - [MEGARequest transferredBytes] - Returns the timestamp of the last access
 *
 * @param gateway Payment gateway
 * Currently supported payment gateways are:
 * - MEGAPaymentMethodItunes = 2
 * - MEGAPaymentMethodGoogleWallet = 3
 * - MEGAPaymentMethodWindowsStore = 13
 *
 * @param receipt Purchase receipt
 * @param lastPublicHandle Last public node handle accessed by the user in the last 24h
 * @param lastPublicHandleType Indicates the type of lastPublicHandle, valid values are:
 *      - AffiliateTypeId = 1
 *      - AffiliateTypeFileFolder = 2
 *      - AffiliateTypeChat = 3
 *      - AffiliateTypeContact = 4
 *
 * @param lastAccessTimestamp Timestamp of the last access
 */
- (void)submitPurchase:(MEGAPaymentMethod)gateway receipt:(NSString *)receipt lastPublicHandle:(uint64_t)lastPublicHandle lastPublicHandleType:(AffiliateType)lastPublicHandleType lastAccessTimestamp:(uint64_t)lastAccessTimestamp;

/**
 * @brief Change the password of the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeChangePassword.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest password] - Returns the old password
 * - [MEGARequest newPassword] - Returns the new password
 *
 * @param oldPassword Old password (optional, it can be nil to not check the old password).
 * @param newPassword New password.
 * @param delegate Delegate to track this request.
 */
- (void)changePassword:(nullable NSString *)oldPassword newPassword:(NSString *)newPassword delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Change the password of the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeChangePassword.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest password] - Returns the old password
 * - [MEGARequest newPassword] - Returns the new password
 *
 * @param oldPassword Old password (optional, it can be nil to not check the old password).
 * @param newPassword New password.
 */
- (void)changePassword:(nullable NSString *)oldPassword newPassword:(NSString *)newPassword;

/**
 * @brief Notify the user has exported the master key
 *
 * This function should be called when the user exports the master key by
 * clicking on "Copy" or "Save file" options.
 *
 * As result, the user attribute MEGAUserAttributePwdReminder will be updated
 * to remember the user has a backup of his/her master key. In consequence,
 * MEGA will not ask the user to remind the password for the account.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributePwdReminder
 * - [MEGARequest: text] - Returns the new value for the attribute
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)masterKeyExportedWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Notify the user has exported the master key
 *
 * This function should be called when the user exports the master key by
 * clicking on "Copy" or "Save file" options.
 *
 * As result, the user attribute MEGAUserAttributePwdReminder will be updated
 * to remember the user has a backup of his/her master key. In consequence,
 * MEGA will not ask the user to remind the password for the account.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributePwdReminder
 * - [MEGARequest text] - Returns the new value for the attribute
 */
- (void)masterKeyExported;

/**
 * @brief Notify the user has successfully checked his password
 *
 * This function should be called when the user demonstrates that he remembers
 * the password to access the account
 *
 * As result, the user attribute MEGAUserAttributePwdReminder will be updated
 * to remember this event. In consequence, MEGA will not continue asking the user
 * to remind the password for the account in a short time.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributePwdReminder
 * - [MEGARequest text] - Returns the new value for the attribute
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)passwordReminderDialogSucceededWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Notify the user has successfully checked his password
 *
 * This function should be called when the user demonstrates that he remembers
 * the password to access the account
 *
 * As result, the user attribute MEGAUserAttributePwdReminder will be updated
 * to remember this event. In consequence, MEGA will not continue asking the user
 * to remind the password for the account in a short time.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributePwdReminder
 * - [MEGARequest text] - Returns the new value for the attribute
 */
- (void)passwordReminderDialogSucceeded;

/**
 * @brief Notify the user has successfully skipped the password check
 *
 * This function should be called when the user skips the verification of
 * the password to access the account
 *
 * As result, the user attribute MEGAUserAttributePwdReminder will be updated
 * to remember this event. In consequence, MEGA will not continue asking the user
 * to remind the password for the account in a short time.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributePwdReminder
 * - [MEGARequest text] - Returns the new value for the attribute
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)passwordReminderDialogSkippedWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Notify the user has successfully skipped the password check
 *
 * This function should be called when the user skips the verification of
 * the password to access the account
 *
 * As result, the user attribute MEGAUserAttributePwdReminder will be updated
 * to remember this event. In consequence, MEGA will not continue asking the user
 * to remind the password for the account in a short time.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributePwdReminder
 * - [MEGARequest text] - Returns the new value for the attribute
 */
- (void)passwordReminderDialogSkipped;

/**
 * @brief Notify the user wants to totally disable the password check
 *
 * This function should be called when the user rejects to verify that he remembers
 * the password to access the account and doesn't want to see the reminder again.
 *
 * As result, the user attribute MEGAUserAttributePwdReminder will be updated
 * to remember this event. In consequence, MEGA will not ask the user
 * to remind the password for the account again.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributePwdReminder
 * - [MEGARequest text] - Returns the new value for the attribute
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)passwordReminderDialogBlockedWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Notify the user wants to totally disable the password check
 *
 * This function should be called when the user rejects to verify that he remembers
 * the password to access the account and doesn't want to see the reminder again.
 *
 * As result, the user attribute MEGAUserAttributePwdReminder will be updated
 * to remember this event. In consequence, MEGA will not ask the user
 * to remind the password for the account again.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributePwdReminder
 * - [MEGARequest text] - Returns the new value for the attribute
 */
- (void)passwordReminderDialogBlocked;

/**
 * @brief Check if the app should show the password reminder dialog to the user
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributePwdReminder
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest flag] - Returns YES if the password reminder dialog should be shown
 *
 * If the corresponding user attribute is not set yet, the request will fail with the
 * error code MEGAErrorTypeApiENoent but the value of [MEGARequest flag] will still
 * be valid.
 *
 * @param atLogout YES if the check is being done just before a logout
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)shouldShowPasswordReminderDialogAtLogout:(BOOL)atLogout delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Check if the app should show the password reminder dialog to the user
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributePwdReminder
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest flag] - Returns YES if the password reminder dialog should be shown
 *
 * If the corresponding user attribute is not set yet, the request will fail with the
 * error code MEGAErrorTypeApiENoent but the value of  [MEGARequest flag] will still
 * be valid.
 *
 * @param atLogout YES if the check is being done just before a logout
 */
- (void)shouldShowPasswordReminderDialogAtLogout:(BOOL)atLogout;

/**
 * @brief Check if the master key has been exported
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributePwdReminder
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest access] - Returns YES if the master key has been exported
 *
 * If the corresponding user attribute is not set yet, the request will fail with the
 * error code MEGAErrorTypeApiENoent.
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)isMasterKeyExportedWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Check if the master key has been exported
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributePwdReminder
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest access] - Returns YES if the master key has been exported
 *
 * If the corresponding user attribute is not set yet, the request will fail with the
 * error code MEGAErrorTypeApiENoent.
 *
 */
- (void)isMasterKeyExported;

/**
 * @brief Enable or disable the generation of rich previews
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeRichPreviews
 *
 * @param enable YES to enable the generation of rich previews
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)enableRichPreviews:(BOOL)enable delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Enable or disable the generation of rich previews
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeRichPreviews
 *
 * @param enable YES to enable the generation of rich previews
 */
- (void)enableRichPreviews:(BOOL)enable;

/**
 * @brief Check if rich previews are automatically generated
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeRichPreviews
 * - [MEGARequest numDetails] - Returns zero
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest flag] - Returns YES if generation of rich previews is enabled
 *
 * If the corresponding user attribute is not set yet, the request will fail with the
 * error code MEGAErrorTypeApiENoent, but the value of [MEGARequest flag] will still be valid (NO).
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)isRichPreviewsEnabledWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Check if rich previews are automatically generated
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeRichPreviews
 * - [MEGARequest numDetails] - Returns zero
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest flag] - Returns YES if generation of rich previews is enabled
 *
 * If the corresponding user attribute is not set yet, the request will fail with the
 * error code MEGAErrorTypeApiENoent, but the value of [MEGARequest flag] will still be valid (NO).
 *
 */
- (void)isRichPreviewsEnabled;

/**
 * @brief Check if the app should show the rich link warning dialog to the user
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeRichPreviews
 * - [MEGARequest numDetails] - Returns one
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest flag] - Returns YES if it is necessary to show the rich link warning
 * - [MEGARequest number] - Returns the number of times that user has indicated that doesn't want
 * modify the message with a rich link. If number is bigger than three, the extra option "Never"
 * must be added to the warning dialog.
 *
 * If the corresponding user attribute is not set yet, the request will fail with the
 * error code MEGAErrorTypeApiENoent, but the value of [MEGARequest flag] will still be valid (YES).
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)shouldShowRichLinkWarningWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Check if the app should show the rich link warning dialog to the user
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeRichPreviews
 * - [MEGARequest numDetails] - Returns one
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest flag] - Returns YES if it is necessary to show the rich link warning
 * - [MEGARequest number] - Returns the number of times that user has indicated that doesn't want
 * modify the message with a rich link. If number is bigger than three, the extra option "Never"
 * must be added to the warning dialog.
 *
 * If the corresponding user attribute is not set yet, the request will fail with the
 * error code MEGAErrorTypeApiENoent, but the value of [MEGARequest flag] will still be valid (YES).
 *
 */
- (void)shouldShowRichLinkWarning;

/**
 * @brief Set the number of times "Not now" option has been selected in the rich link warning dialog
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeRichPreviews
 *
 * @param value Number of times "Not now" option has been selected
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)setRichLinkWarningCounterValue:(NSUInteger)value delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Set the number of times "Not now" option has been selected in the rich link warning dialog
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeRichPreviews
 *
 * @param value Number of times "Not now" option has been selected
 */
- (void)setRichLinkWarningCounterValue:(NSUInteger)value;

/**
 * @brief Enable the sending of geolocation messages
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeGeolocation
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)enableGeolocationWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Enable the sending of geolocation messages
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeGeolocation
 */
- (void)enableGeolocation;

/**
 * @brief Check if the sending of geolocation messages is enabled
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeGeolocation
 *
 * Sending a Geolocation message is enabled if the MEGARequest object, received in onRequestFinish,
 * has error code MEGAErrorTypeApiOk. In other cases, send geolocation messages is not enabled and
 * the application has to answer before send a message of this type.
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)isGeolocationEnabledWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Check if the sending of geolocation messages is enabled
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeGeolocation
 *
 * Sending a Geolocation message is enabled if the MEGARequest object, received in onRequestFinish,
 * has error code MEGAErrorTypeApiOk. In other cases, send geolocation messages is not enabled and
 * the application has to answer before send a message of this type.
 */
- (void)isGeolocationEnabled;

/**
 * @brief Set My Chat Files target folder.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeMyChatFilesFolder
 * - [MEGARequest megaStringDictionary] - Returns a megaStringDictionary.
 * The key "h" in the map contains the nodehandle specified as parameter encoded in B64
 *
 * @param handle Handle of the node to be used as target folder
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)setMyChatFilesFolderWithHandle:(uint64_t)handle delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Set My Chat Files target folder.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeMyChatFilesFolder
 * - [MEGARequest megaStringDictionary] - Returns a megaStringDictionary.
 * The key "h" in the map contains the nodehandle specified as parameter encoded in B64
 *
 * @param handle Handle of the node to be used as target folder
 */
- (void)setMyChatFilesFolderWithHandle:(uint64_t)handle;

/**
 * @brief Gets My chat files target folder.
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeMyChatFilesFolder
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest nodeHandle] - Returns the handle of the node where My Chat Files are stored
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)getMyChatFilesFolderWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Gets My chat files target folder.
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeMyChatFilesFolder
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest nodeHandle] - Returns the handle of the node where My Chat Files are stored
 */
- (void)getMyChatFilesFolder;

/**
 * @brief Set Camera Uploads target folder.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeCameraUploadsFolder
 * - [MEGARequest megaStringDictionary] - Returns a megaStringDictionary.
 * The key "h" in the map contains the nodehandle specified as parameter encoded in B64
 *
 * @param handle Handle of the node to be used as target folder
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)setCameraUploadsFolderWithHandle:(uint64_t)handle delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Set Camera Uploads target folder.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeCameraUploadsFolder
 * - [MEGARequest megaStringDictionary] - Returns a megaStringDictionary.
 * The key "h" in the map contains the nodehandle specified as parameter encoded in B64
 *
 * @param handle Handle of the node to be used as target folder
 */
- (void)setCameraUploadsFolderWithHandle:(uint64_t)handle;

/**
 * @brief Gets Camera Uploads target folder.
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeCameraUploadsFolder
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest nodeHandle] - Returns the handle of the node where Camera Uploads files are stored
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)getCameraUploadsFolderWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Gets Camera Uploads target folder.
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeCameraUploadsFolder
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest nodeHandle] - Returns the handle of the node where Camera Uploads files are stored
 */
- (void)getCameraUploadsFolder;

/**
 * @brief Get the number of days for rubbish-bin cleaning scheduler
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeRubbishTime
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest number] - Returns the days for rubbish-bin cleaning scheduler.
 * Zero means that the rubbish-bin cleaning scheduler is disabled (only if the account is PRO)
 * Any negative value means that the configured value is invalid.
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)getRubbishBinAutopurgePeriodWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get the number of days for rubbish-bin cleaning scheduler
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeRubbishTime
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest number] - Returns the days for rubbish-bin cleaning scheduler.
 * Zero means that the rubbish-bin cleaning scheduler is disabled (only if the account is PRO)
 * Any negative value means that the configured value is invalid.
 *
 */
- (void)getRubbishBinAutopurgePeriod;

/**
 * @brief Set the number of days for rubbish-bin cleaning scheduler
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeRubbishTime
 * - [MEGARequest number] - Returns the days for rubbish-bin cleaning scheduler passed as parameter
 *
 * @param days Number of days for rubbish-bin cleaning scheduler. It must be >= 0.
 * The value zero disables the rubbish-bin cleaning scheduler (only for PRO accounts).
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)setRubbishBinAutopurgePeriodInDays:(NSInteger)days delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Set the number of days for rubbish-bin cleaning scheduler
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributeRubbishTime
 * - [MEGARequest number] - Returns the days for rubbish-bin cleaning scheduler passed as parameter
 *
 * @param days Number of days for rubbish-bin cleaning scheduler. It must be >= 0.
 * The value zero disables the rubbish-bin cleaning scheduler (only for PRO accounts).
 *
 */
- (void)setRubbishBinAutopurgePeriodInDays:(NSInteger)days;

/**
 * @brief Use HTTPS communications only
 *
 * The default behavior is to use HTTP for transfers and the persistent connection
 * to wait for external events. Those communications don't require HTTPS because
 * all transfer data is already end-to-end encrypted and no data is transmitted
 * over the connection to wait for events (it's just closed when there are new events).
 *
 * This feature should only be enabled if there are problems to contact MEGA servers
 * through HTTP because otherwise it doesn't have any benefit and will cause a
 * higher CPU usage.
 *
 * See [MEGASdk usingHttpsOnly]
 *
 * @param httpsOnly YES to use HTTPS communications only
 * @param delegate MEGARequestDelegate to track this request.
 */
- (void)useHttpsOnly:(BOOL)httpsOnly delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Use HTTPS communications only
 *
 * The default behavior is to use HTTP for transfers and the persistent connection
 * to wait for external events. Those communications don't require HTTPS because
 * all transfer data is already end-to-end encrypted and no data is transmitted
 * over the connection to wait for events (it's just closed when there are new events).
 *
 * This feature should only be enabled if there are problems to contact MEGA servers
 * through HTTP because otherwise it doesn't have any benefit and will cause a
 * higher CPU usage.
 *
 * See [MEGASdk usingHttpsOnly]
 *
 * @param httpsOnly YES to use HTTPS communications only
 */
- (void)useHttpsOnly:(BOOL)httpsOnly;

/**
 * @brief Check if the SDK is using HTTPS communications only
 *
 * The default behavior is to use HTTP for transfers and the persistent connection
 * to wait for external events. Those communications don't require HTTPS because
 * all transfer data is already end-to-end encrypted and no data is transmitted
 * over the connection to wait for events (it's just closed when there are new events).
 *
 * See [MEGASdk useHttpsOnly:]
 *
 * @return YES if the SDK is using HTTPS communications only. Otherwise NO.
 */
- (BOOL)usingHttpsOnly;

/**
 * @brief Invite another person to be your MEGA contact
 *
 * The user doesn't need to be registered on MEGA. If the email isn't associated with
 * a MEGA account, an invitation email will be sent with the text in the "message" parameter.
 *
 * The associated request type with this request is MEGARequestTypeInviteContact
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email of the contact
 * - [MEGARequest text] - Returns the text of the invitation
 * - [MEGARequest number] - Returns the action
 *
 * Sending a reminder within a two week period since you started or your last reminder will
 * fail the API returning the error code MEGAErrorTypeApiEAccess.
 *
 * @param email Email of the new contact
 * @param message Message for the user (can be nil)
 * @param action Action for this contact request. Valid values are:
 * - MEGAInviteActionAdd = 0
 * - MEGAInviteActionDelete = 1
 * - MEGAInviteActionRemind = 2
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)inviteContactWithEmail:(NSString *)email message:(nullable NSString *)message action:(MEGAInviteAction)action delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Invite another person to be your MEGA contact
 *
 * The user doesn't need to be registered on MEGA. If the email isn't associated with
 * a MEGA account, an invitation email will be sent with the text in the "message" parameter.
 *
 * The associated request type with this request is MEGARequestTypeInviteContact
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email of the contact
 * - [MEGARequest text] - Returns the text of the invitation
 * - [MEGARequest number] - Returns the action
 *
 * Sending a reminder within a two week period since you started or your last reminder will
 * fail the API returning the error code MEGAErrorTypeApiEAccess.
 *
 * @param email Email of the new contact
 * @param message Message for the user (can be nil)
 * @param action Action for this contact request. Valid values are:
 * - MEGAInviteActionAdd = 0
 * - MEGAInviteActionDelete = 1
 * - MEGAInviteActionRemind = 2
 *
 */
- (void)inviteContactWithEmail:(NSString *)email message:(nullable NSString *)message action:(MEGAInviteAction)action;

/**
 * @brief Invite another person to be your MEGA contact using a contact link handle
 *
 * The associated request type with this request is MEGARequestTypeInviteContact
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email of the contact
 * - [MEGARequest text] - Returns the text of the invitation
 * - [MEGARequest number] - Returns the action
 * - [MEGARequest nodeHandle] - Returns the contact link handle
 *
 * Sending a reminder within a two week period since you started or your last reminder will
 * fail the API returning the error code MEGAErrorTypeApiEAccess.
 *
 * @param email Email of the new contact
 * @param message Message for the user (can be nil)
 * @param action Action for this contact request. Valid values are:
 * - MEGAInviteActionAdd = 0
 * - MEGAInviteActionDelete = 1
 * - MEGAInviteActionRemind = 2
 *
 * @param handle Contact link handle of the other account. This parameter is considered only if the
 * \c action is MEGAInviteActionAdd. Otherwise, it's ignored and it has no effect.
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)inviteContactWithEmail:(NSString *)email message:(nullable NSString *)message action:(MEGAInviteAction)action handle:(uint64_t)handle delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Invite another person to be your MEGA contact using a contact link handle
 *
 * The associated request type with this request is MEGARequestTypeInviteContact
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email of the contact
 * - [MEGARequest text] - Returns the text of the invitation
 * - [MEGARequest number] - Returns the action
 * - [MEGARequest nodeHandle] - Returns the contact link handle
 *
 * Sending a reminder within a two week period since you started or your last reminder will
 * fail the API returning the error code MEGAErrorTypeApiEAccess.
 *
 * @param email Email of the new contact
 * @param message Message for the user (can be nil)
 * @param action Action for this contact request. Valid values are:
 * - MEGAInviteActionAdd = 0
 * - MEGAInviteActionDelete = 1
 * - MEGAInviteActionRemind = 2
 *
 * @param handle Contact link handle of the other account. This parameter is considered only if the
 * \c action is MEGAInviteActionAdd. Otherwise, it's ignored and it has no effect.
 */
- (void)inviteContactWithEmail:(NSString *)email message:(nullable NSString *)message action:(MEGAInviteAction)action handle:(uint64_t)handle;

/**
 * @brief Reply to a contact request
 * @param request Contact request. You can get your pending contact requests using [MEGASdk incomingContactRequests]
 * @param action Action for this contact request. Valid values are:
 * - MEGAReplyActionAccept = 0
 * - MEGAReplyActionDeny = 1
 * - MEGAReplyActionIgnore = 2
 *
 * The associated request type with this request is MEGARequestTypeReplyContactRequest
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the contact request
 * - [MEGARequest number] - Returns the action
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)replyContactRequest:(MEGAContactRequest *)request action:(MEGAReplyAction)action delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Reply to a contact request
 * @param request Contact request. You can get your pending contact requests using [MEGASdk incomingContactRequests]
 * @param action Action for this contact request. Valid values are:
 * - MEGAReplyActionAccept = 0
 * - MEGAReplyActionDeny = 1
 * - MEGAReplyActionIgnore = 2
 *
 * The associated request type with this request is MEGARequestTypeReplyContactRequest
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest nodeHandle] - Returns the handle of the contact request
 * - [MEGARequest number] - Returns the action
 *
 */
- (void)replyContactRequest:(MEGAContactRequest *)request action:(MEGAReplyAction)action;

/**
 * @brief Remove a contact from the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeRemoveContact.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email of the contact
 *
 * @param user User of the contact to be removed.
 * @param delegate Delegate to track this request.
 */
- (void)removeContactUser:(MEGAUser *)user delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Remove a contact from the MEGA account.
 *
 * The associated request type with this request is MEGARequestTypeRemoveContact.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest email] - Returns the email of the contact
 *
 * @param user User of the contact to be removed.
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
- (void)submitFeedbackWithRating:(NSInteger)rating comment:(NSString *)comment delegate:(id<MEGARequestDelegate>)delegate __attribute__((deprecated("This function is for internal usage of MEGA apps.")));

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
- (void)submitFeedbackWithRating:(NSInteger)rating comment:(NSString *)comment __attribute__((deprecated("This function is for internal usage of MEGA apps.")));

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
- (void)reportDebugEventWithText:(NSString *)text delegate:(id<MEGARequestDelegate>)delegate __attribute__((deprecated("This function is for internal usage of MEGA apps.")));

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

- (void)reportDebugEventWithText:(NSString *)text __attribute__((deprecated("This function is for internal usage of MEGA apps.")));

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

/**
 * @brief Close a MEGA session
 *
 * All clients using this session will be automatically logged out.
 *
 * You can get session information using [MEGASdk getExtendedAccountDetailsWithSessions:purchases:transactions:].
 * Then use [MEGAAccountDetails numSessions] and [MEGAAccountDetails session]
 * to get session info.
 * [MEGAAccountDetails handle] provides the handle that this function needs.
 *
 * If you use -1, all sessions except the current one will be closed
 *
 * @param sessionHandle Handle of the session. Use -1 to cancel all sessions except the current one
 * @param delegate Delegate to track this request
 */
- (void)killSession:(uint64_t)sessionHandle delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Close a MEGA session
 *
 * All clients using this session will be automatically logged out.
 *
 * You can get session information using [MEGASdk getExtendedAccountDetailsWithSessions:purchases:transactions:].
 * Then use [MEGAAccountDetails numSessions] and [MEGAAccountDetails session]
 * to get session info.
 * [MEGAAccountDetails handle] provides the handle that this function needs.
 *
 * If you use -1, all sessions except the current one will be closed
 *
 * @param sessionHandle Handle of the session. Use -1 to cancel all sessions except the current one
 */
- (void)killSession:(uint64_t)sessionHandle;

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
- (nullable MEGATransfer *)transferByTag:(NSInteger)transferTag;
/**
 * @brief Upload a file.
 *
 * If the status of the business account is expired, onTransferFinish will be called with the error
 * code MEGAErrorTypeApiEBusinessPastDue. In this case, apps should show a warning message similar to
 * "Your business account is overdue, please contact your administrator."
 *
 * @param localPath Local path of the file.
 * @param parent Node for the file in the MEGA account.
 * @param delegate Delegate to track this transfer.
 */
- (void)startUploadWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent delegate:(id<MEGATransferDelegate>)delegate;

/**
 * @brief Upload a file.
 *
 * If the status of the business account is expired, onTransferFinish will be called with the error
 * code MEGAErrorTypeApiEBusinessPastDue. In this case, apps should show a warning message similar to
 * "Your business account is overdue, please contact your administrator."
 *
 * @param localPath Local path of the file.
 * @param parent Node for the file in the MEGA account.
 */
- (void)startUploadWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent;

/**
 * @brief Upload a file with a custom name.
 *
 * If the status of the business account is expired, onTransferFinish will be called with the error
 * code MEGAErrorTypeApiEBusinessPastDue. In this case, apps should show a warning message similar to
 * "Your business account is overdue, please contact your administrator."
 *
 * @param localPath Local path of the file.
 * @param parent Parent node for the file in the MEGA account.
 * @param filename Custom file name for the file in MEGA.
 * @param delegate Delegate to track this transfer.
 */
- (void)startUploadToFileWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent filename:(NSString *)filename delegate:(id<MEGATransferDelegate>)delegate;

/**
 * @brief Upload a file with a custom name.
 *
 * If the status of the business account is expired, onTransferFinish will be called with the error
 * code MEGAErrorTypeApiEBusinessPastDue. In this case, apps should show a warning message similar to
 * "Your business account is overdue, please contact your administrator."
 *
 * @param localPath Local path of the file.
 * @param parent Parent node for the file in the MEGA account.
 * @param filename Custom file name for the file in MEGA.
 */
- (void)startUploadToFileWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent filename:(NSString *)filename;

/**
 * @brief Upload a file with a custom name.
 *
 * If the status of the business account is expired, onTransferFinish will be called with the error
 * code MEGAErrorTypeApiEBusinessPastDue. In this case, apps should show a warning message similar to
 * "Your business account is overdue, please contact your administrator."
 *
 * @param localPath Local path of the file.
 * @param parent Parent node for the file in the MEGA account.
 * @param appData Custom app data to save in the MEGATransfer object
 * The data in this parameter can be accessed using [MEGATransfer appData] in delegates
 * @param delegate Delegate to track this transfer.
 */
- (void)startUploadWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent appData:(nullable NSString *)appData delegate:(id<MEGATransferDelegate>)delegate;

/**
 * @brief Upload a file with a custom name.
 *
 * If the status of the business account is expired, onTransferFinish will be called with the error
 * code MEGAErrorTypeApiEBusinessPastDue. In this case, apps should show a warning message similar to
 * "Your business account is overdue, please contact your administrator."
 *
 * @param localPath Local path of the file.
 * @param parent Parent node for the file in the MEGA account.
 * @param appData Custom app data to save in the MEGATransfer object
 * The data in this parameter can be accessed using [MEGATransfer appData] in delegates
 */
- (void)startUploadWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent appData:(nullable NSString *)appData;

/**
 * @brief Upload a file or a folder, saving custom app data during the transfer
 *
 * If the status of the business account is expired, onTransferFinish will be called with the error
 * code MEGAErrorTypeApiEBusinessPastDue. In this case, apps should show a warning message similar to
 * "Your business account is overdue, please contact your administrator."
 *
 * @param localPath Local path of the file or folder
 * @param parent Parent node for the file or folder in the MEGA account
 * @param appData Custom app data to save in the MEGATransfer object
 * The data in this parameter can be accessed using [MEGATransfer appData] in callbacks
 * related to the transfer.
 * @param isSourceTemporary Pass the ownership of the file to the SDK, that will DELETE it when the upload finishes.
 * This parameter is intended to automatically delete temporary files that are only created to be uploaded.
 * Use this parameter with caution. Set it to YES only if you are sure about what are you doing.
 * @param delegate MEGATransferDelegate to track this transfer
 */
- (void)startUploadWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent appData:(nullable NSString *)appData isSourceTemporary:(BOOL)isSourceTemporary delegate:(id<MEGATransferDelegate>)delegate;

/**
 * @brief Upload a file or a folder, saving custom app data during the transfer
 *
 * If the status of the business account is expired, onTransferFinish will be called with the error
 * code MEGAErrorTypeApiEBusinessPastDue. In this case, apps should show a warning message similar to
 * "Your business account is overdue, please contact your administrator."
 *
 * @param localPath Local path of the file or folder
 * @param parent Parent node for the file or folder in the MEGA account
 * @param appData Custom app data to save in the MEGATransfer object
 * The data in this parameter can be accessed using [MEGATransfer appData] in callbacks
 * related to the transfer.
 * @param isSourceTemporary Pass the ownership of the file to the SDK, that will DELETE it when the upload finishes.
 * This parameter is intended to automatically delete temporary files that are only created to be uploaded.
 * Use this parameter with caution. Set it to YES only if you are sure about what are you doing.
 */
- (void)startUploadWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent appData:(nullable NSString *)appData isSourceTemporary:(BOOL)isSourceTemporary;

/**
 * @brief Upload a file or a folder, putting the transfer on top of the upload queue
 *
 * If the status of the business account is expired, onTransferFinish will be called with the error
 * code MEGAErrorTypeApiEBusinessPastDue. In this case, apps should show a warning message similar to
 * "Your business account is overdue, please contact your administrator."
 *
 * @param localPath Local path of the file or folder
 * @param parent Parent node for the file or folder in the MEGA account
 * @param appData Custom app data to save in the MEGATransfer object
 * The data in this parameter can be accessed using [MEGATransfer appData] in callbacks
 * related to the transfer.
 * @param isSourceTemporary Pass the ownership of the file to the SDK, that will DELETE it when the upload finishes.
 * This parameter is intended to automatically delete temporary files that are only created to be uploaded.
 * Use this parameter with caution. Set it to YES only if you are sure about what are you doing.
 * @param delegate MEGATransferDelegate to track this transfer
 */
- (void)startUploadTopPriorityWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent appData:(nullable NSString *)appData isSourceTemporary:(BOOL)isSourceTemporary delegate:(id<MEGATransferDelegate>)delegate;

/**
 * @brief Upload a file or a folder, putting the transfer on top of the upload queue
 *
 * If the status of the business account is expired, onTransferFinish will be called with the error
 * code MEGAErrorTypeApiEBusinessPastDue. In this case, apps should show a warning message similar to
 * "Your business account is overdue, please contact your administrator."
 *
 * @param localPath Local path of the file or folder
 * @param parent Parent node for the file or folder in the MEGA account
 * @param appData Custom app data to save in the MEGATransfer object
 * The data in this parameter can be accessed using [MEGATransfer appData] in callbacks
 * related to the transfer.
 * @param isSourceTemporary Pass the ownership of the file to the SDK, that will DELETE it when the upload finishes.
 * This parameter is intended to automatically delete temporary files that are only created to be uploaded.
 * Use this parameter with caution. Set it to YES only if you are sure about what are you doing.

 */
- (void)startUploadTopPriorityWithLocalPath:(NSString *)localPath parent:(MEGANode *)parent appData:(nullable NSString *)appData isSourceTemporary:(BOOL)isSourceTemporary;

/**
* @brief Upload a file or a folder
*
* This method should be used ONLY to share by chat a local file. In case the file
* is already uploaded, but the corresponding node is missing the thumbnail and/or preview,
* this method will force a new upload from the scratch (ensuring the file attributes are set),
* instead of doing a remote copy.
*
* If the status of the business account is expired, onTransferFinish will be called with the error
* code MEGAErrorTypeApiEBusinessPastDue. In this case, apps should show a warning message similar to
* "Your business account is overdue, please contact your administrator."
*
* @param localPath Local path of the file
* @param parent Parent node for the file in the MEGA account
* @param appData Custom app data to save in the MEGATransfer object
* The data in this parameter can be accessed using [MEGATransfer appData] in callbacks
* related to the transfer.
* @param isSourceTemporary Pass the ownership of the file to the SDK, that will DELETE it when the upload finishes.
* This parameter is intended to automatically delete temporary files that are only created to be uploaded.
* Use this parameter with caution. Set it to YES only if you are sure about what are you doing.
* @param delegate MEGATransferDelegate to track this transfer
*
* The custom modification time will be only applied for file transfers. If a folder
* is transferred using this function, the custom modification time won't have any effect
*/
- (void)startUploadForChatWithLocalPath:(NSString *)localPath
                                 parent:(MEGANode *)parent
                                appData:(nullable NSString *)appData
                      isSourceTemporary:(BOOL)isSourceTemporary
                               delegate:(id<MEGATransferDelegate>)delegate;

/**
 * @brief Download a file from MEGA.
 *
 * If the status of the business account is expired, onTransferFinish will be called with the error
 * code MEGAErrorTypeApiEBusinessPastDue. In this case, apps should show a warning message similar to
 * "Your business account is overdue, please contact your administrator."
 *
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
 *
 * If the status of the business account is expired, onTransferFinish will be called with the error
 * code MEGAErrorTypeApiEBusinessPastDue. In this case, apps should show a warning message similar to
 * "Your business account is overdue, please contact your administrator."
 *
 * @param node MEGANode that identifies the file.
 * @param localPath Destination path for the file.
 * If this path is a local folder, it must end with a '\' or '/' character and the file name
 * in MEGA will be used to store a file inside that folder. If the path doesn't finish with
 * one of these characters, the file will be downloaded to a file in that path.
 */
- (void)startDownloadNode:(MEGANode *)node localPath:(NSString *)localPath;

/**
 * @brief Download a file from MEGA.
 *
 * If the status of the business account is expired, onTransferFinish will be called with the error
 * code MEGAErrorTypeApiEBusinessPastDue. In this case, apps should show a warning message similar to
 * "Your business account is overdue, please contact your administrator."
 *
 * @param node MEGANode that identifies the file.
 * @param localPath Destination path for the file.
 * If this path is a local folder, it must end with a '\' or '/' character and the file name
 * in MEGA will be used to store a file inside that folder. If the path doesn't finish with
 * one of these characters, the file will be downloaded to a file in that path.
 * @param appData Custom app data to save in the MEGATransfer object
 * The data in this parameter can be accessed using [MEGATransfer appData] in delegates
 * related to the transfer.
 *
 * @param delegate Delegate to track this transfer.
 */
- (void)startDownloadNode:(MEGANode *)node localPath:(NSString *)localPath appData:(nullable NSString *)appData delegate:(id<MEGATransferDelegate>)delegate;

/**
 * @brief Download a file from MEGA.
 *
 * If the status of the business account is expired, onTransferFinish will be called with the error
 * code MEGAErrorTypeApiEBusinessPastDue. In this case, apps should show a warning message similar to
 * "Your business account is overdue, please contact your administrator."
 *
 * @param node MEGANode that identifies the file.
 * @param localPath Destination path for the file.
 * If this path is a local folder, it must end with a '\' or '/' character and the file name
 * in MEGA will be used to store a file inside that folder. If the path doesn't finish with
 * one of these characters, the file will be downloaded to a file in that path.
 * @param appData Custom app data to save in the MEGATransfer object
 * The data in this parameter can be accessed using [MEGATransfer appData] in delegates
 * related to the transfer.
 *
 */
- (void)startDownloadNode:(MEGANode *)node localPath:(NSString *)localPath appData:(nullable NSString *)appData;

/**
 * @brief Download a file or a folder from MEGA, putting the transfer on top of the download queue.
 *
 * If the status of the business account is expired, onTransferFinish will be called with the error
 * code MEGAErrorTypeApiEBusinessPastDue. In this case, apps should show a warning message similar to
 * "Your business account is overdue, please contact your administrator."
 *
 * @param node MEGANode that identifies the file.
 * @param localPath Destination path for the file.
 * If this path is a local folder, it must end with a '\' or '/' character and the file name
 * in MEGA will be used to store a file inside that folder. If the path doesn't finish with
 * one of these characters, the file will be downloaded to a file in that path.
 * @param appData Custom app data to save in the MEGATransfer object
 * The data in this parameter can be accessed using [MEGATransfer appData] in delegates
 * related to the transfer.
 *
 * @param delegate Delegate to track this transfer.
 */
- (void)startDownloadTopPriorityWithNode:(MEGANode *)node localPath:(NSString *)localPath appData:(nullable NSString *)appData delegate:(id<MEGATransferDelegate>)delegate;

/**
 * @brief Download a file or a folder from MEGA, putting the transfer on top of the download queue.
 *
 * If the status of the business account is expired, onTransferFinish will be called with the error
 * code MEGAErrorTypeApiEBusinessPastDue. In this case, apps should show a warning message similar to
 * "Your business account is overdue, please contact your administrator."
 *
 * @param node MEGANode that identifies the file.
 * @param localPath Destination path for the file.
 * If this path is a local folder, it must end with a '\' or '/' character and the file name
 * in MEGA will be used to store a file inside that folder. If the path doesn't finish with
 * one of these characters, the file will be downloaded to a file in that path.
 * @param appData Custom app data to save in the MEGATransfer object
 * The data in this parameter can be accessed using [MEGATransfer appData] in delegates
 * related to the transfer.
 *
 */
- (void)startDownloadTopPriorityWithNode:(MEGANode *)node localPath:(NSString *)localPath appData:(nullable NSString *)appData;

/**
 * @brief Start an streaming download for a file in MEGA
 *
 * Streaming downloads don't save the downloaded data into a local file. It is provided 
 * in the callback [MEGATransferDelegate onTransferData:transfer:]. Only the MEGATransferDelegate
 * passed to this function will receive [MEGATransferDelegate onTransferData:transfer:] callbacks.
 * MEGATransferDelegate objects registered with [MEGASdk addMEGATransferDelegate:] won't 
 * receive them for performance reasons.
 *
 * If the status of the business account is expired, onTransferFinish will be called with the error
 * code MEGAErrorTypeApiEBusinessPastDue. In this case, apps should show a warning message similar to
 * "Your business account is overdue, please contact your administrator."
 *
 * @param node MEGANode that identifies the file (public nodes aren't supported yet)
 * @param startPos First byte to download from the file
 * @param size Size of the data to download
 * @param delegate MEGATransferDelegate to track this transfer
 */
- (void)startStreamingNode:(MEGANode *)node startPos:(NSNumber *)startPos size:(NSNumber *)size delegate:(id<MEGATransferDelegate>)delegate;

/**
 * @brief Start an streaming download for a file in MEGA
 *
 * Streaming downloads don't save the downloaded data into a local file. It is provided
 * in the callback [MEGATransferDelegate onTransferData:transfer:]. Only the MEGATransferDelegate
 * passed to this function will receive [MEGATransferDelegate onTransferData:transfer:] callbacks.
 * MEGATransferDelegate objects registered with [MEGASdk addMEGATransferDelegate:] won't
 * receive them for performance reasons.
 *
 * If the status of the business account is expired, onTransferFinish will be called with the error
 * code MEGAErrorTypeApiEBusinessPastDue. In this case, apps should show a warning message similar to
 * "Your business account is overdue, please contact your administrator."
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
* @brief Move a transfer to the top of the transfer queue
*
* If the transfer is successfully moved, onTransferUpdate will be called
* for the corresponding listeners of the moved transfer and the new priority
* of the transfer will be available using [MEGATransfer priority]
*
* The associated request type with this request is MEGARequestTypeCancelTransfer.
* Valid data in the MEGARequest object received on callbacks:
* - [MEGARequest transferTag]  - Returns the tag of the transfer to move
*
* @param transfer MEGATransfer object that identifies the transfer.
* You can get this object in any MEGATransferDelegate callback or any MEGADelegate callback
* related to transfers.
*
* @param delegate Delegate to track this request.
*/
- (void)moveTransferToFirst:(MEGATransfer *)transfer delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Move a transfer to the top of the transfer queue
 *
 * If the transfer is successfully moved, onTransferUpdate will be called
 * for the corresponding listeners of the moved transfer and the new priority
 * of the transfer will be available using [MEGATransfer priority]
 *
 * The associated request type with this request is MegaRequest::TYPE_MOVE_TRANSFER
 * Valid data in the MegaRequest object received on callbacks:
 * - [MEGARequest transferTag]  - Returns the tag of the transfer to move
 *
 * @param transfer MEGATransfer object that identifies the transfer
 */
- (void)moveTransferToFirst:(MEGATransfer *)transfer;

/**
* @brief Move a transfer to the bottom of the transfer queue
*
* If the transfer is successfully moved, onTransferUpdate will be called
* for the corresponding listeners of the moved transfer and the new priority
* of the transfer will be available using [MEGATransfer priority]
*
* The associated request type with this request is MEGARequestTypeCancelTransfer.
* Valid data in the MEGARequest object received on callbacks:
* - [MEGARequest transferTag]  - Returns the tag of the transfer to move
*
* @param transfer MEGATransfer object that identifies the transfer.
* You can get this object in any MEGATransferDelegate callback or any MEGADelegate callback
* related to transfers.
*
* @param delegate Delegate to track this request.
*/
- (void)moveTransferToLast:(MEGATransfer *)transfer delegate:(id<MEGARequestDelegate>)delegate;

/**
* @brief Move a transfer to the bottom of the transfer queue
*
* If the transfer is successfully moved, onTransferUpdate will be called
* for the corresponding listeners of the moved transfer and the new priority
* of the transfer will be available using [MEGATransfer priority]
*
* The associated request type with this request is MEGARequestTypeCancelTransfer.
* Valid data in the MEGARequest object received on callbacks:
* - [MEGARequest transferTag]  - Returns the tag of the transfer to move
*
* @param transfer MEGATransfer object that identifies the transfer.
* You can get this object in any MEGATransferDelegate callback or any MEGADelegate callback
* related to transfers.
*
*/
- (void)moveTransferToLast:(MEGATransfer *)transfer;

/**
* @brief Move a transfer before another one in the transfer queue
*
* If the transfer is successfully moved, onTransferUpdate will be called
* for the corresponding listeners of the moved transfer and the new priority
* of the transfer will be available using [MEGATransfer priority]
*
* The associated request type with this request is MEGARequestTypeCancelTransfer.
* Valid data in the MEGARequest object received on callbacks:
* - [MEGARequest transferTag]  - Returns the tag of the transfer to move
*
* @param transfer Transfer to move
* @param prevTransfer Transfer with the target position
* You can get this object in any MEGATransferDelegate callback or any MEGADelegate callback
* related to transfers.
*
* @param delegate Delegate to track this request.
*/
- (void)moveTransferBefore:(MEGATransfer *)transfer prevTransfer:(MEGATransfer *)prevTransfer delegate:(id<MEGARequestDelegate>)delegate;

/**
* @brief Move a transfer before another one in the transfer queue
*
* If the transfer is successfully moved, onTransferUpdate will be called
* for the corresponding listeners of the moved transfer and the new priority
* of the transfer will be available using [MEGATransfer priority]
*
* The associated request type with this request is MEGARequestTypeCancelTransfer.
* Valid data in the MEGARequest object received on callbacks:
* - [MEGARequest transferTag]  - Returns the tag of the transfer to move
*
* @param transfer Transfer to move
* @param prevTransfer Transfer with the target position
* You can get this object in any MEGATransferDelegate callback or any MEGADelegate callback
* related to transfers.
*
*/
- (void)moveTransferBefore:(MEGATransfer *)transfer prevTransfer:(MEGATransfer *)prevTransfer;

/**
 * @brief Cancel all transfers of the same type.
 *
 * The associated request type with this request is MEGARequestTypeCancelTransfers.
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the first parameter
 *
 * @param direction Type of transfers to cancel.
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
 * @param direction Type of transfers to cancel.
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
 */
- (void)pauseTransfers:(BOOL)pause;

/**
 * @brief Pause/resume a transfer
 *
 * The associated request type with this request is MEGARequestTypePauseTransfer
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest transferTag] - Returns the tag of the transfer to pause or resume
 * - [MEGARequest flag] - Returns YES if the transfer has to be pause or NO if it has to be resumed
 *
 * @param transfer Transfer to pause or resume
 * @param pause YES to pause the transfer or NO to resume it
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)pauseTransfer:(MEGATransfer *)transfer pause:(BOOL)pause delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Pause/resume a transfer
 *
 * The associated request type with this request is MEGARequestTypePauseTransfer
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest transferTag] - Returns the tag of the transfer to pause or resume
 * - [MEGARequest flag] - Returns YES if the transfer has to be pause or NO if it has to be resumed
 *
 * @param transfer Transfer to pause or resume
 * @param pause YES to pause the transfer or NO to resume it
 */
- (void)pauseTransfer:(MEGATransfer *)transfer pause:(BOOL)pause;

/**
 * @brief Pause/resume a transfer
 *
 * The associated request type with this request is MEGARequestTypePauseTransfer
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest transferTag] - Returns the tag of the transfer to pause or resume
 * - [MEGARequest flag] - Returns YES if the transfer has to be pause or NO if it has to be resumed
 *
 * @param transferTag Tag of the transfer to pause or resume
 * @param pause YES to pause the transfer or NO to resume it
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)pauseTransferByTag:(NSInteger)transferTag pause:(BOOL)pause delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Pause/resume a transfer
 *
 * The associated request type with this request is MEGARequestTypePauseTransfer
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest transferTag] - Returns the tag of the transfer to pause or resume
 * - [MEGARequest flag] - Returns YES if the transfer has to be pause or NO if it has to be resumed
 *
 * @param transferTag Tag of the transfer to pause or resume
 * @param pause YES to pause the transfer or NO to resume it
 */
- (void)pauseTransferByTag:(NSInteger)transferTag pause:(BOOL)pause;

/**
 * @brief Enable the resumption of transfers
 *
 * This function enables the cache of transfers, so they can be resumed later.
 * Additionally, if a previous cache already exists (from previous executions),
 * then this function also resumes the existing cached transfers.
 *
 * @note Cached downloads expire after 10 days since the last time they were active.
 * @note Cached uploads expire after 24 hours since the last time they were active.
 * @note Cached transfers related to files that have been modified since they were
 * added to the cache are discarded, since the file has changed.
 *
 * A log in or a log out automatically disables this feature.
 *
 * When the MEGASdk object is logged in, the cache of transfers is identified
 * and protected using the session and the recovery key, so transfers won't
 * be resumable using a different session or a different account. The
 * recommended way of using this function to resume transfers for an account
 * is calling it in the callback onRequestFinish related to [MEGASdk fetchNodes]
 *
 * When the MEGASdk object is not logged in, it's still possible to use this
 * feature. However, since there isn't any available data to identify
 * and protect the cache, a default identifier and key are used. To improve
 * the protection of the transfer cache and allow the usage of this feature
 * with several non logged in instances of MEGASdk at once without clashes,
 * it's possible to set a custom identifier for the transfer cache in the
 * optional parameter of this function. If that parameter is used, the
 * encryption key for the transfer cache will be derived from it.
 *
 * @param loggedOutId Identifier for a non logged in instance of MEGASdk.
 * It doesn't have any effect if MEGASdk is logged in.
 */
- (void)enableTransferResumption:(NSString *)loggedOutId;

/**
 * @brief Enable the resumption of transfers
 *
 * This function enables the cache of transfers, so they can be resumed later.
 * Additionally, if a previous cache already exists (from previous executions),
 * then this function also resumes the existing cached transfers.
 *
 * @note Cached downloads expire after 10 days since the last time they were active.
 * @note Cached uploads expire after 24 hours since the last time they were active.
 * @note Cached transfers related to files that have been modified since they were
 * added to the cache are discarded, since the file has changed.
 *
 * A log in or a log out automatically disables this feature.
 *
 * When the MEGASdk object is logged in, the cache of transfers is identified
 * and protected using the session and the recovery key, so transfers won't
 * be resumable using a different session or a different account. The
 * recommended way of using this function to resume transfers for an account
 * is calling it in the callback onRequestFinish related to [MEGASdk fetchNodes]
 *
 * When the MEGASdk object is not logged in, it's still possible to use this
 * feature. However, since there isn't any available data to identify
 * and protect the cache, a default identifier and key are used. To improve
 * the protection of the transfer cache and allow the usage of this feature
 * with several non logged in instances of MEGASdk at once without clashes,
 * it's possible to set a custom identifier for the transfer cache in the
 * optional parameter of this function. If that parameter is used, the
 * encryption key for the transfer cache will be derived from it.
 */
- (void)enableTransferResumption;

/**
 * @brief Disable the resumption of transfers
 *
 * This function disables the resumption of transfers and also deletes
 * the transfer cache if it exists. See also [MEGASdk enableTransferResumption:].
 *
 * @param loggedOutId Identifier for a non logged in instance of MEGASdk.
 * It doesn't have any effect if MEGASdk is logged in.
 */
- (void)disableTransferResumption:(NSString *)loggedOutId;

/**
 * @brief Disable the resumption of transfers
 *
 * This function disables the resumption of transfers and also deletes
 * the transfer cache if it exists. See also [MEGASdk enableTransferResumption:].
 *
 */
- (void)disableTransferResumption;

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
 * @param bpsLimit -1 to automatically select the limit, 0 for no limit, otherwise the speed limit
 * in bytes per second.
 */
- (void)setUploadLimitWithBpsLimit:(NSInteger)bpsLimit;

/**
 * @brief Request the URL suitable for uploading a media file.
 *
 * This function requests the URL needed for uploading the file. The URL will need the urlSuffix
 * from the encryptFileAtPath:startPosition:length:outputFilePath:urlSuffix:adjustsSizeOnly:
 * in MEGABackgroundMediaUpload to be appended before actually sending.
 * The result of the request is signalled by the delegate onRequestFinsish callback with MEGARequestTypeGetBackgroundUploadURL.
 * Provided the error code is MEGAErrorTypeApiOk, the URL is available from uploadURLString in the MEGABackgroundMediaUpload.
 *
 * Call this function just once (per file) to find out the URL to upload to, and upload all the pieces to the same
 * URL. If errors are encountered and the operation must be restarted from scratch, then a new URL should be requested.
 * A new URL could specify a different upload server for example.
 *
 * @param filesize The size of the file
 * @param mediaUpload A pointer to the MEGABackgroundMediaUpload object tracking this upload
 * @param delegate The MEGARequestDelegate to be called back with the result
 */
- (void)requestBackgroundUploadURLWithFileSize:(int64_t)filesize mediaUpload:(MEGABackgroundMediaUpload *)mediaUpload delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Create the node after completing the background upload of the file.
 *
 * Call this function after completing the background upload of all the file data
 * The node representing the file will be created in the cloud, with all the suitable
 * attributes and file attributes attached.
 * The associated request type with this request is MEGARequestTypeCompleteBackgroundUpload.
 *
 * @param mediaUpload The MEGABackgroundMediaUpload object tracking this upload.
 * @param fileName The leaf name of the file, utf-8 encoded.
 * @param parentNode The folder node under which this new file should appear.
 * @param fingerprint The fingerprint for the uploaded file.
 * To generate this, you can use the following APIs in MEGASdk:
 * - fingerprintForFilePath:
 * - fingerprintForData:modificationTime:
 * - fingerprintForFilePath:modificationTime:
 * @param originalFingerprint If the file uploaded is modified from the original,
 *        pass the fingerprint of the original file here, otherwise nil.
 * @param token The N binary bytes of the token returned from the file upload (of the last portion). N=36 currently.
 * @param delegate The MEGARequestDelegate to be called back with the result.
 */
- (void)completeBackgroundMediaUpload:(MEGABackgroundMediaUpload *)mediaUpload fileName:(NSString *)fileName parentNode:(MEGANode *)parentNode fingerprint:(NSString *)fingerprint originalFingerprint:(nullable NSString *)originalFingerprint binaryUploadToken:(NSData *)token delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Call this to enable the library to attach media info attributes.
 *
 * Those attributes allows to know if a file is a video, and play it with the correct codec.
 *
 * If media info is not ready, this function returns NO and automatically retrieves the mappings for type names
 * and MEGA encodings, required to analyse media files. When media info is received, the callbacks
 * onEvent is called with the EventMediaInfoReady event type.
 *
 * @return YES if the library is ready, otherwise NO (the request for media translation data is sent to MEGA).
 */
- (BOOL)ensureMediaInfo;

/**
 * @brief confirm available memory to avoid OOM situations
 *
 * Before queueing a thumbnail or preview upload (or other memory intensive task),
 * it may be useful on some devices to check if there is plenty of memory available
 * in the memory pool used by MEGASdk (especially since some platforms may not have
 * the facility to check for themselves, and/or deallocation may need to wait on a GC)
 * and if not, delay until any current resource constraints (eg. other current operations,
 * or other RAM-hungry apps in the device), have finished. This function just
 * makes several memory allocations and then immediately releases them. If all allocations
 * succeeded, it returns YES, indicating that memory is (probably) available.
 * Of course, another app or operation may grab that memory immediately so it not a
 * guarantee. However it may help to reduce the frequency of OOM situations on phones for example.
 *
 * @param count The number of allocations to make
 * @param size The size of those memory allocations
 * @return YES if all the allocations succeeded
 */
- (BOOL)testAllocationByAllocationCount:(NSUInteger)count allocationSize:(NSUInteger)size;

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
 * Same behavior than MEGASortOrderTypeDefaultAsc
 *
 * - MEGASortOrderTypeAlphabeticalDesc = 10
 * Same behavior than MEGASortOrderTypeDefaultDesc
 *
 * - MEGASortOrderTypePhotoAsc = 11
 * Sort with photos first, then by date ascending
 *
 * - MEGASortOrderTypePhotoDesc = 12
 * Sort with photos first, then by date descending
 *
 * - MEGASortOrderTypeVideoAsc = 13
 * Sort with videos first, then by date ascending
 *
 * - MEGASortOrderTypeVideoDesc = 14
 * Sort with videos first, then by date descending
 *
 * @deprecated MEGASortOrderTypeAlphabeticalAsc and MEGASortOrderTypeAlphabeticalDesc
 * are equivalent to MEGASortOrderTypeDefaultAsc and MEGASortOrderTypeDefaultDesc.
 * They will be eventually removed.
 *
 * @return List with all child MEGANode objects.
 */
- (MEGANodeList *)childrenForParent:(MEGANode *)parent order:(NSInteger)order;

/**
 * @brief Get all children of a MEGANode.
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
- (nullable MEGANode *)childNodeForParent:(MEGANode *)parent name:(NSString *)name;

/**
 * @brief Get all versions of a file
 * @param node Node to check
 * @return List with all versions of the node, including the current version
 */
- (MEGANodeList *)versionsForNode:(MEGANode *)node;

/**
 * @brief Get the number of versions of a file
 * @param node Node to check
 * @return Number of versions of the node, including the current version
 */
- (NSInteger)numberOfVersionsForNode:(MEGANode *)node;

/**
 * @brief Check if a file has previous versions
 * @param node Node to check
 * @return YES if the node has any previous version
 */
- (BOOL)hasVersionsForNode:(MEGANode *)node;

/**
 * @brief Get information about the contents of a folder
 *
 * The associated request type with this request is MEGARequestTypeFolderInfo
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest megaFolderInfo] - MEGAFolderInfo object with the information related to the folder
 *
 * @param node Folder node to inspect
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)getFolderInfoForNode:(MEGANode *)node delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get information about the contents of a folder
 *
 * The associated request type with this request is MEGARequestTypeFolderInfo
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest megaFolderInfo] - MEGAFolderInfo object with the information related to the folder
 *
 * @param node Folder node to inspect
 */
- (void)getFolderInfoForNode:(MEGANode *)node;

/**
 * @brief Get file and folder children of a MEGANode separatedly
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
 * Same behavior than MEGASortOrderTypeDefaultAsc
 *
 * - MEGASortOrderTypeAlphabeticalDesc = 10
 * Same behavior than MEGASortOrderTypeDefaultDesc
 *
 * - MEGASortOrderTypePhotoAsc = 11
 * Sort with photos first, then by date ascending
 *
 * - MEGASortOrderTypePhotoDesc = 12
 * Sort with photos first, then by date descending
 *
 * - MEGASortOrderTypeVideoAsc = 13
 * Sort with videos first, then by date ascending
 *
 * - MEGASortOrderTypeVideoDesc = 14
 * Sort with videos first, then by date descending
 *
 * @deprecated MEGASortOrderTypeAlphabeticalAsc and MEGASortOrderTypeAlphabeticalDesc
 * are equivalent to MEGASortOrderTypeDefaultAsc and MEGASortOrderTypeDefaultDesc.
 * They will be eventually removed.
 *
 * @return Lists with files and folders child MegaNode objects
 */
- (MEGAChildrenLists *)fileFolderChildrenForParent:(MEGANode *)parent order:(NSInteger)order;

/**
 * @brief Get file and folder children of a MEGANode separatedly
 *
 * @param parent Parent node.
 *
 * @return Lists with files and folders child MegaNode objects
 */
- (MEGAChildrenLists *)fileFolderChildrenForParent:(MEGANode *)parent;

/**
 * @brief Get the parent node of a MEGANode.
 *
 * If the node doesn't exist in the account or
 * it is a root node, this function returns nil.
 *
 * @param node MEGANode to get the parent.
 * @return The parent of the provided node.
 */
- (nullable MEGANode *)parentNodeForNode:(MEGANode *)node;

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
- (nullable NSString *)nodePathForNode:(MEGANode *)node;

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
- (nullable MEGANode *)nodeForPath:(NSString *)path node:(MEGANode *)node;

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
- (nullable MEGANode *)nodeForPath:(NSString *)path;

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
- (nullable MEGANode *)nodeForHandle:(uint64_t)handle;

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
- (nullable MEGAUser *)contactForEmail:(NSString *)email;

/**
 * @brief Get all MEGAUserAlerts for the logged in user
 *
 * You take the ownership of the returned value
 *
 * @return List of MEGAUserAlert objects
 */
- (MEGAUserAlertList *)userAlertList;

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
 * @brief Get a list with all active inboud sharings
 *
 * @param order Order for the returned list.
 * @return List of MegaShare objects that other users are sharing with this account
 */
- (MEGAShareList *)inSharesList:(MEGASortOrderType)order;

/**
 * @brief Get the user relative to an incoming share
 *
 * This function will return nil if the node is not found or doesn't represent
 * the root of an incoming share.
 *
 * @param node Incoming share
 * @return MEGAUser relative to the incoming share
 */
- (nullable MEGAUser *)userFromInShareNode:(MEGANode *)node;

/**
* @brief Get the user relative to an incoming share
*
* This function will return nil if the node is not found.
*
* If recurse is true, it will return nil if the root corresponding to
* the node received as argument doesn't represent the root of an incoming share.
* Otherwise, it will return nil if the node doesn't represent
* the root of an incoming share.
*
* @param node Node to look for inshare user.
* @param recurse use root node corresponding to the node passed
* @return MegaUser relative to the incoming share
*/
- (nullable MEGAUser *)userFromInShareNode:(MEGANode *)node recurse:(BOOL)recurse;

/**
 * @brief Check if a MEGANode is being shared.
 *
 * For nodes that are being shared, you can get a a list of MEGAShare
 * objects using [MEGASdk outSharesForNode:].
 *
 * @param node Node to check.
 * @return YES is the MEGANode is being shared, otherwise NO.
 *
 * @deprecated This function is intended for debugging and internal purposes and will be probably removed in future updates.
 * Use [MEGANode isShared] instead.
 */
- (BOOL)isSharedNode:(MEGANode *)node __attribute__((deprecated("This function is intended for debugging and internal purposes and will be probably removed in future updates. Use [MEGANode isShared] instead.")));

/**
 * @brief Get a list with all active outbound sharings
 *
 * @param order Order for the returned list.
 * @return List of MegaShare objects
 */
- (MEGAShareList *)outShares:(MEGASortOrderType)order;

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
 * @brief Get a list with all public links
 *
 * @param order Order for the returned list.
 * Valid value for order are: MEGASortOrderTypeNone, MEGASortOrderTypeDefaultAsc,
 * MEGASortOrderTypeDefaultDesc, MEGASortOrderTypeLinkCreationAsc,
 * MEGASortOrderTypeLinkCreationDesc
 * @return List of MEGANode objects that are shared with everyone via public link
 */
- (MEGANodeList *)publicLinks:(MEGASortOrderType)order;

/**
 * @brief Get a list with all incoming contact requests
 *
 * @return List of MEGAContactRequest objects
 */
- (MEGAContactRequestList *)incomingContactRequests;

/**
 * @brief Get a list with all outgoing contact requests
 *
 * @return List of MEGAContactRequest objects
 */
- (MEGAContactRequestList *)outgoingContactRequests;

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
- (nullable NSString *)fingerprintForFilePath:(NSString *)filePath;

/**
 * @brief Get a Base64-encoded fingerprint from a NSData and a modification time
 *
 * If the input stream is nil, has a negative size or can't be read, this function returns nil
 *
 * @param data NSData that provides the data to create the fingerprint
 * @param modificationTime Modification time that will be taken into account for the creation of the fingerprint
 * @return Base64-encoded fingerprint
 */
- (nullable NSString *)fingerprintForData:(NSData *)data modificationTime:(NSDate *)modificationTime;

/**
 * @brief Get a Base64-encoded fingerprint from a local file and a modification time
 *
 * If the file can't be found or can't be opened, this function returns nil.
 *
 * @param filePath Local file path.
 * @param modificationTime Modification time that will be taken into account for the creation of the fingerprint
 * @return Base64-encoded fingerprint
 */
- (nullable NSString *)fingerprintForFilePath:(NSString *)filePath modificationTime:(NSDate *)modificationTime;

/**
 * @brief Get a Base64-encoded fingerprint for a node.
 *
 * If the node doesn't exist or doesn't have a fingerprint, this function returns nil.
 *
 * @param node Node for which we want to get the fingerprint.
 * @return Base64-encoded fingerprint for the file.
 *
 * @deprecated Use [MEGANode fingerprint] instead of this function
 */
- (nullable NSString *)fingerprintForNode:(MEGANode *)node __attribute__((deprecated("Use [MEGANode fingerprint] instead of this function.")));

/**
 * @brief Returns a node with the provided fingerprint.
 *
 * If there isn't any node in the account with that fingerprint, this function returns nil.
 *
 * @param fingerprint Fingerprint to check.
 * @return MEGANode object with the provided fingerprint.
 */
- (nullable MEGANode *)nodeForFingerprint:(NSString *)fingerprint;

/**
 * @brief Returns a node with the provided fingerprint.
 *
 * If there isn't any node in the account with that fingerprint, this function returns nil.
 *
 * @param fingerprint Fingerprint to check.
 * @param parent Preferred parent node
 * @return MEGANode object with the provided fingerprint.
 */
- (nullable MEGANode *)nodeForFingerprint:(NSString *)fingerprint parent:(MEGANode *)parent;

/**
 * @brief Returns nodes that have an original fingerprint equal to the supplied value
 *
 * Search the node tree and return a list of nodes that have an original fingerprint, which
 * matches the supplied originalfingerprint.
 *
 * @param fingerprint Original fingerprint to check
 * @return List of nodes with the same original fingerprint
 */
- (MEGANodeList *)nodesForOriginalFingerprint:(NSString *)fingerprint;

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
- (nullable NSString *)CRCForFilePath:(NSString *)filePath;

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
- (nullable NSString *)CRCForNode:(MEGANode *)node;

/**
 * @brief Get the CRC from a fingerPrint
 *
 * @param fingerprint fingerPrint from which we want to get the CRC
 * @return Base64-encoded CRC from the fingerPrint
 */
- (nullable NSString *)CRCForFingerprint:(NSString *)fingerprint;
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

- (nullable MEGANode *)nodeByCRC:(NSString *)crc parent:(MEGANode *)parent;

/**
 * @brief Get the access level of a MEGANode.
 * @param node MEGANode to check.
 * @return Access level of the node.
 * Valid values are:
 * - MEGAShareTypeAccessOwner
 * - MEGAShareTypeAccessFull
 * - MEGAShareTypeAccessReadWrite
 * - MEGAShareTypeAccessRead
 * - MEGAShareTypeAccessUnknown
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
 * @brief Check if a node is in the Rubbish bin tree
 *
 * @param node Node to check
 * @return YES if the node is in the Rubbish bin
 */
- (BOOL)isNodeInRubbish:(MEGANode *)node;

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
 * @param cancelToken MEGACancelToken to be able to cancel the processing at any time.
 * @param recursive YES if you want to seach recursively in the node tree.
 * NO if you want to seach in the children of the node only
 * @param order MEGASortOrderType for the returned list.
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
 * Same behavior than MEGASortOrderTypeDefaultAsc
 *
 * - MEGASortOrderTypeAlphabeticalDesc = 10
 * Same behavior than MEGASortOrderTypeDefaultDesc
 *
 * - MEGASortOrderTypePhotoAsc = 11
 * Sort with photos first, then by date ascending
 *
 * - MEGASortOrderTypePhotoDesc = 12
 * Sort with photos first, then by date descending
 *
 * - MEGASortOrderTypeVideoAsc = 13
 * Sort with videos first, then by date ascending
 *
 * - MEGASortOrderTypeVideoDesc = 14
 * Sort with videos first, then by date descending
 *
 * @deprecated MEGASortOrderTypeAlphabeticalAsc and MEGASortOrderTypeAlphabeticalDesc
 * are equivalent to MEGASortOrderTypeDefaultAsc and MEGASortOrderTypeDefaultDesc.
 * They will be eventually removed.
 *
 * @return List of nodes that contain the desired string in their name.
 */
- (MEGANodeList *)nodeListSearchForNode:(MEGANode *)node searchString:(NSString *)searchString cancelToken:(MEGACancelToken *)cancelToken recursive:(BOOL)recursive order:(MEGASortOrderType)order;

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
 * @brief Return an array of buckets, each bucket containing a list of recently added/modified nodes
 *
 * Each bucket contains files that were added/modified in a set, by a single user.
 * This function, that takes no parameters, uses the defaults for the MEGA apps
 * which are (currently) within the last 30 days, and max 10000 nodes.
 *
 * @return Array of buckets containing nodes that were added/modifed as a set
 */
- (NSMutableArray *)recentActions;

/**
 * @brief Return an array of buckets, each bucket containing a list of recently added/modified nodes
 *
 * Each bucket contains files that were added/modified in a set, by a single user.
 *
 * @param days Age of actions since added/modified nodes will be considered (in days).
 * @param maxNodes Maximum amount of nodes to be considered.
 *
 * @return Array of buckets containing nodes that were added/modifed as a set
 */
- (NSMutableArray *)recentActionsSinceDays:(NSInteger)days maxNodes:(NSInteger)maxNodes;

/**
 * @brief Process a node tree using a MEGATreeProcessorDelegate implementation
 * @param node The parent node of the tree to explore
 * @param recursive YES if you want to recursively process the whole node tree.
 * @param delegate MEGATreeProcessorDelegate that will receive callbacks for every node in the tree
 * NO if you want to process the children of the node only
 *
 * @return YES if all nodes were processed. NO otherwise (the operation can be
 * cancelled by [MEGATreeProcessorDelegate processMEGANode:])
 */
- (BOOL)processMEGANodeTree:(MEGANode *)node recursive:(BOOL)recursive delegate:(id<MEGATreeProcessorDelegate>)delegate;

/**
 * @brief Returns a MEGANode that can be downloaded with any instance of MEGASdk
 *
 * This function only allows to authorize file nodes.
 *
 * You can use [MEGASdk startDownloadNode:localPath:] with the resulting node with any instance
 * of MEGASdk, even if it's logged into another account, a public folder, or not
 * logged in.
 *
 * If the first parameter is a public node or an already authorized node, this
 * function returns a copy of the node, because it can be already downloaded
 * with any MEGASdk instance.
 *
 * If the node in the first parameter belongs to the account or public folder
 * in which the current MEGASdk object is logged in, this funtion returns an
 * authorized node.
 *
 * If the first parameter is nil or a node that is not a public node, is not
 * already authorized and doesn't belong to the current MEGASdk, this function
 * returns nil.
 *
 * @param node MEGANode to authorize
 * @return Authorized node, or nil if the node can't be authorized or is not a file
 */
- (nullable MEGANode *)authorizeNode:(MEGANode *)node;

#ifdef ENABLE_CHAT

/**
 * @brief Returns a MegaNode that can be downloaded/copied with a chat-authorization
 *
 * During preview of chat-links, you need to call this method to authorize the MegaNode
 * from a node-attachment message, so the API allows to access to it. The parameter to
 * authorize the access can be retrieved from [MEGAChatRoom authorizationToken] when
 * the chatroom in in preview mode.
 *
 * You can use [MEGASdk startDownload] and/or [MEGASdk copyNode] with the resulting
 * node with any instance of MEGASdk, even if it's logged into another account,
 * a public folder, or not logged in.
 *
 * @param node MEGANode to authorize
 * @param cauth Authorization token (public handle of the chatroom in B64url encoding)
 * @return Authorized node, or nil if the node can't be authorized
 */
- (nullable MEGANode *)authorizeChatNode:(MEGANode *)node cauth:(NSString *)cauth;

#endif

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
 * @param name Name to convert (UTF8)
 * @return Converted name (UTF8)
 */
- (nullable NSString *)escapeFsIncompatible:(NSString *)name;

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
 * @param name Name to convert (UTF8)
 * @param destinationPath Destination file path
 * @return Converted name (UTF8)
 */
- (nullable NSString *)escapeFsIncompatible:(NSString *)name destinationPath:(NSString *)destinationPath;

/**
 * @brief Unescape a file name escaped with [MEGASdk escapeFsIncompatible:]
 *
 * The input string must be UTF8 encoded. The returned value will be UTF8 too.
 *
 * @param localName Escaped name to convert (UTF8)
 * @return Converted name (UTF8)
 */
- (nullable NSString *)unescapeFsIncompatible:(NSString *)localName;

/**
 * @brief Unescape a file name escaped with [MEGASdk escapeFsIncompatible:]
 *
 * The input string must be UTF8 encoded. The returned value will be UTF8 too.
 *
 * @param localName Escaped name to convert (UTF8)
 * @param destinationPath Destination file path
 * @return Converted name (UTF8)
 */
- (nullable NSString *)unescapeFsIncompatible:(NSString *)localName  destinationPath:(NSString *)destinationPath;

/**
 * @brief Change the API URL
 *
 * This function allows to change the API URL.
 * It's only useful for testing or debugging purposes.
 *
 * @param apiURL New API URL
 * @param disablepkp YES to disable public key pinning for this URL
 */
- (void)changeApiUrl:(NSString *)apiURL disablepkp:(BOOL)disablepkp;

/**
 * @brief Set the language code used by the app
 * @param languageCode Language code used by the app
 *
 * @return YES if the language code is known for the SDK, otherwise NO
 */
-(BOOL)setLanguageCode:(NSString *)languageCode;

/**
 * @brief Set the preferred language of the user
 *
 * Valid data in the MEGARequest object received in onRequestFinish:
 * - [MEGARequest text] - Return the language code
 *
 * If the language code is unknown for the SDK, the error code will be MEGAErrorTypeApiENoent
 *
 * This attribute is automatically created by the server. Apps only need
 * to set the new value when the user changes the language.
 *
 * @param languageCode code to be set
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)setLanguangePreferenceCode:(NSString *)languageCode delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Set the preferred language of the user
 *
 * Valid data in the MEGARequest object received in onRequestFinish:
 * - [MEGARequest text] - Return the language code
 *
 * If the language code is unknown for the SDK, the error code will be MEGAErrorTypeApiENoent
 *
 * This attribute is automatically created by the server. Apps only need
 * to set the new value when the user changes the language.
 *
 * @param languageCode code to be set
 */
- (void)setLanguangePreferenceCode:(NSString *)languageCode;

/**
 * @brief Get the preferred language of the user
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest text] - Return the language code
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)getLanguagePreferenceWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get the preferred language of the user
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest text] - Return the language code
 *
 */
- (void)getLanguagePreference;

/**
 * @brief Enable or disable file versioning
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 *
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the value MEGAUserAttributeDisableVersions
 *
 * Valid data in the MEGARequest object received in onRequestFinish:
 * - [MEGARequest text] - "1" for disable, "0" for enable
 *
 * @param disable YES to disable file versioning. NO to enable it
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)setFileVersionsOption:(BOOL)disable delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Enable or disable file versioning
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 *
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the value MEGAUserAttributeDisableVersions
 *
 * Valid data in the MEGARequest object received in onRequestFinish:
 * - [MEGARequest text] - "1" for disable, "0" for enable
 *
 * @param disable YES to disable file versioning. NO to enable it
 */
- (void)setFileVersionsOption:(BOOL)disable;

/**
 * @brief Check if file versioning is enabled or disabled
 *
 * If the option has never been set, the error code will be MEGAErrorTypeApiENoent.
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 *
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the value MEGAUserAttributeDisableVersions
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest text] - "1" for disable, "0" for enable
 * - [MEGARequest flag] - YES if disabled, NO if enabled
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)getFileVersionsOptionWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Check if file versioning is enabled or disabled
 *
 * If the option has never been set, the error code will be MEGAErrorTypeApiENoent.
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 *
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the value MEGAUserAttributeDisableVersions
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest text] - "1" for disable, "0" for enable
 * - [MEGARequest flag] - YES if disabled, NO if enabled
 */
- (void)getFileVersionsOption;

/**
 * @brief Enable or disable the automatic approval of incoming contact requests using a contact link
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 *
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the value MEGAUserAttributeContactLinkVerification
 *
 * Valid data in the MEGARequest object received in onRequestFinish:
 * - [MEGARequest text] - "0" for disable, "1" for enable
 *
 * @param disable YES to disable the automatic approval of incoming contact requests using a contact link
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)setContactLinksOptionDisable:(BOOL)disable delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Enable or disable the automatic approval of incoming contact requests using a contact link
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 *
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the value MEGAUserAttributeContactLinkVerification
 *
 * Valid data in the MEGARequest object received in onRequestFinish:
 * - [MEGARequest text] - "0" for disable, "1" for enable
 *
 * @param disable YES to disable the automatic approval of incoming contact requests using a contact link
 */
- (void)setContactLinksOptionDisable:(BOOL)disable;

/**
 * @brief Check if the automatic approval of incoming contact requests using contact links is enabled or disabled
 *
 * If the option has never been set, the error code will be MEGAErrorTypeApiENoent.
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 *
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the value MEGAUserAttributeContactLinkVerification
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest text] - "0" for disable, "1" for enable
 * - [MEGARequest flag] - NO if disabled, YES if enabled
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)getContactLinksOptionWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Check if the automatic approval of incoming contact requests using contact links is enabled or disabled
 *
 * If the option has never been set, the error code will be MEGAErrorTypeApiENoent.
 *
 * The associated request type with this request is MEGARequestTypeGetAttrUser
 *
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest paramType] - Returns the value MEGAUserAttributeContactLinkVerification
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest text] - "0" for disable, "1" for enable
 * - [MEGARequest flag] - NO if disabled, YES if enabled
 */
- (void)getContactLinksOption;

/**
 * @brief Keep retrying when public key pinning fails
 *
 * By default, when the check of the MEGA public key fails, it causes an automatic
 * logout. Pass NO to this function to disable that automatic logout and
 * keep the SDK retrying the request.
 *
 * Even if the automatic logout is disabled, a request of the type MEGARequestTypeLogout
 * will be automatically created and callbacks (onRequestStart, onRequestFinish) will
 * be sent. However, logout won't be really executed and in onRequestFinish the error code
 * for the request will be MEGAErrorTypeApiEIncomplete
 *
 * @param enable YES to keep retrying failed requests due to a fail checking the MEGA public key
 * or NO to perform an automatic logout in that case
 */
- (void)retrySSLErrors:(BOOL)enable;

/**
 * @brief Enable / disable the public key pinning
 *
 * Public key pinning is enabled by default for all sensible communications.
 * It is strongly discouraged to disable this feature.
 *
 * @param enable YES to keep public key pinning enabled, NO to disable it
 */
- (void)setPublicKeyPinning:(BOOL)enable;

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

/**
 * @brief Create an avatar for an image
 * @param imagePath Image path
 * @param destinationPath Destination path for the avatar (including the file name)
 * @return YES if the avatar was successfully created, otherwise NO.
 */
- (BOOL)createAvatar:(NSString *)imagePath destinationPath:(NSString *)destinationPath;

#ifdef HAVE_LIBUV

#pragma mark - HTTP Proxy Server

/**
 * @brief Start an HTTP proxy server in specified port
 *
 * If this function returns YES, that means that the server is
 * ready to accept connections. The initialization is synchronous.
 *
 * The server will serve files using this URL format:
 * http://[::1]/<NodeHandle>/<NodeName>
 *
 * The node name must be URL encoded and must match with the node handle.
 * You can generate a correct link for a MEGANode using [MEGASdk httpServerGetLocalLink]
 *
 * If the node handle belongs to a folder node, a web with the list of files
 * inside the folder is returned.
 *
 * It's important to know that the HTTP proxy server has several configuration options
 * that can restrict the nodes that will be served and the connections that will be accepted.
 *
 * These are the default options:
 * - The restricted mode of the server is set to HTTPServerAllowCreatedLocalLinks
 * (see [MEGASdk httServerSetRestrictedMode])
 *
 * - Folder nodes are NOT allowed to be served (see [MEGASdk httpServerEnableFolderServer])
 * - File nodes are allowed to be served (see [MEGASdk httpServerEnableFileServer])
 * - Subtitles support is disabled (see [MEGASdk httpServerEnableSubtitlesSupport])
 *
 * The HTTP server will only stream a node if it's allowed by all configuration options.
 *
 * @param localOnly YES to listen on ::1 only, NO to listen on all network interfaces
 * @param port Port in which the server must accept connections
 * @return YES is the server is ready, NO if the initialization failed
 */
- (BOOL)httpServerStart:(BOOL)localOnly port:(NSInteger)port;

/**
 * @brief Stop the HTTP proxy server
 *
 * When this function returns, the server is already shutdown.
 * If the HTTP proxy server isn't running, this functions does nothing
 */
- (void)httpServerStop;

/**
 * @brief Check if the HTTP proxy server is running
 * @return 0 if the server is not running. Otherwise the port in which it's listening to
 */
- (NSInteger)httpServerIsRunning;

/**
 * @brief Check if the HTTP proxy server is listening on all network interfaces
 * @return YES if the HTTP proxy server is listening on 127.0.0.1 only, or it's not started.
 * If it's started and listening on all network interfaces, this function returns NO
 */
- (BOOL)httpServerIsLocalOnly;

/**
 * @brief Allow/forbid to serve files
 *
 * By default, files are served (when the server is running)
 *
 * Even if files are allowed to be served by this function, restrictions related to
 * other configuration options ([MEGASdk httpServerSetRestrictedMode]) are still applied.
 *
 * @param enable YES to allow to server files, NO to forbid it
 */
- (void)httpServerEnableFileServer:(BOOL)enable;

/**
 * @brief Check if it's allowed to serve files
 *
 * This function can return YES even if the HTTP proxy server is not running
 *
 * Even if files are allowed to be served by this function, restrictions related to
 * other configuration options ([MEGASdk httpServerSetRestrictedMode]) are still applied.
 *
 * @return YES if it's allowed to serve files, otherwise NO
 */
- (BOOL)httpServerIsFileServerEnabled;

/**
 * @brief Allow/forbid to serve folders
 *
 * By default, folders are NOT served
 *
 * Even if folders are allowed to be served by this function, restrictions related to
 * other configuration options ([MEGASdk httpServerSetRestrictedMode]) are still applied.
 *
 * @param enable YES to allow to server folders, NO to forbid it
 */
- (void)httpServerEnableFolderServer:(BOOL)enable;

/**
 * @brief Check if it's allowed to serve folders
 *
 * This function can return YES even if the HTTP proxy server is not running
 *
 * Even if folders are allowed to be served by this function, restrictions related to
 * other configuration options ([MEGASdk httpServerSetRestrictedMode]) are still applied.
 *
 * @return YES if it's allowed to serve folders, otherwise NO
 */
- (BOOL)httpServerIsFolderServerEnabled;

/**
 * @brief Enable/disable the restricted mode of the HTTP server
 *
 * This function allows to restrict the nodes that are allowed to be served.
 * For not allowed links, the server will return "407 Forbidden".
 *
 * Possible values are:
 * - HTTPServerDenyAll = -1
 * All nodes are forbidden
 *
 * - HTTPServerAllowAll = 0
 * All nodes are allowed to be served
 *
 * - HTTPServerAllowCreatedLocalLinks = 1 (default)
 * Only links created with [MEGASdk httpServerGetLocalLink] are allowed to be served
 *
 * - HTTPServerAllowLastLocalLink = 2
 * Only the last link created with [MEGASdk httpServerGetLocalLink] is allowed to be served
 *
 * If a different value from the list above is passed to this function, it won't have any effect and the previous
 * state of this option will be preserved.
 *
 * The default value of this property is HTTPServerAllowCreatedLocalLinks
 *
 * The state of this option is preserved even if the HTTP server is restarted, but the
 * the HTTP proxy server only remembers the generated links since the last call to
 * [MEGASdk httpServerStart]
 *
 * Even if nodes are allowed to be served by this function, restrictions related to
 * other configuration options ([MEGASdk httpServerEnableFileServer],
 * [MEGASdk httpServerEnableFolderServer]) are still applied.
 *
 * @param mode Required state for the restricted mode of the HTTP proxy server
 */
- (void)httpServerSetRestrictedMode:(NSInteger)mode;

/**
 * @brief Check if the HTTP proxy server is working in restricted mode
 *
 * Possible return values are:
 * - HTTPServerDenyAll = -1
 * All nodes are forbidden
 *
 * - HTTPServerAllowAll = 0
 * All nodes are allowed to be served
 *
 * - HTTPServerAllowCreatedLocalLinks = 1 (default)
 * Only links created with [MEGASdk httpServerGetLocalLink] are allowed to be served
 *
 * - HTTPServerAllowLastLocalLink = 2
 * Only the last link created with [MEGASdk httpServerGetLocalLink] is allowed to be served
 *
 * The default value of this property is HTTPServerAllowCreatedLocalLinks
 *
 * See [MEGASdk httpServerEnableRestrictedMode] and [MEGASdk httpServerStart]
 *
 * Even if nodes are allowed to be served by this function, restrictions related to
 * other configuration options ([MEGASdk httpServerEnableFileServer],
 * [MEGASdk httpServerEnableFolderServer]) are still applied.
 *
 * @return State of the restricted mode of the HTTP proxy server
 */
- (NSInteger)httpServerGetRestrictedMode;

/**
 * @brief Enable/disable the support for subtitles
 *
 * Subtitles support allows to stream some special links that otherwise wouldn't be valid.
 * For example, let's suppose that the server is streaming this video:
 * http://120.0.0.1:4443/<Base64Handle>/MyHolidays.avi
 *
 * Some media players scan HTTP servers looking for subtitle files and request links like these ones:
 * http://120.0.0.1:4443/<Base64Handle>/MyHolidays.txt
 * http://120.0.0.1:4443/<Base64Handle>/MyHolidays.srt
 *
 * Even if a file with that name is in the same folder of the MEGA account, the node wouldn't be served because
 * the node handle wouldn't match.
 *
 * When this feature is enabled, the HTTP proxy server will check if there are files with that name
 * in the same folder as the node corresponding to the handle in the link.
 *
 * If a matching file is found, the name is exactly the same as the the node with the specified handle
 * (except the extension), the node with that handle is allowed to be streamed and this feature is enabled
 * the HTTP proxy server will serve that file.
 *
 * This feature is disabled by default.
 *
 * @param enable YES to enable subtitles support, NO to disable it
 */
- (void)httpServerEnableSubtitlesSupport:(BOOL)enable;

/**
 * @brief Check if the support for subtitles is enabled
 *
 * See [MEGASdk httpServerEnableSubtitlesSupport].
 *
 * This feature is disabled by default.
 *
 * @return YES of the support for subtibles is enables, otherwise NO
 */
- (BOOL)httpServerIsSubtitlesSupportEnabled;

/**
 * @brief Add a delegate to receive information about the HTTP proxy server
 *
 * This is the valid data that will be provided on callbacks:
 * - [MEGATransfer type] - It will be MEGATransferTypeLocalHTTPDownload
 * - [MEGATransfer path] - URL requested to the HTTP proxy server
 * - [MEGATransfer fileName] - Name of the requested file (if any, otherwise nil)
 * - [MEGATransfer nodeHandle] - Handle of the requested file (if any, otherwise nil)
 * - [MEGATransfer totalBytes] - Total bytes of the response (response headers + file, if required)
 * - [MEGATransfer startPos] - Start position (for range requests only, otherwise -1)
 * - [MEGATransfer endPos] - End position (for range requests only, otherwise -1)
 *
 * On the onTransferFinish error, the error code associated to the MEGAError can be:
 * - MEGAErrorTypeApiEIncomplete - If the whole response wasn't sent
 * (it's normal to get this error code sometimes because media players close connections when they have
 * the data that they need)
 *
 * - MEGAErrorTypeApiERead - If the connection with MEGA storage servers failed
 * - MEGAErrorTypeApiEAgain - If the download speed is too slow for streaming
 * - A number > 0 means an HTTP error code returned to the client
 *
 * @param delegate Delegate to receive information about the HTTP proxy server
 */
- (void)httpServerAddDelegate:(id<MEGATransferDelegate>)delegate;

/**
 * @brief Stop the reception of callbacks related to the HTTP proxy server on this delegate
 * @param delegate Delegate that won't continue receiving information
 */
- (void)httpServerRemoveDelegate:(id<MEGATransferDelegate>)delegate;

/**
 * @brief Returns a URL to a node in the local HTTP proxy server
 *
 * The HTTP proxy server must be running before using this function, otherwise
 * it will return nil.
 *
 * You take the ownership of the returned value
 *
 * @param node Node to generate the local HTTP link
 * @return URL to the node in the local HTTP proxy server, otherwise nil
 */
- (nullable NSURL *)httpServerGetLocalLink:(MEGANode *)node;

/**
 * @brief Set the maximum buffer size for the internal buffer
 *
 * The HTTP proxy server has an internal buffer to store the data received from MEGA
 * while it's being sent to clients. When the buffer is full, the connection with
 * the MEGA storage server is closed, when the buffer has few data, the connection
 * with the MEGA storage server is started again.
 *
 * Even with very fast connections, due to the possible latency starting new connections,
 * if this buffer is small the streaming can have problems due to the overhead caused by
 * the excessive number of POST requests.
 *
 * It's recommended to set this buffer at least to 1MB
 *
 * For connections that request less data than the buffer size, the HTTP proxy server
 * will only allocate the required memory to complete the request to minimize the
 * memory usage.
 *
 * The new value will be taken into account since the next request received by
 * the HTTP proxy server, not for ongoing requests. It's possible and effective
 * to call this function even before the server has been started, and the value
 * will be still active even if the server is stopped and started again.
 *
 * @param bufferSize Maximum buffer size (in bytes) or a number <= 0 to use the
 * internal default value
 */
- (void)httpServerSetMaxBufferSize:(NSInteger)bufferSize;
/**
 * @brief Get the maximum size of the internal buffer size
 *
 * See [MEGASdk httpServerSetMaxBufferSize]
 *
 * @return Maximum size of the internal buffer size (in bytes)
 */
- (NSInteger)httpServerGetMaxBufferSize;

/**
 * @brief Set the maximum size of packets sent to clients
 *
 * For each connection, the HTTP proxy server only sends one write to the underlying
 * socket at once. This parameter allows to set the size of that write.
 *
 * A small value could cause a lot of writes and would lower the performance.
 *
 * A big value could send too much data to the output buffer of the socket. That could
 * keep the internal buffer full of data that hasn't been sent to the client yet,
 * preventing the retrieval of additional data from the MEGA storage server. In that
 * circumstances, the client could read a lot of data at once and the HTTP server
 * could not have enough time to get more data fast enough.
 *
 * It's recommended to set this value to at least 8192 and no more than the 25% of
 * the maximum buffer size ([MEGASdk httpServerSetMaxBufferSize]).
 *
 * The new value will be takein into account since the next request received by
 * the HTTP proxy server, not for ongoing requests. It's possible and effective
 * to call this function even before the server has been started, and the value
 * will be still active even if the server is stopped and started again.
 *
 * @param outputSize Maximun size of data packets sent to clients (in bytes) or
 * a number <= 0 to use the internal default value
 */
- (void)httpServerSetMaxOutputSize:(NSInteger)outputSize;
/**
 * @brief Get the maximum size of the packets sent to clients
 *
 * See [MEGASdk httpServerSetMaxOutputSize]
 *
 * @return Maximum size of the packets sent to clients (in bytes)
 */
- (NSInteger)httpServerGetMaxOutputSize;

#endif

/**
 * @brief Get the MIME type associated with the extension
 *
 * @param extension File extension (with or without a leading dot)
 * @return MIME type associated with the extension
 */
+ (nullable NSString *)mimeTypeByExtension:(NSString *)extension;

/**
 * @brief Register a device token for iOS push notifications
 *
 * This function attach a device token to the current session, which is intended to get push notifications.
 *
 * The associated request type with this request is MEGARequestTypeRegisterPushNotification
 * Valid data in the MEGARequest object received on delegate:
 * - [MEGARequest text] - Returns the device token provided.
 *
 * @param deviceToken NSString representing the device token to be registered.
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)registeriOSdeviceToken:(NSString *)deviceToken delegate:(id<MEGARequestDelegate>)delegate;


/**
 * @brief Register a device token for iOS push notifications
 *
 * This function attach a device token to the current session, which is intended to get push notifications.
 *
 * The associated request type with this request is MEGARequestTypeRegisterPushNotification
 * Valid data in the MEGARequest object received on delegate:
 * - [MEGARequest text] - Returns the device token provided.
 *
 * @param deviceToken NSString representing the device token to be registered.
 */
- (void)registeriOSdeviceToken:(NSString *)deviceToken;

/**
 * @brief Register a device token for iOS VoIP push notifications
 *
 * This function attach a device token to the current session, which is intended to get push notifications.
 *
 * The associated request type with this request is MEGARequestTypeRegisterPushNotification
 * Valid data in the MEGARequest object received on delegate:
 * - [MEGARequest text] - Returns the device token provided.
 *
 * @param deviceToken NSString representing the device token to be registered.
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)registeriOSVoIPdeviceToken:(NSString *)deviceToken delegate:(id<MEGARequestDelegate>)delegate;


/**
 * @brief Register a device token for iOS VoIP push notifications
 *
 * This function attach a device token to the current session, which is intended to get push notifications.
 *
 * The associated request type with this request is MEGARequestTypeRegisterPushNotification
 * Valid data in the MEGARequest object received on delegate:
 * - [MEGARequest text] - Returns the device token provided.
 *
 * @param deviceToken NSString representing the device token to be registered.
 */
- (void)registeriOSVoIPdeviceToken:(NSString *)deviceToken;

/**
 * @brief Get the MEGA Achievements of the account logged in
 *
 * The associated request type with this request is MEGARequestTypeGetAchievements
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest flag] - Always NO
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest megaAchievementsDetails] - Details of the MEGA Achievements of this account
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)getAccountAchievementsWithDelegate:(id<MEGARequestDelegate>)delegate;


/**
 * @brief Get the MEGA Achievements of the account logged in
 *
 * The associated request type with this request is MEGARequestTypeGetAchievements
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest flag] - Always NO
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest megaAchievementsDetails] - Details of the MEGA Achievements of this account
 *
 */
- (void)getAccountAchievements;

/**
 * @brief Get the list of existing MEGA Achievements
 *
 * Similar to [MEGASdk getAccountAchievements], this method returns only the base storage and
 * the details for the different achievement classes, related to the
 * account that is logged in.
 * This function can be used to give an indication of what is available for advertising
 * for unregistered users, despite it can be used with a logged in account with no difference.
 *
 * @note: if the IP address is not achievement enabled (it belongs to a country where MEGA
 * Achievements are not enabled), the request will fail with MEGAErrorTypeApiEAccess.
 *
 * The associated request type with this request is MEGARequestTypeGetAchievements
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest flag] - Always YES
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequestm megaAchievementsDetails] - Details of the list of existing MEGA Achievements
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)getMegaAchievementsWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get the list of existing MEGA Achievements
 *
 * Similar to [MEGASdk getAccountAchievements], this method returns only the base storage and
 * the details for the different achievement classes, related to the
 * account that is logged in.
 * This function can be used to give an indication of what is available for advertising
 * for unregistered users, despite it can be used with a logged in account with no difference.
 *
 * @note: if the IP address is not achievement enabled (it belongs to a country where MEGA
 * Achievements are not enabled), the request will fail with MEGAErrorTypeApiEAccess.
 *
 * If the IP address is not achievement enabled, the request will fail with MEGAErrorTypeApiEAccess.
 *
 * The associated request type with this request is MEGARequestTypeGetAchievements
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest flag] - Always YES
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequestm megaAchievementsDetails] - Details of the list of existing MEGA Achievements
 *
 */
- (void)getMegaAchievements;

/**
 * @brief Retrieve basic information about a folder link
 *
 * This function retrieves basic information from a folder link, like the number of files / folders
 * and the name of the folder. For folder links containing a lot of files/folders,
 * this function is more efficient than a fetchnodes.
 *
 * Valid data in the MegaRequest object received on all callbacks:
 * - [MEGARequest link] - Returns the public link to the folder
 *
 * Valid data in the MegaRequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest megaFolderInfo] - Returns information about the contents of the folder
 * - [MEGARequest nodeHandle] - Returns the public handle of the folder
 * - [MEGARequest parentHandle] - Returns the handle of the owner of the folder
 * - [MEGARequest text] - Returns the name of the folder.
 * If there's no name, it returns the special status string "CRYPTO_ERROR".
 * If the length of the name is zero, it returns the special status string "BLANK".
 *
 * On the onRequestFinish error, the error code associated to the MegaError can be:
 * - MEGAErrorTypeApiEArgs - If the link is not a valid folder link
 * - MEGAErrorTypeApiEKey - If the public link does not contain the key or it is invalid
 *
 * @param folderLink Public link to a folder in MEGA
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)getPublicLinkInformationWithFolderLink:(NSString *)folderLink delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Retrieve basic information about a folder link
 *
 * This function retrieves basic information from a folder link, like the number of files / folders
 * and the name of the folder. For folder links containing a lot of files/folders,
 * this function is more efficient than a fetchnodes.
 *
 * Valid data in the MegaRequest object received on all callbacks:
 * - [MEGARequest link] - Returns the public link to the folder
 *
 * Valid data in the MegaRequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest megaFolderInfo] - Returns information about the contents of the folder
 * - [MEGARequest nodeHandle] - Returns the public handle of the folder
 * - [MEGARequest parentHandle] - Returns the handle of the owner of the folder
 * - [MEGARequest text] - Returns the name of the folder.
 * If there's no name, it returns the special status string "CRYPTO_ERROR".
 * If the length of the name is zero, it returns the special status string "BLANK".
 *
 * On the onRequestFinish error, the error code associated to the MegaError can be:
 * - MEGAErrorTypeApiEArgs - If the link is not a valid folder link
 * - MEGAErrorTypeApiEKey - If the public link does not contain the key or it is invalid
 *
 * @param folderLink Public link to a folder in MEGA
 */
- (void)getPublicLinkInformationWithFolderLink:(NSString *)folderLink;

#pragma mark - SMS

/**
 * @brief Check if the opt-in or account ublocking SMS is allowed.
 *
 * The result indicated whether the sendSMSVerificationCode() function can be used.
 *
 * @return SMSState enum to indicate the SMS state for the current account.
 */
- (SMSState)smsAllowedState;


/**
 * @brief Get the verified phone number for the account logged in
 *
 * Returns the phone number previously confirmed with sendSMSVerificationCodeToPhoneNumber:delegate
 * and checkSMSVerificationCode:delegate metnhods in MEGASdk.
 *
 * @return nil if there is no verified number, otherwise a string containing that phone number.
 */
- (nullable NSString *)smsVerifiedPhoneNumber;

/**
 * @brief Requests the currently available country calling codes
 *
 * The response value is stored as a dictionary of mapping from two-letter country code
 * to a list of calling codes. For instance:
 * {
 *   "AD": ["376"],
 *   "AE": ["971", "13"],
 * }
 *
 * Valid data in the delegate object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)getCountryCallingCodesWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Send a verification code txt to the supplied phone number
 *
 * Sends a 6 digit code to the user's phone. The phone number is supplied in this function call.
 * The code is sent by SMS to the user. Once the user receives it, they can type it into the app
 * and the call checkSMSVerificationCode:delegate: in MEGASdk to validate the user did
 * receive the verification code, so that really is their phone number.
 *
 * The frequency with which this call can be used is very limited (the API allows at most
 * two SMS mssages sent for phone number per 24 hour period), so it's important to get the
 * number right on the first try. The result will be MEGAErrorTypeApiETempUnavail if it has
 * been tried too frequently.
 *
 * Make sure to test the result of smsAllowedState in MEGASdk before calling this function.
 *
 * Valid data in the MegaRequest object received on callbacks:
 * - text in MEGARequest - the phoneNumber as supplied to this function
 *
 * When the operation completes, MEGAErrorType can be:
 * - MEGAErrorTypeApiETempUnavail if a limit is reached.
 * - MEGAErrorTypeApiEAccess if your account is already verified with an SMS number
 * - MEGAErrorTypeApiEExist if the number is already verified for some other account.
 * - MEGAErrorTypeApiEArgs if the phone number is badly formatted or invalid.
 * - MEGAErrorTypeApiOk is returned upon success.
 *
 * @param phoneNumber The phone number to txt the code to, supplied by the user.
 * @param delegate A MEGARequestDelegate callback to track this request
 */
- (void)sendSMSVerificationCodeToPhoneNumber:(NSString *)phoneNumber delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Check a verification code that the user should have received via txt
 *
 * This function validates that the user received the verification code sent by sendSMSVerificationCodeToPhoneNumber:delegate in MEGASdk.
 *
 * Valid data in the MEGARequest object received on callbacks:
 * - text in MEGARequest - the verificationCode as supplied to this function
 *
 * When the operation completes, MEGAErrorType can be:
 * - MEGAErrorTypeApiEAccess if you have reached the verification limits.
 * - MEGAErrorTypeApiEFailed if the verification code does not match.
 * - MEGAErrorTypeApiEExpired if the phone number was verified on a different account.
 * - MEGAErrorTypeApiOk is returned upon success.
 *
 * @param verificationCode A string supplied by the user, that they should have received via txt.
 * @param delegate A MEGARequestDelegate callback to track this request
 */
- (void)checkSMSVerificationCode:(NSString *)verificationCode delegate:(id<MEGARequestDelegate>)delegate;

/*
 * @brief Requests the user contacts registered in MEGA and verificated through SMS.
 *
 * Valid data in the MEGARequest object received on callbacks:
 * - [MegaRequest getMegaStringTable] Returns the array with registered contacts
 *
 * The associated request type with this request is MegaRequest::TYPE_GET_REGISTERED_CONTACTS
 * On the onRequestFinish error, the error code associated to the MegaError can be:
 * - MEGAErrorTypeApiEArgs if your contact details are invalid (malformed SMS number for example).
 * - MEGAErrorTypeApiETooMany if the request exceeds the details limit that can be looked up per account.
 * - MEGAErrorTypeApiOk is returned upon success.
 *
 * @param contacts An NSArray containing user contacts (NSDictionary "phoneNumber":"userName").
 * @param listener MEGARequestDelegate to track this request
 */
- (void)getRegisteredContacts:(NSArray<NSDictionary *> *)contacts delegate:(id<MEGARequestDelegate>)delegate;

#pragma mark - Push Notification Settings

/**
 * @brief Get push notification settings
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributePushSettings
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest megaPushNotificationSettings] Returns settings for push notifications
 *
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)getPushNotificationSettingsWithDelegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Get push notification settings
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributePushSettings
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest megaPushNotificationSettings] Returns settings for push notifications
 */
- (void)getPushNotificationSettings;

/**
 * @brief Set push notification settings.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributePushSettings
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest megaPushNotificationSettings] Returns settings for push notifications
 *
 * @param pushNotificationSettings Push notification settings of the user. (An instance of MEGAPushNotificationSettings).
 * @param delegate MEGARequestDelegate to track this request
 */
- (void)setPushNotificationSettings:(MEGAPushNotificationSettings *)pushNotificationSettings
                           delegate:(id<MEGARequestDelegate>)delegate;

/**
 * @brief Set push notification settings.
 *
 * The associated request type with this request is MEGARequestTypeSetAttrUser
 * - [MEGARequest paramType] - Returns the attribute type MEGAUserAttributePushSettings
 *
 * Valid data in the MEGARequest object received in onRequestFinish when the error code
 * is MEGAErrorTypeApiOk:
 * - [MEGARequest megaPushNotificationSettings] Returns settings for push notifications
 *
 * @param pushNotificationSettings Push notification settings of the user. (An instance of MEGAPushNotificationSettings).
 */
- (void)setPushNotificationSettings:(MEGAPushNotificationSettings *)pushNotificationSettings;

#pragma mark - Debug

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
 * @brief Enable log to console
 *
 * By default, log to console is NO.
 *
 * @param enable YES to show messages in console, NO to skip them.
 */
+ (void)setLogToConsole:(BOOL)enable;

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

/**
 * @brief Send events to the stats server
 *
 * The associated request type with this request is MEGARequestTypeSendEvent
 * Valid data in the MEGARequest object received on callbacks:
 * - [MEGARequest number] - Returns the event type
 * - [MEGARequest text] - Returns the event message
 *
 * @param eventType Event type
 * @param message Event message
 * @param delegate Delegate to track this request
 *
 * @deprecated This function is for internal usage of MEGA apps for debug purposes. This info
 * is sent to MEGA servers.
 *
 * @note Event types are restricted to the following ranges:
 *  - MEGAchat:  [99000, 99150)
 *  - Android:   [99200, 99300)
 *  - iOS:       [99300, 99400)
 *  - MEGA SDK:  [99400, 99500)
 *  - MEGAsync:  [99500, 99600)
 *  - Webclient: [99600, 99800]
 */
- (void)sendEvent:(NSInteger)eventType message:(NSString *)message delegate:(id<MEGARequestDelegate>)delegate;

/**
* @brief Send events to the stats server
*
* The associated request type with this request is MEGARequestTypeSendEvent
* Valid data in the MEGARequest object received on callbacks:
* - [MEGARequest number] - Returns the event type
* - [MEGARequest text] - Returns the event message
*
* @param eventType Event type
* @param message Event message
*
* @deprecated This function is for internal usage of MEGA apps for debug purposes. This info
* is sent to MEGA servers.
*
* @note Event types are restricted to the following ranges:
*  - MEGAchat:  [99000, 99150)
*  - Android:   [99200, 99300)
*  - iOS:       [99300, 99400)
*  - MEGA SDK:  [99400, 99500)
*  - MEGAsync:  [99500, 99600)
*  - Webclient: [99600, 99800]
*/
- (void)sendEvent:(NSInteger)eventType message:(NSString *)message;

@end

NS_ASSUME_NONNULL_END
