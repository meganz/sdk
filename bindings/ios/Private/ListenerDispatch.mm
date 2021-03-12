/**
 * @file ListenerDispatch.m
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

#import "ListenerDispatch.h"

void dispatch(ListenerQueueType queueType, ListenerBlock block) {
    switch (queueType) {
        case ListenerQueueTypeCurrent:
            block();
            break;
        case ListenerQueueTypeMain:
            dispatch_async(dispatch_get_main_queue(), block);
            break;
        case ListenerQueueTypeGlobalBackground:
            dispatch_async(dispatch_get_global_queue(QOS_CLASS_BACKGROUND, 0), block);
            break;
        case ListenerQueueTypeGlobalUtility:
            dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), block);
            break;
        case ListenerQueueTypeGlobalUserInitiated:
            dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), block);
            break;
        default:
            block();
    }
}
