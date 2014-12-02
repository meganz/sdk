#import "MEGANode.h"
#import "megaapi.h"

@interface MEGANode (init)

- (instancetype)initWithMegaNode:(mega::MegaNode *)megaNode cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaNode *)getCPtr;

@end
