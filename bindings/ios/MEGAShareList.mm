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

-(MEGAShare *)shareAtIndex:(NSInteger)index {
    return self.shareList ? [[MEGAShare alloc] initWithMegaShare:self.shareList->get((int)index)->copy() cMemoryOwn:YES] : nil;
}

-(NSNumber *)size {
    return self.shareList ? [[NSNumber alloc] initWithInt:self.shareList->size()] : nil;
}

@end
