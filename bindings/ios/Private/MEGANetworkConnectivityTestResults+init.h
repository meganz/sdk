/**
 * @file MEGANetworkConnectivityTestResults+init.h
 * @brief Private functions of MEGANetworkConnectivityTestResults
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

@interface MEGANetworkConnectivityTestResults (init)

- (instancetype)initWithMegaNetworkConnectivityTestResults:(MegaNetworkConnectivityTestResults *)megaNetworkConnectivityTestResults cMemoryOwn:(BOOL)cMemoryOwn;
- (MegaNetworkConnectivityTestResults *)getCPtr;

@end
