/**
 * @file MEGAVPNCluster.h
 * @brief Container to store information of a VPN Cluster.
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
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/**
 * @brief Container to store information of a VPN Cluster.
 *
 *  - Host
 *  - DNS: list of IPs
 *
 * Instances of this class are immutable.
 */
@interface MEGAVPNCluster : NSObject

/**
 * @brief Get the host of this VPN Cluster.
 *
 * The caller does not take ownership of the returned value, which is valid as long as the current
 * instance is valid.
 *
 * @return The host of this VPN Cluster, always not-null.
 */
@property (nonatomic, readonly) NSString *host;

/**
 * @brief Get the list of IPs for the current VPN Cluster.
 *
 * The caller takes ownership of the returned instance.
 *
 * @return An NSArray containing the IPs for the current VPN Cluster, always not-null.
 */
@property (nonatomic, readonly) NSArray<NSString *> *dns;

@end

NS_ASSUME_NONNULL_END
