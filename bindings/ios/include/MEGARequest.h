/**
 * @file MEGARequest.h
 * @brief Provides information about an asynchronous request
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
#import "MEGAAccountDetails.h"
#import "MEGAPricing.h"
#import "MEGAAchievementsDetails.h"
#import "MEGAFolderInfo.h"
#import "MEGATimeZoneDetails.h"
#import "MEGAStringList.h"
#import "MEGAPushNotificationSettings.h"
#import "MEGABannerList.h"
#import "MEGAHandleList.h"
#import "MEGACurrency.h"
#import "MEGARecentActionBucket.h"
#import "MEGABackupInfo.h"
#import "MEGASet.h"
#import "MEGASetElement.h"
#import "MEGAVPNCredentials.h"
#import "MEGANetworkConnectivityTestResults.h"
#import "MEGAVPNRegion.h"
#import "MEGANotificationList.h"

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM (NSInteger, MEGARequestType) {
    MEGARequestTypeLogin = 0,
    MEGARequestTypeCreateFolder = 1,
    MEGARequestTypeMove = 2,
    MEGARequestTypeCopy = 3,
    MEGARequestTypeRename = 4,
    MEGARequestTypeRemove = 5,
    MEGARequestTypeShare = 6,
    MEGARequestTypeImportLink = 7,
    MEGARequestTypeExport = 8,
    MEGARequestTypeFetchNodes = 9,
    MEGARequestTypeAccountDetails = 10,
    MEGARequestTypeChangePassword = 11,
    MEGARequestTypeUpload = 12,
    MEGARequestTypeLogout = 13,
    MEGARequestTypeGetPublicNode = 14,
    MEGARequestTypeGetAttrFile = 15,
    MEGARequestTypeSetAttrFile = 16,
    MEGARequestTypeGetAttrUser = 17,
    MEGARequestTypeSetAttrUser = 18,
    MEGARequestTypeRetryPendingConnections = 19,
    MEGARequestTypeRemoveContact = 20,
    MEGARequestTypeCreateAccount = 21,
    MEGARequestTypeConfirmAccount = 22,
    MEGARequestTypeQuerySignUpLink = 23,
    MEGARequestTypeAddSync = 24,
    MEGARequestTypeRemoveSync = 25,
    MEGARequestTypeDisableSync = 26, // Deprecated
    MEGARequestTypeEnableSync = 27, // Deprecated
    MEGARequestTypeCopySyncConfig = 28,
    MEGARequestTypeCopyCachedConfig = 29,
    MEGARequestTypeImportSyncConfigs = 30,
    MEGARequestTypeRemoveSyncs = 31,
    MEGARequestTypePauseTransfers = 32,
    MEGARequestTypeCancelTransfer = 33,
    MEGARequestTypeCancelTransfers = 34,
    MEGARequestTypeDelete = 35,
    MEGARequestTypeReportEvent = 36,
    MEGARequestTypeCancelAttrFile = 37,
    MEGARequestTypeGetPricing = 38,
    MEGARequestTypeGetPaymentId = 39,
    MEGARequestTypeGetUserData = 40,
    MEGARequestTypeLoadBalancing = 41,
    MEGARequestTypeKillSession = 42,
    MEGARequestTypeSubmitPurchaseReceipt = 43,
    MEGARequestTypeCreditCardStore = 44,
    MEGARequestTypeUpgradeAccount = 45,
    MEGARequestTypeCreditCardQuerySubscriptions = 46,
    MEGARequestTypeCreditCardCancelSubscriptions = 47,
    MEGARequestTypeGetSessionTransferUrl = 48,
    MEGARequestTypeGetPaymentMethods = 49,
    MEGARequestTypeInviteContact = 50,
    MEGARequestTypeReplyContactRequest = 51,
    MEGARequestTypeSubmitFeedback = 52,
    MEGARequestTypeSendEvent = 53,
    MEGARequestTypeCleanRubbishBin = 54,
    MEGARequestTypeSetAttrNode = 55,
    MEGARequestTypeChatCreate = 56,
    MEGARequestTypeChatFetch = 57,
    MEGARequestTypeChatInvite = 58,
    MEGARequestTypeChatRemove = 59,
    MEGARequestTypeChatUrl = 60,
    MEGARequestTypeChatGrantAccess = 61,
    MEGARequestTypeChatRemoveAccess = 62,
    MEGARequestTypeUseHttpsOnly = 63,
    MEGARequestTypeSetProxy = 64,
    MEGARequestTypeGetRecoveryLink = 65,
    MEGARequestTypeQueryRecoveryLink = 66,
    MEGARequestTypeConfirmRecoveryLink = 67,
    MEGARequestTypeGetCancelLink = 68,
    MEGARequestTypeConfirmCancelLink = 69,
    MEGARequestTypeGetChangeEmailLink = 70,
    MEGARequestTypeConfirmChangeEmailLink = 71,
    MEGARequestTypeChatUpdatePermissions = 72,
    MEGARequestTypeChatTruncate = 73,
    MEGARequestTypeChatSetTitle = 74,
    MEGARequestTypeSetMaxConnections = 75,
    MEGARequestTypePauseTransfer = 76,
    MEGARequestTypeMoveTransfer = 77,
    MEGARequestTypeChatPresenceUrl = 78,
    MEGARequestTypeRegisterPushNotification = 79,
    MEGARequestTypeGetUserEmail = 80,
    MEGARequestTypeAppVersion = 81,
    MEGARequestTypeGetLocalSSLCertificate = 82,
    MEGARequestTypeSendSignupLink = 83,
    MEGARequestTypeQueryDns = 84,
    MEGARequestTypeQueryGelb = 85, // Deprecated
    MEGARequestTypeChatStats = 86,
    MEGARequestTypeDownloadFile = 87,
    MEGARequestTypeQueryTransferQuota = 88,
    MEGARequestTypePasswordLink = 89,
    MEGARequestTypeGetAchievements = 90,
    MEGARequestTypeRestore = 91,
    MEGARequestTypeRemoveVersions = 92,
    MEGARequestTypeChatArchive = 93,
    MEGARequestTypeWhyAmIBlocked = 94,
    MEGARequestTypeContactLinkCreate = 95,
    MEGARequestTypeContactLinkQuery = 96,
    MEGARequestTypeContactLinkDelete = 97,
    MEGARequestTypeFolderInfo = 98,
    MEGARequestTypeRichLink = 99,
    MEGARequestTypeKeepMeAlive = 100,
    MEGARequestTypeMultiFactorAuthCheck = 101,
    MEGARequestTypeMultiFactorAuthGet = 102,
    MEGARequestTypeMultiFactorAuthSet = 103,
    MEGARequestTypeAddBackup = 104,
    MEGARequestTypeRemoveBackup = 105,
    MEGARequestTypeTimer = 106,
    MEGARequestTypeAbortCurrentBackup = 107,
    MEGARequestTypeGetPSA = 108,
    MEGARequestTypeFetchTimeZone = 109,
    MEGARequestTypeUseralertAcknowledge = 110,
    MEGARequestTypeChatLinkHandle = 111,
    MEGARequestTypeChatLinkUrl = 112,
    MEGARequestTypeSetPrivateMode = 113,
    MEGARequestTypeAutojoinPublicChat = 114,
    MEGARequestTypeCatchup = 115,
    MEGARequestTypePublicLinkInformation = 116,
    MEGARequestTypeGetBackgroundUploadURL = 117,
    MEGARequestTypeCompleteBackgroundUpload = 118,
    MEGARequestTypeCloudStorageUsed = 119,
    MEGARequestTypeSendSMSVerificationCode = 120,
    MEGARequestTypeCheckSMSVerificationCode = 121,
    MEGARequestTypeGetRegisteredContacts = 122, // Deprecated
    MEGARequestTypeGetCountryCallingCodes = 123,
    MEGARequestTypeVerifyCredentials = 124,
    MEGARequestTypeGetMiscFlags = 125,
    MEGARequestTypeResendVerificationEmail = 126,
    MEGARequestTypeSupportTicket = 127,
    MEGARequestTypeRetentionTime = 128,
    MEGARequestTypeResetSmsVerifiedNumber = 129,
    MEGARequestTypeSendDevCommand = 130,
    MEGARequestTypeGetBanners = 131,
    MEGARequestTypeDismissBanner = 132,
    MEGARequestTypeBackupPut = 133,
    MEGARequestTypeBackupRemove = 134,
    MEGARequestTypeBackupPutHeartbeat = 135,
    MEGARequestTypeFetchAds = 136,
    MEGARequestTypeQueryAds = 137,
    MEGARequestTypeGetAttrNode = 138,
    MEGARequestTypeLoadExternalDriveBackups = 139,
    MEGARequestTypeCloseExternalDriveBackups = 140,
    MEGARequestTypeGetDownloadUrls = 141,
    MEGARequestTypeStartChatCall = 142,
    MEGARequestTypeJoinChatCall = 143,
    MEGARequestTypeEndChatCall = 144,
    MEGARequestTypeGetFAUploadUrl = 145,
    MEGARequestTypeExecuteOnThread = 146,
    MEGARequestTypeGetChatOptions = 147,
    MEGARequestTypeGetRecentActions = 148,
    MEGARequestTypeCheckRecoveryKey = 149,
    MEGARequestTypeSetMyBackups = 150,
    MEGARequestTypePutSet = 151,
    MEGARequestTypeRemoveSet = 152,
    MEGARequestTypeFetchSet = 153,
    MEGARequestTypePutSetElement = 154,
    MEGARequestTypeRemoveSetElement = 155,
    MEGARequestTypeRemoveOldBackupNodes = 156,
    MEGARequestTypeSetSyncRunstate = 157,
    MEGARequestTypeAddUpdateScheduledMeeting = 158,
    MEGARequestTypeDelScheduledMeeting = 159,
    MEGARequestTypeFetchScheduledMeeting = 160,
    MEGARequestTypeFetchScheduledMeetingOccurrences = 161,
    MEGARequestTypeOpenShareDialog = 162,
    MEGARequestTypeUpgradeSecurity = 163,
    MEGARequestTypePutSetElements = 164,
    MEGARequestTypeRemoveSetElements = 165,
    MEGARequestTypeExportSet = 166,
    MEGARequestTypeExportedSetElement = 167,
    MEGARequestTypeGetRecommenedProPlan = 168,
    MEGARequestTypeBackupInfo = 169,
    MEGARequestTypeBackupRemoveMD = 170,
    MEGARequestTypeABTestActive = 171,
    MEGARequestTypeGetVPNRegions = 172,
    MEGARequestTypeGetVPNCredentials = 173,
    MEGARequestTypePutVPNCredentials = 174,
    MEGARequestTypeDeleteVPNCredentials = 175,
    MEGARequestTypeCheckVPNCredentials = 176,
    MEGARequestTypeGetSyncStallList = 177,
    MEGARequestTypeFetchCreditCardInfo = 178,
    MEGARequestTypeMoveToDebris = 179,
    MEGARequestTypeRingIndividualInCall = 180,
    MEGARequestTypeCreateNodeTree = 181,
    MEGARequestTypeCreatePasswordManagerBase = 182,
    MEGARequestTypeCreatePasswordNode = 183,
    MEGARequestTypeUpdatePasswordNode = 184,
    MEGARequestTypeGetNotifications = 185,
    MEGARequestTypeTagNode = 186,
    MEGARequestTypeAddMount = 187,
    MEGARequestTypeDisableMount = 188,
    MEGARequestTypeEnableMount = 189,
    MEGARequestTypeRemoveMount = 190,
    MEGARequestTypeSetMountFlags = 191,
    MEGARequestTypeDelAttrUser = 192,
    MEGARequestTypeBackupPauseMD = 194,
    MEGARequestTypeBackupResumeMD = 195,
    MEGARequestTypeImportPasswordsFromFile = 196,
    MEGARequestTypeGetActiveSurveyTriggerActions = 197,
    MEGARequestTypeGetSurvey = 198,
    MEGARequestTypeAnswerSurvey = 199,
    MEGARequestTypeChangeSyncRoot = 200,
    MEGARequestTypeGetMyIP = 201,
    MEGARequestTypeSetSyncUploadThrottleValues = 202,
    MEGARequestTypeGetSyncUploadThrottleValues = 203,
    MEGARequestTypeGetSyncUploadThrottleLimits = 204,
    MEGARequestTypeCheckSyncUploadThrottledValues = 205,
    MEGARequestTypeRunNetworkConnectivityTest = 206,
    TotalOfRequestTypes = 207
};

typedef NS_ENUM (NSInteger, MEGANodeAccessLevel) {
    MEGANodeAccessLevelAccessUnknown = -1,
    MEGANodeAccessLevelRdOnly = 0,            // cannot add, rename or delete
    MEGANodeAccessLevelRdWr,                  // cannot rename or delete
    MEGANodeAccessLevelFull,                  // all operations that do not require ownership permitted
    MEGANodeAccessLevelOwner,                 // node is in caller's ROOT, INCOMING or RUBBISH trees
    MEGANodeAccessLevelOwnerPreLogin
};

/**
 * @brief Provides information about an asynchronous request.
 *
 * Most functions in this API are asynchonous, except the ones that never require to
 * contact MEGA servers. Developers can use delegates (MEGADelegate, MEGARequestDelegate)
 * to track the progress of each request. MEGARequest objects are provided in callbacks sent
 * to these delegates and allow developers to know the state of the request, their parameters
 * and their results.
 *
 * Objects of this class aren't live, they are snapshots of the state of the request
 * when the object is created, they are immutable.
 *
 * These objects have a high number of 'properties', but only some of them return valid values
 * for each type of request. Documentation of each request specify which fields are valid.
 *
 */
