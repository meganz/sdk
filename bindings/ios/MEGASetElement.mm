#import "MegaSetElement.h"
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

- (instancetype)clone {
    return self.setElement ? [[MEGASetElement alloc] initWithMegaSetElement:self.setElement->copy() cMemoryOwn:YES] : nil;
}

- (uint64_t)handle {
    return self.setElement ? self.setElement->id() : ::mega::INVALID_HANDLE;
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

@end
