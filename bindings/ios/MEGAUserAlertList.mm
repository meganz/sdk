/**
 * @file MEGAUserAlertList.mm
 * @brief List of MEGAUserAlert objects
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

#import "MEGAUserAlertList.h"
#import "MEGAUserAlertList+init.h"
#import "MEGAUserAlert+init.h"

using namespace mega;

@interface MEGAUserAlertList ()

@property MegaUserAlertList *megaUserAlertList;
@property BOOL cMemoryOwn;

@end

@implementation MEGAUserAlertList

- (instancetype)initWithMegaUserAlertList:(MegaUserAlertList *)megaUserAlertList cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self) {
        _megaUserAlertList = megaUserAlertList;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaUserAlertList;
    }
}

- (nullable MEGAUserAlert *)usertAlertAtIndex:(NSInteger)index {
    return self.megaUserAlertList ? [[MEGAUserAlert alloc] initWithMegaUserAlert:self.megaUserAlertList->get((int)index)->copy() cMemoryOwn:YES] : nil;
}

- (NSInteger)size {
    return self.megaUserAlertList ? self.megaUserAlertList->size() : 0;
}

@end