@interface MEGARequest : NSObject

/**
 * @brief Type of request associated with the object.
 */
@property (readonly, nonatomic) MEGARequestType type;

/**
 * @brief A readable string that shows the type of request.
 *
 * This property is a pointer to a statically allocated buffer.
 * You don't have to free the returned pointer
 *
 */
@property (readonly, nonatomic, nullable) NSString *requestString;

/**
 * @brief The handle of a node related to the request.
 *
 * This value is valid for these requests:
 * - [MEGASdk moveNode:newParent:] - Returns the handle of the node to move
 * - [MEGASdk copyNode:newParent:] - Returns the handle of the node to copy
 * - [MEGASdk renameNode:newName:] - Returns the handle of the node to rename
 * - [MEGASdk removeNode:] - Returns the handle of the node to remove
 * - [MEGASdk shareNode:withUser:level:] - Returns the handle of the folder to share
 * - [MEGASdk getThumbnailNode:destinationFilePath:] - Returns the handle of the node to get the thumbnail
 * - [MEGASdk getPreviewlNode:destinationFilePath:] - Return the handle of the node to get the preview
 * - [MEGASdk cancelGetThumbnailNode:] - Return the handle of the node
 * - [MEGASdk cancelGetPreviewNode:] - Returns the handle of the node
 * - [MEGASdk setThumbnailNode:sourceFilePath:] - Returns the handle of the node
 * - [MEGASdk setPreviewNode:sourceFilePath:] - Returns the handle of the node
 * - [MEGASdk exportNode:] - Returns the handle of the node
 * - [MEGASdk disableExportNode:] - Returns the handle of the node
 * - [MEGASdk getPaymentIdForProductHandle:] - Returns the handle of the folder in MEGA
 *
 * This value is valid for these requests in onRequestFinish when the
 * error code is MEGAErrorTypeApiOk:
 * - [MEGASdk createFolderWithName:parent:] - Returns the handle of the new folder
 * - [MEGASdk copyNode:newParent:] - Returns the handle of the new node
 * - [MEGASdk importMegaFileLink:parent:] - Returns the handle of the new node
 *
 */
