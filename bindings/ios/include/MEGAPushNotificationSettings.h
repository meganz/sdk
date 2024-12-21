/**
 * @file MEGAPushNotificationSettings.h
 * @brief Push Notification related MEGASdk methods.
 *
 * (c) 2019- by Mega Limited, Auckland, New Zealand
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

NS_ASSUME_NONNULL_BEGIN

/**
 * @brief Provides information about the notification settings
 *
 * The notifications can be configured:
 *
 * 1. Globally
 *  1.1. Mute all notifications
 *  1.2. Notify only during a schedule: from one time to another time of the day, specifying the timezone of reference
 *  1.3. Do Not Disturb for a period of time: it overrides the schedule, if any (no notification will be generated)
 *
 * 2. Chats: Mute for all chats notifications
 *
 * 3. Per chat:
 *  2.1. Mute all notifications from the specified chat
 *  2.2. Always notify for the specified chat
 *  2.3. Do Not Disturb for a period of time for the specified chat
 *
 * @note Notification settings per chat override any global notification setting.
 * @note The DND mode per chat is not compatible with the option to always notify and viceversa.
 *
 * 4. Contacts: new incoming contact request, outgoing contact request accepted...
 * 5. Shared folders: new shared folder, access removed...
 *
 */

@interface MEGAPushNotificationSettings : NSObject

/**
* @brief Getter returns the timestamp (in seconds since the Epoch) until the chats DND mode is enabled and setter can be used to set the global DND mode for chat for a period of time.
*
* This property returns valid value only if the value of property globalChatsDndEnabled is YES.
* No chat notifications will be generated until the specified timestamp.
*
*
* If there's no DND mode established, this function returns -1.
* @note a DND value of 0 means the DND does not expire.
*
*/
@property (assign, nonatomic) int64_t globalChatsDNDTimestamp;

/**
* @brief Getter returns whether Do-Not-Disturb mode for chats is enabled or not and setter can be used to enable or disable notifications related to all chats.
*/
@property (assign, nonatomic) BOOL globalChatsDndEnabled;


/**
 * @brief Returns whether Do-Not-Disturb mode for a chat is enabled or not
 *
 * @param chatId handle of the node that identifies the chat room
 * @return YES if enabled, NO otherwise
 */
- (BOOL)isChatDndEnabledForChatId:(uint64_t)chatId;

/**
 * @brief Enable or disable notifications for a chat
 *
 * If notifications for this chat are disabled, the DND settings for this chat,
 * if any, will be cleared.
 *
 * @note Settings per chat override any global notification setting.
 *
 * @param enabled YES to enable, NO to disable
 * @param chatId handle of the node that identifies the chat room
 */
- (void)setChatEnabled:(BOOL)enabled forChatId:(uint64_t)chatId;

/**
 * @brief Returns the timestamp until the Do-Not-Disturb mode for a chat
 *
 * This method returns a valid value only if [MEGAPushNotificationSettings isChatEnabled]
 * returns NO and [MEGAPushNotificationSettings isChatDndEnabled] returns YES.
 *
 * If there's no DND mode established for the specified chat, this function returns -1.
 * @note a DND value of 0 means the DND does not expire.
 *
 * @param chatId handle of the Node that identifies the chat room
 * @return timestamp until DND mode is enabled (in seconds since the Epoch)
 */
- (int64_t)timestampForChatId:(uint64_t)chatId;

/**
 * @brief Set the DND mode for a chat for a period of time
 *
 * No notifications will be generated until the specified timestamp.
 *
 * This setting is not compatible with the "Always notify". If DND mode is
 * configured, the "Always notify" will be disabled.
 *
 * If chat notifications were totally disabled for the specified chat, this
 * function will enable them back (but will not generate notification until
 * the specified timestamp).
 *
 * @param chatId handle of the node that identifies the chat room
 * @param timestamp timestamp until DND mode is enabled (in seconds since the Epoch)
 */
- (void)setChatDndForChatId:(uint64_t)chatId untilTimestamp:(int64_t)timestamp;

@end

NS_ASSUME_NONNULL_END
