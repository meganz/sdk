/**
 * @file MEGAStringIntegerMap.mm
 * @brief Map of integer values with string keys (map<string, int64_t>)
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

#import "MEGAStringIntegerMap.h"
#import "MEGAStringIntegerMap+init.h"
#import "MEGAStringList.h"
#import "MEGAStringList+init.h"

using namespace mega;

@interface MEGAStringIntegerMap ()

@property MegaStringIntegerMap *megaStringIntegerMap;
@property BOOL cMemoryOwn;

@end

@implementation MEGAStringIntegerMap

- (instancetype)initWithMegaStringIntegerMap:(mega::MegaStringIntegerMap *)megaStringIntegerMap cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];

    if (self) {
        _megaStringIntegerMap = megaStringIntegerMap;
        _cMemoryOwn = cMemoryOwn;
    }

    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaStringIntegerMap;
    }
}

- (mega::MegaStringIntegerMap *)getCPtr {
    return self.megaStringIntegerMap;
}

- (NSInteger)size {
    return self.megaStringIntegerMap ? (NSInteger)self.megaStringIntegerMap->size() : 0;
}

- (MEGAStringList *)keys {
    if (!self.megaStringIntegerMap) {
        return nil;
    }
    MegaStringList *keys = self.megaStringIntegerMap->getKeys();
    if (!keys) {
        return nil;
    }
    return [[MEGAStringList alloc] initWithMegaStringList:keys cMemoryOwn:YES];
}

- (int64_t)integerValueForKey:(NSString *)key {
    if (!self.megaStringIntegerMap) {
        return -1;
    }
    const char *cKey = [key UTF8String];
    MegaIntegerList *values = self.megaStringIntegerMap->get(cKey);
    if (!values || values->size() == 0) {
        return -1;
    }
    int64_t value = values->get(0);
    delete values;
    return value;
}

- (NSDictionary<NSString *, NSNumber *> *)toDictionary {
    NSMutableDictionary<NSString *, NSNumber *> *dictionary = [NSMutableDictionary dictionary];
    MEGAStringList *keyList = [self keys];
    if (keyList) {
        for (NSUInteger i = 0; i < [keyList size]; i++) {
            NSString *key = [keyList stringAtIndex:i];
            int64_t value = [self integerValueForKey:key];
            dictionary[key] = @(value);
        }
    }
    return [dictionary copy];
}

@end
