/**
 * @file MEGAContactRequestList.mm
 * @brief List of MEGAContactRequest objects
 *
 * (c) 2013-2015 by Mega Limited, Auckland, New Zealand
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

#import "MEGAContactRequestList.h"
#import "MEGAContactRequest+init.h"

using namespace mega;

@interface MEGAContactRequestList ()

@property MegaContactRequestList *megaContactRequestList;
@property BOOL cMemoryOwn;

@end

@implementation MEGAContactRequestList

- (instancetype)initWithMegaContactRequestList:(MegaContactRequestList *)megaContactRequestList cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _megaContactRequestList = megaContactRequestList;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaContactRequestList;
    }
}

- (MegaContactRequestList *)getCPtr {
    return self.megaContactRequestList;
}

- (NSInteger)size {
    return self.megaContactRequestList ? self.megaContactRequestList->size() : -1;
}

- (MEGAContactRequest *)contactRequestAtIndex:(NSInteger)index {
    return self.megaContactRequestList ? [[MEGAContactRequest alloc] initWithMegaContactRequest:self.megaContactRequestList->get((int)index)->copy() cMemoryOwn:YES] : nil;
}

@end
