/**
 * @file MEGANetworkConnectivityTestResults.mm
 * @brief Container to store the network connectivity test results.
 *
 * (c) 2025 by Mega Limited, Auckland, New Zealand
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
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#import "MEGANetworkConnectivityTestResults.h"
#import "megaapi.h"

using namespace mega;

@interface MEGANetworkConnectivityTestResults ()
@property (nonatomic, assign) MegaNetworkConnectivityTestResults *megaNetworkConnectivityTestResults;
@property (nonatomic, assign) BOOL cMemoryOwn;
@end

@implementation MEGANetworkConnectivityTestResults

- (instancetype)initWithMegaNetworkConnectivityTestResults:(MegaNetworkConnectivityTestResults *)megaNetworkConnectivityTestResults cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    if (self) {
        _megaNetworkConnectivityTestResults = megaNetworkConnectivityTestResults;
        _cMemoryOwn = cMemoryOwn;
    }
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaNetworkConnectivityTestResults;
    }
}

- (MegaNetworkConnectivityTestResults *)getCPtr {
    return self.megaNetworkConnectivityTestResults;
}

- (MEGANetworkConnectivityTestResult)ipv4UDP {
    return (MEGANetworkConnectivityTestResult) (self.megaNetworkConnectivityTestResults ? self.megaNetworkConnectivityTestResults->getIPv4UDP() : -1);
}

- (MEGANetworkConnectivityTestResult)ipv4DNS {
    return (MEGANetworkConnectivityTestResult) (self.megaNetworkConnectivityTestResults ? self.megaNetworkConnectivityTestResults->getIPv4DNS() : -1);
}

- (MEGANetworkConnectivityTestResult)ipv6UDP {
    return (MEGANetworkConnectivityTestResult) (self.megaNetworkConnectivityTestResults ? self.megaNetworkConnectivityTestResults->getIPv6UDP() : -1);
}

- (MEGANetworkConnectivityTestResult)ipv6DNS {
    return (MEGANetworkConnectivityTestResult) (self.megaNetworkConnectivityTestResults ? self.megaNetworkConnectivityTestResults->getIPv6DNS() : -1);
}

- (MEGANetworkConnectivityTestResults *)copy {
    return [[MEGANetworkConnectivityTestResults alloc] initWithMegaNetworkConnectivityTestResults:self.megaNetworkConnectivityTestResults->copy() cMemoryOwn:YES];
}

@end
