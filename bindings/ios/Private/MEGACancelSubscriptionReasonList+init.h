/**
 * @file MEGACancelSubscriptionReasonList+init.h
 * @brief Private functions of MEGACancelSubscriptionReasonList
 *
 * (c) 2024-Present by Mega Limited, Auckland, New Zealand
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
#import "MEGACancelSubscriptionReasonList.h"
#import "megaapi.h"

@interface MEGACancelSubscriptionReasonList (init)

- (instancetype)initWithMegaCancelSubscriptionReasonList:(mega::MegaCancelSubscriptionReasonList *)megaCancelSubscriptionReasonList cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaCancelSubscriptionReasonList *)getCPtr;

@end
