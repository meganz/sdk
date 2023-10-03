/**
 * @file MEGAEvent.h
 * @brief Provides information about an event
 *
 * (c) 2013-2017 by Mega Limited, Auckland, New Zealand
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

typedef NS_ENUM(NSUInteger, Event) {
    EventCommitDB = 0,
    EventAccountConfirmation = 1,
    EventChangeToHttps = 2,
    EventDisconnect = 3,
    EventAccountBlocked = 4,
    EventStorage = 5,
    EventNodesCurrent = 6,
    EventMediaInfoReady = 7,
    EventStorageSumChanged = 8,
    EventBusinessStatus = 9,
    EventKeyModified = 10,
    EventMiscFlagsReady = 11,
#ifdef ENABLE_SYNC
    EventSyncsDisabled = 13,
    EventSyncsRestored = 14,
#endif
    EventReqStatProgress = 15,
    EventReloading = 16,
    EventReload = 17,
    EventUpgradeSecurity = 18,
};

NS_ASSUME_NONNULL_BEGIN

/**
 * @brief Provides information about an event
 *
 * Objects of this class aren't live, they are snapshots of the state of the event
 * when the object is created, they are immutable.
 */
@interface MEGAEvent : NSObject

/**
 * @brief The type of the event associated with the object
 */
@property (nonatomic, readonly) Event type;

/**
 * @brief Text relative to this event
 */
@property (nonatomic, readonly, nullable) NSString *text;

/**
 * @brief A number relative to this event
 *
 */
@property (nonatomic, readonly) NSInteger number;

NS_ASSUME_NONNULL_END

@end
