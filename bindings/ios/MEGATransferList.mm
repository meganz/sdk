#import "MEGATransferList.h"
#import "MEGATransfer+init.h"

using namespace mega;

@interface MEGATransferList ()

@property MegaTransferList *transferList;
@property BOOL cMemoryOwn;

@end

@implementation MEGATransferList

- (instancetype)initWithTransferList:(MegaTransferList *)transferList cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _transferList = transferList;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _transferList;
    }
}

- (MegaTransferList *)getCPtr {
    return self.transferList;
}

- (MEGATransfer *)transferAtIndex:(NSInteger)index {
    return self.transferList ? [[MEGATransfer alloc] initWithMegaTransfer:self.transferList->get((int)index)->copy() cMemoryOwn:YES] : nil;
}

- (NSNumber *)size {
    return self.transferList ? [[NSNumber alloc] initWithInt:self.transferList->size()] : nil;
}

@end
