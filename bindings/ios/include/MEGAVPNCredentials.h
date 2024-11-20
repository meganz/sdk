/**
 * @file MEGAVPNCredentials.h
 * @brief Container to store information of a VPN Cluster.
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
#import <Foundation/Foundation.h>
#import "MEGAIntegerList.h"
#import "MEGAStringList.h"
#import "MEGAVPNRegion.h"

@interface MEGAVPNCredentials : NSObject

/**
 * @brief Gets the list of SlotIDs.
 *
 * @return A MEGAIntegerList containing the SlotIDs.
 */
- (nonnull MEGAIntegerList *)slotIDs;

/**
 * @brief Gets the list of available VPN regions.
 *
 * @return A MEGAStringList containing the VPN regions.
 */
- (nonnull MEGAStringList *)vpnRegions;

/**
 * @brief Get the list of the available VPN regions, including the clusters for each region.
 *
 * The caller takes the ownership of the returned array.
 *
 * @return An NSArray of MEGAVPNRegion objects.
 */
- (nonnull NSArray<MEGAVPNRegion *> *)vpnRegionsDetailed;

/**
 * @brief Gets the IPv4 address associated with a given SlotID.
 *
 * @param slotID The SlotID for which the IPv4 address is requested.
 * @return A string containing the IPv4 address.
 */
- (nullable NSString *)ipv4ForSlotID:(NSInteger)slotID;

/**
 * @brief Gets the IPv6 address associated with a given SlotID.
 *
 * @param slotID The SlotID for which the IPv6 address is requested.
 * @return A string containing the IPv6 address.
 */
- (nullable NSString *)ipv6ForSlotID:(NSInteger)slotID;

/**
 * @brief Gets the DeviceID associated with a given SlotID.
 *
 * @param slotID The SlotID for which the DeviceID is requested.
 * @return A string containing the DeviceID.
 */
- (nullable NSString *)deviceIDForSlotID:(NSInteger)slotID;

/**
 * @brief Gets the ClusterID associated with a given SlotID.
 *
 * @param slotID The SlotID for which the ClusterID is requested.
 * @return An integer containing the ClusterID.
 */
- (NSInteger)clusterIDForSlotID:(NSInteger)slotID;

/**
 * @brief Gets the Cluster Public Key associated with a given ClusterID.
 *
 * @param clusterID The ClusterID for which the Public Key is requested.
 * @return A string containing the Cluster Public Key.
 */
- (nullable NSString *)clusterPublicKeyForClusterID:(NSInteger)clusterID;

@end
