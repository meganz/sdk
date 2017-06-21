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

- (instancetype)clone {
    return self.megaHandleList ? [[MEGAHandleList alloc] initWithMegaHandleList:self.megaHandleList cMemoryOwn:YES] : nil;
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
    return self.megaHandleList ? self.megaHandleList->get((unsigned int)index) : 1;
}

@end