@property (readonly, nonatomic) uint64_t nodeHandle;

/**
 * @brief A link related to the request.
 *
 * This value is valid for these requests:
 * - [MEGASdk querySignupLink:] - Returns the confirmation link
 * - [MEGASdk confirmAccountWithLink:password:] - Returns the confirmation link
 * - [MEGASdk loginToFolderLink:] - Returns the link to the folder
 * - [MEGASdk importMegaFileLink:parent:] - Returns the link to the file to import
 * - [MEGASdk publicNodeForMegaFileLink:] - Returns the link to the file
 *
 * This value is valid for these requests in onRequestFinish when the
 * error code is MEGAErrorTypeApiOk:
 * - [MEGASdk exportNode:] - Returns the public link
 * - [MEGASdk getPaymentIdForProductHandle:] - Returns the payment link
 *
 */
@property (readonly, nonatomic, nullable) NSString *link;

/**
 * @brief The handle of a parent node related to the request.
 *
 * This value is valid for these requests:
 * - [MEGASdk createFolderWithName:parent:] - Returns the handle of the parent folder
 * - [MEGASdk moveNode:newParent:] - Returns the handle of the new parent for the node
 * - [MEGASdk copyNode:newParent:] - Returns the handle of the parent for the new node
 * - [MEGASdk importMegaFileLink:parent:] - Returns the handle of the node that receives the imported file
 *
 */
