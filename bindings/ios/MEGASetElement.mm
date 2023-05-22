/**
 * @file MEGASetElement.mm
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

#import "MEGASetElement.h"
#import "megaapi.h"

using namespace mega;

@interface MEGASetElement()

@property MegaSetElement *setElement;
@property BOOL cMemoryOwn;

@end

@implementation MEGASetElement

- (instancetype)initWithMegaSetElement:(MegaSetElement *)setElement cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _setElement = setElement;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _setElement;
    }
}

- (uint64_t)handle {
    return self.setElement ? self.setElement->id() : ::mega::INVALID_HANDLE;
}

- (uint64_t)ownerId {
    return self.setElement ? self.setElement->setId() : ::mega::INVALID_HANDLE;
}

- (uint64_t)nodeId {
    return self.setElement ? self.setElement->node() : ::mega::INVALID_HANDLE;
}

- (uint64_t)order {
    return self.setElement ? self.setElement->order() : 0;
}

- (NSDate *)timestamp {
    return self.setElement ? [[NSDate alloc] initWithTimeIntervalSince1970:self.setElement->ts()] : nil;
}

- (NSString *)name {
    if (!self.setElement) return nil;
    
    return self.setElement->name() ? [[NSString alloc] initWithUTF8String:self.setElement->name()] : nil;
}

- (BOOL)hasChangedType:(MEGASetElementChangeType)changeType {
    return self.setElement ? self.setElement->hasChanged((int)changeType) : NO;
}

- (MEGASetElementChangeType)changes {
    return (MEGASetElementChangeType) (self.setElement ? self.setElement->getChanges() : 0);
}

@end
