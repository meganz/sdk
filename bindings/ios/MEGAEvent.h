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
    EventChangeToHttps = 2
};

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
@property (nonatomic, readonly) NSString *text;

/**
 * @brief Creates a copy of this MEGAEvent object
 *
 * The resulting object is fully independent of the source MEGAEvent,
 * it contains a copy of all internal attributes, so it will be valid after
 * the original object is deleted.
 *
 * You are the owner of the returned object
 *
 * @return Copy of the MEGAEvent object
 */
- (instancetype)clone;

@end