@property (readonly, nonatomic) uint64_t parentHandle;

/**
 * @brief A session key related to the request.
 *
 * This value is valid for these requests:
 * - [MEGASdk fastLoginWithSession:] - Returns session key used to access the account
 *
 */
@property (readonly, nonatomic, nullable) NSString *sessionKey;

/**
 * @brief A name related to the request.
 *
 * This value is valid for these requests:
 * - [MEGASdk createAccountWithEmail:password:name:] - Returns the name of the user
 * - [MEGASdk createFolderWithName:parent:] - Returns the name of the new folder
 * - [MEGASdk renameNode:newName:] - Returns the new name for the node
 *
 * This value is valid for these request in onRequestFinish when the
 * error code is MEGAErrorTypeApiOk:
 * - [MEGASdk querySignupLink:] - Returns the name of the user
 * - [MEGASdk confirmAccountWithLink:password:] - Returns the name of the user
 *
 */
@property (readonly, nonatomic, nullable) NSString *name;

/**
 * @brief An email related to the request.
 *
 * This value is valid for these requests:
 * - [MEGASdk loginWithEmail:password:] - Returns the email of the account
 * - [MEGASdk fastLoginWithEmail:password:] - Returns the email of the account
 * - [MEGASdk loginToFolderLink:] - Returns the string "FOLDER"
 * - [MEGASdk createAccountWithEmail:password:name:] - Returns the name of the user
 * - [MEGASdk shareNode:withUser:level:] - Returns the handle of the folder to share
 * - [MEGASdk getAvatarUser:destinationFilePath:] - Returns the email of the user to get the avatar
 * - [MEGASdk removeContactWithEmail:] - Returns the email of the contact
 * - [MEGASdk getUserData] - Returns the name of the user
 *
 * This value is valid for these request in onRequestFinish when the
 * error code is MEGAErrorTypeApiOk:
 * - [MEGASdk querySignupLink:] - Returns the name of the user
 * - [MEGASdk confirmAccountWithLink:password:] - Returns the name of the user
 *
 */
