/**
 * @file MEGAVPNRegion.h
 * @brief Container to store information of a VPN Region.
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
#import "MEGAVPNCluster.h"

NS_ASSUME_NONNULL_BEGIN

/**
 * @brief Container to store information of a VPN Region.
 *
 *  - Name (example: hMLKTUojS6o, 1MvzBCx1Uf4)
 *  - Country Code (example: ES, LU)
 *  - Country Name (example: Spain, Luxembourg)
 *  - Region Name (optional) (example: Esch-sur-Alzette)
 *  - Town Name (Optional) (example: Bettembourg)
 *  - Clusters (contain information like host, DNS list, possibly others)
 *
 * Instances of this class are immutable.
 */
@interface MEGAVPNRegion : NSObject

/**
 * @brief Get the name of this VPN Region.
 *
 * @return The name of this VPN Region, always not-null.
 */
@property (nonatomic, readonly, nonnull) NSString *name;

/**
 * @brief Get the country code where the VPN Region is located.
 *
 * @return The country code for this VPN Region, always not-null.
 */
@property (nonatomic, readonly, nonnull) NSString *countryCode;

/**
 * @brief Get the name of the country where the VPN Region is located.
 *
 * @return The country name for this VPN Region, always not-null.
 */
@property (nonatomic, readonly, nonnull) NSString *countryName;

/**
 * @brief Get the name of the country region where this VPN Region is located.
 *
 * Optional value. It may be empty for certain VPN Regions.
 *
 * @return The country region name for this VPN Region, always not-null.
 */
@property (nonatomic, readonly, nonnull) NSString *regionName;

/**
 * @brief Get the name of the town where this VPN is located.
 *
 * Optional value. It may be empty for certain VPN Regions.
 *
 * @return The name of the town for this VPN Region, always not-null.
 */
@property (nonatomic, readonly, nonnull) NSString *townName;

/**
 * @brief Get a container with all Clusters of this VPN Region.
 *
 * The caller takes ownership of the returned instance.
 *
 * @return A dictionary mapping cluster IDs (NSNumber) to MEGAVPNCluster objects, always not-null.
 */
@property (nonatomic, readonly, nonnull) NSDictionary<NSNumber *, MEGAVPNCluster *> *clusters;

@end

NS_ASSUME_NONNULL_END
