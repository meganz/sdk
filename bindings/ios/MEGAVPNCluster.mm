/**
 * @file MEGAVPNCluster.mm
 * @brief Container to store information of a VPN Cluster.
 *
 * (c) 2024 by Mega Limited, Auckland, New Zealand
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
#import "MEGAVPNCluster.h"
#import "MEGAStringList+init.h"

@interface MEGAVPNCluster ()

@property (nonatomic) mega::MegaVpnCluster *megaVpnCluster;
@property (nonatomic) BOOL cMemoryOwn;

@end

@implementation MEGAVPNCluster

- (instancetype)initWithMegaVpnCluster:(mega::MegaVpnCluster *)megaVpnCluster cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    if (self) {
        _megaVpnCluster = megaVpnCluster;
        _cMemoryOwn = cMemoryOwn;
    }
    return self;
}

- (void)dealloc {
    if (_cMemoryOwn) {
        delete _megaVpnCluster;
    }
}

- (mega::MegaVpnCluster *)getCPtr {
    return self.megaVpnCluster;
}

- (nonnull NSString *)host {
    const char *host = self.megaVpnCluster->getHost();
    return host ? [NSString stringWithUTF8String:host] : @"";
}

- (nonnull NSArray<NSString *> *)dns {
    if (!self.megaVpnCluster) {
        return @[];
    }
    mega::MegaStringList *dnsList = self.megaVpnCluster->getDns();
    if (!dnsList) {
        return @[];
    }
    int count = dnsList->size();
    NSMutableArray<NSString *> *dnsArray = [[NSMutableArray alloc] initWithCapacity:(NSUInteger)count];
    for (int i = 0; i < count; i++) {
        const char *dnsEntry = dnsList->get(i);
        if (dnsEntry) {
            [dnsArray addObject:[NSString stringWithUTF8String:dnsEntry]];
        }
    }
    delete dnsList;
    return dnsArray;
}

@end
