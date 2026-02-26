/**
 * @file MEGAStringList.mm
 * @brief List of strings
 *
 * (c) 2017- by Mega Limited, Auckland, New Zealand
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

#import "MEGAStringList.h"
#import "MEGAStringList+init.h"

using namespace mega;

@interface MEGAStringList ()

@property MegaStringList *megaStringList;
@property BOOL cMemoryOwn;

@end

@implementation MEGAStringList

- (instancetype)initWithMegaStringList:(mega::MegaStringList *)megaStringList cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self) {
        _megaStringList = megaStringList;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaStringList;
    }
}

- (mega::MegaStringList *)getCPtr {
    return self.megaStringList;
}

- (NSInteger)size {
    return self.megaStringList ? self.megaStringList->size() : 0;
}

- (nullable NSString *)stringAtIndex:(NSInteger)index {
    return self.megaStringList ? [[NSString alloc] initWithUTF8String:self.megaStringList->get((int)index)] : nil;
}

- (nullable NSArray<NSString *>*)toStringArray {
    if (!self.megaStringList) {
        return nil;
    }

    int size = self.megaStringList->size();

    NSMutableArray *array = [NSMutableArray arrayWithCapacity:size];

    for (NSUInteger i = 0; i < size; i++) {
        [array addObject:[self stringAtIndex:i]];
    }

    return [array copy];
}

@end
