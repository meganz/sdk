/**
 * @file MEGAUserAlert.h
 * @brief Represents a user alert in MEGA.
 *
 * (c) 2018-Present by Mega Limited, Auckland, New Zealand
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

typedef NS_ENUM(NSInteger, MEGAUserAlertType) {
    MEGAUserAlertTypeIncomingPendingContactRequest,
    MEGAUserAlertTypeIncomingPendingContactCancelled,
    MEGAUserAlertTypeIncomingPendingContactReminder,
    MEGAUserAlertTypeContactChangeDeletedYou,
    MEGAUserAlertTypeContactChangeContactEstablished,
    MEGAUserAlertTypeContactChangeAccountDeleted,
    MEGAUserAlertTypeContactChangeBlockedYou,
    MEGAUserAlertTypeUpdatePendingContactIncomingIgnored,
    MEGAUserAlertTypeUpdatePendingContactIncomingAccepted,
    MEGAUserAlertTypeUpdatePendingContactIncomingDenied,
    MEGAUserAlertTypeUpdatePendingContactOutgoingAccepted,
    MEGAUserAlertTypeUpdatePendingContactOutgoingDenied,
    MEGAUserAlertTypeNewShare,
    MEGAUserAlertTypeDeletedShare,
    MEGAUserAlertTypeNewShareNodes,
    MEGAUserAlertTypeRemovedSharesNodes,
    MEGAUserAlertTypePaymentSucceeded,
    MEGAUserAlertTypePaymentFailed,
    MEGAUserAlertTypePaymentReminder,
    MEGAUserAlertTypeTakedown,
    MEGAUserAlertTypeTakedownReinstated,
    MEGAUserAlertTypeTotal
};

/**
 * @brief Represents a user alert in MEGA.
 * Alerts are the notifictions appearing under the bell in the webclient
 *
 * Objects of this class aren't live, they are snapshots of the state
 * in MEGA when the object is created, they are immutable.
 *
 * MEGAUserAlerts can be retrieved with [MEGASdk userAlertList]
 *
 */
@interface MEGAUserAlert : NSObject

/**
 * @brief The id of the alert
 *
 * The ids are assigned to alerts sequentially from program start,
 * however there may be gaps. The id can be used to create an
 * association with a UI element in order to process updates in callbacks.
 */
@property (nonatomic, readonly) NSUInteger identifier;

/**
 * @brief Whether the alert has been acknowledged by this client or another
 */
@property (nonatomic, readonly, getter=isSeen) BOOL seen;

/**
 * @brief Whether the alert is still relevant to the logged in user.
 *
 * An alert may be relevant initially but become non-relevant, eg. payment reminder.
 * Alerts which are no longer relevant are usually removed from the visible list.
 */
@property (nonatomic, readonly, getter=isRelevant) BOOL relevant;

/**
 * @brief The type of alert associated with the object
 *
 * @return Type of alert associated with the object
 */
@property (nonatomic, readonly) MEGAUserAlertType type;

/**
 * @brief A readable string that shows the type of alert
 *
 * This function returns a pointer to a statically allocated buffer.
 * You don't have to free the returned pointer
 */
@property (nonatomic, readonly) NSString *typeString;

/**
 * @brief The handle of a user related to the alert
 *
 * This value is valid for user related alerts.
 *
 * @return the associated user's handle, otherwise UNDEF
 */
@property (readonly, nonatomic) uint64_t userHandle;

/**
 * @brief The handle of a node related to the alert
 *
 * This value is valid for alerts that relate to a single node.
 *
 * @return the relevant node handle, or UNDEF if this alert does not have one.
 */
@property (readonly, nonatomic) uint64_t nodeHandle;

/**
 * @brief An email related to the alert
 *
 * This value is valid for alerts that relate to another user, provided the
 * user could be looked up at the time the alert arrived. If it was not available,
 * this function will return false and the client can request it via the userHandle.
 */
@property (nonatomic, readonly) NSString *email;

/**
 * @brief The path of a file, folder, or node related to the alert
 *
 * This value is valid for those alerts that relate to a single path, provided
 * it could be looked up from the cached nodes at the time the alert arrived.
 * Otherwise, it may be obtainable via the nodeHandle.
 */
@property (nonatomic, readonly) NSString *path;

/**
 * @brief The name of a file, folder, or node related to the alert
 *
 * This value is valid for those alerts that relate to a single name, provided
 * it could be looked up from the cached nodes at the time the alert arrived.
 * Otherwise, it may be obtainable via the nodeHandle.
 */
@property (nonatomic, readonly) NSString *name;

/**
 * @brief The heading related to this alert
 *
 * This value is valid for all alerts, and similar to the strings displayed in the
 * webclient alerts.
 */
@property (nonatomic, readonly) NSString *heading;

/**
 * @brief The title related to this alert
 *
 * This value is valid for all alerts, and similar to the strings displayed in the
 * webclient alerts.
 */
@property (nonatomic, readonly) NSString *title;

/**
 * @brief Indicates if the user alert is changed by yourself or by another client.
 *
 * This value is only useful for user alerts notified by [MEGADelegate onUserAlertsUpdate] or
 * [MEGAGlobalDelegate onUserAlertsUpdate] that can notify about user alerts modifications.
 *
 * @return NO if the change is external. YES if the change is the result of a
 * request sent by this instance of the SDK.
 */
@property (nonatomic, readonly, getter=isOwnChange) BOOL ownChange;

/**
 * @brief Creates a copy of this MEGAUserAlert object.
 *
 * The resulting object is fully independent of the source MEGAUserAlert,
 * it contains a copy of all internal attributes, so it will be valid after
 * the original object is deleted.
 *
 * @return Copy of the MEGAUserAlert object
 */
- (instancetype)clone;

/**
 * @brief Returns a number related to this alert
 *
 * This value is valid for these alerts:
 * MEGAUserAlertTypeNewShareNodes (0: folder count 1: file count )
 * MEGAUserAlertTypeRemovedSharesNodes (0: item count )
 *
 * @return Number related to this request, or -1 if the index is invalid
 */
- (int64_t)numberAtIndex:(NSUInteger)index;

/**
 * @brief Returns a timestamp related to this alert
 *
 * This value is valid for index 0 for all requests, indicating when the alert occurred.
 * Additionally MEGAUserAlertTypePaymentReminder index 1 is the timestamp of the expiry of the period.
 *
 * @return Timestamp related to this request, or -1 if the index is invalid
 */
- (int64_t)timestampAtIndex:(NSUInteger)index;

/**
 * @brief Returns an additional string, related to the alert
 *
 * This value is currently only valid for
 *   MEGAUserAlertTypePaymentSucceeded   index 0: the plan name
 *   MEGAUserAlertTypePaymentFailed      index 0: the plan name
 *
 * @return a pointer to the string if index is valid; otherwise nil
 */
- (NSString *)stringAtIndex:(NSUInteger)index;

@end
