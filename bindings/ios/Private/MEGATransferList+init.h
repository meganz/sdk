#import "MEGATransferList.h"
#import "megaapi.h"

@interface MEGATransferList (init)

- (instancetype)initWithTransferList:(mega::MegaTransferList *)transferList cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaTransferList *)getCPtr;

@end