@property (readonly, nonatomic, nullable) NSString *email;

/**
 * @brief A password related to the request.
 *
 * This value is valid for these requests:
 * - [MEGASdk loginWithEmail:password:] - Returns the email of the account
 * - [MEGASdk fastLoginWithEmail:password:] - Returns the email of the account
 * - [MEGASdk createAccountWithEmail:password:name:] - Returns the name of the user
 * - [MEGASdk confirmAccountWithLink:password:] - Returns the password for the account
 * - [MEGASdk changePassword:newPassword:]  - Returns the old password of the account (first parameter)
 *
 */
@property (readonly, nonatomic, nullable) NSString *password;

/**
 * @brief A new password related to the request.
 *
 * This value is valid for these requests:
 * - [MEGASdk changePassword:newPassword:]  - Returns the old password of the account (first parameter)
 *
 */
@property (readonly, nonatomic, nullable) NSString *newPassword;

/**
 * @brief An access level related to the request.
 *
 * This value is valid for these requests:
 * - [MEGASdk shareNode:withUser:level:] - Returns the access level for the shared folder
 * - [MEGASdk exportNode:] - Returns YES
 * - [MEGASdk disableExportNode:] - Returns NO
 *
 */
@property (readonly, nonatomic) MEGANodeAccessLevel access;

