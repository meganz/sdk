/**
 * @file MEGAHandleList.mm
 * @brief List of MegaHandle
 *
 * (c) 2013-2017 by Mega Limited, Auckland, New Zealand
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

#import "MEGAHandleList.h"
#import "megaapi.h"

using namespace mega;

@interface MEGAHandleList ()

@property MegaHandleList *megaHandleList;
@property BOOL cMemoryOwn;

@end

@implementation MEGAHandleList

- (instancetype)init {
    self = [super init];
    
    if (self != nil) {
        _megaHandleList = MegaHandleList::createInstance();
    }
    
    return self;
}

- (instancetype)initWithMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _megaHandleList = MegaHandleList::createInstance();
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (instancetype)initWithMegaHandleList:(MegaHandleList *)megaHandleList cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self) {
        _megaHandleList = megaHandleList;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaHandleList;
    }
}

- (MegaHandleList *)getCPtr {
    return self.megaHandleList;
}

- (NSString *)description {
    return [NSString stringWithFormat:@"<%@: size=%ld>",
            [self class], (long)self.size];
}

- (NSUInteger)size {
    return self.megaHandleList ? self.megaHandleList->size() : 0;
}

- (void)addMegaHandle:(uint64_t)handle {
    if (!self.megaHandleList) return;
    self.megaHandleList->addMegaHandle(handle);
}

- (uint64_t)megaHandleAtIndex:(NSUInteger)index {
    return self.megaHandleList ? self.megaHandleList->get((unsigned int)index) : INVALID_HANDLE;
}

@end
