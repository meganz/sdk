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
#import "MEGANotificationList.h"

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM (NSInteger, MEGARequestType) {
    MEGARequestTypeLogin,
    MEGARequestTypeCreateFolder,
    MEGARequestTypeMove,
    MEGARequestTypeCopy,
    MEGARequestTypeRename,
    MEGARequestTypeRemove,
    MEGARequestTypeShare,
    MEGARequestTypeImportLink,
    MEGARequestTypeExport,
    MEGARequestTypeFetchNodes,
    MEGARequestTypeAccountDetails,
    MEGARequestTypeChangePassword,
    MEGARequestTypeUpload,
    MEGARequestTypeLogout,
    MEGARequestTypeGetPublicNode,
    MEGARequestTypeGetAttrFile,
    MEGARequestTypeSetAttrFile,
    MEGARequestTypeGetAttrUser,
    MEGARequestTypeSetAttrUser,
    MEGARequestTypeRetryPendingConnections,
    MEGARequestTypeRemoveContact,
    MEGARequestTypeCreateAccount,
    MEGARequestTypeConfirmAccount,
    MEGARequestTypeQuerySignUpLink,
    MEGARequestTypeAddSync,
    MEGARequestTypeRemoveSync,
    MEGARequestTypeDisableSync,
    MEGARequestTypeEnableSync,
    MEGARequestTypeCopySyncConfig,
    MEGARequestTypeCopyCachedConfig,
    MEGARequestTypeImportSyncConfigs,
    MEGARequestTypeRemoveSyncs,
    MEGARequestTypePauseTransfers,
    MEGARequestTypeCancelTransfer,
    MEGARequestTypeCancelTransfers,
    MEGARequestTypeDelete,
    MEGARequestTypeReportEvent,
    MEGARequestTypeCancelAttrFile,
    MEGARequestTypeGetPricing,
    MEGARequestTypeGetPaymentId,
    MEGARequestTypeGetUserData,
    MEGARequestTypeLoadBalancing,
    MEGARequestTypeKillSession,
    MEGARequestTypeSubmitPurchaseReceipt,
    MEGARequestTypeCreditCardStore,
    MEGARequestTypeUpgradeAccount,
    MEGARequestTypeCreditCardQuerySubscriptions,
    MEGARequestTypeCreditCardCancelSubscriptions,
    MEGARequestTypeGetSessionTransferUrl,
    MEGARequestTypeGetPaymentMethods,
    MEGARequestTypeInviteContact,
    MEGARequestTypeReplyContactRequest,
    MEGARequestTypeSubmitFeedback,
    MEGARequestTypeSendEvent,
    MEGARequestTypeCleanRubbishBin,
    MEGARequestTypeSetAttrNode,
    MEGARequestTypeChatCreate,
    MEGARequestTypeChatFetch,
    MEGARequestTypeChatInvite,
    MEGARequestTypeChatRemove,
    MEGARequestTypeChatUrl,
    MEGARequestTypeChatGrantAccess,
    MEGARequestTypeChatRemoveAccess,
    MEGARequestTypeUseHttpsOnly,
    MEGARequestTypeSetProxy,
    MEGARequestTypeGetRecoveryLink,
    MEGARequestTypeQueryRecoveryLink,
    MEGARequestTypeConfirmRecoveryLink,
    MEGARequestTypeGetCancelLink,
    MEGARequestTypeConfirmCancelLink,
    MEGARequestTypeGetChangeEmailLink,
    MEGARequestTypeConfirmChangeEmailLink,
    MEGARequestTypeChatUpdatePermissions,
    MEGARequestTypeChatTruncate,
    MEGARequestTypeChatSetTitle,
    MEGARequestTypeSetMaxConnections,
    MEGARequestTypePauseTransfer,
    MEGARequestTypeMoveTransfer,
    MEGARequestTypeChatPresenceUrl,
    MEGARequestTypeRegisterPushNotification,
    MEGARequestTypeGetUserEmail,
    MEGARequestTypeAppVersion,
    MEGARequestTypeGetLocalSSLCertificate,
    MEGARequestTypeSendSignupLink,
    MEGARequestTypeQueryDns,
    MEGARequestTypeQueryGelb,
    MEGARequestTypeChatStats,
    MEGARequestTypeDownloadFile,
    MEGARequestTypeQueryTransferQuota,
    MEGARequestTypePasswordLink,
    MEGARequestTypeGetAchievements,
    MEGARequestTypeRestore,
    MEGARequestTypeRemoveVersions,
    MEGARequestTypeChatArchive,
    MEGARequestTypeWhyAmIBlocked,
    MEGARequestTypeContactLinkCreate,
    MEGARequestTypeContactLinkQuery,
    MEGARequestTypeContactLinkDelete,
    MEGARequestTypeFolderInfo,
    MEGARequestTypeRichLink,
    MEGARequestTypeKeepMeAlive,
    MEGARequestTypeMultiFactorAuthCheck,
    MEGARequestTypeMultiFactorAuthGet,
    MEGARequestTypeMultiFactorAuthSet,
    MEGARequestTypeAddBackup,
    MEGARequestTypeRemoveBackup,
    MEGARequestTypeTimer,
    MEGARequestTypeAbortCurrentBackup,
    MEGARequestTypeGetPSA,
    MEGARequestTypeFetchTimeZone,
    MEGARequestTypeUseralertAcknowledge,
    MEGARequestTypeChatLinkHandle,
    MEGARequestTypeChatLinkUrl,
    MEGARequestTypeSetPrivateMode,
    MEGARequestTypeAutojoinPublicChat,
    MEGARequestTypeCatchup,
    MEGARequestTypePublicLinkInformation,
    MEGARequestTypeGetBackgroundUploadURL,
    MEGARequestTypeCompleteBackgroundUpload,
    MEGARequestTypeCloudStorageUsed,
    MEGARequestTypeSendSMSVerificationCode,
    MEGARequestTypeCheckSMSVerificationCode,
    MEGARequestTypeGetRegisteredContacts,  // Deprecated
    MEGARequestTypeGetCountryCallingCodes,
    MEGARequestTypeVerifyCredentials,
    MEGARequestTypeGetMiscFlags,
    MEGARequestTypeResendVerificationEmail,
    MEGARequestTypeSupportTicket,
    MEGARequestTypeRetentionTime,
    MEGARequestTypeResetSmsVerifiedNumber,
    MEGARequestTypeSendDevCommand,
    MEGARequestTypeGetBanners,
    MEGARequestTypeDismissBanner,
    MEGARequestTypeBackupPut,
    MEGARequestTypeBackupRemove,
    MEGARequestTypeBackupPutHeartbeat,
    MEGARequestTypeFetchAds,
    MEGARequestTypeQueryAds,
    MEGARequestTypeGetAttrNode,
    MEGARequestTypeLoadExternalDriveBackups,
    MEGARequestTypeCloseExternalDriveBackups,
    MEGARequestTypeGetDownloadUrls,
    MEGARequestTypeStartChatCall,
    MEGARequestTypeJoinChatCall,
    MEGARequestTypeEndChatCall,
    MEGARequestTypeGetFAUploadUrl,
    MEGARequestTypeExecuteOnThread,
    MEGARequestTypeGetChatOptions,
    MEGARequestTypeGetRecentActions,
    MEGARequestTypeCheckRecoveryKey,
    MEGARequestTypeSetMyBackups,
    MEGARequestTypePutSet,
    MEGARequestTypeRemoveSet,
    MEGARequestTypeFetchSet,
    MEGARequestTypePutSetElement,
    MEGARequestTypeRemoveSetElement,
    MEGARequestTypeRemoveOldBackupNodes,
    MEGARequestTypeSetSyncRunstate,
    MEGARequestTypeAddUpdateScheduledMeeting,
    MEGARequestTypeDelScheduledMeeting,
    MEGARequestTypeFetchScheduledMeeting,
    MEGARequestTypeFetchScheduledMeetingOccurrences,
    MEGARequestTypeOpenShareDialog,
    MEGARequestTypeUpgradeSecurity,
    MEGARequestTypePutSetElements,
    MEGARequestTypeRemoveSetElements,
    MEGARequestTypeExportSet,
    MEGARequestTypeExportedSetElement,
    MEGARequestTypeGetRecommenedProPlan,
    MEGARequestTypeBackupInfo,
    MEGARequestTypeBackupRemoveMD,
    MEGARequestTypeABTestActive,
    MEGARequestTypeGetVPNRegions,
    MEGARequestTypeGetVPNCredentials,
    MEGARequestTypePutVPNCredentials,
    MEGARequestTypeDeleteVPNCredentials,
    MEGARequestTypeCheckVPNCredentials,
    MEGARequestTypeGetNotifications,
    TotalOfRequestTypes
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
