/**
 * @file MEGATransferList.mm
 * @brief List of MEGATransfer objects
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
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
#import "MEGATransferList.h"
#import "MEGATransfer+init.h"

using namespace mega;

@interface MEGATransferList ()

@property MegaTransferList *transferList;
@property BOOL cMemoryOwn;

@end

@implementation MEGATransferList

- (instancetype)initWithTransferList:(MegaTransferList *)transferList cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _transferList = transferList;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _transferList;
    }
}

- (MegaTransferList *)getCPtr {
    return self.transferList;
}

- (nullable MEGATransfer *)transferAtIndex:(NSInteger)index {
    return self.transferList ? [[MEGATransfer alloc] initWithMegaTransfer:self.transferList->get((int)index)->copy() cMemoryOwn:YES] : nil;
}

- (NSInteger)size {
    return self.transferList ? self.transferList->size() : 0;
}

@end
