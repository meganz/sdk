/**
 * @file MEGABannerList.mm
 * @brief List of MEGABanner objects
 *
 * (c) 2018-Present by Mega Limited, Auckland, New Zealand
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
#import "megaapi.h"
#import "MEGABanner.h"
#import "MEGABanner+init.h"
#import "MEGABannerList.h"

using namespace mega;

@interface MEGABannerList ()

@property MegaBannerList *megaBannerList;
@property BOOL cMemoryOwn;

@end

@implementation MEGABannerList

- (instancetype)initWithMegaBannerList:(MegaBannerList *)megaBannerList cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self) {
        _megaBannerList = megaBannerList;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaBannerList;
    }
}

- (MEGABanner *)bannerAtIndex:(NSInteger)index {
    return self.megaBannerList ?
        [[MEGABanner alloc] initWithMegaBanner:self.megaBannerList->get((int)index)->copy() cMemoryOwn:YES] : nil;
}

- (NSInteger)size {
    return self.megaBannerList ? self.megaBannerList->size() : 0;
}

@end

