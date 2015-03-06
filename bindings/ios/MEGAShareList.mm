/**
 * @file MEGAShareList.mm
 * @brief List of MEGAShare objects
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
#import "MEGAShareList.h"
#import "MEGAShare+init.h"

using namespace mega;

@interface MEGAShareList ()

@property MegaShareList *shareList;
@property BOOL cMemoryOwn;

@end

@implementation MEGAShareList

- (instancetype)initWithShareList:(MegaShareList *)shareList cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _shareList = shareList;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _shareList;
    }
}

- (MegaShareList *)getCPtr {
    return self.shareList;
}

- (MEGAShare *)shareAtIndex:(NSInteger)index {
    return self.shareList ? [[MEGAShare alloc] initWithMegaShare:self.shareList->get((int)index)->copy() cMemoryOwn:YES] : nil;
}

- (NSNumber *)size {
    return self.shareList ? [[NSNumber alloc] initWithInt:self.shareList->size()] : nil;
}

@end
