/**
 * @file MEGAVPNRegion.mm
 * @brief Container to store information of a VPN Region.
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
#import "MEGAVPNRegion.h"
#import "MEGAVPNCluster.h"
#import "MEGAVPNCluster+init.h"
#import "MEGAIntegerList+init.h"

@interface MEGAVPNRegion ()

@property (nonatomic) mega::MegaVpnRegion *megaVpnRegion;
@property (nonatomic) BOOL cMemoryOwn;

@end

@implementation MEGAVPNRegion

- (instancetype)initWithMegaVpnRegion:(mega::MegaVpnRegion *)megaVpnRegion cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    if (self) {
        _megaVpnRegion = megaVpnRegion;
        _cMemoryOwn = cMemoryOwn;
    }
    return self;
}

- (void)dealloc {
    if (_cMemoryOwn) {
        delete _megaVpnRegion;
    }
}

- (mega::MegaVpnRegion *)getCPtr {
    return _megaVpnRegion;
}

- (NSString *)name {
    const char *name = _megaVpnRegion->getName();
    return name ? [NSString stringWithUTF8String:name] : @"";
}

- (NSString *)countryCode {
    const char *countryCode = _megaVpnRegion->getCountryCode();
    return countryCode ? [NSString stringWithUTF8String:countryCode] : @"";
}

- (NSString *)countryName {
    const char *countryName = _megaVpnRegion->getCountryName();
    return countryName ? [NSString stringWithUTF8String:countryName] : @"";
}

- (NSString *)regionName {
    const char *regionName = _megaVpnRegion->getRegionName();
    return regionName ? [NSString stringWithUTF8String:regionName] : @"";
}

- (NSString *)townName {
    const char *townName = _megaVpnRegion->getTownName();
    return townName ? [NSString stringWithUTF8String:townName] : @"";
}

- (NSDictionary<NSNumber *, MEGAVPNCluster *> *)clusters {
    if (!_megaVpnRegion) {
        return nil;
    }
    mega::MegaVpnClusterMap *clusterMap = _megaVpnRegion->getClusters();
    if (!clusterMap) {
        return nil;
    }
    mega::MegaIntegerList *keys = clusterMap->getKeys();
    int count = (int)keys->size();
    NSMutableDictionary<NSNumber *, MEGAVPNCluster *> *clustersDict = [NSMutableDictionary.alloc initWithCapacity:(NSUInteger)count];
    for (int i = 0; i < count; i++) {
        int64_t key = keys->get(i);
        mega::MegaVpnCluster *cluster = clusterMap->get(key);
        if (cluster) {
            MEGAVPNCluster *vpnCluster = [MEGAVPNCluster.alloc initWithMegaVpnCluster:cluster->copy() cMemoryOwn:YES];
            clustersDict[@(key)] = vpnCluster;
        }
    }
    delete keys;
    delete clusterMap;
    return clustersDict.copy;
}

@end
