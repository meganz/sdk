/**
 * @file MEGABanner+init.h
 * @brief Private functions of MEGABanner
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
#import "MEGABanner.h"
#import "megaapi.h"

@interface MEGABanner (init)

- (instancetype)initWithMegaBanner:(mega::MegaBanner *)megaBanner cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaBanner *)getCPtr;

@end

