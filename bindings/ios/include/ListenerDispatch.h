/**
 * @file ListenerDispatch.h
 * @brief dispatch functions for listener
 *
 * (c) 2021 - by Mega Limited, Auckland, New Zealand
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

#ifndef ListenerDispatch_h
#define ListenerDispatch_h

typedef NS_ENUM (NSUInteger, ListenerQueueType) {
    ListenerQueueTypeCurrent, // Current thread will be used. It is recommended to use current thread whenever it is possible. It has no thread switching, hence good performance
    ListenerQueueTypeMain, // Main queue for UI update
    ListenerQueueTypeGlobalBackground, // Global queue with QOS background
    ListenerQueueTypeGlobalUtility, // Global queue with QOS utility
    ListenerQueueTypeGlobalUserInitiated, // Global queue with QOS user initiated
};

typedef void (^ListenerBlock)(void);

void dispatch(ListenerQueueType queueType, ListenerBlock block);

#endif
