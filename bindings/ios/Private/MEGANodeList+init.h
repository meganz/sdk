#import "MEGANodeList.h"
#import "megaapi.h"

@interface MEGANodeList (init)

- (instancetype)initWithNodeList:(mega::MegaNodeList *)nodelist cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaNodeList *)getCPtr;

@end