/**
 * @brief The path of a file related to the request.
 *
 * This value is valid for these requests:
 * - [MEGASdk getThumbnailNode:destinationFilePath:] - Returns the destination path for the thumbnail
 * - [MEGASdk getPreviewlNode:destinationFilePath:] - Returns the destination path for the preview
 * - [MEGASdk getAvatarUser:destinationFilePath:] - Returns the destination path for the avatar
 * - [MEGASdk setThumbnailNode:sourceFilePath:] - Returns the source path for the thumbnail
 * - [MEGASdk setPreviewNode:sourceFilePath:] - Returns the source path for the preview
 * - [MEGASdk setAvatarUserWithSourceFilePath:] - Returns the source path for the avatar
 *
 */
@property (readonly, nonatomic, nullable) NSString *file;

/**
 * @brief Number of times that a request has temporarily failed.
 */
@property (readonly, nonatomic) NSInteger numRetry;

/**
 * @brief A public node related to the request.
 *
 * If you want to use the returned node beyond the deletion of the MEGARequest object,
 * you must call [MEGANode clone] or use [MEGARequest publicNode] instead.
 *
 * @return Public node related to the request
 *
 */
@property (readonly, nonatomic, nullable) MEGANode *publicNode;

/**
 * @brief The type of parameter related to the request.
 *
 * This value is valid for these requests:
 * - [MEGASdk getThumbnailNode:destinationFilePath:] - Returns MEGAAttributeTypeThumbnail
 * - [MEGASdk getPreviewNode:destinationFilePath:] - Returns MEGAAttributeTypePreview
 * - [MEGASdk setThumbnailNode:sourceFilePath:] - Returns MEGAAttributeTypeThumbnail
 * - [MEGASdk setPreviewNode:sourceFilePath:] - Returns MEGAAttributeTypePreview
 *
 */
@property (readonly, nonatomic) NSInteger paramType;

/**
 * @brief Text relative to this request.
 *
 * This value is valid for these requests:
 * - [MEGASdk submitFeedbackWithRating:comment:] - Returns the comment about the app
 * - [MEGASdk reportDebugEventWithText:] - Returns the debug message
 *
 */
@property (readonly, nonatomic, nullable) NSString *text;

/**
 * @brief Number related to this request
 *
 * This value is valid for these requests:
 * - [MEGASdk retryPendingConnections] - Returns if transfers are retried
 * - [MEGASdk submitFeedbackWithRating:comment:] - Returns the rating for the app
 *
 */
@property (readonly, nonatomic) long long number;

/**
 * @brief A flag related to the request.
 *
 * This value is valid for these request in onRequestFinish when the
 * error code is MEGAErrorTypeApiOk:
 * - [MEGASdk queryTransferQuota] - YES if it is expected to get an overquota error, otherwise NO
 *
 */
@property (readonly, nonatomic) BOOL flag;

/**
 * @brief Number of transferred bytes during the request.
 */
@property (readonly, nonatomic) long long transferredBytes;

/**
 * @brief Number of bytes that the SDK will have to transfer to finish the request.
 */
@property (readonly, nonatomic) long long totalBytes;

/**
 * @brief Details related to the MEGA account.
 *
 * This value is valid for these request in onRequestFinish when the
 * error code is MEGAErrorTypeApiOk:
 * - [MEGASdk getAccountDetails] - Details of the MEGA account
 *
 */
