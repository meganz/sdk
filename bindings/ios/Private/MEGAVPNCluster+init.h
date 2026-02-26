/**
 * @file MEGAVPNCluster+init.h
 * @brief Private functions of MEGAVPNCluster
 *
 * (c) 2024 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright
 * Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this program.
 */
#import "MEGAVPNCluster.h"
#import "megaapi.h"

NS_ASSUME_NONNULL_BEGIN

@interface MEGAVPNCluster (init)

- (instancetype)initWithMegaVpnCluster:(mega::MegaVpnCluster *)megaVpnCluster cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaVpnCluster *)getCPtr;

@end

NS_ASSUME_NONNULL_END
