/**
 * @file MEGAVPNCredentials.mm
 * @brief List of MEGAUser objects
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
#import "MEGAIntegerList+init.h"
#import "MEGAStringList+init.h"

@interface MEGAVPNCredentials ()
@property mega::MegaVpnCredentials *megaVpnCredentials;
@property BOOL cMemoryOwn;
@end

@implementation MEGAVPNCredentials

- (instancetype)initWithMegaVpnCredentials:(mega::MegaVpnCredentials *)megaVpnCredentials cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    if (self) {
        _megaVpnCredentials = megaVpnCredentials;
        _cMemoryOwn = cMemoryOwn;
    }
    return self;
}

- (void)dealloc {
    if (_cMemoryOwn) {
        delete _megaVpnCredentials;
    }
}

- (MEGAVPNCredentials *)clone {
    return [[MEGAVPNCredentials alloc] initWithMegaVpnCredentials:_megaVpnCredentials->copy() cMemoryOwn:YES];
}

- (mega::MegaVpnCredentials *)getCPtr {
    return self.megaVpnCredentials;
}

- (MEGAIntegerList *)slotIDs {
    return [[MEGAIntegerList alloc] initWithMegaIntegerList:_megaVpnCredentials->getSlotIDs() cMemoryOwn:YES];
}

- (MEGAStringList *)vpnRegions {
    return [[MEGAStringList alloc] initWithMegaStringList:_megaVpnCredentials->getVpnRegions() cMemoryOwn:YES];
}

- (NSString *)ipv4ForSlotID:(NSInteger)slotID {
    const char *ipv4 = _megaVpnCredentials->getIPv4((int)slotID);
    return ipv4 ? [[NSString alloc] initWithUTF8String:ipv4] : nil;
}

- (NSString *)ipv6ForSlotID:(NSInteger)slotID {
    const char *ipv6 = _megaVpnCredentials->getIPv6((int)slotID);
    return ipv6 ? [[NSString alloc] initWithUTF8String:ipv6] : nil;
}

- (NSString *)deviceIDForSlotID:(NSInteger)slotID {
    const char *deviceID = _megaVpnCredentials->getDeviceID((int)slotID);
    return deviceID ? [[NSString alloc] initWithUTF8String:deviceID] : nil;
}

- (NSInteger)clusterIDForSlotID:(NSInteger)slotID {
    return _megaVpnCredentials->getClusterID((int)slotID);
}

- (NSString *)clusterPublicKeyForClusterID:(NSInteger)clusterID {
    const char *publicKey = _megaVpnCredentials->getClusterPublicKey((int)clusterID);
    return publicKey ? [[NSString alloc] initWithUTF8String:publicKey] : nil;
}

@end
