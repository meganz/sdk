/**
 * @file MEGAAccountSubscription+init.h
 * @brief Private functions of MEGAAccountSubscription
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
#import "MEGAAccountSubscription.h"
#import "megaapi.h"

@interface MEGAAccountSubscription (init)

- (instancetype)initWithMegaAccountSubscription:(mega::MegaAccountSubscription *)accountSubscription cMemoryOwn:(BOOL)cMemoryOwn;
+ (mega::MegaAccountSubscription *)getCPtr;

@end
