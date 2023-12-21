/**
 * @file MEGASet.mm
 * @brief Represents a node (file/folder) in the MEGA account
 *
 * (c) 2022- by Mega Limited, Auckland, New Zealand
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

#import "MEGASet.h"
#import "megaapi.h"

using namespace mega;

@interface MEGASet()

@property MegaSet *set;
@property BOOL cMemoryOwn;

@end

@implementation MEGASet

- (instancetype)initWithMegaSet:(MegaSet *)set cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _set = set;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _set;
    }
}

- (uint64_t)handle {
    return self.set ? self.set->id(): ::mega::INVALID_HANDLE;
}

- (uint64_t)userId {
    return self.set ? self.set->user() : 0;
}

- (uint64_t)publicId {
    return self.set ? self.set->publicId() : 0;
}

- (nullable NSDate *)timestamp {
    return self.set ? [[NSDate alloc] initWithTimeIntervalSince1970:self.set->ts()] : nil;
}

- (nullable NSDate *)timestampCreated {
    return self.set ? [[NSDate alloc] initWithTimeIntervalSince1970:self.set->cts()] : nil;
}

- (MEGASetType)type {
    return (MEGASetType) (self.set ? self.set->type() : MEGASetTypeInvalid);
}

- (nullable NSString *)name {
    if (!self.set) return nil;
    
    return self.set->name() ? [[NSString alloc] initWithUTF8String:self.set->name()] : nil;
}

- (uint64_t)cover {
    return self.set ? self.set->cover(): ::mega::INVALID_HANDLE;
}

- (BOOL)hasChangedType:(MEGASetChangeType)changeType {
    return self.set ? self.set->hasChanged(int(changeType)) : NO;
}

- (MEGASetChangeType)changes {
    return (MEGASetChangeType) (self.set ? self.set->getChanges() : 0);
}

- (BOOL)isExported {
    return self.set ? self.set->isExported() : NO;
}

@end
