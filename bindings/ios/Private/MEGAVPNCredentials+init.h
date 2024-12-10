/**
 * @file MEGAVPNCredentials+init.h
 * @brief Private functions of MEGAVPNCredentials
 *
 * (c) 2023- by Mega Limited, Auckland, New Zealand
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

#import "MEGAVPNCredentials.h"
#import "megaapi.h"

@interface MEGAVPNCredentials (init)

/**
 * @brief Initializes a new instance of MEGAVPNCredentials
 *
 * @param megaVpnCredentials The pointer to the C++ MegaVpnCredentials instance
 * @param cMemoryOwn Indicates whether this instance owns the C++ object memory
 */
- (instancetype)initWithMegaVpnCredentials:(mega::MegaVpnCredentials *)megaVpnCredentials cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaVpnCredentials *)getCPtr;

@end