@property (readonly, nonatomic, nullable) MEGAAccountDetails *megaAccountDetails;

/**
 * @brief Available pricing plans to upgrade a MEGA account.
 *
 * This value is valid for these request in onRequestFinish when the
 * error code is MEGAErrorTypeApiOk:
 * - [MEGASdk getPricing] - Returns the available pricing plans
 *
 */
@property (readonly, nonatomic, nullable) MEGAPricing *pricing;

/**
 * @brief Currency data related to prices
 *
 * This value is valid for these request in onRequestFinish when the
 * error code is MEGAErrorTypeApiOk:
 * - [MEGASdk getPricing] - Returns the currency data related to prices
 */
@property (readonly, nonatomic, nullable) MEGACurrency *currency;

/**
 * @brief Details related to the MEGA Achievements of this account
 *
 * This value is valid for these request in onRequestFinish when the
 * error code is MEGAErrorTypeApiOk:
 * - [MEGASdk getMegaAchievements] - Details of the MEGA Achievements of this account
 *
 */
@property (readonly, nonatomic, nullable) MEGAAchievementsDetails *megaAchievementsDetails;

/**
 * @brief Get details about timezones and the current default
 *
 * This value is valid for these request in onRequestFinish when the
 * error code is MEGAErrorTypeApiOk:
 * - [MEGASdk fetchTimeZones] - Details about timezones and the current default
 *
 * In any other case, this function returns NULL.
 *
 * @return Details about timezones and the current default
 */
@property (readonly, nonatomic, nullable) MEGATimeZoneDetails *megaTimeZoneDetails;

/**
 * @brief Information about the contents of a folder
 *
 * This value is valid for these requests in onRequestFinish when the
 * error code is MEGAErrorTypeApiOk:
 * - [MEGASdk getFolderInfoForNode:] - Returns the information related to the folder
 *
 */
@property (readonly, nonatomic, nullable) MEGAFolderInfo *megaFolderInfo;

/**
 * @brief Returns settings for push notifications
 *
 * This value is valid for these requests in onRequestFinish when the
 * error code is MEGAErrorTypeApiOk:
 * - [MEGASdk getPushNotificationSettingsWithDelegate] - Returns settings for push notifications
*/
@property (readonly, nonatomic, nullable) MEGAPushNotificationSettings *megaPushNotificationSettings;

/**
 * @brief Tag of a transfer related to the request.
 *
 * This value is valid for these requests:
 * - [MEGASdk cancelTransfer:] - Returns the tag of the cancelled transfer ([MEGATransfer tag])
 *
 */
@property (readonly, nonatomic) NSInteger transferTag;

/**
 * @brief Number of details related to this request.
 */
@property (readonly, nonatomic) NSInteger numDetails;

/**
 * @brief Returns a dictionary of mega string list.
 */
@property (readonly, nonatomic, nullable) NSDictionary<NSString *, MEGAStringList*> *megaStringListDictionary;

/**
 * @brief Returns the attribute values as dictionary with key and value as string.
 *
 * This value is valid for these requests:
 * - [MEGASdk getUserAttributeType:] - Returns the attribute value of the current account.
 *
 * @return Dictionary containing the key-value pairs of the attribute as string.
 */
@property (readonly, nonatomic, nullable) NSDictionary<NSString *, NSString*> *megaStringDictionary;

/**
 * @brief Returns problematic content as key and error.
 *
 * This value is valid for these requests:
 * - [MEGASdk importPasswordsFromFile:] - Import passwords from a file into your Password Manager tree.
 *
 * @return Dictionary containing the key-value pairs of problematic content as key and error code as value.
 */
@property (readonly, nonatomic, nullable) NSDictionary<NSString *, MEGAIntegerList *> *megaStringIntegerDictionary;

/**
 * @brief Gets the string table response from a request mapped into a collection of NSArray of NSStrings.
 *
 */
@property (readonly, nonatomic, nullable) NSArray<NSArray<NSString *> *> *stringTableArray;


