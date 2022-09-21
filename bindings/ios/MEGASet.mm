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

- (instancetype)clone {
    return self.set ? [[MEGASet alloc] initWithMegaSet:self.set->copy() cMemoryOwn:YES] : nil;
}

- (uint64_t)handle {
    return self.set ? self.set->id(): ::mega::INVALID_HANDLE;
}

- (uint64_t)userId {
    return self.set ? self.set->user() : 0;
}

- (NSDate *)timestamp {
    return self.set ? [[NSDate alloc] initWithTimeIntervalSince1970:self.set->ts()] : nil;
}

- (NSString *)name {
    if (!self.set) return nil;
    
    return self.set->name() ? [[NSString alloc] initWithUTF8String:self.set->name()] : nil;
}

- (uint64_t)cover {
    return self.set ? self.set->cover(): ::mega::INVALID_HANDLE;
}

@end
