#import "MEGATransfer.h"
#import "megaapi.h"

@interface MEGATransfer (init)

- (instancetype)initWithMegaTransfer:(mega::MegaTransfer *)megaTransfer cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaTransfer *)getCPtr;

@end