/**
 * @brief Gets the banners response from a request mapped into MEGABannerList.
 *
 * This value is valid for these requests:
 * - [MEGASdk getBanners:] - Obtains the user's banner in MEGA.
 *
 */
@property (readonly, nonatomic, nullable) MEGABannerList *bannerList;

/**
 * @brief Array of MEGAHandle (NSNumber)
 *
 */
@property (readonly, nonatomic, nullable) NSArray<NSNumber *> *megaHandleArray;

/// Array of recent actions buckets
/// 
/// This value is valid for these requests:
///
/// - [MEGASdk getRecentActionsAsyncSinceDays:maxNodes:delegate:]
/// 
/// - [MEGASdk getRecentActionsAsyncSinceDays:maxNodes:]
/// 
@property (readonly, nonatomic, nullable) NSArray<MEGARecentActionBucket *> *recentActionsBuckets;

/// Array of all registered backups of the current use
///
/// This value is valid for these requests:
///
/// - [MEGASdk getBackupInfo:]
///
@property (readonly, nonatomic, nullable) NSArray<MEGABackupInfo *> *backupInfoList;

/**
 * @brief Returns a MEGASet explicitly fetched from online API (typically using 'aft' command)
 *
 * This value is valid for these requests:
 * - [MEGASdk fetchSet:delegate:]
 *
 * @return requested MEGASet or nil if not found
 */
@property (readonly, nonatomic, nullable) MEGASet *set;

/**
 * @brief Returns the list of elements, part of the MEGASetElement explicitly fetched from online API (typically using 'aft' command)
 *
 * This value is valid for these requests:
 * - [MEGASdk fetchSet:delegate:]
 *
 * @return list of elements in the requested MEGASet, or nil if Set not found
 */
@property (readonly, nonatomic, nullable) NSArray<MEGASetElement *> *elementsInSet;

/**
 * @brief Returns the string list
 *
 * This value is valid for these requests:
 * - [MEGASdk fetchAds:delegate] - A list of the adslot ids to fetch
 *
 *  @return an object of MEGAStringList
 */
@property (readonly, nonatomic, nullable) MEGAStringList *megaStringList;

/**
 * @brief Container class to store and load Mega VPN credentials data.
 *
 *  - SlotIDs occupied by VPN credentials.
 *  - Full list of VPN regions.
 *  - IPv4 and IPv6 used on each SlotID.
 *  - ClusterID used on each SlotID.
 *  - Cluster Public Key associated to each ClusterID.
 *
 *  @return an object of MEGAVPNCredentials
 */
@property (readonly, nonatomic, nullable) MEGAVPNCredentials *megaVpnCredentials;

/**
 * @brief Container class to store the results of a network connectivity test
 *
 * @return an object of MEGANetworkConnectivityTestResult
 */
@property (readonly, nonatomic, nullable) MEGANetworkConnectivityTestResults *megaNetworkConnectivityTestResults;

/**
 * @brief Provide all available VPN Regions, including their details.
 *
 * The data included for each Region is the following:
 * - Name (example: hMLKTUojS6o, 1MvzBCx1Uf4)
 * - Country Code (example: ES, LU)
 * - Country Name (example: Spain, Luxembourg)
 * - Region Name (optional) (example: Esch-sur-Alzette)
 * - Town Name (Optional) (example: Bettembourg)
 * - Map of {ClusterID, Cluster}.
 * - For each Cluster:
 *    · Host.
 *    · DNS IP list (as a MEGAStringList).
 *
 * @return An array of MEGAVPNRegion objects with available VPN Regions, if the relevant request was sent;
 * Returns empty if otherwise.
 */
@property (readonly, nonatomic) NSArray<MEGAVPNRegion *> *megaVpnRegions;

/**
 * @brief Get list of available notifications for Notification Center
 *
 * This value is valid only for the following requests:
 * - [MEGASdk getNotificationsWithDelegate]
 *
 * @return an object of MEGANotificationList or nil if not found
 */
@property (readonly, nonatomic, nullable) MEGANotificationList *megaNotifications;

@end

NS_ASSUME_NONNULL_END
